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
#ifndef _ASM_NLM_FLASH_H_
#define _ASM_NLM_FLASH_H_

#define FLASH_CSBASE_ADDR(cs)		(cs)
#define FLASH_CSADDR_MASK(cs)		(0x10 + (cs))
#define FLASH_CSDEV_PARM(cs)		(0x20 + (cs))
#define FLASH_CSTIME_PARMA(cs)		(0x30 + (cs))
#define FLASH_CSTIME_PARMB(cs)		(0x40 + (cs))

#define FLASH_INT_MASK			0x50
#define FLASH_INT_STATUS		0x60
#define FLASH_ERROR_STATUS		0x70
#define FLASH_ERROR_ADDR		0x80

#define FLASH_NAND_CLE(cs)		(0x90 + (cs))
#define FLASH_NAND_ALE(cs)		(0xa0 + (cs))

#define FLASH_NAND_CSDEV_PARAM		0x000041e6
#define FLASH_NAND_CSTIME_PARAMA	0x4f400e22
#define FLASH_NAND_CSTIME_PARAMB	0x000083cf

#endif
