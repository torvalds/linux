// SPDX-License-Identifier: GPL-2.0
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

#include <linux/module.h>

#include "elppdu.h"

static bool trace_io;
module_param(trace_io, bool, 0600);
MODULE_PARM_DESC(trace_io, "Trace MMIO reads/writes");

void *pdu_linux_map_regs(struct device *dev, struct resource *regs)
{
	return devm_ioremap_resource(dev, regs);
}
EXPORT_SYMBOL(pdu_linux_map_regs);

void pdu_io_write32(void *addr, unsigned long val)
{
	if (trace_io)
		SYNHW_PRINT("PDU: write %.8lx -> %p\n", val, addr);

	writel(val, addr);
}
EXPORT_SYMBOL(pdu_io_write32);

void pdu_io_cached_write32(void *addr, unsigned long val, uint32_t *cache)
{
	if (*cache == val) {
		if (trace_io) {
			SYNHW_PRINT("PDU: write %.8lx -> %p (cached)\n", val,
				    addr);
		}
		return;
	}

	*cache = val;
	pdu_io_write32(addr, val);
}
EXPORT_SYMBOL(pdu_io_cached_write32);

unsigned long pdu_io_read32(void *addr)
{
	unsigned long val;

	val = readl(addr);

	if (trace_io)
		SYNHW_PRINT("PDU: read  %.8lx <- %p\n", val, addr);

	return val;
}
EXPORT_SYMBOL(pdu_io_read32);

/* Platform specific memory allocation */
void *pdu_malloc(unsigned long n)
{
	return vmalloc(n);
}

void pdu_free(void *p)
{
	vfree(p);
}

/* Convert SDK error codes to corresponding kernel error codes. */
int pdu_error_code(int code)
{
	switch (code) {
	case CRYPTO_INPROGRESS:
		return -EINPROGRESS;
	case CRYPTO_INVALID_HANDLE:
	case CRYPTO_INVALID_CONTEXT:
		return -ENXIO;
	case CRYPTO_NOT_INITIALIZED:
		return -ENODATA;
	case CRYPTO_INVALID_SIZE:
	case CRYPTO_INVALID_ALG:
	case CRYPTO_INVALID_KEY_SIZE:
	case CRYPTO_INVALID_ARGUMENT:
	case CRYPTO_INVALID_BLOCK_ALIGNMENT:
	case CRYPTO_INVALID_MODE:
	case CRYPTO_INVALID_KEY:
	case CRYPTO_INVALID_IV_SIZE:
	case CRYPTO_INVALID_ICV_KEY_SIZE:
	case CRYPTO_INVALID_PARAMETER_SIZE:
	case CRYPTO_REPLAY:
	case CRYPTO_INVALID_PROTOCOL:
	case CRYPTO_RESEED_REQUIRED:
		return -EINVAL;
	case CRYPTO_NOT_IMPLEMENTED:
	case CRYPTO_MODULE_DISABLED:
		return -ENOTSUPP;
	case CRYPTO_NO_MEM:
		return -ENOMEM;
	case CRYPTO_INVALID_PAD:
	case CRYPTO_INVALID_SEQUENCE:
		return -EILSEQ;
	case CRYPTO_MEMORY_ERROR:
		return -EIO;
	case CRYPTO_TIMEOUT:
		return -ETIMEDOUT;
	case CRYPTO_HALTED:
		return -ECANCELED;
	case CRYPTO_AUTHENTICATION_FAILED:
	case CRYPTO_SEQUENCE_OVERFLOW:
	case CRYPTO_INVALID_VERSION:
		return -EPROTO;
	case CRYPTO_FIFO_FULL:
		return -EBUSY;
	case CRYPTO_SRM_FAILED:
	case CRYPTO_DISABLED:
	case CRYPTO_LAST_ERROR:
		return -EAGAIN;
	case CRYPTO_FAILED:
	case CRYPTO_FATAL:
		return -EIO;
	case CRYPTO_INVALID_FIRMWARE:
		return -ENOEXEC;
	case CRYPTO_NOT_FOUND:
		return -ENOENT;
	}

	/*
	 * Any unrecognized code is either success (i.e., zero) or a negative
	 * error code, which may be meaningless but at least will still be
	 * recognized as an error.
	 */
	return code;
}
EXPORT_SYMBOL(pdu_error_code);

static int __init pdu_mod_init(void)
{
	return 0;
}

static void __exit pdu_mod_exit(void)
{
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Synopsys, Inc.");
module_init(pdu_mod_init);
module_exit(pdu_mod_exit);
