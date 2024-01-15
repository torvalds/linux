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

#ifndef __INPUT_SYSTEM_2400_PUBLIC_H_INCLUDED__
#define __INPUT_SYSTEM_2400_PUBLIC_H_INCLUDED__

#include <type_support.h>

/*! Set compression parameters for cfg[cfg_ID] of RECEIVER[ID]

 \param	ID[in]				RECEIVER identifier
 \param	cfg_ID[in]			Configuration identifier
 \param	comp[in]			Compression method
 \param	pred[in]			Predictor method

 \NOTE: the storage of compression configuration is
	implementation specific. The config can be
	carried either on MIPI ports or on MIPI channels

 \return none, RECEIVER[ID].cfg[cfg_ID] = {comp, pred}
 */
void receiver_set_compression(
    const rx_ID_t				ID,
    const unsigned int			cfg_ID,
    const mipi_compressor_t		comp,
    const mipi_predictor_t		pred);

/*! Enable PORT[port_ID] of RECEIVER[ID]

 \param	ID[in]				RECEIVER identifier
 \param	port_ID[in]			mipi PORT identifier
 \param	cnd[in]				irq predicate

 \return None, enable(RECEIVER[ID].PORT[port_ID])
 */
void receiver_port_enable(
    const rx_ID_t				ID,
    const enum mipi_port_id		port_ID,
    const bool					cnd);

/*! Flag if PORT[port_ID] of RECEIVER[ID] is enabled

 \param	ID[in]				RECEIVER identifier
 \param	port_ID[in]			mipi PORT identifier

 \return enable(RECEIVER[ID].PORT[port_ID]) == true
 */
bool is_receiver_port_enabled(
    const rx_ID_t				ID,
    const enum mipi_port_id		port_ID);

/*! Enable the IRQ channels of PORT[port_ID] of RECEIVER[ID]

 \param	ID[in]				RECEIVER identifier
 \param	port_ID[in]			mipi PORT identifier
 \param	irq_info[in]		irq channels

 \return None, enable(RECEIVER[ID].PORT[port_ID].irq_info)
 */
void receiver_irq_enable(
    const rx_ID_t				ID,
    const enum mipi_port_id		port_ID,
    const rx_irq_info_t			irq_info);

/*! Return the IRQ status of PORT[port_ID] of RECEIVER[ID]

 \param	ID[in]				RECEIVER identifier
 \param	port_ID[in]			mipi PORT identifier

 \return RECEIVER[ID].PORT[port_ID].irq_info
 */
rx_irq_info_t receiver_get_irq_info(
    const rx_ID_t				ID,
    const enum mipi_port_id		port_ID);

/*! Clear the IRQ status of PORT[port_ID] of RECEIVER[ID]

 \param	ID[in]				RECEIVER identifier
 \param	port_ID[in]			mipi PORT identifier
 \param	irq_info[in]		irq status

 \return None, clear(RECEIVER[ID].PORT[port_ID].irq_info)
 */
void receiver_irq_clear(
    const rx_ID_t				ID,
    const enum mipi_port_id			port_ID,
    const rx_irq_info_t			irq_info);

/*! Write to a control register of INPUT_SYSTEM[ID]

 \param	ID[in]				INPUT_SYSTEM identifier
 \param	reg[in]				register index
 \param value[in]			The data to be written

 \return none, INPUT_SYSTEM[ID].ctrl[reg] = value
 */
STORAGE_CLASS_INPUT_SYSTEM_H void input_system_reg_store(
    const input_system_ID_t			ID,
    const hrt_address			reg,
    const hrt_data				value);

/*! Read from a control register of INPUT_SYSTEM[ID]

 \param	ID[in]				INPUT_SYSTEM identifier
 \param	reg[in]				register index
 \param value[in]			The data to be written

 \return INPUT_SYSTEM[ID].ctrl[reg]
 */
STORAGE_CLASS_INPUT_SYSTEM_H hrt_data input_system_reg_load(
    const input_system_ID_t			ID,
    const hrt_address			reg);

/*! Write to a control register of RECEIVER[ID]

 \param	ID[in]				RECEIVER identifier
 \param	reg[in]				register index
 \param value[in]			The data to be written

 \return none, RECEIVER[ID].ctrl[reg] = value
 */
STORAGE_CLASS_INPUT_SYSTEM_H void receiver_reg_store(
    const rx_ID_t				ID,
    const hrt_address			reg,
    const hrt_data				value);

/*! Read from a control register of RECEIVER[ID]

 \param	ID[in]				RECEIVER identifier
 \param	reg[in]				register index
 \param value[in]			The data to be written

 \return RECEIVER[ID].ctrl[reg]
 */
STORAGE_CLASS_INPUT_SYSTEM_H hrt_data receiver_reg_load(
    const rx_ID_t				ID,
    const hrt_address			reg);

/*! Write to a control register of PORT[port_ID] of RECEIVER[ID]

 \param	ID[in]				RECEIVER identifier
 \param	port_ID[in]			mipi PORT identifier
 \param	reg[in]				register index
 \param value[in]			The data to be written

 \return none, RECEIVER[ID].PORT[port_ID].ctrl[reg] = value
 */
STORAGE_CLASS_INPUT_SYSTEM_H void receiver_port_reg_store(
    const rx_ID_t				ID,
    const enum mipi_port_id			port_ID,
    const hrt_address			reg,
    const hrt_data				value);

/*! Read from a control register PORT[port_ID] of of RECEIVER[ID]

 \param	ID[in]				RECEIVER identifier
 \param	port_ID[in]			mipi PORT identifier
 \param	reg[in]				register index
 \param value[in]			The data to be written

 \return RECEIVER[ID].PORT[port_ID].ctrl[reg]
 */
STORAGE_CLASS_INPUT_SYSTEM_H hrt_data receiver_port_reg_load(
    const rx_ID_t				ID,
    const enum mipi_port_id		port_ID,
    const hrt_address			reg);

/*! Write to a control register of SUB_SYSTEM[sub_ID] of INPUT_SYSTEM[ID]

 \param	ID[in]				INPUT_SYSTEM identifier
 \param	port_ID[in]			sub system identifier
 \param	reg[in]				register index
 \param value[in]			The data to be written

 \return none, INPUT_SYSTEM[ID].SUB_SYSTEM[sub_ID].ctrl[reg] = value
 */
STORAGE_CLASS_INPUT_SYSTEM_H void input_system_sub_system_reg_store(
    const input_system_ID_t			ID,
    const sub_system_ID_t			sub_ID,
    const hrt_address			reg,
    const hrt_data				value);

/*! Read from a control register SUB_SYSTEM[sub_ID] of INPUT_SYSTEM[ID]

 \param	ID[in]				INPUT_SYSTEM identifier
 \param	port_ID[in]			sub system identifier
 \param	reg[in]				register index
 \param value[in]			The data to be written

 \return INPUT_SYSTEM[ID].SUB_SYSTEM[sub_ID].ctrl[reg]
 */
STORAGE_CLASS_INPUT_SYSTEM_H hrt_data input_system_sub_system_reg_load(
    const input_system_ID_t		ID,
    const sub_system_ID_t		sub_ID,
    const hrt_address			reg);

///////////////////////////////////////////////////////////////////////////
//
//    Functions for configuration phase on input system.
//
///////////////////////////////////////////////////////////////////////////

// Function that resets current configuration.
// remove the argument since it should be private.
input_system_err_t input_system_configuration_reset(void);

// Function that commits current configuration.
// remove the argument since it should be private.
input_system_err_t input_system_configuration_commit(void);

///////////////////////////////////////////////////////////////////////////
//
// User functions:
//		(encoded generic function)
//    - no checking
//    - decoding name and agruments into the generic (channel) configuration
//    function.
//
///////////////////////////////////////////////////////////////////////////

// FIFO channel config function user

input_system_err_t	input_system_csi_fifo_channel_cfg(
    u32				ch_id,
    input_system_csi_port_t	port,
    backend_channel_cfg_t	backend_ch,
    target_cfg2400_t			target
);

input_system_err_t	input_system_csi_fifo_channel_with_counting_cfg(
    u32				ch_id,
    u32				nof_frame,
    input_system_csi_port_t	port,
    backend_channel_cfg_t	backend_ch,
    u32				mem_region_size,
    u32				nof_mem_regions,
    target_cfg2400_t			target
);

// SRAM channel config function user

input_system_err_t	input_system_csi_sram_channel_cfg(
    u32				ch_id,
    input_system_csi_port_t	port,
    backend_channel_cfg_t	backend_ch,
    u32				csi_mem_region_size,
    u32				csi_nof_mem_regions,
    target_cfg2400_t			target
);

//XMEM channel config function user

input_system_err_t	input_system_csi_xmem_channel_cfg(
    u32				ch_id,
    input_system_csi_port_t port,
    backend_channel_cfg_t	backend_ch,
    u32				mem_region_size,
    u32				nof_mem_regions,
    u32				acq_mem_region_size,
    u32				acq_nof_mem_regions,
    target_cfg2400_t			target,
    uint32_t				nof_xmem_buffers
);

input_system_err_t	input_system_csi_xmem_capture_only_channel_cfg(
    u32				ch_id,
    u32				nof_frames,
    input_system_csi_port_t port,
    u32				csi_mem_region_size,
    u32				csi_nof_mem_regions,
    u32				acq_mem_region_size,
    u32				acq_nof_mem_regions,
    target_cfg2400_t			target
);

input_system_err_t	input_system_csi_xmem_acquire_only_channel_cfg(
    u32				ch_id,
    u32				nof_frames,
    input_system_csi_port_t port,
    backend_channel_cfg_t	backend_ch,
    u32				acq_mem_region_size,
    u32				acq_nof_mem_regions,
    target_cfg2400_t			target
);

// Non - CSI channel config function user

input_system_err_t	input_system_prbs_channel_cfg(
    u32		ch_id,
    u32		nof_frames,
    u32		seed,
    u32		sync_gen_width,
    u32		sync_gen_height,
    u32		sync_gen_hblank_cycles,
    u32		sync_gen_vblank_cycles,
    target_cfg2400_t	target
);

input_system_err_t	input_system_tpg_channel_cfg(
    u32		ch_id,
    u32		nof_frames,//not used yet
    u32		x_mask,
    u32		y_mask,
    u32		x_delta,
    u32		y_delta,
    u32		xy_mask,
    u32		sync_gen_width,
    u32		sync_gen_height,
    u32		sync_gen_hblank_cycles,
    u32		sync_gen_vblank_cycles,
    target_cfg2400_t	target
);

input_system_err_t	input_system_gpfifo_channel_cfg(
    u32		ch_id,
    u32		nof_frames,
    target_cfg2400_t	target
);

#endif /* __INPUT_SYSTEM_PUBLIC_H_INCLUDED__ */
