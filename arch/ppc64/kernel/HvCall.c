/*
 * HvCall.c
 * Copyright (C) 2001  Mike Corrigan IBM Corporation
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <asm/page.h>
#include <asm/abs_addr.h>
#include <asm/iSeries/HvCall.h>
#include <asm/iSeries/HvCallSc.h>
#include <asm/iSeries/HvTypes.h>


void HvCall_writeLogBuffer(const void *buffer, u64 len)
{
	struct HvLpBufferList hv_buf;
	u64 left_this_page;
	u64 cur = virt_to_abs(buffer);

	while (len) {
		hv_buf.addr = cur;
		left_this_page = ((cur & PAGE_MASK) + PAGE_SIZE) - cur;
		if (left_this_page > len)
			left_this_page = len;
		hv_buf.len = left_this_page;
		len -= left_this_page;
		HvCall2(HvCallBaseWriteLogBuffer,
				virt_to_abs(&hv_buf),
				left_this_page);
		cur = (cur & PAGE_MASK) + PAGE_SIZE;
	}
}
