/*
 * Helper functions for the SPI-3 bridge FPGA on the Radisys ENP2611
 * Copyright (C) 2004, 2005 Lennert Buytenhek <buytenh@wantstofly.org>
 * Dedicated to Marija Kulikova.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __CALEB_H
#define __CALEB_H

void caleb_reset(void);
void caleb_enable_rx(int port);
void caleb_disable_rx(int port);
void caleb_enable_tx(int port);
void caleb_disable_tx(int port);


#endif
