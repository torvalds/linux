/*
 * Copyright (c) 2005-2010 Brocade Communications Systems, Inc.
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

#ifndef __BFI_PBC_H__
#define __BFI_PBC_H__

#pragma pack(1)

#define BFI_PBC_MAX_BLUNS	8
#define BFI_PBC_MAX_VPORTS	16

#define BFI_PBC_PORT_DISABLED	 2
/**
 * PBC boot lun configuration
 */
struct bfi_pbc_blun_s {
	wwn_t		tgt_pwwn;
	lun_t		tgt_lun;
};

/**
 * PBC virtual port configuration
 */
struct bfi_pbc_vport_s {
	wwn_t		vp_pwwn;
	wwn_t		vp_nwwn;
};

/**
 * BFI pre-boot configuration information
 */
struct bfi_pbc_s {
	u8		port_enabled;
	u8		boot_enabled;
	u8		nbluns;
	u8		nvports;
	u8		port_speed;
	u8		rsvd_a;
	u16		hss;
	wwn_t		pbc_pwwn;
	wwn_t		pbc_nwwn;
	struct bfi_pbc_blun_s blun[BFI_PBC_MAX_BLUNS];
	struct bfi_pbc_vport_s vport[BFI_PBC_MAX_VPORTS];
};

#pragma pack()

#endif /* __BFI_PBC_H__ */
