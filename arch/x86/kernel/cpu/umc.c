#include <linux/kernel.h>
#include <linux/init.h>
#include <asm/processor.h>
#include "cpu.h"

/*
 * UMC chips appear to be only either 386 or 486,
 * so no special init takes place.
 */

static struct cpu_dev umc_cpu_dev __cpuinitdata = {
	.c_vendor	= "UMC",
	.c_ident	= { "UMC UMC UMC" },
	.c_models = {
		{ .vendor = X86_VENDOR_UMC, .family = 4, .model_names =
		  {
			  [1] = "U5D",
			  [2] = "U5S",
		  }
		},
	},
};

cpu_vendor_dev_register(X86_VENDOR_UMC, &umc_cpu_dev);

