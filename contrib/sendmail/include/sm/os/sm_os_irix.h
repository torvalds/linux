/*
 * Copyright (c) 2000-2001 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 *	$Id: sm_os_irix.h,v 1.8 2013-11-22 20:51:34 ca Exp $
 */

/*
**  Silicon Graphics IRIX
**
**	Compiles on 4.0.1.
**
**	Use IRIX64 instead of IRIX for 64-bit IRIX (6.0).
**	Use IRIX5 instead of IRIX for IRIX 5.x.
**
**	This version tries to be adaptive using _MIPS_SIM:
**		_MIPS_SIM == _ABIO32 (= 1)    Abi: -32 on IRIX 6.2
**		_MIPS_SIM == _ABIN32 (= 2)    Abi: -n32 on IRIX 6.2
**		_MIPS_SIM == _ABI64  (= 3)    Abi: -64 on IRIX 6.2
**
**		_MIPS_SIM is 1 also on IRIX 5.3
**
**	IRIX64 changes from Mark R. Levinson <ml@cvdev.rochester.edu>.
**	IRIX5 changes from Kari E. Hurtta <Kari.Hurtta@fmi.fi>.
**	Adaptive changes from Kari E. Hurtta <Kari.Hurtta@fmi.fi>.
*/

#ifndef IRIX
# define IRIX
#endif /* ! IRIX */
#if _MIPS_SIM > 0 && !defined(IRIX5)
# define IRIX5			/* IRIX5 or IRIX6 */
#endif /* _MIPS_SIM > 0 && !defined(IRIX5) */
#if _MIPS_SIM > 1 && !defined(IRIX6) && !defined(IRIX64)
# define IRIX6			/* IRIX6 */
#endif /* _MIPS_SIM > 1 && !defined(IRIX6) && !defined(IRIX64) */

#define SM_OS_NAME	"irix"

#if defined(IRIX6) || defined(IRIX64)
# define SM_CONF_LONGLONG	1
#endif /* defined(IRIX6) || defined(IRIX64) */

#if defined(IRIX64) || defined(IRIX5) || defined(IRIX6)
# define SM_CONF_SYS_CDEFS_H	1
#endif /* defined(IRIX64) || defined(IRIX5) || defined(IRIX6) */

/* try LLONG tests in libsm/t-types.c? */
#ifndef SM_CONF_TEST_LLONG
# define SM_CONF_TEST_LLONG	0
#endif /* !SM_CONF_TEST_LLONG */
