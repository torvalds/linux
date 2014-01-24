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
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/syscore_ops.h>
#include <linux/amba/bus.h>
#include <linux/amba/kmi.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irqchip/versatile-fpga.h>
#include <linux/mtd/physmap.h>
#include <linux/clk.h>
#include <linux/platform_data/clk-integrator.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/stat.h>
#include <linux/sys_soc.h>
#include <linux/termios.h>
#include <linux/sched_clock.h>

#include <mach/hardware.h>
#include <mach/platform.h>
#include <asm/hardware/arm_timer.h>
#include <asm/setup.h>
#include <asm/param.h>		/* HZ */
#include <asm/mach-types.h>

#include <mach/lm.h>

#include <asm/mach/arch.h>
#include <asm/mach/irq.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>

#include "cm.h"
#include "common.h"
#include "pci_v3.h"

/* Base address to the AP system controller */
void __iomem *ap_syscon_base;

/*
 * All IO addresses are mapped onto VA 0xFFFx.xxxx, where x.xxxx
 * is the (PA >> 12).
 *
 * Setup a VA for the Integrator interrupt controller (for header #0,
 * just for now).
 */
#define VA_IC_BASE	__io_address(INTEGRATOR_IC_BASE)
#define VA_EBI_BASE	__io_address(INTEGRATOR_EBI_BASE)
#define VA_CMIC_BASE	__io_address(INTEGRATOR_HDR_IC)

/*
 * Logical      Physical
 * ef000000			Cache flush
 * f1000000	10000000	Core module registers
 * f1100000	11000000	System controller registers
 * f1200000	12000000	EBI registers
 * f1300000	13000000	Counter/Timer
 * f1400000	14000000	Interrupt controller
 * f1600000	16000000	UART 0
 * f1700000	17000000	UART 1
 * f1a00000	1a000000	Debug LEDs
 * f1b00000	1b000000	GPIO
 */

static struct map_desc ap_io_desc[] __initdata __maybe_unused = {
	{
		.virtual	= IO_ADDRESS(INTEGRATOR_HDR_BASE),
		.pfn		= __phys_to_pfn(INTEGRATOR_HDR_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	= IO_ADDRESS(INTEGRATOR_EBI_BASE),
		.pfn		= __phys_to_pfn(INTEGRATOR_EBI_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	= IO_ADDRESS(INTEGRATOR_CT_BASE),
		.pfn		= __phys_to_pfn(INTEGRATOR_CT_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	= IO_ADDRESS(INTEGRATOR_IC_BASE),
		.pfn		= __phys_to_pfn(INTEGRATOR_IC_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	= IO_ADDRESS(INTEGRATOR_UART0_BASE),
		.pfn		= __phys_to_pfn(INTEGRATOR_UART0_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	= IO_ADDRESS(INTEGRATOR_DBG_BASE),
		.pfn		= __phys_to_pfn(INTEGRATOR_DBG_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	= IO_ADDRESS(INTEGRATOR_AP_GPIO_BASE),
		.pfn		= __phys_to_pfn(INTEGRATOR_AP_GPIO_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}
};

static void __init ap_map_io(void)
{
	iotable_init(ap_io_desc, ARRAY_SIZE(ap_io_desc));
	pci_v3_early_init();
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
 * Flash handling.
 */
#define EBI_CSR1 (VA_EBI_BASE + INTEGRATOR_EBI_CSR1_OFFSET)
#define EBI_LOCK (VA_EBI_BASE + INTEGRATOR_EBI_LOCK_OFFSET)

static int ap_flash_init(struct platform_device *dev)
{
	u32 tmp;

	writel(INTEGRATOR_SC_CTRL_nFLVPPEN | INTEGRATOR_SC_CTRL_nFLWP,
	       ap_syscon_base + INTEGRATOR_SC_CTRLC_OFFSET);

	tmp = readl(EBI_CSR1) | INTEGRATOR_EBI_WRITE_ENABLE;
	writel(tmp, EBI_CSR1);

	if (!(readl(EBI_CSR1) & INTEGRATOR_EBI_WRITE_ENABLE)) {
		writel(0xa05f, EBI_LOCK);
		writel(tmp, EBI_CSR1);
		writel(0, EBI_LOCK);
	}
	return 0;
}

static void ap_flash_exit(struct platform_device *dev)
{
	u32 tmp;

	writel(INTEGRATOR_SC_CTRL_nFLVPPEN | INTEGRATOR_SC_CTRL_nFLWP,
	       ap_syscon_base + INTEGRATOR_SC_CTRLC_OFFSET);

	tmp = readl(EBI_CSR1) & ~INTEGRATOR_EBI_WRITE_ENABLE;
	writel(tmp, EBI_CSR1);

	if (readl(EBI_CSR1) & INTEGRATOR_EBI_WRITE_ENABLE) {
		writel(0xa05f, EBI_LOCK);
		writel(tmp, EBI_CSR1);
		writel(0, EBI_LOCK);
	}
}

static void ap_flash_set_vpp(struct platform_device *pdev, int on)
{
	if (on)
		writel(INTEGRATOR_SC_CTRL_nFLVPPEN,
		       ap_syscon_base + INTEGRATOR_SC_CTRLS_OFFSET);
	else
		writel(INTEGRATOR_SC_CTRL_nFLVPPEN,
		       ap_syscon_base + INTEGRATOR_SC_CTRLC_OFFSET);
}

static struct physmap_flash_data ap_flash_data = {
	.width		= 4,
	.init		= ap_flash_init,
	.exit		= ap_flash_exit,
	.set_vpp	= ap_flash_set_vpp,
};

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

	__raw_writel(ctrls, ap_syscon_base + INTEGRATOR_SC_CTRLS_OFFSET);
	__raw_writel(ctrlc, ap_syscon_base + INTEGRATOR_SC_CTRLC_OFFSET);
}

struct amba_pl010_data ap_uart_data = {
	.set_mctrl = integrator_uart_set_mctrl,
};

/*
 * Where is the timer (VA)?
 */
#define TIMER0_VA_BASE __io_address(INTEGRATOR_TIMER0_BASE)
#define TIMER1_VA_BASE __io_address(INTEGRATOR_TIMER1_BASE)
#define TIMER2_VA_BASE __io_address(INTEGRATOR_TIMER2_BASE)

static unsigned long timer_reload;

static u64 notrace integrator_read_sched_clock(void)
{
	return -readl((void __iomem *) TIMER2_VA_BASE + TIMER_VALUE);
}

static void integrator_clocksource_init(unsigned long inrate,
					void __iomem *base)
{
	u32 ctrl = TIMER_CTRL_ENABLE | TIMER_CTRL_PERIODIC;
	unsigned long rate = inrate;

	if (rate >= 1500000) {
		rate /= 16;
		ctrl |= TIMER_CTRL_DIV16;
	}

	writel(0xffff, base + TIMER_LOAD);
	writel(ctrl, base + TIMER_CTRL);

	clocksource_mmio_init(base + TIMER_VALUE, "timer2",
			rate, 200, 16, clocksource_mmio_readl_down);
	sched_clock_register(integrator_read_sched_clock, 16, rate);
}

static void __iomem * clkevt_base;

/*
 * IRQ handler for the timer
 */
static irqreturn_t integrator_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;

	/* clear the interrupt */
	writel(1, clkevt_base + TIMER_INTCLR);

	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static void clkevt_set_mode(enum clock_event_mode mode, struct clock_event_device *evt)
{
	u32 ctrl = readl(clkevt_base + TIMER_CTRL) & ~TIMER_CTRL_ENABLE;

	/* Disable timer */
	writel(ctrl, clkevt_base + TIMER_CTRL);

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		/* Enable the timer and start the periodic tick */
		writel(timer_reload, clkevt_base + TIMER_LOAD);
		ctrl |= TIMER_CTRL_PERIODIC | TIMER_CTRL_ENABLE;
		writel(ctrl, clkevt_base + TIMER_CTRL);
		break;
	case CLOCK_EVT_MODE_ONESHOT:
		/* Leave the timer disabled, .set_next_event will enable it */
		ctrl &= ~TIMER_CTRL_PERIODIC;
		writel(ctrl, clkevt_base + TIMER_CTRL);
		break;
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
	case CLOCK_EVT_MODE_RESUME:
	default:
		/* Just leave in disabled state */
		break;
	}

}

static int clkevt_set_next_event(unsigned long next, struct clock_event_device *evt)
{
	unsigned long ctrl = readl(clkevt_base + TIMER_CTRL);

	writel(ctrl & ~TIMER_CTRL_ENABLE, clkevt_base + TIMER_CTRL);
	writel(next, clkevt_base + TIMER_LOAD);
	writel(ctrl | TIMER_CTRL_ENABLE, clkevt_base + TIMER_CTRL);

	return 0;
}

static struct clock_event_device integrator_clockevent = {
	.name		= "timer1",
	.features	= CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.set_mode	= clkevt_set_mode,
	.set_next_event	= clkevt_set_next_event,
	.rating		= 300,
};

static struct irqaction integrator_timer_irq = {
	.name		= "timer",
	.flags		= IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler	= integrator_timer_interrupt,
	.dev_id		= &integrator_clockevent,
};

static void integrator_clockevent_init(unsigned long inrate,
				void __iomem *base, int irq)
{
	unsigned long rate = inrate;
	unsigned int ctrl = 0;

	clkevt_base = base;
	/* Calculate and program a divisor */
	if (rate > 0x100000 * HZ) {
		rate /= 256;
		ctrl |= TIMER_CTRL_DIV256;
	} else if (rate > 0x10000 * HZ) {
		rate /= 16;
		ctrl |= TIMER_CTRL_DIV16;
	}
	timer_reload = rate / HZ;
	writel(ctrl, clkevt_base + TIMER_CTRL);

	setup_irq(irq, &integrator_timer_irq);
	clockevents_config_and_register(&integrator_clockevent,
					rate,
					1,
					0xffffU);
}

void __init ap_init_early(void)
{
}

static void __init ap_of_timer_init(void)
{
	struct device_node *node;
	const char *path;
	void __iomem *base;
	int err;
	int irq;
	struct clk *clk;
	unsigned long rate;

	clk = clk_get_sys("ap_timer", NULL);
	BUG_ON(IS_ERR(clk));
	clk_prepare_enable(clk);
	rate = clk_get_rate(clk);

	err = of_property_read_string(of_aliases,
				"arm,timer-primary", &path);
	if (WARN_ON(err))
		return;
	node = of_find_node_by_path(path);
	base = of_iomap(node, 0);
	if (WARN_ON(!base))
		return;
	writel(0, base + TIMER_CTRL);
	integrator_clocksource_init(rate, base);

	err = of_property_read_string(of_aliases,
				"arm,timer-secondary", &path);
	if (WARN_ON(err))
		return;
	node = of_find_node_by_path(path);
	base = of_iomap(node, 0);
	if (WARN_ON(!base))
		return;
	irq = irq_of_parse_and_map(node, 0);
	writel(0, base + TIMER_CTRL);
	integrator_clockevent_init(rate, base, irq);
}

static const struct of_device_id fpga_irq_of_match[] __initconst = {
	{ .compatible = "arm,versatile-fpga-irq", .data = fpga_irq_of_init, },
	{ /* Sentinel */ }
};

static void __init ap_init_irq_of(void)
{
	cm_init();
	of_irq_init(fpga_irq_of_match);
	integrator_clk_init(false);
}

/* For the Device Tree, add in the UART callbacks as AUXDATA */
static struct of_dev_auxdata ap_auxdata_lookup[] __initdata = {
	OF_DEV_AUXDATA("arm,primecell", INTEGRATOR_RTC_BASE,
		"rtc", NULL),
	OF_DEV_AUXDATA("arm,primecell", INTEGRATOR_UART0_BASE,
		"uart0", &ap_uart_data),
	OF_DEV_AUXDATA("arm,primecell", INTEGRATOR_UART1_BASE,
		"uart1", &ap_uart_data),
	OF_DEV_AUXDATA("arm,primecell", KMI0_BASE,
		"kmi0", NULL),
	OF_DEV_AUXDATA("arm,primecell", KMI1_BASE,
		"kmi1", NULL),
	OF_DEV_AUXDATA("cfi-flash", INTEGRATOR_FLASH_BASE,
		"physmap-flash", &ap_flash_data),
	{ /* sentinel */ },
};

static const struct of_device_id ap_syscon_match[] = {
	{ .compatible = "arm,integrator-ap-syscon"},
	{ },
};

static void __init ap_init_of(void)
{
	unsigned long sc_dec;
	struct device_node *root;
	struct device_node *syscon;
	struct device *parent;
	struct soc_device *soc_dev;
	struct soc_device_attribute *soc_dev_attr;
	u32 ap_sc_id;
	int err;
	int i;

	/* Here we create an SoC device for the root node */
	root = of_find_node_by_path("/");
	if (!root)
		return;

	syscon = of_find_matching_node(root, ap_syscon_match);
	if (!syscon)
		return;

	ap_syscon_base = of_iomap(syscon, 0);
	if (!ap_syscon_base)
		return;

	ap_sc_id = readl(ap_syscon_base);

	soc_dev_attr = kzalloc(sizeof(*soc_dev_attr), GFP_KERNEL);
	if (!soc_dev_attr)
		return;

	err = of_property_read_string(root, "compatible",
				      &soc_dev_attr->soc_id);
	if (err)
		return;
	err = of_property_read_string(root, "model", &soc_dev_attr->machine);
	if (err)
		return;
	soc_dev_attr->family = "Integrator";
	soc_dev_attr->revision = kasprintf(GFP_KERNEL, "%c",
					   'A' + (ap_sc_id & 0x0f));

	soc_dev = soc_device_register(soc_dev_attr);
	if (IS_ERR(soc_dev)) {
		kfree(soc_dev_attr->revision);
		kfree(soc_dev_attr);
		return;
	}

	parent = soc_device_to_device(soc_dev);
	integrator_init_sysfs(parent, ap_sc_id);

	of_platform_populate(root, of_default_bus_match_table,
			ap_auxdata_lookup, parent);

	sc_dec = readl(ap_syscon_base + INTEGRATOR_SC_DEC_OFFSET);
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
	.handle_irq	= fpga_handle_irq,
	.init_time	= ap_of_timer_init,
	.init_machine	= ap_init_of,
	.restart	= integrator_restart,
	.dt_compat      = ap_dt_board_compat,
MACHINE_END
