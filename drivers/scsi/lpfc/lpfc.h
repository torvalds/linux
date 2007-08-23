/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2004-2007 Emulex.  All rights reserved.           *
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

struct lpfc_sli2_slim;

#define LPFC_MAX_TARGET		256	/* max number of targets supported */
#define LPFC_MAX_DISC_THREADS	64	/* max outstanding discovery els
					   requests */
#define LPFC_MAX_NS_RETRY	3	/* Number of retry attempts to contact
					   the NameServer  before giving up. */
#define LPFC_CMD_PER_LUN	3	/* max outstanding cmds per lun */
#define LPFC_SG_SEG_CNT		64	/* sg element count per scsi cmnd */
#define LPFC_IOCB_LIST_CNT	2250	/* list of IOCBs for fast-path usage. */
#define LPFC_Q_RAMP_UP_INTERVAL 120     /* lun q_depth ramp up interval */

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

/* Define macros for 64 bit support */
#define putPaddrLow(addr)    ((uint32_t) (0xffffffff & (u64)(addr)))
#define putPaddrHigh(addr)   ((uint32_t) (0xffffffff & (((u64)(addr))>>32)))
#define getPaddr(high, low)  ((dma_addr_t)( \
			     (( (u64)(high)<<16 ) << 16)|( (u64)(low))))
/* Provide maximum configuration definitions. */
#define LPFC_DRVR_TIMEOUT	16	/* driver iocb timeout value in sec */
#define FC_MAX_ADPTMSG		64

#define MAX_HBAEVT	32

enum lpfc_polling_flags {
	ENABLE_FCP_RING_POLLING = 0x1,
	DISABLE_FCP_RING_INT    = 0x2
};

/* Provide DMA memory definitions the driver uses per port instance. */
struct lpfc_dmabuf {
	struct list_head list;
	void *virt;		/* virtual address ptr */
	dma_addr_t phys;	/* mapped address */
};

struct lpfc_dma_pool {
	struct lpfc_dmabuf   *elements;
	uint32_t    max_count;
	uint32_t    current_count;
};

struct hbq_dmabuf {
	struct lpfc_dmabuf dbuf;
	uint32_t size;
	uint32_t tag;
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
		uint32_t rsvd2  :24;  /* Reserved                             */
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
		uint32_t rsvd2  :24;  /* Reserved                             */
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
	uint32_t elsRcvRPS;
	uint32_t elsRcvRPL;
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

enum sysfs_mbox_state {
	SMBOX_IDLE,
	SMBOX_WRITING,
	SMBOX_READING
};

struct lpfc_sysfs_mbox {
	enum sysfs_mbox_state state;
	size_t                offset;
	struct lpfcMboxq *    mbox;
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
	struct list_head listentry;
	struct lpfc_hba *phba;
	uint8_t port_type;
#define LPFC_PHYSICAL_PORT 1
#define LPFC_NPIV_PORT  2
#define LPFC_FABRIC_PORT 3
	enum discovery_state port_state;

	uint16_t vpi;

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
#define FC_ESTABLISH_LINK       0x200	 /* Reestablish Link */
#define FC_RSCN_DISCOVERY       0x400	 /* Auth all devices after RSCN */
#define FC_SCSI_SCAN_TMO        0x4000	 /* scsi scan timer running */
#define FC_ABORT_DISCOVERY      0x8000	 /* we want to abort discovery */
#define FC_NDISC_ACTIVE         0x10000	 /* NPort discovery active */
#define FC_BYPASSED_MODE        0x20000	 /* NPort is in bypassed mode */
#define FC_RFF_NOT_SUPPORTED    0x40000	 /* RFF_ID was rejected by switch */
#define FC_VPORT_NEEDS_REG_VPI	0x80000  /* Needs to have its vpi registered */
#define FC_RSCN_DEFERRED	0x100000 /* A deferred RSCN being processed */

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

	int32_t stopped;   /* HBA has not been restarted since last ERATT */
	uint8_t fc_linkspeed;	/* Link speed after last READ_LA */

	uint32_t num_disc_nodes;	/*in addition to hba_state */

	uint32_t fc_nlp_cnt;	/* outstanding NODELIST requests */
	uint32_t fc_rscn_id_cnt;	/* count of RSCNs payloads in list */
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

#define WORKER_MBOX_TMO                0x100	/* hba: MBOX timeout */
#define WORKER_HB_TMO                  0x200	/* hba: Heart beat timeout */
#define WORKER_FABRIC_BLOCK_TMO        0x400	/* hba: fabric block timout */
#define WORKER_RAMP_DOWN_QUEUE         0x800	/* hba: Decrease Q depth */
#define WORKER_RAMP_UP_QUEUE           0x1000	/* hba: Increase Q depth */

	struct timer_list fc_fdmitmo;
	struct timer_list els_tmofunc;

	int unreg_vpi_cmpl;

	uint8_t load_flag;
#define FC_LOADING		0x1	/* HBA in process of loading drvr */
#define FC_UNLOADING		0x2	/* HBA in process of unloading drvr */
	char  *vname;		        /* Application assigned name */

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

	uint32_t dev_loss_tmo_changed;

	struct fc_vport *fc_vport;

#ifdef CONFIG_LPFC_DEBUG_FS
	struct dentry *debug_disc_trc;
	struct dentry *debug_nodelist;
	struct dentry *vport_debugfs_root;
	struct lpfc_debugfs_trc *disc_trc;
	atomic_t disc_trc_cnt;
#endif
};

struct hbq_s {
	uint16_t entry_count;	  /* Current number of HBQ slots */
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

struct lpfc_hba {
	struct lpfc_sli sli;
	uint32_t sli_rev;		/* SLI2 or SLI3 */
	uint32_t sli3_options;		/* Mask of enabled SLI3 options */
#define LPFC_SLI3_ENABLED	 0x01
#define LPFC_SLI3_HBQ_ENABLED	 0x02
#define LPFC_SLI3_NPIV_ENABLED	 0x04
#define LPFC_SLI3_VPORT_TEARDOWN 0x08
	uint32_t iocb_cmd_size;
	uint32_t iocb_rsp_size;

	enum hba_state link_state;
	uint32_t link_flag;	/* link state flags */
#define LS_LOOPBACK_MODE      0x1	/* NPort is in Loopback mode */
					/* This flag is set while issuing */
					/* INIT_LINK mailbox command */
#define LS_NPIV_FAB_SUPPORTED 0x2	/* Fabric supports NPIV */
#define LS_IGNORE_ERATT       0x3	/* intr handler should ignore ERATT */

	struct lpfc_sli2_slim *slim2p;
	struct lpfc_dmabuf hbqslimp;

	dma_addr_t slim2p_mapping;

	uint16_t pci_cfg_value;

	uint8_t work_found;
#define LPFC_MAX_WORKER_ITERATION  4

	uint8_t fc_linkspeed;	/* Link speed after last READ_LA */

	uint32_t fc_eventTag;	/* event tag for link attention */


	struct timer_list fc_estabtmo;	/* link establishment timer */
	/* These fields used to be binfo */
	uint32_t fc_pref_DID;	/* preferred D_ID */
	uint8_t  fc_pref_ALPA;	/* preferred AL_PA */
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
	uint32_t cfg_topology;
	uint32_t cfg_link_speed;
	uint32_t cfg_cr_delay;
	uint32_t cfg_cr_count;
	uint32_t cfg_multi_ring_support;
	uint32_t cfg_multi_ring_rctl;
	uint32_t cfg_multi_ring_type;
	uint32_t cfg_poll;
	uint32_t cfg_poll_tmo;
	uint32_t cfg_use_msi;
	uint32_t cfg_sg_seg_cnt;
	uint32_t cfg_sg_dma_buf_size;
	uint64_t cfg_soft_wwnn;
	uint64_t cfg_soft_wwpn;
	uint32_t cfg_hba_queue_depth;


	lpfc_vpd_t vpd;		/* vital product data */

	struct pci_dev *pcidev;
	struct list_head      work_list;
	uint32_t              work_ha;      /* Host Attention Bits for WT */
	uint32_t              work_ha_mask; /* HA Bits owned by WT        */
	uint32_t              work_hs;      /* HS stored in case of ERRAT */
	uint32_t              work_status[2]; /* Extra status from SLIM */

	wait_queue_head_t    *work_wait;
	struct task_struct   *worker_thread;

	uint32_t hbq_count;	        /* Count of configured HBQs */
	struct hbq_s hbqs[LPFC_MAX_HBQS]; /* local copy of hbq indicies  */

	unsigned long pci_bar0_map;     /* Physical address for PCI BAR0 */
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

	/*
	 * stat  counters
	 */
	uint64_t fc4InputRequests;
	uint64_t fc4OutputRequests;
	uint64_t fc4ControlRequests;

	struct lpfc_sysfs_mbox sysfs_mbox;

	/* fastpath list. */
	spinlock_t scsi_buf_list_lock;
	struct list_head lpfc_scsi_buf_list;
	uint32_t total_scsi_bufs;
	struct list_head lpfc_iocb_list;
	uint32_t total_iocbq_bufs;
	spinlock_t hbalock;

	/* pci_mem_pools */
	struct pci_pool *lpfc_scsi_dma_buf_pool;
	struct pci_pool *lpfc_mbuf_pool;
	struct pci_pool *lpfc_hbq_pool;
	struct lpfc_dma_pool lpfc_mbuf_safety_pool;

	mempool_t *mbox_mem_pool;
	mempool_t *nlp_mem_pool;

	struct fc_host_statistics link_stats;
	uint8_t using_msi;

	struct list_head port_list;
	struct lpfc_vport *pport;	/* physical lpfc_vport pointer */
	uint16_t max_vpi;		/* Maximum virtual nports */
#define LPFC_MAX_VPI 100		/* Max number of VPI supported */
#define LPFC_MAX_VPORTS (LPFC_MAX_VPI+1)/* Max number of VPorts supported */
	unsigned long *vpi_bmask;	/* vpi allocation table */

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
#ifdef CONFIG_LPFC_DEBUG_FS
	struct dentry *hba_debugfs_root;
	atomic_t debugfs_vport_count;
	struct dentry *debug_hbqinfo;
	struct dentry *debug_dumpslim;
	struct dentry *debug_slow_ring_trc;
	struct lpfc_debugfs_trc *slow_ring_trc;
	atomic_t slow_ring_trc_cnt;
#endif

	/* Fields used for heart beat. */
	unsigned long last_completion_time;
	struct timer_list hb_tmofunc;
	uint8_t hb_outstanding;
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

#define FC_REG_DUMP_EVENT	0x10	/* Register for Dump events */

