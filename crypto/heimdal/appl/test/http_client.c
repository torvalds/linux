/*
 * Copyright (c) 2003 - 2005 Kungliga Tekniska Högskolan
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

#include "test_locl.h"
#include <gssapi/gssapi.h>
#include <gssapi/gssapi_krb5.h>
#include <gssapi/gssapi_spnego.h>
#include "gss_common.h"
#include <base64.h>

RCSID("$Id$");

/*
 * A simplistic client implementing draft-brezak-spnego-http-04.txt
 */

static int
do_connect (const char *hostname, const char *port)
{
    struct addrinfo *ai, *a;
    struct addrinfo hints;
    int error;
    int s = -1;

    memset (&hints, 0, sizeof(hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = 0;

    error = getaddrinfo (hostname, port, &hints, &ai);
    if (error)
	errx (1, "getaddrinfo(%s): %s", hostname, gai_strerror(error));

    for (a = ai; a != NULL; a = a->ai_next) {
	s = socket (a->ai_family, a->ai_socktype, a->ai_protocol);
	if (s < 0)
	    continue;
	if (connect (s, a->ai_addr, a->ai_addrlen) < 0) {
	    warn ("connect(%s)", hostname);
 	    close (s);
 	    continue;
	}
	break;
    }
    freeaddrinfo (ai);
    if (a == NULL)
	errx (1, "failed to contact %s", hostname);

    return s;
}

static void
fdprintf(int s, const char *fmt, ...)
{
    size_t len;
    ssize_t ret;
    va_list ap;
    char *str, *buf;

    va_start(ap, fmt);
    vasprintf(&str, fmt, ap);
    va_end(ap);

    if (str == NULL)
	errx(1, "vasprintf");

    buf = str;
    len = strlen(buf);
    while (len) {
	ret = write(s, buf, len);
	if (ret == 0)
	    err(1, "connection closed");
	else if (ret < 0)
	    err(1, "error");
	len -= ret;
	buf += ret;
    }
    free(str);
}

static int help_flag;
static int version_flag;
static int verbose_flag;
static int mutual_flag = 1;
static int delegate_flag;
static char *port_str = "http";
static char *gss_service = "HTTP";

static struct getargs http_args[] = {
    { "verbose", 'v', arg_flag, &verbose_flag, "verbose logging", },
    { "port", 'p', arg_string, &port_str, "port to connect to", "port" },
    { "delegate", 0, arg_flag, &delegate_flag, "gssapi delegate credential" },
    { "gss-service", 's', arg_string, &gss_service, "gssapi service to use",
      "service" },
    { "mech", 'm', arg_string, &mech, "gssapi mech to use", "mech" },
    { "mutual", 0, arg_negative_flag, &mutual_flag, "no gssapi mutual auth" },
    { "help", 'h', arg_flag, &help_flag },
    { "version", 0, arg_flag, &version_flag }
};

static int num_http_args = sizeof(http_args) / sizeof(http_args[0]);

static void
usage(int code)
{
    arg_printusage(http_args, num_http_args, NULL, "host [page]");
    exit(code);
}

/*
 *
 */

struct http_req {
    char *response;
    char **headers;
    int num_headers;
    void *body;
    size_t body_size;
};


static void
http_req_zero(struct http_req *req)
{
    req->response = NULL;
    req->headers = NULL;
    req->num_headers = 0;
    req->body = NULL;
    req->body_size = 0;
}

static void
http_req_free(struct http_req *req)
{
    int i;

    free(req->response);
    for (i = 0; i < req->num_headers; i++)
	free(req->headers[i]);
    free(req->headers);
    free(req->body);
    http_req_zero(req);
}

static const char *
http_find_header(struct http_req *req, const char *header)
{
    int i, len = strlen(header);

    for (i = 0; i < req->num_headers; i++) {
	if (strncasecmp(header, req->headers[i], len) == 0) {
	    return req->headers[i] + len + 1;
	}
    }
    return NULL;
}


static int
http_query(const char *host, const char *page,
	   char **headers, int num_headers, struct http_req *req)
{
    enum { RESPONSE, HEADER, BODY } state;
    ssize_t ret;
    char in_buf[1024], *in_ptr = in_buf;
    size_t in_len = 0;
    int s, i;

    http_req_zero(req);

    s = do_connect(host, port_str);
    if (s < 0)
	errx(1, "connection failed");

    fdprintf(s, "GET %s HTTP/1.0\r\n", page);
    for (i = 0; i < num_headers; i++)
	fdprintf(s, "%s\r\n", headers[i]);
    fdprintf(s, "Host: %s\r\n\r\n", host);

    state = RESPONSE;

    while (1) {
	ret = read (s, in_ptr, sizeof(in_buf) - in_len - 1);
	if (ret == 0)
	    break;
	else if (ret < 0)
	    err (1, "read: %lu", (unsigned long)ret);

	in_buf[ret + in_len] = '\0';

	if (state == HEADER || state == RESPONSE) {
	    char *p;

	    in_len += ret;
	    in_ptr += ret;

	    while (1) {
		p = strstr(in_buf, "\r\n");

		if (p == NULL) {
		    break;
		} else if (p == in_buf) {
		    memmove(in_buf, in_buf + 2, sizeof(in_buf) - 2);
		    state = BODY;
		    in_len -= 2;
		    in_ptr -= 2;
		    break;
		} else if (state == RESPONSE) {
		    req->response = emalloc(p - in_buf + 1);
		    memcpy(req->response, in_buf, p - in_buf);
		    req->response[p - in_buf] = '\0';
		    state = HEADER;
		} else {
		    req->headers = realloc(req->headers,
					   (req->num_headers + 1) * sizeof(req->headers[0]));
		    req->headers[req->num_headers] = emalloc(p - in_buf + 1);
		    memcpy(req->headers[req->num_headers], in_buf, p - in_buf);
		    req->headers[req->num_headers][p - in_buf] = '\0';
		    if (req->headers[req->num_headers] == NULL)
			errx(1, "strdup");
		    req->num_headers++;
		}
		memmove(in_buf, p + 2, sizeof(in_buf) - (p - in_buf) - 2);
		in_len -= (p - in_buf) + 2;
		in_ptr -= (p - in_buf) + 2;
	    }
	}

	if (state == BODY) {

	    req->body = erealloc(req->body, req->body_size + ret + 1);

	    memcpy((char *)req->body + req->body_size, in_buf, ret);
	    req->body_size += ret;
	    ((char *)req->body)[req->body_size] = '\0';

	    in_ptr = in_buf;
	    in_len = 0;
	} else
	    abort();
    }

    if (verbose_flag) {
	int i;
	printf("response: %s\n", req->response);
	for (i = 0; i < req->num_headers; i++)
	    printf("header[%d] %s\n", i, req->headers[i]);
	printf("body: %.*s\n", (int)req->body_size, (char *)req->body);
    }

    close(s);
    return 0;
}


int
main(int argc, char **argv)
{
    struct http_req req;
    const char *host, *page;
    int i, done, print_body, gssapi_done, gssapi_started;
    char *headers[10]; /* XXX */
    int num_headers;
    gss_ctx_id_t context_hdl = GSS_C_NO_CONTEXT;
    gss_name_t server = GSS_C_NO_NAME;
    int optind = 0;
    gss_OID mech_oid;
    OM_uint32 flags;

    setprogname(argv[0]);

    if(getarg(http_args, num_http_args, argc, argv, &optind))
	usage(1);

    if (help_flag)
	usage (0);

    if(version_flag) {
	print_version(NULL);
	exit(0);
    }

    argc -= optind;
    argv += optind;

    mech_oid = select_mech(mech);

    if (argc != 1 && argc != 2)
	errx(1, "usage: %s host [page]", getprogname());
    host = argv[0];
    if (argc == 2)
	page = argv[1];
    else
	page = "/";

    flags = 0;
    if (delegate_flag)
	flags |= GSS_C_DELEG_FLAG;
    if (mutual_flag)
	flags |= GSS_C_MUTUAL_FLAG;

    done = 0;
    num_headers = 0;
    gssapi_done = 1;
    gssapi_started = 0;
    do {
	print_body = 0;

	http_query(host, page, headers, num_headers, &req);
	for (i = 0 ; i < num_headers; i++)
	    free(headers[i]);
	num_headers = 0;

	if (strstr(req.response, " 200 ") != NULL) {
	    print_body = 1;
	    done = 1;
	} else if (strstr(req.response, " 401 ") != NULL) {
	    if (http_find_header(&req, "WWW-Authenticate:") == NULL)
		errx(1, "Got %s but missed `WWW-Authenticate'", req.response);
	    gssapi_done = 0;
	}

	if (!gssapi_done) {
	    const char *h = http_find_header(&req, "WWW-Authenticate:");
	    if (h == NULL)
		errx(1, "Got %s but missed `WWW-Authenticate'", req.response);

	    if (strncasecmp(h, "Negotiate", 9) == 0) {
		OM_uint32 maj_stat, min_stat;
		gss_buffer_desc input_token, output_token;

		if (verbose_flag)
		    printf("Negotiate found\n");

		if (server == GSS_C_NO_NAME) {
		    char *name;
		    asprintf(&name, "%s@%s", gss_service, host);
		    input_token.length = strlen(name);
		    input_token.value = name;

		    maj_stat = gss_import_name(&min_stat,
					       &input_token,
					       GSS_C_NT_HOSTBASED_SERVICE,
					       &server);
		    if (GSS_ERROR(maj_stat))
			gss_err (1, min_stat, "gss_inport_name");
		    free(name);
		    input_token.length = 0;
		    input_token.value = NULL;
		}

		i = 9;
		while(h[i] && isspace((unsigned char)h[i]))
		    i++;
		if (h[i] != '\0') {
		    int len = strlen(&h[i]);
		    if (len == 0)
			errx(1, "invalid Negotiate token");
		    input_token.value = emalloc(len);
		    len = base64_decode(&h[i], input_token.value);
		    if (len < 0)
			errx(1, "invalid base64 Negotiate token %s", &h[i]);
		    input_token.length = len;
		} else {
		    if (gssapi_started)
			errx(1, "Negotiate already started");
		    gssapi_started = 1;

		    input_token.length = 0;
		    input_token.value = NULL;
		}

		maj_stat =
		    gss_init_sec_context(&min_stat,
					 GSS_C_NO_CREDENTIAL,
					 &context_hdl,
					 server,
					 mech_oid,
					 flags,
					 0,
					 GSS_C_NO_CHANNEL_BINDINGS,
					 &input_token,
					 NULL,
					 &output_token,
					 NULL,
					 NULL);
		if (GSS_ERROR(maj_stat))
		    gss_err (1, min_stat, "gss_init_sec_context");
		else if (maj_stat & GSS_S_CONTINUE_NEEDED)
		    gssapi_done = 0;
		else {
		    gss_name_t targ_name, src_name;
		    gss_buffer_desc name_buffer;
		    gss_OID mech_type;

		    gssapi_done = 1;

		    printf("Negotiate done: %s\n", mech);

		    maj_stat = gss_inquire_context(&min_stat,
						   context_hdl,
						   &src_name,
						   &targ_name,
						   NULL,
						   &mech_type,
						   NULL,
						   NULL,
						   NULL);
		    if (GSS_ERROR(maj_stat))
			gss_err (1, min_stat, "gss_inquire_context");

		    maj_stat = gss_display_name(&min_stat,
						src_name,
						&name_buffer,
						NULL);
		    if (GSS_ERROR(maj_stat))
			gss_err (1, min_stat, "gss_display_name");

		    printf("Source: %.*s\n",
			   (int)name_buffer.length,
			   (char *)name_buffer.value);

		    gss_release_buffer(&min_stat, &name_buffer);

		    maj_stat = gss_display_name(&min_stat,
						targ_name,
						&name_buffer,
						NULL);
		    if (GSS_ERROR(maj_stat))
			gss_err (1, min_stat, "gss_display_name");

		    printf("Target: %.*s\n",
			   (int)name_buffer.length,
			   (char *)name_buffer.value);

		    gss_release_name(&min_stat, &targ_name);
		    gss_release_buffer(&min_stat, &name_buffer);
		}

		if (output_token.length) {
		    char *neg_token;

		    base64_encode(output_token.value,
				  output_token.length,
				  &neg_token);

		    asprintf(&headers[0], "Authorization: Negotiate %s",
			     neg_token);

		    num_headers = 1;
		    free(neg_token);
		    gss_release_buffer(&min_stat, &output_token);
		}
		if (input_token.length)
		    free(input_token.value);

	    } else
		done = 1;
	} else
	    done = 1;

	if (verbose_flag) {
	    printf("%s\n\n", req.response);

	    for (i = 0; i < req.num_headers; i++)
		printf("%s\n", req.headers[i]);
	    printf("\n");
	}
	if (print_body || verbose_flag)
	    printf("%.*s\n", (int)req.body_size, (char *)req.body);

	http_req_free(&req);
    } while (!done);

    if (gssapi_done == 0)
	errx(1, "gssapi not done but http dance done");

    return 0;
}
