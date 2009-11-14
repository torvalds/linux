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
 *  bfa_fcb_rport.h BFA FCS rport driver interfaces
 */

#ifndef __BFA_FCB_RPORT_H__
#define __BFA_FCB_RPORT_H__

/**
 *  fcs_rport_fcb Remote port driver interfaces
 */


struct bfad_rport_s;

/*
 * Callback functions from BFA FCS to driver
 */

/**
 * 	Completion callback for bfa_fcs_rport_add().
 *
 * @param[in] rport_drv - driver instance of rport
 *
 * @return None
 */
void bfa_fcb_rport_add(struct bfad_rport_s *rport_drv);

/**
 * 	Completion callback for bfa_fcs_rport_remove().
 *
 * @param[in] rport_drv - driver instance of rport
 *
 * @return None
 */
void bfa_fcb_rport_remove(struct bfad_rport_s *rport_drv);

/**
 * 		Call to allocate a rport instance.
 *
 * @param[in] bfad - driver instance
 * @param[out] rport - BFA FCS instance of rport
 * @param[out] rport_drv - driver instance of rport
 *
 * @retval BFA_STATUS_OK - successfully allocated
 * @retval BFA_STATUS_ENOMEM - cannot allocate
 */
bfa_status_t bfa_fcb_rport_alloc(struct bfad_s *bfad,
			struct bfa_fcs_rport_s **rport,
			struct bfad_rport_s **rport_drv);

/**
 * 	Call to free rport memory resources.
 *
 * @param[in] bfad - driver instance
 * @param[in] rport_drv - driver instance of rport
 *
 * @return None
 */
void bfa_fcb_rport_free(struct bfad_s *bfad, struct bfad_rport_s **rport_drv);



#endif /* __BFA_FCB_RPORT_H__ */
