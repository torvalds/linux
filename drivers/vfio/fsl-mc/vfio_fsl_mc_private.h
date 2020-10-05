/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/*
 * Copyright 2013-2016 Freescale Semiconductor Inc.
 * Copyright 2016,2019-2020 NXP
 */

#ifndef VFIO_FSL_MC_PRIVATE_H
#define VFIO_FSL_MC_PRIVATE_H

#define VFIO_FSL_MC_OFFSET_SHIFT    40
#define VFIO_FSL_MC_OFFSET_MASK (((u64)(1) << VFIO_FSL_MC_OFFSET_SHIFT) - 1)

#define VFIO_FSL_MC_OFFSET_TO_INDEX(off) ((off) >> VFIO_FSL_MC_OFFSET_SHIFT)

#define VFIO_FSL_MC_INDEX_TO_OFFSET(index)	\
	((u64)(index) << VFIO_FSL_MC_OFFSET_SHIFT)

struct vfio_fsl_mc_reflck {
	struct kref		kref;
	struct mutex		lock;
};

struct vfio_fsl_mc_region {
	u32			flags;
	u32			type;
	u64			addr;
	resource_size_t		size;
};

struct vfio_fsl_mc_device {
	struct fsl_mc_device		*mc_dev;
	struct notifier_block        nb;
	int				refcnt;
	struct vfio_fsl_mc_region	*regions;
	struct vfio_fsl_mc_reflck   *reflck;
};

#endif /* VFIO_FSL_MC_PRIVATE_H */
