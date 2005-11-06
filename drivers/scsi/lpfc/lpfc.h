/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2004-2005 Emulex.  All rights reserved.           *
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

struct lpfc_sli2_slim;

#define LPFC_MAX_TARGET         256	/* max targets supported */
#define LPFC_MAX_DISC_THREADS	64	/* max outstanding discovery els req */
#define LPFC_MAX_NS_RETRY       3	/* max NameServer retries */

#define LPFC_DFT_HBA_Q_DEPTH	2048	/* max cmds per hba */
#define LPFC_LC_HBA_Q_DEPTH	1024	/* max cmds per low cost hba */
#define LPFC_LP101_HBA_Q_DEPTH	128	/* max cmds per low cost hba */

#define LPFC_CMD_PER_LUN	30	/* max outstanding cmds per lun */
#define LPFC_SG_SEG_CNT		64	/* sg element count per scsi cmnd */
#define LPFC_IOCB_LIST_CNT	2250	/* list of IOCBs for fast-path usage. */

/* Define macros for 64 bit support */
#define putPaddrLow(addr)    ((uint32_t) (0xffffffff & (u64)(addr)))
#define putPaddrHigh(addr)   ((uint32_t) (0xffffffff & (((u64)(addr))>>32)))
#define getPaddr(high, low)  ((dma_addr_t)( \
			     (( (u64)(high)<<16 ) << 16)|( (u64)(low))))
/* Provide maximum configuration definitions. */
#define LPFC_DRVR_TIMEOUT	16	/* driver iocb timeout value in sec */
#define MAX_FCP_TARGET		256	/* max num of FCP targets supported */
#define FC_MAX_ADPTMSG		64

#define MAX_HBAEVT	32

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
	uint32_t elsRcvRRQ;
	uint32_t elsXmitFLOGI;
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

struct lpfc_hba {
	struct list_head hba_list;	/* List of hbas/ports */
	struct lpfc_sli sli;
	struct lpfc_sli2_slim *slim2p;
	dma_addr_t slim2p_mapping;
	uint16_t pci_cfg_value;

	uint32_t hba_state;

#define LPFC_INIT_START           1	/* Initial state after board reset */
#define LPFC_INIT_MBX_CMDS        2	/* Initialize HBA with mbox commands */
#define LPFC_LINK_DOWN            3	/* HBA initialized, link is down */
#define LPFC_LINK_UP              4	/* Link is up  - issue READ_LA */
#define LPFC_LOCAL_CFG_LINK       5	/* local NPORT Id configured */
#define LPFC_FLOGI                6	/* FLOGI sent to Fabric */
#define LPFC_FABRIC_CFG_LINK      7	/* Fabric assigned NPORT Id
					   configured */
#define LPFC_NS_REG               8	/* Register with NameServer */
#define LPFC_NS_QRY               9	/* Query NameServer for NPort ID list */
#define LPFC_BUILD_DISC_LIST      10	/* Build ADISC and PLOGI lists for
					 * device authentication / discovery */
#define LPFC_DISC_AUTH            11	/* Processing ADISC list */
#define LPFC_CLEAR_LA             12	/* authentication cmplt - issue
					   CLEAR_LA */
#define LPFC_HBA_READY            32
#define LPFC_HBA_ERROR            0xff

	uint8_t fc_linkspeed;	/* Link speed after last READ_LA */

	uint32_t fc_eventTag;	/* event tag for link attention */
	uint32_t fc_prli_sent;	/* cntr for outstanding PRLIs */

	uint32_t num_disc_nodes;	/*in addition to hba_state */

	struct timer_list fc_estabtmo;	/* link establishment timer */
	struct timer_list fc_disctmo;	/* Discovery rescue timer */
	struct timer_list fc_fdmitmo;	/* fdmi timer */
	/* These fields used to be binfo */
	struct lpfc_name fc_nodename;	/* fc nodename */
	struct lpfc_name fc_portname;	/* fc portname */
	uint32_t fc_pref_DID;	/* preferred D_ID */
	uint8_t fc_pref_ALPA;	/* preferred AL_PA */
	uint32_t fc_edtov;	/* E_D_TOV timer value */
	uint32_t fc_arbtov;	/* ARB_TOV timer value */
	uint32_t fc_ratov;	/* R_A_TOV timer value */
	uint32_t fc_rttov;	/* R_T_TOV timer value */
	uint32_t fc_altov;	/* AL_TOV timer value */
	uint32_t fc_crtov;	/* C_R_TOV timer value */
	uint32_t fc_citov;	/* C_I_TOV timer value */
	uint32_t fc_myDID;	/* fibre channel S_ID */
	uint32_t fc_prevDID;	/* previous fibre channel S_ID */

	struct serv_parm fc_sparam;	/* buffer for our service parameters */
	struct serv_parm fc_fabparam;	/* fabric service parameters buffer */
	uint8_t alpa_map[128];	/* AL_PA map from READ_LA */

	uint8_t fc_ns_retry;	/* retries for fabric nameserver */
	uint32_t fc_nlp_cnt;	/* outstanding NODELIST requests */
	uint32_t fc_rscn_id_cnt;	/* count of RSCNs payloads in list */
	struct lpfc_dmabuf *fc_rscn_id_list[FC_MAX_HOLD_RSCN];
	uint32_t lmt;
	uint32_t fc_flag;	/* FC flags */
#define FC_PT2PT                0x1	/* pt2pt with no fabric */
#define FC_PT2PT_PLOGI          0x2	/* pt2pt initiate PLOGI */
#define FC_DISC_TMO             0x4	/* Discovery timer running */
#define FC_PUBLIC_LOOP          0x8	/* Public loop */
#define FC_LBIT                 0x10	/* LOGIN bit in loopinit set */
#define FC_RSCN_MODE            0x20	/* RSCN cmd rcv'ed */
#define FC_NLP_MORE             0x40	/* More node to process in node tbl */
#define FC_OFFLINE_MODE         0x80	/* Interface is offline for diag */
#define FC_FABRIC               0x100	/* We are fabric attached */
#define FC_ESTABLISH_LINK       0x200	/* Reestablish Link */
#define FC_RSCN_DISCOVERY       0x400	/* Authenticate all devices after RSCN*/
#define FC_LOADING		0x1000	/* HBA in process of loading drvr */
#define FC_UNLOADING		0x2000	/* HBA in process of unloading drvr */
#define FC_SCSI_SCAN_TMO        0x4000	/* scsi scan timer running */
#define FC_ABORT_DISCOVERY      0x8000	/* we want to abort discovery */
#define FC_NDISC_ACTIVE         0x10000	/* NPort discovery active */

	uint32_t fc_topology;	/* link topology, from LINK INIT */

	struct lpfc_stats fc_stat;

	/* These are the head/tail pointers for the bind, plogi, adisc, unmap,
	 *  and map lists.  Their counters are immediately following.
	 */
	struct list_head fc_plogi_list;
	struct list_head fc_adisc_list;
	struct list_head fc_reglogin_list;
	struct list_head fc_prli_list;
	struct list_head fc_nlpunmap_list;
	struct list_head fc_nlpmap_list;
	struct list_head fc_npr_list;
	struct list_head fc_unused_list;

	/* Keep counters for the number of entries in each list. */
	uint16_t fc_plogi_cnt;
	uint16_t fc_adisc_cnt;
	uint16_t fc_reglogin_cnt;
	uint16_t fc_prli_cnt;
	uint16_t fc_unmap_cnt;
	uint16_t fc_map_cnt;
	uint16_t fc_npr_cnt;
	uint16_t fc_unused_cnt;
	struct lpfc_nodelist fc_fcpnodev; /* nodelist entry for no device */
	uint32_t nport_event_cnt;	/* timestamp for nlplist entry */

#define LPFC_RPI_HASH_SIZE     64
#define LPFC_RPI_HASH_FUNC(x)  ((x) & (0x3f))
	/* ptr to active D_ID / RPIs */
	struct lpfc_nodelist *fc_nlplookup[LPFC_RPI_HASH_SIZE];
	uint32_t wwnn[2];
	uint32_t RandomData[7];

	uint32_t cfg_log_verbose;
	uint32_t cfg_lun_queue_depth;
	uint32_t cfg_nodev_tmo;
	uint32_t cfg_hba_queue_depth;
	uint32_t cfg_fcp_class;
	uint32_t cfg_use_adisc;
	uint32_t cfg_ack0;
	uint32_t cfg_topology;
	uint32_t cfg_scan_down;
	uint32_t cfg_link_speed;
	uint32_t cfg_cr_delay;
	uint32_t cfg_cr_count;
	uint32_t cfg_fdmi_on;
	uint32_t cfg_fcp_bind_method;
	uint32_t cfg_discovery_threads;
	uint32_t cfg_max_luns;
	uint32_t cfg_sg_seg_cnt;
	uint32_t cfg_sg_dma_buf_size;

	lpfc_vpd_t vpd;		/* vital product data */

	struct Scsi_Host *host;
	struct pci_dev *pcidev;
	struct list_head      work_list;
	uint32_t              work_ha;      /* Host Attention Bits for WT */
	uint32_t              work_ha_mask; /* HA Bits owned by WT        */
	uint32_t              work_hs;      /* HS stored in case of ERRAT */
	uint32_t              work_status[2]; /* Extra status from SLIM */
	uint32_t              work_hba_events; /* Timeout to be handled  */
#define WORKER_DISC_TMO                0x1	/* Discovery timeout */
#define WORKER_ELS_TMO                 0x2	/* ELS timeout */
#define WORKER_MBOX_TMO                0x4	/* MBOX timeout */
#define WORKER_FDMI_TMO                0x8	/* FDMI timeout */

	wait_queue_head_t    *work_wait;
	struct task_struct   *worker_thread;

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

	struct timer_list els_tmofunc;
	/*
	 * stat  counters
	 */
	uint64_t fc4InputRequests;
	uint64_t fc4OutputRequests;
	uint64_t fc4ControlRequests;

	struct lpfc_sysfs_mbox sysfs_mbox;

	/* fastpath list. */
	struct list_head lpfc_scsi_buf_list;
	uint32_t total_scsi_bufs;
	struct list_head lpfc_iocb_list;
	uint32_t total_iocbq_bufs;

	/* pci_mem_pools */
	struct pci_pool *lpfc_scsi_dma_buf_pool;
	struct pci_pool *lpfc_mbuf_pool;
	struct lpfc_dma_pool lpfc_mbuf_safety_pool;

	mempool_t *mbox_mem_pool;
	mempool_t *nlp_mem_pool;
	struct list_head freebufList;
	struct list_head ctrspbuflist;
	struct list_head rnidrspbuflist;

	struct fc_host_statistics link_stats;
};


struct rnidrsp {
	void *buf;
	uint32_t uniqueid;
	struct list_head list;
	uint32_t data;
};
