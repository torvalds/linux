// SPDX-License-Identifier: GPL-2.0+
/*
 *  comedi/drivers/ni_routing/ni_route_values.c
 *  Route information for NI boards.
 *
 *  COMEDI - Linux Control and Measurement Device Interface
 *  Copyright (C) 2016 Spencer E. Olson <olsonse@umich.edu>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

/*
 * This file includes the tables that are a list of all the values of various
 * signals routes available on NI hardware.  In many cases, one does not
 * explicitly make these routes, rather one might indicate that something is
 * used as the source of one particular trigger or another (using
 * *_src=TRIG_EXT).
 *
 * The contents of this file are generated using the tools in
 * comedi/drivers/ni_routing/tools
 *
 * Please use those tools to help maintain the contents of this file.
 */

#include "ni_route_values.h"
#include "ni_route_values/all.h"

const struct family_route_values *const ni_all_route_values[] = {
	&ni_660x_route_values,
	&ni_eseries_route_values,
	&ni_mseries_route_values,
	NULL,
};
