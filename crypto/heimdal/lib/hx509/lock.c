/*
 * Copyright (c) 2005 - 2006 Kungliga Tekniska Högskolan
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

#include "hx_locl.h"

/**
 * @page page_lock Locking and unlocking certificates and encrypted data.
 *
 * See the library functions here: @ref hx509_lock
 */

struct hx509_lock_data {
    struct _hx509_password password;
    hx509_certs certs;
    hx509_prompter_fct prompt;
    void *prompt_data;
};

static struct hx509_lock_data empty_lock_data = {
    { 0, NULL }
};

hx509_lock _hx509_empty_lock = &empty_lock_data;

/*
 *
 */

int
hx509_lock_init(hx509_context context, hx509_lock *lock)
{
    hx509_lock l;
    int ret;

    *lock = NULL;

    l = calloc(1, sizeof(*l));
    if (l == NULL)
	return ENOMEM;

    ret = hx509_certs_init(context,
			   "MEMORY:locks-internal",
			   0,
			   NULL,
			   &l->certs);
    if (ret) {
	free(l);
	return ret;
    }

    *lock = l;

    return 0;
}

int
hx509_lock_add_password(hx509_lock lock, const char *password)
{
    void *d;
    char *s;

    s = strdup(password);
    if (s == NULL)
	return ENOMEM;

    d = realloc(lock->password.val,
		(lock->password.len + 1) * sizeof(lock->password.val[0]));
    if (d == NULL) {
	free(s);
	return ENOMEM;
    }
    lock->password.val = d;
    lock->password.val[lock->password.len] = s;
    lock->password.len++;

    return 0;
}

const struct _hx509_password *
_hx509_lock_get_passwords(hx509_lock lock)
{
    return &lock->password;
}

hx509_certs
_hx509_lock_unlock_certs(hx509_lock lock)
{
    return lock->certs;
}

void
hx509_lock_reset_passwords(hx509_lock lock)
{
    size_t i;
    for (i = 0; i < lock->password.len; i++)
	free(lock->password.val[i]);
    free(lock->password.val);
    lock->password.val = NULL;
    lock->password.len = 0;
}

int
hx509_lock_add_cert(hx509_context context, hx509_lock lock, hx509_cert cert)
{
    return hx509_certs_add(context, lock->certs, cert);
}

int
hx509_lock_add_certs(hx509_context context, hx509_lock lock, hx509_certs certs)
{
    return hx509_certs_merge(context, lock->certs, certs);
}

void
hx509_lock_reset_certs(hx509_context context, hx509_lock lock)
{
    hx509_certs certs = lock->certs;
    int ret;

    ret = hx509_certs_init(context,
			   "MEMORY:locks-internal",
			   0,
			   NULL,
			   &lock->certs);
    if (ret == 0)
	hx509_certs_free(&certs);
    else
	lock->certs = certs;
}

int
_hx509_lock_find_cert(hx509_lock lock, const hx509_query *q, hx509_cert *c)
{
    *c = NULL;
    return 0;
}

int
hx509_lock_set_prompter(hx509_lock lock, hx509_prompter_fct prompt, void *data)
{
    lock->prompt = prompt;
    lock->prompt_data = data;
    return 0;
}

void
hx509_lock_reset_promper(hx509_lock lock)
{
    lock->prompt = NULL;
    lock->prompt_data = NULL;
}

static int
default_prompter(void *data, const hx509_prompt *prompter)
{
    if (hx509_prompt_hidden(prompter->type)) {
	if(UI_UTIL_read_pw_string(prompter->reply.data,
				  prompter->reply.length,
				  prompter->prompt,
				  0))
	    return 1;
    } else {
	char *s = prompter->reply.data;

	fputs (prompter->prompt, stdout);
	fflush (stdout);
	if(fgets(prompter->reply.data,
		 prompter->reply.length,
		 stdin) == NULL)
	    return 1;
	s[strcspn(s, "\n")] = '\0';
    }
    return 0;
}

int
hx509_lock_prompt(hx509_lock lock, hx509_prompt *prompt)
{
    if (lock->prompt == NULL)
	return HX509_CRYPTO_NO_PROMPTER;
    return (*lock->prompt)(lock->prompt_data, prompt);
}

void
hx509_lock_free(hx509_lock lock)
{
    if (lock) {
	hx509_certs_free(&lock->certs);
	hx509_lock_reset_passwords(lock);
	memset(lock, 0, sizeof(*lock));
	free(lock);
    }
}

int
hx509_prompt_hidden(hx509_prompt_type type)
{
    /* default to hidden if unknown */

    switch (type) {
    case HX509_PROMPT_TYPE_QUESTION:
    case HX509_PROMPT_TYPE_INFO:
	return 0;
    default:
	return 1;
    }
}

int
hx509_lock_command_string(hx509_lock lock, const char *string)
{
    if (strncasecmp(string, "PASS:", 5) == 0) {
	hx509_lock_add_password(lock, string + 5);
    } else if (strcasecmp(string, "PROMPT") == 0) {
	hx509_lock_set_prompter(lock, default_prompter, NULL);
    } else
	return HX509_UNKNOWN_LOCK_COMMAND;
    return 0;
}
