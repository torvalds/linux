/*
 * Copyright (c) 1997 - 2005 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "krb5_locl.h"
#include <dirent.h>

#ifndef _WIN32

/* see if principal is mentioned in the filename access file, return
   TRUE (in result) if so, FALSE otherwise */

static krb5_error_code
check_one_file(krb5_context context,
	       const char *filename,
	       struct passwd *pwd,
	       krb5_principal principal,
	       krb5_boolean *result)
{
    FILE *f;
    char buf[BUFSIZ];
    krb5_error_code ret;
    struct stat st;

    *result = FALSE;

    f = fopen (filename, "r");
    if (f == NULL)
	return errno;
    rk_cloexec_file(f);

    /* check type and mode of file */
    if (fstat(fileno(f), &st) != 0) {
	fclose (f);
	return errno;
    }
    if (S_ISDIR(st.st_mode)) {
	fclose (f);
	return EISDIR;
    }
    if (st.st_uid != pwd->pw_uid && st.st_uid != 0) {
	fclose (f);
	return EACCES;
    }
    if ((st.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
	fclose (f);
	return EACCES;
    }

    while (fgets (buf, sizeof(buf), f) != NULL) {
	krb5_principal tmp;
	char *newline = buf + strcspn(buf, "\n");

	if(*newline != '\n') {
	    int c;
	    c = fgetc(f);
	    if(c != EOF) {
		while(c != EOF && c != '\n')
		    c = fgetc(f);
		/* line was too long, so ignore it */
		continue;
	    }
	}
	*newline = '\0';
	ret = krb5_parse_name (context, buf, &tmp);
	if (ret)
	    continue;
	*result = krb5_principal_compare (context, principal, tmp);
	krb5_free_principal (context, tmp);
	if (*result) {
	    fclose (f);
	    return 0;
	}
    }
    fclose (f);
    return 0;
}

static krb5_error_code
check_directory(krb5_context context,
		const char *dirname,
		struct passwd *pwd,
		krb5_principal principal,
		krb5_boolean *result)
{
    DIR *d;
    struct dirent *dent;
    char filename[MAXPATHLEN];
    krb5_error_code ret = 0;
    struct stat st;

    *result = FALSE;

    if(lstat(dirname, &st) < 0)
	return errno;

    if (!S_ISDIR(st.st_mode))
	return ENOTDIR;

    if (st.st_uid != pwd->pw_uid && st.st_uid != 0)
	return EACCES;
    if ((st.st_mode & (S_IWGRP | S_IWOTH)) != 0)
	return EACCES;

    if((d = opendir(dirname)) == NULL)
	return errno;

    {
	int fd;
	struct stat st2;

	fd = dirfd(d);
	if(fstat(fd, &st2) < 0) {
	    closedir(d);
	    return errno;
	}
	if(st.st_dev != st2.st_dev || st.st_ino != st2.st_ino) {
	    closedir(d);
	    return EACCES;
	}
    }

    while((dent = readdir(d)) != NULL) {
	if(strcmp(dent->d_name, ".") == 0 ||
	   strcmp(dent->d_name, "..") == 0 ||
	   dent->d_name[0] == '#' ||			  /* emacs autosave */
	   dent->d_name[strlen(dent->d_name) - 1] == '~') /* emacs backup */
	    continue;
	snprintf(filename, sizeof(filename), "%s/%s", dirname, dent->d_name);
	ret = check_one_file(context, filename, pwd, principal, result);
	if(ret == 0 && *result == TRUE)
	    break;
	ret = 0; /* don't propagate errors upstream */
    }
    closedir(d);
    return ret;
}

#endif  /* !_WIN32 */

static krb5_boolean
match_local_principals(krb5_context context,
		       krb5_principal principal,
		       const char *luser)
{
    krb5_error_code ret;
    krb5_realm *realms, *r;
    krb5_boolean result = FALSE;

    /* multi-component principals can never match */
    if(krb5_principal_get_comp_string(context, principal, 1) != NULL)
	return FALSE;

    ret = krb5_get_default_realms (context, &realms);
    if (ret)
	return FALSE;

    for (r = realms; *r != NULL; ++r) {
	if(strcmp(krb5_principal_get_realm(context, principal),
		  *r) != 0)
	    continue;
	if(strcmp(krb5_principal_get_comp_string(context, principal, 0),
		  luser) == 0) {
	    result = TRUE;
	    break;
	}
    }
    krb5_free_host_realm (context, realms);
    return result;
}

/**
 * This function takes the name of a local user and checks if
 * principal is allowed to log in as that user.
 *
 * The user may have a ~/.k5login file listing principals that are
 * allowed to login as that user. If that file does not exist, all
 * principals with a first component identical to the username, and a
 * realm considered local, are allowed access.
 *
 * The .k5login file must contain one principal per line, be owned by
 * user and not be writable by group or other (but must be readable by
 * anyone).
 *
 * Note that if the file exists, no implicit access rights are given
 * to user@@LOCALREALM.
 *
 * Optionally, a set of files may be put in ~/.k5login.d (a
 * directory), in which case they will all be checked in the same
 * manner as .k5login.  The files may be called anything, but files
 * starting with a hash (#) , or ending with a tilde (~) are
 * ignored. Subdirectories are not traversed. Note that this directory
 * may not be checked by other Kerberos implementations.
 *
 * If no configuration file exists, match user against local domains,
 * ie luser@@LOCAL-REALMS-IN-CONFIGURATION-FILES.
 *
 * @param context Kerberos 5 context.
 * @param principal principal to check if allowed to login
 * @param luser local user id
 *
 * @return returns TRUE if access should be granted, FALSE otherwise.
 *
 * @ingroup krb5_support
 */

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_kuserok (krb5_context context,
	      krb5_principal principal,
	      const char *luser)
{
#ifndef _WIN32
    char *buf;
    size_t buflen;
    struct passwd *pwd = NULL;
    char *profile_dir = NULL;
    krb5_error_code ret;
    krb5_boolean result = FALSE;

    krb5_boolean found_file = FALSE;

#ifdef POSIX_GETPWNAM_R
    char pwbuf[2048];
    struct passwd pw;

    if(getpwnam_r(luser, &pw, pwbuf, sizeof(pwbuf), &pwd) != 0)
	return FALSE;
#else
    pwd = getpwnam (luser);
#endif
    if (pwd == NULL)
	return FALSE;
    profile_dir = pwd->pw_dir;

#define KLOGIN "/.k5login"
    buflen = strlen(profile_dir) + sizeof(KLOGIN) + 2; /* 2 for .d */
    buf = malloc(buflen);
    if(buf == NULL)
	return FALSE;
    /* check user's ~/.k5login */
    strlcpy(buf, profile_dir, buflen);
    strlcat(buf, KLOGIN, buflen);
    ret = check_one_file(context, buf, pwd, principal, &result);

    if(ret == 0 && result == TRUE) {
	free(buf);
	return TRUE;
    }

    if(ret != ENOENT)
	found_file = TRUE;

    strlcat(buf, ".d", buflen);
    ret = check_directory(context, buf, pwd, principal, &result);
    free(buf);
    if(ret == 0 && result == TRUE)
	return TRUE;

    if(ret != ENOENT && ret != ENOTDIR)
	found_file = TRUE;

    /* finally if no files exist, allow all principals matching
       <localuser>@<LOCALREALM> */
    if(found_file == FALSE)
	return match_local_principals(context, principal, luser);

    return FALSE;
#else
    /* The .k5login file may be on a remote profile and we don't have
       access to the profile until we have a token handle for the
       user's credentials. */
    return match_local_principals(context, principal, luser);
#endif
}
