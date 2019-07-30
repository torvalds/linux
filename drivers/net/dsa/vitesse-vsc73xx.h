/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/device.h>
#include <linux/etherdevice.h>
#include <linux/gpio/driver.h>

/**
 * struct vsc73xx - VSC73xx state container
 */
struct vsc73xx {
	struct device			*dev;
	struct gpio_desc		*reset;
	struct dsa_switch		*ds;
	struct gpio_chip		gc;
	u16				chipid;
	u8				addr[ETH_ALEN];
	const struct vsc73xx_ops	*ops;
	void				*priv;
};

struct vsc73xx_ops {
	int (*read)(struct vsc73xx *vsc, u8 block, u8 subblock, u8 reg,
		    u32 *val);
	int (*write)(struct vsc73xx *vsc, u8 block, u8 subblock, u8 reg,
		     u32 val);
};

int vsc73xx_is_addr_valid(u8 block, u8 subblock);
int vsc73xx_probe(struct vsc73xx *vsc);
int vsc73xx_remove(struct vsc73xx *vsc);
