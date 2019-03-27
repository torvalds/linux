/*
 * /src/NTP/ntp4-dev/libparse/parsestreams.c,v 4.11 2005/04/16 17:32:10 kardel RELEASE_20050508_A
 *
 * parsestreams.c,v 4.11 2005/04/16 17:32:10 kardel RELEASE_20050508_A
 *
 * STREAMS module for reference clocks
 * (SunOS4.x)
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

#define KERNEL			/* MUST */
#define VDDRV			/* SHOULD */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifndef lint
static char rcsid[] = "parsestreams.c,v 4.11 2005/04/16 17:32:10 kardel RELEASE_20050508_A";
#endif

#ifndef KERNEL
#include "Bletch: MUST COMPILE WITH KERNEL DEFINE"
#endif

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/buf.h>
#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/time.h>
#include <sundev/mbvar.h>
#include <sun/autoconf.h>
#include <sys/stream.h>
#include <sys/stropts.h>
#include <sys/dir.h>
#include <sys/signal.h>
#include <sys/termios.h>
#include <sys/termio.h>
#include <sys/ttold.h>
#include <sys/user.h>
#include <sys/tty.h>

#ifdef VDDRV
#include <sun/vddrv.h>
#endif

#include "ntp_stdlib.h"
#include "ntp_fp.h"
/*
 * just make checking compilers more silent
 */
extern int printf      (const char *, ...);
extern int putctl1     (queue_t *, int, int);
extern int canput      (queue_t *);
extern void putbq      (queue_t *, mblk_t *);
extern void freeb      (mblk_t *);
extern void qreply     (queue_t *, mblk_t *);
extern void freemsg    (mblk_t *);
extern void panic      (const char *, ...);
extern void usec_delay (int);

#include "parse.h"
#include "sys/parsestreams.h"

/*
 * use microtime instead of uniqtime if advised to
 */
#ifdef MICROTIME
#define uniqtime microtime
#endif

#ifdef VDDRV
static unsigned int parsebusy = 0;

/*--------------- loadable driver section -----------------------------*/

extern struct streamtab parseinfo;


#ifdef PPS_SYNC
static char mnam[] = "PARSEPPS     ";	/* name this baby - keep room for revision number */
#else
static char mnam[] = "PARSE        ";	/* name this baby - keep room for revision number */
#endif
struct vdldrv parsesync_vd =
{
	VDMAGIC_PSEUDO,		/* nothing like a real driver - a STREAMS module */
	mnam,
};

/*
 * strings support usually not in kernel
 */
static int
Strlen(
	register const char *s
	)
{
	register int c;

	c = 0;
	if (s)
	{
		while (*s++)
		{
			c++;
		}
	}
	return c;
}

static void
Strncpy(
	register char *t,
	register char *s,
	register int   c
	)
{
	if (s && t)
	{
		while ((c-- > 0) && (*t++ = *s++))
		    ;
	}
}

static int
Strcmp(
	register const char *s,
	register const char *t
	)
{
	register int c = 0;

	if (!s || !t || (s == t))
	{
		return 0;
	}

	while (!(c = *s++ - *t++) && *s && *t)
	    /* empty loop */;

	return c;
}

static int
Strncmp(
	register char *s,
	register char *t,
	register int n
	)
{
	register int c = 0;

	if (!s || !t || (s == t))
	{
		return 0;
	}

	while (n-- && !(c = *s++ - *t++) && *s && *t)
	    /* empty loop */;

	return c;
}

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

/*
 * driver init routine
 * since no mechanism gets us into and out of the fmodsw, we have to
 * do it ourselves
 */
/*ARGSUSED*/
int
xxxinit(
	unsigned int fc,
	struct vddrv *vdp,
	addr_t vdin,
	struct vdstat *vds
	)
{
	extern struct fmodsw fmodsw[];
	extern int fmodcnt;

	struct fmodsw *fm    = fmodsw;
	struct fmodsw *fmend = &fmodsw[fmodcnt];
	struct fmodsw *ifm   = (struct fmodsw *)0;
	char *mname          = parseinfo.st_rdinit->qi_minfo->mi_idname;

	switch (fc)
	{
	    case VDLOAD:
		vdp->vdd_vdtab = (struct vdlinkage *)&parsesync_vd;
		/*
		 * now, jog along fmodsw scanning for an empty slot
		 * and deposit our name there
		 */
		while (fm <= fmend)
		{
			if (!Strncmp(fm->f_name, mname, FMNAMESZ))
			{
				printf("vddrinit[%s]: STREAMS module already loaded.\n", mname);
				return(EBUSY);
			}
			else
			    if ((ifm == (struct fmodsw *)0) &&
				(fm->f_name[0] == '\0') &&
				(fm->f_str == (struct streamtab *)0))
			    {
				    /*
				     * got one - so move in
				     */
				    ifm = fm;
				    break;
			    }
			fm++;
		}

		if (ifm == (struct fmodsw *)0)
		{
			printf("vddrinit[%s]: no slot free for STREAMS module\n", mname);
			return (ENOSPC);
		}
		else
		{
			static char revision[] = "4.7";
			char *s, *S, *t;

			s = rcsid;		/* NOOP - keep compilers happy */

			Strncpy(ifm->f_name, mname, FMNAMESZ);
			ifm->f_name[FMNAMESZ] = '\0';
			ifm->f_str = &parseinfo;
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

			t = parsesync_vd.Drv_name;
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
				if (Strlen(t) >= (S - s))
				{
					(void) Strncpy(t, s, S - s);
				}
			}
			return (0);
		}
		break;

	    case VDUNLOAD:
		if (parsebusy > 0)
		{
			printf("vddrinit[%s]: STREAMS module has still %d instances active.\n", mname, parsebusy);
			return (EBUSY);
		}
		else
		{
			while (fm <= fmend)
			{
				if (!Strncmp(fm->f_name, mname, FMNAMESZ))
				{
					/*
					 * got it - kill entry
					 */
					fm->f_name[0] = '\0';
					fm->f_str = (struct streamtab *)0;
					fm++;

					break;
				}
				fm++;
			}
			if (fm > fmend)
			{
				printf("vddrinit[%s]: cannot find entry for STREAMS module\n", mname);
				return (ENXIO);
			}
			else
			    return (0);
		}


	    case VDSTAT:
		return (0);

	    default:
		return (EIO);

	}
	return EIO;
}

#endif

/*--------------- stream module definition ----------------------------*/

static int parseopen  (queue_t *, dev_t, int, int);
static int parseclose (queue_t *, int);
static int parsewput  (queue_t *, mblk_t *);
static int parserput  (queue_t *, mblk_t *);
static int parsersvc  (queue_t *);

static char mn[] = "parse";

static struct module_info driverinfo =
{
	0,				/* module ID number */
	mn,			/* module name */
	0,				/* minimum accepted packet size */
	INFPSZ,			/* maximum accepted packet size */
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

struct streamtab parseinfo =	/* stream info element for dpr driver */
{
	&rinit,			/* read queue */
	&winit,			/* write queue */
	NULL,				/* read mux */
	NULL,				/* write mux */
	NULL				/* module auto push */
};

/*--------------- driver data structures ----------------------------*/

/*
 * we usually have an inverted signal - but you
 * can change this to suit your needs
 */
int cd_invert = 1;		/* invert status of CD line - PPS support via CD input */

int parsedebug = ~0;

extern void uniqtime (struct timeval *);

/*--------------- module implementation -----------------------------*/

#define TIMEVAL_USADD(_X_, _US_) {\
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

static int
setup_stream(
	     queue_t *q,
	     int mode
	     )
{
	mblk_t *mp;

	mp = allocb(sizeof(struct stroptions), BPRI_MED);
	if (mp)
	{
		struct stroptions *str = (struct stroptions *)(void *)mp->b_rptr;

		str->so_flags   = SO_READOPT|SO_HIWAT|SO_LOWAT;
		str->so_readopt = (mode == M_PARSE) ? RMSGD : RNORM;
		str->so_hiwat   = (mode == M_PARSE) ? sizeof(parsetime_t) : 256;
		str->so_lowat   = 0;
		mp->b_datap->db_type = M_SETOPTS;
		mp->b_wptr += sizeof(struct stroptions);
		putnext(q, mp);
		return putctl1(WR(q)->q_next, M_CTL, (mode == M_PARSE) ? MC_SERVICEIMM :
			       MC_SERVICEDEF);
	}
	else
	{
		parseprintf(DD_OPEN,("parse: setup_stream - FAILED - no MEMORY for allocb\n"));
		return 0;
	}
}

/*ARGSUSED*/
static int
parseopen(
	queue_t *q,
	dev_t dev,
	int flag,
	int sflag
	)
{
	register parsestream_t *parse;
	static int notice = 0;

	parseprintf(DD_OPEN,("parse: OPEN\n"));

	if (sflag != MODOPEN)
	{			/* open only for modules */
		parseprintf(DD_OPEN,("parse: OPEN - FAILED - not MODOPEN\n"));
		return OPENFAIL;
	}

	if (q->q_ptr != (caddr_t)NULL)
	{
		u.u_error = EBUSY;
		parseprintf(DD_OPEN,("parse: OPEN - FAILED - EXCLUSIVE ONLY\n"));
		return OPENFAIL;
	}

#ifdef VDDRV
	parsebusy++;
#endif

	q->q_ptr = (caddr_t)kmem_alloc(sizeof(parsestream_t));
	if (q->q_ptr == (caddr_t)0)
	{
		parseprintf(DD_OPEN,("parse: OPEN - FAILED - no memory\n"));
#ifdef VDDRV
		parsebusy--;
#endif
		return OPENFAIL;
	}
	WR(q)->q_ptr = q->q_ptr;

	parse = (parsestream_t *)(void *)q->q_ptr;
	bzero((caddr_t)parse, sizeof(*parse));
	parse->parse_queue     = q;
	parse->parse_status    = PARSE_ENABLE;
	parse->parse_ppsclockev.tv.tv_sec  = 0;
	parse->parse_ppsclockev.tv.tv_usec = 0;
	parse->parse_ppsclockev.serial     = 0;

	if (!parse_ioinit(&parse->parse_io))
	{
		/*
		 * ok guys - beat it
		 */
		kmem_free((caddr_t)parse, sizeof(parsestream_t));
#ifdef VDDRV
		parsebusy--;
#endif
		return OPENFAIL;
	}

	if (setup_stream(q, M_PARSE))
	{
		(void) init_linemon(q);	/* hook up PPS ISR routines if possible */

		parseprintf(DD_OPEN,("parse: OPEN - SUCCEEDED\n"));

		/*
		 * I know that you know the delete key, but you didn't write this
		 * code, did you ? - So, keep the message in here.
		 */
		if (!notice)
		{
#ifdef VDDRV
			printf("%s: Copyright (C) 1991-2005, Frank Kardel\n", parsesync_vd.Drv_name);
#else
			printf("%s: Copyright (C) 1991-2005, Frank Kardel\n", "parsestreams.c,v 4.11 2005/04/16 17:32:10 kardel RELEASE_20050508_A");
#endif
			notice = 1;
		}

		return MODOPEN;
	}
	else
	{
		kmem_free((caddr_t)parse, sizeof(parsestream_t));

#ifdef VDDRV
		parsebusy--;
#endif
		return OPENFAIL;
	}
}

/*ARGSUSED*/
static int
parseclose(
	queue_t *q,
	int flags
	)
{
	register parsestream_t *parse = (parsestream_t *)(void *)q->q_ptr;
	register unsigned long s;

	parseprintf(DD_CLOSE,("parse: CLOSE\n"));

	s = splhigh();

	if (parse->parse_dqueue)
	    close_linemon(parse->parse_dqueue, q);
	parse->parse_dqueue = (queue_t *)0;

	(void) splx(s);

	parse_ioend(&parse->parse_io);

	kmem_free((caddr_t)parse, sizeof(parsestream_t));

	q->q_ptr = (caddr_t)NULL;
	WR(q)->q_ptr = (caddr_t)NULL;

#ifdef VDDRV
	parsebusy--;
#endif
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
		if (canput(q->q_next) || (mp->b_datap->db_type > QPCTL))
		{
			putnext(q, mp);
			parseprintf(DD_RSVC,("parse: RSVC - putnext\n"));
		}
		else
		{
			putbq(q, mp);
			parseprintf(DD_RSVC,("parse: RSVC - flow control wait\n"));
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
	register mblk_t *mp
	)
{
	register int ok = 1;
	register mblk_t *datap;
	register struct iocblk *iocp;
	parsestream_t         *parse = (parsestream_t *)(void *)q->q_ptr;

	parseprintf(DD_WPUT,("parse: parsewput\n"));

	switch (mp->b_datap->db_type)
	{
	    default:
		putnext(q, mp);
		break;

	    case M_IOCTL:
		    iocp = (struct iocblk *)(void *)mp->b_rptr;
		switch (iocp->ioc_cmd)
		{
		    default:
			parseprintf(DD_WPUT,("parse: parsewput - forward M_IOCTL\n"));
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
				parsectl_t *dct = (parsectl_t *)(void *)mp->b_cont->b_rptr;

				switch (iocp->ioc_cmd)
				{
				    case PARSEIOC_TIMECODE:
					parseprintf(DD_WPUT,("parse: parsewput - PARSEIOC_TIMECODE\n"));
					ok = parse_timecode(dct, &parse->parse_io);
					break;

				    case PARSEIOC_SETFMT:
					parseprintf(DD_WPUT,("parse: parsewput - PARSEIOC_SETFMT\n"));
					ok = parse_setfmt(dct, &parse->parse_io);
					break;

				    case PARSEIOC_GETFMT:
					parseprintf(DD_WPUT,("parse: parsewput - PARSEIOC_GETFMT\n"));
					ok = parse_getfmt(dct, &parse->parse_io);
					break;

				    case PARSEIOC_SETCS:
					parseprintf(DD_WPUT,("parse: parsewput - PARSEIOC_SETCS\n"));
					ok = parse_setcs(dct, &parse->parse_io);
					break;
				}
				mp->b_datap->db_type = ok ? M_IOCACK : M_IOCNAK;
			}
			else
			{
				mp->b_datap->db_type = M_IOCNAK;
			}
			parseprintf(DD_WPUT,("parse: parsewput qreply - %s\n", (mp->b_datap->db_type == M_IOCNAK) ? "M_IOCNAK" : "M_IOCACK"));
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
       register mblk_t **mp
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
	return (unsigned)~0;
}

/*
 * convert incoming data
 */
static int
parserput(
	queue_t *q,
	mblk_t *mp
	)
{
	unsigned char type;

	switch (type = mp->b_datap->db_type)
	{
	    default:
		/*
		 * anything we don't know will be put on queue
		 * the service routine will move it to the next one
		 */
		parseprintf(DD_RPUT,("parse: parserput - forward type 0x%x\n", type));
		if (canput(q->q_next) || (mp->b_datap->db_type > QPCTL))
		{
			putnext(q, mp);
		}
		else
		    putq(q, mp);
		break;

	    case M_BREAK:
	    case M_DATA:
		    {
			    register parsestream_t * parse = (parsestream_t *)(void *)q->q_ptr;
			    register mblk_t *nmp;
			    register unsigned long ch;
			    timestamp_t ctime;

			    /*
			     * get time on packet delivery
			     */
			    uniqtime(&ctime.tv);

			    if (!(parse->parse_status & PARSE_ENABLE))
			    {
				    parseprintf(DD_RPUT,("parse: parserput - parser disabled - forward type 0x%x\n", type));
				    if (canput(q->q_next) || (mp->b_datap->db_type > QPCTL))
				    {
					    putnext(q, mp);
				    }
				    else
					putq(q, mp);
			    }
			    else
			    {
				    parseprintf(DD_RPUT,("parse: parserput - M_%s\n", (type == M_DATA) ? "DATA" : "BREAK"));

				    if (type == M_DATA)
				    {
					    /*
					     * parse packet looking for start an end characters
					     */
					    while (mp != (mblk_t *)NULL)
					    {
						    ch = rdchar(&mp);
						    if (ch != ~0 && parse_ioread(&parse->parse_io, (unsigned int)ch, &ctime))
						    {
							    /*
							     * up up and away (hopefully ...)
							     * don't press it if resources are tight or nobody wants it
							     */
							    nmp = (mblk_t *)NULL;
							    if (canput(parse->parse_queue->q_next) && (nmp = allocb(sizeof(parsetime_t), BPRI_MED)))
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
					    if (parse_ioread(&parse->parse_io, (unsigned int)0, &ctime))
					    {
						    /*
						     * up up and away (hopefully ...)
						     * don't press it if resources are tight or nobody wants it
						     */
						    nmp = (mblk_t *)NULL;
						    if (canput(parse->parse_queue->q_next) && (nmp = allocb(sizeof(parsetime_t), BPRI_MED)))
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
			    register parsestream_t * parse = (parsestream_t *)(void *)q->q_ptr;
			    timestamp_t ctime;
			    register mblk_t *nmp;
			    register int status = cd_invert ^ (type == M_UNHANGUP);

			    uniqtime(&ctime.tv);

			    parseprintf(DD_RPUT,("parse: parserput - M_%sHANGUP\n", (type == M_HANGUP) ? "" : "UN"));

			    if ((parse->parse_status & PARSE_ENABLE) &&
				parse_iopps(&parse->parse_io, (int)(status ? SYNC_ONE : SYNC_ZERO), &ctime))
			    {
				    nmp = (mblk_t *)NULL;
				    if (canput(parse->parse_queue->q_next) && (nmp = allocb(sizeof(parsetime_t), BPRI_MED)))
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
				if (canput(q->q_next) || (mp->b_datap->db_type > QPCTL))
				{
					putnext(q, mp);
				}
				else
				    putq(q, mp);

			    if (status)
			    {
				    parse->parse_ppsclockev.tv = ctime.tv;
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
	register queue_t *q
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

		parseprintf(DD_INSTALL, ("init_linemon: driver is \"%s\"\n", dname));

#ifdef sun
		if (dname && !Strcmp(dname, "zs"))
		{
			return init_zs_linemon(dq, q);
		}
		else
#endif
		{
			parseprintf(DD_INSTALL, ("init_linemon: driver \"%s\" not suitable for CD monitoring\n", dname));
			return 0;
		}
	}
	parseprintf(DD_INSTALL, ("init_linemon: cannot find driver\n"));
	return 0;
}

static void
close_linemon(
	register queue_t *q,
	register queue_t *my_q
	)
{
	/*
	 * find appropriate driver dependent routine
	 */
	if (q->q_qinfo && q->q_qinfo->qi_minfo)
	{
		register char *dname = q->q_qinfo->qi_minfo->mi_idname;

#ifdef sun
		if (dname && !Strcmp(dname, "zs"))
		{
			close_zs_linemon(q, my_q);
			return;
		}
		parseprintf(DD_INSTALL, ("close_linemon: cannot find driver close routine for \"%s\"\n", dname));
#endif
	}
	parseprintf(DD_INSTALL, ("close_linemon: cannot find driver name\n"));
}

#ifdef sun

#include <sundev/zsreg.h>
#include <sundev/zscom.h>
#include <sundev/zsvar.h>

static unsigned long cdmask  = ZSRR0_CD;

struct savedzsops
{
	struct zsops  zsops;
	struct zsops *oldzsops;
};

struct zsops   *emergencyzs;
extern void zsopinit   (struct zscom *, struct zsops *);
static int  zs_xsisr   (struct zscom *);	/* zs external status interupt handler */

static int
init_zs_linemon(
	register queue_t *q,
	register queue_t *my_q
	)
{
	register struct zscom *zs;
	register struct savedzsops *szs;
	register parsestream_t  *parsestream = (parsestream_t *)(void *)my_q->q_ptr;
	/*
	 * we expect the zsaline pointer in the q_data pointer
	 * from there on we insert our on EXTERNAL/STATUS ISR routine
	 * into the interrupt path, before the standard handler
	 */
	zs = ((struct zsaline *)(void *)q->q_ptr)->za_common;
	if (!zs)
	{
		/*
		 * well - not found on startup - just say no (shouldn't happen though)
		 */
		return 0;
	}
	else
	{
		unsigned long s;

		/*
		 * we do a direct replacement, in case others fiddle also
		 * if somebody else grabs our hook and we disconnect
		 * we are in DEEP trouble - panic is likely to be next, sorry
		 */
		szs = (struct savedzsops *)(void *)kmem_alloc(sizeof(struct savedzsops));

		if (szs == (struct savedzsops *)0)
		{
			parseprintf(DD_INSTALL, ("init_zs_linemon: CD monitor NOT installed - no memory\n"));

			return 0;
		}
		else
		{
			parsestream->parse_data   = (void *)szs;

			s = splhigh();

			parsestream->parse_dqueue = q; /* remember driver */

			szs->zsops            = *zs->zs_ops;
			szs->zsops.zsop_xsint = zs_xsisr; /* place our bastard */
			szs->oldzsops         = zs->zs_ops;
			emergencyzs           = zs->zs_ops;

			zsopinit(zs, &szs->zsops); /* hook it up */

			(void) splx(s);

			parseprintf(DD_INSTALL, ("init_zs_linemon: CD monitor installed\n"));

			return 1;
		}
	}
}

/*
 * unregister our ISR routine - must call under splhigh()
 */
static void
close_zs_linemon(
	register queue_t *q,
	register queue_t *my_q
	)
{
	register struct zscom *zs;
	register parsestream_t  *parsestream = (parsestream_t *)(void *)my_q->q_ptr;

	zs = ((struct zsaline *)(void *)q->q_ptr)->za_common;
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

		zsopinit(zs, szs->oldzsops); /* reset to previous handler functions */

		kmem_free((caddr_t)szs, sizeof (struct savedzsops));

		parseprintf(DD_INSTALL, ("close_zs_linemon: CD monitor deleted\n"));
		return;
	}
}

#define MAXDEPTH 50		/* maximum allowed stream crawl */

#ifdef PPS_SYNC
extern void hardpps (struct timeval *, long);
#ifdef PPS_NEW
extern struct timeval timestamp;
#else
extern struct timeval pps_time;
#endif
#endif

/*
 * take external status interrupt (only CD interests us)
 */
static int
zs_xsisr(
	 struct zscom *zs
	)
{
	register struct zsaline *za = (struct zsaline *)(void *)zs->zs_priv;
	register struct zscc_device *zsaddr = zs->zs_addr;
	register queue_t *q;
	register unsigned char zsstatus;
	register int loopcheck;
	register char *dname;
#ifdef PPS_SYNC
	register unsigned int s;
	register long usec;
#endif

	/*
	 * pick up current state
	 */
	zsstatus = zsaddr->zscc_control;

	if ((za->za_rr0 ^ zsstatus) & (cdmask))
	{
		timestamp_t cdevent;
		register int status;

		za->za_rr0 = (za->za_rr0 & ~(cdmask)) | (zsstatus & (cdmask));

#ifdef PPS_SYNC
		s = splclock();
#ifdef PPS_NEW
		usec = timestamp.tv_usec;
#else
		usec = pps_time.tv_usec;
#endif
#endif
		/*
		 * time stamp
		 */
		uniqtime(&cdevent.tv);

#ifdef PPS_SYNC
		(void)splx(s);
#endif

		/*
		 * logical state
		 */
		status = cd_invert ? (zsstatus & cdmask) == 0 : (zsstatus & cdmask) != 0;

#ifdef PPS_SYNC
		if (status)
		{
			usec = cdevent.tv.tv_usec - usec;
			if (usec < 0)
			    usec += 1000000;

			hardpps(&cdevent.tv, usec);
		}
#endif

		q = za->za_ttycommon.t_readq;

		/*
		 * ok - now the hard part - find ourself
		 */
		loopcheck = MAXDEPTH;

		while (q)
		{
			if (q->q_qinfo && q->q_qinfo->qi_minfo)
			{
				dname = q->q_qinfo->qi_minfo->mi_idname;

				if (!Strcmp(dname, parseinfo.st_rdinit->qi_minfo->mi_idname))
				{
					/*
					 * back home - phew (hopping along stream queues might
					 * prove dangerous to your health)
					 */

					if ((((parsestream_t *)(void *)q->q_ptr)->parse_status & PARSE_ENABLE) &&
					    parse_iopps(&((parsestream_t *)(void *)q->q_ptr)->parse_io, (int)(status ? SYNC_ONE : SYNC_ZERO), &cdevent))
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
						parse_iodone(&((parsestream_t *)(void *)q->q_ptr)->parse_io);
					}

					if (status)
					{
						((parsestream_t *)(void *)q->q_ptr)->parse_ppsclockev.tv = cdevent.tv;
						++(((parsestream_t *)(void *)q->q_ptr)->parse_ppsclockev.serial);
					}

					parseprintf(DD_ISR, ("zs_xsisr: CD event %s has been posted for \"%s\"\n", status ? "ONE" : "ZERO", dname));
					break;
				}
			}

			q = q->q_next;

			if (!loopcheck--)
			{
				panic("zs_xsisr: STREAMS Queue corrupted - CD event");
			}
		}

		/*
		 * only pretend that CD has been handled
		 */
		ZSDELAY(2);

		if (!((za->za_rr0 ^ zsstatus) & ~(cdmask)))
		{
			/*
			 * all done - kill status indication and return
			 */
			zsaddr->zscc_control = ZSWR0_RESET_STATUS; /* might kill other conditions here */
			return 0;
		}
	}

	if (zsstatus & cdmask)	/* fake CARRIER status */
		za->za_flags |= ZAS_CARR_ON;
	else
		za->za_flags &= ~ZAS_CARR_ON;

	/*
	 * we are now gathered here to process some unusual external status
	 * interrupts.
	 * any CD events have also been handled and shouldn't be processed
	 * by the original routine (unless we have a VERY busy port pin)
	 * some initializations are done here, which could have been done before for
	 * both code paths but have been avoided for minimum path length to
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
			if (!Strcmp(dname, parseinfo.st_rdinit->qi_minfo->mi_idname))
			{
				register int (*zsisr) (struct zscom *);

				/*
				 * back home - phew (hopping along stream queues might
				 * prove dangerous to your health)
				 */
				if ((zsisr = ((struct savedzsops *)((parsestream_t *)(void *)q->q_ptr)->parse_data)->oldzsops->zsop_xsint))
					return zsisr(zs);
				else
				    panic("zs_xsisr: unable to locate original ISR");

				parseprintf(DD_ISR, ("zs_xsisr: non CD event was processed for \"%s\"\n", dname));
				/*
				 * now back to our program ...
				 */
				return 0;
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
	return 0;
}
#endif				/* sun */

/*
 * History:
 *
 * parsestreams.c,v
 * Revision 4.11  2005/04/16 17:32:10  kardel
 * update copyright
 *
 * Revision 4.10  2004/11/14 16:06:08  kardel
 * update Id tags
 *
 * Revision 4.9  2004/11/14 15:29:41  kardel
 * support PPSAPI, upgrade Copyright to Berkeley style
 *
 * Revision 4.7  1999/11/28 09:13:53  kardel
 * RECON_4_0_98F
 *
 * Revision 4.6  1998/12/20 23:45:31  kardel
 * fix types and warnings
 *
 * Revision 4.5  1998/11/15 21:23:38  kardel
 * ntp_memset() replicated in Sun kernel files
 *
 * Revision 4.4  1998/06/13 12:15:59  kardel
 * superfluous variable removed
 *
 * Revision 4.3  1998/06/12 15:23:08  kardel
 * fix prototypes
 * adjust for ansi2knr
 *
 * Revision 4.2  1998/05/24 18:16:22  kardel
 * moved copy of shadow status to the beginning
 *
 * Revision 4.1  1998/05/24 09:38:47  kardel
 * streams initiated iopps calls (M_xHANGUP) are now consistent with the
 * respective calls from zs_xsisr()
 * simulation of CARRIER status to avoid unecessary M_xHANGUP messages
 *
 * Revision 4.0  1998/04/10 19:45:38  kardel
 * Start 4.0 release version numbering
 *
 * from V3 3.37 log info deleted 1998/04/11 kardel
 */
