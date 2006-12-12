/*
 *
 * Copyright (c) 2003 Gerd Knorr
 * Copyright (c) 2003 Pavel Machek
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/input.h>

#include "bttv.h"
#include "bttvp.h"


static int debug;
module_param(debug, int, 0644);    /* debug level (0,1,2) */
static int repeat_delay = 500;
module_param(repeat_delay, int, 0644);
static int repeat_period = 33;
module_param(repeat_period, int, 0644);

#define DEVNAME "bttv-input"

/* ---------------------------------------------------------------------- */

static void ir_handle_key(struct bttv *btv)
{
	struct bttv_ir *ir = btv->remote;
	u32 gpio,data;

	/* read gpio value */
	gpio = bttv_gpio_read(&btv->c);
	if (ir->polling) {
		if (ir->last_gpio == gpio)
			return;
		ir->last_gpio = gpio;
	}

	/* extract data */
	data = ir_extract_bits(gpio, ir->mask_keycode);
	dprintk(KERN_INFO DEVNAME ": irq gpio=0x%x code=%d | %s%s%s\n",
		gpio, data,
		ir->polling               ? "poll"  : "irq",
		(gpio & ir->mask_keydown) ? " down" : "",
		(gpio & ir->mask_keyup)   ? " up"   : "");

	if ((ir->mask_keydown  &&  (0 != (gpio & ir->mask_keydown))) ||
	    (ir->mask_keyup    &&  (0 == (gpio & ir->mask_keyup)))) {
		ir_input_keydown(ir->dev,&ir->ir,data,data);
	} else {
		ir_input_nokey(ir->dev,&ir->ir);
	}

}

void bttv_input_irq(struct bttv *btv)
{
	struct bttv_ir *ir = btv->remote;

	if (!ir->polling)
		ir_handle_key(btv);
}

static void bttv_input_timer(unsigned long data)
{
	struct bttv *btv = (struct bttv*)data;
	struct bttv_ir *ir = btv->remote;
	unsigned long timeout;

	ir_handle_key(btv);
	timeout = jiffies + (ir->polling * HZ / 1000);
	mod_timer(&ir->timer, timeout);
}

/* ---------------------------------------------------------------*/

static int rc5_remote_gap = 885;
module_param(rc5_remote_gap, int, 0644);
static int rc5_key_timeout = 200;
module_param(rc5_key_timeout, int, 0644);

#define RC5_START(x)	(((x)>>12)&3)
#define RC5_TOGGLE(x)	(((x)>>11)&1)
#define RC5_ADDR(x)	(((x)>>6)&31)
#define RC5_INSTR(x)	((x)&63)

/* decode raw bit pattern to RC5 code */
static u32 rc5_decode(unsigned int code)
{
	unsigned int org_code = code;
	unsigned int pair;
	unsigned int rc5 = 0;
	int i;

	code = (code << 1) | 1;
	for (i = 0; i < 14; ++i) {
		pair = code & 0x3;
		code >>= 2;

		rc5 <<= 1;
		switch (pair) {
		case 0:
		case 2:
			break;
		case 1:
			rc5 |= 1;
			break;
		case 3:
			dprintk(KERN_WARNING "bad code: %x\n", org_code);
			return 0;
		}
	}
	dprintk(KERN_WARNING "code=%x, rc5=%x, start=%x, toggle=%x, address=%x, "
		"instr=%x\n", rc5, org_code, RC5_START(rc5),
		RC5_TOGGLE(rc5), RC5_ADDR(rc5), RC5_INSTR(rc5));
	return rc5;
}

static int bttv_rc5_irq(struct bttv *btv)
{
	struct bttv_ir *ir = btv->remote;
	struct timeval tv;
	u32 gpio;
	u32 gap;
	unsigned long current_jiffies, timeout;

	/* read gpio port */
	gpio = bttv_gpio_read(&btv->c);

	/* remote IRQ? */
	if (!(gpio & 0x20))
		return 0;

	/* get time of bit */
	current_jiffies = jiffies;
	do_gettimeofday(&tv);

	/* avoid overflow with gap >1s */
	if (tv.tv_sec - ir->base_time.tv_sec > 1) {
		gap = 200000;
	} else {
		gap = 1000000 * (tv.tv_sec - ir->base_time.tv_sec) +
		    tv.tv_usec - ir->base_time.tv_usec;
	}

	/* active code => add bit */
	if (ir->active) {
		/* only if in the code (otherwise spurious IRQ or timer
		   late) */
		if (ir->last_bit < 28) {
			ir->last_bit = (gap - rc5_remote_gap / 2) /
			    rc5_remote_gap;
			ir->code |= 1 << ir->last_bit;
		}
		/* starting new code */
	} else {
		ir->active = 1;
		ir->code = 0;
		ir->base_time = tv;
		ir->last_bit = 0;

		timeout = current_jiffies + (500 + 30 * HZ) / 1000;
		mod_timer(&ir->timer_end, timeout);
	}

	/* toggle GPIO pin 4 to reset the irq */
	bttv_gpio_write(&btv->c, gpio & ~(1 << 4));
	bttv_gpio_write(&btv->c, gpio | (1 << 4));
	return 1;
}


static void bttv_rc5_timer_end(unsigned long data)
{
	struct bttv_ir *ir = (struct bttv_ir *)data;
	struct timeval tv;
	unsigned long current_jiffies, timeout;
	u32 gap;

	/* get time */
	current_jiffies = jiffies;
	do_gettimeofday(&tv);

	/* avoid overflow with gap >1s */
	if (tv.tv_sec - ir->base_time.tv_sec > 1) {
		gap = 200000;
	} else {
		gap = 1000000 * (tv.tv_sec - ir->base_time.tv_sec) +
		    tv.tv_usec - ir->base_time.tv_usec;
	}

	/* Allow some timmer jitter (RC5 is ~24ms anyway so this is ok) */
	if (gap < 28000) {
		dprintk(KERN_WARNING "spurious timer_end\n");
		return;
	}

	ir->active = 0;
	if (ir->last_bit < 20) {
		/* ignore spurious codes (caused by light/other remotes) */
		dprintk(KERN_WARNING "short code: %x\n", ir->code);
	} else {
		u32 rc5 = rc5_decode(ir->code);

		/* two start bits? */
		if (RC5_START(rc5) != 3) {
			dprintk(KERN_WARNING "rc5 start bits invalid: %u\n", RC5_START(rc5));

			/* right address? */
		} else if (RC5_ADDR(rc5) == 0x0) {
			u32 toggle = RC5_TOGGLE(rc5);
			u32 instr = RC5_INSTR(rc5);

			/* Good code, decide if repeat/repress */
			if (toggle != RC5_TOGGLE(ir->last_rc5) ||
			    instr != RC5_INSTR(ir->last_rc5)) {
				dprintk(KERN_WARNING "instruction %x, toggle %x\n", instr,
					toggle);
				ir_input_nokey(ir->dev, &ir->ir);
				ir_input_keydown(ir->dev, &ir->ir, instr,
						 instr);
			}

			/* Set/reset key-up timer */
			timeout = current_jiffies + (500 + rc5_key_timeout
						     * HZ) / 1000;
			mod_timer(&ir->timer_keyup, timeout);

			/* Save code for repeat test */
			ir->last_rc5 = rc5;
		}
	}
}

static void bttv_rc5_timer_keyup(unsigned long data)
{
	struct bttv_ir *ir = (struct bttv_ir *)data;

	dprintk(KERN_DEBUG "key released\n");
	ir_input_nokey(ir->dev, &ir->ir);
}

/* ---------------------------------------------------------------------- */

static void bttv_ir_start(struct bttv *btv, struct bttv_ir *ir)
{
	if (ir->polling) {
		init_timer(&ir->timer);
		ir->timer.function = bttv_input_timer;
		ir->timer.data     = (unsigned long)btv;
		ir->timer.expires  = jiffies + HZ;
		add_timer(&ir->timer);
	} else if (ir->rc5_gpio) {
		/* set timer_end for code completion */
		init_timer(&ir->timer_end);
		ir->timer_end.function = bttv_rc5_timer_end;
		ir->timer_end.data = (unsigned long)ir;

		init_timer(&ir->timer_keyup);
		ir->timer_keyup.function = bttv_rc5_timer_keyup;
		ir->timer_keyup.data = (unsigned long)ir;
	}
}

static void bttv_ir_stop(struct bttv *btv)
{
	if (btv->remote->polling) {
		del_timer_sync(&btv->remote->timer);
		flush_scheduled_work();
	}

	if (btv->remote->rc5_gpio) {
		u32 gpio;

		del_timer_sync(&btv->remote->timer_end);
		flush_scheduled_work();

		gpio = bttv_gpio_read(&btv->c);
		bttv_gpio_write(&btv->c, gpio & ~(1 << 4));
	}
}

int bttv_input_init(struct bttv *btv)
{
	struct bttv_ir *ir;
	IR_KEYTAB_TYPE *ir_codes = NULL;
	struct input_dev *input_dev;
	int ir_type = IR_TYPE_OTHER;
	int err = -ENOMEM;

	if (!btv->has_remote)
		return -ENODEV;

	ir = kzalloc(sizeof(*ir),GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!ir || !input_dev)
		goto err_out_free;

	/* detect & configure */
	switch (btv->c.type) {
	case BTTV_BOARD_AVERMEDIA:
	case BTTV_BOARD_AVPHONE98:
	case BTTV_BOARD_AVERMEDIA98:
		ir_codes         = ir_codes_avermedia;
		ir->mask_keycode = 0xf88000;
		ir->mask_keydown = 0x010000;
		ir->polling      = 50; // ms
		break;

	case BTTV_BOARD_AVDVBT_761:
	case BTTV_BOARD_AVDVBT_771:
		ir_codes         = ir_codes_avermedia_dvbt;
		ir->mask_keycode = 0x0f00c0;
		ir->mask_keydown = 0x000020;
		ir->polling      = 50; // ms
		break;

	case BTTV_BOARD_PXELVWPLTVPAK:
		ir_codes         = ir_codes_pixelview;
		ir->mask_keycode = 0x003e00;
		ir->mask_keyup   = 0x010000;
		ir->polling      = 50; // ms
		break;
	case BTTV_BOARD_PV_M4900:
	case BTTV_BOARD_PV_BT878P_9B:
	case BTTV_BOARD_PV_BT878P_PLUS:
		ir_codes         = ir_codes_pixelview;
		ir->mask_keycode = 0x001f00;
		ir->mask_keyup   = 0x008000;
		ir->polling      = 50; // ms
		break;

	case BTTV_BOARD_WINFAST2000:
		ir_codes         = ir_codes_winfast;
		ir->mask_keycode = 0x1f8;
		break;
	case BTTV_BOARD_MAGICTVIEW061:
	case BTTV_BOARD_MAGICTVIEW063:
		ir_codes         = ir_codes_winfast;
		ir->mask_keycode = 0x0008e000;
		ir->mask_keydown = 0x00200000;
		break;
	case BTTV_BOARD_APAC_VIEWCOMP:
		ir_codes         = ir_codes_apac_viewcomp;
		ir->mask_keycode = 0x001f00;
		ir->mask_keyup   = 0x008000;
		ir->polling      = 50; // ms
		break;
	case BTTV_BOARD_CONCEPTRONIC_CTVFMI2:
	case BTTV_BOARD_CONTVFMI:
		ir_codes         = ir_codes_pixelview;
		ir->mask_keycode = 0x001F00;
		ir->mask_keyup   = 0x006000;
		ir->polling      = 50; // ms
		break;
	case BTTV_BOARD_NEBULA_DIGITV:
		ir_codes = ir_codes_nebula;
		btv->custom_irq = bttv_rc5_irq;
		ir->rc5_gpio = 1;
		break;
	case BTTV_BOARD_MACHTV_MAGICTV:
		ir_codes         = ir_codes_apac_viewcomp;
		ir->mask_keycode = 0x001F00;
		ir->mask_keyup   = 0x004000;
		ir->polling      = 50; /* ms */
		break;
	}
	if (NULL == ir_codes) {
		dprintk(KERN_INFO "Ooops: IR config error [card=%d]\n", btv->c.type);
		err = -ENODEV;
		goto err_out_free;
	}

	if (ir->rc5_gpio) {
		u32 gpio;
		/* enable remote irq */
		bttv_gpio_inout(&btv->c, (1 << 4), 1 << 4);
		gpio = bttv_gpio_read(&btv->c);
		bttv_gpio_write(&btv->c, gpio & ~(1 << 4));
		bttv_gpio_write(&btv->c, gpio | (1 << 4));
	} else {
		/* init hardware-specific stuff */
		bttv_gpio_inout(&btv->c, ir->mask_keycode | ir->mask_keydown, 0);
	}

	/* init input device */
	ir->dev = input_dev;

	snprintf(ir->name, sizeof(ir->name), "bttv IR (card=%d)",
		 btv->c.type);
	snprintf(ir->phys, sizeof(ir->phys), "pci-%s/ir0",
		 pci_name(btv->c.pci));

	ir_input_init(input_dev, &ir->ir, ir_type, ir_codes);
	input_dev->name = ir->name;
	input_dev->phys = ir->phys;
	input_dev->id.bustype = BUS_PCI;
	input_dev->id.version = 1;
	if (btv->c.pci->subsystem_vendor) {
		input_dev->id.vendor  = btv->c.pci->subsystem_vendor;
		input_dev->id.product = btv->c.pci->subsystem_device;
	} else {
		input_dev->id.vendor  = btv->c.pci->vendor;
		input_dev->id.product = btv->c.pci->device;
	}
	input_dev->cdev.dev = &btv->c.pci->dev;

	btv->remote = ir;
	bttv_ir_start(btv, ir);

	/* all done */
	err = input_register_device(btv->remote->dev);
	if (err)
		goto err_out_stop;

	/* the remote isn't as bouncy as a keyboard */
	ir->dev->rep[REP_DELAY] = repeat_delay;
	ir->dev->rep[REP_PERIOD] = repeat_period;

	return 0;

 err_out_stop:
	bttv_ir_stop(btv);
	btv->remote = NULL;
 err_out_free:
	input_free_device(input_dev);
	kfree(ir);
	return err;
}

void bttv_input_fini(struct bttv *btv)
{
	if (btv->remote == NULL)
		return;

	bttv_ir_stop(btv);
	input_unregister_device(btv->remote->dev);
	kfree(btv->remote);
	btv->remote = NULL;
}


/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
