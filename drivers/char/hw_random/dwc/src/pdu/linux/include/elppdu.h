/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This Synopsys software and associated documentation (hereinafter the
 * "Software") is an unsupported proprietary work of Synopsys, Inc. unless
 * otherwise expressly agreed to in writing between Synopsys and you. The
 * Software IS NOT an item of Licensed Software or a Licensed Product under
 * any End User Software License Agreement or Agreement for Licensed Products
 * with Synopsys or any supplement thereto. Synopsys is a registered trademark
 * of Synopsys, Inc. Other names included in the SOFTWARE may be the
 * trademarks of their respective owners.
 *
 * The contents of this file are dual-licensed; you may select either version
 * 2 of the GNU General Public License ("GPL") or the BSD-3-Clause license
 * ("BSD-3-Clause"). The GPL is included in the COPYING file accompanying the
 * SOFTWARE. The BSD License is copied below.
 *
 * BSD-3-Clause License:
 * Copyright (c) 2011-2017 Synopsys, Inc. and/or its affiliates.
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer, without
 *    modification.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. The names of the above-listed copyright holders may not be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef SYNPDU_H_
#define SYNPDU_H_

/* Platform Specific */
#include <linux/kernel.h>       /* printk() */
#include <linux/types.h>        /* size_t */
#include <linux/string.h>       /* memcpy()/etc */
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/vmalloc.h>
#include <linux/io.h>
#include <linux/ctype.h>
#include <linux/version.h>

#ifndef PDU_BASE_ADDR
#define PDU_BASE_ADDR	0x14c3b000
#endif

#ifndef PDU_BASE_IRQ
#define PDU_BASE_IRQ	91
#endif

#define PDU_SINGLE_CORE			1
#define PDU_SINGLE_NIST_TRNG		1

#if 1
#define SYNHW_PRINT printk
#else
#define SYNHW_PRINT(...)
#endif

#define CPU_YIELD
#define SYNHW_MEMCPY memcpy

// Debug modifier for printing, in linux adding KERN_DEBUG makes the output only show up in debug logs (avoids /var/log/messages)
#define SYNHW_PRINT_DEBUG            KERN_DEBUG

// Locking
#define PDU_LOCK_TYPE                spinlock_t
#define PDU_INIT_LOCK(lock)          spin_lock_init(lock)

// these are for IRQ contexts
#define PDU_LOCK(lock, flags)        spin_lock_irqsave(lock, flags)
#define PDU_UNLOCK(lock, flags)      spin_unlock_irqrestore(lock, flags)

// these are for bottom half BH contexts
#define PDU_LOCK_TYPE_BH                struct mutex
#define PDU_INIT_LOCK_BH(lock)       mutex_init(lock)
#define PDU_LOCK_BH(lock)            mutex_lock(lock)
#define PDU_UNLOCK_BH(lock)          mutex_unlock(lock)

#include "../../common/include/elppdu_error.h"

void *pdu_linux_map_regs(struct device *dev, struct resource *regs);

void pdu_io_write32(void *addr, unsigned long val);
void pdu_io_cached_write32(void *addr, unsigned long val, u32 *cache);
unsigned long pdu_io_read32(void *addr);

void pdu_to_dev32(void *addr, u32 *src, unsigned long nword);
void pdu_from_dev32(u32 *dst, void *addr, unsigned long nword);
void pdu_to_dev32_big(void *addr, const unsigned char *src, unsigned long nword);
void pdu_from_dev32_big(unsigned char *dst, void *addr, unsigned long nword);
void pdu_to_dev32_little(void *addr, const unsigned char *src, unsigned long nword);
void pdu_from_dev32_little(unsigned char *dst, void *addr, unsigned long nword);
void pdu_from_dev32_s(unsigned char *dst, void *addr, unsigned long nword, int endian);
void pdu_to_dev32_s(void *addr, const unsigned char *src, unsigned long nword, int endian);

void *pdu_malloc(unsigned long n);
void pdu_free(void *p);

int pdu_error_code(int code);

#endif

