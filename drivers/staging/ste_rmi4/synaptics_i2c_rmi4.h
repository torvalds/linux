/**
 *
 * Synaptics Register Mapped Interface (RMI4) I2C Physical Layer Driver.
 * Copyright (c) 2007-2010, Synaptics Incorporated
 *
 * Author: Js HA <js.ha@stericsson.com> for ST-Ericsson
 * Author: Naveen Kumar G <naveen.gaddipati@stericsson.com> for ST-Ericsson
 * Copyright 2010 (c) ST-Ericsson AB
 */
/*
 * This file is licensed under the GPL2 license.
 *
 *#############################################################################
 * GPL
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 *#############################################################################
 */

#ifndef _SYNAPTICS_RMI4_H_INCLUDED_
#define _SYNAPTICS_RMI4_H_INCLUDED_

/**
 * struct synaptics_rmi4_platform_data - contains the rmi4 platform data
 * @irq_number: irq number
 * @irq_type: irq type
 * @x flip: x flip flag
 * @y flip: y flip flag
 *
 * This structure gives platform data for rmi4.
 */
struct synaptics_rmi4_platform_data {
	int irq_type;
	bool x_flip;
	bool y_flip;
};

#endif
