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

#ifndef __BFA_DEFS_H__
#define __BFA_DEFS_H__

#include "bfa_fc.h"
#include "bfa_os_inc.h"

#define BFA_MFG_SERIALNUM_SIZE                  11
#define STRSZ(_n)                               (((_n) + 4) & ~3)

/*
 * Manufacturing card type
 */
enum {
	BFA_MFG_TYPE_CB_MAX  = 825,      /*  Crossbow card type max     */
	BFA_MFG_TYPE_FC8P2   = 825,      /*  8G 2port FC card           */
	BFA_MFG_TYPE_FC8P1   = 815,      /*  8G 1port FC card           */
	BFA_MFG_TYPE_FC4P2   = 425,      /*  4G 2port FC card           */
	BFA_MFG_TYPE_FC4P1   = 415,      /*  4G 1port FC card           */
	BFA_MFG_TYPE_CNA10P2 = 1020,     /*  10G 2port CNA card */
	BFA_MFG_TYPE_CNA10P1 = 1010,     /*  10G 1port CNA card */
	BFA_MFG_TYPE_JAYHAWK = 804,      /*  Jayhawk mezz card          */
	BFA_MFG_TYPE_WANCHESE = 1007,    /*  Wanchese mezz card */
	BFA_MFG_TYPE_ASTRA    = 807,     /*  Astra mezz card            */
	BFA_MFG_TYPE_LIGHTNING_P0 = 902, /*  Lightning mezz card - old  */
	BFA_MFG_TYPE_LIGHTNING = 1741,   /*  Lightning mezz card        */
	BFA_MFG_TYPE_INVALID = 0,        /*  Invalid card type          */
};

#pragma pack(1)

/*
 * Check if Mezz card
 */
#define bfa_mfg_is_mezz(type) (( \
	(type) == BFA_MFG_TYPE_JAYHAWK || \
	(type) == BFA_MFG_TYPE_WANCHESE || \
	(type) == BFA_MFG_TYPE_ASTRA || \
	(type) == BFA_MFG_TYPE_LIGHTNING_P0 || \
	(type) == BFA_MFG_TYPE_LIGHTNING))

/*
 * Check if the card having old wwn/mac handling
 */
#define bfa_mfg_is_old_wwn_mac_model(type) (( \
	(type) == BFA_MFG_TYPE_FC8P2 || \
	(type) == BFA_MFG_TYPE_FC8P1 || \
	(type) == BFA_MFG_TYPE_FC4P2 || \
	(type) == BFA_MFG_TYPE_FC4P1 || \
	(type) == BFA_MFG_TYPE_CNA10P2 || \
	(type) == BFA_MFG_TYPE_CNA10P1 || \
	(type) == BFA_MFG_TYPE_JAYHAWK || \
	(type) == BFA_MFG_TYPE_WANCHESE))

#define bfa_mfg_increment_wwn_mac(m, i)                         \
do {                                                            \
	u32 t = ((u32)(m)[0] << 16) | ((u32)(m)[1] << 8) | \
		(u32)(m)[2];  \
	t += (i);      \
	(m)[0] = (t >> 16) & 0xFF;                              \
	(m)[1] = (t >> 8) & 0xFF;                               \
	(m)[2] = t & 0xFF;                                      \
} while (0)

/*
 * VPD data length
 */
#define BFA_MFG_VPD_LEN                 512

/*
 * VPD vendor tag
 */
enum {
	BFA_MFG_VPD_UNKNOWN     = 0,     /*  vendor unknown             */
	BFA_MFG_VPD_IBM         = 1,     /*  vendor IBM                 */
	BFA_MFG_VPD_HP          = 2,     /*  vendor HP                  */
	BFA_MFG_VPD_DELL        = 3,     /*  vendor DELL                */
	BFA_MFG_VPD_PCI_IBM     = 0x08,  /*  PCI VPD IBM                */
	BFA_MFG_VPD_PCI_HP      = 0x10,  /*  PCI VPD HP         */
	BFA_MFG_VPD_PCI_DELL    = 0x20,  /*  PCI VPD DELL               */
	BFA_MFG_VPD_PCI_BRCD    = 0xf8,  /*  PCI VPD Brocade            */
};

/*
 * All numerical fields are in big-endian format.
 */
struct bfa_mfg_vpd_s {
	u8              version;        /*  vpd data version */
	u8              vpd_sig[3];     /*  characters 'V', 'P', 'D' */
	u8              chksum;         /*  u8 checksum */
	u8              vendor;         /*  vendor */
	u8      len;            /*  vpd data length excluding header */
	u8      rsv;
	u8              data[BFA_MFG_VPD_LEN];  /*  vpd data */
};

#pragma pack()

/*
 * Status return values
 */
enum bfa_status {
	BFA_STATUS_OK		= 0,	/*  Success */
	BFA_STATUS_FAILED	= 1,	/*  Operation failed */
	BFA_STATUS_EINVAL	= 2,	/*  Invalid params Check input
					 *  parameters */
	BFA_STATUS_ENOMEM	= 3,	/*  Out of resources */
	BFA_STATUS_ETIMER	= 5,	/*  Timer expired - Retry, if persists,
					 *  contact support */
	BFA_STATUS_EPROTOCOL	= 6,	/*  Protocol error */
	BFA_STATUS_DEVBUSY	= 13,	/*  Device busy - Retry operation */
	BFA_STATUS_UNKNOWN_LWWN = 18,	/*  LPORT PWWN not found */
	BFA_STATUS_UNKNOWN_RWWN = 19,	/*  RPORT PWWN not found */
	BFA_STATUS_VPORT_EXISTS = 21,	/*  VPORT already exists */
	BFA_STATUS_VPORT_MAX	= 22,	/*  Reached max VPORT supported limit */
	BFA_STATUS_UNSUPP_SPEED	= 23,	/*  Invalid Speed Check speed setting */
	BFA_STATUS_INVLD_DFSZ	= 24,	/*  Invalid Max data field size */
	BFA_STATUS_FABRIC_RJT	= 29,	/*  Reject from attached fabric */
	BFA_STATUS_VPORT_WWN_BP	= 46,	/*  WWN is same as base port's WWN */
	BFA_STATUS_NO_FCPIM_NEXUS = 52,	/* No FCP Nexus exists with the rport */
	BFA_STATUS_IOC_FAILURE	= 56,	/* IOC failure - Retry, if persists
					 * contact support */
	BFA_STATUS_INVALID_WWN	= 57,	/*  Invalid WWN */
	BFA_STATUS_DIAG_BUSY	= 71,	/*  diag busy */
	BFA_STATUS_ENOFSAVE	= 78,	/*  No saved firmware trace */
	BFA_STATUS_IOC_DISABLED = 82,   /* IOC is already disabled */
	BFA_STATUS_INVALID_MAC  = 134, /*  Invalid MAC address */
	BFA_STATUS_PBC		= 154, /*  Operation not allowed for pre-boot
					*  configuration */
	BFA_STATUS_TRUNK_ENABLED = 164, /* Trunk is already enabled on
					 * this adapter */
	BFA_STATUS_TRUNK_DISABLED  = 165, /* Trunking is disabled on
					   * the adapter */
	BFA_STATUS_IOPROFILE_OFF = 175, /* IO profile OFF */
	BFA_STATUS_MAX_VAL		/* Unknown error code */
};
#define bfa_status_t enum bfa_status

enum bfa_eproto_status {
	BFA_EPROTO_BAD_ACCEPT = 0,
	BFA_EPROTO_UNKNOWN_RSP = 1
};
#define bfa_eproto_status_t enum bfa_eproto_status

enum bfa_boolean {
	BFA_FALSE = 0,
	BFA_TRUE  = 1
};
#define bfa_boolean_t enum bfa_boolean

#define BFA_STRING_32	32
#define BFA_VERSION_LEN 64

/*
 * ---------------------- adapter definitions ------------
 */

/*
 * BFA adapter level attributes.
 */
enum {
	BFA_ADAPTER_SERIAL_NUM_LEN = STRSZ(BFA_MFG_SERIALNUM_SIZE),
					/*
					 *!< adapter serial num length
					 */
	BFA_ADAPTER_MODEL_NAME_LEN  = 16,  /*  model name length */
	BFA_ADAPTER_MODEL_DESCR_LEN = 128, /*  model description length */
	BFA_ADAPTER_MFG_NAME_LEN    = 8,   /*  manufacturer name length */
	BFA_ADAPTER_SYM_NAME_LEN    = 64,  /*  adapter symbolic name length */
	BFA_ADAPTER_OS_TYPE_LEN	    = 64,  /*  adapter os type length */
};

struct bfa_adapter_attr_s {
	char		manufacturer[BFA_ADAPTER_MFG_NAME_LEN];
	char		serial_num[BFA_ADAPTER_SERIAL_NUM_LEN];
	u32	card_type;
	char		model[BFA_ADAPTER_MODEL_NAME_LEN];
	char		model_descr[BFA_ADAPTER_MODEL_DESCR_LEN];
	wwn_t		pwwn;
	char		node_symname[FC_SYMNAME_MAX];
	char		hw_ver[BFA_VERSION_LEN];
	char		fw_ver[BFA_VERSION_LEN];
	char		optrom_ver[BFA_VERSION_LEN];
	char		os_type[BFA_ADAPTER_OS_TYPE_LEN];
	struct bfa_mfg_vpd_s	vpd;
	struct mac_s	mac;

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

/*
 * ---------------------- IOC definitions ------------
 */

enum {
	BFA_IOC_DRIVER_LEN	= 16,
	BFA_IOC_CHIP_REV_LEN	= 8,
};

/*
 * Driver and firmware versions.
 */
struct bfa_ioc_driver_attr_s {
	char		driver[BFA_IOC_DRIVER_LEN];	/*  driver name */
	char		driver_ver[BFA_VERSION_LEN];	/*  driver version */
	char		fw_ver[BFA_VERSION_LEN];	/*  firmware version */
	char		bios_ver[BFA_VERSION_LEN];	/*  bios version */
	char		efi_ver[BFA_VERSION_LEN];	/*  EFI version */
	char		ob_ver[BFA_VERSION_LEN];	/*  openboot version */
};

/*
 * IOC PCI device attributes
 */
struct bfa_ioc_pci_attr_s {
	u16	vendor_id;	/*  PCI vendor ID */
	u16	device_id;	/*  PCI device ID */
	u16	ssid;		/*  subsystem ID */
	u16	ssvid;		/*  subsystem vendor ID */
	u32	pcifn;		/*  PCI device function */
	u32	rsvd;		/* padding */
	char		chip_rev[BFA_IOC_CHIP_REV_LEN];	 /*  chip revision */
};

/*
 * IOC states
 */
enum bfa_ioc_state {
	BFA_IOC_UNINIT		= 1,	/*  IOC is in uninit state */
	BFA_IOC_RESET		= 2,	/*  IOC is in reset state */
	BFA_IOC_SEMWAIT		= 3,	/*  Waiting for IOC h/w semaphore */
	BFA_IOC_HWINIT		= 4,	/*  IOC h/w is being initialized */
	BFA_IOC_GETATTR		= 5,	/*  IOC is being configured */
	BFA_IOC_OPERATIONAL	= 6,	/*  IOC is operational */
	BFA_IOC_INITFAIL	= 7,	/*  IOC hardware failure */
	BFA_IOC_FAIL		= 8,	/*  IOC heart-beat failure */
	BFA_IOC_DISABLING	= 9,	/*  IOC is being disabled */
	BFA_IOC_DISABLED	= 10,	/*  IOC is disabled */
	BFA_IOC_FWMISMATCH	= 11,	/*  IOC f/w different from drivers */
	BFA_IOC_ENABLING	= 12,	/*  IOC is being enabled */
};

/*
 * IOC firmware stats
 */
struct bfa_fw_ioc_stats_s {
	u32	enable_reqs;
	u32	disable_reqs;
	u32	get_attr_reqs;
	u32	dbg_sync;
	u32	dbg_dump;
	u32	unknown_reqs;
};

/*
 * IOC driver stats
 */
struct bfa_ioc_drv_stats_s {
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

/*
 * IOC statistics
 */
struct bfa_ioc_stats_s {
	struct bfa_ioc_drv_stats_s	drv_stats; /*  driver IOC stats */
	struct bfa_fw_ioc_stats_s	fw_stats;  /*  firmware IOC stats */
};

enum bfa_ioc_type_e {
	BFA_IOC_TYPE_FC		= 1,
	BFA_IOC_TYPE_FCoE	= 2,
	BFA_IOC_TYPE_LL		= 3,
};

/*
 * IOC attributes returned in queries
 */
struct bfa_ioc_attr_s {
	enum bfa_ioc_type_e		ioc_type;
	enum bfa_ioc_state		state;		/*  IOC state      */
	struct bfa_adapter_attr_s	adapter_attr;	/*  HBA attributes */
	struct bfa_ioc_driver_attr_s	driver_attr;	/*  driver attr    */
	struct bfa_ioc_pci_attr_s	pci_attr;
	u8				port_id;	/*  port number    */
	u8				rsvd[7];	/*  64bit align    */
};

/*
 * ---------------------- mfg definitions ------------
 */

/*
 * Checksum size
 */
#define BFA_MFG_CHKSUM_SIZE			16

#define BFA_MFG_PARTNUM_SIZE			14
#define BFA_MFG_SUPPLIER_ID_SIZE		10
#define BFA_MFG_SUPPLIER_PARTNUM_SIZE		20
#define BFA_MFG_SUPPLIER_SERIALNUM_SIZE		20
#define BFA_MFG_SUPPLIER_REVISION_SIZE		4

#pragma pack(1)

/*
 * All numerical fields are in big-endian format.
 */
struct bfa_mfg_block_s {
	u8		version;	/*  manufacturing block version */
	u8		mfg_sig[3];	/*  characters 'M', 'F', 'G' */
	u16	mfgsize;	/*  mfg block size */
	u16	u16_chksum;	/*  old u16 checksum */
	char		brcd_serialnum[STRSZ(BFA_MFG_SERIALNUM_SIZE)];
	char		brcd_partnum[STRSZ(BFA_MFG_PARTNUM_SIZE)];
	u8		mfg_day;	/*  manufacturing day */
	u8		mfg_month;	/*  manufacturing month */
	u16	mfg_year;	/*  manufacturing year */
	wwn_t		mfg_wwn;	/*  wwn base for this adapter */
	u8		num_wwn;	/*  number of wwns assigned */
	u8		mfg_speeds;	/*  speeds allowed for this adapter */
	u8		rsv[2];
	char		supplier_id[STRSZ(BFA_MFG_SUPPLIER_ID_SIZE)];
	char		supplier_partnum[STRSZ(BFA_MFG_SUPPLIER_PARTNUM_SIZE)];
	char
		supplier_serialnum[STRSZ(BFA_MFG_SUPPLIER_SERIALNUM_SIZE)];
	char
		supplier_revision[STRSZ(BFA_MFG_SUPPLIER_REVISION_SIZE)];
	mac_t		mfg_mac;	/*  mac address */
	u8		num_mac;	/*  number of mac addresses */
	u8		rsv2;
	u32	mfg_type;	/*  card type */
	u8		rsv3[108];
	u8		md5_chksum[BFA_MFG_CHKSUM_SIZE]; /*  md5 checksum */
};

#pragma pack()

/*
 * ---------------------- pci definitions ------------
 */

/*
 * PCI device and vendor ID information
 */
enum {
	BFA_PCI_VENDOR_ID_BROCADE	= 0x1657,
	BFA_PCI_DEVICE_ID_FC_8G2P	= 0x13,
	BFA_PCI_DEVICE_ID_FC_8G1P	= 0x17,
	BFA_PCI_DEVICE_ID_CT		= 0x14,
	BFA_PCI_DEVICE_ID_CT_FC		= 0x21,
};

#define bfa_asic_id_ct(devid)			\
	((devid) == BFA_PCI_DEVICE_ID_CT ||	\
	 (devid) == BFA_PCI_DEVICE_ID_CT_FC)

/*
 * PCI sub-system device and vendor ID information
 */
enum {
	BFA_PCI_FCOE_SSDEVICE_ID	= 0x14,
};

/*
 * Maximum number of device address ranges mapped through different BAR(s)
 */
#define BFA_PCI_ACCESS_RANGES 1

/*
 *	Port speed settings. Each specific speed is a bit field. Use multiple
 *	bits to specify speeds to be selected for auto-negotiation.
 */
enum bfa_port_speed {
	BFA_PORT_SPEED_UNKNOWN = 0,
	BFA_PORT_SPEED_1GBPS	= 1,
	BFA_PORT_SPEED_2GBPS	= 2,
	BFA_PORT_SPEED_4GBPS	= 4,
	BFA_PORT_SPEED_8GBPS	= 8,
	BFA_PORT_SPEED_10GBPS	= 10,
	BFA_PORT_SPEED_16GBPS	= 16,
	BFA_PORT_SPEED_AUTO =
		(BFA_PORT_SPEED_1GBPS | BFA_PORT_SPEED_2GBPS |
		 BFA_PORT_SPEED_4GBPS | BFA_PORT_SPEED_8GBPS),
};
#define bfa_port_speed_t enum bfa_port_speed

enum {
	BFA_BOOT_BOOTLUN_MAX = 4,       /*  maximum boot lun per IOC */
	BFA_PREBOOT_BOOTLUN_MAX = 8,    /*  maximum preboot lun per IOC */
};

#define BOOT_CFG_REV1   1
#define BOOT_CFG_VLAN   1

/*
 *      Boot options setting. Boot options setting determines from where
 *      to get the boot lun information
 */
enum bfa_boot_bootopt {
	BFA_BOOT_AUTO_DISCOVER  = 0, /*  Boot from blun provided by fabric */
	BFA_BOOT_STORED_BLUN = 1, /*  Boot from bluns stored in flash */
	BFA_BOOT_FIRST_LUN      = 2, /*  Boot from first discovered blun */
	BFA_BOOT_PBC    = 3, /*  Boot from pbc configured blun  */
};

#pragma pack(1)
/*
 * Boot lun information.
 */
struct bfa_boot_bootlun_s {
	wwn_t   pwwn;   /*  port wwn of target */
	lun_t   lun;    /*  64-bit lun */
};
#pragma pack()

/*
 * BOOT boot configuraton
 */
struct bfa_boot_pbc_s {
	u8              enable;         /*  enable/disable SAN boot */
	u8              speed;          /*  boot speed settings */
	u8              topology;       /*  boot topology setting */
	u8              rsvd1;
	u32     nbluns;         /*  number of boot luns */
	struct bfa_boot_bootlun_s pblun[BFA_PREBOOT_BOOTLUN_MAX];
};

#endif /* __BFA_DEFS_H__ */
