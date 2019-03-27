/*
 * tg.c generate WWV or IRIG signals for test
 */
/*
 * This program can generate audio signals that simulate the WWV/H
 * broadcast timecode. Alternatively, it can generate the IRIG-B
 * timecode commonly used to synchronize laboratory equipment. It is
 * intended to test the WWV/H driver (refclock_wwv.c) and the IRIG
 * driver (refclock_irig.c) in the NTP driver collection.
 *
 * Besides testing the drivers themselves, this program can be used to
 * synchronize remote machines over audio transmission lines or program
 * feeds. The program reads the time on the local machine and sets the
 * initial epoch of the signal generator within one millisecond.
 * Alernatively, the initial epoch can be set to an arbitrary time. This
 * is useful when searching for bugs and testing for correct response to
 * a leap second in UTC. Note however, the ultimate accuracy is limited
 * by the intrinsic frequency error of the codec sample clock, which can
 # reach well over 100 PPM.
 *
 * The default is to route generated signals to the line output
 * jack; the s option on the command line routes these signals to the
 * internal speaker as well. The v option controls the speaker volume
 * over the range 0-255. The signal generator by default uses WWV
 * format; the h option switches to WWVH format and the i option
 * switches to IRIG-B format.
 *
 * Once started the program runs continuously. The default initial epoch
 * for the signal generator is read from the computer system clock when
 * the program starts. The y option specifies an alternate epoch using a
 * string yydddhhmmss, where yy is the year of century, ddd the day of
 * year, hh the hour of day and mm the minute of hour. For instance,
 * 1946Z on 1 January 2006 is 060011946. The l option lights the leap
 * warning bit in the WWV/H timecode, so is handy to check for correct
 * behavior at the next leap second epoch. The remaining options are
 * specified below under the Parse Options heading. Most of these are
 * for testing.
 *
 * During operation the program displays the WWV/H timecode (9 digits)
 * or IRIG timecode (20 digits) as each new string is constructed. The
 * display is followed by the BCD binary bits as transmitted. Note that
 * the transmissionorder is low-order first as the frame is processed
 * left to right. For WWV/H The leap warning L preceeds the first bit.
 * For IRIG the on-time marker M preceeds the first (units) bit, so its
 * code is delayed one bit and the next digit (tens) needs only three
 * bits.
 *
 * The program has been tested with the Sun Blade 1500 running Solaris
 * 10, but not yet with other machines. It uses no special features and
 * should be readily portable to other hardware and operating systems.
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/audio.h>
#include <math.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#define	SECOND	8000		/* one second of 125-us samples */
#define BUFLNG	400		/* buffer size */
#define	DEVICE	"/dev/audio"	/* default audio device */
#define	WWV	0		/* WWV encoder */
#define	IRIG	1		/* IRIG-B encoder */
#define	OFF	0		/* zero amplitude */
#define	LOW	1		/* low amplitude */
#define	HIGH	2		/* high amplitude */
#define	DATA0	200		/* WWV/H 0 pulse */
#define	DATA1	500		/* WWV/H 1 pulse */
#define PI	800		/* WWV/H PI pulse */
#define	M2	2		/* IRIG 0 pulse */
#define	M5	5		/* IRIG 1 pulse */
#define	M8	8		/* IRIG PI pulse */

/*
 * Companded sine table amplitude 3000 units
 */
int c3000[] = {1, 48, 63, 70, 78, 82, 85, 89, 92, 94,	/* 0-9 */
     96,  98,  99, 100, 101, 101, 102, 103, 103, 103,	/* 10-19 */
    103, 103, 103, 103, 102, 101, 101, 100,  99,  98,	/* 20-29 */
     96,  94,  92,  89,  85,  82,  78,  70,  63,  48,	/* 30-39 */
    129, 176, 191, 198, 206, 210, 213, 217, 220, 222,	/* 40-49 */
    224, 226, 227, 228, 229, 229, 230, 231, 231, 231, 	/* 50-59 */
    231, 231, 231, 231, 230, 229, 229, 228, 227, 226,	/* 60-69 */
    224, 222, 220, 217, 213, 210, 206, 198, 191, 176}; 	/* 70-79 */
/*
 * Companded sine table amplitude 6000 units
 */
int c6000[] = {1, 63, 78, 86, 93, 98, 101, 104, 107, 110, /* 0-9 */
    112, 113, 115, 116, 117, 117, 118, 118, 119, 119,	/* 10-19 */
    119, 119, 119, 118, 118, 117, 117, 116, 115, 113,	/* 20-29 */
    112, 110, 107, 104, 101,  98,  93,  86,  78,  63,	/* 30-39 */
    129, 191, 206, 214, 221, 226, 229, 232, 235, 238,	/* 40-49 */
    240, 241, 243, 244, 245, 245, 246, 246, 247, 247, 	/* 50-59 */
    247, 247, 247, 246, 246, 245, 245, 244, 243, 241,	/* 60-69 */
    240, 238, 235, 232, 229, 226, 221, 214, 206, 191}; 	/* 70-79 */

/*
 * Decoder operations at the end of each second are driven by a state
 * machine. The transition matrix consists of a dispatch table indexed
 * by second number. Each entry in the table contains a case switch
 * number and argument.
 */
struct progx {
	int sw;			/* case switch number */
	int arg;		/* argument */
};

/*
 * Case switch numbers
 */
#define DATA	0		/* send data (0, 1, PI) */
#define COEF	1		/* send BCD bit */
#define	DEC	2		/* decrement to next digit */
#define	MIN	3		/* minute pulse */
#define	LEAP	4		/* leap warning */
#define	DUT1	5		/* DUT1 bits */
#define	DST1	6		/* DST1 bit */
#define	DST2	7		/* DST2 bit */

/*
 * WWV/H format (100-Hz, 9 digits, 1 m frame)
 */
struct progx progx[] = {
	{MIN,	800},		/* 0 minute sync pulse */
	{DATA,	DATA0},		/* 1 */
	{DST2,	0},		/* 2 DST2 */
	{LEAP,	0},		/* 3 leap warning */
	{COEF,	1},		/* 4 1 year units */
	{COEF,	2},		/* 5 2 */
	{COEF,	4},		/* 6 4 */
	{COEF,	8},		/* 7 8 */
	{DEC,	DATA0},		/* 8 */
	{DATA,	PI},		/* 9 p1 */
	{COEF,	1},		/* 10 1 minute units */
	{COEF,	2},		/* 11 2 */
	{COEF,	4},		/* 12 4 */
	{COEF,	8},		/* 13 8 */
	{DEC,	DATA0},		/* 14 */
	{COEF,	1},		/* 15 10 minute tens */
	{COEF,	2},		/* 16 20 */
	{COEF,	4},		/* 17 40 */
	{COEF,	8},		/* 18 80 (not used) */
	{DEC,	PI},		/* 19 p2 */
	{COEF,	1},		/* 20 1 hour units */
	{COEF,	2},		/* 21 2 */
	{COEF,	4},		/* 22 4 */
	{COEF,	8},		/* 23 8 */
	{DEC,	DATA0},		/* 24 */
	{COEF,	1},		/* 25 10 hour tens */
	{COEF,	2},		/* 26 20 */
	{COEF,	4},		/* 27 40 (not used) */
	{COEF,	8},		/* 28 80 (not used) */
	{DEC,	PI},		/* 29 p3 */
	{COEF,	1},		/* 30 1 day units */
	{COEF,	2},		/* 31 2 */
	{COEF,	4},		/* 32 4 */
	{COEF,	8},		/* 33 8 */
	{DEC,	DATA0},		/* 34 not used */
	{COEF,	1},		/* 35 10 day tens */
	{COEF,	2},		/* 36 20 */
	{COEF,	4},		/* 37 40 */
	{COEF,	8},		/* 38 80 */
	{DEC,	PI},		/* 39 p4 */
	{COEF,	1},		/* 40 100 day hundreds */
	{COEF,	2},		/* 41 200 */
	{COEF,	4},		/* 42 400 (not used) */
	{COEF,	8},		/* 43 800 (not used) */
	{DEC,	DATA0},		/* 44 */
	{DATA,	DATA0},		/* 45 */
	{DATA,	DATA0},		/* 46 */
	{DATA,	DATA0},		/* 47 */
	{DATA,	DATA0},		/* 48 */
	{DATA,	PI},		/* 49 p5 */
	{DUT1,	8},		/* 50 DUT1 sign */
	{COEF,	1},		/* 51 10 year tens */
	{COEF,	2},		/* 52 20 */
	{COEF,	4},		/* 53 40 */
	{COEF,	8},		/* 54 80 */
	{DST1,	0},		/* 55 DST1 */
	{DUT1,	1},		/* 56 0.1 DUT1 fraction */
	{DUT1,	2},		/* 57 0.2 */
	{DUT1,	4},		/* 58 0.4 */
	{DATA,	PI},		/* 59 p6 */
	{DATA,	DATA0},		/* 60 leap */
};

/*
 * IRIG format except first frame (1000 Hz, 20 digits, 1 s frame)
 */
struct progx progy[] = {
	{COEF,	1},		/* 0 1 units */
	{COEF,	2},		/* 1 2 */
	{COEF,	4},		/* 2 4 */
	{COEF,	8},		/* 3 8 */
	{DEC,	M2},		/* 4 im */
	{COEF,	1},		/* 5 10 tens */
	{COEF,	2},		/* 6 20 */
	{COEF,	4},		/* 7 40 */
	{COEF,	8},		/* 8 80 */
	{DEC,	M8},		/* 9 pi */
};

/*
 * IRIG format first frame (1000 Hz, 20 digits, 1 s frame)
 */
struct progx progz[] = {
	{MIN,	M8},		/* 0 pi (second) */
	{COEF,	1},		/* 1 1 units */
	{COEF,	2},		/* 2 2 */
	{COEF,	4},		/* 3 4 */
	{COEF,	8},		/* 4 8 */
	{DEC,	M2},		/* 5 im */
	{COEF,	1},		/* 6 10 tens */
	{COEF,	2},		/* 7 20 */
	{COEF,	4},		/* 8 40 */
	{DEC,	M8},		/* 9 pi */
};

/*
 * Forward declarations
 */
void	sec(int);		/* send second */
void	digit(int);		/* encode digit */
void	peep(int, int, int);	/* send cycles */
void	delay(int);		/* delay samples */

/*
 * Global variables
 */
char	buffer[BUFLNG];		/* output buffer */
int	bufcnt = 0;		/* buffer counter */
int	second = 0;		/* seconds counter */
int	fd;			/* audio codec file descriptor */
int	tone = 1000;		/* WWV sync frequency */
int	level = AUDIO_MAX_GAIN / 8; /* output level */
int	port = AUDIO_LINE_OUT;	/* output port */
int	encode = WWV;		/* encoder select */
int	leap = 0;		/* leap indicator */
int	dst = 0;		/* winter/summer time */
int	dut1 = 0;		/* DUT1 correction (sign, magnitude) */
int	utc = 0;		/* option epoch */

/*
 * Main program
 */
int
main(
	int	argc,		/* command line options */
	char	**argv		/* poiniter to list of tokens */
	)
{
	struct timeval tv;	/* system clock at startup */
	audio_info_t info;	/* Sun audio structure */
	struct tm *tm = NULL;	/* structure returned by gmtime */
	char	device[50];	/* audio device */
	char	code[100];	/* timecode */
	int	rval, temp, arg, sw, ptr;
	int	minute, hour, day, year;
	int	i;

	/*
	 * Parse options
	 */
	strlcpy(device, DEVICE, sizeof(device));
	year = 0;
	while ((temp = getopt(argc, argv, "a:dhilsu:v:y:")) != -1) {
		switch (temp) {

		case 'a':	/* specify audio device (/dev/audio) */
			strlcpy(device, optarg, sizeof(device));
			break;

		case 'd':	/* set DST for summer (WWV/H only) */
			dst++;
			break;

		case 'h':	/* select WWVH sync frequency */
			tone = 1200;
			break;

		case 'i':	/* select irig format */
			encode = IRIG;
			break;

		case 'l':	/* set leap warning bit (WWV/H only) */
			leap++;
			break;

		case 's':	/* enable speaker */
			port |= AUDIO_SPEAKER;
			break;

		case 'u':	/* set DUT1 offset (-7 to +7) */
			sscanf(optarg, "%d", &dut1);
			if (dut1 < 0)
				dut1 = abs(dut1);
			else
				dut1 |= 0x8;
			break;

		case 'v':	/* set output level (0-255) */
			sscanf(optarg, "%d", &level);
			break;

		case 'y':	/* set initial date and time */
			sscanf(optarg, "%2d%3d%2d%2d", &year, &day,
			    &hour, &minute);
			utc++;
			break;

		defult:
			printf("invalid option %c\n", temp);
			break;
		}
	}

	/*
	 * Open audio device and set options
	 */
	fd = open("/dev/audio", O_WRONLY);
	if (fd <= 0) {
		printf("audio open %s\n", strerror(errno));
		exit(1);
	}
	rval = ioctl(fd, AUDIO_GETINFO, &info);
	if (rval < 0) {
		printf("audio control %s\n", strerror(errno));
		exit(0);
	}
	info.play.port = port;
	info.play.gain = level;
	info.play.sample_rate = SECOND;
	info.play.channels = 1;
	info.play.precision = 8;
	info.play.encoding = AUDIO_ENCODING_ULAW;
	printf("port %d gain %d rate %d chan %d prec %d encode %d\n",
	    info.play.port, info.play.gain, info.play.sample_rate,
	    info.play.channels, info.play.precision,
	    info.play.encoding);
	ioctl(fd, AUDIO_SETINFO, &info);

 	/*
	 * Unless specified otherwise, read the system clock and
	 * initialize the time.
	 */
	if (!utc) {
		gettimeofday(&tv, NULL);
		tm = gmtime(&tv.tv_sec);
		minute = tm->tm_min;
		hour = tm->tm_hour;
		day = tm->tm_yday + 1;
		year = tm->tm_year % 100;
		second = tm->tm_sec;

		/*
		 * Delay the first second so the generator is accurately
		 * aligned with the system clock within one sample (125
		 * microseconds ).
		 */
		delay(SECOND - tv.tv_usec * 8 / 1000);
	}
	memset(code, 0, sizeof(code));
	switch (encode) {

	/*
	 * For WWV/H and default time, carefully set the signal
	 * generator seconds number to agree with the current time.
	 */ 
	case WWV:
		printf("year %d day %d time %02d:%02d:%02d tone %d\n",
		    year, day, hour, minute, second, tone);
		snprintf(code, sizeof(code), "%01d%03d%02d%02d%01d",
		    year / 10, day, hour, minute, year % 10);
		printf("%s\n", code);
		ptr = 8;
		for (i = 0; i <= second; i++) {
			if (progx[i].sw == DEC)
				ptr--;
		}
		break;

	/*
	 * For IRIG the signal generator runs every second, so requires
	 * no additional alignment.
	 */
	case IRIG:
		printf("sbs %x year %d day %d time %02d:%02d:%02d\n",
		    0, year, day, hour, minute, second);
		break;
	}

	/*
	 * Run the signal generator to generate new timecode strings
	 * once per minute for WWV/H and once per second for IRIG.
	 */
	while(1) {

		/*
		 * Crank the state machine to propagate carries to the
		 * year of century. Note that we delayed up to one
		 * second for alignment after reading the time, so this
		 * is the next second.
		 */
		second = (second + 1) % 60;
		if (second == 0) {
			minute++;
			if (minute >= 60) {
				minute = 0;
				hour++;
			}
			if (hour >= 24) {
				hour = 0;
				day++;
			}

			/*
			 * At year rollover check for leap second.
			 */
			if (day >= (year & 0x3 ? 366 : 367)) {
				if (leap) {
					sec(DATA0);
					printf("\nleap!");
					leap = 0;
				}
				day = 1;
				year++;
			}
			if (encode == WWV) {
				snprintf(code, sizeof(code),
				    "%01d%03d%02d%02d%01d", year / 10,
				    day, hour, minute, year % 10);
				printf("\n%s\n", code);
				ptr = 8;
			}
		}
		if (encode == IRIG) {
			snprintf(code, sizeof(code),
			    "%04x%04d%06d%02d%02d%02d", 0, year, day,
			    hour, minute, second);
			printf("%s\n", code);
			ptr = 19;
		}

		/*
		 * Generate data for the second
		 */
		switch(encode) {

		/*
		 * The IRIG second consists of 20 BCD digits of width-
		 * modulateod pulses at 2, 5 and 8 ms and modulated 50
		 * percent on the 1000-Hz carrier.
		 */
		case IRIG:
			for (i = 0; i < 100; i++) {
				if (i < 10) {
					sw = progz[i].sw;
					arg = progz[i].arg;
				} else {
					sw = progy[i % 10].sw;
					arg = progy[i % 10].arg;
				}
				switch(sw) {

				case COEF:	/* send BCD bit */
					if (code[ptr] & arg) {
						peep(M5, 1000, HIGH);
						peep(M5, 1000, LOW);
						printf("1");
					} else {
						peep(M2, 1000, HIGH);
						peep(M8, 1000, LOW);
						printf("0");
					}
					break;

				case DEC:	/* send IM/PI bit */
					ptr--;
					printf(" ");
					peep(arg, 1000, HIGH);
					peep(10 - arg, 1000, LOW);
					break;

				case MIN:	/* send data bit */
					peep(arg, 1000, HIGH);
					peep(10 - arg, 1000, LOW);
					printf("M ");
					break;
				}
				if (ptr < 0)
					break;
			}
			printf("\n");
			break;

		/*
		 * The WWV/H second consists of 9 BCD digits of width-
		 * modulateod pulses 200, 500 and 800 ms at 100-Hz.
		 */
		case WWV:
			sw = progx[second].sw;
			arg = progx[second].arg;
			switch(sw) {

			case DATA:		/* send data bit */
				sec(arg);
				break;

			case COEF:		/* send BCD bit */
				if (code[ptr] & arg) {
					sec(DATA1);
					printf("1");
				} else {
					sec(DATA0);
					printf("0");
				}
				break;

			case LEAP:		/* send leap bit */
				if (leap) {
					sec(DATA1);
					printf("L ");
				} else {
					sec(DATA0);
					printf("  ");
				}
				break;

			case DEC:		/* send data bit */
				ptr--;
				sec(arg);
				printf(" ");
				break;

			case MIN:		/* send minute sync */
				peep(arg, tone, HIGH);
				peep(1000 - arg, tone, OFF);
				break;

			case DUT1:		/* send DUT1 bits */
				if (dut1 & arg)
					sec(DATA1);
				else
					sec(DATA0);
				break;
				
			case DST1:		/* send DST1 bit */
				ptr--;
				if (dst)
					sec(DATA1);
				else
					sec(DATA0);
				printf(" ");
				break;

			case DST2:		/* send DST2 bit */
				if (dst)
					sec(DATA1);
				else
					sec(DATA0);
				break;
			}
		}
	}
}


/*
 * Generate WWV/H 0 or 1 data pulse.
 */
void sec(
	int	code		/* DATA0, DATA1, PI */
	)
{
	/*
	 * The WWV data pulse begins with 5 ms of 1000 Hz follwed by a
	 * guard time of 25 ms. The data pulse is 170, 570 or 770 ms at
	 * 100 Hz corresponding to 0, 1 or position indicator (PI),
	 * respectively. Note the 100-Hz data pulses are transmitted 6
	 * dB below the 1000-Hz sync pulses. Originally the data pulses
	 * were transmited 10 dB below the sync pulses, but the station
	 * engineers increased that to 6 dB because the Heath GC-1000
	 * WWV/H radio clock worked much better.
	 */
	peep(5, tone, HIGH);		/* send seconds tick */
	peep(25, tone, OFF);
	peep(code - 30, 100, LOW);	/* send data */
	peep(1000 - code, 100, OFF);
}


/*
 * Generate cycles of 100 Hz or any multiple of 100 Hz.
 */
void peep(
	int	pulse,		/* pulse length (ms) */
	int	freq,		/* frequency (Hz) */
	int	amp		/* amplitude */
	)
{
	int	increm;		/* phase increment */
	int	i, j;

	if (amp == OFF || freq == 0)
		increm = 10;
	else
		increm = freq / 100;
	j = 0;
	for (i = 0 ; i < pulse * 8; i++) {
		switch (amp) {

		case HIGH:
			buffer[bufcnt++] = ~c6000[j];
			break;

		case LOW:
			buffer[bufcnt++] = ~c3000[j];
			break;

		default:
			buffer[bufcnt++] = ~0;
		}
		if (bufcnt >= BUFLNG) {
			write(fd, buffer, BUFLNG);
			bufcnt = 0;
		}
		j = (j + increm) % 80;
	}
}


/*
 * Delay for initial phasing
 */
void delay (
	int	delay		/* delay in samples */
	)
{
	int	samples;	/* samples remaining */

	samples = delay;
	memset(buffer, 0, BUFLNG);
	while (samples >= BUFLNG) {
		write(fd, buffer, BUFLNG);
		samples -= BUFLNG;
	}
		write(fd, buffer, samples);
}
