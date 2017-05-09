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

#ifndef __DMA_PUBLIC_H_INCLUDED__
#define __DMA_PUBLIC_H_INCLUDED__

#include "system_types.h"

typedef struct dma_state_s		dma_state_t;

/*! Read the control registers of DMA[ID]

 \param	ID[in]				DMA identifier
 \param	state[out]			input formatter state structure

 \return none, state = DMA[ID].state
 */
extern void dma_get_state(
	const dma_ID_t		ID,
	dma_state_t			*state);

/*! Write to a control register of DMA[ID]

 \param	ID[in]				DMA identifier
 \param	reg[in]				register index
 \param value[in]			The data to be written

 \return none, DMA[ID].ctrl[reg] = value
 */
STORAGE_CLASS_DMA_H void dma_reg_store(
	const dma_ID_t		ID,
	const unsigned int	reg,
	const hrt_data		value);

/*! Read from a control register of DMA[ID]

 \param	ID[in]				DMA identifier
 \param	reg[in]				register index
 \param value[in]			The data to be written

 \return DMA[ID].ctrl[reg]
 */
STORAGE_CLASS_DMA_H hrt_data dma_reg_load(
	const dma_ID_t		ID,
	const unsigned int	reg);


/*! Set maximum burst size of DMA[ID]

 \param ID[in]				DMA identifier
 \param conn[in]			Connection to set max burst size for
 \param max_burst_size[in]		Maximum burst size in words

 \return none
*/
void
dma_set_max_burst_size(
	dma_ID_t		ID,
	dma_connection		conn,
	uint32_t		max_burst_size);

#endif /* __DMA_PUBLIC_H_INCLUDED__ */
