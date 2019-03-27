/*
 * Stanford Enetfilter subroutines for tcpdump
 *
 * Based on the MERIT NNstat etherifrt.c and the Ultrix pcap-pf.c
 * subroutines.
 *
 * Rayan Zachariassen, CA*Net
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/types.h>
#include <sys/time.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <net/if.h>
#include <pcap/bpf.h>
#include <net/enet.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <stdio.h>
#include <errno.h>

#include "interface.h"

struct packet_header {
#ifdef	IBMRTPC
	struct LengthWords	length;
	struct tap_header	tap;
#endif	/* IBMRTPC */
	u_char			packet[8]
};

extern int errno;

#define BUFSPACE (4*1024)

/* Forwards */
static void efReadError(int, char *);

void
readloop(int cnt, int if_fd, struct bpf_program *fp, printfunc printit)
{
#ifdef	IBMRTPC
	register struct packet_header *ph;
	register u_char *bp;
	register int inc;
#else	/* !IBMRTPC */
	static struct timeval tv = { 0 };
#endif	/* IBMRTPC */
	register int cc, caplen;
	register struct bpf_insn *fcode = fp->bf_insns;
	union {
		struct packet_header hdr;
		u_char	p[BUFSPACE];
		u_short	s;
	} buf;

	while (1) {
		if ((cc = read(if_fd, (char *)buf.p, sizeof(buf))) < 0)
			efReadError(if_fd, "reader");

#ifdef	IBMRTPC
		/*
		 * Loop through each packet.
		 */
		bp = buf.p;
		while (cc > 0) {
			ph = (struct packet_header *)bp;
			caplen = ph->tap.th_wirelen > snaplen ? snaplen : ph->tap
.th_wirelen ;
			if (bpf_filter(fcode, (char *)ph->packet,
						ph->tap.th_wirelen, caplen)) {
				if (cnt >= 0 && --cnt < 0)
					goto out;
				(*printit)((char *)ph->packet,
					(struct timeval *)ph->tap.th_timestamp,
					ph->tap.th_wirelen, caplen);
			}
			inc = ph->length.PacketOffset;
			cc -= inc;
			bp += inc;
		}
#else	/* !IBMRTPC */
		caplen = cc > snaplen ? snaplen : cc ;
		if (bpf_filter(fcode, buf.hdr.packet, cc, caplen)) {
			if (cnt >= 0 && --cnt < 0)
				goto out;
			(*printit)(buf.hdr.packet, &tv, cc, caplen);
		}
#endif	/* IBMRTPC */
	}
 out:
	wrapup(if_fd);
}

/* Call ONLY if read() has returned an error on packet filter */
static void
efReadError(int fid, char *msg)
{
	if (errno == EINVAL) {	/* read MAXINT bytes already! */
		if (lseek(fid, 0, 0) < 0) {
			perror("tcpdump: efReadError/lseek");
			exit(-1);
		}
		else
			return;
	}
	else {
		(void) fprintf(stderr, "tcpdump: ");
		perror(msg);
		exit(-1);
	}
}

void
wrapup(int fd)
{
#ifdef	IBMRTPC
	struct enstats es;

	if (ioctl(fd, EIOSTATS, &es) == -1) {
		perror("tcpdump: enet ioctl EIOSTATS error");
		exit(-1);
	}

	fprintf(stderr, "%d packets queued", es.enStat_Rcnt);
	if (es.enStat_Rdrops > 0)
		fprintf(stderr, ", %d dropped", es.enStat_Rdrops);
	if (es.enStat_Reads > 0)
		fprintf(stderr, ", %d tcpdump %s", es.enStat_Reads,
				es.enStat_Reads > 1 ? "reads" : "read");
	if (es.enStat_MaxRead > 1)
		fprintf(stderr, ", %d packets in largest read",
			es.enStat_MaxRead);
	putc('\n', stderr);
#endif	/* IBMRTPC */
	close(fd);
}

int
initdevice(char *device, int pflag, int *linktype)
{
	struct eniocb ctl;
	struct enfilter filter;
	u_int maxwaiting;
	int if_fd;

#ifdef	IBMRTPC
	GETENETDEVICE(0, O_RDONLY, &if_fd);
#else	/* !IBMRTPC */
	if_fd = open("/dev/enet", O_RDONLY, 0);
#endif	/* IBMRTPC */

	if (if_fd == -1) {
		perror("tcpdump: enet open error");
		error(
"your system may not be properly configured; see \"man enet(4)\"");
		exit(-1);
	}

	/*  Get operating parameters. */

	if (ioctl(if_fd, EIOCGETP, (char *)&ctl) == -1) {
		perror("tcpdump: enet ioctl EIOCGETP error");
		exit(-1);
	}

	/*  Set operating parameters. */

#ifdef	IBMRTPC
	ctl.en_rtout = 1 * ctl.en_hz;
	ctl.en_tr_etherhead = 1;
	ctl.en_tap_network = 1;
	ctl.en_multi_packet = 1;
	ctl.en_maxlen = BUFSPACE;
#else	/* !IBMRTPC */
	ctl.en_rtout = 64;	/* randomly picked value for HZ */
#endif	/* IBMRTPC */
	if (ioctl(if_fd, EIOCSETP, &ctl) == -1) {
		perror("tcpdump: enet ioctl EIOCSETP error");
		exit(-1);
	}

	/*  Flush the receive queue, since we've changed
	    the operating parameters and we otherwise might
	    receive data without headers. */

	if (ioctl(if_fd, EIOCFLUSH) == -1) {
		perror("tcpdump: enet ioctl EIOCFLUSH error");
		exit(-1);
	}

	/*  Set the receive queue depth to its maximum. */

	maxwaiting = ctl.en_maxwaiting;
	if (ioctl(if_fd, EIOCSETW, &maxwaiting) == -1) {
		perror("tcpdump: enet ioctl EIOCSETW error");
		exit(-1);
	}

#ifdef	IBMRTPC
	/*  Clear statistics. */

	if (ioctl(if_fd, EIOCLRSTAT, 0) == -1) {
		perror("tcpdump: enet ioctl EIOCLRSTAT error");
		exit(-1);
	}
#endif	/* IBMRTPC */

	/*  Set the filter (accept all packets). */

	filter.enf_Priority = 3;
	filter.enf_FilterLen = 0;
	if (ioctl(if_fd, EIOCSETF, &filter) == -1) {
		perror("tcpdump: enet ioctl EIOCSETF error");
		exit(-1);
	}
	/*
	 * "enetfilter" supports only ethernets.
	 */
	*linktype = DLT_EN10MB;

	return(if_fd);
}
