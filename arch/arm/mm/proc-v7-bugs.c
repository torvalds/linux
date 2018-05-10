// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/smp.h>

static __maybe_unused void cpu_v7_check_auxcr_set(bool *warned,
						  u32 mask, const char *msg)
{
	u32 aux_cr;

	asm("mrc p15, 0, %0, c1, c0, 1" : "=r" (aux_cr));

	if ((aux_cr & mask) != mask) {
		if (!*warned)
			pr_err("CPU%u: %s", smp_processor_id(), msg);
		*warned = true;
	}
}

static DEFINE_PER_CPU(bool, spectre_warned);

static void check_spectre_auxcr(bool *warned, u32 bit)
{
	if (IS_ENABLED(CONFIG_HARDEN_BRANCH_PREDICTOR) &&
		cpu_v7_check_auxcr_set(warned, bit,
				       "Spectre v2: firmware did not set auxiliary control register IBE bit, system vulnerable\n");
}

void cpu_v7_ca8_ibe(void)
{
	check_spectre_auxcr(this_cpu_ptr(&spectre_warned), BIT(6));
}

void cpu_v7_ca15_ibe(void)
{
	check_spectre_auxcr(this_cpu_ptr(&spectre_warned), BIT(0));
}
