/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2010-2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef _EVENT_FIFO_LOCAL_H
#define _EVENT_FIFO_LOCAL_H

/*
 * All events come from connections mapped on the system
 * bus but do not use a global IRQ
 */
#include "event_fifo_global.h"

typedef enum {
	SP0_EVENT_ID,
	ISP0_EVENT_ID,
	STR2MIPI_EVENT_ID,
	N_EVENT_ID
} event_ID_t;

#define	EVENT_QUERY_BIT		0

/* Events are read from FIFO */
static const hrt_address event_source_addr[N_EVENT_ID] = {
	0x0000000000380000ULL,
	0x0000000000380004ULL,
	0xffffffffffffffffULL};

/* Read from FIFO are blocking, query data availability */
static const hrt_address event_source_query_addr[N_EVENT_ID] = {
	0x0000000000380010ULL,
	0x0000000000380014ULL,
	0xffffffffffffffffULL};

/* Events are written to FIFO */
static const hrt_address event_sink_addr[N_EVENT_ID] = {
	0x0000000000380008ULL,
	0x000000000038000CULL,
	0x0000000000090104ULL};

/* Writes to FIFO are blocking, query data space */
static const hrt_address event_sink_query_addr[N_EVENT_ID] = {
	0x0000000000380018ULL,
	0x000000000038001CULL,
	0x000000000009010CULL};

#endif /* _EVENT_FIFO_LOCAL_H */
