/*
 *
 * keyboard input driver for i2c IR remote controls
 *
 * Copyright (c) 2000-2003 Gerd Knorr <kraxel@bytesex.org>
 * modified for PixelView (BT878P+W/FM) by
 *      Michal Kochanowicz <mkochano@pld.org.pl>
 *      Christoph Bartelmus <lirc@bartelmus.de>
 * modified for KNC ONE TV Station/Anubis Typhoon TView Tuner by
 *      Ulrich Mueller <ulrich.mueller42@web.de>
 * modified for em2820 based USB TV tuners by
 *      Markus Rechberger <mrechberger@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/workqueue.h>
#include <asm/semaphore.h>
#include <media/ir-common.h>

static IR_KEYTAB_TYPE ir_codes_em2820[IR_KEYTAB_SIZE] = {
	[ 0x00 ] = KEY_CHANNEL,
	[ 0x01 ] = KEY_SELECT,
	[ 0x02 ] = KEY_MUTE,
	[ 0x03 ] = KEY_POWER,
	[ 0x04 ] = KEY_KP1,
	[ 0x05 ] = KEY_KP2,
	[ 0x06 ] = KEY_KP3,
	[ 0x07 ] = KEY_CHANNELUP,
	[ 0x08 ] = KEY_KP4,
	[ 0x09 ] = KEY_KP5,
	[ 0x0a ] = KEY_KP6,

	[ 0x0b ] = KEY_CHANNELDOWN,
	[ 0x0c ] = KEY_KP7,
	[ 0x0d ] = KEY_KP8,
	[ 0x0e ] = KEY_KP9,
	[ 0x0f ] = KEY_VOLUMEUP,
	[ 0x10 ] = KEY_KP0,
	[ 0x11 ] = KEY_MENU,
	[ 0x12 ] = KEY_PRINT,

	[ 0x13 ] = KEY_VOLUMEDOWN,
	[ 0x15 ] = KEY_PAUSE,
	[ 0x17 ] = KEY_RECORD,
	[ 0x18 ] = KEY_REWIND,
	[ 0x19 ] = KEY_PLAY,
	[ 0x1b ] = KEY_BACKSPACE,
	[ 0x1d ] = KEY_STOP,
	[ 0x40 ] = KEY_ZOOM,
};

/* Mark Phalan <phalanm@o2.ie> */
static IR_KEYTAB_TYPE ir_codes_pv951[IR_KEYTAB_SIZE] = {
	[ 0x00 ] = KEY_KP0,
	[ 0x01 ] = KEY_KP1,
	[ 0x02 ] = KEY_KP2,
	[ 0x03 ] = KEY_KP3,
	[ 0x04 ] = KEY_KP4,
	[ 0x05 ] = KEY_KP5,
	[ 0x06 ] = KEY_KP6,
	[ 0x07 ] = KEY_KP7,
	[ 0x08 ] = KEY_KP8,
	[ 0x09 ] = KEY_KP9,

	[ 0x12 ] = KEY_POWER,
	[ 0x10 ] = KEY_MUTE,
	[ 0x1f ] = KEY_VOLUMEDOWN,
	[ 0x1b ] = KEY_VOLUMEUP,
	[ 0x1a ] = KEY_CHANNELUP,
	[ 0x1e ] = KEY_CHANNELDOWN,
	[ 0x0e ] = KEY_PAGEUP,
	[ 0x1d ] = KEY_PAGEDOWN,
	[ 0x13 ] = KEY_SOUND,

	[ 0x18 ] = KEY_KPPLUSMINUS,	/* CH +/- */
	[ 0x16 ] = KEY_SUBTITLE,		/* CC */
	[ 0x0d ] = KEY_TEXT,		/* TTX */
	[ 0x0b ] = KEY_TV,		/* AIR/CBL */
	[ 0x11 ] = KEY_PC,		/* PC/TV */
	[ 0x17 ] = KEY_OK,		/* CH RTN */
	[ 0x19 ] = KEY_MODE, 		/* FUNC */
	[ 0x0c ] = KEY_SEARCH, 		/* AUTOSCAN */

	/* Not sure what to do with these ones! */
	[ 0x0f ] = KEY_SELECT, 		/* SOURCE */
	[ 0x0a ] = KEY_KPPLUS,		/* +100 */
	[ 0x14 ] = KEY_KPEQUAL,		/* SYNC */
	[ 0x1c ] = KEY_MEDIA,             /* PC/TV */
};

static IR_KEYTAB_TYPE ir_codes_purpletv[IR_KEYTAB_SIZE] = {
	[ 0x03 ] = KEY_POWER,
	[ 0x6f ] = KEY_MUTE,
	[ 0x10 ] = KEY_BACKSPACE,	/* Recall */

	[ 0x11 ] = KEY_KP0,
	[ 0x04 ] = KEY_KP1,
	[ 0x05 ] = KEY_KP2,
	[ 0x06 ] = KEY_KP3,
	[ 0x08 ] = KEY_KP4,
	[ 0x09 ] = KEY_KP5,
	[ 0x0a ] = KEY_KP6,
	[ 0x0c ] = KEY_KP7,
	[ 0x0d ] = KEY_KP8,
	[ 0x0e ] = KEY_KP9,
	[ 0x12 ] = KEY_KPDOT,		/* 100+ */

	[ 0x07 ] = KEY_VOLUMEUP,
	[ 0x0b ] = KEY_VOLUMEDOWN,
	[ 0x1a ] = KEY_KPPLUS,
	[ 0x18 ] = KEY_KPMINUS,
	[ 0x15 ] = KEY_UP,
	[ 0x1d ] = KEY_DOWN,
	[ 0x0f ] = KEY_CHANNELUP,
	[ 0x13 ] = KEY_CHANNELDOWN,
	[ 0x48 ] = KEY_ZOOM,

	[ 0x1b ] = KEY_VIDEO,		/* Video source */
	[ 0x49 ] = KEY_LANGUAGE,	/* MTS Select */
	[ 0x19 ] = KEY_SEARCH,		/* Auto Scan */

	[ 0x4b ] = KEY_RECORD,
	[ 0x46 ] = KEY_PLAY,
	[ 0x45 ] = KEY_PAUSE,   	/* Pause */
	[ 0x44 ] = KEY_STOP,
	[ 0x40 ] = KEY_FORWARD,   	/* Forward ? */
	[ 0x42 ] = KEY_REWIND,   	/* Backward ? */

};

struct IR {
	struct i2c_client      c;
	struct input_dev       *input;
	struct ir_input_state  ir;

	struct work_struct     work;
	struct timer_list      timer;
	char                   phys[32];
	int                    (*get_key)(struct IR*, u32*, u32*);
};

/* ----------------------------------------------------------------------- */
/* insmod parameters                                                       */

static int debug;
module_param(debug, int, 0644);    /* debug level (0,1,2) */

#define DEVNAME "ir-kbd-i2c"
#define dprintk(level, fmt, arg...)	if (debug >= level) \
	printk(KERN_DEBUG DEVNAME ": " fmt , ## arg)

#define IR_PINNACLE_REMOTE 0x01
#define IR_TERRATEC_REMOTE 0x02

/* ----------------------------------------------------------------------- */

static int get_key_haup(struct IR *ir, u32 *ir_key, u32 *ir_raw)
{
	unsigned char buf[3];
	int start, toggle, dev, code;

	/* poll IR chip */
	if (3 != i2c_master_recv(&ir->c,buf,3))
		return -EIO;

	/* split rc5 data block ... */
	start  = (buf[0] >> 6) &    3;
	toggle = (buf[0] >> 5) &    1;
	dev    =  buf[0]       & 0x1f;
	code   = (buf[1] >> 2) & 0x3f;

	if (3 != start)
		/* no key pressed */
		return 0;
	dprintk(1,"ir hauppauge (rc5): s%d t%d dev=%d code=%d\n",
		start, toggle, dev, code);

	/* return key */
	*ir_key = code;
	*ir_raw = (start << 12) | (toggle << 11) | (dev << 6) | code;
	return 1;
}

static int get_key_pixelview(struct IR *ir, u32 *ir_key, u32 *ir_raw)
{
        unsigned char b;

	/* poll IR chip */
	if (1 != i2c_master_recv(&ir->c,&b,1)) {
		dprintk(1,"read error\n");
		return -EIO;
	}
	*ir_key = b;
	*ir_raw = b;
	return 1;
}

static int get_key_pv951(struct IR *ir, u32 *ir_key, u32 *ir_raw)
{
        unsigned char b;

	/* poll IR chip */
	if (1 != i2c_master_recv(&ir->c,&b,1)) {
		dprintk(1,"read error\n");
		return -EIO;
	}

	/* ignore 0xaa */
	if (b==0xaa)
		return 0;
	dprintk(2,"key %02x\n", b);

	*ir_key = b;
	*ir_raw = b;
	return 1;
}

static int get_key_knc1(struct IR *ir, u32 *ir_key, u32 *ir_raw)
{
	unsigned char b;

	/* poll IR chip */
	if (1 != i2c_master_recv(&ir->c,&b,1)) {
		dprintk(1,"read error\n");
		return -EIO;
	}

	/* it seems that 0xFE indicates that a button is still hold
	   down, while 0xFF indicates that no button is hold
	   down. 0xFE sequences are sometimes interrupted by 0xFF */

	dprintk(2,"key %02x\n", b);

	if (b == 0xFF)
		return 0;

	if (b == 0xFE)
		/* keep old data */
		return 1;

	*ir_key = b;
	*ir_raw = b;
	return 1;
}

static int get_key_purpletv(struct IR *ir, u32 *ir_key, u32 *ir_raw)
{
        unsigned char b;

	/* poll IR chip */
	if (1 != i2c_master_recv(&ir->c,&b,1)) {
		dprintk(1,"read error\n");
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

/* ----------------------------------------------------------------------- */

static void ir_key_poll(struct IR *ir)
{
	static u32 ir_key, ir_raw;
	int rc;

	dprintk(2,"ir_poll_key\n");
	rc = ir->get_key(ir, &ir_key, &ir_raw);
	if (rc < 0) {
		dprintk(2,"error\n");
		return;
	}

	if (0 == rc) {
		ir_input_nokey(ir->input, &ir->ir);
	} else {
		ir_input_keydown(ir->input, &ir->ir, ir_key, ir_raw);
	}
}

static void ir_timer(unsigned long data)
{
	struct IR *ir = (struct IR*)data;
	schedule_work(&ir->work);
}

static void ir_work(void *data)
{
	struct IR *ir = data;
	ir_key_poll(ir);
	mod_timer(&ir->timer, jiffies+HZ/10);
}

/* ----------------------------------------------------------------------- */

static int ir_attach(struct i2c_adapter *adap, int addr,
		      unsigned short flags, int kind);
static int ir_detach(struct i2c_client *client);
static int ir_probe(struct i2c_adapter *adap);

static struct i2c_driver driver = {
        .name           = "ir remote kbd driver",
        .id             = I2C_DRIVERID_EXP3, /* FIXME */
        .flags          = I2C_DF_NOTIFY,
        .attach_adapter = ir_probe,
        .detach_client  = ir_detach,
};

static struct i2c_client client_template =
{
        .name = "unset",
        .driver = &driver
};

static int ir_attach(struct i2c_adapter *adap, int addr,
		     unsigned short flags, int kind)
{
	IR_KEYTAB_TYPE *ir_codes = NULL;
	char *name;
	int ir_type;
        struct IR *ir;
	struct input_dev *input_dev;

	ir = kzalloc(sizeof(struct IR), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!ir || !input_dev) {
		kfree(ir);
		input_free_device(input_dev);
                return -ENOMEM;
	}

	ir->c = client_template;
	ir->input = input_dev;

	i2c_set_clientdata(&ir->c, ir);
	ir->c.adapter = adap;
	ir->c.addr    = addr;

	switch(addr) {
	case 0x64:
		name        = "Pixelview";
		ir->get_key = get_key_pixelview;
		ir_type     = IR_TYPE_OTHER;
		ir_codes    = ir_codes_empty;
		break;
	case 0x4b:
		name        = "PV951";
		ir->get_key = get_key_pv951;
		ir_type     = IR_TYPE_OTHER;
		ir_codes    = ir_codes_pv951;
		break;
	case 0x18:
	case 0x1a:
		name        = "Hauppauge";
		ir->get_key = get_key_haup;
		ir_type     = IR_TYPE_RC5;
		ir_codes    = ir_codes_rc5_tv;
		break;
	case 0x30:
		switch(kind){
		case IR_TERRATEC_REMOTE:
			name        = "Terratec IR";
			ir->get_key = get_key_knc1;
			ir_type     = IR_TYPE_OTHER;
			ir_codes    = ir_codes_em2820;
			break;
		default:
			name        = "KNC One";
			ir->get_key = get_key_knc1;
			ir_type     = IR_TYPE_OTHER;
			ir_codes    = ir_codes_em2820;
		}
		break;
	case 0x47:
	case 0x7a:
		switch(kind){
		case IR_PINNACLE_REMOTE:
			name        = "Pinnacle IR Remote";
			ir->get_key = get_key_purpletv;
			ir_type     = IR_TYPE_OTHER;
			ir_codes    = ir_codes_em2820;
			break;
		default:
			name        = "Purple TV";
			ir->get_key = get_key_purpletv;
			ir_type     = IR_TYPE_OTHER;
			ir_codes    = ir_codes_empty;
		}
		break;
	default:
		/* shouldn't happen */
		printk(DEVNAME ": Huh? unknown i2c address (0x%02x)?\n",addr);
		kfree(ir);
		return -1;
	}

	/* register i2c device */
	i2c_attach_client(&ir->c);
	snprintf(ir->c.name, sizeof(ir->c.name), "i2c IR (%s)", name);
	snprintf(ir->phys, sizeof(ir->phys), "%s/%s/ir0",
		 ir->c.adapter->dev.bus_id,
		 ir->c.dev.bus_id);

	/* init + register input device */
	ir_input_init(input_dev, &ir->ir, ir_type, ir_codes);
	input_dev->id.bustype	= BUS_I2C;
	input_dev->name		= ir->c.name;
	input_dev->phys		= ir->phys;

	input_register_device(ir->input);

	/* start polling via eventd */
	INIT_WORK(&ir->work, ir_work, ir);
	init_timer(&ir->timer);
	ir->timer.function = ir_timer;
	ir->timer.data     = (unsigned long)ir;
	schedule_work(&ir->work);

	return 0;
}

static int ir_detach(struct i2c_client *client)
{
        struct IR *ir = i2c_get_clientdata(client);

	/* kill outstanding polls */
	del_timer(&ir->timer);
	flush_scheduled_work();

	/* unregister devices */
	input_unregister_device(ir->input);
	i2c_detach_client(&ir->c);

	/* free memory */
	kfree(ir);
	return 0;
}

static int ir_probe(struct i2c_adapter *adap)
{

	/* The external IR receiver is at i2c address 0x34 (0x35 for
	   reads).  Future Hauppauge cards will have an internal
	   receiver at 0x30 (0x31 for reads).  In theory, both can be
	   fitted, and Hauppauge suggest an external overrides an
	   internal.

	   That's why we probe 0x1a (~0x34) first. CB
	*/

	static const int probe_bttv[] = { 0x1a, 0x18, 0x4b, 0x64, 0x30, -1};
	static const int probe_saa7134[] = { 0x7a, -1 };
	static const int probe_em2820[] = { 0x47, 0x30, -1 };
	const int *probe = NULL;
	int attached = 0;

	struct i2c_client c; unsigned char buf; int i,rc;

	switch (adap->id) {
	case I2C_HW_B_BT848:
		probe = probe_bttv;
		break;
	case I2C_HW_SAA7134:
		probe = probe_saa7134;
		break;
	case I2C_HW_B_EM2820:
		probe = probe_em2820;
		break;
	}
	if (NULL == probe)
		return 0;

	memset(&c,0,sizeof(c));
	c.adapter = adap;
	for (i = 0; -1 != probe[i] && attached != 1; i++) {
		c.addr = probe[i];
		rc = i2c_master_recv(&c,&buf,1);
		dprintk(1,"probe 0x%02x @ %s: %s\n",
			probe[i], adap->name,
			(1 == rc) ? "yes" : "no");
		switch(adap->id){
			case I2C_HW_B_BT848:
			case I2C_HW_SAA7134:
				if (1 == rc) {
					ir_attach(adap,probe[i],0,0);
					attached=1;
					break;
				}
			case I2C_HW_B_EM2820:
				/* windows logs are needed for fixing the pinnacle device */
				if (1 == rc && 0xff == buf){
					ir_attach(adap,probe[i],0,IR_TERRATEC_REMOTE);
					attached=1;
				}
				break;
		}
	}
	return 0;
}

/* ----------------------------------------------------------------------- */

MODULE_AUTHOR("Gerd Knorr, Michal Kochanowicz, Christoph Bartelmus, Ulrich Mueller");
MODULE_DESCRIPTION("input driver for i2c IR remote controls");
MODULE_LICENSE("GPL");

static int __init ir_init(void)
{
	return i2c_add_driver(&driver);
}

static void __exit ir_fini(void)
{
	i2c_del_driver(&driver);
}

module_init(ir_init);
module_exit(ir_fini);

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
