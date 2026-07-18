/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef __MXL862XX_HOST_H
#define __MXL862XX_HOST_H

#include "mxl862xx.h"

void mxl862xx_host_init(struct mxl862xx_priv *priv);
void mxl862xx_host_shutdown(struct mxl862xx_priv *priv);
int mxl862xx_api_wrap(struct mxl862xx_priv *priv, u16 cmd, void *data, u16 size,
		      bool read, bool quiet);

#define MXL862XX_API_WRITE(dev, cmd, data) \
	mxl862xx_api_wrap(dev, cmd, &(data), sizeof((data)), false, false)
#define MXL862XX_API_READ(dev, cmd, data) \
	mxl862xx_api_wrap(dev, cmd, &(data), sizeof((data)), true, false)
#define MXL862XX_API_READ_QUIET(dev, cmd, data) \
	mxl862xx_api_wrap(dev, cmd, &(data), sizeof((data)), true, true)

int mxl862xx_reset(struct mxl862xx_priv *priv);

#endif /* __MXL862XX_HOST_H */
