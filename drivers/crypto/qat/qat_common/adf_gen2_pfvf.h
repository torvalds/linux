/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2021 Intel Corporation */
#ifndef ADF_GEN2_PFVF_H
#define ADF_GEN2_PFVF_H

#include <linux/types.h>
#include "adf_accel_devices.h"

#define ADF_GEN2_ERRSOU3 (0x3A000 + 0x0C)
#define ADF_GEN2_ERRSOU5 (0x3A000 + 0xD8)
#define ADF_GEN2_ERRMSK3 (0x3A000 + 0x1C)
#define ADF_GEN2_ERRMSK5 (0x3A000 + 0xDC)

u32 adf_gen2_get_pf2vf_offset(u32 i);
u32 adf_gen2_get_vf2pf_sources(void __iomem *pmisc_bar);
void adf_gen2_enable_vf2pf_interrupts(void __iomem *pmisc_addr, u32 vf_mask);
void adf_gen2_disable_vf2pf_interrupts(void __iomem *pmisc_addr, u32 vf_mask);

#endif /* ADF_GEN2_PFVF_H */
