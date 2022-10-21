// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 1999 - 2003 ARM Limited
 * Copyright 2000 Deep Blue Solutions Ltd
 * Copyright 2008 Cavium Networks
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/clockchips.h>
#include <linux/io.h>
#include <linux/irqchip/arm-gic.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/usb/ehci_pdriver.h>
#include <linux/usb/ohci_pdriver.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include <asm/mach/irq.h>
#include <asm/hardware/cache-l2x0.h>
#include "cns3xxx.h"
#include "core.h"
#include "pm.h"

static struct map_desc cns3xxx_io_desc[] __initdata = {
	{
		.virtual	= CNS3XXX_TC11MP_SCU_BASE_VIRT,
		.pfn		= __phys_to_pfn(CNS3XXX_TC11MP_SCU_BASE),
		.length		= SZ_8K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= CNS3XXX_TIMER1_2_3_BASE_VIRT,
		.pfn		= __phys_to_pfn(CNS3XXX_TIMER1_2_3_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= CNS3XXX_MISC_BASE_VIRT,
		.pfn		= __phys_to_pfn(CNS3XXX_MISC_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= CNS3XXX_PM_BASE_VIRT,
		.pfn		= __phys_to_pfn(CNS3XXX_PM_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
#ifdef CONFIG_PCI
	}, {
		.virtual	= CNS3XXX_PCIE0_HOST_BASE_VIRT,
		.pfn		= __phys_to_pfn(CNS3XXX_PCIE0_HOST_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= CNS3XXX_PCIE0_CFG0_BASE_VIRT,
		.pfn		= __phys_to_pfn(CNS3XXX_PCIE0_CFG0_BASE),
		.length		= SZ_64K, /* really 4 KiB at offset 32 KiB */
		.type		= MT_DEVICE,
	}, {
		.virtual	= CNS3XXX_PCIE0_CFG1_BASE_VIRT,
		.pfn		= __phys_to_pfn(CNS3XXX_PCIE0_CFG1_BASE),
		.length		= SZ_16M,
		.type		= MT_DEVICE,
	}, {
		.virtual	= CNS3XXX_PCIE1_HOST_BASE_VIRT,
		.pfn		= __phys_to_pfn(CNS3XXX_PCIE1_HOST_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= CNS3XXX_PCIE1_CFG0_BASE_VIRT,
		.pfn		= __phys_to_pfn(CNS3XXX_PCIE1_CFG0_BASE),
		.length		= SZ_64K, /* really 4 KiB at offset 32 KiB */
		.type		= MT_DEVICE,
	}, {
		.virtual	= CNS3XXX_PCIE1_CFG1_BASE_VIRT,
		.pfn		= __phys_to_pfn(CNS3XXX_PCIE1_CFG1_BASE),
		.length		= SZ_16M,
		.type		= MT_DEVICE,
#endif
	},
};

void __init cns3xxx_map_io(void)
{
	iotable_init(cns3xxx_io_desc, ARRAY_SIZE(cns3xxx_io_desc));
}

/* used by entry-macro.S */
void __init cns3xxx_init_irq(void)
{
	gic_init(IOMEM(CNS3XXX_TC11MP_GIC_DIST_BASE_VIRT),
		 IOMEM(CNS3XXX_TC11MP_GIC_CPU_BASE_VIRT));
}

void cns3xxx_power_off(void)
{
	u32 __iomem *pm_base = IOMEM(CNS3XXX_PM_BASE_VIRT);
	u32 clkctrl;

	printk(KERN_INFO "powering system down...\n");

	clkctrl = readl(pm_base + PM_SYS_CLK_CTRL_OFFSET);
	clkctrl &= 0xfffff1ff;
	clkctrl |= (0x5 << 9);		/* Hibernate */
	writel(clkctrl, pm_base + PM_SYS_CLK_CTRL_OFFSET);

}

/*
 * Timer
 */
static void __iomem *cns3xxx_tmr1;

static int cns3xxx_shutdown(struct clock_event_device *clk)
{
	writel(0, cns3xxx_tmr1 + TIMER1_2_CONTROL_OFFSET);
	return 0;
}

static int cns3xxx_set_oneshot(struct clock_event_device *clk)
{
	unsigned long ctrl = readl(cns3xxx_tmr1 + TIMER1_2_CONTROL_OFFSET);

	/* period set, and timer enabled in 'next_event' hook */
	ctrl |= (1 << 2) | (1 << 9);
	writel(ctrl, cns3xxx_tmr1 + TIMER1_2_CONTROL_OFFSET);
	return 0;
}

static int cns3xxx_set_periodic(struct clock_event_device *clk)
{
	unsigned long ctrl = readl(cns3xxx_tmr1 + TIMER1_2_CONTROL_OFFSET);
	int pclk = cns3xxx_cpu_clock() / 8;
	int reload;

	reload = pclk * 20 / (3 * HZ) * 0x25000;
	writel(reload, cns3xxx_tmr1 + TIMER1_AUTO_RELOAD_OFFSET);
	ctrl |= (1 << 0) | (1 << 2) | (1 << 9);
	writel(ctrl, cns3xxx_tmr1 + TIMER1_2_CONTROL_OFFSET);
	return 0;
}

static int cns3xxx_timer_set_next_event(unsigned long evt,
					struct clock_event_device *unused)
{
	unsigned long ctrl = readl(cns3xxx_tmr1 + TIMER1_2_CONTROL_OFFSET);

	writel(evt, cns3xxx_tmr1 + TIMER1_AUTO_RELOAD_OFFSET);
	writel(ctrl | (1 << 0), cns3xxx_tmr1 + TIMER1_2_CONTROL_OFFSET);

	return 0;
}

static struct clock_event_device cns3xxx_tmr1_clockevent = {
	.name			= "cns3xxx timer1",
	.features		= CLOCK_EVT_FEAT_PERIODIC |
				  CLOCK_EVT_FEAT_ONESHOT,
	.set_state_shutdown	= cns3xxx_shutdown,
	.set_state_periodic	= cns3xxx_set_periodic,
	.set_state_oneshot	= cns3xxx_set_oneshot,
	.tick_resume		= cns3xxx_shutdown,
	.set_next_event		= cns3xxx_timer_set_next_event,
	.rating			= 350,
	.cpumask		= cpu_all_mask,
};

static void __init cns3xxx_clockevents_init(unsigned int timer_irq)
{
	cns3xxx_tmr1_clockevent.irq = timer_irq;
	clockevents_config_and_register(&cns3xxx_tmr1_clockevent,
					(cns3xxx_cpu_clock() >> 3) * 1000000,
					0xf, 0xffffffff);
}

/*
 * IRQ handler for the timer
 */
static irqreturn_t cns3xxx_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = &cns3xxx_tmr1_clockevent;
	u32 __iomem *stat = cns3xxx_tmr1 + TIMER1_2_INTERRUPT_STATUS_OFFSET;
	u32 val;

	/* Clear the interrupt */
	val = readl(stat);
	writel(val & ~(1 << 2), stat);

	evt->event_handler(evt);

	return IRQ_HANDLED;
}

/*
 * Set up the clock source and clock events devices
 */
static void __init __cns3xxx_timer_init(unsigned int timer_irq)
{
	u32 val;
	u32 irq_mask;

	/*
	 * Initialise to a known state (all timers off)
	 */

	/* disable timer1 and timer2 */
	writel(0, cns3xxx_tmr1 + TIMER1_2_CONTROL_OFFSET);
	/* stop free running timer3 */
	writel(0, cns3xxx_tmr1 + TIMER_FREERUN_CONTROL_OFFSET);

	/* timer1 */
	writel(0x5C800, cns3xxx_tmr1 + TIMER1_COUNTER_OFFSET);
	writel(0x5C800, cns3xxx_tmr1 + TIMER1_AUTO_RELOAD_OFFSET);

	writel(0, cns3xxx_tmr1 + TIMER1_MATCH_V1_OFFSET);
	writel(0, cns3xxx_tmr1 + TIMER1_MATCH_V2_OFFSET);

	/* mask irq, non-mask timer1 overflow */
	irq_mask = readl(cns3xxx_tmr1 + TIMER1_2_INTERRUPT_MASK_OFFSET);
	irq_mask &= ~(1 << 2);
	irq_mask |= 0x03;
	writel(irq_mask, cns3xxx_tmr1 + TIMER1_2_INTERRUPT_MASK_OFFSET);

	/* down counter */
	val = readl(cns3xxx_tmr1 + TIMER1_2_CONTROL_OFFSET);
	val |= (1 << 9);
	writel(val, cns3xxx_tmr1 + TIMER1_2_CONTROL_OFFSET);

	/* timer2 */
	writel(0, cns3xxx_tmr1 + TIMER2_MATCH_V1_OFFSET);
	writel(0, cns3xxx_tmr1 + TIMER2_MATCH_V2_OFFSET);

	/* mask irq */
	irq_mask = readl(cns3xxx_tmr1 + TIMER1_2_INTERRUPT_MASK_OFFSET);
	irq_mask |= ((1 << 3) | (1 << 4) | (1 << 5));
	writel(irq_mask, cns3xxx_tmr1 + TIMER1_2_INTERRUPT_MASK_OFFSET);

	/* down counter */
	val = readl(cns3xxx_tmr1 + TIMER1_2_CONTROL_OFFSET);
	val |= (1 << 10);
	writel(val, cns3xxx_tmr1 + TIMER1_2_CONTROL_OFFSET);

	/* Make irqs happen for the system timer */
	if (request_irq(timer_irq, cns3xxx_timer_interrupt,
			IRQF_TIMER | IRQF_IRQPOLL, "timer", NULL))
		pr_err("Failed to request irq %d (timer)\n", timer_irq);

	cns3xxx_clockevents_init(timer_irq);
}

void __init cns3xxx_timer_init(void)
{
	cns3xxx_tmr1 = IOMEM(CNS3XXX_TIMER1_2_3_BASE_VIRT);

	__cns3xxx_timer_init(IRQ_CNS3XXX_TIMER0);
}

#ifdef CONFIG_CACHE_L2X0

void __init cns3xxx_l2x0_init(void)
{
	void __iomem *base = ioremap(CNS3XXX_L2C_BASE, SZ_4K);
	u32 val;

	if (WARN_ON(!base))
		return;

	/*
	 * Tag RAM Control register
	 *
	 * bit[10:8]	- 1 cycle of write accesses latency
	 * bit[6:4]	- 1 cycle of read accesses latency
	 * bit[3:0]	- 1 cycle of setup latency
	 *
	 * 1 cycle of latency for setup, read and write accesses
	 */
	val = readl(base + L310_TAG_LATENCY_CTRL);
	val &= 0xfffff888;
	writel(val, base + L310_TAG_LATENCY_CTRL);

	/*
	 * Data RAM Control register
	 *
	 * bit[10:8]	- 1 cycles of write accesses latency
	 * bit[6:4]	- 1 cycles of read accesses latency
	 * bit[3:0]	- 1 cycle of setup latency
	 *
	 * 1 cycle of latency for setup, read and write accesses
	 */
	val = readl(base + L310_DATA_LATENCY_CTRL);
	val &= 0xfffff888;
	writel(val, base + L310_DATA_LATENCY_CTRL);

	/* 32 KiB, 8-way, parity disable */
	l2x0_init(base, 0x00500000, 0xfe0f0fff);
}

#endif /* CONFIG_CACHE_L2X0 */

static int csn3xxx_usb_power_on(struct platform_device *pdev)
{
	/*
	 * EHCI and OHCI share the same clock and power,
	 * resetting twice would cause the 1st controller been reset.
	 * Therefore only do power up  at the first up device, and
	 * power down at the last down device.
	 *
	 * Set USB AHB INCR length to 16
	 */
	if (atomic_inc_return(&usb_pwr_ref) == 1) {
		cns3xxx_pwr_power_up(1 << PM_PLL_HM_PD_CTRL_REG_OFFSET_PLL_USB);
		cns3xxx_pwr_clk_en(1 << PM_CLK_GATE_REG_OFFSET_USB_HOST);
		cns3xxx_pwr_soft_rst(1 << PM_SOFT_RST_REG_OFFST_USB_HOST);
		__raw_writel((__raw_readl(MISC_CHIP_CONFIG_REG) | (0X2 << 24)),
			MISC_CHIP_CONFIG_REG);
	}

	return 0;
}

static void csn3xxx_usb_power_off(struct platform_device *pdev)
{
	/*
	 * EHCI and OHCI share the same clock and power,
	 * resetting twice would cause the 1st controller been reset.
	 * Therefore only do power up  at the first up device, and
	 * power down at the last down device.
	 */
	if (atomic_dec_return(&usb_pwr_ref) == 0)
		cns3xxx_pwr_clk_dis(1 << PM_CLK_GATE_REG_OFFSET_USB_HOST);
}

static struct usb_ehci_pdata cns3xxx_usb_ehci_pdata = {
	.power_on	= csn3xxx_usb_power_on,
	.power_off	= csn3xxx_usb_power_off,
};

static struct usb_ohci_pdata cns3xxx_usb_ohci_pdata = {
	.num_ports	= 1,
	.power_on	= csn3xxx_usb_power_on,
	.power_off	= csn3xxx_usb_power_off,
};

static const struct of_dev_auxdata cns3xxx_auxdata[] __initconst = {
	{ "intel,usb-ehci", CNS3XXX_USB_BASE, "ehci-platform", &cns3xxx_usb_ehci_pdata },
	{ "intel,usb-ohci", CNS3XXX_USB_OHCI_BASE, "ohci-platform", &cns3xxx_usb_ohci_pdata },
	{ "cavium,cns3420-ahci", CNS3XXX_SATA2_BASE, "ahci", NULL },
	{ "cavium,cns3420-sdhci", CNS3XXX_SDIO_BASE, "ahci", NULL },
	{},
};

static void __init cns3xxx_init(void)
{
	struct device_node *dn;

	cns3xxx_l2x0_init();

	dn = of_find_compatible_node(NULL, NULL, "cavium,cns3420-ahci");
	if (of_device_is_available(dn)) {
		u32 tmp;
	
		tmp = __raw_readl(MISC_SATA_POWER_MODE);
		tmp |= 0x1 << 16; /* Disable SATA PHY 0 from SLUMBER Mode */
		tmp |= 0x1 << 17; /* Disable SATA PHY 1 from SLUMBER Mode */
		__raw_writel(tmp, MISC_SATA_POWER_MODE);
	
		/* Enable SATA PHY */
		cns3xxx_pwr_power_up(0x1 << PM_PLL_HM_PD_CTRL_REG_OFFSET_SATA_PHY0);
		cns3xxx_pwr_power_up(0x1 << PM_PLL_HM_PD_CTRL_REG_OFFSET_SATA_PHY1);
	
		/* Enable SATA Clock */
		cns3xxx_pwr_clk_en(0x1 << PM_CLK_GATE_REG_OFFSET_SATA);
	
		/* De-Asscer SATA Reset */
		cns3xxx_pwr_soft_rst(CNS3XXX_PWR_SOFTWARE_RST(SATA));
	}
	of_node_put(dn);

	dn = of_find_compatible_node(NULL, NULL, "cavium,cns3420-sdhci");
	if (of_device_is_available(dn)) {
		u32 __iomem *gpioa = IOMEM(CNS3XXX_MISC_BASE_VIRT + 0x0014);
		u32 gpioa_pins = __raw_readl(gpioa);
	
		/* MMC/SD pins share with GPIOA */
		gpioa_pins |= 0x1fff0004;
		__raw_writel(gpioa_pins, gpioa);
	
		cns3xxx_pwr_clk_en(CNS3XXX_PWR_CLK_EN(SDIO));
		cns3xxx_pwr_soft_rst(CNS3XXX_PWR_SOFTWARE_RST(SDIO));
	}
	of_node_put(dn);

	pm_power_off = cns3xxx_power_off;

	of_platform_default_populate(NULL, cns3xxx_auxdata, NULL);
}

static const char *const cns3xxx_dt_compat[] __initconst = {
	"cavium,cns3410",
	"cavium,cns3420",
	NULL,
};

DT_MACHINE_START(CNS3XXX_DT, "Cavium Networks CNS3xxx")
	.dt_compat	= cns3xxx_dt_compat,
	.map_io		= cns3xxx_map_io,
	.init_irq	= cns3xxx_init_irq,
	.init_time	= cns3xxx_timer_init,
	.init_machine	= cns3xxx_init,
	.init_late	= cns3xxx_pcie_init_late,
	.restart	= cns3xxx_restart,
MACHINE_END
