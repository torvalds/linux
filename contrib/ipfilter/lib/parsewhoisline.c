/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * $Id: parsewhoisline.c,v 1.2.2.5 2012/07/22 08:04:24 darren_r Exp $
 */
#include "ipf.h"

/*
Microsoft Corp MICROSOFT19 (NET-198-136-97-0-1) 198.137.97.0 - 198.137.97.255
Microsoft Corp SAVV-S233053-6 (NET-206-79-74-32-1) 206.79.74.32 - 206.79.74.47
 */
int
parsewhoisline(line, addrp, maskp)
	char *line;
	addrfamily_t *addrp;
	addrfamily_t *maskp;
{
	struct in_addr a1, a2;
	char *src = line;
	char *s = NULL;

	if (line == NULL)
		return -1;

	while (*src != '\0') {
		s = strchr(src, '(');
		if (s == NULL)
			break;

		if (strncmp(s, "(NET", 4)) {
			src = s + 1;
		}
		break;
	}

	if (s == NULL)
		return -1;

	memset(addrp, 0x00, sizeof(*maskp));
	memset(maskp, 0x00, sizeof(*maskp));

	if (*(s + 4) == '6') {
#ifdef USE_INET6
		i6addr_t a61, a62;

		s = strchr(s, ')');
		if (s == NULL || *++s != ' ')
			return -1;
		/*
		 * Parse the IPv6
		 */
		if (inet_pton(AF_INET6, s, &a61.in6) != 1)
			return -1;

		s = strchr(s, ' ');
		if (s == NULL || strncmp(s, " - ", 3))
			return -1;

		s += 3;
		if (inet_pton(AF_INET6, s, &a62) != 1)
			return -1;

		addrp->adf_addr = a61;
		addrp->adf_family = AF_INET6;
		addrp->adf_len = offsetof(addrfamily_t, adf_addr) +
				 sizeof(struct in6_addr);

		maskp->adf_addr.i6[0] = ~(a62.i6[0] ^ a61.i6[0]);
		maskp->adf_addr.i6[1] = ~(a62.i6[1] ^ a61.i6[1]);
		maskp->adf_addr.i6[2] = ~(a62.i6[2] ^ a61.i6[2]);
		maskp->adf_addr.i6[3] = ~(a62.i6[3] ^ a61.i6[3]);

		/*
		 * If the mask that's been generated isn't a consecutive mask
		 * then we can't add it into a pool.
		 */
		if (count6bits(maskp->adf_addr.i6) == -1)
			return -1;

		maskp->adf_family = AF_INET6;
		maskp->adf_len = addrp->adf_len;

		if (IP6_MASKNEQ(&addrp->adf_addr.in6, &maskp->adf_addr.in6,
				&addrp->adf_addr.in6)) {
			return -1;
		}
		return 0;
#else
		return -1;
#endif
	}

	s = strchr(s, ')');
	if (s == NULL || *++s != ' ')
		return -1;

	s++;

	if (inet_aton(s, &a1) != 1)
		return -1;

	s = strchr(s, ' ');
	if (s == NULL || strncmp(s, " - ", 3))
		return -1;

	s += 3;
	if (inet_aton(s, &a2) != 1)
		return -1;

	addrp->adf_addr.in4 = a1;
	addrp->adf_family = AF_INET;
	addrp->adf_len = offsetof(addrfamily_t, adf_addr) +
			 sizeof(struct in_addr);
	maskp->adf_addr.in4.s_addr = ~(a2.s_addr ^ a1.s_addr);

	/*
	 * If the mask that's been generated isn't a consecutive mask then
	 * we can't add it into a pool.
	 */
	if (count4bits(maskp->adf_addr.in4.s_addr) == -1)
		return -1;

	maskp->adf_family = AF_INET;
	maskp->adf_len = addrp->adf_len;
	bzero((char *)maskp + maskp->adf_len, sizeof(*maskp) - maskp->adf_len);
	if ((addrp->adf_addr.in4.s_addr & maskp->adf_addr.in4.s_addr) !=
	    addrp->adf_addr.in4.s_addr)
		return -1;
	return 0;
}
