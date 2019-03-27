/*
 * Copyright (c) 1997-2000, 2003-2005 Kungliga Tekniska Högskolan
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

#include "kadm5_locl.h"
#include "kadm5-pwcheck.h"

#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif
#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#endif

static int
min_length_passwd_quality (krb5_context context,
			   krb5_principal principal,
			   krb5_data *pwd,
			   const char *opaque,
			   char *message,
			   size_t length)
{
    uint32_t min_length = krb5_config_get_int_default(context, NULL, 6,
						      "password_quality",
						      "min_length",
						      NULL);

    if (pwd->length < min_length) {
	strlcpy(message, "Password too short", length);
	return 1;
    } else
	return 0;
}

static const char *
min_length_passwd_quality_v0 (krb5_context context,
			      krb5_principal principal,
			      krb5_data *pwd)
{
    static char message[1024];
    int ret;

    message[0] = '\0';

    ret = min_length_passwd_quality(context, principal, pwd, NULL,
				    message, sizeof(message));
    if (ret)
	return message;
    return NULL;
}


static int
char_class_passwd_quality (krb5_context context,
			   krb5_principal principal,
			   krb5_data *pwd,
			   const char *opaque,
			   char *message,
			   size_t length)
{
    const char *classes[] = {
	"ABCDEFGHIJKLMNOPQRSTUVWXYZ",
	"abcdefghijklmnopqrstuvwxyz",
	"1234567890",
	"!@#$%^&*()/?<>,.{[]}\\|'~`\" "
    };
    int counter = 0, req_classes;
    size_t i, len;
    char *pw;

    req_classes = krb5_config_get_int_default(context, NULL, 3,
					      "password_quality",
					      "min_classes",
					      NULL);

    len = pwd->length + 1;
    pw = malloc(len);
    if (pw == NULL) {
	strlcpy(message, "out of memory", length);
	return 1;
    }
    strlcpy(pw, pwd->data, len);
    len = strlen(pw);

    for (i = 0; i < sizeof(classes)/sizeof(classes[0]); i++) {
	if (strcspn(pw, classes[i]) < len)
	    counter++;
    }
    memset(pw, 0, pwd->length + 1);
    free(pw);
    if (counter < req_classes) {
	snprintf(message, length,
	    "Password doesn't meet complexity requirement.\n"
	    "Add more characters from the following classes:\n"
	    "1. English uppercase characters (A through Z)\n"
	    "2. English lowercase characters (a through z)\n"
	    "3. Base 10 digits (0 through 9)\n"
	    "4. Nonalphanumeric characters (e.g., !, $, #, %%)");
	return 1;
    }
    return 0;
}

static int
external_passwd_quality (krb5_context context,
			 krb5_principal principal,
			 krb5_data *pwd,
			 const char *opaque,
			 char *message,
			 size_t length)
{
    krb5_error_code ret;
    const char *program;
    char *p;
    pid_t child;
    int status;
    char reply[1024];
    FILE *in = NULL, *out = NULL, *error = NULL;

    if (memchr(pwd->data, '\n', pwd->length) != NULL) {
	snprintf(message, length, "password contains newline, "
		 "not valid for external test");
	return 1;
    }

    program = krb5_config_get_string(context, NULL,
				     "password_quality",
				     "external_program",
				     NULL);
    if (program == NULL) {
	snprintf(message, length, "external password quality "
		 "program not configured");
	return 1;
    }

    ret = krb5_unparse_name(context, principal, &p);
    if (ret) {
	strlcpy(message, "out of memory", length);
	return 1;
    }

    child = pipe_execv(&in, &out, &error, program, program, p, NULL);
    if (child < 0) {
	snprintf(message, length, "external password quality "
		 "program failed to execute for principal %s", p);
	free(p);
	return 1;
    }

    fprintf(in, "principal: %s\n"
	    "new-password: %.*s\n"
	    "end\n",
	    p, (int)pwd->length, (char *)pwd->data);

    fclose(in);

    if (fgets(reply, sizeof(reply), out) == NULL) {

	if (fgets(reply, sizeof(reply), error) == NULL) {
	    snprintf(message, length, "external password quality "
		     "program failed without error");

	} else {
	    reply[strcspn(reply, "\n")] = '\0';
	    snprintf(message, length, "External password quality "
		     "program failed: %s", reply);
	}

	fclose(out);
	fclose(error);
	wait_for_process(child);
	return 1;
    }
    reply[strcspn(reply, "\n")] = '\0';

    fclose(out);
    fclose(error);

    status = wait_for_process(child);

    if (SE_IS_ERROR(status) || SE_PROCSTATUS(status) != 0) {
	snprintf(message, length, "external program failed: %s", reply);
	free(p);
	return 1;
    }

    if (strcmp(reply, "APPROVED") != 0) {
	snprintf(message, length, "%s", reply);
	free(p);
	return 1;
    }

    free(p);

    return 0;
}


static kadm5_passwd_quality_check_func_v0 passwd_quality_check =
	min_length_passwd_quality_v0;

struct kadm5_pw_policy_check_func builtin_funcs[] = {
    { "minimum-length", min_length_passwd_quality },
    { "character-class", char_class_passwd_quality },
    { "external-check", external_passwd_quality },
    { NULL, NULL }
};
struct kadm5_pw_policy_verifier builtin_verifier = {
    "builtin",
    KADM5_PASSWD_VERSION_V1,
    "Heimdal builtin",
    builtin_funcs
};

static struct kadm5_pw_policy_verifier **verifiers;
static int num_verifiers;

/*
 * setup the password quality hook
 */

#ifndef RTLD_NOW
#define RTLD_NOW 0
#endif

void
kadm5_setup_passwd_quality_check(krb5_context context,
				 const char *check_library,
				 const char *check_function)
{
#ifdef HAVE_DLOPEN
    void *handle;
    void *sym;
    int *version;
    const char *tmp;

    if(check_library == NULL) {
	tmp = krb5_config_get_string(context, NULL,
				     "password_quality",
				     "check_library",
				     NULL);
	if(tmp != NULL)
	    check_library = tmp;
    }
    if(check_function == NULL) {
	tmp = krb5_config_get_string(context, NULL,
				     "password_quality",
				     "check_function",
				     NULL);
	if(tmp != NULL)
	    check_function = tmp;
    }
    if(check_library != NULL && check_function == NULL)
	check_function = "passwd_check";

    if(check_library == NULL)
	return;
    handle = dlopen(check_library, RTLD_NOW);
    if(handle == NULL) {
	krb5_warnx(context, "failed to open `%s'", check_library);
	return;
    }
    version = (int *) dlsym(handle, "version");
    if(version == NULL) {
	krb5_warnx(context,
		   "didn't find `version' symbol in `%s'", check_library);
	dlclose(handle);
	return;
    }
    if(*version != KADM5_PASSWD_VERSION_V0) {
	krb5_warnx(context,
		   "version of loaded library is %d (expected %d)",
		   *version, KADM5_PASSWD_VERSION_V0);
	dlclose(handle);
	return;
    }
    sym = dlsym(handle, check_function);
    if(sym == NULL) {
	krb5_warnx(context,
		   "didn't find `%s' symbol in `%s'",
		   check_function, check_library);
	dlclose(handle);
	return;
    }
    passwd_quality_check = (kadm5_passwd_quality_check_func_v0) sym;
#endif /* HAVE_DLOPEN */
}

#ifdef HAVE_DLOPEN

static krb5_error_code
add_verifier(krb5_context context, const char *check_library)
{
    struct kadm5_pw_policy_verifier *v, **tmp;
    void *handle;
    int i;

    handle = dlopen(check_library, RTLD_NOW);
    if(handle == NULL) {
	krb5_warnx(context, "failed to open `%s'", check_library);
	return ENOENT;
    }
    v = (struct kadm5_pw_policy_verifier *) dlsym(handle, "kadm5_password_verifier");
    if(v == NULL) {
	krb5_warnx(context,
		   "didn't find `kadm5_password_verifier' symbol "
		   "in `%s'", check_library);
	dlclose(handle);
	return ENOENT;
    }
    if(v->version != KADM5_PASSWD_VERSION_V1) {
	krb5_warnx(context,
		   "version of loaded library is %d (expected %d)",
		   v->version, KADM5_PASSWD_VERSION_V1);
	dlclose(handle);
	return EINVAL;
    }
    for (i = 0; i < num_verifiers; i++) {
	if (strcmp(v->name, verifiers[i]->name) == 0)
	    break;
    }
    if (i < num_verifiers) {
	krb5_warnx(context, "password verifier library `%s' is already loaded",
		   v->name);
	dlclose(handle);
	return 0;
    }

    tmp = realloc(verifiers, (num_verifiers + 1) * sizeof(*verifiers));
    if (tmp == NULL) {
	krb5_warnx(context, "out of memory");
	dlclose(handle);
	return 0;
    }
    verifiers = tmp;
    verifiers[num_verifiers] = v;
    num_verifiers++;

    return 0;
}

#endif

krb5_error_code
kadm5_add_passwd_quality_verifier(krb5_context context,
				  const char *check_library)
{
#ifdef HAVE_DLOPEN

    if(check_library == NULL) {
	krb5_error_code ret;
	char **tmp;

	tmp = krb5_config_get_strings(context, NULL,
				      "password_quality",
				      "policy_libraries",
				      NULL);
	if(tmp == NULL || *tmp == NULL)
	    return 0;

	while (*tmp) {
	    ret = add_verifier(context, *tmp);
	    if (ret)
		return ret;
	    tmp++;
	}
	return 0;
    } else {
	return add_verifier(context, check_library);
    }
#else
    return 0;
#endif /* HAVE_DLOPEN */
}

/*
 *
 */

static const struct kadm5_pw_policy_check_func *
find_func(krb5_context context, const char *name)
{
    const struct kadm5_pw_policy_check_func *f;
    char *module = NULL;
    const char *p, *func;
    int i;

    p = strchr(name, ':');
    if (p) {
	size_t len = p - name + 1;
	func = p + 1;
	module = malloc(len);
	if (module == NULL)
	    return NULL;
	strlcpy(module, name, len);
    } else
	func = name;

    /* Find module in loaded modules first */
    for (i = 0; i < num_verifiers; i++) {
	if (module && strcmp(module, verifiers[i]->name) != 0)
	    continue;
	for (f = verifiers[i]->funcs; f->name ; f++)
	    if (strcmp(func, f->name) == 0) {
		if (module)
		    free(module);
		return f;
	    }
    }
    /* Lets try try the builtin modules */
    if (module == NULL || strcmp(module, "builtin") == 0) {
	for (f = builtin_verifier.funcs; f->name ; f++)
	    if (strcmp(func, f->name) == 0) {
		if (module)
		    free(module);
		return f;
	    }
    }
    if (module)
	free(module);
    return NULL;
}

const char *
kadm5_check_password_quality (krb5_context context,
			      krb5_principal principal,
			      krb5_data *pwd_data)
{
    const struct kadm5_pw_policy_check_func *proc;
    static char error_msg[1024];
    const char *msg;
    char **v, **vp;
    int ret;

    /*
     * Check if we should use the old version of policy function.
     */

    v = krb5_config_get_strings(context, NULL,
				"password_quality",
				"policies",
				NULL);
    if (v == NULL) {
	msg = (*passwd_quality_check) (context, principal, pwd_data);
	if (msg)
	    krb5_set_error_message(context, 0, "password policy failed: %s", msg);
	return msg;
    }

    error_msg[0] = '\0';

    msg = NULL;
    for(vp = v; *vp; vp++) {
	proc = find_func(context, *vp);
	if (proc == NULL) {
	    msg = "failed to find password verifier function";
	    krb5_set_error_message(context, 0, "Failed to find password policy "
				   "function: %s", *vp);
	    break;
	}
	ret = (proc->func)(context, principal, pwd_data, NULL,
			   error_msg, sizeof(error_msg));
	if (ret) {
	    krb5_set_error_message(context, 0, "Password policy "
				   "%s failed with %s",
				   proc->name, error_msg);
	    msg = error_msg;
	    break;
	}
    }
    krb5_config_free_strings(v);

    /* If the default quality check isn't used, lets check that the
     * old quality function the user have set too */
    if (msg == NULL && passwd_quality_check != min_length_passwd_quality_v0) {
	msg = (*passwd_quality_check) (context, principal, pwd_data);
	if (msg)
	    krb5_set_error_message(context, 0, "(old) password policy "
				   "failed with %s", msg);

    }
    return msg;
}
