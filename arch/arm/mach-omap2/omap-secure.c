// SPDX-License-Identifier: GPL-2.0-only
/*
 * OMAP Secure API infrastructure.
 *
 * Copyright (C) 2011 Texas Instruments, Inc.
 *	Santosh Shilimkar <santosh.shilimkar@ti.com>
 * Copyright (C) 2012 Ivaylo Dimitrov <freemangordon@abv.bg>
 * Copyright (C) 2013 Pali Rohár <pali@kernel.org>
 */

#include <linux/arm-smccc.h>
#include <linux/cpu_pm.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/memblock.h>
#include <linux/of.h>

#include <asm/cacheflush.h>
#include <asm/memblock.h>

#include "common.h"
#include "omap-secure.h"
#include "soc.h"

static phys_addr_t omap_secure_memblock_base;

bool optee_available;

#define OMAP_SIP_SMC_STD_CALL_VAL(func_num) \
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_STD_CALL, ARM_SMCCC_SMC_32, \
	ARM_SMCCC_OWNER_SIP, (func_num))

static void __init omap_optee_init_check(void)
{
	struct device_node *np;

	/*
	 * We only check that the OP-TEE node is present and available. The
	 * OP-TEE kernel driver is not needed for the type of interaction made
	 * with OP-TEE here so the driver's status is not checked.
	 */
	np = of_find_node_by_path("/firmware/optee");
	if (np && of_device_is_available(np))
		optee_available = true;
	of_node_put(np);
}

/**
 * omap_sec_dispatcher: Routine to dispatch low power secure
 * service routines
 * @idx: The HAL API index
 * @flag: The flag indicating criticality of operation
 * @nargs: Number of valid arguments out of four.
 * @arg1, arg2, arg3 args4: Parameters passed to secure API
 *
 * Return the non-zero error value on failure.
 */
u32 omap_secure_dispatcher(u32 idx, u32 flag, u32 nargs, u32 arg1, u32 arg2,
							 u32 arg3, u32 arg4)
{
	u32 ret;
	u32 param[5];

	param[0] = nargs;
	param[1] = arg1;
	param[2] = arg2;
	param[3] = arg3;
	param[4] = arg4;

	/*
	 * Secure API needs physical address
	 * pointer for the parameters
	 */
	flush_cache_all();
	outer_clean_range(__pa(param), __pa(param + 5));
	ret = omap_smc2(idx, flag, __pa(param));

	return ret;
}

void omap_smccc_smc(u32 fn, u32 arg)
{
	struct arm_smccc_res res;

	arm_smccc_smc(OMAP_SIP_SMC_STD_CALL_VAL(fn), arg,
		      0, 0, 0, 0, 0, 0, &res);
	WARN(res.a0, "Secure function call 0x%08x failed\n", fn);
}

void omap_smc1(u32 fn, u32 arg)
{
	/*
	 * If this platform has OP-TEE installed we use ARM SMC calls
	 * otherwise fall back to the OMAP ROM style calls.
	 */
	if (optee_available)
		omap_smccc_smc(fn, arg);
	else
		_omap_smc1(fn, arg);
}

/* Allocate the memory to save secure ram */
int __init omap_secure_ram_reserve_memblock(void)
{
	u32 size = OMAP_SECURE_RAM_STORAGE;

	size = ALIGN(size, SECTION_SIZE);
	omap_secure_memblock_base = arm_memblock_steal(size, SECTION_SIZE);

	return 0;
}

phys_addr_t omap_secure_ram_mempool_base(void)
{
	return omap_secure_memblock_base;
}

#if defined(CONFIG_ARCH_OMAP3) && defined(CONFIG_PM)
u32 omap3_save_secure_ram(void __iomem *addr, int size)
{
	u32 ret;
	u32 param[5];

	if (size != OMAP3_SAVE_SECURE_RAM_SZ)
		return OMAP3_SAVE_SECURE_RAM_SZ;

	param[0] = 4;		/* Number of arguments */
	param[1] = __pa(addr);	/* Physical address for saving */
	param[2] = 0;
	param[3] = 1;
	param[4] = 1;

	ret = save_secure_ram_context(__pa(param));

	return ret;
}
#endif

/**
 * rx51_secure_dispatcher: Routine to dispatch secure PPA API calls
 * @idx: The PPA API index
 * @process: Process ID
 * @flag: The flag indicating criticality of operation
 * @nargs: Number of valid arguments out of four.
 * @arg1, arg2, arg3 args4: Parameters passed to secure API
 *
 * Return the non-zero error value on failure.
 *
 * NOTE: rx51_secure_dispatcher differs from omap_secure_dispatcher because
 *       it calling omap_smc3() instead omap_smc2() and param[0] is nargs+1
 */
u32 rx51_secure_dispatcher(u32 idx, u32 process, u32 flag, u32 nargs,
			   u32 arg1, u32 arg2, u32 arg3, u32 arg4)
{
	u32 ret;
	u32 param[5];

	param[0] = nargs+1; /* RX-51 needs number of arguments + 1 */
	param[1] = arg1;
	param[2] = arg2;
	param[3] = arg3;
	param[4] = arg4;

	/*
	 * Secure API needs physical address
	 * pointer for the parameters
	 */
	local_irq_disable();
	local_fiq_disable();
	flush_cache_all();
	outer_clean_range(__pa(param), __pa(param + 5));
	ret = omap_smc3(idx, process, flag, __pa(param));
	flush_cache_all();
	local_fiq_enable();
	local_irq_enable();

	return ret;
}

/**
 * rx51_secure_update_aux_cr: Routine to modify the contents of Auxiliary Control Register
 *  @set_bits: bits to set in ACR
 *  @clr_bits: bits to clear in ACR
 *
 * Return the non-zero error value on failure.
*/
u32 rx51_secure_update_aux_cr(u32 set_bits, u32 clear_bits)
{
	u32 acr;

	/* Read ACR */
	asm volatile ("mrc p15, 0, %0, c1, c0, 1" : "=r" (acr));
	acr &= ~clear_bits;
	acr |= set_bits;

	return rx51_secure_dispatcher(RX51_PPA_WRITE_ACR,
				      0,
				      FLAG_START_CRITICAL,
				      1, acr, 0, 0, 0);
}

/**
 * rx51_secure_rng_call: Routine for HW random generator
 */
u32 rx51_secure_rng_call(u32 ptr, u32 count, u32 flag)
{
	return rx51_secure_dispatcher(RX51_PPA_HWRNG,
				      0,
				      NO_FLAG,
				      3, ptr, count, flag, 0);
}

void __init omap_secure_init(void)
{
	omap_optee_init_check();
}

/*
 * Dummy dispatcher call after core OSWR and MPU off. Updates the ROM return
 * address after MMU has been re-enabled after CPU1 has been woken up again.
 * Otherwise the ROM code will attempt to use the earlier physical return
 * address that got set with MMU off when waking up CPU1. Only used on secure
 * devices.
 */
static int cpu_notifier(struct notifier_block *nb, unsigned long cmd, void *v)
{
	switch (cmd) {
	case CPU_CLUSTER_PM_EXIT:
		omap_secure_dispatcher(OMAP4_PPA_SERVICE_0,
				       FLAG_START_CRITICAL,
				       0, 0, 0, 0, 0);
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block secure_notifier_block = {
	.notifier_call = cpu_notifier,
};

static int __init secure_pm_init(void)
{
	if (omap_type() == OMAP2_DEVICE_TYPE_GP || !soc_is_omap44xx())
		return 0;

	cpu_pm_register_notifier(&secure_notifier_block);

	return 0;
}
omap_arch_initcall(secure_pm_init);
