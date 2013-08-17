/*
 * Coldfire generic GPIO pinmux support.
 *
 * (C) Copyright 2009, Steven King <sfking@fdwdc.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef pinmux_h
#define pinmux_h

#define MCFPINMUX_NONE		-1

extern int mcf_pinmux_request(unsigned, unsigned);
extern void mcf_pinmux_release(unsigned, unsigned);

static inline int mcf_pinmux_is_valid(unsigned pinmux)
{
	return pinmux != MCFPINMUX_NONE;
}

#endif

