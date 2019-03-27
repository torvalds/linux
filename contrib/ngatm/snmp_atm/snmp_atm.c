/*
 * Copyright (c) 2001-2002
 *	Fraunhofer Institute for Open Communication Systems (FhG Fokus).
 *	All rights reserved.
 * Copyright (c) 2003-2004
 *	Hartmut Brandt.
 *	All rights reserved.
 *
 * Author: Hartmut Brandt <harti@freebsd.org>
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Begemot: libunimsg/snmp_atm/snmp_atm.c,v 1.3 2005/05/23 11:46:46 brandt_h Exp $
 *
 * SNMP module for ATM hardware interfaces.
 */

#include "atm.h"
#include "atm_tree.h"
#include "atm_oid.h"

#include <sys/ioctl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <net/if_types.h>
#include <net/if_media.h>
#include <net/if_atm.h>

struct lmodule *module;

/* list of all (known) ATM interfaces */
struct atmif_list atmif_list = TAILQ_HEAD_INITIALIZER(atmif_list);

/* whether we are started or not */
static int started;

/* last time table was changed */
static uint64_t last_change;

/* for the registration */
static const struct asn_oid oid_begemotAtm = OIDX_begemotAtm;

/* the registration */
static u_int reg_atm;

/*
 * Find an ATM interface by name
 */
struct atmif *
atm_find_if_name(const char *name)
{
	struct atmif_priv *aif;

	TAILQ_FOREACH(aif, &atmif_list, link)
		if (strcmp(aif->pub.ifp->name, name) == 0)
			return (&aif->pub);
	return (NULL);
}

/*
 * get the interface from the interface index
 */
struct atmif *
atm_find_if(u_int ifindex)
{
	struct atmif_priv *aif;

	TAILQ_FOREACH(aif, &atmif_list, link)
		if (aif->index == ifindex)
			return (&aif->pub);
	return (NULL);
}

/*
 * Send notification to all listeners.
 */
void
atmif_send_notification(struct atmif_priv *aif, enum atmif_notify code,
    uintptr_t arg)
{
	struct atmif_reg *r0, *r1;

	r0 = TAILQ_FIRST(&aif->notify);
	while (r0 != NULL) {
		r1 = TAILQ_NEXT(r0, link);
		r0->func(&aif->pub, code, arg, r0->data);
		r0 = r1;
	}
}

/*
 * Destroy an interface
 */
static void
atmif_destroy(struct atmif_priv *aif)
{
	struct atmif_reg *r0;

	atmif_send_notification(aif, ATMIF_NOTIFY_DESTROY,
	    (uintptr_t)0);

	atmif_sys_destroy(aif);

	if (aif->ifpreg != NULL)
		mibif_unnotify(aif->ifpreg);

	while ((r0 = TAILQ_FIRST(&aif->notify)) != NULL) {
		TAILQ_REMOVE(&aif->notify, r0, link);
		free(r0);
	}

	TAILQ_REMOVE(&atmif_list, aif, link);
	free(aif);

	last_change = this_tick;
}

/*
 * Function gets called from the MIB-II module for events on that interface
 */
static void
atmif_notify(struct mibif *ifp __unused, enum mibif_notify event, void *data)
{
	struct atmif_priv *aif = data;

	switch (event) {

	  case MIBIF_NOTIFY_DESTROY:
		atmif_destroy(aif);
		break;
	}
}

/*
 * Check the carrier state of the interface
 */
void
atmif_check_carrier(struct atmif_priv *aif)
{
	struct ifmediareq ifmr;
	enum atmif_carrier_state ost = aif->pub.carrier;

	memset(&ifmr, 0, sizeof(ifmr));
	strcpy(ifmr.ifm_name, aif->pub.ifp->name);

	if (ioctl(mib_netsock, SIOCGIFMEDIA, &ifmr) == -1) {
		aif->pub.carrier = ATMIF_CARRIER_UNKNOWN;
		return;
	}
	if (!(ifmr.ifm_status & IFM_AVALID)) {
		aif->pub.carrier = ATMIF_CARRIER_UNKNOWN;
		return;
	}
	if (ifmr.ifm_status & IFM_ACTIVE)
		aif->pub.carrier = ATMIF_CARRIER_ON;
	else
		aif->pub.carrier = ATMIF_CARRIER_OFF;

	if (ost != aif->pub.carrier)
		atmif_send_notification(aif, ATMIF_NOTIFY_CARRIER,
		    (uintptr_t)ost);
}

/*
 * Retrieve the SUNI mode
 */
static int
atmif_get_mode(struct atmif_priv *aif)
{
	struct ifmediareq ifmr;

	memset(&ifmr, 0, sizeof(ifmr));
	strcpy(ifmr.ifm_name, aif->pub.ifp->name);

	if (ioctl(mib_netsock, SIOCGIFMEDIA, &ifmr) < 0) {
		syslog(LOG_ERR, "SIOCGIFMEDIA: %m");
		aif->pub.mode = ATMIF_SUNI_MODE_UNKNOWN;
		return (SNMP_ERR_GENERR);
	}
	if (ifmr.ifm_current & IFM_ATM_SDH)
		aif->pub.mode = ATMIF_SUNI_MODE_SDH;
	else
		aif->pub.mode = ATMIF_SUNI_MODE_SONET;

	return (SNMP_ERR_NOERROR);
}

/*
 * Change the SUNI mod
 */
static int
atmif_set_mode(struct atmif_priv *aif, int newmode)
{
	struct ifmediareq ifmr;
	struct ifreq ifr;

	memset(&ifmr, 0, sizeof(ifmr));
	strcpy(ifmr.ifm_name, aif->pub.ifp->name);

	/* get current mode */
	if (ioctl(mib_netsock, SIOCGIFMEDIA, &ifmr) < 0) {
		syslog(LOG_ERR, "SIOCGIFMEDIA: %m");
		return (SNMP_ERR_GENERR);
	}

	memset(&ifr, 0, sizeof(ifr));
	strcpy(ifr.ifr_name, aif->pub.ifp->name);

	ifr.ifr_media = ifmr.ifm_current;
	if (newmode == ATMIF_SUNI_MODE_SDH)
		ifr.ifr_media |= IFM_ATM_SDH;
	else
		ifr.ifr_media &= ~IFM_ATM_SDH;

	if (ioctl(mib_netsock, SIOCSIFMEDIA, &ifr) < 0) {
		syslog(LOG_ERR, "SIOCSIFMEDIA: %m");
		return (SNMP_ERR_GENERR);
	}

	aif->pub.mode = newmode;
	return (SNMP_ERR_NOERROR);
}

/*
 * Attach to an ATM interface
 */
static void
attach_if(struct mibif *ifp)
{
	struct atmif_priv *aif;

	/* we should not know it */
	TAILQ_FOREACH(aif, &atmif_list, link)
		if (aif->pub.ifp == ifp) {
			syslog(LOG_CRIT, "new ATM if already known '%s'",
			    ifp->name);
			return;
		}

	/*
	 * tap it
	 */
	if ((aif = malloc(sizeof(*aif))) == NULL) {
		syslog(LOG_ERR, "new atmif: %m");
		return;
	}
	memset(aif, 0, sizeof(*aif));

	aif->pub.ifp = ifp;
	aif->index = ifp->index;
	TAILQ_INIT(&aif->notify);

	if (atmif_sys_attach_if(aif)) {
		free(aif);
		return;
	}

	aif->ifpreg = mibif_notify(ifp, module, atmif_notify, aif);

	aif->pub.carrier = ATMIF_CARRIER_UNKNOWN;
	atmif_check_carrier(aif);
	(void)atmif_get_mode(aif);

	INSERT_OBJECT_INT(aif, &atmif_list);

	last_change = this_tick;

	return;
}

/*
 * Function gets called when a new interface is created. If this is an
 * ATM interface, hook in. Claim the interface in any case even when
 * the creation of our data structures fails.
 */
static int
new_if(struct mibif *ifp)
{
	if (!started || ifp->mib.ifmd_data.ifi_type != IFT_ATM ||
	    ifp->xnotify != NULL)
		return (0);

	attach_if(ifp);
	return (1);
}

/*
 * Start the module
 */
static void
atm_start(void)
{
	struct mibif *ifp;

	reg_atm = or_register(&oid_begemotAtm, 
	    "The Begemot MIB for ATM interfaces.", module);

	started = 1;
	for (ifp = mib_first_if(); ifp != NULL; ifp = mib_next_if(ifp))
		if (ifp->mib.ifmd_data.ifi_type == IFT_ATM &&
		    ifp->xnotify == NULL)
			attach_if(ifp);
}

/*
 * Called when modules is loaded
 */
static int
atm_init(struct lmodule *mod, int argc __unused, char *argv[] __unused)
{
	module = mod;

	/* register to get creation messages for ATM interfaces */
	if (mib_register_newif(new_if, module)) {
		syslog(LOG_ERR, "cannot register newif function: %m");
		return (-1);
	}

	return (0);
}

/*
 * Called when module gets unloaded - free all resources
 */
static int
atm_fini(void)
{
	struct atmif_priv *aif;

	while ((aif = TAILQ_FIRST(&atmif_list)) != NULL)
		atmif_destroy(aif);

	mib_unregister_newif(module);
	or_unregister(reg_atm);

	return (0);
}

/*
 * Other module unloaded/loaded
 */
static void
atm_loading(const struct lmodule *mod, int loading)
{
	struct atmif_priv *aif;
	struct atmif_reg *r0, *r1;

	if (!loading) {
		/* remove notifications for this module */
		TAILQ_FOREACH(aif, &atmif_list, link)
			TAILQ_FOREACH_SAFE(r0, &aif->notify, link, r1) {
				if (r0->mod == mod) {
					TAILQ_REMOVE(&aif->notify, r0, link);
					free(r0);
				}
			}
	}
}

const struct snmp_module config = {
	.comment = "This module implements a private MIB for ATM interfaces.",
	.init =		atm_init,
	.fini =		atm_fini,
	.start =	atm_start,
	.tree =		atm_ctree,
	.tree_size =	atm_CTREE_SIZE,
	.loading =	atm_loading
};

/*
 * Get the interface point for a table access
 */
int
atmif_get_aif(struct snmp_value *value, u_int sub, enum snmp_op op,
    struct atmif_priv **aifp)
{
	switch (op) {

	  case SNMP_OP_GETNEXT:
		if ((*aifp = NEXT_OBJECT_INT(&atmif_list,
		    &value->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		value->var.len = sub + 1;
		value->var.subs[sub] = (*aifp)->index;
		break;

	  case SNMP_OP_GET:
		if ((*aifp = FIND_OBJECT_INT(&atmif_list,
		    &value->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		break;

	  case SNMP_OP_SET:
		if ((*aifp = FIND_OBJECT_INT(&atmif_list,
		    &value->var, sub)) == NULL)
			return (SNMP_ERR_NO_CREATION);
		break;

	  case SNMP_OP_ROLLBACK:
	  case SNMP_OP_COMMIT:
		if ((*aifp = FIND_OBJECT_INT(&atmif_list,
		    &value->var, sub)) == NULL)
			abort();
		return (SNMP_ERR_NOERROR);
	}

	if ((*aifp)->pub.mib->pcr == 0) {
		mib_fetch_ifmib((*aifp)->pub.ifp);
		atmif_sys_fill_mib(*aifp);
		atmif_check_carrier(*aifp);
	}

	return (SNMP_ERR_NOERROR);
}

/* 
 * Table of all ATM interfaces
 */
int
op_atmif(struct snmp_context *ctx __unused, struct snmp_value *value,
    u_int sub, u_int vindex __unused, enum snmp_op op)
{
	struct atmif_priv *aif;
	int err;

	if ((err = atmif_get_aif(value, sub, op, &aif)) != SNMP_ERR_NOERROR)
		return (err);

	if (op == SNMP_OP_SET) {
		switch (value->var.subs[sub - 1]) {

		  default:
			return (SNMP_ERR_NOT_WRITEABLE);

		  case LEAF_begemotAtmIfMode:
			if ((err = atmif_get_mode(aif)) != SNMP_ERR_NOERROR)
				return (err);
			if (aif->pub.mode == ATMIF_SUNI_MODE_UNKNOWN)
				return (SNMP_ERR_INCONS_VALUE);
			if (value->v.integer != ATMIF_SUNI_MODE_SONET &&
			    value->v.integer != ATMIF_SUNI_MODE_SDH)
				return (SNMP_ERR_WRONG_VALUE);
			if ((u_int)value->v.integer == aif->pub.mode)
				return (SNMP_ERR_NOERROR);
			return (atmif_set_mode(aif, value->v.integer));
		}
		abort();
	}

	switch (value->var.subs[sub - 1]) {

	  case LEAF_begemotAtmIfName:
		return (string_get(value, aif->pub.ifp->name, -1));

	  case LEAF_begemotAtmIfPcr:
		value->v.uint32 = aif->pub.mib->pcr;
		return (SNMP_ERR_NOERROR);

	  case LEAF_begemotAtmIfMedia:
		value->v.integer = aif->pub.mib->media;
		return (SNMP_ERR_NOERROR);

	  case LEAF_begemotAtmIfVpiBits:
		value->v.uint32 = aif->pub.mib->vpi_bits;
		return (SNMP_ERR_NOERROR);

	  case LEAF_begemotAtmIfVciBits:
		value->v.uint32 = aif->pub.mib->vci_bits;
		return (SNMP_ERR_NOERROR);

	  case LEAF_begemotAtmIfMaxVpcs:
		value->v.uint32 = aif->pub.mib->max_vpcs;
		return (SNMP_ERR_NOERROR);

	  case LEAF_begemotAtmIfMaxVccs:
		value->v.uint32 = aif->pub.mib->max_vccs;
		return (SNMP_ERR_NOERROR);

	  case LEAF_begemotAtmIfEsi:
		return (string_get(value, aif->pub.mib->esi, 6));

	  case LEAF_begemotAtmIfCarrierStatus:
		value->v.integer = aif->pub.carrier;
		return (SNMP_ERR_NOERROR);

	  case LEAF_begemotAtmIfMode:
		if ((err = atmif_get_mode(aif)) != SNMP_ERR_NOERROR)
			return (err);
		value->v.integer = aif->pub.mode;
		return (SNMP_ERR_NOERROR);
	}
	abort();
}

/* 
 * Hardware table
 */
int
op_atmhw(struct snmp_context *ctx __unused, struct snmp_value *value,
    u_int sub, u_int vindex __unused, enum snmp_op op)
{
	struct atmif_priv *aif;
	int err;

	if ((err = atmif_get_aif(value, sub, op, &aif)) != SNMP_ERR_NOERROR)
		return (err);
	if (op == SNMP_OP_SET)
		return (SNMP_ERR_NOT_WRITEABLE);

	switch (value->var.subs[sub - 1]) {

	  case LEAF_begemotAtmHWVendor:
		return (atm_sys_get_hw_vendor(aif, value));

	  case LEAF_begemotAtmHWDevice:
		return (atm_sys_get_hw_device(aif, value));

	  case LEAF_begemotAtmHWSerial:
		value->v.uint32 = aif->pub.mib->serial;
		return (SNMP_ERR_NOERROR);

	  case LEAF_begemotAtmHWVersion:
		value->v.uint32 = aif->pub.mib->hw_version;
		return (SNMP_ERR_NOERROR);

	  case LEAF_begemotAtmHWSoftVersion:
		value->v.uint32 = aif->pub.mib->sw_version;
		return (SNMP_ERR_NOERROR);

	}
	abort();
}

/*
 * Scalars
 */
int
op_atm(struct snmp_context *ctx __unused, struct snmp_value *value,
    u_int sub, u_int vindex __unused, enum snmp_op op)
{
	switch (op) {

	  case SNMP_OP_GETNEXT:
		abort();

	  case SNMP_OP_GET:
		switch (value->var.subs[sub - 1]) {

		  case LEAF_begemotAtmIfTableLastChange:
			value->v.uint32 =
			    (last_change == 0 ? 0 : last_change - start_tick);
			return (SNMP_ERR_NOERROR);
		}
		abort();

	  case SNMP_OP_SET:
		return (SNMP_ERR_NOT_WRITEABLE);

	  case SNMP_OP_ROLLBACK:
	  case SNMP_OP_COMMIT:
		abort();
	}
	abort();
}

/*
 * Register for interface notifications
 */
void *
atm_notify_aif(struct atmif *pub, const struct lmodule *mod,
    atmif_event_f func, void *arg)
{
	struct atmif_priv *aif = (struct atmif_priv *)pub;
	struct atmif_reg *r0;

	if ((r0 = malloc(sizeof(*r0))) == NULL) {
		syslog(LOG_CRIT, "out of memory");
		return (NULL);
	}
	r0->func = func;
	r0->mod = mod;
	r0->data = arg;
	r0->aif = aif;

	TAILQ_INSERT_TAIL(&aif->notify, r0, link);

	return (r0);
}

/*
 * Unregister it
 */
void
atm_unnotify_aif(void *arg)
{
	struct atmif_reg *r0 = arg;

	TAILQ_REMOVE(&r0->aif->notify, r0, link);
	free(r0);
}
