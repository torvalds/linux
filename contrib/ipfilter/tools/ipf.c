/*	$FreeBSD$	*/

/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 */
#include "ipf.h"
#include <fcntl.h>
#include <ctype.h>
#include <sys/ioctl.h>
#include "netinet/ipl.h"

#if !defined(lint)
static const char sccsid[] = "@(#)ipf.c	1.23 6/5/96 (C) 1993-2000 Darren Reed";
static const char rcsid[] = "@(#)$Id$";
#endif

#if !defined(__SVR4) && defined(__GNUC__)
extern	char	*index __P((const char *, int));
#endif

extern	char	*optarg;
extern	int	optind;
extern	frentry_t *frtop;


void	ipf_frsync __P((void));
void	zerostats __P((void));
int	main __P((int, char *[]));

int	opts = 0;
int	outputc = 0;
int	use_inet6 = 0;
int	exitstatus = 0;

static	void	procfile __P((char *));
static	void	flushfilter __P((char *, int *));
static	void	set_state __P((u_int));
static	void	showstats __P((friostat_t *));
static	void	packetlogon __P((char *));
static	void	swapactive __P((void));
static	int	opendevice __P((char *, int));
static	void	closedevice __P((void));
static	char	*ipfname = IPL_NAME;
static	void	usage __P((void));
static	int	showversion __P((void));
static	int	get_flags __P((void));
static	int	ipf_interceptadd __P((int, ioctlfunc_t, void *));

static	int	fd = -1;
static	ioctlfunc_t	iocfunctions[IPL_LOGSIZE] = { ioctl, ioctl, ioctl,
						      ioctl, ioctl, ioctl,
						      ioctl, ioctl };

/* XXX	The following was added to satisfy a rescue/rescue/ build
   XXX	requirement.  */
int	nohdrfields;

static void usage()
{
	fprintf(stderr, "usage: ipf [-6AdDEInoPrRsvVyzZ] %s %s %s\n",
		"[-l block|pass|nomatch|state|nat]", "[-cc] [-F i|o|a|s|S|u]",
		"[-f filename] [-T <tuneopts>]");
	exit(1);
}


int main(argc,argv)
	int argc;
	char *argv[];
{
	int c, *filter = NULL;

	if (argc < 2)
		usage();

	assigndefined(getenv("IPF_PREDEFINED"));

	while ((c = getopt(argc, argv, "46Ac:dDEf:F:Il:m:noPrRsT:vVyzZ")) != -1) {
		switch (c)
		{
		case '?' :
			usage();
			break;
		case '4' :
			use_inet6 = -1;
			break;
		case '6' :
			use_inet6 = 1;
			break;
		case 'A' :
			opts &= ~OPT_INACTIVE;
			break;
		case 'c' :
			if (strcmp(optarg, "c") == 0)
				outputc = 1;
			break;
		case 'E' :
			set_state((u_int)1);
			break;
		case 'D' :
			set_state((u_int)0);
			break;
		case 'd' :
			opts ^= OPT_DEBUG;
			break;
		case 'f' :
			procfile(optarg);
			break;
		case 'F' :
			flushfilter(optarg, filter);
			break;
		case 'I' :
			opts ^= OPT_INACTIVE;
			break;
		case 'l' :
			packetlogon(optarg);
			break;
		case 'm' :
			filter = parseipfexpr(optarg, NULL);
			break;
		case 'n' :
			opts ^= OPT_DONOTHING|OPT_DONTOPEN;
			break;
		case 'o' :
			break;
		case 'P' :
			ipfname = IPAUTH_NAME;
			break;
		case 'R' :
			opts ^= OPT_NORESOLVE;
			break;
		case 'r' :
			opts ^= OPT_REMOVE;
			break;
		case 's' :
			swapactive();
			break;
		case 'T' :
			if (opendevice(ipfname, 1) >= 0)
				ipf_dotuning(fd, optarg, ioctl);
			break;
		case 'v' :
			opts += OPT_VERBOSE;
			break;
		case 'V' :
			if (showversion())
				exit(1);
			break;
		case 'y' :
			ipf_frsync();
			break;
		case 'z' :
			opts ^= OPT_ZERORULEST;
			break;
		case 'Z' :
			zerostats();
			break;
		}
	}

	if (optind < 2)
		usage();

	if (fd != -1)
		(void) close(fd);

	return(exitstatus);
	/* NOTREACHED */
}


static int opendevice(ipfdev, check)
	char *ipfdev;
	int check;
{
	if (opts & OPT_DONOTHING)
		return -2;

	if (check && checkrev(ipfname) == -1) {
		fprintf(stderr, "User/kernel version check failed\n");
		return -2;
	}

	if (!ipfdev)
		ipfdev = ipfname;

	if (fd == -1)
		if ((fd = open(ipfdev, O_RDWR)) == -1)
			if ((fd = open(ipfdev, O_RDONLY)) == -1)
				ipferror(fd, "open device");
	return fd;
}


static void closedevice()
{
	close(fd);
	fd = -1;
}


static	int	get_flags()
{
	int i = 0;

	if ((opendevice(ipfname, 1) != -2) &&
	    (ioctl(fd, SIOCGETFF, &i) == -1)) {
		ipferror(fd, "SIOCGETFF");
		return 0;
	}
	return i;
}


static	void	set_state(enable)
	u_int	enable;
{
	if (opendevice(ipfname, 0) != -2) {
		if (ioctl(fd, SIOCFRENB, &enable) == -1) {
			if (errno == EBUSY) {
				fprintf(stderr,
					"IP FIlter: already initialized\n");
			} else {
				ipferror(fd, "SIOCFRENB");
			}
		}
	}
	return;
}


static	void	procfile(file)
	char	*file;
{
	(void) opendevice(ipfname, 1);

	initparse();

	ipf_parsefile(fd, ipf_interceptadd, iocfunctions, file);

	if (outputc) {
		printC(0);
		printC(1);
		emit(-1, -1, NULL, NULL);
	}
}


static int ipf_interceptadd(fd, ioctlfunc, ptr)
	int fd;
	ioctlfunc_t ioctlfunc;
	void *ptr;
{
	if (outputc)
		printc(ptr);

	if (ipf_addrule(fd, ioctlfunc, ptr) != 0)
		exitstatus = 1;
	return 0;
}


static void packetlogon(opt)
	char	*opt;
{
	int	flag, xfd, logopt, change = 0;

	flag = get_flags();
	if (flag != 0) {
		if ((opts & (OPT_DONOTHING|OPT_VERBOSE)) == OPT_VERBOSE)
			printf("log flag is currently %#x\n", flag);
	}

	flag &= ~(FF_LOGPASS|FF_LOGNOMATCH|FF_LOGBLOCK);

	if (strstr(opt, "pass")) {
		flag |= FF_LOGPASS;
		if (opts & OPT_VERBOSE)
			printf("set log flag: pass\n");
		change = 1;
	}
	if (strstr(opt, "nomatch")) {
		flag |= FF_LOGNOMATCH;
		if (opts & OPT_VERBOSE)
			printf("set log flag: nomatch\n");
		change = 1;
	}
	if (strstr(opt, "block") || strchr(opt, 'd')) {
		flag |= FF_LOGBLOCK;
		if (opts & OPT_VERBOSE)
			printf("set log flag: block\n");
		change = 1;
	}
	if (strstr(opt, "none")) {
		if (opts & OPT_VERBOSE)
			printf("disable all log flags\n");
		change = 1;
	}

	if (change == 1) {
		if (opendevice(ipfname, 1) != -2 &&
		    (ioctl(fd, SIOCSETFF, &flag) != 0))
			ipferror(fd, "ioctl(SIOCSETFF)");
	}

	if ((opts & (OPT_DONOTHING|OPT_VERBOSE)) == OPT_VERBOSE) {
		flag = get_flags();
		printf("log flags are now %#x\n", flag);
	}

	if (strstr(opt, "state")) {
		if (opts & OPT_VERBOSE)
			printf("set state log flag\n");
		xfd = open(IPSTATE_NAME, O_RDWR);
		if (xfd >= 0) {
			logopt = 0;
			if (ioctl(xfd, SIOCGETLG, &logopt))
				ipferror(fd, "ioctl(SIOCGETLG)");
			else {
				logopt = 1 - logopt;
				if (ioctl(xfd, SIOCSETLG, &logopt))
					ipferror(xfd, "ioctl(SIOCSETLG)");
			}
			close(xfd);
		}
	}

	if (strstr(opt, "nat")) {
		if (opts & OPT_VERBOSE)
			printf("set nat log flag\n");
		xfd = open(IPNAT_NAME, O_RDWR);
		if (xfd >= 0) {
			logopt = 0;
			if (ioctl(xfd, SIOCGETLG, &logopt))
				ipferror(xfd, "ioctl(SIOCGETLG)");
			else {
				logopt = 1 - logopt;
				if (ioctl(xfd, SIOCSETLG, &logopt))
					ipferror(xfd, "ioctl(SIOCSETLG)");
			}
			close(xfd);
		}
	}
}


static void flushfilter(arg, filter)
	char *arg;
	int *filter;
{
	int	fl = 0, rem;

	if (!arg || !*arg)
		return;
	if (!strcmp(arg, "s") || !strcmp(arg, "S") || ISDIGIT(*arg)) {
		if (*arg == 'S')
			fl = 0;
		else if (*arg == 's')
			fl = 1;
		else
			fl = atoi(arg);
		rem = fl;

		closedevice();
		if (opendevice(IPSTATE_NAME, 1) == -2)
			exit(1);

		if (!(opts & OPT_DONOTHING)) {
			if (use_inet6) {
				fprintf(stderr,
					"IPv6 rules are no longer seperate\n");
			} else if (filter != NULL) {
				ipfobj_t obj;

				obj.ipfo_rev = IPFILTER_VERSION;
				obj.ipfo_size = filter[0] * sizeof(int);
				obj.ipfo_type = IPFOBJ_IPFEXPR;
				obj.ipfo_ptr = filter;
				if (ioctl(fd, SIOCMATCHFLUSH, &obj) == -1) {
					ipferror(fd, "ioctl(SIOCMATCHFLUSH)");
					fl = -1;
				} else {
					fl = obj.ipfo_retval;
				}
			} else {
				if (ioctl(fd, SIOCIPFFL, &fl) == -1) {
					ipferror(fd, "ioctl(SIOCIPFFL)");
					exit(1);
				}
			}
		}
		if ((opts & (OPT_DONOTHING|OPT_DEBUG)) == OPT_DEBUG) {
			printf("remove flags %s (%d)\n", arg, rem);
		}
		if ((opts & (OPT_DONOTHING|OPT_VERBOSE)) == OPT_VERBOSE) {
			printf("%d state entries removed\n", fl);
		}
		closedevice();
		return;
	} else if (strchr(arg, 'i') || strchr(arg, 'I'))
		fl = FR_INQUE;
	else if (strchr(arg, 'o') || strchr(arg, 'O'))
		fl = FR_OUTQUE;
	else if (strchr(arg, 'a') || strchr(arg, 'A'))
		fl = FR_OUTQUE|FR_INQUE;
	else {
		fprintf(stderr, "Incorrect flush argument: %s\n", arg);
		usage();
	}
	if (opts & OPT_INACTIVE)
		fl |= FR_INACTIVE;
	rem = fl;

	if (opendevice(ipfname, 1) == -2)
		exit(1);

	if (!(opts & OPT_DONOTHING)) {
		if (use_inet6) {
			if (ioctl(fd, SIOCIPFL6, &fl) == -1) {
				ipferror(fd, "ioctl(SIOCIPFL6)");
				exit(1);
			}
		} else {
			if (ioctl(fd, SIOCIPFFL, &fl) == -1) {
				ipferror(fd, "ioctl(SIOCIPFFL)");
				exit(1);
			}
		}
	}

	if ((opts & (OPT_DONOTHING|OPT_DEBUG)) == OPT_DEBUG) {
		printf("remove flags %s%s (%d)\n", (rem & FR_INQUE) ? "I" : "",
			(rem & FR_OUTQUE) ? "O" : "", rem);
	}
	if ((opts & (OPT_DONOTHING|OPT_VERBOSE)) == OPT_VERBOSE) {
		printf("%d filter rules removed\n", fl);
	}
	return;
}


static void swapactive()
{
	int in = 2;

	if (opendevice(ipfname, 1) != -2 && ioctl(fd, SIOCSWAPA, &in) == -1)
		ipferror(fd, "ioctl(SIOCSWAPA)");
	else
		printf("Set %d now inactive\n", in);
}


void ipf_frsync()
{
	int frsyn = 0;

	if (opendevice(ipfname, 1) != -2 && ioctl(fd, SIOCFRSYN, &frsyn) == -1)
		ipferror(fd, "SIOCFRSYN");
	else
		printf("filter sync'd\n");
}


void zerostats()
{
	ipfobj_t	obj;
	friostat_t	fio;

	obj.ipfo_rev = IPFILTER_VERSION;
	obj.ipfo_type = IPFOBJ_IPFSTAT;
	obj.ipfo_size = sizeof(fio);
	obj.ipfo_ptr = &fio;
	obj.ipfo_offset = 0;

	if (opendevice(ipfname, 1) != -2) {
		if (ioctl(fd, SIOCFRZST, &obj) == -1) {
			ipferror(fd, "ioctl(SIOCFRZST)");
			exit(-1);
		}
		showstats(&fio);
	}

}


/*
 * read the kernel stats for packets blocked and passed
 */
static void showstats(fp)
	friostat_t	*fp;
{
	printf("bad packets:\t\tin %lu\tout %lu\n",
			fp->f_st[0].fr_bad, fp->f_st[1].fr_bad);
	printf(" input packets:\t\tblocked %lu passed %lu nomatch %lu",
			fp->f_st[0].fr_block, fp->f_st[0].fr_pass,
			fp->f_st[0].fr_nom);
	printf(" counted %lu\n", fp->f_st[0].fr_acct);
	printf("output packets:\t\tblocked %lu passed %lu nomatch %lu",
			fp->f_st[1].fr_block, fp->f_st[1].fr_pass,
			fp->f_st[1].fr_nom);
	printf(" counted %lu\n", fp->f_st[0].fr_acct);
	printf(" input packets logged:\tblocked %lu passed %lu\n",
			fp->f_st[0].fr_bpkl, fp->f_st[0].fr_ppkl);
	printf("output packets logged:\tblocked %lu passed %lu\n",
			fp->f_st[1].fr_bpkl, fp->f_st[1].fr_ppkl);
}


static int showversion()
{
	struct friostat fio;
	ipfobj_t ipfo;
	u_32_t flags;
	char *s;
	int vfd;

	bzero((caddr_t)&ipfo, sizeof(ipfo));
	ipfo.ipfo_rev = IPFILTER_VERSION;
	ipfo.ipfo_size = sizeof(fio);
	ipfo.ipfo_ptr = (void *)&fio;
	ipfo.ipfo_type = IPFOBJ_IPFSTAT;

	printf("ipf: %s (%d)\n", IPL_VERSION, (int)sizeof(frentry_t));

	if ((vfd = open(ipfname, O_RDONLY)) == -1) {
		perror("open device");
		return 1;
	}

	if (ioctl(vfd, SIOCGETFS, &ipfo)) {
		ipferror(vfd, "ioctl(SIOCGETFS)");
		close(vfd);
		return 1;
	}
	close(vfd);
	flags = get_flags();

	printf("Kernel: %-*.*s\n", (int)sizeof(fio.f_version),
		(int)sizeof(fio.f_version), fio.f_version);
	printf("Running: %s\n", (fio.f_running > 0) ? "yes" : "no");
	printf("Log Flags: %#x = ", flags);
	s = "";
	if (flags & FF_LOGPASS) {
		printf("pass");
		s = ", ";
	}
	if (flags & FF_LOGBLOCK) {
		printf("%sblock", s);
		s = ", ";
	}
	if (flags & FF_LOGNOMATCH) {
		printf("%snomatch", s);
		s = ", ";
	}
	if (flags & FF_BLOCKNONIP) {
		printf("%snonip", s);
		s = ", ";
	}
	if (!*s)
		printf("none set");
	putchar('\n');

	printf("Default: ");
	if (FR_ISPASS(fio.f_defpass))
		s = "pass";
	else if (FR_ISBLOCK(fio.f_defpass))
		s = "block";
	else
		s = "nomatch -> block";
	printf("%s all, Logging: %savailable\n", s, fio.f_logging ? "" : "un");
	printf("Active list: %d\n", fio.f_active);
	printf("Feature mask: %#x\n", fio.f_features);

	return 0;
}
