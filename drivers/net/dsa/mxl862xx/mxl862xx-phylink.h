/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef __MXL862XX_PHYLINK_H
#define __MXL862XX_PHYLINK_H

#include <linux/phylink.h>

#include "mxl862xx.h"

#define MXL862XX_SERDES_SLOT(port) \
	(((port) - MXL862XX_FIRST_SERDES_PORT) % MXL862XX_SERDES_SLOTS)
#define MXL862XX_SERDES_PORT_ID(port) \
	(((port) - MXL862XX_FIRST_SERDES_PORT) / MXL862XX_SERDES_SLOTS)

extern const struct phylink_mac_ops mxl862xx_phylink_mac_ops;
void mxl862xx_phylink_get_caps(struct dsa_switch *ds, int port,
			       struct phylink_config *config);
void mxl862xx_setup_pcs(struct mxl862xx_priv *priv, struct mxl862xx_pcs *pcs,
			int port);

#endif /* __MXL862XX_PHYLINK_H */
