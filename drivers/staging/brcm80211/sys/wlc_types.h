/*
 * Copyright (c) 2010 Broadcom Corporation
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

#ifndef _wlc_types_h_
#define _wlc_types_h_

/* forward declarations */

struct wlc_info;
typedef struct wlc_hw_info wlc_hw_info_t;
typedef struct wlc_if wlc_if_t;
typedef struct wl_if wl_if_t;
typedef struct led_info led_info_t;
typedef struct bmac_led bmac_led_t;
typedef struct bmac_led_info bmac_led_info_t;
typedef struct scb_module scb_module_t;
typedef struct ba_info ba_info_t;
typedef struct ampdu_info ampdu_info_t;
typedef struct ratesel_info ratesel_info_t;
typedef struct wlc_ap_info wlc_ap_info_t;
typedef struct wlc_auth_info wlc_auth_info_t;
typedef struct supplicant supplicant_t;
typedef struct authenticator authenticator_t;
typedef struct antsel_info antsel_info_t;
typedef struct bmac_pmq bmac_pmq_t;

struct d11init;

#ifndef _hnddma_pub_
#define _hnddma_pub_
typedef const struct hnddma_pub hnddma_t;
#endif				/* _hnddma_pub_ */

#endif				/* _wlc_types_h_ */
