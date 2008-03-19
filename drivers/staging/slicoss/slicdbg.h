/**************************************************************************
 *
 * Copyright (c) 2000-2002 Alacritech, Inc.  All rights reserved.
 *
 * $Id: slicdbg.h,v 1.2 2006/03/27 15:10:04 mook Exp $
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY ALACRITECH, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL ALACRITECH, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * official policies, either expressed or implied, of Alacritech, Inc.
 *
 **************************************************************************/

/*
 * FILENAME: slicdbg.h
 *
 * All debug and assertion-based definitions and macros are included
 * in this file for the SLICOSS driver.
 */
#ifndef _SLIC_DEBUG_H_
#define _SLIC_DEBUG_H_

#ifdef SLIC_DEFAULT_LOG_LEVEL
#else
#define SLICLEVEL   KERN_DEBUG
#endif
#define SLIC_DISPLAY              printk
#define DBG_ERROR(n, args...)   SLIC_DISPLAY(KERN_EMERG n, ##args)

#define SLIC_DEBUG_MESSAGE 1
#if SLIC_DEBUG_MESSAGE
/*#define DBG_MSG(n, args...)      SLIC_DISPLAY(SLICLEVEL n, ##args)*/
#define DBG_MSG(n, args...)
#else
#define DBG_MSG(n, args...)
#endif

#ifdef ASSERT
#undef ASSERT
#endif

#if SLIC_ASSERT_ENABLED
#ifdef CONFIG_X86_64
#define VALID_ADDRESS(p)  (1)
#else
#define VALID_ADDRESS(p)  (((ulong32)(p) & 0x80000000) || ((ulong32)(p) == 0))
#endif
#ifndef ASSERT
#define ASSERT(a)                                                             \
    {                                                                         \
	if (!(a)) {                                                           \
		DBG_ERROR("ASSERT() Failure: file %s, function %s  line %d\n",\
		__FILE__, __func__, __LINE__);                          \
		slic_assert_fail();                                       \
	}                                                                 \
    }
#endif
#ifndef ASSERTMSG
#define ASSERTMSG(a,msg)                                                  \
    {                                                                     \
	if (!(a)) {                                                       \
		DBG_ERROR("ASSERT() Failure: file %s, function %s"\
			"line %d: %s\n",\
			__FILE__, __func__, __LINE__, (msg));            \
		slic_assert_fail();                                      \
	}                                                                \
    }
#endif
#else
#ifndef ASSERT
#define ASSERT(a)
#endif
#ifndef ASSERTMSG
#define ASSERTMSG(a, msg)
#endif
#endif /* SLIC_ASSERT_ENABLED  */

#endif  /*  _SLIC_DEBUG_H_  */
