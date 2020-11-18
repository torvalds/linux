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

#ifndef __IBUF_CTRL_PUBLIC_H_INCLUDED__
#define __IBUF_CTRL_PUBLIC_H_INCLUDED__

#ifdef USE_INPUT_SYSTEM_VERSION_2401
/*****************************************************
 *
 * Native command interface (NCI).
 *
 *****************************************************/
/**
 * @brief Get the ibuf-controller state.
 * Get the state of the ibuf-controller regiester-set.
 *
 * @param[in]	id		The global unique ID of the input-buffer controller.
 * @param[out]	state	Point to the register-state.
 */
STORAGE_CLASS_IBUF_CTRL_H void ibuf_ctrl_get_state(
    const ibuf_ctrl_ID_t ID,
    ibuf_ctrl_state_t *state);

/**
 * @brief Get the state of the ibuf-controller process.
 * Get the state of the register set per buf-controller process.
 *
 * @param[in]	id			The global unique ID of the input-buffer controller.
 * @param[in]	proc_id		The process ID.
 * @param[out]	state		Point to the process state.
 */
STORAGE_CLASS_IBUF_CTRL_H void ibuf_ctrl_get_proc_state(
    const ibuf_ctrl_ID_t ID,
    const u32 proc_id,
    ibuf_ctrl_proc_state_t *state);
/**
 * @brief Dump the ibuf-controller state.
 * Dump the state of the ibuf-controller regiester-set.
 *
 * @param[in]	id		The global unique ID of the input-buffer controller.
 * @param[in]	state		Pointer to the register-state.
 */
STORAGE_CLASS_IBUF_CTRL_H void ibuf_ctrl_dump_state(
    const ibuf_ctrl_ID_t ID,
    ibuf_ctrl_state_t *state);
/* end of NCI */

/*****************************************************
 *
 * Device level interface (DLI).
 *
 *****************************************************/
/**
 * @brief Load the register value.
 * Load the value of the register of the ibuf-controller.
 *
 * @param[in]	ID	The global unique ID for the ibuf-controller instance.
 * @param[in]	reg	The offset address of the register.
 *
 * @return the value of the register.
 */
STORAGE_CLASS_IBUF_CTRL_H hrt_data ibuf_ctrl_reg_load(
    const ibuf_ctrl_ID_t ID,
    const hrt_address reg);

/**
 * @brief Store a value to the register.
 * Store a value to the registe of the ibuf-controller.
 *
 * @param[in]	ID		The global unique ID for the ibuf-controller instance.
 * @param[in]	reg		The offset address of the register.
 * @param[in]	value	The value to be stored.
 *
 */
STORAGE_CLASS_IBUF_CTRL_H void ibuf_ctrl_reg_store(
    const ibuf_ctrl_ID_t ID,
    const hrt_address reg,
    const hrt_data value);
/* end of DLI */

#endif /* USE_INPUT_SYSTEM_VERSION_2401 */
#endif /* __IBUF_CTRL_PUBLIC_H_INCLUDED__ */
