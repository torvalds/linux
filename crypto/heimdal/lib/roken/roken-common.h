/*
 * Copyright (c) 1995 - 2005 Kungliga Tekniska Högskolan
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

/* $Id$ */

#ifndef __ROKEN_COMMON_H__
#define __ROKEN_COMMON_H__

#ifndef ROKEN_LIB_FUNCTION
#ifdef _WIN32
#define ROKEN_LIB_FUNCTION
#define ROKEN_LIB_CALL     __cdecl
#else
#define ROKEN_LIB_FUNCTION
#define ROKEN_LIB_CALL
#endif
#endif

#ifdef __cplusplus
#define ROKEN_CPP_START	extern "C" {
#define ROKEN_CPP_END	}
#else
#define ROKEN_CPP_START
#define ROKEN_CPP_END
#endif

#ifndef INADDR_NONE
#define INADDR_NONE 0xffffffff
#endif

#ifndef INADDR_LOOPBACK
#define INADDR_LOOPBACK 0x7f000001
#endif

#ifndef SOMAXCONN
#define SOMAXCONN 5
#endif

#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif

#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif

#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif

#ifndef max
#define max(a,b) (((a)>(b))?(a):(b))
#endif

#ifndef min
#define min(a,b) (((a)<(b))?(a):(b))
#endif

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#ifndef LOG_DAEMON
#define openlog(id,option,facility) openlog((id),(option))
#define	LOG_DAEMON	0
#endif
#ifndef LOG_ODELAY
#define LOG_ODELAY 0
#endif
#ifndef LOG_NDELAY
#define LOG_NDELAY 0x08
#endif
#ifndef LOG_CONS
#define LOG_CONS 0
#endif
#ifndef LOG_AUTH
#define LOG_AUTH 0
#endif
#ifndef LOG_AUTHPRIV
#define LOG_AUTHPRIV LOG_AUTH
#endif

#ifndef F_OK
#define F_OK 0
#endif

#ifndef O_ACCMODE
#define O_ACCMODE	003
#endif

#ifndef _WIN32

#ifndef _PATH_DEV
#define _PATH_DEV "/dev/"
#endif

#ifndef _PATH_DEVNULL
#define _PATH_DEVNULL "/dev/null"
#endif

#ifndef _PATH_HEQUIV
#define _PATH_HEQUIV "/etc/hosts.equiv"
#endif

#ifndef _PATH_VARRUN
#define _PATH_VARRUN "/var/run/"
#endif

#ifndef _PATH_BSHELL
#define _PATH_BSHELL "/bin/sh"
#endif

#ifndef MAXPATHLEN
#define MAXPATHLEN (1024+4)
#endif

#endif	/* !_WIN32 */

#ifndef PATH_MAX
#define PATH_MAX MAX_PATH
#endif

#ifndef RETSIGTYPE
#define RETSIGTYPE void
#endif

#ifndef SIG_ERR
#define SIG_ERR ((RETSIGTYPE (*)(int))-1)
#endif

/*
 * error code for getipnodeby{name,addr}
 */

#ifndef HOST_NOT_FOUND
#define HOST_NOT_FOUND 1
#endif

#ifndef TRY_AGAIN
#define TRY_AGAIN 2
#endif

#ifndef NO_RECOVERY
#define NO_RECOVERY 3
#endif

#ifndef NO_DATA
#define NO_DATA 4
#endif

#ifndef NO_ADDRESS
#define NO_ADDRESS NO_DATA
#endif

/*
 * error code for getaddrinfo
 */

#ifndef EAI_NOERROR
#define EAI_NOERROR	0	/* no error */
#endif

#ifndef EAI_NONAME

#define EAI_ADDRFAMILY	1	/* address family for nodename not supported */
#define EAI_AGAIN	2	/* temporary failure in name resolution */
#define EAI_BADFLAGS	3	/* invalid value for ai_flags */
#define EAI_FAIL	4	/* non-recoverable failure in name resolution */
#define EAI_FAMILY	5	/* ai_family not supported */
#define EAI_MEMORY	6	/* memory allocation failure */
#define EAI_NODATA	7	/* no address associated with nodename */
#define EAI_NONAME	8	/* nodename nor servname provided, or not known */
#define EAI_SERVICE	9	/* servname not supported for ai_socktype */
#define EAI_SOCKTYPE   10	/* ai_socktype not supported */
#define EAI_SYSTEM     11	/* system error returned in errno */

#endif /* EAI_NONAME */

/* flags for getaddrinfo() */

#ifndef AI_PASSIVE
#define AI_PASSIVE	0x01
#define AI_CANONNAME	0x02
#endif /* AI_PASSIVE */

#ifndef AI_NUMERICHOST
#define AI_NUMERICHOST	0x04
#endif

#ifndef AI_NUMERICSERV
#define AI_NUMERICSERV  0x08
#endif

/* flags for getnameinfo() */

#ifndef NI_DGRAM
#define NI_DGRAM	0x01
#define NI_NAMEREQD	0x02
#define NI_NOFQDN	0x04
#define NI_NUMERICHOST	0x08
#define NI_NUMERICSERV	0x10
#endif

/*
 * constants for getnameinfo
 */

#ifndef NI_MAXHOST
#define NI_MAXHOST  1025
#define NI_MAXSERV    32
#endif

/*
 * constants for inet_ntop
 */

#ifndef INET_ADDRSTRLEN
#define INET_ADDRSTRLEN    16
#endif

#ifndef INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN   46
#endif

/*
 * for shutdown(2)
 */

#ifndef SHUT_RD
#define SHUT_RD 0
#endif

#ifndef SHUT_WR
#define SHUT_WR 1
#endif

#ifndef SHUT_RDWR
#define SHUT_RDWR 2
#endif

#ifndef HAVE___ATTRIBUTE__
#define __attribute__(x)
#endif

ROKEN_CPP_START

#ifndef IRIX4 /* fix for compiler bug */
#ifndef _WIN32
#ifdef RETSIGTYPE
typedef RETSIGTYPE (*SigAction)(int);
SigAction signal(int iSig, SigAction pAction); /* BSD compatible */
#endif
#endif
#endif

#define SE_E_UNSPECIFIED (-1)
#define SE_E_FORKFAILED  (-2)
#define SE_E_WAITPIDFAILED (-3)
#define SE_E_EXECTIMEOUT (-4)
#define SE_E_NOEXEC   126
#define SE_E_NOTFOUND 127

#define SE_PROCSTATUS(st) (((st) >= 0 && (st) < 126)? st: -1)
#define SE_PROCSIGNAL(st) (((st) >= 128)? (st) - 128: -1)
#define SE_IS_ERROR(st) ((st) < 0 || (st) >= 126)


#define simple_execve rk_simple_execve
ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
simple_execve(const char*, char*const[], char*const[]);

#define simple_execve_timed rk_simple_execve_timed
ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
simple_execve_timed(const char *, char *const[],
		    char *const [], time_t (*)(void *),
		    void *, time_t);

#define simple_execvp rk_simple_execvp
ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
simple_execvp(const char*, char *const[]);

#define simple_execvp_timed rk_simple_execvp_timed
ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
simple_execvp_timed(const char *, char *const[],
		    time_t (*)(void *), void *, time_t);

#define simple_execlp rk_simple_execlp
ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
simple_execlp(const char*, ...);

#define simple_execle rk_simple_execle
ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
simple_execle(const char*, ...);

#define wait_for_process rk_wait_for_process
ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
wait_for_process(pid_t);

#define wait_for_process_timed rk_wait_for_process_timed
ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
wait_for_process_timed(pid_t, time_t (*)(void *),
		       void *, time_t);

#define pipe_execv rk_pipe_execv
ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
pipe_execv(FILE**, FILE**, FILE**, const char*, ...);

#define print_version rk_print_version
ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
print_version(const char *);

#define eread rk_eread
ROKEN_LIB_FUNCTION ssize_t ROKEN_LIB_CALL
eread (int fd, void *buf, size_t nbytes);

#define ewrite rk_ewrite
ROKEN_LIB_FUNCTION ssize_t ROKEN_LIB_CALL
ewrite (int fd, const void *buf, size_t nbytes);

struct hostent;

#define hostent_find_fqdn rk_hostent_find_fqdn
ROKEN_LIB_FUNCTION const char * ROKEN_LIB_CALL
hostent_find_fqdn (const struct hostent *);

#define esetenv rk_esetenv
ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
esetenv(const char *, const char *, int);

#define socket_set_address_and_port rk_socket_set_address_and_port
ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
socket_set_address_and_port (struct sockaddr *, const void *, int);

#define socket_addr_size rk_socket_addr_size
ROKEN_LIB_FUNCTION size_t ROKEN_LIB_CALL
socket_addr_size (const struct sockaddr *);

#define socket_set_any rk_socket_set_any
ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
socket_set_any (struct sockaddr *, int);

#define socket_sockaddr_size rk_socket_sockaddr_size
ROKEN_LIB_FUNCTION size_t ROKEN_LIB_CALL
socket_sockaddr_size (const struct sockaddr *);

#define socket_get_address rk_socket_get_address
ROKEN_LIB_FUNCTION void * ROKEN_LIB_CALL
socket_get_address (const struct sockaddr *);

#define socket_get_port rk_socket_get_port
ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
socket_get_port (const struct sockaddr *);

#define socket_set_port rk_socket_set_port
ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
socket_set_port (struct sockaddr *, int);

#define socket_set_portrange rk_socket_set_portrange
ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
socket_set_portrange (rk_socket_t, int, int);

#define socket_set_debug rk_socket_set_debug
ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
socket_set_debug (rk_socket_t);

#define socket_set_tos rk_socket_set_tos
ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
socket_set_tos (rk_socket_t, int);

#define socket_set_reuseaddr rk_socket_set_reuseaddr
ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
socket_set_reuseaddr (rk_socket_t, int);

#define socket_set_ipv6only rk_socket_set_ipv6only
ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
socket_set_ipv6only (rk_socket_t, int);

#define socket_to_fd rk_socket_to_fd
ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
socket_to_fd(rk_socket_t, int);

#define vstrcollect rk_vstrcollect
ROKEN_LIB_FUNCTION char ** ROKEN_LIB_CALL
vstrcollect(va_list *ap);

#define strcollect rk_strcollect
ROKEN_LIB_FUNCTION char ** ROKEN_LIB_CALL
strcollect(char *first, ...);

#define timevalfix rk_timevalfix
ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
timevalfix(struct timeval *t1);

#define timevaladd rk_timevaladd
ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
timevaladd(struct timeval *t1, const struct timeval *t2);

#define timevalsub rk_timevalsub
ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
timevalsub(struct timeval *t1, const struct timeval *t2);

#define pid_file_write rk_pid_file_write
ROKEN_LIB_FUNCTION char * ROKEN_LIB_CALL
pid_file_write (const char *progname);

#define pid_file_delete rk_pid_file_delete
ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
pid_file_delete (char **);

#define read_environment rk_read_environment
ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
read_environment(const char *file, char ***env);

#define free_environment rk_free_environment
ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
free_environment(char **);

#define warnerr rk_warnerr
ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
rk_warnerr(int doerrno, const char *fmt, va_list ap)
    __attribute__ ((format (printf, 2, 0)));

ROKEN_LIB_FUNCTION void * ROKEN_LIB_CALL
rk_realloc(void *, size_t);

struct rk_strpool;

ROKEN_LIB_FUNCTION char * ROKEN_LIB_CALL
rk_strpoolcollect(struct rk_strpool *);

ROKEN_LIB_FUNCTION struct rk_strpool * ROKEN_LIB_CALL
rk_strpoolprintf(struct rk_strpool *, const char *, ...)
    __attribute__ ((format (printf, 2, 3)));

ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
rk_strpoolfree(struct rk_strpool *);

ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
rk_dumpdata (const char *, const void *, size_t);

ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
rk_undumpdata (const char *, void **, size_t *);

ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
rk_xfree (void *);

ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
rk_cloexec(int);

ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
rk_cloexec_file(FILE *);

ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
rk_cloexec_dir(DIR *);

ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
ct_memcmp(const void *, const void *, size_t);

void ROKEN_LIB_FUNCTION
rk_random_init(void);

ROKEN_CPP_END

#endif /* __ROKEN_COMMON_H__ */
