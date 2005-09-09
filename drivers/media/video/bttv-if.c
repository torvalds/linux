/*

    bttv-if.c  --  old gpio interface to other kernel modules
                   don't use in new code, will go away in 2.7
		   have a look at bttv-gpio.c instead.

    bttv - Bt848 frame grabber driver

    Copyright (C) 1996,97,98 Ralph  Metzler (rjkm@thp.uni-koeln.de)
                           & Marcus Metzler (mocm@thp.uni-koeln.de)
    (c) 1999-2003 Gerd Knorr <kraxel@bytesex.org>

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
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <asm/io.h>

#include "bttvp.h"

EXPORT_SYMBOL(bttv_get_cardinfo);
EXPORT_SYMBOL(bttv_get_pcidev);
EXPORT_SYMBOL(bttv_get_id);
EXPORT_SYMBOL(bttv_gpio_enable);
EXPORT_SYMBOL(bttv_read_gpio);
EXPORT_SYMBOL(bttv_write_gpio);
EXPORT_SYMBOL(bttv_get_gpio_queue);
EXPORT_SYMBOL(bttv_i2c_call);

/* ----------------------------------------------------------------------- */
/* Exported functions - for other modules which want to access the         */
/*                      gpio ports (IR for example)                        */
/*                      see bttv.h for comments                            */

int bttv_get_cardinfo(unsigned int card, int *type, unsigned *cardid)
{
	printk("The bttv_* interface is obsolete and will go away,\n"
	       "please use the new, sysfs based interface instead.\n");
	if (card >= bttv_num) {
		return -1;
	}
	*type   = bttvs[card].c.type;
	*cardid = bttvs[card].cardid;
	return 0;
}

struct pci_dev* bttv_get_pcidev(unsigned int card)
{
	if (card >= bttv_num)
		return NULL;
	return bttvs[card].c.pci;
}

int bttv_get_id(unsigned int card)
{
	printk("The bttv_* interface is obsolete and will go away,\n"
	       "please use the new, sysfs based interface instead.\n");
	if (card >= bttv_num) {
		return -1;
	}
	return bttvs[card].c.type;
}


int bttv_gpio_enable(unsigned int card, unsigned long mask, unsigned long data)
{
	struct bttv *btv;

	if (card >= bttv_num) {
		return -EINVAL;
	}

	btv = &bttvs[card];
	gpio_inout(mask,data);
	if (bttv_gpio)
		bttv_gpio_tracking(btv,"extern enable");
	return 0;
}

int bttv_read_gpio(unsigned int card, unsigned long *data)
{
	struct bttv *btv;

	if (card >= bttv_num) {
		return -EINVAL;
	}

	btv = &bttvs[card];

	if(btv->shutdown) {
		return -ENODEV;
	}

/* prior setting BT848_GPIO_REG_INP is (probably) not needed
   because we set direct input on init */
	*data = gpio_read();
	return 0;
}

int bttv_write_gpio(unsigned int card, unsigned long mask, unsigned long data)
{
	struct bttv *btv;

	if (card >= bttv_num) {
		return -EINVAL;
	}

	btv = &bttvs[card];

/* prior setting BT848_GPIO_REG_INP is (probably) not needed
   because direct input is set on init */
	gpio_bits(mask,data);
	if (bttv_gpio)
		bttv_gpio_tracking(btv,"extern write");
	return 0;
}

wait_queue_head_t* bttv_get_gpio_queue(unsigned int card)
{
	struct bttv *btv;

	if (card >= bttv_num) {
		return NULL;
	}

	btv = &bttvs[card];
	if (bttvs[card].shutdown) {
		return NULL;
	}
	return &btv->gpioq;
}

void bttv_i2c_call(unsigned int card, unsigned int cmd, void *arg)
{
	if (card >= bttv_num)
		return;
	bttv_call_i2c_clients(&bttvs[card], cmd, arg);
}

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
