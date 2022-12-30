/*
 * PMC-Sierra PM8001/8081/8088/8089 SAS/SATA based host adapters driver
 *
 * Copyright (c) 2008-2009 USI Co., Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 */

#ifndef _PM8001_SAS_H_
#define _PM8001_SAS_H_

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/ctype.h>
#include <linux/dma-mapping.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <scsi/libsas.h>
#include <scsi/scsi_tcq.h>
#include <scsi/sas_ata.h>
#include <linux/atomic.h>
#include <linux/blk-mq.h>
#include <linux/blk-mq-pci.h>
#include "pm8001_defs.h"

#define DRV_NAME		"pm80xx"
#define DRV_VERSION		"0.1.40"
#define PM8001_FAIL_LOGGING	0x01 /* Error message logging */
#define PM8001_INIT_LOGGING	0x02 /* driver init logging */
#define PM8001_DISC_LOGGING	0x04 /* discovery layer logging */
#define PM8001_IO_LOGGING	0x08 /* I/O path logging */
#define PM8001_EH_LOGGING	0x10 /* libsas EH function logging*/
#define PM8001_IOCTL_LOGGING	0x20 /* IOCTL message logging */
#define PM8001_MSG_LOGGING	0x40 /* misc message logging */
#define PM8001_DEV_LOGGING	0x80 /* development message logging */
#define PM8001_DEVIO_LOGGING	0x100 /* development io message logging */
#define PM8001_IOERR_LOGGING	0x200 /* development io err message logging */

#define pm8001_info(HBA, fmt, ...)					\
	pr_info("%s:: %s %d: " fmt,					\
		(HBA)->name, __func__, __LINE__, ##__VA_ARGS__)

#define pm8001_dbg(HBA, level, fmt, ...)				\
do {									\
	if (unlikely((HBA)->logging_level & PM8001_##level##_LOGGING))	\
		pm8001_info(HBA, fmt, ##__VA_ARGS__);			\
} while (0)

#define PM8001_USE_TASKLET
#define PM8001_USE_MSIX
#define PM8001_READ_VPD


#define IS_SPCV_12G(dev)	((dev->device == 0X8074)		\
				|| (dev->device == 0X8076)		\
				|| (dev->device == 0X8077)		\
				|| (dev->device == 0X8070)		\
				|| (dev->device == 0X8072))

#define PM8001_NAME_LENGTH		32/* generic length of strings */
extern struct list_head hba_list;
extern const struct pm8001_dispatch pm8001_8001_dispatch;
extern const struct pm8001_dispatch pm8001_80xx_dispatch;

struct pm8001_hba_info;
struct pm8001_ccb_info;
struct pm8001_device;

struct pm8001_ioctl_payload {
	u32	signature;
	u16	major_function;
	u16	minor_function;
	u16	status;
	u16	offset;
	u16	id;
	u32	wr_length;
	u32	rd_length;
	u8	*func_specific;
};

#define MPI_FATAL_ERROR_TABLE_OFFSET_MASK 0xFFFFFF
#define MPI_FATAL_ERROR_TABLE_SIZE(value) ((0xFF000000 & value) >> SHIFT24)
#define MPI_FATAL_EDUMP_TABLE_LO_OFFSET            0x00     /* HNFBUFL */
#define MPI_FATAL_EDUMP_TABLE_HI_OFFSET            0x04     /* HNFBUFH */
#define MPI_FATAL_EDUMP_TABLE_LENGTH               0x08     /* HNFBLEN */
#define MPI_FATAL_EDUMP_TABLE_HANDSHAKE            0x0C     /* FDDHSHK */
#define MPI_FATAL_EDUMP_TABLE_STATUS               0x10     /* FDDTSTAT */
#define MPI_FATAL_EDUMP_TABLE_ACCUM_LEN            0x14     /* ACCDDLEN */
#define MPI_FATAL_EDUMP_TABLE_TOTAL_LEN		   0x18	    /* TOTALLEN */
#define MPI_FATAL_EDUMP_TABLE_SIGNATURE		   0x1C     /* SIGNITURE */
#define MPI_FATAL_EDUMP_HANDSHAKE_RDY              0x1
#define MPI_FATAL_EDUMP_HANDSHAKE_BUSY             0x0
#define MPI_FATAL_EDUMP_TABLE_STAT_RSVD                 0x0
#define MPI_FATAL_EDUMP_TABLE_STAT_DMA_FAILED           0x1
#define MPI_FATAL_EDUMP_TABLE_STAT_NF_SUCCESS_MORE_DATA 0x2
#define MPI_FATAL_EDUMP_TABLE_STAT_NF_SUCCESS_DONE      0x3
#define TYPE_GSM_SPACE        1
#define TYPE_QUEUE            2
#define TYPE_FATAL            3
#define TYPE_NON_FATAL        4
#define TYPE_INBOUND          1
#define TYPE_OUTBOUND         2
struct forensic_data {
	u32  data_type;
	union {
		struct {
			u32  direct_len;
			u32  direct_offset;
			void  *direct_data;
		} gsm_buf;
		struct {
			u16  queue_type;
			u16  queue_index;
			u32  direct_len;
			void  *direct_data;
		} queue_buf;
		struct {
			u32  direct_len;
			u32  direct_offset;
			u32  read_len;
			void  *direct_data;
		} data_buf;
	};
};

/* bit31-26 - mask bar */
#define SCRATCH_PAD0_BAR_MASK                    0xFC000000
/* bit25-0  - offset mask */
#define SCRATCH_PAD0_OFFSET_MASK                 0x03FFFFFF
/* if AAP error state */
#define SCRATCH_PAD0_AAPERR_MASK                 0xFFFFFFFF
/* Inbound doorbell bit7 */
#define SPCv_MSGU_CFG_TABLE_NONFATAL_DUMP	 0x80
/* Inbound doorbell bit7 SPCV */
#define SPCV_MSGU_CFG_TABLE_TRANSFER_DEBUG_INFO  0x80
#define MAIN_MERRDCTO_MERRDCES		         0xA0/* DWORD 0x28) */

struct pm8001_dispatch {
	char *name;
	int (*chip_init)(struct pm8001_hba_info *pm8001_ha);
	void (*chip_post_init)(struct pm8001_hba_info *pm8001_ha);
	int (*chip_soft_rst)(struct pm8001_hba_info *pm8001_ha);
	void (*chip_rst)(struct pm8001_hba_info *pm8001_ha);
	int (*chip_ioremap)(struct pm8001_hba_info *pm8001_ha);
	void (*chip_iounmap)(struct pm8001_hba_info *pm8001_ha);
	irqreturn_t (*isr)(struct pm8001_hba_info *pm8001_ha, u8 vec);
	u32 (*is_our_interrupt)(struct pm8001_hba_info *pm8001_ha);
	int (*isr_process_oq)(struct pm8001_hba_info *pm8001_ha, u8 vec);
	void (*interrupt_enable)(struct pm8001_hba_info *pm8001_ha, u8 vec);
	void (*interrupt_disable)(struct pm8001_hba_info *pm8001_ha, u8 vec);
	void (*make_prd)(struct scatterlist *scatter, int nr, void *prd);
	int (*smp_req)(struct pm8001_hba_info *pm8001_ha,
		struct pm8001_ccb_info *ccb);
	int (*ssp_io_req)(struct pm8001_hba_info *pm8001_ha,
		struct pm8001_ccb_info *ccb);
	int (*sata_req)(struct pm8001_hba_info *pm8001_ha,
		struct pm8001_ccb_info *ccb);
	int (*phy_start_req)(struct pm8001_hba_info *pm8001_ha,	u8 phy_id);
	int (*phy_stop_req)(struct pm8001_hba_info *pm8001_ha, u8 phy_id);
	int (*reg_dev_req)(struct pm8001_hba_info *pm8001_ha,
		struct pm8001_device *pm8001_dev, u32 flag);
	int (*dereg_dev_req)(struct pm8001_hba_info *pm8001_ha, u32 device_id);
	int (*phy_ctl_req)(struct pm8001_hba_info *pm8001_ha,
		u32 phy_id, u32 phy_op);
	int (*task_abort)(struct pm8001_hba_info *pm8001_ha,
		struct pm8001_ccb_info *ccb);
	int (*ssp_tm_req)(struct pm8001_hba_info *pm8001_ha,
		struct pm8001_ccb_info *ccb, struct sas_tmf_task *tmf);
	int (*get_nvmd_req)(struct pm8001_hba_info *pm8001_ha, void *payload);
	int (*set_nvmd_req)(struct pm8001_hba_info *pm8001_ha, void *payload);
	int (*fw_flash_update_req)(struct pm8001_hba_info *pm8001_ha,
		void *payload);
	int (*set_dev_state_req)(struct pm8001_hba_info *pm8001_ha,
		struct pm8001_device *pm8001_dev, u32 state);
	int (*sas_diag_start_end_req)(struct pm8001_hba_info *pm8001_ha,
		u32 state);
	int (*sas_diag_execute_req)(struct pm8001_hba_info *pm8001_ha,
		u32 state);
	int (*sas_re_init_req)(struct pm8001_hba_info *pm8001_ha);
	int (*fatal_errors)(struct pm8001_hba_info *pm8001_ha);
	void (*hw_event_ack_req)(struct pm8001_hba_info *pm8001_ha,
		u32 Qnum, u32 SEA, u32 port_id, u32 phyId, u32 param0,
		u32 param1);
};

struct pm8001_chip_info {
	u32     encrypt;
	u32	n_phy;
	const struct pm8001_dispatch	*dispatch;
};
#define PM8001_CHIP_DISP	(pm8001_ha->chip->dispatch)

struct pm8001_port {
	struct asd_sas_port	sas_port;
	u8			port_attached;
	u16			wide_port_phymap;
	u8			port_state;
	u8			port_id;
	struct list_head	list;
};

struct pm8001_phy {
	struct pm8001_hba_info	*pm8001_ha;
	struct pm8001_port	*port;
	struct asd_sas_phy	sas_phy;
	struct sas_identify	identify;
	struct scsi_device	*sdev;
	u64			dev_sas_addr;
	u32			phy_type;
	struct completion	*enable_completion;
	u32			frame_rcvd_size;
	u8			frame_rcvd[32];
	u8			phy_attached;
	u8			phy_state;
	enum sas_linkrate	minimum_linkrate;
	enum sas_linkrate	maximum_linkrate;
	struct completion	*reset_completion;
	bool			port_reset_status;
	bool			reset_success;
};

/* port reset status */
#define PORT_RESET_SUCCESS	0x00
#define PORT_RESET_TMO		0x01

struct pm8001_device {
	enum sas_device_type	dev_type;
	struct domain_device	*sas_device;
	u32			attached_phy;
	u32			id;
	struct completion	*dcompletion;
	struct completion	*setds_completion;
	u32			device_id;
	atomic_t		running_req;
};

struct pm8001_prd_imt {
	__le32			len;
	__le32			e;
};

struct pm8001_prd {
	__le64			addr;		/* 64-bit buffer address */
	struct pm8001_prd_imt	im_len;		/* 64-bit length */
} __attribute__ ((packed));
/*
 * CCB(Command Control Block)
 */
struct pm8001_ccb_info {
	struct sas_task		*task;
	u32			n_elem;
	u32			ccb_tag;
	dma_addr_t		ccb_dma_handle;
	struct pm8001_device	*device;
	struct pm8001_prd	*buf_prd;
	struct fw_control_ex	*fw_control_context;
	u8			open_retry;
};

struct mpi_mem {
	void			*virt_ptr;
	dma_addr_t		phys_addr;
	u32			phys_addr_hi;
	u32			phys_addr_lo;
	u32			total_len;
	u32			num_elements;
	u32			element_size;
	u32			alignment;
};

struct mpi_mem_req {
	/* The number of element in the  mpiMemory array */
	u32			count;
	/* The array of structures that define memroy regions*/
	struct mpi_mem		region[USI_MAX_MEMCNT];
};

struct encrypt {
	u32	cipher_mode;
	u32	sec_mode;
	u32	status;
	u32	flag;
};

struct sas_phy_attribute_table {
	u32	phystart1_16[16];
	u32	outbound_hw_event_pid1_16[16];
};

union main_cfg_table {
	struct {
	u32			signature;
	u32			interface_rev;
	u32			firmware_rev;
	u32			max_out_io;
	u32			max_sgl;
	u32			ctrl_cap_flag;
	u32			gst_offset;
	u32			inbound_queue_offset;
	u32			outbound_queue_offset;
	u32			inbound_q_nppd_hppd;
	u32			outbound_hw_event_pid0_3;
	u32			outbound_hw_event_pid4_7;
	u32			outbound_ncq_event_pid0_3;
	u32			outbound_ncq_event_pid4_7;
	u32			outbound_tgt_ITNexus_event_pid0_3;
	u32			outbound_tgt_ITNexus_event_pid4_7;
	u32			outbound_tgt_ssp_event_pid0_3;
	u32			outbound_tgt_ssp_event_pid4_7;
	u32			outbound_tgt_smp_event_pid0_3;
	u32			outbound_tgt_smp_event_pid4_7;
	u32			upper_event_log_addr;
	u32			lower_event_log_addr;
	u32			event_log_size;
	u32			event_log_option;
	u32			upper_iop_event_log_addr;
	u32			lower_iop_event_log_addr;
	u32			iop_event_log_size;
	u32			iop_event_log_option;
	u32			fatal_err_interrupt;
	u32			fatal_err_dump_offset0;
	u32			fatal_err_dump_length0;
	u32			fatal_err_dump_offset1;
	u32			fatal_err_dump_length1;
	u32			hda_mode_flag;
	u32			anolog_setup_table_offset;
	u32			rsvd[4];
	} pm8001_tbl;

	struct {
	u32			signature;
	u32			interface_rev;
	u32			firmware_rev;
	u32			max_out_io;
	u32			max_sgl;
	u32			ctrl_cap_flag;
	u32			gst_offset;
	u32			inbound_queue_offset;
	u32			outbound_queue_offset;
	u32			inbound_q_nppd_hppd;
	u32			rsvd[8];
	u32			crc_core_dump;
	u32			rsvd1;
	u32			upper_event_log_addr;
	u32			lower_event_log_addr;
	u32			event_log_size;
	u32			event_log_severity;
	u32			upper_pcs_event_log_addr;
	u32			lower_pcs_event_log_addr;
	u32			pcs_event_log_size;
	u32			pcs_event_log_severity;
	u32			fatal_err_interrupt;
	u32			fatal_err_dump_offset0;
	u32			fatal_err_dump_length0;
	u32			fatal_err_dump_offset1;
	u32			fatal_err_dump_length1;
	u32			gpio_led_mapping;
	u32			analog_setup_table_offset;
	u32			int_vec_table_offset;
	u32			phy_attr_table_offset;
	u32			port_recovery_timer;
	u32			interrupt_reassertion_delay;
	u32			fatal_n_non_fatal_dump;	        /* 0x28 */
	u32			ila_version;
	u32			inc_fw_version;
	} pm80xx_tbl;
};

union general_status_table {
	struct {
	u32			gst_len_mpistate;
	u32			iq_freeze_state0;
	u32			iq_freeze_state1;
	u32			msgu_tcnt;
	u32			iop_tcnt;
	u32			rsvd;
	u32			phy_state[8];
	u32			gpio_input_val;
	u32			rsvd1[2];
	u32			recover_err_info[8];
	} pm8001_tbl;
	struct {
	u32			gst_len_mpistate;
	u32			iq_freeze_state0;
	u32			iq_freeze_state1;
	u32			msgu_tcnt;
	u32			iop_tcnt;
	u32			rsvd[9];
	u32			gpio_input_val;
	u32			rsvd1[2];
	u32			recover_err_info[8];
	} pm80xx_tbl;
};
struct inbound_queue_table {
	u32			element_pri_size_cnt;
	u32			upper_base_addr;
	u32			lower_base_addr;
	u32			ci_upper_base_addr;
	u32			ci_lower_base_addr;
	u32			pi_pci_bar;
	u32			pi_offset;
	u32			total_length;
	void			*base_virt;
	void			*ci_virt;
	u32			reserved;
	__le32			consumer_index;
	u32			producer_idx;
	spinlock_t		iq_lock;
};
struct outbound_queue_table {
	u32			element_size_cnt;
	u32			upper_base_addr;
	u32			lower_base_addr;
	void			*base_virt;
	u32			pi_upper_base_addr;
	u32			pi_lower_base_addr;
	u32			ci_pci_bar;
	u32			ci_offset;
	u32			total_length;
	void			*pi_virt;
	u32			interrup_vec_cnt_delay;
	u32			dinterrup_to_pci_offset;
	__le32			producer_index;
	u32			consumer_idx;
	spinlock_t		oq_lock;
	unsigned long		lock_flags;
};
struct pm8001_hba_memspace {
	void __iomem  		*memvirtaddr;
	u64			membase;
	u32			memsize;
};
struct isr_param {
	struct pm8001_hba_info *drv_inst;
	u32 irq_id;
};
struct pm8001_hba_info {
	char			name[PM8001_NAME_LENGTH];
	struct list_head	list;
	unsigned long		flags;
	spinlock_t		lock;/* host-wide lock */
	spinlock_t		bitmap_lock;
	struct pci_dev		*pdev;/* our device */
	struct device		*dev;
	struct pm8001_hba_memspace io_mem[6];
	struct mpi_mem_req	memoryMap;
	struct encrypt		encrypt_info; /* support encryption */
	struct forensic_data	forensic_info;
	u32			fatal_bar_loc;
	u32			forensic_last_offset;
	u32			fatal_forensic_shift_offset;
	u32			forensic_fatal_step;
	u32			forensic_preserved_accumulated_transfer;
	u32			evtlog_ib_offset;
	u32			evtlog_ob_offset;
	void __iomem	*msg_unit_tbl_addr;/*Message Unit Table Addr*/
	void __iomem	*main_cfg_tbl_addr;/*Main Config Table Addr*/
	void __iomem	*general_stat_tbl_addr;/*General Status Table Addr*/
	void __iomem	*inbnd_q_tbl_addr;/*Inbound Queue Config Table Addr*/
	void __iomem	*outbnd_q_tbl_addr;/*Outbound Queue Config Table Addr*/
	void __iomem	*pspa_q_tbl_addr;
			/*MPI SAS PHY attributes Queue Config Table Addr*/
	void __iomem	*ivt_tbl_addr; /*MPI IVT Table Addr */
	void __iomem	*fatal_tbl_addr; /*MPI IVT Table Addr */
	union main_cfg_table	main_cfg_tbl;
	union general_status_table	gs_tbl;
	struct inbound_queue_table	inbnd_q_tbl[PM8001_MAX_INB_NUM];
	struct outbound_queue_table	outbnd_q_tbl[PM8001_MAX_OUTB_NUM];
	struct sas_phy_attribute_table	phy_attr_table;
					/* MPI SAS PHY attributes */
	u8			sas_addr[SAS_ADDR_SIZE];
	struct sas_ha_struct	*sas;/* SCSI/SAS glue */
	struct Scsi_Host	*shost;
	u32			chip_id;
	const struct pm8001_chip_info	*chip;
	struct completion	*nvmd_completion;
	unsigned long		*rsvd_tags;
	struct pm8001_phy	phy[PM8001_MAX_PHYS];
	struct pm8001_port	port[PM8001_MAX_PHYS];
	u32			id;
	u32			irq;
	u32			iomb_size; /* SPC and SPCV IOMB size */
	struct pm8001_device	*devices;
	struct pm8001_ccb_info	*ccb_info;
	u32			ccb_count;
#ifdef PM8001_USE_MSIX
	int			number_of_intr;/*will be used in remove()*/
	char			intr_drvname[PM8001_MAX_MSIX_VEC]
				[PM8001_NAME_LENGTH+1+3+1];
#endif
#ifdef PM8001_USE_TASKLET
	struct tasklet_struct	tasklet[PM8001_MAX_MSIX_VEC];
#endif
	u32			logging_level;
	u32			link_rate;
	u32			fw_status;
	u32			smp_exp_mode;
	bool			controller_fatal_error;
	const struct firmware 	*fw_image;
	struct isr_param irq_vector[PM8001_MAX_MSIX_VEC];
	u32			non_fatal_count;
	u32			non_fatal_read_length;
	u32 max_q_num;
	u32 ib_offset;
	u32 ob_offset;
	u32 ci_offset;
	u32 pi_offset;
	u32 max_memcnt;
};

struct pm8001_work {
	struct work_struct work;
	struct pm8001_hba_info *pm8001_ha;
	void *data;
	int handler;
};

struct pm8001_fw_image_header {
	u8 vender_id[8];
	u8 product_id;
	u8 hardware_rev;
	u8 dest_partition;
	u8 reserved;
	u8 fw_rev[4];
	__be32  image_length;
	__be32 image_crc;
	__be32 startup_entry;
} __attribute__((packed, aligned(4)));


/**
 * FW Flash Update status values
 */
#define FLASH_UPDATE_COMPLETE_PENDING_REBOOT	0x00
#define FLASH_UPDATE_IN_PROGRESS		0x01
#define FLASH_UPDATE_HDR_ERR			0x02
#define FLASH_UPDATE_OFFSET_ERR			0x03
#define FLASH_UPDATE_CRC_ERR			0x04
#define FLASH_UPDATE_LENGTH_ERR			0x05
#define FLASH_UPDATE_HW_ERR			0x06
#define FLASH_UPDATE_DNLD_NOT_SUPPORTED		0x10
#define FLASH_UPDATE_DISABLED			0x11

/* Device states */
#define DS_OPERATIONAL				0x01
#define DS_PORT_IN_RESET			0x02
#define DS_IN_RECOVERY				0x03
#define DS_IN_ERROR				0x04
#define DS_NON_OPERATIONAL			0x07

/**
 * brief param structure for firmware flash update.
 */
struct fw_flash_updata_info {
	u32			cur_image_offset;
	u32			cur_image_len;
	u32			total_image_len;
	struct pm8001_prd	sgl;
};

struct fw_control_info {
	u32			retcode;/*ret code (status)*/
	u32			phase;/*ret code phase*/
	u32			phaseCmplt;/*percent complete for the current
	update phase */
	u32			version;/*Hex encoded firmware version number*/
	u32			offset;/*Used for downloading firmware	*/
	u32			len; /*len of buffer*/
	u32			size;/* Used in OS VPD and Trace get size
	operations.*/
	u32			reserved;/* padding required for 64 bit
	alignment */
	u8			buffer[];/* Start of buffer */
};
struct fw_control_ex {
	struct fw_control_info *fw_control;
	void			*buffer;/* keep buffer pointer to be
	freed when the response comes*/
	void			*virtAddr;/* keep virtual address of the data */
	void			*usrAddr;/* keep virtual address of the
	user data */
	dma_addr_t		phys_addr;
	u32			len; /* len of buffer  */
	void			*payload; /* pointer to IOCTL Payload */
	u8			inProgress;/*if 1 - the IOCTL request is in
	progress */
	void			*param1;
	void			*param2;
	void			*param3;
};

/* pm8001 workqueue */
extern struct workqueue_struct *pm8001_wq;

/******************** function prototype *********************/
int pm8001_tag_alloc(struct pm8001_hba_info *pm8001_ha, u32 *tag_out);
u32 pm8001_get_ncq_tag(struct sas_task *task, u32 *tag);
void pm8001_ccb_task_free(struct pm8001_hba_info *pm8001_ha,
			  struct pm8001_ccb_info *ccb);
int pm8001_phy_control(struct asd_sas_phy *sas_phy, enum phy_func func,
	void *funcdata);
void pm8001_scan_start(struct Scsi_Host *shost);
int pm8001_scan_finished(struct Scsi_Host *shost, unsigned long time);
int pm8001_queue_command(struct sas_task *task, gfp_t gfp_flags);
int pm8001_abort_task(struct sas_task *task);
int pm8001_clear_task_set(struct domain_device *dev, u8 *lun);
int pm8001_dev_found(struct domain_device *dev);
void pm8001_dev_gone(struct domain_device *dev);
int pm8001_lu_reset(struct domain_device *dev, u8 *lun);
int pm8001_I_T_nexus_reset(struct domain_device *dev);
int pm8001_I_T_nexus_event_handler(struct domain_device *dev);
int pm8001_query_task(struct sas_task *task);
void pm8001_port_formed(struct asd_sas_phy *sas_phy);
void pm8001_open_reject_retry(
	struct pm8001_hba_info *pm8001_ha,
	struct sas_task *task_to_close,
	struct pm8001_device *device_to_close);
int pm8001_mem_alloc(struct pci_dev *pdev, void **virt_addr,
	dma_addr_t *pphys_addr, u32 *pphys_addr_hi, u32 *pphys_addr_lo,
	u32 mem_size, u32 align);

void pm8001_chip_iounmap(struct pm8001_hba_info *pm8001_ha);
int pm8001_mpi_build_cmd(struct pm8001_hba_info *pm8001_ha,
			u32 q_index, u32 opCode, void *payload, size_t nb,
			u32 responseQueue);
int pm8001_mpi_msg_free_get(struct inbound_queue_table *circularQ,
				u16 messageSize, void **messagePtr);
u32 pm8001_mpi_msg_free_set(struct pm8001_hba_info *pm8001_ha, void *pMsg,
			struct outbound_queue_table *circularQ, u8 bc);
u32 pm8001_mpi_msg_consume(struct pm8001_hba_info *pm8001_ha,
			struct outbound_queue_table *circularQ,
			void **messagePtr1, u8 *pBC);
int pm8001_chip_set_dev_state_req(struct pm8001_hba_info *pm8001_ha,
			struct pm8001_device *pm8001_dev, u32 state);
int pm8001_chip_fw_flash_update_req(struct pm8001_hba_info *pm8001_ha,
					void *payload);
int pm8001_chip_fw_flash_update_build(struct pm8001_hba_info *pm8001_ha,
					void *fw_flash_updata_info, u32 tag);
int pm8001_chip_set_nvmd_req(struct pm8001_hba_info *pm8001_ha, void *payload);
int pm8001_chip_get_nvmd_req(struct pm8001_hba_info *pm8001_ha, void *payload);
int pm8001_chip_ssp_tm_req(struct pm8001_hba_info *pm8001_ha,
				struct pm8001_ccb_info *ccb,
				struct sas_tmf_task *tmf);
int pm8001_chip_abort_task(struct pm8001_hba_info *pm8001_ha,
				struct pm8001_ccb_info *ccb);
int pm8001_chip_dereg_dev_req(struct pm8001_hba_info *pm8001_ha, u32 device_id);
void pm8001_chip_make_sg(struct scatterlist *scatter, int nr, void *prd);
void pm8001_work_fn(struct work_struct *work);
int pm8001_handle_event(struct pm8001_hba_info *pm8001_ha,
					void *data, int handler);
void pm8001_mpi_set_dev_state_resp(struct pm8001_hba_info *pm8001_ha,
							void *piomb);
void pm8001_mpi_set_nvmd_resp(struct pm8001_hba_info *pm8001_ha,
							void *piomb);
void pm8001_mpi_get_nvmd_resp(struct pm8001_hba_info *pm8001_ha,
							void *piomb);
int pm8001_mpi_local_phy_ctl(struct pm8001_hba_info *pm8001_ha,
							void *piomb);
void pm8001_get_lrate_mode(struct pm8001_phy *phy, u8 link_rate);
void pm8001_get_attached_sas_addr(struct pm8001_phy *phy, u8 *sas_addr);
void pm8001_bytes_dmaed(struct pm8001_hba_info *pm8001_ha, int i);
int pm8001_mpi_reg_resp(struct pm8001_hba_info *pm8001_ha, void *piomb);
int pm8001_mpi_dereg_resp(struct pm8001_hba_info *pm8001_ha, void *piomb);
int pm8001_mpi_fw_flash_update_resp(struct pm8001_hba_info *pm8001_ha,
							void *piomb);
int pm8001_mpi_general_event(struct pm8001_hba_info *pm8001_ha, void *piomb);
int pm8001_mpi_task_abort_resp(struct pm8001_hba_info *pm8001_ha, void *piomb);
struct sas_task *pm8001_alloc_task(void);
void pm8001_free_task(struct sas_task *task);
void pm8001_tag_free(struct pm8001_hba_info *pm8001_ha, u32 tag);
struct pm8001_device *pm8001_find_dev(struct pm8001_hba_info *pm8001_ha,
					u32 device_id);
int pm80xx_set_thermal_config(struct pm8001_hba_info *pm8001_ha);

int pm8001_bar4_shift(struct pm8001_hba_info *pm8001_ha, u32 shiftValue);
void pm8001_set_phy_profile(struct pm8001_hba_info *pm8001_ha,
	u32 length, u8 *buf);
void pm8001_set_phy_profile_single(struct pm8001_hba_info *pm8001_ha,
		u32 phy, u32 length, u32 *buf);
int pm80xx_bar4_shift(struct pm8001_hba_info *pm8001_ha, u32 shiftValue);
ssize_t pm80xx_get_fatal_dump(struct device *cdev,
		struct device_attribute *attr, char *buf);
ssize_t pm80xx_get_non_fatal_dump(struct device *cdev,
		struct device_attribute *attr, char *buf);
ssize_t pm8001_get_gsm_dump(struct device *cdev, u32, char *buf);
int pm80xx_fatal_errors(struct pm8001_hba_info *pm8001_ha);
void pm8001_free_dev(struct pm8001_device *pm8001_dev);
/* ctl shared API */
extern const struct attribute_group *pm8001_host_groups[];

#define PM8001_INVALID_TAG	((u32)-1)

/*
 * Allocate a new tag and return the corresponding ccb after initializing it.
 */
static inline struct pm8001_ccb_info *
pm8001_ccb_alloc(struct pm8001_hba_info *pm8001_ha,
		 struct pm8001_device *dev, struct sas_task *task)
{
	struct pm8001_ccb_info *ccb;
	struct request *rq = NULL;
	u32 tag;

	if (task)
		rq = sas_task_find_rq(task);

	if (rq) {
		tag = rq->tag + PM8001_RESERVE_SLOT;
	} else if (pm8001_tag_alloc(pm8001_ha, &tag)) {
		pm8001_dbg(pm8001_ha, FAIL, "Failed to allocate a tag\n");
		return NULL;
	}

	ccb = &pm8001_ha->ccb_info[tag];
	ccb->task = task;
	ccb->n_elem = 0;
	ccb->ccb_tag = tag;
	ccb->device = dev;
	ccb->fw_control_context = NULL;
	ccb->open_retry = 0;

	return ccb;
}

/*
 * Free the tag of an initialized ccb.
 */
static inline void pm8001_ccb_free(struct pm8001_hba_info *pm8001_ha,
				   struct pm8001_ccb_info *ccb)
{
	u32 tag = ccb->ccb_tag;

	/*
	 * Cleanup the ccb to make sure that a manual scan of the adapter
	 * ccb_info array can detect ccb's that are in use.
	 * C.f. pm8001_open_reject_retry()
	 */
	ccb->task = NULL;
	ccb->ccb_tag = PM8001_INVALID_TAG;
	ccb->device = NULL;
	ccb->fw_control_context = NULL;

	pm8001_tag_free(pm8001_ha, tag);
}

static inline void pm8001_ccb_task_free_done(struct pm8001_hba_info *pm8001_ha,
					     struct pm8001_ccb_info *ccb)
{
	struct sas_task *task = ccb->task;

	pm8001_ccb_task_free(pm8001_ha, ccb);
	smp_mb(); /*in order to force CPU ordering*/
	task->task_done(task);
}
void pm8001_setds_completion(struct domain_device *dev);
void pm8001_tmf_aborted(struct sas_task *task);

#endif

