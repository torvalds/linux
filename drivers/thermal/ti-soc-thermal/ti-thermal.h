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
#ifndef __TI_THERMAL_H
#define __TI_THERMAL_H

#include "ti-bandgap.h"

/* sensors gradient and offsets */
#define OMAP_GRADIENT_SLOPE_4430				0
#define OMAP_GRADIENT_CONST_4430				20000
#define OMAP_GRADIENT_SLOPE_4460				348
#define OMAP_GRADIENT_CONST_4460				-9301
#define OMAP_GRADIENT_SLOPE_4470				308
#define OMAP_GRADIENT_CONST_4470				-7896

#define OMAP_GRADIENT_SLOPE_5430_CPU				65
#define OMAP_GRADIENT_CONST_5430_CPU				-1791
#define OMAP_GRADIENT_SLOPE_5430_GPU				117
#define OMAP_GRADIENT_CONST_5430_GPU				-2992

#define DRA752_GRADIENT_SLOPE					0
#define DRA752_GRADIENT_CONST					2000

/* PCB sensor calculation constants */
#define OMAP_GRADIENT_SLOPE_W_PCB_4430				0
#define OMAP_GRADIENT_CONST_W_PCB_4430				20000
#define OMAP_GRADIENT_SLOPE_W_PCB_4460				1142
#define OMAP_GRADIENT_CONST_W_PCB_4460				-393
#define OMAP_GRADIENT_SLOPE_W_PCB_4470				1063
#define OMAP_GRADIENT_CONST_W_PCB_4470				-477

#define OMAP_GRADIENT_SLOPE_W_PCB_5430_CPU			100
#define OMAP_GRADIENT_CONST_W_PCB_5430_CPU			484
#define OMAP_GRADIENT_SLOPE_W_PCB_5430_GPU			464
#define OMAP_GRADIENT_CONST_W_PCB_5430_GPU			-5102

#define DRA752_GRADIENT_SLOPE_W_PCB				0
#define DRA752_GRADIENT_CONST_W_PCB				2000

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
 * ti_thermal_get_trip_value - returns trip temperature based on index
 * @i:	trip index
 */
#define ti_thermal_get_trip_value(i)					\
	(OMAP_TRIP_HOT + ((i) * OMAP_TRIP_STEP))

/**
 * ti_thermal_is_valid_trip - check for trip index
 * @i:	trip index
 */
#define ti_thermal_is_valid_trip(trip)				\
	((trip) >= 0 && (trip) < OMAP_TRIP_NUMBER)

#ifdef CONFIG_TI_THERMAL
int ti_thermal_expose_sensor(struct ti_bandgap *bgp, int id, char *domain);
int ti_thermal_remove_sensor(struct ti_bandgap *bgp, int id);
int ti_thermal_report_sensor_temperature(struct ti_bandgap *bgp, int id);
int ti_thermal_register_cpu_cooling(struct ti_bandgap *bgp, int id);
int ti_thermal_unregister_cpu_cooling(struct ti_bandgap *bgp, int id);
#else
static inline
int ti_thermal_expose_sensor(struct ti_bandgap *bgp, int id, char *domain)
{
	return 0;
}

static inline
int ti_thermal_remove_sensor(struct ti_bandgap *bgp, int id)
{
	return 0;
}

static inline
int ti_thermal_report_sensor_temperature(struct ti_bandgap *bgp, int id)
{
	return 0;
}

static inline
int ti_thermal_register_cpu_cooling(struct ti_bandgap *bgp, int id)
{
	return 0;
}

static inline
int ti_thermal_unregister_cpu_cooling(struct ti_bandgap *bgp, int id)
{
	return 0;
}
#endif
#endif
