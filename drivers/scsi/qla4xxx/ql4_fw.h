/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * QLogic iSCSI HBA Driver
 * Copyright (c)  2003-2013 QLogic Corporation
 */

#ifndef _QLA4X_FW_H
#define _QLA4X_FW_H


#define MAX_PRST_DEV_DB_ENTRIES		64
#define MIN_DISC_DEV_DB_ENTRY		MAX_PRST_DEV_DB_ENTRIES
#define MAX_DEV_DB_ENTRIES		512
#define MAX_DEV_DB_ENTRIES_40XX		256

/*************************************************************************
 *
 *		ISP 4010 I/O Register Set Structure and Definitions
 *
 *************************************************************************/

struct port_ctrl_stat_regs {
	__le32 ext_hw_conf;	/* 0x50  R/W */
	__le32 rsrvd0;		/* 0x54 */
	__le32 port_ctrl;	/* 0x58 */
	__le32 port_status;	/* 0x5c */
	__le32 rsrvd1[32];	/* 0x60-0xdf */
	__le32 gp_out;		/* 0xe0 */
	__le32 gp_in;		/* 0xe4 */
	__le32 rsrvd2[5];	/* 0xe8-0xfb */
	__le32 port_err_status; /* 0xfc */
};

struct host_mem_cfg_regs {
	__le32 rsrvd0[12];	/* 0x50-0x79 */
	__le32 req_q_out;	/* 0x80 */
	__le32 rsrvd1[31];	/* 0x84-0xFF */
};

/*
 * ISP 82xx I/O Register Set structure definitions.
 */
struct device_reg_82xx {
	__le32 req_q_out;	/* 0x0000 (R): Request Queue out-Pointer. */
	__le32 reserve1[63];	/* Request Queue out-Pointer. (64 * 4) */
	__le32 rsp_q_in;	/* 0x0100 (R/W): Response Queue In-Pointer. */
	__le32 reserve2[63];	/* Response Queue In-Pointer. */
	__le32 rsp_q_out;	/* 0x0200 (R/W): Response Queue Out-Pointer. */
	__le32 reserve3[63];	/* Response Queue Out-Pointer. */

	__le32 mailbox_in[8];	/* 0x0300 (R/W): Mail box In registers */
	__le32 reserve4[24];
	__le32 hint;		/* 0x0380 (R/W): Host interrupt register */
#define HINT_MBX_INT_PENDING	BIT_0
	__le32 reserve5[31];
	__le32 mailbox_out[8];	/* 0x0400 (R): Mail box Out registers */
	__le32 reserve6[56];

	__le32 host_status;	/* Offset 0x500 (R): host status */
#define HSRX_RISC_MB_INT	BIT_0  /* RISC to Host Mailbox interrupt */
#define HSRX_RISC_IOCB_INT	BIT_1  /* RISC to Host IOCB interrupt */

	__le32 host_int;	/* Offset 0x0504 (R/W): Interrupt status. */
#define ISRX_82XX_RISC_INT	BIT_0 /* RISC interrupt. */
};

/* ISP 83xx I/O Register Set structure */
struct device_reg_83xx {
	__le32 mailbox_in[16];	/* 0x0000 */
	__le32 reserve1[496];	/* 0x0040 */
	__le32 mailbox_out[16];	/* 0x0800 */
	__le32 reserve2[496];
	__le32 mbox_int;	/* 0x1000 */
	__le32 reserve3[63];
	__le32 req_q_out;	/* 0x1100 */
	__le32 reserve4[63];

	__le32 rsp_q_in;	/* 0x1200 */
	__le32 reserve5[1919];

	__le32 req_q_in;	/* 0x3000 */
	__le32 reserve6[3];
	__le32 iocb_int_mask;	/* 0x3010 */
	__le32 reserve7[3];
	__le32 rsp_q_out;	/* 0x3020 */
	__le32 reserve8[3];
	__le32 anonymousbuff;	/* 0x3030 */
	__le32 mb_int_mask;	/* 0x3034 */

	__le32 host_intr;	/* 0x3038 - Host Interrupt Register */
	__le32 risc_intr;	/* 0x303C - RISC Interrupt Register */
	__le32 reserve9[544];
	__le32 leg_int_ptr;	/* 0x38C0 - Legacy Interrupt Pointer Register */
	__le32 leg_int_trig;	/* 0x38C4 - Legacy Interrupt Trigger Control */
	__le32 leg_int_mask;	/* 0x38C8 - Legacy Interrupt Mask Register */
};

#define INT_ENABLE_FW_MB	(1 << 2)
#define INT_MASK_FW_MB		(1 << 2)

/*  remote register set (access via PCI memory read/write) */
struct isp_reg {
#define MBOX_REG_COUNT 8
	__le32 mailbox[MBOX_REG_COUNT];

	__le32 flash_address;	/* 0x20 */
	__le32 flash_data;
	__le32 ctrl_status;

	union {
		struct {
			__le32 nvram;
			__le32 reserved1[2]; /* 0x30 */
		} __attribute__ ((packed)) isp4010;
		struct {
			__le32 intr_mask;
			__le32 nvram; /* 0x30 */
			__le32 semaphore;
		} __attribute__ ((packed)) isp4022;
	} u1;

	__le32 req_q_in;    /* SCSI Request Queue Producer Index */
	__le32 rsp_q_out;   /* SCSI Completion Queue Consumer Index */

	__le32 reserved2[4];	/* 0x40 */

	union {
		struct {
			__le32 ext_hw_conf; /* 0x50 */
			__le32 flow_ctrl;
			__le32 port_ctrl;
			__le32 port_status;

			__le32 reserved3[8]; /* 0x60 */

			__le32 req_q_out; /* 0x80 */

			__le32 reserved4[23]; /* 0x84 */

			__le32 gp_out; /* 0xe0 */
			__le32 gp_in;

			__le32 reserved5[5];

			__le32 port_err_status; /* 0xfc */
		} __attribute__ ((packed)) isp4010;
		struct {
			union {
				struct port_ctrl_stat_regs p0;
				struct host_mem_cfg_regs p1;
			};
		} __attribute__ ((packed)) isp4022;
	} u2;
};				/* 256 x100 */


/* Semaphore Defines for 4010 */
#define QL4010_DRVR_SEM_BITS	0x00000030
#define QL4010_GPIO_SEM_BITS	0x000000c0
#define QL4010_SDRAM_SEM_BITS	0x00000300
#define QL4010_PHY_SEM_BITS	0x00000c00
#define QL4010_NVRAM_SEM_BITS	0x00003000
#define QL4010_FLASH_SEM_BITS	0x0000c000

#define QL4010_DRVR_SEM_MASK	0x00300000
#define QL4010_GPIO_SEM_MASK	0x00c00000
#define QL4010_SDRAM_SEM_MASK	0x03000000
#define QL4010_PHY_SEM_MASK	0x0c000000
#define QL4010_NVRAM_SEM_MASK	0x30000000
#define QL4010_FLASH_SEM_MASK	0xc0000000

/* Semaphore Defines for 4022 */
#define QL4022_RESOURCE_MASK_BASE_CODE 0x7
#define QL4022_RESOURCE_BITS_BASE_CODE 0x4


#define QL4022_DRVR_SEM_MASK	(QL4022_RESOURCE_MASK_BASE_CODE << (1+16))
#define QL4022_DDR_RAM_SEM_MASK (QL4022_RESOURCE_MASK_BASE_CODE << (4+16))
#define QL4022_PHY_GIO_SEM_MASK (QL4022_RESOURCE_MASK_BASE_CODE << (7+16))
#define QL4022_NVRAM_SEM_MASK	(QL4022_RESOURCE_MASK_BASE_CODE << (10+16))
#define QL4022_FLASH_SEM_MASK	(QL4022_RESOURCE_MASK_BASE_CODE << (13+16))

/* nvram address for 4032 */
#define NVRAM_PORT0_BOOT_MODE		0x03b1
#define NVRAM_PORT0_BOOT_PRI_TGT	0x03b2
#define NVRAM_PORT0_BOOT_SEC_TGT	0x03bb
#define NVRAM_PORT1_BOOT_MODE		0x07b1
#define NVRAM_PORT1_BOOT_PRI_TGT	0x07b2
#define NVRAM_PORT1_BOOT_SEC_TGT	0x07bb


/* Page # defines for 4022 */
#define PORT_CTRL_STAT_PAGE			0	/* 4022 */
#define HOST_MEM_CFG_PAGE			1	/* 4022 */
#define LOCAL_RAM_CFG_PAGE			2	/* 4022 */
#define PROT_STAT_PAGE				3	/* 4022 */

/* Register Mask - sets corresponding mask bits in the upper word */
static inline uint32_t set_rmask(uint32_t val)
{
	return (val & 0xffff) | (val << 16);
}


static inline uint32_t clr_rmask(uint32_t val)
{
	return 0 | (val << 16);
}

/*  ctrl_status definitions */
#define CSR_SCSI_PAGE_SELECT			0x00000003
#define CSR_SCSI_INTR_ENABLE			0x00000004	/* 4010 */
#define CSR_SCSI_RESET_INTR			0x00000008
#define CSR_SCSI_COMPLETION_INTR		0x00000010
#define CSR_SCSI_PROCESSOR_INTR			0x00000020
#define CSR_INTR_RISC				0x00000040
#define CSR_BOOT_ENABLE				0x00000080
#define CSR_NET_PAGE_SELECT			0x00000300	/* 4010 */
#define CSR_FUNC_NUM				0x00000700	/* 4022 */
#define CSR_NET_RESET_INTR			0x00000800	/* 4010 */
#define CSR_FORCE_SOFT_RESET			0x00002000	/* 4022 */
#define CSR_FATAL_ERROR				0x00004000
#define CSR_SOFT_RESET				0x00008000
#define ISP_CONTROL_FN_MASK			CSR_FUNC_NUM
#define ISP_CONTROL_FN0_SCSI			0x0500
#define ISP_CONTROL_FN1_SCSI			0x0700

#define INTR_PENDING				(CSR_SCSI_COMPLETION_INTR |\
						 CSR_SCSI_PROCESSOR_INTR |\
						 CSR_SCSI_RESET_INTR)

/* ISP InterruptMask definitions */
#define IMR_SCSI_INTR_ENABLE			0x00000004	/* 4022 */

/* ISP 4022 nvram definitions */
#define NVR_WRITE_ENABLE			0x00000010	/* 4022 */

#define QL4010_NVRAM_SIZE			0x200
#define QL40X2_NVRAM_SIZE			0x800

/*  ISP port_status definitions */

/*  ISP Semaphore definitions */

/*  ISP General Purpose Output definitions */
#define GPOR_TOPCAT_RESET			0x00000004

/*  shadow registers (DMA'd from HA to system memory.  read only) */
struct shadow_regs {
	/* SCSI Request Queue Consumer Index */
	__le32 req_q_out;	/*  0 x0   R */

	/* SCSI Completion Queue Producer Index */
	__le32 rsp_q_in;	/*  4 x4   R */
};		  /*  8 x8 */


/*  External hardware configuration register */
union external_hw_config_reg {
	struct {
		/* FIXME: Do we even need this?	 All values are
		 * referred to by 16 bit quantities.  Platform and
		 * endianess issues. */
		__le32 bReserved0:1;
		__le32 bSDRAMProtectionMethod:2;
		__le32 bSDRAMBanks:1;
		__le32 bSDRAMChipWidth:1;
		__le32 bSDRAMChipSize:2;
		__le32 bParityDisable:1;
		__le32 bExternalMemoryType:1;
		__le32 bFlashBIOSWriteEnable:1;
		__le32 bFlashUpperBankSelect:1;
		__le32 bWriteBurst:2;
		__le32 bReserved1:3;
		__le32 bMask:16;
	};
	uint32_t Asuint32_t;
};

/* 82XX Support  start */
/* 82xx Default FLT Addresses */
#define FA_FLASH_LAYOUT_ADDR_82		0xFC400
#define FA_FLASH_DESCR_ADDR_82		0xFC000
#define FA_BOOT_LOAD_ADDR_82		0x04000
#define FA_BOOT_CODE_ADDR_82		0x20000
#define FA_RISC_CODE_ADDR_82		0x40000
#define FA_GOLD_RISC_CODE_ADDR_82	0x80000
#define FA_FLASH_ISCSI_CHAP		0x540000
#define FA_FLASH_CHAP_SIZE		0xC0000
#define FA_FLASH_ISCSI_DDB		0x420000
#define FA_FLASH_DDB_SIZE		0x080000

/* Flash Description Table */
struct qla_fdt_layout {
	uint8_t sig[4];
	uint16_t version;
	uint16_t len;
	uint16_t checksum;
	uint8_t unused1[2];
	uint8_t model[16];
	uint16_t man_id;
	uint16_t id;
	uint8_t flags;
	uint8_t erase_cmd;
	uint8_t alt_erase_cmd;
	uint8_t wrt_enable_cmd;
	uint8_t wrt_enable_bits;
	uint8_t wrt_sts_reg_cmd;
	uint8_t unprotect_sec_cmd;
	uint8_t read_man_id_cmd;
	uint32_t block_size;
	uint32_t alt_block_size;
	uint32_t flash_size;
	uint32_t wrt_enable_data;
	uint8_t read_id_addr_len;
	uint8_t wrt_disable_bits;
	uint8_t read_dev_id_len;
	uint8_t chip_erase_cmd;
	uint16_t read_timeout;
	uint8_t protect_sec_cmd;
	uint8_t unused2[65];
};

/* Flash Layout Table */

struct qla_flt_location {
	uint8_t sig[4];
	uint16_t start_lo;
	uint16_t start_hi;
	uint8_t version;
	uint8_t unused[5];
	uint16_t checksum;
};

struct qla_flt_header {
	uint16_t version;
	uint16_t length;
	uint16_t checksum;
	uint16_t unused;
};

/* 82xx FLT Regions */
#define FLT_REG_FDT		0x1a
#define FLT_REG_FLT		0x1c
#define FLT_REG_BOOTLOAD_82	0x72
#define FLT_REG_FW_82		0x74
#define FLT_REG_FW_82_1		0x97
#define FLT_REG_GOLD_FW_82	0x75
#define FLT_REG_BOOT_CODE_82	0x78
#define FLT_REG_ISCSI_PARAM	0x65
#define FLT_REG_ISCSI_CHAP	0x63
#define FLT_REG_ISCSI_DDB	0x6A

struct qla_flt_region {
	uint32_t code;
	uint32_t size;
	uint32_t start;
	uint32_t end;
};

/*************************************************************************
 *
 *		Mailbox Commands Structures and Definitions
 *
 *************************************************************************/

/*  Mailbox command definitions */
#define MBOX_CMD_ABOUT_FW			0x0009
#define MBOX_CMD_PING				0x000B
#define PING_IPV6_PROTOCOL_ENABLE		0x1
#define PING_IPV6_LINKLOCAL_ADDR		0x4
#define PING_IPV6_ADDR0				0x8
#define PING_IPV6_ADDR1				0xC
#define MBOX_CMD_ENABLE_INTRS			0x0010
#define INTR_DISABLE				0
#define INTR_ENABLE				1
#define MBOX_CMD_STOP_FW			0x0014
#define MBOX_CMD_ABORT_TASK			0x0015
#define MBOX_CMD_LUN_RESET			0x0016
#define MBOX_CMD_TARGET_WARM_RESET		0x0017
#define MBOX_CMD_GET_MANAGEMENT_DATA		0x001E
#define MBOX_CMD_GET_FW_STATUS			0x001F
#define MBOX_CMD_SET_ISNS_SERVICE		0x0021
#define ISNS_DISABLE				0
#define ISNS_ENABLE				1
#define MBOX_CMD_COPY_FLASH			0x0024
#define MBOX_CMD_WRITE_FLASH			0x0025
#define MBOX_CMD_READ_FLASH			0x0026
#define MBOX_CMD_CLEAR_DATABASE_ENTRY		0x0031
#define MBOX_CMD_CONN_OPEN			0x0074
#define MBOX_CMD_CONN_CLOSE_SESS_LOGOUT		0x0056
#define DDB_NOT_LOGGED_IN			0x09
#define LOGOUT_OPTION_CLOSE_SESSION		0x0002
#define LOGOUT_OPTION_RELOGIN			0x0004
#define LOGOUT_OPTION_FREE_DDB			0x0008
#define MBOX_CMD_SET_PARAM			0x0059
#define SET_DRVR_VERSION			0x200
#define MAX_DRVR_VER_LEN			24
#define MBOX_CMD_EXECUTE_IOCB_A64		0x005A
#define MBOX_CMD_INITIALIZE_FIRMWARE		0x0060
#define MBOX_CMD_GET_INIT_FW_CTRL_BLOCK		0x0061
#define MBOX_CMD_REQUEST_DATABASE_ENTRY		0x0062
#define MBOX_CMD_SET_DATABASE_ENTRY		0x0063
#define MBOX_CMD_GET_DATABASE_ENTRY		0x0064
#define DDB_DS_UNASSIGNED			0x00
#define DDB_DS_NO_CONNECTION_ACTIVE		0x01
#define DDB_DS_DISCOVERY			0x02
#define DDB_DS_SESSION_ACTIVE			0x04
#define DDB_DS_SESSION_FAILED			0x06
#define DDB_DS_LOGIN_IN_PROCESS			0x07
#define MBOX_CMD_GET_FW_STATE			0x0069
#define MBOX_CMD_GET_INIT_FW_CTRL_BLOCK_DEFAULTS 0x006A
#define MBOX_CMD_DIAG_TEST			0x0075
#define MBOX_CMD_GET_SYS_INFO			0x0078
#define MBOX_CMD_GET_NVRAM			0x0078	/* For 40xx */
#define MBOX_CMD_SET_NVRAM			0x0079	/* For 40xx */
#define MBOX_CMD_RESTORE_FACTORY_DEFAULTS	0x0087
#define MBOX_CMD_SET_ACB			0x0088
#define MBOX_CMD_GET_ACB			0x0089
#define MBOX_CMD_DISABLE_ACB			0x008A
#define MBOX_CMD_GET_IPV6_NEIGHBOR_CACHE	0x008B
#define MBOX_CMD_GET_IPV6_DEST_CACHE		0x008C
#define MBOX_CMD_GET_IPV6_DEF_ROUTER_LIST	0x008D
#define MBOX_CMD_GET_IPV6_LCL_PREFIX_LIST	0x008E
#define MBOX_CMD_SET_IPV6_NEIGHBOR_CACHE	0x0090
#define MBOX_CMD_GET_IP_ADDR_STATE		0x0091
#define MBOX_CMD_SEND_IPV6_ROUTER_SOL		0x0092
#define MBOX_CMD_GET_DB_ENTRY_CURRENT_IP_ADDR	0x0093
#define MBOX_CMD_SET_PORT_CONFIG		0x0122
#define MBOX_CMD_GET_PORT_CONFIG		0x0123
#define MBOX_CMD_SET_LED_CONFIG			0x0125
#define MBOX_CMD_GET_LED_CONFIG			0x0126
#define MBOX_CMD_MINIDUMP			0x0129

/* Port Config */
#define ENABLE_INTERNAL_LOOPBACK		0x04
#define ENABLE_EXTERNAL_LOOPBACK		0x08
#define ENABLE_DCBX				0x10

/* Minidump subcommand */
#define MINIDUMP_GET_SIZE_SUBCOMMAND		0x00
#define MINIDUMP_GET_TMPLT_SUBCOMMAND		0x01

/* Mailbox 1 */
#define FW_STATE_READY				0x0000
#define FW_STATE_CONFIG_WAIT			0x0001
#define FW_STATE_WAIT_AUTOCONNECT		0x0002
#define FW_STATE_ERROR				0x0004
#define FW_STATE_CONFIGURING_IP			0x0008

/* Mailbox 3 */
#define FW_ADDSTATE_OPTICAL_MEDIA		0x0001
#define FW_ADDSTATE_DHCPv4_ENABLED		0x0002
#define FW_ADDSTATE_DHCPv4_LEASE_ACQUIRED	0x0004
#define FW_ADDSTATE_DHCPv4_LEASE_EXPIRED	0x0008
#define FW_ADDSTATE_LINK_UP			0x0010
#define FW_ADDSTATE_ISNS_SVC_ENABLED		0x0020
#define FW_ADDSTATE_LINK_SPEED_10MBPS		0x0100
#define FW_ADDSTATE_LINK_SPEED_100MBPS		0x0200
#define FW_ADDSTATE_LINK_SPEED_1GBPS		0x0400
#define FW_ADDSTATE_LINK_SPEED_10GBPS		0x0800

#define MBOX_CMD_GET_DATABASE_ENTRY_DEFAULTS	0x006B
#define IPV6_DEFAULT_DDB_ENTRY			0x0001

#define MBOX_CMD_CONN_OPEN_SESS_LOGIN		0x0074
#define MBOX_CMD_GET_CRASH_RECORD		0x0076	/* 4010 only */
#define MBOX_CMD_GET_CONN_EVENT_LOG		0x0077

#define MBOX_CMD_IDC_ACK			0x0101
#define MBOX_CMD_IDC_TIME_EXTEND		0x0102
#define MBOX_CMD_PORT_RESET			0x0120
#define MBOX_CMD_SET_PORT_CONFIG		0x0122

/*  Mailbox status definitions */
#define MBOX_COMPLETION_STATUS			4
#define MBOX_STS_BUSY				0x0007
#define MBOX_STS_INTERMEDIATE_COMPLETION	0x1000
#define MBOX_STS_COMMAND_COMPLETE		0x4000
#define MBOX_STS_COMMAND_ERROR			0x4005

#define MBOX_ASYNC_EVENT_STATUS			8
#define MBOX_ASTS_SYSTEM_ERROR			0x8002
#define MBOX_ASTS_REQUEST_TRANSFER_ERROR	0x8003
#define MBOX_ASTS_RESPONSE_TRANSFER_ERROR	0x8004
#define MBOX_ASTS_PROTOCOL_STATISTIC_ALARM	0x8005
#define MBOX_ASTS_SCSI_COMMAND_PDU_REJECTED	0x8006
#define MBOX_ASTS_LINK_UP			0x8010
#define MBOX_ASTS_LINK_DOWN			0x8011
#define MBOX_ASTS_DATABASE_CHANGED		0x8014
#define MBOX_ASTS_UNSOLICITED_PDU_RECEIVED	0x8015
#define MBOX_ASTS_SELF_TEST_FAILED		0x8016
#define MBOX_ASTS_LOGIN_FAILED			0x8017
#define MBOX_ASTS_DNS				0x8018
#define MBOX_ASTS_HEARTBEAT			0x8019
#define MBOX_ASTS_NVRAM_INVALID			0x801A
#define MBOX_ASTS_MAC_ADDRESS_CHANGED		0x801B
#define MBOX_ASTS_IP_ADDRESS_CHANGED		0x801C
#define MBOX_ASTS_DHCP_LEASE_EXPIRED		0x801D
#define MBOX_ASTS_DHCP_LEASE_ACQUIRED		0x801F
#define MBOX_ASTS_ISNS_UNSOLICITED_PDU_RECEIVED 0x8021
#define MBOX_ASTS_DUPLICATE_IP			0x8025
#define MBOX_ASTS_ARP_COMPLETE			0x8026
#define MBOX_ASTS_SUBNET_STATE_CHANGE		0x8027
#define MBOX_ASTS_RESPONSE_QUEUE_FULL		0x8028
#define MBOX_ASTS_IP_ADDR_STATE_CHANGED		0x8029
#define MBOX_ASTS_IPV6_DEFAULT_ROUTER_CHANGED	0x802A
#define MBOX_ASTS_IPV6_LINK_MTU_CHANGE		0x802B
#define MBOX_ASTS_IPV6_AUTO_PREFIX_IGNORED	0x802C
#define MBOX_ASTS_IPV6_ND_LOCAL_PREFIX_IGNORED	0x802D
#define MBOX_ASTS_ICMPV6_ERROR_MSG_RCVD		0x802E
#define MBOX_ASTS_INITIALIZATION_FAILED		0x8031
#define MBOX_ASTS_SYSTEM_WARNING_EVENT		0x8036
#define MBOX_ASTS_IDC_COMPLETE			0x8100
#define MBOX_ASTS_IDC_REQUEST_NOTIFICATION	0x8101
#define MBOX_ASTS_IDC_TIME_EXTEND_NOTIFICATION	0x8102
#define MBOX_ASTS_DCBX_CONF_CHANGE		0x8110
#define MBOX_ASTS_TXSCVR_INSERTED		0x8130
#define MBOX_ASTS_TXSCVR_REMOVED		0x8131

#define ISNS_EVENT_DATA_RECEIVED		0x0000
#define ISNS_EVENT_CONNECTION_OPENED		0x0001
#define ISNS_EVENT_CONNECTION_FAILED		0x0002
#define MBOX_ASTS_IPSEC_SYSTEM_FATAL_ERROR	0x8022
#define MBOX_ASTS_SUBNET_STATE_CHANGE		0x8027

/* ACB Configuration Defines */
#define ACB_CONFIG_DISABLE		0x00
#define ACB_CONFIG_SET			0x01

/* ACB/IP Address State Defines */
#define IP_ADDRSTATE_UNCONFIGURED	0
#define IP_ADDRSTATE_INVALID		1
#define IP_ADDRSTATE_ACQUIRING		2
#define IP_ADDRSTATE_TENTATIVE		3
#define IP_ADDRSTATE_DEPRICATED		4
#define IP_ADDRSTATE_PREFERRED		5
#define IP_ADDRSTATE_DISABLING		6

/* FLASH offsets */
#define FLASH_SEGMENT_IFCB	0x04000000

#define FLASH_OPT_RMW_HOLD	0
#define FLASH_OPT_RMW_INIT	1
#define FLASH_OPT_COMMIT	2
#define FLASH_OPT_RMW_COMMIT	3

/* generic defines to enable/disable params */
#define QL4_PARAM_DISABLE	0
#define QL4_PARAM_ENABLE	1

/*************************************************************************/

/* Host Adapter Initialization Control Block (from host) */
struct addr_ctrl_blk {
	uint8_t version;	/* 00 */
#define  IFCB_VER_MIN			0x01
#define  IFCB_VER_MAX			0x02
	uint8_t control;	/* 01 */
#define	 CTRLOPT_NEW_CONN_DISABLE	0x0002

	uint16_t fw_options;	/* 02-03 */
#define	 FWOPT_HEARTBEAT_ENABLE		  0x1000
#define	 FWOPT_SESSION_MODE		  0x0040
#define	 FWOPT_INITIATOR_MODE		  0x0020
#define	 FWOPT_TARGET_MODE		  0x0010
#define	 FWOPT_ENABLE_CRBDB		  0x8000

	uint16_t exec_throttle;	/* 04-05 */
	uint8_t zio_count;	/* 06 */
	uint8_t res0;	/* 07 */
	uint16_t eth_mtu_size;	/* 08-09 */
	uint16_t add_fw_options;	/* 0A-0B */
#define ADFWOPT_SERIALIZE_TASK_MGMT	0x0400
#define ADFWOPT_AUTOCONN_DISABLE	0x0002

	uint8_t hb_interval;	/* 0C */
	uint8_t inst_num; /* 0D */
	uint16_t res1;		/* 0E-0F */
	uint16_t rqq_consumer_idx;	/* 10-11 */
	uint16_t compq_producer_idx;	/* 12-13 */
	uint16_t rqq_len;	/* 14-15 */
	uint16_t compq_len;	/* 16-17 */
	uint32_t rqq_addr_lo;	/* 18-1B */
	uint32_t rqq_addr_hi;	/* 1C-1F */
	uint32_t compq_addr_lo;	/* 20-23 */
	uint32_t compq_addr_hi;	/* 24-27 */
	uint32_t shdwreg_addr_lo;	/* 28-2B */
	uint32_t shdwreg_addr_hi;	/* 2C-2F */

	uint16_t iscsi_opts;	/* 30-31 */
#define ISCSIOPTS_HEADER_DIGEST_EN		0x2000
#define ISCSIOPTS_DATA_DIGEST_EN		0x1000
#define ISCSIOPTS_IMMEDIATE_DATA_EN		0x0800
#define ISCSIOPTS_INITIAL_R2T_EN		0x0400
#define ISCSIOPTS_DATA_SEQ_INORDER_EN		0x0200
#define ISCSIOPTS_DATA_PDU_INORDER_EN		0x0100
#define ISCSIOPTS_CHAP_AUTH_EN			0x0080
#define ISCSIOPTS_SNACK_EN			0x0040
#define ISCSIOPTS_DISCOVERY_LOGOUT_EN		0x0020
#define ISCSIOPTS_BIDI_CHAP_EN			0x0010
#define ISCSIOPTS_DISCOVERY_AUTH_EN		0x0008
#define ISCSIOPTS_STRICT_LOGIN_COMP_EN		0x0004
#define ISCSIOPTS_ERL				0x0003
	uint16_t ipv4_tcp_opts;	/* 32-33 */
#define TCPOPT_DELAYED_ACK_DISABLE	0x8000
#define TCPOPT_DHCP_ENABLE		0x0200
#define TCPOPT_DNS_SERVER_IP_EN		0x0100
#define TCPOPT_SLP_DA_INFO_EN		0x0080
#define TCPOPT_NAGLE_ALGO_DISABLE	0x0020
#define TCPOPT_WINDOW_SCALE_DISABLE	0x0010
#define TCPOPT_TIMER_SCALE		0x000E
#define TCPOPT_TIMESTAMP_ENABLE		0x0001
	uint16_t ipv4_ip_opts;	/* 34-35 */
#define IPOPT_IPV4_PROTOCOL_ENABLE	0x8000
#define IPOPT_IPV4_TOS_EN		0x4000
#define IPOPT_VLAN_TAGGING_ENABLE	0x2000
#define IPOPT_GRAT_ARP_EN		0x1000
#define IPOPT_ALT_CID_EN		0x0800
#define IPOPT_REQ_VID_EN		0x0400
#define IPOPT_USE_VID_EN		0x0200
#define IPOPT_LEARN_IQN_EN		0x0100
#define IPOPT_FRAGMENTATION_DISABLE	0x0010
#define IPOPT_IN_FORWARD_EN		0x0008
#define IPOPT_ARP_REDIRECT_EN		0x0004

	uint16_t iscsi_max_pdu_size;	/* 36-37 */
	uint8_t ipv4_tos;	/* 38 */
	uint8_t ipv4_ttl;	/* 39 */
	uint8_t acb_version;	/* 3A */
#define ACB_NOT_SUPPORTED		0x00
#define ACB_SUPPORTED			0x02 /* Capable of ACB Version 2
						Features */

	uint8_t res2;	/* 3B */
	uint16_t def_timeout;	/* 3C-3D */
	uint16_t iscsi_fburst_len;	/* 3E-3F */
	uint16_t iscsi_def_time2wait;	/* 40-41 */
	uint16_t iscsi_def_time2retain;	/* 42-43 */
	uint16_t iscsi_max_outstnd_r2t;	/* 44-45 */
	uint16_t conn_ka_timeout;	/* 46-47 */
	uint16_t ipv4_port;	/* 48-49 */
	uint16_t iscsi_max_burst_len;	/* 4A-4B */
	uint32_t res5;		/* 4C-4F */
	uint8_t ipv4_addr[4];	/* 50-53 */
	uint16_t ipv4_vlan_tag;	/* 54-55 */
	uint8_t ipv4_addr_state;	/* 56 */
	uint8_t ipv4_cacheid;	/* 57 */
	uint8_t res6[8];	/* 58-5F */
	uint8_t ipv4_subnet[4];	/* 60-63 */
	uint8_t res7[12];	/* 64-6F */
	uint8_t ipv4_gw_addr[4];	/* 70-73 */
	uint8_t res8[0xc];	/* 74-7F */
	uint8_t pri_dns_srvr_ip[4];/* 80-83 */
	uint8_t sec_dns_srvr_ip[4];/* 84-87 */
	uint16_t min_eph_port;	/* 88-89 */
	uint16_t max_eph_port;	/* 8A-8B */
	uint8_t res9[4];	/* 8C-8F */
	uint8_t iscsi_alias[32];/* 90-AF */
	uint8_t res9_1[0x16];	/* B0-C5 */
	uint16_t tgt_portal_grp;/* C6-C7 */
	uint8_t abort_timer;	/* C8	 */
	uint8_t ipv4_tcp_wsf;	/* C9	 */
	uint8_t res10[6];	/* CA-CF */
	uint8_t ipv4_sec_ip_addr[4];	/* D0-D3 */
	uint8_t ipv4_dhcp_vid_len;	/* D4 */
	uint8_t ipv4_dhcp_vid[11];	/* D5-DF */
	uint8_t res11[20];	/* E0-F3 */
	uint8_t ipv4_dhcp_alt_cid_len;	/* F4 */
	uint8_t ipv4_dhcp_alt_cid[11];	/* F5-FF */
	uint8_t iscsi_name[224];	/* 100-1DF */
	uint8_t res12[32];	/* 1E0-1FF */
	uint32_t cookie;	/* 200-203 */
	uint16_t ipv6_port;	/* 204-205 */
	uint16_t ipv6_opts;	/* 206-207 */
#define IPV6_OPT_IPV6_PROTOCOL_ENABLE		0x8000
#define IPV6_OPT_VLAN_TAGGING_ENABLE		0x2000
#define IPV6_OPT_GRAT_NEIGHBOR_ADV_EN		0x1000
#define IPV6_OPT_REDIRECT_EN			0x0004

	uint16_t ipv6_addtl_opts;	/* 208-209 */
#define IPV6_ADDOPT_IGNORE_ICMP_ECHO_REQ		0x0040
#define IPV6_ADDOPT_MLD_EN				0x0004
#define IPV6_ADDOPT_NEIGHBOR_DISCOVERY_ADDR_ENABLE	0x0002 /* Pri ACB
								  Only */
#define IPV6_ADDOPT_AUTOCONFIG_LINK_LOCAL_ADDR		0x0001

	uint16_t ipv6_tcp_opts;	/* 20A-20B */
#define IPV6_TCPOPT_DELAYED_ACK_DISABLE		0x8000
#define IPV6_TCPOPT_NAGLE_ALGO_DISABLE		0x0020
#define IPV6_TCPOPT_WINDOW_SCALE_DISABLE	0x0010
#define IPV6_TCPOPT_TIMER_SCALE			0x000E
#define IPV6_TCPOPT_TIMESTAMP_EN		0x0001
	uint8_t ipv6_tcp_wsf;	/* 20C */
	uint16_t ipv6_flow_lbl;	/* 20D-20F */
	uint8_t ipv6_dflt_rtr_addr[16]; /* 210-21F */
	uint16_t ipv6_vlan_tag;	/* 220-221 */
	uint8_t ipv6_lnk_lcl_addr_state;/* 222 */
	uint8_t ipv6_addr0_state;	/* 223 */
	uint8_t ipv6_addr1_state;	/* 224 */
	uint8_t ipv6_dflt_rtr_state;    /* 225 */
#define IPV6_RTRSTATE_UNKNOWN                   0
#define IPV6_RTRSTATE_MANUAL                    1
#define IPV6_RTRSTATE_ADVERTISED                3
#define IPV6_RTRSTATE_STALE                     4

	uint8_t ipv6_traffic_class;	/* 226 */
	uint8_t ipv6_hop_limit;	/* 227 */
	uint8_t ipv6_if_id[8];	/* 228-22F */
	uint8_t ipv6_addr0[16];	/* 230-23F */
	uint8_t ipv6_addr1[16];	/* 240-24F */
	uint32_t ipv6_nd_reach_time;	/* 250-253 */
	uint32_t ipv6_nd_rexmit_timer;	/* 254-257 */
	uint32_t ipv6_nd_stale_timeout;	/* 258-25B */
	uint8_t ipv6_dup_addr_detect_count;	/* 25C */
	uint8_t ipv6_cache_id;	/* 25D */
	uint8_t res13[18];	/* 25E-26F */
	uint32_t ipv6_gw_advrt_mtu;	/* 270-273 */
	uint8_t res14[140];	/* 274-2FF */
};

#define IP_ADDR_COUNT	4 /* Total 4 IP address supported in one interface
			   * One IPv4, one IPv6 link local and 2 IPv6
			   */

#define IP_STATE_MASK	0x0F000000
#define IP_STATE_SHIFT	24

struct init_fw_ctrl_blk {
	struct addr_ctrl_blk pri;
/*	struct addr_ctrl_blk sec;*/
};

#define PRIMARI_ACB		0
#define SECONDARY_ACB		1

struct addr_ctrl_blk_def {
	uint8_t reserved1[1];	/* 00 */
	uint8_t control;	/* 01 */
	uint8_t reserved2[11];	/* 02-0C */
	uint8_t inst_num;	/* 0D */
	uint8_t reserved3[34];	/* 0E-2F */
	uint16_t iscsi_opts;	/* 30-31 */
	uint16_t ipv4_tcp_opts;	/* 32-33 */
	uint16_t ipv4_ip_opts;	/* 34-35 */
	uint16_t iscsi_max_pdu_size;	/* 36-37 */
	uint8_t ipv4_tos;	/* 38 */
	uint8_t ipv4_ttl;	/* 39 */
	uint8_t reserved4[2];	/* 3A-3B */
	uint16_t def_timeout;	/* 3C-3D */
	uint16_t iscsi_fburst_len;	/* 3E-3F */
	uint8_t reserved5[4];	/* 40-43 */
	uint16_t iscsi_max_outstnd_r2t;	/* 44-45 */
	uint8_t reserved6[2];	/* 46-47 */
	uint16_t ipv4_port;	/* 48-49 */
	uint16_t iscsi_max_burst_len;	/* 4A-4B */
	uint8_t reserved7[4];	/* 4C-4F */
	uint8_t ipv4_addr[4];	/* 50-53 */
	uint16_t ipv4_vlan_tag;	/* 54-55 */
	uint8_t ipv4_addr_state;	/* 56 */
	uint8_t ipv4_cacheid;	/* 57 */
	uint8_t reserved8[8];	/* 58-5F */
	uint8_t ipv4_subnet[4];	/* 60-63 */
	uint8_t reserved9[12];	/* 64-6F */
	uint8_t ipv4_gw_addr[4];	/* 70-73 */
	uint8_t reserved10[84];	/* 74-C7 */
	uint8_t abort_timer;	/* C8    */
	uint8_t ipv4_tcp_wsf;	/* C9    */
	uint8_t reserved11[10];	/* CA-D3 */
	uint8_t ipv4_dhcp_vid_len;	/* D4 */
	uint8_t ipv4_dhcp_vid[11];	/* D5-DF */
	uint8_t reserved12[20];	/* E0-F3 */
	uint8_t ipv4_dhcp_alt_cid_len;	/* F4 */
	uint8_t ipv4_dhcp_alt_cid[11];	/* F5-FF */
	uint8_t iscsi_name[224];	/* 100-1DF */
	uint8_t reserved13[32];	/* 1E0-1FF */
	uint32_t cookie;	/* 200-203 */
	uint16_t ipv6_port;	/* 204-205 */
	uint16_t ipv6_opts;	/* 206-207 */
	uint16_t ipv6_addtl_opts;	/* 208-209 */
	uint16_t ipv6_tcp_opts;		/* 20A-20B */
	uint8_t ipv6_tcp_wsf;		/* 20C */
	uint16_t ipv6_flow_lbl;		/* 20D-20F */
	uint8_t ipv6_dflt_rtr_addr[16];	/* 210-21F */
	uint16_t ipv6_vlan_tag;		/* 220-221 */
	uint8_t ipv6_lnk_lcl_addr_state;	/* 222 */
	uint8_t ipv6_addr0_state;	/* 223 */
	uint8_t ipv6_addr1_state;	/* 224 */
	uint8_t ipv6_dflt_rtr_state;	/* 225 */
	uint8_t ipv6_traffic_class;	/* 226 */
	uint8_t ipv6_hop_limit;		/* 227 */
	uint8_t ipv6_if_id[8];		/* 228-22F */
	uint8_t ipv6_addr0[16];		/* 230-23F */
	uint8_t ipv6_addr1[16];		/* 240-24F */
	uint32_t ipv6_nd_reach_time;	/* 250-253 */
	uint32_t ipv6_nd_rexmit_timer;	/* 254-257 */
	uint32_t ipv6_nd_stale_timeout;	/* 258-25B */
	uint8_t ipv6_dup_addr_detect_count;	/* 25C */
	uint8_t ipv6_cache_id;		/* 25D */
	uint8_t reserved14[18];		/* 25E-26F */
	uint32_t ipv6_gw_advrt_mtu;	/* 270-273 */
	uint8_t reserved15[140];	/* 274-2FF */
};

/*************************************************************************/

#define MAX_CHAP_ENTRIES_40XX	128
#define MAX_CHAP_ENTRIES_82XX	1024
#define MAX_RESRV_CHAP_IDX	3
#define FLASH_CHAP_OFFSET	0x06000000

struct ql4_chap_table {
	uint16_t link;
	uint8_t flags;
	uint8_t secret_len;
#define MIN_CHAP_SECRET_LEN	12
#define MAX_CHAP_SECRET_LEN	100
	uint8_t secret[MAX_CHAP_SECRET_LEN];
#define MAX_CHAP_NAME_LEN	256
	uint8_t name[MAX_CHAP_NAME_LEN];
	uint16_t reserved;
#define CHAP_VALID_COOKIE	0x4092
#define CHAP_INVALID_COOKIE	0xFFEE
	uint16_t cookie;
};

struct dev_db_entry {
	uint16_t options;	/* 00-01 */
#define DDB_OPT_DISC_SESSION  0x10
#define DDB_OPT_TARGET	      0x02 /* device is a target */
#define DDB_OPT_IPV6_DEVICE	0x100
#define DDB_OPT_AUTO_SENDTGTS_DISABLE		0x40
#define DDB_OPT_IPV6_NULL_LINK_LOCAL		0x800 /* post connection */
#define DDB_OPT_IPV6_FW_DEFINED_LINK_LOCAL	0x800 /* pre connection */

#define OPT_IS_FW_ASSIGNED_IPV6		11
#define OPT_IPV6_DEVICE			8
#define OPT_AUTO_SENDTGTS_DISABLE	6
#define OPT_DISC_SESSION		4
#define OPT_ENTRY_STATE			3
	uint16_t exec_throttle;	/* 02-03 */
	uint16_t exec_count;	/* 04-05 */
	uint16_t res0;	/* 06-07 */
	uint16_t iscsi_options;	/* 08-09 */
#define ISCSIOPT_HEADER_DIGEST_EN		13
#define ISCSIOPT_DATA_DIGEST_EN			12
#define ISCSIOPT_IMMEDIATE_DATA_EN		11
#define ISCSIOPT_INITIAL_R2T_EN			10
#define ISCSIOPT_DATA_SEQ_IN_ORDER		9
#define ISCSIOPT_DATA_PDU_IN_ORDER		8
#define ISCSIOPT_CHAP_AUTH_EN			7
#define ISCSIOPT_SNACK_REQ_EN			6
#define ISCSIOPT_DISCOVERY_LOGOUT_EN		5
#define ISCSIOPT_BIDI_CHAP_EN			4
#define ISCSIOPT_DISCOVERY_AUTH_OPTIONAL	3
#define ISCSIOPT_ERL1				1
#define ISCSIOPT_ERL0				0

	uint16_t tcp_options;	/* 0A-0B */
#define TCPOPT_TIMESTAMP_STAT	6
#define TCPOPT_NAGLE_DISABLE	5
#define TCPOPT_WSF_DISABLE	4
#define TCPOPT_TIMER_SCALE3	3
#define TCPOPT_TIMER_SCALE2	2
#define TCPOPT_TIMER_SCALE1	1
#define TCPOPT_TIMESTAMP_EN	0

	uint16_t ip_options;	/* 0C-0D */
#define IPOPT_FRAGMENT_DISABLE	4

	uint16_t iscsi_max_rcv_data_seg_len;	/* 0E-0F */
#define BYTE_UNITS	512
	uint32_t res1;	/* 10-13 */
	uint16_t iscsi_max_snd_data_seg_len;	/* 14-15 */
	uint16_t iscsi_first_burst_len;	/* 16-17 */
	uint16_t iscsi_def_time2wait;	/* 18-19 */
	uint16_t iscsi_def_time2retain;	/* 1A-1B */
	uint16_t iscsi_max_outsnd_r2t;	/* 1C-1D */
	uint16_t ka_timeout;	/* 1E-1F */
	uint8_t isid[6];	/* 20-25 big-endian, must be converted
				 * to little-endian */
	uint16_t tsid;		/* 26-27 */
	uint16_t port;	/* 28-29 */
	uint16_t iscsi_max_burst_len;	/* 2A-2B */
	uint16_t def_timeout;	/* 2C-2D */
	uint16_t res2;	/* 2E-2F */
	uint8_t ip_addr[0x10];	/* 30-3F */
	uint8_t iscsi_alias[0x20];	/* 40-5F */
	uint8_t tgt_addr[0x20];	/* 60-7F */
	uint16_t mss;	/* 80-81 */
	uint16_t res3;	/* 82-83 */
	uint16_t lcl_port;	/* 84-85 */
	uint8_t ipv4_tos;	/* 86 */
	uint16_t ipv6_flow_lbl;	/* 87-89 */
	uint8_t res4[0x36];	/* 8A-BF */
	uint8_t iscsi_name[0xE0];	/* C0-19F : xxzzy Make this a
					 * pointer to a string so we
					 * don't have to reserve so
					 * much RAM */
	uint8_t link_local_ipv6_addr[0x10]; /* 1A0-1AF */
	uint8_t res5[0x10];	/* 1B0-1BF */
#define DDB_NO_LINK	0xFFFF
#define DDB_ISNS	0xFFFD
	uint16_t ddb_link;	/* 1C0-1C1 */
	uint16_t chap_tbl_idx;	/* 1C2-1C3 */
	uint16_t tgt_portal_grp; /* 1C4-1C5 */
	uint8_t tcp_xmt_wsf;	/* 1C6 */
	uint8_t tcp_rcv_wsf;	/* 1C7 */
	uint32_t stat_sn;	/* 1C8-1CB */
	uint32_t exp_stat_sn;	/* 1CC-1CF */
	uint8_t res6[0x2b];	/* 1D0-1FB */
#define DDB_VALID_COOKIE	0x9034
	uint16_t cookie;	/* 1FC-1FD */
	uint16_t len;		/* 1FE-1FF */
};

/*************************************************************************/

/* Flash definitions */

#define FLASH_OFFSET_SYS_INFO	0x02000000
#define FLASH_DEFAULTBLOCKSIZE	0x20000
#define FLASH_EOF_OFFSET	(FLASH_DEFAULTBLOCKSIZE-8) /* 4 bytes
							    * for EOF
							    * signature */
#define FLASH_RAW_ACCESS_ADDR	0x8e000000

#define BOOT_PARAM_OFFSET_PORT0 0x3b0
#define BOOT_PARAM_OFFSET_PORT1 0x7b0

#define FLASH_OFFSET_DB_INFO	0x05000000
#define FLASH_OFFSET_DB_END	(FLASH_OFFSET_DB_INFO + 0x7fff)


struct sys_info_phys_addr {
	uint8_t address[6];	/* 00-05 */
	uint8_t filler[2];	/* 06-07 */
};

struct flash_sys_info {
	uint32_t cookie;	/* 00-03 */
	uint32_t physAddrCount; /* 04-07 */
	struct sys_info_phys_addr physAddr[4]; /* 08-27 */
	uint8_t vendorId[128];	/* 28-A7 */
	uint8_t productId[128]; /* A8-127 */
	uint32_t serialNumber;	/* 128-12B */

	/*  PCI Configuration values */
	uint32_t pciDeviceVendor;	/* 12C-12F */
	uint32_t pciDeviceId;	/* 130-133 */
	uint32_t pciSubsysVendor;	/* 134-137 */
	uint32_t pciSubsysId;	/* 138-13B */

	/*  This validates version 1. */
	uint32_t crumbs;	/* 13C-13F */

	uint32_t enterpriseNumber;	/* 140-143 */

	uint32_t mtu;		/* 144-147 */
	uint32_t reserved0;	/* 148-14b */
	uint32_t crumbs2;	/* 14c-14f */
	uint8_t acSerialNumber[16];	/* 150-15f */
	uint32_t crumbs3;	/* 160-16f */

	/* Leave this last in the struct so it is declared invalid if
	 * any new items are added.
	 */
	uint32_t reserved1[39]; /* 170-1ff */
};	/* 200 */

struct mbx_sys_info {
	uint8_t board_id_str[16];   /*  0-f  Keep board ID string first */
				/* in this structure for GUI. */
	uint16_t board_id;	/* 10-11 board ID code */
	uint16_t phys_port_cnt;	/* 12-13 number of physical network ports */
	uint16_t port_num;	/* 14-15 network port for this PCI function */
				/* (port 0 is first port) */
	uint8_t mac_addr[6];	/* 16-1b MAC address for this PCI function */
	uint32_t iscsi_pci_func_cnt;  /* 1c-1f number of iSCSI PCI functions */
	uint32_t pci_func;	      /* 20-23 this PCI function */
	unsigned char serial_number[16];  /* 24-33 serial number string */
	uint8_t reserved[12];		  /* 34-3f */
};

struct about_fw_info {
	uint16_t fw_major;		/* 00 - 01 */
	uint16_t fw_minor;		/* 02 - 03 */
	uint16_t fw_patch;		/* 04 - 05 */
	uint16_t fw_build;		/* 06 - 07 */
	uint8_t fw_build_date[16];	/* 08 - 17 ASCII String */
	uint8_t fw_build_time[16];	/* 18 - 27 ASCII String */
	uint8_t fw_build_user[16];	/* 28 - 37 ASCII String */
	uint16_t fw_load_source;	/* 38 - 39 */
					/* 1 = Flash Primary,
					   2 = Flash Secondary,
					   3 = Host Download
					*/
	uint8_t reserved1[6];		/* 3A - 3F */
	uint16_t iscsi_major;		/* 40 - 41 */
	uint16_t iscsi_minor;		/* 42 - 43 */
	uint16_t bootload_major;	/* 44 - 45 */
	uint16_t bootload_minor;	/* 46 - 47 */
	uint16_t bootload_patch;	/* 48 - 49 */
	uint16_t bootload_build;	/* 4A - 4B */
	uint8_t extended_timestamp[180];/* 4C - FF */
};

struct crash_record {
	uint16_t fw_major_version;	/* 00 - 01 */
	uint16_t fw_minor_version;	/* 02 - 03 */
	uint16_t fw_patch_version;	/* 04 - 05 */
	uint16_t fw_build_version;	/* 06 - 07 */

	uint8_t build_date[16]; /* 08 - 17 */
	uint8_t build_time[16]; /* 18 - 27 */
	uint8_t build_user[16]; /* 28 - 37 */
	uint8_t card_serial_num[16];	/* 38 - 47 */

	uint32_t time_of_crash_in_secs; /* 48 - 4B */
	uint32_t time_of_crash_in_ms;	/* 4C - 4F */

	uint16_t out_RISC_sd_num_frames;	/* 50 - 51 */
	uint16_t OAP_sd_num_words;	/* 52 - 53 */
	uint16_t IAP_sd_num_frames;	/* 54 - 55 */
	uint16_t in_RISC_sd_num_words;	/* 56 - 57 */

	uint8_t reserved1[28];	/* 58 - 7F */

	uint8_t out_RISC_reg_dump[256]; /* 80 -17F */
	uint8_t in_RISC_reg_dump[256];	/*180 -27F */
	uint8_t in_out_RISC_stack_dump[0];	/*280 - ??? */
};

struct conn_event_log_entry {
#define MAX_CONN_EVENT_LOG_ENTRIES	100
	uint32_t timestamp_sec; /* 00 - 03 seconds since boot */
	uint32_t timestamp_ms;	/* 04 - 07 milliseconds since boot */
	uint16_t device_index;	/* 08 - 09  */
	uint16_t fw_conn_state; /* 0A - 0B  */
	uint8_t event_type;	/* 0C - 0C  */
	uint8_t error_code;	/* 0D - 0D  */
	uint16_t error_code_detail;	/* 0E - 0F  */
	uint8_t num_consecutive_events; /* 10 - 10  */
	uint8_t rsvd[3];	/* 11 - 13  */
};

/*************************************************************************
 *
 *				IOCB Commands Structures and Definitions
 *
 *************************************************************************/
#define IOCB_MAX_CDB_LEN	    16	/* Bytes in a CBD */
#define IOCB_MAX_SENSEDATA_LEN	    32	/* Bytes of sense data */
#define IOCB_MAX_EXT_SENSEDATA_LEN  60  /* Bytes of extended sense data */

/* IOCB header structure */
struct qla4_header {
	uint8_t entryType;
#define ET_STATUS		 0x03
#define ET_MARKER		 0x04
#define ET_CONT_T1		 0x0A
#define ET_STATUS_CONTINUATION	 0x10
#define ET_CMND_T3		 0x19
#define ET_PASSTHRU0		 0x3A
#define ET_PASSTHRU_STATUS	 0x3C
#define ET_MBOX_CMD		0x38
#define ET_MBOX_STATUS		0x39

	uint8_t entryStatus;
	uint8_t systemDefined;
#define SD_ISCSI_PDU	0x01
	uint8_t entryCount;

	/* SyetemDefined definition */
};

/* Generic queue entry structure*/
struct queue_entry {
	uint8_t data[60];
	uint32_t signature;

};

/* 64 bit addressing segment counts*/

#define COMMAND_SEG_A64	  1
#define CONTINUE_SEG_A64  5

/* 64 bit addressing segment definition*/

struct data_seg_a64 {
	struct {
		uint32_t addrLow;
		uint32_t addrHigh;

	} base;

	uint32_t count;

};

/* Command Type 3 entry structure*/

struct command_t3_entry {
	struct qla4_header hdr;	/* 00-03 */

	uint32_t handle;	/* 04-07 */
	uint16_t target;	/* 08-09 */
	uint16_t connection_id; /* 0A-0B */

	uint8_t control_flags;	/* 0C */

	/* data direction  (bits 5-6) */
#define CF_WRITE		0x20
#define CF_READ			0x40
#define CF_NO_DATA		0x00

	/* task attributes (bits 2-0) */
#define CF_HEAD_TAG		0x03
#define CF_ORDERED_TAG		0x02
#define CF_SIMPLE_TAG		0x01

	/* STATE FLAGS FIELD IS A PLACE HOLDER. THE FW WILL SET BITS
	 * IN THIS FIELD AS THE COMMAND IS PROCESSED. WHEN THE IOCB IS
	 * CHANGED TO AN IOSB THIS FIELD WILL HAVE THE STATE FLAGS SET
	 * PROPERLY.
	 */
	uint8_t state_flags;	/* 0D */
	uint8_t cmdRefNum;	/* 0E */
	uint8_t reserved1;	/* 0F */
	uint8_t cdb[IOCB_MAX_CDB_LEN];	/* 10-1F */
	struct scsi_lun lun;	/* FCP LUN (BE). */
	uint32_t cmdSeqNum;	/* 28-2B */
	uint16_t timeout;	/* 2C-2D */
	uint16_t dataSegCnt;	/* 2E-2F */
	uint32_t ttlByteCnt;	/* 30-33 */
	struct data_seg_a64 dataseg[COMMAND_SEG_A64];	/* 34-3F */

};


/* Continuation Type 1 entry structure*/
struct continuation_t1_entry {
	struct qla4_header hdr;

	struct data_seg_a64 dataseg[CONTINUE_SEG_A64];

};

/* Parameterize for 64 or 32 bits */
#define COMMAND_SEG	COMMAND_SEG_A64
#define CONTINUE_SEG	CONTINUE_SEG_A64

#define ET_COMMAND	ET_CMND_T3
#define ET_CONTINUE	ET_CONT_T1

/* Marker entry structure*/
struct qla4_marker_entry {
	struct qla4_header hdr;	/* 00-03 */

	uint32_t system_defined; /* 04-07 */
	uint16_t target;	/* 08-09 */
	uint16_t modifier;	/* 0A-0B */
#define MM_LUN_RESET		0
#define MM_TGT_WARM_RESET	1

	uint16_t flags;		/* 0C-0D */
	uint16_t reserved1;	/* 0E-0F */
	struct scsi_lun lun;	/* FCP LUN (BE). */
	uint64_t reserved2;	/* 18-1F */
	uint64_t reserved3;	/* 20-27 */
	uint64_t reserved4;	/* 28-2F */
	uint64_t reserved5;	/* 30-37 */
	uint64_t reserved6;	/* 38-3F */
};

/* Status entry structure*/
struct status_entry {
	struct qla4_header hdr;	/* 00-03 */

	uint32_t handle;	/* 04-07 */

	uint8_t scsiStatus;	/* 08 */
#define SCSI_CHECK_CONDITION		  0x02

	uint8_t iscsiFlags;	/* 09 */
#define ISCSI_FLAG_RESIDUAL_UNDER	  0x02
#define ISCSI_FLAG_RESIDUAL_OVER	  0x04

	uint8_t iscsiResponse;	/* 0A */

	uint8_t completionStatus;	/* 0B */
#define SCS_COMPLETE			  0x00
#define SCS_INCOMPLETE			  0x01
#define SCS_RESET_OCCURRED		  0x04
#define SCS_ABORTED			  0x05
#define SCS_TIMEOUT			  0x06
#define SCS_DATA_OVERRUN		  0x07
#define SCS_DATA_UNDERRUN		  0x15
#define SCS_QUEUE_FULL			  0x1C
#define SCS_DEVICE_UNAVAILABLE		  0x28
#define SCS_DEVICE_LOGGED_OUT		  0x29

	uint8_t reserved1;	/* 0C */

	/* state_flags MUST be at the same location as state_flags in
	 * the Command_T3/4_Entry */
	uint8_t state_flags;	/* 0D */

	uint16_t senseDataByteCnt;	/* 0E-0F */
	uint32_t residualByteCnt;	/* 10-13 */
	uint32_t bidiResidualByteCnt;	/* 14-17 */
	uint32_t expSeqNum;	/* 18-1B */
	uint32_t maxCmdSeqNum;	/* 1C-1F */
	uint8_t senseData[IOCB_MAX_SENSEDATA_LEN];	/* 20-3F */

};

/* Status Continuation entry */
struct status_cont_entry {
       struct qla4_header hdr; /* 00-03 */
       uint8_t ext_sense_data[IOCB_MAX_EXT_SENSEDATA_LEN]; /* 04-63 */
};

struct passthru0 {
	struct qla4_header hdr;		       /* 00-03 */
	uint32_t handle;	/* 04-07 */
	uint16_t target;	/* 08-09 */
	uint16_t connection_id;	/* 0A-0B */
#define ISNS_DEFAULT_SERVER_CONN_ID	((uint16_t)0x8000)

	uint16_t control_flags;	/* 0C-0D */
#define PT_FLAG_ETHERNET_FRAME		0x8000
#define PT_FLAG_ISNS_PDU		0x8000
#define PT_FLAG_SEND_BUFFER		0x0200
#define PT_FLAG_WAIT_4_RESPONSE		0x0100
#define PT_FLAG_ISCSI_PDU		0x1000

	uint16_t timeout;	/* 0E-0F */
#define PT_DEFAULT_TIMEOUT		30 /* seconds */

	struct data_seg_a64 out_dsd;    /* 10-1B */
	uint32_t res1;		/* 1C-1F */
	struct data_seg_a64 in_dsd;     /* 20-2B */
	uint8_t res2[20];	/* 2C-3F */
};

struct passthru_status {
	struct qla4_header hdr;		       /* 00-03 */
	uint32_t handle;	/* 04-07 */
	uint16_t target;	/* 08-09 */
	uint16_t connectionID;	/* 0A-0B */

	uint8_t completionStatus;	/* 0C */
#define PASSTHRU_STATUS_COMPLETE		0x01

	uint8_t residualFlags;	/* 0D */

	uint16_t timeout;	/* 0E-0F */
	uint16_t portNumber;	/* 10-11 */
	uint8_t res1[10];	/* 12-1B */
	uint32_t outResidual;	/* 1C-1F */
	uint8_t res2[12];	/* 20-2B */
	uint32_t inResidual;	/* 2C-2F */
	uint8_t res4[16];	/* 30-3F */
};

struct mbox_cmd_iocb {
	struct qla4_header hdr;	/* 00-03 */
	uint32_t handle;	/* 04-07 */
	uint32_t in_mbox[8];	/* 08-25 */
	uint32_t res1[6];	/* 26-3F */
};

struct mbox_status_iocb {
	struct qla4_header hdr;	/* 00-03 */
	uint32_t handle;	/* 04-07 */
	uint32_t out_mbox[8];	/* 08-25 */
	uint32_t res1[6];	/* 26-3F */
};

/*
 * ISP queue - response queue entry definition.
 */
struct response {
	uint8_t data[60];
	uint32_t signature;
#define RESPONSE_PROCESSED	0xDEADDEAD	/* Signature */
};

struct ql_iscsi_stats {
	uint64_t mac_tx_frames; /* 0000–0007 */
	uint64_t mac_tx_bytes; /* 0008–000F */
	uint64_t mac_tx_multicast_frames; /* 0010–0017 */
	uint64_t mac_tx_broadcast_frames; /* 0018–001F */
	uint64_t mac_tx_pause_frames; /* 0020–0027 */
	uint64_t mac_tx_control_frames; /* 0028–002F */
	uint64_t mac_tx_deferral; /* 0030–0037 */
	uint64_t mac_tx_excess_deferral; /* 0038–003F */
	uint64_t mac_tx_late_collision; /* 0040–0047 */
	uint64_t mac_tx_abort; /* 0048–004F */
	uint64_t mac_tx_single_collision; /* 0050–0057 */
	uint64_t mac_tx_multiple_collision; /* 0058–005F */
	uint64_t mac_tx_collision; /* 0060–0067 */
	uint64_t mac_tx_frames_dropped; /* 0068–006F */
	uint64_t mac_tx_jumbo_frames; /* 0070–0077 */
	uint64_t mac_rx_frames; /* 0078–007F */
	uint64_t mac_rx_bytes; /* 0080–0087 */
	uint64_t mac_rx_unknown_control_frames; /* 0088–008F */
	uint64_t mac_rx_pause_frames; /* 0090–0097 */
	uint64_t mac_rx_control_frames; /* 0098–009F */
	uint64_t mac_rx_dribble; /* 00A0–00A7 */
	uint64_t mac_rx_frame_length_error; /* 00A8–00AF */
	uint64_t mac_rx_jabber; /* 00B0–00B7 */
	uint64_t mac_rx_carrier_sense_error; /* 00B8–00BF */
	uint64_t mac_rx_frame_discarded; /* 00C0–00C7 */
	uint64_t mac_rx_frames_dropped; /* 00C8–00CF */
	uint64_t mac_crc_error; /* 00D0–00D7 */
	uint64_t mac_encoding_error; /* 00D8–00DF */
	uint64_t mac_rx_length_error_large; /* 00E0–00E7 */
	uint64_t mac_rx_length_error_small; /* 00E8–00EF */
	uint64_t mac_rx_multicast_frames; /* 00F0–00F7 */
	uint64_t mac_rx_broadcast_frames; /* 00F8–00FF */
	uint64_t ip_tx_packets; /* 0100–0107 */
	uint64_t ip_tx_bytes; /* 0108–010F */
	uint64_t ip_tx_fragments; /* 0110–0117 */
	uint64_t ip_rx_packets; /* 0118–011F */
	uint64_t ip_rx_bytes; /* 0120–0127 */
	uint64_t ip_rx_fragments; /* 0128–012F */
	uint64_t ip_datagram_reassembly; /* 0130–0137 */
	uint64_t ip_invalid_address_error; /* 0138–013F */
	uint64_t ip_error_packets; /* 0140–0147 */
	uint64_t ip_fragrx_overlap; /* 0148–014F */
	uint64_t ip_fragrx_outoforder; /* 0150–0157 */
	uint64_t ip_datagram_reassembly_timeout; /* 0158–015F */
	uint64_t ipv6_tx_packets; /* 0160–0167 */
	uint64_t ipv6_tx_bytes; /* 0168–016F */
	uint64_t ipv6_tx_fragments; /* 0170–0177 */
	uint64_t ipv6_rx_packets; /* 0178–017F */
	uint64_t ipv6_rx_bytes; /* 0180–0187 */
	uint64_t ipv6_rx_fragments; /* 0188–018F */
	uint64_t ipv6_datagram_reassembly; /* 0190–0197 */
	uint64_t ipv6_invalid_address_error; /* 0198–019F */
	uint64_t ipv6_error_packets; /* 01A0–01A7 */
	uint64_t ipv6_fragrx_overlap; /* 01A8–01AF */
	uint64_t ipv6_fragrx_outoforder; /* 01B0–01B7 */
	uint64_t ipv6_datagram_reassembly_timeout; /* 01B8–01BF */
	uint64_t tcp_tx_segments; /* 01C0–01C7 */
	uint64_t tcp_tx_bytes; /* 01C8–01CF */
	uint64_t tcp_rx_segments; /* 01D0–01D7 */
	uint64_t tcp_rx_byte; /* 01D8–01DF */
	uint64_t tcp_duplicate_ack_retx; /* 01E0–01E7 */
	uint64_t tcp_retx_timer_expired; /* 01E8–01EF */
	uint64_t tcp_rx_duplicate_ack; /* 01F0–01F7 */
	uint64_t tcp_rx_pure_ackr; /* 01F8–01FF */
	uint64_t tcp_tx_delayed_ack; /* 0200–0207 */
	uint64_t tcp_tx_pure_ack; /* 0208–020F */
	uint64_t tcp_rx_segment_error; /* 0210–0217 */
	uint64_t tcp_rx_segment_outoforder; /* 0218–021F */
	uint64_t tcp_rx_window_probe; /* 0220–0227 */
	uint64_t tcp_rx_window_update; /* 0228–022F */
	uint64_t tcp_tx_window_probe_persist; /* 0230–0237 */
	uint64_t ecc_error_correction; /* 0238–023F */
	uint64_t iscsi_pdu_tx; /* 0240-0247 */
	uint64_t iscsi_data_bytes_tx; /* 0248-024F */
	uint64_t iscsi_pdu_rx; /* 0250-0257 */
	uint64_t iscsi_data_bytes_rx; /* 0258-025F */
	uint64_t iscsi_io_completed; /* 0260-0267 */
	uint64_t iscsi_unexpected_io_rx; /* 0268-026F */
	uint64_t iscsi_format_error; /* 0270-0277 */
	uint64_t iscsi_hdr_digest_error; /* 0278-027F */
	uint64_t iscsi_data_digest_error; /* 0280-0287 */
	uint64_t iscsi_sequence_error; /* 0288-028F */
	uint32_t tx_cmd_pdu; /* 0290-0293 */
	uint32_t tx_resp_pdu; /* 0294-0297 */
	uint32_t rx_cmd_pdu; /* 0298-029B */
	uint32_t rx_resp_pdu; /* 029C-029F */

	uint64_t tx_data_octets; /* 02A0-02A7 */
	uint64_t rx_data_octets; /* 02A8-02AF */

	uint32_t hdr_digest_err; /* 02B0–02B3 */
	uint32_t data_digest_err; /* 02B4–02B7 */
	uint32_t conn_timeout_err; /* 02B8–02BB */
	uint32_t framing_err; /* 02BC–02BF */

	uint32_t tx_nopout_pdus; /* 02C0–02C3 */
	uint32_t tx_scsi_cmd_pdus;  /* 02C4–02C7 */
	uint32_t tx_tmf_cmd_pdus; /* 02C8–02CB */
	uint32_t tx_login_cmd_pdus; /* 02CC–02CF */
	uint32_t tx_text_cmd_pdus; /* 02D0–02D3 */
	uint32_t tx_scsi_write_pdus; /* 02D4–02D7 */
	uint32_t tx_logout_cmd_pdus; /* 02D8–02DB */
	uint32_t tx_snack_req_pdus; /* 02DC–02DF */

	uint32_t rx_nopin_pdus; /* 02E0–02E3 */
	uint32_t rx_scsi_resp_pdus; /* 02E4–02E7 */
	uint32_t rx_tmf_resp_pdus; /* 02E8–02EB */
	uint32_t rx_login_resp_pdus; /* 02EC–02EF */
	uint32_t rx_text_resp_pdus; /* 02F0–02F3 */
	uint32_t rx_scsi_read_pdus; /* 02F4–02F7 */
	uint32_t rx_logout_resp_pdus; /* 02F8–02FB */

	uint32_t rx_r2t_pdus; /* 02FC–02FF */
	uint32_t rx_async_pdus; /* 0300–0303 */
	uint32_t rx_reject_pdus; /* 0304–0307 */

	uint8_t reserved2[264]; /* 0x0308 - 0x040F */
};

#define QLA8XXX_DBG_STATE_ARRAY_LEN		16
#define QLA8XXX_DBG_CAP_SIZE_ARRAY_LEN		8
#define QLA8XXX_DBG_RSVD_ARRAY_LEN		8
#define QLA83XX_DBG_OCM_WNDREG_ARRAY_LEN	16
#define QLA83XX_SS_OCM_WNDREG_INDEX		3
#define QLA83XX_SS_PCI_INDEX			0
#define QLA8022_TEMPLATE_CAP_OFFSET		172
#define QLA83XX_TEMPLATE_CAP_OFFSET		268
#define QLA80XX_TEMPLATE_RESERVED_BITS		16

struct qla4_8xxx_minidump_template_hdr {
	uint32_t entry_type;
	uint32_t first_entry_offset;
	uint32_t size_of_template;
	uint32_t capture_debug_level;
	uint32_t num_of_entries;
	uint32_t version;
	uint32_t driver_timestamp;
	uint32_t checksum;

	uint32_t driver_capture_mask;
	uint32_t driver_info_word2;
	uint32_t driver_info_word3;
	uint32_t driver_info_word4;

	uint32_t saved_state_array[QLA8XXX_DBG_STATE_ARRAY_LEN];
	uint32_t capture_size_array[QLA8XXX_DBG_CAP_SIZE_ARRAY_LEN];
	uint32_t ocm_window_reg[QLA83XX_DBG_OCM_WNDREG_ARRAY_LEN];
	uint32_t capabilities[QLA80XX_TEMPLATE_RESERVED_BITS];
};

#endif /*  _QLA4X_FW_H */
