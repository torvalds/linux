/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
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

#ifndef __RESOURCE_GLOBAL_H_INCLUDED__
#define __RESOURCE_GLOBAL_H_INCLUDED__

#define IS_RESOURCE_VERSION_1

typedef enum {
	DMA_CHANNEL_RESOURCE_TYPE,
	IRQ_CHANNEL_RESOURCE_TYPE,
	MEM_SECTION_RESOURCE_TYPE,
	N_RESOURCE_TYPE
} resource_type_ID_t;

typedef enum {
	PERMANENT_RESOURCE_RESERVATION,
	PERSISTENT_RESOURCE_RESERVATION,
	DEDICTATED_RESOURCE_RESERVATION,
	SHARED_RESOURCE_RESERVATION,
	N_RESOURCE_RESERVATION
} resource_reservation_t;

#endif /* __RESOURCE_GLOBAL_H_INCLUDED__ */
