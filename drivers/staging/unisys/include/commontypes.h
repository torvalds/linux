/* Copyright (C) 2010 - 2013 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

#ifndef _COMMONTYPES_H_
#define _COMMONTYPES_H_

/* define the following to prevent include nesting in kernel header files of
 * similar abbreviated content */
#define _SUPERVISOR_COMMONTYPES_H_

#include <linux/types.h>
#include <linux/version.h>
#include <linux/io.h>
#include <linux/uuid.h>

typedef u64 GUEST_PHYSICAL_ADDRESS;

#define MEMSET(ptr, val, len) memset(ptr, val, len)
#define MEMCMP_IO(m1, m2, len) memcmp((void __force *)m1, m2, len)

#define INLINE inline
#define OFFSETOF offsetof

#define MEMORYBARRIER mb()
#define MEMCPY(dest, src, len) memcpy(dest, src, len)
#define MEMCPY_TOIO(dest, src, len) memcpy_toio(dest, src, len)
#define MEMCPY_FROMIO(dest, src, len) memcpy_fromio(dest, src, len)

#define CHANNEL_GUID_MISMATCH(chType, chName, field, expected, actual, fil, \
			      lin, logCtx)				\
	do {								\
		pr_err("Channel mismatch on channel=%s(%pUL) field=%s expected=%pUL actual=%pUL @%s:%d\n", \
		       chName, &chType, field,	\
		       &expected, &actual, \
		       fil, lin);					\
	} while (0)
#define CHANNEL_U32_MISMATCH(chType, chName, field, expected, actual, fil, \
			     lin, logCtx)				\
	do {								\
		pr_err("Channel mismatch on channel=%s(%pUL) field=%s expected=0x%-8.8lx actual=0x%-8.8lx @%s:%d\n", \
		       chName, &chType, field,	\
		       (unsigned long)expected, (unsigned long)actual,	\
		       fil, lin);					\
	} while (0)

#define CHANNEL_U64_MISMATCH(chType, chName, field, expected, actual, fil, \
			     lin, logCtx)				\
	do {								\
		pr_err("Channel mismatch on channel=%s(%pUL) field=%s expected=0x%-8.8Lx actual=0x%-8.8Lx @%s:%d\n", \
		       chName, &chType, field,	\
		       (unsigned long long)expected,			\
		       (unsigned long long)actual,			\
		       fil, lin);					\
	} while (0)

#define UltraLogEvent(logCtx, EventId, Severity, SubsystemMask, pFunctionName, \
		      LineNumber, Str, args...)				\
	pr_info(Str, ## args)

#define VolatileBarrier() MEMORYBARRIER

#endif

