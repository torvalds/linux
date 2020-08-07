/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2018-2020 Linaro Ltd.
 */
#ifndef _IPA_INTERRUPT_H_
#define _IPA_INTERRUPT_H_

#include <linux/types.h>
#include <linux/bits.h>

struct ipa;
struct ipa_interrupt;

/**
 * enum ipa_irq_id - IPA interrupt type
 * @IPA_IRQ_UC_0:	Microcontroller event interrupt
 * @IPA_IRQ_UC_1:	Microcontroller response interrupt
 * @IPA_IRQ_TX_SUSPEND:	Data ready interrupt
 *
 * The data ready interrupt is signaled if data has arrived that is destined
 * for an AP RX endpoint whose underlying GSI channel is suspended/stopped.
 */
enum ipa_irq_id {
	IPA_IRQ_UC_0		= 2,
	IPA_IRQ_UC_1		= 3,
	IPA_IRQ_TX_SUSPEND	= 14,
	IPA_IRQ_COUNT,		/* Number of interrupt types (not an index) */
};

/**
 * typedef ipa_irq_handler_t - IPA interrupt handler function type
 * @ipa:	IPA pointer
 * @irq_id:	interrupt type
 *
 * Callback function registered by ipa_interrupt_add() to handle a specific
 * IPA interrupt type
 */
typedef void (*ipa_irq_handler_t)(struct ipa *ipa, enum ipa_irq_id irq_id);

/**
 * ipa_interrupt_add() - Register a handler for an IPA interrupt type
 * @irq_id:	IPA interrupt type
 * @handler:	Handler function for the interrupt
 *
 * Add a handler for an IPA interrupt and enable it.  IPA interrupt
 * handlers are run in threaded interrupt context, so are allowed to
 * block.
 */
void ipa_interrupt_add(struct ipa_interrupt *interrupt, enum ipa_irq_id irq_id,
		       ipa_irq_handler_t handler);

/**
 * ipa_interrupt_remove() - Remove the handler for an IPA interrupt type
 * @interrupt:	IPA interrupt structure
 * @irq_id:	IPA interrupt type
 *
 * Remove an IPA interrupt handler and disable it.
 */
void ipa_interrupt_remove(struct ipa_interrupt *interrupt,
			  enum ipa_irq_id irq_id);

/**
 * ipa_interrupt_suspend_enable - Enable TX_SUSPEND for an endpoint
 * @interrupt:		IPA interrupt structure
 * @endpoint_id:	Endpoint whose interrupt should be enabled
 *
 * Note:  The "TX" in the name is from the perspective of the IPA hardware.
 * A TX_SUSPEND interrupt arrives on an AP RX enpoint when packet data can't
 * be delivered to the endpoint because it is suspended (or its underlying
 * channel is stopped).
 */
void ipa_interrupt_suspend_enable(struct ipa_interrupt *interrupt,
				  u32 endpoint_id);

/**
 * ipa_interrupt_suspend_disable - Disable TX_SUSPEND for an endpoint
 * @interrupt:		IPA interrupt structure
 * @endpoint_id:	Endpoint whose interrupt should be disabled
 */
void ipa_interrupt_suspend_disable(struct ipa_interrupt *interrupt,
				   u32 endpoint_id);

/**
 * ipa_interrupt_suspend_clear_all - clear all suspend interrupts
 * @interrupt:	IPA interrupt structure
 *
 * Clear the TX_SUSPEND interrupt for all endpoints that signaled it.
 */
void ipa_interrupt_suspend_clear_all(struct ipa_interrupt *interrupt);

/**
 * ipa_interrupt_simulate_suspend() - Simulate TX_SUSPEND IPA interrupt
 * @interrupt:	IPA interrupt structure
 *
 * This calls the TX_SUSPEND interrupt handler, as if such an interrupt
 * had been signaled.  This is needed to work around a hardware quirk
 * that occurs if aggregation is active on an endpoint when its underlying
 * channel is suspended.
 */
void ipa_interrupt_simulate_suspend(struct ipa_interrupt *interrupt);

/**
 * ipa_interrupt_setup() - Set up the IPA interrupt framework
 * @ipa:	IPA pointer
 *
 * Return:	Pointer to IPA SMP2P info, or a pointer-coded error
 */
struct ipa_interrupt *ipa_interrupt_setup(struct ipa *ipa);

/**
 * ipa_interrupt_teardown() - Tear down the IPA interrupt framework
 * @interrupt:	IPA interrupt structure
 */
void ipa_interrupt_teardown(struct ipa_interrupt *interrupt);

#endif /* _IPA_INTERRUPT_H_ */
