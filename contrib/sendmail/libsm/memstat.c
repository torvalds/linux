/*
 * Copyright (c) 2005-2007 Proofpoint, Inc. and its suppliers.
 *      All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 */

#include <sm/gen.h>
SM_RCSID("@(#)$Id: memstat.c,v 1.7 2013-11-22 20:51:43 ca Exp $")

#include <errno.h>
#include <sm/misc.h>

#if USESWAPCTL
#include <sys/stat.h>
#include <sys/swap.h>

static long sc_page_size;

/*
**  SM_MEMSTAT_OPEN -- open memory statistics
**
**	Parameters:
**		none
**
**	Results:
**		errno as error code, 0: ok
*/

int
sm_memstat_open()
{
	sc_page_size = sysconf(_SC_PAGE_SIZE);
	if (sc_page_size == -1)
		return (errno != 0) ? errno : -1;
	return 0;
}

/*
**  SM_MEMSTAT_CLOSE -- close memory statistics
**
**	Parameters:
**		none
**
**	Results:
**		errno as error code, 0: ok
*/

int
sm_memstat_close()
{
	return 0;
}

/*
**  SM_MEMSTAT_GET -- get memory statistics
**
**	Parameters:
**		resource -- resource to look up
**		pvalue -- (pointer to) memory statistics value (output)
**
**	Results:
**		0: success
**		!=0: error
*/

int
sm_memstat_get(resource, pvalue)
	char *resource;
	long *pvalue;
{
	int r;
	struct anoninfo ai;

	r = swapctl(SC_AINFO, &ai);
	if (r == -1)
		return (errno != 0) ? errno : -1;
	r = ai.ani_max - ai.ani_resv;
	r *= sc_page_size >> 10;
   	*pvalue = r;
	return 0;
}

#elif USEKSTAT

#include <kstat.h>
#include <sys/sysinfo.h>

static kstat_ctl_t *kc;
static kstat_t *kst;

/*
**  SM_MEMSTAT_OPEN -- open memory statistics
**
**	Parameters:
**		none
**
**	Results:
**		errno as error code, 0: ok
*/

int
sm_memstat_open()
{
	kstat_named_t *kn;

	kc = kstat_open();
	if (kc == NULL)
		return (errno != 0) ? errno : -1;
	kst = kstat_lookup(kc, "unix", 0,
		(name != NULL) ? name : "system_pages");
	if (kst == 0)
		return (errno != 0) ? errno : -2;
	return 0;
}

/*
**  SM_MEMSTAT_CLOSE -- close memory statistics
**
**	Parameters:
**		none
**
**	Results:
**		errno as error code, 0: ok
*/

int
sm_memstat_close()
{
	int r;

	if (kc == NULL)
		return 0;
	r = kstat_close(kc);
	if (r != 0)
		return (errno != 0) ? errno : -1;
	return 0;
}

/*
**  SM_MEMSTAT_GET -- get memory statistics
**
**	Parameters:
**		resource -- resource to look up
**		pvalue -- (pointer to) memory statistics value (output)
**
**	Results:
**		0: success
**		!=0: error
*/

int
sm_memstat_get(resource, pvalue)
	char *resource;
	long *pvalue;
{
	int r;
	kstat_named_t *kn;

	if (kc == NULL || kst == NULL)
		return -1;
	if (kstat_read(kc, kst, NULL) == -1)
		return (errno != 0) ? errno : -2;
	kn = kstat_data_lookup(kst,
			(resource != NULL) ? resource: "freemem");
	if (kn == NULL)
		return (errno != 0) ? errno : -3;
   	*pvalue = kn->value.ul;
	return 0;
}

#elif USEPROCMEMINFO

/*
/proc/meminfo?
        total:    used:    free:  shared: buffers:  cached:
Mem:  261468160 252149760  9318400        0  3854336 109813760
Swap: 1052794880 62185472 990609408
MemTotal:       255340 kB
MemFree:          9100 kB
MemShared:           0 kB
Buffers:          3764 kB
Cached:         107240 kB
Active:         104340 kB
Inact_dirty:      4220 kB
Inact_clean:      2444 kB
Inact_target:     4092 kB
HighTotal:           0 kB
HighFree:            0 kB
LowTotal:       255340 kB
LowFree:          9100 kB
SwapTotal:     1028120 kB
SwapFree:       967392 kB
*/

#include <stdio.h>
#include <string.h>
static FILE *fp;

/*
**  SM_MEMSTAT_OPEN -- open memory statistics
**
**	Parameters:
**		none
**
**	Results:
**		errno as error code, 0: ok
*/

int
sm_memstat_open()
{
	fp = fopen("/proc/meminfo", "r");
	return (fp != NULL) ? 0 : errno;
}

/*
**  SM_MEMSTAT_CLOSE -- close memory statistics
**
**	Parameters:
**		none
**
**	Results:
**		errno as error code, 0: ok
*/

int
sm_memstat_close()
{
	if (fp != NULL)
	{
		fclose(fp);
		fp = NULL;
	}
	return 0;
}

/*
**  SM_MEMSTAT_GET -- get memory statistics
**
**	Parameters:
**		resource -- resource to look up
**		pvalue -- (pointer to) memory statistics value (output)
**
**	Results:
**		0: success
**		!=0: error
*/

int
sm_memstat_get(resource, pvalue)
	char *resource;
	long *pvalue;
{
	int r;
	size_t l;
	char buf[80];

	if (resource == NULL)
		return EINVAL;
	if (pvalue == NULL)
		return EINVAL;
	if (fp == NULL)
		return -1;	/* try to reopen? */
	rewind(fp);
	l = strlen(resource);
	if (l >= sizeof(buf))
		return EINVAL;
	while (fgets(buf, sizeof(buf), fp) != NULL)
	{
		if (strncmp(buf, resource, l) == 0 && buf[l] == ':')
		{
			r = sscanf(buf + l + 1, "%ld", pvalue);
			return (r > 0) ? 0 : -1;
		}
	}
	return 0;
}

#else /* USEPROCMEMINFO */

/*
**  SM_MEMSTAT_OPEN -- open memory statistics
**
**	Parameters:
**		none
**
**	Results:
**		errno as error code, 0: ok
*/

int
sm_memstat_open()
{
	return -1;
}

/*
**  SM_MEMSTAT_CLOSE -- close memory statistics
**
**	Parameters:
**		none
**
**	Results:
**		errno as error code, 0: ok
*/

int
sm_memstat_close()
{
	return 0;
}

/*
**  SM_MEMSTAT_GET -- get memory statistics
**
**	Parameters:
**		resource -- resource to look up
**		pvalue -- (pointer to) memory statistics value (output)
**
**	Results:
**		0: success
**		!=0: error
*/

int
sm_memstat_get(resource, pvalue)
	char *resource;
	long *pvalue;
{
	return -1;
}

#endif /* USEKSTAT */
