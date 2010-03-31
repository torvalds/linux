/*
 * Sonics Silicon Backplane
 * Broadcom ChipCommon core driver
 *
 * Copyright 2005, Broadcom Corporation
 * Copyright 2006, 2007, Michael Buesch <mb@bu3sch.de>
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include <linux/ssb/ssb.h>
#include <linux/ssb/ssb_regs.h>
#include <linux/pci.h>

#include "ssb_private.h"


/* Clock sources */
enum ssb_clksrc {
	/* PCI clock */
	SSB_CHIPCO_CLKSRC_PCI,
	/* Crystal slow clock oscillator */
	SSB_CHIPCO_CLKSRC_XTALOS,
	/* Low power oscillator */
	SSB_CHIPCO_CLKSRC_LOPWROS,
};


static inline u32 chipco_write32_masked(struct ssb_chipcommon *cc, u16 offset,
					u32 mask, u32 value)
{
	value &= mask;
	value |= chipco_read32(cc, offset) & ~mask;
	chipco_write32(cc, offset, value);

	return value;
}

void ssb_chipco_set_clockmode(struct ssb_chipcommon *cc,
			      enum ssb_clkmode mode)
{
	struct ssb_device *ccdev = cc->dev;
	struct ssb_bus *bus;
	u32 tmp;

	if (!ccdev)
		return;
	bus = ccdev->bus;
	/* chipcommon cores prior to rev6 don't support dynamic clock control */
	if (ccdev->id.revision < 6)
		return;
	/* chipcommon cores rev10 are a whole new ball game */
	if (ccdev->id.revision >= 10)
		return;
	if (!(cc->capabilities & SSB_CHIPCO_CAP_PCTL))
		return;

	switch (mode) {
	case SSB_CLKMODE_SLOW:
		tmp = chipco_read32(cc, SSB_CHIPCO_SLOWCLKCTL);
		tmp |= SSB_CHIPCO_SLOWCLKCTL_FSLOW;
		chipco_write32(cc, SSB_CHIPCO_SLOWCLKCTL, tmp);
		break;
	case SSB_CLKMODE_FAST:
		ssb_pci_xtal(bus, SSB_GPIO_XTAL, 1); /* Force crystal on */
		tmp = chipco_read32(cc, SSB_CHIPCO_SLOWCLKCTL);
		tmp &= ~SSB_CHIPCO_SLOWCLKCTL_FSLOW;
		tmp |= SSB_CHIPCO_SLOWCLKCTL_IPLL;
		chipco_write32(cc, SSB_CHIPCO_SLOWCLKCTL, tmp);
		break;
	case SSB_CLKMODE_DYNAMIC:
		tmp = chipco_read32(cc, SSB_CHIPCO_SLOWCLKCTL);
		tmp &= ~SSB_CHIPCO_SLOWCLKCTL_FSLOW;
		tmp &= ~SSB_CHIPCO_SLOWCLKCTL_IPLL;
		tmp &= ~SSB_CHIPCO_SLOWCLKCTL_ENXTAL;
		if ((tmp & SSB_CHIPCO_SLOWCLKCTL_SRC) != SSB_CHIPCO_SLOWCLKCTL_SRC_XTAL)
			tmp |= SSB_CHIPCO_SLOWCLKCTL_ENXTAL;
		chipco_write32(cc, SSB_CHIPCO_SLOWCLKCTL, tmp);

		/* for dynamic control, we have to release our xtal_pu "force on" */
		if (tmp & SSB_CHIPCO_SLOWCLKCTL_ENXTAL)
			ssb_pci_xtal(bus, SSB_GPIO_XTAL, 0);
		break;
	default:
		SSB_WARN_ON(1);
	}
}

/* Get the Slow Clock Source */
static enum ssb_clksrc chipco_pctl_get_slowclksrc(struct ssb_chipcommon *cc)
{
	struct ssb_bus *bus = cc->dev->bus;
	u32 uninitialized_var(tmp);

	if (cc->dev->id.revision < 6) {
		if (bus->bustype == SSB_BUSTYPE_SSB ||
		    bus->bustype == SSB_BUSTYPE_PCMCIA)
			return SSB_CHIPCO_CLKSRC_XTALOS;
		if (bus->bustype == SSB_BUSTYPE_PCI) {
			pci_read_config_dword(bus->host_pci, SSB_GPIO_OUT, &tmp);
			if (tmp & 0x10)
				return SSB_CHIPCO_CLKSRC_PCI;
			return SSB_CHIPCO_CLKSRC_XTALOS;
		}
	}
	if (cc->dev->id.revision < 10) {
		tmp = chipco_read32(cc, SSB_CHIPCO_SLOWCLKCTL);
		tmp &= 0x7;
		if (tmp == 0)
			return SSB_CHIPCO_CLKSRC_LOPWROS;
		if (tmp == 1)
			return SSB_CHIPCO_CLKSRC_XTALOS;
		if (tmp == 2)
			return SSB_CHIPCO_CLKSRC_PCI;
	}

	return SSB_CHIPCO_CLKSRC_XTALOS;
}

/* Get maximum or minimum (depending on get_max flag) slowclock frequency. */
static int chipco_pctl_clockfreqlimit(struct ssb_chipcommon *cc, int get_max)
{
	int uninitialized_var(limit);
	enum ssb_clksrc clocksrc;
	int divisor = 1;
	u32 tmp;

	clocksrc = chipco_pctl_get_slowclksrc(cc);
	if (cc->dev->id.revision < 6) {
		switch (clocksrc) {
		case SSB_CHIPCO_CLKSRC_PCI:
			divisor = 64;
			break;
		case SSB_CHIPCO_CLKSRC_XTALOS:
			divisor = 32;
			break;
		default:
			SSB_WARN_ON(1);
		}
	} else if (cc->dev->id.revision < 10) {
		switch (clocksrc) {
		case SSB_CHIPCO_CLKSRC_LOPWROS:
			break;
		case SSB_CHIPCO_CLKSRC_XTALOS:
		case SSB_CHIPCO_CLKSRC_PCI:
			tmp = chipco_read32(cc, SSB_CHIPCO_SLOWCLKCTL);
			divisor = (tmp >> 16) + 1;
			divisor *= 4;
			break;
		}
	} else {
		tmp = chipco_read32(cc, SSB_CHIPCO_SYSCLKCTL);
		divisor = (tmp >> 16) + 1;
		divisor *= 4;
	}

	switch (clocksrc) {
	case SSB_CHIPCO_CLKSRC_LOPWROS:
		if (get_max)
			limit = 43000;
		else
			limit = 25000;
		break;
	case SSB_CHIPCO_CLKSRC_XTALOS:
		if (get_max)
			limit = 20200000;
		else
			limit = 19800000;
		break;
	case SSB_CHIPCO_CLKSRC_PCI:
		if (get_max)
			limit = 34000000;
		else
			limit = 25000000;
		break;
	}
	limit /= divisor;

	return limit;
}

static void chipco_powercontrol_init(struct ssb_chipcommon *cc)
{
	struct ssb_bus *bus = cc->dev->bus;

	if (bus->chip_id == 0x4321) {
		if (bus->chip_rev == 0)
			chipco_write32(cc, SSB_CHIPCO_CHIPCTL, 0x3A4);
		else if (bus->chip_rev == 1)
			chipco_write32(cc, SSB_CHIPCO_CHIPCTL, 0xA4);
	}

	if (!(cc->capabilities & SSB_CHIPCO_CAP_PCTL))
		return;

	if (cc->dev->id.revision >= 10) {
		/* Set Idle Power clock rate to 1Mhz */
		chipco_write32(cc, SSB_CHIPCO_SYSCLKCTL,
			       (chipco_read32(cc, SSB_CHIPCO_SYSCLKCTL) &
				0x0000FFFF) | 0x00040000);
	} else {
		int maxfreq;

		maxfreq = chipco_pctl_clockfreqlimit(cc, 1);
		chipco_write32(cc, SSB_CHIPCO_PLLONDELAY,
			       (maxfreq * 150 + 999999) / 1000000);
		chipco_write32(cc, SSB_CHIPCO_FREFSELDELAY,
			       (maxfreq * 15 + 999999) / 1000000);
	}
}

static void calc_fast_powerup_delay(struct ssb_chipcommon *cc)
{
	struct ssb_bus *bus = cc->dev->bus;
	int minfreq;
	unsigned int tmp;
	u32 pll_on_delay;

	if (bus->bustype != SSB_BUSTYPE_PCI)
		return;
	if (!(cc->capabilities & SSB_CHIPCO_CAP_PCTL))
		return;

	minfreq = chipco_pctl_clockfreqlimit(cc, 0);
	pll_on_delay = chipco_read32(cc, SSB_CHIPCO_PLLONDELAY);
	tmp = (((pll_on_delay + 2) * 1000000) + (minfreq - 1)) / minfreq;
	SSB_WARN_ON(tmp & ~0xFFFF);

	cc->fast_pwrup_delay = tmp;
}

void ssb_chipcommon_init(struct ssb_chipcommon *cc)
{
	if (!cc->dev)
		return; /* We don't have a ChipCommon */
	if (cc->dev->id.revision >= 11)
		cc->status = chipco_read32(cc, SSB_CHIPCO_CHIPSTAT);
	ssb_pmu_init(cc);
	chipco_powercontrol_init(cc);
	ssb_chipco_set_clockmode(cc, SSB_CLKMODE_FAST);
	calc_fast_powerup_delay(cc);
}

void ssb_chipco_suspend(struct ssb_chipcommon *cc)
{
	if (!cc->dev)
		return;
	ssb_chipco_set_clockmode(cc, SSB_CLKMODE_SLOW);
}

void ssb_chipco_resume(struct ssb_chipcommon *cc)
{
	if (!cc->dev)
		return;
	chipco_powercontrol_init(cc);
	ssb_chipco_set_clockmode(cc, SSB_CLKMODE_FAST);
}

/* Get the processor clock */
void ssb_chipco_get_clockcpu(struct ssb_chipcommon *cc,
                             u32 *plltype, u32 *n, u32 *m)
{
	*n = chipco_read32(cc, SSB_CHIPCO_CLOCK_N);
	*plltype = (cc->capabilities & SSB_CHIPCO_CAP_PLLT);
	switch (*plltype) {
	case SSB_PLLTYPE_2:
	case SSB_PLLTYPE_4:
	case SSB_PLLTYPE_6:
	case SSB_PLLTYPE_7:
		*m = chipco_read32(cc, SSB_CHIPCO_CLOCK_MIPS);
		break;
	case SSB_PLLTYPE_3:
		/* 5350 uses m2 to control mips */
		*m = chipco_read32(cc, SSB_CHIPCO_CLOCK_M2);
		break;
	default:
		*m = chipco_read32(cc, SSB_CHIPCO_CLOCK_SB);
		break;
	}
}

/* Get the bus clock */
void ssb_chipco_get_clockcontrol(struct ssb_chipcommon *cc,
				 u32 *plltype, u32 *n, u32 *m)
{
	*n = chipco_read32(cc, SSB_CHIPCO_CLOCK_N);
	*plltype = (cc->capabilities & SSB_CHIPCO_CAP_PLLT);
	switch (*plltype) {
	case SSB_PLLTYPE_6: /* 100/200 or 120/240 only */
		*m = chipco_read32(cc, SSB_CHIPCO_CLOCK_MIPS);
		break;
	case SSB_PLLTYPE_3: /* 25Mhz, 2 dividers */
		if (cc->dev->bus->chip_id != 0x5365) {
			*m = chipco_read32(cc, SSB_CHIPCO_CLOCK_M2);
			break;
		}
		/* Fallthough */
	default:
		*m = chipco_read32(cc, SSB_CHIPCO_CLOCK_SB);
	}
}

void ssb_chipco_timing_init(struct ssb_chipcommon *cc,
			    unsigned long ns)
{
	struct ssb_device *dev = cc->dev;
	struct ssb_bus *bus = dev->bus;
	u32 tmp;

	/* set register for external IO to control LED. */
	chipco_write32(cc, SSB_CHIPCO_PROG_CFG, 0x11);
	tmp = DIV_ROUND_UP(10, ns) << SSB_PROG_WCNT_3_SHIFT;		/* Waitcount-3 = 10ns */
	tmp |= DIV_ROUND_UP(40, ns) << SSB_PROG_WCNT_1_SHIFT;	/* Waitcount-1 = 40ns */
	tmp |= DIV_ROUND_UP(240, ns);				/* Waitcount-0 = 240ns */
	chipco_write32(cc, SSB_CHIPCO_PROG_WAITCNT, tmp);	/* 0x01020a0c for a 100Mhz clock */

	/* Set timing for the flash */
	tmp = DIV_ROUND_UP(10, ns) << SSB_FLASH_WCNT_3_SHIFT;	/* Waitcount-3 = 10nS */
	tmp |= DIV_ROUND_UP(10, ns) << SSB_FLASH_WCNT_1_SHIFT;	/* Waitcount-1 = 10nS */
	tmp |= DIV_ROUND_UP(120, ns);				/* Waitcount-0 = 120nS */
	if ((bus->chip_id == 0x5365) ||
	    (dev->id.revision < 9))
		chipco_write32(cc, SSB_CHIPCO_FLASH_WAITCNT, tmp);
	if ((bus->chip_id == 0x5365) ||
	    (dev->id.revision < 9) ||
	    ((bus->chip_id == 0x5350) && (bus->chip_rev == 0)))
		chipco_write32(cc, SSB_CHIPCO_PCMCIA_MEMWAIT, tmp);

	if (bus->chip_id == 0x5350) {
		/* Enable EXTIF */
		tmp = DIV_ROUND_UP(10, ns) << SSB_PROG_WCNT_3_SHIFT;	  /* Waitcount-3 = 10ns */
		tmp |= DIV_ROUND_UP(20, ns) << SSB_PROG_WCNT_2_SHIFT;  /* Waitcount-2 = 20ns */
		tmp |= DIV_ROUND_UP(100, ns) << SSB_PROG_WCNT_1_SHIFT; /* Waitcount-1 = 100ns */
		tmp |= DIV_ROUND_UP(120, ns);			  /* Waitcount-0 = 120ns */
		chipco_write32(cc, SSB_CHIPCO_PROG_WAITCNT, tmp); /* 0x01020a0c for a 100Mhz clock */
	}
}

/* Set chip watchdog reset timer to fire in 'ticks' backplane cycles */
void ssb_chipco_watchdog_timer_set(struct ssb_chipcommon *cc, u32 ticks)
{
	/* instant NMI */
	chipco_write32(cc, SSB_CHIPCO_WATCHDOG, ticks);
}

void ssb_chipco_irq_mask(struct ssb_chipcommon *cc, u32 mask, u32 value)
{
	chipco_write32_masked(cc, SSB_CHIPCO_IRQMASK, mask, value);
}

u32 ssb_chipco_irq_status(struct ssb_chipcommon *cc, u32 mask)
{
	return chipco_read32(cc, SSB_CHIPCO_IRQSTAT) & mask;
}

u32 ssb_chipco_gpio_in(struct ssb_chipcommon *cc, u32 mask)
{
	return chipco_read32(cc, SSB_CHIPCO_GPIOIN) & mask;
}

u32 ssb_chipco_gpio_out(struct ssb_chipcommon *cc, u32 mask, u32 value)
{
	return chipco_write32_masked(cc, SSB_CHIPCO_GPIOOUT, mask, value);
}

u32 ssb_chipco_gpio_outen(struct ssb_chipcommon *cc, u32 mask, u32 value)
{
	return chipco_write32_masked(cc, SSB_CHIPCO_GPIOOUTEN, mask, value);
}

u32 ssb_chipco_gpio_control(struct ssb_chipcommon *cc, u32 mask, u32 value)
{
	return chipco_write32_masked(cc, SSB_CHIPCO_GPIOCTL, mask, value);
}

u32 ssb_chipco_gpio_intmask(struct ssb_chipcommon *cc, u32 mask, u32 value)
{
	return chipco_write32_masked(cc, SSB_CHIPCO_GPIOIRQ, mask, value);
}

u32 ssb_chipco_gpio_polarity(struct ssb_chipcommon *cc, u32 mask, u32 value)
{
	return chipco_write32_masked(cc, SSB_CHIPCO_GPIOPOL, mask, value);
}

#ifdef CONFIG_SSB_SERIAL
int ssb_chipco_serial_init(struct ssb_chipcommon *cc,
			   struct ssb_serial_port *ports)
{
	struct ssb_bus *bus = cc->dev->bus;
	int nr_ports = 0;
	u32 plltype;
	unsigned int irq;
	u32 baud_base, div;
	u32 i, n;
	unsigned int ccrev = cc->dev->id.revision;

	plltype = (cc->capabilities & SSB_CHIPCO_CAP_PLLT);
	irq = ssb_mips_irq(cc->dev);

	if (plltype == SSB_PLLTYPE_1) {
		/* PLL clock */
		baud_base = ssb_calc_clock_rate(plltype,
						chipco_read32(cc, SSB_CHIPCO_CLOCK_N),
						chipco_read32(cc, SSB_CHIPCO_CLOCK_M2));
		div = 1;
	} else {
		if (ccrev == 20) {
			/* BCM5354 uses constant 25MHz clock */
			baud_base = 25000000;
			div = 48;
			/* Set the override bit so we don't divide it */
			chipco_write32(cc, SSB_CHIPCO_CORECTL,
				       chipco_read32(cc, SSB_CHIPCO_CORECTL)
				       | SSB_CHIPCO_CORECTL_UARTCLK0);
		} else if ((ccrev >= 11) && (ccrev != 15)) {
			/* Fixed ALP clock */
			baud_base = 20000000;
			if (cc->capabilities & SSB_CHIPCO_CAP_PMU) {
				/* FIXME: baud_base is different for devices with a PMU */
				SSB_WARN_ON(1);
			}
			div = 1;
			if (ccrev >= 21) {
				/* Turn off UART clock before switching clocksource. */
				chipco_write32(cc, SSB_CHIPCO_CORECTL,
					       chipco_read32(cc, SSB_CHIPCO_CORECTL)
					       & ~SSB_CHIPCO_CORECTL_UARTCLKEN);
			}
			/* Set the override bit so we don't divide it */
			chipco_write32(cc, SSB_CHIPCO_CORECTL,
				       chipco_read32(cc, SSB_CHIPCO_CORECTL)
				       | SSB_CHIPCO_CORECTL_UARTCLK0);
			if (ccrev >= 21) {
				/* Re-enable the UART clock. */
				chipco_write32(cc, SSB_CHIPCO_CORECTL,
					       chipco_read32(cc, SSB_CHIPCO_CORECTL)
					       | SSB_CHIPCO_CORECTL_UARTCLKEN);
			}
		} else if (ccrev >= 3) {
			/* Internal backplane clock */
			baud_base = ssb_clockspeed(bus);
			div = chipco_read32(cc, SSB_CHIPCO_CLKDIV)
			      & SSB_CHIPCO_CLKDIV_UART;
		} else {
			/* Fixed internal backplane clock */
			baud_base = 88000000;
			div = 48;
		}

		/* Clock source depends on strapping if UartClkOverride is unset */
		if ((ccrev > 0) &&
		    !(chipco_read32(cc, SSB_CHIPCO_CORECTL) & SSB_CHIPCO_CORECTL_UARTCLK0)) {
			if ((cc->capabilities & SSB_CHIPCO_CAP_UARTCLK) ==
			    SSB_CHIPCO_CAP_UARTCLK_INT) {
				/* Internal divided backplane clock */
				baud_base /= div;
			} else {
				/* Assume external clock of 1.8432 MHz */
				baud_base = 1843200;
			}
		}
	}

	/* Determine the registers of the UARTs */
	n = (cc->capabilities & SSB_CHIPCO_CAP_NRUART);
	for (i = 0; i < n; i++) {
		void __iomem *cc_mmio;
		void __iomem *uart_regs;

		cc_mmio = cc->dev->bus->mmio + (cc->dev->core_index * SSB_CORE_SIZE);
		uart_regs = cc_mmio + SSB_CHIPCO_UART0_DATA;
		/* Offset changed at after rev 0 */
		if (ccrev == 0)
			uart_regs += (i * 8);
		else
			uart_regs += (i * 256);

		nr_ports++;
		ports[i].regs = uart_regs;
		ports[i].irq = irq;
		ports[i].baud_base = baud_base;
		ports[i].reg_shift = 0;
	}

	return nr_ports;
}
#endif /* CONFIG_SSB_SERIAL */
