/*
 * Copyright (C) 2011 Ilya Yanok, Emcraft Systems
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define AM35XX_DEFAULT_MDIO_FREQUENCY	1000000

#if defined(CONFIG_TI_DAVINCI_EMAC) || defined(CONFIG_TI_DAVINCI_EMAC_MODULE)
void am35xx_emac_init(unsigned long mdio_bus_freq, u8 rmii_en);
#else
static inline void am35xx_emac_init(unsigned long mdio_bus_freq, u8 rmii_en) {}
#endif
