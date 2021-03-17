/* SPDX-License-Identifier: GPL-2.0+ */
/* vim: set ts=8 sw=8 noet tw=80 nowrap: */
/*
 *  comedi/drivers/ni_routing/ni_route_values.h
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

#ifndef _COMEDI_DRIVERS_NI_ROUTINT_NI_ROUTE_VALUES_H
#define _COMEDI_DRIVERS_NI_ROUTINT_NI_ROUTE_VALUES_H

#include "../../comedi.h"
#include <linux/types.h>

/*
 * This file includes the tables that are a list of all the values of various
 * signals routes available on NI hardware.  In many cases, one does not
 * explicitly make these routes, rather one might indicate that something is
 * used as the source of one particular trigger or another (using
 * *_src=TRIG_EXT).
 *
 * This file is meant to be included by comedi/drivers/ni_routes.c
 */

#define B(x)	((x) - NI_NAMES_BASE)

/** Marks a register value as valid, implemented, and tested. */
#define V(x)	(((x) & 0x7f) | 0x80)

#ifndef NI_ROUTE_VALUE_EXTERNAL_CONVERSION
	/** Marks a register value as implemented but needing testing. */
	#define I(x)	V(x)
	/** Marks a register value as not implemented. */
	#define U(x)	0x0

	typedef u8 register_type;
#else
	/** Marks a register value as implemented but needing testing. */
	#define I(x)	(((x) & 0x7f) | 0x100)
	/** Marks a register value as not implemented. */
	#define U(x)	(((x) & 0x7f) | 0x200)

	/** Tests whether a register is marked as valid/implemented/tested */
	#define MARKED_V(x)	(((x) & 0x80) != 0)
	/** Tests whether a register is implemented but not tested */
	#define MARKED_I(x)	(((x) & 0x100) != 0)
	/** Tests whether a register is not implemented */
	#define MARKED_U(x)	(((x) & 0x200) != 0)

	/* need more space to store extra marks */
	typedef u16 register_type;
#endif

/* Mask out the marking bit(s). */
#define UNMARK(x)	((x) & 0x7f)

/*
 * Gi_SRC(x,1) implements Gi_Src_SubSelect = 1
 *
 * This appears to only really be a valid MUX for m-series devices.
 */
#define Gi_SRC(val, subsel)	((val) | ((subsel) << 6))

/**
 * struct family_route_values - Register values for all routes for a particular
 *				family.
 * @family: lower-case string representation of a specific series or family of
 *	    devices from National Instruments where each member of this family
 *	    shares the same register values for the various signal MUXes.  It
 *	    should be noted that not all devices of any family have access to
 *	    all routes defined.
 * @register_values: Table of all register values for various signal MUXes on
 *	    National Instruments devices.  The first index of this table is the
 *	    signal destination (i.e. identification of the signal MUX).  The
 *	    second index of this table is the signal source (i.e. input of the
 *	    signal MUX).
 */
struct family_route_values {
	const char *family;
	const register_type register_values[NI_NUM_NAMES][NI_NUM_NAMES];

};

extern const struct family_route_values *const ni_all_route_values[];

#endif /* _COMEDI_DRIVERS_NI_ROUTINT_NI_ROUTE_VALUES_H */
