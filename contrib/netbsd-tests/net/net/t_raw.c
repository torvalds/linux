/*	$NetBSD: t_raw.c,v 1.2 2017/01/13 21:30:42 christos Exp $	*/

#include <sys/socket.h>
#include <sys/stat.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <rump/rump.h>
#include <rump/rump_syscalls.h>

#include <atf-c.h>

#include "h_macros.h"

ATF_TC(PRU_SENSE);
ATF_TC_HEAD(PRU_SENSE, tc)
{

	atf_tc_set_md_var(tc, "descr", "Biglock leak with PRU_SENSE on "
	    "raw sockets (PR kern/44369)");
}

ATF_TC_BODY(PRU_SENSE, tc)
{
	struct stat sb;
	int s;

	rump_init();
	RL(s = rump_sys_socket(PF_ROUTE, SOCK_RAW, 0));
	/* call PRU_SENSE.  unfixed bug causes panic in rump_unschedule() */
	RL(rump_sys_fstat(s, &sb));
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, PRU_SENSE);
	return atf_no_error();
}
