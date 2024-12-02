/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef _ABI_GUC_KLVS_ABI_H
#define _ABI_GUC_KLVS_ABI_H

#include <linux/types.h>

/**
 * DOC: GuC KLV
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 | 31:16 | **KEY** - KLV key identifier                                 |
 *  |   |       |   - `GuC Self Config KLVs`_                                  |
 *  |   |       |                                                              |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | **LEN** - length of VALUE (in 32bit dwords)                  |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 |  31:0 | **VALUE** - actual value of the KLV (format depends on KEY)  |
 *  +---+-------+                                                              |
 *  |...|       |                                                              |
 *  +---+-------+                                                              |
 *  | n |  31:0 |                                                              |
 *  +---+-------+--------------------------------------------------------------+
 */

#define GUC_KLV_LEN_MIN				1u
#define GUC_KLV_0_KEY				(0xffffu << 16)
#define GUC_KLV_0_LEN				(0xffffu << 0)
#define GUC_KLV_n_VALUE				(0xffffffffu << 0)

/**
 * DOC: GuC Self Config KLVs
 *
 * `GuC KLV`_ keys available for use with HOST2GUC_SELF_CFG_.
 *
 * _`GUC_KLV_SELF_CFG_H2G_CTB_ADDR` : 0x0902
 *      Refers to 64 bit Global Gfx address of H2G `CT Buffer`_.
 *      Should be above WOPCM address but below APIC base address for native mode.
 *
 * _`GUC_KLV_SELF_CFG_H2G_CTB_DESCRIPTOR_ADDR` : 0x0903
 *      Refers to 64 bit Global Gfx address of H2G `CTB Descriptor`_.
 *      Should be above WOPCM address but below APIC base address for native mode.
 *
 * _`GUC_KLV_SELF_CFG_H2G_CTB_SIZE` : 0x0904
 *      Refers to size of H2G `CT Buffer`_ in bytes.
 *      Should be a multiple of 4K.
 *
 * _`GUC_KLV_SELF_CFG_G2H_CTB_ADDR` : 0x0905
 *      Refers to 64 bit Global Gfx address of G2H `CT Buffer`_.
 *      Should be above WOPCM address but below APIC base address for native mode.
 *
 * _`GUC_KLV_SELF_CFG_G2H_CTB_DESCRIPTOR_ADDR` : 0x0906
 *      Refers to 64 bit Global Gfx address of G2H `CTB Descriptor`_.
 *      Should be above WOPCM address but below APIC base address for native mode.
 *
 * _`GUC_KLV_SELF_CFG_G2H_CTB_SIZE` : 0x0907
 *      Refers to size of G2H `CT Buffer`_ in bytes.
 *      Should be a multiple of 4K.
 */

#define GUC_KLV_SELF_CFG_H2G_CTB_ADDR_KEY		0x0902
#define GUC_KLV_SELF_CFG_H2G_CTB_ADDR_LEN		2u

#define GUC_KLV_SELF_CFG_H2G_CTB_DESCRIPTOR_ADDR_KEY	0x0903
#define GUC_KLV_SELF_CFG_H2G_CTB_DESCRIPTOR_ADDR_LEN	2u

#define GUC_KLV_SELF_CFG_H2G_CTB_SIZE_KEY		0x0904
#define GUC_KLV_SELF_CFG_H2G_CTB_SIZE_LEN		1u

#define GUC_KLV_SELF_CFG_G2H_CTB_ADDR_KEY		0x0905
#define GUC_KLV_SELF_CFG_G2H_CTB_ADDR_LEN		2u

#define GUC_KLV_SELF_CFG_G2H_CTB_DESCRIPTOR_ADDR_KEY	0x0906
#define GUC_KLV_SELF_CFG_G2H_CTB_DESCRIPTOR_ADDR_LEN	2u

#define GUC_KLV_SELF_CFG_G2H_CTB_SIZE_KEY		0x0907
#define GUC_KLV_SELF_CFG_G2H_CTB_SIZE_LEN		1u

/*
 * Per context scheduling policy update keys.
 */
enum  {
	GUC_CONTEXT_POLICIES_KLV_ID_EXECUTION_QUANTUM			= 0x2001,
	GUC_CONTEXT_POLICIES_KLV_ID_PREEMPTION_TIMEOUT			= 0x2002,
	GUC_CONTEXT_POLICIES_KLV_ID_SCHEDULING_PRIORITY			= 0x2003,
	GUC_CONTEXT_POLICIES_KLV_ID_PREEMPT_TO_IDLE_ON_QUANTUM_EXPIRY	= 0x2004,
	GUC_CONTEXT_POLICIES_KLV_ID_SLPM_GT_FREQUENCY			= 0x2005,

	GUC_CONTEXT_POLICIES_KLV_NUM_IDS = 5,
};

#endif /* _ABI_GUC_KLVS_ABI_H */
