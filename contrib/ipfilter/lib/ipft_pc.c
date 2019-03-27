/*	$FreeBSD$	*/

/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * $Id$
 */
#include "ipf.h"
#include "ipt.h"

#if !defined(lint)
static const char rcsid[] = "@(#)$Id$";
#endif

struct	llc	{
	int	lc_type;
	int	lc_sz;	/* LLC header length */
	int	lc_to;	/* LLC Type offset */
	int	lc_tl;	/* LLC Type length */
};

/*
 * While many of these maybe the same, some do have different header formats
 * which make this useful.
 */

static	struct	llc	llcs[] = {
	{ 0, 0, 0, 0 },				/* DLT_NULL */
	{ 1, 14, 12, 2 },			/* DLT_Ethernet */
	{ 10, 0, 0, 0 },			/* DLT_FDDI */
	{ 12, 0, 0, 0 },			/* DLT_RAW */
	{ -1, -1, -1, -1 }
};

typedef struct {
	u_int	id;
	u_short	major;
	u_short	minor;
	u_int	timezone;
	u_int	sigfigs;
	u_int	snaplen;
	u_int	type;
} fileheader_t;

typedef struct {
	u_32_t	seconds;
	u_32_t	microseconds;
	u_32_t	caplen;
	u_32_t	wirelen;
} packetheader_t;

static	int	ipcap_open __P((char *));
static	int	ipcap_close __P((void));
static	int	ipcap_readip __P((mb_t *, char **, int *));
static	int	ipcap_read_rec __P((packetheader_t *));
static	void	iswap_hdr __P((fileheader_t *));

static	int	pfd = -1, swapped = 0;
static	struct llc	*llcp = NULL;

struct	ipread	pcap = { ipcap_open, ipcap_close, ipcap_readip, 0 };

#define	SWAPLONG(y)	\
	((((y)&0xff)<<24) | (((y)&0xff00)<<8) | (((y)&0xff0000)>>8) | (((y)>>24)&0xff))
#define	SWAPSHORT(y)	\
	( (((y)&0xff)<<8) | (((y)&0xff00)>>8) )

static	void	iswap_hdr(p)
	fileheader_t	*p;
{
	p->major = SWAPSHORT(p->major);
	p->minor = SWAPSHORT(p->minor);
	p->timezone = SWAPLONG(p->timezone);
	p->sigfigs = SWAPLONG(p->sigfigs);
	p->snaplen = SWAPLONG(p->snaplen);
	p->type = SWAPLONG(p->type);
}

static	int	ipcap_open(fname)
	char	*fname;
{
	fileheader_t ph;
	int fd, i;

	if (pfd != -1)
		return pfd;

	if (!strcmp(fname, "-"))
		fd = 0;
	else if ((fd = open(fname, O_RDONLY)) == -1)
		return -1;

	if (read(fd, (char *)&ph, sizeof(ph)) != sizeof(ph))
		return -2;

	if (ph.id != 0xa1b2c3d4) {
		if (SWAPLONG(ph.id) != 0xa1b2c3d4) {
			(void) close(fd);
			return -2;
		}
		swapped = 1;
		iswap_hdr(&ph);
	}

	for (i = 0; llcs[i].lc_type != -1; i++)
		if (llcs[i].lc_type == ph.type) {
			llcp = llcs + i;
			break;
		}

	if (llcp == NULL) {
		(void) close(fd);
		return -2;
	}

	pfd = fd;
	printf("opened pcap file %s:\n", fname);
	printf("\tid: %08x version: %d.%d type: %d snap %d\n",
		ph.id, ph.major, ph.minor, ph.type, ph.snaplen);

	return fd;
}


static	int	ipcap_close()
{
	return close(pfd);
}


/*
 * read in the header (and validate) which should be the first record
 * in a pcap file.
 */
static	int	ipcap_read_rec(rec)
	packetheader_t *rec;
{
	int	n, p, i;
	char	*s;

	s = (char *)rec;
	n = sizeof(*rec);

	while (n > 0) {
		i = read(pfd, (char *)rec, sizeof(*rec));
		if (i <= 0)
			return -2;
		s += i;
		n -= i;
	}

	if (swapped) {
		rec->caplen = SWAPLONG(rec->caplen);
		rec->wirelen = SWAPLONG(rec->wirelen);
		rec->seconds = SWAPLONG(rec->seconds);
		rec->microseconds = SWAPLONG(rec->microseconds);
	}
	p = rec->caplen;
	n = MIN(p, rec->wirelen);
	if (!n || n < 0)
		return -3;

	if (p < 0 || p > 65536)
		return -4;
	return p;
}


#ifdef	notyet
/*
 * read an entire pcap packet record.  only the data part is copied into
 * the available buffer, with the number of bytes copied returned.
 */
static	int	ipcap_read(buf, cnt)
	char	*buf;
	int	cnt;
{
	packetheader_t rec;
	static	char	*bufp = NULL;
	int	i, n;

	if ((i = ipcap_read_rec(&rec)) <= 0)
		return i;

	if (!bufp)
		bufp = malloc(i);
	else
		bufp = realloc(bufp, i);

	if (read(pfd, bufp, i) != i)
		return -2;

	n = MIN(i, cnt);
	bcopy(bufp, buf, n);
	return n;
}
#endif


/*
 * return only an IP packet read into buf
 */
static	int	ipcap_readip(mb, ifn, dir)
	mb_t	*mb;
	char	**ifn;
	int	*dir;
{
	static	char	*bufp = NULL;
	packetheader_t	rec;
	struct	llc	*l;
	char	*s, ty[4];
	int	i, j, n;
	char	*buf;
	int	cnt;

#if 0
	ifn = ifn;	/* gcc -Wextra */
	dir = dir;	/* gcc -Wextra */
#endif
	buf = (char *)mb->mb_buf;
	cnt = sizeof(mb->mb_buf);
	l = llcp;

	/* do { */
		if ((i = ipcap_read_rec(&rec)) <= 0)
			return i;

		if (!bufp)
			bufp = malloc(i);
		else
			bufp = realloc(bufp, i);
		s = bufp;

		for (j = i, n = 0; j > 0; ) {
			n = read(pfd, s, j);
			if (n <= 0)
				return -2;
			j -= n;
			s += n;
		}
		s = bufp;

		i -= l->lc_sz;
		s += l->lc_to;
		bcopy(s, ty, l->lc_tl);
		s += l->lc_tl;
	/* } while (ty[0] != 0x8 && ty[1] != 0); */
	n = MIN(i, cnt);
	bcopy(s, buf, n);
	mb->mb_len = n;
	return n;
}
