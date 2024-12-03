/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2010-2015, Intel Corporation.
 */

#ifndef __IRQ_PRIVATE_H_INCLUDED__
#define __IRQ_PRIVATE_H_INCLUDED__

#include "irq_public.h"

#include "device_access.h"

#include "assert_support.h"

STORAGE_CLASS_IRQ_C void irq_reg_store(
    const irq_ID_t		ID,
    const unsigned int	reg,
    const hrt_data		value)
{
	assert(ID < N_IRQ_ID);
	assert(IRQ_BASE[ID] != (hrt_address) - 1);
	ia_css_device_store_uint32(IRQ_BASE[ID] + reg * sizeof(hrt_data), value);
	return;
}

STORAGE_CLASS_IRQ_C hrt_data irq_reg_load(
    const irq_ID_t		ID,
    const unsigned int	reg)
{
	assert(ID < N_IRQ_ID);
	assert(IRQ_BASE[ID] != (hrt_address) - 1);
	return ia_css_device_load_uint32(IRQ_BASE[ID] + reg * sizeof(hrt_data));
}

#endif /* __IRQ_PRIVATE_H_INCLUDED__ */
