/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#ifndef __DAL_IRQ_TYPES_H__
#define __DAL_IRQ_TYPES_H__

#include "os_types.h"

struct dc_context;

typedef void (*interrupt_handler)(void *);

typedef void *irq_handler_idx;
#define DAL_INVALID_IRQ_HANDLER_IDX NULL

/* The order of the IRQ sources is important and MUST match the one's
of base driver */
enum dc_irq_source {
	/* Use as mask to specify invalid irq source */
	DC_IRQ_SOURCE_INVALID = 0,

	DC_IRQ_SOURCE_HPD1,
	DC_IRQ_SOURCE_HPD2,
	DC_IRQ_SOURCE_HPD3,
	DC_IRQ_SOURCE_HPD4,
	DC_IRQ_SOURCE_HPD5,
	DC_IRQ_SOURCE_HPD6,

	DC_IRQ_SOURCE_HPD1RX,
	DC_IRQ_SOURCE_HPD2RX,
	DC_IRQ_SOURCE_HPD3RX,
	DC_IRQ_SOURCE_HPD4RX,
	DC_IRQ_SOURCE_HPD5RX,
	DC_IRQ_SOURCE_HPD6RX,

	DC_IRQ_SOURCE_I2C_DDC1,
	DC_IRQ_SOURCE_I2C_DDC2,
	DC_IRQ_SOURCE_I2C_DDC3,
	DC_IRQ_SOURCE_I2C_DDC4,
	DC_IRQ_SOURCE_I2C_DDC5,
	DC_IRQ_SOURCE_I2C_DDC6,

	DC_IRQ_SOURCE_DPSINK1,
	DC_IRQ_SOURCE_DPSINK2,
	DC_IRQ_SOURCE_DPSINK3,
	DC_IRQ_SOURCE_DPSINK4,
	DC_IRQ_SOURCE_DPSINK5,
	DC_IRQ_SOURCE_DPSINK6,

	DC_IRQ_SOURCE_TIMER,

	DC_IRQ_SOURCE_PFLIP_FIRST,
	DC_IRQ_SOURCE_PFLIP1 = DC_IRQ_SOURCE_PFLIP_FIRST,
	DC_IRQ_SOURCE_PFLIP2,
	DC_IRQ_SOURCE_PFLIP3,
	DC_IRQ_SOURCE_PFLIP4,
	DC_IRQ_SOURCE_PFLIP5,
	DC_IRQ_SOURCE_PFLIP6,
	DC_IRQ_SOURCE_PFLIP_UNDERLAY0,
	DC_IRQ_SOURCE_PFLIP_LAST = DC_IRQ_SOURCE_PFLIP_UNDERLAY0,

	DC_IRQ_SOURCE_GPIOPAD0,
	DC_IRQ_SOURCE_GPIOPAD1,
	DC_IRQ_SOURCE_GPIOPAD2,
	DC_IRQ_SOURCE_GPIOPAD3,
	DC_IRQ_SOURCE_GPIOPAD4,
	DC_IRQ_SOURCE_GPIOPAD5,
	DC_IRQ_SOURCE_GPIOPAD6,
	DC_IRQ_SOURCE_GPIOPAD7,
	DC_IRQ_SOURCE_GPIOPAD8,
	DC_IRQ_SOURCE_GPIOPAD9,
	DC_IRQ_SOURCE_GPIOPAD10,
	DC_IRQ_SOURCE_GPIOPAD11,
	DC_IRQ_SOURCE_GPIOPAD12,
	DC_IRQ_SOURCE_GPIOPAD13,
	DC_IRQ_SOURCE_GPIOPAD14,
	DC_IRQ_SOURCE_GPIOPAD15,
	DC_IRQ_SOURCE_GPIOPAD16,
	DC_IRQ_SOURCE_GPIOPAD17,
	DC_IRQ_SOURCE_GPIOPAD18,
	DC_IRQ_SOURCE_GPIOPAD19,
	DC_IRQ_SOURCE_GPIOPAD20,
	DC_IRQ_SOURCE_GPIOPAD21,
	DC_IRQ_SOURCE_GPIOPAD22,
	DC_IRQ_SOURCE_GPIOPAD23,
	DC_IRQ_SOURCE_GPIOPAD24,
	DC_IRQ_SOURCE_GPIOPAD25,
	DC_IRQ_SOURCE_GPIOPAD26,
	DC_IRQ_SOURCE_GPIOPAD27,
	DC_IRQ_SOURCE_GPIOPAD28,
	DC_IRQ_SOURCE_GPIOPAD29,
	DC_IRQ_SOURCE_GPIOPAD30,

	DC_IRQ_SOURCE_DC1UNDERFLOW,
	DC_IRQ_SOURCE_DC2UNDERFLOW,
	DC_IRQ_SOURCE_DC3UNDERFLOW,
	DC_IRQ_SOURCE_DC4UNDERFLOW,
	DC_IRQ_SOURCE_DC5UNDERFLOW,
	DC_IRQ_SOURCE_DC6UNDERFLOW,

	DC_IRQ_SOURCE_DMCU_SCP,
	DC_IRQ_SOURCE_VBIOS_SW,

	DC_IRQ_SOURCE_VUPDATE1,
	DC_IRQ_SOURCE_VUPDATE2,
	DC_IRQ_SOURCE_VUPDATE3,
	DC_IRQ_SOURCE_VUPDATE4,
	DC_IRQ_SOURCE_VUPDATE5,
	DC_IRQ_SOURCE_VUPDATE6,

	DC_IRQ_SOURCE_VBLANK1,
	DC_IRQ_SOURCE_VBLANK2,
	DC_IRQ_SOURCE_VBLANK3,
	DC_IRQ_SOURCE_VBLANK4,
	DC_IRQ_SOURCE_VBLANK5,
	DC_IRQ_SOURCE_VBLANK6,

	DC_IRQ_SOURCE_DC1_VLINE0,
	DC_IRQ_SOURCE_DC2_VLINE0,
	DC_IRQ_SOURCE_DC3_VLINE0,
	DC_IRQ_SOURCE_DC4_VLINE0,
	DC_IRQ_SOURCE_DC5_VLINE0,
	DC_IRQ_SOURCE_DC6_VLINE0,

	DAL_IRQ_SOURCES_NUMBER
};

enum irq_type
{
	IRQ_TYPE_PFLIP = DC_IRQ_SOURCE_PFLIP1,
	IRQ_TYPE_VUPDATE = DC_IRQ_SOURCE_VUPDATE1,
	IRQ_TYPE_VBLANK = DC_IRQ_SOURCE_VBLANK1,
};

#define DAL_VALID_IRQ_SRC_NUM(src) \
	((src) <= DAL_IRQ_SOURCES_NUMBER && (src) > DC_IRQ_SOURCE_INVALID)

/* Number of Page Flip IRQ Sources. */
#define DAL_PFLIP_IRQ_SRC_NUM \
	(DC_IRQ_SOURCE_PFLIP_LAST - DC_IRQ_SOURCE_PFLIP_FIRST + 1)

/* the number of contexts may be expanded in the future based on needs */
enum dc_interrupt_context {
	INTERRUPT_LOW_IRQ_CONTEXT = 0,
	INTERRUPT_HIGH_IRQ_CONTEXT,
	INTERRUPT_CONTEXT_NUMBER
};

enum dc_interrupt_porlarity {
	INTERRUPT_POLARITY_DEFAULT = 0,
	INTERRUPT_POLARITY_LOW = INTERRUPT_POLARITY_DEFAULT,
	INTERRUPT_POLARITY_HIGH,
	INTERRUPT_POLARITY_BOTH
};

#define DC_DECODE_INTERRUPT_POLARITY(int_polarity) \
	(int_polarity == INTERRUPT_POLARITY_LOW) ? "Low" : \
	(int_polarity == INTERRUPT_POLARITY_HIGH) ? "High" : \
	(int_polarity == INTERRUPT_POLARITY_BOTH) ? "Both" : "Invalid"

struct dc_timer_interrupt_params {
	uint32_t micro_sec_interval;
	enum dc_interrupt_context int_context;
};

struct dc_interrupt_params {
	/* The polarity *change* which will trigger an interrupt.
	 * If 'requested_polarity == INTERRUPT_POLARITY_BOTH', then
	 * 'current_polarity' must be initialised. */
	enum dc_interrupt_porlarity requested_polarity;
	/* If 'requested_polarity == INTERRUPT_POLARITY_BOTH',
	 * 'current_polarity' should contain the current state, which means
	 * the interrupt will be triggered when state changes from what is,
	 * in 'current_polarity'. */
	enum dc_interrupt_porlarity current_polarity;
	enum dc_irq_source irq_source;
	enum dc_interrupt_context int_context;
};

#endif
