/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * QLogic iSCSI HBA Driver
 * Copyright (c)  2003-2013 QLogic Corporation
 */

#ifndef __QL4_DEF_H
#define __QL4_DEF_H

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/dmapool.h>
#include <linux/mempool.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/bsg-lib.h>
#include <linux/vmalloc.h>

#include <net/tcp.h>
#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_iscsi.h>
#include <scsi/scsi_bsg_iscsi.h>
#include <scsi/scsi_netlink.h>
#include <scsi/libiscsi.h>

#include "ql4_dbg.h"
#include "ql4_nx.h"
#include "ql4_fw.h"
#include "ql4_nvram.h"
#include "ql4_83xx.h"

#ifndef PCI_DEVICE_ID_QLOGIC_ISP4010
#define PCI_DEVICE_ID_QLOGIC_ISP4010	0x4010
#endif

#ifndef PCI_DEVICE_ID_QLOGIC_ISP4022
#define PCI_DEVICE_ID_QLOGIC_ISP4022	0x4022
#endif

#ifndef PCI_DEVICE_ID_QLOGIC_ISP4032
#define PCI_DEVICE_ID_QLOGIC_ISP4032	0x4032
#endif

#ifndef PCI_DEVICE_ID_QLOGIC_ISP8022
#define PCI_DEVICE_ID_QLOGIC_ISP8022	0x8022
#endif

#ifndef PCI_DEVICE_ID_QLOGIC_ISP8324
#define PCI_DEVICE_ID_QLOGIC_ISP8324	0x8032
#endif

#ifndef PCI_DEVICE_ID_QLOGIC_ISP8042
#define PCI_DEVICE_ID_QLOGIC_ISP8042	0x8042
#endif

#define ISP4XXX_PCI_FN_1	0x1
#define ISP4XXX_PCI_FN_2	0x3

#define QLA_SUCCESS			0
#define QLA_ERROR			1
#define STATUS(status)		status == QLA_ERROR ? "FAILED" : "SUCCEEDED"

/*
 * Data bit definitions
 */
#define BIT_0	0x1
#define BIT_1	0x2
#define BIT_2	0x4
#define BIT_3	0x8
#define BIT_4	0x10
#define BIT_5	0x20
#define BIT_6	0x40
#define BIT_7	0x80
#define BIT_8	0x100
#define BIT_9	0x200
#define BIT_10	0x400
#define BIT_11	0x800
#define BIT_12	0x1000
#define BIT_13	0x2000
#define BIT_14	0x4000
#define BIT_15	0x8000
#define BIT_16	0x10000
#define BIT_17	0x20000
#define BIT_18	0x40000
#define BIT_19	0x80000
#define BIT_20	0x100000
#define BIT_21	0x200000
#define BIT_22	0x400000
#define BIT_23	0x800000
#define BIT_24	0x1000000
#define BIT_25	0x2000000
#define BIT_26	0x4000000
#define BIT_27	0x8000000
#define BIT_28	0x10000000
#define BIT_29	0x20000000
#define BIT_30	0x40000000
#define BIT_31	0x80000000

/**
 * Macros to help code, maintain, etc.
 **/
#define ql4_printk(level, ha, format, arg...) \
	dev_printk(level , &((ha)->pdev->dev) , format , ## arg)


/*
 * Host adapter default definitions
 ***********************************/
#define MAX_HBAS		16
#define MAX_BUSES		1
#define MAX_TARGETS		MAX_DEV_DB_ENTRIES
#define MAX_LUNS		0xffff
#define MAX_AEN_ENTRIES		MAX_DEV_DB_ENTRIES
#define MAX_DDB_ENTRIES		MAX_DEV_DB_ENTRIES
#define MAX_PDU_ENTRIES		32
#define INVALID_ENTRY		0xFFFF
#define MAX_CMDS_TO_RISC	1024
#define MAX_SRBS		MAX_CMDS_TO_RISC
#define MBOX_AEN_REG_COUNT	8
#define MAX_INIT_RETRIES	5

/*
 * Buffer sizes
 */
#define REQUEST_QUEUE_DEPTH		MAX_CMDS_TO_RISC
#define RESPONSE_QUEUE_DEPTH		64
#define QUEUE_SIZE			64
#define DMA_BUFFER_SIZE			512
#define IOCB_HIWAT_CUSHION		4

/*
 * Misc
 */
#define MAC_ADDR_LEN			6	/* in bytes */
#define IP_ADDR_LEN			4	/* in bytes */
#define IPv6_ADDR_LEN			16	/* IPv6 address size */
#define DRIVER_NAME			"qla4xxx"

#define MAX_LINKED_CMDS_PER_LUN		3
#define MAX_REQS_SERVICED_PER_INTR	1

#define ISCSI_IPADDR_SIZE		4	/* IP address size */
#define ISCSI_ALIAS_SIZE		32	/* ISCSI Alias name size */
#define ISCSI_NAME_SIZE			0xE0	/* ISCSI Name size */

#define QL4_SESS_RECOVERY_TMO		120	/* iSCSI session */
						/* recovery timeout */

#define LSDW(x) ((u32)((u64)(x)))
#define MSDW(x) ((u32)((((u64)(x)) >> 16) >> 16))

#define DEV_DB_NON_PERSISTENT	0
#define DEV_DB_PERSISTENT	1

#define QL4_ISP_REG_DISCONNECT 0xffffffffU

#define COPY_ISID(dst_isid, src_isid) {			\
	int i, j;					\
	for (i = 0, j = ISID_SIZE - 1; i < ISID_SIZE;)	\
		dst_isid[i++] = src_isid[j--];		\
}

#define SET_BITVAL(o, n, v) {	\
	if (o)			\
		n |= v;		\
	else			\
		n &= ~v;	\
}

#define OP_STATE(o, f, p) {			\
	p = (o & f) ? "enable" : "disable";	\
}

/*
 * Retry & Timeout Values
 */
#define MBOX_TOV			60
#define SOFT_RESET_TOV			30
#define RESET_INTR_TOV			3
#define SEMAPHORE_TOV			10
#define ADAPTER_INIT_TOV		30
#define ADAPTER_RESET_TOV		180
#define EXTEND_CMD_TOV			60
#define WAIT_CMD_TOV			5
#define EH_WAIT_CMD_TOV			120
#define FIRMWARE_UP_TOV			60
#define RESET_FIRMWARE_TOV		30
#define LOGOUT_TOV			10
#define IOCB_TOV_MARGIN			10
#define RELOGIN_TOV			18
#define ISNS_DEREG_TOV			5
#define HBA_ONLINE_TOV			30
#define DISABLE_ACB_TOV			30
#define IP_CONFIG_TOV			30
#define LOGIN_TOV			12
#define BOOT_LOGIN_RESP_TOV		60

#define MAX_RESET_HA_RETRIES		2
#define FW_ALIVE_WAIT_TOV		3
#define IDC_EXTEND_TOV			8
#define IDC_COMP_TOV			5
#define LINK_UP_COMP_TOV		30

/*
 * Note: the data structure below does not have a struct iscsi_cmd member since
 * the qla4xxx driver does not use libiscsi for SCSI I/O.
 */
struct qla4xxx_cmd_priv {
	struct srb *srb;
};

static inline struct qla4xxx_cmd_priv *qla4xxx_cmd_priv(struct scsi_cmnd *cmd)
{
	return scsi_cmd_priv(cmd);
}

/*
 * SCSI Request Block structure (srb) that is associated with each scsi_cmnd.
 */
struct srb {
	struct list_head list;	/* (8)	 */
	struct scsi_qla_host *ha;	/* HA the SP is queued on */
	struct ddb_entry *ddb;
	uint16_t flags;		/* (1) Status flags. */

#define SRB_DMA_VALID		BIT_3	/* DMA Buffer mapped. */
#define SRB_GOT_SENSE		BIT_4	/* sense data received. */
	uint8_t state;		/* (1) Status flags. */

#define SRB_NO_QUEUE_STATE	 0	/* Request is in between states */
#define SRB_FREE_STATE		 1
#define SRB_ACTIVE_STATE	 3
#define SRB_ACTIVE_TIMEOUT_STATE 4
#define SRB_SUSPENDED_STATE	 7	/* Request in suspended state */

	struct scsi_cmnd *cmd;	/* (4) SCSI command block */
	dma_addr_t dma_handle;	/* (4) for unmap of single transfers */
	struct kref srb_ref;	/* reference count for this srb */
	uint8_t err_id;		/* error id */
#define SRB_ERR_PORT	   1	/* Request failed because "port down" */
#define SRB_ERR_LOOP	   2	/* Request failed because "loop down" */
#define SRB_ERR_DEVICE	   3	/* Request failed because "device error" */
#define SRB_ERR_OTHER	   4

	uint16_t reserved;
	uint16_t iocb_tov;
	uint16_t iocb_cnt;	/* Number of used iocbs */
	uint16_t cc_stat;

	/* Used for extended sense / status continuation */
	uint8_t *req_sense_ptr;
	uint16_t req_sense_len;
	uint16_t reserved2;
};

/* Mailbox request block structure */
struct mrb {
	struct scsi_qla_host *ha;
	struct mbox_cmd_iocb *mbox;
	uint32_t mbox_cmd;
	uint16_t iocb_cnt;		/* Number of used iocbs */
	uint32_t pid;
};

/*
 * Asynchronous Event Queue structure
 */
struct aen {
        uint32_t mbox_sts[MBOX_AEN_REG_COUNT];
};

struct ql4_aen_log {
        int count;
        struct aen entry[MAX_AEN_ENTRIES];
};

/*
 * Device Database (DDB) structure
 */
struct ddb_entry {
	struct scsi_qla_host *ha;
	struct iscsi_cls_session *sess;
	struct iscsi_cls_conn *conn;

	uint16_t fw_ddb_index;	/* DDB firmware index */
	uint32_t fw_ddb_device_state; /* F/W Device State  -- see ql4_fw.h */
	uint16_t ddb_type;
#define FLASH_DDB 0x01

	struct dev_db_entry fw_ddb_entry;
	int (*unblock_sess)(struct iscsi_cls_session *cls_session);
	int (*ddb_change)(struct scsi_qla_host *ha, uint32_t fw_ddb_index,
			  struct ddb_entry *ddb_entry, uint32_t state);

	/* Driver Re-login  */
	unsigned long flags;		  /* DDB Flags */
#define DDB_CONN_CLOSE_FAILURE		0 /* 0x00000001 */

	uint16_t default_relogin_timeout; /*  Max time to wait for
					   *  relogin to complete */
	atomic_t retry_relogin_timer;	  /* Min Time between relogins
					   * (4000 only) */
	atomic_t relogin_timer;		  /* Max Time to wait for
					   * relogin to complete */
	atomic_t relogin_retry_count;	  /* Num of times relogin has been
					   * retried */
	uint32_t default_time2wait;	  /* Default Min time between
					   * relogins (+aens) */
	uint16_t chap_tbl_idx;
};

struct qla_ddb_index {
	struct list_head list;
	uint16_t fw_ddb_idx;
	uint16_t flash_ddb_idx;
	struct dev_db_entry fw_ddb;
	uint8_t flash_isid[6];
};

#define DDB_IPADDR_LEN 64

struct ql4_tuple_ddb {
	int port;
	int tpgt;
	char ip_addr[DDB_IPADDR_LEN];
	char iscsi_name[ISCSI_NAME_SIZE];
	uint16_t options;
#define DDB_OPT_IPV6 0x0e0e
#define DDB_OPT_IPV4 0x0f0f
	uint8_t isid[6];
};

/*
 * DDB states.
 */
#define DDB_STATE_DEAD		0	/* We can no longer talk to
					 * this device */
#define DDB_STATE_ONLINE	1	/* Device ready to accept
					 * commands */
#define DDB_STATE_MISSING	2	/* Device logged off, trying
					 * to re-login */

/*
 * DDB flags.
 */
#define DF_RELOGIN		0	/* Relogin to device */
#define DF_BOOT_TGT		1	/* Boot target entry */
#define DF_ISNS_DISCOVERED	2	/* Device was discovered via iSNS */
#define DF_FO_MASKED		3
#define DF_DISABLE_RELOGIN		4	/* Disable relogin to device */

enum qla4_work_type {
	QLA4_EVENT_AEN,
	QLA4_EVENT_PING_STATUS,
};

struct qla4_work_evt {
	struct list_head list;
	enum qla4_work_type type;
	union {
		struct {
			enum iscsi_host_event_code code;
			uint32_t data_size;
			uint8_t data[];
		} aen;
		struct {
			uint32_t status;
			uint32_t pid;
			uint32_t data_size;
			uint8_t data[];
		} ping;
	} u;
};

struct ql82xx_hw_data {
	/* Offsets for flash/nvram access (set to ~0 if not used). */
	uint32_t flash_conf_off;
	uint32_t flash_data_off;

	uint32_t fdt_wrt_disable;
	uint32_t fdt_erase_cmd;
	uint32_t fdt_block_size;
	uint32_t fdt_unprotect_sec_cmd;
	uint32_t fdt_protect_sec_cmd;

	uint32_t flt_region_flt;
	uint32_t flt_region_fdt;
	uint32_t flt_region_boot;
	uint32_t flt_region_bootload;
	uint32_t flt_region_fw;

	uint32_t flt_iscsi_param;
	uint32_t flt_region_chap;
	uint32_t flt_chap_size;
	uint32_t flt_region_ddb;
	uint32_t flt_ddb_size;
};

struct qla4_8xxx_legacy_intr_set {
	uint32_t int_vec_bit;
	uint32_t tgt_status_reg;
	uint32_t tgt_mask_reg;
	uint32_t pci_int_reg;
};

/* MSI-X Support */
#define QLA_MSIX_ENTRIES	2

/*
 * ISP Operations
 */
struct isp_operations {
	int (*iospace_config) (struct scsi_qla_host *ha);
	void (*pci_config) (struct scsi_qla_host *);
	void (*disable_intrs) (struct scsi_qla_host *);
	void (*enable_intrs) (struct scsi_qla_host *);
	int (*start_firmware) (struct scsi_qla_host *);
	int (*restart_firmware) (struct scsi_qla_host *);
	irqreturn_t (*intr_handler) (int , void *);
	void (*interrupt_service_routine) (struct scsi_qla_host *, uint32_t);
	int (*need_reset) (struct scsi_qla_host *);
	int (*reset_chip) (struct scsi_qla_host *);
	int (*reset_firmware) (struct scsi_qla_host *);
	void (*queue_iocb) (struct scsi_qla_host *);
	void (*complete_iocb) (struct scsi_qla_host *);
	uint16_t (*rd_shdw_req_q_out) (struct scsi_qla_host *);
	uint16_t (*rd_shdw_rsp_q_in) (struct scsi_qla_host *);
	int (*get_sys_info) (struct scsi_qla_host *);
	uint32_t (*rd_reg_direct) (struct scsi_qla_host *, ulong);
	void (*wr_reg_direct) (struct scsi_qla_host *, ulong, uint32_t);
	int (*rd_reg_indirect) (struct scsi_qla_host *, uint32_t, uint32_t *);
	int (*wr_reg_indirect) (struct scsi_qla_host *, uint32_t, uint32_t);
	int (*idc_lock) (struct scsi_qla_host *); /* Context: task, can sleep */
	void (*idc_unlock) (struct scsi_qla_host *);
	void (*rom_lock_recovery) (struct scsi_qla_host *); /* Context: task, can sleep */
	void (*queue_mailbox_command) (struct scsi_qla_host *, uint32_t *, int);
	void (*process_mailbox_interrupt) (struct scsi_qla_host *, int);
};

struct ql4_mdump_size_table {
	uint32_t size;
	uint32_t size_cmask_02;
	uint32_t size_cmask_04;
	uint32_t size_cmask_08;
	uint32_t size_cmask_10;
	uint32_t size_cmask_FF;
	uint32_t version;
};

/*qla4xxx ipaddress configuration details */
struct ipaddress_config {
	uint16_t ipv4_options;
	uint16_t tcp_options;
	uint16_t ipv4_vlan_tag;
	uint8_t ipv4_addr_state;
	uint8_t ip_address[IP_ADDR_LEN];
	uint8_t subnet_mask[IP_ADDR_LEN];
	uint8_t gateway[IP_ADDR_LEN];
	uint32_t ipv6_options;
	uint32_t ipv6_addl_options;
	uint8_t ipv6_link_local_state;
	uint8_t ipv6_addr0_state;
	uint8_t ipv6_addr1_state;
	uint8_t ipv6_default_router_state;
	uint16_t ipv6_vlan_tag;
	struct in6_addr ipv6_link_local_addr;
	struct in6_addr ipv6_addr0;
	struct in6_addr ipv6_addr1;
	struct in6_addr ipv6_default_router_addr;
	uint16_t eth_mtu_size;
	uint16_t ipv4_port;
	uint16_t ipv6_port;
	uint8_t control;
	uint16_t ipv6_tcp_options;
	uint8_t tcp_wsf;
	uint8_t ipv6_tcp_wsf;
	uint8_t ipv4_tos;
	uint8_t ipv4_cache_id;
	uint8_t ipv6_cache_id;
	uint8_t ipv4_alt_cid_len;
	uint8_t ipv4_alt_cid[11];
	uint8_t ipv4_vid_len;
	uint8_t ipv4_vid[11];
	uint8_t ipv4_ttl;
	uint16_t ipv6_flow_lbl;
	uint8_t ipv6_traffic_class;
	uint8_t ipv6_hop_limit;
	uint32_t ipv6_nd_reach_time;
	uint32_t ipv6_nd_rexmit_timer;
	uint32_t ipv6_nd_stale_timeout;
	uint8_t ipv6_dup_addr_detect_count;
	uint32_t ipv6_gw_advrt_mtu;
	uint16_t def_timeout;
	uint8_t abort_timer;
	uint16_t iscsi_options;
	uint16_t iscsi_max_pdu_size;
	uint16_t iscsi_first_burst_len;
	uint16_t iscsi_max_outstnd_r2t;
	uint16_t iscsi_max_burst_len;
	uint8_t iscsi_name[224];
};

#define QL4_CHAP_MAX_NAME_LEN 256
#define QL4_CHAP_MAX_SECRET_LEN 100
#define LOCAL_CHAP	0
#define BIDI_CHAP	1

struct ql4_chap_format {
	u8  intr_chap_name[QL4_CHAP_MAX_NAME_LEN];
	u8  intr_secret[QL4_CHAP_MAX_SECRET_LEN];
	u8  target_chap_name[QL4_CHAP_MAX_NAME_LEN];
	u8  target_secret[QL4_CHAP_MAX_SECRET_LEN];
	u16 intr_chap_name_length;
	u16 intr_secret_length;
	u16 target_chap_name_length;
	u16 target_secret_length;
};

struct ip_address_format {
	u8 ip_type;
	u8 ip_address[16];
};

struct	ql4_conn_info {
	u16	dest_port;
	struct	ip_address_format dest_ipaddr;
	struct	ql4_chap_format chap;
};

struct ql4_boot_session_info {
	u8	target_name[224];
	struct	ql4_conn_info conn_list[1];
};

struct ql4_boot_tgt_info {
	struct ql4_boot_session_info boot_pri_sess;
	struct ql4_boot_session_info boot_sec_sess;
};

/*
 * Linux Host Adapter structure
 */
struct scsi_qla_host {
	/* Linux adapter configuration data */
	unsigned long flags;

#define AF_ONLINE			0 /* 0x00000001 */
#define AF_INIT_DONE			1 /* 0x00000002 */
#define AF_MBOX_COMMAND			2 /* 0x00000004 */
#define AF_MBOX_COMMAND_DONE		3 /* 0x00000008 */
#define AF_ST_DISCOVERY_IN_PROGRESS	4 /* 0x00000010 */
#define AF_INTERRUPTS_ON		6 /* 0x00000040 */
#define AF_GET_CRASH_RECORD		7 /* 0x00000080 */
#define AF_LINK_UP			8 /* 0x00000100 */
#define AF_LOOPBACK			9 /* 0x00000200 */
#define AF_IRQ_ATTACHED			10 /* 0x00000400 */
#define AF_DISABLE_ACB_COMPLETE		11 /* 0x00000800 */
#define AF_HA_REMOVAL			12 /* 0x00001000 */
#define AF_MBOX_COMMAND_NOPOLL		18 /* 0x00040000 */
#define AF_FW_RECOVERY			19 /* 0x00080000 */
#define AF_EEH_BUSY			20 /* 0x00100000 */
#define AF_PCI_CHANNEL_IO_PERM_FAILURE	21 /* 0x00200000 */
#define AF_BUILD_DDB_LIST		22 /* 0x00400000 */
#define AF_82XX_FW_DUMPED		24 /* 0x01000000 */
#define AF_8XXX_RST_OWNER		25 /* 0x02000000 */
#define AF_82XX_DUMP_READING		26 /* 0x04000000 */
#define AF_83XX_IOCB_INTR_ON		28 /* 0x10000000 */
#define AF_83XX_MBOX_INTR_ON		29 /* 0x20000000 */

	unsigned long dpc_flags;

#define DPC_RESET_HA			1 /* 0x00000002 */
#define DPC_RETRY_RESET_HA		2 /* 0x00000004 */
#define DPC_RELOGIN_DEVICE		3 /* 0x00000008 */
#define DPC_RESET_HA_FW_CONTEXT		4 /* 0x00000010 */
#define DPC_RESET_HA_INTR		5 /* 0x00000020 */
#define DPC_ISNS_RESTART		7 /* 0x00000080 */
#define DPC_AEN				9 /* 0x00000200 */
#define DPC_GET_DHCP_IP_ADDR		15 /* 0x00008000 */
#define DPC_LINK_CHANGED		18 /* 0x00040000 */
#define DPC_RESET_ACTIVE		20 /* 0x00100000 */
#define DPC_HA_UNRECOVERABLE		21 /* 0x00200000 ISP-82xx only*/
#define DPC_HA_NEED_QUIESCENT		22 /* 0x00400000 ISP-82xx only*/
#define DPC_POST_IDC_ACK		23 /* 0x00800000 */
#define DPC_RESTORE_ACB			24 /* 0x01000000 */
#define DPC_SYSFS_DDB_EXPORT		25 /* 0x02000000 */

	struct Scsi_Host *host; /* pointer to host data */
	uint32_t tot_ddbs;

	uint16_t iocb_cnt;
	uint16_t iocb_hiwat;

	/* SRB cache. */
#define SRB_MIN_REQ	128
	mempool_t *srb_mempool;

	/* pci information */
	struct pci_dev *pdev;

	struct isp_reg __iomem *reg; /* Base I/O address */
	unsigned long pio_address;
	unsigned long pio_length;
#define MIN_IOBASE_LEN		0x100

	uint16_t req_q_count;

	unsigned long host_no;

	/* NVRAM registers */
	struct eeprom_data *nvram;
	spinlock_t hardware_lock ____cacheline_aligned;
	uint32_t eeprom_cmd_data;

	/* Counters for general statistics */
	uint64_t isr_count;
	uint64_t adapter_error_count;
	uint64_t device_error_count;
	uint64_t total_io_count;
	uint64_t total_mbytes_xferred;
	uint64_t link_failure_count;
	uint64_t invalid_crc_count;
	uint32_t bytes_xfered;
	uint32_t spurious_int_count;
	uint32_t aborted_io_count;
	uint32_t io_timeout_count;
	uint32_t mailbox_timeout_count;
	uint32_t seconds_since_last_intr;
	uint32_t seconds_since_last_heartbeat;
	uint32_t mac_index;

	/* Info Needed for Management App */
	/* --- From GetFwVersion --- */
	uint32_t firmware_version[2];
	uint32_t patch_number;
	uint32_t build_number;
	uint32_t board_id;

	/* --- From Init_FW --- */
	/* init_cb_t *init_cb; */
	uint16_t firmware_options;
	uint8_t alias[32];
	uint8_t name_string[256];
	uint8_t heartbeat_interval;

	/* --- From FlashSysInfo --- */
	uint8_t my_mac[MAC_ADDR_LEN];
	uint8_t serial_number[16];
	uint16_t port_num;
	/* --- From GetFwState --- */
	uint32_t firmware_state;
	uint32_t addl_fw_state;

	/* Linux kernel thread */
	struct workqueue_struct *dpc_thread;
	struct work_struct dpc_work;

	/* Linux timer thread */
	struct timer_list timer;
	uint32_t timer_active;

	/* Recovery Timers */
	atomic_t check_relogin_timeouts;
	uint32_t retry_reset_ha_cnt;
	uint32_t isp_reset_timer;	/* reset test timer */
	uint32_t nic_reset_timer;	/* simulated nic reset test timer */
	int eh_start;
	struct list_head free_srb_q;
	uint16_t free_srb_q_count;
	uint16_t num_srbs_allocated;

	/* DMA Memory Block */
	void *queues;
	dma_addr_t queues_dma;
	unsigned long queues_len;

#define MEM_ALIGN_VALUE \
	    ((max(REQUEST_QUEUE_DEPTH, RESPONSE_QUEUE_DEPTH)) * \
	     sizeof(struct queue_entry))
	/* request and response queue variables */
	dma_addr_t request_dma;
	struct queue_entry *request_ring;
	struct queue_entry *request_ptr;
	dma_addr_t response_dma;
	struct queue_entry *response_ring;
	struct queue_entry *response_ptr;
	dma_addr_t shadow_regs_dma;
	struct shadow_regs *shadow_regs;
	uint16_t request_in;	/* Current indexes. */
	uint16_t request_out;
	uint16_t response_in;
	uint16_t response_out;

	/* aen queue variables */
	uint16_t aen_q_count;	/* Number of available aen_q entries */
	uint16_t aen_in;	/* Current indexes */
	uint16_t aen_out;
	struct aen aen_q[MAX_AEN_ENTRIES];

	struct ql4_aen_log aen_log;/* tracks all aens */

	/* This mutex protects several threads to do mailbox commands
	 * concurrently.
	 */
	struct mutex  mbox_sem;

	/* temporary mailbox status registers */
	volatile uint8_t mbox_status_count;
	volatile uint32_t mbox_status[MBOX_REG_COUNT];

	/* FW ddb index map */
	struct ddb_entry *fw_ddb_index_map[MAX_DDB_ENTRIES];

	/* Saved srb for status continuation entry processing */
	struct srb *status_srb;

	uint8_t acb_version;

	/* qla82xx specific fields */
	struct device_reg_82xx  __iomem *qla4_82xx_reg; /* Base I/O address */
	unsigned long nx_pcibase;	/* Base I/O address */
	uint8_t *nx_db_rd_ptr;		/* Doorbell read pointer */
	unsigned long nx_db_wr_ptr;	/* Door bell write pointer */
	unsigned long first_page_group_start;
	unsigned long first_page_group_end;

	uint32_t crb_win;
	uint32_t curr_window;
	uint32_t ddr_mn_window;
	unsigned long mn_win_crb;
	unsigned long ms_win_crb;
	int qdr_sn_window;
	rwlock_t hw_lock;
	uint16_t func_num;
	int link_width;

	struct qla4_8xxx_legacy_intr_set nx_legacy_intr;
	u32 nx_crb_mask;

	uint8_t revision_id;
	uint32_t fw_heartbeat_counter;

	struct isp_operations *isp_ops;
	struct ql82xx_hw_data hw;

	uint32_t nx_dev_init_timeout;
	uint32_t nx_reset_timeout;
	void *fw_dump;
	uint32_t fw_dump_size;
	uint32_t fw_dump_capture_mask;
	void *fw_dump_tmplt_hdr;
	uint32_t fw_dump_tmplt_size;
	uint32_t fw_dump_skip_size;

	struct completion mbx_intr_comp;

	struct ipaddress_config ip_config;
	struct iscsi_iface *iface_ipv4;
	struct iscsi_iface *iface_ipv6_0;
	struct iscsi_iface *iface_ipv6_1;

	/* --- From About Firmware --- */
	struct about_fw_info fw_info;
	uint32_t fw_uptime_secs;  /* seconds elapsed since fw bootup */
	uint32_t fw_uptime_msecs; /* milliseconds beyond elapsed seconds */
	uint16_t def_timeout; /* Default login timeout */

	uint32_t flash_state;
#define	QLFLASH_WAITING		0
#define	QLFLASH_READING		1
#define	QLFLASH_WRITING		2
	struct dma_pool *chap_dma_pool;
	uint8_t *chap_list; /* CHAP table cache */
	struct mutex  chap_sem;

#define CHAP_DMA_BLOCK_SIZE    512
	struct workqueue_struct *task_wq;
	unsigned long ddb_idx_map[MAX_DDB_ENTRIES / BITS_PER_LONG];
#define SYSFS_FLAG_FW_SEL_BOOT 2
	struct iscsi_boot_kset *boot_kset;
	struct ql4_boot_tgt_info boot_tgt;
	uint16_t phy_port_num;
	uint16_t phy_port_cnt;
	uint16_t iscsi_pci_func_cnt;
	uint8_t model_name[16];
	struct completion disable_acb_comp;
	struct dma_pool *fw_ddb_dma_pool;
#define DDB_DMA_BLOCK_SIZE 512
	uint16_t pri_ddb_idx;
	uint16_t sec_ddb_idx;
	int is_reset;
	uint16_t temperature;

	/* event work list */
	struct list_head work_list;
	spinlock_t work_lock;

	/* mbox iocb */
#define MAX_MRB		128
	struct mrb *active_mrb_array[MAX_MRB];
	uint32_t mrb_index;

	uint32_t *reg_tbl;
	struct qla4_83xx_reset_template reset_tmplt;
	struct device_reg_83xx  __iomem *qla4_83xx_reg; /* Base I/O address
							   for ISP8324 and
							   and ISP8042 */
	uint32_t pf_bit;
	struct qla4_83xx_idc_information idc_info;
	struct addr_ctrl_blk *saved_acb;
	int notify_idc_comp;
	int notify_link_up_comp;
	int idc_extend_tmo;
	struct completion idc_comp;
	struct completion link_up_comp;
};

struct ql4_task_data {
	struct scsi_qla_host *ha;
	uint8_t iocb_req_cnt;
	dma_addr_t data_dma;
	void *req_buffer;
	dma_addr_t req_dma;
	uint32_t req_len;
	void *resp_buffer;
	dma_addr_t resp_dma;
	uint32_t resp_len;
	struct iscsi_task *task;
	struct passthru_status sts;
	struct work_struct task_work;
};

struct qla_endpoint {
	struct Scsi_Host *host;
	struct sockaddr_storage dst_addr;
};

struct qla_conn {
	struct qla_endpoint *qla_ep;
};

static inline int is_ipv4_enabled(struct scsi_qla_host *ha)
{
	return ((ha->ip_config.ipv4_options & IPOPT_IPV4_PROTOCOL_ENABLE) != 0);
}

static inline int is_ipv6_enabled(struct scsi_qla_host *ha)
{
	return ((ha->ip_config.ipv6_options &
		IPV6_OPT_IPV6_PROTOCOL_ENABLE) != 0);
}

static inline int is_qla4010(struct scsi_qla_host *ha)
{
	return ha->pdev->device == PCI_DEVICE_ID_QLOGIC_ISP4010;
}

static inline int is_qla4022(struct scsi_qla_host *ha)
{
	return ha->pdev->device == PCI_DEVICE_ID_QLOGIC_ISP4022;
}

static inline int is_qla4032(struct scsi_qla_host *ha)
{
	return ha->pdev->device == PCI_DEVICE_ID_QLOGIC_ISP4032;
}

static inline int is_qla40XX(struct scsi_qla_host *ha)
{
	return is_qla4032(ha) || is_qla4022(ha) || is_qla4010(ha);
}

static inline int is_qla8022(struct scsi_qla_host *ha)
{
	return ha->pdev->device == PCI_DEVICE_ID_QLOGIC_ISP8022;
}

static inline int is_qla8032(struct scsi_qla_host *ha)
{
	return ha->pdev->device == PCI_DEVICE_ID_QLOGIC_ISP8324;
}

static inline int is_qla8042(struct scsi_qla_host *ha)
{
	return ha->pdev->device == PCI_DEVICE_ID_QLOGIC_ISP8042;
}

static inline int is_qla80XX(struct scsi_qla_host *ha)
{
	return is_qla8022(ha) || is_qla8032(ha) || is_qla8042(ha);
}

static inline int is_aer_supported(struct scsi_qla_host *ha)
{
	return ((ha->pdev->device == PCI_DEVICE_ID_QLOGIC_ISP8022) ||
		(ha->pdev->device == PCI_DEVICE_ID_QLOGIC_ISP8324) ||
		(ha->pdev->device == PCI_DEVICE_ID_QLOGIC_ISP8042));
}

static inline int adapter_up(struct scsi_qla_host *ha)
{
	return (test_bit(AF_ONLINE, &ha->flags) != 0) &&
	       (test_bit(AF_LINK_UP, &ha->flags) != 0) &&
	       (!test_bit(AF_LOOPBACK, &ha->flags));
}

static inline struct scsi_qla_host* to_qla_host(struct Scsi_Host *shost)
{
	return (struct scsi_qla_host *)iscsi_host_priv(shost);
}

static inline void __iomem* isp_semaphore(struct scsi_qla_host *ha)
{
	return (is_qla4010(ha) ?
		&ha->reg->u1.isp4010.nvram :
		&ha->reg->u1.isp4022.semaphore);
}

static inline void __iomem* isp_nvram(struct scsi_qla_host *ha)
{
	return (is_qla4010(ha) ?
		&ha->reg->u1.isp4010.nvram :
		&ha->reg->u1.isp4022.nvram);
}

static inline void __iomem* isp_ext_hw_conf(struct scsi_qla_host *ha)
{
	return (is_qla4010(ha) ?
		&ha->reg->u2.isp4010.ext_hw_conf :
		&ha->reg->u2.isp4022.p0.ext_hw_conf);
}

static inline void __iomem* isp_port_status(struct scsi_qla_host *ha)
{
	return (is_qla4010(ha) ?
		&ha->reg->u2.isp4010.port_status :
		&ha->reg->u2.isp4022.p0.port_status);
}

static inline void __iomem* isp_port_ctrl(struct scsi_qla_host *ha)
{
	return (is_qla4010(ha) ?
		&ha->reg->u2.isp4010.port_ctrl :
		&ha->reg->u2.isp4022.p0.port_ctrl);
}

static inline void __iomem* isp_port_error_status(struct scsi_qla_host *ha)
{
	return (is_qla4010(ha) ?
		&ha->reg->u2.isp4010.port_err_status :
		&ha->reg->u2.isp4022.p0.port_err_status);
}

static inline void __iomem * isp_gp_out(struct scsi_qla_host *ha)
{
	return (is_qla4010(ha) ?
		&ha->reg->u2.isp4010.gp_out :
		&ha->reg->u2.isp4022.p0.gp_out);
}

static inline int eeprom_ext_hw_conf_offset(struct scsi_qla_host *ha)
{
	return (is_qla4010(ha) ?
		offsetof(struct eeprom_data, isp4010.ext_hw_conf) / 2 :
		offsetof(struct eeprom_data, isp4022.ext_hw_conf) / 2);
}

int ql4xxx_sem_spinlock(struct scsi_qla_host * ha, u32 sem_mask, u32 sem_bits);
void ql4xxx_sem_unlock(struct scsi_qla_host * ha, u32 sem_mask);
int ql4xxx_sem_lock(struct scsi_qla_host * ha, u32 sem_mask, u32 sem_bits);

static inline int ql4xxx_lock_flash(struct scsi_qla_host *a)
{
	if (is_qla4010(a))
		return ql4xxx_sem_spinlock(a, QL4010_FLASH_SEM_MASK,
					   QL4010_FLASH_SEM_BITS);
	else
		return ql4xxx_sem_spinlock(a, QL4022_FLASH_SEM_MASK,
					   (QL4022_RESOURCE_BITS_BASE_CODE |
					    (a->mac_index)) << 13);
}

static inline void ql4xxx_unlock_flash(struct scsi_qla_host *a)
{
	if (is_qla4010(a))
		ql4xxx_sem_unlock(a, QL4010_FLASH_SEM_MASK);
	else
		ql4xxx_sem_unlock(a, QL4022_FLASH_SEM_MASK);
}

static inline int ql4xxx_lock_nvram(struct scsi_qla_host *a)
{
	if (is_qla4010(a))
		return ql4xxx_sem_spinlock(a, QL4010_NVRAM_SEM_MASK,
					   QL4010_NVRAM_SEM_BITS);
	else
		return ql4xxx_sem_spinlock(a, QL4022_NVRAM_SEM_MASK,
					   (QL4022_RESOURCE_BITS_BASE_CODE |
					    (a->mac_index)) << 10);
}

static inline void ql4xxx_unlock_nvram(struct scsi_qla_host *a)
{
	if (is_qla4010(a))
		ql4xxx_sem_unlock(a, QL4010_NVRAM_SEM_MASK);
	else
		ql4xxx_sem_unlock(a, QL4022_NVRAM_SEM_MASK);
}

static inline int ql4xxx_lock_drvr(struct scsi_qla_host *a)
{
	if (is_qla4010(a))
		return ql4xxx_sem_lock(a, QL4010_DRVR_SEM_MASK,
				       QL4010_DRVR_SEM_BITS);
	else
		return ql4xxx_sem_lock(a, QL4022_DRVR_SEM_MASK,
				       (QL4022_RESOURCE_BITS_BASE_CODE |
					(a->mac_index)) << 1);
}

static inline void ql4xxx_unlock_drvr(struct scsi_qla_host *a)
{
	if (is_qla4010(a))
		ql4xxx_sem_unlock(a, QL4010_DRVR_SEM_MASK);
	else
		ql4xxx_sem_unlock(a, QL4022_DRVR_SEM_MASK);
}

static inline int ql4xxx_reset_active(struct scsi_qla_host *ha)
{
	return test_bit(DPC_RESET_ACTIVE, &ha->dpc_flags) ||
	       test_bit(DPC_RESET_HA, &ha->dpc_flags) ||
	       test_bit(DPC_RETRY_RESET_HA, &ha->dpc_flags) ||
	       test_bit(DPC_RESET_HA_INTR, &ha->dpc_flags) ||
	       test_bit(DPC_RESET_HA_FW_CONTEXT, &ha->dpc_flags) ||
	       test_bit(DPC_HA_UNRECOVERABLE, &ha->dpc_flags);

}

static inline int qla4_8xxx_rd_direct(struct scsi_qla_host *ha,
				      const uint32_t crb_reg)
{
	return ha->isp_ops->rd_reg_direct(ha, ha->reg_tbl[crb_reg]);
}

static inline void qla4_8xxx_wr_direct(struct scsi_qla_host *ha,
				       const uint32_t crb_reg,
				       const uint32_t value)
{
	ha->isp_ops->wr_reg_direct(ha, ha->reg_tbl[crb_reg], value);
}

/*---------------------------------------------------------------------------*/

/* Defines for qla4xxx_initialize_adapter() and qla4xxx_recover_adapter() */

#define INIT_ADAPTER    0
#define RESET_ADAPTER   1

#define PRESERVE_DDB_LIST	0
#define REBUILD_DDB_LIST	1

/* Defines for process_aen() */
#define PROCESS_ALL_AENS	 0
#define FLUSH_DDB_CHANGED_AENS	 1

/* Defines for udev events */
#define QL4_UEVENT_CODE_FW_DUMP		0

#endif	/*_QLA4XXX_H */
