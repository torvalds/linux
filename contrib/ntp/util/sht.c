/* 
 * sht.c - Testprogram for shared memory refclock
 * read/write shared memory segment; see usage
 */
#include "config.h"

#ifndef SYS_WINNT
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#else
#include <windows.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <iostream.h>
#define sleep(x) Sleep(x*1000)
#endif
#include <assert.h>

struct shmTime {
	int    mode; /* 0 - if valid set
		      *       use values, 
		      *       clear valid
		      * 1 - if valid set 
		      *       if count before and after read of values is equal,
		      *         use values 
		      *       clear valid
		      */
	volatile int	count;
	time_t		clockTimeStampSec;
	int		clockTimeStampUSec;
	time_t		receiveTimeStampSec;
	int		receiveTimeStampUSec;
	int		leap;
	int		precision;
	int		nsamples;
	volatile int	valid;
	unsigned	clockTimeStampNSec;	/* Unsigned ns timestamps */
	unsigned	receiveTimeStampNSec;	/* Unsigned ns timestamps */
};

static struct shmTime *
getShmTime (
	int unit
	)
{
#ifndef SYS_WINNT
	int shmid=shmget (0x4e545030+unit, sizeof (struct shmTime), IPC_CREAT|0777);
	if (shmid==-1) {
		perror ("shmget");
		exit (1);
	}
	else {
		struct shmTime *p=(struct shmTime *)shmat (shmid, 0, 0);
		if ((int)(long)p==-1) {
			perror ("shmat");
			p=0;
		}
		assert (p!=0);
		return p;
	}
#else
	char buf[10];
	LPSECURITY_ATTRIBUTES psec=0;
	snprintf (buf, sizeof(buf), "NTP%d", unit);
	SECURITY_DESCRIPTOR sd;
	SECURITY_ATTRIBUTES sa;
	HANDLE shmid;

	assert (InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION));
	assert (SetSecurityDescriptorDacl(&sd,1,0,0));
	sa.nLength=sizeof (SECURITY_ATTRIBUTES);
	sa.lpSecurityDescriptor=&sd;
	sa.bInheritHandle=0;
	shmid=CreateFileMapping ((HANDLE)0xffffffff, 0, PAGE_READWRITE,
				 psec, sizeof (struct shmTime),buf);
	if (!shmid) {
		shmid=CreateFileMapping ((HANDLE)0xffffffff, 0, PAGE_READWRITE,
					 0, sizeof (struct shmTime),buf);
		cout <<"CreateFileMapping with psec!=0 failed"<<endl;
	}

	if (!shmid) {
		char mbuf[1000];
		FormatMessage (FORMAT_MESSAGE_FROM_SYSTEM,
			       0, GetLastError (), 0, mbuf, sizeof (mbuf), 0);
		int x=GetLastError ();
		cout <<"CreateFileMapping "<<buf<<":"<<mbuf<<endl;
		exit (1);
	}
	else {
		struct shmTime *p=(struct shmTime *) MapViewOfFile (shmid, 
								    FILE_MAP_WRITE, 0, 0, sizeof (struct shmTime));
		if (p==0) {
			char mbuf[1000];
			FormatMessage (FORMAT_MESSAGE_FROM_SYSTEM,
				       0, GetLastError (), 0, mbuf, sizeof (mbuf), 0);
			cout <<"MapViewOfFile "<<buf<<":"<<mbuf<<endl;
			exit (1);
		}
		return p;
	}
	return 0;
#endif
}


int
main (
	int argc,
	char *argv[]
	)
{
	volatile struct shmTime *p;
	int unit;
	char *argp;

	if (argc<=1) {
	  usage:
		printf ("usage: %s [uu:]{r[c][l]|w|snnn}\n",argv[0]);
		printf ("       uu use clock unit uu (default: 2)\n");
		printf ("       r read shared memory\n");
		printf ("       c clear valid-flag\n");
		printf ("       l loop (so, rcl will read and clear in a loop\n");
		printf ("       w write shared memory with current time\n");
		printf ("       snnnn set nsamples to nnn\n");
		printf ("       lnnnn set leap to nnn\n");
		printf ("       pnnnn set precision to -nnn\n");
		exit (0);
	}

	srand(time(NULL));
		
	unit = strtoul(argv[1], &argp, 10);
	if (argp == argv[1])
		unit = 2;
	else if (*argp == ':')
		argp++;
	else
		goto usage;

	p=getShmTime(unit);
	switch (*argp) {
	case 's':
		p->nsamples=atoi(argp+1);
		break;

	case 'l':
		p->leap=atoi(argp+1);
		break;

	case 'p':
		p->precision=-atoi(argp+1);
		break;

	case 'r': {
		int clear=0;
		int loop=0;
		printf ("reader\n");		
		while (*++argp) {
			switch (*argp) {
			case 'l': loop=1; break;
			case 'c': clear=1; break;
			default : goto usage;
			}
		}
again:
		printf ("mode=%d, count=%d, clock=%ld.%09u, rec=%ld.%09u,\n",
			p->mode,p->count,
			(long)p->clockTimeStampSec,p->clockTimeStampNSec,
			(long)p->receiveTimeStampSec,p->receiveTimeStampNSec);
		printf ("  leap=%d, precision=%d, nsamples=%d, valid=%d\n",
			p->leap, p->precision, p->nsamples, p->valid);
		if (!p->valid)
			printf ("***\n");
		if (clear) {
			p->valid=0;
			printf ("cleared\n");
		}
		if (loop) {
			sleep (1);
			goto again;
		}
		break;
	}

	case 'w': {
		/* To show some life action, we read the system
		 * clock and use a bit of fuzz from 'random()' to get a
		 * bit of wobbling into the values (so we can observe a
		 * certain jitter!)
		 */
		time_t clk_sec, rcv_sec;
		u_int  clk_frc, rcv_frc;

#if defined(HAVE_CLOCK_GETTIME) && defined(CLOCK_REALTIME)
		
		/* Here we have a high-resolution system clock, and
		 * we're not afraid to use it!
		 */
		struct timespec tmptime;
		if (0 == clock_gettime(CLOCK_REALTIME, &tmptime)) {
			rcv_sec = tmptime.tv_sec;
			rcv_frc = (u_int)tmptime.tv_nsec;
		}
		else
#endif
		{
			time(&rcv_sec);
			rcv_frc = (u_int)random() % 1000000000u;
		}
		/* add a wobble of ~3.5msec to the clock time */
		clk_sec = rcv_sec;
		clk_frc = rcv_frc + (u_int)(random()%7094713 - 3547356);
		/* normalise result -- the SHM driver is picky! */
		while ((int)clk_frc < 0) {
			clk_frc += 1000000000;
			clk_sec -= 1;
		}
		while ((int)clk_frc >= 1000000000) {
			clk_frc -= 1000000000;
			clk_sec += 1;
		}
		
		/* Most 'real' time sources would create a clock
		 * (reference) time stamp where the fraction is zero,
		 * but that's not an actual requirement. So we show how
		 * to deal with the time stamps in general; changing the
		 * behaviour for cases where the fraction of the
		 * clock time is zero should be trivial.
		 */ 
		printf ("writer\n");
		p->mode=0;
		if (!p->valid) {
			p->clockTimeStampSec    = clk_sec;
			p->clockTimeStampUSec   = clk_frc / 1000; /* truncate! */
			p->clockTimeStampNSec   = clk_frc;
			p->receiveTimeStampSec  = rcv_sec;
			p->receiveTimeStampUSec = rcv_frc / 1000; /* truncate! */
			p->receiveTimeStampNSec = rcv_frc;
			printf ("%ld.%09u %ld.%09u\n",
				(long)p->clockTimeStampSec  , p->clockTimeStampNSec  ,
				(long)p->receiveTimeStampSec, p->receiveTimeStampNSec);
			p->valid=1;
		}
		else {
			printf ("p->valid still set\n"); /* not an error! */
		}
		break;
	}
	default:
		break;
	}
	return 0;
}
