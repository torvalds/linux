#include <sys/cdefs.h>
__RCSID("$NetBSD: t_udp.c,v 1.2 2013/01/06 02:22:50 christos Exp $");

#include <sys/socket.h>
#include <netinet/in.h>

#include <netdb.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include <atf-c.h>

static const char msg[] = "sendto test";

static void
sendit(int family)
{
	struct addrinfo hints;
	struct addrinfo *res;
	int             S, s;
	int             e;

	/* lookup localhost addr, depending on argv[1] */
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = family;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;
	hints.ai_flags = 0;

	e = getaddrinfo("localhost", "9999", &hints, &res);
	ATF_REQUIRE_MSG(e == 0, "getaddrinfo AF=%d: %s", family,
	    gai_strerror(e));

	/* server socket */
	S = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	ATF_REQUIRE_MSG(S >= 0, "server-socket AF=%d: %s", family,
	    strerror(errno));

	e = bind(S, res->ai_addr, res->ai_addrlen);
	ATF_REQUIRE_MSG(e == 0, "bind AF=%d: %s", family,
	    strerror(errno));

	/* client socket */
	s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	ATF_REQUIRE_MSG(s >= 0, "client-socket AF=%d: %s", family,
	    strerror(errno));

	/* sendto */
	e = sendto(s, msg, sizeof(msg), 0, res->ai_addr, res->ai_addrlen);
	ATF_REQUIRE_MSG(e == sizeof(msg), "sendto(1) AF=%d: %s", family,
	    strerror(errno));

	e = sendto(s, msg, sizeof(msg), 0, res->ai_addr, res->ai_addrlen);
	ATF_REQUIRE_MSG(e == sizeof(msg), "sendto(2) AF=%d: %s", family,
	    strerror(errno));

	/* connect + send */
	e = connect(s, res->ai_addr, res->ai_addrlen);
	ATF_REQUIRE_MSG(e == 0, "connect(1) AF=%d: %s", family,
	    strerror(errno));

	e = send(s, msg, sizeof(msg), 0);
	ATF_REQUIRE_MSG(e == sizeof(msg), "send(1) AF=%d: %s", family,
	    strerror(errno));

	e = connect(s, res->ai_addr, res->ai_addrlen);
	ATF_REQUIRE_MSG(e == 0, "connect(2) AF=%d: %s", family,
	    strerror(errno));

	e = send(s, msg, sizeof(msg), 0);
	ATF_REQUIRE_MSG(e == sizeof(msg), "send(2) AF=%d: %s", family,
	    strerror(errno));

	close(s);
}

ATF_TC(udp4_send);
ATF_TC_HEAD(udp4_send, tc)
{

	atf_tc_set_md_var(tc, "descr", "Check that inet4 udp send works both"
	    " for connected and unconnected sockets");
}

ATF_TC_BODY(udp4_send, tc)
{
	sendit(AF_INET);
}

ATF_TC(udp6_send);
ATF_TC_HEAD(udp6_send, tc)
{

	atf_tc_set_md_var(tc, "descr", "Check that inet6 udp send works both"
	    " for connected and unconnected sockets");
}

ATF_TC_BODY(udp6_send, tc)
{
	sendit(AF_INET6);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, udp4_send);
	ATF_TP_ADD_TC(tp, udp6_send);
	return atf_no_error();
}
