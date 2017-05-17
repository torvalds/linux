/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2010-2015, Intel Corporation.
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

#include "assert_support.h"
#include "irq.h"

#ifndef __INLINE_GP_DEVICE__
#define __INLINE_GP_DEVICE__
#endif
#include "gp_device.h"	/* _REG_GP_IRQ_REQUEST_ADDR */

#include "platform_support.h"			/* hrt_sleep() */

STORAGE_CLASS_INLINE void irq_wait_for_write_complete(
	const irq_ID_t		ID);

STORAGE_CLASS_INLINE bool any_irq_channel_enabled(
	const irq_ID_t				ID);

STORAGE_CLASS_INLINE irq_ID_t virq_get_irq_id(
	const virq_id_t		irq_ID,
	unsigned int		*channel_ID);

#ifndef __INLINE_IRQ__
#include "irq_private.h"
#endif /* __INLINE_IRQ__ */

static unsigned short IRQ_N_CHANNEL[N_IRQ_ID] = {
	IRQ0_ID_N_CHANNEL,
	IRQ1_ID_N_CHANNEL,
	IRQ2_ID_N_CHANNEL,
	IRQ3_ID_N_CHANNEL};

static unsigned short IRQ_N_ID_OFFSET[N_IRQ_ID + 1] = {
	IRQ0_ID_OFFSET,
	IRQ1_ID_OFFSET,
	IRQ2_ID_OFFSET,
	IRQ3_ID_OFFSET,
	IRQ_END_OFFSET};

static virq_id_t IRQ_NESTING_ID[N_IRQ_ID] = {
	N_virq_id,
	virq_ifmt,
	virq_isys,
	virq_isel};

void irq_clear_all(
	const irq_ID_t				ID)
{
	hrt_data	mask = 0xFFFFFFFF;

	assert(ID < N_IRQ_ID);
	assert(IRQ_N_CHANNEL[ID] <= HRT_DATA_WIDTH);

	if (IRQ_N_CHANNEL[ID] < HRT_DATA_WIDTH) {
		mask = ~((~(hrt_data)0)>>IRQ_N_CHANNEL[ID]);
	}

	irq_reg_store(ID,
		_HRT_IRQ_CONTROLLER_CLEAR_REG_IDX, mask);
return;
}

/*
 * Do we want the user to be able to set the signalling method ?
 */
void irq_enable_channel(
	const irq_ID_t				ID,
    const unsigned int			irq_id)
{
	unsigned int mask = irq_reg_load(ID,
		_HRT_IRQ_CONTROLLER_MASK_REG_IDX);
	unsigned int enable = irq_reg_load(ID,
		_HRT_IRQ_CONTROLLER_ENABLE_REG_IDX);
	unsigned int edge_in = irq_reg_load(ID,
		_HRT_IRQ_CONTROLLER_EDGE_REG_IDX);
	unsigned int me = 1U << irq_id;

	assert(ID < N_IRQ_ID);
	assert(irq_id < IRQ_N_CHANNEL[ID]);

	mask |= me;
	enable |= me;
	edge_in |= me;	/* rising edge */

/* to avoid mishaps configuration must follow the following order */

/* mask this interrupt */
	irq_reg_store(ID,
		_HRT_IRQ_CONTROLLER_MASK_REG_IDX, mask & ~me);
/* rising edge at input */
	irq_reg_store(ID,
		_HRT_IRQ_CONTROLLER_EDGE_REG_IDX, edge_in);
/* enable interrupt to output */
	irq_reg_store(ID,
		_HRT_IRQ_CONTROLLER_ENABLE_REG_IDX, enable);
/* clear current irq only */
	irq_reg_store(ID,
		_HRT_IRQ_CONTROLLER_CLEAR_REG_IDX, me);
/* unmask interrupt from input */
	irq_reg_store(ID,
		_HRT_IRQ_CONTROLLER_MASK_REG_IDX, mask);

	irq_wait_for_write_complete(ID);

return;
}

void irq_enable_pulse(
	const irq_ID_t	ID,
	bool 			pulse)
{
	unsigned int edge_out = 0x0;

	if (pulse) {
		edge_out = 0xffffffff;
	}
	/* output is given as edge, not pulse */
	irq_reg_store(ID,
		_HRT_IRQ_CONTROLLER_EDGE_NOT_PULSE_REG_IDX, edge_out);
return;
}

void irq_disable_channel(
	const irq_ID_t				ID,
	const unsigned int			irq_id)
{
	unsigned int mask = irq_reg_load(ID,
		_HRT_IRQ_CONTROLLER_MASK_REG_IDX);
	unsigned int enable = irq_reg_load(ID,
		_HRT_IRQ_CONTROLLER_ENABLE_REG_IDX);
	unsigned int me = 1U << irq_id;

	assert(ID < N_IRQ_ID);
	assert(irq_id < IRQ_N_CHANNEL[ID]);

	mask &= ~me;
	enable &= ~me;

/* enable interrupt to output */
	irq_reg_store(ID,
		_HRT_IRQ_CONTROLLER_ENABLE_REG_IDX, enable);
/* unmask interrupt from input */
	irq_reg_store(ID,
		_HRT_IRQ_CONTROLLER_MASK_REG_IDX, mask);
/* clear current irq only */
	irq_reg_store(ID,
		_HRT_IRQ_CONTROLLER_CLEAR_REG_IDX, me);

	irq_wait_for_write_complete(ID);

return;
}

enum hrt_isp_css_irq_status irq_get_channel_id(
	const irq_ID_t				ID,
	unsigned int				*irq_id)
{
	unsigned int irq_status = irq_reg_load(ID,
		_HRT_IRQ_CONTROLLER_STATUS_REG_IDX);
	unsigned int idx;
	enum hrt_isp_css_irq_status status = hrt_isp_css_irq_status_success;

	assert(ID < N_IRQ_ID);
	assert(irq_id != NULL);

/* find the first irq bit */
	for (idx = 0; idx < IRQ_N_CHANNEL[ID]; idx++) {
		if (irq_status & (1U << idx))
			break;
	}
	if (idx == IRQ_N_CHANNEL[ID])
		return hrt_isp_css_irq_status_error;

/* now check whether there are more bits set */
	if (irq_status != (1U << idx))
		status = hrt_isp_css_irq_status_more_irqs;

	irq_reg_store(ID,
		_HRT_IRQ_CONTROLLER_CLEAR_REG_IDX, 1U << idx);

	irq_wait_for_write_complete(ID);

	if (irq_id != NULL)
		*irq_id = (unsigned int)idx;

return status;
}

static const hrt_address IRQ_REQUEST_ADDR[N_IRQ_SW_CHANNEL_ID] = {
	_REG_GP_IRQ_REQUEST0_ADDR,
	_REG_GP_IRQ_REQUEST1_ADDR};

void irq_raise(
	const irq_ID_t				ID,
	const irq_sw_channel_id_t	irq_id)
{
	hrt_address		addr;

	OP___assert(ID == IRQ0_ID);
	OP___assert(IRQ_BASE[ID] != (hrt_address)-1);
	OP___assert(irq_id < N_IRQ_SW_CHANNEL_ID);

	(void)ID;

	addr = IRQ_REQUEST_ADDR[irq_id];
/* The SW IRQ pins are remapped to offset zero */
	gp_device_reg_store(GP_DEVICE0_ID,
		(unsigned int)addr, 1);
	gp_device_reg_store(GP_DEVICE0_ID,
		(unsigned int)addr, 0);
return;
}

void irq_controller_get_state(
	const irq_ID_t				ID,
	irq_controller_state_t		*state)
{
	assert(ID < N_IRQ_ID);
	assert(state != NULL);

	state->irq_edge = irq_reg_load(ID,
		_HRT_IRQ_CONTROLLER_EDGE_REG_IDX);
	state->irq_mask = irq_reg_load(ID,
		_HRT_IRQ_CONTROLLER_MASK_REG_IDX);
	state->irq_status = irq_reg_load(ID,
		_HRT_IRQ_CONTROLLER_STATUS_REG_IDX);
	state->irq_enable = irq_reg_load(ID,
		_HRT_IRQ_CONTROLLER_ENABLE_REG_IDX);
	state->irq_level_not_pulse = irq_reg_load(ID,
		_HRT_IRQ_CONTROLLER_EDGE_NOT_PULSE_REG_IDX);
return;
}

bool any_virq_signal(void)
{
	unsigned int irq_status = irq_reg_load(IRQ0_ID,
		_HRT_IRQ_CONTROLLER_STATUS_REG_IDX);

return (irq_status != 0);
}

void cnd_virq_enable_channel(
	const virq_id_t				irq_ID,
	const bool					en)
{
	irq_ID_t		i;
	unsigned int	channel_ID;
	irq_ID_t		ID = virq_get_irq_id(irq_ID, &channel_ID);
	
	assert(ID < N_IRQ_ID);

	for (i=IRQ1_ID;i<N_IRQ_ID;i++) {
	/* It is not allowed to enable the pin of a nested IRQ directly */
		assert(irq_ID != IRQ_NESTING_ID[i]);
	}

	if (en) {
		irq_enable_channel(ID, channel_ID);
		if (IRQ_NESTING_ID[ID] != N_virq_id) {
/* Single level nesting, otherwise we'd need to recurse */
			irq_enable_channel(IRQ0_ID, IRQ_NESTING_ID[ID]);
		}
	} else {
		irq_disable_channel(ID, channel_ID);
		if ((IRQ_NESTING_ID[ID] != N_virq_id) && !any_irq_channel_enabled(ID)) {
/* Only disable the top if the nested ones are empty */
			irq_disable_channel(IRQ0_ID, IRQ_NESTING_ID[ID]);
		}
	}
return;
}


void virq_clear_all(void)
{
	irq_ID_t	irq_id;

	for (irq_id = (irq_ID_t)0; irq_id < N_IRQ_ID; irq_id++) {
		irq_clear_all(irq_id);
	}
return;
}

enum hrt_isp_css_irq_status virq_get_channel_signals(
	virq_info_t					*irq_info)
{
	enum hrt_isp_css_irq_status irq_status = hrt_isp_css_irq_status_error;
	irq_ID_t ID;

	assert(irq_info != NULL);

	for (ID = (irq_ID_t)0 ; ID < N_IRQ_ID; ID++) {
		if (any_irq_channel_enabled(ID)) {
			hrt_data	irq_data = irq_reg_load(ID,
				_HRT_IRQ_CONTROLLER_STATUS_REG_IDX);

			if (irq_data != 0) {
/* The error condition is an IRQ pulse received with no IRQ status written */
				irq_status = hrt_isp_css_irq_status_success;
			}

			irq_info->irq_status_reg[ID] |= irq_data;

			irq_reg_store(ID,
				_HRT_IRQ_CONTROLLER_CLEAR_REG_IDX, irq_data);

			irq_wait_for_write_complete(ID);
		}
	}

return irq_status;
}

void virq_clear_info(
	virq_info_t					*irq_info)
{
	irq_ID_t ID;

	assert(irq_info != NULL);

	for (ID = (irq_ID_t)0 ; ID < N_IRQ_ID; ID++) {
			irq_info->irq_status_reg[ID] = 0;
	}
return;
}

enum hrt_isp_css_irq_status virq_get_channel_id(
	virq_id_t					*irq_id)
{
	unsigned int irq_status = irq_reg_load(IRQ0_ID,
		_HRT_IRQ_CONTROLLER_STATUS_REG_IDX);
	unsigned int idx;
	enum hrt_isp_css_irq_status status = hrt_isp_css_irq_status_success;
	irq_ID_t ID;

	assert(irq_id != NULL);

/* find the first irq bit on device 0 */
	for (idx = 0; idx < IRQ_N_CHANNEL[IRQ0_ID]; idx++) {
		if (irq_status & (1U << idx))
			break;
	}

	if (idx == IRQ_N_CHANNEL[IRQ0_ID]) {
		return hrt_isp_css_irq_status_error;
	}

/* Check whether there are more bits set on device 0 */
	if (irq_status != (1U << idx)) {
		status = hrt_isp_css_irq_status_more_irqs;
	}

/* Check whether we have an IRQ on one of the nested devices */
	for (ID = N_IRQ_ID-1 ; ID > (irq_ID_t)0; ID--) {
		if (IRQ_NESTING_ID[ID] == (virq_id_t)idx) {
			break;
		}
	}

/* If we have a nested IRQ, load that state, discard the device 0 state */
	if (ID != IRQ0_ID) {
		irq_status = irq_reg_load(ID,
			_HRT_IRQ_CONTROLLER_STATUS_REG_IDX);
/* find the first irq bit on device "id" */
		for (idx = 0; idx < IRQ_N_CHANNEL[ID]; idx++) {
			if (irq_status & (1U << idx))
				break;
		}

		if (idx == IRQ_N_CHANNEL[ID]) {
			return hrt_isp_css_irq_status_error;
		}

/* Alternatively check whether there are more bits set on this device */
		if (irq_status != (1U << idx)) {
			status = hrt_isp_css_irq_status_more_irqs;
		} else {
/* If this device is empty, clear the state on device 0 */
			irq_reg_store(IRQ0_ID,
				_HRT_IRQ_CONTROLLER_CLEAR_REG_IDX, 1U << IRQ_NESTING_ID[ID]);
		}
	} /* if (ID != IRQ0_ID) */

/* Here we proceed to clear the IRQ on detected device, if no nested IRQ, this is device 0 */
	irq_reg_store(ID,
		_HRT_IRQ_CONTROLLER_CLEAR_REG_IDX, 1U << idx);

	irq_wait_for_write_complete(ID);

	idx += IRQ_N_ID_OFFSET[ID];
	if (irq_id != NULL)
		*irq_id = (virq_id_t)idx;

return status;
}

STORAGE_CLASS_INLINE void irq_wait_for_write_complete(
	const irq_ID_t		ID)
{
	assert(ID < N_IRQ_ID);
	assert(IRQ_BASE[ID] != (hrt_address)-1);
	(void)ia_css_device_load_uint32(IRQ_BASE[ID] +
		_HRT_IRQ_CONTROLLER_ENABLE_REG_IDX*sizeof(hrt_data));
}

STORAGE_CLASS_INLINE bool any_irq_channel_enabled(
	const irq_ID_t				ID)
{
	hrt_data	en_reg;

	assert(ID < N_IRQ_ID);

	en_reg = irq_reg_load(ID,
		_HRT_IRQ_CONTROLLER_ENABLE_REG_IDX);

return (en_reg != 0);
}

STORAGE_CLASS_INLINE irq_ID_t virq_get_irq_id(
	const virq_id_t		irq_ID,
	unsigned int		*channel_ID)
{
	irq_ID_t ID;

	assert(channel_ID != NULL);

	for (ID = (irq_ID_t)0 ; ID < N_IRQ_ID; ID++) {
		if (irq_ID < IRQ_N_ID_OFFSET[ID + 1]) {
			break;
		}
	}

	*channel_ID = (unsigned int)irq_ID - IRQ_N_ID_OFFSET[ID];

return ID;
}
