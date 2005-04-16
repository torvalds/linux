#ifndef _FTAPE_TRACING_H
#define _FTAPE_TRACING_H

/*
 * Copyright (C) 1994-1996 Bas Laarhoven,
 *           (C) 1996-1997 Claus-Justus Heine.

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2, or (at your option)
 any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; see the file COPYING.  If not, write to
 the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.

 *
 * $Source: /homes/cvs/ftape-stacked/ftape/lowlevel/ftape-tracing.h,v $
 * $Revision: 1.2 $
 * $Date: 1997/10/05 19:18:28 $
 *
 *      This file contains definitions that eases the debugging of the
 *      QIC-40/80/3010/3020 floppy-tape driver "ftape" for Linux.
 */

#include <linux/config.h>
#include <linux/kernel.h>

/*
 *  Be very careful with TRACE_EXIT and TRACE_ABORT.
 *
 *  if (something) TRACE_EXIT error;
 *
 *  will NOT work. Use
 *
 *  if (something) {
 *    TRACE_EXIT error;
 *  }
 *
 *  instead. Maybe a bit dangerous, but save lots of lines of code.
 */

#define LL_X "%d/%d KB"
#define LL(x) (unsigned int)((__u64)(x)>>10), (unsigned int)((x)&1023)

typedef enum {
	ft_t_nil = -1,
	ft_t_bug,
	ft_t_err,
	ft_t_warn,
	ft_t_info,
	ft_t_noise,
	ft_t_flow,
	ft_t_fdc_dma,
	ft_t_data_flow,
	ft_t_any
} ft_trace_t;

#ifdef  CONFIG_FT_NO_TRACE_AT_ALL
/*  the compiler will optimize away most TRACE() macros
 */
#define FT_TRACE_TOP_LEVEL	ft_t_bug
#define TRACE_FUN(level)	do {} while(0)
#define TRACE_EXIT		return
#define TRACE(l, m, i...)						\
{									\
	if ((ft_trace_t)(l) == FT_TRACE_TOP_LEVEL) {			\
		printk(KERN_INFO"ftape%s(%s):\n"	                \
		       KERN_INFO m".\n" ,__FILE__, __FUNCTION__ , ##i);	\
	}								\
}
#define SET_TRACE_LEVEL(l)      if ((l) == (l)) do {} while(0)
#define TRACE_LEVEL		FT_TRACE_TOP_LEVEL

#else

#ifdef CONFIG_FT_NO_TRACE
/*  the compiler will optimize away many TRACE() macros
 *  the ftape_simple_trace_call() function simply increments 
 *  the function nest level.
 */ 
#define FT_TRACE_TOP_LEVEL	ft_t_warn
#define TRACE_FUN(level)	ftape_function_nest_level++
#define TRACE_EXIT		ftape_function_nest_level--; return

#else
#ifdef CONFIG_FT_FULL_DEBUG
#define FT_TRACE_TOP_LEVEL ft_t_any
#else
#define FT_TRACE_TOP_LEVEL ft_t_flow
#endif
#define TRACE_FUN(level)					\
	const ft_trace_t _tracing = level;			\
	if (ftape_tracing >= (ft_trace_t)(level) &&		\
	    (ft_trace_t)(level) <= FT_TRACE_TOP_LEVEL)		\
		ftape_trace_call(__FILE__, __FUNCTION__);	\
	ftape_function_nest_level ++;

#define TRACE_EXIT						\
	--ftape_function_nest_level;				\
	if (ftape_tracing >= (ft_trace_t)(_tracing) &&		\
	    (ft_trace_t)(_tracing) <= FT_TRACE_TOP_LEVEL)	\
		ftape_trace_exit(__FILE__, __FUNCTION__);	\
	return

#endif

#define TRACE(l, m, i...)					\
{								\
	if (ftape_tracing >= (ft_trace_t)(l) &&			\
	    (ft_trace_t)(l) <= FT_TRACE_TOP_LEVEL) {		\
		ftape_trace_log(__FILE__, __FUNCTION__);	\
		printk(m".\n" ,##i);				\
	}							\
}

#define SET_TRACE_LEVEL(l) 				\
{							\
	if ((ft_trace_t)(l) <= FT_TRACE_TOP_LEVEL) {	\
		ftape_tracing = (ft_trace_t)(l);	\
	} else {					\
		ftape_tracing = FT_TRACE_TOP_LEVEL;	\
	}						\
}
#define TRACE_LEVEL    							     \
((ftape_tracing <= FT_TRACE_TOP_LEVEL) ? ftape_tracing : FT_TRACE_TOP_LEVEL)


/*      Global variables declared in tracing.c
 */
extern ft_trace_t ftape_tracing;  /* sets default level */
extern int ftape_function_nest_level;

/*      Global functions declared in tracing.c
 */
extern void ftape_trace_call(const char *file, const char *name);
extern void ftape_trace_exit(const char *file, const char *name);
extern void ftape_trace_log (const char *file, const char *name);

#endif /* !defined(CONFIG_FT_NO_TRACE_AT_ALL) */

/*
 *   Abort with a message.
 */
#define TRACE_ABORT(res, i...)			\
{						\
 	TRACE(i);				\
	TRACE_EXIT res;				\
}

/*   The following transforms the common "if(result < 0) ... " into a
 *   one-liner.
 */
#define _TRACE_CATCH(level, fun, action)				\
{									\
	int _res = (fun);						\
	if (_res < 0) {							\
		do { action /* */ ; } while(0);				\
		TRACE_ABORT(_res, level, "%s failed: %d", #fun,	_res);	\
	}								\
}

#define TRACE_CATCH(fun, fail) _TRACE_CATCH(ft_t_err, fun, fail)

/*  Abort the current function when signalled. This doesn't belong here,
 *  but rather into ftape-rw.h (maybe)
 */
#define FT_SIGNAL_EXIT(sig_mask)					\
	if (sigtestsetmask(&current->pending.signal, sig_mask)) {	\
		TRACE_ABORT(-EINTR,					\
			    ft_t_warn,					\
			    "interrupted by non-blockable signal");	\
	}

#endif /* _FTAPE_TRACING_H */
