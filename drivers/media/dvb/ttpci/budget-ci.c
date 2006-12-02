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

#include "budget.h"

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/spinlock.h>
#include <media/ir-common.h>

#include "dvb_ca_en50221.h"
#include "stv0299.h"
#include "stv0297.h"
#include "tda1004x.h"
#include "lnbp21.h"
#include "bsbe1.h"
#include "bsru6.h"

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

/* Milliseconds during which key presses are regarded as key repeat and during
 * which the debounce logic is active
 */
#define IR_REPEAT_TIMEOUT	350

/* RC5 device wildcard */
#define IR_DEVICE_ANY		255

/* Some remotes sends multiple sequences per keypress (e.g. Zenith sends two),
 * this setting allows the superflous sequences to be ignored
 */
static int debounce = 0;
module_param(debounce, int, 0644);
MODULE_PARM_DESC(debounce, "ignore repeated IR sequences (default: 0 = ignore no sequences)");

static int rc5_device = -1;
module_param(rc5_device, int, 0644);
MODULE_PARM_DESC(rc5_device, "only IR commands to given RC5 device (device = 0 - 31, any device = 255, default: autodetect)");

static int ir_debug = 0;
module_param(ir_debug, int, 0644);
MODULE_PARM_DESC(ir_debug, "enable debugging information for IR decoding");

struct budget_ci_ir {
	struct input_dev *dev;
	struct tasklet_struct msp430_irq_tasklet;
	char name[72]; /* 40 + 32 for (struct saa7146_dev).name */
	char phys[32];
	struct ir_input_state state;
	int rc5_device;
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
	static int bounces = 0;
	int device;
	int toggle;
	static int prev_toggle = -1;
	static u32 ir_key;
	u32 command = ttpci_budget_debiread(&budget_ci->budget, DEBINOSWAP, DEBIADDR_IR, 2, 1, 0) >> 8;

	/*
	 * The msp430 chip can generate two different bytes, command and device
	 *
	 * type1: X1CCCCCC, C = command bits (0 - 63)
	 * type2: X0TDDDDD, D = device bits (0 - 31), T = RC5 toggle bit
	 *
	 * More than one command byte may be generated before the device byte
	 * Only when we have both, a correct keypress is generated
	 */

	/* Is this a RC5 command byte? */
	if (command & 0x40) {
		if (ir_debug)
			printk("budget_ci: received command byte 0x%02x\n", command);
		ir_key = command & 0x3f;
		return;
	}

	/* It's a RC5 device byte */
	if (ir_debug)
		printk("budget_ci: received device byte 0x%02x\n", command);
	device = command & 0x1f;
	toggle = command & 0x20;

	if (budget_ci->ir.rc5_device != IR_DEVICE_ANY && budget_ci->ir.rc5_device != device)
		return;

	/* Ignore repeated key sequences if requested */
	if (toggle == prev_toggle && ir_key == dev->repeat_key &&
	    bounces > 0 && timer_pending(&dev->timer)) {
		if (ir_debug)
			printk("budget_ci: debounce logic ignored IR command\n");
		bounces--;
		return;
	}
	prev_toggle = toggle;

	/* Are we still waiting for a keyup event? */
	if (del_timer(&dev->timer))
		ir_input_nokey(dev, &budget_ci->ir.state);

	/* Generate keypress */
	if (ir_debug)
		printk("budget_ci: generating keypress 0x%02x\n", ir_key);
	ir_input_keydown(dev, &budget_ci->ir.state, ir_key, (ir_key & (command << 8)));

	/* Do we want to delay the keyup event? */
	if (debounce) {
		bounces = debounce;
		mod_timer(&dev->timer, jiffies + msecs_to_jiffies(IR_REPEAT_TIMEOUT));
	} else {
		ir_input_nokey(dev, &budget_ci->ir.state);
	}
}

static void msp430_ir_debounce(unsigned long data)
{
	struct input_dev *dev = (struct input_dev *) data;

	if (dev->rep[0] == 0 || dev->rep[0] == ~0) {
		input_event(dev, EV_KEY, key_map[dev->repeat_key], 0);
	} else {
		dev->rep[0] = 0;
		dev->timer.expires = jiffies + HZ * 350 / 1000;
		add_timer(&dev->timer);
		input_event(dev, EV_KEY, key_map[dev->repeat_key], 2);	/* REPEAT */
	}
	input_sync(dev);
}

static void msp430_ir_interrupt(unsigned long data)
{
	struct budget_ci *budget_ci = (struct budget_ci *) data;
	struct input_dev *dev = budget_ci->ir.dev;
	unsigned int code =
		ttpci_budget_debiread(&budget_ci->budget, DEBINOSWAP, DEBIADDR_IR, 2, 1, 0) >> 8;

	if (code & 0x40) {
		code &= 0x3f;

		if (timer_pending(&dev->timer)) {
			if (code == dev->repeat_key) {
				++dev->rep[0];
				return;
			}
			del_timer(&dev->timer);
			input_event(dev, EV_KEY, key_map[dev->repeat_key], 0);
		}

		if (!key_map[code]) {
			printk("DVB (%s): no key for %02x!\n", __FUNCTION__, code);
			return;
		}

		input_event(dev, EV_KEY, key_map[code], 1);
		input_sync(dev);

		/* initialize debounce and repeat */
		dev->repeat_key = code;
		/* Zenith remote _always_ sends 2 sequences */
		dev->rep[0] = ~0;
		mod_timer(&dev->timer, jiffies + msecs_to_jiffies(350));
	}
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
# if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,15)
	input_dev->cdev.dev = &saa->pci->dev;
# else
	input_dev->dev = &saa->pci->dev;
# endif

	/* Select keymap and address */
	switch (budget_ci->budget.dev->pci->subsystem_device) {
	case 0x100c:
	case 0x100f:
	case 0x1010:
	case 0x1011:
	case 0x1012:
	case 0x1017:
		/* The hauppauge keymap is a superset of these remotes */
		ir_input_init(input_dev, &budget_ci->ir.state,
			      IR_TYPE_RC5, ir_codes_hauppauge_new);

		if (rc5_device < 0)
			budget_ci->ir.rc5_device = 0x1f;
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

	/* initialise the key-up debounce timeout handler */
	input_dev->timer.function = msp430_ir_keyup;
	input_dev->timer.data = (unsigned long) &budget_ci->ir;

	error = input_register_device(input_dev);
	if (error) {
		printk(KERN_ERR "budget_ci: could not init driver for IR device (code %d)\n", error);
		goto out2;
	}

	tasklet_init(&budget_ci->ir.msp430_irq_tasklet, msp430_ir_interrupt,
		     (unsigned long) budget_ci);

	saa7146_write(saa, IER, saa7146_read(saa, IER) | MASK_06);
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

	saa7146_write(saa, IER, saa7146_read(saa, IER) & ~MASK_06);
	saa7146_setgpio(saa, 3, SAA7146_GPIO_INPUT);
	tasklet_kill(&budget_ci->ir.msp430_irq_tasklet);

	if (del_timer(&dev->timer)) {
		ir_input_nokey(dev, &budget_ci->ir.state);
		input_sync(dev);
	}

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
	saa7146_write(saa, MC1, saa7146_read(saa, MC1) | (0x800 << 16) | 0x800);

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
		saa7146_write(saa, IER, saa7146_read(saa, IER) | MASK_03);
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
	saa7146_write(saa, MC1, saa7146_read(saa, MC1) | (0x800 << 16));
	return result;
}

static void ciintf_deinit(struct budget_ci *budget_ci)
{
	struct saa7146_dev *saa = budget_ci->budget.dev;

	// disable CI interrupts
	if (budget_ci->ci_irq) {
		saa7146_write(saa, IER, saa7146_read(saa, IER) & ~MASK_03);
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
	saa7146_write(saa, MC1, saa7146_read(saa, MC1) | (0x800 << 16));
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
	.lock_output = STV0229_LOCKOUTPUT_1,
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
		philips_tdm1316l_config.invert = 1;
		budget_ci->budget.dvb_frontend =
			dvb_attach(tda10046_attach, &philips_tdm1316l_config, &budget_ci->budget.i2c_adap);
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
				printk("%s: No LNBP21 found!\n", __FUNCTION__);
				dvb_frontend_detach(budget_ci->budget.dvb_frontend);
				budget_ci->budget.dvb_frontend = NULL;
			}
		}

		break;
	}

	if (budget_ci->budget.dvb_frontend == NULL) {
		printk("budget-ci: A frontend driver was not found for device %04x/%04x subsystem %04x/%04x\n",
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

	err = ttpci_budget_init(&budget_ci->budget, dev, info, THIS_MODULE);
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

static struct pci_device_id pci_tbl[] = {
	MAKE_EXTENSION_PCI(ttbci, 0x13c2, 0x100c),
	MAKE_EXTENSION_PCI(ttbci, 0x13c2, 0x100f),
	MAKE_EXTENSION_PCI(ttbcci, 0x13c2, 0x1010),
	MAKE_EXTENSION_PCI(ttbt2, 0x13c2, 0x1011),
	MAKE_EXTENSION_PCI(ttbtci, 0x13c2, 0x1012),
	MAKE_EXTENSION_PCI(ttbs2, 0x13c2, 0x1017),
	{
	 .vendor = 0,
	 }
};

MODULE_DEVICE_TABLE(pci, pci_tbl);

static struct saa7146_extension budget_extension = {
	.name = "budget_ci dvb",
	.flags = SAA7146_I2C_SHORT_DELAY,

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
