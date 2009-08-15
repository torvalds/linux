/*
 * Intel Langwell USB OTG transceiver driver
 * Copyright (C) 2008 - 2009, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */
/* This driver helps to switch Langwell OTG controller function between host
 * and peripheral. It works with EHCI driver and Langwell client controller
 * driver together.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/moduleparam.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/usb.h>
#include <linux/usb/otg.h>
#include <linux/notifier.h>
#include <asm/ipc_defs.h>
#include <linux/delay.h>
#include "../core/hcd.h"

#include <linux/usb/langwell_otg.h>

#define	DRIVER_DESC		"Intel Langwell USB OTG transceiver driver"
#define	DRIVER_VERSION		"3.0.0.32L.0002"

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("Henry Yuan <hang.yuan@intel.com>, Hao Wu <hao.wu@intel.com>");
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");

static const char driver_name[] = "langwell_otg";

static int langwell_otg_probe(struct pci_dev *pdev,
			const struct pci_device_id *id);
static void langwell_otg_remove(struct pci_dev *pdev);
static int langwell_otg_suspend(struct pci_dev *pdev, pm_message_t message);
static int langwell_otg_resume(struct pci_dev *pdev);

static int langwell_otg_set_host(struct otg_transceiver *otg,
				struct usb_bus *host);
static int langwell_otg_set_peripheral(struct otg_transceiver *otg,
				struct usb_gadget *gadget);
static int langwell_otg_start_srp(struct otg_transceiver *otg);

static const struct pci_device_id pci_ids[] = {{
	.class =        ((PCI_CLASS_SERIAL_USB << 8) | 0xfe),
	.class_mask =   ~0,
	.vendor =	0x8086,
	.device =	0x0811,
	.subvendor =	PCI_ANY_ID,
	.subdevice =	PCI_ANY_ID,
}, { /* end: all zeroes */ }
};

static struct pci_driver otg_pci_driver = {
	.name =		(char *) driver_name,
	.id_table =	pci_ids,

	.probe =	langwell_otg_probe,
	.remove =	langwell_otg_remove,

	.suspend =	langwell_otg_suspend,
	.resume =	langwell_otg_resume,
};

static const char *state_string(enum usb_otg_state state)
{
	switch (state) {
	case OTG_STATE_A_IDLE:
		return "a_idle";
	case OTG_STATE_A_WAIT_VRISE:
		return "a_wait_vrise";
	case OTG_STATE_A_WAIT_BCON:
		return "a_wait_bcon";
	case OTG_STATE_A_HOST:
		return "a_host";
	case OTG_STATE_A_SUSPEND:
		return "a_suspend";
	case OTG_STATE_A_PERIPHERAL:
		return "a_peripheral";
	case OTG_STATE_A_WAIT_VFALL:
		return "a_wait_vfall";
	case OTG_STATE_A_VBUS_ERR:
		return "a_vbus_err";
	case OTG_STATE_B_IDLE:
		return "b_idle";
	case OTG_STATE_B_SRP_INIT:
		return "b_srp_init";
	case OTG_STATE_B_PERIPHERAL:
		return "b_peripheral";
	case OTG_STATE_B_WAIT_ACON:
		return "b_wait_acon";
	case OTG_STATE_B_HOST:
		return "b_host";
	default:
		return "UNDEFINED";
	}
}

/* HSM timers */
static inline struct langwell_otg_timer *otg_timer_initializer
(void (*function)(unsigned long), unsigned long expires, unsigned long data)
{
	struct langwell_otg_timer *timer;
	timer = kmalloc(sizeof(struct langwell_otg_timer), GFP_KERNEL);
	timer->function = function;
	timer->expires = expires;
	timer->data = data;
	return timer;
}

static struct langwell_otg_timer *a_wait_vrise_tmr, *a_wait_bcon_tmr,
	*a_aidl_bdis_tmr, *b_ase0_brst_tmr, *b_se0_srp_tmr, *b_srp_res_tmr,
	*b_bus_suspend_tmr;

static struct list_head active_timers;

static struct langwell_otg *the_transceiver;

/* host/client notify transceiver when event affects HNP state */
void langwell_update_transceiver()
{
	otg_dbg("transceiver driver is notified\n");
	queue_work(the_transceiver->qwork, &the_transceiver->work);
}
EXPORT_SYMBOL(langwell_update_transceiver);

static int langwell_otg_set_host(struct otg_transceiver *otg,
					struct usb_bus *host)
{
	otg->host = host;

	return 0;
}

static int langwell_otg_set_peripheral(struct otg_transceiver *otg,
					struct usb_gadget *gadget)
{
	otg->gadget = gadget;

	return 0;
}

static int langwell_otg_set_power(struct otg_transceiver *otg,
				unsigned mA)
{
	return 0;
}

/* A-device drives vbus, controlled through PMIC CHRGCNTL register*/
static void langwell_otg_drv_vbus(int on)
{
	struct ipc_pmic_reg_data	pmic_data = {0};
	struct ipc_pmic_reg_data	battery_data;

	/* Check if battery is attached or not */
	battery_data.pmic_reg_data[0].register_address = 0xd2;
	battery_data.ioc = 0;
	battery_data.num_entries = 1;
	if (ipc_pmic_register_read(&battery_data)) {
		otg_dbg("Failed to read PMIC register 0xd2.\n");
		return;
	}

	if ((battery_data.pmic_reg_data[0].value & 0x20) == 0) {
		otg_dbg("no battery attached\n");
		return;
	}

	/* Workaround for battery attachment issue */
	if (battery_data.pmic_reg_data[0].value == 0x34) {
		otg_dbg("battery \n");
		return;
	}

	otg_dbg("battery attached\n");

	pmic_data.ioc = 0;
	pmic_data.pmic_reg_data[0].register_address = 0xD4;
	pmic_data.num_entries = 1;
	if (on)
		pmic_data.pmic_reg_data[0].value = 0x20;
	else
		pmic_data.pmic_reg_data[0].value = 0xc0;

	if (ipc_pmic_register_write(&pmic_data, TRUE))
		otg_dbg("Failed to write PMIC.\n");

}

/* charge vbus or discharge vbus through a resistor to ground */
static void langwell_otg_chrg_vbus(int on)
{

	u32	val;

	val = readl(the_transceiver->regs + CI_OTGSC);

	if (on)
		writel((val & ~OTGSC_INTSTS_MASK) | OTGSC_VC,
				the_transceiver->regs + CI_OTGSC);
	else
		writel((val & ~OTGSC_INTSTS_MASK) | OTGSC_VD,
				the_transceiver->regs + CI_OTGSC);

}

/* Start SRP */
static int langwell_otg_start_srp(struct otg_transceiver *otg)
{
	u32	val;

	otg_dbg("Start SRP ->\n");

	val = readl(the_transceiver->regs + CI_OTGSC);

	writel((val & ~OTGSC_INTSTS_MASK) | OTGSC_HADP,
		the_transceiver->regs + CI_OTGSC);

	/* Check if the data plus is finished or not */
	msleep(8);
	val = readl(the_transceiver->regs + CI_OTGSC);
	if (val & (OTGSC_HADP | OTGSC_DP))
		otg_dbg("DataLine SRP Error\n");

	/* FIXME: VBus SRP */

	return 0;
}


/* stop SOF via bus_suspend */
static void langwell_otg_loc_sof(int on)
{
	struct usb_hcd	*hcd;
	int		err;

	otg_dbg("loc_sof -> %d\n", on);

	hcd = bus_to_hcd(the_transceiver->otg.host);
	if (on)
		err = hcd->driver->bus_resume(hcd);
	else
		err = hcd->driver->bus_suspend(hcd);

	if (err)
		otg_dbg("Failed to resume/suspend bus - %d\n", err);
}

static void langwell_otg_phy_low_power(int on)
{
	u32	val;

	otg_dbg("phy low power mode-> %d\n", on);

	val = readl(the_transceiver->regs + CI_HOSTPC1);
	if (on)
		writel(val | HOSTPC1_PHCD, the_transceiver->regs + CI_HOSTPC1);
	else
		writel(val & ~HOSTPC1_PHCD, the_transceiver->regs + CI_HOSTPC1);
}

/* Enable/Disable OTG interrupt */
static void langwell_otg_intr(int on)
{
	u32 val;

	otg_dbg("interrupt -> %d\n", on);

	val = readl(the_transceiver->regs + CI_OTGSC);
	if (on) {
		val = val | (OTGSC_INTEN_MASK | OTGSC_IDPU);
		writel(val, the_transceiver->regs + CI_OTGSC);
	} else {
		val = val & ~(OTGSC_INTEN_MASK | OTGSC_IDPU);
		writel(val, the_transceiver->regs + CI_OTGSC);
	}
}

/* set HAAR: Hardware Assist Auto-Reset */
static void langwell_otg_HAAR(int on)
{
	u32	val;

	otg_dbg("HAAR -> %d\n", on);

	val = readl(the_transceiver->regs + CI_OTGSC);
	if (on)
		writel((val & ~OTGSC_INTSTS_MASK) | OTGSC_HAAR,
				the_transceiver->regs + CI_OTGSC);
	else
		writel((val & ~OTGSC_INTSTS_MASK) & ~OTGSC_HAAR,
				the_transceiver->regs + CI_OTGSC);
}

/* set HABA: Hardware Assist B-Disconnect to A-Connect */
static void langwell_otg_HABA(int on)
{
	u32	val;

	otg_dbg("HABA -> %d\n", on);

	val = readl(the_transceiver->regs + CI_OTGSC);
	if (on)
		writel((val & ~OTGSC_INTSTS_MASK) | OTGSC_HABA,
				the_transceiver->regs + CI_OTGSC);
	else
		writel((val & ~OTGSC_INTSTS_MASK) & ~OTGSC_HABA,
				the_transceiver->regs + CI_OTGSC);
}

static int langwell_otg_check_se0_srp(int on)
{
	u32 val;

	int delay_time = TB_SE0_SRP * 10; /* step is 100us */

	otg_dbg("check_se0_srp -> \n");

	do {
		udelay(100);
		if (!delay_time--)
			break;
		val = readl(the_transceiver->regs + CI_PORTSC1);
		val &= PORTSC_LS;
	} while (!val);

	otg_dbg("check_se0_srp <- \n");
	return val;
}

/* The timeout callback function to set time out bit */
static void set_tmout(unsigned long indicator)
{
	*(int *)indicator = 1;
}

void langwell_otg_nsf_msg(unsigned long indicator)
{
	switch (indicator) {
	case 2:
	case 4:
	case 6:
	case 7:
		printk(KERN_ERR "OTG:NSF-%lu - deivce not responding\n",
				indicator);
		break;
	case 3:
		printk(KERN_ERR "OTG:NSF-%lu - deivce not supported\n",
				indicator);
		break;
	default:
		printk(KERN_ERR "Do not have this kind of NSF\n");
		break;
	}
}

/* Initialize timers */
static void langwell_otg_init_timers(struct otg_hsm *hsm)
{
	/* HSM used timers */
	a_wait_vrise_tmr = otg_timer_initializer(&set_tmout, TA_WAIT_VRISE,
				(unsigned long)&hsm->a_wait_vrise_tmout);
	a_wait_bcon_tmr = otg_timer_initializer(&set_tmout, TA_WAIT_BCON,
				(unsigned long)&hsm->a_wait_bcon_tmout);
	a_aidl_bdis_tmr = otg_timer_initializer(&set_tmout, TA_AIDL_BDIS,
				(unsigned long)&hsm->a_aidl_bdis_tmout);
	b_ase0_brst_tmr = otg_timer_initializer(&set_tmout, TB_ASE0_BRST,
				(unsigned long)&hsm->b_ase0_brst_tmout);
	b_se0_srp_tmr = otg_timer_initializer(&set_tmout, TB_SE0_SRP,
				(unsigned long)&hsm->b_se0_srp);
	b_srp_res_tmr = otg_timer_initializer(&set_tmout, TB_SRP_RES,
				(unsigned long)&hsm->b_srp_res_tmout);
	b_bus_suspend_tmr = otg_timer_initializer(&set_tmout, TB_BUS_SUSPEND,
				(unsigned long)&hsm->b_bus_suspend_tmout);
}

/* Free timers */
static void langwell_otg_free_timers(void)
{
	kfree(a_wait_vrise_tmr);
	kfree(a_wait_bcon_tmr);
	kfree(a_aidl_bdis_tmr);
	kfree(b_ase0_brst_tmr);
	kfree(b_se0_srp_tmr);
	kfree(b_srp_res_tmr);
	kfree(b_bus_suspend_tmr);
}

/* Add timer to timer list */
static void langwell_otg_add_timer(void *gtimer)
{
	struct langwell_otg_timer *timer = (struct langwell_otg_timer *)gtimer;
	struct langwell_otg_timer *tmp_timer;
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
		val32 = readl(the_transceiver->regs + CI_OTGSC);
		writel(val32 | OTGSC_1MSE, the_transceiver->regs + CI_OTGSC);
	}

	list_add_tail(&timer->list, &active_timers);
}

/* Remove timer from the timer list; clear timeout status */
static void langwell_otg_del_timer(void *gtimer)
{
	struct langwell_otg_timer *timer = (struct langwell_otg_timer *)gtimer;
	struct langwell_otg_timer *tmp_timer, *del_tmp;
	u32 val32;

	list_for_each_entry_safe(tmp_timer, del_tmp, &active_timers, list)
		if (tmp_timer == timer)
			list_del(&timer->list);

	if (list_empty(&active_timers)) {
		val32 = readl(the_transceiver->regs + CI_OTGSC);
		writel(val32 & ~OTGSC_1MSE, the_transceiver->regs + CI_OTGSC);
	}
}

/* Reduce timer count by 1, and find timeout conditions.*/
static int langwell_otg_tick_timer(u32 *int_sts)
{
	struct langwell_otg_timer *tmp_timer, *del_tmp;
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
		otg_dbg("tick timer: disable 1ms int\n");
		*int_sts = *int_sts & ~OTGSC_1MSE;
	}
	return expired;
}

static void reset_otg(void)
{
	u32	val;
	int	delay_time = 1000;

	otg_dbg("reseting OTG controller ...\n");
	val = readl(the_transceiver->regs + CI_USBCMD);
	writel(val | USBCMD_RST, the_transceiver->regs + CI_USBCMD);
	do {
		udelay(100);
		if (!delay_time--)
			otg_dbg("reset timeout\n");
		val = readl(the_transceiver->regs + CI_USBCMD);
		val &= USBCMD_RST;
	} while (val != 0);
	otg_dbg("reset done.\n");
}

static void set_host_mode(void)
{
	u32 	val;

	reset_otg();
	val = readl(the_transceiver->regs + CI_USBMODE);
	val = (val & (~USBMODE_CM)) | USBMODE_HOST;
	writel(val, the_transceiver->regs + CI_USBMODE);
}

static void set_client_mode(void)
{
	u32 	val;

	reset_otg();
	val = readl(the_transceiver->regs + CI_USBMODE);
	val = (val & (~USBMODE_CM)) | USBMODE_DEVICE;
	writel(val, the_transceiver->regs + CI_USBMODE);
}

static void init_hsm(void)
{
	struct langwell_otg	*langwell = the_transceiver;
	u32			val32;

	/* read OTGSC after reset */
	val32 = readl(langwell->regs + CI_OTGSC);
	otg_dbg("%s: OTGSC init value = 0x%x\n", __func__, val32);

	/* set init state */
	if (val32 & OTGSC_ID) {
		langwell->hsm.id = 1;
		langwell->otg.default_a = 0;
		set_client_mode();
		langwell->otg.state = OTG_STATE_B_IDLE;
		langwell_otg_drv_vbus(0);
	} else {
		langwell->hsm.id = 0;
		langwell->otg.default_a = 1;
		set_host_mode();
		langwell->otg.state = OTG_STATE_A_IDLE;
	}

	/* set session indicator */
	if (val32 & OTGSC_BSE)
		langwell->hsm.b_sess_end = 1;
	if (val32 & OTGSC_BSV)
		langwell->hsm.b_sess_vld = 1;
	if (val32 & OTGSC_ASV)
		langwell->hsm.a_sess_vld = 1;
	if (val32 & OTGSC_AVV)
		langwell->hsm.a_vbus_vld = 1;

	/* defautly power the bus */
	langwell->hsm.a_bus_req = 1;
	langwell->hsm.a_bus_drop = 0;
	/* defautly don't request bus as B device */
	langwell->hsm.b_bus_req = 0;
	/* no system error */
	langwell->hsm.a_clr_err = 0;
}

static irqreturn_t otg_dummy_irq(int irq, void *_dev)
{
	void __iomem	*reg_base = _dev;
	u32	val;
	u32	int_mask = 0;

	val = readl(reg_base + CI_USBMODE);
	if ((val & USBMODE_CM) != USBMODE_DEVICE)
		return IRQ_NONE;

	val = readl(reg_base + CI_USBSTS);
	int_mask = val & INTR_DUMMY_MASK;

	if (int_mask == 0)
		return IRQ_NONE;

	/* clear hsm.b_conn here since host driver can't detect it
	*  otg_dummy_irq called means B-disconnect happened.
	*/
	if (the_transceiver->hsm.b_conn) {
		the_transceiver->hsm.b_conn = 0;
		if (spin_trylock(&the_transceiver->wq_lock)) {
			queue_work(the_transceiver->qwork,
				&the_transceiver->work);
			spin_unlock(&the_transceiver->wq_lock);
		}
	}
	/* Clear interrupts */
	writel(int_mask, reg_base + CI_USBSTS);
	return IRQ_HANDLED;
}

static irqreturn_t otg_irq(int irq, void *_dev)
{
	struct	langwell_otg *langwell = _dev;
	u32	int_sts, int_en;
	u32	int_mask = 0;
	int	flag = 0;

	int_sts = readl(langwell->regs + CI_OTGSC);
	int_en = (int_sts & OTGSC_INTEN_MASK) >> 8;
	int_mask = int_sts & int_en;
	if (int_mask == 0)
		return IRQ_NONE;

	if (int_mask & OTGSC_IDIS) {
		otg_dbg("%s: id change int\n", __func__);
		langwell->hsm.id = (int_sts & OTGSC_ID) ? 1 : 0;
		flag = 1;
	}
	if (int_mask & OTGSC_DPIS) {
		otg_dbg("%s: data pulse int\n", __func__);
		langwell->hsm.a_srp_det = (int_sts & OTGSC_DPS) ? 1 : 0;
		flag = 1;
	}
	if (int_mask & OTGSC_BSEIS) {
		otg_dbg("%s: b session end int\n", __func__);
		langwell->hsm.b_sess_end = (int_sts & OTGSC_BSE) ? 1 : 0;
		flag = 1;
	}
	if (int_mask & OTGSC_BSVIS) {
		otg_dbg("%s: b session valid int\n", __func__);
		langwell->hsm.b_sess_vld = (int_sts & OTGSC_BSV) ? 1 : 0;
		flag = 1;
	}
	if (int_mask & OTGSC_ASVIS) {
		otg_dbg("%s: a session valid int\n", __func__);
		langwell->hsm.a_sess_vld = (int_sts & OTGSC_ASV) ? 1 : 0;
		flag = 1;
	}
	if (int_mask & OTGSC_AVVIS) {
		otg_dbg("%s: a vbus valid int\n", __func__);
		langwell->hsm.a_vbus_vld = (int_sts & OTGSC_AVV) ? 1 : 0;
		flag = 1;
	}

	if (int_mask & OTGSC_1MSS) {
		/* need to schedule otg_work if any timer is expired */
		if (langwell_otg_tick_timer(&int_sts))
			flag = 1;
	}

	writel((int_sts & ~OTGSC_INTSTS_MASK) | int_mask,
			langwell->regs + CI_OTGSC);
	if (flag)
		queue_work(langwell->qwork, &langwell->work);

	return IRQ_HANDLED;
}

static void langwell_otg_work(struct work_struct *work)
{
	struct langwell_otg *langwell = container_of(work,
					struct langwell_otg, work);
	int	retval;

	otg_dbg("%s: old state = %s\n", __func__,
			state_string(langwell->otg.state));

	switch (langwell->otg.state) {
	case OTG_STATE_UNDEFINED:
	case OTG_STATE_B_IDLE:
		if (!langwell->hsm.id) {
			langwell_otg_del_timer(b_srp_res_tmr);
			langwell->otg.default_a = 1;
			langwell->hsm.a_srp_det = 0;

			langwell_otg_chrg_vbus(0);
			langwell_otg_drv_vbus(0);

			set_host_mode();
			langwell->otg.state = OTG_STATE_A_IDLE;
			queue_work(langwell->qwork, &langwell->work);
		} else if (langwell->hsm.b_srp_res_tmout) {
			langwell->hsm.b_srp_res_tmout = 0;
			langwell->hsm.b_bus_req = 0;
			langwell_otg_nsf_msg(6);
		} else if (langwell->hsm.b_sess_vld) {
			langwell_otg_del_timer(b_srp_res_tmr);
			langwell->hsm.b_sess_end = 0;
			langwell->hsm.a_bus_suspend = 0;

			langwell_otg_chrg_vbus(0);
			if (langwell->client_ops) {
				langwell->client_ops->resume(langwell->pdev);
				langwell->otg.state = OTG_STATE_B_PERIPHERAL;
			} else
				otg_dbg("client driver not loaded.\n");

		} else if (langwell->hsm.b_bus_req &&
				(langwell->hsm.b_sess_end)) {
			/* workaround for b_se0_srp detection */
			retval = langwell_otg_check_se0_srp(0);
			if (retval) {
				langwell->hsm.b_bus_req = 0;
				otg_dbg("LS is not SE0, try again later\n");
			} else {
				/* Start SRP */
				langwell_otg_start_srp(&langwell->otg);
				langwell_otg_add_timer(b_srp_res_tmr);
			}
		}
		break;
	case OTG_STATE_B_SRP_INIT:
		if (!langwell->hsm.id) {
			langwell->otg.default_a = 1;
			langwell->hsm.a_srp_det = 0;

			langwell_otg_drv_vbus(0);
			langwell_otg_chrg_vbus(0);

			langwell->otg.state = OTG_STATE_A_IDLE;
			queue_work(langwell->qwork, &langwell->work);
		} else if (langwell->hsm.b_sess_vld) {
			langwell_otg_chrg_vbus(0);
			if (langwell->client_ops) {
				langwell->client_ops->resume(langwell->pdev);
				langwell->otg.state = OTG_STATE_B_PERIPHERAL;
			} else
				otg_dbg("client driver not loaded.\n");
		}
		break;
	case OTG_STATE_B_PERIPHERAL:
		if (!langwell->hsm.id) {
			langwell->otg.default_a = 1;
			langwell->hsm.a_srp_det = 0;

			langwell_otg_drv_vbus(0);
			langwell_otg_chrg_vbus(0);
			set_host_mode();

			if (langwell->client_ops) {
				langwell->client_ops->suspend(langwell->pdev,
					PMSG_FREEZE);
			} else
				otg_dbg("client driver has been removed.\n");

			langwell->otg.state = OTG_STATE_A_IDLE;
			queue_work(langwell->qwork, &langwell->work);
		} else if (!langwell->hsm.b_sess_vld) {
			langwell->hsm.b_hnp_enable = 0;

			if (langwell->client_ops) {
				langwell->client_ops->suspend(langwell->pdev,
					PMSG_FREEZE);
			} else
				otg_dbg("client driver has been removed.\n");

			langwell->otg.state = OTG_STATE_B_IDLE;
		} else if (langwell->hsm.b_bus_req && langwell->hsm.b_hnp_enable
			&& langwell->hsm.a_bus_suspend) {

			if (langwell->client_ops) {
				langwell->client_ops->suspend(langwell->pdev,
					PMSG_FREEZE);
			} else
				otg_dbg("client driver has been removed.\n");

			langwell_otg_HAAR(1);
			langwell->hsm.a_conn = 0;

			if (langwell->host_ops) {
				langwell->host_ops->probe(langwell->pdev,
					langwell->host_ops->id_table);
				langwell->otg.state = OTG_STATE_B_WAIT_ACON;
			} else
				otg_dbg("host driver not loaded.\n");

			langwell->hsm.a_bus_resume = 0;
			langwell->hsm.b_ase0_brst_tmout = 0;
			langwell_otg_add_timer(b_ase0_brst_tmr);
		}
		break;

	case OTG_STATE_B_WAIT_ACON:
		if (!langwell->hsm.id) {
			langwell_otg_del_timer(b_ase0_brst_tmr);
			langwell->otg.default_a = 1;
			langwell->hsm.a_srp_det = 0;

			langwell_otg_drv_vbus(0);
			langwell_otg_chrg_vbus(0);
			set_host_mode();

			langwell_otg_HAAR(0);
			if (langwell->host_ops)
				langwell->host_ops->remove(langwell->pdev);
			else
				otg_dbg("host driver has been removed.\n");
			langwell->otg.state = OTG_STATE_A_IDLE;
			queue_work(langwell->qwork, &langwell->work);
		} else if (!langwell->hsm.b_sess_vld) {
			langwell_otg_del_timer(b_ase0_brst_tmr);
			langwell->hsm.b_hnp_enable = 0;
			langwell->hsm.b_bus_req = 0;
			langwell_otg_chrg_vbus(0);
			langwell_otg_HAAR(0);

			if (langwell->host_ops)
				langwell->host_ops->remove(langwell->pdev);
			else
				otg_dbg("host driver has been removed.\n");
			langwell->otg.state = OTG_STATE_B_IDLE;
		} else if (langwell->hsm.a_conn) {
			langwell_otg_del_timer(b_ase0_brst_tmr);
			langwell_otg_HAAR(0);
			langwell->otg.state = OTG_STATE_B_HOST;
			queue_work(langwell->qwork, &langwell->work);
		} else if (langwell->hsm.a_bus_resume ||
				langwell->hsm.b_ase0_brst_tmout) {
			langwell_otg_del_timer(b_ase0_brst_tmr);
			langwell_otg_HAAR(0);
			langwell_otg_nsf_msg(7);

			if (langwell->host_ops)
				langwell->host_ops->remove(langwell->pdev);
			else
				otg_dbg("host driver has been removed.\n");

			langwell->hsm.a_bus_suspend = 0;
			langwell->hsm.b_bus_req = 0;

			if (langwell->client_ops)
				langwell->client_ops->resume(langwell->pdev);
			else
				otg_dbg("client driver not loaded.\n");

			langwell->otg.state = OTG_STATE_B_PERIPHERAL;
		}
		break;

	case OTG_STATE_B_HOST:
		if (!langwell->hsm.id) {
			langwell->otg.default_a = 1;
			langwell->hsm.a_srp_det = 0;

			langwell_otg_drv_vbus(0);
			langwell_otg_chrg_vbus(0);
			set_host_mode();
			if (langwell->host_ops)
				langwell->host_ops->remove(langwell->pdev);
			else
				otg_dbg("host driver has been removed.\n");
			langwell->otg.state = OTG_STATE_A_IDLE;
			queue_work(langwell->qwork, &langwell->work);
		} else if (!langwell->hsm.b_sess_vld) {
			langwell->hsm.b_hnp_enable = 0;
			langwell->hsm.b_bus_req = 0;
			langwell_otg_chrg_vbus(0);
			if (langwell->host_ops)
				langwell->host_ops->remove(langwell->pdev);
			else
				otg_dbg("host driver has been removed.\n");
			langwell->otg.state = OTG_STATE_B_IDLE;
		} else if ((!langwell->hsm.b_bus_req) ||
				(!langwell->hsm.a_conn)) {
			langwell->hsm.b_bus_req = 0;
			langwell_otg_loc_sof(0);
			if (langwell->host_ops)
				langwell->host_ops->remove(langwell->pdev);
			else
				otg_dbg("host driver has been removed.\n");

			langwell->hsm.a_bus_suspend = 0;

			if (langwell->client_ops)
				langwell->client_ops->resume(langwell->pdev);
			else
				otg_dbg("client driver not loaded.\n");

			langwell->otg.state = OTG_STATE_B_PERIPHERAL;
		}
		break;

	case OTG_STATE_A_IDLE:
		langwell->otg.default_a = 1;
		if (langwell->hsm.id) {
			langwell->otg.default_a = 0;
			langwell->hsm.b_bus_req = 0;
			langwell_otg_drv_vbus(0);
			langwell_otg_chrg_vbus(0);

			langwell->otg.state = OTG_STATE_B_IDLE;
			queue_work(langwell->qwork, &langwell->work);
		} else if (langwell->hsm.a_sess_vld) {
			langwell_otg_drv_vbus(1);
			langwell->hsm.a_srp_det = 1;
			langwell->hsm.a_wait_vrise_tmout = 0;
			langwell_otg_add_timer(a_wait_vrise_tmr);
			langwell->otg.state = OTG_STATE_A_WAIT_VRISE;
			queue_work(langwell->qwork, &langwell->work);
		} else if (!langwell->hsm.a_bus_drop &&
			(langwell->hsm.a_srp_det || langwell->hsm.a_bus_req)) {
			langwell_otg_drv_vbus(1);
			langwell->hsm.a_wait_vrise_tmout = 0;
			langwell_otg_add_timer(a_wait_vrise_tmr);
			langwell->otg.state = OTG_STATE_A_WAIT_VRISE;
			queue_work(langwell->qwork, &langwell->work);
		}
		break;
	case OTG_STATE_A_WAIT_VRISE:
		if (langwell->hsm.id) {
			langwell_otg_del_timer(a_wait_vrise_tmr);
			langwell->hsm.b_bus_req = 0;
			langwell->otg.default_a = 0;
			langwell_otg_drv_vbus(0);
			langwell->otg.state = OTG_STATE_B_IDLE;
		} else if (langwell->hsm.a_vbus_vld) {
			langwell_otg_del_timer(a_wait_vrise_tmr);
			if (langwell->host_ops)
				langwell->host_ops->probe(langwell->pdev,
						langwell->host_ops->id_table);
			else
				otg_dbg("host driver not loaded.\n");
			langwell->hsm.b_conn = 0;
			langwell->hsm.a_set_b_hnp_en = 0;
			langwell->hsm.a_wait_bcon_tmout = 0;
			langwell_otg_add_timer(a_wait_bcon_tmr);
			langwell->otg.state = OTG_STATE_A_WAIT_BCON;
		} else if (langwell->hsm.a_wait_vrise_tmout) {
			if (langwell->hsm.a_vbus_vld) {
				if (langwell->host_ops)
					langwell->host_ops->probe(
						langwell->pdev,
						langwell->host_ops->id_table);
				else
					otg_dbg("host driver not loaded.\n");
				langwell->hsm.b_conn = 0;
				langwell->hsm.a_set_b_hnp_en = 0;
				langwell->hsm.a_wait_bcon_tmout = 0;
				langwell_otg_add_timer(a_wait_bcon_tmr);
				langwell->otg.state = OTG_STATE_A_WAIT_BCON;
			} else {
				langwell_otg_drv_vbus(0);
				langwell->otg.state = OTG_STATE_A_VBUS_ERR;
			}
		}
		break;
	case OTG_STATE_A_WAIT_BCON:
		if (langwell->hsm.id) {
			langwell_otg_del_timer(a_wait_bcon_tmr);

			langwell->otg.default_a = 0;
			langwell->hsm.b_bus_req = 0;
			if (langwell->host_ops)
				langwell->host_ops->remove(langwell->pdev);
			else
				otg_dbg("host driver has been removed.\n");
			langwell_otg_drv_vbus(0);
			langwell->otg.state = OTG_STATE_B_IDLE;
			queue_work(langwell->qwork, &langwell->work);
		} else if (!langwell->hsm.a_vbus_vld) {
			langwell_otg_del_timer(a_wait_bcon_tmr);

			if (langwell->host_ops)
				langwell->host_ops->remove(langwell->pdev);
			else
				otg_dbg("host driver has been removed.\n");
			langwell_otg_drv_vbus(0);
			langwell->otg.state = OTG_STATE_A_VBUS_ERR;
		} else if (langwell->hsm.a_bus_drop ||
				(langwell->hsm.a_wait_bcon_tmout &&
				!langwell->hsm.a_bus_req)) {
			langwell_otg_del_timer(a_wait_bcon_tmr);

			if (langwell->host_ops)
				langwell->host_ops->remove(langwell->pdev);
			else
				otg_dbg("host driver has been removed.\n");
			langwell_otg_drv_vbus(0);
			langwell->otg.state = OTG_STATE_A_WAIT_VFALL;
		} else if (langwell->hsm.b_conn) {
			langwell_otg_del_timer(a_wait_bcon_tmr);

			langwell->hsm.a_suspend_req = 0;
			langwell->otg.state = OTG_STATE_A_HOST;
			if (!langwell->hsm.a_bus_req &&
				langwell->hsm.a_set_b_hnp_en) {
				/* It is not safe enough to do a fast
				 * transistion from A_WAIT_BCON to
				 * A_SUSPEND */
				msleep(10000);
				if (langwell->hsm.a_bus_req)
					break;

				if (request_irq(langwell->pdev->irq,
					otg_dummy_irq, IRQF_SHARED,
					driver_name, langwell->regs) != 0) {
					otg_dbg("request interrupt %d fail\n",
					langwell->pdev->irq);
				}

				langwell_otg_HABA(1);
				langwell->hsm.b_bus_resume = 0;
				langwell->hsm.a_aidl_bdis_tmout = 0;
				langwell_otg_add_timer(a_aidl_bdis_tmr);

				langwell_otg_loc_sof(0);
				langwell->otg.state = OTG_STATE_A_SUSPEND;
			} else if (!langwell->hsm.a_bus_req &&
				!langwell->hsm.a_set_b_hnp_en) {
				struct pci_dev *pdev = langwell->pdev;
				if (langwell->host_ops)
					langwell->host_ops->remove(pdev);
				else
					otg_dbg("host driver removed.\n");
				langwell_otg_drv_vbus(0);
				langwell->otg.state = OTG_STATE_A_WAIT_VFALL;
			}
		}
		break;
	case OTG_STATE_A_HOST:
		if (langwell->hsm.id) {
			langwell->otg.default_a = 0;
			langwell->hsm.b_bus_req = 0;
			if (langwell->host_ops)
				langwell->host_ops->remove(langwell->pdev);
			else
				otg_dbg("host driver has been removed.\n");
			langwell_otg_drv_vbus(0);
			langwell->otg.state = OTG_STATE_B_IDLE;
			queue_work(langwell->qwork, &langwell->work);
		} else if (langwell->hsm.a_bus_drop ||
		(!langwell->hsm.a_set_b_hnp_en && !langwell->hsm.a_bus_req)) {
			if (langwell->host_ops)
				langwell->host_ops->remove(langwell->pdev);
			else
				otg_dbg("host driver has been removed.\n");
			langwell_otg_drv_vbus(0);
			langwell->otg.state = OTG_STATE_A_WAIT_VFALL;
		} else if (!langwell->hsm.a_vbus_vld) {
			if (langwell->host_ops)
				langwell->host_ops->remove(langwell->pdev);
			else
				otg_dbg("host driver has been removed.\n");
			langwell_otg_drv_vbus(0);
			langwell->otg.state = OTG_STATE_A_VBUS_ERR;
		} else if (langwell->hsm.a_set_b_hnp_en
				&& !langwell->hsm.a_bus_req) {
			/* Set HABA to enable hardware assistance to signal
			 *  A-connect after receiver B-disconnect. Hardware
			 *  will then set client mode and enable URE, SLE and
			 *  PCE after the assistance. otg_dummy_irq is used to
			 *  clean these ints when client driver is not resumed.
			 */
			if (request_irq(langwell->pdev->irq,
				otg_dummy_irq, IRQF_SHARED, driver_name,
				langwell->regs) != 0) {
				otg_dbg("request interrupt %d failed\n",
						langwell->pdev->irq);
			}

			/* set HABA */
			langwell_otg_HABA(1);
			langwell->hsm.b_bus_resume = 0;
			langwell->hsm.a_aidl_bdis_tmout = 0;
			langwell_otg_add_timer(a_aidl_bdis_tmr);
			langwell_otg_loc_sof(0);
			langwell->otg.state = OTG_STATE_A_SUSPEND;
		} else if (!langwell->hsm.b_conn || !langwell->hsm.a_bus_req) {
			langwell->hsm.a_wait_bcon_tmout = 0;
			langwell->hsm.a_set_b_hnp_en = 0;
			langwell_otg_add_timer(a_wait_bcon_tmr);
			langwell->otg.state = OTG_STATE_A_WAIT_BCON;
		}
		break;
	case OTG_STATE_A_SUSPEND:
		if (langwell->hsm.id) {
			langwell_otg_del_timer(a_aidl_bdis_tmr);
			langwell_otg_HABA(0);
			free_irq(langwell->pdev->irq, langwell->regs);
			langwell->otg.default_a = 0;
			langwell->hsm.b_bus_req = 0;
			if (langwell->host_ops)
				langwell->host_ops->remove(langwell->pdev);
			else
				otg_dbg("host driver has been removed.\n");
			langwell_otg_drv_vbus(0);
			langwell->otg.state = OTG_STATE_B_IDLE;
			queue_work(langwell->qwork, &langwell->work);
		} else if (langwell->hsm.a_bus_req ||
				langwell->hsm.b_bus_resume) {
			langwell_otg_del_timer(a_aidl_bdis_tmr);
			langwell_otg_HABA(0);
			free_irq(langwell->pdev->irq, langwell->regs);
			langwell->hsm.a_suspend_req = 0;
			langwell_otg_loc_sof(1);
			langwell->otg.state = OTG_STATE_A_HOST;
		} else if (langwell->hsm.a_aidl_bdis_tmout ||
				langwell->hsm.a_bus_drop) {
			langwell_otg_del_timer(a_aidl_bdis_tmr);
			langwell_otg_HABA(0);
			free_irq(langwell->pdev->irq, langwell->regs);
			if (langwell->host_ops)
				langwell->host_ops->remove(langwell->pdev);
			else
				otg_dbg("host driver has been removed.\n");
			langwell_otg_drv_vbus(0);
			langwell->otg.state = OTG_STATE_A_WAIT_VFALL;
		} else if (!langwell->hsm.b_conn &&
				langwell->hsm.a_set_b_hnp_en) {
			langwell_otg_del_timer(a_aidl_bdis_tmr);
			langwell_otg_HABA(0);
			free_irq(langwell->pdev->irq, langwell->regs);

			if (langwell->host_ops)
				langwell->host_ops->remove(langwell->pdev);
			else
				otg_dbg("host driver has been removed.\n");

			langwell->hsm.b_bus_suspend = 0;
			langwell->hsm.b_bus_suspend_vld = 0;
			langwell->hsm.b_bus_suspend_tmout = 0;

			/* msleep(200); */
			if (langwell->client_ops)
				langwell->client_ops->resume(langwell->pdev);
			else
				otg_dbg("client driver not loaded.\n");

			langwell_otg_add_timer(b_bus_suspend_tmr);
			langwell->otg.state = OTG_STATE_A_PERIPHERAL;
			break;
		} else if (!langwell->hsm.a_vbus_vld) {
			langwell_otg_del_timer(a_aidl_bdis_tmr);
			langwell_otg_HABA(0);
			free_irq(langwell->pdev->irq, langwell->regs);
			if (langwell->host_ops)
				langwell->host_ops->remove(langwell->pdev);
			else
				otg_dbg("host driver has been removed.\n");
			langwell_otg_drv_vbus(0);
			langwell->otg.state = OTG_STATE_A_VBUS_ERR;
		}
		break;
	case OTG_STATE_A_PERIPHERAL:
		if (langwell->hsm.id) {
			langwell_otg_del_timer(b_bus_suspend_tmr);
			langwell->otg.default_a = 0;
			langwell->hsm.b_bus_req = 0;
			if (langwell->client_ops)
				langwell->client_ops->suspend(langwell->pdev,
					PMSG_FREEZE);
			else
				otg_dbg("client driver has been removed.\n");
			langwell_otg_drv_vbus(0);
			langwell->otg.state = OTG_STATE_B_IDLE;
			queue_work(langwell->qwork, &langwell->work);
		} else if (!langwell->hsm.a_vbus_vld) {
			langwell_otg_del_timer(b_bus_suspend_tmr);
			if (langwell->client_ops)
				langwell->client_ops->suspend(langwell->pdev,
					PMSG_FREEZE);
			else
				otg_dbg("client driver has been removed.\n");
			langwell_otg_drv_vbus(0);
			langwell->otg.state = OTG_STATE_A_VBUS_ERR;
		} else if (langwell->hsm.a_bus_drop) {
			langwell_otg_del_timer(b_bus_suspend_tmr);
			if (langwell->client_ops)
				langwell->client_ops->suspend(langwell->pdev,
					PMSG_FREEZE);
			else
				otg_dbg("client driver has been removed.\n");
			langwell_otg_drv_vbus(0);
			langwell->otg.state = OTG_STATE_A_WAIT_VFALL;
		} else if (langwell->hsm.b_bus_suspend) {
			langwell_otg_del_timer(b_bus_suspend_tmr);
			if (langwell->client_ops)
				langwell->client_ops->suspend(langwell->pdev,
					PMSG_FREEZE);
			else
				otg_dbg("client driver has been removed.\n");

			if (langwell->host_ops)
				langwell->host_ops->probe(langwell->pdev,
						langwell->host_ops->id_table);
			else
				otg_dbg("host driver not loaded.\n");
			langwell->hsm.a_set_b_hnp_en = 0;
			langwell->hsm.a_wait_bcon_tmout = 0;
			langwell_otg_add_timer(a_wait_bcon_tmr);
			langwell->otg.state = OTG_STATE_A_WAIT_BCON;
		} else if (langwell->hsm.b_bus_suspend_tmout) {
			u32	val;
			val = readl(langwell->regs + CI_PORTSC1);
			if (!(val & PORTSC_SUSP))
				break;
			if (langwell->client_ops)
				langwell->client_ops->suspend(langwell->pdev,
						PMSG_FREEZE);
			else
				otg_dbg("client driver has been removed.\n");
			if (langwell->host_ops)
				langwell->host_ops->probe(langwell->pdev,
						langwell->host_ops->id_table);
			else
				otg_dbg("host driver not loaded.\n");
			langwell->hsm.a_set_b_hnp_en = 0;
			langwell->hsm.a_wait_bcon_tmout = 0;
			langwell_otg_add_timer(a_wait_bcon_tmr);
			langwell->otg.state = OTG_STATE_A_WAIT_BCON;
		}
		break;
	case OTG_STATE_A_VBUS_ERR:
		if (langwell->hsm.id) {
			langwell->otg.default_a = 0;
			langwell->hsm.a_clr_err = 0;
			langwell->hsm.a_srp_det = 0;
			langwell->otg.state = OTG_STATE_B_IDLE;
			queue_work(langwell->qwork, &langwell->work);
		} else if (langwell->hsm.a_clr_err) {
			langwell->hsm.a_clr_err = 0;
			langwell->hsm.a_srp_det = 0;
			reset_otg();
			init_hsm();
			if (langwell->otg.state == OTG_STATE_A_IDLE)
				queue_work(langwell->qwork, &langwell->work);
		}
		break;
	case OTG_STATE_A_WAIT_VFALL:
		if (langwell->hsm.id) {
			langwell->otg.default_a = 0;
			langwell->otg.state = OTG_STATE_B_IDLE;
			queue_work(langwell->qwork, &langwell->work);
		} else if (langwell->hsm.a_bus_req) {
			langwell_otg_drv_vbus(1);
			langwell->hsm.a_wait_vrise_tmout = 0;
			langwell_otg_add_timer(a_wait_vrise_tmr);
			langwell->otg.state = OTG_STATE_A_WAIT_VRISE;
		} else if (!langwell->hsm.a_sess_vld) {
			langwell->hsm.a_srp_det = 0;
			langwell_otg_drv_vbus(0);
			set_host_mode();
			langwell->otg.state = OTG_STATE_A_IDLE;
		}
		break;
	default:
		;
	}

	otg_dbg("%s: new state = %s\n", __func__,
			state_string(langwell->otg.state));
}

	static ssize_t
show_registers(struct device *_dev, struct device_attribute *attr, char *buf)
{
	struct langwell_otg *langwell;
	char *next;
	unsigned size;
	unsigned t;

	langwell = the_transceiver;
	next = buf;
	size = PAGE_SIZE;

	t = scnprintf(next, size,
		"\n"
		"USBCMD = 0x%08x \n"
		"USBSTS = 0x%08x \n"
		"USBINTR = 0x%08x \n"
		"ASYNCLISTADDR = 0x%08x \n"
		"PORTSC1 = 0x%08x \n"
		"HOSTPC1 = 0x%08x \n"
		"OTGSC = 0x%08x \n"
		"USBMODE = 0x%08x \n",
		readl(langwell->regs + 0x30),
		readl(langwell->regs + 0x34),
		readl(langwell->regs + 0x38),
		readl(langwell->regs + 0x48),
		readl(langwell->regs + 0x74),
		readl(langwell->regs + 0xb4),
		readl(langwell->regs + 0xf4),
		readl(langwell->regs + 0xf8)
		);
	size -= t;
	next += t;

	return PAGE_SIZE - size;
}
static DEVICE_ATTR(registers, S_IRUGO, show_registers, NULL);

static ssize_t
show_hsm(struct device *_dev, struct device_attribute *attr, char *buf)
{
	struct langwell_otg *langwell;
	char *next;
	unsigned size;
	unsigned t;

	langwell = the_transceiver;
	next = buf;
	size = PAGE_SIZE;

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
		state_string(langwell->otg.state),
		langwell->hsm.a_bus_resume,
		langwell->hsm.a_bus_suspend,
		langwell->hsm.a_conn,
		langwell->hsm.a_sess_vld,
		langwell->hsm.a_srp_det,
		langwell->hsm.a_vbus_vld,
		langwell->hsm.b_bus_resume,
		langwell->hsm.b_bus_suspend,
		langwell->hsm.b_conn,
		langwell->hsm.b_se0_srp,
		langwell->hsm.b_sess_end,
		langwell->hsm.b_sess_vld,
		langwell->hsm.id,
		langwell->hsm.a_set_b_hnp_en,
		langwell->hsm.b_srp_done,
		langwell->hsm.b_hnp_enable,
		langwell->hsm.a_wait_vrise_tmout,
		langwell->hsm.a_wait_bcon_tmout,
		langwell->hsm.a_aidl_bdis_tmout,
		langwell->hsm.b_ase0_brst_tmout,
		langwell->hsm.a_bus_drop,
		langwell->hsm.a_bus_req,
		langwell->hsm.a_clr_err,
		langwell->hsm.a_suspend_req,
		langwell->hsm.b_bus_req,
		langwell->hsm.b_bus_suspend_tmout,
		langwell->hsm.b_bus_suspend_vld
		);
	size -= t;
	next += t;

	return PAGE_SIZE - size;
}
static DEVICE_ATTR(hsm, S_IRUGO, show_hsm, NULL);

static ssize_t
get_a_bus_req(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct langwell_otg *langwell;
	char *next;
	unsigned size;
	unsigned t;

	langwell =  the_transceiver;
	next = buf;
	size = PAGE_SIZE;

	t = scnprintf(next, size, "%d", langwell->hsm.a_bus_req);
	size -= t;
	next += t;

	return PAGE_SIZE - size;
}

static ssize_t
set_a_bus_req(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct langwell_otg *langwell;
	langwell = the_transceiver;
	if (!langwell->otg.default_a)
		return -1;
	if (count > 2)
		return -1;

	if (buf[0] == '0') {
		langwell->hsm.a_bus_req = 0;
		otg_dbg("a_bus_req = 0\n");
	} else if (buf[0] == '1') {
		/* If a_bus_drop is TRUE, a_bus_req can't be set */
		if (langwell->hsm.a_bus_drop)
			return -1;
		langwell->hsm.a_bus_req = 1;
		otg_dbg("a_bus_req = 1\n");
	}
	if (spin_trylock(&langwell->wq_lock)) {
		queue_work(langwell->qwork, &langwell->work);
		spin_unlock(&langwell->wq_lock);
	}
	return count;
}
static DEVICE_ATTR(a_bus_req, S_IRUGO | S_IWUGO, get_a_bus_req, set_a_bus_req);

static ssize_t
get_a_bus_drop(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct langwell_otg *langwell;
	char *next;
	unsigned size;
	unsigned t;

	langwell =  the_transceiver;
	next = buf;
	size = PAGE_SIZE;

	t = scnprintf(next, size, "%d", langwell->hsm.a_bus_drop);
	size -= t;
	next += t;

	return PAGE_SIZE - size;
}

static ssize_t
set_a_bus_drop(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct langwell_otg *langwell;
	langwell = the_transceiver;
	if (!langwell->otg.default_a)
		return -1;
	if (count > 2)
		return -1;

	if (buf[0] == '0') {
		langwell->hsm.a_bus_drop = 0;
		otg_dbg("a_bus_drop = 0\n");
	} else if (buf[0] == '1') {
		langwell->hsm.a_bus_drop = 1;
		langwell->hsm.a_bus_req = 0;
		otg_dbg("a_bus_drop = 1, then a_bus_req = 0\n");
	}
	if (spin_trylock(&langwell->wq_lock)) {
		queue_work(langwell->qwork, &langwell->work);
		spin_unlock(&langwell->wq_lock);
	}
	return count;
}
static DEVICE_ATTR(a_bus_drop, S_IRUGO | S_IWUGO,
	get_a_bus_drop, set_a_bus_drop);

static ssize_t
get_b_bus_req(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct langwell_otg *langwell;
	char *next;
	unsigned size;
	unsigned t;

	langwell =  the_transceiver;
	next = buf;
	size = PAGE_SIZE;

	t = scnprintf(next, size, "%d", langwell->hsm.b_bus_req);
	size -= t;
	next += t;

	return PAGE_SIZE - size;
}

static ssize_t
set_b_bus_req(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct langwell_otg *langwell;
	langwell = the_transceiver;

	if (langwell->otg.default_a)
		return -1;

	if (count > 2)
		return -1;

	if (buf[0] == '0') {
		langwell->hsm.b_bus_req = 0;
		otg_dbg("b_bus_req = 0\n");
	} else if (buf[0] == '1') {
		langwell->hsm.b_bus_req = 1;
		otg_dbg("b_bus_req = 1\n");
	}
	if (spin_trylock(&langwell->wq_lock)) {
		queue_work(langwell->qwork, &langwell->work);
		spin_unlock(&langwell->wq_lock);
	}
	return count;
}
static DEVICE_ATTR(b_bus_req, S_IRUGO | S_IWUGO, get_b_bus_req, set_b_bus_req);

static ssize_t
set_a_clr_err(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct langwell_otg *langwell;
	langwell = the_transceiver;

	if (!langwell->otg.default_a)
		return -1;
	if (count > 2)
		return -1;

	if (buf[0] == '1') {
		langwell->hsm.a_clr_err = 1;
		otg_dbg("a_clr_err = 1\n");
	}
	if (spin_trylock(&langwell->wq_lock)) {
		queue_work(langwell->qwork, &langwell->work);
		spin_unlock(&langwell->wq_lock);
	}
	return count;
}
static DEVICE_ATTR(a_clr_err, S_IWUGO, NULL, set_a_clr_err);

static struct attribute *inputs_attrs[] = {
	&dev_attr_a_bus_req.attr,
	&dev_attr_a_bus_drop.attr,
	&dev_attr_b_bus_req.attr,
	&dev_attr_a_clr_err.attr,
	NULL,
};

static struct attribute_group debug_dev_attr_group = {
	.name = "inputs",
	.attrs = inputs_attrs,
};

int langwell_register_host(struct pci_driver *host_driver)
{
	int	ret = 0;

	the_transceiver->host_ops = host_driver;
	queue_work(the_transceiver->qwork, &the_transceiver->work);
	otg_dbg("host controller driver is registered\n");

	return ret;
}
EXPORT_SYMBOL(langwell_register_host);

void langwell_unregister_host(struct pci_driver *host_driver)
{
	if (the_transceiver->host_ops)
		the_transceiver->host_ops->remove(the_transceiver->pdev);
	the_transceiver->host_ops = NULL;
	the_transceiver->hsm.a_bus_drop = 1;
	queue_work(the_transceiver->qwork, &the_transceiver->work);
	otg_dbg("host controller driver is unregistered\n");
}
EXPORT_SYMBOL(langwell_unregister_host);

int langwell_register_peripheral(struct pci_driver *client_driver)
{
	int	ret = 0;

	if (client_driver)
		ret = client_driver->probe(the_transceiver->pdev,
				client_driver->id_table);
	if (!ret) {
		the_transceiver->client_ops = client_driver;
		queue_work(the_transceiver->qwork, &the_transceiver->work);
		otg_dbg("client controller driver is registered\n");
	}

	return ret;
}
EXPORT_SYMBOL(langwell_register_peripheral);

void langwell_unregister_peripheral(struct pci_driver *client_driver)
{
	if (the_transceiver->client_ops)
		the_transceiver->client_ops->remove(the_transceiver->pdev);
	the_transceiver->client_ops = NULL;
	the_transceiver->hsm.b_bus_req = 0;
	queue_work(the_transceiver->qwork, &the_transceiver->work);
	otg_dbg("client controller driver is unregistered\n");
}
EXPORT_SYMBOL(langwell_unregister_peripheral);

static int langwell_otg_probe(struct pci_dev *pdev,
		const struct pci_device_id *id)
{
	unsigned long		resource, len;
	void __iomem 		*base = NULL;
	int			retval;
	u32			val32;
	struct langwell_otg	*langwell;
	char			qname[] = "langwell_otg_queue";

	retval = 0;
	otg_dbg("\notg controller is detected.\n");
	if (pci_enable_device(pdev) < 0) {
		retval = -ENODEV;
		goto done;
	}

	langwell = kzalloc(sizeof *langwell, GFP_KERNEL);
	if (langwell == NULL) {
		retval = -ENOMEM;
		goto done;
	}
	the_transceiver = langwell;

	/* control register: BAR 0 */
	resource = pci_resource_start(pdev, 0);
	len = pci_resource_len(pdev, 0);
	if (!request_mem_region(resource, len, driver_name)) {
		retval = -EBUSY;
		goto err;
	}
	langwell->region = 1;

	base = ioremap_nocache(resource, len);
	if (base == NULL) {
		retval = -EFAULT;
		goto err;
	}
	langwell->regs = base;

	if (!pdev->irq) {
		otg_dbg("No IRQ.\n");
		retval = -ENODEV;
		goto err;
	}

	langwell->qwork = create_workqueue(qname);
	if (!langwell->qwork) {
		otg_dbg("cannot create workqueue %s\n", qname);
		retval = -ENOMEM;
		goto err;
	}
	INIT_WORK(&langwell->work, langwell_otg_work);

	/* OTG common part */
	langwell->pdev = pdev;
	langwell->otg.dev = &pdev->dev;
	langwell->otg.label = driver_name;
	langwell->otg.set_host = langwell_otg_set_host;
	langwell->otg.set_peripheral = langwell_otg_set_peripheral;
	langwell->otg.set_power = langwell_otg_set_power;
	langwell->otg.start_srp = langwell_otg_start_srp;
	langwell->otg.state = OTG_STATE_UNDEFINED;
	if (otg_set_transceiver(&langwell->otg)) {
		otg_dbg("can't set transceiver\n");
		retval = -EBUSY;
		goto err;
	}

	reset_otg();
	init_hsm();

	spin_lock_init(&langwell->lock);
	spin_lock_init(&langwell->wq_lock);
	INIT_LIST_HEAD(&active_timers);
	langwell_otg_init_timers(&langwell->hsm);

	if (request_irq(pdev->irq, otg_irq, IRQF_SHARED,
				driver_name, langwell) != 0) {
		otg_dbg("request interrupt %d failed\n", pdev->irq);
		retval = -EBUSY;
		goto err;
	}

	/* enable OTGSC int */
	val32 = OTGSC_DPIE | OTGSC_BSEIE | OTGSC_BSVIE |
		OTGSC_ASVIE | OTGSC_AVVIE | OTGSC_IDIE | OTGSC_IDPU;
	writel(val32, langwell->regs + CI_OTGSC);

	retval = device_create_file(&pdev->dev, &dev_attr_registers);
	if (retval < 0) {
		otg_dbg("Can't register sysfs attribute: %d\n", retval);
		goto err;
	}

	retval = device_create_file(&pdev->dev, &dev_attr_hsm);
	if (retval < 0) {
		otg_dbg("Can't hsm sysfs attribute: %d\n", retval);
		goto err;
	}

	retval = sysfs_create_group(&pdev->dev.kobj, &debug_dev_attr_group);
	if (retval < 0) {
		otg_dbg("Can't register sysfs attr group: %d\n", retval);
		goto err;
	}

	if (langwell->otg.state == OTG_STATE_A_IDLE)
		queue_work(langwell->qwork, &langwell->work);

	return 0;

err:
	if (the_transceiver)
		langwell_otg_remove(pdev);
done:
	return retval;
}

static void langwell_otg_remove(struct pci_dev *pdev)
{
	struct langwell_otg *langwell;

	langwell = the_transceiver;

	if (langwell->qwork) {
		flush_workqueue(langwell->qwork);
		destroy_workqueue(langwell->qwork);
	}
	langwell_otg_free_timers();

	/* disable OTGSC interrupt as OTGSC doesn't change in reset */
	writel(0, langwell->regs + CI_OTGSC);

	if (pdev->irq)
		free_irq(pdev->irq, langwell);
	if (langwell->regs)
		iounmap(langwell->regs);
	if (langwell->region)
		release_mem_region(pci_resource_start(pdev, 0),
				pci_resource_len(pdev, 0));

	otg_set_transceiver(NULL);
	pci_disable_device(pdev);
	sysfs_remove_group(&pdev->dev.kobj, &debug_dev_attr_group);
	device_remove_file(&pdev->dev, &dev_attr_hsm);
	device_remove_file(&pdev->dev, &dev_attr_registers);
	kfree(langwell);
	langwell = NULL;
}

static void transceiver_suspend(struct pci_dev *pdev)
{
	pci_save_state(pdev);
	pci_set_power_state(pdev, PCI_D3hot);
	langwell_otg_phy_low_power(1);
}

static int langwell_otg_suspend(struct pci_dev *pdev, pm_message_t message)
{
	int 	ret = 0;
	struct langwell_otg *langwell;

	langwell = the_transceiver;

	/* Disbale OTG interrupts */
	langwell_otg_intr(0);

	if (pdev->irq)
		free_irq(pdev->irq, langwell);

	/* Prevent more otg_work */
	flush_workqueue(langwell->qwork);
	spin_lock(&langwell->wq_lock);

	/* start actions */
	switch (langwell->otg.state) {
	case OTG_STATE_A_IDLE:
	case OTG_STATE_B_IDLE:
	case OTG_STATE_A_WAIT_VFALL:
	case OTG_STATE_A_VBUS_ERR:
		transceiver_suspend(pdev);
		break;
	case OTG_STATE_A_WAIT_VRISE:
		langwell_otg_del_timer(a_wait_vrise_tmr);
		langwell->hsm.a_srp_det = 0;
		langwell_otg_drv_vbus(0);
		langwell->otg.state = OTG_STATE_A_IDLE;
		transceiver_suspend(pdev);
		break;
	case OTG_STATE_A_WAIT_BCON:
		langwell_otg_del_timer(a_wait_bcon_tmr);
		if (langwell->host_ops)
			ret = langwell->host_ops->suspend(pdev, message);
		langwell_otg_drv_vbus(0);
		break;
	case OTG_STATE_A_HOST:
		if (langwell->host_ops)
			ret = langwell->host_ops->suspend(pdev, message);
		langwell_otg_drv_vbus(0);
		langwell_otg_phy_low_power(1);
		break;
	case OTG_STATE_A_SUSPEND:
		langwell_otg_del_timer(a_aidl_bdis_tmr);
		langwell_otg_HABA(0);
		if (langwell->host_ops)
			langwell->host_ops->remove(pdev);
		else
			otg_dbg("host driver has been removed.\n");
		langwell_otg_drv_vbus(0);
		transceiver_suspend(pdev);
		langwell->otg.state = OTG_STATE_A_WAIT_VFALL;
		break;
	case OTG_STATE_A_PERIPHERAL:
		if (langwell->client_ops)
			ret = langwell->client_ops->suspend(pdev, message);
		else
			otg_dbg("client driver has been removed.\n");
		langwell_otg_drv_vbus(0);
		transceiver_suspend(pdev);
		langwell->otg.state = OTG_STATE_A_WAIT_VFALL;
		break;
	case OTG_STATE_B_HOST:
		if (langwell->host_ops)
			langwell->host_ops->remove(pdev);
		else
			otg_dbg("host driver has been removed.\n");
		langwell->hsm.b_bus_req = 0;
		transceiver_suspend(pdev);
		langwell->otg.state = OTG_STATE_B_IDLE;
		break;
	case OTG_STATE_B_PERIPHERAL:
		if (langwell->client_ops)
			ret = langwell->client_ops->suspend(pdev, message);
		else
			otg_dbg("client driver has been removed.\n");
		break;
	case OTG_STATE_B_WAIT_ACON:
		langwell_otg_del_timer(b_ase0_brst_tmr);
		langwell_otg_HAAR(0);
		if (langwell->host_ops)
			langwell->host_ops->remove(pdev);
		else
			otg_dbg("host driver has been removed.\n");
		langwell->hsm.b_bus_req = 0;
		langwell->otg.state = OTG_STATE_B_IDLE;
		transceiver_suspend(pdev);
		break;
	default:
		otg_dbg("error state before suspend\n ");
		break;
	}
	spin_unlock(&langwell->wq_lock);

	return ret;
}

static void transceiver_resume(struct pci_dev *pdev)
{
	pci_restore_state(pdev);
	pci_set_power_state(pdev, PCI_D0);
	langwell_otg_phy_low_power(0);
}

static int langwell_otg_resume(struct pci_dev *pdev)
{
	int 	ret = 0;
	struct langwell_otg *langwell;

	langwell = the_transceiver;

	spin_lock(&langwell->wq_lock);

	switch (langwell->otg.state) {
	case OTG_STATE_A_IDLE:
	case OTG_STATE_B_IDLE:
	case OTG_STATE_A_WAIT_VFALL:
	case OTG_STATE_A_VBUS_ERR:
		transceiver_resume(pdev);
		break;
	case OTG_STATE_A_WAIT_BCON:
		langwell_otg_add_timer(a_wait_bcon_tmr);
		langwell_otg_drv_vbus(1);
		if (langwell->host_ops)
			ret = langwell->host_ops->resume(pdev);
		break;
	case OTG_STATE_A_HOST:
		langwell_otg_drv_vbus(1);
		langwell_otg_phy_low_power(0);
		if (langwell->host_ops)
			ret = langwell->host_ops->resume(pdev);
		break;
	case OTG_STATE_B_PERIPHERAL:
		if (langwell->client_ops)
			ret = langwell->client_ops->resume(pdev);
		else
			otg_dbg("client driver not loaded.\n");
		break;
	default:
		otg_dbg("error state before suspend\n ");
		break;
	}

	if (request_irq(pdev->irq, otg_irq, IRQF_SHARED,
				driver_name, the_transceiver) != 0) {
		otg_dbg("request interrupt %d failed\n", pdev->irq);
		ret = -EBUSY;
	}

	/* enable OTG interrupts */
	langwell_otg_intr(1);

	spin_unlock(&langwell->wq_lock);

	queue_work(langwell->qwork, &langwell->work);


	return ret;
}

static int __init langwell_otg_init(void)
{
	return pci_register_driver(&otg_pci_driver);
}
module_init(langwell_otg_init);

static void __exit langwell_otg_cleanup(void)
{
	pci_unregister_driver(&otg_pci_driver);
}
module_exit(langwell_otg_cleanup);
