// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2000-2003 Deep Blue Solutions Ltd
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

#include "integrator-hardware.h"
#include "integrator-cm.h"
#include "integrator.h"

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
	struct device_node *syscon;

	of_platform_default_populate(NULL, ap_auxdata_lookup, NULL);

	syscon = of_find_matching_node(NULL, ap_syscon_match);
	if (!syscon)
		return;
	ap_syscon_map = syscon_node_to_regmap(syscon);
	if (IS_ERR(ap_syscon_map)) {
		pr_crit("could not find Integrator/AP system controller\n");
		return;
	}
}

static const char * ap_dt_board_compat[] = {
	"arm,integrator-ap",
	NULL,
};

DT_MACHINE_START(INTEGRATOR_AP_DT, "ARM Integrator/AP (Device Tree)")
	.reserve	= integrator_reserve,
	.map_io		= ap_map_io,
	.init_irq	= ap_init_irq_of,
	.init_machine	= ap_init_of,
	.dt_compat      = ap_dt_board_compat,
MACHINE_END
