/*
 * $Id: h3600_ts_input.c,v 1.4 2002/01/23 06:39:37 jsimmons Exp $
 *
 *  Copyright (c) 2001 "Crazy" James Simmons jsimmons@transvirtual.com
 *
 *  Sponsored by Transvirtual Technology.
 *
 *  Derived from the code in h3600_ts.[ch] by Charles Flynn
 */

/*
 * Driver for the h3600 Touch Screen and other Atmel controlled devices.
 */

/*
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
 *
 * Should you need to contact me, the author, you can do so by
 * e-mail - mail your message to <jsimmons@transvirtual.com>.
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/serio.h>
#include <linux/init.h>
#include <linux/delay.h>

/* SA1100 serial defines */
#include <asm/arch/hardware.h>
#include <asm/arch/irqs.h>

#define DRIVER_DESC	"H3600 touchscreen driver"

MODULE_AUTHOR("James Simmons <jsimmons@transvirtual.com>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

/*
 * Definitions & global arrays.
 */

/* The start and end of frame characters SOF and EOF */
#define CHAR_SOF                0x02
#define CHAR_EOF                0x03
#define FRAME_OVERHEAD          3       /* CHAR_SOF,CHAR_EOF,LENGTH = 3 */

/*
        Atmel events and response IDs contained in frame.
        Programmer has no control over these numbers.
        TODO there are holes - specifically  1,7,0x0a
*/
#define VERSION_ID              0       /* Get Version (request/respose) */
#define KEYBD_ID                2       /* Keyboard (event) */
#define TOUCHS_ID               3       /* Touch Screen (event)*/
#define EEPROM_READ_ID          4       /* (request/response) */
#define EEPROM_WRITE_ID         5       /* (request/response) */
#define THERMAL_ID              6       /* (request/response) */
#define NOTIFY_LED_ID           8       /* (request/response) */
#define BATTERY_ID              9       /* (request/response) */
#define SPI_READ_ID             0x0b    /* ( request/response) */
#define SPI_WRITE_ID            0x0c    /* ( request/response) */
#define FLITE_ID                0x0d    /* backlight ( request/response) */
#define STX_ID                  0xa1    /* extension pack status (req/resp) */

#define MAX_ID                  14

#define H3600_MAX_LENGTH 16
#define H3600_KEY 0xf

#define H3600_SCANCODE_RECORD	1	 /* 1 -> record button */
#define H3600_SCANCODE_CALENDAR 2	 /* 2 -> calendar */
#define H3600_SCANCODE_CONTACTS 3	 /* 3 -> contact */
#define H3600_SCANCODE_Q	4	 /* 4 -> Q button */
#define	H3600_SCANCODE_START	5	 /* 5 -> start menu */
#define	H3600_SCANCODE_UP	6	 /* 6 -> up */
#define H3600_SCANCODE_RIGHT	7	 /* 7 -> right */
#define H3600_SCANCODE_LEFT	8	 /* 8 -> left */
#define H3600_SCANCODE_DOWN	9	 /* 9 -> down */

/*
 * Per-touchscreen data.
 */
struct h3600_dev {
	struct input_dev *dev;
	struct serio *serio;
	unsigned char event;	/* event ID from packet */
	unsigned char chksum;
	unsigned char len;
	unsigned char idx;
	unsigned char buf[H3600_MAX_LENGTH];
	char phys[32];
};

static irqreturn_t action_button_handler(int irq, void *dev_id)
{
	int down = (GPLR & GPIO_BITSY_ACTION_BUTTON) ? 0 : 1;
	struct input_dev *dev = (struct input_dev *) dev_id;

	input_report_key(dev, KEY_ENTER, down);
	input_sync(dev);

	return IRQ_HANDLED;
}

static irqreturn_t npower_button_handler(int irq, void *dev_id)
{
	int down = (GPLR & GPIO_BITSY_NPOWER_BUTTON) ? 0 : 1;
	struct input_dev *dev = (struct input_dev *) dev_id;

	/*
	 * This interrupt is only called when we release the key. So we have
	 * to fake a key press.
	 */
	input_report_key(dev, KEY_SUSPEND, 1);
	input_report_key(dev, KEY_SUSPEND, down);
	input_sync(dev);

	return IRQ_HANDLED;
}

#ifdef CONFIG_PM

static int flite_brightness = 25;

enum flite_pwr {
	FLITE_PWR_OFF = 0,
	FLITE_PWR_ON = 1
};

/*
 * h3600_flite_power: enables or disables power to frontlight, using last bright */
unsigned int h3600_flite_power(struct input_dev *dev, enum flite_pwr pwr)
{
	unsigned char brightness = (pwr == FLITE_PWR_OFF) ? 0 : flite_brightness;
	struct h3600_dev *ts = dev->private;

	/* Must be in this order */
	ts->serio->write(ts->serio, 1);
	ts->serio->write(ts->serio, pwr);
	ts->serio->write(ts->serio, brightness);
	return 0;
}

#endif

/*
 * This function translates the native event packets to linux input event
 * packets. Some packets coming from serial are not touchscreen related. In
 * this case we send them off to be processed elsewhere.
 */
static void h3600ts_process_packet(struct h3600_dev *ts)
{
	struct input_dev *dev = ts->dev;
	static int touched = 0;
	int key, down = 0;

	switch (ts->event) {
		/*
		   Buttons - returned as a single byte
			7 6 5 4 3 2 1 0
			S x x x N N N N

		   S       switch state ( 0=pressed 1=released)
		   x       Unused.
		   NNNN    switch number 0-15

		   Note: This is true for non interrupt generated key events.
		*/
		case KEYBD_ID:
			down = (ts->buf[0] & 0x80) ? 0 : 1;

			switch (ts->buf[0] & 0x7f) {
				case H3600_SCANCODE_RECORD:
					key = KEY_RECORD;
					break;
				case H3600_SCANCODE_CALENDAR:
					key = KEY_PROG1;
                                        break;
				case H3600_SCANCODE_CONTACTS:
					key = KEY_PROG2;
					break;
				case H3600_SCANCODE_Q:
					key = KEY_Q;
					break;
				case H3600_SCANCODE_START:
					key = KEY_PROG3;
					break;
				case H3600_SCANCODE_UP:
					key = KEY_UP;
					break;
				case H3600_SCANCODE_RIGHT:
					key = KEY_RIGHT;
					break;
				case H3600_SCANCODE_LEFT:
					key = KEY_LEFT;
					break;
				case H3600_SCANCODE_DOWN:
					key = KEY_DOWN;
					break;
				default:
					key = 0;
			}
			if (key)
				input_report_key(dev, key, down);
			break;
		/*
		 * Native touchscreen event data is formatted as shown below:-
		 *
		 *      +-------+-------+-------+-------+
		 *      | Xmsb  | Xlsb  | Ymsb  | Ylsb  |
		 *      +-------+-------+-------+-------+
		 *       byte 0    1       2       3
		 */
		case TOUCHS_ID:
			if (!touched) {
				input_report_key(dev, BTN_TOUCH, 1);
				touched = 1;
			}

			if (ts->len) {
				unsigned short x, y;

				x = ts->buf[0]; x <<= 8; x += ts->buf[1];
				y = ts->buf[2]; y <<= 8; y += ts->buf[3];

				input_report_abs(dev, ABS_X, x);
				input_report_abs(dev, ABS_Y, y);
			} else {
				input_report_key(dev, BTN_TOUCH, 0);
				touched = 0;
			}
			break;
		default:
			/* Send a non input event elsewhere */
			break;
	}

	input_sync(dev);
}

/*
 * h3600ts_event() handles events from the input module.
 */
static int h3600ts_event(struct input_dev *dev, unsigned int type,
			 unsigned int code, int value)
{
#if 0
	struct h3600_dev *ts = dev->private;

	switch (type) {
		case EV_LED: {
		//	ts->serio->write(ts->serio, SOME_CMD);
			return 0;
		}
	}
	return -1;
#endif
	return 0;
}

/*
        Frame format
  byte    1       2               3              len + 4
        +-------+---------------+---------------+--=------------+
        |SOF    |id     |len    | len bytes     | Chksum        |
        +-------+---------------+---------------+--=------------+
  bit   0     7  8    11 12   15 16

        +-------+---------------+-------+
        |SOF    |id     |0      |Chksum | - Note Chksum does not include SOF
        +-------+---------------+-------+
  bit   0     7  8    11 12   15 16

*/

static int state;

/* decode States  */
#define STATE_SOF       0       /* start of FRAME */
#define STATE_ID        1       /* state where we decode the ID & len */
#define STATE_DATA      2       /* state where we decode data */
#define STATE_EOF       3       /* state where we decode checksum or EOF */

static irqreturn_t h3600ts_interrupt(struct serio *serio, unsigned char data,
                                     unsigned int flags)
{
	struct h3600_dev *ts = serio_get_drvdata(serio);

	/*
	 * We have a new frame coming in.
	 */
	switch (state) {
		case STATE_SOF:
			if (data == CHAR_SOF)
				state = STATE_ID;
			break;
		case STATE_ID:
			ts->event = (data & 0xf0) >> 4;
			ts->len = (data & 0xf);
			ts->idx = 0;
			if (ts->event >= MAX_ID) {
				state = STATE_SOF;
				break;
			}
			ts->chksum = data;
			state = (ts->len > 0) ? STATE_DATA : STATE_EOF;
			break;
		case STATE_DATA:
			ts->chksum += data;
			ts->buf[ts->idx]= data;
			if (++ts->idx == ts->len)
				state = STATE_EOF;
			break;
		case STATE_EOF:
			state = STATE_SOF;
			if (data == CHAR_EOF || data == ts->chksum)
				h3600ts_process_packet(ts);
			break;
		default:
			printk("Error3\n");
			break;
	}

	return IRQ_HANDLED;
}

/*
 * h3600ts_connect() is the routine that is called when someone adds a
 * new serio device that supports H3600 protocol and registers it as
 * an input device.
 */
static int h3600ts_connect(struct serio *serio, struct serio_driver *drv)
{
	struct h3600_dev *ts;
	struct input_dev *input_dev;
	int err;

	ts = kzalloc(sizeof(struct h3600_dev), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!ts || !input_dev) {
		err = -ENOMEM;
		goto fail1;
	}

	ts->serio = serio;
	ts->dev = input_dev;
	snprintf(ts->phys, sizeof(ts->phys), "%s/input0", serio->phys);

	input_dev->name = "H3600 TouchScreen";
	input_dev->phys = ts->phys;
	input_dev->id.bustype = BUS_RS232;
	input_dev->id.vendor = SERIO_H3600;
	input_dev->id.product = 0x0666;  /* FIXME !!! We can ask the hardware */
	input_dev->id.version = 0x0100;
	input_dev->cdev.dev = &serio->dev;
	input_dev->private = ts;

	input_dev->event = h3600ts_event;

	input_dev->evbit[0] = BIT(EV_KEY) | BIT(EV_ABS) | BIT(EV_LED) | BIT(EV_PWR);
	input_dev->ledbit[0] = BIT(LED_SLEEP);
	input_set_abs_params(input_dev, ABS_X, 60, 985, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, 35, 1024, 0, 0);

	set_bit(KEY_RECORD, input_dev->keybit);
	set_bit(KEY_Q, input_dev->keybit);
	set_bit(KEY_PROG1, input_dev->keybit);
	set_bit(KEY_PROG2, input_dev->keybit);
	set_bit(KEY_PROG3, input_dev->keybit);
	set_bit(KEY_UP, input_dev->keybit);
	set_bit(KEY_RIGHT, input_dev->keybit);
	set_bit(KEY_LEFT, input_dev->keybit);
	set_bit(KEY_DOWN, input_dev->keybit);
	set_bit(KEY_ENTER, input_dev->keybit);
	set_bit(KEY_SUSPEND, input_dev->keybit);
	set_bit(BTN_TOUCH, input_dev->keybit);

	/* Device specific stuff */
	set_GPIO_IRQ_edge(GPIO_BITSY_ACTION_BUTTON, GPIO_BOTH_EDGES);
	set_GPIO_IRQ_edge(GPIO_BITSY_NPOWER_BUTTON, GPIO_RISING_EDGE);

	if (request_irq(IRQ_GPIO_BITSY_ACTION_BUTTON, action_button_handler,
			IRQF_SHARED | IRQF_DISABLED, "h3600_action", &ts->dev)) {
		printk(KERN_ERR "h3600ts.c: Could not allocate Action Button IRQ!\n");
		err = -EBUSY;
		goto fail2;
	}

	if (request_irq(IRQ_GPIO_BITSY_NPOWER_BUTTON, npower_button_handler,
			IRQF_SHARED | IRQF_DISABLED, "h3600_suspend", &ts->dev)) {
		printk(KERN_ERR "h3600ts.c: Could not allocate Power Button IRQ!\n");
		err = -EBUSY;
		goto fail3;
	}

	serio_set_drvdata(serio, ts);

	err = serio_open(serio, drv);
	if (err)
		return err;

	//h3600_flite_control(1, 25);     /* default brightness */
	input_register_device(ts->dev);

	return 0;

fail3:	free_irq(IRQ_GPIO_BITSY_NPOWER_BUTTON, ts->dev);
fail2:	free_irq(IRQ_GPIO_BITSY_ACTION_BUTTON, ts->dev);
fail1:	serio_set_drvdata(serio, NULL);
	input_free_device(input_dev);
	kfree(ts);
	return err;
}

/*
 * h3600ts_disconnect() is the opposite of h3600ts_connect()
 */

static void h3600ts_disconnect(struct serio *serio)
{
	struct h3600_dev *ts = serio_get_drvdata(serio);

	free_irq(IRQ_GPIO_BITSY_ACTION_BUTTON, &ts->dev);
	free_irq(IRQ_GPIO_BITSY_NPOWER_BUTTON, &ts->dev);
	input_get_device(ts->dev);
	input_unregister_device(ts->dev);
	serio_close(serio);
	serio_set_drvdata(serio, NULL);
	input_put_device(ts->dev);
	kfree(ts);
}

/*
 * The serio driver structure.
 */

static struct serio_device_id h3600ts_serio_ids[] = {
	{
		.type	= SERIO_RS232,
		.proto	= SERIO_H3600,
		.id	= SERIO_ANY,
		.extra	= SERIO_ANY,
	},
	{ 0 }
};

MODULE_DEVICE_TABLE(serio, h3600ts_serio_ids);

static struct serio_driver h3600ts_drv = {
	.driver		= {
		.name	= "h3600ts",
	},
	.description	= DRIVER_DESC,
	.id_table	= h3600ts_serio_ids,
	.interrupt	= h3600ts_interrupt,
	.connect	= h3600ts_connect,
	.disconnect	= h3600ts_disconnect,
};

/*
 * The functions for inserting/removing us as a module.
 */

static int __init h3600ts_init(void)
{
	serio_register_driver(&h3600ts_drv);
	return 0;
}

static void __exit h3600ts_exit(void)
{
	serio_unregister_driver(&h3600ts_drv);
}

module_init(h3600ts_init);
module_exit(h3600ts_exit);
