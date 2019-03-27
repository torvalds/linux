/*
 * Copyright (c) 2000-2001 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 *	$Id: assert.h,v 1.11 2013-11-22 20:51:31 ca Exp $
 */

/*
**  libsm abnormal program termination and assertion checking
**  See libsm/assert.html for documentation.
*/

#ifndef SM_ASSERT_H
# define SM_ASSERT_H

# include <sm/gen.h>
# include <sm/debug.h>

/*
**  abnormal program termination
*/

typedef void (*SM_ABORT_HANDLER_T) __P((const char *, int, const char *));

extern SM_DEAD(void
sm_abort_at __P((
	const char *,
	int,
	const char *)));

extern void
sm_abort_sethandler __P((
	SM_ABORT_HANDLER_T));

extern SM_DEAD(void PRINTFLIKE(1, 2)
sm_abort __P((
	char *,
	...)));

/*
**  assertion checking
*/

# ifndef SM_CHECK_ALL
#  define SM_CHECK_ALL		1
# endif /* ! SM_CHECK_ALL */

# ifndef SM_CHECK_REQUIRE
#  define SM_CHECK_REQUIRE	SM_CHECK_ALL
# endif /* ! SM_CHECK_REQUIRE */

# ifndef SM_CHECK_ENSURE
#  define SM_CHECK_ENSURE	SM_CHECK_ALL
# endif /* ! SM_CHECK_ENSURE */

# ifndef SM_CHECK_ASSERT
#  define SM_CHECK_ASSERT	SM_CHECK_ALL
# endif /* ! SM_CHECK_ASSERT */

# if SM_CHECK_REQUIRE
#  if defined(__STDC__) || defined(__cplusplus)
#   define SM_REQUIRE(cond) \
	((void) ((cond) || (sm_abort_at(__FILE__, __LINE__, \
	"SM_REQUIRE(" #cond ") failed"), 0)))
#  else /* defined(__STDC__) || defined(__cplusplus) */
#   define SM_REQUIRE(cond) \
	((void) ((cond) || (sm_abort_at(__FILE__, __LINE__, \
	"SM_REQUIRE(cond) failed"), 0)))
#  endif /* defined(__STDC__) || defined(__cplusplus) */
# else /* SM_CHECK_REQUIRE */
#  define SM_REQUIRE(cond)	((void) 0)
# endif /* SM_CHECK_REQUIRE */

# define SM_REQUIRE_ISA(obj, magic) \
		SM_REQUIRE((obj) != NULL && (obj)->sm_magic == (magic))

# if SM_CHECK_ENSURE
#  if defined(__STDC__) || defined(__cplusplus)
#   define SM_ENSURE(cond) \
	((void) ((cond) || (sm_abort_at(__FILE__, __LINE__, \
	"SM_ENSURE(" #cond ") failed"), 0)))
#  else /* defined(__STDC__) || defined(__cplusplus) */
#   define SM_ENSURE(cond) \
	((void) ((cond) || (sm_abort_at(__FILE__, __LINE__, \
	"SM_ENSURE(cond) failed"), 0)))
#  endif /* defined(__STDC__) || defined(__cplusplus) */
# else /* SM_CHECK_ENSURE */
#  define SM_ENSURE(cond)	((void) 0)
# endif /* SM_CHECK_ENSURE */

# if SM_CHECK_ASSERT
#  if defined(__STDC__) || defined(__cplusplus)
#   define SM_ASSERT(cond) \
	((void) ((cond) || (sm_abort_at(__FILE__, __LINE__, \
	"SM_ASSERT(" #cond ") failed"), 0)))
#  else /* defined(__STDC__) || defined(__cplusplus) */
#   define SM_ASSERT(cond) \
	((void) ((cond) || (sm_abort_at(__FILE__, __LINE__, \
	"SM_ASSERT(cond) failed"), 0)))
#  endif /* defined(__STDC__) || defined(__cplusplus) */
# else /* SM_CHECK_ASSERT */
#  define SM_ASSERT(cond)	((void) 0)
# endif /* SM_CHECK_ASSERT */

extern SM_DEBUG_T SmExpensiveRequire;
extern SM_DEBUG_T SmExpensiveEnsure;
extern SM_DEBUG_T SmExpensiveAssert;

#endif /* ! SM_ASSERT_H */
