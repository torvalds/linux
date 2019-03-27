
/***********************************************************************
 * Copyright (c) 2009, Secure Endpoints Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 **********************************************************************/

#include "krb5_locl.h"

typedef int PTYPE;

#ifdef _WIN32
#include <shlobj.h>
#include <sddl.h>

/*
 * Expand a %{TEMP} token
 *
 * The %{TEMP} token expands to the temporary path for the current
 * user as returned by GetTempPath().
 *
 * @note: Since the GetTempPath() function relies on the TMP or TEMP
 * environment variables, this function will failover to the system
 * temporary directory until the user profile is loaded.  In addition,
 * the returned path may or may not exist.
 */
static int
_expand_temp_folder(krb5_context context, PTYPE param, const char *postfix, char **ret)
{
    TCHAR tpath[MAX_PATH];
    size_t len;

    if (!GetTempPath(sizeof(tpath)/sizeof(tpath[0]), tpath)) {
	if (context)
	    krb5_set_error_message(context, EINVAL,
				   "Failed to get temporary path (GLE=%d)",
				   GetLastError());
	return EINVAL;
    }

    len = strlen(tpath);

    if (len > 0 && tpath[len - 1] == '\\')
	tpath[len - 1] = '\0';

    *ret = strdup(tpath);

    if (*ret == NULL) {
	if (context)
	    krb5_set_error_message(context, ENOMEM, "strdup - Out of memory");
	return ENOMEM;
    }

    return 0;
}

extern HINSTANCE _krb5_hInstance;

/*
 * Expand a %{BINDIR} token
 *
 * This is also used to expand a few other tokens on Windows, since
 * most of the executable binaries end up in the same directory.  The
 * "bin" directory is considered to be the directory in which the
 * krb5.dll is located.
 */
static int
_expand_bin_dir(krb5_context context, PTYPE param, const char *postfix, char **ret)
{
    TCHAR path[MAX_PATH];
    TCHAR *lastSlash;
    DWORD nc;

    nc = GetModuleFileName(_krb5_hInstance, path, sizeof(path)/sizeof(path[0]));
    if (nc == 0 ||
	nc == sizeof(path)/sizeof(path[0])) {
	return EINVAL;
    }

    lastSlash = strrchr(path, '\\');
    if (lastSlash != NULL) {
	TCHAR *fslash = strrchr(lastSlash, '/');

	if (fslash != NULL)
	    lastSlash = fslash;

	*lastSlash = '\0';
    }

    if (postfix) {
	if (strlcat(path, postfix, sizeof(path)/sizeof(path[0])) >= sizeof(path)/sizeof(path[0]))
	    return EINVAL;
    }

    *ret = strdup(path);
    if (*ret == NULL)
	return ENOMEM;

    return 0;
}

/*
 *  Expand a %{USERID} token
 *
 *  The %{USERID} token expands to the string representation of the
 *  user's SID.  The user account that will be used is the account
 *  corresponding to the current thread's security token.  This means
 *  that:
 *
 *  - If the current thread token has the anonymous impersonation
 *    level, the call will fail.
 *
 *  - If the current thread is impersonating a token at
 *    SecurityIdentification level the call will fail.
 *
 */
static int
_expand_userid(krb5_context context, PTYPE param, const char *postfix, char **ret)
{
    int rv = EINVAL;
    HANDLE hThread = NULL;
    HANDLE hToken = NULL;
    PTOKEN_OWNER pOwner = NULL;
    DWORD len = 0;
    LPTSTR strSid = NULL;

    hThread = GetCurrentThread();

    if (!OpenThreadToken(hThread, TOKEN_QUERY,
			 FALSE,	/* Open the thread token as the
				   current thread user. */
			 &hToken)) {

	DWORD le = GetLastError();

	if (le == ERROR_NO_TOKEN) {
	    HANDLE hProcess = GetCurrentProcess();

	    le = 0;
	    if (!OpenProcessToken(hProcess, TOKEN_QUERY, &hToken))
		le = GetLastError();
	}

	if (le != 0) {
	    if (context)
		krb5_set_error_message(context, rv,
				       "Can't open thread token (GLE=%d)", le);
	    goto _exit;
	}
    }

    if (!GetTokenInformation(hToken, TokenOwner, NULL, 0, &len)) {
	if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
	    if (context)
		krb5_set_error_message(context, rv,
				       "Unexpected error reading token information (GLE=%d)",
				       GetLastError());
	    goto _exit;
	}

	if (len == 0) {
	    if (context)
		krb5_set_error_message(context, rv,
				      "GetTokenInformation() returned truncated buffer");
	    goto _exit;
	}

	pOwner = malloc(len);
	if (pOwner == NULL) {
	    if (context)
		krb5_set_error_message(context, rv, "Out of memory");
	    goto _exit;
	}
    } else {
	if (context)
	    krb5_set_error_message(context, rv, "GetTokenInformation() returned truncated buffer");
	goto _exit;
    }

    if (!GetTokenInformation(hToken, TokenOwner, pOwner, len, &len)) {
	if (context)
	    krb5_set_error_message(context, rv, "GetTokenInformation() failed. GLE=%d", GetLastError());
	goto _exit;
    }

    if (!ConvertSidToStringSid(pOwner->Owner, &strSid)) {
	if (context)
	    krb5_set_error_message(context, rv, "Can't convert SID to string. GLE=%d", GetLastError());
	goto _exit;
    }

    *ret = strdup(strSid);
    if (*ret == NULL && context)
	krb5_set_error_message(context, rv, "Out of memory");

    rv = 0;

 _exit:
    if (hToken != NULL)
	CloseHandle(hToken);

    if (pOwner != NULL)
	free (pOwner);

    if (strSid != NULL)
	LocalFree(strSid);

    return rv;
}

/*
 * Expand a folder identified by a CSIDL
 */

static int
_expand_csidl(krb5_context context, PTYPE folder, const char *postfix, char **ret)
{
    TCHAR path[MAX_PATH];
    size_t len;

    if (SHGetFolderPath(NULL, folder, NULL, SHGFP_TYPE_CURRENT, path) != S_OK) {
	if (context)
	    krb5_set_error_message(context, EINVAL, "Unable to determine folder path");
	return EINVAL;
    }

    len = strlen(path);

    if (len > 0 && path[len - 1] == '\\')
	path[len - 1] = '\0';

    if (postfix &&
	strlcat(path, postfix, sizeof(path)/sizeof(path[0])) >= sizeof(path)/sizeof(path[0])) {
	return ENOMEM;
    }

    *ret = strdup(path);
    if (*ret == NULL) {
	if (context)
	    krb5_set_error_message(context, ENOMEM, "Out of memory");
	return ENOMEM;
    }
    return 0;
}

#else

static int
_expand_path(krb5_context context, PTYPE param, const char *postfix, char **ret)
{
    *ret = strdup(postfix);
    if (*ret == NULL) {
	krb5_set_error_message(context, ENOMEM, "malloc - out of memory");
	return ENOMEM;
    }
    return 0;
}

static int
_expand_temp_folder(krb5_context context, PTYPE param, const char *postfix, char **ret)
{
    const char *p = NULL;

    if (issuid())
	p = getenv("TEMP");
    if (p)
	*ret = strdup(p);
    else
	*ret = strdup("/tmp");
    if (*ret == NULL)
	return ENOMEM;
    return 0;
}

static int
_expand_userid(krb5_context context, PTYPE param, const char *postfix, char **str)
{
    int ret = asprintf(str, "%ld", (unsigned long)getuid());
    if (ret < 0 || *str == NULL)
	return ENOMEM;
    return 0;
}


#endif /* _WIN32 */

/**
 * Expand a %{null} token
 *
 * The expansion of a %{null} token is always the empty string.
 */

static int
_expand_null(krb5_context context, PTYPE param, const char *postfix, char **ret)
{
    *ret = strdup("");
    if (*ret == NULL) {
	if (context)
	    krb5_set_error_message(context, ENOMEM, "Out of memory");
	return ENOMEM;
    }
    return 0;
}


static const struct token {
    const char * tok;
    int ftype;
#define FTYPE_CSIDL 0
#define FTYPE_SPECIAL 1

    PTYPE param;
    const char * postfix;

    int (*exp_func)(krb5_context, PTYPE, const char *, char **);

#define SPECIALP(f, P) FTYPE_SPECIAL, 0, P, f
#define SPECIAL(f) SPECIALP(f, NULL)

} tokens[] = {
#ifdef _WIN32
#define CSIDLP(C,P) FTYPE_CSIDL, C, P, _expand_csidl
#define CSIDL(C) CSIDLP(C, NULL)

    {"APPDATA", CSIDL(CSIDL_APPDATA)}, /* Roaming application data (for current user) */
    {"COMMON_APPDATA", CSIDL(CSIDL_COMMON_APPDATA)}, /* Application data (all users) */
    {"LOCAL_APPDATA", CSIDL(CSIDL_LOCAL_APPDATA)}, /* Local application data (for current user) */
    {"SYSTEM", CSIDL(CSIDL_SYSTEM)}, /* Windows System folder (e.g. %WINDIR%\System32) */
    {"WINDOWS", CSIDL(CSIDL_WINDOWS)}, /* Windows folder */
    {"USERCONFIG", CSIDLP(CSIDL_APPDATA, "\\" PACKAGE)}, /* Per user Heimdal configuration file path */
    {"COMMONCONFIG", CSIDLP(CSIDL_COMMON_APPDATA, "\\" PACKAGE)}, /* Common Heimdal configuration file path */
    {"LIBDIR", SPECIAL(_expand_bin_dir)},
    {"BINDIR", SPECIAL(_expand_bin_dir)},
    {"LIBEXEC", SPECIAL(_expand_bin_dir)},
    {"SBINDIR", SPECIAL(_expand_bin_dir)},
#else
    {"LIBDIR", FTYPE_SPECIAL, 0, LIBDIR, _expand_path},
    {"BINDIR", FTYPE_SPECIAL, 0, BINDIR, _expand_path},
    {"LIBEXEC", FTYPE_SPECIAL, 0, LIBEXECDIR, _expand_path},
    {"SBINDIR", FTYPE_SPECIAL, 0, SBINDIR, _expand_path},
#endif
    {"TEMP", SPECIAL(_expand_temp_folder)},
    {"USERID", SPECIAL(_expand_userid)},
    {"uid", SPECIAL(_expand_userid)},
    {"null", SPECIAL(_expand_null)}
};

static int
_expand_token(krb5_context context,
	      const char *token,
	      const char *token_end,
	      char **ret)
{
    size_t i;

    *ret = NULL;

    if (token[0] != '%' || token[1] != '{' || token_end[0] != '}' ||
	token_end - token <= 2) {
	if (context)
	    krb5_set_error_message(context, EINVAL,"Invalid token.");
	return EINVAL;
    }

    for (i = 0; i < sizeof(tokens)/sizeof(tokens[0]); i++) {
	if (!strncmp(token+2, tokens[i].tok, (token_end - token) - 2))
	    return tokens[i].exp_func(context, tokens[i].param,
				      tokens[i].postfix, ret);
    }

    if (context)
	krb5_set_error_message(context, EINVAL, "Invalid token.");
    return EINVAL;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
_krb5_expand_path_tokens(krb5_context context,
			 const char *path_in,
			 char **ppath_out)
{
    char *tok_begin, *tok_end, *append;
    const char *path_left;
    size_t len = 0;

    if (path_in == NULL || *path_in == '\0') {
        *ppath_out = strdup("");
        return 0;
    }

    *ppath_out = NULL;

    for (path_left = path_in; path_left && *path_left; ) {

	tok_begin = strstr(path_left, "%{");

	if (tok_begin && tok_begin != path_left) {

	    append = malloc((tok_begin - path_left) + 1);
	    if (append) {
		memcpy(append, path_left, tok_begin - path_left);
		append[tok_begin - path_left] = '\0';
	    }
	    path_left = tok_begin;

	} else if (tok_begin) {

	    tok_end = strchr(tok_begin, '}');
	    if (tok_end == NULL) {
		if (*ppath_out)
		    free(*ppath_out);
		*ppath_out = NULL;
		if (context)
		    krb5_set_error_message(context, EINVAL, "variable missing }");
		return EINVAL;
	    }

	    if (_expand_token(context, tok_begin, tok_end, &append)) {
		if (*ppath_out)
		    free(*ppath_out);
		*ppath_out = NULL;
		return EINVAL;
	    }

	    path_left = tok_end + 1;
	} else {

	    append = strdup(path_left);
	    path_left = NULL;

	}

	if (append == NULL) {

	    if (*ppath_out)
		free(*ppath_out);
	    *ppath_out = NULL;
	    if (context)
		krb5_set_error_message(context, ENOMEM, "malloc - out of memory");
	    return ENOMEM;

	}

	{
	    size_t append_len = strlen(append);
	    char * new_str = realloc(*ppath_out, len + append_len + 1);

	    if (new_str == NULL) {
		free(append);
		if (*ppath_out)
		    free(*ppath_out);
		*ppath_out = NULL;
		if (context)
		    krb5_set_error_message(context, ENOMEM, "malloc - out of memory");
		return ENOMEM;
	    }

	    *ppath_out = new_str;
	    memcpy(*ppath_out + len, append, append_len + 1);
	    len = len + append_len;
	    free(append);
	}
    }

#ifdef _WIN32
    /* Also deal with slashes */
    if (*ppath_out) {
	char * c;
	for (c = *ppath_out; *c; c++)
	    if (*c == '/')
		*c = '\\';
    }
#endif

    return 0;
}
