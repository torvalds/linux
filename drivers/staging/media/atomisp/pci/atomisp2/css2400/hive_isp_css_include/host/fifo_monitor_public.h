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

#ifndef __FIFO_MONITOR_PUBLIC_H_INCLUDED__
#define __FIFO_MONITOR_PUBLIC_H_INCLUDED__

#include "system_types.h"

typedef struct fifo_channel_state_s		fifo_channel_state_t;
typedef struct fifo_switch_state_s		fifo_switch_state_t;
typedef struct fifo_monitor_state_s		fifo_monitor_state_t;

/*! Set a fifo switch multiplex
 
 \param	ID[in]				FIFO_MONITOR identifier
 \param	switch_id[in]		fifo switch identifier
 \param	sel[in]				fifo switch selector

 \return none, fifo_switch[switch_id].sel = sel
 */
STORAGE_CLASS_FIFO_MONITOR_H void fifo_switch_set(
	const fifo_monitor_ID_t		ID,
	const fifo_switch_t			switch_id,
	const hrt_data				sel);

/*! Get a fifo switch multiplex
 
 \param	ID[in]				FIFO_MONITOR identifier
 \param	switch_id[in]		fifo switch identifier

 \return fifo_switch[switch_id].sel
 */
STORAGE_CLASS_FIFO_MONITOR_H hrt_data fifo_switch_get(
	const fifo_monitor_ID_t		ID,
	const fifo_switch_t			switch_id);

/*! Read the state of FIFO_MONITOR[ID]
 
 \param	ID[in]				FIFO_MONITOR identifier
 \param	state[out]			fifo monitor state structure

 \return none, state = FIFO_MONITOR[ID].state
 */
extern void fifo_monitor_get_state(
	const fifo_monitor_ID_t		ID,
	fifo_monitor_state_t		*state);

/*! Read the state of a fifo channel
 
 \param	ID[in]				FIFO_MONITOR identifier
 \param	channel_id[in]		fifo channel identifier
 \param	state[out]			fifo channel state structure

 \return none, state = fifo_channel[channel_id].state
 */
extern void fifo_channel_get_state(
	const fifo_monitor_ID_t		ID,
	const fifo_channel_t		channel_id,
	fifo_channel_state_t		*state);

/*! Read the state of a fifo switch
 
 \param	ID[in]				FIFO_MONITOR identifier
 \param	switch_id[in]		fifo switch identifier
 \param	state[out]			fifo switch state structure

 \return none, state = fifo_switch[switch_id].state
 */
extern void fifo_switch_get_state(
	const fifo_monitor_ID_t		ID,
	const fifo_switch_t			switch_id,
	fifo_switch_state_t			*state);

/*! Write to a control register of FIFO_MONITOR[ID]
 
 \param	ID[in]				FIFO_MONITOR identifier
 \param	reg[in]				register index
 \param value[in]			The data to be written

 \return none, FIFO_MONITOR[ID].ctrl[reg] = value
 */
STORAGE_CLASS_FIFO_MONITOR_H void fifo_monitor_reg_store(
	const fifo_monitor_ID_t		ID,
	const unsigned int			reg,
	const hrt_data				value);

/*! Read from a control register of FIFO_MONITOR[ID]
 
 \param	ID[in]				FIFO_MONITOR identifier
 \param	reg[in]				register index
 \param value[in]			The data to be written

 \return FIFO_MONITOR[ID].ctrl[reg]
 */
STORAGE_CLASS_FIFO_MONITOR_H hrt_data fifo_monitor_reg_load(
	const fifo_monitor_ID_t		ID,
	const unsigned int			reg);

#endif /* __FIFO_MONITOR_PUBLIC_H_INCLUDED__ */
