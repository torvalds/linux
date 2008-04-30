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

#include "em28xx.h"

static unsigned int ir_debug;
module_param(ir_debug, int, 0644);
MODULE_PARM_DESC(ir_debug, "enable debug messages [IR]");

#define dprintk(fmt, arg...) \
	if (ir_debug) { \
		printk(KERN_DEBUG "%s/ir: " fmt, ir->c.name , ## arg); \
	}

/* ----------------------------------------------------------------------- */

int em28xx_get_key_terratec(struct IR_i2c *ir, u32 *ir_key, u32 *ir_raw)
{
	unsigned char b;

	/* poll IR chip */
	if (1 != i2c_master_recv(&ir->c, &b, 1)) {
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


int em28xx_get_key_em_haup(struct IR_i2c *ir, u32 *ir_key, u32 *ir_raw)
{
	unsigned char buf[2];
	unsigned char code;

	/* poll IR chip */
	if (2 != i2c_master_recv(&ir->c, buf, 2))
		return -EIO;

	/* Does eliminate repeated parity code */
	if (buf[1] == 0xff)
		return 0;

	ir->old = buf[1];

	/* Rearranges bits to the right order */
	code =   ((buf[0]&0x01)<<5) | /* 0010 0000 */
		 ((buf[0]&0x02)<<3) | /* 0001 0000 */
		 ((buf[0]&0x04)<<1) | /* 0000 1000 */
		 ((buf[0]&0x08)>>1) | /* 0000 0100 */
		 ((buf[0]&0x10)>>3) | /* 0000 0010 */
		 ((buf[0]&0x20)>>5);  /* 0000 0001 */

	dprintk("ir hauppauge (em2840): code=0x%02x (rcv=0x%02x)\n",
			code, buf[0]);

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

	if (3 != i2c_master_recv(&ir->c, buf, 3)) {
		dprintk("read error\n");
		return -EIO;
	}

	dprintk("key %02x\n", buf[2]&0x3f);
	if (buf[0] != 0x00)
		return 0;

	*ir_key = buf[2]&0x3f;
	*ir_raw = buf[2]&0x3f;

	return 1;
}

/* ----------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
