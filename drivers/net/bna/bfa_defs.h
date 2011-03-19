/*
 * Linux network driver for Brocade Converged Network Adapter.
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
/*
 * Copyright (c) 2005-2010 Brocade Communications Systems, Inc.
 * All rights reserved
 * www.brocade.com
 */

#ifndef __BFA_DEFS_H__
#define __BFA_DEFS_H__

#include "cna.h"
#include "bfa_defs_status.h"
#include "bfa_defs_mfg_comm.h"

#define BFA_STRING_32	32
#define BFA_VERSION_LEN 64

/**
 * ---------------------- adapter definitions ------------
 */

/**
 * BFA adapter level attributes.
 */
enum {
	BFA_ADAPTER_SERIAL_NUM_LEN = STRSZ(BFA_MFG_SERIALNUM_SIZE),
					/*
					 *!< adapter serial num length
					 */
	BFA_ADAPTER_MODEL_NAME_LEN  = 16,  /*!< model name length */
	BFA_ADAPTER_MODEL_DESCR_LEN = 128, /*!< model description length */
	BFA_ADAPTER_MFG_NAME_LEN    = 8,   /*!< manufacturer name length */
	BFA_ADAPTER_SYM_NAME_LEN    = 64,  /*!< adapter symbolic name length */
	BFA_ADAPTER_OS_TYPE_LEN	    = 64,  /*!< adapter os type length */
};

struct bfa_adapter_attr {
	char		manufacturer[BFA_ADAPTER_MFG_NAME_LEN];
	char		serial_num[BFA_ADAPTER_SERIAL_NUM_LEN];
	u32	card_type;
	char		model[BFA_ADAPTER_MODEL_NAME_LEN];
	char		model_descr[BFA_ADAPTER_MODEL_DESCR_LEN];
	u64		pwwn;
	char		node_symname[FC_SYMNAME_MAX];
	char		hw_ver[BFA_VERSION_LEN];
	char		fw_ver[BFA_VERSION_LEN];
	char		optrom_ver[BFA_VERSION_LEN];
	char		os_type[BFA_ADAPTER_OS_TYPE_LEN];
	struct bfa_mfg_vpd vpd;
	struct mac mac;

	u8		nports;
	u8		max_speed;
	u8		prototype;
	char	        asic_rev;

	u8		pcie_gen;
	u8		pcie_lanes_orig;
	u8		pcie_lanes;
	u8	        cna_capable;

	u8		is_mezz;
	u8		trunk_capable;
};

/**
 * ---------------------- IOC definitions ------------
 */

enum {
	BFA_IOC_DRIVER_LEN	= 16,
	BFA_IOC_CHIP_REV_LEN 	= 8,
};

/**
 * Driver and firmware versions.
 */
struct bfa_ioc_driver_attr {
	char		driver[BFA_IOC_DRIVER_LEN];	/*!< driver name */
	char		driver_ver[BFA_VERSION_LEN];	/*!< driver version */
	char		fw_ver[BFA_VERSION_LEN];	/*!< firmware version */
	char		bios_ver[BFA_VERSION_LEN];	/*!< bios version */
	char		efi_ver[BFA_VERSION_LEN];	/*!< EFI version */
	char		ob_ver[BFA_VERSION_LEN];	/*!< openboot version */
};

/**
 * IOC PCI device attributes
 */
struct bfa_ioc_pci_attr {
	u16	vendor_id;	/*!< PCI vendor ID */
	u16	device_id;	/*!< PCI device ID */
	u16	ssid;		/*!< subsystem ID */
	u16	ssvid;		/*!< subsystem vendor ID */
	u32	pcifn;		/*!< PCI device function */
	u32	rsvd;		/* padding */
	char		chip_rev[BFA_IOC_CHIP_REV_LEN];	 /*!< chip revision */
};

/**
 * IOC states
 */
enum bfa_ioc_state {
	BFA_IOC_UNINIT		= 1,	/*!< IOC is in uninit state */
	BFA_IOC_RESET		= 2,	/*!< IOC is in reset state */
	BFA_IOC_SEMWAIT		= 3,	/*!< Waiting for IOC h/w semaphore */
	BFA_IOC_HWINIT		= 4,	/*!< IOC h/w is being initialized */
	BFA_IOC_GETATTR		= 5,	/*!< IOC is being configured */
	BFA_IOC_OPERATIONAL	= 6,	/*!< IOC is operational */
	BFA_IOC_INITFAIL	= 7,	/*!< IOC hardware failure */
	BFA_IOC_FAIL		= 8,	/*!< IOC heart-beat failure */
	BFA_IOC_DISABLING	= 9,	/*!< IOC is being disabled */
	BFA_IOC_DISABLED	= 10,	/*!< IOC is disabled */
	BFA_IOC_FWMISMATCH	= 11,	/*!< IOC f/w different from drivers */
	BFA_IOC_ENABLING	= 12,	/*!< IOC is being enabled */
};

/**
 * IOC firmware stats
 */
struct bfa_fw_ioc_stats {
	u32	enable_reqs;
	u32	disable_reqs;
	u32	get_attr_reqs;
	u32	dbg_sync;
	u32	dbg_dump;
	u32	unknown_reqs;
};

/**
 * IOC driver stats
 */
struct bfa_ioc_drv_stats {
	u32	ioc_isrs;
	u32	ioc_enables;
	u32	ioc_disables;
	u32	ioc_hbfails;
	u32	ioc_boots;
	u32	stats_tmos;
	u32	hb_count;
	u32	disable_reqs;
	u32	enable_reqs;
	u32	disable_replies;
	u32	enable_replies;
};

/**
 * IOC statistics
 */
struct bfa_ioc_stats {
	struct bfa_ioc_drv_stats drv_stats; /*!< driver IOC stats */
	struct bfa_fw_ioc_stats fw_stats;  /*!< firmware IOC stats */
};

enum bfa_ioc_type {
	BFA_IOC_TYPE_FC		= 1,
	BFA_IOC_TYPE_FCoE	= 2,
	BFA_IOC_TYPE_LL		= 3,
};

/**
 * IOC attributes returned in queries
 */
struct bfa_ioc_attr {
	enum bfa_ioc_type ioc_type;
	enum bfa_ioc_state 		state;		/*!< IOC state      */
	struct bfa_adapter_attr adapter_attr;	/*!< HBA attributes */
	struct bfa_ioc_driver_attr driver_attr;	/*!< driver attr    */
	struct bfa_ioc_pci_attr pci_attr;
	u8				port_id;	/*!< port number    */
	u8				rsvd[7];	/*!< 64bit align    */
};

/**
 * ---------------------- mfg definitions ------------
 */

/**
 * Checksum size
 */
#define BFA_MFG_CHKSUM_SIZE			16

#define BFA_MFG_PARTNUM_SIZE			14
#define BFA_MFG_SUPPLIER_ID_SIZE		10
#define BFA_MFG_SUPPLIER_PARTNUM_SIZE		20
#define BFA_MFG_SUPPLIER_SERIALNUM_SIZE		20
#define BFA_MFG_SUPPLIER_REVISION_SIZE		4

#pragma pack(1)

/**
 * @brief BFA adapter manufacturing block definition.
 *
 * All numerical fields are in big-endian format.
 */
struct bfa_mfg_block {
	u8		version;	/*!< manufacturing block version */
	u8		mfg_sig[3];	/*!< characters 'M', 'F', 'G' */
	u16	mfgsize;	/*!< mfg block size */
	u16	u16_chksum;	/*!< old u16 checksum */
	char		brcd_serialnum[STRSZ(BFA_MFG_SERIALNUM_SIZE)];
	char		brcd_partnum[STRSZ(BFA_MFG_PARTNUM_SIZE)];
	u8		mfg_day;	/*!< manufacturing day */
	u8		mfg_month;	/*!< manufacturing month */
	u16	mfg_year;	/*!< manufacturing year */
	u64		mfg_wwn;	/*!< wwn base for this adapter */
	u8		num_wwn;	/*!< number of wwns assigned */
	u8		mfg_speeds;	/*!< speeds allowed for this adapter */
	u8		rsv[2];
	char		supplier_id[STRSZ(BFA_MFG_SUPPLIER_ID_SIZE)];
	char		supplier_partnum[STRSZ(BFA_MFG_SUPPLIER_PARTNUM_SIZE)];
	char
		supplier_serialnum[STRSZ(BFA_MFG_SUPPLIER_SERIALNUM_SIZE)];
	char
		supplier_revision[STRSZ(BFA_MFG_SUPPLIER_REVISION_SIZE)];
	mac_t		mfg_mac;	/*!< mac address */
	u8		num_mac;	/*!< number of mac addresses */
	u8		rsv2;
	u32	mfg_type;	/*!< card type */
	u8		rsv3[108];
	u8		md5_chksum[BFA_MFG_CHKSUM_SIZE]; /*!< md5 checksum */
};

#pragma pack()

/**
 * ---------------------- pci definitions ------------
 */

#define bfa_asic_id_ct(devid)			\
	((devid) == PCI_DEVICE_ID_BROCADE_CT ||	\
	(devid) == PCI_DEVICE_ID_BROCADE_CT_FC)

#endif /* __BFA_DEFS_H__ */
