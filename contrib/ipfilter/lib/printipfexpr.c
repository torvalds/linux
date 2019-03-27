#include "ipf.h"

static void printport __P((int *));
static void printhosts __P((int *));
static void printsingle __P((int *));
#ifdef USE_INET6
static void printhostsv6 __P((int *));
#endif

void
printipfexpr(array)
	int *array;
{
	int i, nelems, j, not;
	ipfexp_t *ipfe;

	nelems = array[0];

	for (i = 1; i < nelems; ) {
		ipfe = (ipfexp_t *)(array + i);
		if (ipfe->ipfe_cmd == IPF_EXP_END)
			break;

		not = ipfe->ipfe_not;

		switch (ipfe->ipfe_cmd)
		{
		case IPF_EXP_IP_ADDR :
			PRINTF("ip.addr %s= ", not ? "!" : "");
			printhosts(array + i);
			break;

		case IPF_EXP_IP_PR :
			PRINTF("ip.p %s= ", not ? "!" : "");
			printsingle(array + i);
			break;

		case IPF_EXP_IP_SRCADDR :
			PRINTF("ip.src %s= ", not ? "!" : "");
			printhosts(array + i);
			break;

		case IPF_EXP_IP_DSTADDR :
			PRINTF("ip.dst %s= ", not ? "!" : "");
			printhosts(array + i);
			break;

		case IPF_EXP_TCP_PORT :
			PRINTF("tcp.port %s= ", not ? "!" : "");
			printport(array + i);
			break;

		case IPF_EXP_TCP_DPORT :
			PRINTF("tcp.dport %s= ", not ? "!" : "");
			printport(array + i);
			break;

		case IPF_EXP_TCP_SPORT :
			PRINTF("tcp.sport %s= ", not ? "!" : "");
			printport(array + i);
			break;

		case IPF_EXP_TCP_FLAGS :
			PRINTF("tcp.flags %s= ", not ? "!" : "");

			for (j = 0; j < ipfe->ipfe_narg; ) {
				printtcpflags(array[i + 4], array[i + 5]);
				j += 2;
				if (j < array[4])
					putchar(',');
			}
			break;

		case IPF_EXP_UDP_PORT :
			PRINTF("udp.port %s= ", not ? "!" : "");
			printport(array + i);
			break;

		case IPF_EXP_UDP_DPORT :
			PRINTF("udp.dport %s= ", not ? "!" : "");
			printport(array + i);
			break;

		case IPF_EXP_UDP_SPORT :
			PRINTF("udp.sport %s= ", not ? "!" : "");
			printport(array + i);
			break;

		case IPF_EXP_IDLE_GT :
			PRINTF("idle-gt %s= ", not ? "!" : "");
			printsingle(array + i);
			break;

		case IPF_EXP_TCP_STATE :
			PRINTF("tcp-state %s= ", not ? "!" : "");
			printsingle(array + i);
			break;

#ifdef USE_INET6
		case IPF_EXP_IP6_ADDR :
			PRINTF("ip6.addr %s= ", not ? "!" : "");
			printhostsv6(array + i);
			break;

		case IPF_EXP_IP6_SRCADDR :
			PRINTF("ip6.src %s= ", not ? "!" : "");
			printhostsv6(array + i);
			break;

		case IPF_EXP_IP6_DSTADDR :
			PRINTF("ip6.dst %s= ", not ? "!" : "");
			printhostsv6(array + i);
			break;
#endif

		case IPF_EXP_END :
			break;

		default :
			PRINTF("#%#x,len=%d;",
			       ipfe->ipfe_cmd, ipfe->ipfe_narg);
		}

		if (array[i] != IPF_EXP_END)
			putchar(';');

		i += ipfe->ipfe_size;
		if (array[i] != IPF_EXP_END)
			putchar(' ');
	}
}


static void
printsingle(array)
	int *array;
{
	ipfexp_t *ipfe = (ipfexp_t *)array;
	int i;

	for (i = 0; i < ipfe->ipfe_narg; ) {
		PRINTF("%d", array[i + 4]);
		i++;
		if (i < ipfe->ipfe_narg)
			putchar(',');
	}
}


static void
printport(array)
	int *array;
{
	ipfexp_t *ipfe = (ipfexp_t *)array;
	int i;

	for (i = 0; i < ipfe->ipfe_narg; ) {
		PRINTF("%d", ntohs(array[i + 4]));
		i++;
		if (i < ipfe->ipfe_narg)
			putchar(',');
	}
}


static void
printhosts(array)
	int *array;
{
	ipfexp_t *ipfe = (ipfexp_t *)array;
	int i, j;

	for (i = 0, j = 0; i < ipfe->ipfe_narg; j++) {
		printhostmask(AF_INET, (u_32_t *)ipfe->ipfe_arg0 + j * 2,
			      (u_32_t *)ipfe->ipfe_arg0 + j * 2 + 1);
		i += 2;
		if (i < ipfe->ipfe_narg)
			putchar(',');
	}
}


#ifdef USE_INET6
static void
printhostsv6(array)
	int *array;
{
	ipfexp_t *ipfe = (ipfexp_t *)array;
	int i, j;

	for (i = 4, j= 0; i < ipfe->ipfe_size; j++) {
		printhostmask(AF_INET6, (u_32_t *)ipfe->ipfe_arg0 + j * 8,
			      (u_32_t *)ipfe->ipfe_arg0 + j * 8 + 4);
		i += 8;
		if (i < ipfe->ipfe_size)
			putchar(',');
	}
}
#endif
