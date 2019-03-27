#include <sys/types.h>

#include <rump/rump.h>
#include <rump/rump_syscalls.h>

#include <atf-c.h>

ATF_TC(extattrctl_namei);
ATF_TC_HEAD(extattrctl_namei, tc)
{

	atf_tc_set_md_var(tc, "descr", "extattrctl namei safety (kern/43328)");
}

ATF_TC_BODY(extattrctl_namei, tc)
{

	rump_init();

	rump_sys_extattrctl("/anyfile", 0, "/", 0, 0);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, extattrctl_namei);

	return atf_no_error();
}
