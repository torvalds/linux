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
 *  bfa_fcb_vport.h BFA FCS virtual port driver interfaces
 */

#ifndef __BFA_FCB_VPORT_H__
#define __BFA_FCB_VPORT_H__

/**
 *  fcs_vport_fcb Virtual port driver interfaces
 */


struct bfad_vport_s;

/*
 * Callback functions from BFA FCS to driver
 */

/**
 * 	Completion callback for bfa_fcs_vport_delete().
 *
 * @param[in] vport_drv - driver instance of vport
 *
 * @return None
 */
void bfa_fcb_vport_delete(struct bfad_vport_s *vport_drv);
void bfa_fcb_pbc_vport_create(struct bfad_s *bfad, struct bfi_pbc_vport_s);



#endif /* __BFA_FCB_VPORT_H__ */
