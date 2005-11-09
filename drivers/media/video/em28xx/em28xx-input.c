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
#include <linux/usb.h>

#include "em2820.h"

static unsigned int disable_ir = 0;
module_param(disable_ir, int, 0444);
MODULE_PARM_DESC(disable_ir,"disable infrared remote support");

static unsigned int ir_debug = 0;
module_param(ir_debug, int, 0644);
MODULE_PARM_DESC(ir_debug,"enable debug messages [IR]");

#define dprintk(fmt, arg...)	if (ir_debug) \
	printk(KERN_DEBUG "%s/ir: " fmt, ir->c.name , ## arg)

/* ---------------------------------------------------------------------- */

static IR_KEYTAB_TYPE ir_codes_em_pinnacle[IR_KEYTAB_SIZE] = {
	[  0 ] = KEY_CHANNEL,
	[  1 ] = KEY_SELECT,
	[  2 ] = KEY_MUTE,
	[  3 ] = KEY_POWER,
	[  4 ] = KEY_KP1,
	[  5 ] = KEY_KP2,
	[  6 ] = KEY_KP3,
	[  7 ] = KEY_CHANNELUP,
	[  8 ] = KEY_KP4,
	[  9 ] = KEY_KP5,
	[ 10 ] = KEY_KP6,

	[ 11 ] = KEY_CHANNELDOWN,
	[ 12 ] = KEY_KP7,
	[ 13 ] = KEY_KP8,
	[ 14 ] = KEY_KP9,
	[ 15 ] = KEY_VOLUMEUP,
	[ 16 ] = KEY_KP0,
	[ 17 ] = KEY_MENU,
	[ 18 ] = KEY_PRINT,

	[ 19 ] = KEY_VOLUMEDOWN,
	[ 21 ] = KEY_PAUSE,
	[ 23 ] = KEY_RECORD,
	[ 24 ] = KEY_REWIND,
	[ 25 ] = KEY_PLAY,
	[ 27 ] = KEY_BACKSPACE,
	[ 29 ] = KEY_STOP,
	[ 31 ] = KEY_ZOOM,
};

/* ----------------------------------------------------------------------- */

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

	/* avoid fast reapeating */
	if (buf[1]==ir->old)
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

/* ----------------------------------------------------------------------- */
void em2820_set_ir(struct em2820 * dev,struct IR_i2c *ir)
{
	if (disable_ir)
		return ;

	/* detect & configure */
	switch (dev->model) {
	case (EM2800_BOARD_UNKNOWN):
		break;
	case (EM2820_BOARD_UNKNOWN):
		break;
	case (EM2820_BOARD_TERRATEC_CINERGY_250):
		break;
	case (EM2820_BOARD_PINNACLE_USB_2):
		ir->ir_codes = ir_codes_em_pinnacle;
		break;
	case (EM2820_BOARD_HAUPPAUGE_WINTV_USB_2):
		ir->ir_codes = ir_codes_hauppauge_new;
		ir->get_key = get_key_em_haup;
		snprintf(ir->c.name, sizeof(ir->c.name), "i2c IR (EM2840 Hauppage)");
		break;
	case (EM2820_BOARD_MSI_VOX_USB_2):
		break;
	case (EM2800_BOARD_TERRATEC_CINERGY_200):
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
