/*
 * Copyright (c) 2006 QLogic, Inc. All rights reserved.
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

#include "ipath_kernel.h"

/**
 * ipath_enable_wc - enable write combining for MMIO writes to the device
 * @dd: infinipath device
 *
 * This routine is x86_64-specific; it twiddles the CPU's MTRRs to enable
 * write combining.
 */
int ipath_enable_wc(struct ipath_devdata *dd)
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
	 */
	pioaddr = addr + dd->ipath_piobufbase;
	piolen = (dd->ipath_piobcnt2k +
		  dd->ipath_piobcnt4k) *
		ALIGN(dd->ipath_piobcnt2k +
		      dd->ipath_piobcnt4k, dd->ipath_palign);

	for (bits = 0; !(piolen & (1ULL << bits)); bits++)
		/* do nothing */ ;

	if (piolen != (1ULL << bits)) {
		piolen >>= bits;
		while (piolen >>= 1)
			bits++;
		piolen = 1ULL << (bits + 1);
	}
	if (pioaddr & (piolen - 1)) {
		u64 atmp;
		ipath_dbg("pioaddr %llx not on right boundary for size "
			  "%llx, fixing\n",
			  (unsigned long long) pioaddr,
			  (unsigned long long) piolen);
		atmp = pioaddr & ~(piolen - 1);
		if (atmp < addr || (atmp + piolen) > (addr + len)) {
			ipath_dev_err(dd, "No way to align address/size "
				      "(%llx/%llx), no WC mtrr\n",
				      (unsigned long long) atmp,
				      (unsigned long long) piolen << 1);
			ret = -ENODEV;
		} else {
			ipath_dbg("changing WC base from %llx to %llx, "
				  "len from %llx to %llx\n",
				  (unsigned long long) pioaddr,
				  (unsigned long long) atmp,
				  (unsigned long long) piolen,
				  (unsigned long long) piolen << 1);
			pioaddr = atmp;
			piolen <<= 1;
		}
	}

	if (!ret) {
		int cookie;
		ipath_cdbg(VERBOSE, "Setting mtrr for chip to WC "
			   "(addr %llx, len=0x%llx)\n",
			   (unsigned long long) pioaddr,
			   (unsigned long long) piolen);
		cookie = mtrr_add(pioaddr, piolen, MTRR_TYPE_WRCOMB, 0);
		if (cookie < 0) {
			{
				dev_info(&dd->pcidev->dev,
					 "mtrr_add()  WC for PIO bufs "
					 "failed (%d)\n",
					 cookie);
				ret = -EINVAL;
			}
		} else {
			ipath_cdbg(VERBOSE, "Set mtrr for chip to WC, "
				   "cookie is %d\n", cookie);
			dd->ipath_wc_cookie = cookie;
			dd->ipath_wc_base = (unsigned long) pioaddr;
			dd->ipath_wc_len = (unsigned long) piolen;
		}
	}

	return ret;
}

/**
 * ipath_disable_wc - disable write combining for MMIO writes to the device
 * @dd: infinipath device
 */
void ipath_disable_wc(struct ipath_devdata *dd)
{
	if (dd->ipath_wc_cookie) {
		int r;
		ipath_cdbg(VERBOSE, "undoing WCCOMB on pio buffers\n");
		r = mtrr_del(dd->ipath_wc_cookie, dd->ipath_wc_base,
			     dd->ipath_wc_len);
		if (r < 0)
			dev_info(&dd->pcidev->dev,
				 "mtrr_del(%lx, %lx, %lx) failed: %d\n",
				 dd->ipath_wc_cookie, dd->ipath_wc_base,
				 dd->ipath_wc_len, r);
		dd->ipath_wc_cookie = 0; /* even on failure */
	}
}

/**
 * ipath_unordered_wc - indicate whether write combining is ordered
 *
 * Because our performance depends on our ability to do write combining mmio
 * writes in the most efficient way, we need to know if we are on an Intel
 * or AMD x86_64 processor.  AMD x86_64 processors flush WC buffers out in
 * the order completed, and so no special flushing is required to get
 * correct ordering.  Intel processors, however, will flush write buffers
 * out in "random" orders, and so explicit ordering is needed at times.
 */
int ipath_unordered_wc(void)
{
	return boot_cpu_data.x86_vendor != X86_VENDOR_AMD;
}
