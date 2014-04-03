/*
 *
 * handle saa7134 IR remotes via linux kernel input layer.
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
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/slab.h>

#include "saa7134-reg.h"
#include "saa7134.h"

#define MODULE_NAME "saa7134"

static unsigned int disable_ir;
module_param(disable_ir, int, 0444);
MODULE_PARM_DESC(disable_ir,"disable infrared remote support");

static unsigned int ir_debug;
module_param(ir_debug, int, 0644);
MODULE_PARM_DESC(ir_debug,"enable debug messages [IR]");

static int pinnacle_remote;
module_param(pinnacle_remote, int, 0644);    /* Choose Pinnacle PCTV remote */
MODULE_PARM_DESC(pinnacle_remote, "Specify Pinnacle PCTV remote: 0=coloured, 1=grey (defaults to 0)");

#define dprintk(fmt, arg...)	if (ir_debug) \
	printk(KERN_DEBUG "%s/ir: " fmt, dev->name , ## arg)
#define i2cdprintk(fmt, arg...)    if (ir_debug) \
	printk(KERN_DEBUG "%s/ir: " fmt, ir->name , ## arg)

/* Helper function for raw decoding at GPIO16 or GPIO18 */
static int saa7134_raw_decode_irq(struct saa7134_dev *dev);

/* -------------------- GPIO generic keycode builder -------------------- */

static int build_key(struct saa7134_dev *dev)
{
	struct saa7134_card_ir *ir = dev->remote;
	u32 gpio, data;

	/* here comes the additional handshake steps for some cards */
	switch (dev->board) {
	case SAA7134_BOARD_GOTVIEW_7135:
		saa_setb(SAA7134_GPIO_GPSTATUS1, 0x80);
		saa_clearb(SAA7134_GPIO_GPSTATUS1, 0x80);
		break;
	}
	/* rising SAA7134_GPIO_GPRESCAN reads the status */
	saa_clearb(SAA7134_GPIO_GPMODE3,SAA7134_GPIO_GPRESCAN);
	saa_setb(SAA7134_GPIO_GPMODE3,SAA7134_GPIO_GPRESCAN);

	gpio = saa_readl(SAA7134_GPIO_GPSTATUS0 >> 2);
	if (ir->polling) {
		if (ir->last_gpio == gpio)
			return 0;
		ir->last_gpio = gpio;
	}

	data = ir_extract_bits(gpio, ir->mask_keycode);
	dprintk("build_key gpio=0x%x mask=0x%x data=%d\n",
		gpio, ir->mask_keycode, data);

	switch (dev->board) {
	case SAA7134_BOARD_KWORLD_PLUS_TV_ANALOG:
		if (data == ir->mask_keycode)
			rc_keyup(ir->dev);
		else
			rc_keydown_notimeout(ir->dev, RC_TYPE_UNKNOWN, data, 0);
		return 0;
	}

	if (ir->polling) {
		if ((ir->mask_keydown  &&  (0 != (gpio & ir->mask_keydown))) ||
		    (ir->mask_keyup    &&  (0 == (gpio & ir->mask_keyup)))) {
			rc_keydown_notimeout(ir->dev, RC_TYPE_UNKNOWN, data, 0);
		} else {
			rc_keyup(ir->dev);
		}
	}
	else {	/* IRQ driven mode - handle key press and release in one go */
		if ((ir->mask_keydown  &&  (0 != (gpio & ir->mask_keydown))) ||
		    (ir->mask_keyup    &&  (0 == (gpio & ir->mask_keyup)))) {
			rc_keydown_notimeout(ir->dev, RC_TYPE_UNKNOWN, data, 0);
			rc_keyup(ir->dev);
		}
	}

	return 0;
}

/* --------------------- Chip specific I2C key builders ----------------- */

static int get_key_flydvb_trio(struct IR_i2c *ir, enum rc_type *protocol,
			       u32 *scancode, u8 *toggle)
{
	int gpio;
	int attempt = 0;
	unsigned char b;

	/* We need this to access GPI Used by the saa_readl macro. */
	struct saa7134_dev *dev = ir->c->adapter->algo_data;

	if (dev == NULL) {
		i2cdprintk("get_key_flydvb_trio: "
			   "ir->c->adapter->algo_data is NULL!\n");
		return -EIO;
	}

	/* rising SAA7134_GPIGPRESCAN reads the status */
	saa_clearb(SAA7134_GPIO_GPMODE3, SAA7134_GPIO_GPRESCAN);
	saa_setb(SAA7134_GPIO_GPMODE3, SAA7134_GPIO_GPRESCAN);

	gpio = saa_readl(SAA7134_GPIO_GPSTATUS0 >> 2);

	if (0x40000 & ~gpio)
		return 0; /* No button press */

	/* No button press - only before first key pressed */
	if (b == 0xFF)
		return 0;

	/* poll IR chip */
	/* weak up the IR chip */
	b = 0;

	while (1 != i2c_master_send(ir->c, &b, 1)) {
		if ((attempt++) < 10) {
			/*
			 * wait a bit for next attempt -
			 * I don't know how make it better
			 */
			msleep(10);
			continue;
		}
		i2cdprintk("send wake up byte to pic16C505 (IR chip)"
			   "failed %dx\n", attempt);
		return -EIO;
	}
	if (1 != i2c_master_recv(ir->c, &b, 1)) {
		i2cdprintk("read error\n");
		return -EIO;
	}

	*protocol = RC_TYPE_UNKNOWN;
	*scancode = b;
	*toggle = 0;
	return 1;
}

static int get_key_msi_tvanywhere_plus(struct IR_i2c *ir, enum rc_type *protocol,
				       u32 *scancode, u8 *toggle)
{
	unsigned char b;
	int gpio;

	/* <dev> is needed to access GPIO. Used by the saa_readl macro. */
	struct saa7134_dev *dev = ir->c->adapter->algo_data;
	if (dev == NULL) {
		i2cdprintk("get_key_msi_tvanywhere_plus: "
			   "ir->c->adapter->algo_data is NULL!\n");
		return -EIO;
	}

	/* rising SAA7134_GPIO_GPRESCAN reads the status */

	saa_clearb(SAA7134_GPIO_GPMODE3, SAA7134_GPIO_GPRESCAN);
	saa_setb(SAA7134_GPIO_GPMODE3, SAA7134_GPIO_GPRESCAN);

	gpio = saa_readl(SAA7134_GPIO_GPSTATUS0 >> 2);

	/* GPIO&0x40 is pulsed low when a button is pressed. Don't do
	   I2C receive if gpio&0x40 is not low. */

	if (gpio & 0x40)
		return 0;       /* No button press */

	/* GPIO says there is a button press. Get it. */

	if (1 != i2c_master_recv(ir->c, &b, 1)) {
		i2cdprintk("read error\n");
		return -EIO;
	}

	/* No button press */

	if (b == 0xff)
		return 0;

	/* Button pressed */

	dprintk("get_key_msi_tvanywhere_plus: Key = 0x%02X\n", b);
	*protocol = RC_TYPE_UNKNOWN;
	*scancode = b;
	*toggle = 0;
	return 1;
}

/* copied and modified from get_key_msi_tvanywhere_plus() */
static int get_key_kworld_pc150u(struct IR_i2c *ir, enum rc_type *protocol,
				 u32 *scancode, u8 *toggle)
{
	unsigned char b;
	unsigned int gpio;

	/* <dev> is needed to access GPIO. Used by the saa_readl macro. */
	struct saa7134_dev *dev = ir->c->adapter->algo_data;
	if (dev == NULL) {
		i2cdprintk("get_key_kworld_pc150u: "
			   "ir->c->adapter->algo_data is NULL!\n");
		return -EIO;
	}

	/* rising SAA7134_GPIO_GPRESCAN reads the status */

	saa_clearb(SAA7134_GPIO_GPMODE3, SAA7134_GPIO_GPRESCAN);
	saa_setb(SAA7134_GPIO_GPMODE3, SAA7134_GPIO_GPRESCAN);

	gpio = saa_readl(SAA7134_GPIO_GPSTATUS0 >> 2);

	/* GPIO&0x100 is pulsed low when a button is pressed. Don't do
	   I2C receive if gpio&0x100 is not low. */

	if (gpio & 0x100)
		return 0;       /* No button press */

	/* GPIO says there is a button press. Get it. */

	if (1 != i2c_master_recv(ir->c, &b, 1)) {
		i2cdprintk("read error\n");
		return -EIO;
	}

	/* No button press */

	if (b == 0xff)
		return 0;

	/* Button pressed */

	dprintk("get_key_kworld_pc150u: Key = 0x%02X\n", b);
	*protocol = RC_TYPE_UNKNOWN;
	*scancode = b;
	*toggle = 0;
	return 1;
}

static int get_key_purpletv(struct IR_i2c *ir, enum rc_type *protocol,
			    u32 *scancode, u8 *toggle)
{
	unsigned char b;

	/* poll IR chip */
	if (1 != i2c_master_recv(ir->c, &b, 1)) {
		i2cdprintk("read error\n");
		return -EIO;
	}

	/* no button press */
	if (b==0)
		return 0;

	/* repeating */
	if (b & 0x80)
		return 1;

	*protocol = RC_TYPE_UNKNOWN;
	*scancode = b;
	*toggle = 0;
	return 1;
}

static int get_key_hvr1110(struct IR_i2c *ir, enum rc_type *protocol,
			   u32 *scancode, u8 *toggle)
{
	unsigned char buf[5];

	/* poll IR chip */
	if (5 != i2c_master_recv(ir->c, buf, 5))
		return -EIO;

	/* Check if some key were pressed */
	if (!(buf[0] & 0x80))
		return 0;

	/*
	 * buf[3] & 0x80 is always high.
	 * buf[3] & 0x40 is a parity bit. A repeat event is marked
	 * by preserving it into two separate readings
	 * buf[4] bits 0 and 1, and buf[1] and buf[2] are always
	 * zero.
	 *
	 * Note that the keymap which the hvr1110 uses is RC5.
	 *
	 * FIXME: start bits could maybe be used...?
	 */
	*protocol = RC_TYPE_RC5;
	*scancode = RC_SCANCODE_RC5(buf[3] & 0x1f, buf[4] >> 2);
	*toggle = !!(buf[3] & 0x40);
	return 1;
}


static int get_key_beholdm6xx(struct IR_i2c *ir, enum rc_type *protocol,
			      u32 *scancode, u8 *toggle)
{
	unsigned char data[12];
	u32 gpio;

	struct saa7134_dev *dev = ir->c->adapter->algo_data;

	/* rising SAA7134_GPIO_GPRESCAN reads the status */
	saa_clearb(SAA7134_GPIO_GPMODE3, SAA7134_GPIO_GPRESCAN);
	saa_setb(SAA7134_GPIO_GPMODE3, SAA7134_GPIO_GPRESCAN);

	gpio = saa_readl(SAA7134_GPIO_GPSTATUS0 >> 2);

	if (0x400000 & ~gpio)
		return 0; /* No button press */

	ir->c->addr = 0x5a >> 1;

	if (12 != i2c_master_recv(ir->c, data, 12)) {
		i2cdprintk("read error\n");
		return -EIO;
	}

	if (data[9] != (unsigned char)(~data[8]))
		return 0;

	*protocol = RC_TYPE_NEC;
	*scancode = RC_SCANCODE_NECX(data[11] << 8 | data[10], data[9]);
	*toggle = 0;
	return 1;
}

/* Common (grey or coloured) pinnacle PCTV remote handling
 *
 */
static int get_key_pinnacle(struct IR_i2c *ir, enum rc_type *protocol,
			    u32 *scancode, u8 *toggle, int parity_offset,
			    int marker, int code_modulo)
{
	unsigned char b[4];
	unsigned int start = 0,parity = 0,code = 0;

	/* poll IR chip */
	if (4 != i2c_master_recv(ir->c, b, 4)) {
		i2cdprintk("read error\n");
		return -EIO;
	}

	for (start = 0; start < ARRAY_SIZE(b); start++) {
		if (b[start] == marker) {
			code=b[(start+parity_offset + 1) % 4];
			parity=b[(start+parity_offset) % 4];
		}
	}

	/* Empty Request */
	if (parity == 0)
		return 0;

	/* Repeating... */
	if (ir->old == parity)
		return 0;

	ir->old = parity;

	/* drop special codes when a key is held down a long time for the grey controller
	   In this case, the second bit of the code is asserted */
	if (marker == 0xfe && (code & 0x40))
		return 0;

	code %= code_modulo;

	*protocol = RC_TYPE_UNKNOWN;
	*scancode = code;
	*toggle = 0;

	i2cdprintk("Pinnacle PCTV key %02x\n", code);
	return 1;
}

/* The grey pinnacle PCTV remote
 *
 *  There are one issue with this remote:
 *   - I2c packet does not change when the same key is pressed quickly. The workaround
 *     is to hold down each key for about half a second, so that another code is generated
 *     in the i2c packet, and the function can distinguish key presses.
 *
 * Sylvain Pasche <sylvain.pasche@gmail.com>
 */
static int get_key_pinnacle_grey(struct IR_i2c *ir, enum rc_type *protocol,
				 u32 *scancode, u8 *toggle)
{

	return get_key_pinnacle(ir, protocol, scancode, toggle, 1, 0xfe, 0xff);
}


/* The new pinnacle PCTV remote (with the colored buttons)
 *
 * Ricardo Cerqueira <v4l@cerqueira.org>
 */
static int get_key_pinnacle_color(struct IR_i2c *ir, enum rc_type *protocol,
				  u32 *scancode, u8 *toggle)
{
	/* code_modulo parameter (0x88) is used to reduce code value to fit inside IR_KEYTAB_SIZE
	 *
	 * this is the only value that results in 42 unique
	 * codes < 128
	 */

	return get_key_pinnacle(ir, protocol, scancode, toggle, 2, 0x80, 0x88);
}

void saa7134_input_irq(struct saa7134_dev *dev)
{
	struct saa7134_card_ir *ir;

	if (!dev || !dev->remote)
		return;

	ir = dev->remote;
	if (!ir->running)
		return;

	if (!ir->polling && !ir->raw_decode) {
		build_key(dev);
	} else if (ir->raw_decode) {
		saa7134_raw_decode_irq(dev);
	}
}

static void saa7134_input_timer(unsigned long data)
{
	struct saa7134_dev *dev = (struct saa7134_dev *)data;
	struct saa7134_card_ir *ir = dev->remote;

	build_key(dev);
	mod_timer(&ir->timer, jiffies + msecs_to_jiffies(ir->polling));
}

static void ir_raw_decode_timer_end(unsigned long data)
{
	struct saa7134_dev *dev = (struct saa7134_dev *)data;

	ir_raw_event_handle(dev->remote->dev);
}

static int __saa7134_ir_start(void *priv)
{
	struct saa7134_dev *dev = priv;
	struct saa7134_card_ir *ir;

	if (!dev || !dev->remote)
		return -EINVAL;

	ir  = dev->remote;
	if (ir->running)
		return 0;

	/* Moved here from saa7134_input_init1() because the latter
	 * is not called on device resume */
	switch (dev->board) {
	case SAA7134_BOARD_MD2819:
	case SAA7134_BOARD_KWORLD_VSTREAM_XPERT:
	case SAA7134_BOARD_AVERMEDIA_305:
	case SAA7134_BOARD_AVERMEDIA_307:
	case SAA7134_BOARD_AVERMEDIA_STUDIO_305:
	case SAA7134_BOARD_AVERMEDIA_STUDIO_505:
	case SAA7134_BOARD_AVERMEDIA_STUDIO_307:
	case SAA7134_BOARD_AVERMEDIA_STUDIO_507:
	case SAA7134_BOARD_AVERMEDIA_STUDIO_507UA:
	case SAA7134_BOARD_AVERMEDIA_GO_007_FM:
	case SAA7134_BOARD_AVERMEDIA_M102:
	case SAA7134_BOARD_AVERMEDIA_GO_007_FM_PLUS:
		/* Without this we won't receive key up events */
		saa_setb(SAA7134_GPIO_GPMODE0, 0x4);
		saa_setb(SAA7134_GPIO_GPSTATUS0, 0x4);
		break;
	case SAA7134_BOARD_AVERMEDIA_777:
	case SAA7134_BOARD_AVERMEDIA_A16AR:
		/* Without this we won't receive key up events */
		saa_setb(SAA7134_GPIO_GPMODE1, 0x1);
		saa_setb(SAA7134_GPIO_GPSTATUS1, 0x1);
		break;
	case SAA7134_BOARD_AVERMEDIA_A16D:
		/* Without this we won't receive key up events */
		saa_setb(SAA7134_GPIO_GPMODE1, 0x1);
		saa_setb(SAA7134_GPIO_GPSTATUS1, 0x1);
		break;
	case SAA7134_BOARD_GOTVIEW_7135:
		saa_setb(SAA7134_GPIO_GPMODE1, 0x80);
		break;
	}

	ir->running = true;

	if (ir->polling) {
		setup_timer(&ir->timer, saa7134_input_timer,
			    (unsigned long)dev);
		ir->timer.expires = jiffies + HZ;
		add_timer(&ir->timer);
	} else if (ir->raw_decode) {
		/* set timer_end for code completion */
		setup_timer(&ir->timer, ir_raw_decode_timer_end,
			    (unsigned long)dev);
	}

	return 0;
}

static void __saa7134_ir_stop(void *priv)
{
	struct saa7134_dev *dev = priv;
	struct saa7134_card_ir *ir;

	if (!dev || !dev->remote)
		return;

	ir  = dev->remote;
	if (!ir->running)
		return;

	if (ir->polling || ir->raw_decode)
		del_timer_sync(&ir->timer);

	ir->running = false;

	return;
}

int saa7134_ir_start(struct saa7134_dev *dev)
{
	if (dev->remote->users)
		return __saa7134_ir_start(dev);

	return 0;
}

void saa7134_ir_stop(struct saa7134_dev *dev)
{
	if (dev->remote->users)
		__saa7134_ir_stop(dev);
}

static int saa7134_ir_open(struct rc_dev *rc)
{
	struct saa7134_dev *dev = rc->priv;

	dev->remote->users++;
	return __saa7134_ir_start(dev);
}

static void saa7134_ir_close(struct rc_dev *rc)
{
	struct saa7134_dev *dev = rc->priv;

	dev->remote->users--;
	if (!dev->remote->users)
		__saa7134_ir_stop(dev);
}

int saa7134_input_init1(struct saa7134_dev *dev)
{
	struct saa7134_card_ir *ir;
	struct rc_dev *rc;
	char *ir_codes = NULL;
	u32 mask_keycode = 0;
	u32 mask_keydown = 0;
	u32 mask_keyup   = 0;
	unsigned polling = 0;
	bool raw_decode  = false;
	int err;

	if (dev->has_remote != SAA7134_REMOTE_GPIO)
		return -ENODEV;
	if (disable_ir)
		return -ENODEV;

	/* detect & configure */
	switch (dev->board) {
	case SAA7134_BOARD_FLYVIDEO2000:
	case SAA7134_BOARD_FLYVIDEO3000:
	case SAA7134_BOARD_FLYTVPLATINUM_FM:
	case SAA7134_BOARD_FLYTVPLATINUM_MINI2:
	case SAA7134_BOARD_ROVERMEDIA_LINK_PRO_FM:
		ir_codes     = RC_MAP_FLYVIDEO;
		mask_keycode = 0xEC00000;
		mask_keydown = 0x0040000;
		break;
	case SAA7134_BOARD_CINERGY400:
	case SAA7134_BOARD_CINERGY600:
	case SAA7134_BOARD_CINERGY600_MK3:
		ir_codes     = RC_MAP_CINERGY;
		mask_keycode = 0x00003f;
		mask_keyup   = 0x040000;
		break;
	case SAA7134_BOARD_ECS_TVP3XP:
	case SAA7134_BOARD_ECS_TVP3XP_4CB5:
		ir_codes     = RC_MAP_EZTV;
		mask_keycode = 0x00017c;
		mask_keyup   = 0x000002;
		polling      = 50; // ms
		break;
	case SAA7134_BOARD_KWORLD_XPERT:
	case SAA7134_BOARD_AVACSSMARTTV:
		ir_codes     = RC_MAP_PIXELVIEW;
		mask_keycode = 0x00001F;
		mask_keyup   = 0x000020;
		polling      = 50; // ms
		break;
	case SAA7134_BOARD_MD2819:
	case SAA7134_BOARD_KWORLD_VSTREAM_XPERT:
	case SAA7134_BOARD_AVERMEDIA_305:
	case SAA7134_BOARD_AVERMEDIA_307:
	case SAA7134_BOARD_AVERMEDIA_STUDIO_305:
	case SAA7134_BOARD_AVERMEDIA_STUDIO_505:
	case SAA7134_BOARD_AVERMEDIA_STUDIO_307:
	case SAA7134_BOARD_AVERMEDIA_STUDIO_507:
	case SAA7134_BOARD_AVERMEDIA_STUDIO_507UA:
	case SAA7134_BOARD_AVERMEDIA_GO_007_FM:
	case SAA7134_BOARD_AVERMEDIA_M102:
	case SAA7134_BOARD_AVERMEDIA_GO_007_FM_PLUS:
		ir_codes     = RC_MAP_AVERMEDIA;
		mask_keycode = 0x0007C8;
		mask_keydown = 0x000010;
		polling      = 50; // ms
		/* GPIO stuff moved to __saa7134_ir_start() */
		break;
	case SAA7134_BOARD_AVERMEDIA_M135A:
		ir_codes     = RC_MAP_AVERMEDIA_M135A;
		mask_keydown = 0x0040000;	/* Enable GPIO18 line on both edges */
		mask_keyup   = 0x0040000;
		mask_keycode = 0xffff;
		raw_decode   = true;
		break;
	case SAA7134_BOARD_AVERMEDIA_M733A:
		ir_codes     = RC_MAP_AVERMEDIA_M733A_RM_K6;
		mask_keydown = 0x0040000;
		mask_keyup   = 0x0040000;
		mask_keycode = 0xffff;
		raw_decode   = true;
		break;
	case SAA7134_BOARD_AVERMEDIA_777:
	case SAA7134_BOARD_AVERMEDIA_A16AR:
		ir_codes     = RC_MAP_AVERMEDIA;
		mask_keycode = 0x02F200;
		mask_keydown = 0x000400;
		polling      = 50; // ms
		/* GPIO stuff moved to __saa7134_ir_start() */
		break;
	case SAA7134_BOARD_AVERMEDIA_A16D:
		ir_codes     = RC_MAP_AVERMEDIA_A16D;
		mask_keycode = 0x02F200;
		mask_keydown = 0x000400;
		polling      = 50; /* ms */
		/* GPIO stuff moved to __saa7134_ir_start() */
		break;
	case SAA7134_BOARD_KWORLD_TERMINATOR:
		ir_codes     = RC_MAP_PIXELVIEW;
		mask_keycode = 0x00001f;
		mask_keyup   = 0x000060;
		polling      = 50; // ms
		break;
	case SAA7134_BOARD_MANLI_MTV001:
	case SAA7134_BOARD_MANLI_MTV002:
		ir_codes     = RC_MAP_MANLI;
		mask_keycode = 0x001f00;
		mask_keyup   = 0x004000;
		polling      = 50; /* ms */
		break;
	case SAA7134_BOARD_BEHOLD_409FM:
	case SAA7134_BOARD_BEHOLD_401:
	case SAA7134_BOARD_BEHOLD_403:
	case SAA7134_BOARD_BEHOLD_403FM:
	case SAA7134_BOARD_BEHOLD_405:
	case SAA7134_BOARD_BEHOLD_405FM:
	case SAA7134_BOARD_BEHOLD_407:
	case SAA7134_BOARD_BEHOLD_407FM:
	case SAA7134_BOARD_BEHOLD_409:
	case SAA7134_BOARD_BEHOLD_505FM:
	case SAA7134_BOARD_BEHOLD_505RDS_MK5:
	case SAA7134_BOARD_BEHOLD_505RDS_MK3:
	case SAA7134_BOARD_BEHOLD_507_9FM:
	case SAA7134_BOARD_BEHOLD_507RDS_MK3:
	case SAA7134_BOARD_BEHOLD_507RDS_MK5:
		ir_codes     = RC_MAP_MANLI;
		mask_keycode = 0x003f00;
		mask_keyup   = 0x004000;
		polling      = 50; /* ms */
		break;
	case SAA7134_BOARD_BEHOLD_COLUMBUS_TVFM:
		ir_codes     = RC_MAP_BEHOLD_COLUMBUS;
		mask_keycode = 0x003f00;
		mask_keyup   = 0x004000;
		polling      = 50; // ms
		break;
	case SAA7134_BOARD_SEDNA_PC_TV_CARDBUS:
		ir_codes     = RC_MAP_PCTV_SEDNA;
		mask_keycode = 0x001f00;
		mask_keyup   = 0x004000;
		polling      = 50; // ms
		break;
	case SAA7134_BOARD_GOTVIEW_7135:
		ir_codes     = RC_MAP_GOTVIEW7135;
		mask_keycode = 0x0003CC;
		mask_keydown = 0x000010;
		polling	     = 5; /* ms */
		/* GPIO stuff moved to __saa7134_ir_start() */
		break;
	case SAA7134_BOARD_VIDEOMATE_TV_PVR:
	case SAA7134_BOARD_VIDEOMATE_GOLD_PLUS:
	case SAA7134_BOARD_VIDEOMATE_TV_GOLD_PLUSII:
		ir_codes     = RC_MAP_VIDEOMATE_TV_PVR;
		mask_keycode = 0x00003F;
		mask_keyup   = 0x400000;
		polling      = 50; // ms
		break;
	case SAA7134_BOARD_PROTEUS_2309:
		ir_codes     = RC_MAP_PROTEUS_2309;
		mask_keycode = 0x00007F;
		mask_keyup   = 0x000080;
		polling      = 50; // ms
		break;
	case SAA7134_BOARD_VIDEOMATE_DVBT_300:
	case SAA7134_BOARD_VIDEOMATE_DVBT_200:
		ir_codes     = RC_MAP_VIDEOMATE_TV_PVR;
		mask_keycode = 0x003F00;
		mask_keyup   = 0x040000;
		break;
	case SAA7134_BOARD_FLYDVBS_LR300:
	case SAA7134_BOARD_FLYDVBT_LR301:
	case SAA7134_BOARD_FLYDVBTDUO:
		ir_codes     = RC_MAP_FLYDVB;
		mask_keycode = 0x0001F00;
		mask_keydown = 0x0040000;
		break;
	case SAA7134_BOARD_ASUSTeK_P7131_DUAL:
	case SAA7134_BOARD_ASUSTeK_P7131_HYBRID_LNA:
	case SAA7134_BOARD_ASUSTeK_P7131_ANALOG:
		ir_codes     = RC_MAP_ASUS_PC39;
		mask_keydown = 0x0040000;	/* Enable GPIO18 line on both edges */
		mask_keyup   = 0x0040000;
		mask_keycode = 0xffff;
		raw_decode   = true;
		break;
	case SAA7134_BOARD_ASUSTeK_PS3_100:
		ir_codes     = RC_MAP_ASUS_PS3_100;
		mask_keydown = 0x0040000;
		mask_keyup   = 0x0040000;
		mask_keycode = 0xffff;
		raw_decode   = true;
		break;
	case SAA7134_BOARD_ENCORE_ENLTV:
	case SAA7134_BOARD_ENCORE_ENLTV_FM:
		ir_codes     = RC_MAP_ENCORE_ENLTV;
		mask_keycode = 0x00007f;
		mask_keyup   = 0x040000;
		polling      = 50; // ms
		break;
	case SAA7134_BOARD_ENCORE_ENLTV_FM53:
	case SAA7134_BOARD_ENCORE_ENLTV_FM3:
		ir_codes     = RC_MAP_ENCORE_ENLTV_FM53;
		mask_keydown = 0x0040000;	/* Enable GPIO18 line on both edges */
		mask_keyup   = 0x0040000;
		mask_keycode = 0xffff;
		raw_decode   = true;
		break;
	case SAA7134_BOARD_10MOONSTVMASTER3:
		ir_codes     = RC_MAP_ENCORE_ENLTV;
		mask_keycode = 0x5f80000;
		mask_keyup   = 0x8000000;
		polling      = 50; //ms
		break;
	case SAA7134_BOARD_GENIUS_TVGO_A11MCE:
		ir_codes     = RC_MAP_GENIUS_TVGO_A11MCE;
		mask_keycode = 0xff;
		mask_keydown = 0xf00000;
		polling = 50; /* ms */
		break;
	case SAA7134_BOARD_REAL_ANGEL_220:
		ir_codes     = RC_MAP_REAL_AUDIO_220_32_KEYS;
		mask_keycode = 0x3f00;
		mask_keyup   = 0x4000;
		polling = 50; /* ms */
		break;
	case SAA7134_BOARD_KWORLD_PLUS_TV_ANALOG:
		ir_codes     = RC_MAP_KWORLD_PLUS_TV_ANALOG;
		mask_keycode = 0x7f;
		polling = 40; /* ms */
		break;
	case SAA7134_BOARD_VIDEOMATE_S350:
		ir_codes     = RC_MAP_VIDEOMATE_S350;
		mask_keycode = 0x003f00;
		mask_keydown = 0x040000;
		break;
	case SAA7134_BOARD_LEADTEK_WINFAST_DTV1000S:
		ir_codes     = RC_MAP_WINFAST;
		mask_keycode = 0x5f00;
		mask_keyup   = 0x020000;
		polling      = 50; /* ms */
		break;
	case SAA7134_BOARD_VIDEOMATE_M1F:
		ir_codes     = RC_MAP_VIDEOMATE_K100;
		mask_keycode = 0x0ff00;
		mask_keyup   = 0x040000;
		break;
	case SAA7134_BOARD_HAUPPAUGE_HVR1150:
	case SAA7134_BOARD_HAUPPAUGE_HVR1120:
		ir_codes     = RC_MAP_HAUPPAUGE;
		mask_keydown = 0x0040000;	/* Enable GPIO18 line on both edges */
		mask_keyup   = 0x0040000;
		mask_keycode = 0xffff;
		raw_decode   = true;
		break;
	}
	if (NULL == ir_codes) {
		printk("%s: Oops: IR config error [card=%d]\n",
		       dev->name, dev->board);
		return -ENODEV;
	}

	ir = kzalloc(sizeof(*ir), GFP_KERNEL);
	rc = rc_allocate_device();
	if (!ir || !rc) {
		err = -ENOMEM;
		goto err_out_free;
	}

	ir->dev = rc;
	dev->remote = ir;

	/* init hardware-specific stuff */
	ir->mask_keycode = mask_keycode;
	ir->mask_keydown = mask_keydown;
	ir->mask_keyup   = mask_keyup;
	ir->polling      = polling;
	ir->raw_decode	 = raw_decode;

	/* init input device */
	snprintf(ir->name, sizeof(ir->name), "saa7134 IR (%s)",
		 saa7134_boards[dev->board].name);
	snprintf(ir->phys, sizeof(ir->phys), "pci-%s/ir0",
		 pci_name(dev->pci));

	rc->priv = dev;
	rc->open = saa7134_ir_open;
	rc->close = saa7134_ir_close;
	if (raw_decode)
		rc->driver_type = RC_DRIVER_IR_RAW;

	rc->input_name = ir->name;
	rc->input_phys = ir->phys;
	rc->input_id.bustype = BUS_PCI;
	rc->input_id.version = 1;
	if (dev->pci->subsystem_vendor) {
		rc->input_id.vendor  = dev->pci->subsystem_vendor;
		rc->input_id.product = dev->pci->subsystem_device;
	} else {
		rc->input_id.vendor  = dev->pci->vendor;
		rc->input_id.product = dev->pci->device;
	}
	rc->dev.parent = &dev->pci->dev;
	rc->map_name = ir_codes;
	rc->driver_name = MODULE_NAME;

	err = rc_register_device(rc);
	if (err)
		goto err_out_free;

	return 0;

err_out_free:
	rc_free_device(rc);
	dev->remote = NULL;
	kfree(ir);
	return err;
}

void saa7134_input_fini(struct saa7134_dev *dev)
{
	if (NULL == dev->remote)
		return;

	saa7134_ir_stop(dev);
	rc_unregister_device(dev->remote->dev);
	kfree(dev->remote);
	dev->remote = NULL;
}

void saa7134_probe_i2c_ir(struct saa7134_dev *dev)
{
	struct i2c_board_info info;
	struct i2c_msg msg_msi = {
		.addr = 0x50,
		.flags = I2C_M_RD,
		.len = 0,
		.buf = NULL,
	};
	int rc;

	if (disable_ir) {
		dprintk("IR has been disabled, not probing for i2c remote\n");
		return;
	}

	memset(&info, 0, sizeof(struct i2c_board_info));
	memset(&dev->init_data, 0, sizeof(dev->init_data));
	strlcpy(info.type, "ir_video", I2C_NAME_SIZE);

	switch (dev->board) {
	case SAA7134_BOARD_PINNACLE_PCTV_110i:
	case SAA7134_BOARD_PINNACLE_PCTV_310i:
		dev->init_data.name = "Pinnacle PCTV";
		if (pinnacle_remote == 0) {
			dev->init_data.get_key = get_key_pinnacle_color;
			dev->init_data.ir_codes = RC_MAP_PINNACLE_COLOR;
			info.addr = 0x47;
		} else {
			dev->init_data.get_key = get_key_pinnacle_grey;
			dev->init_data.ir_codes = RC_MAP_PINNACLE_GREY;
			info.addr = 0x47;
		}
		break;
	case SAA7134_BOARD_UPMOST_PURPLE_TV:
		dev->init_data.name = "Purple TV";
		dev->init_data.get_key = get_key_purpletv;
		dev->init_data.ir_codes = RC_MAP_PURPLETV;
		info.addr = 0x7a;
		break;
	case SAA7134_BOARD_MSI_TVATANYWHERE_PLUS:
		dev->init_data.name = "MSI TV@nywhere Plus";
		dev->init_data.get_key = get_key_msi_tvanywhere_plus;
		dev->init_data.ir_codes = RC_MAP_MSI_TVANYWHERE_PLUS;
		/*
		 * MSI TV@nyware Plus requires more frequent polling
		 * otherwise it will miss some keypresses
		 */
		dev->init_data.polling_interval = 50;
		info.addr = 0x30;
		/* MSI TV@nywhere Plus controller doesn't seem to
		   respond to probes unless we read something from
		   an existing device. Weird...
		   REVISIT: might no longer be needed */
		rc = i2c_transfer(&dev->i2c_adap, &msg_msi, 1);
		dprintk("probe 0x%02x @ %s: %s\n",
			msg_msi.addr, dev->i2c_adap.name,
			(1 == rc) ? "yes" : "no");
		break;
	case SAA7134_BOARD_KWORLD_PC150U:
		/* copied and modified from MSI TV@nywhere Plus */
		dev->init_data.name = "Kworld PC150-U";
		dev->init_data.get_key = get_key_kworld_pc150u;
		dev->init_data.ir_codes = RC_MAP_KWORLD_PC150U;
		info.addr = 0x30;
		/* MSI TV@nywhere Plus controller doesn't seem to
		   respond to probes unless we read something from
		   an existing device. Weird...
		   REVISIT: might no longer be needed */
		rc = i2c_transfer(&dev->i2c_adap, &msg_msi, 1);
		dprintk("probe 0x%02x @ %s: %s\n",
			msg_msi.addr, dev->i2c_adap.name,
			(1 == rc) ? "yes" : "no");
		break;
	case SAA7134_BOARD_HAUPPAUGE_HVR1110:
		dev->init_data.name = "HVR 1110";
		dev->init_data.get_key = get_key_hvr1110;
		dev->init_data.ir_codes = RC_MAP_HAUPPAUGE;
		info.addr = 0x71;
		break;
	case SAA7134_BOARD_BEHOLD_607FM_MK3:
	case SAA7134_BOARD_BEHOLD_607FM_MK5:
	case SAA7134_BOARD_BEHOLD_609FM_MK3:
	case SAA7134_BOARD_BEHOLD_609FM_MK5:
	case SAA7134_BOARD_BEHOLD_607RDS_MK3:
	case SAA7134_BOARD_BEHOLD_607RDS_MK5:
	case SAA7134_BOARD_BEHOLD_609RDS_MK3:
	case SAA7134_BOARD_BEHOLD_609RDS_MK5:
	case SAA7134_BOARD_BEHOLD_M6:
	case SAA7134_BOARD_BEHOLD_M63:
	case SAA7134_BOARD_BEHOLD_M6_EXTRA:
	case SAA7134_BOARD_BEHOLD_H6:
	case SAA7134_BOARD_BEHOLD_X7:
	case SAA7134_BOARD_BEHOLD_H7:
	case SAA7134_BOARD_BEHOLD_A7:
		dev->init_data.name = "BeholdTV";
		dev->init_data.get_key = get_key_beholdm6xx;
		dev->init_data.ir_codes = RC_MAP_BEHOLD;
		dev->init_data.type = RC_BIT_NEC;
		info.addr = 0x2d;
		break;
	case SAA7134_BOARD_AVERMEDIA_CARDBUS_501:
	case SAA7134_BOARD_AVERMEDIA_CARDBUS_506:
		info.addr = 0x40;
		break;
	case SAA7134_BOARD_AVERMEDIA_A706:
		info.addr = 0x41;
		break;
	case SAA7134_BOARD_FLYDVB_TRIO:
		dev->init_data.name = "FlyDVB Trio";
		dev->init_data.get_key = get_key_flydvb_trio;
		dev->init_data.ir_codes = RC_MAP_FLYDVB;
		info.addr = 0x0b;
		break;
	default:
		dprintk("No I2C IR support for board %x\n", dev->board);
		return;
	}

	if (dev->init_data.name)
		info.platform_data = &dev->init_data;
	i2c_new_device(&dev->i2c_adap, &info);
}

static int saa7134_raw_decode_irq(struct saa7134_dev *dev)
{
	struct saa7134_card_ir *ir = dev->remote;
	unsigned long timeout;
	int space;

	/* Generate initial event */
	saa_clearb(SAA7134_GPIO_GPMODE3, SAA7134_GPIO_GPRESCAN);
	saa_setb(SAA7134_GPIO_GPMODE3, SAA7134_GPIO_GPRESCAN);
	space = saa_readl(SAA7134_GPIO_GPSTATUS0 >> 2) & ir->mask_keydown;
	ir_raw_event_store_edge(dev->remote->dev, space ? IR_SPACE : IR_PULSE);

	/*
	 * Wait 15 ms from the start of the first IR event before processing
	 * the event. This time is enough for NEC protocol. May need adjustments
	 * to work with other protocols.
	 */
	smp_mb();

	if (!timer_pending(&ir->timer)) {
		timeout = jiffies + msecs_to_jiffies(15);
		mod_timer(&ir->timer, timeout);
	}

	return 1;
}
