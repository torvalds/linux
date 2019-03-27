/*-
 * Copyright (c) 2004, 2009 Apple Inc.
 * Copyright (c) 2006, 2016 Robert N. M. Watson
 * All rights reserved.
 *
 * Portions of this software were developed by BAE Systems, the University of
 * Cambridge Computer Laboratory, and Memorial University under DARPA/AFRL
 * contract FA8650-15-C-7558 ("CADETS"), as part of the DARPA Transparent
 * Computing (TC) research program.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <config/config.h>

#include <bsm/libbsm.h>

#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#ifdef HAVE_PTHREAD_MUTEX_LOCK
#include <pthread.h>
#endif
#include <stdio.h>
#include <stdlib.h>

#ifndef HAVE_STRLCAT
#include <compat/strlcat.h>
#endif
#ifndef HAVE_STRLCPY
#include <compat/strlcpy.h>
#endif

#include <sys/stat.h>

/*
 * Parse the contents of the audit_control file to return the audit control
 * parameters.  These static fields are protected by 'mutex'.
 */
static FILE	*fp = NULL;
static char	linestr[AU_LINE_MAX];
static char	*delim = ":";

static char	inacdir = 0;
static char	ptrmoved = 0;

#ifdef HAVE_PTHREAD_MUTEX_LOCK
static pthread_mutex_t	mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

/*
 * Audit policy string token table for au_poltostr() and au_strtopol().
 */
struct audit_polstr {
	long		 ap_policy;
	const char	*ap_str;
};

static struct audit_polstr au_polstr[] = {
	{ AUDIT_CNT,		"cnt"		},
	{ AUDIT_AHLT,		"ahlt"		},
	{ AUDIT_ARGV,		"argv"		},
	{ AUDIT_ARGE,		"arge"		},
	{ AUDIT_SEQ,		"seq"		},
	{ AUDIT_WINDATA,	"windata"	},
	{ AUDIT_USER,		"user"		},
	{ AUDIT_GROUP,		"group"		},
	{ AUDIT_TRAIL,		"trail"		},
	{ AUDIT_PATH,		"path"		},
	{ AUDIT_SCNT,		"scnt"		},
	{ AUDIT_PUBLIC,		"public"	},
	{ AUDIT_ZONENAME,	"zonename"	},
	{ AUDIT_PERZONE,	"perzone"	},
	{ -1,			NULL		}
};

/*
 * Returns the string value corresponding to the given label from the
 * configuration file.
 *
 * Must be called with mutex held.
 */
static int
getstrfromtype_locked(const char *name, char **str)
{
	char *type, *nl;
	char *tokptr;
	char *last;

	*str = NULL;

	if ((fp == NULL) && ((fp = fopen(AUDIT_CONTROL_FILE, "r")) == NULL))
		return (-1); /* Error */

	while (1) {
		if (fgets(linestr, AU_LINE_MAX, fp) == NULL) {
			if (ferror(fp))
				return (-1);
			return (0);	/* EOF */
		}

		if (linestr[0] == '#')
			continue;

		/* Remove trailing new line character and white space. */
		nl = strchr(linestr, '\0') - 1;
		while (nl >= linestr && ('\n' == *nl || ' ' == *nl ||
			'\t' == *nl)) {
			*nl = '\0';
			nl--;
		}

		tokptr = linestr;
		if ((type = strtok_r(tokptr, delim, &last)) != NULL) {
			if (strcmp(name, type) == 0) {
				/* Found matching name. */
				*str = last;
				return (0); /* Success */
			}
		}
	}
}

/*
 * Convert a given time value with a multiplier (seconds, hours, days, years) to
 * seconds.  Return 0 on success.
 */
static int
au_timetosec(time_t *seconds, u_long value, char mult)
{
	if (NULL == seconds)
		return (-1);

	switch(mult) {
	case 's':
		/* seconds */
		*seconds = (time_t)value;
		break;

	case 'h':
		/* hours */
		*seconds = (time_t)value * 60 * 60;
		break;

	case 'd':
		/* days */
		*seconds = (time_t)value * 60 * 60 * 24;
		break;

	case 'y':
		/* years.  Add a day for each 4th (leap) year. */
		*seconds = (time_t)value * 60 * 60 * 24 * 364 +
		    ((time_t)value / 4) * 60 * 60 * 24;
		break;

	default:
		return (-1);
	}
	return (0);
}

/*
 * Convert a given disk space value with a multiplier (bytes, kilobytes,
 * megabytes, gigabytes) to bytes.  Return 0 on success.
 */
static int
au_spacetobytes(size_t *bytes, u_long value, char mult)
{
	if (NULL == bytes)
		return (-1);

	switch(mult) {
	case 'B':
	case ' ':
		/* Bytes */
		*bytes = (size_t)value;
		break;

	case 'K':
		/* Kilobytes */
		*bytes = (size_t)value * 1024;
		break;

	case 'M':
		/* Megabytes */
		*bytes = (size_t)value * 1024 * 1024;
		break;

	case 'G':
		/* Gigabytes */
		*bytes = (size_t)value * 1024 * 1024 * 1024;
		break;

	default:
		return (-1);
	}
	return (0);
}

/*
 * Convert a policy to a string.  Return -1 on failure, or >= 0 representing
 * the actual size of the string placed in the buffer (excluding terminating
 * nul).
 */
ssize_t
au_poltostr(int policy, size_t maxsize, char *buf)
{
	int first = 1;
	int i = 0;

	if (maxsize < 1)
		return (-1);
	buf[0] = '\0';

	do {
		if (policy & au_polstr[i].ap_policy) {
			if (!first && strlcat(buf, ",", maxsize) >= maxsize)
				return (-1);
			if (strlcat(buf, au_polstr[i].ap_str, maxsize) >=
			    maxsize)
				return (-1);
			first = 0;
		}
	} while (NULL != au_polstr[++i].ap_str);

	return (strlen(buf));
}

/*
 * Convert a string to a policy.  Return -1 on failure (with errno EINVAL,
 * ENOMEM) or 0 on success.
 */
int
au_strtopol(const char *polstr, int *policy)
{
	char *bufp, *string;
	char *buffer;
	int i, matched;

	*policy = 0;
	buffer = strdup(polstr);
	if (buffer == NULL)
		return (-1);

	bufp = buffer;
	while ((string = strsep(&bufp, ",")) != NULL) {
		matched = i = 0;

		do {
			if (strcmp(string, au_polstr[i].ap_str) == 0) {
				*policy |= au_polstr[i].ap_policy;
				matched = 1;
				break;
			}
		} while (NULL != au_polstr[++i].ap_str);

		if (!matched) {
			free(buffer);
			errno = EINVAL;
			return (-1);
		}
	}
	free(buffer);
	return (0);
}

/*
 * Rewind the file pointer to beginning.
 */
static void
setac_locked(void)
{
	static time_t lastctime = 0;
	struct stat sbuf;

	ptrmoved = 1;
	if (fp != NULL) {
		/*
		 * Check to see if the file on disk has changed.  If so,
		 * force a re-read of the file by closing it.
		 */
		if (fstat(fileno(fp), &sbuf) < 0)
			goto closefp;
		if (lastctime != sbuf.st_ctime) {
			lastctime = sbuf.st_ctime;
closefp:
			fclose(fp);
			fp = NULL;
			return;
		}

		fseek(fp, 0, SEEK_SET);
	}
}

void
setac(void)
{

#ifdef HAVE_PTHREAD_MUTEX_LOCK
	pthread_mutex_lock(&mutex);
#endif
	setac_locked();
#ifdef HAVE_PTHREAD_MUTEX_LOCK
	pthread_mutex_unlock(&mutex);
#endif
}

/*
 * Close the audit_control file.
 */
void
endac(void)
{

#ifdef HAVE_PTHREAD_MUTEX_LOCK
	pthread_mutex_lock(&mutex);
#endif
	ptrmoved = 1;
	if (fp != NULL) {
		fclose(fp);
		fp = NULL;
	}
#ifdef HAVE_PTHREAD_MUTEX_LOCK
	pthread_mutex_unlock(&mutex);
#endif
}

/*
 * Return audit directory information from the audit control file.
 */
int
getacdir(char *name, int len)
{
	char *dir;
	int ret = 0;

	/*
	 * Check if another function was called between successive calls to
	 * getacdir.
	 */
#ifdef HAVE_PTHREAD_MUTEX_LOCK
	pthread_mutex_lock(&mutex);
#endif
	if (inacdir && ptrmoved) {
		ptrmoved = 0;
		if (fp != NULL)
			fseek(fp, 0, SEEK_SET);
		ret = 2;
	}
	if (getstrfromtype_locked(DIR_CONTROL_ENTRY, &dir) < 0) {
#ifdef HAVE_PTHREAD_MUTEX_LOCK
		pthread_mutex_unlock(&mutex);
#endif
		return (-2);
	}
	if (dir == NULL) {
#ifdef HAVE_PTHREAD_MUTEX_LOCK
		pthread_mutex_unlock(&mutex);
#endif
		return (-1);
	}
	if (strlen(dir) >= (size_t)len) {
#ifdef HAVE_PTHREAD_MUTEX_LOCK
		pthread_mutex_unlock(&mutex);
#endif
		return (-3);
	}
	strlcpy(name, dir, len);
#ifdef HAVE_PTHREAD_MUTEX_LOCK
	pthread_mutex_unlock(&mutex);
#endif
	return (ret);
}

/*
 * Return 1 if dist value is set to 'yes' or 'on'.
 * Return 0 if dist value is set to something else.
 * Return negative value on error.
 */
int
getacdist(void)
{
	char *str;
	int ret;

#ifdef HAVE_PTHREAD_MUTEX_LOCK
	pthread_mutex_lock(&mutex);
#endif
	setac_locked();
	if (getstrfromtype_locked(DIST_CONTROL_ENTRY, &str) < 0) {
#ifdef HAVE_PTHREAD_MUTEX_LOCK
		pthread_mutex_unlock(&mutex);
#endif
		return (-2);
	}
	if (str == NULL) {
#ifdef HAVE_PTHREAD_MUTEX_LOCK
		pthread_mutex_unlock(&mutex);
#endif
		return (0);
	}
	if (strcasecmp(str, "on") == 0 || strcasecmp(str, "yes") == 0)
		ret = 1;
	else
		ret = 0;
#ifdef HAVE_PTHREAD_MUTEX_LOCK
	pthread_mutex_unlock(&mutex);
#endif
	return (ret);
}

/*
 * Return the minimum free diskspace value from the audit control file.
 */
int
getacmin(int *min_val)
{
	char *min;

#ifdef HAVE_PTHREAD_MUTEX_LOCK
	pthread_mutex_lock(&mutex);
#endif
	setac_locked();
	if (getstrfromtype_locked(MINFREE_CONTROL_ENTRY, &min) < 0) {
#ifdef HAVE_PTHREAD_MUTEX_LOCK
		pthread_mutex_unlock(&mutex);
#endif
		return (-2);
	}
	if (min == NULL) {
#ifdef HAVE_PTHREAD_MUTEX_LOCK
		pthread_mutex_unlock(&mutex);
#endif
		return (-1);
	}
	*min_val = atoi(min);
#ifdef HAVE_PTHREAD_MUTEX_LOCK
	pthread_mutex_unlock(&mutex);
#endif
	return (0);
}

/*
 * Return the desired trail rotation size from the audit control file.
 */
int
getacfilesz(size_t *filesz_val)
{
	char *str;
	size_t val;
	char mult;
	int nparsed;

#ifdef HAVE_PTHREAD_MUTEX_LOCK
	pthread_mutex_lock(&mutex);
#endif
	setac_locked();
	if (getstrfromtype_locked(FILESZ_CONTROL_ENTRY, &str) < 0) {
#ifdef HAVE_PTHREAD_MUTEX_LOCK
		pthread_mutex_unlock(&mutex);
#endif
		return (-2);
	}
	if (str == NULL) {
#ifdef HAVE_PTHREAD_MUTEX_LOCK
		pthread_mutex_unlock(&mutex);
#endif
		errno = EINVAL;
		return (-1);
	}

	/* Trim off any leading white space. */
	while (*str == ' ' || *str == '\t')
		str++;

	nparsed = sscanf(str, "%ju%c", (uintmax_t *)&val, &mult);

	switch (nparsed) {
	case 1:
		/* If no multiplier then assume 'B' (bytes). */
		mult = 'B';
		/* fall through */
	case 2:
		if (au_spacetobytes(filesz_val, val, mult) == 0)
			break;
		/* fall through */
	default:
		errno = EINVAL;
#ifdef HAVE_PTHREAD_MUTEX_LOCK
		pthread_mutex_unlock(&mutex);
#endif
		return (-1);
	}

	/*
	 * The file size must either be 0 or >= MIN_AUDIT_FILE_SIZE.  0
	 * indicates no rotation size.
	 */
	if (*filesz_val < 0 || (*filesz_val > 0 &&
		*filesz_val < MIN_AUDIT_FILE_SIZE)) {
#ifdef HAVE_PTHREAD_MUTEX_LOCK
		pthread_mutex_unlock(&mutex);
#endif
		filesz_val = 0L;
		errno = EINVAL;
		return (-1);
	}
#ifdef HAVE_PTHREAD_MUTEX_LOCK
	pthread_mutex_unlock(&mutex);
#endif
	return (0);
}

static int
getaccommon(const char *name, char *auditstr, int len)
{
	char *str;

#ifdef HAVE_PTHREAD_MUTEX_LOCK
	pthread_mutex_lock(&mutex);
#endif
	setac_locked();
	if (getstrfromtype_locked(name, &str) < 0) {
#ifdef HAVE_PTHREAD_MUTEX_LOCK
		pthread_mutex_unlock(&mutex);
#endif
		return (-2);
	}

	/*
	 * getstrfromtype_locked() can return NULL for an empty value -- make
	 * sure to handle this by coercing the NULL back into an empty string.
	 */
	if (str != NULL && (strlen(str) >= (size_t)len)) {
#ifdef HAVE_PTHREAD_MUTEX_LOCK
		pthread_mutex_unlock(&mutex);
#endif
		return (-3);
	}
	strlcpy(auditstr, str != NULL ? str : "", len);
#ifdef HAVE_PTHREAD_MUTEX_LOCK
	pthread_mutex_unlock(&mutex);
#endif
	return (0);
}

/*
 * Return the system audit value from the audit contol file.
 */
int
getacflg(char *auditstr, int len)
{

	return (getaccommon(FLAGS_CONTROL_ENTRY, auditstr, len));
}

/*
 * Return the non attributable flags from the audit contol file.
 */
int
getacna(char *auditstr, int len)
{

	return (getaccommon(NA_CONTROL_ENTRY, auditstr, len));
}

/*
 * Return the policy field from the audit control file.
 */
int
getacpol(char *auditstr, size_t len)
{

	return (getaccommon(POLICY_CONTROL_ENTRY, auditstr, len));
}

int
getachost(char *auditstr, size_t len)
{

	return (getaccommon(HOST_CONTROL_ENTRY, auditstr, len));
}

/*
 * Set expiration conditions.
 */
static int
setexpirecond(time_t *age, size_t *size, u_long value, char mult)
{

	if (isupper(mult) || ' ' == mult)
		return (au_spacetobytes(size, value, mult));
	else
		return (au_timetosec(age, value, mult));
}

/*
 * Return the expire-after field from the audit control file.
 */
int
getacexpire(int *andflg, time_t *age, size_t *size)
{
	char *str;
	int nparsed;
	u_long val1, val2;
	char mult1, mult2;
	char andor[AU_LINE_MAX];

	*age = 0L;
	*size = 0LL;
	*andflg = 0;

#ifdef HAVE_PTHREAD_MUTEX_LOCK
	pthread_mutex_lock(&mutex);
#endif
	setac_locked();
	if (getstrfromtype_locked(EXPIRE_AFTER_CONTROL_ENTRY, &str) < 0) {
#ifdef HAVE_PTHREAD_MUTEX_LOCK
		pthread_mutex_unlock(&mutex);
#endif
		return (-2);
	}
	if (str == NULL) {
#ifdef HAVE_PTHREAD_MUTEX_LOCK
		pthread_mutex_unlock(&mutex);
#endif
		return (-1);
	}

	/* First, trim off any leading white space. */
	while (*str == ' ' || *str == '\t')
		str++;

	nparsed = sscanf(str, "%lu%c%[ \tadnorADNOR]%lu%c", &val1, &mult1,
	    andor, &val2, &mult2);

	switch (nparsed) {
	case 1:
		/* If no multiplier then assume 'B' (Bytes). */
		mult1 = 'B';
		/* fall through */
	case 2:
		/* One expiration condition. */
		if (setexpirecond(age, size, val1, mult1) != 0) {
#ifdef HAVE_PTHREAD_MUTEX_LOCK
			pthread_mutex_unlock(&mutex);
#endif
			return (-1);
		}
		break;

	case 5:
		/* Two expiration conditions. */
		if (setexpirecond(age, size, val1, mult1) != 0 ||
		    setexpirecond(age, size, val2, mult2) != 0) {
#ifdef HAVE_PTHREAD_MUTEX_LOCK
			pthread_mutex_unlock(&mutex);
#endif
			return (-1);
		}
		if (strcasestr(andor, "and") != NULL)
			*andflg = 1;
		else if (strcasestr(andor, "or") != NULL)
			*andflg = 0;
		else {
#ifdef HAVE_PTHREAD_MUTEX_LOCK
			pthread_mutex_unlock(&mutex);
#endif
			return (-1);
		}
		break;

	default:
#ifdef HAVE_PTHREAD_MUTEX_LOCK
		pthread_mutex_unlock(&mutex);
#endif
		return (-1);
	}

#ifdef HAVE_PTHREAD_MUTEX_LOCK
	pthread_mutex_unlock(&mutex);
#endif
	return (0);
}
/*
 * Return the desired queue size from the audit control file.
 */
int
getacqsize(int *qsz_val)
{
	char *str;
	int nparsed;

#ifdef HAVE_PTHREAD_MUTEX_LOCK
	pthread_mutex_lock(&mutex);
#endif
	setac_locked();
	if (getstrfromtype_locked(QSZ_CONTROL_ENTRY, &str) < 0) {
#ifdef HAVE_PTHREAD_MUTEX_LOCK
		pthread_mutex_unlock(&mutex);
#endif
		return (-2);
	}
	if (str == NULL) {
#ifdef HAVE_PTHREAD_MUTEX_LOCK
		pthread_mutex_unlock(&mutex);
#endif
		*qsz_val = USE_DEFAULT_QSZ;
		return (0);
	}

	/* Trim off any leading white space. */
	while (*str == ' ' || *str == '\t')
		str++;

	nparsed = sscanf(str, "%d", (int *)qsz_val);

	if (nparsed != 1) {
		errno = EINVAL;
#ifdef HAVE_PTHREAD_MUTEX_LOCK
		pthread_mutex_unlock(&mutex);
#endif
		return (-1);
	}

	/* The queue size must either be 0 or < AQ_MAXHIGH */
	if (*qsz_val < 0 || *qsz_val > AQ_MAXHIGH) {
#ifdef HAVE_PTHREAD_MUTEX_LOCK
		pthread_mutex_unlock(&mutex);
#endif
		qsz_val = 0L;
		errno = EINVAL;
		return (-1);
	}
#ifdef HAVE_PTHREAD_MUTEX_LOCK
	pthread_mutex_unlock(&mutex);
#endif
	return (0);
}
