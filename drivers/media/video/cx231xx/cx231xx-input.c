/*
  handle cx231xx IR remotes via linux kernel input layer.

  Copyright (C) 2008 <srinivasa.deevi at conexant dot com>
		Based on em28xx driver

		< This is a place holder for IR now.>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/usb.h>
#include <linux/slab.h>

#include "cx231xx.h"

static unsigned int ir_debug;
module_param(ir_debug, int, 0644);
MODULE_PARM_DESC(ir_debug, "enable debug messages [IR]");

#define MODULE_NAME "cx231xx"

#define i2cdprintk(fmt, arg...) \
	if (ir_debug) { \
		printk(KERN_DEBUG "%s/ir: " fmt, ir->name , ## arg); \
	}

#define dprintk(fmt, arg...) \
	if (ir_debug) { \
		printk(KERN_DEBUG "%s/ir: " fmt, ir->name , ## arg); \
	}

/**********************************************************
 Polling structure used by cx231xx IR's
 **********************************************************/

struct cx231xx_ir_poll_result {
	unsigned int toggle_bit:1;
	unsigned int read_count:7;
	u8 rc_address;
	u8 rc_data[4];
};

struct cx231xx_IR {
	struct cx231xx *dev;
	struct input_dev *input;
	char name[32];
	char phys[32];

	/* poll external decoder */
	int polling;
	struct work_struct work;
	struct timer_list timer;
	unsigned int last_readcount;

	int (*get_key) (struct cx231xx_IR *, struct cx231xx_ir_poll_result *);
};

/**********************************************************
 Polling code for cx231xx
 **********************************************************/

static void cx231xx_ir_handle_key(struct cx231xx_IR *ir)
{
	int result;
	struct cx231xx_ir_poll_result poll_result;

	/* read the registers containing the IR status */
	result = ir->get_key(ir, &poll_result);
	if (result < 0) {
		dprintk("ir->get_key() failed %d\n", result);
		return;
	}

	dprintk("ir->get_key result tb=%02x rc=%02x lr=%02x data=%02x\n",
		poll_result.toggle_bit, poll_result.read_count,
		ir->last_readcount, poll_result.rc_data[0]);

	if (poll_result.read_count > 0 &&
	    poll_result.read_count != ir->last_readcount)
		ir_keydown(ir->input,
			   poll_result.rc_data[0],
			   poll_result.toggle_bit);

	if (ir->dev->chip_id == CHIP_ID_EM2874)
		/* The em2874 clears the readcount field every time the
		   register is read.  The em2860/2880 datasheet says that it
		   is supposed to clear the readcount, but it doesn't.  So with
		   the em2874, we are looking for a non-zero read count as
		   opposed to a readcount that is incrementing */
		ir->last_readcount = 0;
	else
		ir->last_readcount = poll_result.read_count;

	}
}

static void ir_timer(unsigned long data)
{
	struct cx231xx_IR *ir = (struct cx231xx_IR *)data;

	schedule_work(&ir->work);
}

static void cx231xx_ir_work(struct work_struct *work)
{
	struct cx231xx_IR *ir = container_of(work, struct cx231xx_IR, work);

	cx231xx_ir_handle_key(ir);
	mod_timer(&ir->timer, jiffies + msecs_to_jiffies(ir->polling));
}

void cx231xx_ir_start(struct cx231xx_IR *ir)
{
	setup_timer(&ir->timer, ir_timer, (unsigned long)ir);
	INIT_WORK(&ir->work, cx231xx_ir_work);
	schedule_work(&ir->work);
}

static void cx231xx_ir_stop(struct cx231xx_IR *ir)
{
	del_timer_sync(&ir->timer);
	flush_scheduled_work();
}

int cx231xx_ir_init(struct cx231xx *dev)
{
	struct cx231xx_IR *ir;
	struct input_dev *input_dev;
	u8 ir_config;
	int err = -ENOMEM;

	if (dev->board.ir_codes == NULL) {
		/* No remote control support */
		return 0;
	}

	ir = kzalloc(sizeof(*ir), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!ir || !input_dev)
		goto err_out_free;

	ir->input = input_dev;

	/* Setup the proper handler based on the chip */
	switch (dev->chip_id) {
	default:
		printk("Unrecognized cx231xx chip id: IR not supported\n");
		goto err_out_free;
	}

	/* This is how often we ask the chip for IR information */
	ir->polling = 100;	/* ms */

	/* init input device */
	snprintf(ir->name, sizeof(ir->name), "cx231xx IR (%s)", dev->name);

	usb_make_path(dev->udev, ir->phys, sizeof(ir->phys));
	strlcat(ir->phys, "/input0", sizeof(ir->phys));

	input_dev->name = ir->name;
	input_dev->phys = ir->phys;
	input_dev->id.bustype = BUS_USB;
	input_dev->id.version = 1;
	input_dev->id.vendor = le16_to_cpu(dev->udev->descriptor.idVendor);
	input_dev->id.product = le16_to_cpu(dev->udev->descriptor.idProduct);

	input_dev->dev.parent = &dev->udev->dev;
	/* record handles to ourself */
	ir->dev = dev;
	dev->ir = ir;

	cx231xx_ir_start(ir);

	/* all done */
	err = __ir_input_register(ir->input, dev->board.ir_codes,
				NULL, MODULE_NAME);
	if (err)
		goto err_out_stop;

	return 0;
err_out_stop:
	cx231xx_ir_stop(ir);
	dev->ir = NULL;
err_out_free:
	kfree(ir);
	return err;
}

int cx231xx_ir_fini(struct cx231xx *dev)
{
	struct cx231xx_IR *ir = dev->ir;

	/* skip detach on non attached boards */
	if (!ir)
		return 0;

	cx231xx_ir_stop(ir);
	ir_input_unregister(ir->input);
	kfree(ir);

	/* done */
	dev->ir = NULL;
	return 0;
}
