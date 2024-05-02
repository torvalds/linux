/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */

#ifndef _XE_ARGS_H_
#define _XE_ARGS_H_

#include <linux/args.h>

/*
 * Why don't the following macros have the XE prefix?
 *
 * Once we find more potential users outside of the Xe driver, we plan to move
 * all of the following macros unchanged to linux/args.h.
 */

/**
 * CALL_ARGS - Invoke a macro, but allow parameters to be expanded beforehand.
 * @f: name of the macro to invoke
 * @args: arguments for the macro
 *
 * This macro allows calling macros which names might generated or we want to
 * make sure it's arguments will be correctly expanded.
 *
 * Example:
 *
 *	#define foo	X,Y,Z,Q
 *	#define bar	COUNT_ARGS(foo)
 *	#define buz	CALL_ARGS(COUNT_ARGS, foo)
 *
 *	With above definitions bar expands to 1 while buz expands to 4.
 */
#define CALL_ARGS(f, args...)		__CALL_ARGS(f, args)
#define __CALL_ARGS(f, args...)		f(args)

/**
 * DROP_FIRST - Returns all arguments except the first one.
 * @args: arguments
 *
 * This helper macro allows manipulation the argument list before passing it
 * to the next level macro.
 *
 * Example:
 *
 *	#define foo	X,Y,Z,Q
 *	#define bar	CALL_ARGS(COUNT_ARGS, DROP_FIRST(foo))
 *
 *	With above definitions bar expands to 3.
 */
#define DROP_FIRST(args...)		__DROP_FIRST(args)
#define __DROP_FIRST(a, b...)		b

/**
 * PICK_FIRST - Returns the first argument.
 * @args: arguments
 *
 * This helper macro allows manipulation the argument list before passing it
 * to the next level macro.
 *
 * Example:
 *
 *	#define foo	X,Y,Z,Q
 *	#define bar	PICK_FIRST(foo)
 *
 *	With above definitions bar expands to X.
 */
#define PICK_FIRST(args...)		__PICK_FIRST(args)
#define __PICK_FIRST(a, b...)		a

/**
 * PICK_LAST - Returns the last argument.
 * @args: arguments
 *
 * This helper macro allows manipulation the argument list before passing it
 * to the next level macro.
 *
 * Like COUNT_ARGS() this macro works up to 12 arguments.
 *
 * Example:
 *
 *	#define foo	X,Y,Z,Q
 *	#define bar	PICK_LAST(foo)
 *
 *	With above definitions bar expands to Q.
 */
#define PICK_LAST(args...)		__PICK_ARG(COUNT_ARGS(args), args)
#define __PICK_ARG(n, args...)		CALL_ARGS(CONCATENATE(PICK_ARG, n), args)
#define PICK_ARG1(args...)		PICK_FIRST(args)
#define PICK_ARG2(args...)		PICK_ARG1(DROP_FIRST(args))
#define PICK_ARG3(args...)		PICK_ARG2(DROP_FIRST(args))
#define PICK_ARG4(args...)		PICK_ARG3(DROP_FIRST(args))
#define PICK_ARG5(args...)		PICK_ARG4(DROP_FIRST(args))
#define PICK_ARG6(args...)		PICK_ARG5(DROP_FIRST(args))
#define PICK_ARG7(args...)		PICK_ARG6(DROP_FIRST(args))
#define PICK_ARG8(args...)		PICK_ARG7(DROP_FIRST(args))
#define PICK_ARG9(args...)		PICK_ARG8(DROP_FIRST(args))
#define PICK_ARG10(args...)		PICK_ARG9(DROP_FIRST(args))
#define PICK_ARG11(args...)		PICK_ARG10(DROP_FIRST(args))
#define PICK_ARG12(args...)		PICK_ARG11(DROP_FIRST(args))

/**
 * ARGS_SEP_COMMA - Definition of a comma character.
 *
 * This definition can be used in cases where any intermediate macro expects
 * fixed number of arguments, but we want to pass more arguments which can
 * be properly evaluated only by the next level macro.
 *
 * Example:
 *
 *	#define foo(f)	f(X) f(Y) f(Z) f(Q)
 *	#define bar	DROP_FIRST(foo(ARGS_SEP_COMMA __stringify))
 *	#define buz	CALL_ARGS(COUNT_ARGS, DROP_FIRST(foo(ARGS_SEP_COMMA)))
 *
 *	With above definitions bar expands to
 *		"X", "Y", "Z", "Q"
 *	and buz expands to 4.
 */
#define ARGS_SEP_COMMA			,

#endif
