/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Huawei HiNIC PCI Express Linux driver
 * Copyright(c) 2017 Huawei Technologies Co., Ltd
 */

#ifndef HINIC_HW_IO_H
#define HINIC_HW_IO_H

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/semaphore.h>
#include <linux/sizes.h>

#include "hinic_hw_if.h"
#include "hinic_hw_eqs.h"
#include "hinic_hw_wq.h"
#include "hinic_hw_cmdq.h"
#include "hinic_hw_qp.h"

#define HINIC_DB_PAGE_SIZE      SZ_4K
#define HINIC_DB_SIZE           SZ_4M
#define HINIC_HW_WQ_PAGE_SIZE	SZ_4K
#define HINIC_DEFAULT_WQ_PAGE_SIZE SZ_256K

#define HINIC_DB_MAX_AREAS      (HINIC_DB_SIZE / HINIC_DB_PAGE_SIZE)

enum hinic_db_type {
	HINIC_DB_CMDQ_TYPE,
	HINIC_DB_SQ_TYPE,
};

enum hinic_io_path {
	HINIC_CTRL_PATH,
	HINIC_DATA_PATH,
};

struct hinic_free_db_area {
	int             db_idx[HINIC_DB_MAX_AREAS];

	int             alloc_pos;
	int             return_pos;

	int             num_free;

	/* Lock for getting db area */
	struct semaphore        idx_lock;
};

struct hinic_func_to_io {
	struct hinic_hwif       *hwif;
	struct hinic_hwdev      *hwdev;
	struct hinic_ceqs       ceqs;

	struct hinic_wqs        wqs;

	struct hinic_wq         *sq_wq;
	struct hinic_wq         *rq_wq;

	struct hinic_qp         *qps;
	u16                     max_qps;

	u16			sq_depth;
	u16			rq_depth;

	void __iomem            **sq_db;
	void __iomem            *db_base;

	void                    *ci_addr_base;
	dma_addr_t              ci_dma_base;

	struct hinic_free_db_area       free_db_area;

	void __iomem                    *cmdq_db_area[HINIC_MAX_CMDQ_TYPES];

	struct hinic_cmdqs              cmdqs;

	u16			max_vfs;
	struct vf_data_storage	*vf_infos;
	u8			link_status;
};

struct hinic_wq_page_size {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_idx;
	u8	ppf_idx;
	u8	page_size;

	u32	rsvd1;
};

int hinic_set_wq_page_size(struct hinic_hwdev *hwdev, u16 func_idx,
			   u32 page_size);

int hinic_io_create_qps(struct hinic_func_to_io *func_to_io,
			u16 base_qpn, int num_qps,
			struct msix_entry *sq_msix_entries,
			struct msix_entry *rq_msix_entries);

void hinic_io_destroy_qps(struct hinic_func_to_io *func_to_io,
			  int num_qps);

int hinic_io_init(struct hinic_func_to_io *func_to_io,
		  struct hinic_hwif *hwif, u16 max_qps, int num_ceqs,
		  struct msix_entry *ceq_msix_entries);

void hinic_io_free(struct hinic_func_to_io *func_to_io);

#endif
