/*
 * Copyright (c) 2012 Intel Corporation. All rights reserved.
 * Copyright (c) 2006 - 2012 QLogic Corporation. All rights reserved.
 * Copyright (c) 2003, 2004, 2005, 2006 PathScale, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * This file is conditionally built on x86_64 only.  Otherwise weak symbol
 * versions of the functions exported from here are used.
 */

#include <linux/pci.h>
#include <asm/mtrr.h>
#include <asm/processor.h>

#include "qib.h"

/**
 * qib_enable_wc - enable write combining for MMIO writes to the device
 * @dd: qlogic_ib device
 *
 * This routine is x86_64-specific; it twiddles the CPU's MTRRs to enable
 * write combining.
 */
int qib_enable_wc(struct qib_devdata *dd)
{
	int ret = 0;
	u64 pioaddr, piolen;
	unsigned bits;
	const unsigned long addr = pci_resource_start(dd->pcidev, 0);
	const size_t len = pci_resource_len(dd->pcidev, 0);

	/*
	 * Set the PIO buffers to be WCCOMB, so we get HT bursts to the
	 * chip.  Linux (possibly the hardware) requires it to be on a power
	 * of 2 address matching the length (which has to be a power of 2).
	 * For rev1, that means the base address, for rev2, it will be just
	 * the PIO buffers themselves.
	 * For chips with two sets of buffers, the calculations are
	 * somewhat more complicated; we need to sum, and the piobufbase
	 * register has both offsets, 2K in low 32 bits, 4K in high 32 bits.
	 * The buffers are still packed, so a single range covers both.
	 */
	if (dd->piobcnt2k && dd->piobcnt4k) {
		/* 2 sizes for chip */
		unsigned long pio2kbase, pio4kbase;

		pio2kbase = dd->piobufbase & 0xffffffffUL;
		pio4kbase = (dd->piobufbase >> 32) & 0xffffffffUL;
		if (pio2kbase < pio4kbase) {
			/* all current chips */
			pioaddr = addr + pio2kbase;
			piolen = pio4kbase - pio2kbase +
				dd->piobcnt4k * dd->align4k;
		} else {
			pioaddr = addr + pio4kbase;
			piolen = pio2kbase - pio4kbase +
				dd->piobcnt2k * dd->palign;
		}
	} else {  /* single buffer size (2K, currently) */
		pioaddr = addr + dd->piobufbase;
		piolen = dd->piobcnt2k * dd->palign +
			dd->piobcnt4k * dd->align4k;
	}

	for (bits = 0; !(piolen & (1ULL << bits)); bits++)
		; /* do nothing */

	if (piolen != (1ULL << bits)) {
		piolen >>= bits;
		while (piolen >>= 1)
			bits++;
		piolen = 1ULL << (bits + 1);
	}
	if (pioaddr & (piolen - 1)) {
		u64 atmp = pioaddr & ~(piolen - 1);

		if (atmp < addr || (atmp + piolen) > (addr + len)) {
			qib_dev_err(dd,
				"No way to align address/size (%llx/%llx), no WC mtrr\n",
				(unsigned long long) atmp,
				(unsigned long long) piolen << 1);
			ret = -ENODEV;
		} else {
			pioaddr = atmp;
			piolen <<= 1;
		}
	}

	if (!ret) {
		int cookie;

		cookie = mtrr_add(pioaddr, piolen, MTRR_TYPE_WRCOMB, 0);
		if (cookie < 0) {
			{
				qib_devinfo(dd->pcidev,
					 "mtrr_add()  WC for PIO bufs failed (%d)\n",
					 cookie);
				ret = -EINVAL;
			}
		} else {
			dd->wc_cookie = cookie;
			dd->wc_base = (unsigned long) pioaddr;
			dd->wc_len = (unsigned long) piolen;
		}
	}

	return ret;
}

/**
 * qib_disable_wc - disable write combining for MMIO writes to the device
 * @dd: qlogic_ib device
 */
void qib_disable_wc(struct qib_devdata *dd)
{
	if (dd->wc_cookie) {
		int r;

		r = mtrr_del(dd->wc_cookie, dd->wc_base,
			     dd->wc_len);
		if (r < 0)
			qib_devinfo(dd->pcidev,
				 "mtrr_del(%lx, %lx, %lx) failed: %d\n",
				 dd->wc_cookie, dd->wc_base,
				 dd->wc_len, r);
		dd->wc_cookie = 0; /* even on failure */
	}
}

/**
 * qib_unordered_wc - indicate whether write combining is ordered
 *
 * Because our performance depends on our ability to do write combining mmio
 * writes in the most efficient way, we need to know if we are on an Intel
 * or AMD x86_64 processor.  AMD x86_64 processors flush WC buffers out in
 * the order completed, and so no special flushing is required to get
 * correct ordering.  Intel processors, however, will flush write buffers
 * out in "random" orders, and so explicit ordering is needed at times.
 */
int qib_unordered_wc(void)
{
	return boot_cpu_data.x86_vendor != X86_VENDOR_AMD;
}
