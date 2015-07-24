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
#include <linux/io.h>
#include <linux/irqchip.h>
#include <linux/mtd/physmap.h>
#include <linux/platform_data/clk-integrator.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/stat.h>
#include <linux/termios.h>

#include <asm/setup.h>
#include <asm/param.h>		/* HZ */
#include <asm/mach-types.h>

#include <asm/mach/arch.h>
#include <asm/mach/irq.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>

#include "hardware.h"
#include "cm.h"
#include "common.h"
#include "pci_v3.h"
#include "lm.h"

/* Base address to the AP system controller */
void __iomem *ap_syscon_base;
/* Base address to the external bus interface */
static void __iomem *ebi_base;


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
 * ef000000			Cache flush
 * f1100000	11000000	System controller registers
 * f1300000	13000000	Counter/Timer
 * f1400000	14000000	Interrupt controller
 * f1600000	16000000	UART 0
 * f1700000	17000000	UART 1
 * f1a00000	1a000000	Debug LEDs
 * f1b00000	1b000000	GPIO
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
static int ap_flash_init(struct platform_device *dev)
{
	u32 tmp;

	writel(INTEGRATOR_SC_CTRL_nFLVPPEN | INTEGRATOR_SC_CTRL_nFLWP,
	       ap_syscon_base + INTEGRATOR_SC_CTRLC_OFFSET);

	tmp = readl(ebi_base + INTEGRATOR_EBI_CSR1_OFFSET) |
		INTEGRATOR_EBI_WRITE_ENABLE;
	writel(tmp, ebi_base + INTEGRATOR_EBI_CSR1_OFFSET);

	if (!(readl(ebi_base + INTEGRATOR_EBI_CSR1_OFFSET)
	      & INTEGRATOR_EBI_WRITE_ENABLE)) {
		writel(0xa05f, ebi_base + INTEGRATOR_EBI_LOCK_OFFSET);
		writel(tmp, ebi_base + INTEGRATOR_EBI_CSR1_OFFSET);
		writel(0, ebi_base + INTEGRATOR_EBI_LOCK_OFFSET);
	}
	return 0;
}

static void ap_flash_exit(struct platform_device *dev)
{
	u32 tmp;

	writel(INTEGRATOR_SC_CTRL_nFLVPPEN | INTEGRATOR_SC_CTRL_nFLWP,
	       ap_syscon_base + INTEGRATOR_SC_CTRLC_OFFSET);

	tmp = readl(ebi_base + INTEGRATOR_EBI_CSR1_OFFSET) &
		~INTEGRATOR_EBI_WRITE_ENABLE;
	writel(tmp, ebi_base + INTEGRATOR_EBI_CSR1_OFFSET);

	if (readl(ebi_base + INTEGRATOR_EBI_CSR1_OFFSET) &
	    INTEGRATOR_EBI_WRITE_ENABLE) {
		writel(0xa05f, ebi_base + INTEGRATOR_EBI_LOCK_OFFSET);
		writel(tmp, ebi_base + INTEGRATOR_EBI_CSR1_OFFSET);
		writel(0, ebi_base + INTEGRATOR_EBI_LOCK_OFFSET);
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

static const struct of_device_id ebi_match[] = {
	{ .compatible = "arm,external-bus-interface"},
	{ },
};

static void __init ap_init_of(void)
{
	unsigned long sc_dec;
	struct device_node *syscon;
	struct device_node *ebi;
	int i;

	syscon = of_find_matching_node(NULL, ap_syscon_match);
	if (!syscon)
		return;
	ebi = of_find_matching_node(NULL, ebi_match);
	if (!ebi)
		return;

	ap_syscon_base = of_iomap(syscon, 0);
	if (!ap_syscon_base)
		return;
	ebi_base = of_iomap(ebi, 0);
	if (!ebi_base)
		return;

	of_platform_populate(NULL, of_default_bus_match_table,
			ap_auxdata_lookup, NULL);

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
	.init_machine	= ap_init_of,
	.dt_compat      = ap_dt_board_compat,
MACHINE_END
