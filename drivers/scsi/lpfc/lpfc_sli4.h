/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2009 Emulex.  All rights reserved.                *
 * EMULEX and SLI are trademarks of Emulex.                        *
 * www.emulex.com                                                  *
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

#define LPFC_ACTIVE_MBOX_WAIT_CNT               100
#define LPFC_RELEASE_NOTIFICATION_INTERVAL	32
#define LPFC_GET_QE_REL_INT			32
#define LPFC_RPI_LOW_WATER_MARK			10
/* Number of SGL entries can be posted in a 4KB nonembedded mbox command */
#define LPFC_NEMBED_MBOX_SGL_CNT		254

/* Multi-queue arrangement for fast-path FCP work queues */
#define LPFC_FN_EQN_MAX       8
#define LPFC_SP_EQN_DEF       1
#define LPFC_FP_EQN_DEF       1
#define LPFC_FP_EQN_MIN       1
#define LPFC_FP_EQN_MAX       (LPFC_FN_EQN_MAX - LPFC_SP_EQN_DEF)

#define LPFC_FN_WQN_MAX       32
#define LPFC_SP_WQN_DEF       1
#define LPFC_FP_WQN_DEF       4
#define LPFC_FP_WQN_MIN       1
#define LPFC_FP_WQN_MAX       (LPFC_FN_WQN_MAX - LPFC_SP_WQN_DEF)

/*
 * Provide the default FCF Record attributes used by the driver
 * when nonFIP mode is configured and there is no other default
 * FCF Record attributes.
 */
#define LPFC_FCOE_FCF_DEF_INDEX	0
#define LPFC_FCOE_FCF_GET_FIRST	0xFFFF
#define LPFC_FCOE_FCF_NEXT_NONE	0xFFFF

/* First 3 bytes of default FCF MAC is specified by FC_MAP */
#define LPFC_FCOE_FCF_MAC3	0xFF
#define LPFC_FCOE_FCF_MAC4	0xFF
#define LPFC_FCOE_FCF_MAC5	0xFE
#define LPFC_FCOE_FCF_MAP0	0x0E
#define LPFC_FCOE_FCF_MAP1	0xFC
#define LPFC_FCOE_FCF_MAP2	0x00
#define LPFC_FCOE_MAX_RCV_SIZE	0x5AC
#define LPFC_FCOE_FKA_ADV_PER	0
#define LPFC_FCOE_FIP_PRIORITY	0x80

enum lpfc_sli4_queue_type {
	LPFC_EQ,
	LPFC_GCQ,
	LPFC_MCQ,
	LPFC_WCQ,
	LPFC_RCQ,
	LPFC_MQ,
	LPFC_WQ,
	LPFC_HRQ,
	LPFC_DRQ
};

/* The queue sub-type defines the functional purpose of the queue */
enum lpfc_sli4_queue_subtype {
	LPFC_NONE,
	LPFC_MBOX,
	LPFC_FCP,
	LPFC_ELS,
	LPFC_USOL
};

union sli4_qe {
	void *address;
	struct lpfc_eqe *eqe;
	struct lpfc_cqe *cqe;
	struct lpfc_mcqe *mcqe;
	struct lpfc_wcqe_complete *wcqe_complete;
	struct lpfc_wcqe_release *wcqe_release;
	struct sli4_wcqe_xri_aborted *wcqe_xri_aborted;
	struct lpfc_rcqe_complete *rcqe_complete;
	struct lpfc_mqe *mqe;
	union  lpfc_wqe *wqe;
	struct lpfc_rqe *rqe;
};

struct lpfc_queue {
	struct list_head list;
	enum lpfc_sli4_queue_type type;
	enum lpfc_sli4_queue_subtype subtype;
	struct lpfc_hba *phba;
	struct list_head child_list;
	uint32_t entry_count;	/* Number of entries to support on the queue */
	uint32_t entry_size;	/* Size of each queue entry. */
	uint32_t queue_id;	/* Queue ID assigned by the hardware */
	struct list_head page_list;
	uint32_t page_count;	/* Number of pages allocated for this queue */

	uint32_t host_index;	/* The host's index for putting or getting */
	uint32_t hba_index;	/* The last known hba index for get or put */
	union sli4_qe qe[1];	/* array to index entries (must be last) */
};

struct lpfc_cq_event {
	struct list_head list;
	union {
		struct lpfc_mcqe		mcqe_cmpl;
		struct lpfc_acqe_link		acqe_link;
		struct lpfc_acqe_fcoe		acqe_fcoe;
		struct lpfc_acqe_dcbx		acqe_dcbx;
		struct lpfc_rcqe		rcqe_cmpl;
		struct sli4_wcqe_xri_aborted	wcqe_axri;
	} cqe;
};

struct lpfc_sli4_link {
	uint8_t speed;
	uint8_t duplex;
	uint8_t status;
	uint8_t physical;
	uint8_t fault;
};

struct lpfc_fcf {
	uint8_t	 fabric_name[8];
	uint8_t  mac_addr[6];
	uint16_t fcf_indx;
	uint16_t fcfi;
	uint32_t fcf_flag;
#define FCF_AVAILABLE	0x01 /* FCF available for discovery */
#define FCF_REGISTERED	0x02 /* FCF registered with FW */
#define FCF_DISCOVERED	0x04 /* FCF discovery started  */
#define FCF_BOOT_ENABLE 0x08 /* Boot bios use this FCF */
#define FCF_IN_USE	0x10 /* Atleast one discovery completed */
#define FCF_VALID_VLAN	0x20 /* Use the vlan id specified */
	uint32_t priority;
	uint32_t addr_mode;
	uint16_t vlan_id;
};

#define LPFC_REGION23_SIGNATURE "RG23"
#define LPFC_REGION23_VERSION	1
#define LPFC_REGION23_LAST_REC  0xff
struct lpfc_fip_param_hdr {
	uint8_t type;
#define FCOE_PARAM_TYPE		0xA0
	uint8_t length;
#define FCOE_PARAM_LENGTH	2
	uint8_t parm_version;
#define FIPP_VERSION		0x01
	uint8_t parm_flags;
#define	lpfc_fip_param_hdr_fipp_mode_SHIFT	6
#define	lpfc_fip_param_hdr_fipp_mode_MASK	0x3
#define lpfc_fip_param_hdr_fipp_mode_WORD	parm_flags
#define	FIPP_MODE_ON				0x2
#define	FIPP_MODE_OFF				0x0
#define FIPP_VLAN_VALID				0x1
};

struct lpfc_fcoe_params {
	uint8_t fc_map[3];
	uint8_t reserved1;
	uint16_t vlan_tag;
	uint8_t reserved[2];
};

struct lpfc_fcf_conn_hdr {
	uint8_t type;
#define FCOE_CONN_TBL_TYPE		0xA1
	uint8_t length;   /* words */
	uint8_t reserved[2];
};

struct lpfc_fcf_conn_rec {
	uint16_t flags;
#define	FCFCNCT_VALID		0x0001
#define	FCFCNCT_BOOT		0x0002
#define	FCFCNCT_PRIMARY		0x0004   /* if not set, Secondary */
#define	FCFCNCT_FBNM_VALID	0x0008
#define	FCFCNCT_SWNM_VALID	0x0010
#define	FCFCNCT_VLAN_VALID	0x0020
#define	FCFCNCT_AM_VALID	0x0040
#define	FCFCNCT_AM_PREFERRED	0x0080   /* if not set, AM Required */
#define	FCFCNCT_AM_SPMA		0x0100	 /* if not set, FPMA */

	uint16_t vlan_tag;
	uint8_t fabric_name[8];
	uint8_t switch_name[8];
};

struct lpfc_fcf_conn_entry {
	struct list_head list;
	struct lpfc_fcf_conn_rec conn_rec;
};

/*
 * Define the host's bootstrap mailbox.  This structure contains
 * the member attributes needed to create, use, and destroy the
 * bootstrap mailbox region.
 *
 * The macro definitions for the bmbx data structure are defined
 * in lpfc_hw4.h with the register definition.
 */
struct lpfc_bmbx {
	struct lpfc_dmabuf *dmabuf;
	struct dma_address dma_address;
	void *avirt;
	dma_addr_t aphys;
	uint32_t bmbx_size;
};

#define LPFC_EQE_SIZE LPFC_EQE_SIZE_4

#define LPFC_EQE_SIZE_4B 	4
#define LPFC_EQE_SIZE_16B	16
#define LPFC_CQE_SIZE		16
#define LPFC_WQE_SIZE		64
#define LPFC_MQE_SIZE		256
#define LPFC_RQE_SIZE		8

#define LPFC_EQE_DEF_COUNT	1024
#define LPFC_CQE_DEF_COUNT      256
#define LPFC_WQE_DEF_COUNT      256
#define LPFC_MQE_DEF_COUNT      16
#define LPFC_RQE_DEF_COUNT	512

#define LPFC_QUEUE_NOARM	false
#define LPFC_QUEUE_REARM	true


/*
 * SLI4 CT field defines
 */
#define SLI4_CT_RPI 0
#define SLI4_CT_VPI 1
#define SLI4_CT_VFI 2
#define SLI4_CT_FCFI 3

#define LPFC_SLI4_MAX_SEGMENT_SIZE 0x10000

/*
 * SLI4 specific data structures
 */
struct lpfc_max_cfg_param {
	uint16_t max_xri;
	uint16_t xri_base;
	uint16_t xri_used;
	uint16_t max_rpi;
	uint16_t rpi_base;
	uint16_t rpi_used;
	uint16_t max_vpi;
	uint16_t vpi_base;
	uint16_t vpi_used;
	uint16_t max_vfi;
	uint16_t vfi_base;
	uint16_t vfi_used;
	uint16_t max_fcfi;
	uint16_t fcfi_base;
	uint16_t fcfi_used;
	uint16_t max_eq;
	uint16_t max_rq;
	uint16_t max_cq;
	uint16_t max_wq;
};

struct lpfc_hba;
/* SLI4 HBA multi-fcp queue handler struct */
struct lpfc_fcp_eq_hdl {
	uint32_t idx;
	struct lpfc_hba *phba;
};

/* SLI4 HBA data structure entries */
struct lpfc_sli4_hba {
	void __iomem *conf_regs_memmap_p; /* Kernel memory mapped address for
					     PCI BAR0, config space registers */
	void __iomem *ctrl_regs_memmap_p; /* Kernel memory mapped address for
					     PCI BAR1, control registers */
	void __iomem *drbl_regs_memmap_p; /* Kernel memory mapped address for
					     PCI BAR2, doorbell registers */
	/* BAR0 PCI config space register memory map */
	void __iomem *UERRLOregaddr; /* Address to UERR_STATUS_LO register */
	void __iomem *UERRHIregaddr; /* Address to UERR_STATUS_HI register */
	void __iomem *ONLINE0regaddr; /* Address to components of internal UE */
	void __iomem *ONLINE1regaddr; /* Address to components of internal UE */
#define LPFC_ONLINE_NERR	0xFFFFFFFF
	void __iomem *SCRATCHPADregaddr; /* Address to scratchpad register */
	/* BAR1 FCoE function CSR register memory map */
	void __iomem *STAregaddr;    /* Address to HST_STATE register */
	void __iomem *ISRregaddr;    /* Address to HST_ISR register */
	void __iomem *IMRregaddr;    /* Address to HST_IMR register */
	void __iomem *ISCRregaddr;   /* Address to HST_ISCR register */
	/* BAR2 VF-0 doorbell register memory map */
	void __iomem *RQDBregaddr;   /* Address to RQ_DOORBELL register */
	void __iomem *WQDBregaddr;   /* Address to WQ_DOORBELL register */
	void __iomem *EQCQDBregaddr; /* Address to EQCQ_DOORBELL register */
	void __iomem *MQDBregaddr;   /* Address to MQ_DOORBELL register */
	void __iomem *BMBXregaddr;   /* Address to BootStrap MBX register */

	struct msix_entry *msix_entries;
	uint32_t cfg_eqn;
	struct lpfc_fcp_eq_hdl *fcp_eq_hdl; /* FCP per-WQ handle */
	/* Pointers to the constructed SLI4 queues */
	struct lpfc_queue **fp_eq; /* Fast-path event queue */
	struct lpfc_queue *sp_eq;  /* Slow-path event queue */
	struct lpfc_queue **fcp_wq;/* Fast-path FCP work queue */
	struct lpfc_queue *mbx_wq; /* Slow-path MBOX work queue */
	struct lpfc_queue *els_wq; /* Slow-path ELS work queue */
	struct lpfc_queue *hdr_rq; /* Slow-path Header Receive queue */
	struct lpfc_queue *dat_rq; /* Slow-path Data Receive queue */
	struct lpfc_queue **fcp_cq;/* Fast-path FCP compl queue */
	struct lpfc_queue *mbx_cq; /* Slow-path mailbox complete queue */
	struct lpfc_queue *els_cq; /* Slow-path ELS response complete queue */
	struct lpfc_queue *rxq_cq; /* Slow-path unsolicited complete queue */

	/* Setup information for various queue parameters */
	int eq_esize;
	int eq_ecount;
	int cq_esize;
	int cq_ecount;
	int wq_esize;
	int wq_ecount;
	int mq_esize;
	int mq_ecount;
	int rq_esize;
	int rq_ecount;
#define LPFC_SP_EQ_MAX_INTR_SEC         10000
#define LPFC_FP_EQ_MAX_INTR_SEC         10000

	uint32_t intr_enable;
	struct lpfc_bmbx bmbx;
	struct lpfc_max_cfg_param max_cfg_param;
	uint16_t next_xri; /* last_xri - max_cfg_param.xri_base = used */
	uint16_t next_rpi;
	uint16_t scsi_xri_max;
	uint16_t scsi_xri_cnt;
	struct list_head lpfc_free_sgl_list;
	struct list_head lpfc_sgl_list;
	struct lpfc_sglq **lpfc_els_sgl_array;
	struct list_head lpfc_abts_els_sgl_list;
	struct lpfc_scsi_buf **lpfc_scsi_psb_array;
	struct list_head lpfc_abts_scsi_buf_list;
	uint32_t total_sglq_bufs;
	struct lpfc_sglq **lpfc_sglq_active_list;
	struct list_head lpfc_rpi_hdr_list;
	unsigned long *rpi_bmask;
	uint16_t rpi_count;
	struct lpfc_sli4_flags sli4_flags;
	struct list_head sp_rspiocb_work_queue;
	struct list_head sp_cqe_event_pool;
	struct list_head sp_asynce_work_queue;
	struct list_head sp_fcp_xri_aborted_work_queue;
	struct list_head sp_els_xri_aborted_work_queue;
	struct list_head sp_unsol_work_queue;
	struct lpfc_sli4_link link_state;
	spinlock_t abts_scsi_buf_list_lock; /* list of aborted SCSI IOs */
	spinlock_t abts_sgl_list_lock; /* list of aborted els IOs */
};

enum lpfc_sge_type {
	GEN_BUFF_TYPE,
	SCSI_BUFF_TYPE
};

struct lpfc_sglq {
	/* lpfc_sglqs are used in double linked lists */
	struct list_head list;
	struct list_head clist;
	enum lpfc_sge_type buff_type; /* is this a scsi sgl */
	uint16_t iotag;         /* pre-assigned IO tag */
	uint16_t sli4_xritag;   /* pre-assigned XRI, (OXID) tag. */
	struct sli4_sge *sgl;	/* pre-assigned SGL */
	void *virt;		/* virtual address. */
	dma_addr_t phys;	/* physical address */
};

struct lpfc_rpi_hdr {
	struct list_head list;
	uint32_t len;
	struct lpfc_dmabuf *dmabuf;
	uint32_t page_count;
	uint32_t start_rpi;
};

/*
 * SLI4 specific function prototypes
 */
int lpfc_pci_function_reset(struct lpfc_hba *);
int lpfc_sli4_hba_setup(struct lpfc_hba *);
int lpfc_sli4_hba_down(struct lpfc_hba *);
int lpfc_sli4_config(struct lpfc_hba *, struct lpfcMboxq *, uint8_t,
		     uint8_t, uint32_t, bool);
void lpfc_sli4_mbox_cmd_free(struct lpfc_hba *, struct lpfcMboxq *);
void lpfc_sli4_mbx_sge_set(struct lpfcMboxq *, uint32_t, dma_addr_t, uint32_t);
void lpfc_sli4_mbx_sge_get(struct lpfcMboxq *, uint32_t,
			   struct lpfc_mbx_sge *);

void lpfc_sli4_hba_reset(struct lpfc_hba *);
struct lpfc_queue *lpfc_sli4_queue_alloc(struct lpfc_hba *, uint32_t,
			uint32_t);
void lpfc_sli4_queue_free(struct lpfc_queue *);
uint32_t lpfc_eq_create(struct lpfc_hba *, struct lpfc_queue *, uint16_t);
uint32_t lpfc_cq_create(struct lpfc_hba *, struct lpfc_queue *,
			struct lpfc_queue *, uint32_t, uint32_t);
uint32_t lpfc_mq_create(struct lpfc_hba *, struct lpfc_queue *,
			struct lpfc_queue *, uint32_t);
uint32_t lpfc_wq_create(struct lpfc_hba *, struct lpfc_queue *,
			struct lpfc_queue *, uint32_t);
uint32_t lpfc_rq_create(struct lpfc_hba *, struct lpfc_queue *,
			struct lpfc_queue *, struct lpfc_queue *, uint32_t);
uint32_t lpfc_eq_destroy(struct lpfc_hba *, struct lpfc_queue *);
uint32_t lpfc_cq_destroy(struct lpfc_hba *, struct lpfc_queue *);
uint32_t lpfc_mq_destroy(struct lpfc_hba *, struct lpfc_queue *);
uint32_t lpfc_wq_destroy(struct lpfc_hba *, struct lpfc_queue *);
uint32_t lpfc_rq_destroy(struct lpfc_hba *, struct lpfc_queue *,
			 struct lpfc_queue *);
int lpfc_sli4_queue_setup(struct lpfc_hba *);
void lpfc_sli4_queue_unset(struct lpfc_hba *);
int lpfc_sli4_post_sgl(struct lpfc_hba *, dma_addr_t, dma_addr_t, uint16_t);
int lpfc_sli4_repost_scsi_sgl_list(struct lpfc_hba *);
int lpfc_sli4_remove_all_sgl_pages(struct lpfc_hba *);
uint16_t lpfc_sli4_next_xritag(struct lpfc_hba *);
int lpfc_sli4_post_async_mbox(struct lpfc_hba *);
int lpfc_sli4_post_sgl_list(struct lpfc_hba *phba);
int lpfc_sli4_post_scsi_sgl_block(struct lpfc_hba *, struct list_head *, int);
struct lpfc_cq_event *__lpfc_sli4_cq_event_alloc(struct lpfc_hba *);
struct lpfc_cq_event *lpfc_sli4_cq_event_alloc(struct lpfc_hba *);
void __lpfc_sli4_cq_event_release(struct lpfc_hba *, struct lpfc_cq_event *);
void lpfc_sli4_cq_event_release(struct lpfc_hba *, struct lpfc_cq_event *);
int lpfc_sli4_init_rpi_hdrs(struct lpfc_hba *);
int lpfc_sli4_post_rpi_hdr(struct lpfc_hba *, struct lpfc_rpi_hdr *);
int lpfc_sli4_post_all_rpi_hdrs(struct lpfc_hba *);
struct lpfc_rpi_hdr *lpfc_sli4_create_rpi_hdr(struct lpfc_hba *);
void lpfc_sli4_remove_rpi_hdrs(struct lpfc_hba *);
int lpfc_sli4_alloc_rpi(struct lpfc_hba *);
void lpfc_sli4_free_rpi(struct lpfc_hba *, int);
void lpfc_sli4_remove_rpis(struct lpfc_hba *);
void lpfc_sli4_async_event_proc(struct lpfc_hba *);
int lpfc_sli4_resume_rpi(struct lpfc_nodelist *);
void lpfc_sli4_fcp_xri_abort_event_proc(struct lpfc_hba *);
void lpfc_sli4_els_xri_abort_event_proc(struct lpfc_hba *);
void lpfc_sli4_fcp_xri_aborted(struct lpfc_hba *,
			       struct sli4_wcqe_xri_aborted *);
void lpfc_sli4_els_xri_aborted(struct lpfc_hba *,
			       struct sli4_wcqe_xri_aborted *);
int lpfc_sli4_brdreset(struct lpfc_hba *);
int lpfc_sli4_add_fcf_record(struct lpfc_hba *, struct fcf_record *);
void lpfc_sli_remove_dflt_fcf(struct lpfc_hba *);
int lpfc_sli4_get_els_iocb_cnt(struct lpfc_hba *);
int lpfc_sli4_init_vpi(struct lpfc_hba *, uint16_t);
uint32_t lpfc_sli4_cq_release(struct lpfc_queue *, bool);
uint32_t lpfc_sli4_eq_release(struct lpfc_queue *, bool);
void lpfc_sli4_fcfi_unreg(struct lpfc_hba *, uint16_t);
int lpfc_sli4_read_fcf_record(struct lpfc_hba *, uint16_t);
void lpfc_mbx_cmpl_read_fcf_record(struct lpfc_hba *, LPFC_MBOXQ_t *);
int lpfc_sli4_post_status_check(struct lpfc_hba *);
uint8_t lpfc_sli4_mbox_opcode_get(struct lpfc_hba *, struct lpfcMboxq *);

