/*
 * Copyright 2003-2013 Broadcom Corporation.
 * All Rights Reserved.
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

#include <linux/kernel.h>
#include <linux/bootmem.h>

#include <linux/of_fdt.h>
#include <linux/of_platform.h>
#include <linux/of_device.h>

#include <asm/prom.h>

extern u32 __dtb_xlp_evp_begin[], __dtb_xlp_svp_begin[], __dtb_xlp_fvp_begin[],
	__dtb_xlp_gvp_begin[], __dtb_xlp_rvp_begin[];
static void *xlp_fdt_blob;

void __init *xlp_dt_init(void *fdtp)
{
	if (!fdtp) {
		switch (current_cpu_data.processor_id & PRID_IMP_MASK) {
#ifdef CONFIG_DT_XLP_RVP
		case PRID_IMP_NETLOGIC_XLP5XX:
			fdtp = __dtb_xlp_rvp_begin;
			break;
#endif
#ifdef CONFIG_DT_XLP_GVP
		case PRID_IMP_NETLOGIC_XLP9XX:
			fdtp = __dtb_xlp_gvp_begin;
			break;
#endif
#ifdef CONFIG_DT_XLP_FVP
		case PRID_IMP_NETLOGIC_XLP2XX:
			fdtp = __dtb_xlp_fvp_begin;
			break;
#endif
#ifdef CONFIG_DT_XLP_SVP
		case PRID_IMP_NETLOGIC_XLP3XX:
			fdtp = __dtb_xlp_svp_begin;
			break;
#endif
#ifdef CONFIG_DT_XLP_EVP
		case PRID_IMP_NETLOGIC_XLP8XX:
			fdtp = __dtb_xlp_evp_begin;
			break;
#endif
		default:
			/* Pick a built-in if any, and hope for the best */
			fdtp = __dtb_start;
			break;
		}
	}
	xlp_fdt_blob = fdtp;
	return fdtp;
}

void __init xlp_early_init_devtree(void)
{
	__dt_setup_arch(xlp_fdt_blob);
}

void __init device_tree_init(void)
{
	unflatten_and_copy_device_tree();
}

static struct of_device_id __initdata xlp_ids[] = {
	{ .compatible = "simple-bus", },
	{},
};

int __init xlp8xx_ds_publish_devices(void)
{
	if (!of_have_populated_dt())
		return 0;
	return of_platform_bus_probe(NULL, xlp_ids, NULL);
}

device_initcall(xlp8xx_ds_publish_devices);
