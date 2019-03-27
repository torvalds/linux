/*
 * Copyright (c) 2013 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 */

#include <sm/gen.h>
SM_IDSTR(id, "@(#)$Id: t-inet6_ntop.c,v 1.2 2013-11-22 20:51:43 ca Exp $")

#include <sm/conf.h>
#if NETINET6
#include <sm/io.h>
#include <sm/test.h>
#include <sm/string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static char *ipv6f[] = {
	"1234:5678:9abc:def0:fedc:dead:f00f:101",
	"1080:0:0:0:8:800:200c:417a",
	"ff01:0:0:0:0:0:0:43",
	"0:0:0:0:0:0:0:1",
	"1:0:0:0:0:0:0:1",
	"0:1:0:0:0:0:0:1",
	"0:0:1:0:0:0:0:1",
	"0:0:0:1:0:0:0:1",
	"0:0:0:0:1:0:0:1",
	"0:0:0:0:0:1:0:1",
	"0:0:0:0:0:0:1:1",
	"1:a:b:c:d:e:f:9",
	"0:0:0:0:0:0:0:0",
	NULL
};

static void
test()
{
	int i, r;
	struct sockaddr_in6 addr;
	char *ip, *ipf, ipv6str[INET6_ADDRSTRLEN];

	for (i = 0; (ip = ipv6f[i]) != NULL; i++) {
		r = inet_pton(AF_INET6, ip, &addr.sin6_addr);
		SM_TEST(r == 1);
		ipf = sm_inet6_ntop(&addr.sin6_addr, ipv6str, sizeof(ipv6str));
		SM_TEST(ipf != NULL);
		SM_TEST(strcmp(ipf, ip) == 0);
	}
}

int
main(argc, argv)
	int argc;
	char **argv;
{
	sm_test_begin(argc, argv, "test inet6_ntop");
	test();
	return sm_test_end();
}
#else /* NETINET6 */

int
main(argc, argv)
	int argc;
	char **argv;
{
	return 0;
}
#endif /* NETINET6 */
