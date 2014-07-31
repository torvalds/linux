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

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/version.h>
#include <linux/io.h>
#include <linux/uuid.h>
#else
#include <stdint.h>
#include <syslog.h>
#endif

#define S16 int16_t
#define S32 int32_t
#define S64 int64_t

#ifdef __KERNEL__

#ifdef CONFIG_X86_32
#define UINTN u32
#else
#define UINTN u64
#endif

#else

#include <stdint.h>
#if __WORDSIZE == 32
#define UINTN u32
#elif __WORDSIZE == 64
#define UINTN u64
#else
#error Unsupported __WORDSIZE
#endif

#endif

typedef u64 GUEST_PHYSICAL_ADDRESS;

#define MEMSET(ptr, val, len) memset(ptr, val, len)
#define MEMCMP(m1, m2, len) memcmp(m1, m2, len)
#define MEMCMP_IO(m1, m2, len) memcmp((void __force *)m1, m2, len)
#define STRLEN(s) ((UINTN)strlen((const char *)s))
#define STRCPY(d, s) (strcpy((char *)d, (const char *)s))

#define INLINE inline
#define OFFSETOF offsetof

#ifdef __KERNEL__
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

#else
#define MEMCPY(dest, src, len) memcpy(dest, src, len)

#define MEMORYBARRIER mb()

#define CHANNEL_GUID_MISMATCH(chType, chName, field, expected, actual, fil, \
			      lin, logCtx)				\
	do {								\
		syslog(LOG_USER | LOG_ERR,				\
		       "Channel mismatch on channel=%s(%pUL) field=%s expected=%pUL actual=%pUL @%s:%d", \
		       chName, &chType, field,	\
		       &expected, &actual, \
		       fil, lin);					\
	} while (0)

#define CHANNEL_U32_MISMATCH(chType, chName, field, expected, actual, fil, \
			     lin, logCtx)				\
	do {								\
		syslog(LOG_USER | LOG_ERR,				\
		       "Channel mismatch on channel=%s(%pUL) field=%s expected=0x%-8.8lx actual=0x%-8.8lx @%s:%d", \
		       chName, chType, field,	\
		       (unsigned long)expected, (unsigned long)actual,	\
		       fil, lin);					\
	} while (0)

#define CHANNEL_U64_MISMATCH(chType, chName, field, expected, actual, fil, \
			     lin, logCtx)				\
	do {								\
		syslog(LOG_USER | LOG_ERR,				\
		       "Channel mismatch on channel=%s(%pUL) field=%s expected=0x%-8.8Lx actual=0x%-8.8Lx @%s:%d", \
		       chName, chType, field,	\
		       (unsigned long long)expected,			\
		       (unsigned long long)actual,			\
		       fil, lin);					\
	} while (0)

#define UltraLogEvent(logCtx, EventId, Severity, SubsystemMask, pFunctionName, \
		      LineNumber, Str, args...)				\
	syslog(LOG_USER | LOG_INFO, Str, ## args)
#endif

#define VolatileBarrier() MEMORYBARRIER

#endif

