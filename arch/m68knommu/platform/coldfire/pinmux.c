/*
 * Coldfire generic GPIO pinmux support.
 *
 * (C) Copyright 2009, Steven King <sfking@fdwdc.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>

#include <asm/pinmux.h>

int mcf_pinmux_request(unsigned pinmux, unsigned func)
{
	return 0;
}

void mcf_pinmux_release(unsigned pinmux, unsigned func)
{
}
