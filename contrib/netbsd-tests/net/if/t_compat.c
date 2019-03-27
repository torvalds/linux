/*	$NetBSD: t_compat.c,v 1.4 2016/11/12 15:12:59 kre Exp $	*/

#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <rump/rump.h>
#include <rump/rump_syscalls.h>

#include "../config/netconfig.c"

/*
 * Test for stack smashing in compat ioctl handling.  Adapted as an
 * atf test from code provided by Onno van der Linden in PR kern/44054
 */

struct oifreq {
        char    ifr_name[IFNAMSIZ];             /* if name, e.g. "en0" */
        union {
                struct  sockaddr ifru_addr;
                struct  sockaddr ifru_dstaddr;
                struct  sockaddr ifru_broadaddr;
                short   ifru_flags;  
                int     ifru_metric;
                int     ifru_mtu; 
                int     ifru_dlt;
                u_int   ifru_value;
                void *  ifru_data;
                struct {
                        uint32_t        b_buflen;
                        void            *b_buf;
                } ifru_b;
        } ifr_ifru;
};      
#define OOSIOCGIFBRDADDR _IOWR('i', 18, struct oifreq)

ATF_TC(OOSIOCGIFBRDADDR);
ATF_TC_HEAD(OOSIOCGIFBRDADDR, tc)
{

	atf_tc_set_md_var(tc, "descr", "Checks that OOSIOCGIFBRDADDR works "
	    "(PR kern/44054)");
}

ATF_TC_BODY(OOSIOCGIFBRDADDR, tc)
{
        int fd, ifnum;
        struct oifreq ifreq;
        struct sockaddr_in *sin;
	int rv;

        memset(&ifreq,'\0',sizeof ifreq);

	rump_init();

	/* create an interface and give it netmask 0xffff0000 */
	rv = rump_pub_shmif_create("bus", &ifnum);
	if (rv)
		atf_tc_fail("failed to create shmif: %s", strerror(rv));
	sprintf(ifreq.ifr_name, "shmif%d", ifnum);
	netcfg_rump_if(ifreq.ifr_name, "1.7.64.10", "255.255.0.0");

	atf_tc_expect_fail("PR kern/51610: rump does not include COMPAT_43");

	/* query kernel for iface bcast */
        RL(fd = rump_sys_socket(AF_INET, SOCK_DGRAM, 0));
        RL(rump_sys_ioctl(fd, OOSIOCGIFBRDADDR, &ifreq));

	/* make sure we got what we deserve */
        sin = (struct sockaddr_in *)&ifreq.ifr_broadaddr;
	ATF_REQUIRE_EQ(sin->sin_addr.s_addr, htonl(0x0107ffff));
        rump_sys_close(fd);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, OOSIOCGIFBRDADDR);
	return atf_no_error();
}
