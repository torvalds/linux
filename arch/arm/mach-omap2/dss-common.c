/*
 * Copyright (C) 2012 Texas Instruments, Inc..
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

/*
 * NOTE: this is a transitional file to help with DT adaptation.
 * This file will be removed when DSS supports DT.
 */

#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>

#include <video/omapdss.h>
#include <video/omap-panel-data.h>

#include "soc.h"
#include "dss-common.h"
#include "mux.h"
#include "display.h"

