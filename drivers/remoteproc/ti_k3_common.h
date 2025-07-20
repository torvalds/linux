/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * TI K3 Remote Processor(s) driver common code
 *
 * Refactored out of ti_k3_r5_remoteproc.c, ti_k3_dsp_remoteproc.c and
 * ti_k3_m4_remoteproc.c.
 *
 * ti_k3_r5_remoteproc.c:
 * Copyright (C) 2017-2022 Texas Instruments Incorporated - https://www.ti.com/
 *	Suman Anna <s-anna@ti.com>
 *
 * ti_k3_dsp_remoteproc.c:
 * Copyright (C) 2018-2022 Texas Instruments Incorporated - https://www.ti.com/
 *	Suman Anna <s-anna@ti.com>
 *
 * ti_k3_m4_remoteproc.c:
 * Copyright (C) 2021-2024 Texas Instruments Incorporated - https://www.ti.com/
 *	Hari Nagalla <hnagalla@ti.com>
 */

#ifndef REMOTEPROC_TI_K3_COMMON_H
#define REMOTEPROC_TI_K3_COMMON_H

#define KEYSTONE_RPROC_LOCAL_ADDRESS_MASK	(SZ_16M - 1)

/**
 * struct k3_rproc_mem - internal memory structure
 * @cpu_addr: MPU virtual address of the memory region
 * @bus_addr: Bus address used to access the memory region
 * @dev_addr: Device address of the memory region from remote processor view
 * @size: Size of the memory region
 */
struct k3_rproc_mem {
	void __iomem *cpu_addr;
	phys_addr_t bus_addr;
	u32 dev_addr;
	size_t size;
};

/**
 * struct k3_rproc_mem_data - memory definitions for a remote processor
 * @name: name for this memory entry
 * @dev_addr: device address for the memory entry
 */
struct k3_rproc_mem_data {
	const char *name;
	const u32 dev_addr;
};

/**
 * struct k3_rproc_dev_data - device data structure for a remote processor
 * @mems: pointer to memory definitions for a remote processor
 * @num_mems: number of memory regions in @mems
 * @boot_align_addr: boot vector address alignment granularity
 * @uses_lreset: flag to denote the need for local reset management
 */
struct k3_rproc_dev_data {
	const struct k3_rproc_mem_data *mems;
	u32 num_mems;
	u32 boot_align_addr;
	bool uses_lreset;
};

/**
 * struct k3_rproc - k3 remote processor driver structure
 * @dev: cached device pointer
 * @rproc: remoteproc device handle
 * @mem: internal memory regions data
 * @num_mems: number of internal memory regions
 * @rmem: reserved memory regions data
 * @num_rmems: number of reserved memory regions
 * @reset: reset control handle
 * @data: pointer to DSP-specific device data
 * @tsp: TI-SCI processor control handle
 * @ti_sci: TI-SCI handle
 * @ti_sci_id: TI-SCI device identifier
 * @mbox: mailbox channel handle
 * @client: mailbox client to request the mailbox channel
 * @priv: void pointer to carry any private data
 */
struct k3_rproc {
	struct device *dev;
	struct rproc *rproc;
	struct k3_rproc_mem *mem;
	int num_mems;
	struct k3_rproc_mem *rmem;
	int num_rmems;
	struct reset_control *reset;
	const struct k3_rproc_dev_data *data;
	struct ti_sci_proc *tsp;
	const struct ti_sci_handle *ti_sci;
	u32 ti_sci_id;
	struct mbox_chan *mbox;
	struct mbox_client client;
	void *priv;
};

void k3_rproc_mbox_callback(struct mbox_client *client, void *data);
void k3_rproc_kick(struct rproc *rproc, int vqid);
int k3_rproc_reset(struct k3_rproc *kproc);
int k3_rproc_release(struct k3_rproc *kproc);
int k3_rproc_request_mbox(struct rproc *rproc);
int k3_rproc_prepare(struct rproc *rproc);
int k3_rproc_unprepare(struct rproc *rproc);
int k3_rproc_start(struct rproc *rproc);
int k3_rproc_stop(struct rproc *rproc);
int k3_rproc_attach(struct rproc *rproc);
int k3_rproc_detach(struct rproc *rproc);
struct resource_table *k3_get_loaded_rsc_table(struct rproc *rproc,
					       size_t *rsc_table_sz);
void *k3_rproc_da_to_va(struct rproc *rproc, u64 da, size_t len,
			bool *is_iomem);
int k3_rproc_of_get_memories(struct platform_device *pdev,
			     struct k3_rproc *kproc);
void k3_mem_release(void *data);
int k3_reserved_mem_init(struct k3_rproc *kproc);
void k3_release_tsp(void *data);
#endif /* REMOTEPROC_TI_K3_COMMON_H */
