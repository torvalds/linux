/*
 * Copyright 2017 Broadcom. All Rights Reserved.
 * The term "Broadcom" refers to Broadcom Limited and/or its subsidiaries.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation. The full GNU General
 * Public License is included in this distribution in the file called COPYING.
 *
 * Contact Information:
 * linux-drivers@broadcom.com
 *
 */

#ifndef _BEISCSI_MAIN_
#define _BEISCSI_MAIN_

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/if_ether.h>
#include <linux/in.h>
#include <linux/ctype.h>
#include <linux/module.h>
#include <linux/aer.h>
#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/iscsi_proto.h>
#include <scsi/libiscsi.h>
#include <scsi/scsi_transport_iscsi.h>

#define DRV_NAME		"be2iscsi"
#define BUILD_STR		"11.4.0.0"
#define BE_NAME			"Emulex OneConnect" \
				"Open-iSCSI Driver version" BUILD_STR
#define DRV_DESC		BE_NAME " " "Driver"

#define BE_VENDOR_ID		0x19A2
#define ELX_VENDOR_ID		0x10DF
/* DEVICE ID's for BE2 */
#define BE_DEVICE_ID1		0x212
#define OC_DEVICE_ID1		0x702
#define OC_DEVICE_ID2		0x703

/* DEVICE ID's for BE3 */
#define BE_DEVICE_ID2		0x222
#define OC_DEVICE_ID3		0x712

/* DEVICE ID for SKH */
#define OC_SKH_ID1		0x722

#define BE2_IO_DEPTH		1024
#define BE2_MAX_SESSIONS	256
#define BE2_TMFS		16
#define BE2_NOPOUT_REQ		16
#define BE2_SGE			32
#define BE2_DEFPDU_HDR_SZ	64
#define BE2_DEFPDU_DATA_SZ	8192
#define BE2_MAX_NUM_CQ_PROC	512

#define MAX_CPUS		64
#define BEISCSI_MAX_NUM_CPUS	7

#define BEISCSI_VER_STRLEN 32

#define BEISCSI_SGLIST_ELEMENTS	30

/**
 * BE_INVLDT_CMD_TBL_SZ is 128 which is total number commands that can
 * be invalidated at a time, consider it before changing the value of
 * BEISCSI_CMD_PER_LUN.
 */
#define BEISCSI_CMD_PER_LUN	128	/* scsi_host->cmd_per_lun */
#define BEISCSI_MAX_SECTORS	1024	/* scsi_host->max_sectors */
#define BEISCSI_TEMPLATE_HDR_PER_CXN_SIZE 128 /* Template size per cxn */

#define BEISCSI_MAX_CMD_LEN	16	/* scsi_host->max_cmd_len */
#define BEISCSI_NUM_MAX_LUN	256	/* scsi_host->max_lun */
#define BEISCSI_NUM_DEVICES_SUPPORTED	0x01
#define BEISCSI_MAX_FRAGS_INIT	192
#define BE_NUM_MSIX_ENTRIES	1

#define BE_SENSE_INFO_SIZE		258
#define BE_ISCSI_PDU_HEADER_SIZE	64
#define BE_MIN_MEM_SIZE			16384
#define MAX_CMD_SZ			65536
#define IIOC_SCSI_DATA                  0x05	/* Write Operation */

/**
 * hardware needs the async PDU buffers to be posted in multiples of 8
 * So have atleast 8 of them by default
 */

#define HWI_GET_ASYNC_PDU_CTX(phwi, ulp_num)	\
	(phwi->phwi_ctxt->pasync_ctx[ulp_num])

/********* Memory BAR register ************/
#define PCICFG_MEMBAR_CTRL_INT_CTRL_OFFSET	0xfc
/**
 * Host Interrupt Enable, if set interrupts are enabled although "PCI Interrupt
 * Disable" may still globally block interrupts in addition to individual
 * interrupt masks; a mechanism for the device driver to block all interrupts
 * atomically without having to arbitrate for the PCI Interrupt Disable bit
 * with the OS.
 */
#define MEMBAR_CTRL_INT_CTRL_HOSTINTR_MASK	(1 << 29)	/* bit 29 */

/********* ISR0 Register offset **********/
#define CEV_ISR0_OFFSET				0xC18
#define CEV_ISR_SIZE				4

/**
 * Macros for reading/writing a protection domain or CSR registers
 * in BladeEngine.
 */

#define DB_TXULP0_OFFSET 0x40
#define DB_RXULP0_OFFSET 0xA0
/********* Event Q door bell *************/
#define DB_EQ_OFFSET			DB_CQ_OFFSET
#define DB_EQ_RING_ID_LOW_MASK		0x1FF	/* bits 0 - 8 */
/* Clear the interrupt for this eq */
#define DB_EQ_CLR_SHIFT			(9)	/* bit 9 */
/* Must be 1 */
#define DB_EQ_EVNT_SHIFT		(10)	/* bit 10 */
/* Higher Order EQ_ID bit */
#define DB_EQ_RING_ID_HIGH_MASK	0x1F /* bits 11 - 15 */
#define DB_EQ_HIGH_SET_SHIFT	11
#define DB_EQ_HIGH_FEILD_SHIFT	9
/* Number of event entries processed */
#define DB_EQ_NUM_POPPED_SHIFT		(16)	/* bits 16 - 28 */
/* Rearm bit */
#define DB_EQ_REARM_SHIFT		(29)	/* bit 29 */

/********* Compl Q door bell *************/
#define DB_CQ_OFFSET			0x120
#define DB_CQ_RING_ID_LOW_MASK		0x3FF	/* bits 0 - 9 */
/* Higher Order CQ_ID bit */
#define DB_CQ_RING_ID_HIGH_MASK	0x1F /* bits 11 - 15 */
#define DB_CQ_HIGH_SET_SHIFT	11
#define DB_CQ_HIGH_FEILD_SHIFT	10

/* Number of event entries processed */
#define DB_CQ_NUM_POPPED_SHIFT		(16)	/* bits 16 - 28 */
/* Rearm bit */
#define DB_CQ_REARM_SHIFT		(29)	/* bit 29 */

#define GET_HWI_CONTROLLER_WS(pc)	(pc->phwi_ctrlr)
#define HWI_GET_DEF_BUFQ_ID(pc, ulp_num) (((struct hwi_controller *)\
		(GET_HWI_CONTROLLER_WS(pc)))->default_pdu_data[ulp_num].id)
#define HWI_GET_DEF_HDRQ_ID(pc, ulp_num) (((struct hwi_controller *)\
		(GET_HWI_CONTROLLER_WS(pc)))->default_pdu_hdr[ulp_num].id)

#define PAGES_REQUIRED(x) \
	((x < PAGE_SIZE) ? 1 :  ((x + PAGE_SIZE - 1) / PAGE_SIZE))

#define BEISCSI_MSI_NAME 20 /* size of msi_name string */

#define MEM_DESCR_OFFSET 8
#define BEISCSI_DEFQ_HDR 1
#define BEISCSI_DEFQ_DATA 0
enum be_mem_enum {
	HWI_MEM_ADDN_CONTEXT,
	HWI_MEM_WRB,
	HWI_MEM_WRBH,
	HWI_MEM_SGLH,
	HWI_MEM_SGE,
	HWI_MEM_TEMPLATE_HDR_ULP0,
	HWI_MEM_ASYNC_HEADER_BUF_ULP0,	/* 6 */
	HWI_MEM_ASYNC_DATA_BUF_ULP0,
	HWI_MEM_ASYNC_HEADER_RING_ULP0,
	HWI_MEM_ASYNC_DATA_RING_ULP0,
	HWI_MEM_ASYNC_HEADER_HANDLE_ULP0,
	HWI_MEM_ASYNC_DATA_HANDLE_ULP0,	/* 11 */
	HWI_MEM_ASYNC_PDU_CONTEXT_ULP0,
	HWI_MEM_TEMPLATE_HDR_ULP1,
	HWI_MEM_ASYNC_HEADER_BUF_ULP1,	/* 14 */
	HWI_MEM_ASYNC_DATA_BUF_ULP1,
	HWI_MEM_ASYNC_HEADER_RING_ULP1,
	HWI_MEM_ASYNC_DATA_RING_ULP1,
	HWI_MEM_ASYNC_HEADER_HANDLE_ULP1,
	HWI_MEM_ASYNC_DATA_HANDLE_ULP1,	/* 19 */
	HWI_MEM_ASYNC_PDU_CONTEXT_ULP1,
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
	unsigned int icds_per_ctrl;
	unsigned int num_sge_per_io;
	unsigned int defpdu_hdr_sz;
	unsigned int defpdu_data_sz;
	unsigned int num_cq_entries;
	unsigned int num_eq_entries;
	unsigned int wrbs_per_cxn;
	unsigned int hwi_ws_sz;
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

#define BEISCSI_GET_ULP_FROM_CRI(phwi_ctrlr, cri) \
	(phwi_ctrlr->wrb_context[cri].ulp_num)
struct hwi_wrb_context {
	spinlock_t wrb_lock;
	struct list_head wrb_handle_list;
	struct list_head wrb_handle_drvr_list;
	struct wrb_handle **pwrb_handle_base;
	struct wrb_handle **pwrb_handle_basestd;
	struct iscsi_wrb *plast_wrb;
	unsigned short alloc_index;
	unsigned short free_index;
	unsigned short wrb_handles_available;
	unsigned short cid;
	uint8_t ulp_num;	/* ULP to which CID binded */
	uint16_t register_set;
	uint16_t doorbell_format;
	uint32_t doorbell_offset;
};

struct ulp_cid_info {
	unsigned short *cid_array;
	unsigned short avlbl_cids;
	unsigned short cid_alloc;
	unsigned short cid_free;
};

#include "be.h"
#define chip_be2(phba)      (phba->generation == BE_GEN2)
#define chip_be3_r(phba)    (phba->generation == BE_GEN3)
#define is_chip_be2_be3r(phba) (chip_be3_r(phba) || (chip_be2(phba)))

#define BEISCSI_ULP0    0
#define BEISCSI_ULP1    1
#define BEISCSI_ULP_COUNT   2
#define BEISCSI_ULP0_LOADED 0x01
#define BEISCSI_ULP1_LOADED 0x02

#define BEISCSI_ULP_AVLBL_CID(phba, ulp_num) \
	(((struct ulp_cid_info *)phba->cid_array_info[ulp_num])->avlbl_cids)
#define BEISCSI_ULP0_AVLBL_CID(phba) \
	BEISCSI_ULP_AVLBL_CID(phba, BEISCSI_ULP0)
#define BEISCSI_ULP1_AVLBL_CID(phba) \
	BEISCSI_ULP_AVLBL_CID(phba, BEISCSI_ULP1)

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
	unsigned int num_cpus;
	unsigned int nxt_cqid;
	char *msi_name[MAX_CPUS];
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
	spinlock_t async_pdu_lock;
	struct list_head hba_queue;
#define BE_MAX_SESSION 2048
#define BE_INVALID_CID 0xffff
#define BE_SET_CID_TO_CRI(cri_index, cid) \
			  (phba->cid_to_cri_map[cid] = cri_index)
#define BE_GET_CRI_FROM_CID(cid) (phba->cid_to_cri_map[cid])
	unsigned short cid_to_cri_map[BE_MAX_SESSION];
	struct ulp_cid_info *cid_array_info[BEISCSI_ULP_COUNT];
	struct iscsi_endpoint **ep_array;
	struct beiscsi_conn **conn_table;
	struct Scsi_Host *shost;
	struct iscsi_iface *ipv4_iface;
	struct iscsi_iface *ipv6_iface;
	struct {
		/**
		 * group together since they are used most frequently
		 * for cid to cri conversion
		 */
#define BEISCSI_PHYS_PORT_MAX	4
		unsigned int phys_port;
		/* valid values of phys_port id are 0, 1, 2, 3 */
		unsigned int eqid_count;
		unsigned int cqid_count;
		unsigned int iscsi_cid_start[BEISCSI_ULP_COUNT];
#define BEISCSI_GET_CID_COUNT(phba, ulp_num) \
		(phba->fw_config.iscsi_cid_count[ulp_num])
		unsigned int iscsi_cid_count[BEISCSI_ULP_COUNT];
		unsigned int iscsi_icd_count[BEISCSI_ULP_COUNT];
		unsigned int iscsi_icd_start[BEISCSI_ULP_COUNT];
		unsigned int iscsi_chain_start[BEISCSI_ULP_COUNT];
		unsigned int iscsi_chain_count[BEISCSI_ULP_COUNT];

		unsigned short iscsi_features;
		uint16_t dual_ulp_aware;
		unsigned long ulp_supported;
	} fw_config;

	unsigned long state;
#define BEISCSI_HBA_ONLINE	0
#define BEISCSI_HBA_LINK_UP	1
#define BEISCSI_HBA_BOOT_FOUND	2
#define BEISCSI_HBA_BOOT_WORK	3
#define BEISCSI_HBA_UER_SUPP	4
#define BEISCSI_HBA_PCI_ERR	5
#define BEISCSI_HBA_FW_TIMEOUT	6
#define BEISCSI_HBA_IN_UE	7
#define BEISCSI_HBA_IN_TPE	8

/* error bits */
#define BEISCSI_HBA_IN_ERR	((1 << BEISCSI_HBA_PCI_ERR) | \
				 (1 << BEISCSI_HBA_FW_TIMEOUT) | \
				 (1 << BEISCSI_HBA_IN_UE) | \
				 (1 << BEISCSI_HBA_IN_TPE))

	u8 optic_state;
	struct delayed_work eqd_update;
	/* update EQ delay timer every 1000ms */
#define BEISCSI_EQD_UPDATE_INTERVAL	1000
	struct timer_list hw_check;
	/* check for UE every 1000ms */
#define BEISCSI_UE_DETECT_INTERVAL	1000
	u32 ue2rp;
	struct delayed_work recover_port;
	struct work_struct sess_work;

	bool mac_addr_set;
	u8 mac_address[ETH_ALEN];
	u8 port_name;
	u8 port_speed;
	char fw_ver_str[BEISCSI_VER_STRLEN];
	struct workqueue_struct *wq;	/* The actuak work queue */
	struct be_ctrl_info ctrl;
	unsigned int generation;
	unsigned int interface_handle;

	struct be_aic_obj aic_obj[MAX_CPUS];
	unsigned int attr_log_enable;
	int (*iotask_fn)(struct iscsi_task *,
			struct scatterlist *sg,
			uint32_t num_sg, uint32_t xferlen,
			uint32_t writedir);
	struct boot_struct {
		int retry;
		unsigned int tag;
		unsigned int s_handle;
		struct be_dma_mem nonemb_cmd;
		enum {
			BEISCSI_BOOT_REOPEN_SESS = 1,
			BEISCSI_BOOT_GET_SHANDLE,
			BEISCSI_BOOT_GET_SINFO,
			BEISCSI_BOOT_LOGOUT_SESS,
			BEISCSI_BOOT_CREATE_KSET,
		} action;
		struct mgmt_session_info boot_sess;
		struct iscsi_boot_kset *boot_kset;
	} boot_struct;
	struct work_struct boot_work;
};

#define beiscsi_hba_in_error(phba) ((phba)->state & BEISCSI_HBA_IN_ERR)
#define beiscsi_hba_is_online(phba) \
	(!beiscsi_hba_in_error((phba)) && \
	 test_bit(BEISCSI_HBA_ONLINE, &phba->state))

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
	u32 doorbell_offset;
	u32 beiscsi_conn_cid;
	struct beiscsi_endpoint *ep;
	unsigned short login_in_progress;
	struct wrb_handle *plogin_wrb_handle;
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
	struct iscsi_scsi_req iscsi_hdr;
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
	int num_sg;
	struct hwi_wrb_context *pwrb_context;
	itt_t libiscsi_itt;
	struct be_cmd_bhs *cmd_bhs;
	struct be_bus_address bhs_pa;
	unsigned short bhs_len;
	dma_addr_t mtask_addr;
	uint32_t mtask_data_count;
	uint8_t wrb_type;
};

struct be_nonio_bhs {
	struct iscsi_hdr iscsi_hdr;
	unsigned char pad1[16];
	struct pdu_data_out iscsi_data_pdu;
	unsigned char pad2[BE_SENSE_INFO_SIZE -
			sizeof(struct pdu_data_out)];
};

struct be_status_bhs {
	struct iscsi_scsi_req iscsi_hdr;
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
	u32 dw[6];
};

#define OFFLD_PARAMS_ERL	0x00000003
#define OFFLD_PARAMS_DDE	0x00000004
#define OFFLD_PARAMS_HDE	0x00000008
#define OFFLD_PARAMS_IR2T	0x00000010
#define OFFLD_PARAMS_IMD	0x00000020
#define OFFLD_PARAMS_DATA_SEQ_INORDER   0x00000040
#define OFFLD_PARAMS_PDU_SEQ_INORDER    0x00000080
#define OFFLD_PARAMS_MAX_R2T 0x00FFFF00

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
	u8 data_seq_inorder[1];
	u8 pdu_seq_inorder[1];
	u8 max_r2t[16];
	u8 pad[8];
	u8 exp_statsn[32];
	u8 max_recv_data_segment_length[32];
};

struct hd_async_handle {
	struct list_head link;
	struct be_bus_address pa;
	void *pbuffer;
	u32 buffer_len;
	u16 index;
	u16 cri;
	u8 is_header;
	u8 is_final;
	u8 in_use;
};

#define BEISCSI_ASYNC_HDQ_SIZE(phba, ulp) \
	(BEISCSI_GET_CID_COUNT((phba), (ulp)) * 2)

/**
 * This has list of async PDUs that are waiting to be processed.
 * Buffers live in this list for a brief duration before they get
 * processed and posted back to hardware.
 * Note that we don't really need one cri_wait_queue per async_entry.
 * We need one cri_wait_queue per CRI. Its easier to manage if this
 * is tagged along with the async_entry.
 */
struct hd_async_entry {
	struct cri_wait_queue {
		unsigned short hdr_len;
		unsigned int bytes_received;
		unsigned int bytes_needed;
		struct list_head list;
	} wq;
	/* handles posted to FW resides here */
	struct hd_async_handle *header;
	struct hd_async_handle *data;
};

struct hd_async_buf_context {
	struct be_bus_address pa_base;
	void *va_base;
	void *ring_base;
	struct hd_async_handle *handle_base;
	u32 buffer_size;
	u16 pi;
};

/**
 * hd_async_context is declared for each ULP supporting iSCSI function.
 */
struct hd_async_context {
	struct hd_async_buf_context async_header;
	struct hd_async_buf_context async_data;
	u16 num_entries;
	/**
	 * When unsol PDU is in, it needs to be chained till all the bytes are
	 * received and then processing is done. hd_async_entry is created
	 * based on the cid_count for each ULP. When unsol PDU comes in based
	 * on the conn_id it needs to be added to the correct async_entry wq.
	 * Below defined cid_to_async_cri_map is used to reterive the
	 * async_cri_map for a particular connection.
	 *
	 * This array is initialized after beiscsi_create_wrb_rings returns.
	 *
	 * - this method takes more memory space, fixed to 2K
	 * - any support for connections greater than this the array size needs
	 * to be incremented
	 */
#define BE_GET_ASYNC_CRI_FROM_CID(cid) (pasync_ctx->cid_to_async_cri_map[cid])
	unsigned short cid_to_async_cri_map[BE_MAX_SESSION];
	/**
	 * This is a variable size array. Don`t add anything after this field!!
	 */
	struct hd_async_entry *async_entry;
};

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

struct amap_i_t_dpdu_cqe_v2 {
	u8 db_addr_hi[32];  /* DWORD 0 */
	u8 db_addr_lo[32];  /* DWORD 1 */
	u8 code[6]; /* DWORD 2 */
	u8 num_cons; /* DWORD 2*/
	u8 rsvd0[8]; /* DWORD 2 */
	u8 dpl[17]; /* DWORD 2 */
	u8 index[16]; /* DWORD 3 */
	u8 cid[13]; /* DWORD 3 */
	u8 rsvd1; /* DWORD 3 */
	u8 final; /* DWORD 3 */
	u8 valid; /* DWORD 3 */
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
#define SKH_WRB_TYPE_OFFSET 27
#define BE_WRB_TYPE_OFFSET  28

#define ADAPTER_SET_WRB_TYPE(pwrb, wrb_type, type_offset) \
		(pwrb->dw[0] |= (wrb_type << type_offset))

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

struct amap_iscsi_wrb_v2 {
	u8 r2t_exp_dtl[25]; /* DWORD 0 */
	u8 rsvd0[2];    /* DWORD 0*/
	u8 type[5];     /* DWORD 0 */
	u8 ptr2nextwrb[8];  /* DWORD 1 */
	u8 wrb_idx[8];      /* DWORD 1 */
	u8 lun[16];     /* DWORD 1 */
	u8 sgl_idx[16]; /* DWORD 2 */
	u8 ref_sgl_icd_idx[16]; /* DWORD 2 */
	u8 exp_data_sn[32]; /* DWORD 3 */
	u8 iscsi_bhs_addr_hi[32];   /* DWORD 4 */
	u8 iscsi_bhs_addr_lo[32];   /* DWORD 5 */
	u8 cq_id[16];   /* DWORD 6 */
	u8 rsvd1[16];   /* DWORD 6 */
	u8 cmdsn_itt[32];   /* DWORD 7 */
	u8 sge0_addr_hi[32];    /* DWORD 8 */
	u8 sge0_addr_lo[32];    /* DWORD 9 */
	u8 sge0_offset[24]; /* DWORD 10 */
	u8 rsvd2[7];    /* DWORD 10 */
	u8 sge0_last;   /* DWORD 10 */
	u8 sge0_len[17];    /* DWORD 11 */
	u8 rsvd3[7];    /* DWORD 11 */
	u8 diff_enbl;   /* DWORD 11 */
	u8 u_run;       /* DWORD 11 */
	u8 o_run;       /* DWORD 11 */
	u8 invld;     /* DWORD 11 */
	u8 dsp;         /* DWORD 11 */
	u8 dmsg;        /* DWORD 11 */
	u8 rsvd4;       /* DWORD 11 */
	u8 lt;          /* DWORD 11 */
	u8 sge1_addr_hi[32];    /* DWORD 12 */
	u8 sge1_addr_lo[32];    /* DWORD 13 */
	u8 sge1_r2t_offset[24]; /* DWORD 14 */
	u8 rsvd5[7];    /* DWORD 14 */
	u8 sge1_last;   /* DWORD 14 */
	u8 sge1_len[17];    /* DWORD 15 */
	u8 rsvd6[15];   /* DWORD 15 */
} __packed;


struct wrb_handle *alloc_wrb_handle(struct beiscsi_hba *phba, unsigned int cid,
				     struct hwi_wrb_context **pcontext);
void
free_mgmt_sgl_handle(struct beiscsi_hba *phba, struct sgl_handle *psgl_handle);

void beiscsi_free_mgmt_task_handles(struct beiscsi_conn *beiscsi_conn,
				     struct iscsi_task *task);

void hwi_ring_cq_db(struct beiscsi_hba *phba,
		     unsigned int id, unsigned int num_processed,
		     unsigned char rearm);

unsigned int beiscsi_process_cq(struct be_eq_obj *pbe_eq, int budget);
void beiscsi_process_mcc_cq(struct beiscsi_hba *phba);

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
#define BE_TGT_CTX_UPDT_CMD 0x07
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

#define BEISCSI_MAX_RECV_DATASEG_LEN    (64 * 1024)
#define BEISCSI_MAX_CXNS    1
struct amap_iscsi_target_context_update_wrb_v2 {
	u8 max_burst_length[24];    /* DWORD 0 */
	u8 rsvd0[3];    /* DWORD 0 */
	u8 type[5];     /* DWORD 0 */
	u8 ptr2nextwrb[8];  /* DWORD 1 */
	u8 wrb_idx[8];      /* DWORD 1 */
	u8 rsvd1[16];       /* DWORD 1 */
	u8 max_send_data_segment_length[24];    /* DWORD 2 */
	u8 rsvd2[8];    /* DWORD 2 */
	u8 first_burst_length[24]; /* DWORD 3 */
	u8 rsvd3[8]; /* DOWRD 3 */
	u8 max_r2t[16]; /* DWORD 4 */
	u8 rsvd4;       /* DWORD 4 */
	u8 hde;         /* DWORD 4 */
	u8 dde;         /* DWORD 4 */
	u8 erl[2];      /* DWORD 4 */
	u8 rsvd5[6];    /* DWORD 4 */
	u8 imd;         /* DWORD 4 */
	u8 ir2t;        /* DWORD 4 */
	u8 rsvd6[3];    /* DWORD 4 */
	u8 stat_sn[32];     /* DWORD 5 */
	u8 rsvd7[32];   /* DWORD 6 */
	u8 rsvd8[32];   /* DWORD 7 */
	u8 max_recv_dataseg_len[24];    /* DWORD 8 */
	u8 rsvd9[8]; /* DWORD 8 */
	u8 rsvd10[32];   /* DWORD 9 */
	u8 rsvd11[32];   /* DWORD 10 */
	u8 max_cxns[16]; /* DWORD 11 */
	u8 rsvd12[11]; /* DWORD  11*/
	u8 invld; /* DWORD 11 */
	u8 rsvd13;/* DWORD 11*/
	u8 dmsg; /* DWORD 11 */
	u8 data_seq_inorder; /* DWORD 11 */
	u8 pdu_seq_inorder; /* DWORD 11 */
	u8 rsvd14[32]; /*DWORD 12 */
	u8 rsvd15[32]; /* DWORD 13 */
	u8 rsvd16[32]; /* DWORD 14 */
	u8 rsvd17[32]; /* DWORD 15 */
} __packed;


struct be_ring {
	u32 pages;		/* queue size in pages */
	u32 id;			/* queue id assigned by beklib */
	u32 num;		/* number of elements in queue */
	u32 cidx;		/* consumer index */
	u32 pidx;		/* producer index -- not used by most rings */
	u32 item_size;		/* size in bytes of one object */
	u8 ulp_num;	/* ULP to which CID binded */
	u16 register_set;
	u16 doorbell_format;
	u32 doorbell_offset;

	void *va;		/* The virtual address of the ring.  This
				 * should be last to allow 32 & 64 bit debugger
				 * extensions to work.
				 */
};

struct hwi_controller {
	struct list_head io_sgl_list;
	struct list_head eh_sgl_list;
	struct sgl_handle *psgl_handle_base;

	struct hwi_wrb_context *wrb_context;
	struct be_ring default_pdu_hdr[BEISCSI_ULP_COUNT];
	struct be_ring default_pdu_data[BEISCSI_ULP_COUNT];
	struct hwi_context_memory *phwi_ctxt;
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
	unsigned short wrb_index;
	struct iscsi_task *pio_handle;
	struct iscsi_wrb *pwrb;
};

struct hwi_context_memory {
	/* Adaptive interrupt coalescing (AIC) info */
	u16 min_eqd;		/* in usecs */
	u16 max_eqd;		/* in usecs */
	u16 cur_eqd;		/* in usecs */
	struct be_eq_obj be_eq[MAX_CPUS];
	struct be_queue_info be_cq[MAX_CPUS - 1];

	struct be_queue_info *be_wrbq;
	/**
	 * Create array of ULP number for below entries as DEFQ
	 * will be created for both ULP if iSCSI Protocol is
	 * loaded on both ULP.
	 */
	struct be_queue_info be_def_hdrq[BEISCSI_ULP_COUNT];
	struct be_queue_info be_def_dataq[BEISCSI_ULP_COUNT];
	struct hd_async_context *pasync_ctx[BEISCSI_ULP_COUNT];
};

void beiscsi_start_boot_work(struct beiscsi_hba *phba, unsigned int s_handle);

/* Logging related definitions */
#define BEISCSI_LOG_INIT	0x0001	/* Initialization events */
#define BEISCSI_LOG_MBOX	0x0002	/* Mailbox Events */
#define BEISCSI_LOG_MISC	0x0004	/* Miscllaneous Events */
#define BEISCSI_LOG_EH		0x0008	/* Error Handler */
#define BEISCSI_LOG_IO		0x0010	/* IO Code Path */
#define BEISCSI_LOG_CONFIG	0x0020	/* CONFIG Code Path */
#define BEISCSI_LOG_ISCSI	0x0040	/* SCSI/iSCSI Protocol related Logs */

#define __beiscsi_log(phba, level, fmt, arg...) \
	shost_printk(level, phba->shost, fmt, __LINE__, ##arg)

#define beiscsi_log(phba, level, mask, fmt, arg...) \
do { \
	uint32_t log_value = phba->attr_log_enable; \
		if (((mask) & log_value) || (level[1] <= '3')) \
			__beiscsi_log(phba, level, fmt, ##arg); \
} while (0);

#endif
