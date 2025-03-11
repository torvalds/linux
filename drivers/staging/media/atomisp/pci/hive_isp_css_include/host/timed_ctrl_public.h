/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __TIMED_CTRL_PUBLIC_H_INCLUDED__
#define __TIMED_CTRL_PUBLIC_H_INCLUDED__

#include "system_local.h"

/*! Write to a control register of TIMED_CTRL[ID]

 \param	ID[in]				TIMED_CTRL identifier
 \param	reg_addr[in]		register byte address
 \param value[in]			The data to be written

 \return none, TIMED_CTRL[ID].ctrl[reg] = value
 */
STORAGE_CLASS_TIMED_CTRL_H void timed_ctrl_reg_store(
    const timed_ctrl_ID_t	ID,
    const unsigned int		reg_addr,
    const hrt_data			value);

void timed_ctrl_snd_commnd(
    const timed_ctrl_ID_t				ID,
    hrt_data				mask,
    hrt_data				condition,
    hrt_data				counter,
    hrt_address				addr,
    hrt_data				value);

void timed_ctrl_snd_sp_commnd(
    const timed_ctrl_ID_t				ID,
    hrt_data				mask,
    hrt_data				condition,
    hrt_data				counter,
    const sp_ID_t			SP_ID,
    hrt_address				offset,
    hrt_data				value);

void timed_ctrl_snd_gpio_commnd(
    const timed_ctrl_ID_t				ID,
    hrt_data				mask,
    hrt_data				condition,
    hrt_data				counter,
    const gpio_ID_t			GPIO_ID,
    hrt_address				offset,
    hrt_data				value);

#endif /* __TIMED_CTRL_PUBLIC_H_INCLUDED__ */
