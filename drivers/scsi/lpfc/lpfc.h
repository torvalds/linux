/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2017-2023 Broadcom. All Rights Reserved. The term *
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.     *
 * Copyright (C) 2004-2016 Emulex.  All rights reserved.           *
 * EMULEX and SLI are trademarks of Emulex.                        *
 * www.broadcom.com                                                *
 * Portions Copyright (C) 2004-2005 Christoph Hellwig              *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of version 2 of the GNU General       *
 * Public License as published by the Free Software Foundation.    *
 * This program is distributed in the hope that it will be useful. *
 * ALL EXPRESS OR IMPLIED CONDITIONS, REPRESENTATIONS AND          *
 * WARRANTIES, INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,  *
 * FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT, ARE      *
 * DISCLAIMED, EXCEPT TO THE EXTENT THAT SUCH DISCLAIMERS ARE HELD *
 * TO BE LEGALLY INVALID.  See the GNU General Public License for  *
 * more details, a copy of which can be found in the file COPYING  *
 * included with this package.                                     *
 *******************************************************************/

#include <scsi/scsi_host.h>
#include <linux/hashtable.h>
#include <linux/ktime.h>
#include <linux/workqueue.h>

#if defined(CONFIG_DEBUG_FS) && !defined(CONFIG_SCSI_LPFC_DEBUG_FS)
#define CONFIG_SCSI_LPFC_DEBUG_FS
#endif

struct lpfc_sli2_slim;

#define ELX_MODEL_NAME_SIZE	80

#define LPFC_PCI_DEV_LP		0x1
#define LPFC_PCI_DEV_OC		0x2

#define LPFC_SLI_REV2		2
#define LPFC_SLI_REV3		3
#define LPFC_SLI_REV4		4

#define LPFC_MAX_TARGET		4096	/* max number of targets supported */
#define LPFC_MAX_DISC_THREADS	64	/* max outstanding discovery els
					   requests */
#define LPFC_MAX_NS_RETRY	3	/* Number of retry attempts to contact
					   the NameServer  before giving up. */
#define LPFC_CMD_PER_LUN	3	/* max outstanding cmds per lun */
#define LPFC_DEFAULT_SG_SEG_CNT 64	/* sg element count per scsi cmnd */

#define LPFC_DEFAULT_XPSGL_SIZE	256
#define LPFC_MAX_SG_TABLESIZE	0xffff
#define LPFC_MIN_SG_SLI4_BUF_SZ	0x800	/* based on LPFC_DEFAULT_SG_SEG_CNT */
#define LPFC_MAX_BG_SLI4_SEG_CNT_DIF 128 /* sg element count for BlockGuard */
#define LPFC_MAX_SG_SEG_CNT_DIF 512	/* sg element count per scsi cmnd  */
#define LPFC_MAX_SG_SEG_CNT	4096	/* sg element count per scsi cmnd */
#define LPFC_MIN_SG_SEG_CNT	32	/* sg element count per scsi cmnd */
#define LPFC_MAX_SGL_SEG_CNT	512	/* SGL element count per scsi cmnd */
#define LPFC_MAX_BPL_SEG_CNT	4096	/* BPL element count per scsi cmnd */
#define LPFC_MAX_NVME_SEG_CNT	256	/* max SGL element cnt per NVME cmnd */

#define LPFC_MAX_SGE_SIZE       0x80000000 /* Maximum data allowed in a SGE */
#define LPFC_IOCB_LIST_CNT	2250	/* list of IOCBs for fast-path usage. */
#define LPFC_Q_RAMP_UP_INTERVAL 120     /* lun q_depth ramp up interval */
#define LPFC_VNAME_LEN		100	/* vport symbolic name length */
#define LPFC_TGTQ_RAMPUP_PCENT	5	/* Target queue rampup in percentage */
#define LPFC_MIN_TGT_QDEPTH	10
#define LPFC_MAX_TGT_QDEPTH	0xFFFF

/*
 * Following time intervals are used of adjusting SCSI device
 * queue depths when there are driver resource error or Firmware
 * resource error.
 */
/* 1 Second */
#define QUEUE_RAMP_DOWN_INTERVAL	(msecs_to_jiffies(1000 * 1))

/* Number of exchanges reserved for discovery to complete */
#define LPFC_DISC_IOCB_BUFF_COUNT 20

#define LPFC_HB_MBOX_INTERVAL   5	/* Heart beat interval in seconds. */
#define LPFC_HB_MBOX_TIMEOUT    30	/* Heart beat timeout  in seconds. */

/* Error Attention event polling interval */
#define LPFC_ERATT_POLL_INTERVAL	5 /* EATT poll interval in seconds */

/* Define macros for 64 bit support */
#define putPaddrLow(addr)    ((uint32_t) (0xffffffff & (u64)(addr)))
#define putPaddrHigh(addr)   ((uint32_t) (0xffffffff & (((u64)(addr))>>32)))
#define getPaddr(high, low)  ((dma_addr_t)( \
			     (( (u64)(high)<<16 ) << 16)|( (u64)(low))))
/* Provide maximum configuration definitions. */
#define LPFC_DRVR_TIMEOUT	16	/* driver iocb timeout value in sec */
#define FC_MAX_ADPTMSG		64

#define MAX_HBAEVT	32
#define MAX_HBAS_NO_RESET 16

/* Number of MSI-X vectors the driver uses */
#define LPFC_MSIX_VECTORS	2

/* lpfc wait event data ready flag */
#define LPFC_DATA_READY		0	/* bit 0 */

/* queue dump line buffer size */
#define LPFC_LBUF_SZ		128

/* mailbox system shutdown options */
#define LPFC_MBX_NO_WAIT	0
#define LPFC_MBX_WAIT		1

#define LPFC_CFG_PARAM_MAGIC_NUM 0xFEAA0005
#define LPFC_PORT_CFG_NAME "/cfg/port.cfg"

#define lpfc_rangecheck(val, min, max) \
	((uint)(val) >= (uint)(min) && (val) <= (max))

enum lpfc_polling_flags {
	ENABLE_FCP_RING_POLLING = 0x1,
	DISABLE_FCP_RING_INT    = 0x2
};

struct perf_prof {
	uint16_t cmd_cpu[40];
	uint16_t rsp_cpu[40];
	uint16_t qh_cpu[40];
	uint16_t wqidx[40];
};

/*
 * Provide for FC4 TYPE x28 - NVME.  The
 * bit mask for FCP and NVME is 0x8 identically
 * because they are 32 bit positions distance.
 */
#define LPFC_FC4_TYPE_BITMASK	0x00000100

/* Provide DMA memory definitions the driver uses per port instance. */
struct lpfc_dmabuf {
	struct list_head list;
	void *virt;		/* virtual address ptr */
	dma_addr_t phys;	/* mapped address */
	uint32_t   buffer_tag;	/* used for tagged queue ring */
};

struct lpfc_nvmet_ctxbuf {
	struct list_head list;
	struct lpfc_async_xchg_ctx *context;
	struct lpfc_iocbq *iocbq;
	struct lpfc_sglq *sglq;
	struct work_struct defer_work;
};

struct lpfc_dma_pool {
	struct lpfc_dmabuf   *elements;
	uint32_t    max_count;
	uint32_t    current_count;
};

struct hbq_dmabuf {
	struct lpfc_dmabuf hbuf;
	struct lpfc_dmabuf dbuf;
	uint16_t total_size;
	uint16_t bytes_recv;
	uint32_t tag;
	struct lpfc_cq_event cq_event;
	unsigned long time_stamp;
	void *context;
};

struct rqb_dmabuf {
	struct lpfc_dmabuf hbuf;
	struct lpfc_dmabuf dbuf;
	uint16_t total_size;
	uint16_t bytes_recv;
	uint16_t idx;
	struct lpfc_queue *hrq;	  /* ptr to associated Header RQ */
	struct lpfc_queue *drq;	  /* ptr to associated Data RQ */
};

/* Priority bit.  Set value to exceed low water mark in lpfc_mem. */
#define MEM_PRI		0x100


/****************************************************************************/
/*      Device VPD save area                                                */
/****************************************************************************/
typedef struct lpfc_vpd {
	uint32_t status;	/* vpd status value */
	uint32_t length;	/* number of bytes actually returned */
	struct {
		uint32_t rsvd1;	/* Revision numbers */
		uint32_t biuRev;
		uint32_t smRev;
		uint32_t smFwRev;
		uint32_t endecRev;
		uint16_t rBit;
		uint8_t fcphHigh;
		uint8_t fcphLow;
		uint8_t feaLevelHigh;
		uint8_t feaLevelLow;
		uint32_t postKernRev;
		uint32_t opFwRev;
		uint8_t opFwName[16];
		uint32_t sli1FwRev;
		uint8_t sli1FwName[16];
		uint32_t sli2FwRev;
		uint8_t sli2FwName[16];
	} rev;
	struct {
#ifdef __BIG_ENDIAN_BITFIELD
		uint32_t rsvd3  :20;  /* Reserved                             */
		uint32_t rsvd2	: 3;  /* Reserved                             */
		uint32_t cbg	: 1;  /* Configure BlockGuard                 */
		uint32_t cmv	: 1;  /* Configure Max VPIs                   */
		uint32_t ccrp   : 1;  /* Config Command Ring Polling          */
		uint32_t csah   : 1;  /* Configure Synchronous Abort Handling */
		uint32_t chbs   : 1;  /* Cofigure Host Backing store          */
		uint32_t cinb   : 1;  /* Enable Interrupt Notification Block  */
		uint32_t cerbm	: 1;  /* Configure Enhanced Receive Buf Mgmt  */
		uint32_t cmx	: 1;  /* Configure Max XRIs                   */
		uint32_t cmr	: 1;  /* Configure Max RPIs                   */
#else	/*  __LITTLE_ENDIAN */
		uint32_t cmr	: 1;  /* Configure Max RPIs                   */
		uint32_t cmx	: 1;  /* Configure Max XRIs                   */
		uint32_t cerbm	: 1;  /* Configure Enhanced Receive Buf Mgmt  */
		uint32_t cinb   : 1;  /* Enable Interrupt Notification Block  */
		uint32_t chbs   : 1;  /* Cofigure Host Backing store          */
		uint32_t csah   : 1;  /* Configure Synchronous Abort Handling */
		uint32_t ccrp   : 1;  /* Config Command Ring Polling          */
		uint32_t cmv	: 1;  /* Configure Max VPIs                   */
		uint32_t cbg	: 1;  /* Configure BlockGuard                 */
		uint32_t rsvd2	: 3;  /* Reserved                             */
		uint32_t rsvd3  :20;  /* Reserved                             */
#endif
	} sli3Feat;
} lpfc_vpd_t;


/*
 * lpfc stat counters
 */
struct lpfc_stats {
	/* Statistics for ELS commands */
	uint32_t elsLogiCol;
	uint32_t elsRetryExceeded;
	uint32_t elsXmitRetry;
	uint32_t elsDelayRetry;
	uint32_t elsRcvDrop;
	uint32_t elsRcvFrame;
	uint32_t elsRcvRSCN;
	uint32_t elsRcvRNID;
	uint32_t elsRcvFARP;
	uint32_t elsRcvFARPR;
	uint32_t elsRcvFLOGI;
	uint32_t elsRcvPLOGI;
	uint32_t elsRcvADISC;
	uint32_t elsRcvPDISC;
	uint32_t elsRcvFAN;
	uint32_t elsRcvLOGO;
	uint32_t elsRcvPRLO;
	uint32_t elsRcvPRLI;
	uint32_t elsRcvLIRR;
	uint32_t elsRcvRLS;
	uint32_t elsRcvRPL;
	uint32_t elsRcvRRQ;
	uint32_t elsRcvRTV;
	uint32_t elsRcvECHO;
	uint32_t elsRcvLCB;
	uint32_t elsRcvRDP;
	uint32_t elsRcvRDF;
	uint32_t elsXmitFLOGI;
	uint32_t elsXmitFDISC;
	uint32_t elsXmitPLOGI;
	uint32_t elsXmitPRLI;
	uint32_t elsXmitADISC;
	uint32_t elsXmitLOGO;
	uint32_t elsXmitSCR;
	uint32_t elsXmitRSCN;
	uint32_t elsXmitRNID;
	uint32_t elsXmitFARP;
	uint32_t elsXmitFARPR;
	uint32_t elsXmitACC;
	uint32_t elsXmitLSRJT;

	uint32_t frameRcvBcast;
	uint32_t frameRcvMulti;
	uint32_t strayXmitCmpl;
	uint32_t frameXmitDelay;
	uint32_t xriCmdCmpl;
	uint32_t xriStatErr;
	uint32_t LinkUp;
	uint32_t LinkDown;
	uint32_t LinkMultiEvent;
	uint32_t NoRcvBuf;
	uint32_t fcpCmd;
	uint32_t fcpCmpl;
	uint32_t fcpRspErr;
	uint32_t fcpRemoteStop;
	uint32_t fcpPortRjt;
	uint32_t fcpPortBusy;
	uint32_t fcpError;
	uint32_t fcpLocalErr;
};

struct lpfc_hba;


#define LPFC_VMID_TIMER   300	/* timer interval in seconds */

#define LPFC_MAX_VMID_SIZE      256
#define LPFC_COMPRESS_VMID_SIZE 16

union lpfc_vmid_io_tag {
	u32 app_id;	/* App Id vmid */
	u8 cs_ctl_vmid;	/* Priority tag vmid */
};

#define JIFFIES_PER_HR	(HZ * 60 * 60)

struct lpfc_vmid {
	u8 flag;
#define LPFC_VMID_SLOT_FREE     0x0
#define LPFC_VMID_SLOT_USED     0x1
#define LPFC_VMID_REQ_REGISTER  0x2
#define LPFC_VMID_REGISTERED    0x4
#define LPFC_VMID_DE_REGISTER   0x8
	char host_vmid[LPFC_MAX_VMID_SIZE];
	union lpfc_vmid_io_tag un;
	struct hlist_node hnode;
	u64 io_rd_cnt;
	u64 io_wr_cnt;
	u8 vmid_len;
	u8 delete_inactive; /* Delete if inactive flag 0 = no, 1 = yes */
	u32 hash_index;
	u64 __percpu *last_io_time;
};

#define lpfc_vmid_is_type_priority_tag(vport)\
	(vport->vmid_priority_tagging ? 1 : 0)

#define LPFC_VMID_HASH_SIZE     256
#define LPFC_VMID_HASH_MASK     255
#define LPFC_VMID_HASH_SHIFT    6

struct lpfc_vmid_context {
	struct lpfc_vmid *vmp;
	struct lpfc_nodelist *nlp;
	bool instantiated;
};

struct lpfc_vmid_priority_range {
	u8 low;
	u8 high;
	u8 qos;
};

struct lpfc_vmid_priority_info {
	u32 num_descriptors;
	struct lpfc_vmid_priority_range *vmid_range;
};

#define QFPA_EVEN_ONLY 0x01
#define QFPA_ODD_ONLY  0x02
#define QFPA_EVEN_ODD  0x03

enum discovery_state {
	LPFC_VPORT_UNKNOWN     =  0,    /* vport state is unknown */
	LPFC_VPORT_FAILED      =  1,    /* vport has failed */
	LPFC_LOCAL_CFG_LINK    =  6,    /* local NPORT Id configured */
	LPFC_FLOGI             =  7,    /* FLOGI sent to Fabric */
	LPFC_FDISC             =  8,    /* FDISC sent for vport */
	LPFC_FABRIC_CFG_LINK   =  9,    /* Fabric assigned NPORT Id
				         * configured */
	LPFC_NS_REG            =  10,   /* Register with NameServer */
	LPFC_NS_QRY            =  11,   /* Query NameServer for NPort ID list */
	LPFC_BUILD_DISC_LIST   =  12,   /* Build ADISC and PLOGI lists for
				         * device authentication / discovery */
	LPFC_DISC_AUTH         =  13,   /* Processing ADISC list */
	LPFC_VPORT_READY       =  32,
};

enum hba_state {
	LPFC_LINK_UNKNOWN    =   0,   /* HBA state is unknown */
	LPFC_WARM_START      =   1,   /* HBA state after selective reset */
	LPFC_INIT_START      =   2,   /* Initial state after board reset */
	LPFC_INIT_MBX_CMDS   =   3,   /* Initialize HBA with mbox commands */
	LPFC_LINK_DOWN       =   4,   /* HBA initialized, link is down */
	LPFC_LINK_UP         =   5,   /* Link is up  - issue READ_LA */
	LPFC_CLEAR_LA        =   6,   /* authentication cmplt - issue
				       * CLEAR_LA */
	LPFC_HBA_READY       =  32,
	LPFC_HBA_ERROR       =  -1
};

struct lpfc_trunk_link_state {
	enum hba_state state;
	uint8_t fault;
};

struct lpfc_trunk_link  {
	struct lpfc_trunk_link_state link0,
				     link1,
				     link2,
				     link3;
	u32 phy_lnk_speed;
};

/* Format of congestion module parameters */
struct lpfc_cgn_param {
	uint32_t cgn_param_magic;
	uint8_t  cgn_param_version;	/* version 1 */
	uint8_t  cgn_param_mode;	/* 0=off 1=managed 2=monitor only */
#define LPFC_CFG_OFF		0
#define LPFC_CFG_MANAGED	1
#define LPFC_CFG_MONITOR	2
	uint8_t  cgn_rsvd1;
	uint8_t  cgn_rsvd2;
	uint8_t  cgn_param_level0;
	uint8_t  cgn_param_level1;
	uint8_t  cgn_param_level2;
	uint8_t  byte11;
	uint8_t  byte12;
	uint8_t  byte13;
	uint8_t  byte14;
	uint8_t  byte15;
};

/* Max number of days of congestion data */
#define LPFC_MAX_CGN_DAYS 10

/* Format of congestion buffer info
 * This structure defines memory thats allocated and registered with
 * the HBA firmware. When adding or removing fields from this structure
 * the alignment must match the HBA firmware.
 */

struct lpfc_cgn_info {
	/* Header */
	__le16   cgn_info_size;		/* is sizeof(struct lpfc_cgn_info) */
	uint8_t  cgn_info_version;	/* represents format of structure */
#define LPFC_CGN_INFO_V1	1
#define LPFC_CGN_INFO_V2	2
#define LPFC_CGN_INFO_V3	3
	uint8_t  cgn_info_mode;		/* 0=off 1=managed 2=monitor only */
	uint8_t  cgn_info_detect;
	uint8_t  cgn_info_action;
	uint8_t  cgn_info_level0;
	uint8_t  cgn_info_level1;
	uint8_t  cgn_info_level2;

	/* Start Time */
	uint8_t  cgn_info_month;
	uint8_t  cgn_info_day;
	uint8_t  cgn_info_year;
	uint8_t  cgn_info_hour;
	uint8_t  cgn_info_minute;
	uint8_t  cgn_info_second;

	/* minute / hours / daily indices */
	uint8_t  cgn_index_minute;
	uint8_t  cgn_index_hour;
	uint8_t  cgn_index_day;

	__le16   cgn_warn_freq;
	__le16   cgn_alarm_freq;
	__le16   cgn_lunq;
	uint8_t  cgn_pad1[8];

	/* Driver Information */
	__le16   cgn_drvr_min[60];
	__le32   cgn_drvr_hr[24];
	__le32   cgn_drvr_day[LPFC_MAX_CGN_DAYS];

	/* Congestion Warnings */
	__le16   cgn_warn_min[60];
	__le32   cgn_warn_hr[24];
	__le32   cgn_warn_day[LPFC_MAX_CGN_DAYS];

	/* Latency Information */
	__le32   cgn_latency_min[60];
	__le32   cgn_latency_hr[24];
	__le32   cgn_latency_day[LPFC_MAX_CGN_DAYS];

	/* Bandwidth Information */
	__le16   cgn_bw_min[60];
	__le16   cgn_bw_hr[24];
	__le16   cgn_bw_day[LPFC_MAX_CGN_DAYS];

	/* Congestion Alarms */
	__le16   cgn_alarm_min[60];
	__le32   cgn_alarm_hr[24];
	__le32   cgn_alarm_day[LPFC_MAX_CGN_DAYS];

	struct_group(cgn_stat,
		uint8_t  cgn_stat_npm;		/* Notifications per minute */

		/* Start Time */
		uint8_t  cgn_stat_month;
		uint8_t  cgn_stat_day;
		uint8_t  cgn_stat_year;
		uint8_t  cgn_stat_hour;
		uint8_t  cgn_stat_minute;
		uint8_t  cgn_pad2[2];

		__le32   cgn_notification;
		__le32   cgn_peer_notification;
		__le32   link_integ_notification;
		__le32   delivery_notification;

		uint8_t  cgn_stat_cgn_month; /* Last congestion notification FPIN */
		uint8_t  cgn_stat_cgn_day;
		uint8_t  cgn_stat_cgn_year;
		uint8_t  cgn_stat_cgn_hour;
		uint8_t  cgn_stat_cgn_min;
		uint8_t  cgn_stat_cgn_sec;

		uint8_t  cgn_stat_peer_month; /* Last peer congestion FPIN */
		uint8_t  cgn_stat_peer_day;
		uint8_t  cgn_stat_peer_year;
		uint8_t  cgn_stat_peer_hour;
		uint8_t  cgn_stat_peer_min;
		uint8_t  cgn_stat_peer_sec;

		uint8_t  cgn_stat_lnk_month; /* Last link integrity FPIN */
		uint8_t  cgn_stat_lnk_day;
		uint8_t  cgn_stat_lnk_year;
		uint8_t  cgn_stat_lnk_hour;
		uint8_t  cgn_stat_lnk_min;
		uint8_t  cgn_stat_lnk_sec;

		uint8_t  cgn_stat_del_month; /* Last delivery notification FPIN */
		uint8_t  cgn_stat_del_day;
		uint8_t  cgn_stat_del_year;
		uint8_t  cgn_stat_del_hour;
		uint8_t  cgn_stat_del_min;
		uint8_t  cgn_stat_del_sec;
	);

	__le32   cgn_info_crc;
#define LPFC_CGN_CRC32_MAGIC_NUMBER	0x1EDC6F41
#define LPFC_CGN_CRC32_SEED		0xFFFFFFFF
};

#define LPFC_CGN_INFO_SZ	(sizeof(struct lpfc_cgn_info) -  \
				sizeof(uint32_t))

struct lpfc_cgn_stat {
	atomic64_t total_bytes;
	atomic64_t rcv_bytes;
	atomic64_t rx_latency;
#define LPFC_CGN_NOT_SENT	0xFFFFFFFFFFFFFFFFLL
	atomic_t rx_io_cnt;
};

struct lpfc_cgn_acqe_stat {
	atomic64_t alarm;
	atomic64_t warn;
};

struct lpfc_vport {
	struct lpfc_hba *phba;
	struct list_head listentry;
	uint8_t port_type;
#define LPFC_PHYSICAL_PORT 1
#define LPFC_NPIV_PORT  2
#define LPFC_FABRIC_PORT 3
	enum discovery_state port_state;

	uint16_t vpi;
	uint16_t vfi;
	uint8_t vpi_state;
#define LPFC_VPI_REGISTERED	0x1

	uint32_t fc_flag;	/* FC flags */
/* Several of these flags are HBA centric and should be moved to
 * phba->link_flag (e.g. FC_PTP, FC_PUBLIC_LOOP)
 */
#define FC_PT2PT                0x1	 /* pt2pt with no fabric */
#define FC_PT2PT_PLOGI          0x2	 /* pt2pt initiate PLOGI */
#define FC_DISC_TMO             0x4	 /* Discovery timer running */
#define FC_PUBLIC_LOOP          0x8	 /* Public loop */
#define FC_LBIT                 0x10	 /* LOGIN bit in loopinit set */
#define FC_RSCN_MODE            0x20	 /* RSCN cmd rcv'ed */
#define FC_NLP_MORE             0x40	 /* More node to process in node tbl */
#define FC_OFFLINE_MODE         0x80	 /* Interface is offline for diag */
#define FC_FABRIC               0x100	 /* We are fabric attached */
#define FC_VPORT_LOGO_RCVD      0x200    /* LOGO received on vport */
#define FC_RSCN_DISCOVERY       0x400	 /* Auth all devices after RSCN */
#define FC_LOGO_RCVD_DID_CHNG   0x800    /* FDISC on phys port detect DID chng*/
#define FC_PT2PT_NO_NVME        0x1000   /* Don't send NVME PRLI */
#define FC_SCSI_SCAN_TMO        0x4000	 /* scsi scan timer running */
#define FC_ABORT_DISCOVERY      0x8000	 /* we want to abort discovery */
#define FC_NDISC_ACTIVE         0x10000	 /* NPort discovery active */
#define FC_BYPASSED_MODE        0x20000	 /* NPort is in bypassed mode */
#define FC_VPORT_NEEDS_REG_VPI	0x80000  /* Needs to have its vpi registered */
#define FC_RSCN_DEFERRED	0x100000 /* A deferred RSCN being processed */
#define FC_VPORT_NEEDS_INIT_VPI 0x200000 /* Need to INIT_VPI before FDISC */
#define FC_VPORT_CVL_RCVD	0x400000 /* VLink failed due to CVL	 */
#define FC_VFI_REGISTERED	0x800000 /* VFI is registered */
#define FC_FDISC_COMPLETED	0x1000000/* FDISC completed */
#define FC_DISC_DELAYED		0x2000000/* Delay NPort discovery */

	uint32_t ct_flags;
#define FC_CT_RFF_ID		0x1	 /* RFF_ID accepted by switch */
#define FC_CT_RNN_ID		0x2	 /* RNN_ID accepted by switch */
#define FC_CT_RSNN_NN		0x4	 /* RSNN_NN accepted by switch */
#define FC_CT_RSPN_ID		0x8	 /* RSPN_ID accepted by switch */
#define FC_CT_RFT_ID		0x10	 /* RFT_ID accepted by switch */
#define FC_CT_RPRT_DEFER	0x20	 /* Defer issuing FDMI RPRT */

	struct list_head fc_nodes;

	/* Keep counters for the number of entries in each list. */
	uint16_t fc_plogi_cnt;
	uint16_t fc_adisc_cnt;
	uint16_t fc_reglogin_cnt;
	uint16_t fc_prli_cnt;
	uint16_t fc_unmap_cnt;
	uint16_t fc_map_cnt;
	uint16_t fc_npr_cnt;
	uint16_t fc_unused_cnt;
	struct serv_parm fc_sparam;	/* buffer for our service parameters */

	uint32_t fc_myDID;	/* fibre channel S_ID */
	uint32_t fc_prevDID;	/* previous fibre channel S_ID */
	struct lpfc_name fabric_portname;
	struct lpfc_name fabric_nodename;

	int32_t stopped;   /* HBA has not been restarted since last ERATT */
	uint8_t fc_linkspeed;	/* Link speed after last READ_LA */

	uint32_t num_disc_nodes;	/* in addition to hba_state */
	uint32_t gidft_inp;		/* cnt of outstanding GID_FTs */

	uint32_t fc_nlp_cnt;	/* outstanding NODELIST requests */
	uint32_t fc_rscn_id_cnt;	/* count of RSCNs payloads in list */
	uint32_t fc_rscn_flush;		/* flag use of fc_rscn_id_list */
	struct lpfc_dmabuf *fc_rscn_id_list[FC_MAX_HOLD_RSCN];
	struct lpfc_name fc_nodename;	/* fc nodename */
	struct lpfc_name fc_portname;	/* fc portname */

	struct lpfc_work_evt disc_timeout_evt;

	struct timer_list fc_disctmo;	/* Discovery rescue timer */
	uint8_t fc_ns_retry;	/* retries for fabric nameserver */
	uint32_t fc_prli_sent;	/* cntr for outstanding PRLIs */

	spinlock_t work_port_lock;
	uint32_t work_port_events; /* Timeout to be handled  */
#define WORKER_DISC_TMO                0x1	/* vport: Discovery timeout */
#define WORKER_ELS_TMO                 0x2	/* vport: ELS timeout */
#define WORKER_DELAYED_DISC_TMO        0x8	/* vport: delayed discovery */

#define WORKER_MBOX_TMO                0x100	/* hba: MBOX timeout */
#define WORKER_HB_TMO                  0x200	/* hba: Heart beat timeout */
#define WORKER_FABRIC_BLOCK_TMO        0x400	/* hba: fabric block timeout */
#define WORKER_RAMP_DOWN_QUEUE         0x800	/* hba: Decrease Q depth */
#define WORKER_RAMP_UP_QUEUE           0x1000	/* hba: Increase Q depth */
#define WORKER_SERVICE_TXQ             0x2000	/* hba: IOCBs on the txq */
#define WORKER_CHECK_INACTIVE_VMID     0x4000	/* hba: check inactive vmids */
#define WORKER_CHECK_VMID_ISSUE_QFPA   0x8000	/* vport: Check if qfpa needs
						 * to be issued */

	struct timer_list els_tmofunc;
	struct timer_list delayed_disc_tmo;

	uint8_t load_flag;
#define FC_LOADING		0x1	/* HBA in process of loading drvr */
#define FC_UNLOADING		0x2	/* HBA in process of unloading drvr */
#define FC_ALLOW_FDMI		0x4	/* port is ready for FDMI requests */
#define FC_ALLOW_VMID		0x8	/* Allow VMID I/Os */
#define FC_DEREGISTER_ALL_APP_ID	0x10	/* Deregister all VMIDs */
	/* Vport Config Parameters */
	uint32_t cfg_scan_down;
	uint32_t cfg_lun_queue_depth;
	uint32_t cfg_nodev_tmo;
	uint32_t cfg_devloss_tmo;
	uint32_t cfg_restrict_login;
	uint32_t cfg_peer_port_login;
	uint32_t cfg_fcp_class;
	uint32_t cfg_use_adisc;
	uint32_t cfg_discovery_threads;
	uint32_t cfg_log_verbose;
	uint32_t cfg_enable_fc4_type;
	uint32_t cfg_max_luns;
	uint32_t cfg_enable_da_id;
	uint32_t cfg_max_scsicmpl_time;
	uint32_t cfg_tgt_queue_depth;
	uint32_t cfg_first_burst_size;
	uint32_t dev_loss_tmo_changed;
	/* VMID parameters */
	u8 lpfc_vmid_host_uuid[LPFC_COMPRESS_VMID_SIZE];
	u32 max_vmid;	/* maximum VMIDs allowed per port */
	u32 cur_vmid_cnt;	/* Current VMID count */
#define LPFC_MIN_VMID	4
#define LPFC_MAX_VMID	255
	u32 vmid_inactivity_timeout;	/* Time after which the VMID */
						/* deregisters from switch */
	u32 vmid_priority_tagging;
#define LPFC_VMID_PRIO_TAG_DISABLE	0 /* Disable */
#define LPFC_VMID_PRIO_TAG_SUP_TARGETS	1 /* Allow supported targets only */
#define LPFC_VMID_PRIO_TAG_ALL_TARGETS	2 /* Allow all targets */
	unsigned long *vmid_priority_range;
#define LPFC_VMID_MAX_PRIORITY_RANGE    256
#define LPFC_VMID_PRIORITY_BITMAP_SIZE  32
	u8 vmid_flag;
#define LPFC_VMID_IN_USE		0x1
#define LPFC_VMID_ISSUE_QFPA		0x2
#define LPFC_VMID_QFPA_CMPL		0x4
#define LPFC_VMID_QOS_ENABLED		0x8
#define LPFC_VMID_TIMER_ENBLD		0x10
#define LPFC_VMID_TYPE_PRIO		0x20
	struct fc_qfpa_res *qfpa_res;

	struct fc_vport *fc_vport;

	struct lpfc_vmid *vmid;
	DECLARE_HASHTABLE(hash_table, 8);
	rwlock_t vmid_lock;
	struct lpfc_vmid_priority_info vmid_priority;

#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	struct dentry *debug_disc_trc;
	struct dentry *debug_nodelist;
	struct dentry *debug_nvmestat;
	struct dentry *debug_scsistat;
	struct dentry *debug_ioktime;
	struct dentry *debug_hdwqstat;
	struct dentry *vport_debugfs_root;
	struct lpfc_debugfs_trc *disc_trc;
	atomic_t disc_trc_cnt;
#endif
	struct list_head rcv_buffer_list;
	unsigned long rcv_buffer_time_stamp;
	uint32_t vport_flag;
#define STATIC_VPORT		0x1
#define FAWWPN_PARAM_CHG	0x2

	uint16_t fdmi_num_disc;
	uint32_t fdmi_hba_mask;
	uint32_t fdmi_port_mask;

	/* There is a single nvme instance per vport. */
	struct nvme_fc_local_port *localport;
	uint8_t  nvmei_support; /* driver supports NVME Initiator */
	uint32_t last_fcp_wqidx;
	uint32_t rcv_flogi_cnt; /* How many unsol FLOGIs ACK'd. */
};

struct hbq_s {
	uint16_t entry_count;	  /* Current number of HBQ slots */
	uint16_t buffer_count;	  /* Current number of buffers posted */
	uint32_t next_hbqPutIdx;  /* Index to next HBQ slot to use */
	uint32_t hbqPutIdx;	  /* HBQ slot to use */
	uint32_t local_hbqGetIdx; /* Local copy of Get index from Port */
	void    *hbq_virt;	  /* Virtual ptr to this hbq */
	struct list_head hbq_buffer_list;  /* buffers assigned to this HBQ */
				  /* Callback for HBQ buffer allocation */
	struct hbq_dmabuf *(*hbq_alloc_buffer) (struct lpfc_hba *);
				  /* Callback for HBQ buffer free */
	void               (*hbq_free_buffer) (struct lpfc_hba *,
					       struct hbq_dmabuf *);
};

/* this matches the position in the lpfc_hbq_defs array */
#define LPFC_ELS_HBQ	0
#define LPFC_MAX_HBQS	1

enum hba_temp_state {
	HBA_NORMAL_TEMP,
	HBA_OVER_TEMP
};

enum intr_type_t {
	NONE = 0,
	INTx,
	MSI,
	MSIX,
};

#define LPFC_CT_CTX_MAX		64
struct unsol_rcv_ct_ctx {
	uint32_t ctxt_id;
	uint32_t SID;
	uint32_t valid;
#define UNSOL_INVALID		0
#define UNSOL_VALID		1
	uint16_t oxid;
	uint16_t rxid;
};

#define LPFC_USER_LINK_SPEED_AUTO	0	/* auto select (default)*/
#define LPFC_USER_LINK_SPEED_1G		1	/* 1 Gigabaud */
#define LPFC_USER_LINK_SPEED_2G		2	/* 2 Gigabaud */
#define LPFC_USER_LINK_SPEED_4G		4	/* 4 Gigabaud */
#define LPFC_USER_LINK_SPEED_8G		8	/* 8 Gigabaud */
#define LPFC_USER_LINK_SPEED_10G	10	/* 10 Gigabaud */
#define LPFC_USER_LINK_SPEED_16G	16	/* 16 Gigabaud */
#define LPFC_USER_LINK_SPEED_32G	32	/* 32 Gigabaud */
#define LPFC_USER_LINK_SPEED_64G	64	/* 64 Gigabaud */
#define LPFC_USER_LINK_SPEED_MAX	LPFC_USER_LINK_SPEED_64G

#define LPFC_LINK_SPEED_STRING "0, 1, 2, 4, 8, 10, 16, 32, 64"

enum nemb_type {
	nemb_mse = 1,
	nemb_hbd
};

enum mbox_type {
	mbox_rd = 1,
	mbox_wr
};

enum dma_type {
	dma_mbox = 1,
	dma_ebuf
};

enum sta_type {
	sta_pre_addr = 1,
	sta_pos_addr
};

struct lpfc_mbox_ext_buf_ctx {
	uint32_t state;
#define LPFC_BSG_MBOX_IDLE		0
#define LPFC_BSG_MBOX_HOST              1
#define LPFC_BSG_MBOX_PORT		2
#define LPFC_BSG_MBOX_DONE		3
#define LPFC_BSG_MBOX_ABTS		4
	enum nemb_type nembType;
	enum mbox_type mboxType;
	uint32_t numBuf;
	uint32_t mbxTag;
	uint32_t seqNum;
	struct lpfc_dmabuf *mbx_dmabuf;
	struct list_head ext_dmabuf_list;
};

struct lpfc_epd_pool {
	/* Expedite pool */
	struct list_head list;
	u32 count;
	spinlock_t lock;	/* lock for expedite pool */
};

enum ras_state {
	INACTIVE,
	REG_INPROGRESS,
	ACTIVE
};

struct lpfc_ras_fwlog {
	uint8_t *fwlog_buff;
	uint32_t fw_buffcount; /* Buffer size posted to FW */
#define LPFC_RAS_BUFF_ENTERIES  16      /* Each entry can hold max of 64k */
#define LPFC_RAS_MAX_ENTRY_SIZE (64 * 1024)
#define LPFC_RAS_MIN_BUFF_POST_SIZE (256 * 1024)
#define LPFC_RAS_MAX_BUFF_POST_SIZE (1024 * 1024)
	uint32_t fw_loglevel; /* Log level set */
	struct lpfc_dmabuf lwpd;
	struct list_head fwlog_buff_list;

	/* RAS support status on adapter */
	bool ras_hwsupport; /* RAS Support available on HW or not */
	bool ras_enabled;   /* Ras Enabled for the function */
#define LPFC_RAS_DISABLE_LOGGING 0x00
#define LPFC_RAS_ENABLE_LOGGING 0x01
	enum ras_state state;    /* RAS logging running state */
};

#define DBG_LOG_STR_SZ 256
#define DBG_LOG_SZ 256

struct dbg_log_ent {
	char log[DBG_LOG_STR_SZ];
	u64     t_ns;
};

enum lpfc_irq_chann_mode {
	/* Assign IRQs to all possible cpus that have hardware queues */
	NORMAL_MODE,

	/* Assign IRQs only to cpus on the same numa node as HBA */
	NUMA_MODE,

	/* Assign IRQs only on non-hyperthreaded CPUs. This is the
	 * same as normal_mode, but assign IRQS only on physical CPUs.
	 */
	NHT_MODE,
};

enum lpfc_hba_bit_flags {
	FABRIC_COMANDS_BLOCKED,
	HBA_PCI_ERR,
};

struct lpfc_hba {
	/* SCSI interface function jump table entries */
	struct lpfc_io_buf * (*lpfc_get_scsi_buf)
		(struct lpfc_hba *phba, struct lpfc_nodelist *ndlp,
		struct scsi_cmnd *cmnd);
	int (*lpfc_scsi_prep_dma_buf)
		(struct lpfc_hba *, struct lpfc_io_buf *);
	void (*lpfc_scsi_unprep_dma_buf)
		(struct lpfc_hba *, struct lpfc_io_buf *);
	void (*lpfc_release_scsi_buf)
		(struct lpfc_hba *, struct lpfc_io_buf *);
	void (*lpfc_rampdown_queue_depth)
		(struct lpfc_hba *);
	void (*lpfc_scsi_prep_cmnd)
		(struct lpfc_vport *, struct lpfc_io_buf *,
		 struct lpfc_nodelist *);
	int (*lpfc_scsi_prep_cmnd_buf)
		(struct lpfc_vport *vport,
		 struct lpfc_io_buf *lpfc_cmd,
		 uint8_t tmo);
	int (*lpfc_scsi_prep_task_mgmt_cmd)
		(struct lpfc_vport *vport,
		 struct lpfc_io_buf *lpfc_cmd,
		 u64 lun, u8 task_mgmt_cmd);

	/* IOCB interface function jump table entries */
	int (*__lpfc_sli_issue_iocb)
		(struct lpfc_hba *, uint32_t,
		 struct lpfc_iocbq *, uint32_t);
	int (*__lpfc_sli_issue_fcp_io)
		(struct lpfc_hba *phba, uint32_t ring_number,
		 struct lpfc_iocbq *piocb, uint32_t flag);
	void (*__lpfc_sli_release_iocbq)(struct lpfc_hba *,
			 struct lpfc_iocbq *);
	int (*lpfc_hba_down_post)(struct lpfc_hba *phba);
	void (*lpfc_scsi_cmd_iocb_cmpl)
		(struct lpfc_hba *, struct lpfc_iocbq *, struct lpfc_iocbq *);

	/* MBOX interface function jump table entries */
	int (*lpfc_sli_issue_mbox)
		(struct lpfc_hba *, LPFC_MBOXQ_t *, uint32_t);

	/* Slow-path IOCB process function jump table entries */
	void (*lpfc_sli_handle_slow_ring_event)
		(struct lpfc_hba *phba, struct lpfc_sli_ring *pring,
		 uint32_t mask);

	/* INIT device interface function jump table entries */
	int (*lpfc_sli_hbq_to_firmware)
		(struct lpfc_hba *, uint32_t, struct hbq_dmabuf *);
	int (*lpfc_sli_brdrestart)
		(struct lpfc_hba *);
	int (*lpfc_sli_brdready)
		(struct lpfc_hba *, uint32_t);
	void (*lpfc_handle_eratt)
		(struct lpfc_hba *);
	void (*lpfc_stop_port)
		(struct lpfc_hba *);
	int (*lpfc_hba_init_link)
		(struct lpfc_hba *, uint32_t);
	int (*lpfc_hba_down_link)
		(struct lpfc_hba *, uint32_t);
	int (*lpfc_selective_reset)
		(struct lpfc_hba *);

	int (*lpfc_bg_scsi_prep_dma_buf)
		(struct lpfc_hba *, struct lpfc_io_buf *);

	/* Prep SLI WQE/IOCB jump table entries */
	void (*__lpfc_sli_prep_els_req_rsp)(struct lpfc_iocbq *cmdiocbq,
					    struct lpfc_vport *vport,
					    struct lpfc_dmabuf *bmp,
					    u16 cmd_size, u32 did, u32 elscmd,
					    u8 tmo, u8 expect_rsp);
	void (*__lpfc_sli_prep_gen_req)(struct lpfc_iocbq *cmdiocbq,
					struct lpfc_dmabuf *bmp, u16 rpi,
					u32 num_entry, u8 tmo);
	void (*__lpfc_sli_prep_xmit_seq64)(struct lpfc_iocbq *cmdiocbq,
					   struct lpfc_dmabuf *bmp, u16 rpi,
					   u16 ox_id, u32 num_entry, u8 rctl,
					   u8 last_seq, u8 cr_cx_cmd);
	void (*__lpfc_sli_prep_abort_xri)(struct lpfc_iocbq *cmdiocbq,
					  u16 ulp_context, u16 iotag,
					  u8 ulp_class, u16 cqid, bool ia,
					  bool wqec);

	/* expedite pool */
	struct lpfc_epd_pool epd_pool;

	/* SLI4 specific HBA data structure */
	struct lpfc_sli4_hba sli4_hba;

	struct workqueue_struct *wq;
	struct delayed_work     eq_delay_work;

#define LPFC_IDLE_STAT_DELAY 1000
	struct delayed_work	idle_stat_delay_work;

	struct lpfc_sli sli;
	uint8_t pci_dev_grp;	/* lpfc PCI dev group: 0x0, 0x1, 0x2,... */
	uint32_t sli_rev;		/* SLI2, SLI3, or SLI4 */
	uint32_t sli3_options;		/* Mask of enabled SLI3 options */
#define LPFC_SLI3_HBQ_ENABLED		0x01
#define LPFC_SLI3_NPIV_ENABLED		0x02
#define LPFC_SLI3_VPORT_TEARDOWN	0x04
#define LPFC_SLI3_CRP_ENABLED		0x08
#define LPFC_SLI3_BG_ENABLED		0x20
#define LPFC_SLI3_DSS_ENABLED		0x40
#define LPFC_SLI4_PERFH_ENABLED		0x80
#define LPFC_SLI4_PHWQ_ENABLED		0x100
	uint32_t iocb_cmd_size;
	uint32_t iocb_rsp_size;

	struct lpfc_trunk_link  trunk_link;
	enum hba_state link_state;
	uint32_t link_flag;	/* link state flags */
#define LS_LOOPBACK_MODE      0x1	/* NPort is in Loopback mode */
					/* This flag is set while issuing */
					/* INIT_LINK mailbox command */
#define LS_NPIV_FAB_SUPPORTED 0x2	/* Fabric supports NPIV */
#define LS_IGNORE_ERATT       0x4	/* intr handler should ignore ERATT */
#define LS_MDS_LINK_DOWN      0x8	/* MDS Diagnostics Link Down */
#define LS_MDS_LOOPBACK       0x10	/* MDS Diagnostics Link Up (Loopback) */
#define LS_CT_VEN_RPA         0x20	/* Vendor RPA sent to switch */
#define LS_EXTERNAL_LOOPBACK  0x40	/* External loopback plug inserted */

	uint32_t hba_flag;	/* hba generic flags */
#define HBA_ERATT_HANDLED	0x1 /* This flag is set when eratt handled */
#define DEFER_ERATT		0x2 /* Deferred error attention in progress */
#define HBA_FCOE_MODE		0x4 /* HBA function in FCoE Mode */
#define HBA_SP_QUEUE_EVT	0x8 /* Slow-path qevt posted to worker thread*/
#define HBA_POST_RECEIVE_BUFFER 0x10 /* Rcv buffers need to be posted */
#define HBA_PERSISTENT_TOPO	0x20 /* Persistent topology support in hba */
#define ELS_XRI_ABORT_EVENT	0x40 /* ELS_XRI abort event was queued */
#define ASYNC_EVENT		0x80
#define LINK_DISABLED		0x100 /* Link disabled by user */
#define FCF_TS_INPROG           0x200 /* FCF table scan in progress */
#define FCF_RR_INPROG           0x400 /* FCF roundrobin flogi in progress */
#define HBA_FIP_SUPPORT		0x800 /* FIP support in HBA */
#define HBA_DEVLOSS_TMO         0x2000 /* HBA in devloss timeout */
#define HBA_RRQ_ACTIVE		0x4000 /* process the rrq active list */
#define HBA_IOQ_FLUSH		0x8000 /* FCP/NVME I/O queues being flushed */
#define HBA_RECOVERABLE_UE	0x20000 /* Firmware supports recoverable UE */
#define HBA_FORCED_LINK_SPEED	0x40000 /*
					 * Firmware supports Forced Link Speed
					 * capability
					 */
#define HBA_FLOGI_ISSUED	0x100000 /* FLOGI was issued */
#define HBA_SHORT_CMF		0x200000 /* shorter CMF timer routine */
#define HBA_CGN_DAY_WRAP	0x400000 /* HBA Congestion info day wraps */
#define HBA_DEFER_FLOGI		0x800000 /* Defer FLOGI till read_sparm cmpl */
#define HBA_SETUP		0x1000000 /* Signifies HBA setup is completed */
#define HBA_NEEDS_CFG_PORT	0x2000000 /* SLI3 - needs a CONFIG_PORT mbox */
#define HBA_HBEAT_INP		0x4000000 /* mbox HBEAT is in progress */
#define HBA_HBEAT_TMO		0x8000000 /* HBEAT initiated after timeout */
#define HBA_FLOGI_OUTSTANDING	0x10000000 /* FLOGI is outstanding */
#define HBA_RHBA_CMPL		0x20000000 /* RHBA FDMI command is successful */

	struct completion *fw_dump_cmpl; /* cmpl event tracker for fw_dump */
	uint32_t fcp_ring_in_use; /* When polling test if intr-hndlr active*/
	struct lpfc_dmabuf slim2p;

	MAILBOX_t *mbox;
	uint32_t *mbox_ext;
	struct lpfc_mbox_ext_buf_ctx mbox_ext_buf_ctx;
	uint32_t ha_copy;
	struct _PCB *pcb;
	struct _IOCB *IOCBs;

	struct lpfc_dmabuf hbqslimp;

	uint16_t pci_cfg_value;

	uint8_t fc_linkspeed;	/* Link speed after last READ_LA */

	uint32_t fc_eventTag;	/* event tag for link attention */
	uint32_t link_events;

	/* These fields used to be binfo */
	uint32_t fc_pref_DID;	/* preferred D_ID */
	uint8_t  fc_pref_ALPA;	/* preferred AL_PA */
	uint32_t fc_edtovResol; /* E_D_TOV timer resolution */
	uint32_t fc_edtov;	/* E_D_TOV timer value */
	uint32_t fc_arbtov;	/* ARB_TOV timer value */
	uint32_t fc_ratov;	/* R_A_TOV timer value */
	uint32_t fc_rttov;	/* R_T_TOV timer value */
	uint32_t fc_altov;	/* AL_TOV timer value */
	uint32_t fc_crtov;	/* C_R_TOV timer value */

	struct serv_parm fc_fabparam;	/* fabric service parameters buffer */
	uint8_t alpa_map[128];	/* AL_PA map from READ_LA */

	uint32_t lmt;

	uint32_t fc_topology;	/* link topology, from LINK INIT */
	uint32_t fc_topology_changed;	/* link topology, from LINK INIT */

	struct lpfc_stats fc_stat;

	struct lpfc_nodelist fc_fcpnodev; /* nodelist entry for no device */
	uint32_t nport_event_cnt;	/* timestamp for nlplist entry */

	uint8_t  wwnn[8];
	uint8_t  wwpn[8];
	uint32_t RandomData[7];
	uint8_t  fcp_embed_io;
	uint8_t  nvmet_support;	/* driver supports NVMET */
#define LPFC_NVMET_MAX_PORTS	32
	uint8_t  mds_diags_support;
	uint8_t  bbcredit_support;
	uint8_t  enab_exp_wqcq_pages;
	u8	 nsler; /* Firmware supports FC-NVMe-2 SLER */

	/* HBA Config Parameters */
	uint32_t cfg_ack0;
	uint32_t cfg_xri_rebalancing;
	uint32_t cfg_xpsgl;
	uint32_t cfg_enable_npiv;
	uint32_t cfg_enable_rrq;
	uint32_t cfg_topology;
	uint32_t cfg_link_speed;
#define LPFC_FCF_FOV 1		/* Fast fcf failover */
#define LPFC_FCF_PRIORITY 2	/* Priority fcf failover */
	uint32_t cfg_fcf_failover_policy;
	uint32_t cfg_fcp_io_sched;
	uint32_t cfg_ns_query;
	uint32_t cfg_fcp2_no_tgt_reset;
	uint32_t cfg_cr_delay;
	uint32_t cfg_cr_count;
	uint32_t cfg_multi_ring_support;
	uint32_t cfg_multi_ring_rctl;
	uint32_t cfg_multi_ring_type;
	uint32_t cfg_poll;
	uint32_t cfg_poll_tmo;
	uint32_t cfg_task_mgmt_tmo;
	uint32_t cfg_use_msi;
	uint32_t cfg_auto_imax;
	uint32_t cfg_fcp_imax;
	uint32_t cfg_force_rscn;
	uint32_t cfg_cq_poll_threshold;
	uint32_t cfg_cq_max_proc_limit;
	uint32_t cfg_fcp_cpu_map;
	uint32_t cfg_fcp_mq_threshold;
	uint32_t cfg_hdw_queue;
	uint32_t cfg_irq_chann;
	uint32_t cfg_suppress_rsp;
	uint32_t cfg_nvme_oas;
	uint32_t cfg_nvme_embed_cmd;
	uint32_t cfg_nvmet_mrq_post;
	uint32_t cfg_nvmet_mrq;
	uint32_t cfg_enable_nvmet;
	uint32_t cfg_nvme_enable_fb;
	uint32_t cfg_nvmet_fb_size;
	uint32_t cfg_total_seg_cnt;
	uint32_t cfg_sg_seg_cnt;
	uint32_t cfg_nvme_seg_cnt;
	uint32_t cfg_scsi_seg_cnt;
	uint32_t cfg_sg_dma_buf_size;
	uint32_t cfg_hba_queue_depth;
	uint32_t cfg_enable_hba_reset;
	uint32_t cfg_enable_hba_heartbeat;
	uint32_t cfg_fof;
	uint32_t cfg_EnableXLane;
	uint8_t cfg_oas_tgt_wwpn[8];
	uint8_t cfg_oas_vpt_wwpn[8];
	uint32_t cfg_oas_lun_state;
#define OAS_LUN_ENABLE	1
#define OAS_LUN_DISABLE	0
	uint32_t cfg_oas_lun_status;
#define OAS_LUN_STATUS_EXISTS	0x01
	uint32_t cfg_oas_flags;
#define OAS_FIND_ANY_VPORT	0x01
#define OAS_FIND_ANY_TARGET	0x02
#define OAS_LUN_VALID	0x04
	uint32_t cfg_oas_priority;
	uint32_t cfg_XLanePriority;
	uint32_t cfg_enable_bg;
	uint32_t cfg_prot_mask;
	uint32_t cfg_prot_guard;
	uint32_t cfg_hostmem_hgp;
	uint32_t cfg_log_verbose;
	uint32_t cfg_enable_fc4_type;
#define LPFC_ENABLE_FCP  1
#define LPFC_ENABLE_NVME 2
#define LPFC_ENABLE_BOTH 3
#if (IS_ENABLED(CONFIG_NVME_FC))
#define LPFC_MAX_ENBL_FC4_TYPE LPFC_ENABLE_BOTH
#define LPFC_DEF_ENBL_FC4_TYPE LPFC_ENABLE_BOTH
#else
#define LPFC_MAX_ENBL_FC4_TYPE LPFC_ENABLE_FCP
#define LPFC_DEF_ENBL_FC4_TYPE LPFC_ENABLE_FCP
#endif
	uint32_t cfg_sriov_nr_virtfn;
	uint32_t cfg_request_firmware_upgrade;
	uint32_t cfg_suppress_link_up;
	uint32_t cfg_rrq_xri_bitmap_sz;
	u32      cfg_fcp_wait_abts_rsp;
	uint32_t cfg_delay_discovery;
	uint32_t cfg_sli_mode;
#define LPFC_INITIALIZE_LINK              0	/* do normal init_link mbox */
#define LPFC_DELAY_INIT_LINK              1	/* layered driver hold off */
#define LPFC_DELAY_INIT_LINK_INDEFINITELY 2	/* wait, manual intervention */
	uint32_t cfg_fdmi_on;
#define LPFC_FDMI_NO_SUPPORT	0	/* FDMI not supported */
#define LPFC_FDMI_SUPPORT	1	/* FDMI supported? */
	uint32_t cfg_enable_SmartSAN;
	uint32_t cfg_enable_mds_diags;
	uint32_t cfg_ras_fwlog_level;
	uint32_t cfg_ras_fwlog_buffsize;
	uint32_t cfg_ras_fwlog_func;
	uint32_t cfg_enable_bbcr;	/* Enable BB Credit Recovery */
	uint32_t cfg_enable_dpp;	/* Enable Direct Packet Push */
	uint32_t cfg_enable_pbde;
	uint32_t cfg_enable_mi;
	struct nvmet_fc_target_port *targetport;
	lpfc_vpd_t vpd;		/* vital product data */

	u32 cfg_max_vmid;	/* maximum VMIDs allowed per port */
	u32 cfg_vmid_app_header;
#define LPFC_VMID_APP_HEADER_DISABLE	0
#define LPFC_VMID_APP_HEADER_ENABLE	1
	u32 cfg_vmid_priority_tagging;
	u32 cfg_vmid_inactivity_timeout;	/* Time after which the VMID */
						/* deregisters from switch */
	struct pci_dev *pcidev;
	struct list_head      work_list;
	uint32_t              work_ha;      /* Host Attention Bits for WT */
	uint32_t              work_ha_mask; /* HA Bits owned by WT        */
	uint32_t              work_hs;      /* HS stored in case of ERRAT */
	uint32_t              work_status[2]; /* Extra status from SLIM */

	wait_queue_head_t    work_waitq;
	struct task_struct   *worker_thread;
	unsigned long data_flags;
	uint32_t border_sge_num;

	uint32_t hbq_in_use;		/* HBQs in use flag */
	uint32_t hbq_count;	        /* Count of configured HBQs */
	struct hbq_s hbqs[LPFC_MAX_HBQS]; /* local copy of hbq indicies  */

	atomic_t fcp_qidx;         /* next FCP WQ (RR Policy) */
	atomic_t nvme_qidx;        /* next NVME WQ (RR Policy) */

	phys_addr_t pci_bar0_map;     /* Physical address for PCI BAR0 */
	phys_addr_t pci_bar1_map;     /* Physical address for PCI BAR1 */
	phys_addr_t pci_bar2_map;     /* Physical address for PCI BAR2 */
	void __iomem *slim_memmap_p;	/* Kernel memory mapped address for
					   PCI BAR0 */
	void __iomem *ctrl_regs_memmap_p;/* Kernel memory mapped address for
					    PCI BAR2 */

	void __iomem *pci_bar0_memmap_p; /* Kernel memory mapped address for
					    PCI BAR0 with dual-ULP support */
	void __iomem *pci_bar2_memmap_p; /* Kernel memory mapped address for
					    PCI BAR2 with dual-ULP support */
	void __iomem *pci_bar4_memmap_p; /* Kernel memory mapped address for
					    PCI BAR4 with dual-ULP support */
#define PCI_64BIT_BAR0	0
#define PCI_64BIT_BAR2	2
#define PCI_64BIT_BAR4	4
	void __iomem *MBslimaddr;	/* virtual address for mbox cmds */
	void __iomem *HAregaddr;	/* virtual address for host attn reg */
	void __iomem *CAregaddr;	/* virtual address for chip attn reg */
	void __iomem *HSregaddr;	/* virtual address for host status
					   reg */
	void __iomem *HCregaddr;	/* virtual address for host ctl reg */

	struct lpfc_hgp __iomem *host_gp; /* Host side get/put pointers */
	struct lpfc_pgp   *port_gp;
	uint32_t __iomem  *hbq_put;     /* Address in SLIM to HBQ put ptrs */
	uint32_t          *hbq_get;     /* Host mem address of HBQ get ptrs */

	int brd_no;			/* FC board number */
	char SerialNumber[32];		/* adapter Serial Number */
	char OptionROMVersion[32];	/* adapter BIOS / Fcode version */
	char BIOSVersion[16];		/* Boot BIOS version */
	char ModelDesc[256];		/* Model Description */
	char ModelName[80];		/* Model Name */
	char ProgramType[256];		/* Program Type */
	char Port[20];			/* Port No */
	uint8_t vpd_flag;               /* VPD data flag */

#define VPD_MODEL_DESC      0x1         /* valid vpd model description */
#define VPD_MODEL_NAME      0x2         /* valid vpd model name */
#define VPD_PROGRAM_TYPE    0x4         /* valid vpd program type */
#define VPD_PORT            0x8         /* valid vpd port data */
#define VPD_MASK            0xf         /* mask for any vpd data */


	struct timer_list fcp_poll_timer;
	struct timer_list eratt_poll;
	uint32_t eratt_poll_interval;

	uint64_t bg_guard_err_cnt;
	uint64_t bg_apptag_err_cnt;
	uint64_t bg_reftag_err_cnt;

	/* fastpath list. */
	spinlock_t scsi_buf_list_get_lock;  /* SCSI buf alloc list lock */
	spinlock_t scsi_buf_list_put_lock;  /* SCSI buf free list lock */
	struct list_head lpfc_scsi_buf_list_get;
	struct list_head lpfc_scsi_buf_list_put;
	uint32_t total_scsi_bufs;
	struct list_head lpfc_iocb_list;
	uint32_t total_iocbq_bufs;
	struct list_head active_rrq_list;
	spinlock_t hbalock;
	struct work_struct  unblock_request_work; /* SCSI layer unblock IOs */

	/* dma_mem_pools */
	struct dma_pool *lpfc_sg_dma_buf_pool;
	struct dma_pool *lpfc_mbuf_pool;
	struct dma_pool *lpfc_hrb_pool;	/* header receive buffer pool */
	struct dma_pool *lpfc_drb_pool; /* data receive buffer pool */
	struct dma_pool *lpfc_nvmet_drb_pool; /* data receive buffer pool */
	struct dma_pool *lpfc_hbq_pool;	/* SLI3 hbq buffer pool */
	struct dma_pool *lpfc_cmd_rsp_buf_pool;
	struct lpfc_dma_pool lpfc_mbuf_safety_pool;

	mempool_t *mbox_mem_pool;
	mempool_t *nlp_mem_pool;
	mempool_t *rrq_pool;
	mempool_t *active_rrq_pool;

	struct fc_host_statistics link_stats;
	enum lpfc_irq_chann_mode irq_chann_mode;
	enum intr_type_t intr_type;
	uint32_t intr_mode;
#define LPFC_INTR_ERROR	0xFFFFFFFF
	struct list_head port_list;
	spinlock_t port_list_lock;	/* lock for port_list mutations */
	struct lpfc_vport *pport;	/* physical lpfc_vport pointer */
	uint16_t max_vpi;		/* Maximum virtual nports */
#define LPFC_MAX_VPI	0xFF		/* Max number VPI supported 0 - 0xff */
#define LPFC_MAX_VPORTS	0x100		/* Max vports per port, with pport */
	uint16_t max_vports;            /*
					 * For IOV HBAs max_vpi can change
					 * after a reset. max_vports is max
					 * number of vports present. This can
					 * be greater than max_vpi.
					 */
	uint16_t vpi_base;
	uint16_t vfi_base;
	unsigned long *vpi_bmask;	/* vpi allocation table */
	uint16_t *vpi_ids;
	uint16_t vpi_count;
	struct list_head lpfc_vpi_blk_list;

	/* Data structure used by fabric iocb scheduler */
	struct list_head fabric_iocb_list;
	atomic_t fabric_iocb_count;
	struct timer_list fabric_block_timer;
	unsigned long bit_flags;
	atomic_t num_rsrc_err;
	atomic_t num_cmd_success;
	unsigned long last_rsrc_error_time;
	unsigned long last_ramp_down_time;
#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	struct dentry *hba_debugfs_root;
	atomic_t debugfs_vport_count;
	struct dentry *debug_multixri_pools;
	struct dentry *debug_hbqinfo;
	struct dentry *debug_dumpHostSlim;
	struct dentry *debug_dumpHBASlim;
	struct dentry *debug_InjErrLBA;  /* LBA to inject errors at */
	struct dentry *debug_InjErrNPortID;  /* NPortID to inject errors at */
	struct dentry *debug_InjErrWWPN;  /* WWPN to inject errors at */
	struct dentry *debug_writeGuard; /* inject write guard_tag errors */
	struct dentry *debug_writeApp;   /* inject write app_tag errors */
	struct dentry *debug_writeRef;   /* inject write ref_tag errors */
	struct dentry *debug_readGuard;  /* inject read guard_tag errors */
	struct dentry *debug_readApp;    /* inject read app_tag errors */
	struct dentry *debug_readRef;    /* inject read ref_tag errors */

	struct dentry *debug_nvmeio_trc;
	struct lpfc_debugfs_nvmeio_trc *nvmeio_trc;
	struct dentry *debug_hdwqinfo;
#ifdef LPFC_HDWQ_LOCK_STAT
	struct dentry *debug_lockstat;
#endif
	struct dentry *debug_cgn_buffer;
	struct dentry *debug_rx_monitor;
	struct dentry *debug_ras_log;
	atomic_t nvmeio_trc_cnt;
	uint32_t nvmeio_trc_size;
	uint32_t nvmeio_trc_output_idx;

	/* T10 DIF error injection */
	uint32_t lpfc_injerr_wgrd_cnt;
	uint32_t lpfc_injerr_wapp_cnt;
	uint32_t lpfc_injerr_wref_cnt;
	uint32_t lpfc_injerr_rgrd_cnt;
	uint32_t lpfc_injerr_rapp_cnt;
	uint32_t lpfc_injerr_rref_cnt;
	uint32_t lpfc_injerr_nportid;
	struct lpfc_name lpfc_injerr_wwpn;
	sector_t lpfc_injerr_lba;
#define LPFC_INJERR_LBA_OFF	(sector_t)(-1)

	struct dentry *debug_slow_ring_trc;
	struct lpfc_debugfs_trc *slow_ring_trc;
	atomic_t slow_ring_trc_cnt;
	/* iDiag debugfs sub-directory */
	struct dentry *idiag_root;
	struct dentry *idiag_pci_cfg;
	struct dentry *idiag_bar_acc;
	struct dentry *idiag_que_info;
	struct dentry *idiag_que_acc;
	struct dentry *idiag_drb_acc;
	struct dentry *idiag_ctl_acc;
	struct dentry *idiag_mbx_acc;
	struct dentry *idiag_ext_acc;
	uint8_t lpfc_idiag_last_eq;
#endif
	uint16_t nvmeio_trc_on;

	/* Used for deferred freeing of ELS data buffers */
	struct list_head elsbuf;
	int elsbuf_cnt;
	int elsbuf_prev_cnt;

	uint8_t temp_sensor_support;
	/* Fields used for heart beat. */
	unsigned long last_completion_time;
	unsigned long skipped_hb;
	struct timer_list hb_tmofunc;
	struct timer_list rrq_tmr;
	enum hba_temp_state over_temp_state;
	/*
	 * Following bit will be set for all buffer tags which are not
	 * associated with any HBQ.
	 */
#define QUE_BUFTAG_BIT  (1<<31)
	uint32_t buffer_tag_count;

/* Maximum number of events that can be outstanding at any time*/
#define LPFC_MAX_EVT_COUNT 512
	atomic_t fast_event_count;
	uint32_t fcoe_eventtag;
	uint32_t fcoe_eventtag_at_fcf_scan;
	uint32_t fcoe_cvl_eventtag;
	uint32_t fcoe_cvl_eventtag_attn;
	struct lpfc_fcf fcf;
	uint8_t fc_map[3];
	uint8_t valid_vlan;
	uint16_t vlan_id;
	struct list_head fcf_conn_rec_list;

	bool defer_flogi_acc_flag;
	uint16_t defer_flogi_acc_rx_id;
	uint16_t defer_flogi_acc_ox_id;

	spinlock_t ct_ev_lock; /* synchronize access to ct_ev_waiters */
	struct list_head ct_ev_waiters;
	struct unsol_rcv_ct_ctx ct_ctx[LPFC_CT_CTX_MAX];
	uint32_t ctx_idx;
	struct timer_list inactive_vmid_poll;

	/* RAS Support */
	struct lpfc_ras_fwlog ras_fwlog;

	uint32_t iocb_cnt;
	uint32_t iocb_max;
	atomic_t sdev_cnt;
	spinlock_t devicelock;	/* lock for luns list */
	mempool_t *device_data_mem_pool;
	struct list_head luns;
#define LPFC_TRANSGRESSION_HIGH_TEMPERATURE	0x0080
#define LPFC_TRANSGRESSION_LOW_TEMPERATURE	0x0040
#define LPFC_TRANSGRESSION_HIGH_VOLTAGE		0x0020
#define LPFC_TRANSGRESSION_LOW_VOLTAGE		0x0010
#define LPFC_TRANSGRESSION_HIGH_TXBIAS		0x0008
#define LPFC_TRANSGRESSION_LOW_TXBIAS		0x0004
#define LPFC_TRANSGRESSION_HIGH_TXPOWER		0x0002
#define LPFC_TRANSGRESSION_LOW_TXPOWER		0x0001
#define LPFC_TRANSGRESSION_HIGH_RXPOWER		0x8000
#define LPFC_TRANSGRESSION_LOW_RXPOWER		0x4000
	uint16_t sfp_alarm;
	uint16_t sfp_warning;

#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	uint16_t hdwqstat_on;
#define LPFC_CHECK_OFF		0
#define LPFC_CHECK_NVME_IO	1
#define LPFC_CHECK_NVMET_IO	2
#define LPFC_CHECK_SCSI_IO	4
	uint16_t ktime_on;
	uint64_t ktime_data_samples;
	uint64_t ktime_status_samples;
	uint64_t ktime_last_cmd;
	uint64_t ktime_seg1_total;
	uint64_t ktime_seg1_min;
	uint64_t ktime_seg1_max;
	uint64_t ktime_seg2_total;
	uint64_t ktime_seg2_min;
	uint64_t ktime_seg2_max;
	uint64_t ktime_seg3_total;
	uint64_t ktime_seg3_min;
	uint64_t ktime_seg3_max;
	uint64_t ktime_seg4_total;
	uint64_t ktime_seg4_min;
	uint64_t ktime_seg4_max;
	uint64_t ktime_seg5_total;
	uint64_t ktime_seg5_min;
	uint64_t ktime_seg5_max;
	uint64_t ktime_seg6_total;
	uint64_t ktime_seg6_min;
	uint64_t ktime_seg6_max;
	uint64_t ktime_seg7_total;
	uint64_t ktime_seg7_min;
	uint64_t ktime_seg7_max;
	uint64_t ktime_seg8_total;
	uint64_t ktime_seg8_min;
	uint64_t ktime_seg8_max;
	uint64_t ktime_seg9_total;
	uint64_t ktime_seg9_min;
	uint64_t ktime_seg9_max;
	uint64_t ktime_seg10_total;
	uint64_t ktime_seg10_min;
	uint64_t ktime_seg10_max;
#endif
	/* CMF objects */
	struct lpfc_cgn_stat __percpu *cmf_stat;
	uint32_t cmf_interval_rate;  /* timer interval limit in ms */
	uint32_t cmf_timer_cnt;
#define LPFC_CMF_INTERVAL 90
	uint64_t cmf_link_byte_count;
	uint64_t cmf_max_line_rate;
	uint64_t cmf_max_bytes_per_interval;
	uint64_t cmf_last_sync_bw;
#define  LPFC_CMF_BLK_SIZE 512
	struct hrtimer cmf_timer;
	atomic_t cmf_bw_wait;
	atomic_t cmf_busy;
	atomic_t cmf_stop_io;      /* To block request and stop IO's */
	uint32_t cmf_active_mode;
	uint32_t cmf_info_per_interval;
#define LPFC_MAX_CMF_INFO 32
	struct timespec64 cmf_latency;  /* Interval congestion timestamp */
	uint32_t cmf_last_ts;   /* Interval congestion time (ms) */
	uint32_t cmf_active_info;

	/* Signal / FPIN handling for Congestion Mgmt */
	u8 cgn_reg_fpin;           /* Negotiated value from RDF */
	u8 cgn_init_reg_fpin;      /* Initial value from READ_CONFIG */
#define LPFC_CGN_FPIN_NONE	0x0
#define LPFC_CGN_FPIN_WARN	0x1
#define LPFC_CGN_FPIN_ALARM	0x2
#define LPFC_CGN_FPIN_BOTH	(LPFC_CGN_FPIN_WARN | LPFC_CGN_FPIN_ALARM)

	u8 cgn_reg_signal;          /* Negotiated value from EDC */
	u8 cgn_init_reg_signal;     /* Initial value from READ_CONFIG */
		/* cgn_reg_signal and cgn_init_reg_signal use
		 * enum fc_edc_cg_signal_cap_types
		 */
	u16 cgn_fpin_frequency;		/* In units of msecs */
#define LPFC_FPIN_INIT_FREQ	0xffff
	u32 cgn_sig_freq;
	u32 cgn_acqe_cnt;

	/* RX monitor handling for CMF */
	struct lpfc_rx_info_monitor *rx_monitor;
	atomic_t rx_max_read_cnt;       /* Maximum read bytes */
	uint64_t rx_block_cnt;

	/* Congestion parameters from flash */
	struct lpfc_cgn_param cgn_p;

	/* Statistics counter for ACQE cgn alarms and warnings */
	struct lpfc_cgn_acqe_stat cgn_acqe_stat;

	/* Congestion buffer information */
	struct lpfc_dmabuf *cgn_i;      /* Congestion Info buffer */
	atomic_t cgn_fabric_warn_cnt;   /* Total warning cgn events for info */
	atomic_t cgn_fabric_alarm_cnt;  /* Total alarm cgn events for info */
	atomic_t cgn_sync_warn_cnt;     /* Total warning events for SYNC wqe */
	atomic_t cgn_sync_alarm_cnt;    /* Total alarm events for SYNC wqe */
	atomic_t cgn_driver_evt_cnt;    /* Total driver cgn events for fmw */
	atomic_t cgn_latency_evt_cnt;
	struct timespec64 cgn_daily_ts;
	atomic64_t cgn_latency_evt;     /* Avg latency per minute */
	unsigned long cgn_evt_timestamp;
#define LPFC_CGN_TIMER_TO_MIN   60000 /* ms in a minute */
	uint32_t cgn_evt_minute;
#define LPFC_SEC_MIN		60
#define LPFC_MIN_HOUR		60
#define LPFC_HOUR_DAY		24
#define LPFC_MIN_DAY		(LPFC_MIN_HOUR * LPFC_HOUR_DAY)

	struct hlist_node cpuhp;	/* used for cpuhp per hba callback */
	struct timer_list cpuhp_poll_timer;
	struct list_head poll_list;	/* slowpath eq polling list */
#define LPFC_POLL_HB	1		/* slowpath heartbeat */

	char os_host_name[MAXHOSTNAMELEN];

	/* LD Signaling */
	u32 degrade_activate_threshold;
	u32 degrade_deactivate_threshold;
	u32 fec_degrade_interval;

	atomic_t dbg_log_idx;
	atomic_t dbg_log_cnt;
	atomic_t dbg_log_dmping;
	struct dbg_log_ent dbg_log[DBG_LOG_SZ];
};

#define LPFC_MAX_RXMONITOR_ENTRY	800
#define LPFC_MAX_RXMONITOR_DUMP		32
struct rx_info_entry {
	uint64_t cmf_bytes;	/* Total no of read bytes for CMF_SYNC_WQE */
	uint64_t total_bytes;   /* Total no of read bytes requested */
	uint64_t rcv_bytes;     /* Total no of read bytes completed */
	uint64_t avg_io_size;
	uint64_t avg_io_latency;/* Average io latency in microseconds */
	uint64_t max_read_cnt;  /* Maximum read bytes */
	uint64_t max_bytes_per_interval;
	uint32_t cmf_busy;
	uint32_t cmf_info;      /* CMF_SYNC_WQE info */
	uint32_t io_cnt;
	uint32_t timer_utilization;
	uint32_t timer_interval;
};

struct lpfc_rx_info_monitor {
	struct rx_info_entry *ring; /* info organized in a circular buffer */
	u32 head_idx, tail_idx; /* index to head/tail of ring */
	spinlock_t lock; /* spinlock for ring */
	u32 entries; /* storing number entries/size of ring */
};

static inline struct Scsi_Host *
lpfc_shost_from_vport(struct lpfc_vport *vport)
{
	return container_of((void *) vport, struct Scsi_Host, hostdata[0]);
}

static inline void
lpfc_set_loopback_flag(struct lpfc_hba *phba)
{
	if (phba->cfg_topology == FLAGS_LOCAL_LB)
		phba->link_flag |= LS_LOOPBACK_MODE;
	else
		phba->link_flag &= ~LS_LOOPBACK_MODE;
}

static inline int
lpfc_is_link_up(struct lpfc_hba *phba)
{
	return  phba->link_state == LPFC_LINK_UP ||
		phba->link_state == LPFC_CLEAR_LA ||
		phba->link_state == LPFC_HBA_READY;
}

static inline void
lpfc_worker_wake_up(struct lpfc_hba *phba)
{
	/* Set the lpfc data pending flag */
	set_bit(LPFC_DATA_READY, &phba->data_flags);

	/* Wake up worker thread */
	wake_up(&phba->work_waitq);
	return;
}

static inline int
lpfc_readl(void __iomem *addr, uint32_t *data)
{
	uint32_t temp;
	temp = readl(addr);
	if (temp == 0xffffffff)
		return -EIO;
	*data = temp;
	return 0;
}

static inline int
lpfc_sli_read_hs(struct lpfc_hba *phba)
{
	/*
	 * There was a link/board error. Read the status register to retrieve
	 * the error event and process it.
	 */
	phba->sli.slistat.err_attn_event++;

	/* Save status info and check for unplug error */
	if (lpfc_readl(phba->HSregaddr, &phba->work_hs) ||
		lpfc_readl(phba->MBslimaddr + 0xa8, &phba->work_status[0]) ||
		lpfc_readl(phba->MBslimaddr + 0xac, &phba->work_status[1])) {
		return -EIO;
	}

	/* Clear chip Host Attention error bit */
	writel(HA_ERATT, phba->HAregaddr);
	readl(phba->HAregaddr); /* flush */
	phba->pport->stopped = 1;

	return 0;
}

static inline struct lpfc_sli_ring *
lpfc_phba_elsring(struct lpfc_hba *phba)
{
	/* Return NULL if sli_rev has become invalid due to bad fw */
	if (phba->sli_rev != LPFC_SLI_REV4  &&
	    phba->sli_rev != LPFC_SLI_REV3  &&
	    phba->sli_rev != LPFC_SLI_REV2)
		return NULL;

	if (phba->sli_rev == LPFC_SLI_REV4) {
		if (phba->sli4_hba.els_wq)
			return phba->sli4_hba.els_wq->pring;
		else
			return NULL;
	}
	return &phba->sli.sli3_ring[LPFC_ELS_RING];
}

/**
 * lpfc_next_online_cpu - Finds next online CPU on cpumask
 * @mask: Pointer to phba's cpumask member.
 * @start: starting cpu index
 *
 * Note: If no valid cpu found, then nr_cpu_ids is returned.
 *
 **/
static inline unsigned int
lpfc_next_online_cpu(const struct cpumask *mask, unsigned int start)
{
	unsigned int cpu_it;

	for_each_cpu_wrap(cpu_it, mask, start) {
		if (cpu_online(cpu_it))
			break;
	}

	return cpu_it;
}
/**
 * lpfc_sli4_mod_hba_eq_delay - update EQ delay
 * @phba: Pointer to HBA context object.
 * @q: The Event Queue to update.
 * @delay: The delay value (in us) to be written.
 *
 **/
static inline void
lpfc_sli4_mod_hba_eq_delay(struct lpfc_hba *phba, struct lpfc_queue *eq,
			   u32 delay)
{
	struct lpfc_register reg_data;

	reg_data.word0 = 0;
	bf_set(lpfc_sliport_eqdelay_id, &reg_data, eq->queue_id);
	bf_set(lpfc_sliport_eqdelay_delay, &reg_data, delay);
	writel(reg_data.word0, phba->sli4_hba.u.if_type2.EQDregaddr);
	eq->q_mode = delay;
}


/*
 * Macro that declares tables and a routine to perform enum type to
 * ascii string lookup.
 *
 * Defines a <key,value> table for an enum. Uses xxx_INIT defines for
 * the enum to populate the table.  Macro defines a routine (named
 * by caller) that will search all elements of the table for the key
 * and return the name string if found or "Unrecognized" if not found.
 */
#define DECLARE_ENUM2STR_LOOKUP(routine, enum_name, enum_init)		\
static struct {								\
	enum enum_name		value;					\
	char			*name;					\
} fc_##enum_name##_e2str_names[] = enum_init;				\
static const char *routine(enum enum_name table_key)			\
{									\
	int i;								\
	char *name = "Unrecognized";					\
									\
	for (i = 0; i < ARRAY_SIZE(fc_##enum_name##_e2str_names); i++) {\
		if (fc_##enum_name##_e2str_names[i].value == table_key) {\
			name = fc_##enum_name##_e2str_names[i].name;	\
			break;						\
		}							\
	}								\
	return name;							\
}

/**
 * lpfc_is_vmid_enabled - returns if VMID is enabled for either switch types
 * @phba: Pointer to HBA context object.
 *
 * Relationship between the enable, target support and if vmid tag is required
 * for the particular combination
 * ---------------------------------------------------
 * Switch    Enable Flag  Target Support  VMID Needed
 * ---------------------------------------------------
 * App Id     0              NA              N
 * App Id     1               0              N
 * App Id     1               1              Y
 * Pr Tag     0              NA              N
 * Pr Tag     1               0              N
 * Pr Tag     1               1              Y
 * Pr Tag     2               *              Y
 ---------------------------------------------------
 *
 **/
static inline int lpfc_is_vmid_enabled(struct lpfc_hba *phba)
{
	return phba->cfg_vmid_app_header || phba->cfg_vmid_priority_tagging;
}

static inline
u8 get_job_ulpstatus(struct lpfc_hba *phba, struct lpfc_iocbq *iocbq)
{
	if (phba->sli_rev == LPFC_SLI_REV4)
		return bf_get(lpfc_wcqe_c_status, &iocbq->wcqe_cmpl);
	else
		return iocbq->iocb.ulpStatus;
}

static inline
u32 get_job_word4(struct lpfc_hba *phba, struct lpfc_iocbq *iocbq)
{
	if (phba->sli_rev == LPFC_SLI_REV4)
		return iocbq->wcqe_cmpl.parameter;
	else
		return iocbq->iocb.un.ulpWord[4];
}

static inline
u8 get_job_cmnd(struct lpfc_hba *phba, struct lpfc_iocbq *iocbq)
{
	if (phba->sli_rev == LPFC_SLI_REV4)
		return bf_get(wqe_cmnd, &iocbq->wqe.generic.wqe_com);
	else
		return iocbq->iocb.ulpCommand;
}

static inline
u16 get_job_ulpcontext(struct lpfc_hba *phba, struct lpfc_iocbq *iocbq)
{
	if (phba->sli_rev == LPFC_SLI_REV4)
		return bf_get(wqe_ctxt_tag, &iocbq->wqe.generic.wqe_com);
	else
		return iocbq->iocb.ulpContext;
}

static inline
u16 get_job_rcvoxid(struct lpfc_hba *phba, struct lpfc_iocbq *iocbq)
{
	if (phba->sli_rev == LPFC_SLI_REV4)
		return bf_get(wqe_rcvoxid, &iocbq->wqe.generic.wqe_com);
	else
		return iocbq->iocb.unsli3.rcvsli3.ox_id;
}

static inline
u32 get_job_data_placed(struct lpfc_hba *phba, struct lpfc_iocbq *iocbq)
{
	if (phba->sli_rev == LPFC_SLI_REV4)
		return iocbq->wcqe_cmpl.total_data_placed;
	else
		return iocbq->iocb.un.genreq64.bdl.bdeSize;
}

static inline
u32 get_job_abtsiotag(struct lpfc_hba *phba, struct lpfc_iocbq *iocbq)
{
	if (phba->sli_rev == LPFC_SLI_REV4)
		return iocbq->wqe.abort_cmd.wqe_com.abort_tag;
	else
		return iocbq->iocb.un.acxri.abortIoTag;
}

static inline
u32 get_job_els_rsp64_did(struct lpfc_hba *phba, struct lpfc_iocbq *iocbq)
{
	if (phba->sli_rev == LPFC_SLI_REV4)
		return bf_get(wqe_els_did, &iocbq->wqe.els_req.wqe_dest);
	else
		return iocbq->iocb.un.elsreq64.remoteID;
}
