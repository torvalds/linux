/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef __HAB_QNX_H
#define __HAB_QNX_H
#include "hab.h"
#include "hab_pipe.h"

#include "hab_qvm_os.h"

struct qvm_channel {
	int be;

	struct hab_pipe *pipe;
	struct hab_pipe_endpoint *pipe_ep;
	struct hab_shared_buf *tx_buf;
	struct hab_shared_buf *rx_buf;
	struct dbg_items *dbg_itms;
	spinlock_t io_lock;

	/* common but only for guest */
	struct guest_shm_factory *guest_factory;
	struct guest_shm_control *guest_ctrl;

	/* cached guest ctrl idx value to prevent trap when accessed */
	uint32_t idx;

	/* Guest VM */
	unsigned int guest_intr;
	unsigned int guest_iid;
	unsigned int factory_addr;
	unsigned int irq;

	/* os-specific part */
	struct qvm_channel_os *os_data;

	/* debug only */
	struct workqueue_struct *wq;
	struct work_data {
		struct work_struct work;
		int data; /* free to modify */
	} wdata;
	char *side_buf; /* to store the contents from hab-pipe */
};

/* This is common but only for guest in HQX */
struct shmem_irq_config {
	unsigned long factory_addr; /* from gvm settings when provided */
	int irq; /* from gvm settings when provided */
};

struct qvm_plugin_info {
	struct shmem_irq_config *pchan_settings;
	int setting_size;
	int curr;
	int probe_cnt;
};

extern struct qvm_plugin_info qvm_priv_info;

/* Shared mem size in each direction for communication pipe */
#define PIPE_SHMEM_SIZE (512 * 1024)

void hab_pipe_reset(struct physical_channel *pchan);
void habhyp_notify(void *commdev);
unsigned long hab_shmem_factory_va(unsigned long factory_addr);
char *hab_shmem_attach(struct qvm_channel *dev, const char *name,
	uint32_t pages);
uint64_t get_guest_ctrl_paddr(struct qvm_channel *dev,
	unsigned long factory_addr, int irq, const char *name, uint32_t pages);
#endif /* __HAB_QNX_H */
