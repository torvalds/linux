/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003 Atheros Communications, Inc.,  All Rights Reserved.
 * Copyright (C) 2006 FON Technology, SL.
 * Copyright (C) 2006 Imre Kaloz <kaloz@openwrt.org>
 * Copyright (C) 2006 Felix Fietkau <nbd@openwrt.org>
 * Copyright (C) 2012 Alexandros C. Couloumbis <alex@ozo.com>
 */

/*
 * Platform devices for Atheros AR2315 SoCs
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/irqdomain.h>
#include <linux/interrupt.h>
#include <linux/reboot.h>
#include <asm/bootinfo.h>
#include <asm/reboot.h>
#include <asm/time.h>

#include "devices.h"
#include "ar2315.h"
#include "ar2315_regs.h"

static void __iomem *ar2315_rst_base;
static struct irq_domain *ar2315_misc_irq_domain;

static inline u32 ar2315_rst_reg_read(u32 reg)
{
	return __raw_readl(ar2315_rst_base + reg);
}

static inline void ar2315_rst_reg_write(u32 reg, u32 val)
{
	__raw_writel(val, ar2315_rst_base + reg);
}

static inline void ar2315_rst_reg_mask(u32 reg, u32 mask, u32 val)
{
	u32 ret = ar2315_rst_reg_read(reg);

	ret &= ~mask;
	ret |= val;
	ar2315_rst_reg_write(reg, ret);
}

static irqreturn_t ar2315_ahb_err_handler(int cpl, void *dev_id)
{
	ar2315_rst_reg_write(AR2315_AHB_ERR0, AR2315_AHB_ERROR_DET);
	ar2315_rst_reg_read(AR2315_AHB_ERR1);

	pr_emerg("AHB fatal error\n");
	machine_restart("AHB error"); /* Catastrophic failure */

	return IRQ_HANDLED;
}

static struct irqaction ar2315_ahb_err_interrupt  = {
	.handler	= ar2315_ahb_err_handler,
	.name		= "ar2315-ahb-error",
};

static void ar2315_misc_irq_handler(unsigned irq, struct irq_desc *desc)
{
	u32 pending = ar2315_rst_reg_read(AR2315_ISR) &
		      ar2315_rst_reg_read(AR2315_IMR);
	unsigned nr, misc_irq = 0;

	if (pending) {
		struct irq_domain *domain = irq_get_handler_data(irq);

		nr = __ffs(pending);
		misc_irq = irq_find_mapping(domain, nr);
	}

	if (misc_irq) {
		if (nr == AR2315_MISC_IRQ_GPIO)
			ar2315_rst_reg_write(AR2315_ISR, AR2315_ISR_GPIO);
		else if (nr == AR2315_MISC_IRQ_WATCHDOG)
			ar2315_rst_reg_write(AR2315_ISR, AR2315_ISR_WD);
		generic_handle_irq(misc_irq);
	} else {
		spurious_interrupt();
	}
}

static void ar2315_misc_irq_unmask(struct irq_data *d)
{
	ar2315_rst_reg_mask(AR2315_IMR, 0, BIT(d->hwirq));
}

static void ar2315_misc_irq_mask(struct irq_data *d)
{
	ar2315_rst_reg_mask(AR2315_IMR, BIT(d->hwirq), 0);
}

static struct irq_chip ar2315_misc_irq_chip = {
	.name		= "ar2315-misc",
	.irq_unmask	= ar2315_misc_irq_unmask,
	.irq_mask	= ar2315_misc_irq_mask,
};

static int ar2315_misc_irq_map(struct irq_domain *d, unsigned irq,
			       irq_hw_number_t hw)
{
	irq_set_chip_and_handler(irq, &ar2315_misc_irq_chip, handle_level_irq);
	return 0;
}

static struct irq_domain_ops ar2315_misc_irq_domain_ops = {
	.map = ar2315_misc_irq_map,
};

/*
 * Called when an interrupt is received, this function
 * determines exactly which interrupt it was, and it
 * invokes the appropriate handler.
 *
 * Implicitly, we also define interrupt priority by
 * choosing which to dispatch first.
 */
static void ar2315_irq_dispatch(void)
{
	u32 pending = read_c0_status() & read_c0_cause();

	if (pending & CAUSEF_IP3)
		do_IRQ(AR2315_IRQ_WLAN0);
	else if (pending & CAUSEF_IP2)
		do_IRQ(AR2315_IRQ_MISC);
	else if (pending & CAUSEF_IP7)
		do_IRQ(ATH25_IRQ_CPU_CLOCK);
	else
		spurious_interrupt();
}

void __init ar2315_arch_init_irq(void)
{
	struct irq_domain *domain;
	unsigned irq;

	ath25_irq_dispatch = ar2315_irq_dispatch;

	domain = irq_domain_add_linear(NULL, AR2315_MISC_IRQ_COUNT,
				       &ar2315_misc_irq_domain_ops, NULL);
	if (!domain)
		panic("Failed to add IRQ domain");

	irq = irq_create_mapping(domain, AR2315_MISC_IRQ_AHB);
	setup_irq(irq, &ar2315_ahb_err_interrupt);

	irq_set_chained_handler(AR2315_IRQ_MISC, ar2315_misc_irq_handler);
	irq_set_handler_data(AR2315_IRQ_MISC, domain);

	ar2315_misc_irq_domain = domain;
}

static void ar2315_restart(char *command)
{
	void (*mips_reset_vec)(void) = (void *)0xbfc00000;

	local_irq_disable();

	/* try reset the system via reset control */
	ar2315_rst_reg_write(AR2315_COLD_RESET, AR2317_RESET_SYSTEM);

	/* Cold reset does not work on the AR2315/6, use the GPIO reset bits
	 * a workaround. Give it some time to attempt a gpio based hardware
	 * reset (atheros reference design workaround) */

	/* TODO: implement the GPIO reset workaround */

	/* Some boards (e.g. Senao EOC-2610) don't implement the reset logic
	 * workaround. Attempt to jump to the mips reset location -
	 * the boot loader itself might be able to recover the system */
	mips_reset_vec();
}

/*
 * This table is indexed by bits 5..4 of the CLOCKCTL1 register
 * to determine the predevisor value.
 */
static int clockctl1_predivide_table[4] __initdata = { 1, 2, 4, 5 };
static int pllc_divide_table[5] __initdata = { 2, 3, 4, 6, 3 };

static unsigned __init ar2315_sys_clk(u32 clock_ctl)
{
	unsigned int pllc_ctrl, cpu_div;
	unsigned int pllc_out, refdiv, fdiv, divby2;
	unsigned int clk_div;

	pllc_ctrl = ar2315_rst_reg_read(AR2315_PLLC_CTL);
	refdiv = ATH25_REG_MS(pllc_ctrl, AR2315_PLLC_REF_DIV);
	refdiv = clockctl1_predivide_table[refdiv];
	fdiv = ATH25_REG_MS(pllc_ctrl, AR2315_PLLC_FDBACK_DIV);
	divby2 = ATH25_REG_MS(pllc_ctrl, AR2315_PLLC_ADD_FDBACK_DIV) + 1;
	pllc_out = (40000000 / refdiv) * (2 * divby2) * fdiv;

	/* clkm input selected */
	switch (clock_ctl & AR2315_CPUCLK_CLK_SEL_M) {
	case 0:
	case 1:
		clk_div = ATH25_REG_MS(pllc_ctrl, AR2315_PLLC_CLKM_DIV);
		clk_div = pllc_divide_table[clk_div];
		break;
	case 2:
		clk_div = ATH25_REG_MS(pllc_ctrl, AR2315_PLLC_CLKC_DIV);
		clk_div = pllc_divide_table[clk_div];
		break;
	default:
		pllc_out = 40000000;
		clk_div = 1;
		break;
	}

	cpu_div = ATH25_REG_MS(clock_ctl, AR2315_CPUCLK_CLK_DIV);
	cpu_div = cpu_div * 2 ?: 1;

	return pllc_out / (clk_div * cpu_div);
}

static inline unsigned ar2315_cpu_frequency(void)
{
	return ar2315_sys_clk(ar2315_rst_reg_read(AR2315_CPUCLK));
}

static inline unsigned ar2315_apb_frequency(void)
{
	return ar2315_sys_clk(ar2315_rst_reg_read(AR2315_AMBACLK));
}

void __init ar2315_plat_time_init(void)
{
	mips_hpt_frequency = ar2315_cpu_frequency() / 2;
}

void __init ar2315_plat_mem_setup(void)
{
	void __iomem *sdram_base;
	u32 memsize, memcfg;
	u32 config;

	/* Detect memory size */
	sdram_base = ioremap_nocache(AR2315_SDRAMCTL_BASE,
				     AR2315_SDRAMCTL_SIZE);
	memcfg = __raw_readl(sdram_base + AR2315_MEM_CFG);
	memsize   = 1 + ATH25_REG_MS(memcfg, AR2315_MEM_CFG_DATA_WIDTH);
	memsize <<= 1 + ATH25_REG_MS(memcfg, AR2315_MEM_CFG_COL_WIDTH);
	memsize <<= 1 + ATH25_REG_MS(memcfg, AR2315_MEM_CFG_ROW_WIDTH);
	memsize <<= 3;
	add_memory_region(0, memsize, BOOT_MEM_RAM);
	iounmap(sdram_base);

	ar2315_rst_base = ioremap_nocache(AR2315_RST_BASE, AR2315_RST_SIZE);

	/* Clear any lingering AHB errors */
	config = read_c0_config();
	write_c0_config(config & ~0x3);
	ar2315_rst_reg_write(AR2315_AHB_ERR0, AR2315_AHB_ERROR_DET);
	ar2315_rst_reg_read(AR2315_AHB_ERR1);
	ar2315_rst_reg_write(AR2315_WDT_CTRL, AR2315_WDT_CTRL_IGNORE);

	_machine_restart = ar2315_restart;
}
