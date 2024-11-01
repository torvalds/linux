// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 * Hardware accelerated Matrox Millennium I, II, Mystique, G100, G200, G400 and G450.
 *
 * (c) 1998-2002 Petr Vandrovec <vandrove@vc.cvut.cz>
 *
 * Version: 1.64 2002/06/10
 *
 * See matroxfb_base.c for contributors.
 *
 */

#include "matroxfb_base.h"
#include "matroxfb_maven.h"
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/i2c-algo-bit.h>

/* MGA-TVO I2C for G200, G400 */
#define MAT_CLK		0x20
#define MAT_DATA	0x10
/* primary head DDC for Mystique(?), G100, G200, G400 */
#define DDC1_CLK	0x08
#define DDC1_DATA	0x02
/* primary head DDC for Millennium, Millennium II */
#define DDC1B_CLK	0x10
#define DDC1B_DATA	0x04
/* secondary head DDC for G400 */
#define DDC2_CLK	0x04
#define DDC2_DATA	0x01

/******************************************************/

struct matroxfb_dh_maven_info {
	struct i2c_bit_adapter	maven;
	struct i2c_bit_adapter	ddc1;
	struct i2c_bit_adapter	ddc2;
};

static int matroxfb_read_gpio(struct matrox_fb_info* minfo) {
	unsigned long flags;
	int v;

	matroxfb_DAC_lock_irqsave(flags);
	v = matroxfb_DAC_in(minfo, DAC_XGENIODATA);
	matroxfb_DAC_unlock_irqrestore(flags);
	return v;
}

static void matroxfb_set_gpio(struct matrox_fb_info* minfo, int mask, int val) {
	unsigned long flags;
	int v;

	matroxfb_DAC_lock_irqsave(flags);
	v = (matroxfb_DAC_in(minfo, DAC_XGENIOCTRL) & mask) | val;
	matroxfb_DAC_out(minfo, DAC_XGENIOCTRL, v);
	/* We must reset GENIODATA very often... XFree plays with this register */
	matroxfb_DAC_out(minfo, DAC_XGENIODATA, 0x00);
	matroxfb_DAC_unlock_irqrestore(flags);
}

/* software I2C functions */
static inline void matroxfb_i2c_set(struct matrox_fb_info* minfo, int mask, int state) {
	if (state)
		state = 0;
	else
		state = mask;
	matroxfb_set_gpio(minfo, ~mask, state);
}

static void matroxfb_gpio_setsda(void* data, int state) {
	struct i2c_bit_adapter* b = data;
	matroxfb_i2c_set(b->minfo, b->mask.data, state);
}

static void matroxfb_gpio_setscl(void* data, int state) {
	struct i2c_bit_adapter* b = data;
	matroxfb_i2c_set(b->minfo, b->mask.clock, state);
}

static int matroxfb_gpio_getsda(void* data) {
	struct i2c_bit_adapter* b = data;
	return (matroxfb_read_gpio(b->minfo) & b->mask.data) ? 1 : 0;
}

static int matroxfb_gpio_getscl(void* data) {
	struct i2c_bit_adapter* b = data;
	return (matroxfb_read_gpio(b->minfo) & b->mask.clock) ? 1 : 0;
}

static const struct i2c_algo_bit_data matrox_i2c_algo_template =
{
	.setsda		= matroxfb_gpio_setsda,
	.setscl		= matroxfb_gpio_setscl,
	.getsda		= matroxfb_gpio_getsda,
	.getscl		= matroxfb_gpio_getscl,
	.udelay		= 10,
	.timeout	= 100,
};

static int i2c_bus_reg(struct i2c_bit_adapter* b, struct matrox_fb_info* minfo, 
		unsigned int data, unsigned int clock, const char *name,
		int class)
{
	int err;

	b->minfo = minfo;
	b->mask.data = data;
	b->mask.clock = clock;
	b->adapter.owner = THIS_MODULE;
	snprintf(b->adapter.name, sizeof(b->adapter.name), name,
		minfo->fbcon.node);
	i2c_set_adapdata(&b->adapter, b);
	b->adapter.class = class;
	b->adapter.algo_data = &b->bac;
	b->adapter.dev.parent = &minfo->pcidev->dev;
	b->bac = matrox_i2c_algo_template;
	b->bac.data = b;
	err = i2c_bit_add_bus(&b->adapter);
	b->initialized = !err;
	return err;
}

static void i2c_bit_bus_del(struct i2c_bit_adapter* b) {
	if (b->initialized) {
		i2c_del_adapter(&b->adapter);
		b->initialized = 0;
	}
}

static inline void i2c_maven_done(struct matroxfb_dh_maven_info* minfo2) {
	i2c_bit_bus_del(&minfo2->maven);
}

static inline void i2c_ddc1_done(struct matroxfb_dh_maven_info* minfo2) {
	i2c_bit_bus_del(&minfo2->ddc1);
}

static inline void i2c_ddc2_done(struct matroxfb_dh_maven_info* minfo2) {
	i2c_bit_bus_del(&minfo2->ddc2);
}

static void* i2c_matroxfb_probe(struct matrox_fb_info* minfo) {
	int err;
	unsigned long flags;
	struct matroxfb_dh_maven_info* m2info;

	m2info = kzalloc(sizeof(*m2info), GFP_KERNEL);
	if (!m2info)
		return NULL;

	matroxfb_DAC_lock_irqsave(flags);
	matroxfb_DAC_out(minfo, DAC_XGENIODATA, 0xFF);
	matroxfb_DAC_out(minfo, DAC_XGENIOCTRL, 0x00);
	matroxfb_DAC_unlock_irqrestore(flags);

	switch (minfo->chip) {
		case MGA_2064:
		case MGA_2164:
			err = i2c_bus_reg(&m2info->ddc1, minfo,
					  DDC1B_DATA, DDC1B_CLK,
					  "DDC:fb%u #0", I2C_CLASS_DDC);
			break;
		default:
			err = i2c_bus_reg(&m2info->ddc1, minfo,
					  DDC1_DATA, DDC1_CLK,
					  "DDC:fb%u #0", I2C_CLASS_DDC);
			break;
	}
	if (err)
		goto fail_ddc1;
	if (minfo->devflags.dualhead) {
		err = i2c_bus_reg(&m2info->ddc2, minfo,
				  DDC2_DATA, DDC2_CLK,
				  "DDC:fb%u #1", I2C_CLASS_DDC);
		if (err == -ENODEV) {
			printk(KERN_INFO "i2c-matroxfb: VGA->TV plug detected, DDC unavailable.\n");
		} else if (err)
			printk(KERN_INFO "i2c-matroxfb: Could not register secondary output i2c bus. Continuing anyway.\n");
		/* Register maven bus even on G450/G550 */
		err = i2c_bus_reg(&m2info->maven, minfo,
				  MAT_DATA, MAT_CLK, "MAVEN:fb%u", 0);
		if (err)
			printk(KERN_INFO "i2c-matroxfb: Could not register Maven i2c bus. Continuing anyway.\n");
		else {
			struct i2c_board_info maven_info = {
				I2C_BOARD_INFO("maven", 0x1b),
			};
			unsigned short const addr_list[2] = {
				0x1b, I2C_CLIENT_END
			};

			i2c_new_scanned_device(&m2info->maven.adapter,
					       &maven_info, addr_list, NULL);
		}
	}
	return m2info;
fail_ddc1:;
	kfree(m2info);
	printk(KERN_ERR "i2c-matroxfb: Could not register primary adapter DDC bus.\n");
	return NULL;
}

static void i2c_matroxfb_remove(struct matrox_fb_info* minfo, void* data) {
	struct matroxfb_dh_maven_info* m2info = data;

	i2c_maven_done(m2info);
	i2c_ddc2_done(m2info);
	i2c_ddc1_done(m2info);
	kfree(m2info);
}

static struct matroxfb_driver i2c_matroxfb = {
	.node =		LIST_HEAD_INIT(i2c_matroxfb.node),
	.name =		"i2c-matroxfb",
	.probe = 	i2c_matroxfb_probe,
	.remove =	i2c_matroxfb_remove,
};

static int __init i2c_matroxfb_init(void) {
	if (matroxfb_register_driver(&i2c_matroxfb)) {
		printk(KERN_ERR "i2c-matroxfb: failed to register driver\n");
		return -ENXIO;
	}
	return 0;
}

static void __exit i2c_matroxfb_exit(void) {
	matroxfb_unregister_driver(&i2c_matroxfb);
}

MODULE_AUTHOR("(c) 1999-2002 Petr Vandrovec <vandrove@vc.cvut.cz>");
MODULE_DESCRIPTION("Support module providing I2C buses present on Matrox videocards");

module_init(i2c_matroxfb_init);
module_exit(i2c_matroxfb_exit);
/* no __setup required */
MODULE_LICENSE("GPL");
