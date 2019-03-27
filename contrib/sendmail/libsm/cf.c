/*
 * Copyright (c) 2001 Proofpoint, Inc. and its suppliers.
 *      All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 */

#include <sm/gen.h>
SM_RCSID("@(#)$Id: cf.c,v 1.8 2013-11-22 20:51:42 ca Exp $")

#include <ctype.h>
#include <errno.h>

#include <sm/cf.h>
#include <sm/io.h>
#include <sm/string.h>
#include <sm/heap.h>

/*
**  SM_CF_GETOPT -- look up option values in the sendmail.cf file
**
**	Open the sendmail.cf file and parse all of the 'O' directives.
**	Each time one of the options named in the option vector optv
**	is found, store a malloced copy of the option value in optv.
**
**	Parameters:
**		path -- pathname of sendmail.cf file
**		optc -- size of option vector
**		optv -- pointer to option vector
**
**	Results:
**		0 on success, or an errno value on failure.
**		An exception is raised on malloc failure.
*/

int
sm_cf_getopt(path, optc, optv)
	char *path;
	int optc;
	SM_CF_OPT_T *optv;
{
	SM_FILE_T *cfp;
	char buf[2048];
	char *p;
	char *id;
	char *idend;
	char *val;
	int i;

	cfp = sm_io_open(SmFtStdio, SM_TIME_DEFAULT, path, SM_IO_RDONLY, NULL);
	if (cfp == NULL)
		return errno;

	while (sm_io_fgets(cfp, SM_TIME_DEFAULT, buf, sizeof(buf)) >= 0)
	{
		p = strchr(buf, '\n');
		if (p != NULL)
			*p = '\0';

		if (buf[0] != 'O' || buf[1] != ' ')
			continue;

		id = &buf[2];
		val = strchr(id, '=');
		if (val == NULL)
			val = idend = id + strlen(id);
		else
		{
			idend = val;
			++val;
			while (*val == ' ')
				++val;
			while (idend > id && idend[-1] == ' ')
				--idend;
			*idend = '\0';
		}

		for (i = 0; i < optc; ++i)
		{
			if (sm_strcasecmp(optv[i].opt_name, id) == 0)
			{
				optv[i].opt_val = sm_strdup_x(val);
				break;
			}
		}
	}
	if (sm_io_error(cfp))
	{
		int save_errno = errno;

		(void) sm_io_close(cfp, SM_TIME_DEFAULT);
		errno = save_errno;
		return errno;
	}
	(void) sm_io_close(cfp, SM_TIME_DEFAULT);
	return 0;
}
