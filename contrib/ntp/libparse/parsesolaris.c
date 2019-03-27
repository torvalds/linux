/*
 * /src/NTP/ntp4-dev/libparse/parsesolaris.c,v 4.11 2005/04/16 17:32:10 kardel RELEASE_20050508_A
 *
 * parsesolaris.c,v 4.11 2005/04/16 17:32:10 kardel RELEASE_20050508_A
 *
 * STREAMS module for reference clocks
 *
 * Copyright (c) 1995-2005 by Frank Kardel <kardel <AT> ntp.org>
 * Copyright (c) 1989-1994 by Frank Kardel, Friedrich-Alexander Universitaet Erlangen-Nuernberg, Germany
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
 */

#define _KERNEL			/* it is a _KERNEL module */

#ifndef lint
static char rcsid[] = "parsesolaris.c,v 4.11 2005/04/16 17:32:10 kardel RELEASE_20050508_A";
#endif

#include <config.h>
#include <sys/types.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/time.h>
#include <sys/termios.h>
#include <sys/stream.h>
#include <sys/strtty.h>
#include <sys/stropts.h>
#include <sys/modctl.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#ifdef __GNUC__ /* makes it compile on Solaris 2.6 - acc doesn't like it -- GREAT! */
#include <stdarg.h>
#endif

#include "ntp_fp.h"
#include "parse.h"
#include <sys/parsestreams.h>

/*--------------- loadable driver section -----------------------------*/

static struct streamtab parseinfo;

static struct fmodsw fmod_templ =
{
	"parse",			/* module name */
	&parseinfo,			/* module information */
	D_NEW|D_MP|D_MTQPAIR,		/* exclusive for q pair */
	/* lock ptr */
};

extern struct mod_ops mod_strmodops;

static struct modlstrmod modlstrmod =
{
	&mod_strmodops,		/* a STREAMS module */
	"PARSE      - NTP reference",	/* name this baby - keep room for revision number */
	&fmod_templ
};

static struct modlinkage modlinkage =
{
	MODREV_1,
	{
		&modlstrmod,
		NULL
	}
};

/*
 * module management routines
 */
/*ARGSUSED*/
int
_init(
     void
     )
{
	static char revision[] = "4.6";
	char *s, *S;
	char *t;

#ifndef lint
	t = rcsid;
#endif

	/*
	 * copy RCS revision into Drv_name
	 *
	 * are we forcing RCS here to do things it was not built for ?
	 */
	s = revision;
	if (*s == '$')
	{
		/*
		 * skip "$Revision: "
		 * if present. - not necessary on a -kv co (cvs export)
		 */
		while (*s && (*s != ' '))
		{
			s++;
		}
		if (*s == ' ') s++;
	}

	t = modlstrmod.strmod_linkinfo;
	while (*t && (*t != ' '))
	{
		t++;
	}
	if (*t == ' ') t++;

	S = s;
	while (*S && (((*S >= '0') && (*S <= '9')) || (*S == '.')))
	{
		S++;
	}

	if (*s && *t && (S > s))
	{
		if (strlen(t) >= (S - s))
		{
			strlcpy(t, s, (unsigned)(S - s));
		}
	}
	return (mod_install(&modlinkage));
}

/*ARGSUSED*/
int
_info(
      struct modinfo *modinfop
      )
{
	return (mod_info(&modlinkage, modinfop));
}

/*ARGSUSED*/
int
_fini(
      void
      )
{
	if (mod_remove(&modlinkage) != DDI_SUCCESS)
	{
		return EBUSY;
	}
	else
	    return DDI_SUCCESS;
}

/*--------------- stream module definition ----------------------------*/

static int parseopen  (queue_t *, dev_t *, int, int, cred_t *);
static int parseclose (queue_t *, int);
static int parsewput  (queue_t *, mblk_t *);
static int parserput  (queue_t *, mblk_t *);
static int parsersvc  (queue_t *);

static struct module_info driverinfo =
{
	0,				/* module ID number */
	fmod_templ.f_name,		/* module name - why repeated here ? compat ?*/
	0,				/* minimum accepted packet size */
	INFPSZ,				/* maximum accepted packet size */
	1,				/* high water mark - flow control */
	0				/* low water mark - flow control */
};

static struct qinit rinit =	/* read queue definition */
{
	parserput,			/* put procedure */
	parsersvc,			/* service procedure */
	parseopen,			/* open procedure */
	parseclose,			/* close procedure */
	NULL,				/* admin procedure - NOT USED FOR NOW */
	&driverinfo,			/* information structure */
	NULL				/* statistics */
};

static struct qinit winit =	/* write queue definition */
{
	parsewput,			/* put procedure */
	NULL,				/* service procedure */
	NULL,				/* open procedure */
	NULL,				/* close procedure */
	NULL,				/* admin procedure - NOT USED FOR NOW */
	&driverinfo,			/* information structure */
	NULL				/* statistics */
};

static struct streamtab parseinfo =	/* stream info element for parse driver */
{
	&rinit,			/* read queue */
	&winit,			/* write queue */
	NULL,				/* read mux */
	NULL				/* write mux */
};

/*--------------- driver data structures ----------------------------*/

/*
 * we usually have an inverted signal - but you
 * can change this to suit your needs
 */
int cd_invert = 1;		/* invert status of CD line - PPS support via CD input */

#ifdef PARSEDEBUG
int parsedebug = ~0;
#else
int parsedebug = 0;
#endif

/*--------------- module implementation -----------------------------*/

#define TIMEVAL_USADD(_X_, _US_) do {\
	(_X_)->tv_usec += (_US_);\
	if ((_X_)->tv_usec >= 1000000)\
	{\
	    (_X_)->tv_sec++;\
	    (_X_)->tv_usec -= 1000000;\
	}\
     } while (0)

static int init_linemon (queue_t *);
static void close_linemon (queue_t *, queue_t *);

#define M_PARSE		0x0001
#define M_NOPARSE	0x0002

void
ntp_memset(
	char *a,
	int x,
	int c
	)
{
	while (c-- > 0)
	    *a++ = x;
}

static void
pprintf(
	int lev,
	char *form,
	...
	)
{
	va_list ap;

	va_start(ap, form);

	if (lev & parsedebug)
		vcmn_err(CE_CONT, form, ap);

	va_end(ap);
}

static int
setup_stream(
	     queue_t *q,
	     int mode
	     )
{
	register mblk_t *mp;

	pprintf(DD_OPEN,"parse: SETUP_STREAM - setting up stream for q=%x\n", q);

	mp = allocb(sizeof(struct stroptions), BPRI_MED);
	if (mp)
	{
		struct stroptions *str = (void *)mp->b_wptr;

		str->so_flags   = SO_READOPT|SO_HIWAT|SO_LOWAT|SO_ISNTTY;
		str->so_readopt = (mode == M_PARSE) ? RMSGD : RNORM;
		str->so_hiwat   = (mode == M_PARSE) ? sizeof(parsetime_t) : 256;
		str->so_lowat   = 0;
		mp->b_datap->db_type = M_SETOPTS;
		mp->b_wptr     += sizeof(struct stroptions);
		if (!q)
		    panic("NULL q - strange");
		putnext(q, mp);
		return putctl1(WR(q)->q_next, M_CTL, (mode == M_PARSE) ? MC_SERVICEIMM :
			       MC_SERVICEDEF);
	}
	else
	{
		pprintf(DD_OPEN, "parse: setup_stream - FAILED - no MEMORY for allocb\n");
		return 0;
	}
}

/*ARGSUSED*/
static int
parseopen(
	  queue_t *q,
	  dev_t *dev,
	  int flag,
	  int sflag,
	  cred_t *credp
	  )
{
	register parsestream_t *parse;
	static int notice = 0;

	pprintf(DD_OPEN, "parse: OPEN - q=%x\n", q);

	if (sflag != MODOPEN)
	{			/* open only for modules */
		pprintf(DD_OPEN, "parse: OPEN - FAILED - not MODOPEN\n");
		return EIO;
	}

	if (q->q_ptr != (caddr_t)NULL)
	{
		pprintf(DD_OPEN, "parse: OPEN - FAILED - EXCLUSIVE ONLY\n");
		return EBUSY;
	}

	q->q_ptr = (caddr_t)kmem_alloc(sizeof(parsestream_t), KM_SLEEP);
	if (q->q_ptr == (caddr_t)0)
	{
		return ENOMEM;
	}

	pprintf(DD_OPEN, "parse: OPEN - parse area q=%x, q->q_ptr=%x\n", q, q->q_ptr);
	WR(q)->q_ptr = q->q_ptr;
	pprintf(DD_OPEN, "parse: OPEN - WQ parse area q=%x, q->q_ptr=%x\n", WR(q), WR(q)->q_ptr);

	parse = (parsestream_t *) q->q_ptr;
	bzero((caddr_t)parse, sizeof(*parse));
	parse->parse_queue     = q;
	parse->parse_status    = PARSE_ENABLE;
	parse->parse_ppsclockev.tv.tv_sec  = 0;
	parse->parse_ppsclockev.tv.tv_usec = 0;
	parse->parse_ppsclockev.serial     = 0;

	qprocson(q);

	pprintf(DD_OPEN, "parse: OPEN - initializing io subsystem q=%x\n", q);

	if (!parse_ioinit(&parse->parse_io))
	{
		/*
		 * ok guys - beat it
		 */
		qprocsoff(q);

		kmem_free((caddr_t)parse, sizeof(parsestream_t));

		return EIO;
	}

	pprintf(DD_OPEN, "parse: OPEN - initializing stream q=%x\n", q);

	if (setup_stream(q, M_PARSE))
	{
		(void) init_linemon(q);	/* hook up PPS ISR routines if possible */
		pprintf(DD_OPEN, "parse: OPEN - SUCCEEDED\n");

		/*
		 * I know that you know the delete key, but you didn't write this
		 * code, did you ? - So, keep the message in here.
		 */
		if (!notice)
		{
		  cmn_err(CE_CONT, "?%s: Copyright (c) 1993-2005, Frank Kardel\n", modlstrmod.strmod_linkinfo);
			notice = 1;
		}

		return 0;
	}
	else
	{
		qprocsoff(q);

		kmem_free((caddr_t)parse, sizeof(parsestream_t));

		return EIO;
	}
}

/*ARGSUSED*/
static int
parseclose(
	   queue_t *q,
	   int flags
	   )
{
	register parsestream_t *parse = (parsestream_t *)q->q_ptr;
	register unsigned long s;

	pprintf(DD_CLOSE, "parse: CLOSE\n");

	qprocsoff(q);

	s = splhigh();

	if (parse->parse_dqueue)
	    close_linemon(parse->parse_dqueue, q);
	parse->parse_dqueue = (queue_t *)0;

	(void) splx(s);

	parse_ioend(&parse->parse_io);

	kmem_free((caddr_t)parse, sizeof(parsestream_t));

	q->q_ptr = (caddr_t)NULL;
	WR(q)->q_ptr = (caddr_t)NULL;

	return 0;
}

/*
 * move unrecognized stuff upward
 */
static int
parsersvc(
	  queue_t *q
	  )
{
	mblk_t *mp;

	while ((mp = getq(q)))
	{
		if (canputnext(q) || (mp->b_datap->db_type > QPCTL))
		{
			putnext(q, mp);
			pprintf(DD_RSVC, "parse: RSVC - putnext\n");
		}
		else
		{
			putbq(q, mp);
			pprintf(DD_RSVC, "parse: RSVC - flow control wait\n");
			break;
		}
	}
	return 0;
}

/*
 * do ioctls and
 * send stuff down - dont care about
 * flow control
 */
static int
parsewput(
	  queue_t *q,
	  mblk_t *mp
	  )
{
	register int ok = 1;
	register mblk_t *datap;
	register struct iocblk *iocp;
	parsestream_t         *parse = (parsestream_t *)q->q_ptr;

	pprintf(DD_WPUT, "parse: parsewput\n");

	switch (mp->b_datap->db_type)
	{
	    default:
		putnext(q, mp);
		break;

	    case M_IOCTL:
		iocp = (void *)mp->b_rptr;
		switch (iocp->ioc_cmd)
		{
		    default:
			pprintf(DD_WPUT, "parse: parsewput - forward M_IOCTL\n");
			putnext(q, mp);
			break;

		    case CIOGETEV:
			/*
			 * taken from Craig Leres ppsclock module (and modified)
			 */
			datap = allocb(sizeof(struct ppsclockev), BPRI_MED);
			if (datap == NULL || mp->b_cont)
			{
				mp->b_datap->db_type = M_IOCNAK;
				iocp->ioc_error = (datap == NULL) ? ENOMEM : EINVAL;
				if (datap != NULL)
				    freeb(datap);
				qreply(q, mp);
				break;
			}

			mp->b_cont = datap;
			/* (void *) quiets cast alignment warning */
			*(struct ppsclockev *)(void *)datap->b_wptr = parse->parse_ppsclockev;
			datap->b_wptr +=
				sizeof(struct ppsclockev) / sizeof(*datap->b_wptr);
			mp->b_datap->db_type = M_IOCACK;
			iocp->ioc_count = sizeof(struct ppsclockev);
			qreply(q, mp);
			break;

		    case PARSEIOC_ENABLE:
		    case PARSEIOC_DISABLE:
			    {
				    parse->parse_status = (parse->parse_status & (unsigned)~PARSE_ENABLE) |
					    (iocp->ioc_cmd == PARSEIOC_ENABLE) ?
					    PARSE_ENABLE : 0;
				    if (!setup_stream(RD(q), (parse->parse_status & PARSE_ENABLE) ?
						      M_PARSE : M_NOPARSE))
				    {
					    mp->b_datap->db_type = M_IOCNAK;
				    }
				    else
				    {
					    mp->b_datap->db_type = M_IOCACK;
				    }
				    qreply(q, mp);
				    break;
			    }

		    case PARSEIOC_TIMECODE:
		    case PARSEIOC_SETFMT:
		    case PARSEIOC_GETFMT:
		    case PARSEIOC_SETCS:
			if (iocp->ioc_count == sizeof(parsectl_t))
			{
				parsectl_t *dct = (void *)mp->b_cont->b_rptr;

				switch (iocp->ioc_cmd)
				{
				    case PARSEIOC_TIMECODE:
					pprintf(DD_WPUT, "parse: parsewput - PARSEIOC_TIMECODE\n");
					ok = parse_timecode(dct, &parse->parse_io);
					break;

				    case PARSEIOC_SETFMT:
					pprintf(DD_WPUT, "parse: parsewput - PARSEIOC_SETFMT\n");
					ok = parse_setfmt(dct, &parse->parse_io);
					break;

				    case PARSEIOC_GETFMT:
					pprintf(DD_WPUT, "parse: parsewput - PARSEIOC_GETFMT\n");
					ok = parse_getfmt(dct, &parse->parse_io);
					break;

				    case PARSEIOC_SETCS:
					pprintf(DD_WPUT, "parse: parsewput - PARSEIOC_SETCS\n");
					ok = parse_setcs(dct, &parse->parse_io);
					break;
				}
				mp->b_datap->db_type = ok ? M_IOCACK : M_IOCNAK;
			}
			else
			{
				mp->b_datap->db_type = M_IOCNAK;
			}
			pprintf(DD_WPUT, "parse: parsewput qreply - %s\n", (mp->b_datap->db_type == M_IOCNAK) ? "M_IOCNAK" : "M_IOCACK");
			qreply(q, mp);
			break;
		}
	}
	return 0;
}

/*
 * read characters from streams buffers
 */
static unsigned long
rdchar(
       mblk_t **mp
       )
{
	while (*mp != (mblk_t *)NULL)
	{
		if ((*mp)->b_wptr - (*mp)->b_rptr)
		{
			return (unsigned long)(*(unsigned char *)((*mp)->b_rptr++));
		}
		else
		{
			register mblk_t *mmp = *mp;

			*mp = (*mp)->b_cont;
			freeb(mmp);
		}
	}
	return (unsigned long)~0;
}

/*
 * convert incoming data
 */
static int
parserput(
	  queue_t *q,
	  mblk_t *imp
	  )
{
	register unsigned char type;
	mblk_t *mp = imp;

	switch (type = mp->b_datap->db_type)
	{
	    default:
		/*
		 * anything we don't know will be put on queue
		 * the service routine will move it to the next one
		 */
		pprintf(DD_RPUT, "parse: parserput - forward type 0x%x\n", type);

		if (canputnext(q) || (mp->b_datap->db_type > QPCTL))
		{
			putnext(q, mp);
		}
		else
		    putq(q, mp);
		break;

	    case M_BREAK:
	    case M_DATA:
		    {
			    register parsestream_t * parse = (parsestream_t *)q->q_ptr;
			    register mblk_t *nmp;
			    register unsigned long ch;
			    timestamp_t c_time;
			    timespec_t hres_time;

			    /*
			     * get time on packet delivery
			     */
			    gethrestime(&hres_time);
			    c_time.tv.tv_sec  = hres_time.tv_sec;
			    c_time.tv.tv_usec = hres_time.tv_nsec / 1000;

			    if (!(parse->parse_status & PARSE_ENABLE))
			    {
				    pprintf(DD_RPUT, "parse: parserput - parser disabled - forward type 0x%x\n", type);
				    if (canputnext(q) || (mp->b_datap->db_type > QPCTL))
				    {
					    putnext(q, mp);
				    }
				    else
					putq(q, mp);
			    }
			    else
			    {
				    pprintf(DD_RPUT, "parse: parserput - M_%s\n", (type == M_DATA) ? "DATA" : "BREAK");
				    if (type == M_DATA)
				    {
					    /*
					     * parse packet looking for start an end characters
					     */
					    while (mp != (mblk_t *)NULL)
					    {
						    ch = rdchar(&mp);
						    if (ch != ~0 && parse_ioread(&parse->parse_io, (unsigned int)ch, &c_time))
						    {
							    /*
							     * up up and away (hopefully ...)
							     * don't press it if resources are tight or nobody wants it
							     */
							    nmp = (mblk_t *)NULL;
							    if (canputnext(parse->parse_queue) && (nmp = allocb(sizeof(parsetime_t), BPRI_MED)))
							    {
								    bcopy((caddr_t)&parse->parse_io.parse_dtime, (caddr_t)nmp->b_rptr, sizeof(parsetime_t));
								    nmp->b_wptr += sizeof(parsetime_t);
								    putnext(parse->parse_queue, nmp);
							    }
							    else
								if (nmp) freemsg(nmp);
							    parse_iodone(&parse->parse_io);
						    }
					    }
				    }
				    else
				    {
					    if (parse_ioread(&parse->parse_io, (unsigned int)0, &c_time))
					    {
						    /*
						     * up up and away (hopefully ...)
						     * don't press it if resources are tight or nobody wants it
						     */
						    nmp = (mblk_t *)NULL;
						    if (canputnext(parse->parse_queue) && (nmp = allocb(sizeof(parsetime_t), BPRI_MED)))
						    {
							    bcopy((caddr_t)&parse->parse_io.parse_dtime, (caddr_t)nmp->b_rptr, sizeof(parsetime_t));
							    nmp->b_wptr += sizeof(parsetime_t);
							    putnext(parse->parse_queue, nmp);
						    }
						    else
							if (nmp) freemsg(nmp);
						    parse_iodone(&parse->parse_io);
					    }
					    freemsg(mp);
				    }
				    break;
			    }
		    }

		    /*
		     * CD PPS support for non direct ISR hack
		     */
	    case M_HANGUP:
	    case M_UNHANGUP:
		    {
			    register parsestream_t * parse = (parsestream_t *)q->q_ptr;
			    timestamp_t c_time;
			    timespec_t hres_time;
			    register mblk_t *nmp;
			    register int status = cd_invert ^ (type == M_UNHANGUP);

			    gethrestime(&hres_time);
			    c_time.tv.tv_sec  = hres_time.tv_sec;
			    c_time.tv.tv_usec = hres_time.tv_nsec / 1000;

			    pprintf(DD_RPUT, "parse: parserput - M_%sHANGUP\n", (type == M_HANGUP) ? "" : "UN");

			    if ((parse->parse_status & PARSE_ENABLE) &&
				parse_iopps(&parse->parse_io, status ? SYNC_ONE : SYNC_ZERO, &c_time))
			    {
				    nmp = (mblk_t *)NULL;
				    if (canputnext(parse->parse_queue) && (nmp = allocb(sizeof(parsetime_t), BPRI_MED)))
				    {
					    bcopy((caddr_t)&parse->parse_io.parse_dtime, (caddr_t)nmp->b_rptr, sizeof(parsetime_t));
					    nmp->b_wptr += sizeof(parsetime_t);
					    putnext(parse->parse_queue, nmp);
				    }
				    else
					if (nmp) freemsg(nmp);
				    parse_iodone(&parse->parse_io);
				    freemsg(mp);
			    }
			    else
				if (canputnext(q) || (mp->b_datap->db_type > QPCTL))
				{
					putnext(q, mp);
				}
				else
				    putq(q, mp);

			    if (status)
			    {
				    parse->parse_ppsclockev.tv = c_time.tv;
				    ++(parse->parse_ppsclockev.serial);
			    }
		    }
	}
	return 0;
}

static int  init_zs_linemon  (queue_t *, queue_t *);	/* handle line monitor for "zs" driver */
static void close_zs_linemon (queue_t *, queue_t *);

/*-------------------- CD isr status monitor ---------------*/

static int
init_linemon(
	     queue_t *q
	     )
{
	register queue_t *dq;

	dq = WR(q);
	/*
	 * we ARE doing very bad things down here (basically stealing ISR
	 * hooks)
	 *
	 * so we chase down the STREAMS stack searching for the driver
	 * and if this is a known driver we insert our ISR routine for
	 * status changes in to the ExternalStatus handling hook
	 */
	while (dq->q_next)
	{
		dq = dq->q_next;		/* skip down to driver */
	}

	/*
	 * find appropriate driver dependent routine
	 */
	if (dq->q_qinfo && dq->q_qinfo->qi_minfo)
	{
		register char *dname = dq->q_qinfo->qi_minfo->mi_idname;

		pprintf(DD_INSTALL, "init_linemon: driver is \"%s\"\n", dname);

#ifdef sun
		if (dname && !strcmp(dname, "zs"))
		{
			return init_zs_linemon(dq, q);
		}
		else
#endif
		{
			pprintf(DD_INSTALL, "init_linemon: driver \"%s\" not suitable for CD monitoring\n", dname);
			return 0;
		}
	}
	pprintf(DD_INSTALL, "init_linemon: cannot find driver\n");
	return 0;
}

static void
close_linemon(
	      queue_t *q,
	      queue_t *my_q
	      )
{
	/*
	 * find appropriate driver dependent routine
	 */
	if (q->q_qinfo && q->q_qinfo->qi_minfo)
	{
		register char *dname = q->q_qinfo->qi_minfo->mi_idname;

#ifdef sun
		if (dname && !strcmp(dname, "zs"))
		{
			close_zs_linemon(q, my_q);
			return;
		}
		pprintf(DD_INSTALL, "close_linemon: cannot find driver close routine for \"%s\"\n", dname);
#endif
	}
	pprintf(DD_INSTALL, "close_linemon: cannot find driver name\n");
}

#ifdef sun
#include <sys/tty.h>
#include <sys/zsdev.h>
#include <sys/ser_async.h>
#include <sys/ser_zscc.h>

static void zs_xsisr         (struct zscom *);	/* zs external status interupt handler */

/*
 * there should be some docs telling how to get to
 * sz:zs_usec_delay and zs:initzsops()
 */
#define zs_usec_delay 5

struct savedzsops
{
	struct zsops  zsops;
	struct zsops *oldzsops;
};

static struct zsops   *emergencyzs;

static int
init_zs_linemon(
		queue_t *q,
		queue_t *my_q
		)
{
	register struct zscom *zs;
	register struct savedzsops *szs;
	register parsestream_t  *parsestream = (parsestream_t *)my_q->q_ptr;
	/*
	 * we expect the zsaline pointer in the q_data pointer
	 * from there on we insert our on EXTERNAL/STATUS ISR routine
	 * into the interrupt path, before the standard handler
	 */
	zs = ((struct asyncline *)q->q_ptr)->za_common;
	if (!zs)
	{
		/*
		 * well - not found on startup - just say no (shouldn't happen though)
		 */
		return 0;
	}
	else
	{
		/*
		 * we do a direct replacement, in case others fiddle also
		 * if somebody else grabs our hook and we disconnect
		 * we are in DEEP trouble - panic is likely to be next, sorry
		 */
		szs = (struct savedzsops *) kmem_alloc(sizeof(struct savedzsops), KM_SLEEP);

		if (szs == (struct savedzsops *)0)
		{
			pprintf(DD_INSTALL, "init_zs_linemon: CD monitor NOT installed - no memory\n");

			return 0;
		}
		else
		{
			parsestream->parse_data   = (void *)szs;

			mutex_enter(zs->zs_excl);

			parsestream->parse_dqueue = q; /* remember driver */

			szs->zsops            = *zs->zs_ops;
			szs->zsops.zsop_xsint = (void (*) (struct zscom *))zs_xsisr; /* place our bastard */
			szs->oldzsops         = zs->zs_ops;
			emergencyzs           = zs->zs_ops;

			zs->zs_ops = &szs->zsops; /* hook it up */
			/*
			 * XXX: this is usually done via zsopinit()
			 * - have yet to find a way to call that routine
			 */
			zs->zs_xsint          = (void (*) (struct zscom *))zs_xsisr;

			mutex_exit(zs->zs_excl);

			pprintf(DD_INSTALL, "init_zs_linemon: CD monitor installed\n");

			return 1;
		}
	}
}

/*
 * unregister our ISR routine - must call under splhigh() (or
 * whatever block ZS status interrupts)
 */
static void
close_zs_linemon(
		 queue_t *q,
		 queue_t *my_q
		 )
{
	register struct zscom *zs;
	register parsestream_t  *parsestream = (parsestream_t *)my_q->q_ptr;

	zs = ((struct asyncline *)q->q_ptr)->za_common;
	if (!zs)
	{
		/*
		 * well - not found on startup - just say no (shouldn't happen though)
		 */
		return;
	}
	else
	{
		register struct savedzsops *szs = (struct savedzsops *)parsestream->parse_data;

		mutex_enter(zs->zs_excl);

		zs->zs_ops = szs->oldzsops; /* reset to previous handler functions */
		/*
		 * XXX: revert xsint (usually done via zsopinit() - have still to find
		 * a way to call that bugger
		 */
		zs->zs_xsint = zs->zs_ops->zsop_xsint;

		mutex_exit(zs->zs_excl);

		kmem_free((caddr_t)szs, sizeof (struct savedzsops));

		pprintf(DD_INSTALL, "close_zs_linemon: CD monitor deleted\n");
		return;
	}
}

#define ZSRR0_IGNORE	(ZSRR0_CD|ZSRR0_SYNC|ZSRR0_CTS)

#define MAXDEPTH 50		/* maximum allowed stream crawl */

/*
 * take external status interrupt (only CD interests us)
 */
static void
zs_xsisr(
	 struct zscom *zs
	 )
{
	register struct asyncline *za = (void *)zs->zs_priv;
	register queue_t *q;
	register unsigned char zsstatus;
	register int loopcheck;
	register unsigned char cdstate;
	register const char *dname = "-UNKNOWN-";
	timespec_t hres_time;

	/*
	 * pick up current state
	 */
	zsstatus = SCC_READ0();

	if (za->za_rr0 ^ (cdstate = zsstatus & ZSRR0_CD))
	{
		timestamp_t cdevent;
		register int status;

		/*
		 * time stamp
		 */
		gethrestime(&hres_time);
		cdevent.tv.tv_sec  = hres_time.tv_sec;
		cdevent.tv.tv_usec = hres_time.tv_nsec / 1000;

		q = za->za_ttycommon.t_readq;

		/*
		 * logical state
		 */
		status = cd_invert ? cdstate == 0 : cdstate != 0;

		/*
		 * ok - now the hard part - find ourself
		 */
		loopcheck = MAXDEPTH;

		while (q)
		{
			if (q->q_qinfo && q->q_qinfo->qi_minfo)
			{
				dname = q->q_qinfo->qi_minfo->mi_idname;

				if (!strcmp(dname, parseinfo.st_rdinit->qi_minfo->mi_idname))
				{
					/*
					 * back home - phew (hopping along stream queues might
					 * prove dangerous to your health)
					 */

					if ((((parsestream_t *)q->q_ptr)->parse_status & PARSE_ENABLE) &&
					    parse_iopps(&((parsestream_t *)q->q_ptr)->parse_io, status ? SYNC_ONE : SYNC_ZERO, &cdevent))
					{
						/*
						 * XXX - currently we do not pass up the message, as
						 * we should.
						 * for a correct behaviour wee need to block out
						 * processing until parse_iodone has been posted via
						 * a softcall-ed routine which does the message pass-up
						 * right now PPS information relies on input being
						 * received
						 */
						parse_iodone(&((parsestream_t *)q->q_ptr)->parse_io);
					}

					if (status)
					{
						((parsestream_t *)q->q_ptr)->parse_ppsclockev.tv = cdevent.tv;
						++(((parsestream_t *)q->q_ptr)->parse_ppsclockev.serial);
					}

					pprintf(DD_ISR, "zs_xsisr: CD event %s has been posted for \"%s\"\n", status ? "ONE" : "ZERO", dname);
					break;
				}
			}

			q = q->q_next;

			if (!loopcheck--)
			{
				panic("zs_xsisr: STREAMS Queue corrupted - CD event");
			}
		}

		if (cdstate)	/* fake CARRIER status - XXX currently not coordinated */
		  za->za_flags |= ZAS_CARR_ON;
		else
		  za->za_flags &= ~ZAS_CARR_ON;

		/*
		 * only pretend that CD and ignored transistion (SYNC,CTS)
		 * have been handled
		 */
		za->za_rr0 = (za->za_rr0 & ~ZSRR0_IGNORE) | (zsstatus & ZSRR0_IGNORE);

		if (((za->za_rr0 ^ zsstatus) & ~ZSRR0_IGNORE) == 0)
		{
			/*
			 * all done - kill status indication and return
			 */
			SCC_WRITE0(ZSWR0_RESET_STATUS); /* might kill other conditions here */
			return;
		}
	}

	pprintf(DD_ISR, "zs_xsisr: non CD event 0x%x for \"%s\"\n",
		(za->za_rr0 ^ zsstatus) & ~ZSRR0_CD,dname);
	/*
	 * we are now gathered here to process some unusual external status
	 * interrupts.
	 * any CD events have also been handled and shouldn't be processed
	 * by the original routine (unless we have a VERY busy port pin)
	 * some initializations are done here, which could have been done before for
	 * both code paths but have been avioded for minimum path length to
	 * the uniq_time routine
	 */
	dname = (char *) 0;
	q = za->za_ttycommon.t_readq;

	loopcheck = MAXDEPTH;

	/*
	 * the real thing for everything else ...
	 */
	while (q)
	{
		if (q->q_qinfo && q->q_qinfo->qi_minfo)
		{
			dname = q->q_qinfo->qi_minfo->mi_idname;
			if (!strcmp(dname, parseinfo.st_rdinit->qi_minfo->mi_idname))
			{
				register void (*zsisr) (struct zscom *);

				/*
				 * back home - phew (hopping along stream queues might
				 * prove dangerous to your health)
				 */
				if ((zsisr = ((struct savedzsops *)((parsestream_t *)q->q_ptr)->parse_data)->oldzsops->zsop_xsint))
				    zsisr(zs);
				else
				    panic("zs_xsisr: unable to locate original ISR");

				pprintf(DD_ISR, "zs_xsisr: non CD event was processed for \"%s\"\n", dname);
				/*
				 * now back to our program ...
				 */
				return;
			}
		}

		q = q->q_next;

		if (!loopcheck--)
		{
			panic("zs_xsisr: STREAMS Queue corrupted - non CD event");
		}
	}

	/*
	 * last resort - shouldn't even come here as it indicates
	 * corrupted TTY structures
	 */
	printf("zs_zsisr: looking for \"%s\" - found \"%s\" - taking EMERGENCY path\n", parseinfo.st_rdinit->qi_minfo->mi_idname, dname ? dname : "-NIL-");

	if (emergencyzs && emergencyzs->zsop_xsint)
	    emergencyzs->zsop_xsint(zs);
	else
	    panic("zs_xsisr: no emergency ISR handler");
}
#endif				/* sun */

/*
 * History:
 *
 * parsesolaris.c,v
 * Revision 4.11  2005/04/16 17:32:10  kardel
 * update copyright
 *
 * Revision 4.10  2004/11/14 16:06:08  kardel
 * update Id tags
 *
 * Revision 4.9  2004/11/14 15:29:41  kardel
 * support PPSAPI, upgrade Copyright to Berkeley style
 *
 * Revision 4.6  1998/11/15 21:56:08  kardel
 * ntp_memset not necessary
 *
 * Revision 4.5  1998/11/15 21:23:37  kardel
 * ntp_memset() replicated in Sun kernel files
 *
 * Revision 4.4  1998/06/14 21:09:40  kardel
 * Sun acc cleanup
 *
 * Revision 4.3  1998/06/13 12:14:59  kardel
 * more prototypes
 * fix name clashes
 * allow for ansi2knr
 *
 * Revision 4.2  1998/06/12 15:23:08  kardel
 * fix prototypes
 * adjust for ansi2knr
 *
 * Revision 4.1  1998/05/24 09:38:46  kardel
 * streams initiated iopps calls (M_xHANGUP) are now consistent with the
 * respective calls from zs_xsisr()
 * simulation of CARRIER status to avoid unecessary M_xHANGUP messages
 *
 * Revision 4.0  1998/04/10 19:45:38  kardel
 * Start 4.0 release version numbering
 *
 * from V3 3.28 log info deleted 1998/04/11 kardel
 */
