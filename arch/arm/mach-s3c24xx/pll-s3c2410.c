/*
 * Copyright (c) 2006-2007 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *	Vincent Sanders <vince@arm.linux.org.uk>
 *
 * S3C2410 CPU PLL tables
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/clk.h>
#include <linux/err.h>

#include <plat/cpu.h>
#include <plat/cpu-freq-core.h>

static struct cpufreq_frequency_table pll_vals_12MHz[] = {
    { .frequency = 34000000,  .index = PLLVAL(82, 2, 3),   },
    { .frequency = 45000000,  .index = PLLVAL(82, 1, 3),   },
    { .frequency = 51000000,  .index = PLLVAL(161, 3, 3),  },
    { .frequency = 48000000,  .index = PLLVAL(120, 2, 3),  },
    { .frequency = 56000000,  .index = PLLVAL(142, 2, 3),  },
    { .frequency = 68000000,  .index = PLLVAL(82, 2, 2),   },
    { .frequency = 79000000,  .index = PLLVAL(71, 1, 2),   },
    { .frequency = 85000000,  .index = PLLVAL(105, 2, 2),  },
    { .frequency = 90000000,  .index = PLLVAL(112, 2, 2),  },
    { .frequency = 101000000, .index = PLLVAL(127, 2, 2),  },
    { .frequency = 113000000, .index = PLLVAL(105, 1, 2),  },
    { .frequency = 118000000, .index = PLLVAL(150, 2, 2),  },
    { .frequency = 124000000, .index = PLLVAL(116, 1, 2),  },
    { .frequency = 135000000, .index = PLLVAL(82, 2, 1),   },
    { .frequency = 147000000, .index = PLLVAL(90, 2, 1),   },
    { .frequency = 152000000, .index = PLLVAL(68, 1, 1),   },
    { .frequency = 158000000, .index = PLLVAL(71, 1, 1),   },
    { .frequency = 170000000, .index = PLLVAL(77, 1, 1),   },
    { .frequency = 180000000, .index = PLLVAL(82, 1, 1),   },
    { .frequency = 186000000, .index = PLLVAL(85, 1, 1),   },
    { .frequency = 192000000, .index = PLLVAL(88, 1, 1),   },
    { .frequency = 203000000, .index = PLLVAL(161, 3, 1),  },

    /* 2410A extras */

    { .frequency = 210000000, .index = PLLVAL(132, 2, 1),  },
    { .frequency = 226000000, .index = PLLVAL(105, 1, 1),  },
    { .frequency = 266000000, .index = PLLVAL(125, 1, 1),  },
    { .frequency = 268000000, .index = PLLVAL(126, 1, 1),  },
    { .frequency = 270000000, .index = PLLVAL(127, 1, 1),  },
};

static int s3c2410_plls_add(struct device *dev, struct subsys_interface *sif)
{
	return s3c_plltab_register(pll_vals_12MHz, ARRAY_SIZE(pll_vals_12MHz));
}

static struct subsys_interface s3c2410_plls_interface = {
	.name		= "s3c2410_plls",
	.subsys		= &s3c2410_subsys,
	.add_dev	= s3c2410_plls_add,
};

static int __init s3c2410_pll_init(void)
{
	return subsys_interface_register(&s3c2410_plls_interface);

}
arch_initcall(s3c2410_pll_init);

static struct subsys_interface s3c2410a_plls_interface = {
	.name		= "s3c2410a_plls",
	.subsys		= &s3c2410a_subsys,
	.add_dev	= s3c2410_plls_add,
};

static int __init s3c2410a_pll_init(void)
{
	return subsys_interface_register(&s3c2410a_plls_interface);
}
arch_initcall(s3c2410a_pll_init);
