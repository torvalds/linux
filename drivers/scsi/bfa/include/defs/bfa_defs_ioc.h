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

#ifndef __BFA_DEFS_IOC_H__
#define __BFA_DEFS_IOC_H__

#include <protocol/types.h>
#include <defs/bfa_defs_types.h>
#include <defs/bfa_defs_version.h>
#include <defs/bfa_defs_adapter.h>
#include <defs/bfa_defs_pm.h>

enum {
	BFA_IOC_DRIVER_LEN	= 16,
	BFA_IOC_CHIP_REV_LEN 	= 8,
};

/**
 * Driver and firmware versions.
 */
struct bfa_ioc_driver_attr_s {
	char            driver[BFA_IOC_DRIVER_LEN];	/*  driver name */
	char            driver_ver[BFA_VERSION_LEN];	/*  driver version */
	char            fw_ver[BFA_VERSION_LEN];	/*  firmware version*/
	char            bios_ver[BFA_VERSION_LEN];	/*  bios version */
	char            efi_ver[BFA_VERSION_LEN];	/*  EFI version */
	char            ob_ver[BFA_VERSION_LEN];	/*  openboot version*/
};

/**
 * IOC PCI device attributes
 */
struct bfa_ioc_pci_attr_s {
	u16        vendor_id;	/*  PCI vendor ID */
	u16        device_id;	/*  PCI device ID */
	u16        ssid;		/*  subsystem ID */
	u16        ssvid;		/*  subsystem vendor ID */
	u32        pcifn;		/*  PCI device function */
	u32        rsvd;		/* padding */
	u8         chip_rev[BFA_IOC_CHIP_REV_LEN];	 /*  chip revision */
};

/**
 * IOC states
 */
enum bfa_ioc_state {
	BFA_IOC_RESET       = 1,  /*  IOC is in reset state */
	BFA_IOC_SEMWAIT     = 2,  /*  Waiting for IOC hardware semaphore */
	BFA_IOC_HWINIT 	    = 3,  /*  IOC hardware is being initialized */
	BFA_IOC_GETATTR     = 4,  /*  IOC is being configured */
	BFA_IOC_OPERATIONAL = 5,  /*  IOC is operational */
	BFA_IOC_INITFAIL    = 6,  /*  IOC hardware failure */
	BFA_IOC_HBFAIL      = 7,  /*  IOC heart-beat failure */
	BFA_IOC_DISABLING   = 8,  /*  IOC is being disabled */
	BFA_IOC_DISABLED    = 9,  /*  IOC is disabled */
	BFA_IOC_FWMISMATCH  = 10, /*  IOC firmware different from drivers */
};

/**
 * IOC firmware stats
 */
struct bfa_fw_ioc_stats_s {
	u32        hb_count;
	u32        cfg_reqs;
	u32        enable_reqs;
	u32        disable_reqs;
	u32        stats_reqs;
	u32        clrstats_reqs;
	u32        unknown_reqs;
	u32        ic_reqs;		/*  interrupt coalesce reqs */
};

/**
 * IOC driver stats
 */
struct bfa_ioc_drv_stats_s {
	u32	ioc_isrs;
	u32	ioc_enables;
	u32	ioc_disables;
	u32	ioc_hbfails;
	u32	ioc_boots;
	u32	stats_tmos;
	u32        hb_count;
	u32        disable_reqs;
	u32        enable_reqs;
	u32        disable_replies;
	u32        enable_replies;
};

/**
 * IOC statistics
 */
struct bfa_ioc_stats_s {
	struct bfa_ioc_drv_stats_s	drv_stats; /*  driver IOC stats */
	struct bfa_fw_ioc_stats_s 	fw_stats;  /*  firmware IOC stats */
};


enum bfa_ioc_type_e {
	BFA_IOC_TYPE_FC	  = 1,
	BFA_IOC_TYPE_FCoE = 2,
	BFA_IOC_TYPE_LL	  = 3,
};

/**
 * IOC attributes returned in queries
 */
struct bfa_ioc_attr_s {
	enum bfa_ioc_type_e		ioc_type;
	enum bfa_ioc_state 		state;		/*  IOC state      */
	struct bfa_adapter_attr_s	adapter_attr;	/*  HBA attributes */
	struct bfa_ioc_driver_attr_s 	driver_attr;	/*  driver attr    */
	struct bfa_ioc_pci_attr_s	pci_attr;
	u8				port_id;	/*  port number    */
	u8				rsvd[7];	/*!< 64bit align    */
};

/**
 * BFA IOC level events
 */
enum bfa_ioc_aen_event {
	BFA_IOC_AEN_HBGOOD	= 1,	/*  Heart Beat restore event	*/
	BFA_IOC_AEN_HBFAIL	= 2,	/*  Heart Beat failure event	*/
	BFA_IOC_AEN_ENABLE	= 3,	/*  IOC enabled event		*/
	BFA_IOC_AEN_DISABLE	= 4,	/*  IOC disabled event		*/
	BFA_IOC_AEN_FWMISMATCH	= 5,	/*  IOC firmware mismatch	*/
};

/**
 * BFA IOC level event data, now just a place holder
 */
struct bfa_ioc_aen_data_s {
	wwn_t	pwwn;
	s16 ioc_type;
	mac_t	mac;
};

#endif /* __BFA_DEFS_IOC_H__ */

