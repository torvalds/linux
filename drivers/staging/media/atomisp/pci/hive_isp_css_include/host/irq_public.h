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

#ifndef __IRQ_PUBLIC_H_INCLUDED__
#define __IRQ_PUBLIC_H_INCLUDED__

#include <type_support.h>
#include "system_local.h"

/*! Read the control registers of IRQ[ID]

 \param	ID[in]				IRQ identifier
 \param	state[out]			irq controller state structure

 \return none, state = IRQ[ID].state
 */
void irq_controller_get_state(const irq_ID_t ID,
			      struct irq_controller_state *state);

/*! Write to a control register of IRQ[ID]

 \param	ID[in]				IRQ identifier
 \param	reg[in]				register index
 \param value[in]			The data to be written

 \return none, IRQ[ID].ctrl[reg] = value
 */
STORAGE_CLASS_IRQ_H void irq_reg_store(
    const irq_ID_t		ID,
    const unsigned int	reg,
    const hrt_data		value);

/*! Read from a control register of IRQ[ID]

 \param	ID[in]				IRQ identifier
 \param	reg[in]				register index
 \param value[in]			The data to be written

 \return IRQ[ID].ctrl[reg]
 */
STORAGE_CLASS_IRQ_H hrt_data irq_reg_load(
    const irq_ID_t		ID,
    const unsigned int	reg);

/*! Enable an IRQ channel of IRQ[ID] with a mode

 \param	ID[in]				IRQ (device) identifier
 \param	irq[in]				IRQ (channel) identifier

 \return none, enable(IRQ[ID].channel[irq_ID])
 */
void irq_enable_channel(
    const irq_ID_t				ID,
    const unsigned int			irq_ID);

/*! Enable pulse interrupts for IRQ[ID] with a mode

 \param	ID[in]				IRQ (device) identifier
 \param	enable				enable/disable pulse interrupts

 \return none
 */
void irq_enable_pulse(
    const irq_ID_t	ID,
    bool			pulse);

/*! Disable an IRQ channel of IRQ[ID]

 \param	ID[in]				IRQ (device) identifier
 \param	irq[in]				IRQ (channel) identifier

 \return none, disable(IRQ[ID].channel[irq_ID])
 */
void irq_disable_channel(
    const irq_ID_t				ID,
    const unsigned int			irq);

/*! Clear the state of all IRQ channels of IRQ[ID]

 \param	ID[in]				IRQ (device) identifier

 \return none, clear(IRQ[ID].channel[])
 */
void irq_clear_all(
    const irq_ID_t				ID);

/*! Return the ID of a signalling IRQ channel of IRQ[ID]

 \param	ID[in]				IRQ (device) identifier
 \param irq_id[out]			active IRQ (channel) identifier

 \Note: This function operates as strtok(), based on the return
  state the user is informed if there are additional signalling
  channels

 \return state(IRQ[ID])
 */
enum hrt_isp_css_irq_status irq_get_channel_id(
    const irq_ID_t				ID,
    unsigned int				*irq_id);

/*! Raise an interrupt on channel irq_id of device IRQ[ID]

 \param	ID[in]				IRQ (device) identifier
 \param	irq_id[in]			IRQ (channel) identifier

 \return none, signal(IRQ[ID].channel[irq_id])
 */
void irq_raise(
    const irq_ID_t				ID,
    const irq_sw_channel_id_t	irq_id);

/*! Test if any IRQ channel of the virtual super IRQ has raised a signal

 \return any(VIRQ.channel[irq_ID] != 0)
 */
bool any_virq_signal(void);

/*! Enable an IRQ channel of the virtual super IRQ

 \param	irq[in]				IRQ (channel) identifier
 \param	en[in]				predicate channel enable

 \return none, VIRQ.channel[irq_ID].enable = en
 */
void cnd_virq_enable_channel(
    const enum virq_id				irq_ID,
    const bool					en);

/*! Clear the state of all IRQ channels of the virtual super IRQ

 \return none, clear(VIRQ.channel[])
 */
void virq_clear_all(void);

/*! Clear the IRQ info state of the virtual super IRQ

 \param irq_info[in/out]	The IRQ (channel) state

 \return none
 */
void virq_clear_info(struct virq_info *irq_info);

/*! Return the ID of a signalling IRQ channel of the virtual super IRQ

 \param irq_id[out]			active IRQ (channel) identifier

 \Note: This function operates as strtok(), based on the return
  state the user is informed if there are additional signalling
  channels

 \return state(IRQ[...])
 */
enum hrt_isp_css_irq_status virq_get_channel_id(
    enum virq_id					*irq_id);

/*! Return the IDs of all signaling IRQ channels of the virtual super IRQ

 \param irq_info[out]		all active IRQ (channel) identifiers

 \Note: Unlike "irq_get_channel_id()" this function returns all
  channel signaling info. The new info is OR'd with the current
  info state. N.B. this is the same as repeatedly calling the function
  "irq_get_channel_id()" in a (non-blocked) handler routine

 \return (error(state(IRQ[...]))
 */
enum hrt_isp_css_irq_status
virq_get_channel_signals(struct virq_info *irq_info);

#endif /* __IRQ_PUBLIC_H_INCLUDED__ */
