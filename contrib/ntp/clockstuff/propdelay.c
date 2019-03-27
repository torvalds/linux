/* propdelay.c,v 3.1 1993/07/06 01:05:24 jbj Exp
 * propdelay - compute propagation delays
 *
 * cc -o propdelay propdelay.c -lm
 *
 * "Time and Frequency Users' Manual", NBS Technical Note 695 (1977).
 */

/*
 * This can be used to get a rough idea of the HF propagation delay
 * between two points (usually between you and the radio station).
 * The usage is
 *
 * propdelay latitudeA longitudeA latitudeB longitudeB
 *
 * where points A and B are the locations in question.  You obviously
 * need to know the latitude and longitude of each of the places.
 * The program expects the latitude to be preceded by an 'n' or 's'
 * and the longitude to be preceded by an 'e' or 'w'.  It understands
 * either decimal degrees or degrees:minutes:seconds.  Thus to compute
 * the delay between the WWVH (21:59:26N, 159:46:00W) and WWV (40:40:49N,
 * 105:02:27W) you could use:
 *
 * propdelay n21:59:26 w159:46 n40:40:49 w105:02:27
 *
 * By default it prints out a summer (F2 average virtual height 350 km) and
 * winter (F2 average virtual height 250 km) number.  The results will be
 * quite approximate but are about as good as you can do with HF time anyway.
 * You might pick a number between the values to use, or use the summer
 * value in the summer and switch to the winter value when the static
 * above 10 MHz starts to drop off in the fall.  You can also use the
 * -h switch if you want to specify your own virtual height.
 *
 * You can also do a
 *
 * propdelay -W n45:17:47 w75:45:22
 *
 * to find the propagation delays to WWV and WWVH (from CHU in this
 * case), a
 *
 * propdelay -C n40:40:49 w105:02:27
 *
 * to find the delays to CHU, and a
 *
 * propdelay -G n52:03:17 w98:34:18
 *
 * to find delays to GOES via each of the three satellites.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include <stdio.h>
#include <string.h>

#include "ntp_stdlib.h"

extern	double	sin	(double);
extern	double	cos	(double);
extern	double	acos	(double);
extern	double	tan	(double);
extern	double	atan	(double);
extern	double	sqrt	(double);

#define	STREQ(a, b)	(*(a) == *(b) && strcmp((a), (b)) == 0)

/*
 * Program constants
 */
#define	EARTHRADIUS	(6370.0)	/* raduis of earth (km) */
#define	LIGHTSPEED	(299800.0)	/* speed of light, km/s */
#define	PI		(3.1415926536)
#define	RADPERDEG	(PI/180.0)	/* radians per degree */
#define MILE		(1.609344)      /* km in a mile */

#define	SUMMERHEIGHT	(350.0)		/* summer height in km */
#define	WINTERHEIGHT	(250.0)		/* winter height in km */

#define SATHEIGHT	(6.6110 * 6378.0) /* geosync satellite height in km
					     from centre of earth */

#define WWVLAT  "n40:40:49"
#define WWVLONG "w105:02:27"

#define WWVHLAT  "n21:59:26"
#define WWVHLONG "w159:46:00"

#define CHULAT	"n45:17:47"
#define	CHULONG	"w75:45:22"

#define GOES_UP_LAT  "n37:52:00"
#define GOES_UP_LONG "w75:27:00"
#define GOES_EAST_LONG "w75:00:00"
#define GOES_STBY_LONG "w105:00:00"
#define GOES_WEST_LONG "w135:00:00"
#define GOES_SAT_LAT "n00:00:00"

char *wwvlat = WWVLAT;
char *wwvlong = WWVLONG;

char *wwvhlat = WWVHLAT;
char *wwvhlong = WWVHLONG;

char *chulat = CHULAT;
char *chulong = CHULONG;

char *goes_up_lat = GOES_UP_LAT;
char *goes_up_long = GOES_UP_LONG;
char *goes_east_long = GOES_EAST_LONG;
char *goes_stby_long = GOES_STBY_LONG;
char *goes_west_long = GOES_WEST_LONG;
char *goes_sat_lat = GOES_SAT_LAT;

int hflag = 0;
int Wflag = 0;
int Cflag = 0;
int Gflag = 0;
int height;

char const *progname;

static	void	doit		(double, double, double, double, double, char *);
static	double	latlong		(char *, int);
static	double	greatcircle	(double, double, double, double);
static	double	waveangle	(double, double, int);
static	double	propdelay	(double, double, int);
static	int	finddelay	(double, double, double, double, double, double *);
static	void	satdoit		(double, double, double, double, double, double, char *);
static	void	satfinddelay	(double, double, double, double, double *);
static	double	satpropdelay	(double);

/*
 * main - parse arguments and handle options
 */
int
main(
	int argc,
	char *argv[]
	)
{
	int c;
	int errflg = 0;
	double lat1, long1;
	double lat2, long2;
	double lat3, long3;

	init_lib();

	progname = argv[0];
	while ((c = ntp_getopt(argc, argv, "dh:CWG")) != EOF)
	    switch (c) {
		case 'd':
		    ++debug;
		    break;
		case 'h':
		    hflag++;
		    height = atof(ntp_optarg);
		    if (height <= 0.0) {
			    (void) fprintf(stderr, "height %s unlikely\n",
					   ntp_optarg);
			    errflg++;
		    }
		    break;
		case 'C':
		    Cflag++;
		    break;
		case 'W':
		    Wflag++;
		    break;
		case 'G':
		    Gflag++;
		    break;
		default:
		    errflg++;
		    break;
	    }
	if (errflg || (!(Cflag || Wflag || Gflag) && ntp_optind+4 != argc) || 
	    ((Cflag || Wflag || Gflag) && ntp_optind+2 != argc)) {
		(void) fprintf(stderr,
			       "usage: %s [-d] [-h height] lat1 long1 lat2 long2\n",
			       progname);
		(void) fprintf(stderr," - or -\n");
		(void) fprintf(stderr,
			       "usage: %s -CWG [-d] lat long\n",
			       progname);
		exit(2);
	}

		   
	if (!(Cflag || Wflag || Gflag)) {
		lat1 = latlong(argv[ntp_optind], 1);
		long1 = latlong(argv[ntp_optind + 1], 0);
		lat2 = latlong(argv[ntp_optind + 2], 1);
		long2 = latlong(argv[ntp_optind + 3], 0);
		if (hflag) {
			doit(lat1, long1, lat2, long2, height, "");
		} else {
			doit(lat1, long1, lat2, long2, (double)SUMMERHEIGHT,
			     "summer propagation, ");
			doit(lat1, long1, lat2, long2, (double)WINTERHEIGHT,
			     "winter propagation, ");
		}
	} else if (Wflag) {
		/*
		 * Compute delay from WWV
		 */
		lat1 = latlong(argv[ntp_optind], 1);
		long1 = latlong(argv[ntp_optind + 1], 0);
		lat2 = latlong(wwvlat, 1);
		long2 = latlong(wwvlong, 0);
		if (hflag) {
			doit(lat1, long1, lat2, long2, height, "WWV  ");
		} else {
			doit(lat1, long1, lat2, long2, (double)SUMMERHEIGHT,
			     "WWV  summer propagation, ");
			doit(lat1, long1, lat2, long2, (double)WINTERHEIGHT,
			     "WWV  winter propagation, ");
		}

		/*
		 * Compute delay from WWVH
		 */
		lat2 = latlong(wwvhlat, 1);
		long2 = latlong(wwvhlong, 0);
		if (hflag) {
			doit(lat1, long1, lat2, long2, height, "WWVH ");
		} else {
			doit(lat1, long1, lat2, long2, (double)SUMMERHEIGHT,
			     "WWVH summer propagation, ");
			doit(lat1, long1, lat2, long2, (double)WINTERHEIGHT,
			     "WWVH winter propagation, ");
		}
	} else if (Cflag) {
		lat1 = latlong(argv[ntp_optind], 1);
		long1 = latlong(argv[ntp_optind + 1], 0);
		lat2 = latlong(chulat, 1);
		long2 = latlong(chulong, 0);
		if (hflag) {
			doit(lat1, long1, lat2, long2, height, "CHU ");
		} else {
			doit(lat1, long1, lat2, long2, (double)SUMMERHEIGHT,
			     "CHU summer propagation, ");
			doit(lat1, long1, lat2, long2, (double)WINTERHEIGHT,
			     "CHU winter propagation, ");
		}
	} else if (Gflag) {
		lat1 = latlong(goes_up_lat, 1);
		long1 = latlong(goes_up_long, 0);
		lat3 = latlong(argv[ntp_optind], 1);
		long3 = latlong(argv[ntp_optind + 1], 0);

		lat2 = latlong(goes_sat_lat, 1);

		long2 = latlong(goes_west_long, 0);
		satdoit(lat1, long1, lat2, long2, lat3, long3,
			"GOES Delay via WEST");

		long2 = latlong(goes_stby_long, 0);
		satdoit(lat1, long1, lat2, long2, lat3, long3,
			"GOES Delay via STBY");

		long2 = latlong(goes_east_long, 0);
		satdoit(lat1, long1, lat2, long2, lat3, long3,
			"GOES Delay via EAST");

	}
	exit(0);
}


/*
 * doit - compute a delay and print it
 */
static void
doit(
	double lat1,
	double long1,
	double lat2,
	double long2,
	double h,
	char *str
	)
{
	int hops;
	double delay;

	hops = finddelay(lat1, long1, lat2, long2, h, &delay);
	printf("%sheight %g km, hops %d, delay %g seconds\n",
	       str, h, hops, delay);
}


/*
 * latlong - decode a latitude/longitude value
 */
static double
latlong(
	char *str,
	int islat
	)
{
	register char *cp;
	register char *bp;
	double arg;
	double divby;
	int isneg;
	char buf[32];
	char *colon;

	if (islat) {
		/*
		 * Must be north or south
		 */
		if (*str == 'N' || *str == 'n')
		    isneg = 0;
		else if (*str == 'S' || *str == 's')
		    isneg = 1;
		else
		    isneg = -1;
	} else {
		/*
		 * East is positive, west is negative
		 */
		if (*str == 'E' || *str == 'e')
		    isneg = 0;
		else if (*str == 'W' || *str == 'w')
		    isneg = 1;
		else
		    isneg = -1;
	}

	if (isneg >= 0)
	    str++;

	colon = strchr(str, ':');
	if (colon != NULL) {
		/*
		 * in hhh:mm:ss form
		 */
		cp = str;
		bp = buf;
		while (cp < colon)
		    *bp++ = *cp++;
		*bp = '\0';
		cp++;
		arg = atof(buf);
		divby = 60.0;
		colon = strchr(cp, ':');
		if (colon != NULL) {
			bp = buf;
			while (cp < colon)
			    *bp++ = *cp++;
			*bp = '\0';
			cp++;
			arg += atof(buf) / divby;
			divby = 3600.0;
		}
		if (*cp != '\0')
		    arg += atof(cp) / divby;
	} else {
		arg = atof(str);
	}

	if (isneg == 1)
	    arg = -arg;

	if (debug > 2)
	    (void) printf("latitude/longitude %s = %g\n", str, arg);

	return arg;
}


/*
 * greatcircle - compute the great circle distance in kilometers
 */
static double
greatcircle(
	double lat1,
	double long1,
	double lat2,
	double long2
	)
{
	double dg;
	double l1r, l2r;

	l1r = lat1 * RADPERDEG;
	l2r = lat2 * RADPERDEG;
	dg = EARTHRADIUS * acos(
		(cos(l1r) * cos(l2r) * cos((long2-long1)*RADPERDEG))
		+ (sin(l1r) * sin(l2r)));
	if (debug >= 2)
	    printf(
		    "greatcircle lat1 %g long1 %g lat2 %g long2 %g dist %g\n",
		    lat1, long1, lat2, long2, dg);
	return dg;
}


/*
 * waveangle - compute the wave angle for the given distance, virtual
 *	       height and number of hops.
 */
static double
waveangle(
	double dg,
	double h,
	int n
	)
{
	double theta;
	double delta;

	theta = dg / (EARTHRADIUS * (double)(2 * n));
	delta = atan((h / (EARTHRADIUS * sin(theta))) + tan(theta/2)) - theta;
	if (debug >= 2)
	    printf("waveangle dist %g height %g hops %d angle %g\n",
		   dg, h, n, delta / RADPERDEG);
	return delta;
}


/*
 * propdelay - compute the propagation delay
 */
static double
propdelay(
	double dg,
	double h,
	int n
	)
{
	double phi;
	double theta;
	double td;

	theta = dg / (EARTHRADIUS * (double)(2 * n));
	phi = (PI/2.0) - atan((h / (EARTHRADIUS * sin(theta))) + tan(theta/2));
	td = dg / (LIGHTSPEED * sin(phi));
	if (debug >= 2)
	    printf("propdelay dist %g height %g hops %d time %g\n",
		   dg, h, n, td);
	return td;
}


/*
 * finddelay - find the propagation delay
 */
static int
finddelay(
	double lat1,
	double long1,
	double lat2,
	double long2,
	double h,
	double *delay
	)
{
	double dg;	/* great circle distance */
	double delta;	/* wave angle */
	int n;		/* number of hops */

	dg = greatcircle(lat1, long1, lat2, long2);
	if (debug)
	    printf("great circle distance %g km %g miles\n", dg, dg/MILE);
	
	n = 1;
	while ((delta = waveangle(dg, h, n)) < 0.0) {
		if (debug)
		    printf("tried %d hop%s, no good\n", n, n>1?"s":"");
		n++;
	}
	if (debug)
	    printf("%d hop%s okay, wave angle is %g\n", n, n>1?"s":"",
		   delta / RADPERDEG);

	*delay = propdelay(dg, h, n);
	return n;
}

/*
 * satdoit - compute a delay and print it
 */
static void
satdoit(
	double lat1,
	double long1,
	double lat2,
	double long2,
	double lat3,
	double long3,
	char *str
	)
{
	double up_delay,down_delay;

	satfinddelay(lat1, long1, lat2, long2, &up_delay);
	satfinddelay(lat3, long3, lat2, long2, &down_delay);

	printf("%s, delay %g seconds\n", str, up_delay + down_delay);
}

/*
 * satfinddelay - calculate the one-way delay time between a ground station
 * and a satellite
 */
static void
satfinddelay(
	double lat1,
	double long1,
	double lat2,
	double long2,
	double *delay
	)
{
	double dg;	/* great circle distance */

	dg = greatcircle(lat1, long1, lat2, long2);

	*delay = satpropdelay(dg);
}

/*
 * satpropdelay - calculate the one-way delay time between a ground station
 * and a satellite
 */
static double
satpropdelay(
	double dg
	)
{
	double k1, k2, dist;
	double theta;
	double td;

	theta = dg / (EARTHRADIUS);
	k1 = EARTHRADIUS * sin(theta);
	k2 = SATHEIGHT - (EARTHRADIUS * cos(theta));
	if (debug >= 2)
	    printf("Theta %g k1 %g k2 %g\n", theta, k1, k2);
	dist = sqrt(k1*k1 + k2*k2);
	td = dist / LIGHTSPEED;
	if (debug >= 2)
	    printf("propdelay dist %g height %g time %g\n", dg, dist, td);
	return td;
}
