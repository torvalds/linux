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

#ifndef __ISYS_STREAM2MMIO_PUBLIC_H_INCLUDED__
#define __ISYS_STREAM2MMIO_PUBLIC_H_INCLUDED__

/*****************************************************
 *
 * Native command interface (NCI).
 *
 *****************************************************/
/**
 * @brief Get the stream2mmio-controller state.
 * Get the state of the stream2mmio-controller regiester-set.
 *
 * @param[in]	id		The global unique ID of the steeam2mmio controller.
 * @param[out]	state	Point to the register-state.
 */
STORAGE_CLASS_STREAM2MMIO_H void stream2mmio_get_state(
		const stream2mmio_ID_t ID,
		stream2mmio_state_t *state);

/**
 * @brief Get the state of the stream2mmio-controller sidess.
 * Get the state of the register set per buf-controller sidess.
 *
 * @param[in]	id		The global unique ID of the steeam2mmio controller.
 * @param[in]	sid_id		The sid ID.
 * @param[out]	state		Point to the sid state.
 */
STORAGE_CLASS_STREAM2MMIO_H void stream2mmio_get_sid_state(
		const stream2mmio_ID_t ID,
		const stream2mmio_sid_ID_t sid_id,
		stream2mmio_sid_state_t *state);
/* end of NCI */

/*****************************************************
 *
 * Device level interface (DLI).
 *
 *****************************************************/
/**
 * @brief Load the register value.
 * Load the value of the register of the stream2mmio-controller.
 *
 * @param[in]	ID	The global unique ID for the stream2mmio-controller instance.
 * @param[in]	sid_id	The SID in question.
 * @param[in]	reg_idx	The offet address of the register.
 *
 * @return the value of the register.
 */
STORAGE_CLASS_STREAM2MMIO_H hrt_data stream2mmio_reg_load(
		const stream2mmio_ID_t ID,
		const stream2mmio_sid_ID_t sid_id,
		const uint32_t reg_idx);

/**
 * @brief Dump the SID processor state.
 * Dump the state of the sid regiester-set.
 *
 * @param[in]	state		Pointer to the register-state.
 */
STORAGE_CLASS_STREAM2MMIO_H void stream2mmio_print_sid_state(
		stream2mmio_sid_state_t	*state);
/**
 * @brief Dump the stream2mmio state.
 * Dump the state of the ibuf-controller regiester-set.
 *
 * @param[in]	id		The global unique ID of the st2mmio
 * @param[in]	state		Pointer to the register-state.
 */
STORAGE_CLASS_STREAM2MMIO_H void stream2mmio_dump_state(
		const stream2mmio_ID_t ID,
		stream2mmio_state_t *state);
/**
 * @brief Store a value to the register.
 * Store a value to the registe of the stream2mmio-controller.
 *
 * @param[in]	ID		The global unique ID for the stream2mmio-controller instance.
 * @param[in]	reg		The offet address of the register.
 * @param[in]	value	The value to be stored.
 *
 */
STORAGE_CLASS_STREAM2MMIO_H void stream2mmio_reg_store(
		const stream2mmio_ID_t ID,
		const hrt_address reg,
		const hrt_data value);
/* end of DLI */

#endif /* __ISYS_STREAM2MMIO_PUBLIC_H_INCLUDED__ */
