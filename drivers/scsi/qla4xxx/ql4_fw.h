/*
 * QLogic iSCSI HBA Driver
 * Copyright (c)  2003-2006 QLogic Corporation
 *
 * See LICENSE.qla4xxx for copyright and licensing details.
 */

#ifndef _QLA4X_FW_H
#define _QLA4X_FW_H


#define MAX_PRST_DEV_DB_ENTRIES		64
#define MIN_DISC_DEV_DB_ENTRY		MAX_PRST_DEV_DB_ENTRIES
#define MAX_DEV_DB_ENTRIES 512

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

/*************************************************************************
 *
 *		Mailbox Commands Structures and Definitions
 *
 *************************************************************************/

/*  Mailbox command definitions */
#define MBOX_CMD_ABOUT_FW			0x0009
#define MBOX_CMD_PING				0x000B
#define MBOX_CMD_LUN_RESET			0x0016
#define MBOX_CMD_GET_MANAGEMENT_DATA		0x001E
#define MBOX_CMD_GET_FW_STATUS			0x001F
#define MBOX_CMD_SET_ISNS_SERVICE		0x0021
#define ISNS_DISABLE				0
#define ISNS_ENABLE				1
#define MBOX_CMD_COPY_FLASH			0x0024
#define MBOX_CMD_WRITE_FLASH			0x0025
#define MBOX_CMD_READ_FLASH			0x0026
#define MBOX_CMD_CLEAR_DATABASE_ENTRY		0x0031
#define MBOX_CMD_CONN_CLOSE_SESS_LOGOUT		0x0056
#define LOGOUT_OPTION_CLOSE_SESSION		0x01
#define LOGOUT_OPTION_RELOGIN			0x02
#define MBOX_CMD_EXECUTE_IOCB_A64		0x005A
#define MBOX_CMD_INITIALIZE_FIRMWARE		0x0060
#define MBOX_CMD_GET_INIT_FW_CTRL_BLOCK		0x0061
#define MBOX_CMD_REQUEST_DATABASE_ENTRY		0x0062
#define MBOX_CMD_SET_DATABASE_ENTRY		0x0063
#define MBOX_CMD_GET_DATABASE_ENTRY		0x0064
#define DDB_DS_UNASSIGNED			0x00
#define DDB_DS_NO_CONNECTION_ACTIVE		0x01
#define DDB_DS_SESSION_ACTIVE			0x04
#define DDB_DS_SESSION_FAILED			0x06
#define DDB_DS_LOGIN_IN_PROCESS			0x07
#define MBOX_CMD_GET_FW_STATE			0x0069
#define MBOX_CMD_GET_INIT_FW_CTRL_BLOCK_DEFAULTS 0x006A
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

/* Mailbox 1 */
#define FW_STATE_READY				0x0000
#define FW_STATE_CONFIG_WAIT			0x0001
#define FW_STATE_WAIT_LOGIN			0x0002
#define FW_STATE_ERROR				0x0004
#define FW_STATE_DHCP_IN_PROGRESS		0x0008

/* Mailbox 3 */
#define FW_ADDSTATE_OPTICAL_MEDIA		0x0001
#define FW_ADDSTATE_DHCP_ENABLED		0x0002
#define FW_ADDSTATE_LINK_UP			0x0010
#define FW_ADDSTATE_ISNS_SVC_ENABLED		0x0020
#define MBOX_CMD_GET_DATABASE_ENTRY_DEFAULTS	0x006B
#define MBOX_CMD_CONN_OPEN_SESS_LOGIN		0x0074
#define MBOX_CMD_GET_CRASH_RECORD		0x0076	/* 4010 only */
#define MBOX_CMD_GET_CONN_EVENT_LOG		0x0077

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
#define MBOX_ASTS_IPV6_PREFIX_EXPIRED		0x802B
#define MBOX_ASTS_IPV6_ND_PREFIX_IGNORED	0x802C
#define MBOX_ASTS_IPV6_LCL_PREFIX_IGNORED	0x802D
#define MBOX_ASTS_ICMPV6_ERROR_MSG_RCVD		0x802E

#define ISNS_EVENT_DATA_RECEIVED		0x0000
#define ISNS_EVENT_CONNECTION_OPENED		0x0001
#define ISNS_EVENT_CONNECTION_FAILED		0x0002
#define MBOX_ASTS_IPSEC_SYSTEM_FATAL_ERROR	0x8022
#define MBOX_ASTS_SUBNET_STATE_CHANGE		0x8027

/*************************************************************************/

/* Host Adapter Initialization Control Block (from host) */
struct addr_ctrl_blk {
	uint8_t version;	/* 00 */
	uint8_t control;	/* 01 */

	uint16_t fw_options;	/* 02-03 */
#define	 FWOPT_HEARTBEAT_ENABLE		  0x1000
#define	 FWOPT_SESSION_MODE		  0x0040
#define	 FWOPT_INITIATOR_MODE		  0x0020
#define	 FWOPT_TARGET_MODE		  0x0010

	uint16_t exec_throttle;	/* 04-05 */
	uint8_t zio_count;	/* 06 */
	uint8_t res0;	/* 07 */
	uint16_t eth_mtu_size;	/* 08-09 */
	uint16_t add_fw_options;	/* 0A-0B */

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
	uint16_t ipv4_tcp_opts;	/* 32-33 */
	uint16_t ipv4_ip_opts;	/* 34-35 */

	uint16_t iscsi_max_pdu_size;	/* 36-37 */
	uint8_t ipv4_tos;	/* 38 */
	uint8_t ipv4_ttl;	/* 39 */
	uint8_t acb_version;	/* 3A */
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
	uint16_t ipv6_addtl_opts;	/* 208-209 */
	uint16_t ipv6_tcp_opts;	/* 20A-20B */
	uint8_t ipv6_tcp_wsf;	/* 20C */
	uint16_t ipv6_flow_lbl;	/* 20D-20F */
	uint8_t ipv6_gw_addr[16];	/* 210-21F */
	uint16_t ipv6_vlan_tag;	/* 220-221 */
	uint8_t ipv6_lnk_lcl_addr_state;/* 222 */
	uint8_t ipv6_addr0_state;	/* 223 */
	uint8_t ipv6_addr1_state;	/* 224 */
	uint8_t ipv6_gw_state;	/* 225 */
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

struct init_fw_ctrl_blk {
	struct addr_ctrl_blk pri;
	struct addr_ctrl_blk sec;
};

/*************************************************************************/

struct dev_db_entry {
	uint16_t options;	/* 00-01 */
#define DDB_OPT_DISC_SESSION  0x10
#define DDB_OPT_TARGET	      0x02 /* device is a target */

	uint16_t exec_throttle;	/* 02-03 */
	uint16_t exec_count;	/* 04-05 */
	uint16_t res0;	/* 06-07 */
	uint16_t iscsi_options;	/* 08-09 */
	uint16_t tcp_options;	/* 0A-0B */
	uint16_t ip_options;	/* 0C-0D */
	uint16_t iscsi_max_rcv_data_seg_len;	/* 0E-0F */
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
					 * don't have to reserve soooo
					 * much RAM */
	uint8_t ipv6_addr[0x10];/* 1A0-1AF */
	uint8_t res5[0x10];	/* 1B0-1BF */
	uint16_t ddb_link;	/* 1C0-1C1 */
	uint16_t chap_tbl_idx;	/* 1C2-1C3 */
	uint16_t tgt_portal_grp; /* 1C4-1C5 */
	uint8_t tcp_xmt_wsf;	/* 1C6 */
	uint8_t tcp_rcv_wsf;	/* 1C7 */
	uint32_t stat_sn;	/* 1C8-1CB */
	uint32_t exp_stat_sn;	/* 1CC-1CF */
	uint8_t res6[0x30];	/* 1D0-1FF */
};

/*************************************************************************/

/* Flash definitions */

#define FLASH_OFFSET_SYS_INFO	0x02000000
#define FLASH_DEFAULTBLOCKSIZE	0x20000
#define FLASH_EOF_OFFSET	(FLASH_DEFAULTBLOCKSIZE-8) /* 4 bytes
							    * for EOF
							    * signature */

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

	uint8_t entryStatus;
	uint8_t systemDefined;
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
struct marker_entry {
	struct qla4_header hdr;	/* 00-03 */

	uint32_t system_defined; /* 04-07 */
	uint16_t target;	/* 08-09 */
	uint16_t modifier;	/* 0A-0B */
#define MM_LUN_RESET	     0

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

struct passthru0 {
	struct qla4_header hdr;		       /* 00-03 */
	uint32_t handle;	/* 04-07 */
	uint16_t target;	/* 08-09 */
	uint16_t connectionID;	/* 0A-0B */
#define ISNS_DEFAULT_SERVER_CONN_ID	((uint16_t)0x8000)

	uint16_t controlFlags;	/* 0C-0D */
#define PT_FLAG_ETHERNET_FRAME		0x8000
#define PT_FLAG_ISNS_PDU		0x8000
#define PT_FLAG_SEND_BUFFER		0x0200
#define PT_FLAG_WAIT_4_RESPONSE		0x0100

	uint16_t timeout;	/* 0E-0F */
#define PT_DEFAULT_TIMEOUT		30 /* seconds */

	struct data_seg_a64 outDataSeg64;	/* 10-1B */
	uint32_t res1;		/* 1C-1F */
	struct data_seg_a64 inDataSeg64;	/* 20-2B */
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

#endif /*  _QLA4X_FW_H */
