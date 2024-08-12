/* SPDX-License-Identifier: GPL-2.0+ */
/* SC16IS7xx SPI/I2C tty serial driver */

#ifndef _SC16IS7XX_H_
#define _SC16IS7XX_H_

#include <linux/mod_devicetable.h>
#include <linux/regmap.h>
#include <linux/types.h>

#define SC16IS7XX_NAME		"sc16is7xx"
#define SC16IS7XX_MAX_PORTS	2 /* Maximum number of UART ports per IC. */

struct device;

struct sc16is7xx_devtype {
	char	name[10];
	int	nr_gpio;
	int	nr_uart;
};

extern const struct regmap_config sc16is7xx_regcfg;

extern const struct of_device_id sc16is7xx_dt_ids[];

extern const struct sc16is7xx_devtype sc16is74x_devtype;
extern const struct sc16is7xx_devtype sc16is750_devtype;
extern const struct sc16is7xx_devtype sc16is752_devtype;
extern const struct sc16is7xx_devtype sc16is760_devtype;
extern const struct sc16is7xx_devtype sc16is762_devtype;

const char *sc16is7xx_regmap_name(u8 port_id);

unsigned int sc16is7xx_regmap_port_mask(unsigned int port_id);

int sc16is7xx_probe(struct device *dev, const struct sc16is7xx_devtype *devtype,
		    struct regmap *regmaps[], int irq);

void sc16is7xx_remove(struct device *dev);

#endif /* _SC16IS7XX_H_ */
