/*
 * drivers/clk/at91/sckc.h
 *
 *  Copyright (C) 2013 Boris BREZILLON <b.brezillon@overkiz.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __AT91_SCKC_H_
#define __AT91_SCKC_H_

extern void __init of_at91sam9x5_clk_slow_osc_setup(struct device_node *np,
						    void __iomem *sckcr);
extern void __init of_at91sam9x5_clk_slow_rc_osc_setup(struct device_node *np,
						       void __iomem *sckcr);
extern void __init of_at91sam9x5_clk_slow_setup(struct device_node *np,
						void __iomem *sckcr);

#endif /* __AT91_SCKC_H_ */
