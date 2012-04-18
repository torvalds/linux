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

#ifndef __NLM_HAL_CPUCONTROL_H__
#define __NLM_HAL_CPUCONTROL_H__

#define CPU_BLOCKID_IFU		0
#define CPU_BLOCKID_ICU		1
#define CPU_BLOCKID_IEU		2
#define CPU_BLOCKID_LSU		3
#define CPU_BLOCKID_MMU		4
#define CPU_BLOCKID_PRF		5
#define CPU_BLOCKID_SCH		7
#define CPU_BLOCKID_SCU		8
#define CPU_BLOCKID_FPU		9
#define CPU_BLOCKID_MAP		10

#define LSU_DEFEATURE		0x304
#define LSU_CERRLOG_REGID	0x09
#define SCHED_DEFEATURE		0x700

/* Offsets of interest from the 'MAP' Block */
#define MAP_THREADMODE			0x00
#define MAP_EXT_EBASE_ENABLE		0x04
#define MAP_CCDI_CONFIG			0x08
#define MAP_THRD0_CCDI_STATUS		0x0c
#define MAP_THRD1_CCDI_STATUS		0x10
#define MAP_THRD2_CCDI_STATUS		0x14
#define MAP_THRD3_CCDI_STATUS		0x18
#define MAP_THRD0_DEBUG_MODE		0x1c
#define MAP_THRD1_DEBUG_MODE		0x20
#define MAP_THRD2_DEBUG_MODE		0x24
#define MAP_THRD3_DEBUG_MODE		0x28
#define MAP_MISC_STATE			0x60
#define MAP_DEBUG_READ_CTL		0x64
#define MAP_DEBUG_READ_REG0		0x68
#define MAP_DEBUG_READ_REG1		0x6c

#define MMU_SETUP		0x400
#define MMU_LFSRSEED		0x401
#define MMU_HPW_NUM_PAGE_LVL	0x410
#define MMU_PGWKR_PGDBASE	0x411
#define MMU_PGWKR_PGDSHFT	0x412
#define MMU_PGWKR_PGDMASK	0x413
#define MMU_PGWKR_PUDSHFT	0x414
#define MMU_PGWKR_PUDMASK	0x415
#define MMU_PGWKR_PMDSHFT	0x416
#define MMU_PGWKR_PMDMASK	0x417
#define MMU_PGWKR_PTESHFT	0x418
#define MMU_PGWKR_PTEMASK	0x419

#endif /* __NLM_CPUCONTROL_H__ */
