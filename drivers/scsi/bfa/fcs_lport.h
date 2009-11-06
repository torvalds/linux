/*
 * Copyright (c) 2005-2009 Brocade Communications Systems, Inc.
 * All rights reserved
 * www.brocade.com
 *
 * Linux driver for Brocade Fibre Channel Host Bus Adapter.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License (GPL) Version 2 as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

/**
 *  fcs_lport.h FCS logical port interfaces
 */

#ifndef __FCS_LPORT_H__
#define __FCS_LPORT_H__

#define __VPORT_H__
#include <defs/bfa_defs_port.h>
#include <bfa_svc.h>
#include <fcs/bfa_fcs_lport.h>
#include <fcs/bfa_fcs_rport.h>
#include <fcs/bfa_fcs_vport.h>
#include <fcs_fabric.h>
#include <fcs_ms.h>
#include <cs/bfa_q.h>
#include <fcbuild.h>

/*
 * PID used in P2P/N2N ( In Big Endian)
 */
#define N2N_LOCAL_PID	    0x010000
#define N2N_REMOTE_PID		0x020000

/*
 * Misc Timeouts
 */
/*
 * To be used when spawning a timer before retrying a failed command. Milli
 * Secs.
 */
#define	BFA_FCS_RETRY_TIMEOUT 2000

/*
 * Check for Port/Vport Mode/Role
 */
#define	BFA_FCS_VPORT_IS_INITIATOR_MODE(port) \
		(port->port_cfg.roles & BFA_PORT_ROLE_FCP_IM)

#define	BFA_FCS_VPORT_IS_TARGET_MODE(port) \
		(port->port_cfg.roles & BFA_PORT_ROLE_FCP_TM)

#define	BFA_FCS_VPORT_IS_IPFC_MODE(port) \
		(port->port_cfg.roles & BFA_PORT_ROLE_FCP_IPFC)

/*
 * Is this a Well Known Address
 */
#define BFA_FCS_PID_IS_WKA(pid)  ((bfa_os_ntoh3b(pid) > 0xFFF000) ?  1 : 0)

/*
 * Pointer to elements within Port
 */
#define BFA_FCS_GET_HAL_FROM_PORT(port)  (port->fcs->bfa)
#define BFA_FCS_GET_NS_FROM_PORT(port)  (&port->port_topo.pfab.ns)
#define BFA_FCS_GET_SCN_FROM_PORT(port)  (&port->port_topo.pfab.scn)
#define BFA_FCS_GET_MS_FROM_PORT(port)  (&port->port_topo.pfab.ms)
#define BFA_FCS_GET_FDMI_FROM_PORT(port)  (&port->port_topo.pfab.ms.fdmi)

/*
 * handler for unsolicied frames
 */
void bfa_fcs_port_uf_recv(struct bfa_fcs_port_s *lport, struct fchs_s *fchs,
			u16 len);

/*
 * Following routines will be called by Fabric to indicate port
 * online/offline to vport.
 */
void bfa_fcs_lport_init(struct bfa_fcs_port_s *lport, struct bfa_fcs_s *fcs,
			u16 vf_id, struct bfa_port_cfg_s *port_cfg,
			struct bfa_fcs_vport_s *vport);
void bfa_fcs_port_online(struct bfa_fcs_port_s *port);
void bfa_fcs_port_offline(struct bfa_fcs_port_s *port);
void bfa_fcs_port_delete(struct bfa_fcs_port_s *port);
bfa_boolean_t   bfa_fcs_port_is_online(struct bfa_fcs_port_s *port);

/*
 * Lookup rport based on PID
 */
struct bfa_fcs_rport_s *bfa_fcs_port_get_rport_by_pid(
			struct bfa_fcs_port_s *port, u32 pid);

/*
 * Lookup rport based on PWWN
 */
struct bfa_fcs_rport_s *bfa_fcs_port_get_rport_by_pwwn(
			struct bfa_fcs_port_s *port, wwn_t pwwn);
struct bfa_fcs_rport_s *bfa_fcs_port_get_rport_by_nwwn(
			struct bfa_fcs_port_s *port, wwn_t nwwn);
void bfa_fcs_port_add_rport(struct bfa_fcs_port_s *port,
			struct bfa_fcs_rport_s *rport);
void bfa_fcs_port_del_rport(struct bfa_fcs_port_s *port,
			struct bfa_fcs_rport_s *rport);

void bfa_fcs_port_modinit(struct bfa_fcs_s *fcs);
void bfa_fcs_port_modexit(struct bfa_fcs_s *fcs);
void bfa_fcs_port_lip(struct bfa_fcs_port_s *port);

#endif /* __FCS_LPORT_H__ */
