/*
 * Copyright (c) 1997-2006 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Portions Copyright (c) 2009 Apple Inc. All rights reserved.
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
#include <vis.h>

struct facility {
    int min;
    int max;
    krb5_log_log_func_t log_func;
    krb5_log_close_func_t close_func;
    void *data;
};

static struct facility*
log_realloc(krb5_log_facility *f)
{
    struct facility *fp;
    fp = realloc(f->val, (f->len + 1) * sizeof(*f->val));
    if(fp == NULL)
	return NULL;
    f->len++;
    f->val = fp;
    fp += f->len - 1;
    return fp;
}

struct s2i {
    const char *s;
    int val;
};

#define L(X) { #X, LOG_ ## X }

static struct s2i syslogvals[] = {
    L(EMERG),
    L(ALERT),
    L(CRIT),
    L(ERR),
    L(WARNING),
    L(NOTICE),
    L(INFO),
    L(DEBUG),

    L(AUTH),
#ifdef LOG_AUTHPRIV
    L(AUTHPRIV),
#endif
#ifdef LOG_CRON
    L(CRON),
#endif
    L(DAEMON),
#ifdef LOG_FTP
    L(FTP),
#endif
    L(KERN),
    L(LPR),
    L(MAIL),
#ifdef LOG_NEWS
    L(NEWS),
#endif
    L(SYSLOG),
    L(USER),
#ifdef LOG_UUCP
    L(UUCP),
#endif
    L(LOCAL0),
    L(LOCAL1),
    L(LOCAL2),
    L(LOCAL3),
    L(LOCAL4),
    L(LOCAL5),
    L(LOCAL6),
    L(LOCAL7),
    { NULL, -1 }
};

static int
find_value(const char *s, struct s2i *table)
{
    while(table->s && strcasecmp(table->s, s))
	table++;
    return table->val;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_initlog(krb5_context context,
	     const char *program,
	     krb5_log_facility **fac)
{
    krb5_log_facility *f = calloc(1, sizeof(*f));
    if(f == NULL) {
	krb5_set_error_message(context, ENOMEM,
			       N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    f->program = strdup(program);
    if(f->program == NULL){
	free(f);
	krb5_set_error_message(context, ENOMEM,
			       N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    *fac = f;
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_addlog_func(krb5_context context,
		 krb5_log_facility *fac,
		 int min,
		 int max,
		 krb5_log_log_func_t log_func,
		 krb5_log_close_func_t close_func,
		 void *data)
{
    struct facility *fp = log_realloc(fac);
    if(fp == NULL) {
	krb5_set_error_message(context, ENOMEM,
			       N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    fp->min = min;
    fp->max = max;
    fp->log_func = log_func;
    fp->close_func = close_func;
    fp->data = data;
    return 0;
}


struct _heimdal_syslog_data{
    int priority;
};

static void KRB5_CALLCONV
log_syslog(const char *timestr,
	   const char *msg,
	   void *data)

{
    struct _heimdal_syslog_data *s = data;
    syslog(s->priority, "%s", msg);
}

static void KRB5_CALLCONV
close_syslog(void *data)
{
    free(data);
    closelog();
}

static krb5_error_code
open_syslog(krb5_context context,
	    krb5_log_facility *facility, int min, int max,
	    const char *sev, const char *fac)
{
    struct _heimdal_syslog_data *sd = malloc(sizeof(*sd));
    int i;

    if(sd == NULL) {
	krb5_set_error_message(context, ENOMEM,
			       N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    i = find_value(sev, syslogvals);
    if(i == -1)
	i = LOG_ERR;
    sd->priority = i;
    i = find_value(fac, syslogvals);
    if(i == -1)
	i = LOG_AUTH;
    sd->priority |= i;
    roken_openlog(facility->program, LOG_PID | LOG_NDELAY, i);
    return krb5_addlog_func(context, facility, min, max,
			    log_syslog, close_syslog, sd);
}

struct file_data{
    const char *filename;
    const char *mode;
    FILE *fd;
    int keep_open;
};

static void KRB5_CALLCONV
log_file(const char *timestr,
	 const char *msg,
	 void *data)
{
    struct file_data *f = data;
    char *msgclean;
    size_t len = strlen(msg);
    if(f->keep_open == 0)
	f->fd = fopen(f->filename, f->mode);
    if(f->fd == NULL)
	return;
    /* make sure the log doesn't contain special chars */
    msgclean = malloc((len + 1) * 4);
    if (msgclean == NULL)
	goto out;
    strvisx(msgclean, rk_UNCONST(msg), len, VIS_OCTAL);
    fprintf(f->fd, "%s %s\n", timestr, msgclean);
    free(msgclean);
 out:
    if(f->keep_open == 0) {
	fclose(f->fd);
	f->fd = NULL;
    }
}

static void KRB5_CALLCONV
close_file(void *data)
{
    struct file_data *f = data;
    if(f->keep_open && f->filename)
	fclose(f->fd);
    free(data);
}

static krb5_error_code
open_file(krb5_context context, krb5_log_facility *fac, int min, int max,
	  const char *filename, const char *mode, FILE *f, int keep_open)
{
    struct file_data *fd = malloc(sizeof(*fd));
    if(fd == NULL) {
	krb5_set_error_message(context, ENOMEM,
			       N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    fd->filename = filename;
    fd->mode = mode;
    fd->fd = f;
    fd->keep_open = keep_open;

    return krb5_addlog_func(context, fac, min, max, log_file, close_file, fd);
}



KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_addlog_dest(krb5_context context, krb5_log_facility *f, const char *orig)
{
    krb5_error_code ret = 0;
    int min = 0, max = -1, n;
    char c;
    const char *p = orig;

    n = sscanf(p, "%d%c%d/", &min, &c, &max);
    if(n == 2){
	if(c == '/') {
	    if(min < 0){
		max = -min;
		min = 0;
	    }else{
		max = min;
	    }
	}
    }
    if(n){
	p = strchr(p, '/');
	if(p == NULL) {
	    krb5_set_error_message(context, HEIM_ERR_LOG_PARSE,
				   N_("failed to parse \"%s\"", ""), orig);
	    return HEIM_ERR_LOG_PARSE;
	}
	p++;
    }
    if(strcmp(p, "STDERR") == 0){
	ret = open_file(context, f, min, max, NULL, NULL, stderr, 1);
    }else if(strcmp(p, "CONSOLE") == 0){
	ret = open_file(context, f, min, max, "/dev/console", "w", NULL, 0);
    }else if(strncmp(p, "FILE", 4) == 0 && (p[4] == ':' || p[4] == '=')){
	char *fn;
	FILE *file = NULL;
	int keep_open = 0;
	fn = strdup(p + 5);
	if(fn == NULL) {
	    krb5_set_error_message(context, ENOMEM,
				   N_("malloc: out of memory", ""));
	    return ENOMEM;
	}
	if(p[4] == '='){
	    int i = open(fn, O_WRONLY | O_CREAT |
			 O_TRUNC | O_APPEND, 0666);
	    if(i < 0) {
		ret = errno;
		krb5_set_error_message(context, ret,
				       N_("open(%s) logile: %s", ""), fn,
				       strerror(ret));
		free(fn);
		return ret;
	    }
	    rk_cloexec(i);
	    file = fdopen(i, "a");
	    if(file == NULL){
		ret = errno;
		close(i);
		krb5_set_error_message(context, ret,
				       N_("fdopen(%s) logfile: %s", ""),
				       fn, strerror(ret));
		free(fn);
		return ret;
	    }
	    keep_open = 1;
	}
	ret = open_file(context, f, min, max, fn, "a", file, keep_open);
    }else if(strncmp(p, "DEVICE", 6) == 0 && (p[6] == ':' || p[6] == '=')){
	ret = open_file(context, f, min, max, strdup(p + 7), "w", NULL, 0);
    }else if(strncmp(p, "SYSLOG", 6) == 0 && (p[6] == '\0' || p[6] == ':')){
	char severity[128] = "";
	char facility[128] = "";
	p += 6;
	if(*p != '\0')
	    p++;
	if(strsep_copy(&p, ":", severity, sizeof(severity)) != -1)
	    strsep_copy(&p, ":", facility, sizeof(facility));
	if(*severity == '\0')
	    strlcpy(severity, "ERR", sizeof(severity));
 	if(*facility == '\0')
	    strlcpy(facility, "AUTH", sizeof(facility));
	ret = open_syslog(context, f, min, max, severity, facility);
    }else{
	ret = HEIM_ERR_LOG_PARSE; /* XXX */
	krb5_set_error_message (context, ret,
				N_("unknown log type: %s", ""), p);
    }
    return ret;
}


KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_openlog(krb5_context context,
	     const char *program,
	     krb5_log_facility **fac)
{
    krb5_error_code ret;
    char **p, **q;

    ret = krb5_initlog(context, program, fac);
    if(ret)
	return ret;

    p = krb5_config_get_strings(context, NULL, "logging", program, NULL);
    if(p == NULL)
	p = krb5_config_get_strings(context, NULL, "logging", "default", NULL);
    if(p){
	for(q = p; *q && ret == 0; q++)
	    ret = krb5_addlog_dest(context, *fac, *q);
	krb5_config_free_strings(p);
    }else
	ret = krb5_addlog_dest(context, *fac, "SYSLOG");
    return ret;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_closelog(krb5_context context,
	      krb5_log_facility *fac)
{
    int i;
    for(i = 0; i < fac->len; i++)
	(*fac->val[i].close_func)(fac->val[i].data);
    free(fac->val);
    free(fac->program);
    fac->val = NULL;
    fac->len = 0;
    fac->program = NULL;
    free(fac);
    return 0;
}

#undef __attribute__
#define __attribute__(X)

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_vlog_msg(krb5_context context,
	      krb5_log_facility *fac,
	      char **reply,
	      int level,
	      const char *fmt,
	      va_list ap)
     __attribute__((format (printf, 5, 0)))
{

    char *msg = NULL;
    const char *actual = NULL;
    char buf[64];
    time_t t = 0;
    int i;

    for(i = 0; fac && i < fac->len; i++)
	if(fac->val[i].min <= level &&
	   (fac->val[i].max < 0 || fac->val[i].max >= level)) {
	    if(t == 0) {
		t = time(NULL);
		krb5_format_time(context, t, buf, sizeof(buf), TRUE);
	    }
	    if(actual == NULL) {
		int ret = vasprintf(&msg, fmt, ap);
		if(ret < 0 || msg == NULL)
		    actual = fmt;
		else
		    actual = msg;
	    }
	    (*fac->val[i].log_func)(buf, actual, fac->val[i].data);
	}
    if(reply == NULL)
	free(msg);
    else
	*reply = msg;
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_vlog(krb5_context context,
	  krb5_log_facility *fac,
	  int level,
	  const char *fmt,
	  va_list ap)
     __attribute__((format (printf, 4, 0)))
{
    return krb5_vlog_msg(context, fac, NULL, level, fmt, ap);
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_log_msg(krb5_context context,
	     krb5_log_facility *fac,
	     int level,
	     char **reply,
	     const char *fmt,
	     ...)
     __attribute__((format (printf, 5, 6)))
{
    va_list ap;
    krb5_error_code ret;

    va_start(ap, fmt);
    ret = krb5_vlog_msg(context, fac, reply, level, fmt, ap);
    va_end(ap);
    return ret;
}


KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_log(krb5_context context,
	 krb5_log_facility *fac,
	 int level,
	 const char *fmt,
	 ...)
     __attribute__((format (printf, 4, 5)))
{
    va_list ap;
    krb5_error_code ret;

    va_start(ap, fmt);
    ret = krb5_vlog(context, fac, level, fmt, ap);
    va_end(ap);
    return ret;
}

void KRB5_LIB_FUNCTION
_krb5_debug(krb5_context context,
	    int level,
	    const char *fmt,
	    ...)
    __attribute__((format (printf, 3, 4)))
{
    va_list ap;

    if (context == NULL || context->debug_dest == NULL)
	return;

    va_start(ap, fmt);
    krb5_vlog(context, context->debug_dest, level, fmt, ap);
    va_end(ap);
}

krb5_boolean KRB5_LIB_FUNCTION
_krb5_have_debug(krb5_context context, int level)
{
    if (context == NULL || context->debug_dest == NULL)
	return 0 ;
    return 1;
}
