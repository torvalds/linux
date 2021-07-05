// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2018-2020 Linaro Ltd.
 */

#include <linux/types.h>
#include <linux/io.h>
#include <linux/delay.h>

#include "ipa.h"
#include "ipa_clock.h"
#include "ipa_uc.h"

/**
 * DOC:  The IPA embedded microcontroller
 *
 * The IPA incorporates a microcontroller that is able to do some additional
 * handling/offloading of network activity.  The current code makes
 * essentially no use of the microcontroller, but it still requires some
 * initialization.  It needs to be notified in the event the AP crashes.
 *
 * The microcontroller can generate two interrupts to the AP.  One interrupt
 * is used to indicate that a response to a request from the AP is available.
 * The other is used to notify the AP of the occurrence of an event.  In
 * addition, the AP can interrupt the microcontroller by writing a register.
 *
 * A 128 byte block of structured memory within the IPA SRAM is used together
 * with these interrupts to implement the communication interface between the
 * AP and the IPA microcontroller.  Each side writes data to the shared area
 * before interrupting its peer, which will read the written data in response
 * to the interrupt.  Some information found in the shared area is currently
 * unused.  All remaining space in the shared area is reserved, and must not
 * be read or written by the AP.
 */
/* Supports hardware interface version 0x2000 */

/* Delay to allow a the microcontroller to save state when crashing */
#define IPA_SEND_DELAY		100	/* microseconds */

/**
 * struct ipa_uc_mem_area - AP/microcontroller shared memory area
 * @command:		command code (AP->microcontroller)
 * @reserved0:		reserved bytes; avoid reading or writing
 * @command_param:	low 32 bits of command parameter (AP->microcontroller)
 * @command_param_hi:	high 32 bits of command parameter (AP->microcontroller)
 *
 * @response:		response code (microcontroller->AP)
 * @reserved1:		reserved bytes; avoid reading or writing
 * @response_param:	response parameter (microcontroller->AP)
 *
 * @event:		event code (microcontroller->AP)
 * @reserved2:		reserved bytes; avoid reading or writing
 * @event_param:	event parameter (microcontroller->AP)
 *
 * @first_error_address: address of first error-source on SNOC
 * @hw_state:		state of hardware (including error type information)
 * @warning_counter:	counter of non-fatal hardware errors
 * @reserved3:		reserved bytes; avoid reading or writing
 * @interface_version:	hardware-reported interface version
 * @reserved4:		reserved bytes; avoid reading or writing
 *
 * A shared memory area at the base of IPA resident memory is used for
 * communication with the microcontroller.  The region is 128 bytes in
 * size, but only the first 40 bytes (structured this way) are used.
 */
struct ipa_uc_mem_area {
	u8 command;		/* enum ipa_uc_command */
	u8 reserved0[3];
	__le32 command_param;
	__le32 command_param_hi;
	u8 response;		/* enum ipa_uc_response */
	u8 reserved1[3];
	__le32 response_param;
	u8 event;		/* enum ipa_uc_event */
	u8 reserved2[3];

	__le32 event_param;
	__le32 first_error_address;
	u8 hw_state;
	u8 warning_counter;
	__le16 reserved3;
	__le16 interface_version;
	__le16 reserved4;
};

/** enum ipa_uc_command - commands from the AP to the microcontroller */
enum ipa_uc_command {
	IPA_UC_COMMAND_NO_OP		= 0x0,
	IPA_UC_COMMAND_UPDATE_FLAGS	= 0x1,
	IPA_UC_COMMAND_DEBUG_RUN_TEST	= 0x2,
	IPA_UC_COMMAND_DEBUG_GET_INFO	= 0x3,
	IPA_UC_COMMAND_ERR_FATAL	= 0x4,
	IPA_UC_COMMAND_CLK_GATE		= 0x5,
	IPA_UC_COMMAND_CLK_UNGATE	= 0x6,
	IPA_UC_COMMAND_MEMCPY		= 0x7,
	IPA_UC_COMMAND_RESET_PIPE	= 0x8,
	IPA_UC_COMMAND_REG_WRITE	= 0x9,
	IPA_UC_COMMAND_GSI_CH_EMPTY	= 0xa,
};

/** enum ipa_uc_response - microcontroller response codes */
enum ipa_uc_response {
	IPA_UC_RESPONSE_NO_OP		= 0x0,
	IPA_UC_RESPONSE_INIT_COMPLETED	= 0x1,
	IPA_UC_RESPONSE_CMD_COMPLETED	= 0x2,
	IPA_UC_RESPONSE_DEBUG_GET_INFO	= 0x3,
};

/** enum ipa_uc_event - common cpu events reported by the microcontroller */
enum ipa_uc_event {
	IPA_UC_EVENT_NO_OP		= 0x0,
	IPA_UC_EVENT_ERROR		= 0x1,
	IPA_UC_EVENT_LOG_INFO		= 0x2,
};

static struct ipa_uc_mem_area *ipa_uc_shared(struct ipa *ipa)
{
	const struct ipa_mem *mem = ipa_mem_find(ipa, IPA_MEM_UC_SHARED);
	u32 offset = ipa->mem_offset + mem->offset;

	return ipa->mem_virt + offset;
}

/* Microcontroller event IPA interrupt handler */
static void ipa_uc_event_handler(struct ipa *ipa, enum ipa_irq_id irq_id)
{
	struct ipa_uc_mem_area *shared = ipa_uc_shared(ipa);
	struct device *dev = &ipa->pdev->dev;

	if (shared->event == IPA_UC_EVENT_ERROR)
		dev_err(dev, "microcontroller error event\n");
	else if (shared->event != IPA_UC_EVENT_LOG_INFO)
		dev_err(dev, "unsupported microcontroller event %hhu\n",
			shared->event);
	/* The LOG_INFO event can be safely ignored */
}

/* Microcontroller response IPA interrupt handler */
static void ipa_uc_response_hdlr(struct ipa *ipa, enum ipa_irq_id irq_id)
{
	struct ipa_uc_mem_area *shared = ipa_uc_shared(ipa);

	/* An INIT_COMPLETED response message is sent to the AP by the
	 * microcontroller when it is operational.  Other than this, the AP
	 * should only receive responses from the microcontroller when it has
	 * sent it a request message.
	 *
	 * We can drop the clock reference taken in ipa_uc_setup() once we
	 * know the microcontroller has finished its initialization.
	 */
	switch (shared->response) {
	case IPA_UC_RESPONSE_INIT_COMPLETED:
		ipa->uc_loaded = true;
		ipa_clock_put(ipa);
		break;
	default:
		dev_warn(&ipa->pdev->dev,
			 "unsupported microcontroller response %hhu\n",
			 shared->response);
		break;
	}
}

/* ipa_uc_setup() - Set up the microcontroller */
void ipa_uc_setup(struct ipa *ipa)
{
	/* The microcontroller needs the IPA clock running until it has
	 * completed its initialization.  It signals this by sending an
	 * INIT_COMPLETED response message to the AP.  This could occur after
	 * we have finished doing the rest of the IPA initialization, so we
	 * need to take an extra "proxy" reference, and hold it until we've
	 * received that signal.  (This reference is dropped in
	 * ipa_uc_response_hdlr(), above.)
	 */
	ipa_clock_get(ipa);

	ipa->uc_loaded = false;
	ipa_interrupt_add(ipa->interrupt, IPA_IRQ_UC_0, ipa_uc_event_handler);
	ipa_interrupt_add(ipa->interrupt, IPA_IRQ_UC_1, ipa_uc_response_hdlr);
}

/* Inverse of ipa_uc_setup() */
void ipa_uc_teardown(struct ipa *ipa)
{
	ipa_interrupt_remove(ipa->interrupt, IPA_IRQ_UC_1);
	ipa_interrupt_remove(ipa->interrupt, IPA_IRQ_UC_0);
	if (!ipa->uc_loaded)
		ipa_clock_put(ipa);
}

/* Send a command to the microcontroller */
static void send_uc_command(struct ipa *ipa, u32 command, u32 command_param)
{
	struct ipa_uc_mem_area *shared = ipa_uc_shared(ipa);
	u32 offset;
	u32 val;

	/* Fill in the command data */
	shared->command = command;
	shared->command_param = cpu_to_le32(command_param);
	shared->command_param_hi = 0;
	shared->response = 0;
	shared->response_param = 0;

	/* Use an interrupt to tell the microcontroller the command is ready */
	val = u32_encode_bits(1, UC_INTR_FMASK);
	offset = ipa_reg_irq_uc_offset(ipa->version);
	iowrite32(val, ipa->reg_virt + offset);
}

/* Tell the microcontroller the AP is shutting down */
void ipa_uc_panic_notifier(struct ipa *ipa)
{
	if (!ipa->uc_loaded)
		return;

	send_uc_command(ipa, IPA_UC_COMMAND_ERR_FATAL, 0);

	/* give uc enough time to save state */
	udelay(IPA_SEND_DELAY);
}
