/*
 * Structure for the KSI/Odetics TPRO-S data returned in reponse to a
 * read() call. Note that these are driver-specific and not dependent on
 * 32/64-bit architecture.
 */
struct	tproval {
	u_short	day100;		/* days * 100 */
	u_short	day10;		/* days * 10 */
	u_short	day1;		/* days * 1 */
	u_short	hour10;		/* hours * 10 */
	u_short	hour1;		/* hours * 1 */
	u_short	min10;		/* minutes * 10 */
	u_short	min1;		/* minutes * 1 */
	u_short	sec10;		/* seconds * 10 */
	u_short	sec1;		/* seconds * 1*/
	u_short	ms100;		/* milliseconds * 100 */
	u_short	ms10;		/* milliseconds * 10 */
	u_short	ms1;		/* milliseconds * 1 */
	u_short	usec100;	/* microseconds * 100 */
	u_short	usec10;		/* microseconds * 10 */
	u_short	usec1;		/* microseconds * 1 */
	long tv_sec;		/* seconds */
	long tv_usec;		/* microseconds	*/
	u_short	status;		/* status register */
};

/*
 * Status register bits
 */
#define	TIMEAVAIL 0x0001	/* time available */
#define NOSIGNAL 0x0002		/* insufficient IRIG-B signal */
#define NOSYNC 0x0004		/* local oscillator not synchronized */

/* end of tpro.h */
