/*	$FreeBSD$	*/

/*
 * ipresend.c (C) 1995-1998 Darren Reed
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 */
#if !defined(lint)
static const char sccsid[] = "%W% %G% (C)1995 Darren Reed";
static const char rcsid[] = "@(#)$Id$";
#endif
#include <sys/param.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>
#include "ipsend.h"


extern	char	*optarg;
extern	int	optind;
#ifndef	NO_IPF
extern	struct	ipread	pcap, iphex, iptext;
#endif

int	opts = 0;
#ifndef	DEFAULT_DEVICE
#  ifdef	sun
char	default_device[] = "le0";
#  else
#   ifdef	ultrix
char	default_device[] = "ln0";
#   else
#    ifdef	__bsdi__
char	default_device[] = "ef0";
#    else
char	default_device[] = "lan0";
#    endif
#   endif
#  endif
#else
char	default_device[] = DEFAULT_DEVICE;
#endif


static	void	usage __P((char *));
int	main __P((int, char **));


static void usage(prog)
	char	*prog;
{
	fprintf(stderr, "Usage: %s [options] <-r filename|-R filename>\n\
\t\t-r filename\tsnoop data file to resend\n\
\t\t-R filename\tlibpcap data file to resend\n\
\toptions:\n\
\t\t-d device\tSend out on this device\n\
\t\t-g gateway\tIP gateway to use if non-local dest.\n\
\t\t-m mtu\t\tfake MTU to use when sending out\n\
", prog);
	exit(1);
}


int main(argc, argv)
	int	argc;
	char	**argv;
{
	struct	in_addr	gwip;
	struct	ipread	*ipr = NULL;
	char	*name =  argv[0], *gateway = NULL, *dev = NULL;
	char	*resend = NULL;
	int	mtu = 1500, c;

	while ((c = getopt(argc, argv, "EHPRSTXd:g:m:r:")) != -1)
		switch (c)
		{
		case 'd' :
			dev = optarg;
			break;
		case 'g' :
			gateway = optarg;
			break;
		case 'm' :
			mtu = atoi(optarg);
			if (mtu < 28)
			    {
				fprintf(stderr, "mtu must be > 28\n");
				exit(1);
			    }
		case 'r' :
			resend = optarg;
			break;
		case 'R' :
			opts |= OPT_RAW;
			break;
#ifndef	NO_IPF
		case 'H' :
			ipr = &iphex;
			break;
		case 'P' :
			ipr = &pcap;
			break;
		case 'X' :
			ipr = &iptext;
			break;
#endif
		default :
			fprintf(stderr, "Unknown option \"%c\"\n", c);
			usage(name);
		}

	if (!ipr || !resend)
		usage(name);

	gwip.s_addr = 0;
	if (gateway && resolve(gateway, (char *)&gwip) == -1)
	    {
		fprintf(stderr,"Cant resolve %s\n", gateway);
		exit(2);
	    }

	if (!dev)
		dev = default_device;

	printf("Device:  %s\n", dev);
	printf("Gateway: %s\n", inet_ntoa(gwip));
	printf("mtu:     %d\n", mtu);

	return ip_resend(dev, mtu, ipr, gwip, resend);
}
