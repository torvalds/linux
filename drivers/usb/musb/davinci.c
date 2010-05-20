/*
 * Copyright (C) 2005-2006 by Texas Instruments
 *
 * This file is part of the Inventra Controller Driver for Linux.
 *
 * The Inventra Controller Driver for Linux is free software; you
 * can redistribute it and/or modify it under the terms of the GNU
 * General Public License version 2 as published by the Free Software
 * Foundation.
 *
 * The Inventra Controller Driver for Linux is distributed in
 * the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
 * License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with The Inventra Controller Driver for Linux ; if not,
 * write to the Free Software Foundation, Inc., 59 Temple Place,
 * Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/gpio.h>

#include <mach/hardware.h>
#include <mach/memory.h>
#include <mach/gpio.h>
#include <mach/cputype.h>

#include <asm/mach-types.h>

#include "musb_core.h"

#ifdef CONFIG_MACH_DAVINCI_EVM
#define GPIO_nVBUS_DRV		160
#endif

#include "davinci.h"
#include "cppi_dma.h"


#define USB_PHY_CTRL	IO_ADDRESS(USBPHY_CTL_PADDR)
#define DM355_DEEPSLEEP	IO_ADDRESS(DM355_DEEPSLEEP_PADDR)

/* REVISIT (PM) we should be able to keep the PHY in low power mode most
 * of the time (24 MHZ oscillator and PLL off, etc) by setting POWER.D0
 * and, when in host mode, autosuspending idle root ports... PHYPLLON
 * (overriding SUSPENDM?) then likely needs to stay off.
 */

static inline void phy_on(void)
{
	u32	phy_ctrl = __raw_readl(USB_PHY_CTRL);

	/* power everything up; start the on-chip PHY and its PLL */
	phy_ctrl &= ~(USBPHY_OSCPDWN | USBPHY_OTGPDWN | USBPHY_PHYPDWN);
	phy_ctrl |= USBPHY_SESNDEN | USBPHY_VBDTCTEN | USBPHY_PHYPLLON;
	__raw_writel(phy_ctrl, USB_PHY_CTRL);

	/* wait for PLL to lock before proceeding */
	while ((__raw_readl(USB_PHY_CTRL) & USBPHY_PHYCLKGD) == 0)
		cpu_relax();
}

static inline void phy_off(void)
{
	u32	phy_ctrl = __raw_readl(USB_PHY_CTRL);

	/* powerdown the on-chip PHY, its PLL, and the OTG block */
	phy_ctrl &= ~(USBPHY_SESNDEN | USBPHY_VBDTCTEN | USBPHY_PHYPLLON);
	phy_ctrl |= USBPHY_OSCPDWN | USBPHY_OTGPDWN | USBPHY_PHYPDWN;
	__raw_writel(phy_ctrl, USB_PHY_CTRL);
}

static int dma_off = 1;

void musb_platform_enable(struct musb *musb)
{
	u32	tmp, old, val;

	/* workaround:  setup irqs through both register sets */
	tmp = (musb->epmask & DAVINCI_USB_TX_ENDPTS_MASK)
			<< DAVINCI_USB_TXINT_SHIFT;
	musb_writel(musb->ctrl_base, DAVINCI_USB_INT_MASK_SET_REG, tmp);
	old = tmp;
	tmp = (musb->epmask & (0xfffe & DAVINCI_USB_RX_ENDPTS_MASK))
			<< DAVINCI_USB_RXINT_SHIFT;
	musb_writel(musb->ctrl_base, DAVINCI_USB_INT_MASK_SET_REG, tmp);
	tmp |= old;

	val = ~MUSB_INTR_SOF;
	tmp |= ((val & 0x01ff) << DAVINCI_USB_USBINT_SHIFT);
	musb_writel(musb->ctrl_base, DAVINCI_USB_INT_MASK_SET_REG, tmp);

	if (is_dma_capable() && !dma_off)
		printk(KERN_WARNING "%s %s: dma not reactivated\n",
				__FILE__, __func__);
	else
		dma_off = 0;

	/* force a DRVVBUS irq so we can start polling for ID change */
	if (is_otg_enabled(musb))
		musb_writel(musb->ctrl_base, DAVINCI_USB_INT_SET_REG,
			DAVINCI_INTR_DRVVBUS << DAVINCI_USB_USBINT_SHIFT);
}

/*
 * Disable the HDRC and flush interrupts
 */
void musb_platform_disable(struct musb *musb)
{
	/* because we don't set CTRLR.UINT, "important" to:
	 *  - not read/write INTRUSB/INTRUSBE
	 *  - (except during initial setup, as workaround)
	 *  - use INTSETR/INTCLRR instead
	 */
	musb_writel(musb->ctrl_base, DAVINCI_USB_INT_MASK_CLR_REG,
			  DAVINCI_USB_USBINT_MASK
			| DAVINCI_USB_TXINT_MASK
			| DAVINCI_USB_RXINT_MASK);
	musb_writeb(musb->mregs, MUSB_DEVCTL, 0);
	musb_writel(musb->ctrl_base, DAVINCI_USB_EOI_REG, 0);

	if (is_dma_capable() && !dma_off)
		WARNING("dma still active\n");
}


#ifdef CONFIG_USB_MUSB_HDRC_HCD
#define	portstate(stmt)		stmt
#else
#define	portstate(stmt)
#endif


/*
 * VBUS SWITCHING IS BOARD-SPECIFIC ... at least for the DM6446 EVM,
 * which doesn't wire DRVVBUS to the FET that switches it.  Unclear
 * if that's a problem with the DM6446 chip or just with that board.
 *
 * In either case, the DM355 EVM automates DRVVBUS the normal way,
 * when J10 is out, and TI documents it as handling OTG.
 */

#ifdef CONFIG_MACH_DAVINCI_EVM

static int vbus_state = -1;

/* I2C operations are always synchronous, and require a task context.
 * With unloaded systems, using the shared workqueue seems to suffice
 * to satisfy the 100msec A_WAIT_VRISE timeout...
 */
static void evm_deferred_drvvbus(struct work_struct *ignored)
{
	gpio_set_value_cansleep(GPIO_nVBUS_DRV, vbus_state);
	vbus_state = !vbus_state;
}

#endif	/* EVM */

static void davinci_source_power(struct musb *musb, int is_on, int immediate)
{
#ifdef CONFIG_MACH_DAVINCI_EVM
	if (is_on)
		is_on = 1;

	if (vbus_state == is_on)
		return;
	vbus_state = !is_on;		/* 0/1 vs "-1 == unknown/init" */

	if (machine_is_davinci_evm()) {
		static DECLARE_WORK(evm_vbus_work, evm_deferred_drvvbus);

		if (immediate)
			gpio_set_value_cansleep(GPIO_nVBUS_DRV, vbus_state);
		else
			schedule_work(&evm_vbus_work);
	}
	if (immediate)
		vbus_state = is_on;
#endif
}

static void davinci_set_vbus(struct musb *musb, int is_on)
{
	WARN_ON(is_on && is_peripheral_active(musb));
	davinci_source_power(musb, is_on, 0);
}


#define	POLL_SECONDS	2

static struct timer_list otg_workaround;

static void otg_timer(unsigned long _musb)
{
	struct musb		*musb = (void *)_musb;
	void __iomem		*mregs = musb->mregs;
	u8			devctl;
	unsigned long		flags;

	/* We poll because DaVinci's won't expose several OTG-critical
	* status change events (from the transceiver) otherwise.
	 */
	devctl = musb_readb(mregs, MUSB_DEVCTL);
	DBG(7, "poll devctl %02x (%s)\n", devctl, otg_state_string(musb));

	spin_lock_irqsave(&musb->lock, flags);
	switch (musb->xceiv->state) {
	case OTG_STATE_A_WAIT_VFALL:
		/* Wait till VBUS falls below SessionEnd (~0.2V); the 1.3 RTL
		 * seems to mis-handle session "start" otherwise (or in our
		 * case "recover"), in routine "VBUS was valid by the time
		 * VBUSERR got reported during enumeration" cases.
		 */
		if (devctl & MUSB_DEVCTL_VBUS) {
			mod_timer(&otg_workaround, jiffies + POLL_SECONDS * HZ);
			break;
		}
		musb->xceiv->state = OTG_STATE_A_WAIT_VRISE;
		musb_writel(musb->ctrl_base, DAVINCI_USB_INT_SET_REG,
			MUSB_INTR_VBUSERROR << DAVINCI_USB_USBINT_SHIFT);
		break;
	case OTG_STATE_B_IDLE:
		if (!is_peripheral_enabled(musb))
			break;

		/* There's no ID-changed IRQ, so we have no good way to tell
		 * when to switch to the A-Default state machine (by setting
		 * the DEVCTL.SESSION flag).
		 *
		 * Workaround:  whenever we're in B_IDLE, try setting the
		 * session flag every few seconds.  If it works, ID was
		 * grounded and we're now in the A-Default state machine.
		 *
		 * NOTE setting the session flag is _supposed_ to trigger
		 * SRP, but clearly it doesn't.
		 */
		musb_writeb(mregs, MUSB_DEVCTL,
				devctl | MUSB_DEVCTL_SESSION);
		devctl = musb_readb(mregs, MUSB_DEVCTL);
		if (devctl & MUSB_DEVCTL_BDEVICE)
			mod_timer(&otg_workaround, jiffies + POLL_SECONDS * HZ);
		else
			musb->xceiv->state = OTG_STATE_A_IDLE;
		break;
	default:
		break;
	}
	spin_unlock_irqrestore(&musb->lock, flags);
}

static irqreturn_t davinci_interrupt(int irq, void *__hci)
{
	unsigned long	flags;
	irqreturn_t	retval = IRQ_NONE;
	struct musb	*musb = __hci;
	void __iomem	*tibase = musb->ctrl_base;
	struct cppi	*cppi;
	u32		tmp;

	spin_lock_irqsave(&musb->lock, flags);

	/* NOTE: DaVinci shadows the Mentor IRQs.  Don't manage them through
	 * the Mentor registers (except for setup), use the TI ones and EOI.
	 *
	 * Docs describe irq "vector" registers associated with the CPPI and
	 * USB EOI registers.  These hold a bitmask corresponding to the
	 * current IRQ, not an irq handler address.  Would using those bits
	 * resolve some of the races observed in this dispatch code??
	 */

	/* CPPI interrupts share the same IRQ line, but have their own
	 * mask, state, "vector", and EOI registers.
	 */
	cppi = container_of(musb->dma_controller, struct cppi, controller);
	if (is_cppi_enabled() && musb->dma_controller && !cppi->irq)
		retval = cppi_interrupt(irq, __hci);

	/* ack and handle non-CPPI interrupts */
	tmp = musb_readl(tibase, DAVINCI_USB_INT_SRC_MASKED_REG);
	musb_writel(tibase, DAVINCI_USB_INT_SRC_CLR_REG, tmp);
	DBG(4, "IRQ %08x\n", tmp);

	musb->int_rx = (tmp & DAVINCI_USB_RXINT_MASK)
			>> DAVINCI_USB_RXINT_SHIFT;
	musb->int_tx = (tmp & DAVINCI_USB_TXINT_MASK)
			>> DAVINCI_USB_TXINT_SHIFT;
	musb->int_usb = (tmp & DAVINCI_USB_USBINT_MASK)
			>> DAVINCI_USB_USBINT_SHIFT;

	/* DRVVBUS irqs are the only proxy we have (a very poor one!) for
	 * DaVinci's missing ID change IRQ.  We need an ID change IRQ to
	 * switch appropriately between halves of the OTG state machine.
	 * Managing DEVCTL.SESSION per Mentor docs requires we know its
	 * value, but DEVCTL.BDEVICE is invalid without DEVCTL.SESSION set.
	 * Also, DRVVBUS pulses for SRP (but not at 5V) ...
	 */
	if (tmp & (DAVINCI_INTR_DRVVBUS << DAVINCI_USB_USBINT_SHIFT)) {
		int	drvvbus = musb_readl(tibase, DAVINCI_USB_STAT_REG);
		void __iomem *mregs = musb->mregs;
		u8	devctl = musb_readb(mregs, MUSB_DEVCTL);
		int	err = musb->int_usb & MUSB_INTR_VBUSERROR;

		err = is_host_enabled(musb)
				&& (musb->int_usb & MUSB_INTR_VBUSERROR);
		if (err) {
			/* The Mentor core doesn't debounce VBUS as needed
			 * to cope with device connect current spikes. This
			 * means it's not uncommon for bus-powered devices
			 * to get VBUS errors during enumeration.
			 *
			 * This is a workaround, but newer RTL from Mentor
			 * seems to allow a better one: "re"starting sessions
			 * without waiting (on EVM, a **long** time) for VBUS
			 * to stop registering in devctl.
			 */
			musb->int_usb &= ~MUSB_INTR_VBUSERROR;
			musb->xceiv->state = OTG_STATE_A_WAIT_VFALL;
			mod_timer(&otg_workaround, jiffies + POLL_SECONDS * HZ);
			WARNING("VBUS error workaround (delay coming)\n");
		} else if (is_host_enabled(musb) && drvvbus) {
			MUSB_HST_MODE(musb);
			musb->xceiv->default_a = 1;
			musb->xceiv->state = OTG_STATE_A_WAIT_VRISE;
			portstate(musb->port1_status |= USB_PORT_STAT_POWER);
			del_timer(&otg_workaround);
		} else {
			musb->is_active = 0;
			MUSB_DEV_MODE(musb);
			musb->xceiv->default_a = 0;
			musb->xceiv->state = OTG_STATE_B_IDLE;
			portstate(musb->port1_status &= ~USB_PORT_STAT_POWER);
		}

		/* NOTE:  this must complete poweron within 100 msec
		 * (OTG_TIME_A_WAIT_VRISE) but we don't check for that.
		 */
		davinci_source_power(musb, drvvbus, 0);
		DBG(2, "VBUS %s (%s)%s, devctl %02x\n",
				drvvbus ? "on" : "off",
				otg_state_string(musb),
				err ? " ERROR" : "",
				devctl);
		retval = IRQ_HANDLED;
	}

	if (musb->int_tx || musb->int_rx || musb->int_usb)
		retval |= musb_interrupt(musb);

	/* irq stays asserted until EOI is written */
	musb_writel(tibase, DAVINCI_USB_EOI_REG, 0);

	/* poll for ID change */
	if (is_otg_enabled(musb)
			&& musb->xceiv->state == OTG_STATE_B_IDLE)
		mod_timer(&otg_workaround, jiffies + POLL_SECONDS * HZ);

	spin_unlock_irqrestore(&musb->lock, flags);

	return retval;
}

int musb_platform_set_mode(struct musb *musb, u8 mode)
{
	/* EVM can't do this (right?) */
	return -EIO;
}

int __init musb_platform_init(struct musb *musb)
{
	void __iomem	*tibase = musb->ctrl_base;
	u32		revision;

	usb_nop_xceiv_register();
	musb->xceiv = otg_get_transceiver();
	if (!musb->xceiv)
		return -ENODEV;

	musb->mregs += DAVINCI_BASE_OFFSET;

	clk_enable(musb->clock);

	/* returns zero if e.g. not clocked */
	revision = musb_readl(tibase, DAVINCI_USB_VERSION_REG);
	if (revision == 0)
		goto fail;

	if (is_host_enabled(musb))
		setup_timer(&otg_workaround, otg_timer, (unsigned long) musb);

	musb->board_set_vbus = davinci_set_vbus;
	davinci_source_power(musb, 0, 1);

	/* dm355 EVM swaps D+/D- for signal integrity, and
	 * is clocked from the main 24 MHz crystal.
	 */
	if (machine_is_davinci_dm355_evm()) {
		u32	phy_ctrl = __raw_readl(USB_PHY_CTRL);

		phy_ctrl &= ~(3 << 9);
		phy_ctrl |= USBPHY_DATAPOL;
		__raw_writel(phy_ctrl, USB_PHY_CTRL);
	}

	/* On dm355, the default-A state machine needs DRVVBUS control.
	 * If we won't be a host, there's no need to turn it on.
	 */
	if (cpu_is_davinci_dm355()) {
		u32	deepsleep = __raw_readl(DM355_DEEPSLEEP);

		if (is_host_enabled(musb)) {
			deepsleep &= ~DRVVBUS_OVERRIDE;
		} else {
			deepsleep &= ~DRVVBUS_FORCE;
			deepsleep |= DRVVBUS_OVERRIDE;
		}
		__raw_writel(deepsleep, DM355_DEEPSLEEP);
	}

	/* reset the controller */
	musb_writel(tibase, DAVINCI_USB_CTRL_REG, 0x1);

	/* start the on-chip PHY and its PLL */
	phy_on();

	msleep(5);

	/* NOTE:  irqs are in mixed mode, not bypass to pure-musb */
	pr_debug("DaVinci OTG revision %08x phy %03x control %02x\n",
		revision, __raw_readl(USB_PHY_CTRL),
		musb_readb(tibase, DAVINCI_USB_CTRL_REG));

	musb->isr = davinci_interrupt;
	return 0;

fail:
	clk_disable(musb->clock);

	usb_nop_xceiv_unregister();
	return -ENODEV;
}

int musb_platform_exit(struct musb *musb)
{
	if (is_host_enabled(musb))
		del_timer_sync(&otg_workaround);

	/* force VBUS off */
	if (cpu_is_davinci_dm355()) {
		u32	deepsleep = __raw_readl(DM355_DEEPSLEEP);

		deepsleep &= ~DRVVBUS_FORCE;
		deepsleep |= DRVVBUS_OVERRIDE;
		__raw_writel(deepsleep, DM355_DEEPSLEEP);
	}

	davinci_source_power(musb, 0 /*off*/, 1);

	/* delay, to avoid problems with module reload */
	if (is_host_enabled(musb) && musb->xceiv->default_a) {
		int	maxdelay = 30;
		u8	devctl, warn = 0;

		/* if there's no peripheral connected, this can take a
		 * long time to fall, especially on EVM with huge C133.
		 */
		do {
			devctl = musb_readb(musb->mregs, MUSB_DEVCTL);
			if (!(devctl & MUSB_DEVCTL_VBUS))
				break;
			if ((devctl & MUSB_DEVCTL_VBUS) != warn) {
				warn = devctl & MUSB_DEVCTL_VBUS;
				DBG(1, "VBUS %d\n",
					warn >> MUSB_DEVCTL_VBUS_SHIFT);
			}
			msleep(1000);
			maxdelay--;
		} while (maxdelay > 0);

		/* in OTG mode, another host might be connected */
		if (devctl & MUSB_DEVCTL_VBUS)
			DBG(1, "VBUS off timeout (devctl %02x)\n", devctl);
	}

	phy_off();

	clk_disable(musb->clock);

	usb_nop_xceiv_unregister();

	return 0;
}
