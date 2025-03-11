/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __ISYS_IRQ_PUBLIC_H__
#define __ISYS_IRQ_PUBLIC_H__

#include "isys_irq_global.h"
#include "isys_irq_local.h"


void isys_irqc_state_get(const isys_irq_ID_t	isys_irqc_id,
			 isys_irqc_state_t	*state);

void isys_irqc_state_dump(const isys_irq_ID_t	isys_irqc_id,
			  const isys_irqc_state_t *state);

void isys_irqc_reg_store(const isys_irq_ID_t	isys_irqc_id,
			 const unsigned int	reg_idx,
			 const hrt_data		value);

hrt_data isys_irqc_reg_load(const isys_irq_ID_t	isys_irqc_id,
			    const unsigned int	reg_idx);

void isys_irqc_status_enable(const isys_irq_ID_t isys_irqc_id);


#endif	/* __ISYS_IRQ_PUBLIC_H__ */
