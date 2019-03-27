/* $FreeBSD$ */
/*
 * ipsend.c (C) 1995-1998 Darren Reed
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 */
#if !defined(lint)
static const char sccsid[] = "@(#)ipsend.c	1.5 12/10/95 (C)1995 Darren Reed";
static const char rcsid[] = "@(#)$Id$";
#endif
#include <sys/param.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/in_systm.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>
#include <netinet/ip.h>
# include <netinet/ip_var.h>
#include "ipsend.h"
#include "ipf.h"
# include <netinet/udp_var.h>


extern	char	*optarg;
extern	int	optind;
extern	void	iplang __P((FILE *));

char	options[68];
int	opts;
# ifdef ultrix
char	default_device[] = "ln0";
# else
#  ifdef __bsdi__
char	default_device[] = "ef0";
#  else
char	default_device[] = "le0";
#  endif /* __bsdi__ */
# endif /* ultrix */


static	void	usage __P((char *));
static	void	do_icmp __P((ip_t *, char *));
void udpcksum(ip_t *, struct udphdr *, int);
int	main __P((int, char **));


static	void	usage(prog)
	char	*prog;
{
	fprintf(stderr, "Usage: %s [options] dest [flags]\n\
\toptions:\n\
\t\t-d\tdebug mode\n\
\t\t-i device\tSend out on this device\n\
\t\t-f fragflags\tcan set IP_MF or IP_DF\n\
\t\t-g gateway\tIP gateway to use if non-local dest.\n\
\t\t-I code,type[,gw[,dst[,src]]]\tSet ICMP protocol\n\
\t\t-m mtu\t\tfake MTU to use when sending out\n\
\t\t-P protocol\tSet protocol by name\n\
\t\t-s src\t\tsource address for IP packet\n\
\t\t-T\t\tSet TCP protocol\n\
\t\t-t port\t\tdestination port\n\
\t\t-U\t\tSet UDP protocol\n\
\t\t-v\tverbose mode\n\
\t\t-w <window>\tSet the TCP window size\n\
", prog);
	fprintf(stderr, "Usage: %s [-dv] -L <filename>\n\
\toptions:\n\
\t\t-d\tdebug mode\n\
\t\t-L filename\tUse IP language for sending packets\n\
\t\t-v\tverbose mode\n\
", prog);
	exit(1);
}


static void do_icmp(ip, args)
	ip_t *ip;
	char *args;
{
	struct	icmp	*ic;
	char	*s;

	ip->ip_p = IPPROTO_ICMP;
	ip->ip_len += sizeof(*ic);
	ic = (struct icmp *)(ip + 1);
	bzero((char *)ic, sizeof(*ic));
	if (!(s = strchr(args, ',')))
	    {
		fprintf(stderr, "ICMP args missing: ,\n");
		return;
	    }
	*s++ = '\0';
	ic->icmp_type = atoi(args);
	ic->icmp_code = atoi(s);
	if (ic->icmp_type == ICMP_REDIRECT && strchr(s, ','))
	    {
		char	*t;

		t = strtok(s, ",");
		t = strtok(NULL, ",");
		if (resolve(t, (char *)&ic->icmp_gwaddr) == -1)
		    {
			fprintf(stderr,"Cant resolve %s\n", t);
			exit(2);
		    }
		if ((t = strtok(NULL, ",")))
		    {
			if (resolve(t, (char *)&ic->icmp_ip.ip_dst) == -1)
			    {
				fprintf(stderr,"Cant resolve %s\n", t);
				exit(2);
			    }
			if ((t = strtok(NULL, ",")))
			    {
				if (resolve(t,
					    (char *)&ic->icmp_ip.ip_src) == -1)
				    {
					fprintf(stderr,"Cant resolve %s\n", t);
					exit(2);
				    }
			    }
		    }
	    }
}


int send_packets(dev, mtu, ip, gwip)
	char *dev;
	int mtu;
	ip_t *ip;
	struct in_addr gwip;
{
	int wfd;

	wfd = initdevice(dev, 5);
	if (wfd == -1)
		return -1;
	return send_packet(wfd, mtu, ip, gwip);
}

void
udpcksum(ip_t *ip, struct udphdr *udp, int len)
{
	union pseudoh {
		struct hdr {
			u_short len;
			u_char ttl;
			u_char proto;
			u_32_t src;
			u_32_t dst;
		} h;
		u_short w[6];
	} ph;
	u_32_t temp32;
	u_short *opts;

	ph.h.len = htons(len);
	ph.h.ttl = 0;
	ph.h.proto = IPPROTO_UDP;
	ph.h.src = ip->ip_src.s_addr;
	ph.h.dst = ip->ip_dst.s_addr;
	temp32 = 0;
	opts = &ph.w[0];
	temp32 += opts[0] + opts[1] + opts[2] + opts[3] + opts[4] + opts[5];
	temp32 = (temp32 >> 16) + (temp32 & 65535);
	temp32 += (temp32 >> 16);
	udp->uh_sum = temp32 & 65535;
	udp->uh_sum = chksum((u_short *)udp, len);
	if (udp->uh_sum == 0)
		udp->uh_sum = 0xffff;
}

int main(argc, argv)
	int	argc;
	char	**argv;
{
	FILE	*langfile = NULL;
	struct	in_addr	gwip;
	tcphdr_t	*tcp;
	udphdr_t	*udp;
	ip_t	*ip;
	char	*name =  argv[0], host[MAXHOSTNAMELEN + 1];
	char	*gateway = NULL, *dev = NULL;
	char	*src = NULL, *dst, *s;
	int	mtu = 1500, olen = 0, c, nonl = 0;

	/*
	 * 65535 is maximum packet size...you never know...
	 */
	ip = (ip_t *)calloc(1, 65536);
	tcp = (tcphdr_t *)(ip + 1);
	udp = (udphdr_t *)tcp;
	ip->ip_len = sizeof(*ip);
	IP_HL_A(ip, sizeof(*ip) >> 2);

	while ((c = getopt(argc, argv, "I:L:P:TUdf:i:g:m:o:s:t:vw:")) != -1) {
		switch (c)
		{
		case 'I' :
			nonl++;
			if (ip->ip_p)
			    {
				fprintf(stderr, "Protocol already set: %d\n",
					ip->ip_p);
				break;
			    }
			do_icmp(ip, optarg);
			break;
		case 'L' :
			if (nonl) {
				fprintf(stderr,
					"Incorrect usage of -L option.\n");
				usage(name);
			}
			if (!strcmp(optarg, "-"))
				langfile = stdin;
			else if (!(langfile = fopen(optarg, "r"))) {
				fprintf(stderr, "can't open file %s\n",
					optarg);
				exit(1);
			}
			iplang(langfile);
			return 0;
		case 'P' :
		    {
			struct	protoent	*p;

			nonl++;
			if (ip->ip_p)
			    {
				fprintf(stderr, "Protocol already set: %d\n",
					ip->ip_p);
				break;
			    }
			if ((p = getprotobyname(optarg)))
				ip->ip_p = p->p_proto;
			else
				fprintf(stderr, "Unknown protocol: %s\n",
					optarg);
			break;
		    }
		case 'T' :
			nonl++;
			if (ip->ip_p)
			    {
				fprintf(stderr, "Protocol already set: %d\n",
					ip->ip_p);
				break;
			    }
			ip->ip_p = IPPROTO_TCP;
			ip->ip_len += sizeof(tcphdr_t);
			break;
		case 'U' :
			nonl++;
			if (ip->ip_p)
			    {
				fprintf(stderr, "Protocol already set: %d\n",
					ip->ip_p);
				break;
			    }
			ip->ip_p = IPPROTO_UDP;
			ip->ip_len += sizeof(udphdr_t);
			break;
		case 'd' :
			opts |= OPT_DEBUG;
			break;
		case 'f' :
			nonl++;
			ip->ip_off = strtol(optarg, NULL, 0);
			break;
		case 'g' :
			nonl++;
			gateway = optarg;
			break;
		case 'i' :
			nonl++;
			dev = optarg;
			break;
		case 'm' :
			nonl++;
			mtu = atoi(optarg);
			if (mtu < 28)
			    {
				fprintf(stderr, "mtu must be > 28\n");
				exit(1);
			    }
			break;
		case 'o' :
			nonl++;
			olen = buildopts(optarg, options, (IP_HL(ip) - 5) << 2);
			break;
		case 's' :
			nonl++;
			src = optarg;
			break;
		case 't' :
			nonl++;
			if (ip->ip_p == IPPROTO_TCP || ip->ip_p == IPPROTO_UDP)
				tcp->th_dport = htons(atoi(optarg));
			break;
		case 'v' :
			opts |= OPT_VERBOSE;
			break;
		case 'w' :
			nonl++;
			if (ip->ip_p == IPPROTO_TCP)
				tcp->th_win = atoi(optarg);
			else
				fprintf(stderr, "set protocol to TCP first\n");
			break;
		default :
			fprintf(stderr, "Unknown option \"%c\"\n", c);
			usage(name);
		}
	}

	if (argc - optind < 1)
		usage(name);
	dst = argv[optind++];

	if (!src)
	    {
		gethostname(host, sizeof(host));
		src = host;
	    }

	if (resolve(src, (char *)&ip->ip_src) == -1)
	    {
		fprintf(stderr,"Cant resolve %s\n", src);
		exit(2);
	    }

	if (resolve(dst, (char *)&ip->ip_dst) == -1)
	    {
		fprintf(stderr,"Cant resolve %s\n", dst);
		exit(2);
	    }

	if (!gateway)
		gwip = ip->ip_dst;
	else if (resolve(gateway, (char *)&gwip) == -1)
	    {
		fprintf(stderr,"Cant resolve %s\n", gateway);
		exit(2);
	    }

	if (olen)
	    {
		int hlen;
		char *p;

		printf("Options: %d\n", olen);
		hlen = sizeof(*ip) + olen;
		IP_HL_A(ip, hlen >> 2);
		ip->ip_len += olen;
		p = (char *)malloc(65536);
		if (p == NULL)
		    {
			fprintf(stderr, "malloc failed\n");
			exit(2);
		    }

		bcopy(ip, p, sizeof(*ip));
		bcopy(options, p + sizeof(*ip), olen);
		bcopy(ip + 1, p + hlen, ip->ip_len - hlen);
		ip = (ip_t *)p;

		if (ip->ip_p == IPPROTO_TCP) {
			tcp = (tcphdr_t *)(p + hlen);
		} else if (ip->ip_p == IPPROTO_UDP) {
			udp = (udphdr_t *)(p + hlen);
		}
	    }

	if (ip->ip_p == IPPROTO_TCP)
		for (s = argv[optind]; s && (c = *s); s++)
			switch(c)
			{
			case 'S' : case 's' :
				tcp->th_flags |= TH_SYN;
				break;
			case 'A' : case 'a' :
				tcp->th_flags |= TH_ACK;
				break;
			case 'F' : case 'f' :
				tcp->th_flags |= TH_FIN;
				break;
			case 'R' : case 'r' :
				tcp->th_flags |= TH_RST;
				break;
			case 'P' : case 'p' :
				tcp->th_flags |= TH_PUSH;
				break;
			case 'U' : case 'u' :
				tcp->th_flags |= TH_URG;
				break;
			}

	if (!dev)
		dev = default_device;
	printf("Device:  %s\n", dev);
	printf("Source:  %s\n", inet_ntoa(ip->ip_src));
	printf("Dest:    %s\n", inet_ntoa(ip->ip_dst));
	printf("Gateway: %s\n", inet_ntoa(gwip));
	if (ip->ip_p == IPPROTO_TCP && tcp->th_flags)
		printf("Flags:   %#x\n", tcp->th_flags);
	printf("mtu:     %d\n", mtu);

	if (ip->ip_p == IPPROTO_UDP) {
		udp->uh_sum = 0;
		udpcksum(ip, udp, ip->ip_len - (IP_HL(ip) << 2));
	}
#ifdef	DOSOCKET
	if (ip->ip_p == IPPROTO_TCP && tcp->th_dport)
		return do_socket(dev, mtu, ip, gwip);
#endif
	return send_packets(dev, mtu, ip, gwip);
}
