/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef __MXL862XX_HOST_H
#define __MXL862XX_HOST_H

#include "mxl862xx.h"

int mxl862xx_api_wrap(struct mxl862xx_priv *priv, u16 cmd, void *data, u16 size,
		      bool read, bool quiet);
int mxl862xx_reset(struct mxl862xx_priv *priv);

#endif /* __MXL862XX_HOST_H */
