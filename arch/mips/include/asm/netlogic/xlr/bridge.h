/*
 * Copyright (c) 2003-2012 Broadcom Corporation
 * All Rights Reserved
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the Broadcom
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY BROADCOM ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL BROADCOM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef _ASM_NLM_BRIDGE_H_
#define _ASM_NLM_BRIDGE_H_

#define BRIDGE_DRAM_0_BAR		0
#define BRIDGE_DRAM_1_BAR		1
#define BRIDGE_DRAM_2_BAR		2
#define BRIDGE_DRAM_3_BAR		3
#define BRIDGE_DRAM_4_BAR		4
#define BRIDGE_DRAM_5_BAR		5
#define BRIDGE_DRAM_6_BAR		6
#define BRIDGE_DRAM_7_BAR		7
#define BRIDGE_DRAM_CHN_0_MTR_0_BAR	8
#define BRIDGE_DRAM_CHN_0_MTR_1_BAR	9
#define BRIDGE_DRAM_CHN_0_MTR_2_BAR	10
#define BRIDGE_DRAM_CHN_0_MTR_3_BAR	11
#define BRIDGE_DRAM_CHN_0_MTR_4_BAR	12
#define BRIDGE_DRAM_CHN_0_MTR_5_BAR	13
#define BRIDGE_DRAM_CHN_0_MTR_6_BAR	14
#define BRIDGE_DRAM_CHN_0_MTR_7_BAR	15
#define BRIDGE_DRAM_CHN_1_MTR_0_BAR	16
#define BRIDGE_DRAM_CHN_1_MTR_1_BAR	17
#define BRIDGE_DRAM_CHN_1_MTR_2_BAR	18
#define BRIDGE_DRAM_CHN_1_MTR_3_BAR	19
#define BRIDGE_DRAM_CHN_1_MTR_4_BAR	20
#define BRIDGE_DRAM_CHN_1_MTR_5_BAR	21
#define BRIDGE_DRAM_CHN_1_MTR_6_BAR	22
#define BRIDGE_DRAM_CHN_1_MTR_7_BAR	23
#define BRIDGE_CFG_BAR			24
#define BRIDGE_PHNX_IO_BAR		25
#define BRIDGE_FLASH_BAR		26
#define BRIDGE_SRAM_BAR			27
#define BRIDGE_HTMEM_BAR		28
#define BRIDGE_HTINT_BAR		29
#define BRIDGE_HTPIC_BAR		30
#define BRIDGE_HTSM_BAR			31
#define BRIDGE_HTIO_BAR			32
#define BRIDGE_HTCFG_BAR		33
#define BRIDGE_PCIXCFG_BAR		34
#define BRIDGE_PCIXMEM_BAR		35
#define BRIDGE_PCIXIO_BAR		36
#define BRIDGE_DEVICE_MASK		37
#define BRIDGE_AERR_INTR_LOG1		38
#define BRIDGE_AERR_INTR_LOG2		39
#define BRIDGE_AERR_INTR_LOG3		40
#define BRIDGE_AERR_DEV_STAT		41
#define BRIDGE_AERR1_LOG1		42
#define BRIDGE_AERR1_LOG2		43
#define BRIDGE_AERR1_LOG3		44
#define BRIDGE_AERR1_DEV_STAT		45
#define BRIDGE_AERR_INTR_EN		46
#define BRIDGE_AERR_UPG			47
#define BRIDGE_AERR_CLEAR		48
#define BRIDGE_AERR1_CLEAR		49
#define BRIDGE_SBE_COUNTS		50
#define BRIDGE_DBE_COUNTS		51
#define BRIDGE_BITERR_INT_EN		52

#define BRIDGE_SYS2IO_CREDITS		53
#define BRIDGE_EVNT_CNT_CTRL1		54
#define BRIDGE_EVNT_COUNTER1		55
#define BRIDGE_EVNT_CNT_CTRL2		56
#define BRIDGE_EVNT_COUNTER2		57
#define BRIDGE_RESERVED1		58

#define BRIDGE_DEFEATURE		59
#define BRIDGE_SCRATCH0			60
#define BRIDGE_SCRATCH1			61
#define BRIDGE_SCRATCH2			62
#define BRIDGE_SCRATCH3			63

#endif
