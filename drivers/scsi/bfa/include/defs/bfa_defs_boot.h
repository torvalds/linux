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

#ifndef __BFA_DEFS_BOOT_H__
#define __BFA_DEFS_BOOT_H__

#include <protocol/types.h>
#include <defs/bfa_defs_types.h>
#include <defs/bfa_defs_pport.h>

enum {
	BFA_BOOT_BOOTLUN_MAX = 4,	/*  maximum boot lun per IOC */
};

#define BOOT_CFG_REV1	1

/**
 *      Boot options setting. Boot options setting determines from where
 *      to get the boot lun information
 */
enum bfa_boot_bootopt {
    BFA_BOOT_AUTO_DISCOVER = 0,    /*  Boot from blun provided by fabric */
    BFA_BOOT_STORED_BLUN   = 1,    /*  Boot from bluns stored in flash   */
    BFA_BOOT_FIRST_LUN     = 2,    /*  Boot from first discovered blun   */
};

/**
 * Boot lun information.
 */
struct bfa_boot_bootlun_s {
	wwn_t           pwwn;	/*  port wwn of target */
	lun_t           lun;	/*  64-bit lun */
};

/**
 * BOOT boot configuraton
 */
struct bfa_boot_cfg_s {
	u8         version;
	u8         rsvd1;
	u16        chksum;

	u8         enable;		/*  enable/disable SAN boot */
	u8         speed;		/*  boot speed settings */
	u8         topology;	/*  boot topology setting */
	u8         bootopt;	/*  bfa_boot_bootopt_t */

	u32        nbluns;		/*  number of boot luns */

	u32        rsvd2;

	struct bfa_boot_bootlun_s blun[BFA_BOOT_BOOTLUN_MAX];
	struct bfa_boot_bootlun_s blun_disc[BFA_BOOT_BOOTLUN_MAX];
};


#endif /* __BFA_DEFS_BOOT_H__ */
