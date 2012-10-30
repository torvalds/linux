/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2004-2012 Emulex.  All rights reserved.           *
 * EMULEX and SLI are trademarks of Emulex.                        *
 * www.emulex.com                                                  *
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
#define LPFC_DEFAULT_MENLO_SG_SEG_CNT 128	/* sg element count per scsi
		cmnd for menlo needs nearly twice as for firmware
		downloads using bsg */
#define LPFC_DEFAULT_PROT_SG_SEG_CNT 4096 /* sg protection elements count */
#define LPFC_MAX_SG_SEG_CNT	4096	/* sg element count per scsi cmnd */
#define LPFC_MAX_SGE_SIZE       0x80000000 /* Maximum data allowed in a SGE */
#define LPFC_MAX_PROT_SG_SEG_CNT 4096	/* prot sg element count per scsi cmd*/
#define LPFC_IOCB_LIST_CNT	2250	/* list of IOCBs for fast-path usage. */
#define LPFC_Q_RAMP_UP_INTERVAL 120     /* lun q_depth ramp up interval */
#define LPFC_VNAME_LEN		100	/* vport symbolic name length */
#define LPFC_TGTQ_INTERVAL	40000	/* Min amount of time between tgt
					   queue depth change in millisecs */
#define LPFC_TGTQ_RAMPUP_PCENT	5	/* Target queue rampup in percentage */
#define LPFC_MIN_TGT_QDEPTH	10
#define LPFC_MAX_TGT_QDEPTH	0xFFFF

#define  LPFC_MAX_BUCKET_COUNT 20	/* Maximum no. of buckets for stat data
					   collection. */
/*
 * Following time intervals are used of adjusting SCSI device
 * queue depths when there are driver resource error or Firmware
 * resource error.
 */
#define QUEUE_RAMP_DOWN_INTERVAL	(1 * HZ)   /* 1 Second */
#define QUEUE_RAMP_UP_INTERVAL		(300 * HZ) /* 5 minutes */

/* Number of exchanges reserved for discovery to complete */
#define LPFC_DISC_IOCB_BUFF_COUNT 20

#define LPFC_HB_MBOX_INTERVAL   5	/* Heart beat interval in seconds. */
#define LPFC_HB_MBOX_TIMEOUT    30	/* Heart beat timeout  in seconds. */

#define LPFC_LOOK_AHEAD_OFF	0	/* Look ahead logic is turned off */

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

/* Number of MSI-X vectors the driver uses */
#define LPFC_MSIX_VECTORS	2

/* lpfc wait event data ready flag */
#define LPFC_DATA_READY		(1<<0)

/* queue dump line buffer size */
#define LPFC_LBUF_SZ		128

/* mailbox system shutdown options */
#define LPFC_MBX_NO_WAIT	0
#define LPFC_MBX_WAIT		1

enum lpfc_polling_flags {
	ENABLE_FCP_RING_POLLING = 0x1,
	DISABLE_FCP_RING_INT    = 0x2
};

/* Provide DMA memory definitions the driver uses per port instance. */
struct lpfc_dmabuf {
	struct list_head list;
	void *virt;		/* virtual address ptr */
	dma_addr_t phys;	/* mapped address */
	uint32_t   buffer_tag;	/* used for tagged queue ring */
};

struct lpfc_dma_pool {
	struct lpfc_dmabuf   *elements;
	uint32_t    max_count;
	uint32_t    current_count;
};

struct hbq_dmabuf {
	struct lpfc_dmabuf hbuf;
	struct lpfc_dmabuf dbuf;
	uint32_t size;
	uint32_t tag;
	struct lpfc_cq_event cq_event;
	unsigned long time_stamp;
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
		uint32_t rsvd3  :19;  /* Reserved                             */
		uint32_t cdss	: 1;  /* Configure Data Security SLI          */
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
		uint32_t cdss	: 1;  /* Configure Data Security SLI          */
		uint32_t rsvd3  :19;  /* Reserved                             */
#endif
	} sli3Feat;
} lpfc_vpd_t;

struct lpfc_scsi_buf;


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
	uint32_t elsRcvRPS;
	uint32_t elsRcvRPL;
	uint32_t elsRcvRRQ;
	uint32_t elsRcvRTV;
	uint32_t elsRcvECHO;
	uint32_t elsXmitFLOGI;
	uint32_t elsXmitFDISC;
	uint32_t elsXmitPLOGI;
	uint32_t elsXmitPRLI;
	uint32_t elsXmitADISC;
	uint32_t elsXmitLOGO;
	uint32_t elsXmitSCR;
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

	uint32_t num_disc_nodes;	/*in addition to hba_state */

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
#define WORKER_FDMI_TMO                0x4	/* vport: FDMI timeout */
#define WORKER_DELAYED_DISC_TMO        0x8	/* vport: delayed discovery */

#define WORKER_MBOX_TMO                0x100	/* hba: MBOX timeout */
#define WORKER_HB_TMO                  0x200	/* hba: Heart beat timeout */
#define WORKER_FABRIC_BLOCK_TMO        0x400	/* hba: fabric block timeout */
#define WORKER_RAMP_DOWN_QUEUE         0x800	/* hba: Decrease Q depth */
#define WORKER_RAMP_UP_QUEUE           0x1000	/* hba: Increase Q depth */
#define WORKER_SERVICE_TXQ             0x2000	/* hba: IOCBs on the txq */

	struct timer_list fc_fdmitmo;
	struct timer_list els_tmofunc;
	struct timer_list delayed_disc_tmo;

	int unreg_vpi_cmpl;

	uint8_t load_flag;
#define FC_LOADING		0x1	/* HBA in process of loading drvr */
#define FC_UNLOADING		0x2	/* HBA in process of unloading drvr */
	/* Vport Config Parameters */
	uint32_t cfg_scan_down;
	uint32_t cfg_lun_queue_depth;
	uint32_t cfg_nodev_tmo;
	uint32_t cfg_devloss_tmo;
	uint32_t cfg_restrict_login;
	uint32_t cfg_peer_port_login;
	uint32_t cfg_fcp_class;
	uint32_t cfg_use_adisc;
	uint32_t cfg_fdmi_on;
	uint32_t cfg_discovery_threads;
	uint32_t cfg_log_verbose;
	uint32_t cfg_max_luns;
	uint32_t cfg_enable_da_id;
	uint32_t cfg_max_scsicmpl_time;
	uint32_t cfg_tgt_queue_depth;

	uint32_t dev_loss_tmo_changed;

	struct fc_vport *fc_vport;

#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	struct dentry *debug_disc_trc;
	struct dentry *debug_nodelist;
	struct dentry *vport_debugfs_root;
	struct lpfc_debugfs_trc *disc_trc;
	atomic_t disc_trc_cnt;
#endif
	uint8_t stat_data_enabled;
	uint8_t stat_data_blocked;
	struct list_head rcv_buffer_list;
	unsigned long rcv_buffer_time_stamp;
	uint32_t vport_flag;
#define STATIC_VPORT	1
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

#define LPFC_MAX_HBQS  4
/* this matches the position in the lpfc_hbq_defs array */
#define LPFC_ELS_HBQ	0
#define LPFC_EXTRA_HBQ	1

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

struct unsol_rcv_ct_ctx {
	uint32_t ctxt_id;
	uint32_t SID;
	uint32_t flags;
#define UNSOL_VALID	0x00000001
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
#define LPFC_USER_LINK_SPEED_MAX	LPFC_USER_LINK_SPEED_16G
#define LPFC_USER_LINK_SPEED_BITMAP ((1 << LPFC_USER_LINK_SPEED_16G) | \
				     (1 << LPFC_USER_LINK_SPEED_10G) | \
				     (1 << LPFC_USER_LINK_SPEED_8G) | \
				     (1 << LPFC_USER_LINK_SPEED_4G) | \
				     (1 << LPFC_USER_LINK_SPEED_2G) | \
				     (1 << LPFC_USER_LINK_SPEED_1G) | \
				     (1 << LPFC_USER_LINK_SPEED_AUTO))
#define LPFC_LINK_SPEED_STRING "0, 1, 2, 4, 8, 10, 16"

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

struct lpfc_hba {
	/* SCSI interface function jump table entries */
	int (*lpfc_new_scsi_buf)
		(struct lpfc_vport *, int);
	struct lpfc_scsi_buf * (*lpfc_get_scsi_buf)
		(struct lpfc_hba *, struct lpfc_nodelist *);
	int (*lpfc_scsi_prep_dma_buf)
		(struct lpfc_hba *, struct lpfc_scsi_buf *);
	void (*lpfc_scsi_unprep_dma_buf)
		(struct lpfc_hba *, struct lpfc_scsi_buf *);
	void (*lpfc_release_scsi_buf)
		(struct lpfc_hba *, struct lpfc_scsi_buf *);
	void (*lpfc_rampdown_queue_depth)
		(struct lpfc_hba *);
	void (*lpfc_scsi_prep_cmnd)
		(struct lpfc_vport *, struct lpfc_scsi_buf *,
		 struct lpfc_nodelist *);

	/* IOCB interface function jump table entries */
	int (*__lpfc_sli_issue_iocb)
		(struct lpfc_hba *, uint32_t,
		 struct lpfc_iocbq *, uint32_t);
	void (*__lpfc_sli_release_iocbq)(struct lpfc_hba *,
			 struct lpfc_iocbq *);
	int (*lpfc_hba_down_post)(struct lpfc_hba *phba);
	IOCB_t * (*lpfc_get_iocb_from_iocbq)
		(struct lpfc_iocbq *);
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
		(struct lpfc_hba *, struct lpfc_scsi_buf *);
	/* Add new entries here */

	/* SLI4 specific HBA data structure */
	struct lpfc_sli4_hba sli4_hba;

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

	enum hba_state link_state;
	uint32_t link_flag;	/* link state flags */
#define LS_LOOPBACK_MODE      0x1	/* NPort is in Loopback mode */
					/* This flag is set while issuing */
					/* INIT_LINK mailbox command */
#define LS_NPIV_FAB_SUPPORTED 0x2	/* Fabric supports NPIV */
#define LS_IGNORE_ERATT       0x4	/* intr handler should ignore ERATT */

	uint32_t hba_flag;	/* hba generic flags */
#define HBA_ERATT_HANDLED	0x1 /* This flag is set when eratt handled */
#define DEFER_ERATT		0x2 /* Deferred error attention in progress */
#define HBA_FCOE_MODE		0x4 /* HBA function in FCoE Mode */
#define HBA_SP_QUEUE_EVT	0x8 /* Slow-path qevt posted to worker thread*/
#define HBA_POST_RECEIVE_BUFFER 0x10 /* Rcv buffers need to be posted */
#define FCP_XRI_ABORT_EVENT	0x20
#define ELS_XRI_ABORT_EVENT	0x40
#define ASYNC_EVENT		0x80
#define LINK_DISABLED		0x100 /* Link disabled by user */
#define FCF_TS_INPROG           0x200 /* FCF table scan in progress */
#define FCF_RR_INPROG           0x400 /* FCF roundrobin flogi in progress */
#define HBA_FIP_SUPPORT		0x800 /* FIP support in HBA */
#define HBA_AER_ENABLED		0x1000 /* AER enabled with HBA */
#define HBA_DEVLOSS_TMO         0x2000 /* HBA in devloss timeout */
#define HBA_RRQ_ACTIVE		0x4000 /* process the rrq active list */
#define HBA_FCP_IOQ_FLUSH	0x8000 /* FCP I/O queues being flushed */
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
	uint32_t fc_citov;	/* C_I_TOV timer value */

	struct serv_parm fc_fabparam;	/* fabric service parameters buffer */
	uint8_t alpa_map[128];	/* AL_PA map from READ_LA */

	uint32_t lmt;

	uint32_t fc_topology;	/* link topology, from LINK INIT */

	struct lpfc_stats fc_stat;

	struct lpfc_nodelist fc_fcpnodev; /* nodelist entry for no device */
	uint32_t nport_event_cnt;	/* timestamp for nlplist entry */

	uint8_t  wwnn[8];
	uint8_t  wwpn[8];
	uint32_t RandomData[7];

	/* HBA Config Parameters */
	uint32_t cfg_ack0;
	uint32_t cfg_enable_npiv;
	uint32_t cfg_enable_rrq;
	uint32_t cfg_topology;
	uint32_t cfg_link_speed;
#define LPFC_FCF_FOV 1		/* Fast fcf failover */
#define LPFC_FCF_PRIORITY 2	/* Priority fcf failover */
	uint32_t cfg_fcf_failover_policy;
	uint32_t cfg_fcp_io_sched;
	uint32_t cfg_cr_delay;
	uint32_t cfg_cr_count;
	uint32_t cfg_multi_ring_support;
	uint32_t cfg_multi_ring_rctl;
	uint32_t cfg_multi_ring_type;
	uint32_t cfg_poll;
	uint32_t cfg_poll_tmo;
	uint32_t cfg_use_msi;
	uint32_t cfg_fcp_imax;
	uint32_t cfg_fcp_wq_count;
	uint32_t cfg_fcp_eq_count;
	uint32_t cfg_fcp_io_channel;
	uint32_t cfg_sg_seg_cnt;
	uint32_t cfg_prot_sg_seg_cnt;
	uint32_t cfg_sg_dma_buf_size;
	uint64_t cfg_soft_wwnn;
	uint64_t cfg_soft_wwpn;
	uint32_t cfg_hba_queue_depth;
	uint32_t cfg_enable_hba_reset;
	uint32_t cfg_enable_hba_heartbeat;
	uint32_t cfg_enable_bg;
	uint32_t cfg_hostmem_hgp;
	uint32_t cfg_log_verbose;
	uint32_t cfg_aer_support;
	uint32_t cfg_sriov_nr_virtfn;
	uint32_t cfg_iocb_cnt;
	uint32_t cfg_suppress_link_up;
#define LPFC_INITIALIZE_LINK              0	/* do normal init_link mbox */
#define LPFC_DELAY_INIT_LINK              1	/* layered driver hold off */
#define LPFC_DELAY_INIT_LINK_INDEFINITELY 2	/* wait, manual intervention */
	uint32_t cfg_enable_dss;
	lpfc_vpd_t vpd;		/* vital product data */

	struct pci_dev *pcidev;
	struct list_head      work_list;
	uint32_t              work_ha;      /* Host Attention Bits for WT */
	uint32_t              work_ha_mask; /* HA Bits owned by WT        */
	uint32_t              work_hs;      /* HS stored in case of ERRAT */
	uint32_t              work_status[2]; /* Extra status from SLIM */

	wait_queue_head_t    work_waitq;
	struct task_struct   *worker_thread;
	unsigned long data_flags;

	uint32_t hbq_in_use;		/* HBQs in use flag */
	struct list_head rb_pend_list;  /* Received buffers to be processed */
	uint32_t hbq_count;	        /* Count of configured HBQs */
	struct hbq_s hbqs[LPFC_MAX_HBQS]; /* local copy of hbq indicies  */

	atomic_t fcp_qidx;		/* next work queue to post work to */

	unsigned long pci_bar0_map;     /* Physical address for PCI BAR0 */
	unsigned long pci_bar1_map;     /* Physical address for PCI BAR1 */
	unsigned long pci_bar2_map;     /* Physical address for PCI BAR2 */
	void __iomem *slim_memmap_p;	/* Kernel memory mapped address for
					   PCI BAR0 */
	void __iomem *ctrl_regs_memmap_p;/* Kernel memory mapped address for
					    PCI BAR2 */

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

	uint8_t soft_wwn_enable;

	struct timer_list fcp_poll_timer;
	struct timer_list eratt_poll;

	/*
	 * stat  counters
	 */
	uint64_t fc4InputRequests;
	uint64_t fc4OutputRequests;
	uint64_t fc4ControlRequests;
	uint64_t bg_guard_err_cnt;
	uint64_t bg_apptag_err_cnt;
	uint64_t bg_reftag_err_cnt;

	/* fastpath list. */
	spinlock_t scsi_buf_list_lock;
	struct list_head lpfc_scsi_buf_list;
	uint32_t total_scsi_bufs;
	struct list_head lpfc_iocb_list;
	uint32_t total_iocbq_bufs;
	struct list_head active_rrq_list;
	spinlock_t hbalock;

	/* pci_mem_pools */
	struct pci_pool *lpfc_scsi_dma_buf_pool;
	struct pci_pool *lpfc_mbuf_pool;
	struct pci_pool *lpfc_hrb_pool;	/* header receive buffer pool */
	struct pci_pool *lpfc_drb_pool; /* data receive buffer pool */
	struct pci_pool *lpfc_hbq_pool;	/* SLI3 hbq buffer pool */
	struct lpfc_dma_pool lpfc_mbuf_safety_pool;

	mempool_t *mbox_mem_pool;
	mempool_t *nlp_mem_pool;
	mempool_t *rrq_pool;

	struct fc_host_statistics link_stats;
	enum intr_type_t intr_type;
	uint32_t intr_mode;
#define LPFC_INTR_ERROR	0xFFFFFFFF
	struct msix_entry msix_entries[LPFC_MSIX_VECTORS];

	struct list_head port_list;
	struct lpfc_vport *pport;	/* physical lpfc_vport pointer */
	uint16_t max_vpi;		/* Maximum virtual nports */
#define LPFC_MAX_VPI 0xFFFF		/* Max number of VPI supported */
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
#define	FABRIC_COMANDS_BLOCKED	0
	atomic_t num_rsrc_err;
	atomic_t num_cmd_success;
	unsigned long last_rsrc_error_time;
	unsigned long last_ramp_down_time;
	unsigned long last_ramp_up_time;
#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	struct dentry *hba_debugfs_root;
	atomic_t debugfs_vport_count;
	struct dentry *debug_hbqinfo;
	struct dentry *debug_dumpHostSlim;
	struct dentry *debug_dumpHBASlim;
	struct dentry *debug_dumpData;   /* BlockGuard BPL */
	struct dentry *debug_dumpDif;    /* BlockGuard BPL */
	struct dentry *debug_InjErrLBA;  /* LBA to inject errors at */
	struct dentry *debug_InjErrNPortID;  /* NPortID to inject errors at */
	struct dentry *debug_InjErrWWPN;  /* WWPN to inject errors at */
	struct dentry *debug_writeGuard; /* inject write guard_tag errors */
	struct dentry *debug_writeApp;   /* inject write app_tag errors */
	struct dentry *debug_writeRef;   /* inject write ref_tag errors */
	struct dentry *debug_readGuard;  /* inject read guard_tag errors */
	struct dentry *debug_readApp;    /* inject read app_tag errors */
	struct dentry *debug_readRef;    /* inject read ref_tag errors */

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
#endif

	/* Used for deferred freeing of ELS data buffers */
	struct list_head elsbuf;
	int elsbuf_cnt;
	int elsbuf_prev_cnt;

	uint8_t temp_sensor_support;
	/* Fields used for heart beat. */
	unsigned long last_completion_time;
	unsigned long skipped_hb;
	struct timer_list hb_tmofunc;
	uint8_t hb_outstanding;
	struct timer_list rrq_tmr;
	enum hba_temp_state over_temp_state;
	/* ndlp reference management */
	spinlock_t ndlp_lock;
	/*
	 * Following bit will be set for all buffer tags which are not
	 * associated with any HBQ.
	 */
#define QUE_BUFTAG_BIT  (1<<31)
	uint32_t buffer_tag_count;
	int wait_4_mlo_maint_flg;
	wait_queue_head_t wait_4_mlo_m_q;
	/* data structure used for latency data collection */
#define LPFC_NO_BUCKET	   0
#define LPFC_LINEAR_BUCKET 1
#define LPFC_POWER2_BUCKET 2
	uint8_t  bucket_type;
	uint32_t bucket_base;
	uint32_t bucket_step;

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

	spinlock_t ct_ev_lock; /* synchronize access to ct_ev_waiters */
	struct list_head ct_ev_waiters;
	struct unsol_rcv_ct_ctx ct_ctx[64];
	uint32_t ctx_idx;

	uint8_t menlo_flag;	/* menlo generic flags */
#define HBA_MENLO_SUPPORT	0x1 /* HBA supports menlo commands */
	uint32_t iocb_cnt;
	uint32_t iocb_max;
	atomic_t sdev_cnt;
	uint8_t fips_spec_rev;
	uint8_t fips_level;
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
