/*	$OpenBSD: pflogd.c,v 1.46 2008/10/22 08:16:49 henning Exp $	*/

/*
 * Copyright (c) 2001 Theo de Raadt
 * Copyright (c) 2001 Can Erkin Acar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *    - Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    - Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pcap-int.h>
#include <pcap.h>
#include <syslog.h>
#include <signal.h>
#include <err.h>
#include <errno.h>
#include <stdarg.h>
#include <fcntl.h>
#ifdef __FreeBSD__
#include <ifaddrs.h>
#include "pidfile.h"
#else
#include <util.h>
#endif
#include "pflogd.h"

pcap_t *hpcap;
static FILE *dpcap;

int Debug = 0;
static int snaplen = DEF_SNAPLEN;
static int cur_snaplen = DEF_SNAPLEN;

volatile sig_atomic_t gotsig_close, gotsig_alrm, gotsig_hup, gotsig_usr1;

char *filename = PFLOGD_LOG_FILE;
char *interface = PFLOGD_DEFAULT_IF;
char *filter = NULL;

char errbuf[PCAP_ERRBUF_SIZE];

int log_debug = 0;
unsigned int delay = FLUSH_DELAY;

char *copy_argv(char * const *);
void  dump_packet(u_char *, const struct pcap_pkthdr *, const u_char *);
void  dump_packet_nobuf(u_char *, const struct pcap_pkthdr *, const u_char *);
void  log_pcap_stats(void);
int   flush_buffer(FILE *);
int   if_exists(char *);
int   init_pcap(void);
void  logmsg(int, const char *, ...);
void  purge_buffer(void);
int   reset_dump(int);
int   scan_dump(FILE *, off_t);
int   set_snaplen(int);
void  set_suspended(int);
void  sig_alrm(int);
void  sig_usr1(int);
void  sig_close(int);
void  sig_hup(int);
void  usage(void);

static int try_reset_dump(int);

/* buffer must always be greater than snaplen */
static int    bufpkt = 0;	/* number of packets in buffer */
static int    buflen = 0;	/* allocated size of buffer */
static char  *buffer = NULL;	/* packet buffer */
static char  *bufpos = NULL;	/* position in buffer */
static int    bufleft = 0;	/* bytes left in buffer */

/* if error, stop logging but count dropped packets */
static int suspended = -1;
static long packets_dropped = 0;

void
set_suspended(int s)
{
	if (suspended == s)
		return;

	suspended = s;
	setproctitle("[%s] -s %d -i %s -f %s",
	    suspended ? "suspended" : "running",
	    cur_snaplen, interface, filename);
}

char *
copy_argv(char * const *argv)
{
	size_t len = 0, n;
	char *buf;

	if (argv == NULL)
		return (NULL);

	for (n = 0; argv[n]; n++)
		len += strlen(argv[n])+1;
	if (len == 0)
		return (NULL);

	buf = malloc(len);
	if (buf == NULL)
		return (NULL);

	strlcpy(buf, argv[0], len);
	for (n = 1; argv[n]; n++) {
		strlcat(buf, " ", len);
		strlcat(buf, argv[n], len);
	}
	return (buf);
}

void
logmsg(int pri, const char *message, ...)
{
	va_list ap;
	va_start(ap, message);

	if (log_debug) {
		vfprintf(stderr, message, ap);
		fprintf(stderr, "\n");
	} else
		vsyslog(pri, message, ap);
	va_end(ap);
}

#ifdef __FreeBSD__
__dead2 void
#else
__dead void
#endif
usage(void)
{
	fprintf(stderr, "usage: pflogd [-Dx] [-d delay] [-f filename]");
	fprintf(stderr, " [-i interface] [-p pidfile]\n");
	fprintf(stderr, "              [-s snaplen] [expression]\n");
	exit(1);
}

void
sig_close(int sig)
{
	gotsig_close = 1;
}

void
sig_hup(int sig)
{
	gotsig_hup = 1;
}

void
sig_alrm(int sig)
{
	gotsig_alrm = 1;
}

void
sig_usr1(int sig)
{
	gotsig_usr1 = 1;
}

void
set_pcap_filter(void)
{
	struct bpf_program bprog;

	if (pcap_compile(hpcap, &bprog, filter, PCAP_OPT_FIL, 0) < 0)
		logmsg(LOG_WARNING, "%s", pcap_geterr(hpcap));
	else {
		if (pcap_setfilter(hpcap, &bprog) < 0)
			logmsg(LOG_WARNING, "%s", pcap_geterr(hpcap));
		pcap_freecode(&bprog);
	}
}

int
if_exists(char *ifname)
{
#ifdef __FreeBSD__
	struct ifaddrs *ifdata, *mb;
	int exists = 0;

	getifaddrs(&ifdata);
        if (ifdata == NULL)
		return (0);

	for (mb = ifdata; mb != NULL; mb = mb->ifa_next) {
		if (mb == NULL)
			continue;
		if (strlen(ifname) != strlen(mb->ifa_name))
			continue;
		if (strncmp(ifname, mb->ifa_name, strlen(ifname)) != 0)
			continue;
		exists = 1;
		break;
	}
	freeifaddrs(ifdata);

	return (exists);
#else
	int s;
	struct ifreq ifr;
	struct if_data ifrdat;

	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
		err(1, "socket");
	bzero(&ifr, sizeof(ifr));
	if (strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name)) >=
		sizeof(ifr.ifr_name))
			errx(1, "main ifr_name: strlcpy");
	ifr.ifr_data = (caddr_t)&ifrdat;
	if (ioctl(s, SIOCGIFDATA, (caddr_t)&ifr) == -1)
		return (0);
	if (close(s))
		err(1, "close");

	return (1);
#endif
}

int
init_pcap(void)
{
	hpcap = pcap_open_live(interface, snaplen, 1, PCAP_TO_MS, errbuf);
	if (hpcap == NULL) {
		logmsg(LOG_ERR, "Failed to initialize: %s", errbuf);
		return (-1);
	}

	if (pcap_datalink(hpcap) != DLT_PFLOG) {
		logmsg(LOG_ERR, "Invalid datalink type");
		pcap_close(hpcap);
		hpcap = NULL;
		return (-1);
	}

	set_pcap_filter();

	cur_snaplen = snaplen = pcap_snapshot(hpcap);

	/* lock */
	if (ioctl(pcap_fileno(hpcap), BIOCLOCK) < 0) {
		logmsg(LOG_ERR, "BIOCLOCK: %s", strerror(errno));
		return (-1);
	}

	return (0);
}

int
set_snaplen(int snap)
{
	if (priv_set_snaplen(snap))
		return (1);

	if (cur_snaplen > snap)
		purge_buffer();

	cur_snaplen = snap;

	return (0);
}

int
reset_dump(int nomove)
{
	int ret;

	for (;;) {
		ret = try_reset_dump(nomove);
		if (ret <= 0)
			break;
	}

	return (ret);
}

/*
 * tries to (re)open log file, nomove flag is used with -x switch
 * returns 0: success, 1: retry (log moved), -1: error
 */
int
try_reset_dump(int nomove)
{
	struct pcap_file_header hdr;
	struct stat st;
	int fd;
	FILE *fp;

	if (hpcap == NULL)
		return (-1);

	if (dpcap) {
		flush_buffer(dpcap);
		fclose(dpcap);
		dpcap = NULL;
	}

	/*
	 * Basically reimplement pcap_dump_open() because it truncates
	 * files and duplicates headers and such.
	 */
	fd = priv_open_log();
	if (fd < 0)
		return (-1);

	fp = fdopen(fd, "a+");

	if (fp == NULL) {
		logmsg(LOG_ERR, "Error: %s: %s", filename, strerror(errno));
		close(fd);
		return (-1);
	}
	if (fstat(fileno(fp), &st) == -1) {
		logmsg(LOG_ERR, "Error: %s: %s", filename, strerror(errno));
		fclose(fp);
		return (-1);
	}

	/* set FILE unbuffered, we do our own buffering */
	if (setvbuf(fp, NULL, _IONBF, 0)) {
		logmsg(LOG_ERR, "Failed to set output buffers");
		fclose(fp);
		return (-1);
	}

#define TCPDUMP_MAGIC 0xa1b2c3d4

	if (st.st_size == 0) {
		if (snaplen != cur_snaplen) {
			logmsg(LOG_NOTICE, "Using snaplen %d", snaplen);
			if (set_snaplen(snaplen))
				logmsg(LOG_WARNING,
				    "Failed, using old settings");
		}
		hdr.magic = TCPDUMP_MAGIC;
		hdr.version_major = PCAP_VERSION_MAJOR;
		hdr.version_minor = PCAP_VERSION_MINOR;
		hdr.thiszone = hpcap->tzoff;
		hdr.snaplen = hpcap->snapshot;
		hdr.sigfigs = 0;
		hdr.linktype = hpcap->linktype;

		if (fwrite((char *)&hdr, sizeof(hdr), 1, fp) != 1) {
			fclose(fp);
			return (-1);
		}
	} else if (scan_dump(fp, st.st_size)) {
		fclose(fp);
		if (nomove || priv_move_log()) {
			logmsg(LOG_ERR,
			    "Invalid/incompatible log file, move it away");
			return (-1);
		}
		return (1);
	}

	dpcap = fp;

	set_suspended(0);
	flush_buffer(fp);

	return (0);
}

int
scan_dump(FILE *fp, off_t size)
{
	struct pcap_file_header hdr;
#ifdef __FreeBSD__
	struct pcap_sf_pkthdr ph;
#else
	struct pcap_pkthdr ph;
#endif
	off_t pos;

	/*
	 * Must read the file, compare the header against our new
	 * options (in particular, snaplen) and adjust our options so
	 * that we generate a correct file. Furthermore, check the file
	 * for consistency so that we can append safely.
	 *
	 * XXX this may take a long time for large logs.
	 */
	(void) fseek(fp, 0L, SEEK_SET);

	if (fread((char *)&hdr, sizeof(hdr), 1, fp) != 1) {
		logmsg(LOG_ERR, "Short file header");
		return (1);
	}

	if (hdr.magic != TCPDUMP_MAGIC ||
	    hdr.version_major != PCAP_VERSION_MAJOR ||
	    hdr.version_minor != PCAP_VERSION_MINOR ||
	    hdr.linktype != hpcap->linktype ||
	    hdr.snaplen > PFLOGD_MAXSNAPLEN) {
		return (1);
	}

	pos = sizeof(hdr);

	while (!feof(fp)) {
		off_t len = fread((char *)&ph, 1, sizeof(ph), fp);
		if (len == 0)
			break;

		if (len != sizeof(ph))
			goto error;
		if (ph.caplen > hdr.snaplen || ph.caplen > PFLOGD_MAXSNAPLEN)
			goto error;
		pos += sizeof(ph) + ph.caplen;
		if (pos > size)
			goto error;
		fseek(fp, ph.caplen, SEEK_CUR);
	}

	if (pos != size)
		goto error;

	if (hdr.snaplen != cur_snaplen) {
		logmsg(LOG_WARNING,
		       "Existing file has different snaplen %u, using it",
		       hdr.snaplen);
		if (set_snaplen(hdr.snaplen)) {
			logmsg(LOG_WARNING,
			       "Failed, using old settings, offset %llu",
			       (unsigned long long) size);
		}
	}

	return (0);

 error:
	logmsg(LOG_ERR, "Corrupted log file.");
	return (1);
}

/* dump a packet directly to the stream, which is unbuffered */
void
dump_packet_nobuf(u_char *user, const struct pcap_pkthdr *h, const u_char *sp)
{
	FILE *f = (FILE *)user;
#ifdef __FreeBSD__
	struct pcap_sf_pkthdr sh;
#endif

	if (suspended) {
		packets_dropped++;
		return;
	}

#ifdef __FreeBSD__
	sh.ts.tv_sec = (bpf_int32)h->ts.tv_sec;
	sh.ts.tv_usec = (bpf_int32)h->ts.tv_usec;
	sh.caplen = h->caplen;
	sh.len = h->len;

	if (fwrite((char *)&sh, sizeof(sh), 1, f) != 1) {
#else
	if (fwrite((char *)h, sizeof(*h), 1, f) != 1) {
#endif
		off_t pos = ftello(f);

		/* try to undo header to prevent corruption */
#ifdef __FreeBSD__
		if (pos < sizeof(sh) ||
		    ftruncate(fileno(f), pos - sizeof(sh))) {
#else
		if (pos < sizeof(*h) ||
		    ftruncate(fileno(f), pos - sizeof(*h))) {
#endif
			logmsg(LOG_ERR, "Write failed, corrupted logfile!");
			set_suspended(1);
			gotsig_close = 1;
			return;
		}
		goto error;
	}

	if (fwrite((char *)sp, h->caplen, 1, f) != 1)
		goto error;

	return;

error:
	set_suspended(1);
	packets_dropped ++;
	logmsg(LOG_ERR, "Logging suspended: fwrite: %s", strerror(errno));
}

int
flush_buffer(FILE *f)
{
	off_t offset;
	int len = bufpos - buffer;

	if (len <= 0)
		return (0);

	offset = ftello(f);
	if (offset == (off_t)-1) {
		set_suspended(1);
		logmsg(LOG_ERR, "Logging suspended: ftello: %s",
		    strerror(errno));
		return (1);
	}

	if (fwrite(buffer, len, 1, f) != 1) {
		set_suspended(1);
		logmsg(LOG_ERR, "Logging suspended: fwrite: %s",
		    strerror(errno));
		ftruncate(fileno(f), offset);
		return (1);
	}

	set_suspended(0);
	bufpos = buffer;
	bufleft = buflen;
	bufpkt = 0;

	return (0);
}

void
purge_buffer(void)
{
	packets_dropped += bufpkt;

	set_suspended(0);
	bufpos = buffer;
	bufleft = buflen;
	bufpkt = 0;
}

/* append packet to the buffer, flushing if necessary */
void
dump_packet(u_char *user, const struct pcap_pkthdr *h, const u_char *sp)
{
	FILE *f = (FILE *)user;
#ifdef __FreeBSD__
	struct pcap_sf_pkthdr sh;
	size_t len = sizeof(sh) + h->caplen;
#else
	size_t len = sizeof(*h) + h->caplen;
#endif

	if (len < sizeof(*h) || h->caplen > (size_t)cur_snaplen) {
		logmsg(LOG_NOTICE, "invalid size %u (%u/%u), packet dropped",
		       len, cur_snaplen, snaplen);
		packets_dropped++;
		return;
	}

	if (len <= bufleft)
		goto append;

	if (suspended) {
		packets_dropped++;
		return;
	}

	if (flush_buffer(f)) {
		packets_dropped++;
		return;
	}

	if (len > bufleft) {
		dump_packet_nobuf(user, h, sp);
		return;
	}

 append:       
#ifdef __FreeBSD__
	sh.ts.tv_sec = (bpf_int32)h->ts.tv_sec;
	sh.ts.tv_usec = (bpf_int32)h->ts.tv_usec;
	sh.caplen = h->caplen;
	sh.len = h->len;

	memcpy(bufpos, &sh, sizeof(sh));
	memcpy(bufpos + sizeof(sh), sp, h->caplen);
#else
	memcpy(bufpos, h, sizeof(*h));
	memcpy(bufpos + sizeof(*h), sp, h->caplen);
#endif

	bufpos += len;
	bufleft -= len;
	bufpkt++;

	return;
}

void
log_pcap_stats(void)
{
	struct pcap_stat pstat;
	if (pcap_stats(hpcap, &pstat) < 0)
		logmsg(LOG_WARNING, "Reading stats: %s", pcap_geterr(hpcap));
	else
		logmsg(LOG_NOTICE,
			"%u packets received, %u/%u dropped (kernel/pflogd)",
			pstat.ps_recv, pstat.ps_drop, packets_dropped);
}

int
main(int argc, char **argv)
{
	int ch, np, ret, Xflag = 0;
	pcap_handler phandler = dump_packet;
	const char *errstr = NULL;
	char *pidf = NULL;

	ret = 0;

	closefrom(STDERR_FILENO + 1);

	while ((ch = getopt(argc, argv, "Dxd:f:i:p:s:")) != -1) {
		switch (ch) {
		case 'D':
			Debug = 1;
			break;
		case 'd':
			delay = strtonum(optarg, 5, 60*60, &errstr);
			if (errstr)
				usage();
			break;
		case 'f':
			filename = optarg;
			break;
		case 'i':
			interface = optarg;
			break;
		case 'p':
			pidf = optarg;
			break;
		case 's':
			snaplen = strtonum(optarg, 0, PFLOGD_MAXSNAPLEN,
			    &errstr);
			if (snaplen <= 0)
				snaplen = DEF_SNAPLEN;
			if (errstr)
				snaplen = PFLOGD_MAXSNAPLEN;
			break;
		case 'x':
			Xflag++;
			break;
		default:
			usage();
		}

	}

	log_debug = Debug;
	argc -= optind;
	argv += optind;

	/* does interface exist */
	if (!if_exists(interface)) {
		warn("Failed to initialize: %s", interface);
		logmsg(LOG_ERR, "Failed to initialize: %s", interface);
		logmsg(LOG_ERR, "Exiting, init failure");
		exit(1);
	}

	if (!Debug) {
		openlog("pflogd", LOG_PID | LOG_CONS, LOG_DAEMON);
		if (daemon(0, 0)) {
			logmsg(LOG_WARNING, "Failed to become daemon: %s",
			    strerror(errno));
		}
		pidfile(pidf);
	}

	tzset();
	(void)umask(S_IRWXG | S_IRWXO);

	/* filter will be used by the privileged process */
	if (argc) {
		filter = copy_argv(argv);
		if (filter == NULL)
			logmsg(LOG_NOTICE, "Failed to form filter expression");
	}

	/* initialize pcap before dropping privileges */
	if (init_pcap()) {
		logmsg(LOG_ERR, "Exiting, init failure");
		exit(1);
	}

	/* Privilege separation begins here */
	if (priv_init()) {
		logmsg(LOG_ERR, "unable to privsep");
		exit(1);
	}

	setproctitle("[initializing]");
	/* Process is now unprivileged and inside a chroot */
	signal(SIGTERM, sig_close);
	signal(SIGINT, sig_close);
	signal(SIGQUIT, sig_close);
	signal(SIGALRM, sig_alrm);
	signal(SIGUSR1, sig_usr1);
	signal(SIGHUP, sig_hup);
	alarm(delay);

	buffer = malloc(PFLOGD_BUFSIZE);

	if (buffer == NULL) {
		logmsg(LOG_WARNING, "Failed to allocate output buffer");
		phandler = dump_packet_nobuf;
	} else {
		bufleft = buflen = PFLOGD_BUFSIZE;
		bufpos = buffer;
		bufpkt = 0;
	}

	if (reset_dump(Xflag) < 0) {
		if (Xflag)
			return (1);

		logmsg(LOG_ERR, "Logging suspended: open error");
		set_suspended(1);
	} else if (Xflag)
		return (0);

	while (1) {
		np = pcap_dispatch(hpcap, PCAP_NUM_PKTS,
		    phandler, (u_char *)dpcap);
		if (np < 0) {
			if (!if_exists(interface)) {
				logmsg(LOG_NOTICE, "interface %s went away",
				    interface);
				ret = -1;
				break;
			}
			logmsg(LOG_NOTICE, "%s", pcap_geterr(hpcap));
		}

		if (gotsig_close)
			break;
		if (gotsig_hup) {
			if (reset_dump(0)) {
				logmsg(LOG_ERR,
				    "Logging suspended: open error");
				set_suspended(1);
			}
			gotsig_hup = 0;
		}

		if (gotsig_alrm) {
			if (dpcap)
				flush_buffer(dpcap);
			else 
				gotsig_hup = 1;
			gotsig_alrm = 0;
			alarm(delay);
		}

		if (gotsig_usr1) {
			log_pcap_stats();
			gotsig_usr1 = 0;
		}
	}

	logmsg(LOG_NOTICE, "Exiting");
	if (dpcap) {
		flush_buffer(dpcap);
		fclose(dpcap);
	}
	purge_buffer();

	log_pcap_stats();
	pcap_close(hpcap);
	if (!Debug)
		closelog();
	return (ret);
}
