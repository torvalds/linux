/*
** DO NOT MODIFY THESE VALUES (would make old cowfiles unusable)
*/
#define	MAPUNIT		1024		/* blocksize for bit in bitmap       */
#define	MUSHIFT		10		/* bitshift  for bit in bitmap       */
#define	MUMASK		0x3ff		/* bitmask   for bit in bitmap       */

#define	COWMAGIC	0x574f437f	/* byte-swapped '7f C O W'           */
#define	COWDIRTY	0x01
#define	COWPACKED	0x02
#define	COWVERSION	1

struct cowhead
{
	int		magic;		/* identifies a cowfile              */
	short		version;	/* version of cowhead                */
	short		flags;    	/* flags indicating status           */
	unsigned long	mapunit;	/* blocksize per bit in bitmap       */
	unsigned long	mapsize;	/* total size of bitmap (bytes)      */
	unsigned long	doffset;	/* start-offset datablocks in cow    */
	unsigned long	rdoblocks;	/* size of related read-only file    */
	unsigned long	rdofingerprint;	/* fingerprint of read-only file     */
	unsigned long	cowused;	/* number of datablocks used in cow  */
};

#define COWDEVDIR	"/dev/cow/"
#define COWDEVICE	COWDEVDIR "%ld"
#define COWCONTROL	COWDEVDIR "ctl"

#define MAXCOWS		1024
#define COWCTL		(MAXCOWS-1)	/* minor number of /dev/cow/ctl     */

#define COWPROCDIR	"/proc/cow/"
#define COWPROCFILE	COWPROCDIR "%d"

/*
** ioctl related stuff
*/
#define ANYDEV		((unsigned long)-1)

struct cowpair
{
	unsigned char	*rdofile;	/* pathname of the rdofile           */
	unsigned char	*cowfile;	/* pathname of the cowfile           */
	unsigned short	rdoflen;	/* length of rdofile pathname        */
	unsigned short	cowflen;	/* length of cowfile pathname        */
	unsigned long	device;		/* requested/returned device number  */
};

struct cowwatch
{
	int      	flags;		/* request flags                     */
	unsigned long	device;		/* requested device number           */
	unsigned long	threshold;	/* continue if free Kb < threshold   */
	unsigned long	totalkb;	/* ret: total filesystem size (Kb)   */
	unsigned long	availkb;	/* ret: free  filesystem size (Kb)   */
};

#define	WATCHWAIT	0x01		/* block until threshold reached     */

#define	COWSYNC		_IO  ('C', 1)
#define	COWMKPAIR	_IOW ('C', 2, struct cowpair)
#define	COWRMPAIR	_IOW ('C', 3, unsigned long)
#define	COWWATCH	_IOW ('C', 4, struct cowwatch)
#define	COWCLOSE	_IOW ('C', 5, unsigned long)
#define	COWRDOPEN	_IOW ('C', 6, unsigned long)
