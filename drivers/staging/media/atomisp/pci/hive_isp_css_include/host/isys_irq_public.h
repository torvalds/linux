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

#ifndef __ISYS_IRQ_PUBLIC_H__
#define __ISYS_IRQ_PUBLIC_H__

#include "isys_irq_global.h"
#include "isys_irq_local.h"

#if defined(USE_INPUT_SYSTEM_VERSION_2401)

STORAGE_CLASS_ISYS2401_IRQ_H void isys_irqc_state_get(
    const isys_irq_ID_t	isys_irqc_id,
    isys_irqc_state_t	*state);

STORAGE_CLASS_ISYS2401_IRQ_H void isys_irqc_state_dump(
    const isys_irq_ID_t	isys_irqc_id,
    const isys_irqc_state_t *state);

STORAGE_CLASS_ISYS2401_IRQ_H void isys_irqc_reg_store(
    const isys_irq_ID_t	isys_irqc_id,
    const unsigned int	reg_idx,
    const hrt_data		value);

STORAGE_CLASS_ISYS2401_IRQ_H hrt_data isys_irqc_reg_load(
    const isys_irq_ID_t	isys_irqc_id,
    const unsigned int	reg_idx);

STORAGE_CLASS_ISYS2401_IRQ_H void isys_irqc_status_enable(
    const isys_irq_ID_t	isys_irqc_id);

#endif /* defined(USE_INPUT_SYSTEM_VERSION_2401) */

#endif	/* __ISYS_IRQ_PUBLIC_H__ */
