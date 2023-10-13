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
 * Copyright (c) 2011-2016 Synopsys, Inc. and/or its affiliates.
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
#include <linux/platform_device.h>
#include <linux/io-mapping.h>
#include <linux/irqdomain.h>
#include <linux/of_irq.h>
#include <linux/err.h>

#include "elppdu.h"

static unsigned long vex_baseaddr = PDU_BASE_ADDR;
module_param_named(baseaddr, vex_baseaddr, ulong, 0);
MODULE_PARM_DESC(baseaddr, "Hardware base address (default " __stringify(PDU_BASE_ADDR) ")");

// max of 16 devices
#define MAX_DEV 16

static struct platform_device *devices[MAX_DEV];
static int dev_id;

static void register_device(const char *name, int id,
			    const struct resource *res, unsigned int num)
{
	char suffix[16] = "";
	struct platform_device_info pdevinfo = {
		.name = name,
		.id = id,
		.res = res,
		.num_res = num,
		.dma_mask = 0xffffffff,
	};

	if (dev_id >= MAX_DEV) {
		pr_err("Too many devices; increase MAX_DEV.\n");
		return;
	}

	devices[dev_id] = platform_device_register_full(&pdevinfo);
	if (IS_ERR(devices[dev_id])) {
		if (id >= 0)
			snprintf(suffix, sizeof(suffix), ".%d", id);
		pr_err("Failed to register %s%s\n", name, suffix);

		devices[dev_id] = NULL;
		return;
	}

	dev_id++;
}

static int __init get_irq_num(unsigned int irq_num)
{
	if (IS_ENABLED(CONFIG_ARCH_ZYNQ)) {
		struct of_phandle_args args = { 0 };

	/*
	 * Since this driver is for non-DT use but Zynq uses DT to setup IRQs,
	 * find the GIC by searching for its DT node then manually create the
	 * IRQ mappings.
	 */

		do {
			args.np = of_find_node_with_property(args.np,
							     "interrupt-controller");
			if (!args.np) {
				pr_err("cannot find IRQ controller");
				return -ENODEV;
			}
		} while (!of_device_is_compatible(args.np, "arm,cortex-a9-gic"));

		if (irq_num < 32 || irq_num >= 96) {
			pr_err("SPI interrupts must be in the range [32,96) on Zynq\n");
			return -EINVAL;
		}

		args.args_count = 3;
		args.args[0] = 0; /* SPI */
		args.args[1] = irq_num - 32;
		args.args[2] = 4; /* Active high, level-sensitive */

		irq_num = irq_create_of_mapping(&args);
		of_node_put(args.np);
		if (irq_num == 0)
			return -EINVAL;
	}

	if (irq_num > INT_MAX)
		return -EINVAL;

	return irq_num;
}

static int __init pdu_vex_mod_init(void)
{
	int irq_num = get_irq_num(PDU_BASE_IRQ);
	struct resource res[2];
#ifndef PDU_SINGLE_CORE
	void *pdu_mem;
	int i, rc;
#endif

	if (irq_num >= 0) {
		res[1] = (struct resource){
			.start = irq_num,
			.end = irq_num,
			.flags = IORESOURCE_IRQ,
		};
	} else {
		res[1] = (struct resource){ 0 };
		pr_err("IRQ setup failed (error %d), not using IRQs\n",
		       irq_num);
	}

#ifdef PDU_SINGLE_BASIC_TRNG
	res[0] = (struct resource){
		.start = vex_baseaddr,
		.end = vex_baseaddr + 0x80 - 1,
		.flags = IORESOURCE_MEM,
	};
	register_device("basic_trng", -1, res, 2);
#endif

#ifdef PDU_SINGLE_NIST_TRNG
	res[0] = (struct resource){
		.start = vex_baseaddr,
		.end = vex_baseaddr + 0x800 - 1,
		.flags = IORESOURCE_MEM,
	};
	register_device("nist_trng", -1, res, 2);
#endif

	return 0;
}
module_init(pdu_vex_mod_init);

static void __exit pdu_vex_mod_exit(void)
{
	int i;

	for (i = 0; i < MAX_DEV; i++)
		platform_device_unregister(devices[i]);
}
module_exit(pdu_vex_mod_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Synopsys, Inc.");
