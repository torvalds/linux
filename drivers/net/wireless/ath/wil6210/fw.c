/*
 * Copyright (c) 2014 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/crc32.h>
#include "wil6210.h"
#include "fw.h"

MODULE_FIRMWARE(WIL_FW_NAME);

/* target operations */
/* register read */
#define R(a) ioread32(wil->csr + HOSTADDR(a))
/* register write. wmb() to make sure it is completed */
#define W(a, v) do { iowrite32(v, wil->csr + HOSTADDR(a)); wmb(); } while (0)
/* register set = read, OR, write */
#define S(a, v) W(a, R(a) | v)
/* register clear = read, AND with inverted, write */
#define C(a, v) W(a, R(a) & ~v)

static
void wil_memset_toio_32(volatile void __iomem *dst, u32 val,
			size_t count)
{
	volatile u32 __iomem *d = dst;

	for (count += 4; count > 4; count -= 4)
		__raw_writel(val, d++);
}

#include "fw_inc.c"
