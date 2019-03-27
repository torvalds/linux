/*
 * Copyright (c) 2001-2003
 *	Fraunhofer Institute for Open Communication Systems (FhG Fokus).
 * 	All rights reserved.
 *
 * Author: Harti Brandt <harti@freebsd.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
 * $Begemot: libunimsg/sscop/sscop_main.c,v 1.5 2005/05/23 11:46:17 brandt_h Exp $
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <assert.h>
#include <err.h>

#include <netnatm/unimsg.h>
#include <netnatm/saal/sscop.h>
#include "common.h"

static int sigusr1;		/* got SIGUSR1 */
static int unidir;		/* write only user */
static int end_at_eof = 1;	/* send RELEASE_request at user EOF */

static volatile int ready;	/* flag if connection is established */
static volatile int finished;	/* got release confirm or indication */

static const char usgtxt[] = "\
SSCOP transport protocol\n\
Usage: sscop [-h] [-Fbefirwx3] [-ap=v] [-lN] [-tt=m] [-v X] [-V X] [-W N]\n\
Options:\n\
  -F	  use framing for sscop also\n\
  -V X	  set verbose flags to hex X\n\
  -W N	  set initial window to N\n\
  -a p=v  set parameter 'p' to 'v'\n\
  -b	  enable robustness enhancement\n\
  -e	  don't RELEASE_request on user EOF\n\
  -f	  use begemot frame functions for user fd\n\
  -h	  print this info\n\
  -i	  use user fd only for output\n\
  -lN	  loose every nth message\n\
  -r	  reverse user and sscop file descriptors\n\
  -t t=m  set timer 't' to 'm' milliseconds\n\
  -v X	  set sscop verbose flags to hex X\n\
  -w	  don't start conversation\n\
  -x	  enable POLL after retransmission\n\
  -3	  redirect output to fd 3\n\
Timers are cc, poll, ka, nr or idle; parameters are j, k, cc, pd or stat.\n";

static void sscop_send_manage(struct sscop *, void *,
	enum sscop_maasig, struct uni_msg *, u_int, u_int);
static void sscop_send_upper(struct sscop *, void *, enum sscop_aasig,
	struct SSCOP_MBUF_T *, u_int);
static void sscop_send_lower(struct sscop *, void *, struct SSCOP_MBUF_T *);

static const struct sscop_funcs sscop_funcs = {
	sscop_send_manage,
	sscop_send_upper,
	sscop_send_lower,
	sscop_verbose,
	sscop_start_timer,
	sscop_stop_timer
};

/*
 * SSCOP file descriptor is ready. Allocate and read one message
 * and dispatch a signal.
 */
#ifdef USE_LIBBEGEMOT
static void
proto_infunc(int fd, int mask __unused, void *uap)
#else
static void
proto_infunc(evContext ctx __unused, void *uap, int fd, int mask __unused)
#endif
{
	struct uni_msg *m;

	if ((m = proto_msgin(fd)) != NULL)
		sscop_input((struct sscop *)uap, m);
}

/*
 * User input. Allocate and read message and dispatch signal.
 */
#ifdef USE_LIBBEGEMOT
static void
user_infunc(int fd, int mask __unused, void *uap)
#else
static void
user_infunc(evContext ctx __unused, void *uap, int fd, int mask __unused)
#endif
{
	struct uni_msg *m;

	if ((m = user_msgin(fd)) != NULL)
		sscop_aasig((struct sscop *)uap, SSCOP_DATA_request, m, 0);

	else if (end_at_eof)
		sscop_aasig((struct sscop *)uap, SSCOP_RELEASE_request, 0, 0);
}

static void
onusr1(int s __unused)
{
	sigusr1++;
}

int
main(int argc, char *argv[])
{
	int opt;
	struct sscop *sscop;
	struct sscop_param param;
	struct sigaction sa;
	int wait = 0;
	u_int mask;
#ifndef USE_LIBBEGEMOT
	evEvent ev;
#endif

	/*
	 * Default is to have the USER on stdin and SSCOP on stdout
	 */
	sscop_fd = 0;
	user_fd = 1;
	user_out_fd = -1;

	memset(&param, 0, sizeof(param));
	param.maxk = MAXUSRMSG;
	param.maxj = 0;
	param.maxcc = 4;
	mask = SSCOP_SET_MAXK | SSCOP_SET_MAXJ | SSCOP_SET_MAXCC;

	while((opt = getopt(argc, argv, "3a:befFhil:rt:v:V:wW:x")) != -1)
		switch(opt) {

		  case '3':
			user_out_fd = 3;
			break;

		  case 'e':
			end_at_eof = 0;
			break;

		  case 'f':
			useframe = 1;
			break;

		  case 'F':
			sscopframe = 1;
			break;

		  case 'h':
			fprintf(stderr, usgtxt);
			exit(0);

		  case 'i':
			unidir++;
			break;

		  case 'l':
			loose = strtoul(optarg, NULL, 0);
			break;

		  case 'r':
			sscop_fd = 1;
			user_fd = 0;
			break;

		  case 'v':
			sscop_vflag = strtoul(optarg, NULL, 16);
			break;

		  case 'V':
			verbose = strtoul(optarg, NULL, 16);
			break;

		  case 'w':
			wait = 1;
			break;

		  case 'a':
		  case 't':
		  case 'b':
		  case 'x':
		  case 'W':
			parse_param(&param, &mask, opt, optarg);
			break;
		}

	if(user_out_fd < 0)
		user_out_fd = user_fd;

#ifndef USE_LIBBEGEMOT
	if (evCreate(&evctx))
		err(1, "evCreate");
#endif

	/*
	 * Catch USR1
	 */
	sa.sa_handler = onusr1;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if(sigaction(SIGUSR1, &sa, NULL))
		err(1, "sigaction(SIGUSR1)");

	/*
	 * Allocate and initialize SSCOP
	 */
	if ((sscop = sscop_create(NULL, &sscop_funcs)) == NULL)
		err(1, NULL);
	sscop_setdebug(sscop, sscop_vflag);
	if ((errno = sscop_setparam(sscop, &param, &mask)) != 0)
		err(1, "can't set sscop parameters %#x", mask);

	/*
	 * Register sscop fd
	 */
#ifdef USE_LIBBEGEMOT
	if ((sscop_h = poll_register(sscop_fd, proto_infunc,
	    sscop, POLL_IN)) == -1)
		err(1, "can't select on sscop fd");
#else
	if (evSelectFD(evctx, sscop_fd, EV_READ, proto_infunc, sscop, &sscop_h))
		err(1, "can't select on sscop fd");
#endif

	/*
	 * if we are active - send establish request
	 */
	if(!wait)
		sscop_aasig(sscop, SSCOP_ESTABLISH_request, NULL, 1);

	/*
	 * Run protocol until it get's ready
	 */
	while (sscop_fd >= 0 && !ready) {
#ifdef USE_LIBBEGEMOT
		poll_dispatch(1);
#else
		if (evGetNext(evctx, &ev, EV_WAIT) == 0) {
			if (evDispatch(evctx, ev))
				err(1, "dispatch event");
		} else if (errno != EINTR)
			err(1, "get event");
#endif
	}

	/*
	 * If this led to a closed file - exit.
	 */
	if (sscop_fd < 0) {
		VERBOSE(("SSCOP file descriptor closed - exiting"));
		sscop_destroy(sscop);
		return 0;
	}

	VERBOSE(("READY - starting data transfer"));

	if (!unidir &&
#ifdef USE_LIBBEGEMOT
	    ((user_h = poll_register(user_fd, user_infunc, sscop, POLL_IN)) == -1))
#else
	    evSelectFD(evctx, user_fd, EV_READ, user_infunc, sscop, &user_h))
#endif
		err(1, "can't select on sscop fd");

	while (!sigusr1 && sscop_fd >= 0) {
#ifdef USE_LIBBEGEMOT
		poll_dispatch(1);
#else
		if (evGetNext(evctx, &ev, EV_WAIT) == 0) {
			if (evDispatch(evctx, ev))
				err(1, "dispatch event");
		} else if (errno != EINTR)
			err(1, "get event");
#endif
	}

	if (sigusr1 && sscop_fd >= 0) {
		/*
		 * Release if we still have the connection
		 */
		sscop_aasig(sscop, SSCOP_RELEASE_request, NULL, 0);
		while (!finished && sscop_fd >= 0) {
#ifdef USE_LIBBEGEMOT
			poll_dispatch(1);
#else
			if (evGetNext(evctx, &ev, EV_WAIT) == 0) {
				if (evDispatch(evctx, ev))
					err(1, "dispatch event");
			} else if (errno != EINTR)
				err(1, "get event");
#endif
		}
	}

	VERBOSE(("SSCOP file descriptor closed - exiting"));
	sscop_destroy(sscop);

	return (0);
}



/*
 * AAL OUTPUT
 */
static void
sscop_send_lower(struct sscop *sscop __unused, void *arg __unused,
    struct SSCOP_MBUF_T *m)
{
	proto_msgout(m);
}


/*
 * Write the message to the user and move the window
 */
static void
uoutput(struct sscop *sscop, struct uni_msg *m)
{
	user_msgout(m);
	sscop_window(sscop, +1);
}

/*
 * SSCOP AA-SIGNALS
 */
static void
sscop_send_upper(struct sscop *sscop, void *arg __unused, enum sscop_aasig sig,
    struct SSCOP_MBUF_T *m, u_int p __unused)
{
	VERBOSE(("--> got aa %d(%s)", sig, sscop_signame(sig)));

	switch (sig) {

	  case SSCOP_RELEASE_indication:
		if (end_at_eof) {
			VERBOSE((" ... exiting"));
#ifdef USE_LIBBEGEMOT
			poll_unregister(sscop_h);
#else
			evDeselectFD(evctx, sscop_h);
#endif
			(void)close(sscop_fd);
			sscop_fd = -1;
		}
		finished++;
		if (m)
			uni_msg_destroy(m);
		break;

	  case SSCOP_RELEASE_confirm:
		if (end_at_eof) {
			VERBOSE((" ... exiting"));
#ifdef USE_LIBBEGEMOT
			poll_unregister(sscop_h);
#else
			evDeselectFD(evctx, sscop_h);
#endif
			(void)close(sscop_fd);
			sscop_fd = -1;
		}
		finished++;
		break;

	  case SSCOP_ESTABLISH_indication:
		sscop_aasig(sscop, SSCOP_ESTABLISH_response, NULL, 1);
		ready++;
		if (m)
			uni_msg_destroy(m);
		break;

	  case SSCOP_ESTABLISH_confirm:
		ready++;
		if (m)
			uni_msg_destroy(m);
		break;

	  case SSCOP_DATA_indication:
		assert(m != NULL);
		uoutput(sscop, m);
		break;

	  case SSCOP_UDATA_indication:
		assert(m != NULL);
		VERBOSE(("UDATA.indication ignored"));
		uni_msg_destroy(m);
		break;

	  case SSCOP_RECOVER_indication:
		sscop_aasig(sscop, SSCOP_RECOVER_response, NULL, 0);
		break;

	  case SSCOP_RESYNC_indication:
		sscop_aasig(sscop, SSCOP_RESYNC_response, NULL, 0);
		if (m)
			uni_msg_destroy(m);
		break;

	  case SSCOP_RESYNC_confirm:
		break;

	  case SSCOP_RETRIEVE_indication:
	  case SSCOP_RETRIEVE_COMPL_indication:
		warnx("Ooops. A retrieve indication");
		abort();

	  case SSCOP_ESTABLISH_request:
	  case SSCOP_RELEASE_request:
	  case SSCOP_ESTABLISH_response:
	  case SSCOP_DATA_request:
	  case SSCOP_UDATA_request:
	  case SSCOP_RECOVER_response:
	  case SSCOP_RESYNC_request:
	  case SSCOP_RESYNC_response:
	  case SSCOP_RETRIEVE_request:
		warnx("bad signal for this direction");
		abort();
	}
}

/*
 * This get's called for MAAL
 */
static void
sscop_send_manage(struct sscop *sscop __unused, void *arg __unused,
    enum sscop_maasig sig, struct uni_msg *m, u_int error, u_int cnt)
{
	VERBOSE(("--> got maa %d(%s)", sig, sscop_msigname(sig)));

	switch (sig) {

	  case SSCOP_MDATA_indication:
		VERBOSE(("MDATA.indication ignored"));
		uni_msg_destroy(m);
		break;

	  case SSCOP_MERROR_indication:
		VERBOSE(("MAAL-ERROR.indication '%c' %u", error, cnt));
		break;

	  case SSCOP_MDATA_request:
		warnx("bad signal for this direction");
		abort();
	}
}
