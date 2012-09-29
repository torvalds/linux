/*
 * Broadcom specific AMBA
 * ChipCommon core driver
 *
 * Copyright 2005, Broadcom Corporation
 * Copyright 2006, 2007, Michael Buesch <m@bues.ch>
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include "bcma_private.h"
#include <linux/export.h>
#include <linux/bcma/bcma.h>

static inline u32 bcma_cc_write32_masked(struct bcma_drv_cc *cc, u16 offset,
					 u32 mask, u32 value)
{
	value &= mask;
	value |= bcma_cc_read32(cc, offset) & ~mask;
	bcma_cc_write32(cc, offset, value);

	return value;
}

void bcma_core_chipcommon_early_init(struct bcma_drv_cc *cc)
{
	if (cc->early_setup_done)
		return;

	if (cc->core->id.rev >= 11)
		cc->status = bcma_cc_read32(cc, BCMA_CC_CHIPSTAT);
	cc->capabilities = bcma_cc_read32(cc, BCMA_CC_CAP);
	if (cc->core->id.rev >= 35)
		cc->capabilities_ext = bcma_cc_read32(cc, BCMA_CC_CAP_EXT);

	if (cc->capabilities & BCMA_CC_CAP_PMU)
		bcma_pmu_early_init(cc);

	cc->early_setup_done = true;
}

void bcma_core_chipcommon_init(struct bcma_drv_cc *cc)
{
	u32 leddc_on = 10;
	u32 leddc_off = 90;

	if (cc->setup_done)
		return;

	bcma_core_chipcommon_early_init(cc);

	if (cc->core->id.rev >= 20) {
		bcma_cc_write32(cc, BCMA_CC_GPIOPULLUP, 0);
		bcma_cc_write32(cc, BCMA_CC_GPIOPULLDOWN, 0);
	}

	if (cc->capabilities & BCMA_CC_CAP_PMU)
		bcma_pmu_init(cc);
	if (cc->capabilities & BCMA_CC_CAP_PCTL)
		bcma_err(cc->core->bus, "Power control not implemented!\n");

	if (cc->core->id.rev >= 16) {
		if (cc->core->bus->sprom.leddc_on_time &&
		    cc->core->bus->sprom.leddc_off_time) {
			leddc_on = cc->core->bus->sprom.leddc_on_time;
			leddc_off = cc->core->bus->sprom.leddc_off_time;
		}
		bcma_cc_write32(cc, BCMA_CC_GPIOTIMER,
			((leddc_on << BCMA_CC_GPIOTIMER_ONTIME_SHIFT) |
			 (leddc_off << BCMA_CC_GPIOTIMER_OFFTIME_SHIFT)));
	}

	cc->setup_done = true;
}

/* Set chip watchdog reset timer to fire in 'ticks' backplane cycles */
void bcma_chipco_watchdog_timer_set(struct bcma_drv_cc *cc, u32 ticks)
{
	/* instant NMI */
	bcma_cc_write32(cc, BCMA_CC_WATCHDOG, ticks);
}

void bcma_chipco_irq_mask(struct bcma_drv_cc *cc, u32 mask, u32 value)
{
	bcma_cc_write32_masked(cc, BCMA_CC_IRQMASK, mask, value);
}

u32 bcma_chipco_irq_status(struct bcma_drv_cc *cc, u32 mask)
{
	return bcma_cc_read32(cc, BCMA_CC_IRQSTAT) & mask;
}

u32 bcma_chipco_gpio_in(struct bcma_drv_cc *cc, u32 mask)
{
	return bcma_cc_read32(cc, BCMA_CC_GPIOIN) & mask;
}

u32 bcma_chipco_gpio_out(struct bcma_drv_cc *cc, u32 mask, u32 value)
{
	return bcma_cc_write32_masked(cc, BCMA_CC_GPIOOUT, mask, value);
}

u32 bcma_chipco_gpio_outen(struct bcma_drv_cc *cc, u32 mask, u32 value)
{
	return bcma_cc_write32_masked(cc, BCMA_CC_GPIOOUTEN, mask, value);
}

u32 bcma_chipco_gpio_control(struct bcma_drv_cc *cc, u32 mask, u32 value)
{
	return bcma_cc_write32_masked(cc, BCMA_CC_GPIOCTL, mask, value);
}
EXPORT_SYMBOL_GPL(bcma_chipco_gpio_control);

u32 bcma_chipco_gpio_intmask(struct bcma_drv_cc *cc, u32 mask, u32 value)
{
	return bcma_cc_write32_masked(cc, BCMA_CC_GPIOIRQ, mask, value);
}

u32 bcma_chipco_gpio_polarity(struct bcma_drv_cc *cc, u32 mask, u32 value)
{
	return bcma_cc_write32_masked(cc, BCMA_CC_GPIOPOL, mask, value);
}

#ifdef CONFIG_BCMA_DRIVER_MIPS
void bcma_chipco_serial_init(struct bcma_drv_cc *cc)
{
	unsigned int irq;
	u32 baud_base;
	u32 i;
	unsigned int ccrev = cc->core->id.rev;
	struct bcma_serial_port *ports = cc->serial_ports;

	if (ccrev >= 11 && ccrev != 15) {
		/* Fixed ALP clock */
		baud_base = bcma_pmu_alp_clock(cc);
		if (ccrev >= 21) {
			/* Turn off UART clock before switching clocksource. */
			bcma_cc_write32(cc, BCMA_CC_CORECTL,
				       bcma_cc_read32(cc, BCMA_CC_CORECTL)
				       & ~BCMA_CC_CORECTL_UARTCLKEN);
		}
		/* Set the override bit so we don't divide it */
		bcma_cc_write32(cc, BCMA_CC_CORECTL,
			       bcma_cc_read32(cc, BCMA_CC_CORECTL)
			       | BCMA_CC_CORECTL_UARTCLK0);
		if (ccrev >= 21) {
			/* Re-enable the UART clock. */
			bcma_cc_write32(cc, BCMA_CC_CORECTL,
				       bcma_cc_read32(cc, BCMA_CC_CORECTL)
				       | BCMA_CC_CORECTL_UARTCLKEN);
		}
	} else {
		bcma_err(cc->core->bus, "serial not supported on this device ccrev: 0x%x\n", ccrev);
		return;
	}

	irq = bcma_core_mips_irq(cc->core);

	/* Determine the registers of the UARTs */
	cc->nr_serial_ports = (cc->capabilities & BCMA_CC_CAP_NRUART);
	for (i = 0; i < cc->nr_serial_ports; i++) {
		ports[i].regs = cc->core->io_addr + BCMA_CC_UART0_DATA +
				(i * 256);
		ports[i].irq = irq;
		ports[i].baud_base = baud_base;
		ports[i].reg_shift = 0;
	}
}
#endif /* CONFIG_BCMA_DRIVER_MIPS */
