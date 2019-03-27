/*
 * Copyright (c) 2000-2001, 2004, 2006 Proofpoint, Inc. and its suppliers.
 *      All rights reserved.
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Chris Torek.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 */

#include <sm/gen.h>
SM_RCSID("@(#)$Id: flags.c,v 1.24 2013-11-22 20:51:42 ca Exp $")
#include <sys/types.h>
#include <sys/file.h>
#include <errno.h>
#include <sm/io.h>
#include "local.h"

/*
**  SM_FLAGS -- translate external (user) flags into internal flags
**
**	Paramters:
**		flags -- user select flags
**
**	Returns:
**		Internal flag value matching user selected flags
*/

int
sm_flags(flags)
	int flags;
{
	int ret;

	switch(SM_IO_MODE(flags))
	{
	  case SM_IO_RDONLY:	/* open for reading */
		ret = SMRD;
		break;

	  case SM_IO_WRONLY:	/* open for writing */
		ret = SMWR;
		break;

	  case SM_IO_APPEND:	/* open for appending */
		ret = SMWR;
		break;

	  case SM_IO_RDWR:	/* open for read and write */
		ret = SMRW;
		break;

	  default:
		ret = 0;
		break;
	}
	if (SM_IS_BINARY(flags))
		ret |= SM_IO_BINARY;
	return ret;
}
