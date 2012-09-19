/*
 * OMAP thermal definitions
 *
 * Copyright (C) 2012 Texas Instruments Incorporated - http://www.ti.com/
 * Contact:
 *   Eduardo Valentin <eduardo.valentin@ti.com>
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
#ifndef __OMAP_THERMAL_H
#define __OMAP_THERMAL_H

#include "omap-bandgap.h"

/* sensors gradient and offsets */
#define OMAP_GRADIENT_SLOPE_4460				348
#define OMAP_GRADIENT_CONST_4460				-9301
#define OMAP_GRADIENT_SLOPE_4470				308
#define OMAP_GRADIENT_CONST_4470				-7896

#define OMAP_GRADIENT_SLOPE_5430_CPU				196
#define OMAP_GRADIENT_CONST_5430_CPU				-6822
#define OMAP_GRADIENT_SLOPE_5430_GPU				64
#define OMAP_GRADIENT_CONST_5430_GPU				978

/* PCB sensor calculation constants */
#define OMAP_GRADIENT_SLOPE_W_PCB_4460				1142
#define OMAP_GRADIENT_CONST_W_PCB_4460				-393
#define OMAP_GRADIENT_SLOPE_W_PCB_4470				1063
#define OMAP_GRADIENT_CONST_W_PCB_4470				-477

#define OMAP_GRADIENT_SLOPE_W_PCB_5430_CPU			469
#define OMAP_GRADIENT_CONST_W_PCB_5430_CPU			-1272
#define OMAP_GRADIENT_SLOPE_W_PCB_5430_GPU			378
#define OMAP_GRADIENT_CONST_W_PCB_5430_GPU			154

/* trip points of interest in milicelsius (at hotspot level) */
#define OMAP_TRIP_COLD						100000
#define OMAP_TRIP_HOT						110000
#define OMAP_TRIP_SHUTDOWN					125000
#define OMAP_TRIP_NUMBER					2
#define OMAP_TRIP_STEP							\
	((OMAP_TRIP_SHUTDOWN - OMAP_TRIP_HOT) / (OMAP_TRIP_NUMBER - 1))

/* Update rates */
#define FAST_TEMP_MONITORING_RATE				250

/* helper macros */
/**
 * omap_thermal_get_trip_value - returns trip temperature based on index
 * @i:	trip index
 */
#define omap_thermal_get_trip_value(i)					\
	(OMAP_TRIP_HOT + ((i) * OMAP_TRIP_STEP))

/**
 * omap_thermal_is_valid_trip - check for trip index
 * @i:	trip index
 */
#define omap_thermal_is_valid_trip(trip)				\
	((trip) >= 0 && (trip) < OMAP_TRIP_NUMBER)

#ifdef CONFIG_OMAP_THERMAL
int omap_thermal_expose_sensor(struct omap_bandgap *bg_ptr, int id,
			       char *domain);
int omap_thermal_remove_sensor(struct omap_bandgap *bg_ptr, int id);
int omap_thermal_register_cpu_cooling(struct omap_bandgap *bg_ptr, int id);
int omap_thermal_unregister_cpu_cooling(struct omap_bandgap *bg_ptr, int id);
#else
static inline
int omap_thermal_expose_sensor(struct omap_bandgap *bg_ptr, int id,
			       char *domain)
{
	return 0;
}

static inline
int omap_thermal_remove_sensor(struct omap_bandgap *bg_ptr, int id)
{
	return 0;
}

static inline
int omap_thermal_register_cpu_cooling(struct omap_bandgap *bg_ptr, int id)
{
	return 0;
}

static inline
int omap_thermal_unregister_cpu_cooling(struct omap_bandgap *bg_ptr, int id)
{
	return 0;
}
#endif
#endif
