/*
 *  linux/arch/arm/mach-integrator/integrator_ap.c
 *
 *  Copyright (C) 2000-2003 Deep Blue Solutions Ltd
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
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/syscore_ops.h>
#include <linux/amba/bus.h>
#include <linux/io.h>
#include <linux/irqchip.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/termios.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include "hardware.h"
#include "cm.h"
#include "common.h"
#include "lm.h"

/* Regmap to the AP system controller */
static struct regmap *ap_syscon_map;

/*
 * All IO addresses are mapped onto VA 0xFFFx.xxxx, where x.xxxx
 * is the (PA >> 12).
 *
 * Setup a VA for the Integrator interrupt controller (for header #0,
 * just for now).
 */
#define VA_IC_BASE	__io_address(INTEGRATOR_IC_BASE)

/*
 * Logical      Physical
 * f1400000	14000000	Interrupt controller
 * f1600000	16000000	UART 0
 */

static struct map_desc ap_io_desc[] __initdata __maybe_unused = {
	{
		.virtual	= IO_ADDRESS(INTEGRATOR_IC_BASE),
		.pfn		= __phys_to_pfn(INTEGRATOR_IC_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	= IO_ADDRESS(INTEGRATOR_UART0_BASE),
		.pfn		= __phys_to_pfn(INTEGRATOR_UART0_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}
};

static void __init ap_map_io(void)
{
	iotable_init(ap_io_desc, ARRAY_SIZE(ap_io_desc));
}

#ifdef CONFIG_PM
static unsigned long ic_irq_enable;

static int irq_suspend(void)
{
	ic_irq_enable = readl(VA_IC_BASE + IRQ_ENABLE);
	return 0;
}

static void irq_resume(void)
{
	/* disable all irq sources */
	cm_clear_irqs();
	writel(-1, VA_IC_BASE + IRQ_ENABLE_CLEAR);
	writel(-1, VA_IC_BASE + FIQ_ENABLE_CLEAR);

	writel(ic_irq_enable, VA_IC_BASE + IRQ_ENABLE_SET);
}
#else
#define irq_suspend NULL
#define irq_resume NULL
#endif

static struct syscore_ops irq_syscore_ops = {
	.suspend	= irq_suspend,
	.resume		= irq_resume,
};

static int __init irq_syscore_init(void)
{
	register_syscore_ops(&irq_syscore_ops);

	return 0;
}

device_initcall(irq_syscore_init);

/*
 * For the PL010 found in the Integrator/AP some of the UART control is
 * implemented in the system controller and accessed using a callback
 * from the driver.
 */
static void integrator_uart_set_mctrl(struct amba_device *dev,
				void __iomem *base, unsigned int mctrl)
{
	unsigned int ctrls = 0, ctrlc = 0, rts_mask, dtr_mask;
	u32 phybase = dev->res.start;
	int ret;

	if (phybase == INTEGRATOR_UART0_BASE) {
		/* UART0 */
		rts_mask = 1 << 4;
		dtr_mask = 1 << 5;
	} else {
		/* UART1 */
		rts_mask = 1 << 6;
		dtr_mask = 1 << 7;
	}

	if (mctrl & TIOCM_RTS)
		ctrlc |= rts_mask;
	else
		ctrls |= rts_mask;

	if (mctrl & TIOCM_DTR)
		ctrlc |= dtr_mask;
	else
		ctrls |= dtr_mask;

	ret = regmap_write(ap_syscon_map,
			   INTEGRATOR_SC_CTRLS_OFFSET,
			   ctrls);
	if (ret)
		pr_err("MODEM: unable to write PL010 UART CTRLS\n");

	ret = regmap_write(ap_syscon_map,
			   INTEGRATOR_SC_CTRLC_OFFSET,
			   ctrlc);
	if (ret)
		pr_err("MODEM: unable to write PL010 UART CRTLC\n");
}

struct amba_pl010_data ap_uart_data = {
	.set_mctrl = integrator_uart_set_mctrl,
};

void __init ap_init_early(void)
{
}

static void __init ap_init_irq_of(void)
{
	cm_init();
	irqchip_init();
}

/* For the Device Tree, add in the UART callbacks as AUXDATA */
static struct of_dev_auxdata ap_auxdata_lookup[] __initdata = {
	OF_DEV_AUXDATA("arm,primecell", INTEGRATOR_UART0_BASE,
		"uart0", &ap_uart_data),
	OF_DEV_AUXDATA("arm,primecell", INTEGRATOR_UART1_BASE,
		"uart1", &ap_uart_data),
	{ /* sentinel */ },
};

static const struct of_device_id ap_syscon_match[] = {
	{ .compatible = "arm,integrator-ap-syscon"},
	{ },
};

static void __init ap_init_of(void)
{
	u32 sc_dec;
	struct device_node *syscon;
	int ret;
	int i;

	of_platform_default_populate(NULL, ap_auxdata_lookup, NULL);

	syscon = of_find_matching_node(NULL, ap_syscon_match);
	if (!syscon)
		return;
	ap_syscon_map = syscon_node_to_regmap(syscon);
	if (IS_ERR(ap_syscon_map)) {
		pr_crit("could not find Integrator/AP system controller\n");
		return;
	}

	ret = regmap_read(ap_syscon_map,
			  INTEGRATOR_SC_DEC_OFFSET,
			  &sc_dec);
	if (ret) {
		pr_crit("could not read from Integrator/AP syscon\n");
		return;
	}

	for (i = 0; i < 4; i++) {
		struct lm_device *lmdev;

		if ((sc_dec & (16 << i)) == 0)
			continue;

		lmdev = kzalloc(sizeof(struct lm_device), GFP_KERNEL);
		if (!lmdev)
			continue;

		lmdev->resource.start = 0xc0000000 + 0x10000000 * i;
		lmdev->resource.end = lmdev->resource.start + 0x0fffffff;
		lmdev->resource.flags = IORESOURCE_MEM;
		lmdev->irq = irq_of_parse_and_map(syscon, i);
		lmdev->id = i;

		lm_device_register(lmdev);
	}
}

static const char * ap_dt_board_compat[] = {
	"arm,integrator-ap",
	NULL,
};

DT_MACHINE_START(INTEGRATOR_AP_DT, "ARM Integrator/AP (Device Tree)")
	.reserve	= integrator_reserve,
	.map_io		= ap_map_io,
	.init_early	= ap_init_early,
	.init_irq	= ap_init_irq_of,
	.init_machine	= ap_init_of,
	.dt_compat      = ap_dt_board_compat,
MACHINE_END
