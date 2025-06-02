/* SPDX-License-Identifier: GPL-2.0 */
/*
 * cbe_regs.h
 *
 * This file is intended to hold the various register definitions for CBE
 * on-chip system devices (memory controller, IO controller, etc...)
 *
 * (C) Copyright IBM Corporation 2001,2006
 *
 * Authors: Maximino Aguilar (maguilar@us.ibm.com)
 *          David J. Erb (djerb@us.ibm.com)
 *
 * (c) 2006 Benjamin Herrenschmidt <benh@kernel.crashing.org>, IBM Corp.
 */

#ifndef CBE_REGS_H
#define CBE_REGS_H

#include <asm/cell-pmu.h>

/* Cell page table entries */
#define CBE_IOPTE_PP_W		0x8000000000000000ul /* protection: write */
#define CBE_IOPTE_PP_R		0x4000000000000000ul /* protection: read */
#define CBE_IOPTE_M		0x2000000000000000ul /* coherency required */
#define CBE_IOPTE_SO_R		0x1000000000000000ul /* ordering: writes */
#define CBE_IOPTE_SO_RW		0x1800000000000000ul /* ordering: r & w */
#define CBE_IOPTE_RPN_Mask	0x07fffffffffff000ul /* RPN */
#define CBE_IOPTE_H		0x0000000000000800ul /* cache hint */
#define CBE_IOPTE_IOID_Mask	0x00000000000007fful /* ioid */

#endif /* CBE_REGS_H */
