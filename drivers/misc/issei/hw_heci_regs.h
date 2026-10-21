/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2023-2026 Intel Corporation */
#ifndef _ISSEI_HW_HECI_REGS_H_
#define _ISSEI_HW_HECI_REGS_H_

#include <linux/bits.h>

/* H_CB_WW - Host Circular Buffer (CB) Write Window register */
#define H_CB_WW    0x0
/* H_CSR - Host Control Status register */
#define H_CSR      0x4
#define H_CSR_CBD   GENMASK(31, 24) /* Host Circular Buffer Depth */
#define H_CSR_CBWP  GENMASK(23, 16) /* Host Circular Buffer Write Pointer */
#define H_CSR_CBRP  GENMASK(15, 8)  /* Host Circular Buffer Read Pointer */
#define H_CSR_RST   BIT(4) /* Host Reset */
#define H_CSR_RDY   BIT(3) /* Host Ready */
#define H_CSR_IG    BIT(2) /* Host Interrupt Generate */
#define H_CSR_IS    BIT(1) /* Host Interrupt Status */
#define H_CSR_IE    BIT(0) /* Host Interrupt Enable */
/* FW_CB_RW - FW Circular Buffer Read Window register (read only) */
#define FW_CB_RW   0x8
/* FW_CSR_HA - FW Control Status Host Access register (read only) */
#define FW_CSR_HA  0xC
#define FW_CSR_CBD  GENMASK(31, 24) /* FW CB (Circular Buffer) Depth */
#define FW_CSR_CBWP GENMASK(23, 16) /* FW CB Write Pointer */
#define FW_CSR_CBRP GENMASK(15, 8)  /* FW CB Read Pointer */
#define FW_CSR_RST  BIT(4) /* FW Reset */
#define FW_CSR_RDY  BIT(3) /* FW Ready */
#define FW_CSR_IG   BIT(2) /* FW Interrupt Generate */
#define FW_CSR_IS   BIT(1) /* FW Interrupt Status */
#define FW_CSR_IE   BIT(0) /* FW Interrupt Enable */

#define CB_SLOT_SIZE sizeof(u32) /* Circular Buffer windows size */

#endif /* _ISSEI_HW_HECI_REGS_H_ */
