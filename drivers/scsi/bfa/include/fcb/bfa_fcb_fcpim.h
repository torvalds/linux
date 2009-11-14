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
* : bfad_fcpim.h - BFA FCS initiator mode remote port callbacks
 */

#ifndef __BFAD_FCB_FCPIM_H__
#define __BFAD_FCB_FCPIM_H__

struct bfad_itnim_s;

/*
 * RPIM callbacks
 */

/**
 * 	Memory allocation for remote port instance. Called before PRLI is
 * 	initiated to the remote target port.
 *
 * @param[in] bfad		- driver instance
 * @param[out] itnim		- FCS remote port (IM) instance
 * @param[out] itnim_drv	- driver remote port (IM) instance
 *
 * @return None
 */
void bfa_fcb_itnim_alloc(struct bfad_s *bfad, struct bfa_fcs_itnim_s **itnim,
				    struct bfad_itnim_s **itnim_drv);

/**
 * 		Free remote port (IM) instance.
 *
 * @param[in] bfad	- driver instance
 * @param[in] itnim_drv	- driver remote port instance
 *
 * @return None
 */
void            bfa_fcb_itnim_free(struct bfad_s *bfad,
				   struct bfad_itnim_s *itnim_drv);

/**
 * 	Notification of when login with a remote target device is complete.
 *
 * @param[in] itnim_drv	- driver remote port instance
 *
 * @return None
 */
void            bfa_fcb_itnim_online(struct bfad_itnim_s *itnim_drv);

/**
 * 	Notification when login with the remote device is severed.
 *
 * @param[in] itnim_drv	- driver remote port instance
 *
 * @return None
 */
void            bfa_fcb_itnim_offline(struct bfad_itnim_s *itnim_drv);

void            bfa_fcb_itnim_tov_begin(struct bfad_itnim_s *itnim_drv);
void            bfa_fcb_itnim_tov(struct bfad_itnim_s *itnim_drv);

#endif /* __BFAD_FCB_FCPIM_H__ */
