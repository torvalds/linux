/*
 * Xilinx PS USB otg driver.
 *
 * Copyright 2011 Xilinx, Inc.
 *
 * This file is based on langwell_otg.c file with few minor modifications
 * to support Xilinx PS USB controller.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* This driver helps to switch Xilinx OTG controller function between host
 * and peripheral. It works with EHCI driver and Xilinx client controller
 * driver together.
 */
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/moduleparam.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/usb.h>
#include <linux/usb/otg.h>
#include <linux/usb/hcd.h>
#include <linux/notifier.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/io.h>

#include "../core/usb.h"

#include <linux/xilinx_devices.h>
#include <linux/usb/xilinx_usbps_otg.h>

#define	DRIVER_NAME	"xusbps-otg"

static const char driver_name[] = DRIVER_NAME;

/* HSM timers */
static inline struct xusbps_otg_timer *otg_timer_initializer
(void (*function)(unsigned long), unsigned long expires, unsigned long data)
{
	struct xusbps_otg_timer *timer;
	timer = kmalloc(sizeof(struct xusbps_otg_timer), GFP_KERNEL);
	if (timer == NULL)
		return timer;

	timer->function = function;
	timer->expires = expires;
	timer->data = data;
	return timer;
}

static struct xusbps_otg_timer *a_wait_vrise_tmr, *a_aidl_bdis_tmr,
	*b_se0_srp_tmr, *b_srp_init_tmr;

static struct list_head active_timers;

static struct xusbps_otg *the_transceiver;

/* host/client notify transceiver when event affects HNP state */
void xusbps_update_transceiver(void)
{
	struct xusbps_otg *xotg = the_transceiver;

	dev_dbg(xotg->dev, "transceiver is updated\n");

	if (!xotg->qwork)
		return;

	queue_work(xotg->qwork, &xotg->work);
}
EXPORT_SYMBOL(xusbps_update_transceiver);

static int xusbps_otg_set_host(struct usb_otg *otg,
					struct usb_bus *host)
{
	otg->host = host;

	if (host) {
		if (otg->default_a)
			host->is_b_host = 0;
		else
			host->is_b_host = 1;
	}

	return 0;
}

static int xusbps_otg_set_peripheral(struct usb_otg *otg,
					struct usb_gadget *gadget)
{
	otg->gadget = gadget;

	if (gadget) {
		if (otg->default_a)
			gadget->is_a_peripheral = 1;
		else
			gadget->is_a_peripheral = 0;
	}

	return 0;
}

static int xusbps_otg_set_power(struct usb_phy *otg,
				unsigned mA)
{
	return 0;
}

/* A-device drives vbus, controlled through PMIC CHRGCNTL register*/
static int xusbps_otg_set_vbus(struct usb_otg *otg, bool enabled)
{
	struct xusbps_otg		*xotg = the_transceiver;
	u32 val;

	dev_dbg(xotg->dev, "%s <--- %s\n", __func__, enabled ? "on" : "off");

	/* Enable ulpi VBUS if required */
	if (xotg->ulpi)
		otg_set_vbus(xotg->ulpi->otg, enabled);

	val = readl(xotg->base + CI_PORTSC1);

	if (enabled)
		writel((val | PORTSC_PP), xotg->base + CI_PORTSC1);
	else
		writel((val & ~PORTSC_PP), xotg->base + CI_PORTSC1);

	dev_dbg(xotg->dev, "%s --->\n", __func__);

	return 0;
}

/* Charge vbus for VBUS pulsing in SRP */
static void xusbps_otg_chrg_vbus(int on)
{
	struct xusbps_otg	*xotg = the_transceiver;
	u32	val;

	val = readl(xotg->base + CI_OTGSC) & ~OTGSC_INTSTS_MASK;

	if (on)
		/* stop discharging, start charging */
		val = (val & ~OTGSC_VD) | OTGSC_VC;
	else
		/* stop charging */
		val &= ~OTGSC_VC;

	writel(val, xotg->base + CI_OTGSC);
}

#if 0

/* Discharge vbus through a resistor to ground */
static void xusbps_otg_dischrg_vbus(int on)
{
	struct xusbps_otg	*xotg = the_transceiver;
	u32	val;

	val = readl(xotg->base + CI_OTGSC) & ~OTGSC_INTSTS_MASK;

	if (on)
		/* stop charging, start discharging */
		val = (val & ~OTGSC_VC) | OTGSC_VD;
	else
		val &= ~OTGSC_VD;

	writel(val, xotg->base + CI_OTGSC);
}

#endif

/* Start SRP */
static int xusbps_otg_start_srp(struct usb_otg *otg)
{
	struct xusbps_otg		*xotg = the_transceiver;
	u32				val;

	dev_warn(xotg->dev, "Starting SRP...\n");
	dev_dbg(xotg->dev, "%s --->\n", __func__);

	val = readl(xotg->base + CI_OTGSC);

	writel((val & ~OTGSC_INTSTS_MASK) | OTGSC_HADP,
				xotg->base + CI_OTGSC);

	/* Check if the data plus is finished or not */
	msleep(8);
	val = readl(xotg->base + CI_OTGSC);
	if (val & (OTGSC_HADP | OTGSC_DP))
		dev_dbg(xotg->dev, "DataLine SRP Error\n");

	/* If Vbus is valid, then update the hsm */
	if (val & OTGSC_BSV) {
		dev_dbg(xotg->dev, "no b_sess_vld interrupt\n");

		xotg->hsm.b_sess_vld = 1;
		xusbps_update_transceiver();
		return 0;
	}

	dev_warn(xotg->dev, "Starting VBUS Pulsing...\n");

	/* Disable interrupt - b_sess_vld */
	val = readl(xotg->base + CI_OTGSC);
	val &= (~(OTGSC_BSVIE | OTGSC_BSEIE));
	writel(val, xotg->base + CI_OTGSC);

	/* Start VBus SRP, drive vbus to generate VBus pulse */
	xusbps_otg_chrg_vbus(1);
	msleep(15);
	xusbps_otg_chrg_vbus(0);

	/* Enable interrupt - b_sess_vld*/
	val = readl(xotg->base + CI_OTGSC);
	dev_dbg(xotg->dev, "after VBUS pulse otgsc = %x\n", val);

	val |= (OTGSC_BSVIE | OTGSC_BSEIE);
	writel(val, xotg->base + CI_OTGSC);

	/* If Vbus is valid, then update the hsm */
	if (val & OTGSC_BSV) {
		dev_dbg(xotg->dev, "no b_sess_vld interrupt\n");

		xotg->hsm.b_sess_vld = 1;
		xusbps_update_transceiver();
	}

	dev_dbg(xotg->dev, "%s <---\n", __func__);
	return 0;
}

/* Start HNP */
static int xusbps_otg_start_hnp(struct usb_otg *otg)
{
	struct xusbps_otg	*xotg = the_transceiver;
	unsigned long flag = 0;

	dev_warn(xotg->dev, "Starting HNP...\n");
	dev_dbg(xotg->dev, "%s --->\n", __func__);

	if (xotg->otg.otg->default_a && xotg->otg.otg->host &&
			xotg->otg.otg->host->b_hnp_enable) {
		xotg->hsm.a_suspend_req = 1;
		flag = 1;
	}

	if (!xotg->otg.otg->default_a && xotg->otg.otg->host &&
			xotg->hsm.b_bus_req) {
		xotg->hsm.b_bus_req = 0;
		flag = 1;
	}

	if (flag) {
		if (spin_trylock(&xotg->wq_lock)) {
			xusbps_update_transceiver();
			spin_unlock(&xotg->wq_lock);
		}
	} else
		dev_warn(xotg->dev, "HNP not supported\n");

	dev_dbg(xotg->dev, "%s <---\n", __func__);
	return 0;
}

/* stop SOF via bus_suspend */
static void xusbps_otg_loc_sof(int on)
{
	/* Not used */
}

static void xusbps_otg_phy_low_power(int on)
{
	/* Not used */
}

/* After drv vbus, add 2 ms delay to set PHCD */
static void xusbps_otg_phy_low_power_wait(int on)
{
	struct xusbps_otg	*xotg = the_transceiver;

	dev_dbg(xotg->dev, "add 2ms delay before programing PHCD\n");

	mdelay(2);
	xusbps_otg_phy_low_power(on);
}

#ifdef CONFIG_PM_SLEEP
/* Enable/Disable OTG interrupt */
static void xusbps_otg_intr(int on)
{
	struct xusbps_otg		*xotg = the_transceiver;
	u32				val;

	dev_dbg(xotg->dev, "%s ---> %s\n", __func__, on ? "on" : "off");

	val = readl(xotg->base + CI_OTGSC);

	/* OTGSC_INT_MASK doesn't contains 1msInt */
	if (on) {
		val = val | (OTGSC_INT_MASK);
		writel(val, xotg->base + CI_OTGSC);
	} else {
		val = val & ~(OTGSC_INT_MASK);
		writel(val, xotg->base + CI_OTGSC);
	}

	dev_dbg(xotg->dev, "%s <---\n", __func__);
}
#endif

/* set HAAR: Hardware Assist Auto-Reset */
static void xusbps_otg_HAAR(int on)
{
	/* Not used */
}

/* set HABA: Hardware Assist B-Disconnect to A-Connect */
static void xusbps_otg_HABA(int on)
{
	struct xusbps_otg		*xotg = the_transceiver;
	u32				val;

	dev_dbg(xotg->dev, "%s ---> %s\n", __func__, on ? "on" : "off");

	val = readl(xotg->base + CI_OTGSC);
	if (on)
		writel((val & ~OTGSC_INTSTS_MASK) | OTGSC_HABA,
					xotg->base + CI_OTGSC);
	else
		writel((val & ~OTGSC_INTSTS_MASK) & ~OTGSC_HABA,
					xotg->base + CI_OTGSC);

	dev_dbg(xotg->dev, "%s <---\n", __func__);
}

static int xusbps_otg_check_se0_srp(int on)
{
	struct xusbps_otg	*xotg = the_transceiver;
	int			delay_time = TB_SE0_SRP * 10;
	u32			val;

	dev_dbg(xotg->dev, "%s --->\n", __func__);

	do {
		udelay(100);
		if (!delay_time--)
			break;
		val = readl(xotg->base + CI_PORTSC1);
		val &= PORTSC_LS;
	} while (!val);

	dev_dbg(xotg->dev, "%s <---\n", __func__);
	return val;
}

/* The timeout callback function to set time out bit */
static void set_tmout(unsigned long indicator)
{
	*(int *)indicator = 1;
}

static void xusbps_otg_msg(unsigned long indicator)
{
	struct xusbps_otg	*xotg = the_transceiver;

	switch (indicator) {
	case 2:
	case 4:
	case 6:
	case 7:
		dev_warn(xotg->dev,
			"OTG:%lu - deivce not responding\n", indicator);
		break;
	case 3:
		dev_warn(xotg->dev,
			"OTG:%lu - deivce not supported\n", indicator);
		break;
	default:
		dev_warn(xotg->dev, "Do not have this msg\n");
		break;
	}
}

/* Initialize timers */
static int xusbps_otg_init_timers(struct otg_hsm *hsm)
{
	/* HSM used timers */
	a_wait_vrise_tmr = otg_timer_initializer(&set_tmout, TA_WAIT_VRISE,
				(unsigned long)&hsm->a_wait_vrise_tmout);
	if (a_wait_vrise_tmr == NULL)
		return -ENOMEM;
	a_aidl_bdis_tmr = otg_timer_initializer(&set_tmout, TA_AIDL_BDIS,
				(unsigned long)&hsm->a_aidl_bdis_tmout);
	if (a_aidl_bdis_tmr == NULL)
		return -ENOMEM;
	b_se0_srp_tmr = otg_timer_initializer(&set_tmout, TB_SE0_SRP,
				(unsigned long)&hsm->b_se0_srp);
	if (b_se0_srp_tmr == NULL)
		return -ENOMEM;
	b_srp_init_tmr = otg_timer_initializer(&set_tmout, TB_SRP_INIT,
				(unsigned long)&hsm->b_srp_init_tmout);
	if (b_srp_init_tmr == NULL)
		return -ENOMEM;

	return 0;
}

/* Free timers */
static void xusbps_otg_free_timers(void)
{
	kfree(a_wait_vrise_tmr);
	kfree(a_aidl_bdis_tmr);
	kfree(b_se0_srp_tmr);
	kfree(b_srp_init_tmr);
}

/* The timeout callback function to set time out bit */
static void xusbps_otg_timer_fn(unsigned long indicator)
{
	struct xusbps_otg *xotg = the_transceiver;

	*(int *)indicator = 1;

	dev_dbg(xotg->dev, "kernel timer - timeout\n");

	xusbps_update_transceiver();
}

/* kernel timer used instead of HW based interrupt */
static void xusbps_otg_add_ktimer(enum xusbps_otg_timer_type timers)
{
	struct xusbps_otg		*xotg = the_transceiver;
	unsigned long		j = jiffies;
	unsigned long		data, time;

	switch (timers) {
	case TA_WAIT_VRISE_TMR:
		xotg->hsm.a_wait_vrise_tmout = 0;
		data = (unsigned long)&xotg->hsm.a_wait_vrise_tmout;
		time = TA_WAIT_VRISE;
		break;
	case TA_WAIT_BCON_TMR:
		xotg->hsm.a_wait_bcon_tmout = 0;
		data = (unsigned long)&xotg->hsm.a_wait_bcon_tmout;
		time = TA_WAIT_BCON;
		break;
	case TA_AIDL_BDIS_TMR:
		xotg->hsm.a_aidl_bdis_tmout = 0;
		data = (unsigned long)&xotg->hsm.a_aidl_bdis_tmout;
		time = TA_AIDL_BDIS;
		break;
	case TB_ASE0_BRST_TMR:
		xotg->hsm.b_ase0_brst_tmout = 0;
		data = (unsigned long)&xotg->hsm.b_ase0_brst_tmout;
		time = TB_ASE0_BRST;
		break;
	case TB_SRP_INIT_TMR:
		xotg->hsm.b_srp_init_tmout = 0;
		data = (unsigned long)&xotg->hsm.b_srp_init_tmout;
		time = TB_SRP_INIT;
		break;
	case TB_SRP_FAIL_TMR:
		xotg->hsm.b_srp_fail_tmout = 0;
		data = (unsigned long)&xotg->hsm.b_srp_fail_tmout;
		time = TB_SRP_FAIL;
		break;
	case TB_BUS_SUSPEND_TMR:
		xotg->hsm.b_bus_suspend_tmout = 0;
		data = (unsigned long)&xotg->hsm.b_bus_suspend_tmout;
		time = TB_BUS_SUSPEND;
		break;
	default:
		dev_dbg(xotg->dev, "unkown timer, cannot enable it\n");
		return;
	}

	xotg->hsm_timer.data = data;
	xotg->hsm_timer.function = xusbps_otg_timer_fn;
	xotg->hsm_timer.expires = j + time * HZ / 1000; /* milliseconds */

	add_timer(&xotg->hsm_timer);

	dev_dbg(xotg->dev, "add timer successfully\n");
}

/* Add timer to timer list */
static void xusbps_otg_add_timer(void *gtimer)
{
	struct xusbps_otg *xotg = the_transceiver;
	struct xusbps_otg_timer *timer = (struct xusbps_otg_timer *)gtimer;
	struct xusbps_otg_timer *tmp_timer;
	u32	val32;

	/* Check if the timer is already in the active list,
	 * if so update timer count
	 */
	list_for_each_entry(tmp_timer, &active_timers, list)
		if (tmp_timer == timer) {
			timer->count = timer->expires;
			return;
		}
	timer->count = timer->expires;

	if (list_empty(&active_timers)) {
		val32 = readl(xotg->base + CI_OTGSC);
		writel(val32 | OTGSC_1MSE, xotg->base + CI_OTGSC);
	}

	list_add_tail(&timer->list, &active_timers);
}

/* Remove timer from the timer list; clear timeout status */
static void xusbps_otg_del_timer(void *gtimer)
{
	struct xusbps_otg *xotg = the_transceiver;
	struct xusbps_otg_timer *timer = (struct xusbps_otg_timer *)gtimer;
	struct xusbps_otg_timer *tmp_timer, *del_tmp;
	u32 val32;

	list_for_each_entry_safe(tmp_timer, del_tmp, &active_timers, list)
		if (tmp_timer == timer)
			list_del(&timer->list);

	if (list_empty(&active_timers)) {
		val32 = readl(xotg->base + CI_OTGSC);
		writel(val32 & ~OTGSC_1MSE, xotg->base + CI_OTGSC);
	}
}

/* Reduce timer count by 1, and find timeout conditions.*/
static int xusbps_otg_tick_timer(u32 *int_sts)
{
	struct xusbps_otg	*xotg = the_transceiver;
	struct xusbps_otg_timer *tmp_timer, *del_tmp;
	int expired = 0;

	list_for_each_entry_safe(tmp_timer, del_tmp, &active_timers, list) {
		tmp_timer->count--;
		/* check if timer expires */
		if (!tmp_timer->count) {
			list_del(&tmp_timer->list);
			tmp_timer->function(tmp_timer->data);
			expired = 1;
		}
	}

	if (list_empty(&active_timers)) {
		dev_dbg(xotg->dev, "tick timer: disable 1ms int\n");
		*int_sts = *int_sts & ~OTGSC_1MSE;
	}
	return expired;
}

static void reset_otg(void)
{
	struct xusbps_otg	*xotg = the_transceiver;
	int			delay_time = 1000;
	u32			val;

	dev_dbg(xotg->dev, "reseting OTG controller ...\n");
	val = readl(xotg->base + CI_USBCMD);
	writel(val | USBCMD_RST, xotg->base + CI_USBCMD);
	do {
		udelay(100);
		if (!delay_time--)
			dev_dbg(xotg->dev, "reset timeout\n");
		val = readl(xotg->base + CI_USBCMD);
		val &= USBCMD_RST;
	} while (val != 0);
	dev_dbg(xotg->dev, "reset done.\n");
}

static void set_host_mode(void)
{
	struct xusbps_otg	*xotg = the_transceiver;
	u32			val;

	reset_otg();
	val = readl(xotg->base + CI_USBMODE);
	val = (val & (~USBMODE_CM)) | USBMODE_HOST;
	writel(val, xotg->base + CI_USBMODE);
}

static void set_client_mode(void)
{
	struct xusbps_otg	*xotg = the_transceiver;
	u32			val;

	reset_otg();
	val = readl(xotg->base + CI_USBMODE);
	val = (val & (~USBMODE_CM)) | USBMODE_DEVICE;
	writel(val, xotg->base + CI_USBMODE);
}

static void init_hsm(void)
{
	struct xusbps_otg		*xotg = the_transceiver;
	u32				val32;

	/* read OTGSC after reset */
	val32 = readl(xotg->base + CI_OTGSC);

	/* set init state */
	if (val32 & OTGSC_ID) {
		xotg->hsm.id = 1;
		xotg->otg.otg->default_a = 0;
		set_client_mode();
		xotg->otg.state = OTG_STATE_B_IDLE;
	} else {
		xotg->hsm.id = 0;
		xotg->otg.otg->default_a = 1;
		set_host_mode();
		xotg->otg.state = OTG_STATE_A_IDLE;
	}

	/* set session indicator */
	if (!xotg->otg.otg->default_a) {
		if (val32 & OTGSC_BSE)
			xotg->hsm.b_sess_end = 1;
		if (val32 & OTGSC_BSV)
			xotg->hsm.b_sess_vld = 1;
	} else {
		if (val32 & OTGSC_ASV)
			xotg->hsm.a_sess_vld = 1;
		if (val32 & OTGSC_AVV)
			xotg->hsm.a_vbus_vld = 1;
	}

	/* defautly power the bus */
	xotg->hsm.a_bus_req = 0;
	xotg->hsm.a_bus_drop = 0;
	/* defautly don't request bus as B device */
	xotg->hsm.b_bus_req = 0;
	/* no system error */
	xotg->hsm.a_clr_err = 0;

	xusbps_otg_phy_low_power_wait(1);
}

#ifdef CONFIG_PM_SLEEP
static void update_hsm(void)
{
	struct xusbps_otg		*xotg = the_transceiver;
	u32				val32;

	/* read OTGSC */
	val32 = readl(xotg->base + CI_OTGSC);

	xotg->hsm.id = !!(val32 & OTGSC_ID);
	if (!xotg->otg.otg->default_a) {
		xotg->hsm.b_sess_end = !!(val32 & OTGSC_BSE);
		xotg->hsm.b_sess_vld = !!(val32 & OTGSC_BSV);
	} else {
		xotg->hsm.a_sess_vld = !!(val32 & OTGSC_ASV);
		xotg->hsm.a_vbus_vld = !!(val32 & OTGSC_AVV);
	}
}
#endif

static irqreturn_t otg_dummy_irq(int irq, void *_dev)
{
	struct xusbps_otg	*xotg = the_transceiver;
	void __iomem		*reg_base = _dev;
	u32			val;
	u32			int_mask = 0;

	val = readl(reg_base + CI_USBMODE);
	if ((val & USBMODE_CM) != USBMODE_DEVICE)
		return IRQ_NONE;

	val = readl(reg_base + CI_USBSTS);
	int_mask = val & INTR_DUMMY_MASK;

	if (int_mask == 0)
		return IRQ_NONE;

	/* Clear interrupts */
	writel(int_mask, reg_base + CI_USBSTS);

	/* clear hsm.b_conn here since host driver can't detect it
	*  otg_dummy_irq called means B-disconnect happened.
	*/
	if (xotg->hsm.b_conn) {
		xotg->hsm.b_conn = 0;
		if (spin_trylock(&xotg->wq_lock)) {
			xusbps_update_transceiver();
			spin_unlock(&xotg->wq_lock);
		}
	}

	return IRQ_HANDLED;
}

static irqreturn_t otg_irq(int irq, void *_dev)
{
	struct xusbps_otg		*xotg = _dev;
	u32				int_sts, int_en;
	u32				int_mask = 0;
	int				flag = 0;
	unsigned long flags;

	spin_lock_irqsave(&xotg->lock, flags);
	int_sts = readl(xotg->base + CI_OTGSC);
	int_en = (int_sts & OTGSC_INTEN_MASK) >> 8;
	int_mask = int_sts & int_en;

	if (int_mask == 0) {
		spin_unlock_irqrestore(&xotg->lock, flags);
		return IRQ_NONE;
	}

	writel((int_sts & ~OTGSC_INTSTS_MASK) | int_mask,
					xotg->base + CI_OTGSC);
	if (int_mask & OTGSC_IDIS) {
		dev_dbg(xotg->dev, "%s: id change int\n", __func__);
		xotg->hsm.id = (int_sts & OTGSC_ID) ? 1 : 0;
		dev_dbg(xotg->dev, "id = %d\n", xotg->hsm.id);
		flag = 1;
	}
	if (int_mask & OTGSC_DPIS) {
		dev_dbg(xotg->dev, "%s: data pulse int\n", __func__);
		if (xotg->otg.otg->default_a)
			xotg->hsm.a_srp_det = (int_sts & OTGSC_DPS) ? 1 : 0;
		dev_dbg(xotg->dev, "data pulse = %d\n", xotg->hsm.a_srp_det);
		flag = 1;
	}
	if (int_mask & OTGSC_BSEIS) {
		dev_dbg(xotg->dev, "%s: b session end int\n", __func__);
		if (!xotg->otg.otg->default_a)
			xotg->hsm.b_sess_end = (int_sts & OTGSC_BSE) ? 1 : 0;
		dev_dbg(xotg->dev, "b_sess_end = %d\n", xotg->hsm.b_sess_end);
		flag = 1;
	}
	if (int_mask & OTGSC_BSVIS) {
		dev_dbg(xotg->dev, "%s: b session valid int\n", __func__);
		if (!xotg->otg.otg->default_a)
			xotg->hsm.b_sess_vld = (int_sts & OTGSC_BSV) ? 1 : 0;
		dev_dbg(xotg->dev, "b_sess_vld = %d\n", xotg->hsm.b_sess_vld);
		flag = 1;
	}
	if (int_mask & OTGSC_ASVIS) {
		dev_dbg(xotg->dev, "%s: a session valid int\n", __func__);
		if (xotg->otg.otg->default_a)
			xotg->hsm.a_sess_vld = (int_sts & OTGSC_ASV) ? 1 : 0;
		dev_dbg(xotg->dev, "a_sess_vld = %d\n", xotg->hsm.a_sess_vld);
		flag = 1;
	}
	if (int_mask & OTGSC_AVVIS) {
		dev_dbg(xotg->dev, "%s: a vbus valid int\n", __func__);
		if (xotg->otg.otg->default_a)
			xotg->hsm.a_vbus_vld = (int_sts & OTGSC_AVV) ? 1 : 0;
		dev_dbg(xotg->dev, "a_vbus_vld = %d\n", xotg->hsm.a_vbus_vld);
		flag = 1;
	}

	if (int_mask & OTGSC_1MSS) {
		/* need to schedule otg_work if any timer is expired */
		if (xusbps_otg_tick_timer(&int_sts))
			flag = 1;
	}

	if (flag)
		xusbps_update_transceiver();

	spin_unlock_irqrestore(&xotg->lock, flags);
	return IRQ_HANDLED;
}

/**
 * xotg_usbdev_notify - Notifier function called by usb core.
 * @self:	Pointer to notifier_block structure
 * @action:	action which caused the notifier function call.
 * @dev:	Pointer to the usb device structure.
 *
 * This function is a call back function used by usb core to notify
 * device attach/detach events. This is used by OTG state machine.
 *
 * returns:	Always returns NOTIFY_OK.
 **/
static int xotg_usbdev_notify(struct notifier_block *self,
			       unsigned long action, void *dev)
{
	struct xusbps_otg	*xotg = the_transceiver;
	struct usb_phy *otg = &xotg->otg;
	unsigned long otg_port;
	struct usb_device *udev_otg = NULL;
	struct usb_device *udev;
	u32 flag;

	udev = (struct usb_device *)dev;

	if (!otg->otg->host)
		return NOTIFY_OK;

	otg_port = otg->otg->host->otg_port;

	if (otg->otg->host->root_hub)
		udev_otg = usb_hub_find_child(otg->otg->host->root_hub,
								otg_port - 1);

	/* Not otg device notification */
	if (udev != udev_otg)
		return NOTIFY_OK;

	switch (action) {
	case USB_DEVICE_ADD:
		if (xotg->otg.otg->default_a == 1)
			xotg->hsm.b_conn = 1;
		else
			xotg->hsm.a_conn = 1;
		flag = 1;
		break;
	case USB_DEVICE_REMOVE:
		if (xotg->otg.otg->default_a == 1)
			xotg->hsm.b_conn = 0;
		else
			xotg->hsm.a_conn = 0;
		flag = 1;
		break;
	}
	if (flag)
		xusbps_update_transceiver();

	return NOTIFY_OK;
}

static void xusbps_otg_work(struct work_struct *work)
{
	struct xusbps_otg		*xotg;
	int				retval;

	xotg = container_of(work, struct xusbps_otg, work);

	dev_dbg(xotg->dev, "%s: old state = %s\n", __func__,
		usb_otg_state_string(xotg->otg.state));

	switch (xotg->otg.state) {
	case OTG_STATE_UNDEFINED:
	case OTG_STATE_B_IDLE:
		if (!xotg->hsm.id) {
			xusbps_otg_del_timer(b_srp_init_tmr);
			del_timer_sync(&xotg->hsm_timer);

			xotg->otg.otg->default_a = 1;
			xotg->hsm.a_srp_det = 0;

			xusbps_otg_chrg_vbus(0);
			set_host_mode();
			xusbps_otg_phy_low_power(1);

			xotg->otg.state = OTG_STATE_A_IDLE;
			xusbps_update_transceiver();
		} else if (xotg->hsm.b_sess_vld) {
			xusbps_otg_del_timer(b_srp_init_tmr);
			del_timer_sync(&xotg->hsm_timer);
			xotg->hsm.b_bus_req = 0;
			xotg->hsm.b_sess_end = 0;
			xotg->hsm.a_bus_suspend = 0;
			xusbps_otg_chrg_vbus(0);

			if (xotg->start_peripheral) {
				xotg->start_peripheral(&xotg->otg);
				xotg->otg.state = OTG_STATE_B_PERIPHERAL;
			} else
				dev_dbg(xotg->dev,
					"client driver not loaded\n");
		} else if (xotg->hsm.b_srp_init_tmout) {
			xotg->hsm.b_srp_init_tmout = 0;
			dev_warn(xotg->dev, "SRP init timeout\n");
		} else if (xotg->hsm.b_srp_fail_tmout) {
			xotg->hsm.b_srp_fail_tmout = 0;
			xotg->hsm.b_bus_req = 0;

			/* No silence failure */
			xusbps_otg_msg(6);
			dev_warn(xotg->dev, "SRP failed\n");
		} else if (xotg->hsm.b_bus_req && xotg->hsm.b_sess_end) {
			del_timer_sync(&xotg->hsm_timer);
			/* workaround for b_se0_srp detection */
			retval = xusbps_otg_check_se0_srp(0);
			if (retval) {
				xotg->hsm.b_bus_req = 0;
				dev_dbg(xotg->dev, "LS isn't SE0, try later\n");
			} else {
				/* clear the PHCD before start srp */
				xusbps_otg_phy_low_power(0);

				/* Start SRP */
				xusbps_otg_add_timer(b_srp_init_tmr);
				xotg->otg.otg->start_srp(xotg->otg.otg);
				xusbps_otg_del_timer(b_srp_init_tmr);
				xusbps_otg_add_ktimer(TB_SRP_FAIL_TMR);

				/* reset PHY low power mode here */
				xusbps_otg_phy_low_power_wait(1);
			}
		}
		break;
	case OTG_STATE_B_SRP_INIT:
		if (!xotg->hsm.id) {
			xotg->otg.otg->default_a = 1;
			xotg->hsm.a_srp_det = 0;

			/* Turn off VBus */
			xotg->otg.otg->set_vbus(xotg->otg.otg, false);
			xusbps_otg_chrg_vbus(0);
			set_host_mode();
			xusbps_otg_phy_low_power(1);
			xotg->otg.state = OTG_STATE_A_IDLE;
			xusbps_update_transceiver();
		} else if (xotg->hsm.b_sess_vld) {
			xusbps_otg_chrg_vbus(0);
			if (xotg->start_peripheral) {
				xotg->start_peripheral(&xotg->otg);
				xotg->otg.state = OTG_STATE_B_PERIPHERAL;
			} else
				dev_dbg(xotg->dev,
					"client driver not loaded\n");
		}
		break;
	case OTG_STATE_B_PERIPHERAL:
		if (!xotg->hsm.id) {
			xotg->otg.otg->default_a = 1;
			xotg->hsm.a_srp_det = 0;

			xusbps_otg_chrg_vbus(0);

			if (xotg->stop_peripheral)
				xotg->stop_peripheral(&xotg->otg);
			else
				dev_dbg(xotg->dev,
					"client driver has been removed.\n");

			set_host_mode();
			xusbps_otg_phy_low_power(1);
			xotg->otg.state = OTG_STATE_A_IDLE;
			xusbps_update_transceiver();
		} else if (!xotg->hsm.b_sess_vld) {
			xotg->hsm.b_hnp_enable = 0;
			xotg->hsm.b_bus_req = 0;

			if (xotg->stop_peripheral)
				xotg->stop_peripheral(&xotg->otg);
			else
				dev_dbg(xotg->dev,
					"client driver has been removed.\n");

			xotg->otg.state = OTG_STATE_B_IDLE;
		} else if (xotg->hsm.b_bus_req && xotg->otg.otg->gadget &&
					xotg->otg.otg->gadget->b_hnp_enable &&
					xotg->hsm.a_bus_suspend) {
			dev_warn(xotg->dev, "HNP detected\n");

			if (xotg->stop_peripheral)
				xotg->stop_peripheral(&xotg->otg);
			else
				dev_dbg(xotg->dev,
					"client driver has been removed.\n");

			xusbps_otg_HAAR(1);
			xotg->hsm.a_conn = 0;

			xotg->otg.state = OTG_STATE_B_WAIT_ACON;
			if (xotg->start_host) {
				xotg->start_host(&xotg->otg);
			} else
				dev_dbg(xotg->dev,
						"host driver not loaded.\n");

			xotg->hsm.a_bus_resume = 0;
			xusbps_otg_add_ktimer(TB_ASE0_BRST_TMR);
		}
		break;

	case OTG_STATE_B_WAIT_ACON:
		if (!xotg->hsm.id) {
			/* delete hsm timer for b_ase0_brst_tmr */
			del_timer_sync(&xotg->hsm_timer);

			xotg->otg.otg->default_a = 1;
			xotg->hsm.a_srp_det = 0;

			xusbps_otg_chrg_vbus(0);

			xusbps_otg_HAAR(0);
			if (xotg->stop_host)
				xotg->stop_host(&xotg->otg);
			else
				dev_dbg(xotg->dev,
					"host driver has been removed.\n");

			set_host_mode();
			xusbps_otg_phy_low_power(1);
			xotg->otg.state = OTG_STATE_A_IDLE;
			xusbps_update_transceiver();
		} else if (!xotg->hsm.b_sess_vld) {
			/* delete hsm timer for b_ase0_brst_tmr */
			del_timer_sync(&xotg->hsm_timer);

			xotg->hsm.b_hnp_enable = 0;
			xotg->hsm.b_bus_req = 0;

			xusbps_otg_chrg_vbus(0);
			xusbps_otg_HAAR(0);

			if (xotg->stop_host)
				xotg->stop_host(&xotg->otg);
			else
				dev_dbg(xotg->dev,
					"host driver has been removed.\n");

			set_client_mode();
			xusbps_otg_phy_low_power(1);
			xotg->otg.state = OTG_STATE_B_IDLE;
		} else if (xotg->hsm.a_conn) {
			/* delete hsm timer for b_ase0_brst_tmr */
			del_timer_sync(&xotg->hsm_timer);

			xusbps_otg_HAAR(0);
			xotg->otg.state = OTG_STATE_B_HOST;
			xusbps_update_transceiver();
		} else if (xotg->hsm.a_bus_resume ||
				xotg->hsm.b_ase0_brst_tmout) {
			dev_warn(xotg->dev, "A device connect failed\n");
			/* delete hsm timer for b_ase0_brst_tmr */
			del_timer_sync(&xotg->hsm_timer);

			xusbps_otg_HAAR(0);
			xusbps_otg_msg(7);

			if (xotg->stop_host)
				xotg->stop_host(&xotg->otg);
			else
				dev_dbg(xotg->dev,
					"host driver has been removed.\n");

			xotg->hsm.a_bus_suspend = 0;
			xotg->hsm.b_bus_req = 0;
			xotg->otg.state = OTG_STATE_B_PERIPHERAL;
			if (xotg->start_peripheral)
				xotg->start_peripheral(&xotg->otg);
			else
				dev_dbg(xotg->dev,
					"client driver not loaded.\n");
		}
		break;

	case OTG_STATE_B_HOST:
		if (!xotg->hsm.id) {
			xotg->otg.otg->default_a = 1;
			xotg->hsm.a_srp_det = 0;

			xusbps_otg_chrg_vbus(0);

			if (xotg->stop_host)
				xotg->stop_host(&xotg->otg);
			else
				dev_dbg(xotg->dev,
					"host driver has been removed.\n");

			set_host_mode();
			xusbps_otg_phy_low_power(1);
			xotg->otg.state = OTG_STATE_A_IDLE;
			xusbps_update_transceiver();
		} else if (!xotg->hsm.b_sess_vld) {
			xotg->hsm.b_hnp_enable = 0;
			xotg->hsm.b_bus_req = 0;

			xusbps_otg_chrg_vbus(0);
			if (xotg->stop_host)
				xotg->stop_host(&xotg->otg);
			else
				dev_dbg(xotg->dev,
					"host driver has been removed.\n");

			set_client_mode();
			xusbps_otg_phy_low_power(1);
			xotg->otg.state = OTG_STATE_B_IDLE;
		} else if ((!xotg->hsm.b_bus_req) ||
				(!xotg->hsm.a_conn)) {
			xotg->hsm.b_bus_req = 0;
			xusbps_otg_loc_sof(0);

			/* Fix: The kernel crash in usb_port_suspend
				during HNP */
			msleep(20);

			if (xotg->stop_host)
				xotg->stop_host(&xotg->otg);
			else
				dev_dbg(xotg->dev,
					"host driver has been removed.\n");

			xotg->hsm.a_bus_suspend = 0;
			xotg->otg.state = OTG_STATE_B_PERIPHERAL;
			if (xotg->start_peripheral)
				xotg->start_peripheral(&xotg->otg);
			else
				dev_dbg(xotg->dev,
						"client driver not loaded.\n");
		}
		break;

	case OTG_STATE_A_IDLE:
		xotg->otg.otg->default_a = 1;
		if (xotg->hsm.id) {
			xotg->otg.otg->default_a = 0;
			xotg->hsm.b_bus_req = 0;
			xotg->hsm.vbus_srp_up = 0;

			xusbps_otg_chrg_vbus(0);
			set_client_mode();
			xusbps_otg_phy_low_power(1);
			xotg->otg.state = OTG_STATE_B_IDLE;
			xusbps_update_transceiver();
		} else if (!xotg->hsm.a_bus_drop &&
			(xotg->hsm.a_srp_det || xotg->hsm.a_bus_req)) {
			dev_warn(xotg->dev,
			"SRP detected or User has requested for the Bus\n");
			xusbps_otg_phy_low_power(0);

			/* Turn on VBus */
			xotg->otg.otg->set_vbus(xotg->otg.otg, true);

			xotg->hsm.vbus_srp_up = 0;
			xotg->hsm.a_wait_vrise_tmout = 0;
			xusbps_otg_add_timer(a_wait_vrise_tmr);
			xotg->otg.state = OTG_STATE_A_WAIT_VRISE;
			xusbps_update_transceiver();
		} else if (!xotg->hsm.a_bus_drop && xotg->hsm.a_sess_vld) {
			xotg->hsm.vbus_srp_up = 1;
		} else if (!xotg->hsm.a_sess_vld && xotg->hsm.vbus_srp_up) {
			msleep(10);
			xusbps_otg_phy_low_power(0);

			/* Turn on VBus */
			xotg->otg.otg->set_vbus(xotg->otg.otg, true);
			xotg->hsm.a_srp_det = 1;
			xotg->hsm.vbus_srp_up = 0;
			xotg->hsm.a_wait_vrise_tmout = 0;
			xusbps_otg_add_timer(a_wait_vrise_tmr);
			xotg->otg.state = OTG_STATE_A_WAIT_VRISE;
			xusbps_update_transceiver();
		} else if (!xotg->hsm.a_sess_vld &&
				!xotg->hsm.vbus_srp_up) {
			xusbps_otg_phy_low_power(1);
		}
		break;
	case OTG_STATE_A_WAIT_VRISE:
		if (xotg->hsm.id) {
			xusbps_otg_del_timer(a_wait_vrise_tmr);
			xotg->hsm.b_bus_req = 0;
			xotg->otg.otg->default_a = 0;

			/* Turn off VBus */
			xotg->otg.otg->set_vbus(xotg->otg.otg, false);
			set_client_mode();
			xusbps_otg_phy_low_power_wait(1);
			xotg->otg.state = OTG_STATE_B_IDLE;
		} else if (xotg->hsm.a_vbus_vld) {
			xusbps_otg_del_timer(a_wait_vrise_tmr);
			xotg->hsm.b_conn = 0;
			if (xotg->start_host)
				xotg->start_host(&xotg->otg);
			else {
				dev_dbg(xotg->dev, "host driver not loaded.\n");
				break;
			}
			xusbps_otg_add_ktimer(TA_WAIT_BCON_TMR);
			xotg->otg.state = OTG_STATE_A_WAIT_BCON;
		} else if (xotg->hsm.a_wait_vrise_tmout) {
			xotg->hsm.b_conn = 0;
			if (xotg->hsm.a_vbus_vld) {
				if (xotg->start_host)
					xotg->start_host(&xotg->otg);
				else {
					dev_dbg(xotg->dev,
						"host driver not loaded.\n");
					break;
				}
				xusbps_otg_add_ktimer(TA_WAIT_BCON_TMR);
				xotg->otg.state = OTG_STATE_A_WAIT_BCON;
			} else {

				/* Turn off VBus */
				xotg->otg.otg->set_vbus(xotg->otg.otg, false);
				xusbps_otg_phy_low_power_wait(1);
				xotg->otg.state = OTG_STATE_A_VBUS_ERR;
			}
		}
		break;
	case OTG_STATE_A_WAIT_BCON:
		if (xotg->hsm.id) {
			/* delete hsm timer for a_wait_bcon_tmr */
			del_timer_sync(&xotg->hsm_timer);

			xotg->otg.otg->default_a = 0;
			xotg->hsm.b_bus_req = 0;

			if (xotg->stop_host)
				xotg->stop_host(&xotg->otg);
			else
				dev_dbg(xotg->dev,
					"host driver has been removed.\n");

			/* Turn off VBus */
			xotg->otg.otg->set_vbus(xotg->otg.otg, false);
			set_client_mode();
			xusbps_otg_phy_low_power_wait(1);
			xotg->otg.state = OTG_STATE_B_IDLE;
			xusbps_update_transceiver();
		} else if (!xotg->hsm.a_vbus_vld) {
			/* delete hsm timer for a_wait_bcon_tmr */
			del_timer_sync(&xotg->hsm_timer);

			if (xotg->stop_host)
				xotg->stop_host(&xotg->otg);
			else
				dev_dbg(xotg->dev,
					"host driver has been removed.\n");

			/* Turn off VBus */
			xotg->otg.otg->set_vbus(xotg->otg.otg, false);
			xusbps_otg_phy_low_power_wait(1);
			xotg->otg.state = OTG_STATE_A_VBUS_ERR;
		} else if (xotg->hsm.a_bus_drop ||
				(xotg->hsm.a_wait_bcon_tmout &&
				!xotg->hsm.a_bus_req)) {
			dev_warn(xotg->dev, "B connect timeout\n");
			/* delete hsm timer for a_wait_bcon_tmr */
			del_timer_sync(&xotg->hsm_timer);

			if (xotg->stop_host)
				xotg->stop_host(&xotg->otg);
			else
				dev_dbg(xotg->dev,
					"host driver has been removed.\n");

			/* Turn off VBus */
			xotg->otg.otg->set_vbus(xotg->otg.otg, false);
			xotg->otg.state = OTG_STATE_A_WAIT_VFALL;
		} else if (xotg->hsm.b_conn) {
			/* delete hsm timer for a_wait_bcon_tmr */
			del_timer_sync(&xotg->hsm_timer);

			xotg->hsm.a_suspend_req = 0;
			/* Make it zero as it should not be used by driver */
			xotg->hsm.a_bus_req = 0;
			xotg->hsm.a_srp_det = 0;
			xotg->otg.state = OTG_STATE_A_HOST;
		}
		break;
	case OTG_STATE_A_HOST:
		if (xotg->hsm.id) {
			xotg->otg.otg->default_a = 0;
			xotg->hsm.b_bus_req = 0;

			if (xotg->stop_host)
				xotg->stop_host(&xotg->otg);
			else
				dev_dbg(xotg->dev,
					"host driver has been removed.\n");

			/* Turn off VBus */
			xotg->otg.otg->set_vbus(xotg->otg.otg, false);
			set_client_mode();
			xusbps_otg_phy_low_power_wait(1);
			xotg->otg.state = OTG_STATE_B_IDLE;
			xusbps_update_transceiver();
		} else if (xotg->hsm.a_bus_drop ||
				(xotg->otg.otg->host &&
				!xotg->otg.otg->host->b_hnp_enable &&
					!xotg->hsm.a_bus_req)) {
			if (xotg->stop_host)
				xotg->stop_host(&xotg->otg);
			else
				dev_dbg(xotg->dev,
					"host driver has been removed.\n");

			/* Turn off VBus */
			xotg->otg.otg->set_vbus(xotg->otg.otg, false);
			xotg->otg.state = OTG_STATE_A_WAIT_VFALL;
		} else if (!xotg->hsm.a_vbus_vld) {
			if (xotg->stop_host)
				xotg->stop_host(&xotg->otg);
			else
				dev_dbg(xotg->dev,
					"host driver has been removed.\n");

			/* Turn off VBus */
			xotg->otg.otg->set_vbus(xotg->otg.otg, false);
			xusbps_otg_phy_low_power_wait(1);
			xotg->otg.state = OTG_STATE_A_VBUS_ERR;
		} else if (xotg->otg.otg->host &&
				xotg->otg.otg->host->b_hnp_enable &&
				(!xotg->hsm.a_bus_req ||
					xotg->hsm.a_suspend_req)) {
			/* Set HABA to enable hardware assistance to signal
			 *  A-connect after receiver B-disconnect. Hardware
			 *  will then set client mode and enable URE, SLE and
			 *  PCE after the assistance. otg_dummy_irq is used to
			 *  clean these ints when client driver is not resumed.
			 */
			if (request_irq(xotg->irq, otg_dummy_irq, IRQF_SHARED,
					driver_name, xotg->base) != 0) {
				dev_dbg(xotg->dev,
					"request interrupt %d failed\n",
						xotg->irq);
			}
			/* set HABA */
			xusbps_otg_HABA(1);
			xotg->hsm.b_bus_resume = 0;
			xotg->hsm.a_aidl_bdis_tmout = 0;
			xusbps_otg_loc_sof(0);
			/* clear PHCD to enable HW timer */
			xusbps_otg_phy_low_power(0);
			xusbps_otg_add_timer(a_aidl_bdis_tmr);
			xotg->otg.state = OTG_STATE_A_SUSPEND;
		} else if (!xotg->hsm.b_conn || !xotg->hsm.a_bus_req) {
			xusbps_otg_add_ktimer(TA_WAIT_BCON_TMR);
			xotg->otg.state = OTG_STATE_A_WAIT_BCON;
		}
		break;
	case OTG_STATE_A_SUSPEND:
		if (xotg->hsm.id) {
			xusbps_otg_del_timer(a_aidl_bdis_tmr);
			xusbps_otg_HABA(0);
			free_irq(xotg->irq, xotg->base);
			xotg->otg.otg->default_a = 0;
			xotg->hsm.b_bus_req = 0;

			if (xotg->stop_host)
				xotg->stop_host(&xotg->otg);
			else
				dev_dbg(xotg->dev,
					"host driver has been removed.\n");

			/* Turn off VBus */
			xotg->otg.otg->set_vbus(xotg->otg.otg, false);
			set_client_mode();
			xusbps_otg_phy_low_power(1);
			xotg->otg.state = OTG_STATE_B_IDLE;
			xusbps_update_transceiver();
		} else if (xotg->hsm.a_bus_req ||
				xotg->hsm.b_bus_resume) {
			xusbps_otg_del_timer(a_aidl_bdis_tmr);
			xusbps_otg_HABA(0);
			free_irq(xotg->irq, xotg->base);
			xotg->hsm.a_suspend_req = 0;
			xusbps_otg_loc_sof(1);
			xotg->otg.state = OTG_STATE_A_HOST;
		} else if (xotg->hsm.a_aidl_bdis_tmout ||
				xotg->hsm.a_bus_drop) {
			dev_warn(xotg->dev, "B disconnect timeout\n");
			xusbps_otg_del_timer(a_aidl_bdis_tmr);
			xusbps_otg_HABA(0);
			free_irq(xotg->irq, xotg->base);
			if (xotg->stop_host)
				xotg->stop_host(&xotg->otg);
			else
				dev_dbg(xotg->dev,
					"host driver has been removed.\n");

			/* Turn off VBus */
			xotg->otg.otg->set_vbus(xotg->otg.otg, false);
			xotg->otg.state = OTG_STATE_A_WAIT_VFALL;
		} else if (!xotg->hsm.b_conn && xotg->otg.otg->host &&
				xotg->otg.otg->host->b_hnp_enable) {
			xusbps_otg_del_timer(a_aidl_bdis_tmr);
			xusbps_otg_HABA(0);
			free_irq(xotg->irq, xotg->base);

			if (xotg->stop_host)
				xotg->stop_host(&xotg->otg);
			else
				dev_dbg(xotg->dev,
					"host driver has been removed.\n");

			xotg->hsm.b_bus_suspend = 0;
			xotg->hsm.b_bus_suspend_vld = 0;

			xotg->otg.state = OTG_STATE_A_PERIPHERAL;
			/* msleep(200); */
			if (xotg->start_peripheral)
				xotg->start_peripheral(&xotg->otg);
			else
				dev_dbg(xotg->dev,
					"client driver not loaded.\n");
			xusbps_otg_add_ktimer(TB_BUS_SUSPEND_TMR);
			break;
		} else if (!xotg->hsm.a_vbus_vld) {
			xusbps_otg_del_timer(a_aidl_bdis_tmr);
			xusbps_otg_HABA(0);
			free_irq(xotg->irq, xotg->base);
			if (xotg->stop_host)
				xotg->stop_host(&xotg->otg);
			else
				dev_dbg(xotg->dev,
					"host driver has been removed.\n");

			/* Turn off VBus */
			xotg->otg.otg->set_vbus(xotg->otg.otg, false);
			xusbps_otg_phy_low_power_wait(1);
			xotg->otg.state = OTG_STATE_A_VBUS_ERR;
		}
		break;
	case OTG_STATE_A_PERIPHERAL:
		if (xotg->hsm.id) {
			/* delete hsm timer for b_bus_suspend_tmr */
			del_timer_sync(&xotg->hsm_timer);
			xotg->otg.otg->default_a = 0;
			xotg->hsm.b_bus_req = 0;
			if (xotg->stop_peripheral)
				xotg->stop_peripheral(&xotg->otg);
			else
				dev_dbg(xotg->dev,
					"client driver has been removed.\n");

			/* Turn off VBus */
			xotg->otg.otg->set_vbus(xotg->otg.otg, false);
			set_client_mode();
			xusbps_otg_phy_low_power_wait(1);
			xotg->otg.state = OTG_STATE_B_IDLE;
			xusbps_update_transceiver();
		} else if (!xotg->hsm.a_vbus_vld) {
			/* delete hsm timer for b_bus_suspend_tmr */
			del_timer_sync(&xotg->hsm_timer);

			if (xotg->stop_peripheral)
				xotg->stop_peripheral(&xotg->otg);
			else
				dev_dbg(xotg->dev,
					"client driver has been removed.\n");

			/* Turn off VBus */
			xotg->otg.otg->set_vbus(xotg->otg.otg, false);
			xusbps_otg_phy_low_power_wait(1);
			xotg->otg.state = OTG_STATE_A_VBUS_ERR;
		} else if (xotg->hsm.a_bus_drop) {
			/* delete hsm timer for b_bus_suspend_tmr */
			del_timer_sync(&xotg->hsm_timer);

			if (xotg->stop_peripheral)
				xotg->stop_peripheral(&xotg->otg);
			else
				dev_dbg(xotg->dev,
					"client driver has been removed.\n");

			/* Turn off VBus */
			xotg->otg.otg->set_vbus(xotg->otg.otg, false);
			xotg->otg.state = OTG_STATE_A_WAIT_VFALL;
		} else if (xotg->hsm.b_bus_suspend) {
			dev_warn(xotg->dev, "HNP detected\n");
			/* delete hsm timer for b_bus_suspend_tmr */
			del_timer_sync(&xotg->hsm_timer);

			if (xotg->stop_peripheral)
				xotg->stop_peripheral(&xotg->otg);
			else
				dev_dbg(xotg->dev,
					"client driver has been removed.\n");

			xotg->otg.state = OTG_STATE_A_WAIT_BCON;
			if (xotg->start_host)
				xotg->start_host(&xotg->otg);
			else
				dev_dbg(xotg->dev,
						"host driver not loaded.\n");
			xusbps_otg_add_ktimer(TA_WAIT_BCON_TMR);
		} else if (xotg->hsm.b_bus_suspend_tmout) {
			u32	val;
			val = readl(xotg->base + CI_PORTSC1);
			if (!(val & PORTSC_SUSP))
				break;

			if (xotg->stop_peripheral)
				xotg->stop_peripheral(&xotg->otg);
			else
				dev_dbg(xotg->dev,
					"client driver has been removed.\n");

			xotg->otg.state = OTG_STATE_A_WAIT_BCON;
			if (xotg->start_host)
				xotg->start_host(&xotg->otg);
			else
				dev_dbg(xotg->dev,
						"host driver not loaded.\n");
			xusbps_otg_add_ktimer(TA_WAIT_BCON_TMR);
		}
		break;
	case OTG_STATE_A_VBUS_ERR:
		if (xotg->hsm.id) {
			xotg->otg.otg->default_a = 0;
			xotg->hsm.a_clr_err = 0;
			xotg->hsm.a_srp_det = 0;
			set_client_mode();
			xusbps_otg_phy_low_power(1);
			xotg->otg.state = OTG_STATE_B_IDLE;
			xusbps_update_transceiver();
		} else if (xotg->hsm.a_clr_err) {
			xotg->hsm.a_clr_err = 0;
			xotg->hsm.a_srp_det = 0;
			reset_otg();
			init_hsm();
			if (xotg->otg.state == OTG_STATE_A_IDLE)
				xusbps_update_transceiver();
		} else {
			/* FW will clear PHCD bit when any VBus
			 * event detected. Reset PHCD to 1 again */
			xusbps_otg_phy_low_power(1);
		}
		break;
	case OTG_STATE_A_WAIT_VFALL:
		if (xotg->hsm.id) {
			xotg->otg.otg->default_a = 0;
			set_client_mode();
			xusbps_otg_phy_low_power(1);
			xotg->otg.state = OTG_STATE_B_IDLE;
			xusbps_update_transceiver();
		} else if (xotg->hsm.a_bus_req) {

			/* Turn on VBus */
			xotg->otg.otg->set_vbus(xotg->otg.otg, true);
			xotg->hsm.a_wait_vrise_tmout = 0;
			xusbps_otg_add_timer(a_wait_vrise_tmr);
			xotg->otg.state = OTG_STATE_A_WAIT_VRISE;
		} else if (!xotg->hsm.a_sess_vld) {
			xotg->hsm.a_srp_det = 0;
			set_host_mode();
			xusbps_otg_phy_low_power(1);
			xotg->otg.state = OTG_STATE_A_IDLE;
		}
		break;
	default:
		break;
	}

	dev_dbg(xotg->dev, "%s: new state = %s\n", __func__,
		usb_otg_state_string(xotg->otg.state));
}

static ssize_t
show_registers(struct device *_dev, struct device_attribute *attr, char *buf)
{
	struct xusbps_otg	*xotg = the_transceiver;
	char			*next;
	unsigned		size, t;

	next = buf;
	size = PAGE_SIZE;

	t = scnprintf(next, size,
		"\n"
		"USBCMD = 0x%08x\n"
		"USBSTS = 0x%08x\n"
		"USBINTR = 0x%08x\n"
		"ASYNCLISTADDR = 0x%08x\n"
		"PORTSC1 = 0x%08x\n"
		"OTGSC = 0x%08x\n"
		"USBMODE = 0x%08x\n",
		readl(xotg->base + 0x140),
		readl(xotg->base + 0x144),
		readl(xotg->base + 0x148),
		readl(xotg->base + 0x158),
		readl(xotg->base + 0x184),
		readl(xotg->base + 0x1a4),
		readl(xotg->base + 0x1a8)
	     );
	size -= t;
	next += t;

	return PAGE_SIZE - size;
}
static DEVICE_ATTR(registers, S_IRUGO, show_registers, NULL);

static ssize_t
show_hsm(struct device *_dev, struct device_attribute *attr, char *buf)
{
	struct xusbps_otg		*xotg = the_transceiver;
	char				*next;
	unsigned			size, t;

	next = buf;
	size = PAGE_SIZE;

	if (xotg->otg.otg->host)
		xotg->hsm.a_set_b_hnp_en = xotg->otg.otg->host->b_hnp_enable;

	if (xotg->otg.otg->gadget)
		xotg->hsm.b_hnp_enable = xotg->otg.otg->gadget->b_hnp_enable;

	t = scnprintf(next, size,
		"\n"
		"current state = %s\n"
		"a_bus_resume = \t%d\n"
		"a_bus_suspend = \t%d\n"
		"a_conn = \t%d\n"
		"a_sess_vld = \t%d\n"
		"a_srp_det = \t%d\n"
		"a_vbus_vld = \t%d\n"
		"b_bus_resume = \t%d\n"
		"b_bus_suspend = \t%d\n"
		"b_conn = \t%d\n"
		"b_se0_srp = \t%d\n"
		"b_sess_end = \t%d\n"
		"b_sess_vld = \t%d\n"
		"id = \t%d\n"
		"a_set_b_hnp_en = \t%d\n"
		"b_srp_done = \t%d\n"
		"b_hnp_enable = \t%d\n"
		"a_wait_vrise_tmout = \t%d\n"
		"a_wait_bcon_tmout = \t%d\n"
		"a_aidl_bdis_tmout = \t%d\n"
		"b_ase0_brst_tmout = \t%d\n"
		"a_bus_drop = \t%d\n"
		"a_bus_req = \t%d\n"
		"a_clr_err = \t%d\n"
		"a_suspend_req = \t%d\n"
		"b_bus_req = \t%d\n"
		"b_bus_suspend_tmout = \t%d\n"
		"b_bus_suspend_vld = \t%d\n",
		usb_otg_state_string(xotg->otg.state),
		xotg->hsm.a_bus_resume,
		xotg->hsm.a_bus_suspend,
		xotg->hsm.a_conn,
		xotg->hsm.a_sess_vld,
		xotg->hsm.a_srp_det,
		xotg->hsm.a_vbus_vld,
		xotg->hsm.b_bus_resume,
		xotg->hsm.b_bus_suspend,
		xotg->hsm.b_conn,
		xotg->hsm.b_se0_srp,
		xotg->hsm.b_sess_end,
		xotg->hsm.b_sess_vld,
		xotg->hsm.id,
		xotg->hsm.a_set_b_hnp_en,
		xotg->hsm.b_srp_done,
		xotg->hsm.b_hnp_enable,
		xotg->hsm.a_wait_vrise_tmout,
		xotg->hsm.a_wait_bcon_tmout,
		xotg->hsm.a_aidl_bdis_tmout,
		xotg->hsm.b_ase0_brst_tmout,
		xotg->hsm.a_bus_drop,
		xotg->hsm.a_bus_req,
		xotg->hsm.a_clr_err,
		xotg->hsm.a_suspend_req,
		xotg->hsm.b_bus_req,
		xotg->hsm.b_bus_suspend_tmout,
		xotg->hsm.b_bus_suspend_vld
		);
	size -= t;
	next += t;

	return PAGE_SIZE - size;
}
static DEVICE_ATTR(hsm, S_IRUGO, show_hsm, NULL);

static ssize_t
get_a_bus_req(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct xusbps_otg	*xotg = the_transceiver;
	char			*next;
	unsigned		size, t;

	next = buf;
	size = PAGE_SIZE;

	t = scnprintf(next, size, "%d", xotg->hsm.a_bus_req);
	size -= t;
	next += t;

	return PAGE_SIZE - size;
}

static ssize_t
set_a_bus_req(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct xusbps_otg		*xotg = the_transceiver;

	if (!xotg->otg.otg->default_a)
		return -1;
	if (count > 2)
		return -1;

	if (buf[0] == '0') {
		xotg->hsm.a_bus_req = 0;
		dev_dbg(xotg->dev, "User request: a_bus_req = 0\n");
	} else if (buf[0] == '1') {
		/* If a_bus_drop is TRUE, a_bus_req can't be set */
		if (xotg->hsm.a_bus_drop)
			return -1;
		xotg->hsm.a_bus_req = 1;
		dev_dbg(xotg->dev, "User request: a_bus_req = 1\n");
	}
	if (spin_trylock(&xotg->wq_lock)) {
		xusbps_update_transceiver();
		spin_unlock(&xotg->wq_lock);
	}
	return count;
}
static DEVICE_ATTR(a_bus_req, S_IRUGO | S_IWUSR, get_a_bus_req, set_a_bus_req);

static ssize_t
get_a_bus_drop(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct xusbps_otg	*xotg = the_transceiver;
	char			*next;
	unsigned		size, t;

	next = buf;
	size = PAGE_SIZE;

	t = scnprintf(next, size, "%d", xotg->hsm.a_bus_drop);
	size -= t;
	next += t;

	return PAGE_SIZE - size;
}

static ssize_t
set_a_bus_drop(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct xusbps_otg		*xotg = the_transceiver;

	if (!xotg->otg.otg->default_a)
		return -1;
	if (count > 2)
		return -1;

	if (buf[0] == '0') {
		xotg->hsm.a_bus_drop = 0;
		dev_dbg(xotg->dev, "User request: a_bus_drop = 0\n");
	} else if (buf[0] == '1') {
		xotg->hsm.a_bus_drop = 1;
		xotg->hsm.a_bus_req = 0;
		dev_dbg(xotg->dev, "User request: a_bus_drop = 1\n");
		dev_dbg(xotg->dev, "User request: and a_bus_req = 0\n");
	}
	if (spin_trylock(&xotg->wq_lock)) {
		xusbps_update_transceiver();
		spin_unlock(&xotg->wq_lock);
	}
	return count;
}
static DEVICE_ATTR(a_bus_drop, S_IRUGO | S_IWUSR, get_a_bus_drop,
		set_a_bus_drop);

static ssize_t
get_b_bus_req(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct xusbps_otg	*xotg = the_transceiver;
	char			*next;
	unsigned		size, t;

	next = buf;
	size = PAGE_SIZE;

	t = scnprintf(next, size, "%d", xotg->hsm.b_bus_req);
	size -= t;
	next += t;

	return PAGE_SIZE - size;
}

static ssize_t
set_b_bus_req(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct xusbps_otg		*xotg = the_transceiver;

	if (xotg->otg.otg->default_a)
		return -1;

	if (count > 2)
		return -1;

	if (buf[0] == '0') {
		xotg->hsm.b_bus_req = 0;
		dev_dbg(xotg->dev, "User request: b_bus_req = 0\n");
	} else if (buf[0] == '1') {
		xotg->hsm.b_bus_req = 1;
		dev_dbg(xotg->dev, "User request: b_bus_req = 1\n");
	}
	if (spin_trylock(&xotg->wq_lock)) {
		xusbps_update_transceiver();
		spin_unlock(&xotg->wq_lock);
	}
	return count;
}
static DEVICE_ATTR(b_bus_req, S_IRUGO | S_IWUSR, get_b_bus_req, set_b_bus_req);

static ssize_t
set_a_clr_err(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct xusbps_otg		*xotg = the_transceiver;

	if (!xotg->otg.otg->default_a)
		return -1;
	if (count > 2)
		return -1;

	if (buf[0] == '1') {
		xotg->hsm.a_clr_err = 1;
		dev_dbg(xotg->dev, "User request: a_clr_err = 1\n");
	}
	if (spin_trylock(&xotg->wq_lock)) {
		xusbps_update_transceiver();
		spin_unlock(&xotg->wq_lock);
	}
	return count;
}
static DEVICE_ATTR(a_clr_err, S_IWUSR, NULL, set_a_clr_err);

/**
 * suspend_otg_device - suspend the otg device.
 *
 * @otg:	Pointer to the otg transceiver structure.
 *
 * This function suspends usb devices connected to the otg port
 * of the host controller.
 *
 * returns:	0 on success or error value on failure
 **/
static int suspend_otg_device(struct usb_phy *otg)
{
	struct xusbps_otg		*xotg = the_transceiver;
	unsigned long otg_port = otg->otg->host->otg_port;
	struct usb_device *udev;
	int err;

	udev = usb_hub_find_child(otg->otg->host->root_hub, otg_port - 1);

	if (udev) {
		err = usb_port_suspend(udev, PMSG_SUSPEND);
		if (err < 0)
			dev_dbg(xotg->dev, "HNP fail, %d\n", err);

		/* Change the state of the usb device if HNP is successful */
		usb_set_device_state(udev, USB_STATE_NOTATTACHED);
	} else {
		err = -ENODEV;
		dev_dbg(xotg->dev, "No device connected to roothub\n");
	}
	return err;
}

static ssize_t
do_hnp(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct xusbps_otg		*xotg = the_transceiver;
	unsigned long ret;

	if (count > 2)
		return -1;

	if (buf[0] == '1') {
		if (xotg->otg.otg->default_a && xotg->otg.otg->host &&
				xotg->otg.otg->host->b_hnp_enable &&
				(xotg->otg.state == OTG_STATE_A_HOST)) {
			ret = suspend_otg_device(&xotg->otg);
			if (ret)
				return -1;
		}

		if (!xotg->otg.otg->default_a && xotg->otg.otg->host &&
				xotg->hsm.b_bus_req) {
			ret = suspend_otg_device(&xotg->otg);
			if (ret)
				return -1;
		}
	}
	return count;
}
static DEVICE_ATTR(do_hnp, S_IWUSR, NULL, do_hnp);

static int xusbps_otg_clk_notifier_cb(struct notifier_block *nb,
		unsigned long event, void *data)
{

	switch (event) {
	case PRE_RATE_CHANGE:
		/* if a rate change is announced we need to check whether we can
		 * maintain the current frequency by changing the clock
		 * dividers.
		 */
		/* fall through */
	case POST_RATE_CHANGE:
		return NOTIFY_OK;
	case ABORT_RATE_CHANGE:
	default:
		return NOTIFY_DONE;
	}
}

static struct attribute *inputs_attrs[] = {
	&dev_attr_a_bus_req.attr,
	&dev_attr_a_bus_drop.attr,
	&dev_attr_b_bus_req.attr,
	&dev_attr_a_clr_err.attr,
	&dev_attr_do_hnp.attr,
	NULL,
};

static struct attribute_group debug_dev_attr_group = {
	.name = "inputs",
	.attrs = inputs_attrs,
};

static int xusbps_otg_remove(struct platform_device *pdev)
{
	struct xusbps_otg *xotg = the_transceiver;

	if (xotg->qwork) {
		flush_workqueue(xotg->qwork);
		destroy_workqueue(xotg->qwork);
	}
	xusbps_otg_free_timers();

	/* disable OTGSC interrupt as OTGSC doesn't change in reset */
	writel(0, xotg->base + CI_OTGSC);

	usb_remove_phy(&xotg->otg);
	sysfs_remove_group(&pdev->dev.kobj, &debug_dev_attr_group);
	device_remove_file(&pdev->dev, &dev_attr_hsm);
	device_remove_file(&pdev->dev, &dev_attr_registers);
	clk_notifier_unregister(xotg->clk, &xotg->clk_rate_change_nb);
	clk_disable_unprepare(xotg->clk);

	return 0;
}

static int xusbps_otg_probe(struct platform_device *pdev)
{
	int			retval;
	u32			val32;
	struct xusbps_otg	*xotg;
	char			qname[] = "xusbps_otg_queue";
	struct xusbps_usb2_platform_data *pdata;

	pdata = pdev->dev.platform_data;
	if (!pdata)
		return -ENODEV;

	dev_dbg(&pdev->dev, "\notg controller is detected.\n");

	xotg = devm_kzalloc(&pdev->dev, sizeof(*xotg), GFP_KERNEL);
	if (xotg == NULL)
		return -ENOMEM;

	the_transceiver = xotg;

	/* Setup ulpi phy for OTG */
	xotg->ulpi = pdata->ulpi;

	xotg->otg.otg = devm_kzalloc(sizeof(struct usb_otg), GFP_KERNEL);
	if (!xotg->otg.otg)
		return -ENOMEM;

	xotg->base = pdata->regs;
	xotg->irq = pdata->irq;
	if (!xotg->base || !xotg->irq) {
		retval = -ENODEV;
		goto err;
	}

	xotg->qwork = create_singlethread_workqueue(qname);
	if (!xotg->qwork) {
		dev_dbg(&pdev->dev, "cannot create workqueue %s\n", qname);
		retval = -ENOMEM;
		goto err;
	}
	INIT_WORK(&xotg->work, xusbps_otg_work);

	xotg->clk = pdata->clk;
	retval = clk_prepare_enable(xotg->clk);
	if (retval) {
		dev_err(&pdev->dev, "Unable to enable APER clock.\n");
		goto err;
	}

	xotg->clk_rate_change_nb.notifier_call = xusbps_otg_clk_notifier_cb;
	xotg->clk_rate_change_nb.next = NULL;
	if (clk_notifier_register(xotg->clk, &xotg->clk_rate_change_nb))
		dev_warn(&pdev->dev, "Unable to register clock notifier.\n");

	/* OTG common part */
	xotg->dev = &pdev->dev;
	xotg->otg.dev = xotg->dev;
	xotg->otg.label = driver_name;
	xotg->otg.otg->set_host = xusbps_otg_set_host;
	xotg->otg.otg->set_peripheral = xusbps_otg_set_peripheral;
	xotg->otg.set_power = xusbps_otg_set_power;
	xotg->otg.otg->set_vbus = xusbps_otg_set_vbus;
	xotg->otg.otg->start_srp = xusbps_otg_start_srp;
	xotg->otg.otg->start_hnp = xusbps_otg_start_hnp;
	xotg->otg.state = OTG_STATE_UNDEFINED;

	if (usb_add_phy(&xotg->otg, USB_PHY_TYPE_USB2)) {
		dev_dbg(xotg->dev, "can't set transceiver\n");
		retval = -EBUSY;
		goto err_out_clk_disable;
	}

	pdata->otg = &xotg->otg;
	reset_otg();
	init_hsm();

	spin_lock_init(&xotg->lock);
	spin_lock_init(&xotg->wq_lock);
	INIT_LIST_HEAD(&active_timers);
	retval = xusbps_otg_init_timers(&xotg->hsm);
	if (retval) {
		dev_dbg(&pdev->dev, "Failed to init timers\n");
		goto err_out_clk_disable;
	}

	init_timer(&xotg->hsm_timer);

	xotg->xotg_notifier.notifier_call = xotg_usbdev_notify;
	usb_register_notify((struct notifier_block *)
					&xotg->xotg_notifier.notifier_call);

	retval = devm_request_irq(&pdev->dev, xotg->irq, otg_irq, IRQF_SHARED,
				driver_name, xotg);
	if (retval) {
		dev_dbg(xotg->dev, "request interrupt %d failed\n", xotg->irq);
		retval = -EBUSY;
		goto err_out_clk_disable;
	}

	/* enable OTGSC int */
	val32 = OTGSC_DPIE | OTGSC_BSEIE | OTGSC_BSVIE |
		OTGSC_ASVIE | OTGSC_AVVIE | OTGSC_IDIE | OTGSC_IDPU;
	writel(val32, xotg->base + CI_OTGSC);

	retval = device_create_file(&pdev->dev, &dev_attr_registers);
	if (retval < 0) {
		dev_dbg(xotg->dev,
			"Can't register sysfs attribute: %d\n", retval);
		goto err_out_clk_disable;
	}

	retval = device_create_file(&pdev->dev, &dev_attr_hsm);
	if (retval < 0) {
		dev_dbg(xotg->dev, "Can't hsm sysfs attribute: %d\n", retval);
		goto err_out_clk_disable;
	}

	retval = sysfs_create_group(&pdev->dev.kobj, &debug_dev_attr_group);
	if (retval < 0) {
		dev_dbg(xotg->dev,
			"Can't register sysfs attr group: %d\n", retval);
		goto err_out_clk_disable;
	}

	if (xotg->otg.state == OTG_STATE_A_IDLE)
		xusbps_update_transceiver();

	return 0;

err_out_clk_disable:
	clk_notifier_unregister(xotg->clk, &xotg->clk_rate_change_nb);
	clk_disable_unprepare(xotg->clk);
err:
	xusbps_otg_remove(pdev);

	return retval;
}

#ifdef CONFIG_PM_SLEEP
static void transceiver_suspend(struct platform_device *pdev)
{
	xusbps_otg_phy_low_power(1);
}

static int xusbps_otg_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct xusbps_otg		*xotg = the_transceiver;
	int				ret = 0;

	/* Disbale OTG interrupts */
	xusbps_otg_intr(0);

	if (xotg->irq)
		free_irq(xotg->irq, xotg);

	/* Prevent more otg_work */
	flush_workqueue(xotg->qwork);
	destroy_workqueue(xotg->qwork);
	xotg->qwork = NULL;

	/* start actions */
	switch (xotg->otg.state) {
	case OTG_STATE_A_WAIT_VFALL:
		xotg->otg.state = OTG_STATE_A_IDLE;
	case OTG_STATE_A_IDLE:
	case OTG_STATE_B_IDLE:
	case OTG_STATE_A_VBUS_ERR:
		transceiver_suspend(pdev);
		break;
	case OTG_STATE_A_WAIT_VRISE:
		xusbps_otg_del_timer(a_wait_vrise_tmr);
		xotg->hsm.a_srp_det = 0;

		/* Turn off VBus */
		xotg->otg.otg->set_vbus(xotg->otg.otg, false);
		xotg->otg.state = OTG_STATE_A_IDLE;
		transceiver_suspend(pdev);
		break;
	case OTG_STATE_A_WAIT_BCON:
		del_timer_sync(&xotg->hsm_timer);
		if (xotg->stop_host)
			xotg->stop_host(&xotg->otg);
		else
			dev_dbg(&pdev->dev, "host driver has been removed.\n");

		xotg->hsm.a_srp_det = 0;

		/* Turn off VBus */
		xotg->otg.otg->set_vbus(xotg->otg.otg, false);
		xotg->otg.state = OTG_STATE_A_IDLE;
		transceiver_suspend(pdev);
		break;
	case OTG_STATE_A_HOST:
		if (xotg->stop_host)
			xotg->stop_host(&xotg->otg);
		else
			dev_dbg(&pdev->dev, "host driver has been removed.\n");

		xotg->hsm.a_srp_det = 0;

		/* Turn off VBus */
		xotg->otg.otg->set_vbus(xotg->otg.otg, false);

		xotg->otg.state = OTG_STATE_A_IDLE;
		transceiver_suspend(pdev);
		break;
	case OTG_STATE_A_SUSPEND:
		xusbps_otg_del_timer(a_aidl_bdis_tmr);
		xusbps_otg_HABA(0);
		if (xotg->stop_host)
			xotg->stop_host(&xotg->otg);
		else
			dev_dbg(xotg->dev, "host driver has been removed.\n");
		xotg->hsm.a_srp_det = 0;

		/* Turn off VBus */
		xotg->otg.otg->set_vbus(xotg->otg.otg, false);
		xotg->otg.state = OTG_STATE_A_IDLE;
		transceiver_suspend(pdev);
		break;
	case OTG_STATE_A_PERIPHERAL:
		del_timer_sync(&xotg->hsm_timer);

		if (xotg->stop_peripheral)
			xotg->stop_peripheral(&xotg->otg);
		else
			dev_dbg(&pdev->dev,
				"client driver has been removed.\n");
		xotg->hsm.a_srp_det = 0;

		/* Turn off VBus */
		xotg->otg.otg->set_vbus(xotg->otg.otg, false);
		xotg->otg.state = OTG_STATE_A_IDLE;
		transceiver_suspend(pdev);
		break;
	case OTG_STATE_B_HOST:
		if (xotg->stop_host)
			xotg->stop_host(&xotg->otg);
		else
			dev_dbg(&pdev->dev, "host driver has been removed.\n");
		xotg->hsm.b_bus_req = 0;
		xotg->otg.state = OTG_STATE_B_IDLE;
		transceiver_suspend(pdev);
		break;
	case OTG_STATE_B_PERIPHERAL:
		if (xotg->stop_peripheral)
			xotg->stop_peripheral(&xotg->otg);
		else
			dev_dbg(&pdev->dev,
				"client driver has been removed.\n");
		xotg->otg.state = OTG_STATE_B_IDLE;
		transceiver_suspend(pdev);
		break;
	case OTG_STATE_B_WAIT_ACON:
		/* delete hsm timer for b_ase0_brst_tmr */
		del_timer_sync(&xotg->hsm_timer);

		xusbps_otg_HAAR(0);

		if (xotg->stop_host)
			xotg->stop_host(&xotg->otg);
		else
			dev_dbg(&pdev->dev, "host driver has been removed.\n");
		xotg->hsm.b_bus_req = 0;
		xotg->otg.state = OTG_STATE_B_IDLE;
		transceiver_suspend(pdev);
		break;
	default:
		dev_dbg(xotg->dev, "error state before suspend\n");
		break;
	}

	if (!ret)
		clk_disable(xotg->clk);
	return ret;
}

static void transceiver_resume(struct platform_device *pdev)
{
	/* Not used */
}

static int xusbps_otg_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct xusbps_otg	*xotg = the_transceiver;
	int			ret = 0;

	ret = clk_enable(xotg->clk);
	if (ret) {
		dev_err(&pdev->dev, "cannot enable clock. resume failed.\n");
		return ret;
	}

	transceiver_resume(pdev);

	xotg->qwork = create_singlethread_workqueue("xusbps_otg_queue");
	if (!xotg->qwork) {
		dev_dbg(&pdev->dev, "cannot create xusbps otg workqueuen");
		ret = -ENOMEM;
		goto error;
	}

	if (request_irq(xotg->irq, otg_irq, IRQF_SHARED,
				driver_name, xotg) != 0) {
		dev_dbg(&pdev->dev, "request interrupt %d failed\n", xotg->irq);
		ret = -EBUSY;
		goto error;
	}

	/* enable OTG interrupts */
	xusbps_otg_intr(1);

	update_hsm();

	xusbps_update_transceiver();

	return ret;
error:
	xusbps_otg_intr(0);
	transceiver_suspend(pdev);
	return ret;
}

static const struct dev_pm_ops xusbps_otg_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(xusbps_otg_suspend, xusbps_otg_resume)
};
#define XUSBPS_OTG_PM	(&xusbps_otg_dev_pm_ops)

#else /* ! CONFIG_PM_SLEEP */
#define XUSBPS_OTG_PM	NULL
#endif /* ! CONFIG_PM_SLEEP */

#ifndef CONFIG_USB_XUSBPS_DR_OF
static struct platform_driver xusbps_otg_driver = {
#else
struct platform_driver xusbps_otg_driver = {
#endif
	.probe		= xusbps_otg_probe,
	.remove		= xusbps_otg_remove,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= DRIVER_NAME,
		.pm	= XUSBPS_OTG_PM,
	},
};

#ifndef CONFIG_USB_XUSBPS_DR_OF
module_platform_driver(xusbps_otg_driver);
#endif

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("Xilinx PS USB OTG driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRIVER_NAME);
