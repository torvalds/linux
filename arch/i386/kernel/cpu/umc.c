#include <linux/kernel.h>
#include <linux/init.h>
#include <asm/processor.h>
#include "cpu.h"

/* UMC chips appear to be only either 386 or 486, so no special init takes place.
 */
static void __init init_umc(struct cpuinfo_x86 * c)
{

}

static struct cpu_dev umc_cpu_dev __initdata = {
	.c_vendor	= "UMC",
	.c_ident 	= { "UMC UMC UMC" },
	.c_models = {
		{ .vendor = X86_VENDOR_UMC, .family = 4, .model_names =
		  { 
			  [1] = "U5D", 
			  [2] = "U5S", 
		  }
		},
	},
	.c_init		= init_umc,
};

int __init umc_init_cpu(void)
{
	cpu_devs[X86_VENDOR_UMC] = &umc_cpu_dev;
	return 0;
}

//early_arch_initcall(umc_init_cpu);

static int __init umc_exit_cpu(void)
{
	cpu_devs[X86_VENDOR_UMC] = NULL;
	return 0;
}

late_initcall(umc_exit_cpu);
