/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __ISYS_IRQ_PRIVATE_H__
#define __ISYS_IRQ_PRIVATE_H__

#include "isys_irq_global.h"
#include "isys_irq_local.h"


/* -------------------------------------------------------+
 |             Native command interface (NCI)             |
 + -------------------------------------------------------*/

/**
* @brief Get the isys irq status.
* Refer to "isys_irq.h" for details.
*/
void isys_irqc_state_get(
    const isys_irq_ID_t	isys_irqc_id,
    isys_irqc_state_t *state)
{
	state->edge     = isys_irqc_reg_load(isys_irqc_id, ISYS_IRQ_EDGE_REG_IDX);
	state->mask     = isys_irqc_reg_load(isys_irqc_id, ISYS_IRQ_MASK_REG_IDX);
	state->status   = isys_irqc_reg_load(isys_irqc_id, ISYS_IRQ_STATUS_REG_IDX);
	state->enable   = isys_irqc_reg_load(isys_irqc_id, ISYS_IRQ_ENABLE_REG_IDX);
	state->level_no = isys_irqc_reg_load(isys_irqc_id, ISYS_IRQ_LEVEL_NO_REG_IDX);
	/*
	** Invalid to read/load from write-only register 'clear'
	** state->clear = isys_irqc_reg_load(isys_irqc_id, ISYS_IRQ_CLEAR_REG_IDX);
	*/
}

/**
* @brief Dump the isys irq status.
* Refer to "isys_irq.h" for details.
*/
void isys_irqc_state_dump(
    const isys_irq_ID_t	isys_irqc_id,
    const isys_irqc_state_t *state)
{
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
			    "isys irq controller id %d\n\tstatus:0x%x\n\tedge:0x%x\n\tmask:0x%x\n\tenable:0x%x\n\tlevel_not_pulse:0x%x\n",
			    isys_irqc_id,
			    state->status, state->edge, state->mask, state->enable, state->level_no);
}

/* end of NCI */

/* -------------------------------------------------------+
 |              Device level interface (DLI)              |
 + -------------------------------------------------------*/

/* Support functions */
void isys_irqc_reg_store(
    const isys_irq_ID_t	isys_irqc_id,
    const unsigned int	reg_idx,
    const hrt_data	value)
{
	unsigned int reg_addr;

	assert(isys_irqc_id < N_ISYS_IRQ_ID);
	assert(reg_idx <= ISYS_IRQ_LEVEL_NO_REG_IDX);

	reg_addr = ISYS_IRQ_BASE[isys_irqc_id] + (reg_idx * sizeof(hrt_data));
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
			    "isys irq store at addr(0x%x) val(%u)\n", reg_addr, (unsigned int)value);

	ia_css_device_store_uint32(reg_addr, value);
}

hrt_data isys_irqc_reg_load(
    const isys_irq_ID_t	isys_irqc_id,
    const unsigned int	reg_idx)
{
	unsigned int reg_addr;
	hrt_data value;

	assert(isys_irqc_id < N_ISYS_IRQ_ID);
	assert(reg_idx <= ISYS_IRQ_LEVEL_NO_REG_IDX);

	reg_addr = ISYS_IRQ_BASE[isys_irqc_id] + (reg_idx * sizeof(hrt_data));
	value = ia_css_device_load_uint32(reg_addr);
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
			    "isys irq load from addr(0x%x) val(%u)\n", reg_addr, (unsigned int)value);

	return value;
}

/* end of DLI */


#endif	/* __ISYS_IRQ_PRIVATE_H__ */
