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
#include <linux/usb.h>
#include <linux/slab.h>

#include "em28xx.h"

#define EM28XX_SNAPSHOT_KEY				KEY_CAMERA
#define EM28XX_BUTTONS_DEBOUNCED_QUERY_INTERVAL		500 /* [ms] */
#define EM28XX_BUTTONS_VOLATILE_QUERY_INTERVAL		100 /* [ms] */

static unsigned int ir_debug;
module_param(ir_debug, int, 0644);
MODULE_PARM_DESC(ir_debug, "enable debug messages [IR]");

#define MODULE_NAME "em28xx"

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

	u32 scancode;
};

struct em28xx_IR {
	struct em28xx *dev;
	struct rc_dev *rc;
	char name[32];
	char phys[32];

	/* poll decoder */
	int polling;
	struct delayed_work work;
	unsigned int full_code:1;
	unsigned int last_readcount;
	u64 rc_type;

	/* i2c slave address of external device (if used) */
	u16 i2c_dev_addr;

	int  (*get_key_i2c)(struct i2c_client *, u32 *);
	int  (*get_key)(struct em28xx_IR *, struct em28xx_ir_poll_result *);
};

/**********************************************************
 I2C IR based get keycodes - should be used with ir-kbd-i2c
 **********************************************************/

static int em28xx_get_key_terratec(struct i2c_client *i2c_dev, u32 *ir_key)
{
	unsigned char b;

	/* poll IR chip */
	if (1 != i2c_master_recv(i2c_dev, &b, 1))
		return -EIO;

	/* it seems that 0xFE indicates that a button is still hold
	   down, while 0xff indicates that no button is hold down. */

	if (b == 0xff)
		return 0;

	if (b == 0xfe)
		/* keep old data */
		return 1;

	*ir_key = b;
	return 1;
}

static int em28xx_get_key_em_haup(struct i2c_client *i2c_dev, u32 *ir_key)
{
	unsigned char buf[2];
	u16 code;
	int size;

	/* poll IR chip */
	size = i2c_master_recv(i2c_dev, buf, sizeof(buf));

	if (size != 2)
		return -EIO;

	/* Does eliminate repeated parity code */
	if (buf[1] == 0xff)
		return 0;

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

	/* return key */
	*ir_key = code;
	return 1;
}

static int em28xx_get_key_pinnacle_usb_grey(struct i2c_client *i2c_dev,
					    u32 *ir_key)
{
	unsigned char buf[3];

	/* poll IR chip */

	if (3 != i2c_master_recv(i2c_dev, buf, 3))
		return -EIO;

	if (buf[0] != 0x00)
		return 0;

	*ir_key = buf[2]&0x3f;

	return 1;
}

static int em28xx_get_key_winfast_usbii_deluxe(struct i2c_client *i2c_dev,
					       u32 *ir_key)
{
	unsigned char subaddr, keydetect, key;

	struct i2c_msg msg[] = { { .addr = i2c_dev->addr, .flags = 0, .buf = &subaddr, .len = 1},
				 { .addr = i2c_dev->addr, .flags = I2C_M_RD, .buf = &keydetect, .len = 1} };

	subaddr = 0x10;
	if (2 != i2c_transfer(i2c_dev->adapter, msg, 2))
		return -EIO;
	if (keydetect == 0x00)
		return 0;

	subaddr = 0x00;
	msg[1].buf = &key;
	if (2 != i2c_transfer(i2c_dev->adapter, msg, 2))
		return -EIO;
	if (key == 0x00)
		return 0;

	*ir_key = key;
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

	/* Remote Control Address/Data (Regs 0x46/0x47) */
	poll_result->scancode = msg[1] << 8 | msg[2];

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

	/*
	 * Remote Control Address (Reg 0x52)
	 * Remote Control Data (Reg 0x53-0x55)
	 */
	switch (ir->rc_type) {
	case RC_BIT_RC5:
		poll_result->scancode = msg[1] << 8 | msg[2];
		break;
	case RC_BIT_NEC:
		if ((msg[3] ^ msg[4]) != 0xff)		/* 32 bits NEC */
			poll_result->scancode = (msg[1] << 24) |
						(msg[2] << 16) |
						(msg[3] << 8)  |
						 msg[4];
		else if ((msg[1] ^ msg[2]) != 0xff)	/* 24 bits NEC */
			poll_result->scancode = (msg[1] << 16) |
						(msg[2] << 8)  |
						 msg[3];
		else					/* Normal NEC */
			poll_result->scancode = msg[1] << 8 | msg[3];
		break;
	case RC_BIT_RC6_0:
		poll_result->scancode = msg[1] << 8 | msg[2];
		break;
	default:
		poll_result->scancode = (msg[1] << 24) | (msg[2] << 16) |
					(msg[3] << 8)  | msg[4];
		break;
	}

	return 0;
}

/**********************************************************
 Polling code for em28xx
 **********************************************************/

static int em28xx_i2c_ir_handle_key(struct em28xx_IR *ir)
{
	struct em28xx *dev = ir->dev;
	static u32 ir_key;
	int rc;
	struct i2c_client client;

	client.adapter = &ir->dev->i2c_adap[dev->def_i2c_bus];
	client.addr = ir->i2c_dev_addr;

	rc = ir->get_key_i2c(&client, &ir_key);
	if (rc < 0) {
		dprintk("ir->get_key_i2c() failed: %d\n", rc);
		return rc;
	}

	if (rc) {
		dprintk("%s: keycode = 0x%04x\n", __func__, ir_key);
		rc_keydown(ir->rc, ir_key, 0);
	}
	return 0;
}

static void em28xx_ir_handle_key(struct em28xx_IR *ir)
{
	int result;
	struct em28xx_ir_poll_result poll_result;

	/* read the registers containing the IR status */
	result = ir->get_key(ir, &poll_result);
	if (unlikely(result < 0)) {
		dprintk("ir->get_key() failed: %d\n", result);
		return;
	}

	if (unlikely(poll_result.read_count != ir->last_readcount)) {
		dprintk("%s: toggle: %d, count: %d, key 0x%04x\n", __func__,
			poll_result.toggle_bit, poll_result.read_count,
			poll_result.scancode);
		if (ir->full_code)
			rc_keydown(ir->rc,
				   poll_result.scancode,
				   poll_result.toggle_bit);
		else
			rc_keydown(ir->rc,
				   poll_result.scancode & 0xff,
				   poll_result.toggle_bit);

		if (ir->dev->chip_id == CHIP_ID_EM2874 ||
		    ir->dev->chip_id == CHIP_ID_EM2884)
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

static void em28xx_ir_work(struct work_struct *work)
{
	struct em28xx_IR *ir = container_of(work, struct em28xx_IR, work.work);

	if (ir->i2c_dev_addr) /* external i2c device */
		em28xx_i2c_ir_handle_key(ir);
	else /* internal device */
		em28xx_ir_handle_key(ir);
	schedule_delayed_work(&ir->work, msecs_to_jiffies(ir->polling));
}

static int em28xx_ir_start(struct rc_dev *rc)
{
	struct em28xx_IR *ir = rc->priv;

	INIT_DELAYED_WORK(&ir->work, em28xx_ir_work);
	schedule_delayed_work(&ir->work, 0);

	return 0;
}

static void em28xx_ir_stop(struct rc_dev *rc)
{
	struct em28xx_IR *ir = rc->priv;

	cancel_delayed_work_sync(&ir->work);
}

static int em2860_ir_change_protocol(struct rc_dev *rc_dev, u64 *rc_type)
{
	struct em28xx_IR *ir = rc_dev->priv;
	struct em28xx *dev = ir->dev;

	/* Adjust xclk based on IR table for RC5/NEC tables */
	if (*rc_type & RC_BIT_RC5) {
		dev->board.xclk |= EM28XX_XCLK_IR_RC5_MODE;
		ir->full_code = 1;
		*rc_type = RC_BIT_RC5;
	} else if (*rc_type & RC_BIT_NEC) {
		dev->board.xclk &= ~EM28XX_XCLK_IR_RC5_MODE;
		ir->full_code = 1;
		*rc_type = RC_BIT_NEC;
	} else if (*rc_type & RC_BIT_UNKNOWN) {
		*rc_type = RC_BIT_UNKNOWN;
	} else {
		*rc_type = ir->rc_type;
		return -EINVAL;
	}
	em28xx_write_reg_bits(dev, EM28XX_R0F_XCLK, dev->board.xclk,
			      EM28XX_XCLK_IR_RC5_MODE);

	ir->rc_type = *rc_type;

	return 0;
}

static int em2874_ir_change_protocol(struct rc_dev *rc_dev, u64 *rc_type)
{
	struct em28xx_IR *ir = rc_dev->priv;
	struct em28xx *dev = ir->dev;
	u8 ir_config = EM2874_IR_RC5;

	/* Adjust xclk and set type based on IR table for RC5/NEC/RC6 tables */
	if (*rc_type & RC_BIT_RC5) {
		dev->board.xclk |= EM28XX_XCLK_IR_RC5_MODE;
		ir->full_code = 1;
		*rc_type = RC_BIT_RC5;
	} else if (*rc_type & RC_BIT_NEC) {
		dev->board.xclk &= ~EM28XX_XCLK_IR_RC5_MODE;
		ir_config = EM2874_IR_NEC | EM2874_IR_NEC_NO_PARITY;
		ir->full_code = 1;
		*rc_type = RC_BIT_NEC;
	} else if (*rc_type & RC_BIT_RC6_0) {
		dev->board.xclk |= EM28XX_XCLK_IR_RC5_MODE;
		ir_config = EM2874_IR_RC6_MODE_0;
		ir->full_code = 1;
		*rc_type = RC_BIT_RC6_0;
	} else if (*rc_type & RC_BIT_UNKNOWN) {
		*rc_type = RC_BIT_UNKNOWN;
	} else {
		*rc_type = ir->rc_type;
		return -EINVAL;
	}
	em28xx_write_regs(dev, EM2874_R50_IR_CONFIG, &ir_config, 1);
	em28xx_write_reg_bits(dev, EM28XX_R0F_XCLK, dev->board.xclk,
			      EM28XX_XCLK_IR_RC5_MODE);

	ir->rc_type = *rc_type;

	return 0;
}
static int em28xx_ir_change_protocol(struct rc_dev *rc_dev, u64 *rc_type)
{
	struct em28xx_IR *ir = rc_dev->priv;
	struct em28xx *dev = ir->dev;

	/* Setup the proper handler based on the chip */
	switch (dev->chip_id) {
	case CHIP_ID_EM2860:
	case CHIP_ID_EM2883:
		return em2860_ir_change_protocol(rc_dev, rc_type);
	case CHIP_ID_EM2884:
	case CHIP_ID_EM2874:
	case CHIP_ID_EM28174:
	case CHIP_ID_EM28178:
		return em2874_ir_change_protocol(rc_dev, rc_type);
	default:
		printk("Unrecognized em28xx chip id 0x%02x: IR not supported\n",
			dev->chip_id);
		return -EINVAL;
	}
}

static int em28xx_probe_i2c_ir(struct em28xx *dev)
{
	int i = 0;
	/* Leadtek winfast tv USBII deluxe can find a non working IR-device */
	/* at address 0x18, so if that address is needed for another board in */
	/* the future, please put it after 0x1f. */
	const unsigned short addr_list[] = {
		 0x1f, 0x30, 0x47, I2C_CLIENT_END
	};

	while (addr_list[i] != I2C_CLIENT_END) {
		if (i2c_probe_func_quick_read(&dev->i2c_adap[dev->def_i2c_bus], addr_list[i]) == 1)
			return addr_list[i];
		i++;
	}

	return -ENODEV;
}

/**********************************************************
 Handle buttons
 **********************************************************/

static void em28xx_query_buttons(struct work_struct *work)
{
	struct em28xx *dev =
		container_of(work, struct em28xx, buttons_query_work.work);
	u8 i, j;
	int regval;
	bool is_pressed, was_pressed;
	const struct em28xx_led *led;

	/* Poll and evaluate all addresses */
	for (i = 0; i < dev->num_button_polling_addresses; i++) {
		/* Read value from register */
		regval = em28xx_read_reg(dev, dev->button_polling_addresses[i]);
		if (regval < 0)
			continue;
		/* Check states of the buttons and act */
		j = 0;
		while (dev->board.buttons[j].role >= 0 &&
			 dev->board.buttons[j].role < EM28XX_NUM_BUTTON_ROLES) {
			struct em28xx_button *button = &dev->board.buttons[j];
			/* Check if button uses the current address */
			if (button->reg_r != dev->button_polling_addresses[i]) {
				j++;
				continue;
			}
			/* Determine if button is and was pressed last time */
			is_pressed = regval & button->mask;
			was_pressed = dev->button_polling_last_values[i]
				       & button->mask;
			if (button->inverted) {
				is_pressed = !is_pressed;
				was_pressed = !was_pressed;
			}
			/* Clear button state (if needed) */
			if (is_pressed && button->reg_clearing)
				em28xx_write_reg(dev, button->reg_clearing,
						 (~regval & button->mask)
						    | (regval & ~button->mask));
			/* Handle button state */
			if (!is_pressed || was_pressed) {
				j++;
				continue;
			}
			switch (button->role) {
			case EM28XX_BUTTON_SNAPSHOT:
				/* Emulate the keypress */
				input_report_key(dev->sbutton_input_dev,
						 EM28XX_SNAPSHOT_KEY, 1);
				/* Unpress the key */
				input_report_key(dev->sbutton_input_dev,
						 EM28XX_SNAPSHOT_KEY, 0);
				break;
			case EM28XX_BUTTON_ILLUMINATION:
				led = em28xx_find_led(dev,
						      EM28XX_LED_ILLUMINATION);
				/* Switch illumination LED on/off */
				if (led)
					em28xx_toggle_reg_bits(dev,
							       led->gpio_reg,
							       led->gpio_mask);
				break;
			default:
				WARN_ONCE(1, "BUG: unhandled button role.");
			}
			/* Next button */
			j++;
		}
		/* Save current value for comparison during the next polling */
		dev->button_polling_last_values[i] = regval;
	}
	/* Schedule next poll */
	schedule_delayed_work(&dev->buttons_query_work,
			      msecs_to_jiffies(dev->button_polling_interval));
}

static int em28xx_register_snapshot_button(struct em28xx *dev)
{
	struct input_dev *input_dev;
	int err;

	em28xx_info("Registering snapshot button...\n");
	input_dev = input_allocate_device();
	if (!input_dev)
		return -ENOMEM;

	usb_make_path(dev->udev, dev->snapshot_button_path,
		      sizeof(dev->snapshot_button_path));
	strlcat(dev->snapshot_button_path, "/sbutton",
		sizeof(dev->snapshot_button_path));

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
		return err;
	}

	dev->sbutton_input_dev = input_dev;
	return 0;
}

static void em28xx_init_buttons(struct em28xx *dev)
{
	u8  i = 0, j = 0;
	bool addr_new = 0;

	dev->button_polling_interval = EM28XX_BUTTONS_DEBOUNCED_QUERY_INTERVAL;
	while (dev->board.buttons[i].role >= 0 &&
			 dev->board.buttons[i].role < EM28XX_NUM_BUTTON_ROLES) {
		struct em28xx_button *button = &dev->board.buttons[i];
		/* Check if polling address is already on the list */
		addr_new = 1;
		for (j = 0; j < dev->num_button_polling_addresses; j++) {
			if (button->reg_r == dev->button_polling_addresses[j]) {
				addr_new = 0;
				break;
			}
		}
		/* Check if max. number of polling addresses is exceeded */
		if (addr_new && dev->num_button_polling_addresses
					   >= EM28XX_NUM_BUTTON_ADDRESSES_MAX) {
			WARN_ONCE(1, "BUG: maximum number of button polling addresses exceeded.");
			goto next_button;
		}
		/* Button role specific checks and actions */
		if (button->role == EM28XX_BUTTON_SNAPSHOT) {
			/* Register input device */
			if (em28xx_register_snapshot_button(dev) < 0)
				goto next_button;
		} else if (button->role == EM28XX_BUTTON_ILLUMINATION) {
			/* Check sanity */
			if (!em28xx_find_led(dev, EM28XX_LED_ILLUMINATION)) {
				em28xx_errdev("BUG: illumination button defined, but no illumination LED.\n");
				goto next_button;
			}
		}
		/* Add read address to list of polling addresses */
		if (addr_new) {
			unsigned int index = dev->num_button_polling_addresses;
			dev->button_polling_addresses[index] = button->reg_r;
			dev->num_button_polling_addresses++;
		}
		/* Reduce polling interval if necessary */
		if (!button->reg_clearing)
			dev->button_polling_interval =
					 EM28XX_BUTTONS_VOLATILE_QUERY_INTERVAL;
next_button:
		/* Next button */
		i++;
	}

	/* Start polling */
	if (dev->num_button_polling_addresses) {
		memset(dev->button_polling_last_values, 0,
					       EM28XX_NUM_BUTTON_ADDRESSES_MAX);
		INIT_DELAYED_WORK(&dev->buttons_query_work,
							  em28xx_query_buttons);
		schedule_delayed_work(&dev->buttons_query_work,
			       msecs_to_jiffies(dev->button_polling_interval));
	}
}

static void em28xx_shutdown_buttons(struct em28xx *dev)
{
	/* Cancel polling */
	cancel_delayed_work_sync(&dev->buttons_query_work);
	/* Clear polling addresses list */
	dev->num_button_polling_addresses = 0;
	/* Deregister input devices */
	if (dev->sbutton_input_dev != NULL) {
		em28xx_info("Deregistering snapshot button\n");
		input_unregister_device(dev->sbutton_input_dev);
		dev->sbutton_input_dev = NULL;
	}
}

static int em28xx_ir_init(struct em28xx *dev)
{
	struct em28xx_IR *ir;
	struct rc_dev *rc;
	int err = -ENOMEM;
	u64 rc_type;
	u16 i2c_rc_dev_addr = 0;

	if (dev->is_audio_only) {
		/* Shouldn't initialize IR for this interface */
		return 0;
	}

	if (dev->board.buttons)
		em28xx_init_buttons(dev);

	if (dev->board.has_ir_i2c) {
		i2c_rc_dev_addr = em28xx_probe_i2c_ir(dev);
		if (!i2c_rc_dev_addr) {
			dev->board.has_ir_i2c = 0;
			em28xx_warn("No i2c IR remote control device found.\n");
			return -ENODEV;
		}
	}

	if (dev->board.ir_codes == NULL && !dev->board.has_ir_i2c) {
		/* No remote control support */
		em28xx_warn("Remote control support is not available for "
				"this card.\n");
		return 0;
	}

	em28xx_info("Registering input extension\n");

	ir = kzalloc(sizeof(*ir), GFP_KERNEL);
	rc = rc_allocate_device();
	if (!ir || !rc)
		goto error;

	/* record handles to ourself */
	ir->dev = dev;
	dev->ir = ir;
	ir->rc = rc;

	rc->priv = ir;
	rc->open = em28xx_ir_start;
	rc->close = em28xx_ir_stop;

	if (dev->board.has_ir_i2c) {	/* external i2c device */
		switch (dev->model) {
		case EM2800_BOARD_TERRATEC_CINERGY_200:
		case EM2820_BOARD_TERRATEC_CINERGY_250:
			rc->map_name = RC_MAP_EM_TERRATEC;
			ir->get_key_i2c = em28xx_get_key_terratec;
			break;
		case EM2820_BOARD_PINNACLE_USB_2:
			rc->map_name = RC_MAP_PINNACLE_GREY;
			ir->get_key_i2c = em28xx_get_key_pinnacle_usb_grey;
			break;
		case EM2820_BOARD_HAUPPAUGE_WINTV_USB_2:
			rc->map_name = RC_MAP_HAUPPAUGE;
			ir->get_key_i2c = em28xx_get_key_em_haup;
			rc->allowed_protos = RC_BIT_RC5;
			break;
		case EM2820_BOARD_LEADTEK_WINFAST_USBII_DELUXE:
			rc->map_name = RC_MAP_WINFAST_USBII_DELUXE;
			ir->get_key_i2c = em28xx_get_key_winfast_usbii_deluxe;
			break;
		default:
			err = -ENODEV;
			goto error;
		}

		ir->i2c_dev_addr = i2c_rc_dev_addr;
	} else {	/* internal device */
		switch (dev->chip_id) {
		case CHIP_ID_EM2860:
		case CHIP_ID_EM2883:
			rc->allowed_protos = RC_BIT_RC5 | RC_BIT_NEC;
			ir->get_key = default_polling_getkey;
			break;
		case CHIP_ID_EM2884:
		case CHIP_ID_EM2874:
		case CHIP_ID_EM28174:
		case CHIP_ID_EM28178:
			ir->get_key = em2874_polling_getkey;
			rc->allowed_protos = RC_BIT_RC5 | RC_BIT_NEC |
					     RC_BIT_RC6_0;
			break;
		default:
			err = -ENODEV;
			goto error;
		}

		rc->change_protocol = em28xx_ir_change_protocol;
		rc->map_name = dev->board.ir_codes;

		/* By default, keep protocol field untouched */
		rc_type = RC_BIT_UNKNOWN;
		err = em28xx_ir_change_protocol(rc, &rc_type);
		if (err)
			goto error;
	}

	/* This is how often we ask the chip for IR information */
	ir->polling = 100; /* ms */

	/* init input device */
	snprintf(ir->name, sizeof(ir->name), "em28xx IR (%s)", dev->name);

	usb_make_path(dev->udev, ir->phys, sizeof(ir->phys));
	strlcat(ir->phys, "/input0", sizeof(ir->phys));

	rc->input_name = ir->name;
	rc->input_phys = ir->phys;
	rc->input_id.bustype = BUS_USB;
	rc->input_id.version = 1;
	rc->input_id.vendor = le16_to_cpu(dev->udev->descriptor.idVendor);
	rc->input_id.product = le16_to_cpu(dev->udev->descriptor.idProduct);
	rc->dev.parent = &dev->udev->dev;
	rc->driver_name = MODULE_NAME;

	/* all done */
	err = rc_register_device(rc);
	if (err)
		goto error;

	em28xx_info("Input extension successfully initalized\n");

	return 0;

error:
	dev->ir = NULL;
	rc_free_device(rc);
	kfree(ir);
	return err;
}

static int em28xx_ir_fini(struct em28xx *dev)
{
	struct em28xx_IR *ir = dev->ir;

	if (dev->is_audio_only) {
		/* Shouldn't initialize IR for this interface */
		return 0;
	}

	em28xx_info("Closing input extension");

	em28xx_shutdown_buttons(dev);

	/* skip detach on non attached boards */
	if (!ir)
		return 0;

	if (ir->rc)
		rc_unregister_device(ir->rc);

	/* done */
	kfree(ir);
	dev->ir = NULL;
	return 0;
}

static struct em28xx_ops rc_ops = {
	.id   = EM28XX_RC,
	.name = "Em28xx Input Extension",
	.init = em28xx_ir_init,
	.fini = em28xx_ir_fini,
};

static int __init em28xx_rc_register(void)
{
	return em28xx_register_extension(&rc_ops);
}

static void __exit em28xx_rc_unregister(void)
{
	em28xx_unregister_extension(&rc_ops);
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab <mchehab@redhat.com>");
MODULE_DESCRIPTION(DRIVER_DESC " - input interface");
MODULE_VERSION(EM28XX_VERSION);

module_init(em28xx_rc_register);
module_exit(em28xx_rc_unregister);
