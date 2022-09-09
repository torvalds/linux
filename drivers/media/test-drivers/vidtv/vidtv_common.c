// SPDX-License-Identifier: GPL-2.0
/*
 * The Virtual DVB test driver serves as a reference DVB driver and helps
 * validate the existing APIs in the media subsystem. It can also aid
 * developers working on userspace applications.
 *
 * Copyright (C) 2020 Daniel W. S. Almeida
 */
#define pr_fmt(fmt) KBUILD_MODNAME ":%s, %d: " fmt, __func__, __LINE__

#include <linux/printk.h>
#include <linux/ratelimit.h>
#include <linux/string.h>
#include <linux/types.h>

#include "vidtv_common.h"

/**
 * vidtv_memcpy() - wrapper routine to be used by MPEG-TS
 *	generator, in order to avoid going past the
 *	output buffer.
 * @to:	Starting element to where a MPEG-TS packet will
 *	be copied.
 * @to_offset:	Starting position of the @to buffer to be filled.
 * @to_size:	Size of the @to buffer.
 * @from:	Starting element of the buffer to be copied.
 * @len:	Number of elements to be copy from @from buffer
 *	into @to+ @to_offset buffer.
 *
 * Note:
 *	Real digital TV demod drivers should not have memcpy
 *	wrappers. We use it here because emulating MPEG-TS
 *	generation at kernelspace requires some extra care.
 *
 * Return:
 *	Returns the number of bytes written
 */
u32 vidtv_memcpy(void *to,
		 size_t to_offset,
		 size_t to_size,
		 const void *from,
		 size_t len)
{
	if (unlikely(to_offset + len > to_size)) {
		pr_err_ratelimited("overflow detected, skipping. Try increasing the buffer size. Needed %zu, had %zu\n",
				   to_offset + len,
				   to_size);
		return 0;
	}

	memcpy(to + to_offset, from, len);
	return len;
}

/**
 * vidtv_memset() - wrapper routine to be used by MPEG-TS
 *	generator, in order to avoid going past the
 *	output buffer.
 * @to:	Starting element to set
 * @to_offset:	Starting position of the @to buffer to be filled.
 * @to_size:	Size of the @to buffer.
 * @c:		The value to set the memory to.
 * @len:	Number of elements to be copy from @from buffer
 *	into @to+ @to_offset buffer.
 *
 * Note:
 *	Real digital TV demod drivers should not have memset
 *	wrappers. We use it here because emulating MPEG-TS
 *	generation at kernelspace requires some extra care.
 *
 * Return:
 *	Returns the number of bytes written
 */
u32 vidtv_memset(void *to,
		 size_t to_offset,
		 size_t to_size,
		 const int c,
		 size_t len)
{
	if (unlikely(to_offset + len > to_size)) {
		pr_err_ratelimited("overflow detected, skipping. Try increasing the buffer size. Needed %zu, had %zu\n",
				   to_offset + len,
				   to_size);
		return 0;
	}

	memset(to + to_offset, c, len);
	return len;
}
