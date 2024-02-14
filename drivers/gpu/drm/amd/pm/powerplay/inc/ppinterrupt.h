/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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
 */

#ifndef _PP_INTERRUPT_H_
#define _PP_INTERRUPT_H_

enum amd_thermal_irq {
	AMD_THERMAL_IRQ_LOW_TO_HIGH = 0,
	AMD_THERMAL_IRQ_HIGH_TO_LOW,

	AMD_THERMAL_IRQ_LAST
};

/* The type of the interrupt callback functions in PowerPlay */
typedef int (*irq_handler_func_t)(void *private_data,
				unsigned src_id, const uint32_t *iv_entry);

/* Event Manager action chain list information */
struct pp_interrupt_registration_info {
	irq_handler_func_t call_back; /* Pointer to callback function */
	void *context;                   /* Pointer to callback function context */
	uint32_t src_id;               /* Registered interrupt id */
	const uint32_t *iv_entry;
};

#endif /* _PP_INTERRUPT_H_ */
