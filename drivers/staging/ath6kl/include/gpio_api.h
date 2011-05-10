//------------------------------------------------------------------------------
// <copyright file="gpio_api.h" company="Atheros">
//    Copyright (c) 2004-2010 Atheros Corporation.  All rights reserved.
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//------------------------------------------------------------------------------
//==============================================================================
// Host-side General Purpose I/O API.
//
// Author(s): ="Atheros"
//==============================================================================
#ifndef _GPIO_API_H_
#define _GPIO_API_H_

/*
 * Send a command to the Target in order to change output on GPIO pins.
 */
int wmi_gpio_output_set(struct wmi_t *wmip,
                             u32 set_mask,
                             u32 clear_mask,
                             u32 enable_mask,
                             u32 disable_mask);

/*
 * Send a command to the Target requesting input state of GPIO pins.
 */
int wmi_gpio_input_get(struct wmi_t *wmip);

/*
 * Send a command to the Target to change the value of a GPIO register.
 */
int wmi_gpio_register_set(struct wmi_t *wmip,
                               u32 gpioreg_id,
                               u32 value);

/*
 * Send a command to the Target to fetch the value of a GPIO register.
 */
int wmi_gpio_register_get(struct wmi_t *wmip, u32 gpioreg_id);

/*
 * Send a command to the Target, acknowledging some GPIO interrupts.
 */
int wmi_gpio_intr_ack(struct wmi_t *wmip, u32 ack_mask);

#endif /* _GPIO_API_H_ */
