/*
 * define_trace.h
 *
 * Copyright (C) 2009 Steven Rostedt <rostedt@goodmis.org>
 * Copyright (C) 2010-2012 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; only
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/*
 * Trace files that want to automate creationg of all tracepoints defined
 * in their file should include this file. The following are macros that the
 * trace file may define:
 *
 * TRACE_SYSTEM defines the system the tracepoint is for
 *
 * TRACE_INCLUDE_FILE if the file name is something other than TRACE_SYSTEM.h
 *     This macro may be defined to tell define_trace.h what file to include.
 *     Note, leave off the ".h".
 *
 * TRACE_INCLUDE_PATH if the path is something other than core kernel include/trace
 *     then this macro can define the path to use. Note, the path is relative to
 *     define_trace.h, not the file including it. Full path names for out of tree
 *     modules must be used.
 */

#ifdef CREATE_TRACE_POINTS

/* Prevent recursion */
#undef CREATE_TRACE_POINTS

#include <linux/stringify.h>
/*
 * module.h includes tracepoints, and because ftrace.h
 * pulls in module.h:
 *  trace/ftrace.h -> linux/ftrace_event.h -> linux/perf_event.h ->
 *  linux/ftrace.h -> linux/module.h
 * we must include module.h here before we play with any of
 * the TRACE_EVENT() macros, otherwise the tracepoints included
 * by module.h may break the build.
 */
#include <linux/module.h>

#undef TRACE_EVENT_MAP
#define TRACE_EVENT_MAP(name, map, proto, args, tstruct, assign, print)	\
	DEFINE_TRACE(name)

#undef TRACE_EVENT_CONDITION_MAP
#define TRACE_EVENT_CONDITION_MAP(name, map, proto, args, cond, tstruct, assign, print) \
	TRACE_EVENT(name,						\
		PARAMS(proto),						\
		PARAMS(args),						\
		PARAMS(tstruct),					\
		PARAMS(assign),						\
		PARAMS(print))

#undef TRACE_EVENT_FN_MAP
#define TRACE_EVENT_FN_MAP(name, map, proto, args, tstruct,		\
		assign, print, reg, unreg)			\
	DEFINE_TRACE_FN(name, reg, unreg)

#undef DEFINE_EVENT_MAP
#define DEFINE_EVENT_MAP(template, name, map, proto, args) \
	DEFINE_TRACE(name)

#undef DEFINE_EVENT_PRINT_MAP
#define DEFINE_EVENT_PRINT_MAP(template, name, map, proto, args, print)	\
	DEFINE_TRACE(name)

#undef DEFINE_EVENT_CONDITION_MAP
#define DEFINE_EVENT_CONDITION_MAP(template, name, map, proto, args, cond) \
	DEFINE_EVENT(template, name, PARAMS(proto), PARAMS(args))


#undef TRACE_EVENT
#define TRACE_EVENT(name, proto, args, tstruct, assign, print)	\
	DEFINE_TRACE(name)

#undef TRACE_EVENT_CONDITION
#define TRACE_EVENT_CONDITION(name, proto, args, cond, tstruct, assign, print) \
	TRACE_EVENT(name,						\
		PARAMS(proto),						\
		PARAMS(args),						\
		PARAMS(tstruct),					\
		PARAMS(assign),						\
		PARAMS(print))

#undef TRACE_EVENT_FN
#define TRACE_EVENT_FN(name, proto, args, tstruct,		\
		assign, print, reg, unreg)			\
	DEFINE_TRACE_FN(name, reg, unreg)

#undef DEFINE_EVENT
#define DEFINE_EVENT(template, name, proto, args) \
	DEFINE_TRACE(name)

#undef DEFINE_EVENT_PRINT
#define DEFINE_EVENT_PRINT(template, name, proto, args, print)	\
	DEFINE_TRACE(name)

#undef DEFINE_EVENT_CONDITION
#define DEFINE_EVENT_CONDITION(template, name, proto, args, cond) \
	DEFINE_EVENT(template, name, PARAMS(proto), PARAMS(args))

#undef DECLARE_TRACE
#define DECLARE_TRACE(name, proto, args)	\
	DEFINE_TRACE(name)

#undef TRACE_INCLUDE
#undef __TRACE_INCLUDE

#ifndef TRACE_INCLUDE_FILE
# define TRACE_INCLUDE_FILE TRACE_SYSTEM
# define UNDEF_TRACE_INCLUDE_FILE
#endif

#ifndef TRACE_INCLUDE_PATH
# define __TRACE_INCLUDE(system) <trace/events/system.h>
# define UNDEF_TRACE_INCLUDE_PATH
#else
# define __TRACE_INCLUDE(system) __stringify(TRACE_INCLUDE_PATH/system.h)
#endif

# define TRACE_INCLUDE(system) __TRACE_INCLUDE(system)

/* Let the trace headers be reread */
#define TRACE_HEADER_MULTI_READ

#include TRACE_INCLUDE(TRACE_INCLUDE_FILE)

/* Make all open coded DECLARE_TRACE nops */
#undef DECLARE_TRACE
#define DECLARE_TRACE(name, proto, args)

#ifdef LTTNG_PACKAGE_BUILD
#include "lttng-events.h"
#endif

#undef TRACE_EVENT
#undef TRACE_EVENT_FN
#undef TRACE_EVENT_CONDITION
#undef DEFINE_EVENT
#undef DEFINE_EVENT_PRINT
#undef DEFINE_EVENT_CONDITION
#undef TRACE_EVENT_MAP
#undef TRACE_EVENT_FN_MAP
#undef TRACE_EVENT_CONDITION_MAP
#undef DECLARE_EVENT_CLASS
#undef DEFINE_EVENT_MAP
#undef DEFINE_EVENT_PRINT_MAP
#undef DEFINE_EVENT_CONDITION_MAP
#undef TRACE_HEADER_MULTI_READ

/* Only undef what we defined in this file */
#ifdef UNDEF_TRACE_INCLUDE_FILE
# undef TRACE_INCLUDE_FILE
# undef UNDEF_TRACE_INCLUDE_FILE
#endif

#ifdef UNDEF_TRACE_INCLUDE_PATH
# undef TRACE_INCLUDE_PATH
# undef UNDEF_TRACE_INCLUDE_PATH
#endif

/* We may be processing more files */
#define CREATE_TRACE_POINTS

#endif /* CREATE_TRACE_POINTS */
