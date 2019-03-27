 /*
  * Warning - this relies heavily on the TLI implementation in PTX 2.X and will
  * probably not work under PTX 4.
  * 
  * Author: Tim Wright, Sequent Computer Systems Ltd., UK.
  * 
  * Modified slightly to conform to the new internal interfaces - Wietse
  */

#ifndef lint
static char sccsid[] = "@(#) tli-sequent.c 1.1 94/12/28 17:42:51";
#endif

#ifdef TLI_SEQUENT

/* System libraries. */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/tiuser.h>
#include <sys/stream.h>
#include <sys/stropts.h>
#include <sys/tihdr.h>
#include <sys/timod.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>

extern int errno;
extern char *sys_errlist[];
extern int sys_nerr;
extern int t_errno;
extern char *t_errlist[];
extern int t_nerr;

/* Local stuff. */

#include "tcpd.h"
#include "tli-sequent.h"

/* Forward declarations. */

static char *tli_error();
static void tli_sink();

/* tli_host - determine endpoint info */

int     tli_host(request)
struct request_info *request;
{
    static struct sockaddr_in client;
    static struct sockaddr_in server;
    struct _ti_user *tli_state_ptr;
    union T_primitives *TSI_prim_ptr;
    struct strpeek peek;
    int     len;

    /*
     * Use DNS and socket routines for name and address conversions.
     */

    sock_methods(request);

    /*
     * Find out the client address using getpeerinaddr(). This call is the
     * TLI equivalent to getpeername() under Dynix/ptx.
     */

    len = sizeof(client);
    t_sync(request->fd);
    if (getpeerinaddr(request->fd, &client, len) < 0) {
	tcpd_warn("can't get client address: %s", tli_error());
	return;
    }
    request->client->sin = &client;

    /* Call TLI utility routine to get information on endpoint */
    if ((tli_state_ptr = _t_checkfd(request->fd)) == NULL)
	return;

    if (tli_state_ptr->ti_servtype == T_CLTS) {
	/* UDP - may need to get address the hard way */
	if (client.sin_addr.s_addr == 0) {
	    /* The UDP endpoint is not connected so we didn't get the */
	    /* remote address - get it the hard way ! */

	    /* Look at the control part of the top message on the stream */
	    /* we don't want to remove it from the stream so we use I_PEEK */
	    peek.ctlbuf.maxlen = tli_state_ptr->ti_ctlsize;
	    peek.ctlbuf.len = 0;
	    peek.ctlbuf.buf = tli_state_ptr->ti_ctlbuf;
	    /* Don't even look at the data */
	    peek.databuf.maxlen = -1;
	    peek.databuf.len = 0;
	    peek.databuf.buf = 0;
	    peek.flags = 0;

	    switch (ioctl(request->fd, I_PEEK, &peek)) {
	    case -1:
		tcpd_warn("can't peek at endpoint: %s", tli_error());
		return;
	    case 0:
		/* No control part - we're hosed */
		tcpd_warn("can't get UDP info: %s", tli_error());
		return;
	    default:
		/* FALL THROUGH */
		;
	    }
	    /* Can we even check the PRIM_type ? */
	    if (peek.ctlbuf.len < sizeof(long)) {
		tcpd_warn("UDP control info garbage");
		return;
	    }
	    TSI_prim_ptr = (union T_primitives *) peek.ctlbuf.buf;
	    if (TSI_prim_ptr->type != T_UNITDATA_IND) {
		tcpd_warn("wrong type for UDP control info");
		return;
	    }
	    /* Validate returned unitdata indication packet */
	    if ((peek.ctlbuf.len < sizeof(struct T_unitdata_ind)) ||
		((TSI_prim_ptr->unitdata_ind.OPT_length != 0) &&
		 (peek.ctlbuf.len <
		  TSI_prim_ptr->unitdata_ind.OPT_length +
		  TSI_prim_ptr->unitdata_ind.OPT_offset))) {
		tcpd_warn("UDP control info garbaged");
		return;
	    }
	    /* Extract the address */
	    memcpy(&client,
		   peek.ctlbuf.buf + TSI_prim_ptr->unitdata_ind.SRC_offset,
		   TSI_prim_ptr->unitdata_ind.SRC_length);
	}
	request->sink = tli_sink;
    }
    if (getmyinaddr(request->fd, &server, len) < 0)
	tcpd_warn("can't get local address: %s", tli_error());
    else
	request->server->sin = &server;
}

/* tli_error - convert tli error number to text */

static char *tli_error()
{
    static char buf[40];

    if (t_errno != TSYSERR) {
	if (t_errno < 0 || t_errno >= t_nerr) {
	    sprintf(buf, "Unknown TLI error %d", t_errno);
	    return (buf);
	} else {
	    return (t_errlist[t_errno]);
	}
    } else {
	if (errno < 0 || errno >= sys_nerr) {
	    sprintf(buf, "Unknown UNIX error %d", errno);
	    return (buf);
	} else {
	    return (sys_errlist[errno]);
	}
    }
}

/* tli_sink - absorb unreceived datagram */

static void tli_sink(fd)
int     fd;
{
    struct t_unitdata *unit;
    int     flags;

    /*
     * Something went wrong. Absorb the datagram to keep inetd from looping.
     * Allocate storage for address, control and data. If that fails, sleep
     * for a couple of seconds in an attempt to keep inetd from looping too
     * fast.
     */

    if ((unit = (struct t_unitdata *) t_alloc(fd, T_UNITDATA, T_ALL)) == 0) {
	tcpd_warn("t_alloc: %s", tli_error());
	sleep(5);
    } else {
	(void) t_rcvudata(fd, unit, &flags);
	t_free((void *) unit, T_UNITDATA);
    }
}

#endif /* TLI_SEQUENT */
