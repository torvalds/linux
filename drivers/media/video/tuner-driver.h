/*
    tuner-driver.h - interface for different tuners

    Copyright (C) 1997 Markus Schroeder (schroedm@uni-duesseldorf.de)
    minor modifications by Ralph Metzler (rjkm@thp.uni-koeln.de)

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

#ifndef __TUNER_HW_H__
#define __TUNER_HW_H__

#include <linux/videodev2.h>
#include <linux/i2c.h>

extern unsigned const int tuner_count;

struct tuner_operations {
	void (*set_tv_freq)(struct i2c_client *c, unsigned int freq);
	void (*set_radio_freq)(struct i2c_client *c, unsigned int freq);
	int  (*has_signal)(struct i2c_client *c);
	int  (*is_stereo)(struct i2c_client *c);
	int  (*get_afc)(struct i2c_client *c);
	void (*tuner_status)(struct i2c_client *c);
	void (*standby)(struct i2c_client *c);
	void (*release)(struct i2c_client *c);
};

struct tuner {
	/* device */
	struct i2c_client i2c;

	unsigned int type;	/* chip type */

	unsigned int mode;
	unsigned int mode_mask;	/* Combination of allowable modes */

	unsigned int tv_freq;	/* keep track of the current settings */
	unsigned int radio_freq;
	u16 	     last_div;
	unsigned int audmode;
	v4l2_std_id  std;

	int          using_v4l2;
	void *priv;

	/* used by tda9887 */
	unsigned int       tda9887_config;

	unsigned int config;
	int (*tuner_callback) (void *dev, int command,int arg);

	struct tuner_operations ops;
};

/* ------------------------------------------------------------------------ */

extern int default_tuner_init(struct i2c_client *c);

extern int tda9887_tuner_init(struct i2c_client *c);

extern int microtune_init(struct i2c_client *c);

extern int tda8290_init(struct i2c_client *c);
extern int tda8290_probe(struct i2c_client *c);

extern int tea5761_tuner_init(struct i2c_client *c);
extern int tea5761_autodetection(struct i2c_client *c);

extern int tea5767_autodetection(struct i2c_client *c);
extern int tea5767_tuner_init(struct i2c_client *c);

/* ------------------------------------------------------------------------ */

#define tuner_warn(fmt, arg...) do {\
	printk(KERN_WARNING "%s %d-%04x: " fmt, t->i2c.driver->driver.name, \
			i2c_adapter_id(t->i2c.adapter), t->i2c.addr , ##arg); } while (0)
#define tuner_info(fmt, arg...) do {\
	printk(KERN_INFO "%s %d-%04x: " fmt, t->i2c.driver->driver.name, \
			i2c_adapter_id(t->i2c.adapter), t->i2c.addr , ##arg); } while (0)
#define tuner_dbg(fmt, arg...) do {\
	extern int tuner_debug; \
	if (tuner_debug) \
		printk(KERN_DEBUG "%s %d-%04x: " fmt, t->i2c.driver->driver.name, \
			i2c_adapter_id(t->i2c.adapter), t->i2c.addr , ##arg); } while (0)

#endif /* __TUNER_HW_H__ */

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
