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

#ifndef __CSI_RX_PUBLIC_H_INCLUDED__
#define __CSI_RX_PUBLIC_H_INCLUDED__

#ifdef USE_INPUT_SYSTEM_VERSION_2401
/*****************************************************
 *
 * Native command interface (NCI).
 *
 *****************************************************/
/**
 * @brief Get the csi rx frontend state.
 * Get the state of the csi rx frontend regiester-set.
 *
 * @param[in]	id	The global unique ID of the csi rx fe controller.
 * @param[out]	state	Point to the register-state.
 */
extern void csi_rx_fe_ctrl_get_state(
		const csi_rx_frontend_ID_t ID,
		csi_rx_fe_ctrl_state_t *state);
/**
 * @brief Dump the csi rx frontend state.
 * Dump the state of the csi rx frontend regiester-set.
 *
 * @param[in]	id	The global unique ID of the csi rx fe controller.
 * @param[in]	state	Point to the register-state.
 */
extern void csi_rx_fe_ctrl_dump_state(
		const csi_rx_frontend_ID_t ID,
		csi_rx_fe_ctrl_state_t *state);
/**
 * @brief Get the state of the csi rx fe dlane.
 * Get the state of the register set per dlane process.
 *
 * @param[in]	id			The global unique ID of the input-buffer controller.
 * @param[in]	lane		The lane ID.
 * @param[out]	state		Point to the dlane state.
 */
extern void csi_rx_fe_ctrl_get_dlane_state(
		const csi_rx_frontend_ID_t ID,
		const uint32_t lane,
		csi_rx_fe_ctrl_lane_t *dlane_state);
/**
 * @brief Get the csi rx backend state.
 * Get the state of the csi rx backend regiester-set.
 *
 * @param[in]	id	The global unique ID of the csi rx be controller.
 * @param[out]	state	Point to the register-state.
 */
extern void csi_rx_be_ctrl_get_state(
		const csi_rx_backend_ID_t ID,
		csi_rx_be_ctrl_state_t *state);
/**
 * @brief Dump the csi rx backend state.
 * Dump the state of the csi rx backend regiester-set.
 *
 * @param[in]	id	The global unique ID of the csi rx be controller.
 * @param[in]	state	Point to the register-state.
 */
extern void csi_rx_be_ctrl_dump_state(
		const csi_rx_backend_ID_t ID,
		csi_rx_be_ctrl_state_t *state);
/* end of NCI */

/*****************************************************
 *
 * Device level interface (DLI).
 *
 *****************************************************/
/**
 * @brief Load the register value.
 * Load the value of the register of the csi rx fe.
 *
 * @param[in]	ID	The global unique ID for the ibuf-controller instance.
 * @param[in]	reg	The offet address of the register.
 *
 * @return the value of the register.
 */
extern hrt_data csi_rx_fe_ctrl_reg_load(
	const csi_rx_frontend_ID_t ID,
	const hrt_address reg);
/**
 * @brief Store a value to the register.
 * Store a value to the registe of the csi rx fe.
 *
 * @param[in]	ID		The global unique ID for the ibuf-controller instance.
 * @param[in]	reg		The offet address of the register.
 * @param[in]	value	The value to be stored.
 *
 */
extern void csi_rx_fe_ctrl_reg_store(
	const csi_rx_frontend_ID_t ID,
	const hrt_address reg,
	const hrt_data value);
/**
 * @brief Load the register value.
 * Load the value of the register of the csirx be.
 *
 * @param[in]	ID	The global unique ID for the ibuf-controller instance.
 * @param[in]	reg	The offet address of the register.
 *
 * @return the value of the register.
 */
extern hrt_data csi_rx_be_ctrl_reg_load(
	const csi_rx_backend_ID_t ID,
	const hrt_address reg);
/**
 * @brief Store a value to the register.
 * Store a value to the registe of the csi rx be.
 *
 * @param[in]	ID		The global unique ID for the ibuf-controller instance.
 * @param[in]	reg		The offet address of the register.
 * @param[in]	value	The value to be stored.
 *
 */
extern void csi_rx_be_ctrl_reg_store(
	const csi_rx_backend_ID_t ID,
	const hrt_address reg,
	const hrt_data value);
/* end of DLI */
#endif /* USE_INPUT_SYSTEM_VERSION_2401 */
#endif /* __CSI_RX_PUBLIC_H_INCLUDED__ */
