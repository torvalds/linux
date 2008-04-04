/*
 * Agere Systems Inc.
 * 10/100/1000 Base-T Ethernet Driver for the ET1301 and ET131x series MACs
 *
 * Copyright © 2005 Agere Systems Inc.
 * All rights reserved.
 *   http://www.agere.com
 *
 *------------------------------------------------------------------------------
 *
 * et131x_debug.h - Defines, structs, enums, prototypes, etc. used for
 *                  outputting debug messages to the system logging facility
 *                  (ksyslogd)
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
 * Copyright © 2005 Agere Systems Inc.
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
 * THIS SOFTWARE IS PROVIDED “AS IS” AND ANY EXPRESS OR IMPLIED WARRANTIES,
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
 */

#ifndef __ET131X_DBG_H__
#define __ET131X_DBG_H__

/* Define Masks for debugging types/levels */
#define DBG_ERROR_ON        0x00000001L
#define DBG_WARNING_ON      0x00000002L
#define DBG_NOTICE_ON       0x00000004L
#define DBG_TRACE_ON        0x00000008L
#define DBG_VERBOSE_ON      0x00000010L
#define DBG_PARAM_ON        0x00000020L
#define DBG_BREAK_ON        0x00000040L
#define DBG_RX_ON           0x00000100L
#define DBG_TX_ON           0x00000200L

#ifdef CONFIG_ET131X_DEBUG

/*
 * Set the level of debugging if not done with a preprocessor define. See
 * et131x_main.c, function et131x_init_module() for how the debug level
 * translates into the types of messages displayed.
 */
#ifndef DBG_LVL
#define DBG_LVL	3
#endif /* DBG_LVL */

#define DBG_DEFAULTS		(DBG_ERROR_ON | DBG_WARNING_ON | DBG_BREAK_ON )

#define DBG_FLAGS(A)		(A)->dbgFlags
#define DBG_NAME(A)		(A)->dbgName
#define DBG_LEVEL(A)		(A)->dbgLevel

#ifndef DBG_PRINT
#define DBG_PRINT(S...)		printk(KERN_DEBUG S)
#endif /* DBG_PRINT */

#ifndef DBG_PRINTC
#define DBG_PRINTC(S...)	printk(S)
#endif /* DBG_PRINTC */

#ifndef DBG_TRAP
#define DBG_TRAP		{}	/* BUG() */
#endif /* DBG_TRAP */

#define _ENTER_STR	">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>"
#define _LEAVE_STR	"<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<"

#define _DBG_ENTER(A)	printk(KERN_DEBUG "%s:%.*s:%s\n", DBG_NAME(A),	\
				++DBG_LEVEL(A), _ENTER_STR, __func__)
#define _DBG_LEAVE(A)	printk(KERN_DEBUG "%s:%.*s:%s\n", DBG_NAME(A),	\
				DBG_LEVEL(A)--, _LEAVE_STR, __func__)

#define DBG_ENTER(A)        {if (DBG_FLAGS(A) & DBG_TRACE_ON) \
                                _DBG_ENTER(A);}

#define DBG_LEAVE(A)        {if (DBG_FLAGS(A) & DBG_TRACE_ON) \
                                _DBG_LEAVE(A);}

#define DBG_PARAM(A,N,F,S...)   {if (DBG_FLAGS(A) & DBG_PARAM_ON) \
                                    DBG_PRINT("  %s -- "F"\n",N,S);}

#define DBG_ERROR(A,S...)	\
	if (DBG_FLAGS(A) & DBG_ERROR_ON) {				\
		DBG_PRINT("%s:ERROR:%s ",DBG_NAME(A), __func__);	\
		DBG_PRINTC(S);						\
		DBG_TRAP;						\
	}

#define DBG_WARNING(A,S...) {if (DBG_FLAGS(A) & DBG_WARNING_ON) \
                                {DBG_PRINT("%s:WARNING:%s ",DBG_NAME(A),__func__);DBG_PRINTC(S);}}

#define DBG_NOTICE(A,S...)  {if (DBG_FLAGS(A) & DBG_NOTICE_ON) \
                                {DBG_PRINT("%s:NOTICE:%s ",DBG_NAME(A),__func__);DBG_PRINTC(S);}}

#define DBG_TRACE(A,S...)   {if (DBG_FLAGS(A) & DBG_TRACE_ON) \
                                {DBG_PRINT("%s:TRACE:%s ",DBG_NAME(A), __func__);DBG_PRINTC(S);}}

#define DBG_VERBOSE(A,S...) {if (DBG_FLAGS(A) & DBG_VERBOSE_ON) \
                                {DBG_PRINT("%s:VERBOSE:%s ",DBG_NAME(A), __func__);DBG_PRINTC(S);}}

#define DBG_RX(A,S...)      {if (DBG_FLAGS(A) & DBG_RX_ON) \
                                {DBG_PRINT(S);}}

#define DBG_RX_ENTER(A)     {if (DBG_FLAGS(A) & DBG_RX_ON) \
                                _DBG_ENTER(A);}

#define DBG_RX_LEAVE(A)     {if (DBG_FLAGS(A) & DBG_RX_ON) \
                                _DBG_LEAVE(A);}

#define DBG_TX(A,S...)      {if (DBG_FLAGS(A) & DBG_TX_ON) \
                                {DBG_PRINT(S);}}

#define DBG_TX_ENTER(A)     {if (DBG_FLAGS(A) & DBG_TX_ON) \
                                _DBG_ENTER(A);}

#define DBG_TX_LEAVE(A)     {if (DBG_FLAGS(A) & DBG_TX_ON) \
                                _DBG_LEAVE(A);}

#define DBG_ASSERT(C)       {if (!(C)) \
                                {DBG_PRINT("ASSERT(%s) -- %s#%d (%s)\n", \
                                    #C,__FILE__,__LINE__,__func__); \
                                DBG_TRAP;}}
#define STATIC

typedef struct {
	char *dbgName;
	int dbgLevel;
	unsigned long dbgFlags;
} dbg_info_t;

#else /* CONFIG_ET131X_DEBUG */

#define DBG_DEFN
#define DBG_TRAP
#define DBG_PRINT(S...)
#define DBG_ENTER(A)
#define DBG_LEAVE(A)
#define DBG_PARAM(A,N,F,S...)
#define DBG_ERROR(A,S...)
#define DBG_WARNING(A,S...)
#define DBG_NOTICE(A,S...)
#define DBG_TRACE(A,S...)
#define DBG_VERBOSE(A,S...)
#define DBG_RX(A,S...)
#define DBG_RX_ENTER(A)
#define DBG_RX_LEAVE(A)
#define DBG_TX(A,S...)
#define DBG_TX_ENTER(A)
#define DBG_TX_LEAVE(A)
#define DBG_ASSERT(C)
#define STATIC static

#endif /* CONFIG_ET131X_DEBUG */

/* Forward declaration of the private adapter structure */
struct et131x_adapter;

void DumpTxQueueContents(int dbgLvl, struct et131x_adapter *adapter);
void DumpDeviceBlock(int dbgLvl, struct et131x_adapter *adapter,
		     unsigned int Block);
void DumpDeviceReg(int dbgLvl, struct et131x_adapter *adapter);

#endif /* __ET131X_DBG_H__ */
