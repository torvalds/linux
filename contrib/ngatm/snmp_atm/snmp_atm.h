/*
 * Copyright (c) 2001-2002
 *	Fraunhofer Institute for Open Communication Systems (FhG Fokus).
 *	All rights reserved.
 * Copyright (c) 2003-2004
 *	Hartmut Brandt
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
 * $Begemot: libunimsg/snmp_atm/snmp_atm.h,v 1.2 2004/08/06 17:30:40 brandt Exp $
 */
#ifndef _BSNMP_SNMP_ATM_H
#define _BSNMP_SNMP_ATM_H

enum atmif_notify {
	ATMIF_NOTIFY_DESTROY,	/* interface has been destroyed */
	ATMIF_NOTIFY_CARRIER,	/* carriere change */
	ATMIF_NOTIFY_VCC	/* VCC change */
};

enum atmif_carrier_state {
	ATMIF_CARRIER_ON	= 1,
	ATMIF_CARRIER_OFF	= 2,
	ATMIF_CARRIER_UNKNOWN	= 3,
	ATMIF_CARRIER_NONE	= 4
};

enum atmif_suni_mode {
	ATMIF_SUNI_MODE_SONET	= 1,
	ATMIF_SUNI_MODE_SDH	= 2,
	ATMIF_SUNI_MODE_UNKNOWN	= 3
};

/* forward declaration */
struct atmif;
typedef void (*atmif_event_f)(struct atmif *, enum atmif_notify, uintptr_t,
    void *);

struct atmif_mib {
	u_int	version;	/* currently 0 */

	u_int	device;		/* type of hardware (system specific) */
	u_int	serial;		/* card serial number (device specific) */
	u_int	hw_version;	/* card version (device specific) */
	u_int	sw_version;	/* firmware version (device specific) */
	u_int	media;		/* physical media (see MIB) */

	u_char	esi[6];		/* end system identifier (MAC) */
	u_int	pcr;		/* supported peak cell rate */
	u_int	vpi_bits;	/* number of used bits in VPI field */
	u_int	vci_bits;	/* number of used bits in VCI field */
	u_int	max_vpcs;	/* maximum number of VPCs */
	u_int	max_vccs;	/* maximum number of VCCs */
};

struct atmif {
	struct mibif	*ifp;		/* common interface data */
	struct atmif_mib *mib;		/* ATM MIB */
	enum atmif_carrier_state carrier;
	enum atmif_suni_mode mode;	/* SUNI mode SDH or SONET */
};

/* find an ATM interface by name */
struct atmif *atm_find_if_name(const char *);

/* get the interface from the interface index */
struct atmif *atm_find_if(u_int);

/* register for notifications */
void *atm_notify_aif(struct atmif *, const struct lmodule *mod,
    atmif_event_f, void *);
void atm_unnotify_aif(void *);

/* return the If for a system-specific node number */
struct atmif *atm_node2if(u_int);

/* return the node id for the if */
u_int atm_if2node(struct atmif *);

#endif
