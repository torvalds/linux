/*
 * budget-ci.c: driver for the SAA7146 based Budget DVB cards
 *
 * Compiled from various sources by Michael Hunold <michael@mihu.de>
 *
 *     msp430 IR support contributed by Jack Thomasson <jkt@Helius.COM>
 *     partially based on the Siemens DVB driver by Ralph+Marcus Metzler
 *
 * CI interface support (c) 2004 Andrew de Quincey <adq_dvb@lidskialf.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 * Or, point your browser to http://www.gnu.org/copyleft/gpl.html
 *
 *
 * the project's page is at http://www.linuxtv.org/dvb/
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/spinlock.h>
#include <media/ir-common.h>

#include "budget.h"

#include "dvb_ca_en50221.h"
#include "stv0299.h"
#include "stv0297.h"
#include "tda1004x.h"
#include "stb0899_drv.h"
#include "stb0899_reg.h"
#include "stb6100.h"
#include "lnbp21.h"
#include "bsbe1.h"
#include "bsru6.h"
#include "tda1002x.h"
#include "tda827x.h"

/*
 * Regarding DEBIADDR_IR:
 * Some CI modules hang if random addresses are read.
 * Using address 0x4000 for the IR read means that we
 * use the same address as for CI version, which should
 * be a safe default.
 */
#define DEBIADDR_IR		0x4000
#define DEBIADDR_CICONTROL	0x0000
#define DEBIADDR_CIVERSION	0x4000
#define DEBIADDR_IO		0x1000
#define DEBIADDR_ATTR		0x3000

#define CICONTROL_RESET		0x01
#define CICONTROL_ENABLETS	0x02
#define CICONTROL_CAMDETECT	0x08

#define DEBICICTL		0x00420000
#define DEBICICAM		0x02420000

#define SLOTSTATUS_NONE		1
#define SLOTSTATUS_PRESENT	2
#define SLOTSTATUS_RESET	4
#define SLOTSTATUS_READY	8
#define SLOTSTATUS_OCCUPIED	(SLOTSTATUS_PRESENT|SLOTSTATUS_RESET|SLOTSTATUS_READY)

/*
 * Milliseconds during which a key is regarded as pressed.
 * If an identical command arrives within this time, the timer will start over.
 */
#define IR_KEYPRESS_TIMEOUT	250

/* RC5 device wildcard */
#define IR_DEVICE_ANY		255

static int rc5_device = -1;
module_param(rc5_device, int, 0644);
MODULE_PARM_DESC(rc5_device, "only IR commands to given RC5 device (device = 0 - 31, any device = 255, default: autodetect)");

static int ir_debug;
module_param(ir_debug, int, 0644);
MODULE_PARM_DESC(ir_debug, "enable debugging information for IR decoding");

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

struct budget_ci_ir {
	struct input_dev *dev;
	struct tasklet_struct msp430_irq_tasklet;
	struct timer_list timer_keyup;
	char name[72]; /* 40 + 32 for (struct saa7146_dev).name */
	char phys[32];
	struct ir_input_state state;
	int rc5_device;
	u32 last_raw;
	u32 ir_key;
	bool have_command;
};

struct budget_ci {
	struct budget budget;
	struct tasklet_struct ciintf_irq_tasklet;
	int slot_status;
	int ci_irq;
	struct dvb_ca_en50221 ca;
	struct budget_ci_ir ir;
	u8 tuner_pll_address; /* used for philips_tdm1316l configs */
};

static void msp430_ir_keyup(unsigned long data)
{
	struct budget_ci_ir *ir = (struct budget_ci_ir *) data;
	ir_input_nokey(ir->dev, &ir->state);
}

static void msp430_ir_interrupt(unsigned long data)
{
	struct budget_ci *budget_ci = (struct budget_ci *) data;
	struct input_dev *dev = budget_ci->ir.dev;
	u32 command = ttpci_budget_debiread(&budget_ci->budget, DEBINOSWAP, DEBIADDR_IR, 2, 1, 0) >> 8;
	u32 raw;

	/*
	 * The msp430 chip can generate two different bytes, command and device
	 *
	 * type1: X1CCCCCC, C = command bits (0 - 63)
	 * type2: X0TDDDDD, D = device bits (0 - 31), T = RC5 toggle bit
	 *
	 * Each signal from the remote control can generate one or more command
	 * bytes and one or more device bytes. For the repeated bytes, the
	 * highest bit (X) is set. The first command byte is always generated
	 * before the first device byte. Other than that, no specific order
	 * seems to apply. To make life interesting, bytes can also be lost.
	 *
	 * Only when we have a command and device byte, a keypress is
	 * generated.
	 */

	if (ir_debug)
		printk("budget_ci: received byte 0x%02x\n", command);

	/* Remove repeat bit, we use every command */
	command = command & 0x7f;

	/* Is this a RC5 command byte? */
	if (command & 0x40) {
		budget_ci->ir.have_command = true;
		budget_ci->ir.ir_key = command & 0x3f;
		return;
	}

	/* It's a RC5 device byte */
	if (!budget_ci->ir.have_command)
		return;
	budget_ci->ir.have_command = false;

	if (budget_ci->ir.rc5_device != IR_DEVICE_ANY &&
	    budget_ci->ir.rc5_device != (command & 0x1f))
		return;

	/* Is this a repeated key sequence? (same device, command, toggle) */
	raw = budget_ci->ir.ir_key | (command << 8);
	if (budget_ci->ir.last_raw != raw || !timer_pending(&budget_ci->ir.timer_keyup)) {
		ir_input_nokey(dev, &budget_ci->ir.state);
		ir_input_keydown(dev, &budget_ci->ir.state,
				 budget_ci->ir.ir_key, raw);
		budget_ci->ir.last_raw = raw;
	}

	mod_timer(&budget_ci->ir.timer_keyup, jiffies + msecs_to_jiffies(IR_KEYPRESS_TIMEOUT));
}

static int msp430_ir_init(struct budget_ci *budget_ci)
{
	struct saa7146_dev *saa = budget_ci->budget.dev;
	struct input_dev *input_dev = budget_ci->ir.dev;
	int error;

	budget_ci->ir.dev = input_dev = input_allocate_device();
	if (!input_dev) {
		printk(KERN_ERR "budget_ci: IR interface initialisation failed\n");
		error = -ENOMEM;
		goto out1;
	}

	snprintf(budget_ci->ir.name, sizeof(budget_ci->ir.name),
		 "Budget-CI dvb ir receiver %s", saa->name);
	snprintf(budget_ci->ir.phys, sizeof(budget_ci->ir.phys),
		 "pci-%s/ir0", pci_name(saa->pci));

	input_dev->name = budget_ci->ir.name;

	input_dev->phys = budget_ci->ir.phys;
	input_dev->id.bustype = BUS_PCI;
	input_dev->id.version = 1;
	if (saa->pci->subsystem_vendor) {
		input_dev->id.vendor = saa->pci->subsystem_vendor;
		input_dev->id.product = saa->pci->subsystem_device;
	} else {
		input_dev->id.vendor = saa->pci->vendor;
		input_dev->id.product = saa->pci->device;
	}
	input_dev->dev.parent = &saa->pci->dev;

	/* Select keymap and address */
	switch (budget_ci->budget.dev->pci->subsystem_device) {
	case 0x100c:
	case 0x100f:
	case 0x1011:
	case 0x1012:
		/* The hauppauge keymap is a superset of these remotes */
		ir_input_init(input_dev, &budget_ci->ir.state,
			      IR_TYPE_RC5, ir_codes_hauppauge_new);

		if (rc5_device < 0)
			budget_ci->ir.rc5_device = 0x1f;
		else
			budget_ci->ir.rc5_device = rc5_device;
		break;
	case 0x1010:
	case 0x1017:
	case 0x101a:
		/* for the Technotrend 1500 bundled remote */
		ir_input_init(input_dev, &budget_ci->ir.state,
			      IR_TYPE_RC5, ir_codes_tt_1500);

		if (rc5_device < 0)
			budget_ci->ir.rc5_device = IR_DEVICE_ANY;
		else
			budget_ci->ir.rc5_device = rc5_device;
		break;
	default:
		/* unknown remote */
		ir_input_init(input_dev, &budget_ci->ir.state,
			      IR_TYPE_RC5, ir_codes_budget_ci_old);

		if (rc5_device < 0)
			budget_ci->ir.rc5_device = IR_DEVICE_ANY;
		else
			budget_ci->ir.rc5_device = rc5_device;
		break;
	}

	/* initialise the key-up timeout handler */
	init_timer(&budget_ci->ir.timer_keyup);
	budget_ci->ir.timer_keyup.function = msp430_ir_keyup;
	budget_ci->ir.timer_keyup.data = (unsigned long) &budget_ci->ir;
	budget_ci->ir.last_raw = 0xffff; /* An impossible value */
	error = input_register_device(input_dev);
	if (error) {
		printk(KERN_ERR "budget_ci: could not init driver for IR device (code %d)\n", error);
		goto out2;
	}

	/* note: these must be after input_register_device */
	input_dev->rep[REP_DELAY] = 400;
	input_dev->rep[REP_PERIOD] = 250;

	tasklet_init(&budget_ci->ir.msp430_irq_tasklet, msp430_ir_interrupt,
		     (unsigned long) budget_ci);

	SAA7146_IER_ENABLE(saa, MASK_06);
	saa7146_setgpio(saa, 3, SAA7146_GPIO_IRQHI);

	return 0;

out2:
	input_free_device(input_dev);
out1:
	return error;
}

static void msp430_ir_deinit(struct budget_ci *budget_ci)
{
	struct saa7146_dev *saa = budget_ci->budget.dev;
	struct input_dev *dev = budget_ci->ir.dev;

	SAA7146_IER_DISABLE(saa, MASK_06);
	saa7146_setgpio(saa, 3, SAA7146_GPIO_INPUT);
	tasklet_kill(&budget_ci->ir.msp430_irq_tasklet);

	del_timer_sync(&dev->timer);
	ir_input_nokey(dev, &budget_ci->ir.state);

	input_unregister_device(dev);
}

static int ciintf_read_attribute_mem(struct dvb_ca_en50221 *ca, int slot, int address)
{
	struct budget_ci *budget_ci = (struct budget_ci *) ca->data;

	if (slot != 0)
		return -EINVAL;

	return ttpci_budget_debiread(&budget_ci->budget, DEBICICAM,
				     DEBIADDR_ATTR | (address & 0xfff), 1, 1, 0);
}

static int ciintf_write_attribute_mem(struct dvb_ca_en50221 *ca, int slot, int address, u8 value)
{
	struct budget_ci *budget_ci = (struct budget_ci *) ca->data;

	if (slot != 0)
		return -EINVAL;

	return ttpci_budget_debiwrite(&budget_ci->budget, DEBICICAM,
				      DEBIADDR_ATTR | (address & 0xfff), 1, value, 1, 0);
}

static int ciintf_read_cam_control(struct dvb_ca_en50221 *ca, int slot, u8 address)
{
	struct budget_ci *budget_ci = (struct budget_ci *) ca->data;

	if (slot != 0)
		return -EINVAL;

	return ttpci_budget_debiread(&budget_ci->budget, DEBICICAM,
				     DEBIADDR_IO | (address & 3), 1, 1, 0);
}

static int ciintf_write_cam_control(struct dvb_ca_en50221 *ca, int slot, u8 address, u8 value)
{
	struct budget_ci *budget_ci = (struct budget_ci *) ca->data;

	if (slot != 0)
		return -EINVAL;

	return ttpci_budget_debiwrite(&budget_ci->budget, DEBICICAM,
				      DEBIADDR_IO | (address & 3), 1, value, 1, 0);
}

static int ciintf_slot_reset(struct dvb_ca_en50221 *ca, int slot)
{
	struct budget_ci *budget_ci = (struct budget_ci *) ca->data;
	struct saa7146_dev *saa = budget_ci->budget.dev;

	if (slot != 0)
		return -EINVAL;

	if (budget_ci->ci_irq) {
		// trigger on RISING edge during reset so we know when READY is re-asserted
		saa7146_setgpio(saa, 0, SAA7146_GPIO_IRQHI);
	}
	budget_ci->slot_status = SLOTSTATUS_RESET;
	ttpci_budget_debiwrite(&budget_ci->budget, DEBICICTL, DEBIADDR_CICONTROL, 1, 0, 1, 0);
	msleep(1);
	ttpci_budget_debiwrite(&budget_ci->budget, DEBICICTL, DEBIADDR_CICONTROL, 1,
			       CICONTROL_RESET, 1, 0);

	saa7146_setgpio(saa, 1, SAA7146_GPIO_OUTHI);
	ttpci_budget_set_video_port(saa, BUDGET_VIDEO_PORTB);
	return 0;
}

static int ciintf_slot_shutdown(struct dvb_ca_en50221 *ca, int slot)
{
	struct budget_ci *budget_ci = (struct budget_ci *) ca->data;
	struct saa7146_dev *saa = budget_ci->budget.dev;

	if (slot != 0)
		return -EINVAL;

	saa7146_setgpio(saa, 1, SAA7146_GPIO_OUTHI);
	ttpci_budget_set_video_port(saa, BUDGET_VIDEO_PORTB);
	return 0;
}

static int ciintf_slot_ts_enable(struct dvb_ca_en50221 *ca, int slot)
{
	struct budget_ci *budget_ci = (struct budget_ci *) ca->data;
	struct saa7146_dev *saa = budget_ci->budget.dev;
	int tmp;

	if (slot != 0)
		return -EINVAL;

	saa7146_setgpio(saa, 1, SAA7146_GPIO_OUTLO);

	tmp = ttpci_budget_debiread(&budget_ci->budget, DEBICICTL, DEBIADDR_CICONTROL, 1, 1, 0);
	ttpci_budget_debiwrite(&budget_ci->budget, DEBICICTL, DEBIADDR_CICONTROL, 1,
			       tmp | CICONTROL_ENABLETS, 1, 0);

	ttpci_budget_set_video_port(saa, BUDGET_VIDEO_PORTA);
	return 0;
}

static void ciintf_interrupt(unsigned long data)
{
	struct budget_ci *budget_ci = (struct budget_ci *) data;
	struct saa7146_dev *saa = budget_ci->budget.dev;
	unsigned int flags;

	// ensure we don't get spurious IRQs during initialisation
	if (!budget_ci->budget.ci_present)
		return;

	// read the CAM status
	flags = ttpci_budget_debiread(&budget_ci->budget, DEBICICTL, DEBIADDR_CICONTROL, 1, 1, 0);
	if (flags & CICONTROL_CAMDETECT) {

		// GPIO should be set to trigger on falling edge if a CAM is present
		saa7146_setgpio(saa, 0, SAA7146_GPIO_IRQLO);

		if (budget_ci->slot_status & SLOTSTATUS_NONE) {
			// CAM insertion IRQ
			budget_ci->slot_status = SLOTSTATUS_PRESENT;
			dvb_ca_en50221_camchange_irq(&budget_ci->ca, 0,
						     DVB_CA_EN50221_CAMCHANGE_INSERTED);

		} else if (budget_ci->slot_status & SLOTSTATUS_RESET) {
			// CAM ready (reset completed)
			budget_ci->slot_status = SLOTSTATUS_READY;
			dvb_ca_en50221_camready_irq(&budget_ci->ca, 0);

		} else if (budget_ci->slot_status & SLOTSTATUS_READY) {
			// FR/DA IRQ
			dvb_ca_en50221_frda_irq(&budget_ci->ca, 0);
		}
	} else {

		// trigger on rising edge if a CAM is not present - when a CAM is inserted, we
		// only want to get the IRQ when it sets READY. If we trigger on the falling edge,
		// the CAM might not actually be ready yet.
		saa7146_setgpio(saa, 0, SAA7146_GPIO_IRQHI);

		// generate a CAM removal IRQ if we haven't already
		if (budget_ci->slot_status & SLOTSTATUS_OCCUPIED) {
			// CAM removal IRQ
			budget_ci->slot_status = SLOTSTATUS_NONE;
			dvb_ca_en50221_camchange_irq(&budget_ci->ca, 0,
						     DVB_CA_EN50221_CAMCHANGE_REMOVED);
		}
	}
}

static int ciintf_poll_slot_status(struct dvb_ca_en50221 *ca, int slot, int open)
{
	struct budget_ci *budget_ci = (struct budget_ci *) ca->data;
	unsigned int flags;

	// ensure we don't get spurious IRQs during initialisation
	if (!budget_ci->budget.ci_present)
		return -EINVAL;

	// read the CAM status
	flags = ttpci_budget_debiread(&budget_ci->budget, DEBICICTL, DEBIADDR_CICONTROL, 1, 1, 0);
	if (flags & CICONTROL_CAMDETECT) {
		// mark it as present if it wasn't before
		if (budget_ci->slot_status & SLOTSTATUS_NONE) {
			budget_ci->slot_status = SLOTSTATUS_PRESENT;
		}

		// during a RESET, we check if we can read from IO memory to see when CAM is ready
		if (budget_ci->slot_status & SLOTSTATUS_RESET) {
			if (ciintf_read_attribute_mem(ca, slot, 0) == 0x1d) {
				budget_ci->slot_status = SLOTSTATUS_READY;
			}
		}
	} else {
		budget_ci->slot_status = SLOTSTATUS_NONE;
	}

	if (budget_ci->slot_status != SLOTSTATUS_NONE) {
		if (budget_ci->slot_status & SLOTSTATUS_READY) {
			return DVB_CA_EN50221_POLL_CAM_PRESENT | DVB_CA_EN50221_POLL_CAM_READY;
		}
		return DVB_CA_EN50221_POLL_CAM_PRESENT;
	}

	return 0;
}

static int ciintf_init(struct budget_ci *budget_ci)
{
	struct saa7146_dev *saa = budget_ci->budget.dev;
	int flags;
	int result;
	int ci_version;
	int ca_flags;

	memset(&budget_ci->ca, 0, sizeof(struct dvb_ca_en50221));

	// enable DEBI pins
	saa7146_write(saa, MC1, MASK_27 | MASK_11);

	// test if it is there
	ci_version = ttpci_budget_debiread(&budget_ci->budget, DEBICICTL, DEBIADDR_CIVERSION, 1, 1, 0);
	if ((ci_version & 0xa0) != 0xa0) {
		result = -ENODEV;
		goto error;
	}

	// determine whether a CAM is present or not
	flags = ttpci_budget_debiread(&budget_ci->budget, DEBICICTL, DEBIADDR_CICONTROL, 1, 1, 0);
	budget_ci->slot_status = SLOTSTATUS_NONE;
	if (flags & CICONTROL_CAMDETECT)
		budget_ci->slot_status = SLOTSTATUS_PRESENT;

	// version 0xa2 of the CI firmware doesn't generate interrupts
	if (ci_version == 0xa2) {
		ca_flags = 0;
		budget_ci->ci_irq = 0;
	} else {
		ca_flags = DVB_CA_EN50221_FLAG_IRQ_CAMCHANGE |
				DVB_CA_EN50221_FLAG_IRQ_FR |
				DVB_CA_EN50221_FLAG_IRQ_DA;
		budget_ci->ci_irq = 1;
	}

	// register CI interface
	budget_ci->ca.owner = THIS_MODULE;
	budget_ci->ca.read_attribute_mem = ciintf_read_attribute_mem;
	budget_ci->ca.write_attribute_mem = ciintf_write_attribute_mem;
	budget_ci->ca.read_cam_control = ciintf_read_cam_control;
	budget_ci->ca.write_cam_control = ciintf_write_cam_control;
	budget_ci->ca.slot_reset = ciintf_slot_reset;
	budget_ci->ca.slot_shutdown = ciintf_slot_shutdown;
	budget_ci->ca.slot_ts_enable = ciintf_slot_ts_enable;
	budget_ci->ca.poll_slot_status = ciintf_poll_slot_status;
	budget_ci->ca.data = budget_ci;
	if ((result = dvb_ca_en50221_init(&budget_ci->budget.dvb_adapter,
					  &budget_ci->ca,
					  ca_flags, 1)) != 0) {
		printk("budget_ci: CI interface detected, but initialisation failed.\n");
		goto error;
	}

	// Setup CI slot IRQ
	if (budget_ci->ci_irq) {
		tasklet_init(&budget_ci->ciintf_irq_tasklet, ciintf_interrupt, (unsigned long) budget_ci);
		if (budget_ci->slot_status != SLOTSTATUS_NONE) {
			saa7146_setgpio(saa, 0, SAA7146_GPIO_IRQLO);
		} else {
			saa7146_setgpio(saa, 0, SAA7146_GPIO_IRQHI);
		}
		SAA7146_IER_ENABLE(saa, MASK_03);
	}

	// enable interface
	ttpci_budget_debiwrite(&budget_ci->budget, DEBICICTL, DEBIADDR_CICONTROL, 1,
			       CICONTROL_RESET, 1, 0);

	// success!
	printk("budget_ci: CI interface initialised\n");
	budget_ci->budget.ci_present = 1;

	// forge a fake CI IRQ so the CAM state is setup correctly
	if (budget_ci->ci_irq) {
		flags = DVB_CA_EN50221_CAMCHANGE_REMOVED;
		if (budget_ci->slot_status != SLOTSTATUS_NONE)
			flags = DVB_CA_EN50221_CAMCHANGE_INSERTED;
		dvb_ca_en50221_camchange_irq(&budget_ci->ca, 0, flags);
	}

	return 0;

error:
	saa7146_write(saa, MC1, MASK_27);
	return result;
}

static void ciintf_deinit(struct budget_ci *budget_ci)
{
	struct saa7146_dev *saa = budget_ci->budget.dev;

	// disable CI interrupts
	if (budget_ci->ci_irq) {
		SAA7146_IER_DISABLE(saa, MASK_03);
		saa7146_setgpio(saa, 0, SAA7146_GPIO_INPUT);
		tasklet_kill(&budget_ci->ciintf_irq_tasklet);
	}

	// reset interface
	ttpci_budget_debiwrite(&budget_ci->budget, DEBICICTL, DEBIADDR_CICONTROL, 1, 0, 1, 0);
	msleep(1);
	ttpci_budget_debiwrite(&budget_ci->budget, DEBICICTL, DEBIADDR_CICONTROL, 1,
			       CICONTROL_RESET, 1, 0);

	// disable TS data stream to CI interface
	saa7146_setgpio(saa, 1, SAA7146_GPIO_INPUT);

	// release the CA device
	dvb_ca_en50221_release(&budget_ci->ca);

	// disable DEBI pins
	saa7146_write(saa, MC1, MASK_27);
}

static void budget_ci_irq(struct saa7146_dev *dev, u32 * isr)
{
	struct budget_ci *budget_ci = (struct budget_ci *) dev->ext_priv;

	dprintk(8, "dev: %p, budget_ci: %p\n", dev, budget_ci);

	if (*isr & MASK_06)
		tasklet_schedule(&budget_ci->ir.msp430_irq_tasklet);

	if (*isr & MASK_10)
		ttpci_budget_irq10_handler(dev, isr);

	if ((*isr & MASK_03) && (budget_ci->budget.ci_present) && (budget_ci->ci_irq))
		tasklet_schedule(&budget_ci->ciintf_irq_tasklet);
}

static u8 philips_su1278_tt_inittab[] = {
	0x01, 0x0f,
	0x02, 0x30,
	0x03, 0x00,
	0x04, 0x5b,
	0x05, 0x85,
	0x06, 0x02,
	0x07, 0x00,
	0x08, 0x02,
	0x09, 0x00,
	0x0C, 0x01,
	0x0D, 0x81,
	0x0E, 0x44,
	0x0f, 0x14,
	0x10, 0x3c,
	0x11, 0x84,
	0x12, 0xda,
	0x13, 0x97,
	0x14, 0x95,
	0x15, 0xc9,
	0x16, 0x19,
	0x17, 0x8c,
	0x18, 0x59,
	0x19, 0xf8,
	0x1a, 0xfe,
	0x1c, 0x7f,
	0x1d, 0x00,
	0x1e, 0x00,
	0x1f, 0x50,
	0x20, 0x00,
	0x21, 0x00,
	0x22, 0x00,
	0x23, 0x00,
	0x28, 0x00,
	0x29, 0x28,
	0x2a, 0x14,
	0x2b, 0x0f,
	0x2c, 0x09,
	0x2d, 0x09,
	0x31, 0x1f,
	0x32, 0x19,
	0x33, 0xfc,
	0x34, 0x93,
	0xff, 0xff
};

static int philips_su1278_tt_set_symbol_rate(struct dvb_frontend *fe, u32 srate, u32 ratio)
{
	stv0299_writereg(fe, 0x0e, 0x44);
	if (srate >= 10000000) {
		stv0299_writereg(fe, 0x13, 0x97);
		stv0299_writereg(fe, 0x14, 0x95);
		stv0299_writereg(fe, 0x15, 0xc9);
		stv0299_writereg(fe, 0x17, 0x8c);
		stv0299_writereg(fe, 0x1a, 0xfe);
		stv0299_writereg(fe, 0x1c, 0x7f);
		stv0299_writereg(fe, 0x2d, 0x09);
	} else {
		stv0299_writereg(fe, 0x13, 0x99);
		stv0299_writereg(fe, 0x14, 0x8d);
		stv0299_writereg(fe, 0x15, 0xce);
		stv0299_writereg(fe, 0x17, 0x43);
		stv0299_writereg(fe, 0x1a, 0x1d);
		stv0299_writereg(fe, 0x1c, 0x12);
		stv0299_writereg(fe, 0x2d, 0x05);
	}
	stv0299_writereg(fe, 0x0e, 0x23);
	stv0299_writereg(fe, 0x0f, 0x94);
	stv0299_writereg(fe, 0x10, 0x39);
	stv0299_writereg(fe, 0x15, 0xc9);

	stv0299_writereg(fe, 0x1f, (ratio >> 16) & 0xff);
	stv0299_writereg(fe, 0x20, (ratio >> 8) & 0xff);
	stv0299_writereg(fe, 0x21, (ratio) & 0xf0);

	return 0;
}

static int philips_su1278_tt_tuner_set_params(struct dvb_frontend *fe,
					   struct dvb_frontend_parameters *params)
{
	struct budget_ci *budget_ci = (struct budget_ci *) fe->dvb->priv;
	u32 div;
	u8 buf[4];
	struct i2c_msg msg = {.addr = 0x60,.flags = 0,.buf = buf,.len = sizeof(buf) };

	if ((params->frequency < 950000) || (params->frequency > 2150000))
		return -EINVAL;

	div = (params->frequency + (500 - 1)) / 500;	// round correctly
	buf[0] = (div >> 8) & 0x7f;
	buf[1] = div & 0xff;
	buf[2] = 0x80 | ((div & 0x18000) >> 10) | 2;
	buf[3] = 0x20;

	if (params->u.qpsk.symbol_rate < 4000000)
		buf[3] |= 1;

	if (params->frequency < 1250000)
		buf[3] |= 0;
	else if (params->frequency < 1550000)
		buf[3] |= 0x40;
	else if (params->frequency < 2050000)
		buf[3] |= 0x80;
	else if (params->frequency < 2150000)
		buf[3] |= 0xC0;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	if (i2c_transfer(&budget_ci->budget.i2c_adap, &msg, 1) != 1)
		return -EIO;
	return 0;
}

static struct stv0299_config philips_su1278_tt_config = {

	.demod_address = 0x68,
	.inittab = philips_su1278_tt_inittab,
	.mclk = 64000000UL,
	.invert = 0,
	.skip_reinit = 1,
	.lock_output = STV0299_LOCKOUTPUT_1,
	.volt13_op0_op1 = STV0299_VOLT13_OP1,
	.min_delay_ms = 50,
	.set_symbol_rate = philips_su1278_tt_set_symbol_rate,
};



static int philips_tdm1316l_tuner_init(struct dvb_frontend *fe)
{
	struct budget_ci *budget_ci = (struct budget_ci *) fe->dvb->priv;
	static u8 td1316_init[] = { 0x0b, 0xf5, 0x85, 0xab };
	static u8 disable_mc44BC374c[] = { 0x1d, 0x74, 0xa0, 0x68 };
	struct i2c_msg tuner_msg = {.addr = budget_ci->tuner_pll_address,.flags = 0,.buf = td1316_init,.len =
			sizeof(td1316_init) };

	// setup PLL configuration
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	if (i2c_transfer(&budget_ci->budget.i2c_adap, &tuner_msg, 1) != 1)
		return -EIO;
	msleep(1);

	// disable the mc44BC374c (do not check for errors)
	tuner_msg.addr = 0x65;
	tuner_msg.buf = disable_mc44BC374c;
	tuner_msg.len = sizeof(disable_mc44BC374c);
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	if (i2c_transfer(&budget_ci->budget.i2c_adap, &tuner_msg, 1) != 1) {
		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 1);
		i2c_transfer(&budget_ci->budget.i2c_adap, &tuner_msg, 1);
	}

	return 0;
}

static int philips_tdm1316l_tuner_set_params(struct dvb_frontend *fe, struct dvb_frontend_parameters *params)
{
	struct budget_ci *budget_ci = (struct budget_ci *) fe->dvb->priv;
	u8 tuner_buf[4];
	struct i2c_msg tuner_msg = {.addr = budget_ci->tuner_pll_address,.flags = 0,.buf = tuner_buf,.len = sizeof(tuner_buf) };
	int tuner_frequency = 0;
	u8 band, cp, filter;

	// determine charge pump
	tuner_frequency = params->frequency + 36130000;
	if (tuner_frequency < 87000000)
		return -EINVAL;
	else if (tuner_frequency < 130000000)
		cp = 3;
	else if (tuner_frequency < 160000000)
		cp = 5;
	else if (tuner_frequency < 200000000)
		cp = 6;
	else if (tuner_frequency < 290000000)
		cp = 3;
	else if (tuner_frequency < 420000000)
		cp = 5;
	else if (tuner_frequency < 480000000)
		cp = 6;
	else if (tuner_frequency < 620000000)
		cp = 3;
	else if (tuner_frequency < 830000000)
		cp = 5;
	else if (tuner_frequency < 895000000)
		cp = 7;
	else
		return -EINVAL;

	// determine band
	if (params->frequency < 49000000)
		return -EINVAL;
	else if (params->frequency < 159000000)
		band = 1;
	else if (params->frequency < 444000000)
		band = 2;
	else if (params->frequency < 861000000)
		band = 4;
	else
		return -EINVAL;

	// setup PLL filter and TDA9889
	switch (params->u.ofdm.bandwidth) {
	case BANDWIDTH_6_MHZ:
		tda1004x_writereg(fe, 0x0C, 0x14);
		filter = 0;
		break;

	case BANDWIDTH_7_MHZ:
		tda1004x_writereg(fe, 0x0C, 0x80);
		filter = 0;
		break;

	case BANDWIDTH_8_MHZ:
		tda1004x_writereg(fe, 0x0C, 0x14);
		filter = 1;
		break;

	default:
		return -EINVAL;
	}

	// calculate divisor
	// ((36130000+((1000000/6)/2)) + Finput)/(1000000/6)
	tuner_frequency = (((params->frequency / 1000) * 6) + 217280) / 1000;

	// setup tuner buffer
	tuner_buf[0] = tuner_frequency >> 8;
	tuner_buf[1] = tuner_frequency & 0xff;
	tuner_buf[2] = 0xca;
	tuner_buf[3] = (cp << 5) | (filter << 3) | band;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	if (i2c_transfer(&budget_ci->budget.i2c_adap, &tuner_msg, 1) != 1)
		return -EIO;

	msleep(1);
	return 0;
}

static int philips_tdm1316l_request_firmware(struct dvb_frontend *fe,
					     const struct firmware **fw, char *name)
{
	struct budget_ci *budget_ci = (struct budget_ci *) fe->dvb->priv;

	return request_firmware(fw, name, &budget_ci->budget.dev->pci->dev);
}

static struct tda1004x_config philips_tdm1316l_config = {

	.demod_address = 0x8,
	.invert = 0,
	.invert_oclk = 0,
	.xtal_freq = TDA10046_XTAL_4M,
	.agc_config = TDA10046_AGC_DEFAULT,
	.if_freq = TDA10046_FREQ_3617,
	.request_firmware = philips_tdm1316l_request_firmware,
};

static struct tda1004x_config philips_tdm1316l_config_invert = {

	.demod_address = 0x8,
	.invert = 1,
	.invert_oclk = 0,
	.xtal_freq = TDA10046_XTAL_4M,
	.agc_config = TDA10046_AGC_DEFAULT,
	.if_freq = TDA10046_FREQ_3617,
	.request_firmware = philips_tdm1316l_request_firmware,
};

static int dvbc_philips_tdm1316l_tuner_set_params(struct dvb_frontend *fe, struct dvb_frontend_parameters *params)
{
	struct budget_ci *budget_ci = (struct budget_ci *) fe->dvb->priv;
	u8 tuner_buf[5];
	struct i2c_msg tuner_msg = {.addr = budget_ci->tuner_pll_address,
				    .flags = 0,
				    .buf = tuner_buf,
				    .len = sizeof(tuner_buf) };
	int tuner_frequency = 0;
	u8 band, cp, filter;

	// determine charge pump
	tuner_frequency = params->frequency + 36125000;
	if (tuner_frequency < 87000000)
		return -EINVAL;
	else if (tuner_frequency < 130000000) {
		cp = 3;
		band = 1;
	} else if (tuner_frequency < 160000000) {
		cp = 5;
		band = 1;
	} else if (tuner_frequency < 200000000) {
		cp = 6;
		band = 1;
	} else if (tuner_frequency < 290000000) {
		cp = 3;
		band = 2;
	} else if (tuner_frequency < 420000000) {
		cp = 5;
		band = 2;
	} else if (tuner_frequency < 480000000) {
		cp = 6;
		band = 2;
	} else if (tuner_frequency < 620000000) {
		cp = 3;
		band = 4;
	} else if (tuner_frequency < 830000000) {
		cp = 5;
		band = 4;
	} else if (tuner_frequency < 895000000) {
		cp = 7;
		band = 4;
	} else
		return -EINVAL;

	// assume PLL filter should always be 8MHz for the moment.
	filter = 1;

	// calculate divisor
	tuner_frequency = (params->frequency + 36125000 + (62500/2)) / 62500;

	// setup tuner buffer
	tuner_buf[0] = tuner_frequency >> 8;
	tuner_buf[1] = tuner_frequency & 0xff;
	tuner_buf[2] = 0xc8;
	tuner_buf[3] = (cp << 5) | (filter << 3) | band;
	tuner_buf[4] = 0x80;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	if (i2c_transfer(&budget_ci->budget.i2c_adap, &tuner_msg, 1) != 1)
		return -EIO;

	msleep(50);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	if (i2c_transfer(&budget_ci->budget.i2c_adap, &tuner_msg, 1) != 1)
		return -EIO;

	msleep(1);

	return 0;
}

static u8 dvbc_philips_tdm1316l_inittab[] = {
	0x80, 0x01,
	0x80, 0x00,
	0x81, 0x01,
	0x81, 0x00,
	0x00, 0x09,
	0x01, 0x69,
	0x03, 0x00,
	0x04, 0x00,
	0x07, 0x00,
	0x08, 0x00,
	0x20, 0x00,
	0x21, 0x40,
	0x22, 0x00,
	0x23, 0x00,
	0x24, 0x40,
	0x25, 0x88,
	0x30, 0xff,
	0x31, 0x00,
	0x32, 0xff,
	0x33, 0x00,
	0x34, 0x50,
	0x35, 0x7f,
	0x36, 0x00,
	0x37, 0x20,
	0x38, 0x00,
	0x40, 0x1c,
	0x41, 0xff,
	0x42, 0x29,
	0x43, 0x20,
	0x44, 0xff,
	0x45, 0x00,
	0x46, 0x00,
	0x49, 0x04,
	0x4a, 0x00,
	0x4b, 0x7b,
	0x52, 0x30,
	0x55, 0xae,
	0x56, 0x47,
	0x57, 0xe1,
	0x58, 0x3a,
	0x5a, 0x1e,
	0x5b, 0x34,
	0x60, 0x00,
	0x63, 0x00,
	0x64, 0x00,
	0x65, 0x00,
	0x66, 0x00,
	0x67, 0x00,
	0x68, 0x00,
	0x69, 0x00,
	0x6a, 0x02,
	0x6b, 0x00,
	0x70, 0xff,
	0x71, 0x00,
	0x72, 0x00,
	0x73, 0x00,
	0x74, 0x0c,
	0x80, 0x00,
	0x81, 0x00,
	0x82, 0x00,
	0x83, 0x00,
	0x84, 0x04,
	0x85, 0x80,
	0x86, 0x24,
	0x87, 0x78,
	0x88, 0x10,
	0x89, 0x00,
	0x90, 0x01,
	0x91, 0x01,
	0xa0, 0x04,
	0xa1, 0x00,
	0xa2, 0x00,
	0xb0, 0x91,
	0xb1, 0x0b,
	0xc0, 0x53,
	0xc1, 0x70,
	0xc2, 0x12,
	0xd0, 0x00,
	0xd1, 0x00,
	0xd2, 0x00,
	0xd3, 0x00,
	0xd4, 0x00,
	0xd5, 0x00,
	0xde, 0x00,
	0xdf, 0x00,
	0x61, 0x38,
	0x62, 0x0a,
	0x53, 0x13,
	0x59, 0x08,
	0xff, 0xff,
};

static struct stv0297_config dvbc_philips_tdm1316l_config = {
	.demod_address = 0x1c,
	.inittab = dvbc_philips_tdm1316l_inittab,
	.invert = 0,
	.stop_during_read = 1,
};

static struct tda10023_config tda10023_config = {
	.demod_address = 0xc,
	.invert = 0,
	.xtal = 16000000,
	.pll_m = 11,
	.pll_p = 3,
	.pll_n = 1,
	.deltaf = 0xa511,
};

/* TT S2-3200 DVB-S (STB0899) Inittab */
static const struct stb0899_s1_reg tt3200_stb0899_s1_init_1[] = {

//	 0x0000000b ,	/* SYSREG */
	{ STB0899_DEV_ID		, 0x81 },
	{ STB0899_DISCNTRL1		, 0x32 },
	{ STB0899_DISCNTRL2     	, 0x80 },
	{ STB0899_DISRX_ST0     	, 0x04 },
	{ STB0899_DISRX_ST1     	, 0x00 },
	{ STB0899_DISPARITY     	, 0x00 },
	{ STB0899_DISFIFO       	, 0x00 },
	{ STB0899_DISSTATUS		, 0x20 },
	{ STB0899_DISF22        	, 0x8c },
	{ STB0899_DISF22RX      	, 0x9a },
	//SYSREG ?
	{ STB0899_ACRPRESC      	, 0x11 },
	{ STB0899_ACRDIV1       	, 0x0a },
	{ STB0899_ACRDIV2       	, 0x05 },
	{ STB0899_DACR1         	, 0x00 },
	{ STB0899_DACR2         	, 0x00 },
	{ STB0899_OUTCFG        	, 0x00 },
	{ STB0899_MODECFG       	, 0x00 },
	{ STB0899_IRQSTATUS_3		, 0x30 },
	{ STB0899_IRQSTATUS_2		, 0x00 },
	{ STB0899_IRQSTATUS_1		, 0x00 },
	{ STB0899_IRQSTATUS_0		, 0x00 },
	{ STB0899_IRQMSK_3      	, 0xf3 },
	{ STB0899_IRQMSK_2      	, 0xfc },
	{ STB0899_IRQMSK_1      	, 0xff },
	{ STB0899_IRQMSK_0		, 0xff },
	{ STB0899_IRQCFG		, 0x00 },
	{ STB0899_I2CCFG        	, 0x88 },
	{ STB0899_I2CRPT        	, 0x5c },
	{ STB0899_IOPVALUE5		, 0x00 },
	{ STB0899_IOPVALUE4		, 0x20 },
	{ STB0899_IOPVALUE3		, 0xc9 },
	{ STB0899_IOPVALUE2		, 0x90 },
	{ STB0899_IOPVALUE1		, 0x40 },
	{ STB0899_IOPVALUE0		, 0x00 },
	{ STB0899_GPIO00CFG     	, 0x82 },
	{ STB0899_GPIO01CFG     	, 0x82 },
	{ STB0899_GPIO02CFG     	, 0x82 },
	{ STB0899_GPIO03CFG     	, 0x82 },
	{ STB0899_GPIO04CFG     	, 0x82 },
	{ STB0899_GPIO05CFG     	, 0x82 },
	{ STB0899_GPIO06CFG     	, 0x82 },
	{ STB0899_GPIO07CFG     	, 0x82 },
	{ STB0899_GPIO08CFG     	, 0x82 },
	{ STB0899_GPIO09CFG     	, 0x82 },
	{ STB0899_GPIO10CFG     	, 0x82 },
	{ STB0899_GPIO11CFG     	, 0x82 },
	{ STB0899_GPIO12CFG     	, 0x82 },
	{ STB0899_GPIO13CFG     	, 0x82 },
	{ STB0899_GPIO14CFG     	, 0x82 },
	{ STB0899_GPIO15CFG     	, 0x82 },
	{ STB0899_GPIO16CFG     	, 0x82 },
	{ STB0899_GPIO17CFG     	, 0x82 },
	{ STB0899_GPIO18CFG     	, 0x82 },
	{ STB0899_GPIO19CFG     	, 0x82 },
	{ STB0899_GPIO20CFG     	, 0x82 },
	{ STB0899_SDATCFG       	, 0xb8 },
	{ STB0899_SCLTCFG       	, 0xba },
	{ STB0899_AGCRFCFG      	, 0x1c }, /* 0x11 */
	{ STB0899_GPIO22        	, 0x82 }, /* AGCBB2CFG */
	{ STB0899_GPIO21        	, 0x91 }, /* AGCBB1CFG */
	{ STB0899_DIRCLKCFG     	, 0x82 },
	{ STB0899_CLKOUT27CFG   	, 0x7e },
	{ STB0899_STDBYCFG      	, 0x82 },
	{ STB0899_CS0CFG        	, 0x82 },
	{ STB0899_CS1CFG        	, 0x82 },
	{ STB0899_DISEQCOCFG    	, 0x20 },
	{ STB0899_GPIO32CFG		, 0x82 },
	{ STB0899_GPIO33CFG		, 0x82 },
	{ STB0899_GPIO34CFG		, 0x82 },
	{ STB0899_GPIO35CFG		, 0x82 },
	{ STB0899_GPIO36CFG		, 0x82 },
	{ STB0899_GPIO37CFG		, 0x82 },
	{ STB0899_GPIO38CFG		, 0x82 },
	{ STB0899_GPIO39CFG		, 0x82 },
	{ STB0899_NCOARSE       	, 0x15 }, /* 0x15 = 27 Mhz Clock, F/3 = 198MHz, F/6 = 99MHz */
	{ STB0899_SYNTCTRL      	, 0x02 }, /* 0x00 = CLK from CLKI, 0x02 = CLK from XTALI */
	{ STB0899_FILTCTRL      	, 0x00 },
	{ STB0899_SYSCTRL       	, 0x00 },
	{ STB0899_STOPCLK1      	, 0x20 },
	{ STB0899_STOPCLK2      	, 0x00 },
	{ STB0899_INTBUFSTATUS		, 0x00 },
	{ STB0899_INTBUFCTRL    	, 0x0a },
	{ 0xffff			, 0xff },
};

static const struct stb0899_s2_reg  tt3200_stb0899_s2_init_2[] = {

	{ STB0899_OFF0_DMD_STATUS	, STB0899_BASE_DMD_STATUS	, 0x00000103 },	/* DMDSTATUS	*/
	{ STB0899_OFF0_CRL_FREQ		, STB0899_BASE_CRL_FREQ		, 0x3ed1da56 },	/* CRLFREQ	*/
	{ STB0899_OFF0_BTR_FREQ		, STB0899_BASE_BTR_FREQ		, 0x00004000 },	/* BTRFREQ	*/
	{ STB0899_OFF0_IF_AGC_GAIN	, STB0899_BASE_IF_AGC_GAIN	, 0x00002ade },	/* IFAGCGAIN	*/
	{ STB0899_OFF0_BB_AGC_GAIN	, STB0899_BASE_BB_AGC_GAIN	, 0x000001bc },	/* BBAGCGAIN	*/
	{ STB0899_OFF0_DC_OFFSET	, STB0899_BASE_DC_OFFSET	, 0x00000200 },	/* DCOFFSET	*/
	{ STB0899_OFF0_DMD_CNTRL	, STB0899_BASE_DMD_CNTRL	, 0x0000000f },	/* DMDCNTRL	*/

	{ STB0899_OFF0_IF_AGC_CNTRL	, STB0899_BASE_IF_AGC_CNTRL	, 0x03fb4a20 },	/* IFAGCCNTRL	*/
	{ STB0899_OFF0_BB_AGC_CNTRL	, STB0899_BASE_BB_AGC_CNTRL	, 0x00200c97 },	/* BBAGCCNTRL	*/

	{ STB0899_OFF0_CRL_CNTRL	, STB0899_BASE_CRL_CNTRL	, 0x00000016 },	/* CRLCNTRL	*/
	{ STB0899_OFF0_CRL_PHS_INIT	, STB0899_BASE_CRL_PHS_INIT	, 0x00000000 },	/* CRLPHSINIT	*/
	{ STB0899_OFF0_CRL_FREQ_INIT	, STB0899_BASE_CRL_FREQ_INIT	, 0x00000000 },	/* CRLFREQINIT	*/
	{ STB0899_OFF0_CRL_LOOP_GAIN	, STB0899_BASE_CRL_LOOP_GAIN	, 0x00000000 },	/* CRLLOOPGAIN	*/
	{ STB0899_OFF0_CRL_NOM_FREQ	, STB0899_BASE_CRL_NOM_FREQ	, 0x3ed097b6 },	/* CRLNOMFREQ	*/
	{ STB0899_OFF0_CRL_SWP_RATE	, STB0899_BASE_CRL_SWP_RATE	, 0x00000000 },	/* CRLSWPRATE	*/
	{ STB0899_OFF0_CRL_MAX_SWP	, STB0899_BASE_CRL_MAX_SWP	, 0x00000000 },	/* CRLMAXSWP	*/
	{ STB0899_OFF0_CRL_LK_CNTRL	, STB0899_BASE_CRL_LK_CNTRL	, 0x0f6cdc01 },	/* CRLLKCNTRL	*/
	{ STB0899_OFF0_DECIM_CNTRL	, STB0899_BASE_DECIM_CNTRL	, 0x00000000 },	/* DECIMCNTRL	*/
	{ STB0899_OFF0_BTR_CNTRL	, STB0899_BASE_BTR_CNTRL	, 0x00003993 },	/* BTRCNTRL	*/
	{ STB0899_OFF0_BTR_LOOP_GAIN	, STB0899_BASE_BTR_LOOP_GAIN	, 0x000d3c6f },	/* BTRLOOPGAIN	*/
	{ STB0899_OFF0_BTR_PHS_INIT	, STB0899_BASE_BTR_PHS_INIT	, 0x00000000 },	/* BTRPHSINIT	*/
	{ STB0899_OFF0_BTR_FREQ_INIT	, STB0899_BASE_BTR_FREQ_INIT	, 0x00000000 },	/* BTRFREQINIT	*/
	{ STB0899_OFF0_BTR_NOM_FREQ	, STB0899_BASE_BTR_NOM_FREQ	, 0x0238e38e },	/* BTRNOMFREQ	*/
	{ STB0899_OFF0_BTR_LK_CNTRL	, STB0899_BASE_BTR_LK_CNTRL	, 0x00000000 },	/* BTRLKCNTRL	*/
	{ STB0899_OFF0_DECN_CNTRL	, STB0899_BASE_DECN_CNTRL	, 0x00000000 },	/* DECNCNTRL	*/
	{ STB0899_OFF0_TP_CNTRL		, STB0899_BASE_TP_CNTRL		, 0x00000000 },	/* TPCNTRL	*/
	{ STB0899_OFF0_TP_BUF_STATUS	, STB0899_BASE_TP_BUF_STATUS	, 0x00000000 },	/* TPBUFSTATUS	*/
	{ STB0899_OFF0_DC_ESTIM		, STB0899_BASE_DC_ESTIM		, 0x00000000 },	/* DCESTIM	*/
	{ STB0899_OFF0_FLL_CNTRL	, STB0899_BASE_FLL_CNTRL	, 0x00000000 },	/* FLLCNTRL	*/
	{ STB0899_OFF0_FLL_FREQ_WD	, STB0899_BASE_FLL_FREQ_WD	, 0x40070000 },	/* FLLFREQWD	*/
	{ STB0899_OFF0_ANTI_ALIAS_SEL	, STB0899_BASE_ANTI_ALIAS_SEL	, 0x00000001 },	/* ANTIALIASSEL */
	{ STB0899_OFF0_RRC_ALPHA	, STB0899_BASE_RRC_ALPHA	, 0x00000002 },	/* RRCALPHA	*/
	{ STB0899_OFF0_DC_ADAPT_LSHFT	, STB0899_BASE_DC_ADAPT_LSHFT	, 0x00000000 },	/* DCADAPTISHFT */
	{ STB0899_OFF0_IMB_OFFSET	, STB0899_BASE_IMB_OFFSET	, 0x0000fe01 },	/* IMBOFFSET	*/
	{ STB0899_OFF0_IMB_ESTIMATE	, STB0899_BASE_IMB_ESTIMATE	, 0x00000000 },	/* IMBESTIMATE	*/
	{ STB0899_OFF0_IMB_CNTRL	, STB0899_BASE_IMB_CNTRL	, 0x00000001 },	/* IMBCNTRL	*/
	{ STB0899_OFF0_IF_AGC_CNTRL2	, STB0899_BASE_IF_AGC_CNTRL2	, 0x00005007 },	/* IFAGCCNTRL2	*/
	{ STB0899_OFF0_DMD_CNTRL2	, STB0899_BASE_DMD_CNTRL2	, 0x00000002 },	/* DMDCNTRL2	*/
	{ STB0899_OFF0_TP_BUFFER	, STB0899_BASE_TP_BUFFER	, 0x00000000 },	/* TPBUFFER	*/
	{ STB0899_OFF0_TP_BUFFER1	, STB0899_BASE_TP_BUFFER1	, 0x00000000 },	/* TPBUFFER1	*/
	{ STB0899_OFF0_TP_BUFFER2	, STB0899_BASE_TP_BUFFER2	, 0x00000000 },	/* TPBUFFER2	*/
	{ STB0899_OFF0_TP_BUFFER3	, STB0899_BASE_TP_BUFFER3	, 0x00000000 },	/* TPBUFFER3	*/
	{ STB0899_OFF0_TP_BUFFER4	, STB0899_BASE_TP_BUFFER4	, 0x00000000 },	/* TPBUFFER4	*/
	{ STB0899_OFF0_TP_BUFFER5	, STB0899_BASE_TP_BUFFER5	, 0x00000000 },	/* TPBUFFER5	*/
	{ STB0899_OFF0_TP_BUFFER6	, STB0899_BASE_TP_BUFFER6	, 0x00000000 },	/* TPBUFFER6	*/
	{ STB0899_OFF0_TP_BUFFER7	, STB0899_BASE_TP_BUFFER7	, 0x00000000 },	/* TPBUFFER7	*/
	{ STB0899_OFF0_TP_BUFFER8	, STB0899_BASE_TP_BUFFER8	, 0x00000000 },	/* TPBUFFER8	*/
	{ STB0899_OFF0_TP_BUFFER9	, STB0899_BASE_TP_BUFFER9	, 0x00000000 },	/* TPBUFFER9	*/
	{ STB0899_OFF0_TP_BUFFER10	, STB0899_BASE_TP_BUFFER10	, 0x00000000 },	/* TPBUFFER10	*/
	{ STB0899_OFF0_TP_BUFFER11	, STB0899_BASE_TP_BUFFER11	, 0x00000000 },	/* TPBUFFER11	*/
	{ STB0899_OFF0_TP_BUFFER12	, STB0899_BASE_TP_BUFFER12	, 0x00000000 },	/* TPBUFFER12	*/
	{ STB0899_OFF0_TP_BUFFER13	, STB0899_BASE_TP_BUFFER13	, 0x00000000 },	/* TPBUFFER13	*/
	{ STB0899_OFF0_TP_BUFFER14	, STB0899_BASE_TP_BUFFER14	, 0x00000000 },	/* TPBUFFER14	*/
	{ STB0899_OFF0_TP_BUFFER15	, STB0899_BASE_TP_BUFFER15	, 0x00000000 },	/* TPBUFFER15	*/
	{ STB0899_OFF0_TP_BUFFER16	, STB0899_BASE_TP_BUFFER16	, 0x0000ff00 },	/* TPBUFFER16	*/
	{ STB0899_OFF0_TP_BUFFER17	, STB0899_BASE_TP_BUFFER17	, 0x00000100 },	/* TPBUFFER17	*/
	{ STB0899_OFF0_TP_BUFFER18	, STB0899_BASE_TP_BUFFER18	, 0x0000fe01 },	/* TPBUFFER18	*/
	{ STB0899_OFF0_TP_BUFFER19	, STB0899_BASE_TP_BUFFER19	, 0x000004fe },	/* TPBUFFER19	*/
	{ STB0899_OFF0_TP_BUFFER20	, STB0899_BASE_TP_BUFFER20	, 0x0000cfe7 },	/* TPBUFFER20	*/
	{ STB0899_OFF0_TP_BUFFER21	, STB0899_BASE_TP_BUFFER21	, 0x0000bec6 },	/* TPBUFFER21	*/
	{ STB0899_OFF0_TP_BUFFER22	, STB0899_BASE_TP_BUFFER22	, 0x0000c2bf },	/* TPBUFFER22	*/
	{ STB0899_OFF0_TP_BUFFER23	, STB0899_BASE_TP_BUFFER23	, 0x0000c1c1 },	/* TPBUFFER23	*/
	{ STB0899_OFF0_TP_BUFFER24	, STB0899_BASE_TP_BUFFER24	, 0x0000c1c1 },	/* TPBUFFER24	*/
	{ STB0899_OFF0_TP_BUFFER25	, STB0899_BASE_TP_BUFFER25	, 0x0000c1c1 },	/* TPBUFFER25	*/
	{ STB0899_OFF0_TP_BUFFER26	, STB0899_BASE_TP_BUFFER26	, 0x0000c1c1 },	/* TPBUFFER26	*/
	{ STB0899_OFF0_TP_BUFFER27	, STB0899_BASE_TP_BUFFER27	, 0x0000c1c0 },	/* TPBUFFER27	*/
	{ STB0899_OFF0_TP_BUFFER28	, STB0899_BASE_TP_BUFFER28	, 0x0000c0c0 },	/* TPBUFFER28	*/
	{ STB0899_OFF0_TP_BUFFER29	, STB0899_BASE_TP_BUFFER29	, 0x0000c1c1 },	/* TPBUFFER29	*/
	{ STB0899_OFF0_TP_BUFFER30	, STB0899_BASE_TP_BUFFER30	, 0x0000c1c1 },	/* TPBUFFER30	*/
	{ STB0899_OFF0_TP_BUFFER31	, STB0899_BASE_TP_BUFFER31	, 0x0000c0c1 },	/* TPBUFFER31	*/
	{ STB0899_OFF0_TP_BUFFER32	, STB0899_BASE_TP_BUFFER32	, 0x0000c0c1 },	/* TPBUFFER32	*/
	{ STB0899_OFF0_TP_BUFFER33	, STB0899_BASE_TP_BUFFER33	, 0x0000c1c1 },	/* TPBUFFER33	*/
	{ STB0899_OFF0_TP_BUFFER34	, STB0899_BASE_TP_BUFFER34	, 0x0000c1c1 },	/* TPBUFFER34	*/
	{ STB0899_OFF0_TP_BUFFER35	, STB0899_BASE_TP_BUFFER35	, 0x0000c0c1 },	/* TPBUFFER35	*/
	{ STB0899_OFF0_TP_BUFFER36	, STB0899_BASE_TP_BUFFER36	, 0x0000c1c1 },	/* TPBUFFER36	*/
	{ STB0899_OFF0_TP_BUFFER37	, STB0899_BASE_TP_BUFFER37	, 0x0000c0c1 },	/* TPBUFFER37	*/
	{ STB0899_OFF0_TP_BUFFER38	, STB0899_BASE_TP_BUFFER38	, 0x0000c1c1 },	/* TPBUFFER38	*/
	{ STB0899_OFF0_TP_BUFFER39	, STB0899_BASE_TP_BUFFER39	, 0x0000c0c0 },	/* TPBUFFER39	*/
	{ STB0899_OFF0_TP_BUFFER40	, STB0899_BASE_TP_BUFFER40	, 0x0000c1c0 },	/* TPBUFFER40	*/
	{ STB0899_OFF0_TP_BUFFER41	, STB0899_BASE_TP_BUFFER41	, 0x0000c1c1 },	/* TPBUFFER41	*/
	{ STB0899_OFF0_TP_BUFFER42	, STB0899_BASE_TP_BUFFER42	, 0x0000c0c0 },	/* TPBUFFER42	*/
	{ STB0899_OFF0_TP_BUFFER43	, STB0899_BASE_TP_BUFFER43	, 0x0000c1c0 },	/* TPBUFFER43	*/
	{ STB0899_OFF0_TP_BUFFER44	, STB0899_BASE_TP_BUFFER44	, 0x0000c0c1 },	/* TPBUFFER44	*/
	{ STB0899_OFF0_TP_BUFFER45	, STB0899_BASE_TP_BUFFER45	, 0x0000c1be },	/* TPBUFFER45	*/
	{ STB0899_OFF0_TP_BUFFER46	, STB0899_BASE_TP_BUFFER46	, 0x0000c1c9 },	/* TPBUFFER46	*/
	{ STB0899_OFF0_TP_BUFFER47	, STB0899_BASE_TP_BUFFER47	, 0x0000c0da },	/* TPBUFFER47	*/
	{ STB0899_OFF0_TP_BUFFER48	, STB0899_BASE_TP_BUFFER48	, 0x0000c0ba },	/* TPBUFFER48	*/
	{ STB0899_OFF0_TP_BUFFER49	, STB0899_BASE_TP_BUFFER49	, 0x0000c1c4 },	/* TPBUFFER49	*/
	{ STB0899_OFF0_TP_BUFFER50	, STB0899_BASE_TP_BUFFER50	, 0x0000c1bf },	/* TPBUFFER50	*/
	{ STB0899_OFF0_TP_BUFFER51	, STB0899_BASE_TP_BUFFER51	, 0x0000c0c1 },	/* TPBUFFER51	*/
	{ STB0899_OFF0_TP_BUFFER52	, STB0899_BASE_TP_BUFFER52	, 0x0000c1c0 },	/* TPBUFFER52	*/
	{ STB0899_OFF0_TP_BUFFER53	, STB0899_BASE_TP_BUFFER53	, 0x0000c0c1 },	/* TPBUFFER53	*/
	{ STB0899_OFF0_TP_BUFFER54	, STB0899_BASE_TP_BUFFER54	, 0x0000c1c1 },	/* TPBUFFER54	*/
	{ STB0899_OFF0_TP_BUFFER55	, STB0899_BASE_TP_BUFFER55	, 0x0000c1c1 },	/* TPBUFFER55	*/
	{ STB0899_OFF0_TP_BUFFER56	, STB0899_BASE_TP_BUFFER56	, 0x0000c1c1 },	/* TPBUFFER56	*/
	{ STB0899_OFF0_TP_BUFFER57	, STB0899_BASE_TP_BUFFER57	, 0x0000c1c1 },	/* TPBUFFER57	*/
	{ STB0899_OFF0_TP_BUFFER58	, STB0899_BASE_TP_BUFFER58	, 0x0000c1c1 },	/* TPBUFFER58	*/
	{ STB0899_OFF0_TP_BUFFER59	, STB0899_BASE_TP_BUFFER59	, 0x0000c1c1 },	/* TPBUFFER59	*/
	{ STB0899_OFF0_TP_BUFFER60	, STB0899_BASE_TP_BUFFER60	, 0x0000c1c1 },	/* TPBUFFER60	*/
	{ STB0899_OFF0_TP_BUFFER61	, STB0899_BASE_TP_BUFFER61	, 0x0000c1c1 },	/* TPBUFFER61	*/
	{ STB0899_OFF0_TP_BUFFER62	, STB0899_BASE_TP_BUFFER62	, 0x0000c1c1 },	/* TPBUFFER62	*/
	{ STB0899_OFF0_TP_BUFFER63	, STB0899_BASE_TP_BUFFER63	, 0x0000c1c0 },	/* TPBUFFER63	*/
	{ STB0899_OFF0_RESET_CNTRL	, STB0899_BASE_RESET_CNTRL	, 0x00000001 },	/* RESETCNTRL	*/
	{ STB0899_OFF0_ACM_ENABLE	, STB0899_BASE_ACM_ENABLE	, 0x00005654 },	/* ACMENABLE	*/
	{ STB0899_OFF0_DESCR_CNTRL	, STB0899_BASE_DESCR_CNTRL	, 0x00000000 },	/* DESCRCNTRL	*/
	{ STB0899_OFF0_CSM_CNTRL1	, STB0899_BASE_CSM_CNTRL1	, 0x00020019 },	/* CSMCNTRL1	*/
	{ STB0899_OFF0_CSM_CNTRL2	, STB0899_BASE_CSM_CNTRL2	, 0x004b3237 },	/* CSMCNTRL2	*/
	{ STB0899_OFF0_CSM_CNTRL3	, STB0899_BASE_CSM_CNTRL3	, 0x0003dd17 },	/* CSMCNTRL3	*/
	{ STB0899_OFF0_CSM_CNTRL4	, STB0899_BASE_CSM_CNTRL4	, 0x00008008 },	/* CSMCNTRL4	*/
	{ STB0899_OFF0_UWP_CNTRL1	, STB0899_BASE_UWP_CNTRL1	, 0x002a3106 },	/* UWPCNTRL1	*/
	{ STB0899_OFF0_UWP_CNTRL2	, STB0899_BASE_UWP_CNTRL2	, 0x0006140a },	/* UWPCNTRL2	*/
	{ STB0899_OFF0_UWP_STAT1	, STB0899_BASE_UWP_STAT1	, 0x00008000 },	/* UWPSTAT1	*/
	{ STB0899_OFF0_UWP_STAT2	, STB0899_BASE_UWP_STAT2	, 0x00000000 },	/* UWPSTAT2	*/
	{ STB0899_OFF0_DMD_STAT2	, STB0899_BASE_DMD_STAT2	, 0x00000000 },	/* DMDSTAT2	*/
	{ STB0899_OFF0_FREQ_ADJ_SCALE	, STB0899_BASE_FREQ_ADJ_SCALE	, 0x00000471 },	/* FREQADJSCALE */
	{ STB0899_OFF0_UWP_CNTRL3	, STB0899_BASE_UWP_CNTRL3	, 0x017b0465 },	/* UWPCNTRL3	*/
	{ STB0899_OFF0_SYM_CLK_SEL	, STB0899_BASE_SYM_CLK_SEL	, 0x00000002 },	/* SYMCLKSEL	*/
	{ STB0899_OFF0_SOF_SRCH_TO	, STB0899_BASE_SOF_SRCH_TO	, 0x00196464 },	/* SOFSRCHTO	*/
	{ STB0899_OFF0_ACQ_CNTRL1	, STB0899_BASE_ACQ_CNTRL1	, 0x00000603 },	/* ACQCNTRL1	*/
	{ STB0899_OFF0_ACQ_CNTRL2	, STB0899_BASE_ACQ_CNTRL2	, 0x02046666 },	/* ACQCNTRL2	*/
	{ STB0899_OFF0_ACQ_CNTRL3	, STB0899_BASE_ACQ_CNTRL3	, 0x10046583 },	/* ACQCNTRL3	*/
	{ STB0899_OFF0_FE_SETTLE	, STB0899_BASE_FE_SETTLE	, 0x00010404 },	/* FESETTLE	*/
	{ STB0899_OFF0_AC_DWELL		, STB0899_BASE_AC_DWELL		, 0x0002aa8a },	/* ACDWELL	*/
	{ STB0899_OFF0_ACQUIRE_TRIG	, STB0899_BASE_ACQUIRE_TRIG	, 0x00000000 },	/* ACQUIRETRIG	*/
	{ STB0899_OFF0_LOCK_LOST	, STB0899_BASE_LOCK_LOST	, 0x00000001 },	/* LOCKLOST	*/
	{ STB0899_OFF0_ACQ_STAT1	, STB0899_BASE_ACQ_STAT1	, 0x00000500 },	/* ACQSTAT1	*/
	{ STB0899_OFF0_ACQ_TIMEOUT	, STB0899_BASE_ACQ_TIMEOUT	, 0x0028a0a0 },	/* ACQTIMEOUT	*/
	{ STB0899_OFF0_ACQ_TIME		, STB0899_BASE_ACQ_TIME		, 0x00000000 },	/* ACQTIME	*/
	{ STB0899_OFF0_FINAL_AGC_CNTRL	, STB0899_BASE_FINAL_AGC_CNTRL	, 0x00800c17 },	/* FINALAGCCNTRL*/
	{ STB0899_OFF0_FINAL_AGC_GAIN	, STB0899_BASE_FINAL_AGC_GAIN	, 0x00000000 },	/* FINALAGCCGAIN*/
	{ STB0899_OFF0_EQUALIZER_INIT	, STB0899_BASE_EQUALIZER_INIT	, 0x00000000 },	/* EQUILIZERINIT*/
	{ STB0899_OFF0_EQ_CNTRL		, STB0899_BASE_EQ_CNTRL		, 0x00054802 },	/* EQCNTL	*/
	{ STB0899_OFF0_EQ_I_INIT_COEFF_0, STB0899_BASE_EQ_I_INIT_COEFF_N, 0x00000000 },	/* EQIINITCOEFF0 */
	{ STB0899_OFF1_EQ_I_INIT_COEFF_1, STB0899_BASE_EQ_I_INIT_COEFF_N, 0x00000000 },	/* EQIINITCOEFF1 */
	{ STB0899_OFF2_EQ_I_INIT_COEFF_2, STB0899_BASE_EQ_I_INIT_COEFF_N, 0x00000000 },	/* EQIINITCOEFF2 */
	{ STB0899_OFF3_EQ_I_INIT_COEFF_3, STB0899_BASE_EQ_I_INIT_COEFF_N, 0x00000000 },	/* EQIINITCOEFF3 */
	{ STB0899_OFF4_EQ_I_INIT_COEFF_4, STB0899_BASE_EQ_I_INIT_COEFF_N, 0x00000000 },	/* EQIINITCOEFF4 */
	{ STB0899_OFF5_EQ_I_INIT_COEFF_5, STB0899_BASE_EQ_I_INIT_COEFF_N, 0x00000400 },	/* EQIINITCOEFF5 */
	{ STB0899_OFF6_EQ_I_INIT_COEFF_6, STB0899_BASE_EQ_I_INIT_COEFF_N, 0x00000000 },	/* EQIINITCOEFF6 */
	{ STB0899_OFF7_EQ_I_INIT_COEFF_7, STB0899_BASE_EQ_I_INIT_COEFF_N, 0x00000000 },	/* EQIINITCOEFF7 */
	{ STB0899_OFF8_EQ_I_INIT_COEFF_8, STB0899_BASE_EQ_I_INIT_COEFF_N, 0x00000000 },	/* EQIINITCOEFF8 */
	{ STB0899_OFF9_EQ_I_INIT_COEFF_9, STB0899_BASE_EQ_I_INIT_COEFF_N, 0x00000000 },	/* EQIINITCOEFF9 */
	{ STB0899_OFFa_EQ_I_INIT_COEFF_10,STB0899_BASE_EQ_I_INIT_COEFF_N, 0x00000000 },	/* EQIINITCOEFF10*/
	{ STB0899_OFF0_EQ_Q_INIT_COEFF_0, STB0899_BASE_EQ_Q_INIT_COEFF_N, 0x00000000 },	/* EQQINITCOEFF0 */
	{ STB0899_OFF1_EQ_Q_INIT_COEFF_1, STB0899_BASE_EQ_Q_INIT_COEFF_N, 0x00000000 },	/* EQQINITCOEFF1 */
	{ STB0899_OFF2_EQ_Q_INIT_COEFF_2, STB0899_BASE_EQ_Q_INIT_COEFF_N, 0x00000000 },	/* EQQINITCOEFF2 */
	{ STB0899_OFF3_EQ_Q_INIT_COEFF_3, STB0899_BASE_EQ_Q_INIT_COEFF_N, 0x00000000 },	/* EQQINITCOEFF3 */
	{ STB0899_OFF4_EQ_Q_INIT_COEFF_4, STB0899_BASE_EQ_Q_INIT_COEFF_N, 0x00000000 },	/* EQQINITCOEFF4 */
	{ STB0899_OFF5_EQ_Q_INIT_COEFF_5, STB0899_BASE_EQ_Q_INIT_COEFF_N, 0x00000000 },	/* EQQINITCOEFF5 */
	{ STB0899_OFF6_EQ_Q_INIT_COEFF_6, STB0899_BASE_EQ_Q_INIT_COEFF_N, 0x00000000 },	/* EQQINITCOEFF6 */
	{ STB0899_OFF7_EQ_Q_INIT_COEFF_7, STB0899_BASE_EQ_Q_INIT_COEFF_N, 0x00000000 },	/* EQQINITCOEFF7 */
	{ STB0899_OFF8_EQ_Q_INIT_COEFF_8, STB0899_BASE_EQ_Q_INIT_COEFF_N, 0x00000000 },	/* EQQINITCOEFF8 */
	{ STB0899_OFF9_EQ_Q_INIT_COEFF_9, STB0899_BASE_EQ_Q_INIT_COEFF_N, 0x00000000 },	/* EQQINITCOEFF9 */
	{ STB0899_OFFa_EQ_Q_INIT_COEFF_10,STB0899_BASE_EQ_Q_INIT_COEFF_N, 0x00000000 },	/* EQQINITCOEFF10*/
	{ STB0899_OFF0_EQ_I_OUT_COEFF_0	, STB0899_BASE_EQ_I_OUT_COEFF_N	, 0x00000000 }, /* EQICOEFFSOUT0 */
	{ STB0899_OFF1_EQ_I_OUT_COEFF_1	, STB0899_BASE_EQ_I_OUT_COEFF_N	, 0x00000000 }, /* EQICOEFFSOUT1 */
	{ STB0899_OFF2_EQ_I_OUT_COEFF_2	, STB0899_BASE_EQ_I_OUT_COEFF_N	, 0x00000000 }, /* EQICOEFFSOUT2 */
	{ STB0899_OFF3_EQ_I_OUT_COEFF_3	, STB0899_BASE_EQ_I_OUT_COEFF_N	, 0x00000000 }, /* EQICOEFFSOUT3 */
	{ STB0899_OFF4_EQ_I_OUT_COEFF_4	, STB0899_BASE_EQ_I_OUT_COEFF_N	, 0x00000000 }, /* EQICOEFFSOUT4 */
	{ STB0899_OFF5_EQ_I_OUT_COEFF_5	, STB0899_BASE_EQ_I_OUT_COEFF_N	, 0x00000000 }, /* EQICOEFFSOUT5 */
	{ STB0899_OFF6_EQ_I_OUT_COEFF_6	, STB0899_BASE_EQ_I_OUT_COEFF_N	, 0x00000000 }, /* EQICOEFFSOUT6 */
	{ STB0899_OFF7_EQ_I_OUT_COEFF_7	, STB0899_BASE_EQ_I_OUT_COEFF_N	, 0x00000000 }, /* EQICOEFFSOUT7 */
	{ STB0899_OFF8_EQ_I_OUT_COEFF_8	, STB0899_BASE_EQ_I_OUT_COEFF_N	, 0x00000000 }, /* EQICOEFFSOUT8 */
	{ STB0899_OFF9_EQ_I_OUT_COEFF_9	, STB0899_BASE_EQ_I_OUT_COEFF_N	, 0x00000000 }, /* EQICOEFFSOUT9 */
	{ STB0899_OFFa_EQ_I_OUT_COEFF_10,STB0899_BASE_EQ_I_OUT_COEFF_N	, 0x00000000 }, /* EQICOEFFSOUT10*/
	{ STB0899_OFF0_EQ_Q_OUT_COEFF_0	, STB0899_BASE_EQ_Q_OUT_COEFF_N	, 0x00000000 },	/* EQQCOEFFSOUT0 */
	{ STB0899_OFF1_EQ_Q_OUT_COEFF_1	, STB0899_BASE_EQ_Q_OUT_COEFF_N	, 0x00000000 },	/* EQQCOEFFSOUT1 */
	{ STB0899_OFF2_EQ_Q_OUT_COEFF_2	, STB0899_BASE_EQ_Q_OUT_COEFF_N	, 0x00000000 },	/* EQQCOEFFSOUT2 */
	{ STB0899_OFF3_EQ_Q_OUT_COEFF_3	, STB0899_BASE_EQ_Q_OUT_COEFF_N	, 0x00000000 },	/* EQQCOEFFSOUT3 */
	{ STB0899_OFF4_EQ_Q_OUT_COEFF_4	, STB0899_BASE_EQ_Q_OUT_COEFF_N	, 0x00000000 },	/* EQQCOEFFSOUT4 */
	{ STB0899_OFF5_EQ_Q_OUT_COEFF_5	, STB0899_BASE_EQ_Q_OUT_COEFF_N	, 0x00000000 },	/* EQQCOEFFSOUT5 */
	{ STB0899_OFF6_EQ_Q_OUT_COEFF_6 , STB0899_BASE_EQ_Q_OUT_COEFF_N	, 0x00000000 },	/* EQQCOEFFSOUT6 */
	{ STB0899_OFF7_EQ_Q_OUT_COEFF_7	, STB0899_BASE_EQ_Q_OUT_COEFF_N	, 0x00000000 },	/* EQQCOEFFSOUT7 */
	{ STB0899_OFF8_EQ_Q_OUT_COEFF_8	, STB0899_BASE_EQ_Q_OUT_COEFF_N	, 0x00000000 },	/* EQQCOEFFSOUT8 */
	{ STB0899_OFF9_EQ_Q_OUT_COEFF_9	, STB0899_BASE_EQ_Q_OUT_COEFF_N	, 0x00000000 },	/* EQQCOEFFSOUT9 */
	{ STB0899_OFFa_EQ_Q_OUT_COEFF_10, STB0899_BASE_EQ_Q_OUT_COEFF_N	, 0x00000000 },	/* EQQCOEFFSOUT10*/
	{ 0xffff			, 0xffffffff			, 0xffffffff },
};

static const struct stb0899_s1_reg tt3200_stb0899_s1_init_3[] = {
	{ STB0899_DEMOD         	, 0x00 },
	{ STB0899_RCOMPC        	, 0xc9 },
	{ STB0899_AGC1CN        	, 0x41 },
	{ STB0899_AGC1REF       	, 0x10 },
	{ STB0899_RTC	        	, 0x7a },
	{ STB0899_TMGCFG        	, 0x4e },
	{ STB0899_AGC2REF       	, 0x34 },
	{ STB0899_TLSR          	, 0x84 },
	{ STB0899_CFD           	, 0xc7 },
	{ STB0899_ACLC	        	, 0x87 },
	{ STB0899_BCLC          	, 0x94 },
	{ STB0899_EQON          	, 0x41 },
	{ STB0899_LDT           	, 0xdd },
	{ STB0899_LDT2          	, 0xc9 },
	{ STB0899_EQUALREF      	, 0xb4 },
	{ STB0899_TMGRAMP       	, 0x10 },
	{ STB0899_TMGTHD        	, 0x30 },
	{ STB0899_IDCCOMP		, 0xfb },
	{ STB0899_QDCCOMP		, 0x03 },
	{ STB0899_POWERI		, 0x3b },
	{ STB0899_POWERQ		, 0x3d },
	{ STB0899_RCOMP			, 0x81 },
	{ STB0899_AGCIQIN		, 0x80 },
	{ STB0899_AGC2I1		, 0x04 },
	{ STB0899_AGC2I2		, 0xf5 },
	{ STB0899_TLIR			, 0x25 },
	{ STB0899_RTF			, 0x80 },
	{ STB0899_DSTATUS		, 0x00 },
	{ STB0899_LDI			, 0xca },
	{ STB0899_CFRM			, 0xf1 },
	{ STB0899_CFRL			, 0xf3 },
	{ STB0899_NIRM			, 0x2a },
	{ STB0899_NIRL			, 0x05 },
	{ STB0899_ISYMB			, 0x17 },
	{ STB0899_QSYMB			, 0xfa },
	{ STB0899_SFRH          	, 0x2f },
	{ STB0899_SFRM          	, 0x68 },
	{ STB0899_SFRL          	, 0x40 },
	{ STB0899_SFRUPH        	, 0x2f },
	{ STB0899_SFRUPM        	, 0x68 },
	{ STB0899_SFRUPL        	, 0x40 },
	{ STB0899_EQUAI1		, 0xfd },
	{ STB0899_EQUAQ1		, 0x04 },
	{ STB0899_EQUAI2		, 0x0f },
	{ STB0899_EQUAQ2		, 0xff },
	{ STB0899_EQUAI3		, 0xdf },
	{ STB0899_EQUAQ3		, 0xfa },
	{ STB0899_EQUAI4		, 0x37 },
	{ STB0899_EQUAQ4		, 0x0d },
	{ STB0899_EQUAI5		, 0xbd },
	{ STB0899_EQUAQ5		, 0xf7 },
	{ STB0899_DSTATUS2		, 0x00 },
	{ STB0899_VSTATUS       	, 0x00 },
	{ STB0899_VERROR		, 0xff },
	{ STB0899_IQSWAP		, 0x2a },
	{ STB0899_ECNT1M		, 0x00 },
	{ STB0899_ECNT1L		, 0x00 },
	{ STB0899_ECNT2M		, 0x00 },
	{ STB0899_ECNT2L		, 0x00 },
	{ STB0899_ECNT3M		, 0x00 },
	{ STB0899_ECNT3L		, 0x00 },
	{ STB0899_FECAUTO1      	, 0x06 },
	{ STB0899_FECM	        	, 0x01 },
	{ STB0899_VTH12         	, 0xf0 },
	{ STB0899_VTH23         	, 0xa0 },
	{ STB0899_VTH34	        	, 0x78 },
	{ STB0899_VTH56         	, 0x4e },
	{ STB0899_VTH67         	, 0x48 },
	{ STB0899_VTH78         	, 0x38 },
	{ STB0899_PRVIT         	, 0xff },
	{ STB0899_VITSYNC       	, 0x19 },
	{ STB0899_RSULC         	, 0xb1 }, /* DVB = 0xb1, DSS = 0xa1 */
	{ STB0899_TSULC         	, 0x42 },
	{ STB0899_RSLLC         	, 0x40 },
	{ STB0899_TSLPL	        	, 0x12 },
	{ STB0899_TSCFGH        	, 0x0c },
	{ STB0899_TSCFGM        	, 0x00 },
	{ STB0899_TSCFGL        	, 0x0c },
	{ STB0899_TSOUT			, 0x0d }, /* 0x0d for CAM */
	{ STB0899_RSSYNCDEL     	, 0x00 },
	{ STB0899_TSINHDELH     	, 0x02 },
	{ STB0899_TSINHDELM		, 0x00 },
	{ STB0899_TSINHDELL		, 0x00 },
	{ STB0899_TSLLSTKM		, 0x00 },
	{ STB0899_TSLLSTKL		, 0x00 },
	{ STB0899_TSULSTKM		, 0x00 },
	{ STB0899_TSULSTKL		, 0xab },
	{ STB0899_PCKLENUL		, 0x00 },
	{ STB0899_PCKLENLL		, 0xcc },
	{ STB0899_RSPCKLEN		, 0xcc },
	{ STB0899_TSSTATUS		, 0x80 },
	{ STB0899_ERRCTRL1      	, 0xb6 },
	{ STB0899_ERRCTRL2      	, 0x96 },
	{ STB0899_ERRCTRL3      	, 0x89 },
	{ STB0899_DMONMSK1		, 0x27 },
	{ STB0899_DMONMSK0		, 0x03 },
	{ STB0899_DEMAPVIT      	, 0x5c },
	{ STB0899_PLPARM		, 0x1f },
	{ STB0899_PDELCTRL      	, 0x48 },
	{ STB0899_PDELCTRL2     	, 0x00 },
	{ STB0899_BBHCTRL1      	, 0x00 },
	{ STB0899_BBHCTRL2      	, 0x00 },
	{ STB0899_HYSTTHRESH    	, 0x77 },
	{ STB0899_MATCSTM		, 0x00 },
	{ STB0899_MATCSTL		, 0x00 },
	{ STB0899_UPLCSTM		, 0x00 },
	{ STB0899_UPLCSTL		, 0x00 },
	{ STB0899_DFLCSTM		, 0x00 },
	{ STB0899_DFLCSTL		, 0x00 },
	{ STB0899_SYNCCST		, 0x00 },
	{ STB0899_SYNCDCSTM		, 0x00 },
	{ STB0899_SYNCDCSTL		, 0x00 },
	{ STB0899_ISI_ENTRY		, 0x00 },
	{ STB0899_ISI_BIT_EN		, 0x00 },
	{ STB0899_MATSTRM		, 0x00 },
	{ STB0899_MATSTRL		, 0x00 },
	{ STB0899_UPLSTRM		, 0x00 },
	{ STB0899_UPLSTRL		, 0x00 },
	{ STB0899_DFLSTRM		, 0x00 },
	{ STB0899_DFLSTRL		, 0x00 },
	{ STB0899_SYNCSTR		, 0x00 },
	{ STB0899_SYNCDSTRM		, 0x00 },
	{ STB0899_SYNCDSTRL		, 0x00 },
	{ STB0899_CFGPDELSTATUS1	, 0x10 },
	{ STB0899_CFGPDELSTATUS2	, 0x00 },
	{ STB0899_BBFERRORM		, 0x00 },
	{ STB0899_BBFERRORL		, 0x00 },
	{ STB0899_UPKTERRORM		, 0x00 },
	{ STB0899_UPKTERRORL		, 0x00 },
	{ 0xffff			, 0xff },
};

static const struct stb0899_s2_reg tt3200_stb0899_s2_init_4[] = {
	{ STB0899_OFF0_BLOCK_LNGTH	, STB0899_BASE_BLOCK_LNGTH	, 0x00000008 },	/* BLOCKLNGTH	*/
	{ STB0899_OFF0_ROW_STR		, STB0899_BASE_ROW_STR		, 0x000000b4 },	/* ROWSTR	*/
	{ STB0899_OFF0_BN_END_ADDR	, STB0899_BASE_BN_END_ADDR	, 0x000004b5 },	/* BNANDADDR	*/
	{ STB0899_OFF0_CN_END_ADDR	, STB0899_BASE_CN_END_ADDR	, 0x00000b4b },	/* CNANDADDR	*/
	{ STB0899_OFF0_INFO_LENGTH	, STB0899_BASE_INFO_LENGTH	, 0x00000078 },	/* INFOLENGTH	*/
	{ STB0899_OFF0_BOT_ADDR		, STB0899_BASE_BOT_ADDR		, 0x000001e0 },	/* BOT_ADDR	*/
	{ STB0899_OFF0_BCH_BLK_LN	, STB0899_BASE_BCH_BLK_LN	, 0x0000a8c0 },	/* BCHBLKLN	*/
	{ STB0899_OFF0_BCH_T		, STB0899_BASE_BCH_T		, 0x0000000c },	/* BCHT		*/
	{ STB0899_OFF0_CNFG_MODE	, STB0899_BASE_CNFG_MODE	, 0x00000001 },	/* CNFGMODE	*/
	{ STB0899_OFF0_LDPC_STAT	, STB0899_BASE_LDPC_STAT	, 0x0000000d },	/* LDPCSTAT	*/
	{ STB0899_OFF0_ITER_SCALE	, STB0899_BASE_ITER_SCALE	, 0x00000040 },	/* ITERSCALE	*/
	{ STB0899_OFF0_INPUT_MODE	, STB0899_BASE_INPUT_MODE	, 0x00000000 },	/* INPUTMODE	*/
	{ STB0899_OFF0_LDPCDECRST	, STB0899_BASE_LDPCDECRST	, 0x00000000 },	/* LDPCDECRST	*/
	{ STB0899_OFF0_CLK_PER_BYTE_RW	, STB0899_BASE_CLK_PER_BYTE_RW	, 0x00000008 },	/* CLKPERBYTE	*/
	{ STB0899_OFF0_BCH_ERRORS	, STB0899_BASE_BCH_ERRORS	, 0x00000000 },	/* BCHERRORS	*/
	{ STB0899_OFF0_LDPC_ERRORS	, STB0899_BASE_LDPC_ERRORS	, 0x00000000 },	/* LDPCERRORS	*/
	{ STB0899_OFF0_BCH_MODE		, STB0899_BASE_BCH_MODE		, 0x00000000 },	/* BCHMODE	*/
	{ STB0899_OFF0_ERR_ACC_PER	, STB0899_BASE_ERR_ACC_PER	, 0x00000008 },	/* ERRACCPER	*/
	{ STB0899_OFF0_BCH_ERR_ACC	, STB0899_BASE_BCH_ERR_ACC	, 0x00000000 },	/* BCHERRACC	*/
	{ STB0899_OFF0_FEC_TP_SEL	, STB0899_BASE_FEC_TP_SEL	, 0x00000000 },	/* FECTPSEL	*/
	{ 0xffff			, 0xffffffff			, 0xffffffff },
};

static const struct stb0899_s1_reg tt3200_stb0899_s1_init_5[] = {
	{ STB0899_TSTCK		, 0x00 },
	{ STB0899_TSTRES	, 0x00 },
	{ STB0899_TSTOUT	, 0x00 },
	{ STB0899_TSTIN		, 0x00 },
	{ STB0899_TSTSYS	, 0x00 },
	{ STB0899_TSTCHIP	, 0x00 },
	{ STB0899_TSTFREE	, 0x00 },
	{ STB0899_TSTI2C	, 0x00 },
	{ STB0899_BITSPEEDM	, 0x00 },
	{ STB0899_BITSPEEDL	, 0x00 },
	{ STB0899_TBUSBIT	, 0x00 },
	{ STB0899_TSTDIS	, 0x00 },
	{ STB0899_TSTDISRX	, 0x00 },
	{ STB0899_TSTJETON	, 0x00 },
	{ STB0899_TSTDCADJ	, 0x00 },
	{ STB0899_TSTAGC1	, 0x00 },
	{ STB0899_TSTAGC1N	, 0x00 },
	{ STB0899_TSTPOLYPH	, 0x00 },
	{ STB0899_TSTR		, 0x00 },
	{ STB0899_TSTAGC2	, 0x00 },
	{ STB0899_TSTCTL1	, 0x00 },
	{ STB0899_TSTCTL2	, 0x00 },
	{ STB0899_TSTCTL3	, 0x00 },
	{ STB0899_TSTDEMAP	, 0x00 },
	{ STB0899_TSTDEMAP2	, 0x00 },
	{ STB0899_TSTDEMMON	, 0x00 },
	{ STB0899_TSTRATE	, 0x00 },
	{ STB0899_TSTSELOUT	, 0x00 },
	{ STB0899_TSYNC		, 0x00 },
	{ STB0899_TSTERR	, 0x00 },
	{ STB0899_TSTRAM1	, 0x00 },
	{ STB0899_TSTVSELOUT	, 0x00 },
	{ STB0899_TSTFORCEIN	, 0x00 },
	{ STB0899_TSTRS1	, 0x00 },
	{ STB0899_TSTRS2	, 0x00 },
	{ STB0899_TSTRS3	, 0x00 },
	{ STB0899_GHOSTREG	, 0x81 },
	{ 0xffff		, 0xff },
};

#define TT3200_DVBS2_ESNO_AVE			3
#define TT3200_DVBS2_ESNO_QUANT			32
#define TT3200_DVBS2_AVFRAMES_COARSE		10
#define TT3200_DVBS2_AVFRAMES_FINE		20
#define TT3200_DVBS2_MISS_THRESHOLD		6
#define TT3200_DVBS2_UWP_THRESHOLD_ACQ		1125
#define TT3200_DVBS2_UWP_THRESHOLD_TRACK	758
#define TT3200_DVBS2_UWP_THRESHOLD_SOF		1350
#define TT3200_DVBS2_SOF_SEARCH_TIMEOUT		1664100

#define TT3200_DVBS2_BTR_NCO_BITS		28
#define TT3200_DVBS2_BTR_GAIN_SHIFT_OFFSET	15
#define TT3200_DVBS2_CRL_NCO_BITS		30
#define TT3200_DVBS2_LDPC_MAX_ITER		70

static int stb6100_get_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct dvb_frontend_ops	*frontend_ops = NULL;
	struct dvb_tuner_ops	*tuner_ops = NULL;
	struct tuner_state	t_state;
	int err = 0;

	if (&fe->ops)
		frontend_ops = &fe->ops;
	if (&frontend_ops->tuner_ops)
		tuner_ops = &frontend_ops->tuner_ops;
	if (tuner_ops->get_state) {
		if ((err = tuner_ops->get_state(fe, DVBFE_TUNER_FREQUENCY, &t_state)) < 0) {
			printk("%s: Invalid parameter\n", __func__);
			return err;
		}
		*frequency = t_state.frequency;
		printk("%s: Frequency=%d\n", __func__, t_state.frequency);
	}
	return 0;
}

static int stb6100_set_frequency(struct dvb_frontend *fe, u32 frequency)
{
	struct dvb_frontend_ops	*frontend_ops = NULL;
	struct dvb_tuner_ops	*tuner_ops = NULL;
	struct tuner_state	t_state;
	int err = 0;

	t_state.frequency = frequency;
	if (&fe->ops)
		frontend_ops = &fe->ops;
	if (&frontend_ops->tuner_ops)
		tuner_ops = &frontend_ops->tuner_ops;
	if (tuner_ops->set_state) {
		if ((err = tuner_ops->set_state(fe, DVBFE_TUNER_FREQUENCY, &t_state)) < 0) {
			printk("%s: Invalid parameter\n", __func__);
			return err;
		}
	}
	printk("%s: Frequency=%d\n", __func__, t_state.frequency);
	return 0;
}

static int stb6100_get_bandwidth(struct dvb_frontend *fe, u32 *bandwidth)
{
	struct dvb_frontend_ops	*frontend_ops = &fe->ops;
	struct dvb_tuner_ops	*tuner_ops = &frontend_ops->tuner_ops;
	struct tuner_state	t_state;
	int err = 0;

	if (&fe->ops)
		frontend_ops = &fe->ops;
	if (&frontend_ops->tuner_ops)
		tuner_ops = &frontend_ops->tuner_ops;
	if (tuner_ops->get_state) {
		if ((err = tuner_ops->get_state(fe, DVBFE_TUNER_BANDWIDTH, &t_state)) < 0) {
			printk("%s: Invalid parameter\n", __func__);
			return err;
		}
		*bandwidth = t_state.bandwidth;
	}
	printk("%s: Bandwidth=%d\n", __func__, t_state.bandwidth);
	return 0;
}

static int stb6100_set_bandwidth(struct dvb_frontend *fe, u32 bandwidth)
{
	struct dvb_frontend_ops	*frontend_ops = NULL;
	struct dvb_tuner_ops	*tuner_ops = NULL;
	struct tuner_state	t_state;
	int err = 0;

	t_state.bandwidth = bandwidth;
	if (&fe->ops)
		frontend_ops = &fe->ops;
	if (&frontend_ops->tuner_ops)
		tuner_ops = &frontend_ops->tuner_ops;
	if (tuner_ops->set_state) {
		if ((err = tuner_ops->set_state(fe, DVBFE_TUNER_BANDWIDTH, &t_state)) < 0) {
			printk("%s: Invalid parameter\n", __func__);
			return err;
		}
	}
	printk("%s: Bandwidth=%d\n", __func__, t_state.bandwidth);
	return 0;
}

static struct stb0899_config tt3200_config = {
	.init_dev		= tt3200_stb0899_s1_init_1,
	.init_s2_demod		= tt3200_stb0899_s2_init_2,
	.init_s1_demod		= tt3200_stb0899_s1_init_3,
	.init_s2_fec		= tt3200_stb0899_s2_init_4,
	.init_tst		= tt3200_stb0899_s1_init_5,

	.demod_address 		= 0x68,

	.xtal_freq		= 27000000,
	.inversion		= IQ_SWAP_ON, /* 1 */

	.esno_ave		= TT3200_DVBS2_ESNO_AVE,
	.esno_quant		= TT3200_DVBS2_ESNO_QUANT,
	.avframes_coarse	= TT3200_DVBS2_AVFRAMES_COARSE,
	.avframes_fine		= TT3200_DVBS2_AVFRAMES_FINE,
	.miss_threshold		= TT3200_DVBS2_MISS_THRESHOLD,
	.uwp_threshold_acq	= TT3200_DVBS2_UWP_THRESHOLD_ACQ,
	.uwp_threshold_track	= TT3200_DVBS2_UWP_THRESHOLD_TRACK,
	.uwp_threshold_sof	= TT3200_DVBS2_UWP_THRESHOLD_SOF,
	.sof_search_timeout	= TT3200_DVBS2_SOF_SEARCH_TIMEOUT,

	.btr_nco_bits		= TT3200_DVBS2_BTR_NCO_BITS,
	.btr_gain_shift_offset	= TT3200_DVBS2_BTR_GAIN_SHIFT_OFFSET,
	.crl_nco_bits		= TT3200_DVBS2_CRL_NCO_BITS,
	.ldpc_max_iter		= TT3200_DVBS2_LDPC_MAX_ITER,

	.tuner_get_frequency	= stb6100_get_frequency,
	.tuner_set_frequency	= stb6100_set_frequency,
	.tuner_set_bandwidth	= stb6100_set_bandwidth,
	.tuner_get_bandwidth	= stb6100_get_bandwidth,
	.tuner_set_rfsiggain	= NULL,
};

struct stb6100_config tt3200_stb6100_config = {
	.tuner_address = 0x60
};

static void frontend_init(struct budget_ci *budget_ci)
{
	switch (budget_ci->budget.dev->pci->subsystem_device) {
	case 0x100c:		// Hauppauge/TT Nova-CI budget (stv0299/ALPS BSRU6(tsa5059))
		budget_ci->budget.dvb_frontend =
			dvb_attach(stv0299_attach, &alps_bsru6_config, &budget_ci->budget.i2c_adap);
		if (budget_ci->budget.dvb_frontend) {
			budget_ci->budget.dvb_frontend->ops.tuner_ops.set_params = alps_bsru6_tuner_set_params;
			budget_ci->budget.dvb_frontend->tuner_priv = &budget_ci->budget.i2c_adap;
			break;
		}
		break;

	case 0x100f:		// Hauppauge/TT Nova-CI budget (stv0299b/Philips su1278(tsa5059))
		budget_ci->budget.dvb_frontend =
			dvb_attach(stv0299_attach, &philips_su1278_tt_config, &budget_ci->budget.i2c_adap);
		if (budget_ci->budget.dvb_frontend) {
			budget_ci->budget.dvb_frontend->ops.tuner_ops.set_params = philips_su1278_tt_tuner_set_params;
			break;
		}
		break;

	case 0x1010:		// TT DVB-C CI budget (stv0297/Philips tdm1316l(tda6651tt))
		budget_ci->tuner_pll_address = 0x61;
		budget_ci->budget.dvb_frontend =
			dvb_attach(stv0297_attach, &dvbc_philips_tdm1316l_config, &budget_ci->budget.i2c_adap);
		if (budget_ci->budget.dvb_frontend) {
			budget_ci->budget.dvb_frontend->ops.tuner_ops.set_params = dvbc_philips_tdm1316l_tuner_set_params;
			break;
		}
		break;

	case 0x1011:		// Hauppauge/TT Nova-T budget (tda10045/Philips tdm1316l(tda6651tt) + TDA9889)
		budget_ci->tuner_pll_address = 0x63;
		budget_ci->budget.dvb_frontend =
			dvb_attach(tda10045_attach, &philips_tdm1316l_config, &budget_ci->budget.i2c_adap);
		if (budget_ci->budget.dvb_frontend) {
			budget_ci->budget.dvb_frontend->ops.tuner_ops.init = philips_tdm1316l_tuner_init;
			budget_ci->budget.dvb_frontend->ops.tuner_ops.set_params = philips_tdm1316l_tuner_set_params;
			break;
		}
		break;

	case 0x1012:		// TT DVB-T CI budget (tda10046/Philips tdm1316l(tda6651tt))
		budget_ci->tuner_pll_address = 0x60;
		budget_ci->budget.dvb_frontend =
			dvb_attach(tda10046_attach, &philips_tdm1316l_config_invert, &budget_ci->budget.i2c_adap);
		if (budget_ci->budget.dvb_frontend) {
			budget_ci->budget.dvb_frontend->ops.tuner_ops.init = philips_tdm1316l_tuner_init;
			budget_ci->budget.dvb_frontend->ops.tuner_ops.set_params = philips_tdm1316l_tuner_set_params;
			break;
		}
		break;

	case 0x1017:		// TT S-1500 PCI
		budget_ci->budget.dvb_frontend = dvb_attach(stv0299_attach, &alps_bsbe1_config, &budget_ci->budget.i2c_adap);
		if (budget_ci->budget.dvb_frontend) {
			budget_ci->budget.dvb_frontend->ops.tuner_ops.set_params = alps_bsbe1_tuner_set_params;
			budget_ci->budget.dvb_frontend->tuner_priv = &budget_ci->budget.i2c_adap;

			budget_ci->budget.dvb_frontend->ops.dishnetwork_send_legacy_command = NULL;
			if (dvb_attach(lnbp21_attach, budget_ci->budget.dvb_frontend, &budget_ci->budget.i2c_adap, LNBP21_LLC, 0) == NULL) {
				printk("%s: No LNBP21 found!\n", __func__);
				dvb_frontend_detach(budget_ci->budget.dvb_frontend);
				budget_ci->budget.dvb_frontend = NULL;
			}
		}
		break;

	case 0x101a: /* TT Budget-C-1501 (philips tda10023/philips tda8274A) */
		budget_ci->budget.dvb_frontend = dvb_attach(tda10023_attach, &tda10023_config, &budget_ci->budget.i2c_adap, 0x48);
		if (budget_ci->budget.dvb_frontend) {
			if (dvb_attach(tda827x_attach, budget_ci->budget.dvb_frontend, 0x61, &budget_ci->budget.i2c_adap, NULL) == NULL) {
				printk(KERN_ERR "%s: No tda827x found!\n", __func__);
				dvb_frontend_detach(budget_ci->budget.dvb_frontend);
				budget_ci->budget.dvb_frontend = NULL;
			}
		}
		break;

	case 0x1019:		// TT S2-3200 PCI
		budget_ci->budget.dvb_frontend = stb0899_attach(&tt3200_config, &budget_ci->budget.i2c_adap);
		if (budget_ci->budget.dvb_frontend) {
			if (stb6100_attach(budget_ci->budget.dvb_frontend, &tt3200_stb6100_config, &budget_ci->budget.i2c_adap)) {
				if (!lnbp21_attach(budget_ci->budget.dvb_frontend, &budget_ci->budget.i2c_adap, 0, 0)) {
					printk("%s: No LNBP21 found!\n", __FUNCTION__);
					if (budget_ci->budget.dvb_frontend->ops.tuner_ops.release)
						budget_ci->budget.dvb_frontend->ops.tuner_ops.release(budget_ci->budget.dvb_frontend);
					if (budget_ci->budget.dvb_frontend->ops.release)
						budget_ci->budget.dvb_frontend->ops.release(budget_ci->budget.dvb_frontend);
					budget_ci->budget.dvb_frontend = NULL;
				}
			} else {
				if (budget_ci->budget.dvb_frontend->ops.release)
					budget_ci->budget.dvb_frontend->ops.release(budget_ci->budget.dvb_frontend);
			}
		}
		break;

	}

	if (budget_ci->budget.dvb_frontend == NULL) {
		printk("budget-ci: A frontend driver was not found for device [%04x:%04x] subsystem [%04x:%04x]\n",
		       budget_ci->budget.dev->pci->vendor,
		       budget_ci->budget.dev->pci->device,
		       budget_ci->budget.dev->pci->subsystem_vendor,
		       budget_ci->budget.dev->pci->subsystem_device);
	} else {
		if (dvb_register_frontend
		    (&budget_ci->budget.dvb_adapter, budget_ci->budget.dvb_frontend)) {
			printk("budget-ci: Frontend registration failed!\n");
			dvb_frontend_detach(budget_ci->budget.dvb_frontend);
			budget_ci->budget.dvb_frontend = NULL;
		}
	}
}

static int budget_ci_attach(struct saa7146_dev *dev, struct saa7146_pci_extension_data *info)
{
	struct budget_ci *budget_ci;
	int err;

	budget_ci = kzalloc(sizeof(struct budget_ci), GFP_KERNEL);
	if (!budget_ci) {
		err = -ENOMEM;
		goto out1;
	}

	dprintk(2, "budget_ci: %p\n", budget_ci);

	dev->ext_priv = budget_ci;

	err = ttpci_budget_init(&budget_ci->budget, dev, info, THIS_MODULE,
				adapter_nr);
	if (err)
		goto out2;

	err = msp430_ir_init(budget_ci);
	if (err)
		goto out3;

	ciintf_init(budget_ci);

	budget_ci->budget.dvb_adapter.priv = budget_ci;
	frontend_init(budget_ci);

	ttpci_budget_init_hooks(&budget_ci->budget);

	return 0;

out3:
	ttpci_budget_deinit(&budget_ci->budget);
out2:
	kfree(budget_ci);
out1:
	return err;
}

static int budget_ci_detach(struct saa7146_dev *dev)
{
	struct budget_ci *budget_ci = (struct budget_ci *) dev->ext_priv;
	struct saa7146_dev *saa = budget_ci->budget.dev;
	int err;

	if (budget_ci->budget.ci_present)
		ciintf_deinit(budget_ci);
	msp430_ir_deinit(budget_ci);
	if (budget_ci->budget.dvb_frontend) {
		dvb_unregister_frontend(budget_ci->budget.dvb_frontend);
		dvb_frontend_detach(budget_ci->budget.dvb_frontend);
	}
	err = ttpci_budget_deinit(&budget_ci->budget);

	// disable frontend and CI interface
	saa7146_setgpio(saa, 2, SAA7146_GPIO_INPUT);

	kfree(budget_ci);

	return err;
}

static struct saa7146_extension budget_extension;

MAKE_BUDGET_INFO(ttbs2, "TT-Budget/S-1500 PCI", BUDGET_TT);
MAKE_BUDGET_INFO(ttbci, "TT-Budget/WinTV-NOVA-CI PCI", BUDGET_TT_HW_DISEQC);
MAKE_BUDGET_INFO(ttbt2, "TT-Budget/WinTV-NOVA-T	 PCI", BUDGET_TT);
MAKE_BUDGET_INFO(ttbtci, "TT-Budget-T-CI PCI", BUDGET_TT);
MAKE_BUDGET_INFO(ttbcci, "TT-Budget-C-CI PCI", BUDGET_TT);
MAKE_BUDGET_INFO(ttc1501, "TT-Budget C-1501 PCI", BUDGET_TT);
MAKE_BUDGET_INFO(tt3200, "TT-Budget S2-3200 PCI", BUDGET_TT);

static struct pci_device_id pci_tbl[] = {
	MAKE_EXTENSION_PCI(ttbci, 0x13c2, 0x100c),
	MAKE_EXTENSION_PCI(ttbci, 0x13c2, 0x100f),
	MAKE_EXTENSION_PCI(ttbcci, 0x13c2, 0x1010),
	MAKE_EXTENSION_PCI(ttbt2, 0x13c2, 0x1011),
	MAKE_EXTENSION_PCI(ttbtci, 0x13c2, 0x1012),
	MAKE_EXTENSION_PCI(ttbs2, 0x13c2, 0x1017),
	MAKE_EXTENSION_PCI(ttc1501, 0x13c2, 0x101a),
	MAKE_EXTENSION_PCI(tt3200, 0x13c2, 0x1019),
	{
	 .vendor = 0,
	 }
};

MODULE_DEVICE_TABLE(pci, pci_tbl);

static struct saa7146_extension budget_extension = {
	.name = "budget_ci dvb",
	.flags = SAA7146_USE_I2C_IRQ,

	.module = THIS_MODULE,
	.pci_tbl = &pci_tbl[0],
	.attach = budget_ci_attach,
	.detach = budget_ci_detach,

	.irq_mask = MASK_03 | MASK_06 | MASK_10,
	.irq_func = budget_ci_irq,
};

static int __init budget_ci_init(void)
{
	return saa7146_register_extension(&budget_extension);
}

static void __exit budget_ci_exit(void)
{
	saa7146_unregister_extension(&budget_extension);
}

module_init(budget_ci_init);
module_exit(budget_ci_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Michael Hunold, Jack Thomasson, Andrew de Quincey, others");
MODULE_DESCRIPTION("driver for the SAA7146 based so-called "
		   "budget PCI DVB cards w/ CI-module produced by "
		   "Siemens, Technotrend, Hauppauge");
