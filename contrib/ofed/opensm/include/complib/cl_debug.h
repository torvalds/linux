/*
 * Copyright (c) 2004, 2005 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2005 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

/*
 * Abstract:
 *	Declaration of functions for reporting debug output.
 */

#ifndef _CL_DEBUG_H_
#define _CL_DEBUG_H_

#include <complib/cl_debug_osd.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****h* Component Library/Debug Output
* NAME
*	Debug Output
*
* DESCRIPTION
*	The debug output functions and macros send debug messages to the current
*	debug target.
*********/
/****f* Component Library: Debug Output/cl_break
* NAME
*	cl_break
*
* DESCRIPTION
*	The cl_break function halts execution.
*
* SYNOPSIS
*	void
*	cl_break();
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	In a release build, cl_break has no effect.
*********/
/****f* Component Library: Debug Output/cl_is_debug
* NAME
*	cl_is_debug
*
* DESCRIPTION
*	The cl_is_debug function returns TRUE if the complib was compiled
*  in debug mode, and FALSE otherwise.
*
* SYNOPSIS
*/
boolean_t cl_is_debug(void);
/*
* PARAMETERS
*    None
*
* RETURN VALUE
*	  TRUE if compiled in debug version. FALSE otherwise.
*
* NOTES
*
*********/

#if defined( _DEBUG_ )
#ifndef cl_dbg_out
/****f* Component Library: Debug Output/cl_dbg_out
* NAME
*	cl_dbg_out
*
* DESCRIPTION
*	The cl_dbg_out function sends a debug message to the debug target in
*	debug builds only.
*
* SYNOPSIS
*/
void cl_dbg_out(IN const char *const debug_message, IN ...);
/*
* PARAMETERS
*	debug_message
*		[in] ANSI string formatted identically as for a call to the standard C
*		function printf.
*
*	...
*		[in] Extra parameters for string formatting, as defined for the
*		standard C function printf.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	In a release build, cl_dbg_out has no effect.
*
*	The formatting of the debug_message string is the same as for printf
*
*	cl_dbg_out sends the debug message to the current debug target.
*
* SEE ALSO
*	Debug Output, cl_msg_out
*********/
#endif
#else
static inline void cl_dbg_out(IN const char *const debug_message, IN ...)
{
	UNUSED_PARAM(debug_message);
}
#endif				/* defined( _DEBUG_ ) */

#ifndef cl_msg_out
/****f* Component Library: Debug Output/cl_msg_out
* NAME
*	cl_msg_out
*
* DESCRIPTION
*	The cl_msg_out function sends a debug message to the message log target.
*
* SYNOPSIS
*/
void cl_msg_out(IN const char *const message, IN ...);
/*
* PARAMETERS
*	message
*		[in] ANSI string formatted identically as for a call to the standard C
*		function printf.
*
*	...
*		[in] Extra parameters for string formatting, as defined for the
*		standard C function printf.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	cl_msg_out is available in both debug and release builds.
*
*	The formatting of the message string is the same as for printf
*
*	cl_msg_out sends the message to the current message logging target.
*
* SEE ALSO
*	Debug Output, cl_dbg_out
*********/
#endif

/****d* Component Library: Debug Output/Debug Levels
* NAME
*	Debug Levels
*
* DESCRIPTION
*	The debug output macros reserve the upper bit of the debug level to
*	convey an error.
*
* SYNOPSIS
*/
#define	CL_DBG_DISABLE		0
#define	CL_DBG_ERROR		0x80000000
#define	CL_DBG_ALL			0xFFFFFFFF
/*
* VALUES
*	CL_DBG_DISABLE
*		Disable all debug output, including errors.
*
*	CL_DBG_ERROR
*		Enable error debug output.
*
*	CL_DBG_ALL
*		Enbale all debug output.
*
* NOTES
*	Users can define custom debug levels using the lower 31 bits of their
*	debug level to control non-error debug output.  Error messages are
*	always displayed, regardless of the lower bit definition.
*
*	When specifying the debug output desired for non-error messages
*	(the CHK_LVL parameter in the debug output macros), users must define
*	all bits whose output they are interested in.
*
* SEE ALSO
*	Debug Output, CL_PRINT, CL_ENTER, CL_EXIT, CL_TRACE, CL_TRACE_EXIT
*********/

#if defined(_DEBUG_)

/****d* Component Library: Debug Output/CL_PRINT
* NAME
*	CL_PRINT
*
* DESCRIPTION
*	The CL_PRINT macro sends a string to the current debug target if
*	the requested debug level matches the current debug level.
*
* SYNOPSIS
*	CL_PRINT( DBG_LVL, CHK_LVL, STRING );
*
* PARAMETERS
*	DBG_LVL
*		[in] Debug level for the string to output
*
*	CHK_LVL
*		[in] Current debug level against which to check DBG_LVL
*
*	STRING
*		[in] String to send to the current debug target.  The string includes
*		parentheses in order to allow additional parameters.
*
* RETURN VALUE
*	This macro does not return a value.
*
* EXAMPLE
*	#define	MY_FUNC_DBG_LVL	1
*
*	uint32_t	my_dbg_lvl = CL_DBG_ALL;
*
*	void
*	my_func()
*	{
*		CL_PRINT( MY_FUNC_DBG_LVL, my_dbg_lvl, ("Hello %s!\n", "world") );
*	}
*
* RESULT
*	Hello world!
*
* NOTES
*	The requested string is printed only if all bits set in DBG_LVL are also
*	set in CHK_LVL unless the most significant bit is set (indicating an
*	error), in which case the lower bits are ignored.  CHK_LVL may have
*	additional bits set.
*
*	In multi-processor environments where the current processor can be
*	determined, the zero-based number of the processor on which the output
*	is generated is prepended to the output.
*
* SEE ALSO
*	Debug Output, Debug Levels, CL_ENTER, CL_EXIT, CL_TRACE, CL_TRACE_EXIT
*********/
#define	CL_PRINT( DBG_LVL, CHK_LVL, STRING )								\
	{																		\
	if( DBG_LVL & CL_DBG_ERROR )											\
		cl_dbg_out STRING;													\
	else if( (DBG_LVL & CHK_LVL) == DBG_LVL )								\
		cl_dbg_out STRING;													\
	}

/****d* Component Library: Debug Output/CL_ENTER
* NAME
*	CL_ENTER
*
* DESCRIPTION
*	The CL_ENTER macro marks the entrance into a function by sending a
*	string to the current debug target if the requested debug level matches
*	the current debug level.
*
* SYNOPSIS
*	CL_ENTER( DBG_LVL, CHK_LVL );
*
* PARAMETERS
*	DBG_LVL
*		[in] Debug level for the string to output
*
*	CHK_LVL
*		[in] Current debug level against which to check DBG_LVL
*
* RETURN VALUE
*	This macro does not return a value.
*
* EXAMPLE
*	#define __MODULE__	"my_module"
*	#define	MY_FUNC_DBG_LVL	1
*
*	uint32_t	my_dbg_lvl = CL_DBG_ALL;
*
*	void
*	my_func()
*	{
*		CL_ENTER( MY_FUNC_DBG_LVL, my_dbg_lvl );
*		CL_EXIT( MY_FUNC_DBG_LVL, my_dbg_lvl );
*	}
*
* RESULT
*	my_module:my_func() [
*	my_module:my_func() ]
*
* NOTES
*	The function entrance notification is printed only if all bits set
*	in DBG_LVL are also set in CHK_LVL.  CHK_LVL may have additional bits set.
*
*	If the __MODULE__ preprocessor keyword is defined, that keyword will be
*	prepended to the function name, separated with a colon.
*
*	In multi-processor environments where the current processor can be
*	determined, the zero-based number of the processor on which the output
*	is generated is prepended to the output.
*
* SEE ALSO
*	Debug Output, Debug Levels, CL_PRINT, CL_EXIT, CL_TRACE, CL_TRACE_EXIT
*********/
#define CL_ENTER( DBG_LVL, CHK_LVL )										\
	CL_CHK_STK;																\
	CL_PRINT( DBG_LVL, CHK_LVL, _CL_DBG_ENTER );

/****d* Component Library: Debug Output/CL_EXIT
* NAME
*	CL_EXIT
*
* DESCRIPTION
*	The CL_EXIT macro marks the exit from a function by sending a string
*	to the current debug target if the requested debug level matches the
*	current debug level.
*
* SYNOPSIS
*	CL_EXIT( DBG_LVL, CHK_LVL );
*
* PARAMETERS
*	DBG_LVL
*		[in] Debug level for the string to output
*
*	CHK_LVL
*		[in] Current debug level against which to check DBG_LVL
*
* RETURN VALUE
*	This macro does not return a value.
*
* EXAMPLE
*	#define __MODULE__	"my_module"
*	#define	MY_FUNC_DBG_LVL	1
*
*	uint32_t	my_dbg_lvl = CL_DBG_ALL;
*
*	void
*	my_func()
*	{
*		CL_ENTER( MY_FUNC_DBG_LVL, my_dbg_lvl );
*		CL_EXIT( MY_FUNC_DBG_LVL, my_dbg_lvl );
*	}
*
* RESULT
*	my_module:my_func() [
*	my_module:my_func() ]
*
* NOTES
*	The exit notification is printed only if all bits set in DBG_LVL are also
*	set in CHK_LVL.  CHK_LVL may have additional bits set.
*
*	The CL_EXIT macro must only be used after the CL_ENTRY macro as it
*	depends on that macro's implementation.
*
*	If the __MODULE__ preprocessor keyword is defined, that keyword will be
*	prepended to the function name, separated with a colon.
*
*	In multi-processor environments where the current processor can be
*	determined, the zero-based number of the processor on which the output
*	is generated is prepended to the output.
*
* SEE ALSO
*	Debug Output, Debug Levels, CL_PRINT, CL_ENTER, CL_TRACE, CL_TRACE_EXIT
*********/
#define CL_EXIT( DBG_LVL, CHK_LVL )											\
	CL_PRINT( DBG_LVL, CHK_LVL, _CL_DBG_EXIT );

/****d* Component Library: Debug Output/CL_TRACE
* NAME
*	CL_TRACE
*
* DESCRIPTION
*	The CL_TRACE macro sends a string to the current debug target if
*	the requested debug level matches the current debug level.  The
*	output is prepended with the function name and, depending on the
*	debug level requested, an indication of the severity of the message.
*
* SYNOPSIS
*	CL_TRACE( DBG_LVL, CHK_LVL, STRING );
*
* PARAMETERS
*	DBG_LVL
*		[in] Debug level for the string to output
*
*	CHK_LVL
*		[in] Current debug level against which to check DBG_LVL
*
*	STRING
*		[in] String to send to the current debug target.  The string includes
*		parentheses in order to allow additional parameters.
*
* RETURN VALUE
*	This macro does not return a value.
*
* EXAMPLE
*	#define __MODULE__	"my_module"
*	#define	MY_FUNC_DBG_LVL	1
*
*	uint32_t	my_dbg_lvl = CL_DBG_ALL;
*
*	void
*	my_func()
*	{
*		CL_ENTER( MY_FUNC_DBG_LVL, my_dbg_lvl );
*		CL_TRACE( MY_FUNC_DBG_LVL, my_dbg_lvl, ("Hello %s!\n", "world") );
*		CL_EXIT( MY_FUNC_DBG_LVL, my_dbg_lvl );
*	}
*
* RESULT
*	my_module:my_func() [
*	my_module:my_func(): Hello world!
*	my_module:my_func() ]
*
* NOTES
*	The requested string is printed only if all bits set in DBG_LVL are also
*	set in CHK_LVL.  CHK_LVL may have additional bits set.
*
*	The CL_TRACE macro must only be used after the CL_ENTRY macro as it
*	depends on that macro's implementation.
*
*	If the DBG_LVL has the upper bit set, the output will contain
*	an "!ERROR!" statement between the function name and STRING.
*
*	If the __MODULE__ preprocessor keyword is defined, that keyword will be
*	prepended to the function name, separated with a colon.
*
*	In multi-processor environments where the current processor can be
*	determined, the zero-based number of the processor on which the output
*	is generated is prepended to the output.
*
* SEE ALSO
*	Debug Output, Debug Levels, CL_PRINT, CL_ENTER, CL_EXIT, CL_TRACE_EXIT
*********/
#define CL_TRACE( DBG_LVL, CHK_LVL, STRING )								\
{																			\
switch( DBG_LVL & CL_DBG_ERROR )											\
{																			\
	case CL_DBG_ERROR:														\
		CL_PRINT( DBG_LVL, CHK_LVL, _CL_DBG_ERROR );						\
		break;																\
	default:																\
		CL_PRINT( DBG_LVL, CHK_LVL, _CL_DBG_INFO );							\
		break;																\
}																			\
CL_PRINT( DBG_LVL, CHK_LVL, STRING );										\
}

/****d* Component Library: Debug Output/CL_TRACE_EXIT
* NAME
*	CL_TRACE_EXIT
*
* DESCRIPTION
*	The CL_TRACE_EXIT macro combines the functionality of the CL_TRACE and
*	CL_EXIT macros, in that order.
*
* SYNOPSIS
*	CL_TRACE_EXIT(  DBG_LVL, CHK_LVL, STRING );
*
* PARAMETERS
*	DBG_LVL
*		[in] Debug level for the string to output
*
*	CHK_LVL
*		[in] Current debug level against which to check DBG_LVL
*
*	STRING
*		[in] String to send to the current debug target.  The string includes
*		parentheses in order to allow additional parameters.
*
* RETURN VALUE
*	This macro does not return a value.
*
* EXAMPLE
*	#define __MODULE__	"my_module"
*	#define	MY_FUNC_DBG_LVL	1
*
*	uint32_t	my_dbg_lvl = CL_DBG_ALL;
*
*	void
*	my_func()
*	{
*		CL_ENTER( MY_FUNC_DBG_LVL, my_dbg_lvl );
*		CL_TRACE_EXIT( MY_FUNC_DBG_LVL, my_dbg_lvl, ("Hello %s!\n", "world") );
*	}
*
* RESULT
*	my_module:my_func() [
*	my_module:my_func(): Hello world!
*	my_module:my_func() ]
*
* NOTES
*	The requested string is printed only if all bits set in DBG_LVL are also
*	set in CHK_LVL.  CHK_LVL may have additional bits set.
*
*	The CL_TRACE_EXIT macro must only be used after the CL_ENTRY macro as it
*	depends on that macro's implementation.
*
*	If the DBG_LVL has the upper bit set, the output will contain
*	an "!ERROR!" statement between the function name and STRING.
*
*	If the __MODULE__ preprocessor keyword is defined, that keyword will be
*	prepended to the function name, separated with a colon.
*
*	In multi-processor environments where the current processor can be
*	determined, the zero-based number of the processor on which the output
*	is generated is prepended to the output.
*
* SEE ALSO
*	Debug Output, Debug Levels, CL_PRINT, CL_ENTER, CL_EXIT, CL_TRACE
*********/
#define CL_TRACE_EXIT( DBG_LVL, CHK_LVL, STRING )							\
	CL_TRACE( DBG_LVL, CHK_LVL, STRING );									\
	CL_EXIT( DBG_LVL, CHK_LVL );

#else				/* defined(_DEBUG_) */

/* Define as NULL macros in a free build. */
#define CL_PRINT( DBG_LVL, CHK_LVL, STRING );
#define CL_ENTER( DBG_LVL, CHK_LVL );
#define CL_EXIT( DBG_LVL, CHK_LVL );
#define CL_TRACE( DBG_LVL, CHK_LVL, STRING );
#define CL_TRACE_EXIT( DBG_LVL, CHK_LVL, STRING );

#endif				/* defined(_DEBUG_) */

/****d* Component Library: Debug Output/64-bit Print Format
* NAME
*	64-bit Print Format
*
* DESCRIPTION
*	The 64-bit print keywords allow users to use 64-bit values in debug or
*	console output.
*
*	Different platforms define 64-bit print formats differently. The 64-bit
*	print formats exposed by the component library are supported in all
*	platforms.
*
* VALUES
*	PRId64
*		Print a 64-bit integer in signed decimal format.
*	PRIx64
*		Print a 64-bit integer in hexadecimal format.
*	PRIo64
*		Print a 64-bit integer in octal format.
*	PRIu64
*		Print a 64-bit integer in unsigned decimal format.
*
* EXAMPLE
*	uint64 MyVal = 2;
*	// Print a 64-bit integer in hexadecimal format.
*	cl_dbg_out( "MyVal: 0x%" PRIx64 "\n", MyVal );
*
* NOTES
*	Standard print flags to specify padding and precision can still be used
*	following the '%' sign in the string preceding the 64-bit print keyword.
*
*	The above keywords are strings and make use of compilers' string
*	concatenation ability.
*********/
void complib_init(void);

void complib_exit(void);

END_C_DECLS
#endif				/* _CL_DEBUG_H_ */
