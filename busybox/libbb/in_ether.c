/* vi: set sw=4 ts=4: */
/*
 * Utility routines.
 */
//kbuild:lib-$(CONFIG_ARP) += in_ether.o
//kbuild:lib-$(CONFIG_IFCONFIG) += in_ether.o
//kbuild:lib-$(CONFIG_IFENSLAVE) += in_ether.o

#include "libbb.h"
#include <net/if_arp.h>
#include <net/ethernet.h>

/* Convert Ethernet address from "XX[:]XX[:]XX[:]XX[:]XX[:]XX" to sockaddr.
 * Return nonzero on error.
 */
int FAST_FUNC in_ether(const char *bufp, struct sockaddr *sap)
{
	char *ptr;
	int i, j;
	unsigned char val;
	unsigned char c;

	sap->sa_family = ARPHRD_ETHER;
	ptr = (char *) sap->sa_data;

	i = ETH_ALEN;
	goto first;
	do {
		/* We might get a semicolon here */
		if (*bufp == ':')
			bufp++;
 first:
		j = val = 0;
		do {
			c = *bufp;
			if (((unsigned char)(c - '0')) <= 9) {
				c -= '0';
			} else if ((unsigned char)((c|0x20) - 'a') <= 5) {
				c = (unsigned char)((c|0x20) - 'a') + 10;
			} else {
				if (j && (c == ':' || c == '\0'))
					/* One-digit byte: __:X:__ */
					break;
				return -1;
			}
			++bufp;
			val <<= 4;
			val += c;
			j ^= 1;
		} while (j);

		*ptr++ = val;
	} while (--i);

	/* Error if we aren't at end of string */
	return *bufp;
}
