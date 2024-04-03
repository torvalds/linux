/* SPDX-License-Identifier: GPL-2.0 */
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

#ifndef __ISP_PUBLIC_H_INCLUDED__
#define __ISP_PUBLIC_H_INCLUDED__

#include <type_support.h>
#include "system_local.h"

/*! Enable or disable the program complete irq signal of ISP[ID]

 \param	ID[in]				SP identifier
 \param	cnd[in]				predicate

 \return none, if(cnd) enable(ISP[ID].irq) else disable(ISP[ID].irq)
 */
void cnd_isp_irq_enable(
    const isp_ID_t		ID,
    const bool			cnd);

/*! Write to the status and control register of ISP[ID]

 \param	ID[in]				ISP identifier
 \param	reg[in]				register index
 \param value[in]			The data to be written

 \return none, ISP[ID].sc[reg] = value
 */
STORAGE_CLASS_ISP_H void isp_ctrl_store(
    const isp_ID_t		ID,
    const unsigned int	reg,
    const hrt_data		value);

/*! Read from the status and control register of ISP[ID]

 \param	ID[in]				ISP identifier
 \param	reg[in]				register index
 \param value[in]			The data to be written

 \return ISP[ID].sc[reg]
 */
STORAGE_CLASS_ISP_H hrt_data isp_ctrl_load(
    const isp_ID_t		ID,
    const unsigned int	reg);

/*! Get the status of a bitfield in the control register of ISP[ID]

 \param	ID[in]				ISP identifier
 \param	reg[in]				register index
 \param bit[in]				The bit index to be checked

 \return  (ISP[ID].sc[reg] & (1<<bit)) != 0
 */
STORAGE_CLASS_ISP_H bool isp_ctrl_getbit(
    const isp_ID_t		ID,
    const unsigned int	reg,
    const unsigned int	bit);

/*! Set a bitfield in the control register of ISP[ID]

 \param	ID[in]				ISP identifier
 \param	reg[in]				register index
 \param bit[in]				The bit index to be set

 \return none, ISP[ID].sc[reg] |= (1<<bit)
 */
STORAGE_CLASS_ISP_H void isp_ctrl_setbit(
    const isp_ID_t		ID,
    const unsigned int	reg,
    const unsigned int	bit);

/*! Clear a bitfield in the control register of ISP[ID]

 \param	ID[in]				ISP identifier
 \param	reg[in]				register index
 \param bit[in]				The bit index to be set

 \return none, ISP[ID].sc[reg] &= ~(1<<bit)
 */
STORAGE_CLASS_ISP_H void isp_ctrl_clearbit(
    const isp_ID_t		ID,
    const unsigned int	reg,
    const unsigned int	bit);

/*! Write to the DMEM of ISP[ID]

 \param	ID[in]				ISP identifier
 \param	addr[in]			the address in DMEM
 \param data[in]			The data to be written
 \param size[in]			The size(in bytes) of the data to be written

 \return none, ISP[ID].dmem[addr...addr+size-1] = data
 */
STORAGE_CLASS_ISP_H void isp_dmem_store(
    const isp_ID_t		ID,
    unsigned int		addr,
    const void			*data,
    const size_t		size);

/*! Read from the DMEM of ISP[ID]

 \param	ID[in]				ISP identifier
 \param	addr[in]			the address in DMEM
 \param data[in]			The data to be read
 \param size[in]			The size(in bytes) of the data to be read

 \return none, data = ISP[ID].dmem[addr...addr+size-1]
 */
STORAGE_CLASS_ISP_H void isp_dmem_load(
    const isp_ID_t		ID,
    const unsigned int	addr,
    void				*data,
    const size_t		size);

/*! Write a 32-bit datum to the DMEM of ISP[ID]

 \param	ID[in]				ISP identifier
 \param	addr[in]			the address in DMEM
 \param data[in]			The data to be written
 \param size[in]			The size(in bytes) of the data to be written

 \return none, ISP[ID].dmem[addr] = data
 */
STORAGE_CLASS_ISP_H void isp_dmem_store_uint32(
    const isp_ID_t		ID,
    unsigned int		addr,
    const uint32_t		data);

/*! Load a 32-bit datum from the DMEM of ISP[ID]

 \param	ID[in]				ISP identifier
 \param	addr[in]			the address in DMEM
 \param data[in]			The data to be read
 \param size[in]			The size(in bytes) of the data to be read

 \return none, data = ISP[ID].dmem[addr]
 */
STORAGE_CLASS_ISP_H uint32_t isp_dmem_load_uint32(
    const isp_ID_t		ID,
    const unsigned int	addr);

/*! Concatenate the LSW and MSW into a double precision word

 \param	x0[in]				Integer containing the LSW
 \param	x1[in]				Integer containing the MSW

 \return x0 | (x1 << bits_per_vector_element)
 */
STORAGE_CLASS_ISP_H uint32_t isp_2w_cat_1w(
    const u16		x0,
    const uint16_t		x1);

unsigned int isp_is_ready(isp_ID_t ID);

unsigned int isp_is_sleeping(isp_ID_t ID);

void isp_start(isp_ID_t ID);

void isp_wake(isp_ID_t ID);

#endif /* __ISP_PUBLIC_H_INCLUDED__ */
