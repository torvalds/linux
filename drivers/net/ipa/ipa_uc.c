// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2018-2022 Linaro Ltd.
 */

#include <linux/types.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>

#include "ipa.h"
#include "ipa_uc.h"
#include "ipa_power.h"

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
		dev_err(dev, "unsupported microcontroller event %u\n",
			shared->event);
	/* The LOG_INFO event can be safely ignored */
}

/* Microcontroller response IPA interrupt handler */
static void ipa_uc_response_hdlr(struct ipa *ipa, enum ipa_irq_id irq_id)
{
	struct ipa_uc_mem_area *shared = ipa_uc_shared(ipa);
	struct device *dev = &ipa->pdev->dev;

	/* An INIT_COMPLETED response message is sent to the AP by the
	 * microcontroller when it is operational.  Other than this, the AP
	 * should only receive responses from the microcontroller when it has
	 * sent it a request message.
	 *
	 * We can drop the power reference taken in ipa_uc_power() once we
	 * know the microcontroller has finished its initialization.
	 */
	switch (shared->response) {
	case IPA_UC_RESPONSE_INIT_COMPLETED:
		if (ipa->uc_powered) {
			ipa->uc_loaded = true;
			ipa_power_retention(ipa, true);
			pm_runtime_mark_last_busy(dev);
			(void)pm_runtime_put_autosuspend(dev);
			ipa->uc_powered = false;
		} else {
			dev_warn(dev, "unexpected init_completed response\n");
		}
		break;
	default:
		dev_warn(dev, "unsupported microcontroller response %u\n",
			 shared->response);
		break;
	}
}

/* Configure the IPA microcontroller subsystem */
void ipa_uc_config(struct ipa *ipa)
{
	ipa->uc_powered = false;
	ipa->uc_loaded = false;
	ipa_interrupt_add(ipa->interrupt, IPA_IRQ_UC_0, ipa_uc_event_handler);
	ipa_interrupt_add(ipa->interrupt, IPA_IRQ_UC_1, ipa_uc_response_hdlr);
}

/* Inverse of ipa_uc_config() */
void ipa_uc_deconfig(struct ipa *ipa)
{
	struct device *dev = &ipa->pdev->dev;

	ipa_interrupt_remove(ipa->interrupt, IPA_IRQ_UC_1);
	ipa_interrupt_remove(ipa->interrupt, IPA_IRQ_UC_0);
	if (ipa->uc_loaded)
		ipa_power_retention(ipa, false);

	if (!ipa->uc_powered)
		return;

	pm_runtime_mark_last_busy(dev);
	(void)pm_runtime_put_autosuspend(dev);
}

/* Take a proxy power reference for the microcontroller */
void ipa_uc_power(struct ipa *ipa)
{
	static bool already;
	struct device *dev;
	int ret;

	if (already)
		return;
	already = true;		/* Only do this on first boot */

	/* This power reference dropped in ipa_uc_response_hdlr() above */
	dev = &ipa->pdev->dev;
	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		pm_runtime_put_noidle(dev);
		dev_err(dev, "error %d getting proxy power\n", ret);
	} else {
		ipa->uc_powered = true;
	}
}

/* Send a command to the microcontroller */
static void send_uc_command(struct ipa *ipa, u32 command, u32 command_param)
{
	struct ipa_uc_mem_area *shared = ipa_uc_shared(ipa);
	const struct ipa_reg *reg;
	u32 val;

	/* Fill in the command data */
	shared->command = command;
	shared->command_param = cpu_to_le32(command_param);
	shared->command_param_hi = 0;
	shared->response = 0;
	shared->response_param = 0;

	/* Use an interrupt to tell the microcontroller the command is ready */
	reg = ipa_reg(ipa, IPA_IRQ_UC);
	val = ipa_reg_bit(reg, UC_INTR);

	iowrite32(val, ipa->reg_virt + ipa_reg_offset(reg));
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
