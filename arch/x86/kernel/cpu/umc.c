#include <linux/kernel.h>
#include <linux/init.h>
#include <asm/processor.h>
#include "cpu.h"

/*
 * UMC chips appear to be only either 386 or 486,
 * so no special init takes place.
 */

static const struct cpu_dev umc_cpu_dev = {
	.c_vendor	= "UMC",
	.c_ident	= { "UMC UMC UMC" },
	.legacy_models	= {
		{ .family = 4, .model_names =
		  {
			  [1] = "U5D",
			  [2] = "U5S",
		  }
		},
	},
	.c_x86_vendor	= X86_VENDOR_UMC,
};

cpu_dev_register(umc_cpu_dev);

