/*
 * Hardware definitions for the Toshiba eseries PDAs
 *
 * Copyright (c) 2003 Ian Molton <spyro@f2s.com>
 *
 * This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 */

#include <linux/init.h>

#include <asm/setup.h>
#include <asm/mach/arch.h>
#include <asm/arch/hardware.h>
#include <asm/mach-types.h>

#include <generic.h>

/* Only e800 has 128MB RAM */
static void __init eseries_fixup(struct machine_desc *desc,
                      struct tag *tags, char **cmdline, struct meminfo *mi)
{
	mi->nr_banks=1;
	mi->bank[0].start = 0xa0000000;
	mi->bank[0].node = 0;
	if (machine_is_e800())
		mi->bank[0].size = (128*1024*1024);
	else
		mi->bank[0].size = (64*1024*1024);
}

/* e-series machine definitions */

#ifdef CONFIG_MACH_E330
MACHINE_START(E330, "Toshiba e330")
        /* Maintainer: Ian Molton (spyro@f2s.com) */
        .phys_io        = 0x40000000,
        .io_pg_offst    = (io_p2v(0x40000000) >> 18) & 0xfffc,
        .boot_params    = 0xa0000100,
        .map_io         = pxa_map_io,
        .init_irq       = pxa25x_init_irq,
        .fixup          = eseries_fixup,
        .timer = &pxa_timer,
MACHINE_END
#endif

#ifdef CONFIG_MACH_E740
MACHINE_START(E740, "Toshiba e740")
        /* Maintainer: Ian Molton (spyro@f2s.com) */
        .phys_io        = 0x40000000,
        .io_pg_offst    = (io_p2v(0x40000000) >> 18) & 0xfffc,
        .boot_params    = 0xa0000100,
        .map_io         = pxa_map_io,
        .init_irq       = pxa25x_init_irq,
        .fixup          = eseries_fixup,
        .timer = &pxa_timer,
MACHINE_END
#endif

#ifdef CONFIG_MACH_E750
MACHINE_START(E750, "Toshiba e750")
        /* Maintainer: Ian Molton (spyro@f2s.com) */
        .phys_io        = 0x40000000,
        .io_pg_offst    = (io_p2v(0x40000000) >> 18) & 0xfffc,
        .boot_params    = 0xa0000100,
        .map_io         = pxa_map_io,
        .init_irq       = pxa25x_init_irq,
        .fixup          = eseries_fixup,
        .timer = &pxa_timer,
MACHINE_END
#endif

#ifdef CONFIG_MACH_E400
MACHINE_START(E400, "Toshiba e400")
        /* Maintainer: Ian Molton (spyro@f2s.com) */
        .phys_io        = 0x40000000,
        .io_pg_offst    = (io_p2v(0x40000000) >> 18) & 0xfffc,
        .boot_params    = 0xa0000100,
        .map_io         = pxa_map_io,
        .init_irq       = pxa25x_init_irq,
        .fixup          = eseries_fixup,
        .timer = &pxa_timer,
MACHINE_END
#endif

#ifdef CONFIG_MACH_E800
MACHINE_START(E800, "Toshiba e800")
        /* Maintainer: Ian Molton (spyro@f2s.com) */
        .phys_io        = 0x40000000,
        .io_pg_offst    = (io_p2v(0x40000000) >> 18) & 0xfffc,
        .boot_params    = 0xa0000100,
        .map_io         = pxa_map_io,
        .init_irq       = pxa25x_init_irq,
        .fixup          = eseries_fixup,
        .timer = &pxa_timer,
MACHINE_END
#endif

