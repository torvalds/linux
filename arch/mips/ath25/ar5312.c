/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003 Atheros Communications, Inc.,  All Rights Reserved.
 * Copyright (C) 2006 FON Technology, SL.
 * Copyright (C) 2006 Imre Kaloz <kaloz@openwrt.org>
 * Copyright (C) 2006-2009 Felix Fietkau <nbd@openwrt.org>
 * Copyright (C) 2012 Alexandros C. Couloumbis <alex@ozo.com>
 */

/*
 * Platform devices for Atheros AR5312 SoCs
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/irqdomain.h>
#include <linux/interrupt.h>
#include <linux/memblock.h>
#include <linux/platform_device.h>
#include <linux/mtd/physmap.h>
#include <linux/reboot.h>
#include <asm/bootinfo.h>
#include <asm/reboot.h>
#include <asm/time.h>

#include <ath25_platform.h>

#include "devices.h"
#include "ar5312.h"
#include "ar5312_regs.h"

static void __iomem *ar5312_rst_base;
static struct irq_domain *ar5312_misc_irq_domain;

static inline u32 ar5312_rst_reg_read(u32 reg)
{
	return __raw_readl(ar5312_rst_base + reg);
}

static inline void ar5312_rst_reg_write(u32 reg, u32 val)
{
	__raw_writel(val, ar5312_rst_base + reg);
}

static inline void ar5312_rst_reg_mask(u32 reg, u32 mask, u32 val)
{
	u32 ret = ar5312_rst_reg_read(reg);

	ret &= ~mask;
	ret |= val;
	ar5312_rst_reg_write(reg, ret);
}

static irqreturn_t ar5312_ahb_err_handler(int cpl, void *dev_id)
{
	u32 proc1 = ar5312_rst_reg_read(AR5312_PROC1);
	u32 proc_addr = ar5312_rst_reg_read(AR5312_PROCADDR); /* clears error */
	u32 dma1 = ar5312_rst_reg_read(AR5312_DMA1);
	u32 dma_addr = ar5312_rst_reg_read(AR5312_DMAADDR);   /* clears error */

	pr_emerg("AHB interrupt: PROCADDR=0x%8.8x PROC1=0x%8.8x DMAADDR=0x%8.8x DMA1=0x%8.8x\n",
		 proc_addr, proc1, dma_addr, dma1);

	machine_restart("AHB error"); /* Catastrophic failure */
	return IRQ_HANDLED;
}

static void ar5312_misc_irq_handler(struct irq_desc *desc)
{
	u32 pending = ar5312_rst_reg_read(AR5312_ISR) &
		      ar5312_rst_reg_read(AR5312_IMR);
	unsigned nr;
	int ret = 0;

	if (pending) {
		struct irq_domain *domain = irq_desc_get_handler_data(desc);

		nr = __ffs(pending);

		ret = generic_handle_domain_irq(domain, nr);
		if (nr == AR5312_MISC_IRQ_TIMER)
			ar5312_rst_reg_read(AR5312_TIMER);
	}

	if (!pending || ret)
		spurious_interrupt();
}

/* Enable the specified AR5312_MISC_IRQ interrupt */
static void ar5312_misc_irq_unmask(struct irq_data *d)
{
	ar5312_rst_reg_mask(AR5312_IMR, 0, BIT(d->hwirq));
}

/* Disable the specified AR5312_MISC_IRQ interrupt */
static void ar5312_misc_irq_mask(struct irq_data *d)
{
	ar5312_rst_reg_mask(AR5312_IMR, BIT(d->hwirq), 0);
	ar5312_rst_reg_read(AR5312_IMR); /* flush write buffer */
}

static struct irq_chip ar5312_misc_irq_chip = {
	.name		= "ar5312-misc",
	.irq_unmask	= ar5312_misc_irq_unmask,
	.irq_mask	= ar5312_misc_irq_mask,
};

static int ar5312_misc_irq_map(struct irq_domain *d, unsigned irq,
			       irq_hw_number_t hw)
{
	irq_set_chip_and_handler(irq, &ar5312_misc_irq_chip, handle_level_irq);
	return 0;
}

static const struct irq_domain_ops ar5312_misc_irq_domain_ops = {
	.map = ar5312_misc_irq_map,
};

static void ar5312_irq_dispatch(void)
{
	u32 pending = read_c0_status() & read_c0_cause();

	if (pending & CAUSEF_IP2)
		do_IRQ(AR5312_IRQ_WLAN0);
	else if (pending & CAUSEF_IP5)
		do_IRQ(AR5312_IRQ_WLAN1);
	else if (pending & CAUSEF_IP6)
		do_IRQ(AR5312_IRQ_MISC);
	else if (pending & CAUSEF_IP7)
		do_IRQ(ATH25_IRQ_CPU_CLOCK);
	else
		spurious_interrupt();
}

void __init ar5312_arch_init_irq(void)
{
	struct irq_domain *domain;
	unsigned irq;

	ath25_irq_dispatch = ar5312_irq_dispatch;

	domain = irq_domain_create_linear(NULL, AR5312_MISC_IRQ_COUNT,
					  &ar5312_misc_irq_domain_ops, NULL);
	if (!domain)
		panic("Failed to add IRQ domain");

	irq = irq_create_mapping(domain, AR5312_MISC_IRQ_AHB_PROC);
	if (request_irq(irq, ar5312_ahb_err_handler, 0, "ar5312-ahb-error",
			NULL))
		pr_err("Failed to register ar5312-ahb-error interrupt\n");

	irq_set_chained_handler_and_data(AR5312_IRQ_MISC,
					 ar5312_misc_irq_handler, domain);

	ar5312_misc_irq_domain = domain;
}

static struct physmap_flash_data ar5312_flash_data = {
	.width = 2,
};

static struct resource ar5312_flash_resource = {
	.start = AR5312_FLASH_BASE,
	.end = AR5312_FLASH_BASE + AR5312_FLASH_SIZE - 1,
	.flags = IORESOURCE_MEM,
};

static struct platform_device ar5312_physmap_flash = {
	.name = "physmap-flash",
	.id = 0,
	.dev.platform_data = &ar5312_flash_data,
	.resource = &ar5312_flash_resource,
	.num_resources = 1,
};

static void __init ar5312_flash_init(void)
{
	void __iomem *flashctl_base;
	u32 ctl;

	flashctl_base = ioremap(AR5312_FLASHCTL_BASE,
					AR5312_FLASHCTL_SIZE);

	ctl = __raw_readl(flashctl_base + AR5312_FLASHCTL0);
	ctl &= AR5312_FLASHCTL_MW;

	/* fixup flash width */
	switch (ctl) {
	case AR5312_FLASHCTL_MW16:
		ar5312_flash_data.width = 2;
		break;
	case AR5312_FLASHCTL_MW8:
	default:
		ar5312_flash_data.width = 1;
		break;
	}

	/*
	 * Configure flash bank 0.
	 * Assume 8M window size. Flash will be aliased if it's smaller
	 */
	ctl |= AR5312_FLASHCTL_E | AR5312_FLASHCTL_AC_8M | AR5312_FLASHCTL_RBLE;
	ctl |= 0x01 << AR5312_FLASHCTL_IDCY_S;
	ctl |= 0x07 << AR5312_FLASHCTL_WST1_S;
	ctl |= 0x07 << AR5312_FLASHCTL_WST2_S;
	__raw_writel(ctl, flashctl_base + AR5312_FLASHCTL0);

	/* Disable other flash banks */
	ctl = __raw_readl(flashctl_base + AR5312_FLASHCTL1);
	ctl &= ~(AR5312_FLASHCTL_E | AR5312_FLASHCTL_AC);
	__raw_writel(ctl, flashctl_base + AR5312_FLASHCTL1);
	ctl = __raw_readl(flashctl_base + AR5312_FLASHCTL2);
	ctl &= ~(AR5312_FLASHCTL_E | AR5312_FLASHCTL_AC);
	__raw_writel(ctl, flashctl_base + AR5312_FLASHCTL2);

	iounmap(flashctl_base);
}

void __init ar5312_init_devices(void)
{
	struct ath25_boarddata *config;

	ar5312_flash_init();

	/* Locate board/radio config data */
	ath25_find_config(AR5312_FLASH_BASE, AR5312_FLASH_SIZE);
	config = ath25_board.config;

	/* AR2313 has CPU minor rev. 10 */
	if ((current_cpu_data.processor_id & 0xff) == 0x0a)
		ath25_soc = ATH25_SOC_AR2313;

	/* AR2312 shares the same Silicon ID as AR5312 */
	else if (config->flags & BD_ISCASPER)
		ath25_soc = ATH25_SOC_AR2312;

	/* Everything else is probably AR5312 or compatible */
	else
		ath25_soc = ATH25_SOC_AR5312;

	platform_device_register(&ar5312_physmap_flash);

	switch (ath25_soc) {
	case ATH25_SOC_AR5312:
		if (!ath25_board.radio)
			return;

		if (!(config->flags & BD_WLAN0))
			break;

		ath25_add_wmac(0, AR5312_WLAN0_BASE, AR5312_IRQ_WLAN0);
		break;
	case ATH25_SOC_AR2312:
	case ATH25_SOC_AR2313:
		if (!ath25_board.radio)
			return;
		break;
	default:
		break;
	}

	if (config->flags & BD_WLAN1)
		ath25_add_wmac(1, AR5312_WLAN1_BASE, AR5312_IRQ_WLAN1);
}

static void ar5312_restart(char *command)
{
	/* reset the system */
	local_irq_disable();
	while (1)
		ar5312_rst_reg_write(AR5312_RESET, AR5312_RESET_SYSTEM);
}

/*
 * This table is indexed by bits 5..4 of the CLOCKCTL1 register
 * to determine the predevisor value.
 */
static unsigned clockctl1_predivide_table[4] __initdata = { 1, 2, 4, 5 };

static unsigned __init ar5312_cpu_frequency(void)
{
	u32 scratch, devid, clock_ctl1;
	u32 predivide_mask, multiplier_mask, doubler_mask;
	unsigned predivide_shift, multiplier_shift;
	unsigned predivide_select, predivisor, multiplier;

	/* Trust the bootrom's idea of cpu frequency. */
	scratch = ar5312_rst_reg_read(AR5312_SCRATCH);
	if (scratch)
		return scratch;

	devid = ar5312_rst_reg_read(AR5312_REV);
	devid = (devid & AR5312_REV_MAJ) >> AR5312_REV_MAJ_S;
	if (devid == AR5312_REV_MAJ_AR2313) {
		predivide_mask = AR2313_CLOCKCTL1_PREDIVIDE_MASK;
		predivide_shift = AR2313_CLOCKCTL1_PREDIVIDE_SHIFT;
		multiplier_mask = AR2313_CLOCKCTL1_MULTIPLIER_MASK;
		multiplier_shift = AR2313_CLOCKCTL1_MULTIPLIER_SHIFT;
		doubler_mask = AR2313_CLOCKCTL1_DOUBLER_MASK;
	} else { /* AR5312 and AR2312 */
		predivide_mask = AR5312_CLOCKCTL1_PREDIVIDE_MASK;
		predivide_shift = AR5312_CLOCKCTL1_PREDIVIDE_SHIFT;
		multiplier_mask = AR5312_CLOCKCTL1_MULTIPLIER_MASK;
		multiplier_shift = AR5312_CLOCKCTL1_MULTIPLIER_SHIFT;
		doubler_mask = AR5312_CLOCKCTL1_DOUBLER_MASK;
	}

	/*
	 * Clocking is derived from a fixed 40MHz input clock.
	 *
	 *  cpu_freq = input_clock * MULT (where MULT is PLL multiplier)
	 *  sys_freq = cpu_freq / 4	  (used for APB clock, serial,
	 *				   flash, Timer, Watchdog Timer)
	 *
	 *  cnt_freq = cpu_freq / 2	  (use for CPU count/compare)
	 *
	 * So, for example, with a PLL multiplier of 5, we have
	 *
	 *  cpu_freq = 200MHz
	 *  sys_freq = 50MHz
	 *  cnt_freq = 100MHz
	 *
	 * We compute the CPU frequency, based on PLL settings.
	 */

	clock_ctl1 = ar5312_rst_reg_read(AR5312_CLOCKCTL1);
	predivide_select = (clock_ctl1 & predivide_mask) >> predivide_shift;
	predivisor = clockctl1_predivide_table[predivide_select];
	multiplier = (clock_ctl1 & multiplier_mask) >> multiplier_shift;

	if (clock_ctl1 & doubler_mask)
		multiplier <<= 1;

	return (40000000 / predivisor) * multiplier;
}

static inline unsigned ar5312_sys_frequency(void)
{
	return ar5312_cpu_frequency() / 4;
}

void __init ar5312_plat_time_init(void)
{
	mips_hpt_frequency = ar5312_cpu_frequency() / 2;
}

void __init ar5312_plat_mem_setup(void)
{
	void __iomem *sdram_base;
	u32 memsize, memcfg, bank0_ac, bank1_ac;
	u32 devid;

	/* Detect memory size */
	sdram_base = ioremap(AR5312_SDRAMCTL_BASE,
				     AR5312_SDRAMCTL_SIZE);
	memcfg = __raw_readl(sdram_base + AR5312_MEM_CFG1);
	bank0_ac = ATH25_REG_MS(memcfg, AR5312_MEM_CFG1_AC0);
	bank1_ac = ATH25_REG_MS(memcfg, AR5312_MEM_CFG1_AC1);
	memsize = (bank0_ac ? (1 << (bank0_ac + 1)) : 0) +
		  (bank1_ac ? (1 << (bank1_ac + 1)) : 0);
	memsize <<= 20;
	memblock_add(0, memsize);
	iounmap(sdram_base);

	ar5312_rst_base = ioremap(AR5312_RST_BASE, AR5312_RST_SIZE);

	devid = ar5312_rst_reg_read(AR5312_REV);
	devid >>= AR5312_REV_WMAC_MIN_S;
	devid &= AR5312_REV_CHIP;
	ath25_board.devid = (u16)devid;

	/* Clear any lingering AHB errors */
	ar5312_rst_reg_read(AR5312_PROCADDR);
	ar5312_rst_reg_read(AR5312_DMAADDR);
	ar5312_rst_reg_write(AR5312_WDT_CTRL, AR5312_WDT_CTRL_IGNORE);

	_machine_restart = ar5312_restart;
}

void __init ar5312_arch_init(void)
{
	unsigned irq = irq_create_mapping(ar5312_misc_irq_domain,
					  AR5312_MISC_IRQ_UART0);

	ath25_serial_setup(AR5312_UART0_BASE, irq, ar5312_sys_frequency());
}
