/*
 * Copyright (c) 2000-2001 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 *	$Id: setjmp.h,v 1.4 2013-11-22 20:51:31 ca Exp $
 */

#ifndef SM_SETJMP_H
# define SM_SETJMP_H

# include <sm/config.h>
# include <setjmp.h>

/*
**  sm_setjmp_sig is a setjmp that saves the signal mask.
**  sm_setjmp_nosig is a setjmp that does *not* save the signal mask.
**  SM_JMPBUF_T is used with both of the above macros.
**
**  On most systems, these can be implemented using sigsetjmp.
**  Some old BSD systems do not have sigsetjmp, but they do have
**  setjmp and _setjmp, which are just as good.
*/

# if SM_CONF_SIGSETJMP

typedef sigjmp_buf SM_JMPBUF_T;
#  define sm_setjmp_sig(buf)		sigsetjmp(buf, 1)
#  define sm_setjmp_nosig(buf)		sigsetjmp(buf, 0)
#  define sm_longjmp_sig(buf, val)	siglongjmp(buf, val)
#  define sm_longjmp_nosig(buf, val)	siglongjmp(buf, val)

# else /* SM_CONF_SIGSETJMP */

typedef jmp_buf SM_JMPBUF_T;
#  define sm_setjmp_sig(buf)		setjmp(buf)
#  define sm_longjmp_sig(buf, val)	longjmp(buf, val)
#   define sm_setjmp_nosig(buf)		_setjmp(buf)
#   define sm_longjmp_nosig(buf, val)	_longjmp(buf, val)

# endif /* SM_CONF_SIGSETJMP */

#endif /* ! SM_SETJMP_H */
