/*
  handle em28xx IR remotes via linux kernel input layer.

   Copyright (C) 2005 Ludovico Cavedon <cavedon@sssup.it>
		      Markus Rechberger <mrechberger@gmail.com>
		      Mauro Carvalho Chehab <mchehab@brturbo.com.br>
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
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/usb.h>

#include "em28xx.h"

static unsigned int disable_ir = 0;
module_param(disable_ir, int, 0444);
MODULE_PARM_DESC(disable_ir,"disable infrared remote support");

static unsigned int ir_debug = 0;
module_param(ir_debug, int, 0644);
MODULE_PARM_DESC(ir_debug,"enable debug messages [IR]");

#define dprintk(fmt, arg...)	if (ir_debug) \
	printk(KERN_DEBUG "%s/ir: " fmt, ir->c.name , ## arg)

/* ---------------------------------------------------------------------- */

static IR_KEYTAB_TYPE ir_codes_em_terratec[IR_KEYTAB_SIZE] = {
	[ 0x01 ] = KEY_CHANNEL,
	[ 0x02 ] = KEY_SELECT,
	[ 0x03 ] = KEY_MUTE,
	[ 0x04 ] = KEY_POWER,
	[ 0x05 ] = KEY_KP1,
	[ 0x06 ] = KEY_KP2,
	[ 0x07 ] = KEY_KP3,
	[ 0x08 ] = KEY_CHANNELUP,
	[ 0x09 ] = KEY_KP4,
	[ 0x0a ] = KEY_KP5,
	[ 0x0b ] = KEY_KP6,
	[ 0x0c ] = KEY_CHANNELDOWN,
	[ 0x0d ] = KEY_KP7,
	[ 0x0e ] = KEY_KP8,
	[ 0x0f ] = KEY_KP9,
	[ 0x10 ] = KEY_VOLUMEUP,
	[ 0x11 ] = KEY_KP0,
	[ 0x12 ] = KEY_MENU,
	[ 0x13 ] = KEY_PRINT,
	[ 0x14 ] = KEY_VOLUMEDOWN,
	[ 0x16 ] = KEY_PAUSE,
	[ 0x18 ] = KEY_RECORD,
	[ 0x19 ] = KEY_REWIND,
	[ 0x1a ] = KEY_PLAY,
	[ 0x1b ] = KEY_FORWARD,
	[ 0x1c ] = KEY_BACKSPACE,
	[ 0x1e ] = KEY_STOP,
	[ 0x40 ] = KEY_ZOOM,
};

static IR_KEYTAB_TYPE ir_codes_em_pinnacle_usb[IR_KEYTAB_SIZE] = {
	[ 0x3a ] = KEY_KP0,
	[ 0x31 ] = KEY_KP1,
	[ 0x32 ] = KEY_KP2,
	[ 0x33 ] = KEY_KP3,
	[ 0x34 ] = KEY_KP4,
	[ 0x35 ] = KEY_KP5,
	[ 0x36 ] = KEY_KP6,
	[ 0x37 ] = KEY_KP7,
	[ 0x38 ] = KEY_KP8,
	[ 0x39 ] = KEY_KP9,

	[ 0x2f ] = KEY_POWER,

	[ 0x2e ] = KEY_P,
	[ 0x1f ] = KEY_L,
	[ 0x2b ] = KEY_I,

	[ 0x2d ] = KEY_ZOOM,
	[ 0x1e ] = KEY_ZOOM,
	[ 0x1b ] = KEY_VOLUMEUP,
	[ 0x0f ] = KEY_VOLUMEDOWN,
	[ 0x17 ] = KEY_CHANNELUP,
	[ 0x1c ] = KEY_CHANNELDOWN,
	[ 0x25 ] = KEY_INFO,

	[ 0x3c ] = KEY_MUTE,

	[ 0x3d ] = KEY_LEFT,
	[ 0x3b ] = KEY_RIGHT,

	[ 0x3f ] = KEY_UP,
	[ 0x3e ] = KEY_DOWN,
	[ 0x1a ] = KEY_PAUSE,

	[ 0x1d ] = KEY_MENU,
	[ 0x19 ] = KEY_PLAY,
	[ 0x16 ] = KEY_REWIND,
	[ 0x13 ] = KEY_FORWARD,
	[ 0x15 ] = KEY_PAUSE,
	[ 0x0e ] = KEY_REWIND,
	[ 0x0d ] = KEY_PLAY,
	[ 0x0b ] = KEY_STOP,
	[ 0x07 ] = KEY_FORWARD,
	[ 0x27 ] = KEY_RECORD,
	[ 0x26 ] = KEY_TUNER,
	[ 0x29 ] = KEY_TEXT,
	[ 0x2a ] = KEY_MEDIA,
	[ 0x18 ] = KEY_EPG,
	[ 0x27 ] = KEY_RECORD,
};

/* ----------------------------------------------------------------------- */

static int get_key_terratec(struct IR_i2c *ir, u32 *ir_key, u32 *ir_raw)
{
	unsigned char b;

	/* poll IR chip */
	if (1 != i2c_master_recv(&ir->c,&b,1)) {
		dprintk("read error\n");
		return -EIO;
	}

	/* it seems that 0xFE indicates that a button is still hold
	   down, while 0xff indicates that no button is hold
	   down. 0xfe sequences are sometimes interrupted by 0xFF */

	dprintk("key %02x\n", b);

	if (b == 0xff)
		return 0;

	if (b == 0xfe)
		/* keep old data */
		return 1;

	*ir_key = b;
	*ir_raw = b;
	return 1;
}


static int get_key_em_haup(struct IR_i2c *ir, u32 *ir_key, u32 *ir_raw)
{
	unsigned char buf[2];
	unsigned char code;

	/* poll IR chip */
	if (2 != i2c_master_recv(&ir->c,buf,2))
		return -EIO;

	/* Does eliminate repeated parity code */
	if (buf[1]==0xff)
		return 0;

	ir->old=buf[1];

	/* Rearranges bits to the right order */
	code=    ((buf[0]&0x01)<<5) | /* 0010 0000 */
		 ((buf[0]&0x02)<<3) | /* 0001 0000 */
		 ((buf[0]&0x04)<<1) | /* 0000 1000 */
		 ((buf[0]&0x08)>>1) | /* 0000 0100 */
		 ((buf[0]&0x10)>>3) | /* 0000 0010 */
		 ((buf[0]&0x20)>>5);  /* 0000 0001 */

	dprintk("ir hauppauge (em2840): code=0x%02x (rcv=0x%02x)\n",code,buf[0]);

	/* return key */
	*ir_key = code;
	*ir_raw = code;
	return 1;
}

static int get_key_pinnacle_usb(struct IR_i2c *ir, u32 *ir_key, u32 *ir_raw)
{
	unsigned char buf[3];

	/* poll IR chip */

	if (3 != i2c_master_recv(&ir->c,buf,3)) {
		dprintk("read error\n");
		return -EIO;
	}

	dprintk("key %02x\n", buf[2]&0x3f);
	if (buf[0]!=0x00){
		return 0;
	}

	*ir_key = buf[2]&0x3f;
	*ir_raw = buf[2]&0x3f;

	return 1;
}

/* ----------------------------------------------------------------------- */
void em28xx_set_ir(struct em28xx * dev,struct IR_i2c *ir)
{
	if (disable_ir) {
		ir->get_key=NULL;
		return ;
	}

	/* detect & configure */
	switch (dev->model) {
	case (EM2800_BOARD_UNKNOWN):
		break;
	case (EM2820_BOARD_UNKNOWN):
		break;
	case (EM2800_BOARD_TERRATEC_CINERGY_200):
	case (EM2820_BOARD_TERRATEC_CINERGY_250):
		ir->ir_codes = ir_codes_em_terratec;
		ir->get_key = get_key_terratec;
		snprintf(ir->c.name, sizeof(ir->c.name), "i2c IR (EM28XX Terratec)");
		break;
	case (EM2820_BOARD_PINNACLE_USB_2):
		ir->ir_codes = ir_codes_em_pinnacle_usb;
		ir->get_key = get_key_pinnacle_usb;
		snprintf(ir->c.name, sizeof(ir->c.name), "i2c IR (EM28XX Pinnacle PCTV)");
		break;
	case (EM2820_BOARD_HAUPPAUGE_WINTV_USB_2):
		ir->ir_codes = ir_codes_hauppauge_new;
		ir->get_key = get_key_em_haup;
		snprintf(ir->c.name, sizeof(ir->c.name), "i2c IR (EM2840 Hauppauge)");
		break;
	case (EM2820_BOARD_MSI_VOX_USB_2):
		break;
	case (EM2800_BOARD_LEADTEK_WINFAST_USBII):
		break;
	case (EM2800_BOARD_KWORLD_USB2800):
		break;
	}
}

/* ----------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
