/*
 * Copyright (c) 2000-2001 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 *	$Id: exc.h,v 1.24 2013-11-22 20:51:31 ca Exp $
 */

/*
**  libsm exception handling
**  See libsm/exc.html for documentation.
*/

#ifndef SM_EXC_H
# define SM_EXC_H

#include <sm/setjmp.h>
#include <sm/io.h>
#include <sm/gen.h>
#include <sm/assert.h>

typedef struct sm_exc SM_EXC_T;
typedef struct sm_exc_type SM_EXC_TYPE_T;
typedef union sm_val SM_VAL_T;

/*
**  Exception types
*/

extern const char SmExcTypeMagic[];

struct sm_exc_type
{
	const char	*sm_magic;
	const char	*etype_category;
	const char	*etype_argformat;
	void		(*etype_print) __P((SM_EXC_T *, SM_FILE_T *));
	const char	*etype_printcontext;
};

extern const SM_EXC_TYPE_T SmEtypeOs;
extern const SM_EXC_TYPE_T SmEtypeErr;

extern void
sm_etype_printf __P((
	SM_EXC_T *_exc,
	SM_FILE_T *_stream));

/*
**  Exception objects
*/

extern const char SmExcMagic[];

union sm_val
{
	int		v_int;
	long		v_long;
	char		*v_str;
	SM_EXC_T	*v_exc;
};

struct sm_exc
{
	const char		*sm_magic;
	size_t			exc_refcount;
	const SM_EXC_TYPE_T	*exc_type;
	SM_VAL_T		*exc_argv;
};

# define SM_EXC_INITIALIZER(type, argv) \
	{ \
		SmExcMagic, \
		0, \
		type, \
		argv, \
	}

extern SM_EXC_T *
sm_exc_new_x __P((
	const SM_EXC_TYPE_T *_type,
	...));

extern SM_EXC_T *
sm_exc_addref __P((
	SM_EXC_T *_exc));

extern void
sm_exc_free __P((
	SM_EXC_T *_exc));

extern bool
sm_exc_match __P((
	SM_EXC_T *_exc,
	const char *_pattern));

extern void
sm_exc_write __P((
	SM_EXC_T *_exc,
	SM_FILE_T *_stream));

extern void
sm_exc_print __P((
	SM_EXC_T *_exc,
	SM_FILE_T *_stream));

extern SM_DEAD(void
sm_exc_raise_x __P((
	SM_EXC_T *_exc)));

extern SM_DEAD(void
sm_exc_raisenew_x __P((
	const SM_EXC_TYPE_T *_type,
	...)));

/*
**  Exception handling
*/

typedef void (*SM_EXC_DEFAULT_HANDLER_T) __P((SM_EXC_T *));

extern void
sm_exc_newthread __P((
	SM_EXC_DEFAULT_HANDLER_T _handle));

typedef struct sm_exc_handler SM_EXC_HANDLER_T;
struct sm_exc_handler
{
	SM_EXC_T		*eh_value;
	SM_JMPBUF_T		eh_context;
	SM_EXC_HANDLER_T	*eh_parent;
	int			eh_state;
};

/* values for eh_state */
enum
{
	SM_EH_PUSHED = 2,
	SM_EH_POPPED = 0,
	SM_EH_HANDLED = 1
};

extern SM_EXC_HANDLER_T *SmExcHandler;

# define SM_TRY		{ SM_EXC_HANDLER_T _h; \
			  do { \
			    _h.eh_value = NULL; \
			    _h.eh_parent = SmExcHandler; \
			    _h.eh_state = SM_EH_PUSHED; \
			    SmExcHandler = &_h; \
			    if (sm_setjmp_nosig(_h.eh_context) == 0) {

# define SM_FINALLY	      SM_ASSERT(SmExcHandler == &_h); \
			    } \
			    if (sm_setjmp_nosig(_h.eh_context) == 0) {

# define SM_EXCEPT(e,pat)   } \
			    if (_h.eh_state == SM_EH_HANDLED) \
			      break; \
			    if (_h.eh_state == SM_EH_PUSHED) { \
			      SM_ASSERT(SmExcHandler == &_h); \
			      SmExcHandler = _h.eh_parent; \
			    } \
			    _h.eh_state = sm_exc_match(_h.eh_value,pat) \
			      ? SM_EH_HANDLED : SM_EH_POPPED; \
			    if (_h.eh_state == SM_EH_HANDLED) { \
			      SM_UNUSED(SM_EXC_T *e) = _h.eh_value;

# define SM_END_TRY	  } \
			  } while (0); \
			  if (_h.eh_state == SM_EH_PUSHED) { \
			    SM_ASSERT(SmExcHandler == &_h); \
			    SmExcHandler = _h.eh_parent; \
			    if (_h.eh_value != NULL) \
			      sm_exc_raise_x(_h.eh_value); \
			  } else if (_h.eh_state == SM_EH_POPPED) { \
			    if (_h.eh_value != NULL) \
			      sm_exc_raise_x(_h.eh_value); \
			  } else \
			    sm_exc_free(_h.eh_value); \
			}

#endif /* SM_EXC_H */
