/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2005-2014 Brocade Communications Systems, Inc.
 * Copyright (c) 2014- QLogic Corporation.
 * All rights reserved
 * www.qlogic.com
 *
 * Linux driver for QLogic BR-series Fibre Channel Host Bus Adapter.
 */

#ifndef __BFA_DEFS_H__
#define __BFA_DEFS_H__

#include "bfa_fc.h"
#include "bfad_drv.h"

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
	BFA_MFG_TYPE_PROWLER_F = 1560,	 /*  Prowler FC only cards	*/
	BFA_MFG_TYPE_PROWLER_N = 1410,	 /*  Prowler NIC only cards	*/
	BFA_MFG_TYPE_PROWLER_C = 1710,   /*  Prowler CNA only cards	*/
	BFA_MFG_TYPE_PROWLER_D = 1860,   /*  Prowler Dual cards		*/
	BFA_MFG_TYPE_CHINOOK   = 1867,   /*  Chinook cards		*/
	BFA_MFG_TYPE_CHINOOK2   = 1869,	 /*!< Chinook2 cards		*/
	BFA_MFG_TYPE_INVALID = 0,        /*  Invalid card type		*/
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
	(type) == BFA_MFG_TYPE_LIGHTNING || \
	(type) == BFA_MFG_TYPE_CHINOOK || \
	(type) == BFA_MFG_TYPE_CHINOOK2))

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
	BFA_STATUS_BADFLASH	= 9,	/*  Flash is bad */
	BFA_STATUS_SFP_UNSUPP	= 10,	/*  Unsupported SFP - Replace SFP */
	BFA_STATUS_UNKNOWN_VFID = 11,	/*  VF_ID not found */
	BFA_STATUS_DATACORRUPTED = 12,  /*  Diag returned data corrupted */
	BFA_STATUS_DEVBUSY	= 13,	/*  Device busy - Retry operation */
	BFA_STATUS_HDMA_FAILED  = 16,   /* Host dma failed contact support */
	BFA_STATUS_FLASH_BAD_LEN = 17,	/*  Flash bad length */
	BFA_STATUS_UNKNOWN_LWWN = 18,	/*  LPORT PWWN not found */
	BFA_STATUS_UNKNOWN_RWWN = 19,	/*  RPORT PWWN not found */
	BFA_STATUS_VPORT_EXISTS = 21,	/*  VPORT already exists */
	BFA_STATUS_VPORT_MAX	= 22,	/*  Reached max VPORT supported limit */
	BFA_STATUS_UNSUPP_SPEED	= 23,	/*  Invalid Speed Check speed setting */
	BFA_STATUS_INVLD_DFSZ	= 24,	/*  Invalid Max data field size */
	BFA_STATUS_CMD_NOTSUPP  = 26,   /*  Command/API not supported */
	BFA_STATUS_FABRIC_RJT	= 29,	/*  Reject from attached fabric */
	BFA_STATUS_UNKNOWN_VWWN = 30,	/*  VPORT PWWN not found */
	BFA_STATUS_PORT_OFFLINE = 34,	/*  Port is not online */
	BFA_STATUS_VPORT_WWN_BP	= 46,	/*  WWN is same as base port's WWN */
	BFA_STATUS_PORT_NOT_DISABLED = 47, /* Port not disabled disable port */
	BFA_STATUS_NO_FCPIM_NEXUS = 52,	/* No FCP Nexus exists with the rport */
	BFA_STATUS_IOC_FAILURE	= 56,	/* IOC failure - Retry, if persists
					 * contact support */
	BFA_STATUS_INVALID_WWN	= 57,	/*  Invalid WWN */
	BFA_STATUS_ADAPTER_ENABLED = 60, /* Adapter is not disabled */
	BFA_STATUS_IOC_NON_OP   = 61,	/* IOC is not operational */
	BFA_STATUS_VERSION_FAIL = 70, /* Application/Driver version mismatch */
	BFA_STATUS_DIAG_BUSY	= 71,	/*  diag busy */
	BFA_STATUS_BEACON_ON    = 72,   /* Port Beacon already on */
	BFA_STATUS_ENOFSAVE	= 78,	/*  No saved firmware trace */
	BFA_STATUS_IOC_DISABLED = 82,   /* IOC is already disabled */
	BFA_STATUS_ERROR_TRL_ENABLED  = 87,   /* TRL is enabled */
	BFA_STATUS_ERROR_QOS_ENABLED  = 88,   /* QoS is enabled */
	BFA_STATUS_NO_SFP_DEV = 89,	/* No SFP device check or replace SFP */
	BFA_STATUS_MEMTEST_FAILED = 90, /* Memory test failed contact support */
	BFA_STATUS_LEDTEST_OP = 109, /* LED test is operating */
	BFA_STATUS_INVALID_MAC  = 134, /*  Invalid MAC address */
	BFA_STATUS_CMD_NOTSUPP_CNA = 146, /* Command not supported for CNA */
	BFA_STATUS_PBC		= 154, /*  Operation not allowed for pre-boot
					*  configuration */
	BFA_STATUS_BAD_FWCFG = 156,	/* Bad firmware configuration */
	BFA_STATUS_INVALID_VENDOR = 158, /* Invalid switch vendor */
	BFA_STATUS_SFP_NOT_READY = 159,	/* SFP info is not ready. Retry */
	BFA_STATUS_TRUNK_ENABLED = 164, /* Trunk is already enabled on
					 * this adapter */
	BFA_STATUS_TRUNK_DISABLED  = 165, /* Trunking is disabled on
					   * the adapter */
	BFA_STATUS_IOPROFILE_OFF = 175, /* IO profile OFF */
	BFA_STATUS_PHY_NOT_PRESENT = 183, /* PHY module not present */
	BFA_STATUS_FEATURE_NOT_SUPPORTED = 192,	/* Feature not supported */
	BFA_STATUS_ENTRY_EXISTS = 193,	/* Entry already exists */
	BFA_STATUS_ENTRY_NOT_EXISTS = 194, /* Entry does not exist */
	BFA_STATUS_NO_CHANGE = 195,	/* Feature already in that state */
	BFA_STATUS_FAA_ENABLED = 197,	/* FAA is already enabled */
	BFA_STATUS_FAA_DISABLED = 198,	/* FAA is already disabled */
	BFA_STATUS_FAA_ACQUIRED = 199,	/* FAA is already acquired */
	BFA_STATUS_FAA_ACQ_ADDR = 200,	/* Acquiring addr */
	BFA_STATUS_BBCR_FC_ONLY = 201, /*!< BBCredit Recovery is supported for *
					* FC mode only */
	BFA_STATUS_ERROR_TRUNK_ENABLED = 203,	/* Trunk enabled on adapter */
	BFA_STATUS_MAX_ENTRY_REACHED = 212,	/* MAX entry reached */
	BFA_STATUS_TOPOLOGY_LOOP = 230, /* Topology is set to Loop */
	BFA_STATUS_LOOP_UNSUPP_MEZZ = 231, /* Loop topology is not supported
					    * on mezz cards */
	BFA_STATUS_INVALID_BW = 233,	/* Invalid bandwidth value */
	BFA_STATUS_QOS_BW_INVALID = 234,   /* Invalid QOS bandwidth
					    * configuration */
	BFA_STATUS_DPORT_ENABLED = 235, /* D-port mode is already enabled */
	BFA_STATUS_DPORT_DISABLED = 236, /* D-port mode is already disabled */
	BFA_STATUS_CMD_NOTSUPP_MEZZ = 239, /* Cmd not supported for MEZZ card */
	BFA_STATUS_FRU_NOT_PRESENT = 240, /* fru module not present */
	BFA_STATUS_DPORT_NO_SFP = 243, /* SFP is not present.\n D-port will be
					* enabled but it will be operational
					* only after inserting a valid SFP. */
	BFA_STATUS_DPORT_ERR = 245,	/* D-port mode is enabled */
	BFA_STATUS_DPORT_ENOSYS = 254, /* Switch has no D_Port functionality */
	BFA_STATUS_DPORT_CANT_PERF = 255, /* Switch port is not D_Port capable
					* or D_Port is disabled */
	BFA_STATUS_DPORT_LOGICALERR = 256, /* Switch D_Port fail */
	BFA_STATUS_DPORT_SWBUSY = 257, /* Switch port busy */
	BFA_STATUS_ERR_BBCR_SPEED_UNSUPPORT = 258, /*!< BB credit recovery is
					* supported at max port speed alone */
	BFA_STATUS_ERROR_BBCR_ENABLED  = 259, /*!< BB credit recovery
					* is enabled */
	BFA_STATUS_INVALID_BBSCN = 260, /*!< Invalid BBSCN value.
					 * Valid range is [1-15] */
	BFA_STATUS_DDPORT_ERR = 261, /* Dynamic D_Port mode is active.\n To
					* exit dynamic mode, disable D_Port on
					* the remote port */
	BFA_STATUS_DPORT_SFPWRAP_ERR = 262, /* Clear e/o_wrap fail, check or
						* replace SFP */
	BFA_STATUS_BBCR_CFG_NO_CHANGE = 265, /*!< BBCR is operational.
			* Disable BBCR and try this operation again. */
	BFA_STATUS_DPORT_SW_NOTREADY = 268, /* Remote port is not ready to
					* start dport test. Check remote
					* port status. */
	BFA_STATUS_DPORT_INV_SFP = 271, /* Invalid SFP for D-PORT mode. */
	BFA_STATUS_DPORT_CMD_NOTSUPP    = 273, /* Dport is not supported by
					* remote port */
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
	BFA_ADAPTER_UUID_LEN	    = 16,  /* adapter uuid length */
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
	u8		mfg_day;	/* manufacturing day */
	u8		mfg_month;	/* manufacturing month */
	u16		mfg_year;	/* manufacturing year */
	u16		rsvd;
	u8		uuid[BFA_ADAPTER_UUID_LEN];
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
	BFA_IOC_HWFAIL		= 13,	/*  PCI mapping doesn't exist */
	BFA_IOC_ACQ_ADDR	= 14,	/*  Acquiring addr from fabric */
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
	u32	rsvd;
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
	u8				port_mode;	/*  bfa_mode_s	*/
	u8				cap_bm;		/*  capability	*/
	u8				port_mode_cfg;	/*  bfa_mode_s	*/
	u8				def_fn;		/* 1 if default fn */
	u8				rsvd[3];	/*  64bit align	*/
};

/*
 *			AEN related definitions
 */
enum bfa_aen_category {
	BFA_AEN_CAT_ADAPTER	= 1,
	BFA_AEN_CAT_PORT	= 2,
	BFA_AEN_CAT_LPORT	= 3,
	BFA_AEN_CAT_RPORT	= 4,
	BFA_AEN_CAT_ITNIM	= 5,
	BFA_AEN_CAT_AUDIT	= 8,
	BFA_AEN_CAT_IOC		= 9,
};

/* BFA adapter level events */
enum bfa_adapter_aen_event {
	BFA_ADAPTER_AEN_ADD	= 1,	/* New Adapter found event */
	BFA_ADAPTER_AEN_REMOVE	= 2,	/* Adapter removed event */
};

struct bfa_adapter_aen_data_s {
	char	serial_num[BFA_ADAPTER_SERIAL_NUM_LEN];
	u32	nports; /* Number of NPorts */
	wwn_t	pwwn;   /* WWN of one of its physical port */
};

/* BFA physical port Level events */
enum bfa_port_aen_event {
	BFA_PORT_AEN_ONLINE	= 1,    /* Physical Port online event */
	BFA_PORT_AEN_OFFLINE	= 2,    /* Physical Port offline event */
	BFA_PORT_AEN_RLIR	= 3,    /* RLIR event, not supported */
	BFA_PORT_AEN_SFP_INSERT	= 4,    /* SFP inserted event */
	BFA_PORT_AEN_SFP_REMOVE	= 5,    /* SFP removed event */
	BFA_PORT_AEN_SFP_POM	= 6,    /* SFP POM event */
	BFA_PORT_AEN_ENABLE	= 7,    /* Physical Port enable event */
	BFA_PORT_AEN_DISABLE	= 8,    /* Physical Port disable event */
	BFA_PORT_AEN_AUTH_ON	= 9,    /* Physical Port auth success event */
	BFA_PORT_AEN_AUTH_OFF	= 10,   /* Physical Port auth fail event */
	BFA_PORT_AEN_DISCONNECT	= 11,   /* Physical Port disconnect event */
	BFA_PORT_AEN_QOS_NEG	= 12,   /* Base Port QOS negotiation event */
	BFA_PORT_AEN_FABRIC_NAME_CHANGE	= 13, /* Fabric Name/WWN change */
	BFA_PORT_AEN_SFP_ACCESS_ERROR	= 14, /* SFP read error event */
	BFA_PORT_AEN_SFP_UNSUPPORT	= 15, /* Unsupported SFP event */
};

enum bfa_port_aen_sfp_pom {
	BFA_PORT_AEN_SFP_POM_GREEN = 1, /* Normal */
	BFA_PORT_AEN_SFP_POM_AMBER = 2, /* Warning */
	BFA_PORT_AEN_SFP_POM_RED   = 3, /* Critical */
	BFA_PORT_AEN_SFP_POM_MAX   = BFA_PORT_AEN_SFP_POM_RED
};

struct bfa_port_aen_data_s {
	wwn_t		pwwn;		/* WWN of the physical port */
	wwn_t		fwwn;		/* WWN of the fabric port */
	u32		phy_port_num;	/* For SFP related events */
	u16		ioc_type;
	u16		level;		/* Only transitions will be informed */
	mac_t		mac;		/* MAC address of the ethernet port */
	u16		rsvd;
};

/* BFA AEN logical port events */
enum bfa_lport_aen_event {
	BFA_LPORT_AEN_NEW	= 1,		/* LPort created event */
	BFA_LPORT_AEN_DELETE	= 2,		/* LPort deleted event */
	BFA_LPORT_AEN_ONLINE	= 3,		/* LPort online event */
	BFA_LPORT_AEN_OFFLINE	= 4,		/* LPort offline event */
	BFA_LPORT_AEN_DISCONNECT = 5,		/* LPort disconnect event */
	BFA_LPORT_AEN_NEW_PROP	= 6,		/* VPort created event */
	BFA_LPORT_AEN_DELETE_PROP = 7,		/* VPort deleted event */
	BFA_LPORT_AEN_NEW_STANDARD = 8,		/* VPort created event */
	BFA_LPORT_AEN_DELETE_STANDARD = 9,	/* VPort deleted event */
	BFA_LPORT_AEN_NPIV_DUP_WWN = 10,	/* VPort with duplicate WWN */
	BFA_LPORT_AEN_NPIV_FABRIC_MAX = 11,	/* Max NPIV in fabric/fport */
	BFA_LPORT_AEN_NPIV_UNKNOWN = 12,	/* Unknown NPIV Error code */
};

struct bfa_lport_aen_data_s {
	u16	vf_id;	/* vf_id of this logical port */
	u16	roles;	/* Logical port mode,IM/TM/IP etc */
	u32	rsvd;
	wwn_t	ppwwn;	/* WWN of its physical port */
	wwn_t	lpwwn;	/* WWN of this logical port */
};

/* BFA ITNIM events */
enum bfa_itnim_aen_event {
	BFA_ITNIM_AEN_ONLINE	 = 1,	/* Target online */
	BFA_ITNIM_AEN_OFFLINE	 = 2,	/* Target offline */
	BFA_ITNIM_AEN_DISCONNECT = 3,	/* Target disconnected */
};

struct bfa_itnim_aen_data_s {
	u16		vf_id;		/* vf_id of the IT nexus */
	u16		rsvd[3];
	wwn_t		ppwwn;		/* WWN of its physical port */
	wwn_t		lpwwn;		/* WWN of logical port */
	wwn_t		rpwwn;		/* WWN of remote(target) port */
};

/* BFA audit events */
enum bfa_audit_aen_event {
	BFA_AUDIT_AEN_AUTH_ENABLE	= 1,
	BFA_AUDIT_AEN_AUTH_DISABLE	= 2,
	BFA_AUDIT_AEN_FLASH_ERASE	= 3,
	BFA_AUDIT_AEN_FLASH_UPDATE	= 4,
};

struct bfa_audit_aen_data_s {
	wwn_t	pwwn;
	int	partition_inst;
	int	partition_type;
};

/* BFA IOC level events */
enum bfa_ioc_aen_event {
	BFA_IOC_AEN_HBGOOD  = 1,	/* Heart Beat restore event	*/
	BFA_IOC_AEN_HBFAIL  = 2,	/* Heart Beat failure event	*/
	BFA_IOC_AEN_ENABLE  = 3,	/* IOC enabled event		*/
	BFA_IOC_AEN_DISABLE = 4,	/* IOC disabled event		*/
	BFA_IOC_AEN_FWMISMATCH  = 5,	/* IOC firmware mismatch	*/
	BFA_IOC_AEN_FWCFG_ERROR = 6,	/* IOC firmware config error	*/
	BFA_IOC_AEN_INVALID_VENDOR = 7,
	BFA_IOC_AEN_INVALID_NWWN = 8,	/* Zero NWWN			*/
	BFA_IOC_AEN_INVALID_PWWN = 9	/* Zero PWWN			*/
};

struct bfa_ioc_aen_data_s {
	wwn_t	pwwn;
	u16	ioc_type;
	mac_t	mac;
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
/*
 * Initial capability definition
 */
#define BFA_MFG_IC_FC	0x01
#define BFA_MFG_IC_ETH	0x02

/*
 * Adapter capability mask definition
 */
#define BFA_CM_HBA	0x01
#define BFA_CM_CNA	0x02
#define BFA_CM_NIC	0x04
#define BFA_CM_FC16G	0x08
#define BFA_CM_SRIOV	0x10
#define BFA_CM_MEZZ	0x20

#pragma pack(1)

/*
 * All numerical fields are in big-endian format.
 */
struct bfa_mfg_block_s {
	u8	version;    /*!< manufacturing block version */
	u8     mfg_sig[3]; /*!< characters 'M', 'F', 'G' */
	u16    mfgsize;    /*!< mfg block size */
	u16    u16_chksum; /*!< old u16 checksum */
	char        brcd_serialnum[STRSZ(BFA_MFG_SERIALNUM_SIZE)];
	char        brcd_partnum[STRSZ(BFA_MFG_PARTNUM_SIZE)];
	u8     mfg_day;    /*!< manufacturing day */
	u8     mfg_month;  /*!< manufacturing month */
	u16    mfg_year;   /*!< manufacturing year */
	wwn_t       mfg_wwn;    /*!< wwn base for this adapter */
	u8     num_wwn;    /*!< number of wwns assigned */
	u8     mfg_speeds; /*!< speeds allowed for this adapter */
	u8     rsv[2];
	char    supplier_id[STRSZ(BFA_MFG_SUPPLIER_ID_SIZE)];
	char    supplier_partnum[STRSZ(BFA_MFG_SUPPLIER_PARTNUM_SIZE)];
	char    supplier_serialnum[STRSZ(BFA_MFG_SUPPLIER_SERIALNUM_SIZE)];
	char    supplier_revision[STRSZ(BFA_MFG_SUPPLIER_REVISION_SIZE)];
	mac_t       mfg_mac;    /*!< base mac address */
	u8     num_mac;    /*!< number of mac addresses */
	u8     rsv2;
	u32    card_type;  /*!< card type          */
	char        cap_nic;    /*!< capability nic     */
	char        cap_cna;    /*!< capability cna     */
	char        cap_hba;    /*!< capability hba     */
	char        cap_fc16g;  /*!< capability fc 16g      */
	char        cap_sriov;  /*!< capability sriov       */
	char        cap_mezz;   /*!< capability mezz        */
	u8     rsv3;
	u8     mfg_nports; /*!< number of ports        */
	char        media[8];   /*!< xfi/xaui           */
	char        initial_mode[8]; /*!< initial mode: hba/cna/nic */
	u8     rsv4[84];
	u8     md5_chksum[BFA_MFG_CHKSUM_SIZE]; /*!< md5 checksum */
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
	BFA_PCI_DEVICE_ID_CT2		= 0x22,
	BFA_PCI_DEVICE_ID_CT2_QUAD	= 0x23,
};

#define bfa_asic_id_cb(__d)			\
	((__d) == BFA_PCI_DEVICE_ID_FC_8G2P ||	\
	 (__d) == BFA_PCI_DEVICE_ID_FC_8G1P)
#define bfa_asic_id_ct(__d)			\
	((__d) == BFA_PCI_DEVICE_ID_CT ||	\
	 (__d) == BFA_PCI_DEVICE_ID_CT_FC)
#define bfa_asic_id_ct2(__d)			\
	((__d) == BFA_PCI_DEVICE_ID_CT2 ||	\
	(__d) == BFA_PCI_DEVICE_ID_CT2_QUAD)
#define bfa_asic_id_ctc(__d)	\
	(bfa_asic_id_ct(__d) || bfa_asic_id_ct2(__d))

/*
 * PCI sub-system device and vendor ID information
 */
enum {
	BFA_PCI_FCOE_SSDEVICE_ID	= 0x14,
	BFA_PCI_CT2_SSID_FCoE		= 0x22,
	BFA_PCI_CT2_SSID_ETH		= 0x23,
	BFA_PCI_CT2_SSID_FC		= 0x24,
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
	BFA_PORT_SPEED_AUTO	= 0xf,
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
	wwn_t   pwwn;		/*  port wwn of target */
	struct scsi_lun   lun;  /*  64-bit lun */
};
#pragma pack()

/*
 * BOOT boot configuraton
 */
struct bfa_boot_cfg_s {
	u8		version;
	u8		rsvd1;
	u16		chksum;
	u8		enable;		/* enable/disable SAN boot */
	u8		speed;          /* boot speed settings */
	u8		topology;       /* boot topology setting */
	u8		bootopt;        /* bfa_boot_bootopt_t */
	u32		nbluns;         /* number of boot luns */
	u32		rsvd2;
	struct bfa_boot_bootlun_s blun[BFA_BOOT_BOOTLUN_MAX];
	struct bfa_boot_bootlun_s blun_disc[BFA_BOOT_BOOTLUN_MAX];
};

struct bfa_boot_pbc_s {
	u8              enable;         /*  enable/disable SAN boot */
	u8              speed;          /*  boot speed settings */
	u8              topology;       /*  boot topology setting */
	u8              rsvd1;
	u32     nbluns;         /*  number of boot luns */
	struct bfa_boot_bootlun_s pblun[BFA_PREBOOT_BOOTLUN_MAX];
};

struct bfa_ethboot_cfg_s {
	u8		version;
	u8		rsvd1;
	u16		chksum;
	u8		enable;	/* enable/disable Eth/PXE boot */
	u8		rsvd2;
	u16		vlan;
};

/*
 * ASIC block configuration related structures
 */
#define BFA_ABLK_MAX_PORTS	2
#define BFA_ABLK_MAX_PFS	16
#define BFA_ABLK_MAX		2

#pragma pack(1)
enum bfa_mode_s {
	BFA_MODE_HBA	= 1,
	BFA_MODE_CNA	= 2,
	BFA_MODE_NIC	= 3
};

struct bfa_adapter_cfg_mode_s {
	u16	max_pf;
	u16	max_vf;
	enum bfa_mode_s	mode;
};

struct bfa_ablk_cfg_pf_s {
	u16	pers;
	u8	port_id;
	u8	optrom;
	u8	valid;
	u8	sriov;
	u8	max_vfs;
	u8	rsvd[1];
	u16	num_qpairs;
	u16	num_vectors;
	u16	bw_min;
	u16	bw_max;
};

struct bfa_ablk_cfg_port_s {
	u8	mode;
	u8	type;
	u8	max_pfs;
	u8	rsvd[5];
};

struct bfa_ablk_cfg_inst_s {
	u8	nports;
	u8	max_pfs;
	u8	rsvd[6];
	struct bfa_ablk_cfg_pf_s	pf_cfg[BFA_ABLK_MAX_PFS];
	struct bfa_ablk_cfg_port_s	port_cfg[BFA_ABLK_MAX_PORTS];
};

struct bfa_ablk_cfg_s {
	struct bfa_ablk_cfg_inst_s	inst[BFA_ABLK_MAX];
};


/*
 *	SFP module specific
 */
#define SFP_DIAGMON_SIZE	10 /* num bytes of diag monitor data */

/* SFP state change notification event */
#define BFA_SFP_SCN_REMOVED	0
#define BFA_SFP_SCN_INSERTED	1
#define BFA_SFP_SCN_POM		2
#define BFA_SFP_SCN_FAILED	3
#define BFA_SFP_SCN_UNSUPPORT	4
#define BFA_SFP_SCN_VALID	5

enum bfa_defs_sfp_media_e {
	BFA_SFP_MEDIA_UNKNOWN	= 0x00,
	BFA_SFP_MEDIA_CU	= 0x01,
	BFA_SFP_MEDIA_LW	= 0x02,
	BFA_SFP_MEDIA_SW	= 0x03,
	BFA_SFP_MEDIA_EL	= 0x04,
	BFA_SFP_MEDIA_UNSUPPORT	= 0x05,
};

/*
 * values for xmtr_tech above
 */
enum {
	SFP_XMTR_TECH_CU = (1 << 0),	/* copper FC-BaseT */
	SFP_XMTR_TECH_CP = (1 << 1),	/* copper passive */
	SFP_XMTR_TECH_CA = (1 << 2),	/* copper active */
	SFP_XMTR_TECH_LL = (1 << 3),	/* longwave laser */
	SFP_XMTR_TECH_SL = (1 << 4),	/* shortwave laser w/ OFC */
	SFP_XMTR_TECH_SN = (1 << 5),	/* shortwave laser w/o OFC */
	SFP_XMTR_TECH_EL_INTRA = (1 << 6), /* elec intra-enclosure */
	SFP_XMTR_TECH_EL_INTER = (1 << 7), /* elec inter-enclosure */
	SFP_XMTR_TECH_LC = (1 << 8),	/* longwave laser */
	SFP_XMTR_TECH_SA = (1 << 9)
};

/*
 * Serial ID: Data Fields -- Address A0h
 * Basic ID field total 64 bytes
 */
struct sfp_srlid_base_s {
	u8	id;		/* 00: Identifier */
	u8	extid;		/* 01: Extended Identifier */
	u8	connector;	/* 02: Connector */
	u8	xcvr[8];	/* 03-10: Transceiver */
	u8	encoding;	/* 11: Encoding */
	u8	br_norm;	/* 12: BR, Nominal */
	u8	rate_id;	/* 13: Rate Identifier */
	u8	len_km;		/* 14: Length single mode km */
	u8	len_100m;	/* 15: Length single mode 100m */
	u8	len_om2;	/* 16: Length om2 fiber 10m */
	u8	len_om1;	/* 17: Length om1 fiber 10m */
	u8	len_cu;		/* 18: Length copper 1m */
	u8	len_om3;	/* 19: Length om3 fiber 10m */
	u8	vendor_name[16];/* 20-35 */
	u8	unalloc1;
	u8	vendor_oui[3];	/* 37-39 */
	u8	vendor_pn[16];	/* 40-55 */
	u8	vendor_rev[4];	/* 56-59 */
	u8	wavelen[2];	/* 60-61 */
	u8	unalloc2;
	u8	cc_base;	/* 63: check code for base id field */
};

/*
 * Serial ID: Data Fields -- Address A0h
 * Extended id field total 32 bytes
 */
struct sfp_srlid_ext_s {
	u8	options[2];
	u8	br_max;
	u8	br_min;
	u8	vendor_sn[16];
	u8	date_code[8];
	u8	diag_mon_type;  /* 92: Diagnostic Monitoring type */
	u8	en_options;
	u8	sff_8472;
	u8	cc_ext;
};

/*
 * Diagnostic: Data Fields -- Address A2h
 * Diagnostic and control/status base field total 96 bytes
 */
struct sfp_diag_base_s {
	/*
	 * Alarm and warning Thresholds 40 bytes
	 */
	u8	temp_high_alarm[2]; /* 00-01 */
	u8	temp_low_alarm[2];  /* 02-03 */
	u8	temp_high_warning[2];   /* 04-05 */
	u8	temp_low_warning[2];    /* 06-07 */

	u8	volt_high_alarm[2]; /* 08-09 */
	u8	volt_low_alarm[2];  /* 10-11 */
	u8	volt_high_warning[2];   /* 12-13 */
	u8	volt_low_warning[2];    /* 14-15 */

	u8	bias_high_alarm[2]; /* 16-17 */
	u8	bias_low_alarm[2];  /* 18-19 */
	u8	bias_high_warning[2];   /* 20-21 */
	u8	bias_low_warning[2];    /* 22-23 */

	u8	tx_pwr_high_alarm[2];   /* 24-25 */
	u8	tx_pwr_low_alarm[2];    /* 26-27 */
	u8	tx_pwr_high_warning[2]; /* 28-29 */
	u8	tx_pwr_low_warning[2];  /* 30-31 */

	u8	rx_pwr_high_alarm[2];   /* 32-33 */
	u8	rx_pwr_low_alarm[2];    /* 34-35 */
	u8	rx_pwr_high_warning[2]; /* 36-37 */
	u8	rx_pwr_low_warning[2];  /* 38-39 */

	u8	unallocate_1[16];

	/*
	 * ext_cal_const[36]
	 */
	u8	rx_pwr[20];
	u8	tx_i[4];
	u8	tx_pwr[4];
	u8	temp[4];
	u8	volt[4];
	u8	unallocate_2[3];
	u8	cc_dmi;
};

/*
 * Diagnostic: Data Fields -- Address A2h
 * Diagnostic and control/status extended field total 24 bytes
 */
struct sfp_diag_ext_s {
	u8	diag[SFP_DIAGMON_SIZE];
	u8	unalloc1[4];
	u8	status_ctl;
	u8	rsvd;
	u8	alarm_flags[2];
	u8	unalloc2[2];
	u8	warning_flags[2];
	u8	ext_status_ctl[2];
};

/*
 * Diagnostic: Data Fields -- Address A2h
 * General Use Fields: User Writable Table - Features's Control Registers
 * Total 32 bytes
 */
struct sfp_usr_eeprom_s {
	u8	rsvd1[2];       /* 128-129 */
	u8	ewrap;          /* 130 */
	u8	rsvd2[2];       /*  */
	u8	owrap;          /* 133 */
	u8	rsvd3[2];       /*  */
	u8	prbs;           /* 136: PRBS 7 generator */
	u8	rsvd4[2];       /*  */
	u8	tx_eqz_16;      /* 139: TX Equalizer (16xFC) */
	u8	tx_eqz_8;       /* 140: TX Equalizer (8xFC) */
	u8	rsvd5[2];       /*  */
	u8	rx_emp_16;      /* 143: RX Emphasis (16xFC) */
	u8	rx_emp_8;       /* 144: RX Emphasis (8xFC) */
	u8	rsvd6[2];       /*  */
	u8	tx_eye_adj;     /* 147: TX eye Threshold Adjust */
	u8	rsvd7[3];       /*  */
	u8	tx_eye_qctl;    /* 151: TX eye Quality Control */
	u8	tx_eye_qres;    /* 152: TX eye Quality Result */
	u8	rsvd8[2];       /*  */
	u8	poh[3];         /* 155-157: Power On Hours */
	u8	rsvd9[2];       /*  */
};

struct sfp_mem_s {
	struct sfp_srlid_base_s	srlid_base;
	struct sfp_srlid_ext_s	srlid_ext;
	struct sfp_diag_base_s	diag_base;
	struct sfp_diag_ext_s	diag_ext;
	struct sfp_usr_eeprom_s usr_eeprom;
};

/*
 * transceiver codes (SFF-8472 Rev 10.2 Table 3.5)
 */
union sfp_xcvr_e10g_code_u {
	u8		b;
	struct {
#ifdef __BIG_ENDIAN
		u8	e10g_unall:1;   /* 10G Ethernet compliance */
		u8	e10g_lrm:1;
		u8	e10g_lr:1;
		u8	e10g_sr:1;
		u8	ib_sx:1;    /* Infiniband compliance */
		u8	ib_lx:1;
		u8	ib_cu_a:1;
		u8	ib_cu_p:1;
#else
		u8	ib_cu_p:1;
		u8	ib_cu_a:1;
		u8	ib_lx:1;
		u8	ib_sx:1;    /* Infiniband compliance */
		u8	e10g_sr:1;
		u8	e10g_lr:1;
		u8	e10g_lrm:1;
		u8	e10g_unall:1;   /* 10G Ethernet compliance */
#endif
	} r;
};

union sfp_xcvr_so1_code_u {
	u8		b;
	struct {
		u8	escon:2;    /* ESCON compliance code */
		u8	oc192_reach:1;  /* SONET compliance code */
		u8	so_reach:2;
		u8	oc48_reach:3;
	} r;
};

union sfp_xcvr_so2_code_u {
	u8		b;
	struct {
		u8	reserved:1;
		u8	oc12_reach:3;   /* OC12 reach */
		u8	reserved1:1;
		u8	oc3_reach:3;    /* OC3 reach */
	} r;
};

union sfp_xcvr_eth_code_u {
	u8		b;
	struct {
		u8	base_px:1;
		u8	base_bx10:1;
		u8	e100base_fx:1;
		u8	e100base_lx:1;
		u8	e1000base_t:1;
		u8	e1000base_cx:1;
		u8	e1000base_lx:1;
		u8	e1000base_sx:1;
	} r;
};

struct sfp_xcvr_fc1_code_s {
	u8	link_len:5; /* FC link length */
	u8	xmtr_tech2:3;
	u8	xmtr_tech1:7;   /* FC transmitter technology */
	u8	reserved1:1;
};

union sfp_xcvr_fc2_code_u {
	u8		b;
	struct {
		u8	tw_media:1; /* twin axial pair (tw) */
		u8	tp_media:1; /* shielded twisted pair (sp) */
		u8	mi_media:1; /* miniature coax (mi) */
		u8	tv_media:1; /* video coax (tv) */
		u8	m6_media:1; /* multimode, 62.5m (m6) */
		u8	m5_media:1; /* multimode, 50m (m5) */
		u8	reserved:1;
		u8	sm_media:1; /* single mode (sm) */
	} r;
};

union sfp_xcvr_fc3_code_u {
	u8		b;
	struct {
#ifdef __BIG_ENDIAN
		u8	rsv4:1;
		u8	mb800:1;    /* 800 Mbytes/sec */
		u8	mb1600:1;   /* 1600 Mbytes/sec */
		u8	mb400:1;    /* 400 Mbytes/sec */
		u8	rsv2:1;
		u8	mb200:1;    /* 200 Mbytes/sec */
		u8	rsv1:1;
		u8	mb100:1;    /* 100 Mbytes/sec */
#else
		u8	mb100:1;    /* 100 Mbytes/sec */
		u8	rsv1:1;
		u8	mb200:1;    /* 200 Mbytes/sec */
		u8	rsv2:1;
		u8	mb400:1;    /* 400 Mbytes/sec */
		u8	mb1600:1;   /* 1600 Mbytes/sec */
		u8	mb800:1;    /* 800 Mbytes/sec */
		u8	rsv4:1;
#endif
	} r;
};

struct sfp_xcvr_s {
	union sfp_xcvr_e10g_code_u	e10g;
	union sfp_xcvr_so1_code_u	so1;
	union sfp_xcvr_so2_code_u	so2;
	union sfp_xcvr_eth_code_u	eth;
	struct sfp_xcvr_fc1_code_s	fc1;
	union sfp_xcvr_fc2_code_u	fc2;
	union sfp_xcvr_fc3_code_u	fc3;
};

/*
 *	Flash module specific
 */
#define BFA_FLASH_PART_ENTRY_SIZE	32	/* partition entry size */
#define BFA_FLASH_PART_MAX		32	/* maximal # of partitions */

enum bfa_flash_part_type {
	BFA_FLASH_PART_OPTROM   = 1,    /* option rom partition */
	BFA_FLASH_PART_FWIMG    = 2,    /* firmware image partition */
	BFA_FLASH_PART_FWCFG    = 3,    /* firmware tuneable config */
	BFA_FLASH_PART_DRV      = 4,    /* IOC driver config */
	BFA_FLASH_PART_BOOT     = 5,    /* boot config */
	BFA_FLASH_PART_ASIC     = 6,    /* asic bootstrap configuration */
	BFA_FLASH_PART_MFG      = 7,    /* manufacturing block partition */
	BFA_FLASH_PART_OPTROM2  = 8,    /* 2nd option rom partition */
	BFA_FLASH_PART_VPD      = 9,    /* vpd data of OEM info */
	BFA_FLASH_PART_PBC      = 10,   /* pre-boot config */
	BFA_FLASH_PART_BOOTOVL  = 11,   /* boot overlay partition */
	BFA_FLASH_PART_LOG      = 12,   /* firmware log partition */
	BFA_FLASH_PART_PXECFG   = 13,   /* pxe boot config partition */
	BFA_FLASH_PART_PXEOVL   = 14,   /* pxe boot overlay partition */
	BFA_FLASH_PART_PORTCFG  = 15,   /* port cfg partition */
	BFA_FLASH_PART_ASICBK   = 16,   /* asic backup partition */
};

/*
 * flash partition attributes
 */
struct bfa_flash_part_attr_s {
	u32	part_type;      /* partition type */
	u32	part_instance;  /* partition instance */
	u32	part_off;       /* partition offset */
	u32	part_size;      /* partition size */
	u32	part_len;       /* partition content length */
	u32	part_status;    /* partition status */
	char	rsv[BFA_FLASH_PART_ENTRY_SIZE - 24];
};

/*
 * flash attributes
 */
struct bfa_flash_attr_s {
	u32	status; /* flash overall status */
	u32	npart;  /* num of partitions */
	struct bfa_flash_part_attr_s part[BFA_FLASH_PART_MAX];
};

/*
 *	DIAG module specific
 */
#define LB_PATTERN_DEFAULT	0xB5B5B5B5
#define QTEST_CNT_DEFAULT	10
#define QTEST_PAT_DEFAULT	LB_PATTERN_DEFAULT
#define DPORT_ENABLE_LOOPCNT_DEFAULT (1024 * 1024)

struct bfa_diag_memtest_s {
	u8	algo;
	u8	rsvd[7];
};

struct bfa_diag_memtest_result {
	u32	status;
	u32	addr;
	u32	exp; /* expect value read from reg */
	u32	act; /* actually value read */
	u32	err_status;             /* error status reg */
	u32	err_status1;    /* extra error info reg */
	u32	err_addr; /* error address reg */
	u8	algo;
	u8	rsv[3];
};

struct bfa_diag_loopback_result_s {
	u32	numtxmfrm;      /* no. of transmit frame */
	u32	numosffrm;      /* no. of outstanding frame */
	u32	numrcvfrm;      /* no. of received good frame */
	u32	badfrminf;      /* mis-match info */
	u32	badfrmnum;      /* mis-match fram number */
	u8	status;         /* loopback test result */
	u8	rsvd[3];
};

enum bfa_diag_dport_test_status {
	DPORT_TEST_ST_IDLE	= 0,    /* the test has not started yet. */
	DPORT_TEST_ST_FINAL	= 1,    /* the test done successfully */
	DPORT_TEST_ST_SKIP	= 2,    /* the test skipped */
	DPORT_TEST_ST_FAIL	= 3,    /* the test failed */
	DPORT_TEST_ST_INPRG	= 4,    /* the testing is in progress */
	DPORT_TEST_ST_RESPONDER	= 5,    /* test triggered from remote port */
	DPORT_TEST_ST_STOPPED	= 6,    /* the test stopped by user. */
	DPORT_TEST_ST_MAX
};

enum bfa_diag_dport_test_type {
	DPORT_TEST_ELOOP	= 0,
	DPORT_TEST_OLOOP	= 1,
	DPORT_TEST_ROLOOP	= 2,
	DPORT_TEST_LINK		= 3,
	DPORT_TEST_MAX
};

enum bfa_diag_dport_test_opmode {
	BFA_DPORT_OPMODE_AUTO	= 0,
	BFA_DPORT_OPMODE_MANU	= 1,
};

struct bfa_diag_dport_subtest_result_s {
	u8	status;		/* bfa_diag_dport_test_status */
	u8	rsvd[7];	/* 64bit align */
	u64	start_time;	/* timestamp  */
};

struct bfa_diag_dport_result_s {
	wwn_t	rp_pwwn;	/* switch port wwn  */
	wwn_t	rp_nwwn;	/* switch node wwn  */
	u64	start_time;	/* user/sw start time */
	u64	end_time;	/* timestamp  */
	u8	status;		/* bfa_diag_dport_test_status */
	u8	mode;		/* bfa_diag_dport_test_opmode */
	u8	rsvd;		/* 64bit align */
	u8	speed;		/* link speed for buf_reqd */
	u16	buffer_required;
	u16	frmsz;		/* frame size for buf_reqd */
	u32	lpcnt;		/* Frame count */
	u32	pat;		/* Pattern */
	u32	roundtrip_latency;	/* in nano sec */
	u32	est_cable_distance;	/* in meter */
	struct bfa_diag_dport_subtest_result_s subtest[DPORT_TEST_MAX];
};

struct bfa_diag_ledtest_s {
	u32	cmd;    /* bfa_led_op_t */
	u32	color;  /* bfa_led_color_t */
	u16	freq;   /* no. of blinks every 10 secs */
	u8	led;    /* bitmap of LEDs to be tested */
	u8	rsvd[5];
};

struct bfa_diag_loopback_s {
	u32	loopcnt;
	u32	pattern;
	u8	lb_mode;    /* bfa_port_opmode_t */
	u8	speed;      /* bfa_port_speed_t */
	u8	rsvd[2];
};

/*
 *	PHY module specific
 */
enum bfa_phy_status_e {
	BFA_PHY_STATUS_GOOD	= 0, /* phy is good */
	BFA_PHY_STATUS_NOT_PRESENT	= 1, /* phy does not exist */
	BFA_PHY_STATUS_BAD	= 2, /* phy is bad */
};

/*
 * phy attributes for phy query
 */
struct bfa_phy_attr_s {
	u32	status;         /* phy present/absent status */
	u32	length;         /* firmware length */
	u32	fw_ver;         /* firmware version */
	u32	an_status;      /* AN status */
	u32	pma_pmd_status; /* PMA/PMD link status */
	u32	pma_pmd_signal; /* PMA/PMD signal detect */
	u32	pcs_status;     /* PCS link status */
};

/*
 * phy stats
 */
struct bfa_phy_stats_s {
	u32	status;         /* phy stats status */
	u32	link_breaks;    /* Num of link breaks after linkup */
	u32	pma_pmd_fault;  /* NPMA/PMD fault */
	u32	pcs_fault;      /* PCS fault */
	u32	speed_neg;      /* Num of speed negotiation */
	u32	tx_eq_training; /* Num of TX EQ training */
	u32	tx_eq_timeout;  /* Num of TX EQ timeout */
	u32	crc_error;      /* Num of CRC errors */
};

#pragma pack()

#endif /* __BFA_DEFS_H__ */
