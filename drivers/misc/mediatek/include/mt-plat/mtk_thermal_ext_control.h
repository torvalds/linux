/*
 * Copyright (c) 2009 Travis Geiselbrecht
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef _MTK_THERMAL_EXT_CONTROL_H
#define _MTK_THERMAL_EXT_CONTROL_H

#define THERMAL_MD32_IPI_MSG_BASE 0x1F00
#define THERMAL_AP_IPI_MSG_BASE   0x2F00

typedef enum {
	THERMAL_AP_IPI_MSG_SET_TZ_THRESHOLD = THERMAL_AP_IPI_MSG_BASE,
	THERMAL_AP_IPI_MSG_MD32_START,

	THERMAL_MD32_IPI_MSG_READY = THERMAL_MD32_IPI_MSG_BASE,
	THERMAL_MD32_IPI_MSG_MD32_START_ACK,
	THERMAL_MD32_IPI_MSG_REACH_THRESHOLD,
} thermal_ipi_msg_id;

typedef enum {
/* MTK_THERMAL_EXT_SENSOR_CPU = 0, */
	MTK_THERMAL_EXT_SENSOR_ABB = 0,
	MTK_THERMAL_EXT_SENSOR_PMIC,
	MTK_THERMAL_EXT_SENSOR_BATTERY,
	MTK_THERMAL_EXT_SENSOR_COUNT
} MTK_THERMAL_EXT_SENSOR_ID;

typedef struct {
	int id;			/* id of this tz */
	int polling_delay;	/* polling delay of this tz */
	long high_trip_point;	/* high threshold of this tz */
	long low_trip_point;	/* low threshold of this tz */
} thermal_zone_data;

typedef struct {
	int id;			/* id of this tz */
	long high_trip_point;	/* high threshold of this tz */
	long low_trip_point;	/* low threshold of this tz */
	long temperature;	/* Current temperature gotten from TS */
} thermal_zone_status;

typedef struct {
	short id;
	union {
		thermal_zone_data tz;
		thermal_zone_status tz_status;
	} data;
} thermal_ipi_msg;

#endif				/* _MTK_THERMAL_EXT_CONTROL_H */
