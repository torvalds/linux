/*
 * Copyright (c) 2000 - 2004 Kungliga Tekniska Högskolan
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

#include "kadmin_locl.h"
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

struct kadm_port {
    char *port;
    unsigned short def_port;
    struct kadm_port *next;
} *kadm_ports;

static void
add_kadm_port(krb5_context contextp, const char *service, unsigned int port)
{
    struct kadm_port *p;
    p = malloc(sizeof(*p));
    if(p == NULL) {
	krb5_warnx(contextp, "failed to allocate %lu bytes\n",
		   (unsigned long)sizeof(*p));
	return;
    }

    p->port = strdup(service);
    p->def_port = port;

    p->next = kadm_ports;
    kadm_ports = p;
}

static void
add_standard_ports (krb5_context contextp)
{
    add_kadm_port(contextp, "kerberos-adm", 749);
}

/*
 * parse the set of space-delimited ports in `str' and add them.
 * "+" => all the standard ones
 * otherwise it's port|service[/protocol]
 */

void
parse_ports(krb5_context contextp, const char *str)
{
    char p[128];

    while(strsep_copy(&str, " \t", p, sizeof(p)) != -1) {
	if(strcmp(p, "+") == 0)
	    add_standard_ports(contextp);
	else
	    add_kadm_port(contextp, p, 0);
    }
}

static pid_t pgrp;
sig_atomic_t term_flag, doing_useful_work;

static RETSIGTYPE
sigchld(int sig)
{
    int status;
    /*
     * waitpid() is async safe. will return -1 or 0 on no more zombie
     * children
     */
    while ((waitpid(-1, &status, WNOHANG)) > 0)
	;
    SIGRETURN(0);
}

static RETSIGTYPE
terminate(int sig)
{
    if(getpid() == pgrp) {
	/* parent */
	term_flag = 1;
	signal(sig, SIG_IGN);
	killpg(pgrp, sig);
    } else {
	/* child */
	if(doing_useful_work)
	    term_flag = 1;
	else
	    exit(0);
    }
    SIGRETURN(0);
}

static int
spawn_child(krb5_context contextp, int *socks,
	    unsigned int num_socks, int this_sock)
{
    int e;
    size_t i;
    struct sockaddr_storage __ss;
    struct sockaddr *sa = (struct sockaddr *)&__ss;
    socklen_t sa_size = sizeof(__ss);
    krb5_socket_t s;
    pid_t pid;
    krb5_address addr;
    char buf[128];
    size_t buf_len;

    s = accept(socks[this_sock], sa, &sa_size);
    if(rk_IS_BAD_SOCKET(s)) {
	krb5_warn(contextp, rk_SOCK_ERRNO, "accept");
	return 1;
    }
    e = krb5_sockaddr2address(contextp, sa, &addr);
    if(e)
	krb5_warn(contextp, e, "krb5_sockaddr2address");
    else {
	e = krb5_print_address (&addr, buf, sizeof(buf),
				&buf_len);
	if(e)
	    krb5_warn(contextp, e, "krb5_print_address");
	else
	    krb5_warnx(contextp, "connection from %s", buf);
	krb5_free_address(contextp, &addr);
    }

    pid = fork();
    if(pid == 0) {
	for(i = 0; i < num_socks; i++)
	    rk_closesocket(socks[i]);
	dup2(s, STDIN_FILENO);
	dup2(s, STDOUT_FILENO);
	if(s != STDIN_FILENO && s != STDOUT_FILENO)
	    rk_closesocket(s);
	return 0;
    } else {
	rk_closesocket(s);
    }
    return 1;
}

static void
wait_for_connection(krb5_context contextp,
		    krb5_socket_t *socks, unsigned int num_socks)
{
    unsigned int i;
    int e;
    fd_set orig_read_set, read_set;
    int status, max_fd = -1;

    FD_ZERO(&orig_read_set);

    for(i = 0; i < num_socks; i++) {
#ifdef FD_SETSIZE
	if (socks[i] >= FD_SETSIZE)
	    errx (1, "fd too large");
#endif
	FD_SET(socks[i], &orig_read_set);
	max_fd = max(max_fd, socks[i]);
    }

    pgrp = getpid();

    if(setpgid(0, pgrp) < 0)
	err(1, "setpgid");

    signal(SIGTERM, terminate);
    signal(SIGINT, terminate);
    signal(SIGCHLD, sigchld);

    while (term_flag == 0) {
	read_set = orig_read_set;
	e = select(max_fd + 1, &read_set, NULL, NULL, NULL);
	if(rk_IS_SOCKET_ERROR(e)) {
	    if(rk_SOCK_ERRNO != EINTR)
		krb5_warn(contextp, rk_SOCK_ERRNO, "select");
	} else if(e == 0)
	    krb5_warnx(contextp, "select returned 0");
	else {
	    for(i = 0; i < num_socks; i++) {
		if(FD_ISSET(socks[i], &read_set))
		    if(spawn_child(contextp, socks, num_socks, i) == 0)
			return;
	    }
	}
    }
    signal(SIGCHLD, SIG_IGN);

    while ((waitpid(-1, &status, WNOHANG)) > 0)
	;

    exit(0);
}


void
start_server(krb5_context contextp, const char *port_str)
{
    int e;
    struct kadm_port *p;

    krb5_socket_t *socks = NULL, *tmp;
    unsigned int num_socks = 0;
    int i;

    if (port_str == NULL)
	port_str = "+";

    parse_ports(contextp, port_str);

    for(p = kadm_ports; p; p = p->next) {
	struct addrinfo hints, *ai, *ap;
	char portstr[32];
	memset (&hints, 0, sizeof(hints));
	hints.ai_flags    = AI_PASSIVE;
	hints.ai_socktype = SOCK_STREAM;

	e = getaddrinfo(NULL, p->port, &hints, &ai);
	if(e) {
	    snprintf(portstr, sizeof(portstr), "%u", p->def_port);
	    e = getaddrinfo(NULL, portstr, &hints, &ai);
	}

	if(e) {
	    krb5_warn(contextp, krb5_eai_to_heim_errno(e, errno),
		      "%s", portstr);
	    continue;
	}
	i = 0;
	for(ap = ai; ap; ap = ap->ai_next)
	    i++;
	tmp = realloc(socks, (num_socks + i) * sizeof(*socks));
	if(tmp == NULL) {
	    krb5_warnx(contextp, "failed to reallocate %lu bytes",
		       (unsigned long)(num_socks + i) * sizeof(*socks));
	    continue;
	}
	socks = tmp;
	for(ap = ai; ap; ap = ap->ai_next) {
	    krb5_socket_t s = socket(ap->ai_family, ap->ai_socktype, ap->ai_protocol);
	    if(rk_IS_BAD_SOCKET(s)) {
		krb5_warn(contextp, rk_SOCK_ERRNO, "socket");
		continue;
	    }

	    socket_set_reuseaddr(s, 1);
	    socket_set_ipv6only(s, 1);

	    if (rk_IS_SOCKET_ERROR(bind (s, ap->ai_addr, ap->ai_addrlen))) {
		krb5_warn(contextp, rk_SOCK_ERRNO, "bind");
		rk_closesocket(s);
		continue;
	    }
	    if (rk_IS_SOCKET_ERROR(listen (s, SOMAXCONN))) {
		krb5_warn(contextp, rk_SOCK_ERRNO, "listen");
		rk_closesocket(s);
		continue;
	    }
	    socks[num_socks++] = s;
	}
	freeaddrinfo (ai);
    }
    if(num_socks == 0)
	krb5_errx(contextp, 1, "no sockets to listen to - exiting");

    wait_for_connection(contextp, socks, num_socks);
}
