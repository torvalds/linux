/******************************************************************************
 * acpi.c
 * acpi file for domain 0 kernel
 *
 * Copyright (c) 2011 Konrad Rzeszutek Wilk <konrad.wilk@oracle.com>
 * Copyright (c) 2011 Yu Ke ke.yu@intel.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <xen/acpi.h>
#include <xen/interface/platform.h>
#include <asm/xen/hypercall.h>
#include <asm/xen/hypervisor.h>

int xen_acpi_notify_hypervisor_state(u8 sleep_state,
				     u32 pm1a_cnt, u32 pm1b_cnt)
{
	struct xen_platform_op op = {
		.cmd = XENPF_enter_acpi_sleep,
		.interface_version = XENPF_INTERFACE_VERSION,
		.u = {
			.enter_acpi_sleep = {
				.pm1a_cnt_val = (u16)pm1a_cnt,
				.pm1b_cnt_val = (u16)pm1b_cnt,
				.sleep_state = sleep_state,
			},
		},
	};

	if ((pm1a_cnt & 0xffff0000) || (pm1b_cnt & 0xffff0000)) {
		WARN(1, "Using more than 16bits of PM1A/B 0x%x/0x%x!"
		     "Email xen-devel@lists.xensource.com  Thank you.\n", \
		     pm1a_cnt, pm1b_cnt);
		return -1;
	}

	HYPERVISOR_dom0_op(&op);
	return 1;
}
