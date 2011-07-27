/*
 * Copyright 2003-2011 NetLogic Microsystems, Inc. (NetLogic). All rights
 * reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the NetLogic
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
 * THIS SOFTWARE IS PROVIDED BY NETLOGIC ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL NETLOGIC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _ASM_NLM_GPIO_H
#define _ASM_NLM_GPIO_H

#define NETLOGIC_GPIO_INT_EN_REG		0
#define NETLOGIC_GPIO_INPUT_INVERSION_REG	1
#define NETLOGIC_GPIO_IO_DIR_REG		2
#define NETLOGIC_GPIO_IO_DATA_WR_REG		3
#define NETLOGIC_GPIO_IO_DATA_RD_REG		4

#define NETLOGIC_GPIO_SWRESET_REG		8
#define NETLOGIC_GPIO_DRAM1_CNTRL_REG		9
#define NETLOGIC_GPIO_DRAM1_RATIO_REG		10
#define NETLOGIC_GPIO_DRAM1_RESET_REG		11
#define NETLOGIC_GPIO_DRAM1_STATUS_REG		12
#define NETLOGIC_GPIO_DRAM2_CNTRL_REG		13
#define NETLOGIC_GPIO_DRAM2_RATIO_REG		14
#define NETLOGIC_GPIO_DRAM2_RESET_REG		15
#define NETLOGIC_GPIO_DRAM2_STATUS_REG		16

#define NETLOGIC_GPIO_PWRON_RESET_CFG_REG	21
#define NETLOGIC_GPIO_BIST_ALL_GO_STATUS_REG	24
#define NETLOGIC_GPIO_BIST_CPU_GO_STATUS_REG	25
#define NETLOGIC_GPIO_BIST_DEV_GO_STATUS_REG	26

#define NETLOGIC_GPIO_FUSE_BANK_REG		35
#define NETLOGIC_GPIO_CPU_RESET_REG		40
#define NETLOGIC_GPIO_RNG_REG			43

#define NETLOGIC_PWRON_RESET_PCMCIA_BOOT	17
#define NETLOGIC_GPIO_LED_BITMAP	0x1700000
#define NETLOGIC_GPIO_LED_0_SHIFT		20
#define NETLOGIC_GPIO_LED_1_SHIFT		24

#define NETLOGIC_GPIO_LED_OUTPUT_CODE_RESET	0x01
#define NETLOGIC_GPIO_LED_OUTPUT_CODE_HARD_RESET 0x02
#define NETLOGIC_GPIO_LED_OUTPUT_CODE_SOFT_RESET 0x03
#define NETLOGIC_GPIO_LED_OUTPUT_CODE_MAIN	0x04

#endif
