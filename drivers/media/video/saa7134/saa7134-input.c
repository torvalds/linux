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
#include <linux/input.h>

#include "saa7134-reg.h"
#include "saa7134.h"

static unsigned int disable_ir;
module_param(disable_ir, int, 0444);
MODULE_PARM_DESC(disable_ir,"disable infrared remote support");

static unsigned int ir_debug;
module_param(ir_debug, int, 0644);
MODULE_PARM_DESC(ir_debug,"enable debug messages [IR]");

static int pinnacle_remote;
module_param(pinnacle_remote, int, 0644);    /* Choose Pinnacle PCTV remote */
MODULE_PARM_DESC(pinnacle_remote, "Specify Pinnacle PCTV remote: 0=coloured, 1=grey (defaults to 0)");

static int ir_rc5_remote_gap = 885;
module_param(ir_rc5_remote_gap, int, 0644);
static int ir_rc5_key_timeout = 115;
module_param(ir_rc5_key_timeout, int, 0644);

static int repeat_delay = 500;
module_param(repeat_delay, int, 0644);
MODULE_PARM_DESC(repeat_delay, "delay before key repeat started");
static int repeat_period = 33;
module_param(repeat_period, int, 0644);
MODULE_PARM_DESC(repeat_period, "repeat period between "
    "keypresses when key is down");

static unsigned int disable_other_ir;
module_param(disable_other_ir, int, 0644);
MODULE_PARM_DESC(disable_other_ir, "disable full codes of "
    "alternative remotes from other manufacturers");

#define dprintk(fmt, arg...)	if (ir_debug) \
	printk(KERN_DEBUG "%s/ir: " fmt, dev->name , ## arg)
#define i2cdprintk(fmt, arg...)    if (ir_debug) \
	printk(KERN_DEBUG "%s/ir: " fmt, ir->c.name , ## arg)

/** rc5 functions */
static int saa7134_rc5_irq(struct saa7134_dev *dev);

/* -------------------- GPIO generic keycode builder -------------------- */

static int build_key(struct saa7134_dev *dev)
{
	struct card_ir *ir = dev->remote;
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

	if (ir->polling) {
		if ((ir->mask_keydown  &&  (0 != (gpio & ir->mask_keydown))) ||
		    (ir->mask_keyup    &&  (0 == (gpio & ir->mask_keyup)))) {
			ir_input_keydown(ir->dev, &ir->ir, data, data);
		} else {
			ir_input_nokey(ir->dev, &ir->ir);
		}
	}
	else {	/* IRQ driven mode - handle key press and release in one go */
		if ((ir->mask_keydown  &&  (0 != (gpio & ir->mask_keydown))) ||
		    (ir->mask_keyup    &&  (0 == (gpio & ir->mask_keyup)))) {
			ir_input_keydown(ir->dev, &ir->ir, data, data);
			ir_input_nokey(ir->dev, &ir->ir);
		}
	}

	return 0;
}

/* --------------------- Chip specific I2C key builders ----------------- */

static int get_key_purpletv(struct IR_i2c *ir, u32 *ir_key, u32 *ir_raw)
{
	unsigned char b;

	/* poll IR chip */
	if (1 != i2c_master_recv(&ir->c,&b,1)) {
		i2cdprintk("read error\n");
		return -EIO;
	}

	/* no button press */
	if (b==0)
		return 0;

	/* repeating */
	if (b & 0x80)
		return 1;

	*ir_key = b;
	*ir_raw = b;
	return 1;
}

static int get_key_hvr1110(struct IR_i2c *ir, u32 *ir_key, u32 *ir_raw)
{
	unsigned char buf[5], cod4, code3, code4;

	/* poll IR chip */
	if (5 != i2c_master_recv(&ir->c,buf,5))
		return -EIO;

	cod4	= buf[4];
	code4	= (cod4 >> 2);
	code3	= buf[3];
	if (code3 == 0)
		/* no key pressed */
		return 0;

	/* return key */
	*ir_key = code4;
	*ir_raw = code4;
	return 1;
}


static int get_key_beholdm6xx(struct IR_i2c *ir, u32 *ir_key, u32 *ir_raw)
{
	unsigned char data[12];
	u32 gpio;

	struct saa7134_dev *dev = ir->c.adapter->algo_data;

	/* rising SAA7134_GPIO_GPRESCAN reads the status */
	saa_clearb(SAA7134_GPIO_GPMODE3, SAA7134_GPIO_GPRESCAN);
	saa_setb(SAA7134_GPIO_GPMODE3, SAA7134_GPIO_GPRESCAN);

	gpio = saa_readl(SAA7134_GPIO_GPSTATUS0 >> 2);

	if (0x400000 & ~gpio)
		return 0; /* No button press */

	ir->c.addr = 0x5a >> 1;

	if (12 != i2c_master_recv(&ir->c, data, 12)) {
		i2cdprintk("read error\n");
		return -EIO;
	}
	/* IR of this card normally decode signals NEC-standard from
	 * - Sven IHOO MT 5.1R remote. xxyye718
	 * - Sven DVD HD-10xx remote. xxyyf708
	 * - BBK ...
	 * - mayby others
	 * So, skip not our, if disable full codes mode.
	 */
	if (data[10] != 0x6b && data[11] != 0x86 && disable_other_ir)
		return 0;

	*ir_key = data[9];
	*ir_raw = data[9];

	return 1;
}

/* Common (grey or coloured) pinnacle PCTV remote handling
 *
 */
static int get_key_pinnacle(struct IR_i2c *ir, u32 *ir_key, u32 *ir_raw,
			    int parity_offset, int marker, int code_modulo)
{
	unsigned char b[4];
	unsigned int start = 0,parity = 0,code = 0;

	/* poll IR chip */
	if (4 != i2c_master_recv(&ir->c, b, 4)) {
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

	*ir_raw = code;
	*ir_key = code;

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
static int get_key_pinnacle_grey(struct IR_i2c *ir, u32 *ir_key, u32 *ir_raw)
{

	return get_key_pinnacle(ir, ir_key, ir_raw, 1, 0xfe, 0xff);
}


/* The new pinnacle PCTV remote (with the colored buttons)
 *
 * Ricardo Cerqueira <v4l@cerqueira.org>
 */
static int get_key_pinnacle_color(struct IR_i2c *ir, u32 *ir_key, u32 *ir_raw)
{
	/* code_modulo parameter (0x88) is used to reduce code value to fit inside IR_KEYTAB_SIZE
	 *
	 * this is the only value that results in 42 unique
	 * codes < 128
	 */

	return get_key_pinnacle(ir, ir_key, ir_raw, 2, 0x80, 0x88);
}

void saa7134_input_irq(struct saa7134_dev *dev)
{
	struct card_ir *ir = dev->remote;

	if (!ir->polling && !ir->rc5_gpio) {
		build_key(dev);
	} else if (ir->rc5_gpio) {
		saa7134_rc5_irq(dev);
	}
}

static void saa7134_input_timer(unsigned long data)
{
	struct saa7134_dev *dev = (struct saa7134_dev *)data;
	struct card_ir *ir = dev->remote;

	build_key(dev);
	mod_timer(&ir->timer, jiffies + msecs_to_jiffies(ir->polling));
}

void saa7134_ir_start(struct saa7134_dev *dev, struct card_ir *ir)
{
	if (ir->polling) {
		setup_timer(&ir->timer, saa7134_input_timer,
			    (unsigned long)dev);
		ir->timer.expires  = jiffies + HZ;
		add_timer(&ir->timer);
	} else if (ir->rc5_gpio) {
		/* set timer_end for code completion */
		init_timer(&ir->timer_end);
		ir->timer_end.function = ir_rc5_timer_end;
		ir->timer_end.data = (unsigned long)ir;
		init_timer(&ir->timer_keyup);
		ir->timer_keyup.function = ir_rc5_timer_keyup;
		ir->timer_keyup.data = (unsigned long)ir;
		ir->shift_by = 2;
		ir->start = 0x2;
		ir->addr = 0x17;
		ir->rc5_key_timeout = ir_rc5_key_timeout;
		ir->rc5_remote_gap = ir_rc5_remote_gap;
	}
}

void saa7134_ir_stop(struct saa7134_dev *dev)
{
	if (dev->remote->polling)
		del_timer_sync(&dev->remote->timer);
}

int saa7134_input_init1(struct saa7134_dev *dev)
{
	struct card_ir *ir;
	struct input_dev *input_dev;
	IR_KEYTAB_TYPE *ir_codes = NULL;
	u32 mask_keycode = 0;
	u32 mask_keydown = 0;
	u32 mask_keyup   = 0;
	int polling      = 0;
	int rc5_gpio	 = 0;
	int ir_type      = IR_TYPE_OTHER;
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
		ir_codes     = ir_codes_flyvideo;
		mask_keycode = 0xEC00000;
		mask_keydown = 0x0040000;
		break;
	case SAA7134_BOARD_CINERGY400:
	case SAA7134_BOARD_CINERGY600:
	case SAA7134_BOARD_CINERGY600_MK3:
		ir_codes     = ir_codes_cinergy;
		mask_keycode = 0x00003f;
		mask_keyup   = 0x040000;
		break;
	case SAA7134_BOARD_ECS_TVP3XP:
	case SAA7134_BOARD_ECS_TVP3XP_4CB5:
		ir_codes     = ir_codes_eztv;
		mask_keycode = 0x00017c;
		mask_keyup   = 0x000002;
		polling      = 50; // ms
		break;
	case SAA7134_BOARD_KWORLD_XPERT:
	case SAA7134_BOARD_AVACSSMARTTV:
		ir_codes     = ir_codes_pixelview;
		mask_keycode = 0x00001F;
		mask_keyup   = 0x000020;
		polling      = 50; // ms
		break;
	case SAA7134_BOARD_MD2819:
	case SAA7134_BOARD_KWORLD_VSTREAM_XPERT:
	case SAA7134_BOARD_AVERMEDIA_305:
	case SAA7134_BOARD_AVERMEDIA_307:
	case SAA7134_BOARD_AVERMEDIA_STUDIO_305:
	case SAA7134_BOARD_AVERMEDIA_STUDIO_307:
	case SAA7134_BOARD_AVERMEDIA_STUDIO_507:
	case SAA7134_BOARD_AVERMEDIA_GO_007_FM:
	case SAA7134_BOARD_AVERMEDIA_M102:
		ir_codes     = ir_codes_avermedia;
		mask_keycode = 0x0007C8;
		mask_keydown = 0x000010;
		polling      = 50; // ms
		/* Set GPIO pin2 to high to enable the IR controller */
		saa_setb(SAA7134_GPIO_GPMODE0, 0x4);
		saa_setb(SAA7134_GPIO_GPSTATUS0, 0x4);
		break;
	case SAA7134_BOARD_AVERMEDIA_777:
	case SAA7134_BOARD_AVERMEDIA_A16AR:
		ir_codes     = ir_codes_avermedia;
		mask_keycode = 0x02F200;
		mask_keydown = 0x000400;
		polling      = 50; // ms
		/* Without this we won't receive key up events */
		saa_setb(SAA7134_GPIO_GPMODE1, 0x1);
		saa_setb(SAA7134_GPIO_GPSTATUS1, 0x1);
		break;
	case SAA7134_BOARD_AVERMEDIA_A16D:
		ir_codes     = ir_codes_avermedia_a16d;
		mask_keycode = 0x02F200;
		mask_keydown = 0x000400;
		polling      = 50; /* ms */
		/* Without this we won't receive key up events */
		saa_setb(SAA7134_GPIO_GPMODE1, 0x1);
		saa_setb(SAA7134_GPIO_GPSTATUS1, 0x1);
		break;
	case SAA7134_BOARD_KWORLD_TERMINATOR:
		ir_codes     = ir_codes_pixelview;
		mask_keycode = 0x00001f;
		mask_keyup   = 0x000060;
		polling      = 50; // ms
		break;
	case SAA7134_BOARD_MANLI_MTV001:
	case SAA7134_BOARD_MANLI_MTV002:
		ir_codes     = ir_codes_manli;
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
	case SAA7134_BOARD_BEHOLD_507_9FM:
		ir_codes     = ir_codes_manli;
		mask_keycode = 0x003f00;
		mask_keyup   = 0x004000;
		polling      = 50; /* ms */
		break;
	case SAA7134_BOARD_BEHOLD_COLUMBUS_TVFM:
		ir_codes     = ir_codes_behold_columbus;
		mask_keycode = 0x003f00;
		mask_keyup   = 0x004000;
		polling      = 50; // ms
		break;
	case SAA7134_BOARD_SEDNA_PC_TV_CARDBUS:
		ir_codes     = ir_codes_pctv_sedna;
		mask_keycode = 0x001f00;
		mask_keyup   = 0x004000;
		polling      = 50; // ms
		break;
	case SAA7134_BOARD_GOTVIEW_7135:
		ir_codes     = ir_codes_gotview7135;
		mask_keycode = 0x0003CC;
		mask_keydown = 0x000010;
		polling	     = 5; /* ms */
		saa_setb(SAA7134_GPIO_GPMODE1, 0x80);
		break;
	case SAA7134_BOARD_VIDEOMATE_TV_PVR:
	case SAA7134_BOARD_VIDEOMATE_GOLD_PLUS:
	case SAA7134_BOARD_VIDEOMATE_TV_GOLD_PLUSII:
		ir_codes     = ir_codes_videomate_tv_pvr;
		mask_keycode = 0x00003F;
		mask_keyup   = 0x400000;
		polling      = 50; // ms
		break;
	case SAA7134_BOARD_PROTEUS_2309:
		ir_codes     = ir_codes_proteus_2309;
		mask_keycode = 0x00007F;
		mask_keyup   = 0x000080;
		polling      = 50; // ms
		break;
	case SAA7134_BOARD_VIDEOMATE_DVBT_300:
	case SAA7134_BOARD_VIDEOMATE_DVBT_200:
		ir_codes     = ir_codes_videomate_tv_pvr;
		mask_keycode = 0x003F00;
		mask_keyup   = 0x040000;
		break;
	case SAA7134_BOARD_FLYDVBS_LR300:
	case SAA7134_BOARD_FLYDVBT_LR301:
	case SAA7134_BOARD_FLYDVBTDUO:
		ir_codes     = ir_codes_flydvb;
		mask_keycode = 0x0001F00;
		mask_keydown = 0x0040000;
		break;
	case SAA7134_BOARD_ASUSTeK_P7131_DUAL:
	case SAA7134_BOARD_ASUSTeK_P7131_HYBRID_LNA:
       case SAA7134_BOARD_ASUSTeK_P7131_ANALOG:
		ir_codes     = ir_codes_asus_pc39;
		mask_keydown = 0x0040000;
		rc5_gpio = 1;
		break;
	case SAA7134_BOARD_ENCORE_ENLTV:
	case SAA7134_BOARD_ENCORE_ENLTV_FM:
		ir_codes     = ir_codes_encore_enltv;
		mask_keycode = 0x00007f;
		mask_keyup   = 0x040000;
		polling      = 50; // ms
		break;
	case SAA7134_BOARD_10MOONSTVMASTER3:
		ir_codes     = ir_codes_encore_enltv;
		mask_keycode = 0x5f80000;
		mask_keyup   = 0x8000000;
		polling      = 50; //ms
		break;
	case SAA7134_BOARD_GENIUS_TVGO_A11MCE:
		ir_codes     = ir_codes_genius_tvgo_a11mce;
		mask_keycode = 0xff;
		mask_keydown = 0xf00000;
		polling = 50; /* ms */
		break;
	}
	if (NULL == ir_codes) {
		printk("%s: Oops: IR config error [card=%d]\n",
		       dev->name, dev->board);
		return -ENODEV;
	}

	ir = kzalloc(sizeof(*ir), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!ir || !input_dev) {
		err = -ENOMEM;
		goto err_out_free;
	}

	ir->dev = input_dev;

	/* init hardware-specific stuff */
	ir->mask_keycode = mask_keycode;
	ir->mask_keydown = mask_keydown;
	ir->mask_keyup   = mask_keyup;
	ir->polling      = polling;
	ir->rc5_gpio	 = rc5_gpio;

	/* init input device */
	snprintf(ir->name, sizeof(ir->name), "saa7134 IR (%s)",
		 saa7134_boards[dev->board].name);
	snprintf(ir->phys, sizeof(ir->phys), "pci-%s/ir0",
		 pci_name(dev->pci));

	ir_input_init(input_dev, &ir->ir, ir_type, ir_codes);
	input_dev->name = ir->name;
	input_dev->phys = ir->phys;
	input_dev->id.bustype = BUS_PCI;
	input_dev->id.version = 1;
	if (dev->pci->subsystem_vendor) {
		input_dev->id.vendor  = dev->pci->subsystem_vendor;
		input_dev->id.product = dev->pci->subsystem_device;
	} else {
		input_dev->id.vendor  = dev->pci->vendor;
		input_dev->id.product = dev->pci->device;
	}
	input_dev->dev.parent = &dev->pci->dev;

	dev->remote = ir;
	saa7134_ir_start(dev, ir);

	err = input_register_device(ir->dev);
	if (err)
		goto err_out_stop;

	/* the remote isn't as bouncy as a keyboard */
	ir->dev->rep[REP_DELAY] = repeat_delay;
	ir->dev->rep[REP_PERIOD] = repeat_period;

	return 0;

 err_out_stop:
	saa7134_ir_stop(dev);
	dev->remote = NULL;
 err_out_free:
	input_free_device(input_dev);
	kfree(ir);
	return err;
}

void saa7134_input_fini(struct saa7134_dev *dev)
{
	if (NULL == dev->remote)
		return;

	saa7134_ir_stop(dev);
	input_unregister_device(dev->remote->dev);
	kfree(dev->remote);
	dev->remote = NULL;
}

void saa7134_set_i2c_ir(struct saa7134_dev *dev, struct IR_i2c *ir)
{
	if (disable_ir) {
		dprintk("Found supported i2c remote, but IR has been disabled\n");
		ir->get_key=NULL;
		return;
	}

	switch (dev->board) {
	case SAA7134_BOARD_PINNACLE_PCTV_110i:
	case SAA7134_BOARD_PINNACLE_PCTV_310i:
		snprintf(ir->c.name, sizeof(ir->c.name), "Pinnacle PCTV");
		if (pinnacle_remote == 0) {
			ir->get_key   = get_key_pinnacle_color;
			ir->ir_codes = ir_codes_pinnacle_color;
		} else {
			ir->get_key   = get_key_pinnacle_grey;
			ir->ir_codes = ir_codes_pinnacle_grey;
		}
		break;
	case SAA7134_BOARD_UPMOST_PURPLE_TV:
		snprintf(ir->c.name, sizeof(ir->c.name), "Purple TV");
		ir->get_key   = get_key_purpletv;
		ir->ir_codes  = ir_codes_purpletv;
		break;
	case SAA7134_BOARD_HAUPPAUGE_HVR1110:
		snprintf(ir->c.name, sizeof(ir->c.name), "HVR 1110");
		ir->get_key   = get_key_hvr1110;
		ir->ir_codes  = ir_codes_hauppauge_new;
		break;
	case SAA7134_BOARD_BEHOLD_607_9FM:
	case SAA7134_BOARD_BEHOLD_M6:
	case SAA7134_BOARD_BEHOLD_M63:
	case SAA7134_BOARD_BEHOLD_M6_EXTRA:
	case SAA7134_BOARD_BEHOLD_H6:
		snprintf(ir->c.name, sizeof(ir->c.name), "BeholdTV");
		ir->get_key   = get_key_beholdm6xx;
		ir->ir_codes  = ir_codes_behold;
		break;
	default:
		dprintk("Shouldn't get here: Unknown board %x for I2C IR?\n",dev->board);
		break;
	}

}

static int saa7134_rc5_irq(struct saa7134_dev *dev)
{
	struct card_ir *ir = dev->remote;
	struct timeval tv;
	u32 gap;
	unsigned long current_jiffies, timeout;

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
			ir->last_bit = (gap - ir_rc5_remote_gap / 2) /
			    ir_rc5_remote_gap;
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

	return 1;
}

/* ----------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
