/**************************************************************************
 *
 * Copyright © 2000-2008 Alacritech, Inc.  All rights reserved.
 *
 * $Id: sxgdbg.h,v 1.1 2008/06/27 12:49:28 mook Exp $
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
 * FILENAME: sxgdbg.h
 *
 * All debug and assertion-based definitions and macros are included
 * in this file for the SXGOSS driver.
 */
#ifndef _SXG_DEBUG_H_
#define _SXG_DEBUG_H_

#define ATKDBG  1
#define ATK_TRACE_ENABLED 0

#define DBG_ERROR(n, args...)	printk(KERN_WARNING n, ##args)

#ifdef ASSERT
#undef ASSERT
#endif

#define SXG_ASSERT_ENABLED
#ifdef SXG_ASSERT_ENABLED
#ifndef ASSERT
#define ASSERT(a)                                                          \
    {                                                                      \
        if (!(a)) {                                                        \
            DBG_ERROR("ASSERT() Failure: file %s, function %s  line %d\n", \
                __FILE__, __func__, __LINE__);                             \
        }                                                                  \
    }
#endif
#else
#ifndef ASSERT
#define ASSERT(a)
#endif
#endif /* SXG_ASSERT_ENABLED  */


#ifdef ATKDBG
/*
 *  Global for timer granularity; every driver must have an instance
 *  of this initialized to 0
 */

extern ulong ATKTimerDiv;

/*
 * trace_entry -
 *
 * This structure defines an entry in the trace buffer.  The
 * first few fields mean the same from entry to entry, while
 * the meaning of last several fields change to suit the
 * needs of the trace entry.  Typically they are function call
 * parameters.
 */
struct trace_entry {
        char      	name[8];/* 8 character name - like 's'i'm'b'a'r'c'v' */
        u32   		time;  /* Current clock tic */
        unsigned char   cpu;   /* Current CPU */
        unsigned char   irql;  /* Current IRQL */
        unsigned char   driver;/* The driver which added the trace call */
	/* pad to 4 byte boundary - will probably get used */
        unsigned char   pad2;
        u32		arg1;           /* Caller arg1 */
        u32		arg2;           /* Caller arg2 */
        u32		arg3;           /* Caller arg3 */
        u32		arg4;           /* Caller arg4 */
};

/* Driver types for driver field in struct trace_entry */
#define TRACE_SXG             1
#define TRACE_VPCI            2
#define TRACE_SLIC            3

#define TRACE_ENTRIES   1024

struct sxg_trace_buffer {
	/* aid for windbg extension */
	unsigned int            size;
	unsigned int            in;                    /* Where to add */
	unsigned int            level;                 /* Current Trace level */
	spinlock_t		lock;                  /* For MP tracing */
	struct trace_entry	entries[TRACE_ENTRIES];/* The circular buffer */
};

/*
 * The trace levels
 *
 * XXX At the moment I am only defining critical, important, and noisy.
 * I am leaving room for more if anyone wants them.
 */
#define TRACE_NONE              0   /* For trace level - if no tracing wanted */
#define TRACE_CRITICAL          1   /* minimal tracing - only critical stuff */
#define TRACE_IMPORTANT         5   /* more tracing - anything important */
#define TRACE_NOISY             10  /* Everything in the world */


/* The macros themselves */
#if ATK_TRACE_ENABLED
#define SXG_TRACE_INIT(buffer, tlevel)				\
{								\
	memset((buffer), 0, sizeof(struct sxg_trace_buffer));	\
	(buffer)->level = (tlevel);				\
	(buffer)->size = TRACE_ENTRIES;				\
	spin_lock_init(&(buffer)->lock);			\
}
#else
#define SXG_TRACE_INIT(buffer, tlevel)
#endif

/*The trace macro.  This is active only if ATK_TRACE_ENABLED is set. */
#if ATK_TRACE_ENABLED
#define SXG_TRACE(tdriver, buffer, tlevel, tname, a1, a2, a3, a4) {        \
        if ((buffer) && ((buffer)->level >= (tlevel))) {                   \
                unsigned int            trace_irql = 0;/* ?????? FIX THIS */\
                unsigned int            trace_len;                          \
                struct trace_entry	*trace_entry;			    \
                struct timeval  timev;                                      \
		if(spin_trylock(&(buffer)->lock))	{		     \
	                trace_entry = &(buffer)->entries[(buffer)->in];      \
        	        do_gettimeofday(&timev);                             \
                	                                                     \
	                memset(trace_entry->name, 0, 8);                     \
        	        trace_len = strlen(tname);                           \
	                trace_len = trace_len > 8 ? 8 : trace_len;           \
        	        memcpy(trace_entry->name, (tname), trace_len);       \
	                trace_entry->time = timev.tv_usec;                   \
			trace_entry->cpu = (unsigned char)(smp_processor_id() & 0xFF);\
	                trace_entry->driver = (tdriver);                     \
        	        trace_entry->irql = trace_irql;                      \
	                trace_entry->arg1 = (ulong)(a1);                     \
        	        trace_entry->arg2 = (ulong)(a2);                     \
	                trace_entry->arg3 = (ulong)(a3);                     \
        	        trace_entry->arg4 = (ulong)(a4);                     \
	                                                                     \
        	        (buffer)->in++;                                      \
                	if ((buffer)->in == TRACE_ENTRIES)                   \
	                        (buffer)->in = 0;                            \
        	                                                             \
			spin_unlock(&(buffer)->lock);                        \
 	       	}                                                            \
	}								     \
}
#else
#define SXG_TRACE(tdriver, buffer, tlevel, tname, a1, a2, a3, a4)
#endif

#endif

#endif  /*  _SXG_DEBUG_H_  */
