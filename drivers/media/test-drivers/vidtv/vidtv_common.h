/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The Virtual DVB test driver serves as a reference DVB driver and helps
 * validate the existing APIs in the media subsystem. It can also aid
 * developers working on userspace applications.
 *
 * Copyright (C) 2020 Daniel W. S. Almeida
 */

#ifndef VIDTV_COMMON_H
#define VIDTV_COMMON_H

#include <linux/types.h>

#define CLOCK_UNIT_90KHZ 90000
#define CLOCK_UNIT_27MHZ 27000000
#define VIDTV_SLEEP_USECS 10000
#define VIDTV_MAX_SLEEP_USECS (2 * VIDTV_SLEEP_USECS)
#define VIDTV_DEFAULT_TS_ID 0x744

u32 vidtv_memcpy(void *to,
		 size_t to_offset,
		 size_t to_size,
		 const void *from,
		 size_t len);

u32 vidtv_memset(void *to,
		 size_t to_offset,
		 size_t to_size,
		 int c,
		 size_t len);

#endif // VIDTV_COMMON_H
