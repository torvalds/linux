/*-
 * Copyright (c) 2001-2003 Networks Associates Technology, Inc.
 * Copyright (c) 2004-2015 Dag-Erling Smørgrav
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by ThinkSec AS and
 * Network Associates Laboratories, the Security Research Division of
 * Network Associates, Inc.  under DARPA/SPAWAR contract N66001-01-C-8035
 * ("CBOSS"), as part of the DARPA CHATS research program.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $OpenPAM: openpam_configure.c 938 2017-04-30 21:34:42Z des $
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <sys/param.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <security/pam_appl.h>

#include "openpam_impl.h"
#include "openpam_ctype.h"
#include "openpam_strlcat.h"
#include "openpam_strlcpy.h"

static int openpam_load_chain(pam_handle_t *, const char *, pam_facility_t);

/*
 * Validate a service name.
 *
 * Returns a non-zero value if the argument points to a NUL-terminated
 * string consisting entirely of characters in the POSIX portable filename
 * character set, excluding the path separator character.
 */
static int
valid_service_name(const char *name)
{
	const char *p;

	if (OPENPAM_FEATURE(RESTRICT_SERVICE_NAME)) {
		/* path separator not allowed */
		for (p = name; *p != '\0'; ++p)
			if (!is_pfcs(*p))
				return (0);
	} else {
		/* path separator allowed */
		for (p = name; *p != '\0'; ++p)
			if (!is_pfcs(*p) && *p != '/')
				return (0);
	}
	return (1);
}

/*
 * Parse the facility name.
 *
 * Returns the corresponding pam_facility_t value, or -1 if the argument
 * is not a valid facility name.
 */
static pam_facility_t
parse_facility_name(const char *name)
{
	int i;

	for (i = 0; i < PAM_NUM_FACILITIES; ++i)
		if (strcmp(pam_facility_name[i], name) == 0)
			return (i);
	return ((pam_facility_t)-1);
}

/*
 * Parse the control flag.
 *
 * Returns the corresponding pam_control_t value, or -1 if the argument is
 * not a valid control flag name.
 */
static pam_control_t
parse_control_flag(const char *name)
{
	int i;

	for (i = 0; i < PAM_NUM_CONTROL_FLAGS; ++i)
		if (strcmp(pam_control_flag_name[i], name) == 0)
			return (i);
	return ((pam_control_t)-1);
}

/*
 * Validate a file name.
 *
 * Returns a non-zero value if the argument points to a NUL-terminated
 * string consisting entirely of characters in the POSIX portable filename
 * character set, including the path separator character.
 */
static int
valid_module_name(const char *name)
{
	const char *p;

	if (OPENPAM_FEATURE(RESTRICT_MODULE_NAME)) {
		/* path separator not allowed */
		for (p = name; *p != '\0'; ++p)
			if (!is_pfcs(*p))
				return (0);
	} else {
		/* path separator allowed */
		for (p = name; *p != '\0'; ++p)
			if (!is_pfcs(*p) && *p != '/')
				return (0);
	}
	return (1);
}

typedef enum { pam_conf_style, pam_d_style } openpam_style_t;

/*
 * Extracts given chains from a policy file.
 *
 * Returns the number of policy entries which were found for the specified
 * service and facility, or -1 if a system error occurred or a syntax
 * error was encountered.
 */
static int
openpam_parse_chain(pam_handle_t *pamh,
	const char *service,
	pam_facility_t facility,
	FILE *f,
	const char *filename,
	openpam_style_t style)
{
	pam_chain_t *this, **next;
	pam_facility_t fclt;
	pam_control_t ctlf;
	char *name, *servicename, *modulename;
	int count, lineno, ret, serrno;
	char **wordv, *word;
	int i, wordc;

	count = 0;
	this = NULL;
	name = NULL;
	lineno = 0;
	wordc = 0;
	wordv = NULL;
	while ((wordv = openpam_readlinev(f, &lineno, &wordc)) != NULL) {
		/* blank line? */
		if (wordc == 0) {
			FREEV(wordc, wordv);
			continue;
		}
		i = 0;

		/* check service name if necessary */
		if (style == pam_conf_style &&
		    strcmp(wordv[i++], service) != 0) {
			FREEV(wordc, wordv);
			continue;
		}

		/* check facility name */
		if ((word = wordv[i++]) == NULL ||
		    (fclt = parse_facility_name(word)) == (pam_facility_t)-1) {
			openpam_log(PAM_LOG_ERROR,
			    "%s(%d): missing or invalid facility",
			    filename, lineno);
			errno = EINVAL;
			goto fail;
		}
		if (facility != fclt && facility != PAM_FACILITY_ANY) {
			FREEV(wordc, wordv);
			continue;
		}

		/* check for "include" */
		if ((word = wordv[i++]) != NULL &&
		    strcmp(word, "include") == 0) {
			if ((servicename = wordv[i++]) == NULL ||
			    !valid_service_name(servicename)) {
				openpam_log(PAM_LOG_ERROR,
				    "%s(%d): missing or invalid service name",
				    filename, lineno);
				errno = EINVAL;
				goto fail;
			}
			if (wordv[i] != NULL) {
				openpam_log(PAM_LOG_ERROR,
				    "%s(%d): garbage at end of line",
				    filename, lineno);
				errno = EINVAL;
				goto fail;
			}
			ret = openpam_load_chain(pamh, servicename, fclt);
			FREEV(wordc, wordv);
			if (ret < 0) {
				/*
				 * Bogus errno, but this ensures that the
				 * outer loop does not just ignore the
				 * error and keep searching.
				 */
				if (errno == ENOENT)
					errno = EINVAL;
				goto fail;
			}
			continue;
		}

		/* get control flag */
		if (word == NULL || /* same word we compared to "include" */
		    (ctlf = parse_control_flag(word)) == (pam_control_t)-1) {
			openpam_log(PAM_LOG_ERROR,
			    "%s(%d): missing or invalid control flag",
			    filename, lineno);
			errno = EINVAL;
			goto fail;
		}

		/* get module name */
		if ((modulename = wordv[i++]) == NULL ||
		    !valid_module_name(modulename)) {
			openpam_log(PAM_LOG_ERROR,
			    "%s(%d): missing or invalid module name",
			    filename, lineno);
			errno = EINVAL;
			goto fail;
		}

		/* allocate new entry */
		if ((this = calloc(1, sizeof *this)) == NULL)
			goto syserr;
		this->flag = ctlf;

		/* load module */
		if ((this->module = openpam_load_module(modulename)) == NULL) {
			if (errno == ENOENT)
				errno = ENOEXEC;
			goto fail;
		}

		/*
		 * The remaining items in wordv are the module's
		 * arguments.  We could set this->optv = wordv + i, but
		 * then free(this->optv) wouldn't work.  Instead, we free
		 * the words we've already consumed, shift the rest up,
		 * and clear the tail end of the array.
		 */
		this->optc = wordc - i;
		for (i = 0; i < wordc - this->optc; ++i) {
			FREE(wordv[i]);
		}
		for (i = 0; i < this->optc; ++i) {
			wordv[i] = wordv[wordc - this->optc + i];
			wordv[wordc - this->optc + i] = NULL;
		}
		this->optv = wordv;
		wordv = NULL;
		wordc = 0;

		/* hook it up */
		for (next = &pamh->chains[fclt]; *next != NULL;
		     next = &(*next)->next)
			/* nothing */ ;
		*next = this;
		this = NULL;
		++count;
	}
	/*
	 * The loop ended because openpam_readword() returned NULL, which
	 * can happen for four different reasons: an I/O error (ferror(f)
	 * is true), a memory allocation failure (ferror(f) is false,
	 * feof(f) is false, errno is non-zero), the file ended with an
	 * unterminated quote or backslash escape (ferror(f) is false,
	 * feof(f) is true, errno is non-zero), or the end of the file was
	 * reached without error (ferror(f) is false, feof(f) is true,
	 * errno is zero).
	 */
	if (ferror(f) || errno != 0)
		goto syserr;
	if (!feof(f))
		goto fail;
	fclose(f);
	return (count);
syserr:
	serrno = errno;
	openpam_log(PAM_LOG_ERROR, "%s: %m", filename);
	errno = serrno;
	/* fall through */
fail:
	serrno = errno;
	if (this && this->optc && this->optv)
		FREEV(this->optc, this->optv);
	FREE(this);
	FREEV(wordc, wordv);
	FREE(wordv);
	FREE(name);
	fclose(f);
	errno = serrno;
	return (-1);
}

/*
 * Read the specified chains from the specified file.
 *
 * Returns 0 if the file exists but does not contain any matching lines.
 *
 * Returns -1 and sets errno to ENOENT if the file does not exist.
 *
 * Returns -1 and sets errno to some other non-zero value if the file
 * exists but is unsafe or unreadable, or an I/O error occurs.
 */
static int
openpam_load_file(pam_handle_t *pamh,
	const char *service,
	pam_facility_t facility,
	const char *filename,
	openpam_style_t style)
{
	FILE *f;
	int ret, serrno;

	/* attempt to open the file */
	if ((f = fopen(filename, "r")) == NULL) {
		serrno = errno;
		openpam_log(errno == ENOENT ? PAM_LOG_DEBUG : PAM_LOG_ERROR,
		    "%s: %m", filename);
		errno = serrno;
		RETURNN(-1);
	} else {
		openpam_log(PAM_LOG_DEBUG, "found %s", filename);
	}

	/* verify type, ownership and permissions */
	if (OPENPAM_FEATURE(VERIFY_POLICY_FILE) &&
	    openpam_check_desc_owner_perms(filename, fileno(f)) != 0) {
		/* already logged the cause */
		serrno = errno;
		fclose(f);
		errno = serrno;
		RETURNN(-1);
	}

	/* parse the file */
	ret = openpam_parse_chain(pamh, service, facility,
	    f, filename, style);
	RETURNN(ret);
}

/*
 * Locates the policy file for a given service and reads the given chains
 * from it.
 *
 * Returns the number of policy entries which were found for the specified
 * service and facility, or -1 if a system error occurred or a syntax
 * error was encountered.
 */
static int
openpam_load_chain(pam_handle_t *pamh,
	const char *service,
	pam_facility_t facility)
{
	const char *p, **path;
	char filename[PATH_MAX];
	size_t len;
	openpam_style_t style;
	int ret;

	ENTERS(facility < 0 ? "any" : pam_facility_name[facility]);

	/* either absolute or relative to cwd */
	if (strchr(service, '/') != NULL) {
		if ((p = strrchr(service, '.')) != NULL && strcmp(p, ".conf") == 0)
			style = pam_conf_style;
		else
			style = pam_d_style;
		ret = openpam_load_file(pamh, service, facility,
		    service, style);
		RETURNN(ret);
	}

	/* search standard locations */
	for (path = openpam_policy_path; *path != NULL; ++path) {
		/* construct filename */
		len = strlcpy(filename, *path, sizeof filename);
		if (len >= sizeof filename) {
			errno = ENAMETOOLONG;
			RETURNN(-1);
		}
		if (filename[len - 1] == '/') {
			len = strlcat(filename, service, sizeof filename);
			if (len >= sizeof filename) {
				errno = ENAMETOOLONG;
				RETURNN(-1);
			}
			style = pam_d_style;
		} else {
			style = pam_conf_style;
		}
		ret = openpam_load_file(pamh, service, facility,
		    filename, style);
		/* success */
		if (ret > 0)
			RETURNN(ret);
		/* the file exists, but an error occurred */
		if (ret == -1 && errno != ENOENT)
			RETURNN(ret);
		/* in pam.d style, an empty file counts as a hit */
		if (ret == 0 && style == pam_d_style)
			RETURNN(ret);
	}

	/* no hit */
	errno = ENOENT;
	RETURNN(-1);
}

/*
 * OpenPAM internal
 *
 * Configure a service
 */

int
openpam_configure(pam_handle_t *pamh,
	const char *service)
{
	pam_facility_t fclt;
	int serrno;

	ENTERS(service);
	if (!valid_service_name(service)) {
		openpam_log(PAM_LOG_ERROR, "invalid service name");
		RETURNC(PAM_SYSTEM_ERR);
	}
	if (openpam_load_chain(pamh, service, PAM_FACILITY_ANY) < 0) {
		if (errno != ENOENT)
			goto load_err;
	}
	for (fclt = 0; fclt < PAM_NUM_FACILITIES; ++fclt) {
		if (pamh->chains[fclt] != NULL)
			continue;
		if (OPENPAM_FEATURE(FALLBACK_TO_OTHER)) {
			if (openpam_load_chain(pamh, PAM_OTHER, fclt) < 0)
				goto load_err;
		}
	}
	RETURNC(PAM_SUCCESS);
load_err:
	serrno = errno;
	openpam_clear_chains(pamh->chains);
	errno = serrno;
	RETURNC(PAM_SYSTEM_ERR);
}

/*
 * NODOC
 *
 * Error codes:
 *	PAM_SYSTEM_ERR
 */
