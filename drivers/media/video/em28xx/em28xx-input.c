/*
  handle em28xx IR remotes via linux kernel input layer.

   Copyright (C) 2005 Ludovico Cavedon <cavedon@sssup.it>
		      Markus Rechberger <mrechberger@gmail.com>
		      Mauro Carvalho Chehab <mchehab@infradead.org>
		      Sascha Sommer <saschasommer@freenet.de>

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

#include "em28xx.h"

#define EM28XX_SNAPSHOT_KEY KEY_CAMERA
#define EM28XX_SBUTTON_QUERY_INTERVAL 500
#define EM28XX_R0C_USBSUSP_SNAPSHOT 0x20

static unsigned int ir_debug;
module_param(ir_debug, int, 0644);
MODULE_PARM_DESC(ir_debug, "enable debug messages [IR]");

#define i2cdprintk(fmt, arg...) \
	if (ir_debug) { \
		printk(KERN_DEBUG "%s/ir: " fmt, ir->name , ## arg); \
	}

#define dprintk(fmt, arg...) \
	if (ir_debug) { \
		printk(KERN_DEBUG "%s/ir: " fmt, ir->name , ## arg); \
	}

/**********************************************************
 Polling structure used by em28xx IR's
 **********************************************************/

struct em28xx_ir_poll_result {
	unsigned int toggle_bit:1;
	unsigned int read_count:7;
	u8 rc_address;
	u8 rc_data[4]; /* 1 byte on em2860/2880, 4 on em2874 */
};

struct em28xx_IR {
	struct em28xx *dev;
	struct input_dev *input;
	struct ir_input_state ir;
	char name[32];
	char phys[32];

	/* poll external decoder */
	int polling;
	struct delayed_work work;
	unsigned int last_toggle:1;
	unsigned int full_code:1;
	unsigned int last_readcount;
	unsigned int repeat_interval;

	int  (*get_key)(struct em28xx_IR *, struct em28xx_ir_poll_result *);

	/* IR device properties */

	struct ir_dev_props props;
};

/**********************************************************
 I2C IR based get keycodes - should be used with ir-kbd-i2c
 **********************************************************/

int em28xx_get_key_terratec(struct IR_i2c *ir, u32 *ir_key, u32 *ir_raw)
{
	unsigned char b;

	/* poll IR chip */
	if (1 != i2c_master_recv(ir->c, &b, 1)) {
		i2cdprintk("read error\n");
		return -EIO;
	}

	/* it seems that 0xFE indicates that a button is still hold
	   down, while 0xff indicates that no button is hold
	   down. 0xfe sequences are sometimes interrupted by 0xFF */

	i2cdprintk("key %02x\n", b);

	if (b == 0xff)
		return 0;

	if (b == 0xfe)
		/* keep old data */
		return 1;

	*ir_key = b;
	*ir_raw = b;
	return 1;
}

int em28xx_get_key_em_haup(struct IR_i2c *ir, u32 *ir_key, u32 *ir_raw)
{
	unsigned char buf[2];
	u16 code;
	int size;

	/* poll IR chip */
	size = i2c_master_recv(ir->c, buf, sizeof(buf));

	if (size != 2)
		return -EIO;

	/* Does eliminate repeated parity code */
	if (buf[1] == 0xff)
		return 0;

	ir->old = buf[1];

	/*
	 * Rearranges bits to the right order.
	 * The bit order were determined experimentally by using
	 * The original Hauppauge Grey IR and another RC5 that uses addr=0x08
	 * The RC5 code has 14 bits, but we've experimentally determined
	 * the meaning for only 11 bits.
	 * So, the code translation is not complete. Yet, it is enough to
	 * work with the provided RC5 IR.
	 */
	code =
		 ((buf[0] & 0x01) ? 0x0020 : 0) | /* 		0010 0000 */
		 ((buf[0] & 0x02) ? 0x0010 : 0) | /* 		0001 0000 */
		 ((buf[0] & 0x04) ? 0x0008 : 0) | /* 		0000 1000 */
		 ((buf[0] & 0x08) ? 0x0004 : 0) | /* 		0000 0100 */
		 ((buf[0] & 0x10) ? 0x0002 : 0) | /* 		0000 0010 */
		 ((buf[0] & 0x20) ? 0x0001 : 0) | /* 		0000 0001 */
		 ((buf[1] & 0x08) ? 0x1000 : 0) | /* 0001 0000		  */
		 ((buf[1] & 0x10) ? 0x0800 : 0) | /* 0000 1000		  */
		 ((buf[1] & 0x20) ? 0x0400 : 0) | /* 0000 0100		  */
		 ((buf[1] & 0x40) ? 0x0200 : 0) | /* 0000 0010		  */
		 ((buf[1] & 0x80) ? 0x0100 : 0);  /* 0000 0001		  */

	i2cdprintk("ir hauppauge (em2840): code=0x%02x (rcv=0x%02x%02x)\n",
			code, buf[1], buf[0]);

	/* return key */
	*ir_key = code;
	*ir_raw = code;
	return 1;
}

int em28xx_get_key_pinnacle_usb_grey(struct IR_i2c *ir, u32 *ir_key,
				     u32 *ir_raw)
{
	unsigned char buf[3];

	/* poll IR chip */

	if (3 != i2c_master_recv(ir->c, buf, 3)) {
		i2cdprintk("read error\n");
		return -EIO;
	}

	i2cdprintk("key %02x\n", buf[2]&0x3f);
	if (buf[0] != 0x00)
		return 0;

	*ir_key = buf[2]&0x3f;
	*ir_raw = buf[2]&0x3f;

	return 1;
}

int em28xx_get_key_winfast_usbii_deluxe(struct IR_i2c *ir, u32 *ir_key, u32 *ir_raw)
{
	unsigned char subaddr, keydetect, key;

	struct i2c_msg msg[] = { { .addr = ir->c->addr, .flags = 0, .buf = &subaddr, .len = 1},

				{ .addr = ir->c->addr, .flags = I2C_M_RD, .buf = &keydetect, .len = 1} };

	subaddr = 0x10;
	if (2 != i2c_transfer(ir->c->adapter, msg, 2)) {
		i2cdprintk("read error\n");
		return -EIO;
	}
	if (keydetect == 0x00)
		return 0;

	subaddr = 0x00;
	msg[1].buf = &key;
	if (2 != i2c_transfer(ir->c->adapter, msg, 2)) {
		i2cdprintk("read error\n");
	return -EIO;
	}
	if (key == 0x00)
		return 0;

	*ir_key = key;
	*ir_raw = key;
	return 1;
}

/**********************************************************
 Poll based get keycode functions
 **********************************************************/

/* This is for the em2860/em2880 */
static int default_polling_getkey(struct em28xx_IR *ir,
				  struct em28xx_ir_poll_result *poll_result)
{
	struct em28xx *dev = ir->dev;
	int rc;
	u8 msg[3] = { 0, 0, 0 };

	/* Read key toggle, brand, and key code
	   on registers 0x45, 0x46 and 0x47
	 */
	rc = dev->em28xx_read_reg_req_len(dev, 0, EM28XX_R45_IR,
					  msg, sizeof(msg));
	if (rc < 0)
		return rc;

	/* Infrared toggle (Reg 0x45[7]) */
	poll_result->toggle_bit = (msg[0] >> 7);

	/* Infrared read count (Reg 0x45[6:0] */
	poll_result->read_count = (msg[0] & 0x7f);

	/* Remote Control Address (Reg 0x46) */
	poll_result->rc_address = msg[1];

	/* Remote Control Data (Reg 0x47) */
	poll_result->rc_data[0] = msg[2];

	return 0;
}

static int em2874_polling_getkey(struct em28xx_IR *ir,
				 struct em28xx_ir_poll_result *poll_result)
{
	struct em28xx *dev = ir->dev;
	int rc;
	u8 msg[5] = { 0, 0, 0, 0, 0 };

	/* Read key toggle, brand, and key code
	   on registers 0x51-55
	 */
	rc = dev->em28xx_read_reg_req_len(dev, 0, EM2874_R51_IR,
					  msg, sizeof(msg));
	if (rc < 0)
		return rc;

	/* Infrared toggle (Reg 0x51[7]) */
	poll_result->toggle_bit = (msg[0] >> 7);

	/* Infrared read count (Reg 0x51[6:0] */
	poll_result->read_count = (msg[0] & 0x7f);

	/* Remote Control Address (Reg 0x52) */
	poll_result->rc_address = msg[1];

	/* Remote Control Data (Reg 0x53-55) */
	poll_result->rc_data[0] = msg[2];
	poll_result->rc_data[1] = msg[3];
	poll_result->rc_data[2] = msg[4];

	return 0;
}

/**********************************************************
 Polling code for em28xx
 **********************************************************/

static void em28xx_ir_handle_key(struct em28xx_IR *ir)
{
	int result;
	int do_sendkey = 0;
	struct em28xx_ir_poll_result poll_result;

	/* read the registers containing the IR status */
	result = ir->get_key(ir, &poll_result);
	if (result < 0) {
		dprintk("ir->get_key() failed %d\n", result);
		return;
	}

	dprintk("ir->get_key result tb=%02x rc=%02x lr=%02x data=%02x%02x\n",
		poll_result.toggle_bit, poll_result.read_count,
		ir->last_readcount, poll_result.rc_address,
		poll_result.rc_data[0]);

	if (ir->dev->chip_id == CHIP_ID_EM2874) {
		/* The em2874 clears the readcount field every time the
		   register is read.  The em2860/2880 datasheet says that it
		   is supposed to clear the readcount, but it doesn't.  So with
		   the em2874, we are looking for a non-zero read count as
		   opposed to a readcount that is incrementing */
		ir->last_readcount = 0;
	}

	if (poll_result.read_count == 0) {
		/* The button has not been pressed since the last read */
	} else if (ir->last_toggle != poll_result.toggle_bit) {
		/* A button has been pressed */
		dprintk("button has been pressed\n");
		ir->last_toggle = poll_result.toggle_bit;
		ir->repeat_interval = 0;
		do_sendkey = 1;
	} else if (poll_result.toggle_bit == ir->last_toggle &&
		   poll_result.read_count > 0 &&
		   poll_result.read_count != ir->last_readcount) {
		/* The button is still being held down */
		dprintk("button being held down\n");

		/* Debouncer for first keypress */
		if (ir->repeat_interval++ > 9) {
			/* Start repeating after 1 second */
			do_sendkey = 1;
		}
	}

	if (do_sendkey) {
		dprintk("sending keypress\n");

		if (ir->full_code)
			ir_input_keydown(ir->input, &ir->ir,
					 poll_result.rc_address << 8 |
					 poll_result.rc_data[0]);
		else
			ir_input_keydown(ir->input, &ir->ir,
					 poll_result.rc_data[0]);

		ir_input_nokey(ir->input, &ir->ir);
	}

	ir->last_readcount = poll_result.read_count;
	return;
}

static void em28xx_ir_work(struct work_struct *work)
{
	struct em28xx_IR *ir = container_of(work, struct em28xx_IR, work.work);

	em28xx_ir_handle_key(ir);
	schedule_delayed_work(&ir->work, msecs_to_jiffies(ir->polling));
}

static void em28xx_ir_start(struct em28xx_IR *ir)
{
	INIT_DELAYED_WORK(&ir->work, em28xx_ir_work);
	schedule_delayed_work(&ir->work, 0);
}

static void em28xx_ir_stop(struct em28xx_IR *ir)
{
	cancel_delayed_work_sync(&ir->work);
}

int em28xx_ir_change_protocol(void *priv, u64 ir_type)
{
	int rc = 0;
	struct em28xx_IR *ir = priv;
	struct em28xx *dev = ir->dev;
	u8 ir_config = EM2874_IR_RC5;

	/* Adjust xclk based o IR table for RC5/NEC tables */

	dev->board.ir_codes->ir_type = IR_TYPE_OTHER;
	if (ir_type == IR_TYPE_RC5) {
		dev->board.xclk |= EM28XX_XCLK_IR_RC5_MODE;
		ir->full_code = 1;
	} else if (ir_type == IR_TYPE_NEC) {
		dev->board.xclk &= ~EM28XX_XCLK_IR_RC5_MODE;
		ir_config = EM2874_IR_NEC;
		ir->full_code = 1;
	} else
		rc = -EINVAL;

	dev->board.ir_codes->ir_type = ir_type;

	em28xx_write_reg_bits(dev, EM28XX_R0F_XCLK, dev->board.xclk,
			      EM28XX_XCLK_IR_RC5_MODE);

	/* Setup the proper handler based on the chip */
	switch (dev->chip_id) {
	case CHIP_ID_EM2860:
	case CHIP_ID_EM2883:
		ir->get_key = default_polling_getkey;
		break;
	case CHIP_ID_EM2874:
		ir->get_key = em2874_polling_getkey;
		em28xx_write_regs(dev, EM2874_R50_IR_CONFIG, &ir_config, 1);
		break;
	default:
		printk("Unrecognized em28xx chip id: IR not supported\n");
		rc = -EINVAL;
	}

	return rc;
}

int em28xx_ir_init(struct em28xx *dev)
{
	struct em28xx_IR *ir;
	struct input_dev *input_dev;
	int err = -ENOMEM;

	if (dev->board.ir_codes == NULL) {
		/* No remote control support */
		return 0;
	}

	ir = kzalloc(sizeof(*ir), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!ir || !input_dev)
		goto err_out_free;

	/* record handles to ourself */
	ir->dev = dev;
	dev->ir = ir;

	ir->input = input_dev;

	/*
	 * em2874 supports more protocols. For now, let's just announce
	 * the two protocols that were already tested
	 */
	ir->props.allowed_protos = IR_TYPE_RC5 | IR_TYPE_NEC;
	ir->props.priv = ir;
	ir->props.change_protocol = em28xx_ir_change_protocol;

	/* This is how often we ask the chip for IR information */
	ir->polling = 100; /* ms */

	/* init input device */
	snprintf(ir->name, sizeof(ir->name), "em28xx IR (%s)",
						dev->name);

	usb_make_path(dev->udev, ir->phys, sizeof(ir->phys));
	strlcat(ir->phys, "/input0", sizeof(ir->phys));

	/* Set IR protocol */
	em28xx_ir_change_protocol(ir, dev->board.ir_codes->ir_type);
	err = ir_input_init(input_dev, &ir->ir, IR_TYPE_OTHER);
	if (err < 0)
		goto err_out_free;

	input_dev->name = ir->name;
	input_dev->phys = ir->phys;
	input_dev->id.bustype = BUS_USB;
	input_dev->id.version = 1;
	input_dev->id.vendor = le16_to_cpu(dev->udev->descriptor.idVendor);
	input_dev->id.product = le16_to_cpu(dev->udev->descriptor.idProduct);

	input_dev->dev.parent = &dev->udev->dev;


	em28xx_ir_start(ir);

	/* all done */
	err = ir_input_register(ir->input, dev->board.ir_codes,
				&ir->props);
	if (err)
		goto err_out_stop;

	return 0;
 err_out_stop:
	em28xx_ir_stop(ir);
	dev->ir = NULL;
 err_out_free:
	kfree(ir);
	return err;
}

int em28xx_ir_fini(struct em28xx *dev)
{
	struct em28xx_IR *ir = dev->ir;

	/* skip detach on non attached boards */
	if (!ir)
		return 0;

	em28xx_ir_stop(ir);
	ir_input_unregister(ir->input);
	kfree(ir);

	/* done */
	dev->ir = NULL;
	return 0;
}

/**********************************************************
 Handle Webcam snapshot button
 **********************************************************/

static void em28xx_query_sbutton(struct work_struct *work)
{
	/* Poll the register and see if the button is depressed */
	struct em28xx *dev =
		container_of(work, struct em28xx, sbutton_query_work.work);
	int ret;

	ret = em28xx_read_reg(dev, EM28XX_R0C_USBSUSP);

	if (ret & EM28XX_R0C_USBSUSP_SNAPSHOT) {
		u8 cleared;
		/* Button is depressed, clear the register */
		cleared = ((u8) ret) & ~EM28XX_R0C_USBSUSP_SNAPSHOT;
		em28xx_write_regs(dev, EM28XX_R0C_USBSUSP, &cleared, 1);

		/* Not emulate the keypress */
		input_report_key(dev->sbutton_input_dev, EM28XX_SNAPSHOT_KEY,
				 1);
		/* Now unpress the key */
		input_report_key(dev->sbutton_input_dev, EM28XX_SNAPSHOT_KEY,
				 0);
	}

	/* Schedule next poll */
	schedule_delayed_work(&dev->sbutton_query_work,
			      msecs_to_jiffies(EM28XX_SBUTTON_QUERY_INTERVAL));
}

void em28xx_register_snapshot_button(struct em28xx *dev)
{
	struct input_dev *input_dev;
	int err;

	em28xx_info("Registering snapshot button...\n");
	input_dev = input_allocate_device();
	if (!input_dev) {
		em28xx_errdev("input_allocate_device failed\n");
		return;
	}

	usb_make_path(dev->udev, dev->snapshot_button_path,
		      sizeof(dev->snapshot_button_path));
	strlcat(dev->snapshot_button_path, "/sbutton",
		sizeof(dev->snapshot_button_path));
	INIT_DELAYED_WORK(&dev->sbutton_query_work, em28xx_query_sbutton);

	input_dev->name = "em28xx snapshot button";
	input_dev->phys = dev->snapshot_button_path;
	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REP);
	set_bit(EM28XX_SNAPSHOT_KEY, input_dev->keybit);
	input_dev->keycodesize = 0;
	input_dev->keycodemax = 0;
	input_dev->id.bustype = BUS_USB;
	input_dev->id.vendor = le16_to_cpu(dev->udev->descriptor.idVendor);
	input_dev->id.product = le16_to_cpu(dev->udev->descriptor.idProduct);
	input_dev->id.version = 1;
	input_dev->dev.parent = &dev->udev->dev;

	err = input_register_device(input_dev);
	if (err) {
		em28xx_errdev("input_register_device failed\n");
		input_free_device(input_dev);
		return;
	}

	dev->sbutton_input_dev = input_dev;
	schedule_delayed_work(&dev->sbutton_query_work,
			      msecs_to_jiffies(EM28XX_SBUTTON_QUERY_INTERVAL));
	return;

}

void em28xx_deregister_snapshot_button(struct em28xx *dev)
{
	if (dev->sbutton_input_dev != NULL) {
		em28xx_info("Deregistering snapshot button\n");
		cancel_rearming_delayed_work(&dev->sbutton_query_work);
		input_unregister_device(dev->sbutton_input_dev);
		dev->sbutton_input_dev = NULL;
	}
	return;
}
