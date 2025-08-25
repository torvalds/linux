/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved
 */

#ifndef EGM_DEV_H
#define EGM_DEV_H

#include <linux/nvgrace-egm.h>

int nvgrace_gpu_has_egm_property(struct pci_dev *pdev, u64 *pegmpxm);

struct nvgrace_egm_dev *
nvgrace_gpu_create_aux_device(struct pci_dev *pdev, const char *name,
			      u64 egmphys);

#endif /* EGM_DEV_H */
