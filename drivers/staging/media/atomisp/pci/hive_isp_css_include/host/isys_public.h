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

#ifndef __ISYS_PUBLIC_H_INCLUDED__
#define __ISYS_PUBLIC_H_INCLUDED__

#ifdef ISP2401
/*! Read the state of INPUT_SYSTEM[ID]
 \param ID[in]		INPUT_SYSTEM identifier
 \param state[out]	pointer to input system state structure
 \return none, state = INPUT_SYSTEM[ID].state
 */
STORAGE_CLASS_INPUT_SYSTEM_H input_system_err_t input_system_get_state(
    const input_system_ID_t	ID,
    input_system_state_t *state);
/*! Dump the state of INPUT_SYSTEM[ID]
 \param ID[in]		INPUT_SYSTEM identifier
 \param state[in]	pointer to input system state structure
 \return none
 \depends on host supplied print function as part of ia_css_init()
 */
STORAGE_CLASS_INPUT_SYSTEM_H void input_system_dump_state(
    const input_system_ID_t	ID,
    input_system_state_t *state);
#endif /* ISP2401 */
#endif /* __ISYS_PUBLIC_H_INCLUDED__ */
