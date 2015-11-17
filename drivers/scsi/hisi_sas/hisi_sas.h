/*
 * Copyright (c) 2015 Linaro Ltd.
 * Copyright (c) 2015 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef _HISI_SAS_H_
#define _HISI_SAS_H_

#include <linux/dmapool.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <scsi/libsas.h>

#define DRV_VERSION "v1.0"

#define HISI_SAS_MAX_PHYS	9
#define HISI_SAS_MAX_QUEUES	32
#define HISI_SAS_QUEUE_SLOTS 512
#define HISI_SAS_MAX_ITCT_ENTRIES 4096
#define HISI_SAS_MAX_DEVICES HISI_SAS_MAX_ITCT_ENTRIES
#define HISI_SAS_COMMAND_ENTRIES 8192

#define HISI_SAS_STATUS_BUF_SZ \
		(sizeof(struct hisi_sas_err_record) + 1024)
#define HISI_SAS_COMMAND_TABLE_SZ \
		(((sizeof(union hisi_sas_command_table)+3)/4)*4)

#define HISI_SAS_NAME_LEN 32


enum dev_status {
	HISI_SAS_DEV_NORMAL,
	HISI_SAS_DEV_EH,
};
struct hisi_sas_phy {
	struct asd_sas_phy	sas_phy;
	u64		dev_sas_addr;
};

struct hisi_sas_port {
	struct asd_sas_port	sas_port;
};

struct hisi_sas_cq {
	struct hisi_hba *hisi_hba;
	int	id;
};

struct hisi_sas_device {
	enum sas_device_type	dev_type;
	u64 device_id;
	u8 dev_status;
};

struct hisi_sas_slot {
};

struct hisi_sas_hw {
	int complete_hdr_size;
};

struct hisi_hba {
	/* This must be the first element, used by SHOST_TO_SAS_HA */
	struct sas_ha_struct *p;

	struct platform_device *pdev;
	void __iomem *regs;
	struct regmap *ctrl;
	u32 ctrl_reset_reg;
	u32 ctrl_reset_sts_reg;
	u32 ctrl_clock_ena_reg;
	u8 sas_addr[SAS_ADDR_SIZE];

	int n_phy;

	struct workqueue_struct *wq;

	int slot_index_count;
	unsigned long *slot_index_tags;

	/* SCSI/SAS glue */
	struct sas_ha_struct sha;
	struct Scsi_Host *shost;

	struct hisi_sas_cq cq[HISI_SAS_MAX_QUEUES];
	struct hisi_sas_phy phy[HISI_SAS_MAX_PHYS];
	struct hisi_sas_port port[HISI_SAS_MAX_PHYS];

	int	queue_count;
	char	*int_names;

	struct dma_pool *sge_page_pool;
	struct hisi_sas_device	devices[HISI_SAS_MAX_DEVICES];
	struct dma_pool *command_table_pool;
	struct dma_pool *status_buffer_pool;
	struct hisi_sas_cmd_hdr	*cmd_hdr[HISI_SAS_MAX_QUEUES];
	dma_addr_t cmd_hdr_dma[HISI_SAS_MAX_QUEUES];
	void *complete_hdr[HISI_SAS_MAX_QUEUES];
	dma_addr_t complete_hdr_dma[HISI_SAS_MAX_QUEUES];
	struct hisi_sas_initial_fis *initial_fis;
	dma_addr_t initial_fis_dma;
	struct hisi_sas_itct *itct;
	dma_addr_t itct_dma;
	struct hisi_sas_iost *iost;
	dma_addr_t iost_dma;
	struct hisi_sas_breakpoint *breakpoint;
	dma_addr_t breakpoint_dma;
	struct hisi_sas_breakpoint *sata_breakpoint;
	dma_addr_t sata_breakpoint_dma;
	struct hisi_sas_slot	*slot_info;
	const struct hisi_sas_hw *hw;	/* Low level hw interface */
};

/* Generic HW DMA host memory structures */
/* Delivery queue header */
struct hisi_sas_cmd_hdr {
	/* dw0 */
	__le32 dw0;

	/* dw1 */
	__le32 dw1;

	/* dw2 */
	__le32 dw2;

	/* dw3 */
	__le32 transfer_tags;

	/* dw4 */
	__le32 data_transfer_len;

	/* dw5 */
	__le32 first_burst_num;

	/* dw6 */
	__le32 sg_len;

	/* dw7 */
	__le32 dw7;

	/* dw8-9 */
	__le64 cmd_table_addr;

	/* dw10-11 */
	__le64 sts_buffer_addr;

	/* dw12-13 */
	__le64 prd_table_addr;

	/* dw14-15 */
	__le64 dif_prd_table_addr;
};

struct hisi_sas_itct {
	__le64 qw0;
	__le64 sas_addr;
	__le64 qw2;
	__le64 qw3;
	__le64 qw4;
	__le64 qw_sata_ncq0_3;
	__le64 qw_sata_ncq7_4;
	__le64 qw_sata_ncq11_8;
	__le64 qw_sata_ncq15_12;
	__le64 qw_sata_ncq19_16;
	__le64 qw_sata_ncq23_20;
	__le64 qw_sata_ncq27_24;
	__le64 qw_sata_ncq31_28;
	__le64 qw_non_ncq_iptt;
	__le64 qw_rsvd0;
	__le64 qw_rsvd1;
};

struct hisi_sas_iost {
	__le64 qw0;
	__le64 qw1;
	__le64 qw2;
	__le64 qw3;
};

struct hisi_sas_err_record {
	/* dw0 */
	__le32 dma_err_type;

	/* dw1 */
	__le32 trans_tx_fail_type;

	/* dw2 */
	__le32 trans_rx_fail_type;

	/* dw3 */
	u32 rsvd;
};

struct hisi_sas_initial_fis {
	struct hisi_sas_err_record err_record;
	struct dev_to_host_fis fis;
	u32 rsvd[3];
};

struct hisi_sas_breakpoint {
	u8	data[128];	/*io128 byte*/
};

struct hisi_sas_sge {
	__le64 addr;
	__le32 page_ctrl_0;
	__le32 page_ctrl_1;
	__le32 data_len;
	__le32 data_off;
};

struct hisi_sas_command_table_smp {
	u8 bytes[44];
};

struct hisi_sas_command_table_stp {
	struct	host_to_dev_fis command_fis;
	u8	dummy[12];
	u8	atapi_cdb[ATAPI_CDB_LEN];
};

#define HISI_SAS_SGE_PAGE_CNT SCSI_MAX_SG_SEGMENTS
struct hisi_sas_sge_page {
	struct hisi_sas_sge sge[HISI_SAS_SGE_PAGE_CNT];
};

struct hisi_sas_command_table_ssp {
	struct ssp_frame_hdr hdr;
	union {
		struct {
			struct ssp_command_iu task;
			u32 prot[6];
		};
		struct ssp_tmf_iu ssp_task;
		struct xfer_rdy_iu xfer_rdy;
		struct ssp_response_iu ssp_res;
	} u;
};

union hisi_sas_command_table {
	struct hisi_sas_command_table_ssp ssp;
	struct hisi_sas_command_table_smp smp;
	struct hisi_sas_command_table_stp stp;
};

#endif
