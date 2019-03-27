/*
 * ntp_intres.c - Implements a generic blocking worker child or thread,
 *		  initially to provide a nonblocking solution for DNS
 *		  name to address lookups available with getaddrinfo().
 *
 * This is a new implementation as of 2009 sharing the filename and
 * very little else with the prior implementation, which used a
 * temporary file to receive a single set of requests from the parent,
 * and a NTP mode 7 authenticated request to push back responses.
 *
 * A primary goal in rewriting this code was the need to support the
 * pool configuration directive's requirement to retrieve multiple
 * addresses resolving a single name, which has previously been
 * satisfied with blocking resolver calls from the ntpd mainline code.
 *
 * A secondary goal is to provide a generic mechanism for other
 * blocking operations to be delegated to a worker using a common
 * model for both Unix and Windows ntpd.  ntp_worker.c, work_fork.c,
 * and work_thread.c implement the generic mechanism.  This file
 * implements the two current consumers, getaddrinfo_sometime() and the
 * presently unused getnameinfo_sometime().
 *
 * Both routines deliver results to a callback and manage memory
 * allocation, meaning there is no freeaddrinfo_sometime().
 *
 * The initial implementation for Unix uses a pair of unidirectional
 * pipes, one each for requests and responses, connecting the forked
 * blocking child worker with the ntpd mainline.  The threaded code
 * uses arrays of pointers to queue requests and responses.
 *
 * The parent drives the process, including scheduling sleeps between
 * retries.
 *
 * Memory is managed differently for a child process, which mallocs
 * request buffers to read from the pipe into, whereas the threaded
 * code mallocs a copy of the request to hand off to the worker via
 * the queueing array.  The resulting request buffer is free()d by
 * platform-independent code.  A wrinkle is the request needs to be
 * available to the requestor during response processing.
 *
 * Response memory allocation is also platform-dependent.  With a
 * separate process and pipes, the response is free()d after being
 * written to the pipe.  With threads, the same memory is handed
 * over and the requestor frees it after processing is completed.
 *
 * The code should be generalized to support threads on Unix using
 * much of the same code used for Windows initially.
 *
 */
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include "ntp_workimpl.h"

#ifdef WORKER

#include <stdio.h>
#include <ctype.h>
#include <signal.h>

/**/
#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#include <arpa/inet.h>
/**/
#ifdef HAVE_SYS_PARAM_H
# include <sys/param.h>
#endif

#if !defined(HAVE_RES_INIT) && defined(HAVE___RES_INIT)
# define HAVE_RES_INIT
#endif

#if defined(HAVE_RESOLV_H) && defined(HAVE_RES_INIT)
# ifdef HAVE_ARPA_NAMESER_H
#  include <arpa/nameser.h> /* DNS HEADER struct */
# endif
# ifdef HAVE_NETDB_H
#  include <netdb.h>
# endif
# include <resolv.h>
# ifdef HAVE_INT32_ONLY_WITH_DNS
#  define HAVE_INT32
# endif
# ifdef HAVE_U_INT32_ONLY_WITH_DNS
#  define HAVE_U_INT32
# endif
#endif

#include "ntp.h"
#include "ntp_debug.h"
#include "ntp_malloc.h"
#include "ntp_syslog.h"
#include "ntp_unixtime.h"
#include "ntp_intres.h"
#include "intreswork.h"


/*
 * Following are implementations of getaddrinfo_sometime() and
 * getnameinfo_sometime().  Each is implemented in three routines:
 *
 * getaddrinfo_sometime()		getnameinfo_sometime()
 * blocking_getaddrinfo()		blocking_getnameinfo()
 * getaddrinfo_sometime_complete()	getnameinfo_sometime_complete()
 *
 * The first runs in the parent and marshalls (or serializes) request
 * parameters into a request blob which is processed in the child by
 * the second routine, blocking_*(), which serializes the results into
 * a response blob unpacked by the third routine, *_complete(), which
 * calls the callback routine provided with the request and frees
 * _request_ memory allocated by the first routine.  Response memory
 * is managed by the code which calls the *_complete routines.
 */


/* === typedefs === */
typedef struct blocking_gai_req_tag {	/* marshalled args */
	size_t			octets;
	u_int			dns_idx;
	time_t			scheduled;
	time_t			earliest;
	int			retry;
	struct addrinfo		hints;
	u_int			qflags;
	gai_sometime_callback	callback;
	void *			context;
	size_t			nodesize;
	size_t			servsize;
} blocking_gai_req;

typedef struct blocking_gai_resp_tag {
	size_t			octets;
	int			retcode;
	int			retry;
	int			gai_errno; /* for EAI_SYSTEM case */
	int			ai_count;
	/*
	 * Followed by ai_count struct addrinfo and then ai_count
	 * sockaddr_u and finally the canonical name strings.
	 */
} blocking_gai_resp;

typedef struct blocking_gni_req_tag {
	size_t			octets;
	u_int			dns_idx;
	time_t			scheduled;
	time_t			earliest;
	int			retry;
	size_t			hostoctets;
	size_t			servoctets;
	int			flags;
	gni_sometime_callback	callback;
	void *			context;
	sockaddr_u		socku;
} blocking_gni_req;

typedef struct blocking_gni_resp_tag {
	size_t			octets;
	int			retcode;
	int			gni_errno; /* for EAI_SYSTEM case */
	int			retry;
	size_t			hostoctets;
	size_t			servoctets;
	/*
	 * Followed by hostoctets bytes of null-terminated host,
	 * then servoctets bytes of null-terminated service.
	 */
} blocking_gni_resp;

/* per-DNS-worker state in parent */
typedef struct dnschild_ctx_tag {
	u_int	index;
	time_t	next_dns_timeslot;
} dnschild_ctx;

/* per-DNS-worker state in worker */
typedef struct dnsworker_ctx_tag {
	blocking_child *	c;
	time_t			ignore_scheduled_before;
#ifdef HAVE_RES_INIT
	time_t	next_res_init;
#endif
} dnsworker_ctx;


/* === variables === */
dnschild_ctx **		dnschild_contexts;		/* parent */
u_int			dnschild_contexts_alloc;
dnsworker_ctx **	dnsworker_contexts;		/* child */
u_int			dnsworker_contexts_alloc;

#ifdef HAVE_RES_INIT
static	time_t		next_res_init;
#endif


/* === forward declarations === */
static	u_int		reserve_dnschild_ctx(void);
static	u_int		get_dnschild_ctx(void);
static	dnsworker_ctx *	get_worker_context(blocking_child *, u_int);
static	void		scheduled_sleep(time_t, time_t,
					dnsworker_ctx *);
static	void		manage_dns_retry_interval(time_t *, time_t *,
						  int *, time_t *,
						  int/*BOOL*/);
static	int		should_retry_dns(int, int);
#ifdef HAVE_RES_INIT
static	void		reload_resolv_conf(dnsworker_ctx *);
#else
# define		reload_resolv_conf(wc)		\
	do {						\
		(void)(wc);				\
	} while (FALSE)
#endif
static	void		getaddrinfo_sometime_complete(blocking_work_req,
						      void *, size_t,
						      void *);
static	void		getnameinfo_sometime_complete(blocking_work_req,
						      void *, size_t,
						      void *);


/* === functions === */
/*
 * getaddrinfo_sometime - uses blocking child to call getaddrinfo then
 *			  invokes provided callback completion function.
 */
int
getaddrinfo_sometime_ex(
	const char *		node,
	const char *		service,
	const struct addrinfo *	hints,
	int			retry,
	gai_sometime_callback	callback,
	void *			context,
	u_int			qflags
	)
{
	blocking_gai_req *	gai_req;
	u_int			idx;
	dnschild_ctx *		child_ctx;
	size_t			req_size;
	size_t			nodesize;
	size_t			servsize;
	time_t			now;
	
	REQUIRE(NULL != node);
	if (NULL != hints) {
		REQUIRE(0 == hints->ai_addrlen);
		REQUIRE(NULL == hints->ai_addr);
		REQUIRE(NULL == hints->ai_canonname);
		REQUIRE(NULL == hints->ai_next);
	}

	idx = get_dnschild_ctx();
	child_ctx = dnschild_contexts[idx];

	nodesize = strlen(node) + 1;
	servsize = strlen(service) + 1;
	req_size = sizeof(*gai_req) + nodesize + servsize;

	gai_req = emalloc_zero(req_size);

	gai_req->octets = req_size;
	gai_req->dns_idx = idx;
	now = time(NULL);
	gai_req->scheduled = now;
	gai_req->earliest = max(now, child_ctx->next_dns_timeslot);
	child_ctx->next_dns_timeslot = gai_req->earliest;
	if (hints != NULL)
		gai_req->hints = *hints;
	gai_req->retry = retry;
	gai_req->callback = callback;
	gai_req->context = context;
	gai_req->nodesize = nodesize;
	gai_req->servsize = servsize;
	gai_req->qflags = qflags;

	memcpy((char *)gai_req + sizeof(*gai_req), node, nodesize);
	memcpy((char *)gai_req + sizeof(*gai_req) + nodesize, service,
	       servsize);

	if (queue_blocking_request(
		BLOCKING_GETADDRINFO,
		gai_req,
		req_size, 
		&getaddrinfo_sometime_complete, 
		gai_req)) {

		msyslog(LOG_ERR, "unable to queue getaddrinfo request");
		errno = EFAULT;
		return -1;
	}

	return 0;
}

int
blocking_getaddrinfo(
	blocking_child *	c,
	blocking_pipe_header *	req
	)
{
	blocking_gai_req *	gai_req;
	dnsworker_ctx *		worker_ctx;
	blocking_pipe_header *	resp;
	blocking_gai_resp *	gai_resp;
	char *			node;
	char *			service;
	struct addrinfo *	ai_res;
	struct addrinfo *	ai;
	struct addrinfo *	serialized_ai;
	size_t			canons_octets;
	size_t			this_octets;
	size_t			resp_octets;
	char *			cp;
	time_t			time_now;

	gai_req = (void *)((char *)req + sizeof(*req));
	node = (char *)gai_req + sizeof(*gai_req);
	service = node + gai_req->nodesize;

	worker_ctx = get_worker_context(c, gai_req->dns_idx);
	scheduled_sleep(gai_req->scheduled, gai_req->earliest,
			worker_ctx);
	reload_resolv_conf(worker_ctx);

	/*
	 * Take a shot at the final size, better to overestimate
	 * at first and then realloc to a smaller size.
	 */

	resp_octets = sizeof(*resp) + sizeof(*gai_resp) +
		      16 * (sizeof(struct addrinfo) +
			    sizeof(sockaddr_u)) +
		      256;
	resp = emalloc_zero(resp_octets);
	gai_resp = (void *)(resp + 1);

	TRACE(2, ("blocking_getaddrinfo given node %s serv %s fam %d flags %x\n", 
		  node, service, gai_req->hints.ai_family,
		  gai_req->hints.ai_flags));
#ifdef DEBUG
	if (debug >= 2)
		fflush(stdout);
#endif	
	ai_res = NULL;
	gai_resp->retcode = getaddrinfo(node, service, &gai_req->hints,
					&ai_res);
	gai_resp->retry = gai_req->retry;
#ifdef EAI_SYSTEM
	if (EAI_SYSTEM == gai_resp->retcode)
		gai_resp->gai_errno = errno;
#endif
	canons_octets = 0;

	if (0 == gai_resp->retcode) {
		ai = ai_res;
		while (NULL != ai) {
			gai_resp->ai_count++;
			if (ai->ai_canonname)
				canons_octets += strlen(ai->ai_canonname) + 1;
			ai = ai->ai_next;
		}
		/*
		 * If this query succeeded only after retrying, DNS may have
		 * just become responsive.  Ignore previously-scheduled
		 * retry sleeps once for each pending request, similar to
		 * the way scheduled_sleep() does when its worker_sleep()
		 * is interrupted.
		 */
		if (gai_resp->retry > INITIAL_DNS_RETRY) {
			time_now = time(NULL);
			worker_ctx->ignore_scheduled_before = time_now;
			TRACE(1, ("DNS success after retry, ignoring sleeps scheduled before now (%s)\n",
				  humantime(time_now)));
		}
	}

	/*
	 * Our response consists of a header, followed by ai_count 
	 * addrinfo structs followed by ai_count sockaddr_storage 
	 * structs followed by the canonical names.
	 */
	gai_resp->octets = sizeof(*gai_resp)
			    + gai_resp->ai_count
				* (sizeof(gai_req->hints)
				   + sizeof(sockaddr_u))
			    + canons_octets;

	resp_octets = sizeof(*resp) + gai_resp->octets;
	resp = erealloc(resp, resp_octets);
	gai_resp = (void *)(resp + 1);

	/* cp serves as our current pointer while serializing */
	cp = (void *)(gai_resp + 1);
	canons_octets = 0;

	if (0 == gai_resp->retcode) {
		ai = ai_res;
		while (NULL != ai) {
			memcpy(cp, ai, sizeof(*ai));
			serialized_ai = (void *)cp;
			cp += sizeof(*ai);

			/* transform ai_canonname into offset */
			if (NULL != ai->ai_canonname) {
				serialized_ai->ai_canonname = (char *)canons_octets;
				canons_octets += strlen(ai->ai_canonname) + 1;
			}
			
			/* leave fixup of ai_addr pointer for receiver */

			ai = ai->ai_next;
		}

		ai = ai_res;
		while (NULL != ai) {
			INSIST(ai->ai_addrlen <= sizeof(sockaddr_u));
			memcpy(cp, ai->ai_addr, ai->ai_addrlen);
			cp += sizeof(sockaddr_u);

			ai = ai->ai_next;
		}

		ai = ai_res;
		while (NULL != ai) {
			if (NULL != ai->ai_canonname) {
				this_octets = strlen(ai->ai_canonname) + 1;
				memcpy(cp, ai->ai_canonname, this_octets);
				cp += this_octets;
			}

			ai = ai->ai_next;
		}
		freeaddrinfo(ai_res);
	}

	/*
	 * make sure our walk and earlier calc match
	 */
	DEBUG_INSIST((size_t)(cp - (char *)resp) == resp_octets);

	if (queue_blocking_response(c, resp, resp_octets, req)) {
		msyslog(LOG_ERR, "blocking_getaddrinfo can not queue response");
		return -1;
	}

	return 0;
}

int
getaddrinfo_sometime(
	const char *		node,
	const char *		service,
	const struct addrinfo *	hints,
	int			retry,
	gai_sometime_callback	callback,
	void *			context
	)
{
	return getaddrinfo_sometime_ex(node, service, hints, retry,
				       callback, context, 0);
}


static void
getaddrinfo_sometime_complete(
	blocking_work_req	rtype,
	void *			context,
	size_t			respsize,
	void *			resp
	)
{
	blocking_gai_req *	gai_req;
	blocking_gai_resp *	gai_resp;
	dnschild_ctx *		child_ctx;
	struct addrinfo *	ai;
	struct addrinfo *	next_ai;
	sockaddr_u *		psau;
	char *			node;
	char *			service;
	char *			canon_start;
	time_t			time_now;
	int			again, noerr;
	int			af;
	const char *		fam_spec;
	int			i;

	gai_req = context;
	gai_resp = resp;

	DEBUG_REQUIRE(BLOCKING_GETADDRINFO == rtype);
	DEBUG_REQUIRE(respsize == gai_resp->octets);

	node = (char *)gai_req + sizeof(*gai_req);
	service = node + gai_req->nodesize;

	child_ctx = dnschild_contexts[gai_req->dns_idx];

	if (0 == gai_resp->retcode) {
		/*
		 * If this query succeeded only after retrying, DNS may have
		 * just become responsive.
		 */
		if (gai_resp->retry > INITIAL_DNS_RETRY) {
			time_now = time(NULL);
			child_ctx->next_dns_timeslot = time_now;
			TRACE(1, ("DNS success after retry, %u next_dns_timeslot reset (%s)\n",
				  gai_req->dns_idx, humantime(time_now)));
		}
	} else {
		noerr = !!(gai_req->qflags & GAIR_F_IGNDNSERR);
		again = noerr || should_retry_dns(
					gai_resp->retcode, gai_resp->gai_errno);
		/*
		 * exponential backoff of DNS retries to 64s
		 */
		if (gai_req->retry > 0 && again) {
			/* log the first retry only */
			if (INITIAL_DNS_RETRY == gai_req->retry)
				NLOG(NLOG_SYSINFO) {
					af = gai_req->hints.ai_family;
					fam_spec = (AF_INET6 == af)
						       ? " (AAAA)"
						       : (AF_INET == af)
							     ? " (A)"
							     : "";
#ifdef EAI_SYSTEM
					if (EAI_SYSTEM == gai_resp->retcode) {
						errno = gai_resp->gai_errno;
						msyslog(LOG_INFO,
							"retrying DNS %s%s: EAI_SYSTEM %d: %m",
							node, fam_spec,
							gai_resp->gai_errno);
					} else
#endif
						msyslog(LOG_INFO,
							"retrying DNS %s%s: %s (%d)",
							node, fam_spec,
							gai_strerror(gai_resp->retcode),
							gai_resp->retcode);
				}
			manage_dns_retry_interval(
				&gai_req->scheduled, &gai_req->earliest,
				&gai_req->retry, &child_ctx->next_dns_timeslot,
				noerr);
			if (!queue_blocking_request(
					BLOCKING_GETADDRINFO,
					gai_req,
					gai_req->octets,
					&getaddrinfo_sometime_complete,
					gai_req))
				return;
			else
				msyslog(LOG_ERR,
					"unable to retry hostname %s",
					node);
		}
	}

	/*
	 * fixup pointers in returned addrinfo array
	 */
	ai = (void *)((char *)gai_resp + sizeof(*gai_resp));
	next_ai = NULL;
	for (i = gai_resp->ai_count - 1; i >= 0; i--) {
		ai[i].ai_next = next_ai;
		next_ai = &ai[i];
	}

	psau = (void *)((char *)ai + gai_resp->ai_count * sizeof(*ai));
	canon_start = (char *)psau + gai_resp->ai_count * sizeof(*psau);

	for (i = 0; i < gai_resp->ai_count; i++) {
		if (NULL != ai[i].ai_addr)
			ai[i].ai_addr = &psau->sa;
		psau++;
		if (NULL != ai[i].ai_canonname)
			ai[i].ai_canonname += (size_t)canon_start;
	}

	ENSURE((char *)psau == canon_start);

	if (!gai_resp->ai_count)
		ai = NULL;
	
	(*gai_req->callback)(gai_resp->retcode, gai_resp->gai_errno,
			     gai_req->context, node, service, 
			     &gai_req->hints, ai);

	free(gai_req);
	/* gai_resp is part of block freed by process_blocking_resp() */
}


#ifdef TEST_BLOCKING_WORKER
void gai_test_callback(int rescode, int gai_errno, void *context, const char *name, const char *service, const struct addrinfo *hints, const struct addrinfo *ai_res)
{
	sockaddr_u addr;

	if (rescode) {
		TRACE(1, ("gai_test_callback context %p error rescode %d %s serv %s\n",
			  context, rescode, name, service));
		return;
	}
	while (!rescode && NULL != ai_res) {
		ZERO_SOCK(&addr);
		memcpy(&addr, ai_res->ai_addr, ai_res->ai_addrlen);
		TRACE(1, ("ctx %p fam %d addr %s canon '%s' type %s at %p ai_addr %p ai_next %p\n", 
			  context,
			  AF(&addr),
			  stoa(&addr), 
			  (ai_res->ai_canonname)
			      ? ai_res->ai_canonname
			      : "",
			  (SOCK_DGRAM == ai_res->ai_socktype) 
			      ? "DGRAM" 
			      : (SOCK_STREAM == ai_res->ai_socktype) 
				    ? "STREAM" 
				    : "(other)",
			  ai_res,
			  ai_res->ai_addr,
			  ai_res->ai_next));

		getnameinfo_sometime((sockaddr_u *)ai_res->ai_addr, 128, 32, 0, gni_test_callback, context);

		ai_res = ai_res->ai_next;
	}
}
#endif	/* TEST_BLOCKING_WORKER */


int
getnameinfo_sometime(
	sockaddr_u *		psau,
	size_t			hostoctets,
	size_t			servoctets,
	int			flags,
	gni_sometime_callback	callback,
	void *			context
	)
{
	blocking_gni_req *	gni_req;
	u_int			idx;
	dnschild_ctx *		child_ctx;
	time_t			time_now;
	
	REQUIRE(hostoctets);
	REQUIRE(hostoctets + servoctets < 1024);

	idx = get_dnschild_ctx();
	child_ctx = dnschild_contexts[idx];

	gni_req = emalloc_zero(sizeof(*gni_req));

	gni_req->octets = sizeof(*gni_req);
	gni_req->dns_idx = idx;
	time_now = time(NULL);
	gni_req->scheduled = time_now;
	gni_req->earliest = max(time_now, child_ctx->next_dns_timeslot);
	child_ctx->next_dns_timeslot = gni_req->earliest;
	memcpy(&gni_req->socku, psau, SOCKLEN(psau));
	gni_req->hostoctets = hostoctets;
	gni_req->servoctets = servoctets;
	gni_req->flags = flags;
	gni_req->retry = INITIAL_DNS_RETRY;
	gni_req->callback = callback;
	gni_req->context = context;

	if (queue_blocking_request(
		BLOCKING_GETNAMEINFO,
		gni_req,
		sizeof(*gni_req), 
		&getnameinfo_sometime_complete, 
		gni_req)) {

		msyslog(LOG_ERR, "unable to queue getnameinfo request");
		errno = EFAULT;
		return -1;
	}

	return 0;
}


int
blocking_getnameinfo(
	blocking_child *	c,
	blocking_pipe_header *	req
	)
{
	blocking_gni_req *	gni_req;
	dnsworker_ctx *		worker_ctx;
	blocking_pipe_header *	resp;
	blocking_gni_resp *	gni_resp;
	size_t			octets;
	size_t			resp_octets;
	char *			service;
	char *			cp;
	int			rc;
	time_t			time_now;
	char			host[1024];

	gni_req = (void *)((char *)req + sizeof(*req));

	octets = gni_req->hostoctets + gni_req->servoctets;

	/*
	 * Some alloca() implementations are fragile regarding
	 * large allocations.  We only need room for the host
	 * and service names.
	 */
	REQUIRE(octets < sizeof(host));
	service = host + gni_req->hostoctets;

	worker_ctx = get_worker_context(c, gni_req->dns_idx);
	scheduled_sleep(gni_req->scheduled, gni_req->earliest,
			worker_ctx);
	reload_resolv_conf(worker_ctx);

	/*
	 * Take a shot at the final size, better to overestimate
	 * then realloc to a smaller size.
	 */

	resp_octets = sizeof(*resp) + sizeof(*gni_resp) + octets;
	resp = emalloc_zero(resp_octets);
	gni_resp = (void *)((char *)resp + sizeof(*resp));

	TRACE(2, ("blocking_getnameinfo given addr %s flags 0x%x hostlen %lu servlen %lu\n",
		  stoa(&gni_req->socku), gni_req->flags,
		  (u_long)gni_req->hostoctets, (u_long)gni_req->servoctets));
	
	gni_resp->retcode = getnameinfo(&gni_req->socku.sa,
					SOCKLEN(&gni_req->socku),
					host,
					gni_req->hostoctets,
					service,
					gni_req->servoctets,
					gni_req->flags);
	gni_resp->retry = gni_req->retry;
#ifdef EAI_SYSTEM
	if (EAI_SYSTEM == gni_resp->retcode)
		gni_resp->gni_errno = errno;
#endif

	if (0 != gni_resp->retcode) {
		gni_resp->hostoctets = 0;
		gni_resp->servoctets = 0;
	} else {
		gni_resp->hostoctets = strlen(host) + 1;
		gni_resp->servoctets = strlen(service) + 1;
		/*
		 * If this query succeeded only after retrying, DNS may have
		 * just become responsive.  Ignore previously-scheduled
		 * retry sleeps once for each pending request, similar to
		 * the way scheduled_sleep() does when its worker_sleep()
		 * is interrupted.
		 */
		if (gni_req->retry > INITIAL_DNS_RETRY) {
			time_now = time(NULL);
			worker_ctx->ignore_scheduled_before = time_now;
			TRACE(1, ("DNS success after retrying, ignoring sleeps scheduled before now (%s)\n",
				humantime(time_now)));
		}
	}
	octets = gni_resp->hostoctets + gni_resp->servoctets;
	/*
	 * Our response consists of a header, followed by the host and
	 * service strings, each null-terminated.
	 */
	resp_octets = sizeof(*resp) + sizeof(*gni_resp) + octets;

	resp = erealloc(resp, resp_octets);
	gni_resp = (void *)(resp + 1);

	gni_resp->octets = sizeof(*gni_resp) + octets;

	/* cp serves as our current pointer while serializing */
	cp = (void *)(gni_resp + 1);

	if (0 == gni_resp->retcode) {
		memcpy(cp, host, gni_resp->hostoctets);
		cp += gni_resp->hostoctets;
		memcpy(cp, service, gni_resp->servoctets);
		cp += gni_resp->servoctets;
	}

	INSIST((size_t)(cp - (char *)resp) == resp_octets);
	INSIST(resp_octets - sizeof(*resp) == gni_resp->octets);

	rc = queue_blocking_response(c, resp, resp_octets, req);
	if (rc)
		msyslog(LOG_ERR, "blocking_getnameinfo unable to queue response");
	return rc;
}


static void
getnameinfo_sometime_complete(
	blocking_work_req	rtype,
	void *			context,
	size_t			respsize,
	void *			resp
	)
{
	blocking_gni_req *	gni_req;
	blocking_gni_resp *	gni_resp;
	dnschild_ctx *		child_ctx;
	char *			host;
	char *			service;
	time_t			time_now;
	int			again;

	gni_req = context;
	gni_resp = resp;

	DEBUG_REQUIRE(BLOCKING_GETNAMEINFO == rtype);
	DEBUG_REQUIRE(respsize == gni_resp->octets);

	child_ctx = dnschild_contexts[gni_req->dns_idx];

	if (0 == gni_resp->retcode) {
		/*
		 * If this query succeeded only after retrying, DNS may have
		 * just become responsive.
		 */
		if (gni_resp->retry > INITIAL_DNS_RETRY) {
			time_now = time(NULL);
			child_ctx->next_dns_timeslot = time_now;
			TRACE(1, ("DNS success after retry, %u next_dns_timeslot reset (%s)\n",
				  gni_req->dns_idx, humantime(time_now)));
		}
	} else {
		again = should_retry_dns(gni_resp->retcode, gni_resp->gni_errno);
		/*
		 * exponential backoff of DNS retries to 64s
		 */
		if (gni_req->retry > 0)
			manage_dns_retry_interval(&gni_req->scheduled,
			    &gni_req->earliest, &gni_req->retry,
						  &child_ctx->next_dns_timeslot, FALSE);

		if (gni_req->retry > 0 && again) {
			if (!queue_blocking_request(
				BLOCKING_GETNAMEINFO,
				gni_req,
				gni_req->octets, 
				&getnameinfo_sometime_complete, 
				gni_req))
				return;

			msyslog(LOG_ERR, "unable to retry reverse lookup of %s", stoa(&gni_req->socku));
		}
	}

	if (!gni_resp->hostoctets) {
		host = NULL;
		service = NULL;
	} else {
		host = (char *)gni_resp + sizeof(*gni_resp);
		service = (gni_resp->servoctets) 
			      ? host + gni_resp->hostoctets
			      : NULL;
	}

	(*gni_req->callback)(gni_resp->retcode, gni_resp->gni_errno,
			     &gni_req->socku, gni_req->flags, host,
			     service, gni_req->context);

	free(gni_req);
	/* gni_resp is part of block freed by process_blocking_resp() */
}


#ifdef TEST_BLOCKING_WORKER
void gni_test_callback(int rescode, int gni_errno, sockaddr_u *psau, int flags, const char *host, const char *service, void *context)
{
	if (!rescode)
		TRACE(1, ("gni_test_callback got host '%s' serv '%s' for addr %s context %p\n", 
			  host, service, stoa(psau), context));
	else
		TRACE(1, ("gni_test_callback context %p rescode %d gni_errno %d flags 0x%x addr %s\n",
			  context, rescode, gni_errno, flags, stoa(psau)));
}
#endif	/* TEST_BLOCKING_WORKER */


#ifdef HAVE_RES_INIT
static void
reload_resolv_conf(
	dnsworker_ctx *	worker_ctx
	)
{
	time_t	time_now;

	/*
	 * This is ad-hoc.  Reload /etc/resolv.conf once per minute
	 * to pick up on changes from the DHCP client.  [Bug 1226]
	 * When using threads for the workers, this needs to happen
	 * only once per minute process-wide.
	 */
	time_now = time(NULL);
# ifdef WORK_THREAD
	worker_ctx->next_res_init = next_res_init;
# endif
	if (worker_ctx->next_res_init <= time_now) {
		if (worker_ctx->next_res_init != 0)
			res_init();
		worker_ctx->next_res_init = time_now + 60;
# ifdef WORK_THREAD
		next_res_init = worker_ctx->next_res_init;
# endif
	}
}
#endif	/* HAVE_RES_INIT */


static u_int
reserve_dnschild_ctx(void)
{
	const size_t	ps = sizeof(dnschild_contexts[0]);
	const size_t	cs = sizeof(*dnschild_contexts[0]);
	u_int		c;
	u_int		new_alloc;
	size_t		octets;
	size_t		new_octets;

	c = 0;
	while (TRUE) {
		for ( ; c < dnschild_contexts_alloc; c++) {
			if (NULL == dnschild_contexts[c]) {
				dnschild_contexts[c] = emalloc_zero(cs);

				return c;
			}
		}
		new_alloc = dnschild_contexts_alloc + 20;
		new_octets = new_alloc * ps;
		octets = dnschild_contexts_alloc * ps;
		dnschild_contexts = erealloc_zero(dnschild_contexts,
						  new_octets, octets);
		dnschild_contexts_alloc = new_alloc;
	}
}


static u_int
get_dnschild_ctx(void)
{
	static u_int	shared_ctx = UINT_MAX;

	if (worker_per_query)
		return reserve_dnschild_ctx();

	if (UINT_MAX == shared_ctx)
		shared_ctx = reserve_dnschild_ctx();

	return shared_ctx;
}


static dnsworker_ctx *
get_worker_context(
	blocking_child *	c,
	u_int			idx
	)
{
	u_int		min_new_alloc;
	u_int		new_alloc;
	size_t		octets;
	size_t		new_octets;
	dnsworker_ctx *	retv;

	worker_global_lock(TRUE);
	
	if (dnsworker_contexts_alloc <= idx) {
		min_new_alloc = 1 + idx;
		/* round new_alloc up to nearest multiple of 4 */
		new_alloc = (min_new_alloc + 4) & ~(4 - 1);
		new_octets = new_alloc * sizeof(dnsworker_ctx*);
		octets = dnsworker_contexts_alloc * sizeof(dnsworker_ctx*);
		dnsworker_contexts = erealloc_zero(dnsworker_contexts,
						   new_octets, octets);
		dnsworker_contexts_alloc = new_alloc;
		retv = emalloc_zero(sizeof(dnsworker_ctx));
		dnsworker_contexts[idx] = retv;
	} else if (NULL == (retv = dnsworker_contexts[idx])) {
		retv = emalloc_zero(sizeof(dnsworker_ctx));
		dnsworker_contexts[idx] = retv;
	}
	
	worker_global_lock(FALSE);
	
	ZERO(*retv);
	retv->c = c;
	return retv;
}


static void
scheduled_sleep(
	time_t		scheduled,
	time_t		earliest,
	dnsworker_ctx *	worker_ctx
	)
{
	time_t now;

	if (scheduled < worker_ctx->ignore_scheduled_before) {
		TRACE(1, ("ignoring sleep until %s scheduled at %s (before %s)\n",
			  humantime(earliest), humantime(scheduled),
			  humantime(worker_ctx->ignore_scheduled_before)));
		return;
	}

	now = time(NULL);

	if (now < earliest) {
		TRACE(1, ("sleep until %s scheduled at %s (>= %s)\n",
			  humantime(earliest), humantime(scheduled),
			  humantime(worker_ctx->ignore_scheduled_before)));
		if (-1 == worker_sleep(worker_ctx->c, earliest - now)) {
			/* our sleep was interrupted */
			now = time(NULL);
			worker_ctx->ignore_scheduled_before = now;
#ifdef HAVE_RES_INIT
			worker_ctx->next_res_init = now + 60;
			next_res_init = worker_ctx->next_res_init;
			res_init();
#endif
			TRACE(1, ("sleep interrupted by daemon, ignoring sleeps scheduled before now (%s)\n",
				  humantime(worker_ctx->ignore_scheduled_before)));
		}
	}
}


/*
 * manage_dns_retry_interval is a helper used by
 * getaddrinfo_sometime_complete and getnameinfo_sometime_complete
 * to calculate the new retry interval and schedule the next query.
 */
static void
manage_dns_retry_interval(
	time_t *	pscheduled,
	time_t *	pwhen,
	int *		pretry,
	time_t *	pnext_timeslot,
	int		forever
	)
{
	time_t	now;
	time_t	when;
	int	retry;
	int	retmax;
		
	now = time(NULL);
	retry = *pretry;
	when = max(now + retry, *pnext_timeslot);
	*pnext_timeslot = when;

	/* this exponential backoff is slower than doubling up: The
	 * sequence goes 2-3-4-6-8-12-16-24-32... and the upper limit is
	 * 64 seconds for things that should not repeat forever, and
	 * 1024 when repeated forever.
	 */
	retmax = forever ? 1024 : 64;
	retry <<= 1;
	if (retry & (retry - 1))
		retry &= (retry - 1);
	else
		retry -= (retry >> 2);
	retry = min(retmax, retry);

	*pscheduled = now;
	*pwhen = when;
	*pretry = retry;
}

/*
 * should_retry_dns is a helper used by getaddrinfo_sometime_complete
 * and getnameinfo_sometime_complete which implements ntpd's DNS retry
 * policy.
 */
static int
should_retry_dns(
	int	rescode,
	int	res_errno
	)
{
	static int	eai_again_seen;
	int		again;
#if defined (EAI_SYSTEM) && defined(DEBUG)
	char		msg[256];
#endif

	/*
	 * If the resolver failed, see if the failure is
	 * temporary. If so, return success.
	 */
	again = 0;

	switch (rescode) {

	case EAI_FAIL:
		again = 1;
		break;

	case EAI_AGAIN:
		again = 1;
		eai_again_seen = 1;		/* [Bug 1178] */
		break;

	case EAI_NONAME:
#if defined(EAI_NODATA) && (EAI_NODATA != EAI_NONAME)
	case EAI_NODATA:
#endif
		again = !eai_again_seen;	/* [Bug 1178] */
		break;

#ifdef EAI_SYSTEM
	case EAI_SYSTEM:
		/* 
		 * EAI_SYSTEM means the real error is in errno.  We should be more
		 * discriminating about which errno values require retrying, but
		 * this matches existing behavior.
		 */
		again = 1;
# ifdef DEBUG
		errno_to_str(res_errno, msg, sizeof(msg));
		TRACE(1, ("intres: EAI_SYSTEM errno %d (%s) means try again, right?\n",
			  res_errno, msg));
# endif
		break;
#endif
	}

	TRACE(2, ("intres: resolver returned: %s (%d), %sretrying\n",
		  gai_strerror(rescode), rescode, again ? "" : "not "));

	return again;
}

#else	/* !WORKER follows */
int ntp_intres_nonempty_compilation_unit;
#endif
