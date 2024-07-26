/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2023 Synopsys, Inc. (www.synopsys.com)
 */

#ifndef __SNPS_ACCEL_RPROC_H__
#define __SNPS_ACCEL_RPROC_H__

#include <linux/device.h>
#include <linux/snps_arcsync.h>

struct snps_accel_rproc;

/**
 * struct snps_accel_rproc_mem - internal memory region structure
 * @virt_addr: virtual address of the memory region
 * @phys_addr: CPU address used to access the memory region
 * @dev_addr: device address of the memory region from accelerator view
 * @size: size of the memory region
 */
struct snps_accel_rproc_mem {
	void *virt_addr;
	phys_addr_t phys_addr;
	u32 dev_addr;
	size_t size;
};

/**
 * struct snps_accel_rproc_dev_data - device callbacks for the VPX/NPX remote processors
 * @setup_cluster: perform additional cluster setup (for NPX setup CLN)
 * @start_core: set_ivt/powerup/reset/start one VPX/NPX core
 * @stop_core: stop VPX/NPX core
 */
struct snps_accel_rproc_dev_data {
	int (*setup_cluster)(struct snps_accel_rproc *aproc);
	int (*start_core)(struct snps_accel_rproc *aproc);
	int (*stop_core)(struct snps_accel_rproc *aproc);
};

#define NPU_DEF_NUM_SLICES		16
#define NPU_DEF_CSM_BANKS_PER_GRP	8
#define NPU_DEF_NUM_STU_PER_GRP		2
#define NPU_DEF_SAFETY_LEVEL		1
#define NPU_DEF_CSM_SIZE		0x4000000
#define NPX_DEF_CLN_MAP_START		0xE0000000


/**
 * struct snps_npu_cn - NPU Cluster Network properties
 * @num_slices: NPU cluster slices num
 * @num_grps: number of groups of slices in cluster
 * @slice_per_grp: number of L1 slices per group
 * @csm_banks_per_grp: number of CSM banks
 * @csm_size: full size of the NPX Cluster Shared Memory
 * @stu_per_grp: number of STU lines per group
 * @safety_lvl: functional safety support
 * @map_start: start offset of DMI mappings in the Cluster Network address space
 */
struct snps_npu_cn {
	u32 num_slices;
	u32 num_grps;
	u32 slice_per_grp;
	u32 csm_banks_per_grp;
	u32 csm_size;
	u32 stu_per_grp;
	u32 safety_lvl;
	u32 map_start;
};

/**
 * struct snps_accel_rproc_ctrl_fn - struct with pointers for control functions
 * needed by the rproc driver. The functions themselves are provided by the
 * special external driver, the ARCSync driver
 */
struct snps_accel_rproc_ctrl_fn {
	int (*clk_ctrl)(struct device *dev, u32 clid, u32 cid, u32 val);
	int (*power_ctrl)(struct device *dev, u32 clid, u32 cid, u32 cmd);
	int (*reset)(struct device *dev, u32 clid, u32 cid, u32 cmd);
	int (*start)(struct device *dev, u32 clid, u32 cid);
	int (*halt)(struct device *dev, u32 clid, u32 cid);
	int (*set_ivt)(struct device *dev, u32 clid, u32 cid, phys_addr_t ivt_addr);
	int (*get_status)(struct device *device, u32 clid, u32 cid);
	int (*reset_cluster_group)(struct device *dev, u32 clid, u32 grp, u32 cmd);
	int (*clk_ctrl_cluster_group)(struct device *dev, u32 clid, u32 grp, u32 cmd);
	int (*power_ctrl_cluster_group)(struct device *dev, u32 clid, u32 grp, u32 cmd);
};

/**
 * struct snps_accel_rproc_ctrl - control unit data from the ARCSync driver
 * @dev: pointer to the control driver (ARCSync driver) device struct
 * @fn: struct with pointers for control functions needed by the rproc driver
 * @ver: ARCsync unit version
 * @has_pmu: flag indicating the presence of a Power Management Unit in the control unit
 */
struct snps_accel_rproc_ctrl {
	struct device *dev;
	struct snps_accel_rproc_ctrl_fn fn;
	u32 ver;
	u32 has_pmu;
};

/**
 * struct snps_accel_rproc - remoteproc device instance
 * @rproc: rproc handle
 * @device: rproc device struct
 * @num_mems: number of mem regions to map before loading elf
 * @first_load: flag that indicates first start of processors
 * @cluster_id: cluster id of the processor to start as it seen by ARCSync
 * @num_cores_start: number of cores to work with (power up/reset/start)
 * @core_id: core number (or array of core numbers) inside the cluster to start
 * @ivt_base: base address of the vector table (from elf file)
 * @cn: struct with the Cluster Network properties
 * @ctrl: struct with the control unit (ARCSync) functions and data
 * @mem: internal memory regions data
 * @data: processor specific data and config funcs
 */
struct snps_accel_rproc {
	struct rproc *rproc;
	struct device *device;
	u32 num_mems;
	u32 first_load;
	u32 cluster_id;
	s32 num_cores_start;
	u32 *core_id;
	u64 ivt_base;
	struct snps_npu_cn cn;
	struct snps_accel_rproc_ctrl ctrl;
	struct snps_accel_rproc_mem *mem;
	const struct snps_accel_rproc_dev_data *data;
};

int npx_setup_cluster_default(struct snps_accel_rproc *npu);

#endif
