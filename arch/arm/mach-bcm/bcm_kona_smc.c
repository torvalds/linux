/*
 * Copyright (C) 2013 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/smp.h>
#include <linux/io.h>
#include <linux/ioport.h>

#include <asm/cacheflush.h>
#include <linux/of_address.h>

#include "bcm_kona_smc.h"

static u32		bcm_smc_buffer_phys;	/* physical address */
static void __iomem	*bcm_smc_buffer;	/* virtual address */

struct bcm_kona_smc_data {
	unsigned service_id;
	unsigned arg0;
	unsigned arg1;
	unsigned arg2;
	unsigned arg3;
	unsigned result;
};

static const struct of_device_id bcm_kona_smc_ids[] __initconst = {
	{.compatible = "brcm,kona-smc"},
	{.compatible = "bcm,kona-smc"}, /* deprecated name */
	{},
};

/* Map in the args buffer area */
int __init bcm_kona_smc_init(void)
{
	struct device_node *node;
	const __be32 *prop_val;
	u64 prop_size = 0;
	unsigned long buffer_size;
	u32 buffer_phys;

	/* Read buffer addr and size from the device tree node */
	node = of_find_matching_node(NULL, bcm_kona_smc_ids);
	if (!node)
		return -ENODEV;

	prop_val = of_get_address(node, 0, &prop_size, NULL);
	if (!prop_val)
		return -EINVAL;

	/* We assume space for four 32-bit arguments */
	if (prop_size < 4 * sizeof(u32) || prop_size > (u64)ULONG_MAX)
		return -EINVAL;
	buffer_size = (unsigned long)prop_size;

	buffer_phys = be32_to_cpup(prop_val);
	if (!buffer_phys)
		return -EINVAL;

	bcm_smc_buffer = ioremap(buffer_phys, buffer_size);
	if (!bcm_smc_buffer)
		return -ENOMEM;
	bcm_smc_buffer_phys = buffer_phys;

	pr_info("Kona Secure API initialized\n");

	return 0;
}

/*
 * int bcm_kona_do_smc(u32 service_id, u32 buffer_addr)
 *
 * Only core 0 can run the secure monitor code.  If an "smc" request
 * is initiated on a different core it must be redirected to core 0
 * for execution.  We rely on the caller to handle this.
 *
 * Each "smc" request supplies a service id and the address of a
 * buffer containing parameters related to the service to be
 * performed.  A flags value defines the behavior of the level 2
 * cache and interrupt handling while the secure monitor executes.
 *
 * Parameters to the "smc" request are passed in r4-r6 as follows:
 *     r4	service id
 *     r5	flags (SEC_ROM_*)
 *     r6	physical address of buffer with other parameters
 *
 * Execution of an "smc" request produces two distinct results.
 *
 * First, the secure monitor call itself (regardless of the specific
 * service request) can succeed, or can produce an error.  When an
 * "smc" request completes this value is found in r12; it should
 * always be SEC_EXIT_NORMAL.
 *
 * In addition, the particular service performed produces a result.
 * The values that should be expected depend on the service.  We
 * therefore return this value to the caller, so it can handle the
 * request result appropriately.  This result value is found in r0
 * when the "smc" request completes.
 */
static int bcm_kona_do_smc(u32 service_id, u32 buffer_phys)
{
	register u32 ip asm("ip");	/* Also called r12 */
	register u32 r0 asm("r0");
	register u32 r4 asm("r4");
	register u32 r5 asm("r5");
	register u32 r6 asm("r6");

	r4 = service_id;
	r5 = 0x3;		/* Keep IRQ and FIQ off in SM */
	r6 = buffer_phys;

	asm volatile (
		/* Make sure we got the registers we want */
		__asmeq("%0", "ip")
		__asmeq("%1", "r0")
		__asmeq("%2", "r4")
		__asmeq("%3", "r5")
		__asmeq("%4", "r6")
		".arch_extension sec\n"
		"	smc    #0\n"
		: "=r" (ip), "=r" (r0)
		: "r" (r4), "r" (r5), "r" (r6)
		: "r1", "r2", "r3", "r7", "lr");

	BUG_ON(ip != SEC_EXIT_NORMAL);

	return r0;
}

/* __bcm_kona_smc() should only run on CPU 0, with pre-emption disabled */
static void __bcm_kona_smc(void *info)
{
	struct bcm_kona_smc_data *data = info;
	u32 __iomem *args = bcm_smc_buffer;

	BUG_ON(smp_processor_id() != 0);
	BUG_ON(!args);

	/* Copy the four 32 bit argument values into the bounce area */
	writel_relaxed(data->arg0, args++);
	writel_relaxed(data->arg1, args++);
	writel_relaxed(data->arg2, args++);
	writel(data->arg3, args);

	/* Flush caches for input data passed to Secure Monitor */
	flush_cache_all();

	/* Trap into Secure Monitor and record the request result */
	data->result = bcm_kona_do_smc(data->service_id, bcm_smc_buffer_phys);
}

unsigned bcm_kona_smc(unsigned service_id, unsigned arg0, unsigned arg1,
		  unsigned arg2, unsigned arg3)
{
	struct bcm_kona_smc_data data;

	data.service_id = service_id;
	data.arg0 = arg0;
	data.arg1 = arg1;
	data.arg2 = arg2;
	data.arg3 = arg3;
	data.result = 0;

	/*
	 * Due to a limitation of the secure monitor, we must use the SMP
	 * infrastructure to forward all secure monitor calls to Core 0.
	 */
	smp_call_function_single(0, __bcm_kona_smc, &data, 1);

	return data.result;
}
