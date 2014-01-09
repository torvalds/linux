/*******************************************************************************
 * Agere Systems Inc.
 * Wireless device driver for Linux (wlags49).
 *
 * Copyright (c) 1998-2003 Agere Systems Inc.
 * All rights reserved.
 *   http://www.agere.com
 *
 * Initially developed by TriplePoint, Inc.
 *   http://www.triplepoint.com
 *
 *------------------------------------------------------------------------------
 *
 *   This file contains definitions and macros for debugging.
 *
 *------------------------------------------------------------------------------
 *
 * SOFTWARE LICENSE
 *
 * This software is provided subject to the following terms and conditions,
 * which you should read carefully before using the software.  Using this
 * software indicates your acceptance of these terms and conditions.  If you do
 * not agree with these terms and conditions, do not use the software.
 *
 * Copyright (c) 2003 Agere Systems Inc.
 * All rights reserved.
 *
 * Redistribution and use in source or binary forms, with or without
 * modifications, are permitted provided that the following conditions are met:
 *
 * . Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following Disclaimer as comments in the code as
 *    well as in the documentation and/or other materials provided with the
 *    distribution.
 *
 * . Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following Disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * . Neither the name of Agere Systems Inc. nor the names of the contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * Disclaimer
 *
 * THIS SOFTWARE IS PROVIDED AS IS AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, INFRINGEMENT AND THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  ANY
 * USE, MODIFICATION OR DISTRIBUTION OF THIS SOFTWARE IS SOLELY AT THE USERS OWN
 * RISK. IN NO EVENT SHALL AGERE SYSTEMS INC. OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, INCLUDING, BUT NOT LIMITED TO, CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 ******************************************************************************/

#ifndef _DEBUG_H
#define _DEBUG_H




/* Turn on debugging here if not done with a preprocessor define */
#ifndef DBG
#define DBG 0
#else
#undef	DBG
#define DBG 1
#endif /* DBG */




#if DBG
/****************************************************************************/

/* Set the level of debugging if not done with a preprocessor define. See
   wl_main.c, init_module() for how the debug level translates into the
   the types of messages displayed */
#ifndef DBG_LVL
#define DBG_LVL 5			/* yields nothing via init_module,
					   original value of 5 yields
					   DBG_TRACE_ON and DBG_VERBOSE_ON */
#endif  /*  DBG_LVL*/


#define DBG_ERROR_ON        0x00000001L
#define DBG_WARNING_ON      0x00000002L
#define DBG_NOTICE_ON       0x00000004L
#define DBG_TRACE_ON        0x00000008L
#define DBG_VERBOSE_ON      0x00000010L
#define DBG_PARAM_ON        0x00000020L
#define DBG_BREAK_ON        0x00000040L
#define DBG_RX_ON           0x00000100L
#define DBG_TX_ON           0x00000200L
#define DBG_DS_ON           0x00000400L

#define DBG_DEFAULTS        (DBG_ERROR_ON | DBG_WARNING_ON | DBG_BREAK_ON)

#define DBG_FLAGS(A)        ((A)->DebugFlag)
#define DBG_NAME(A)         ((A)->dbgName)
#define DBG_LEVEL(A)        ((A)->dbgLevel)


#ifndef DBG_PRINT
#   define DBG_PRINT(S...)  printk(KERN_DEBUG S)
#endif /* DBG_PRINT */


#ifndef DBG_PRINTC
#   define DBG_PRINTC(S...) printk(S)
#endif /* DBG_PRINTC */


#define DBG_PARAM(A, N, F, S...)   {if (DBG_FLAGS(A) & DBG_PARAM_ON) \
				DBG_PRINT("  %s -- "F"\n", N, S); }


#define DBG_ERROR(A, S...) do {						\
		if (DBG_FLAGS(A) & DBG_ERROR_ON) {			\
			DBG_PRINT("%s:ERROR:%s ", DBG_NAME(A), __func__); \
			DBG_PRINTC(S);					\
		} } while (0)


#define DBG_WARNING(A, S...) do {					\
		if (DBG_FLAGS(A) & DBG_WARNING_ON) {			\
			DBG_PRINT("%s:WARNING:%s ", DBG_NAME(A), __func__); \
			DBG_PRINTC(S);					\
		} } while (0)


#define DBG_NOTICE(A, S...)  do {					\
		if (DBG_FLAGS(A) & DBG_NOTICE_ON) {			\
			DBG_PRINT("%s:NOTICE:%s ", DBG_NAME(A), __func__); \
			DBG_PRINTC(S);					\
		} } while (0)


#define DBG_TRACE(A, S...)   do { \
		if (DBG_FLAGS(A) & DBG_TRACE_ON) {			\
			DBG_PRINT("%s:%s ", DBG_NAME(A), __func__);	\
			DBG_PRINTC(S);					\
		} } while (0)


#define DBG_RX(A, S...)      {if (DBG_FLAGS(A) & DBG_RX_ON) {\
				DBG_PRINT(S); } }


#define DBG_TX(A, S...)      {if (DBG_FLAGS(A) & DBG_TX_ON) {\
				DBG_PRINT(S); } }

#define DBG_DS(A, S...)      {if (DBG_FLAGS(A) & DBG_DS_ON) {\
				DBG_PRINT(S); } }


#define DBG_ASSERT(C) do { \
		if (!(C)) {						\
			DBG_PRINT("ASSERT(%s) -- %s#%d (%s)\n",		\
				  #C, __FILE__, __LINE__, __func__);	\
		} } while (0)

struct dbg_info {
	char		*dbgName;
	int		dbgLevel;
	unsigned long	DebugFlag;
};

extern struct dbg_info *DbgInfo;


/****************************************************************************/
#else /* DBG */
/****************************************************************************/

#define DBG_PRINT(S...)
#define DBG_PARAM(A, N, F, S...)
#define DBG_ERROR(A, S...)
#define DBG_WARNING(A, S...)
#define DBG_NOTICE(A, S...)
#define DBG_TRACE(A, S...)
#define DBG_RX(A, S...)
#define DBG_TX(A, S...)
#define DBG_DS(A, S...)
#define DBG_ASSERT(C)

#endif /* DBG */
/****************************************************************************/




#endif /* _DEBUG_H */

