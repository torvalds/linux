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
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/input.h>

#include "saa7134-reg.h"
#include "saa7134.h"

static unsigned int disable_ir = 0;
module_param(disable_ir, int, 0444);
MODULE_PARM_DESC(disable_ir,"disable infrared remote support");

static unsigned int ir_debug = 0;
module_param(ir_debug, int, 0644);
MODULE_PARM_DESC(ir_debug,"enable debug messages [IR]");

static int pinnacle_remote = 0;
module_param(pinnacle_remote, int, 0644);    /* Choose Pinnacle PCTV remote */
MODULE_PARM_DESC(pinnacle_remote, "Specify Pinnacle PCTV remote: 0=coloured, 1=grey (defaults to 0)");

#define dprintk(fmt, arg...)	if (ir_debug) \
	printk(KERN_DEBUG "%s/ir: " fmt, dev->name , ## arg)
#define i2cdprintk(fmt, arg...)    if (ir_debug) \
	printk(KERN_DEBUG "%s/ir: " fmt, ir->c.name , ## arg)

/* -------------------- GPIO generic keycode builder -------------------- */

static int build_key(struct saa7134_dev *dev)
{
	struct saa7134_ir *ir = dev->remote;
	u32 gpio, data;

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

void saa7134_input_irq(struct saa7134_dev *dev)
{
	struct saa7134_ir *ir = dev->remote;

	if (!ir->polling)
		build_key(dev);
}

static void saa7134_input_timer(unsigned long data)
{
	struct saa7134_dev *dev = (struct saa7134_dev*)data;
	struct saa7134_ir *ir = dev->remote;
	unsigned long timeout;

	build_key(dev);
	timeout = jiffies + (ir->polling * HZ / 1000);
	mod_timer(&ir->timer, timeout);
}

static void saa7134_ir_start(struct saa7134_dev *dev, struct saa7134_ir *ir)
{
	if (ir->polling) {
		init_timer(&ir->timer);
		ir->timer.function = saa7134_input_timer;
		ir->timer.data     = (unsigned long)dev;
		ir->timer.expires  = jiffies + HZ;
		add_timer(&ir->timer);
	}
}

static void saa7134_ir_stop(struct saa7134_dev *dev)
{
	if (dev->remote->polling)
		del_timer_sync(&dev->remote->timer);
}

int saa7134_input_init1(struct saa7134_dev *dev)
{
	struct saa7134_ir *ir;
	struct input_dev *input_dev;
	IR_KEYTAB_TYPE *ir_codes = NULL;
	u32 mask_keycode = 0;
	u32 mask_keydown = 0;
	u32 mask_keyup   = 0;
	int polling      = 0;
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
	case SAA7134_BOARD_KWORLD_TERMINATOR:
		ir_codes     = ir_codes_pixelview;
		mask_keycode = 0x00001f;
		mask_keyup   = 0x000060;
		polling      = 50; // ms
		break;
	case SAA7134_BOARD_MANLI_MTV001:
	case SAA7134_BOARD_MANLI_MTV002:
	case SAA7134_BOARD_BEHOLD_409FM:
		ir_codes     = ir_codes_manli;
		mask_keycode = 0x001f00;
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
		mask_keycode = 0x0003EC;
		mask_keyup   = 0x008000;
		mask_keydown = 0x000010;
		polling	     = 50; // ms
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
	case SAA7134_BOARD_FLYDVBT_LR301:
	case SAA7134_BOARD_FLYDVBTDUO:
		ir_codes     = ir_codes_flydvb;
		mask_keycode = 0x0001F00;
		mask_keydown = 0x0040000;
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
	input_dev->cdev.dev = &dev->pci->dev;

	dev->remote = ir;
	saa7134_ir_start(dev, ir);

	err = input_register_device(ir->dev);
	if (err)
		goto err_out_stop;

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
	default:
		dprintk("Shouldn't get here: Unknown board %x for I2C IR?\n",dev->board);
		break;
	}

}
/* ----------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
