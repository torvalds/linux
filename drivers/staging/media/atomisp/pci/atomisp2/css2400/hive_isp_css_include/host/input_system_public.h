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

#ifndef __INPUT_SYSTEM_PUBLIC_H_INCLUDED__
#define __INPUT_SYSTEM_PUBLIC_H_INCLUDED__

#include <type_support.h>
#ifdef USE_INPUT_SYSTEM_VERSION_2401
#include "isys_public.h"
#else

typedef struct input_system_state_s		input_system_state_t;
typedef struct receiver_state_s			receiver_state_t;

/*! Read the state of INPUT_SYSTEM[ID]

 \param	ID[in]				INPUT_SYSTEM identifier
 \param	state[out]			input system state structure

 \return none, state = INPUT_SYSTEM[ID].state
 */
extern void input_system_get_state(
	const input_system_ID_t		ID,
	input_system_state_t		*state);

/*! Read the state of RECEIVER[ID]

 \param	ID[in]				RECEIVER identifier
 \param	state[out]			receiver state structure

 \return none, state = RECEIVER[ID].state
 */
extern void receiver_get_state(
	const rx_ID_t				ID,
	receiver_state_t			*state);

/*! Flag whether a MIPI format is YUV420

 \param	mipi_format[in]		MIPI format

 \return mipi_format == YUV420
 */
extern bool is_mipi_format_yuv420(
	const mipi_format_t			mipi_format);

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
extern void receiver_set_compression(
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
extern void receiver_port_enable(
	const rx_ID_t				ID,
	const mipi_port_ID_t		port_ID,
	const bool					cnd);

/*! Flag if PORT[port_ID] of RECEIVER[ID] is enabled

 \param	ID[in]				RECEIVER identifier
 \param	port_ID[in]			mipi PORT identifier

 \return enable(RECEIVER[ID].PORT[port_ID]) == true
 */
extern bool is_receiver_port_enabled(
	const rx_ID_t				ID,
	const mipi_port_ID_t		port_ID);

/*! Enable the IRQ channels of PORT[port_ID] of RECEIVER[ID]

 \param	ID[in]				RECEIVER identifier
 \param	port_ID[in]			mipi PORT identifier
 \param	irq_info[in]		irq channels

 \return None, enable(RECEIVER[ID].PORT[port_ID].irq_info)
 */
extern void receiver_irq_enable(
	const rx_ID_t				ID,
	const mipi_port_ID_t		port_ID,
	const rx_irq_info_t			irq_info);

/*! Return the IRQ status of PORT[port_ID] of RECEIVER[ID]

 \param	ID[in]				RECEIVER identifier
 \param	port_ID[in]			mipi PORT identifier

 \return RECEIVER[ID].PORT[port_ID].irq_info
 */
extern rx_irq_info_t receiver_get_irq_info(
	const rx_ID_t				ID,
	const mipi_port_ID_t		port_ID);

/*! Clear the IRQ status of PORT[port_ID] of RECEIVER[ID]

 \param	ID[in]				RECEIVER identifier
 \param	port_ID[in]			mipi PORT identifier
 \param	irq_info[in]		irq status

 \return None, clear(RECEIVER[ID].PORT[port_ID].irq_info)
 */
extern void receiver_irq_clear(
	const rx_ID_t				ID,
	const mipi_port_ID_t			port_ID,
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
	const mipi_port_ID_t			port_ID,
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
	const mipi_port_ID_t		port_ID,
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
input_system_error_t input_system_configuration_reset(void);

// Function that commits current configuration.
// remove the argument since it should be private.
input_system_error_t input_system_configuration_commit(void);

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

input_system_error_t	input_system_csi_fifo_channel_cfg(
	uint32_t				ch_id,
	input_system_csi_port_t	port,
	backend_channel_cfg_t	backend_ch,
	target_cfg2400_t			target
);

input_system_error_t	input_system_csi_fifo_channel_with_counting_cfg(
	uint32_t				ch_id,
	uint32_t				nof_frame,
	input_system_csi_port_t	port,
	backend_channel_cfg_t	backend_ch,
	uint32_t				mem_region_size,
	uint32_t				nof_mem_regions,
	target_cfg2400_t			target
);


// SRAM channel config function user

input_system_error_t	input_system_csi_sram_channel_cfg(
	uint32_t				ch_id,
	input_system_csi_port_t	port,
	backend_channel_cfg_t	backend_ch,
	uint32_t				csi_mem_region_size,
	uint32_t				csi_nof_mem_regions,
	target_cfg2400_t 			target
);


//XMEM channel config function user

input_system_error_t	input_system_csi_xmem_channel_cfg(
	uint32_t 				ch_id,
	input_system_csi_port_t port,
	backend_channel_cfg_t	backend_ch,
	uint32_t 				mem_region_size,
	uint32_t 				nof_mem_regions,
	uint32_t 				acq_mem_region_size,
	uint32_t 				acq_nof_mem_regions,
	target_cfg2400_t 			target,
	uint32_t 				nof_xmem_buffers
);

input_system_error_t	input_system_csi_xmem_capture_only_channel_cfg(
	uint32_t 				ch_id,
	uint32_t 				nof_frames,
	input_system_csi_port_t port,
	uint32_t 				csi_mem_region_size,
	uint32_t 				csi_nof_mem_regions,
	uint32_t 				acq_mem_region_size,
	uint32_t 				acq_nof_mem_regions,
	target_cfg2400_t 			target
);

input_system_error_t	input_system_csi_xmem_acquire_only_channel_cfg(
	uint32_t 				ch_id,
	uint32_t 				nof_frames,
	input_system_csi_port_t port,
	backend_channel_cfg_t	backend_ch,
	uint32_t 				acq_mem_region_size,
	uint32_t 				acq_nof_mem_regions,
	target_cfg2400_t 			target
);

// Non - CSI channel config function user

input_system_error_t	input_system_prbs_channel_cfg(
	uint32_t 		ch_id,
	uint32_t		nof_frames,
	uint32_t		seed,
	uint32_t		sync_gen_width,
	uint32_t		sync_gen_height,
	uint32_t		sync_gen_hblank_cycles,
	uint32_t		sync_gen_vblank_cycles,
	target_cfg2400_t	target
);


input_system_error_t	input_system_tpg_channel_cfg(
	uint32_t 		ch_id,
	uint32_t 		nof_frames,//not used yet
	uint32_t		x_mask,
	uint32_t		y_mask,
	uint32_t		x_delta,
	uint32_t		y_delta,
	uint32_t		xy_mask,
	uint32_t		sync_gen_width,
	uint32_t		sync_gen_height,
	uint32_t		sync_gen_hblank_cycles,
	uint32_t		sync_gen_vblank_cycles,
	target_cfg2400_t	target
);


input_system_error_t	input_system_gpfifo_channel_cfg(
	uint32_t 		ch_id,
	uint32_t 		nof_frames,
	target_cfg2400_t	target
);
#endif /* #ifdef USE_INPUT_SYSTEM_VERSION_2401 */

#endif /* __INPUT_SYSTEM_PUBLIC_H_INCLUDED__ */
