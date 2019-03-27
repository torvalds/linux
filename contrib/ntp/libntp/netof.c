/* 
 * netof - return the net address part of an ip address in a sockaddr_storage structure
 *         (zero out host part)
 */
#include <config.h>
#include <stdio.h>
#include <syslog.h>

#include "ntp_fp.h"
#include "ntp_net.h"
#include "ntp_stdlib.h"
#include "ntp.h"

sockaddr_u *
netof(
	sockaddr_u *hostaddr
	)
{
	static sockaddr_u	netofbuf[8];
	static int		next_netofbuf;
	u_int32			netnum;
	sockaddr_u *		netaddr;

	netaddr = &netofbuf[next_netofbuf];
	next_netofbuf = (next_netofbuf + 1) % COUNTOF(netofbuf);

	memcpy(netaddr, hostaddr, sizeof(*netaddr));

	if (IS_IPV4(netaddr)) {
		netnum = SRCADR(netaddr);

		/*
		 * We live in a modern CIDR world where the basement nets, which
		 * used to be class A, are now probably associated with each
		 * host address. So, for class-A nets, all bits are significant.
		 */
		if (IN_CLASSC(netnum))
			netnum &= IN_CLASSC_NET;
		else if (IN_CLASSB(netnum))
			netnum &= IN_CLASSB_NET;

		SET_ADDR4(netaddr, netnum);

	} else if (IS_IPV6(netaddr))
		/* assume the typical /64 subnet size */
		zero_mem(&NSRCADR6(netaddr)[8], 8);
#ifdef DEBUG
	else {
		msyslog(LOG_ERR, "netof unknown AF %d", AF(netaddr));
		exit(1);
	}
#endif

	return netaddr;
}
