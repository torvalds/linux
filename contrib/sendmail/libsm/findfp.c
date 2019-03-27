/*
 * Copyright (c) 2000-2002, 2006 Proofpoint, Inc. and its suppliers.
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
SM_RCSID("@(#)$Id: findfp.c,v 1.68 2013-11-22 20:51:42 ca Exp $")
#include <stdlib.h>
#include <unistd.h>
#include <sys/param.h>
#include <errno.h>
#include <string.h>
#include <syslog.h>
#include <sm/io.h>
#include <sm/assert.h>
#include <sm/heap.h>
#include <sm/string.h>
#include <sm/conf.h>
#include "local.h"
#include "glue.h"

bool	Sm_IO_DidInit;	/* IO system has been initialized? */

const char SmFileMagic[] = "sm_file";

/* An open type to map to fopen()-like behavior */
SM_FILE_T SmFtStdio_def =
    {SmFileMagic, 0, 0, 0, (SMRW|SMFBF), -1, {0, 0}, 0, 0, 0,
	sm_stdclose, sm_stdread, sm_stdseek, sm_stdwrite,
	sm_stdopen, sm_stdsetinfo, sm_stdgetinfo, SM_TIME_FOREVER,
	SM_TIME_BLOCK, "stdio" };

/* An open type to map to fdopen()-like behavior */
SM_FILE_T SmFtStdiofd_def =
    {SmFileMagic, 0, 0, 0, (SMRW|SMFBF), -1, {0, 0}, 0, 0, 0,
	sm_stdclose, sm_stdread, sm_stdseek, sm_stdwrite,
	sm_stdfdopen, sm_stdsetinfo, sm_stdgetinfo, SM_TIME_FOREVER,
	SM_TIME_BLOCK, "stdiofd" };

/* A string file type */
SM_FILE_T SmFtString_def =
    {SmFileMagic, 0, 0, 0, (SMRW|SMNBF), -1, {0, 0}, 0, 0, 0,
	sm_strclose, sm_strread, sm_strseek, sm_strwrite,
	sm_stropen, sm_strsetinfo, sm_strgetinfo, SM_TIME_FOREVER,
	SM_TIME_BLOCK, "string" };

#if 0
/* A file type for syslog communications */
SM_FILE_T SmFtSyslog_def =
    {SmFileMagic, 0, 0, 0, (SMRW|SMNBF), -1, {0, 0}, 0, 0, 0,
	sm_syslogclose, sm_syslogread, sm_syslogseek, sm_syslogwrite,
	sm_syslogopen, sm_syslogsetinfo, sm_sysloggetinfo, SM_TIME_FOREVER,
	SM_TIME_BLOCK, "syslog" };
#endif /* 0 */

#define NDYNAMIC 10		/* add ten more whenever necessary */

#define smio(flags, file, name)						\
    {SmFileMagic, 0, 0, 0, flags, file, {0}, 0, SmIoF+file, 0,		\
	sm_stdclose, sm_stdread, sm_stdseek, sm_stdwrite,		\
	sm_stdopen, sm_stdsetinfo, sm_stdgetinfo, SM_TIME_FOREVER,	\
	SM_TIME_BLOCK, name}

/* sm_magic p r w flags file bf lbfsize cookie ival */
#define smstd(flags, file, name)					\
    {SmFileMagic, 0, 0, 0, flags, -1, {0}, 0, 0, file,			\
	sm_stdioclose, sm_stdioread, sm_stdioseek, sm_stdiowrite,	\
	sm_stdioopen, sm_stdiosetinfo, sm_stdiogetinfo, SM_TIME_FOREVER,\
	SM_TIME_BLOCK, name}

/* A file type for interfacing to stdio FILE* streams. */
SM_FILE_T SmFtRealStdio_def = smstd(SMRW|SMNBF, -1, "realstdio");

				/* the usual - (stdin + stdout + stderr) */
static SM_FILE_T usual[SM_IO_OPEN_MAX - 3];
static struct sm_glue smuglue = { 0, SM_IO_OPEN_MAX - 3, usual };

/* List of builtin automagically already open file pointers */
SM_FILE_T SmIoF[6] =
{
	smio(SMRD|SMLBF, SMIOIN_FILENO, "smioin"),	/* smioin */
	smio(SMWR|SMLBF, SMIOOUT_FILENO, "smioout"),	/* smioout */
	smio(SMWR|SMNBF, SMIOERR_FILENO, "smioerr"),	/* smioerr */
	smstd(SMRD|SMNBF, SMIOIN_FILENO, "smiostdin"),	/* smiostdin */
	smstd(SMWR|SMNBF, SMIOOUT_FILENO, "smiostdout"),/* smiostdout */
	smstd(SMWR|SMNBF, SMIOERR_FILENO, "smiostderr") /* smiostderr */
};

/* Structure containing list of currently open file pointers */
struct sm_glue smglue = { &smuglue, 3, SmIoF };

/*
**  SM_MOREGLUE -- adds more space for open file pointers
**
**	Parameters:
**		n -- number of new spaces for file pointers
**
**	Returns:
**		Raises an exception if no more memory.
**		Otherwise, returns a pointer to new spaces.
*/

static struct sm_glue *sm_moreglue_x __P((int));
static SM_FILE_T empty;

static struct sm_glue *
sm_moreglue_x(n)
	register int n;
{
	register struct sm_glue *g;
	register SM_FILE_T *p;

	g = (struct sm_glue *) sm_pmalloc_x(sizeof(*g) + SM_ALIGN_BITS +
					    n * sizeof(SM_FILE_T));
	p = (SM_FILE_T *) SM_ALIGN(g + 1);
	g->gl_next = NULL;
	g->gl_niobs = n;
	g->gl_iobs = p;
	while (--n >= 0)
		*p++ = empty;
	return g;
}

/*
**  SM_FP -- allocate and initialize an SM_FILE structure
**
**	Parameters:
**		t -- file type requested to be opened.
**		flags -- control flags for file type behavior
**		oldfp -- file pointer to reuse if available (optional)
**
**	Returns:
**		Raises exception on memory exhaustion.
**		Aborts if type is invalid.
**		Otherwise, returns file pointer for requested file type.
*/

SM_FILE_T *
sm_fp(t, flags, oldfp)
	const SM_FILE_T *t;
	const int flags;
	SM_FILE_T *oldfp;
{
	register SM_FILE_T *fp;
	register int n;
	register struct sm_glue *g;

	SM_REQUIRE(t->f_open && t->f_close && (t->f_read || t->f_write));

	if (!Sm_IO_DidInit)
		sm_init();

	if (oldfp != NULL)
	{
		fp = oldfp;
		goto found; /* for opening reusing an 'fp' */
	}

	for (g = &smglue;; g = g->gl_next)
	{
		for (fp = g->gl_iobs, n = g->gl_niobs; --n >= 0; fp++)
			if (fp->sm_magic == NULL)
				goto found;
		if (g->gl_next == NULL)
			g->gl_next = sm_moreglue_x(NDYNAMIC);
	}
found:
	fp->sm_magic = SmFileMagic; /* 'fp' now valid and in-use */
	fp->f_p = NULL;		/* no current pointer */
	fp->f_w = 0;		/* nothing to write */
	fp->f_r = 0;		/* nothing to read */
	fp->f_flags = flags;
	fp->f_file = -1;		/* no file */
	fp->f_bf.smb_base = NULL;	/* no buffer */
	fp->f_bf.smb_size = 0;	/* no buffer size with no buffer */
	fp->f_lbfsize = 0;	/* not line buffered */
	fp->f_flushfp = NULL;	/* no associated flush file */

	fp->f_cookie = fp;	/* default: *open* overrides cookie setting */
	fp->f_close = t->f_close;	/* assign close function */
	fp->f_read = t->f_read;		/* assign read function */
	fp->f_seek = t->f_seek;		/* assign seek function */
	fp->f_write = t->f_write;	/* assign write function */
	fp->f_open = t->f_open;		/* assign open function */
	fp->f_setinfo = t->f_setinfo;	/* assign setinfo function */
	fp->f_getinfo = t->f_getinfo;	/* assign getinfo function */
	fp->f_type = t->f_type;		/* file type */

	fp->f_ub.smb_base = NULL;	/* no ungetc buffer */
	fp->f_ub.smb_size = 0;		/* no size for no ungetc buffer */

	if (fp->f_timeout == SM_TIME_DEFAULT)
		fp->f_timeout = SM_TIME_FOREVER;
	else
		fp->f_timeout = t->f_timeout; /* traditional behavior */
	fp->f_timeoutstate = SM_TIME_BLOCK; /* by default */

	return fp;
}

/*
**  SM_CLEANUP -- cleanup function when exit called.
**
**	This function is registered via atexit().
**
**	Parameters:
**		none
**
**	Returns:
**		nothing.
**
**	Side Effects:
**		flushes open files before they are forced closed
*/

void
sm_cleanup()
{
	int timeout = SM_TIME_DEFAULT;

	(void) sm_fwalk(sm_flush, &timeout); /* `cheating' */
}

/*
**  SM_INIT -- called whenever sm_io's internal variables must be set up.
**
**	Parameters:
**		none
**
**	Returns:
**		none
**
**	Side Effects:
**		Registers sm_cleanup() using atexit().
*/

void
sm_init()
{
	if (Sm_IO_DidInit)	/* paranoia */
		return;

	/* more paranoia: initialize pointers in a static variable */
	empty.f_type = NULL;
	empty.sm_magic = NULL;

	/* make sure we clean up on exit */
	atexit(sm_cleanup);		/* conservative */
	Sm_IO_DidInit = true;
}

/*
**  SM_IO_SETINFO -- change info for an open file type (fp)
**
**	The generic SM_IO_WHAT_VECTORS is auto supplied for all file types.
**	If the request is to set info other than SM_IO_WHAT_VECTORS then
**	the request is passed on to the file type's specific setinfo vector.
**	WARNING: this is working on an active/open file type.
**
**	Parameters:
**		fp -- file to make the setting on
**		what -- type of information to set
**		valp -- structure to obtain info from
**
**	Returns:
**		0 on success
**		-1 on error and sets errno:
**			- when what != SM_IO_WHAT_VECTORS and setinfo vector
**				not set
**			- when vectored setinfo returns -1
*/

int
sm_io_setinfo(fp, what, valp)
	SM_FILE_T *fp;
	int what;
	void *valp;
{
	SM_FILE_T *v = (SM_FILE_T *) valp;

	SM_REQUIRE_ISA(fp, SmFileMagic);
	switch (what)
	{
	  case SM_IO_WHAT_VECTORS:

		/*
		**  This is the "generic" available for all.
		**  This allows the function vectors to be replaced
		**  while the file type is active.
		*/

		fp->f_close = v->f_close;
		fp->f_read = v->f_read;
		fp->f_seek = v->f_seek;
		fp->f_write = v->f_write;
		fp->f_open = v->f_open;
		fp->f_setinfo = v->f_setinfo;
		fp->f_getinfo = v->f_getinfo;
		sm_free(fp->f_type);
		fp->f_type = sm_strdup_x(v->f_type);
		return 0;
	  case SM_IO_WHAT_TIMEOUT:
		fp->f_timeout = *((int *)valp);
		return 0;
	}

	/* Otherwise the vector will check it out */
	if (fp->f_setinfo == NULL)
	{
		errno = EINVAL;
		return -1;
	}
	else
		return (*fp->f_setinfo)(fp, what, valp);
}

/*
**  SM_IO_GETINFO -- get information for an active file type (fp)
**
**  This function supplies for all file types the answers for the
**		three requests SM_IO_WHAT_VECTORS, SM_IO_WHAT_TYPE and
**		SM_IO_WHAT_ISTYPE. Other requests are handled by the getinfo
**		vector if available for the open file type.
**	SM_IO_WHAT_VECTORS returns information for the file pointer vectors.
**	SM_IO_WHAT_TYPE returns the type identifier for the file pointer
**	SM_IO_WHAT_ISTYPE returns >0 if the passed in type matches the
**		file pointer's type.
**	SM_IO_IS_READABLE returns 1 if there is data available for reading,
**		0 otherwise.
**
**	Parameters:
**		fp -- file pointer for active file type
**		what -- type of information request
**		valp -- structure to place obtained info into
**
**	Returns:
**		-1 on error and sets errno:
**			- when valp==NULL and request expects otherwise
**			- when request is not SM_IO_WHAT_VECTORS and not
**				SM_IO_WHAT_TYPE and not SM_IO_WHAT_ISTYPE
**				and getinfo vector is NULL
**			- when getinfo type vector returns -1
**		>=0 on success
*/

int
sm_io_getinfo(fp, what, valp)
	SM_FILE_T *fp;
	int what;
	void *valp;
{
	SM_FILE_T *v = (SM_FILE_T *) valp;

	SM_REQUIRE_ISA(fp, SmFileMagic);

	switch (what)
	{
	  case SM_IO_WHAT_VECTORS:
		if (valp == NULL)
		{
			errno = EINVAL;
			return -1;
		}

		/* This is the "generic" available for all */
		v->f_close = fp->f_close;
		v->f_read = fp->f_read;
		v->f_seek = fp->f_seek;
		v->f_write = fp->f_write;
		v->f_open = fp->f_open;
		v->f_setinfo = fp->f_setinfo;
		v->f_getinfo = fp->f_getinfo;
		v->f_type = fp->f_type;
		return 0;

	  case SM_IO_WHAT_TYPE:
		if (valp == NULL)
		{
			errno = EINVAL;
			return -1;
		}
		valp = sm_strdup_x(fp->f_type);
		return 0;

	  case SM_IO_WHAT_ISTYPE:
		if (valp == NULL)
		{
			errno = EINVAL;
			return -1;
		}
		return strcmp(fp->f_type, valp) == 0;

	  case SM_IO_IS_READABLE:

		/* if there is data in the buffer, it must be readable */
		if (fp->f_r > 0)
			return 1;

		/* otherwise query the underlying file */
		break;

	   case SM_IO_WHAT_TIMEOUT:
		*((int *) valp) = fp->f_timeout;
		return 0;

	  case SM_IO_WHAT_FD:
		if (fp->f_file > -1)
			return fp->f_file;

		/* try the file type specific getinfo to see if it knows */
		break;
	}

	/* Otherwise the vector will check it out */
	if (fp->f_getinfo == NULL)
	{
		errno = EINVAL;
		return -1;
	}
	return (*fp->f_getinfo)(fp, what, valp);
}
