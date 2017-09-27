/*
 * Copyright (c) 2012 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _BRCM_LED_H_
#define _BRCM_LED_H_
struct brcms_led {
	char name[32];
	unsigned gpio;
	bool active_low;
};

#ifdef CONFIG_BCMA_DRIVER_GPIO
void brcms_led_unregister(struct brcms_info *wl);
int brcms_led_register(struct brcms_info *wl);
#else
static inline void brcms_led_unregister(struct brcms_info *wl) {};
static inline int brcms_led_register(struct brcms_info *wl)
{
	return -ENOTSUPP;
};
#endif

#endif /* _BRCM_LED_H_ */
