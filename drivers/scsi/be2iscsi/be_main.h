/**
 * Copyright (C) 2005 - 2009 ServerEngines
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.  The full GNU General
 * Public License is included in this distribution in the file called COPYING.
 *
 * Written by: Jayamohan Kallickal (jayamohank@serverengines.com)
 *
 * Contact Information:
 * linux-drivers@serverengines.com
 *
 * ServerEngines
 * 209 N. Fair Oaks Ave
 * Sunnyvale, CA 94085
 *
 */

#ifndef _BEISCSI_MAIN_
#define _BEISCSI_MAIN_

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/in.h>
#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/iscsi_proto.h>
#include <scsi/libiscsi.h>
#include <scsi/scsi_transport_iscsi.h>

#include "be.h"
#define DRV_NAME		"be2iscsi"
#define BUILD_STR		"2.0.527.0"
#define BE_NAME			"ServerEngines BladeEngine2" \
				"Linux iSCSI Driver version" BUILD_STR
#define DRV_DESC		BE_NAME " " "Driver"

#define BE_VENDOR_ID 		0x19A2
#define BE_DEVICE_ID1		0x212
#define OC_DEVICE_ID1		0x702
#define OC_DEVICE_ID2		0x703
#define OC_DEVICE_ID3		0x712
#define OC_DEVICE_ID4		0x222

#define BE2_MAX_SESSIONS	64
#define BE2_CMDS_PER_CXN	128
#define BE2_LOGOUTS		BE2_MAX_SESSIONS
#define BE2_TMFS		16
#define BE2_NOPOUT_REQ		16
#define BE2_ASYNCPDUS		BE2_MAX_SESSIONS
#define BE2_MAX_ICDS		2048
#define BE2_SGE			32
#define BE2_DEFPDU_HDR_SZ	64
#define BE2_DEFPDU_DATA_SZ	8192
#define BE2_IO_DEPTH \
	(BE2_MAX_ICDS / 2 - (BE2_LOGOUTS + BE2_TMFS + BE2_NOPOUT_REQ))

#define MAX_CPUS		31
#define BEISCSI_SGLIST_ELEMENTS	BE2_SGE

#define BEISCSI_MAX_CMNDS	1024	/* Max IO's per Ctrlr sht->can_queue */
#define BEISCSI_CMD_PER_LUN	128	/* scsi_host->cmd_per_lun */
#define BEISCSI_MAX_SECTORS	2048	/* scsi_host->max_sectors */

#define BEISCSI_MAX_CMD_LEN	16	/* scsi_host->max_cmd_len */
#define BEISCSI_NUM_MAX_LUN	256	/* scsi_host->max_lun */
#define BEISCSI_NUM_DEVICES_SUPPORTED	0x01
#define BEISCSI_MAX_FRAGS_INIT	192
#define BE_NUM_MSIX_ENTRIES 	1
#define MPU_EP_SEMAPHORE 	0xac

#define BE_SENSE_INFO_SIZE		258
#define BE_ISCSI_PDU_HEADER_SIZE	64
#define BE_MIN_MEM_SIZE			16384
#define MAX_CMD_SZ			65536
#define IIOC_SCSI_DATA                  0x05	/* Write Operation */

#define DBG_LVL				0x00000001
#define DBG_LVL_1			0x00000001
#define DBG_LVL_2			0x00000002
#define DBG_LVL_3			0x00000004
#define DBG_LVL_4			0x00000008
#define DBG_LVL_5			0x00000010
#define DBG_LVL_6			0x00000020
#define DBG_LVL_7			0x00000040
#define DBG_LVL_8			0x00000080

#define SE_DEBUG(debug_mask, fmt, args...)		\
do {							\
	if (debug_mask & DBG_LVL) {			\
		printk(KERN_ERR "(%s():%d):", __func__, __LINE__);\
		printk(fmt, ##args);			\
	}						\
} while (0);

#define BE_ADAPTER_UP		0x00000000
#define BE_ADAPTER_LINK_DOWN	0x00000001
/**
 * hardware needs the async PDU buffers to be posted in multiples of 8
 * So have atleast 8 of them by default
 */

#define HWI_GET_ASYNC_PDU_CTX(phwi)	(phwi->phwi_ctxt->pasync_ctx)

/********* Memory BAR register ************/
#define PCICFG_MEMBAR_CTRL_INT_CTRL_OFFSET 	0xfc
/**
 * Host Interrupt Enable, if set interrupts are enabled although "PCI Interrupt
 * Disable" may still globally block interrupts in addition to individual
 * interrupt masks; a mechanism for the device driver to block all interrupts
 * atomically without having to arbitrate for the PCI Interrupt Disable bit
 * with the OS.
 */
#define MEMBAR_CTRL_INT_CTRL_HOSTINTR_MASK	(1 << 29)	/* bit 29 */

/********* ISR0 Register offset **********/
#define CEV_ISR0_OFFSET 			0xC18
#define CEV_ISR_SIZE				4

/**
 * Macros for reading/writing a protection domain or CSR registers
 * in BladeEngine.
 */

#define DB_TXULP0_OFFSET 0x40
#define DB_RXULP0_OFFSET 0xA0
/********* Event Q door bell *************/
#define DB_EQ_OFFSET			DB_CQ_OFFSET
#define DB_EQ_RING_ID_MASK		0x1FF	/* bits 0 - 8 */
/* Clear the interrupt for this eq */
#define DB_EQ_CLR_SHIFT			(9)	/* bit 9 */
/* Must be 1 */
#define DB_EQ_EVNT_SHIFT		(10)	/* bit 10 */
/* Number of event entries processed */
#define DB_EQ_NUM_POPPED_SHIFT		(16)	/* bits 16 - 28 */
/* Rearm bit */
#define DB_EQ_REARM_SHIFT		(29)	/* bit 29 */

/********* Compl Q door bell *************/
#define DB_CQ_OFFSET 			0x120
#define DB_CQ_RING_ID_MASK		0x3FF	/* bits 0 - 9 */
/* Number of event entries processed */
#define DB_CQ_NUM_POPPED_SHIFT		(16) 	/* bits 16 - 28 */
/* Rearm bit */
#define DB_CQ_REARM_SHIFT		(29) 	/* bit 29 */

#define GET_HWI_CONTROLLER_WS(pc)	(pc->phwi_ctrlr)
#define HWI_GET_DEF_BUFQ_ID(pc) (((struct hwi_controller *)\
		(GET_HWI_CONTROLLER_WS(pc)))->default_pdu_data.id)
#define HWI_GET_DEF_HDRQ_ID(pc) (((struct hwi_controller *)\
		(GET_HWI_CONTROLLER_WS(pc)))->default_pdu_hdr.id)

#define PAGES_REQUIRED(x) \
	((x < PAGE_SIZE) ? 1 :  ((x + PAGE_SIZE - 1) / PAGE_SIZE))

enum be_mem_enum {
	HWI_MEM_ADDN_CONTEXT,
	HWI_MEM_WRB,
	HWI_MEM_WRBH,
	HWI_MEM_SGLH,
	HWI_MEM_SGE,
	HWI_MEM_ASYNC_HEADER_BUF, 	/* 5 */
	HWI_MEM_ASYNC_DATA_BUF,
	HWI_MEM_ASYNC_HEADER_RING,
	HWI_MEM_ASYNC_DATA_RING,
	HWI_MEM_ASYNC_HEADER_HANDLE,
	HWI_MEM_ASYNC_DATA_HANDLE, 	/* 10 */
	HWI_MEM_ASYNC_PDU_CONTEXT,
	ISCSI_MEM_GLOBAL_HEADER,
	SE_MEM_MAX
};

struct be_bus_address32 {
	unsigned int address_lo;
	unsigned int address_hi;
};

struct be_bus_address64 {
	unsigned long long address;
};

struct be_bus_address {
	union {
		struct be_bus_address32 a32;
		struct be_bus_address64 a64;
	} u;
};

struct mem_array {
	struct be_bus_address bus_address;	/* Bus address of location */
	void *virtual_address;		/* virtual address to the location */
	unsigned int size;		/* Size required by memory block */
};

struct be_mem_descriptor {
	unsigned int index;	/* Index of this memory parameter */
	unsigned int category;	/* type indicates cached/non-cached */
	unsigned int num_elements;	/* number of elements in this
					 * descriptor
					 */
	unsigned int alignment_mask;	/* Alignment mask for this block */
	unsigned int size_in_bytes;	/* Size required by memory block */
	struct mem_array *mem_array;
};

struct sgl_handle {
	unsigned int sgl_index;
	unsigned int type;
	unsigned int cid;
	struct iscsi_task *task;
	struct iscsi_sge *pfrag;
};

struct hba_parameters {
	unsigned int ios_per_ctrl;
	unsigned int cxns_per_ctrl;
	unsigned int asyncpdus_per_ctrl;
	unsigned int icds_per_ctrl;
	unsigned int num_sge_per_io;
	unsigned int defpdu_hdr_sz;
	unsigned int defpdu_data_sz;
	unsigned int num_cq_entries;
	unsigned int num_eq_entries;
	unsigned int wrbs_per_cxn;
	unsigned int crashmode;
	unsigned int hba_num;

	unsigned int mgmt_ws_sz;
	unsigned int hwi_ws_sz;

	unsigned int eto;
	unsigned int ldto;

	unsigned int dbg_flags;
	unsigned int num_cxn;

	unsigned int eq_timer;
	/**
	 * These are calculated from other params. They're here
	 * for debug purposes
	 */
	unsigned int num_mcc_pages;
	unsigned int num_mcc_cq_pages;
	unsigned int num_cq_pages;
	unsigned int num_eq_pages;

	unsigned int num_async_pdu_buf_pages;
	unsigned int num_async_pdu_buf_sgl_pages;
	unsigned int num_async_pdu_buf_cq_pages;

	unsigned int num_async_pdu_hdr_pages;
	unsigned int num_async_pdu_hdr_sgl_pages;
	unsigned int num_async_pdu_hdr_cq_pages;

	unsigned int num_sge;
};

struct beiscsi_hba {
	struct hba_parameters params;
	struct hwi_controller *phwi_ctrlr;
	unsigned int mem_req[SE_MEM_MAX];
	/* PCI BAR mapped addresses */
	u8 __iomem *csr_va;	/* CSR */
	u8 __iomem *db_va;	/* Door  Bell  */
	u8 __iomem *pci_va;	/* PCI Config */
	struct be_bus_address csr_pa;	/* CSR */
	struct be_bus_address db_pa;	/* CSR */
	struct be_bus_address pci_pa;	/* CSR */
	/* PCI representation of our HBA */
	struct pci_dev *pcidev;
	unsigned int state;
	unsigned short asic_revision;
	unsigned int num_cpus;
	unsigned int nxt_cqid;
	struct msix_entry msix_entries[MAX_CPUS + 1];
	bool msix_enabled;
	struct be_mem_descriptor *init_mem;

	unsigned short io_sgl_alloc_index;
	unsigned short io_sgl_free_index;
	unsigned short io_sgl_hndl_avbl;
	struct sgl_handle **io_sgl_hndl_base;
	struct sgl_handle **sgl_hndl_array;

	unsigned short eh_sgl_alloc_index;
	unsigned short eh_sgl_free_index;
	unsigned short eh_sgl_hndl_avbl;
	struct sgl_handle **eh_sgl_hndl_base;
	spinlock_t io_sgl_lock;
	spinlock_t mgmt_sgl_lock;
	spinlock_t isr_lock;
	unsigned int age;
	unsigned short avlbl_cids;
	unsigned short cid_alloc;
	unsigned short cid_free;
	struct beiscsi_conn *conn_table[BE2_MAX_SESSIONS * 2];
	struct list_head hba_queue;
	unsigned short *cid_array;
	struct iscsi_endpoint **ep_array;
	struct Scsi_Host *shost;
	struct {
		/**
		 * group together since they are used most frequently
		 * for cid to cri conversion
		 */
		unsigned int iscsi_cid_start;
		unsigned int phys_port;

		unsigned int isr_offset;
		unsigned int iscsi_icd_start;
		unsigned int iscsi_cid_count;
		unsigned int iscsi_icd_count;
		unsigned int pci_function;

		unsigned short cid_alloc;
		unsigned short cid_free;
		unsigned short avlbl_cids;
		unsigned short iscsi_features;
		spinlock_t cid_lock;
	} fw_config;

	u8 mac_address[ETH_ALEN];
	unsigned short todo_cq;
	unsigned short todo_mcc_cq;
	char wq_name[20];
	struct workqueue_struct *wq;	/* The actuak work queue */
	struct work_struct work_cqs;	/* The work being queued */
	struct be_ctrl_info ctrl;
};

struct beiscsi_session {
	struct pci_pool *bhs_pool;
};

/**
 * struct beiscsi_conn - iscsi connection structure
 */
struct beiscsi_conn {
	struct iscsi_conn *conn;
	struct beiscsi_hba *phba;
	u32 exp_statsn;
	u32 beiscsi_conn_cid;
	struct beiscsi_endpoint *ep;
	unsigned short login_in_progress;
	struct sgl_handle *plogin_sgl_handle;
	struct beiscsi_session *beiscsi_sess;
	struct iscsi_task *task;
};

/* This structure is used by the chip */
struct pdu_data_out {
	u32 dw[12];
};
/**
 * Pseudo amap definition in which each bit of the actual structure is defined
 * as a byte: used to calculate offset/shift/mask of each field
 */
struct amap_pdu_data_out {
	u8 opcode[6];		/* opcode */
	u8 rsvd0[2];		/* should be 0 */
	u8 rsvd1[7];
	u8 final_bit;		/* F bit */
	u8 rsvd2[16];
	u8 ahs_length[8];	/* no AHS */
	u8 data_len_hi[8];
	u8 data_len_lo[16];	/* DataSegmentLength */
	u8 lun[64];
	u8 itt[32];		/* ITT; initiator task tag */
	u8 ttt[32];		/* TTT; valid for R2T or 0xffffffff */
	u8 rsvd3[32];
	u8 exp_stat_sn[32];
	u8 rsvd4[32];
	u8 data_sn[32];
	u8 buffer_offset[32];
	u8 rsvd5[32];
};

struct be_cmd_bhs {
	struct iscsi_cmd iscsi_hdr;
	unsigned char pad1[16];
	struct pdu_data_out iscsi_data_pdu;
	unsigned char pad2[BE_SENSE_INFO_SIZE -
			sizeof(struct pdu_data_out)];
};

struct beiscsi_io_task {
	struct wrb_handle *pwrb_handle;
	struct sgl_handle *psgl_handle;
	struct beiscsi_conn *conn;
	struct scsi_cmnd *scsi_cmnd;
	unsigned int cmd_sn;
	unsigned int flags;
	unsigned short cid;
	unsigned short header_len;
	itt_t libiscsi_itt;
	struct be_cmd_bhs *cmd_bhs;
	struct be_bus_address bhs_pa;
	unsigned short bhs_len;
};

struct be_nonio_bhs {
	struct iscsi_hdr iscsi_hdr;
	unsigned char pad1[16];
	struct pdu_data_out iscsi_data_pdu;
	unsigned char pad2[BE_SENSE_INFO_SIZE -
			sizeof(struct pdu_data_out)];
};

struct be_status_bhs {
	struct iscsi_cmd iscsi_hdr;
	unsigned char pad1[16];
	/**
	 * The plus 2 below is to hold the sense info length that gets
	 * DMA'ed by RxULP
	 */
	unsigned char sense_info[BE_SENSE_INFO_SIZE];
};

struct iscsi_sge {
	u32 dw[4];
};

/**
 * Pseudo amap definition in which each bit of the actual structure is defined
 * as a byte: used to calculate offset/shift/mask of each field
 */
struct amap_iscsi_sge {
	u8 addr_hi[32];
	u8 addr_lo[32];
	u8 sge_offset[22];	/* DWORD 2 */
	u8 rsvd0[9];		/* DWORD 2 */
	u8 last_sge;		/* DWORD 2 */
	u8 len[17];		/* DWORD 3 */
	u8 rsvd1[15];		/* DWORD 3 */
};

struct beiscsi_offload_params {
	u32 dw[5];
};

#define OFFLD_PARAMS_ERL	0x00000003
#define OFFLD_PARAMS_DDE	0x00000004
#define OFFLD_PARAMS_HDE	0x00000008
#define OFFLD_PARAMS_IR2T	0x00000010
#define OFFLD_PARAMS_IMD	0x00000020

/**
 * Pseudo amap definition in which each bit of the actual structure is defined
 * as a byte: used to calculate offset/shift/mask of each field
 */
struct amap_beiscsi_offload_params {
	u8 max_burst_length[32];
	u8 max_send_data_segment_length[32];
	u8 first_burst_length[32];
	u8 erl[2];
	u8 dde[1];
	u8 hde[1];
	u8 ir2t[1];
	u8 imd[1];
	u8 pad[26];
	u8 exp_statsn[32];
};

/* void hwi_complete_drvr_msgs(struct beiscsi_conn *beiscsi_conn,
		struct beiscsi_hba *phba, struct sol_cqe *psol);*/

struct async_pdu_handle {
	struct list_head link;
	struct be_bus_address pa;
	void *pbuffer;
	unsigned int consumed;
	unsigned char index;
	unsigned char is_header;
	unsigned short cri;
	unsigned long buffer_len;
};

struct hwi_async_entry {
	struct {
		unsigned char hdr_received;
		unsigned char hdr_len;
		unsigned short bytes_received;
		unsigned int bytes_needed;
		struct list_head list;
	} wait_queue;

	struct list_head header_busy_list;
	struct list_head data_busy_list;
};

#define BE_MIN_ASYNC_ENTRIES 128

struct hwi_async_pdu_context {
	struct {
		struct be_bus_address pa_base;
		void *va_base;
		void *ring_base;
		struct async_pdu_handle *handle_base;

		unsigned int host_write_ptr;
		unsigned int ep_read_ptr;
		unsigned int writables;

		unsigned int free_entries;
		unsigned int busy_entries;
		unsigned int buffer_size;
		unsigned int num_entries;

		struct list_head free_list;
	} async_header;

	struct {
		struct be_bus_address pa_base;
		void *va_base;
		void *ring_base;
		struct async_pdu_handle *handle_base;

		unsigned int host_write_ptr;
		unsigned int ep_read_ptr;
		unsigned int writables;

		unsigned int free_entries;
		unsigned int busy_entries;
		unsigned int buffer_size;
		struct list_head free_list;
		unsigned int num_entries;
	} async_data;

	/**
	 * This is a varying size list! Do not add anything
	 * after this entry!!
	 */
	struct hwi_async_entry async_entry[BE_MIN_ASYNC_ENTRIES];
};

#define PDUCQE_CODE_MASK	0x0000003F
#define PDUCQE_DPL_MASK		0xFFFF0000
#define PDUCQE_INDEX_MASK	0x0000FFFF

struct i_t_dpdu_cqe {
	u32 dw[4];
} __packed;

/**
 * Pseudo amap definition in which each bit of the actual structure is defined
 * as a byte: used to calculate offset/shift/mask of each field
 */
struct amap_i_t_dpdu_cqe {
	u8 db_addr_hi[32];
	u8 db_addr_lo[32];
	u8 code[6];
	u8 cid[10];
	u8 dpl[16];
	u8 index[16];
	u8 num_cons[10];
	u8 rsvd0[4];
	u8 final;
	u8 valid;
} __packed;

#define CQE_VALID_MASK	0x80000000
#define CQE_CODE_MASK	0x0000003F
#define CQE_CID_MASK	0x0000FFC0

#define EQE_VALID_MASK		0x00000001
#define EQE_MAJORCODE_MASK	0x0000000E
#define EQE_RESID_MASK		0xFFFF0000

struct be_eq_entry {
	u32 dw[1];
} __packed;

/**
 * Pseudo amap definition in which each bit of the actual structure is defined
 * as a byte: used to calculate offset/shift/mask of each field
 */
struct amap_eq_entry {
	u8 valid;		/* DWORD 0 */
	u8 major_code[3];	/* DWORD 0 */
	u8 minor_code[12];	/* DWORD 0 */
	u8 resource_id[16];	/* DWORD 0 */

} __packed;

struct cq_db {
	u32 dw[1];
} __packed;

/**
 * Pseudo amap definition in which each bit of the actual structure is defined
 * as a byte: used to calculate offset/shift/mask of each field
 */
struct amap_cq_db {
	u8 qid[10];
	u8 event[1];
	u8 rsvd0[5];
	u8 num_popped[13];
	u8 rearm[1];
	u8 rsvd1[2];
} __packed;

void beiscsi_process_eq(struct beiscsi_hba *phba);

struct iscsi_wrb {
	u32 dw[16];
} __packed;

#define WRB_TYPE_MASK 0xF0000000

/**
 * Pseudo amap definition in which each bit of the actual structure is defined
 * as a byte: used to calculate offset/shift/mask of each field
 */
struct amap_iscsi_wrb {
	u8 lun[14];		/* DWORD 0 */
	u8 lt;			/* DWORD 0 */
	u8 invld;		/* DWORD 0 */
	u8 wrb_idx[8];		/* DWORD 0 */
	u8 dsp;			/* DWORD 0 */
	u8 dmsg;		/* DWORD 0 */
	u8 undr_run;		/* DWORD 0 */
	u8 over_run;		/* DWORD 0 */
	u8 type[4];		/* DWORD 0 */
	u8 ptr2nextwrb[8];	/* DWORD 1 */
	u8 r2t_exp_dtl[24];	/* DWORD 1 */
	u8 sgl_icd_idx[12];	/* DWORD 2 */
	u8 rsvd0[20];		/* DWORD 2 */
	u8 exp_data_sn[32];	/* DWORD 3 */
	u8 iscsi_bhs_addr_hi[32];	/* DWORD 4 */
	u8 iscsi_bhs_addr_lo[32];	/* DWORD 5 */
	u8 cmdsn_itt[32];	/* DWORD 6 */
	u8 dif_ref_tag[32];	/* DWORD 7 */
	u8 sge0_addr_hi[32];	/* DWORD 8 */
	u8 sge0_addr_lo[32];	/* DWORD 9  */
	u8 sge0_offset[22];	/* DWORD 10 */
	u8 pbs;			/* DWORD 10 */
	u8 dif_mode[2];		/* DWORD 10 */
	u8 rsvd1[6];		/* DWORD 10 */
	u8 sge0_last;		/* DWORD 10 */
	u8 sge0_len[17];	/* DWORD 11 */
	u8 dif_meta_tag[14];	/* DWORD 11 */
	u8 sge0_in_ddr;		/* DWORD 11 */
	u8 sge1_addr_hi[32];	/* DWORD 12 */
	u8 sge1_addr_lo[32];	/* DWORD 13 */
	u8 sge1_r2t_offset[22];	/* DWORD 14 */
	u8 rsvd2[9];		/* DWORD 14 */
	u8 sge1_last;		/* DWORD 14 */
	u8 sge1_len[17];	/* DWORD 15 */
	u8 ref_sgl_icd_idx[12];	/* DWORD 15 */
	u8 rsvd3[2];		/* DWORD 15 */
	u8 sge1_in_ddr;		/* DWORD 15 */

} __packed;

struct wrb_handle *alloc_wrb_handle(struct beiscsi_hba *phba, unsigned int cid,
				    int index);
void
free_mgmt_sgl_handle(struct beiscsi_hba *phba, struct sgl_handle *psgl_handle);

struct pdu_nop_out {
	u32 dw[12];
};

/**
 * Pseudo amap definition in which each bit of the actual structure is defined
 * as a byte: used to calculate offset/shift/mask of each field
 */
struct amap_pdu_nop_out {
	u8 opcode[6];		/* opcode 0x00 */
	u8 i_bit;		/* I Bit */
	u8 x_bit;		/* reserved; should be 0 */
	u8 fp_bit_filler1[7];
	u8 f_bit;		/* always 1 */
	u8 reserved1[16];
	u8 ahs_length[8];	/* no AHS */
	u8 data_len_hi[8];
	u8 data_len_lo[16];	/* DataSegmentLength */
	u8 lun[64];
	u8 itt[32];		/* initiator id for ping or 0xffffffff */
	u8 ttt[32];		/* target id for ping or 0xffffffff */
	u8 cmd_sn[32];
	u8 exp_stat_sn[32];
	u8 reserved5[128];
};

#define PDUBASE_OPCODE_MASK	0x0000003F
#define PDUBASE_DATALENHI_MASK	0x0000FF00
#define PDUBASE_DATALENLO_MASK	0xFFFF0000

struct pdu_base {
	u32 dw[16];
} __packed;

/**
 * Pseudo amap definition in which each bit of the actual structure is defined
 * as a byte: used to calculate offset/shift/mask of each field
 */
struct amap_pdu_base {
	u8 opcode[6];
	u8 i_bit;		/* immediate bit */
	u8 x_bit;		/* reserved, always 0 */
	u8 reserved1[24];	/* opcode-specific fields */
	u8 ahs_length[8];	/* length units is 4 byte words */
	u8 data_len_hi[8];
	u8 data_len_lo[16];	/* DatasegmentLength */
	u8 lun[64];		/* lun or opcode-specific fields */
	u8 itt[32];		/* initiator task tag */
	u8 reserved4[224];
};

struct iscsi_target_context_update_wrb {
	u32 dw[16];
} __packed;

/**
 * Pseudo amap definition in which each bit of the actual structure is defined
 * as a byte: used to calculate offset/shift/mask of each field
 */
struct amap_iscsi_target_context_update_wrb {
	u8 lun[14];		/* DWORD 0 */
	u8 lt;			/* DWORD 0 */
	u8 invld;		/* DWORD 0 */
	u8 wrb_idx[8];		/* DWORD 0 */
	u8 dsp;			/* DWORD 0 */
	u8 dmsg;		/* DWORD 0 */
	u8 undr_run;		/* DWORD 0 */
	u8 over_run;		/* DWORD 0 */
	u8 type[4];		/* DWORD 0 */
	u8 ptr2nextwrb[8];	/* DWORD 1 */
	u8 max_burst_length[19];	/* DWORD 1 */
	u8 rsvd0[5];		/* DWORD 1 */
	u8 rsvd1[15];		/* DWORD 2 */
	u8 max_send_data_segment_length[17];	/* DWORD 2 */
	u8 first_burst_length[14];	/* DWORD 3 */
	u8 rsvd2[2];		/* DWORD 3 */
	u8 tx_wrbindex_drv_msg[8];	/* DWORD 3 */
	u8 rsvd3[5];		/* DWORD 3 */
	u8 session_state[3];	/* DWORD 3 */
	u8 rsvd4[16];		/* DWORD 4 */
	u8 tx_jumbo;		/* DWORD 4 */
	u8 hde;			/* DWORD 4 */
	u8 dde;			/* DWORD 4 */
	u8 erl[2];		/* DWORD 4 */
	u8 domain_id[5];		/* DWORD 4 */
	u8 mode;		/* DWORD 4 */
	u8 imd;			/* DWORD 4 */
	u8 ir2t;		/* DWORD 4 */
	u8 notpredblq[2];	/* DWORD 4 */
	u8 compltonack;		/* DWORD 4 */
	u8 stat_sn[32];		/* DWORD 5 */
	u8 pad_buffer_addr_hi[32];	/* DWORD 6 */
	u8 pad_buffer_addr_lo[32];	/* DWORD 7 */
	u8 pad_addr_hi[32];	/* DWORD 8 */
	u8 pad_addr_lo[32];	/* DWORD 9 */
	u8 rsvd5[32];		/* DWORD 10 */
	u8 rsvd6[32];		/* DWORD 11 */
	u8 rsvd7[32];		/* DWORD 12 */
	u8 rsvd8[32];		/* DWORD 13 */
	u8 rsvd9[32];		/* DWORD 14 */
	u8 rsvd10[32];		/* DWORD 15 */

} __packed;

struct be_ring {
	u32 pages;		/* queue size in pages */
	u32 id;			/* queue id assigned by beklib */
	u32 num;		/* number of elements in queue */
	u32 cidx;		/* consumer index */
	u32 pidx;		/* producer index -- not used by most rings */
	u32 item_size;		/* size in bytes of one object */

	void *va;		/* The virtual address of the ring.  This
				 * should be last to allow 32 & 64 bit debugger
				 * extensions to work.
				 */
};

struct hwi_wrb_context {
	struct list_head wrb_handle_list;
	struct list_head wrb_handle_drvr_list;
	struct wrb_handle **pwrb_handle_base;
	struct wrb_handle **pwrb_handle_basestd;
	struct iscsi_wrb *plast_wrb;
	unsigned short alloc_index;
	unsigned short free_index;
	unsigned short wrb_handles_available;
	unsigned short cid;
};

struct hwi_controller {
	struct list_head io_sgl_list;
	struct list_head eh_sgl_list;
	struct sgl_handle *psgl_handle_base;
	unsigned int wrb_mem_index;

	struct hwi_wrb_context wrb_context[BE2_MAX_SESSIONS * 2];
	struct mcc_wrb *pmcc_wrb_base;
	struct be_ring default_pdu_hdr;
	struct be_ring default_pdu_data;
	struct hwi_context_memory *phwi_ctxt;
	unsigned short cq_errors[CXN_KILLED_CMND_DATA_NOT_ON_SAME_CONN];
};

enum hwh_type_enum {
	HWH_TYPE_IO = 1,
	HWH_TYPE_LOGOUT = 2,
	HWH_TYPE_TMF = 3,
	HWH_TYPE_NOP = 4,
	HWH_TYPE_IO_RD = 5,
	HWH_TYPE_LOGIN = 11,
	HWH_TYPE_INVALID = 0xFFFFFFFF
};

struct wrb_handle {
	enum hwh_type_enum type;
	unsigned short wrb_index;
	unsigned short nxt_wrb_index;

	struct iscsi_task *pio_handle;
	struct iscsi_wrb *pwrb;
};

struct hwi_context_memory {
	/* Adaptive interrupt coalescing (AIC) info */
	u16 min_eqd;		/* in usecs */
	u16 max_eqd;		/* in usecs */
	u16 cur_eqd;		/* in usecs */
	struct be_eq_obj be_eq[MAX_CPUS];
	struct be_queue_info be_cq[MAX_CPUS];

	struct be_queue_info be_def_hdrq;
	struct be_queue_info be_def_dataq;

	struct be_queue_info be_wrbq[BE2_MAX_SESSIONS];
	struct be_mcc_wrb_context *pbe_mcc_context;

	struct hwi_async_pdu_context *pasync_ctx;
};

#endif
