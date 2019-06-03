// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Trusted Foundations support for ARM CPUs
 *
 * Copyright (c) 2013, NVIDIA Corporation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of.h>

#include <linux/firmware/trusted_foundations.h>

#include <asm/firmware.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/outercache.h>

#define TF_CACHE_MAINT		0xfffff100

#define TF_CACHE_ENABLE		1
#define TF_CACHE_DISABLE	2

#define TF_SET_CPU_BOOT_ADDR_SMC 0xfffff200

#define TF_CPU_PM		0xfffffffc
#define TF_CPU_PM_S3		0xffffffe3
#define TF_CPU_PM_S2		0xffffffe6
#define TF_CPU_PM_S2_NO_MC_CLK	0xffffffe5
#define TF_CPU_PM_S1		0xffffffe4
#define TF_CPU_PM_S1_NOFLUSH_L2	0xffffffe7

static unsigned long cpu_boot_addr;

static void tf_generic_smc(u32 type, u32 arg1, u32 arg2)
{
	register u32 r0 asm("r0") = type;
	register u32 r1 asm("r1") = arg1;
	register u32 r2 asm("r2") = arg2;

	asm volatile(
		".arch_extension	sec\n\t"
		"stmfd	sp!, {r4 - r11}\n\t"
		__asmeq("%0", "r0")
		__asmeq("%1", "r1")
		__asmeq("%2", "r2")
		"mov	r3, #0\n\t"
		"mov	r4, #0\n\t"
		"smc	#0\n\t"
		"ldmfd	sp!, {r4 - r11}\n\t"
		:
		: "r" (r0), "r" (r1), "r" (r2)
		: "memory", "r3", "r12", "lr");
}

static int tf_set_cpu_boot_addr(int cpu, unsigned long boot_addr)
{
	cpu_boot_addr = boot_addr;
	tf_generic_smc(TF_SET_CPU_BOOT_ADDR_SMC, cpu_boot_addr, 0);

	return 0;
}

static int tf_prepare_idle(unsigned long mode)
{
	switch (mode) {
	case TF_PM_MODE_LP0:
		tf_generic_smc(TF_CPU_PM, TF_CPU_PM_S3, cpu_boot_addr);
		break;

	case TF_PM_MODE_LP1:
		tf_generic_smc(TF_CPU_PM, TF_CPU_PM_S2, cpu_boot_addr);
		break;

	case TF_PM_MODE_LP1_NO_MC_CLK:
		tf_generic_smc(TF_CPU_PM, TF_CPU_PM_S2_NO_MC_CLK,
			       cpu_boot_addr);
		break;

	case TF_PM_MODE_LP2:
		tf_generic_smc(TF_CPU_PM, TF_CPU_PM_S1, cpu_boot_addr);
		break;

	case TF_PM_MODE_LP2_NOFLUSH_L2:
		tf_generic_smc(TF_CPU_PM, TF_CPU_PM_S1_NOFLUSH_L2,
			       cpu_boot_addr);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

#ifdef CONFIG_CACHE_L2X0
static void tf_cache_write_sec(unsigned long val, unsigned int reg)
{
	u32 l2x0_way_mask = 0xff;

	switch (reg) {
	case L2X0_CTRL:
		if (l2x0_saved_regs.aux_ctrl & L310_AUX_CTRL_ASSOCIATIVITY_16)
			l2x0_way_mask = 0xffff;

		if (val == L2X0_CTRL_EN)
			tf_generic_smc(TF_CACHE_MAINT, TF_CACHE_ENABLE,
				       l2x0_saved_regs.aux_ctrl);
		else
			tf_generic_smc(TF_CACHE_MAINT, TF_CACHE_DISABLE,
				       l2x0_way_mask);
		break;

	default:
		break;
	}
}

static int tf_init_cache(void)
{
	outer_cache.write_sec = tf_cache_write_sec;

	return 0;
}
#endif /* CONFIG_CACHE_L2X0 */

static const struct firmware_ops trusted_foundations_ops = {
	.set_cpu_boot_addr = tf_set_cpu_boot_addr,
	.prepare_idle = tf_prepare_idle,
#ifdef CONFIG_CACHE_L2X0
	.l2x0_init = tf_init_cache,
#endif
};

void register_trusted_foundations(struct trusted_foundations_platform_data *pd)
{
	/*
	 * we are not using version information for now since currently
	 * supported SMCs are compatible with all TF releases
	 */
	register_firmware_ops(&trusted_foundations_ops);
}

void of_register_trusted_foundations(void)
{
	struct device_node *node;
	struct trusted_foundations_platform_data pdata;
	int err;

	node = of_find_compatible_node(NULL, NULL, "tlm,trusted-foundations");
	if (!node)
		return;

	err = of_property_read_u32(node, "tlm,version-major",
				   &pdata.version_major);
	if (err != 0)
		panic("Trusted Foundation: missing version-major property\n");
	err = of_property_read_u32(node, "tlm,version-minor",
				   &pdata.version_minor);
	if (err != 0)
		panic("Trusted Foundation: missing version-minor property\n");
	register_trusted_foundations(&pdata);
}

bool trusted_foundations_registered(void)
{
	return firmware_ops == &trusted_foundations_ops;
}
