#include <linux/device.h>
#include "pl111_drm.h"

#ifndef PL111_VERSATILE_H
#define PL111_VERSATILE_H

int pl111_versatile_init(struct device *dev, struct pl111_drm_dev_private *priv);

#endif
