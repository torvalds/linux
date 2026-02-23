/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef __MXL862XX_H
#define __MXL862XX_H

#include <linux/mdio.h>
#include <net/dsa.h>

#define MXL862XX_MAX_PORTS		17

struct mxl862xx_priv {
	struct dsa_switch *ds;
	struct mdio_device *mdiodev;
};

#endif /* __MXL862XX_H */
