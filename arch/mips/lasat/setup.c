/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 1999 MIPS Technologies, Inc.  All rights reserved.
 *
 * Thomas Horsten <thh@lasat.com>
 * Copyright (C) 2000 LASAT Networks A/S.
 *
 * Brian Murphy <brian@murphy.dk>
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Lasat specific setup.
 */
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/tty.h>

#include <asm/time.h>
#include <asm/cpu.h>
#include <asm/bootinfo.h>
#include <asm/irq.h>
#include <asm/lasat/lasat.h>
#include <asm/lasat/serial.h>

#ifdef CONFIG_PICVUE
#include <linux/notifier.h>
#endif

#include "ds1603.h"
#include <asm/lasat/ds1603.h>
#include <asm/lasat/picvue.h>
#include <asm/lasat/eeprom.h>

#include "prom.h"

int lasat_command_line;
void lasatint_init(void);

extern void lasat_reboot_setup(void);
extern void pcisetup(void);
extern void edhac_init(void *, void *, void *);
extern void addrflt_init(void);

struct lasat_misc lasat_misc_info[N_MACHTYPES] = {
	{
		.reset_reg	= (void *)KSEG1ADDR(0x1c840000),
		.flash_wp_reg	= (void *)KSEG1ADDR(0x1c800000), 2
	}, {
		.reset_reg	= (void *)KSEG1ADDR(0x11080000),
		.flash_wp_reg	= (void *)KSEG1ADDR(0x11000000), 6
	}
};

struct lasat_misc *lasat_misc;

#ifdef CONFIG_DS1603
static struct ds_defs ds_defs[N_MACHTYPES] = {
	{ (void *)DS1603_REG_100, (void *)DS1603_REG_100,
		DS1603_RST_100, DS1603_CLK_100, DS1603_DATA_100,
		DS1603_DATA_SHIFT_100, 0, 0 },
	{ (void *)DS1603_REG_200, (void *)DS1603_DATA_REG_200,
		DS1603_RST_200, DS1603_CLK_200, DS1603_DATA_200,
		DS1603_DATA_READ_SHIFT_200, 1, 2000 }
};
#endif

#ifdef CONFIG_PICVUE
#include "picvue.h"
static struct pvc_defs pvc_defs[N_MACHTYPES] = {
	{ (void *)PVC_REG_100, PVC_DATA_SHIFT_100, PVC_DATA_M_100,
		PVC_E_100, PVC_RW_100, PVC_RS_100 },
	{ (void *)PVC_REG_200, PVC_DATA_SHIFT_200, PVC_DATA_M_200,
		PVC_E_200, PVC_RW_200, PVC_RS_200 }
};
#endif

static int lasat_panic_display(struct notifier_block *this,
			     unsigned long event, void *ptr)
{
#ifdef CONFIG_PICVUE
	unsigned char *string = ptr;
	if (string == NULL)
		string = "Kernel Panic";
	pvc_dump_string(string);
#endif
	return NOTIFY_DONE;
}

static int lasat_panic_prom_monitor(struct notifier_block *this,
			     unsigned long event, void *ptr)
{
	prom_monitor();
	return NOTIFY_DONE;
}

static struct notifier_block lasat_panic_block[] =
{
	{
		.notifier_call	= lasat_panic_display,
		.priority	= INT_MAX
	}, {
		.notifier_call	= lasat_panic_prom_monitor,
		.priority	= INT_MIN
	}
};

void plat_time_init(void)
{
	mips_hpt_frequency = lasat_board_info.li_cpu_hz / 2;
}

void __init plat_timer_setup(struct irqaction *irq)
{
	change_c0_status(ST0_IM, IE_IRQ0 | IE_IRQ5);
}

void __init plat_mem_setup(void)
{
	int i;
	lasat_misc  = &lasat_misc_info[mips_machtype];
#ifdef CONFIG_PICVUE
	picvue = &pvc_defs[mips_machtype];
#endif

	/* Set up panic notifier */
	for (i = 0; i < ARRAY_SIZE(lasat_panic_block); i++)
		atomic_notifier_chain_register(&panic_notifier_list,
				&lasat_panic_block[i]);

	lasat_reboot_setup();

#ifdef CONFIG_DS1603
	ds1603 = &ds_defs[mips_machtype];
#endif

#ifdef DYNAMIC_SERIAL_INIT
	serial_init();
#endif

	pr_info("Lasat specific initialization complete\n");
}
