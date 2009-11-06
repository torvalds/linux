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

#ifndef __VP_PRIV_H__
#define __VP_PRIV_H__

#include <fcs/bfa_fcs_lport.h>
#include <fcs/bfa_fcs_vport.h>

/*
 * Functions exported by vps
 */
void            bfa_fcs_vport_init(struct bfa_fcs_vport_s *vport);

/*
 * Functions exported by vps
 */
void            bfa_fcs_vps_online(struct bfa_fcs_port_s *port);
void            bfa_fcs_vps_offline(struct bfa_fcs_port_s *port);
void            bfa_fcs_vps_lip(struct bfa_fcs_port_s *port);

/*
 * Functions exported by port_fab
 */
void            bfa_fcs_port_fab_init(struct bfa_fcs_port_s *vport);
void            bfa_fcs_port_fab_online(struct bfa_fcs_port_s *vport);
void            bfa_fcs_port_fab_offline(struct bfa_fcs_port_s *vport);
void            bfa_fcs_port_fab_rx_frame(struct bfa_fcs_port_s *port,
					  u8 *rx_frame, u32 len);

/*
 * Functions exported by VP-NS.
 */
void            bfa_fcs_port_ns_init(struct bfa_fcs_port_s *vport);
void            bfa_fcs_port_ns_offline(struct bfa_fcs_port_s *vport);
void            bfa_fcs_port_ns_online(struct bfa_fcs_port_s *vport);
void            bfa_fcs_port_ns_query(struct bfa_fcs_port_s *port);

/*
 * Functions exported by VP-SCN
 */
void            bfa_fcs_port_scn_init(struct bfa_fcs_port_s *vport);
void            bfa_fcs_port_scn_offline(struct bfa_fcs_port_s *vport);
void            bfa_fcs_port_scn_online(struct bfa_fcs_port_s *vport);
void            bfa_fcs_port_scn_process_rscn(struct bfa_fcs_port_s *port,
					      struct fchs_s *rx_frame, u32 len);

/*
 * Functions exported by VP-N2N
 */

void            bfa_fcs_port_n2n_init(struct bfa_fcs_port_s *port);
void            bfa_fcs_port_n2n_online(struct bfa_fcs_port_s *port);
void            bfa_fcs_port_n2n_offline(struct bfa_fcs_port_s *port);
void            bfa_fcs_port_n2n_rx_frame(struct bfa_fcs_port_s *port,
					  u8 *rx_frame, u32 len);

/*
 * Functions exported by VP-LOOP
 */
void            bfa_fcs_port_loop_init(struct bfa_fcs_port_s *port);
void            bfa_fcs_port_loop_online(struct bfa_fcs_port_s *port);
void            bfa_fcs_port_loop_offline(struct bfa_fcs_port_s *port);
void            bfa_fcs_port_loop_lip(struct bfa_fcs_port_s *port);
void            bfa_fcs_port_loop_rx_frame(struct bfa_fcs_port_s *port,
					   u8 *rx_frame, u32 len);

#endif /* __VP_PRIV_H__ */
