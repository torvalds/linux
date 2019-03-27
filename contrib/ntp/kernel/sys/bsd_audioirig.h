/*
 *	$Header: bsd_audioirig.h,v 1.0 93/08/02 12:42:00
 */

#ifndef _BSD_AUDIOIRIG_H_
#define _BSD_AUDIOIRIG_H_

#include <sys/time.h>

/********************************************************************/
/* user interface */

/*
 * irig ioctls
 */
#if defined(__STDC__) || (!defined(sun) && !defined(ibm032) && !defined(__GNUC))
#define AUDIO_IRIG_OPEN         _IO('A', 50)
#define AUDIO_IRIG_CLOSE        _IO('A', 51)
#define AUDIO_IRIG_SETFORMAT    _IOWR('A', 52, int)
#else
#define AUDIO_IRIG_OPEN         _IO(A, 50)
#define AUDIO_IRIG_CLOSE        _IO(A, 51)
#define AUDIO_IRIG_SETFORMAT    _IOWR(A, 52, int)
#endif

/*
 * irig error codes
 */
#define AUDIO_IRIG_BADSIGNAL	0x01
#define AUDIO_IRIG_BADDATA	0x02
#define AUDIO_IRIG_BADSYNC	0x04
#define AUDIO_IRIG_BADCLOCK	0x08
#define AUDIO_IRIG_OLDDATA	0x10

/********************************************************************/

/*
 * auib definitions
 */
#define AUIB_SIZE	(0x0040)
#define AUIB_INC	(0x0008)
#define AUIB_MOD(k)	((k) & 0x0038)
#define AUIB_INIT(ib)	((ib)->ib_head = (ib)->ib_tail = (ib)->ib_lock = \
			 (ib)->phase = (ib)->shi = (ib)->slo = (ib)->high = \
			 (ib)->level0 = (ib)->level1 = \
			 (ib)->shift[0] = (ib)->shift[1] = (ib)->shift[2] = \
			 (ib)->shift[3] = (ib)->sdata[0] = (ib)->sdata[1] = \
			 (ib)->sdata[2] = (ib)->sdata[3] = (ib)->err = 0)
#define AUIB_EMPTY(ib)	((ib)->ib_head == (ib)->ib_tail)
#define AUIB_LEN(ib)	(AUIB_MOD((ib)->ib_tail - (ib)->ib_head))
#define AUIB_LEFT(ib)	(AUIB_MOD((ib)->ib_head - (ib)->ib_tail - 1))
#define IRIGDELAY 3
#define IRIGLEVEL 1355

#ifndef LOCORE
/*
 * irig_time holds IRIG data for one second
 */
struct irig_time {
        struct timeval  stamp;          /* timestamp */
        u_char  bits[13];               /* 100 irig data bits */
	u_char	status;			/* status byte */
        char    time[14];               /* time string */
};

/*
 * auib's are used for IRIG data communication between the trap
 * handler and the software interrupt.
 */
struct auib {
	/* driver variables */
	u_short	active;			/* 0=inactive, else=active */
	u_short	format;			/* time output format */
	struct	irig_time timestr;	/* time structure */
	char	buffer[14];		/* output formation buffer */

	/* hardware interrupt variables */
	struct	timeval tv1,tv2,tv3;	/* time stamps (median filter) */
	int	level0,level1;		/* lo/hi input levels */
	int	level;			/* decision level */
	int	high;			/* recent largest sample */
	int	sl0,sl1;		/* recent sample levels */
	int	lasts;			/* last sample value */
	u_short	scount;			/* sample count */
	u_long	eacc;			/* 10-bit element accumulator */
	u_long	ebit;			/* current bit in element */
	u_char	r_level,mmr1;		/* recording level 0-255 */
	int	shi,slo,phase;		/* AGC variables */
	u_long	err;			/* error status bits */
	int	ecount;			/* count of elements this second */
	long	shift[4];		/* shift register of pos ident */
	long	sdata[4];		/* shift register of symbols */

	int	ib_head;		/* queue head */
	int	ib_tail;		/* queue tail */
	u_short	ib_lock;		/* queue head lock */
	u_long	ib_data[AUIB_SIZE];	/* data buffer */
};
#endif

#endif /* _BSD_AUDIOIRIG_H_ */
