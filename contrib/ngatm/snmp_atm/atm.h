/*
 * Copyright (c) 2004
 *	Deutsches Zentrum fuer Luft- und Raumfahrt.
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
 * $Begemot: libunimsg/snmp_atm/atm.h,v 1.3 2005/05/23 11:46:46 brandt_h Exp $
 */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/if_mib.h>

#include <bsnmp/snmpmod.h>
#include <bsnmp/snmp_mibII.h>
#include <bsnmp/snmp_atm.h>

/*
 * Event registrations
 */
struct atmif_reg {
	TAILQ_ENTRY(atmif_reg) link;
	void		*data;
	atmif_event_f	func;
	const struct lmodule *mod;
	struct atmif_priv *aif;		/* back pointer */
};
TAILQ_HEAD(atmif_reg_list, atmif_reg);

/*
 * Interface data
 */
struct atmif_priv {
	struct atmif	pub;	/* public part, must be first */
	TAILQ_ENTRY(atmif_priv) link;
	u_int		index;		/* if_index */
	void		*ifpreg;
	struct atmif_sys *sys;
	struct atmif_reg_list notify;
};
TAILQ_HEAD(atmif_list, atmif_priv);

/* list of all (known) ATM interfaces */
extern struct atmif_list atmif_list;

extern struct lmodule *module;

/* Check the carrier state of the interface */
void atmif_check_carrier(struct atmif_priv *);

/* Send notification to all listeners. */
void atmif_send_notification(struct atmif_priv *, enum atmif_notify, uintptr_t);

/* Get the interface point for a table access */
int atmif_get_aif(struct snmp_value *, u_int, enum snmp_op,
	struct atmif_priv **);

/* Destroy system dependend stuff. */
void atmif_sys_destroy(struct atmif_priv *);

/* Attach to an ATM interface */
int atmif_sys_attach_if(struct atmif_priv *);

/* Get vendor string */
int atm_sys_get_hw_vendor(struct atmif_priv *, struct snmp_value *);

/* Get device string */
int atm_sys_get_hw_device(struct atmif_priv *, struct snmp_value *);

/* Extract the ATM MIB from the interface's private MIB */
void atmif_sys_fill_mib(struct atmif_priv *);
