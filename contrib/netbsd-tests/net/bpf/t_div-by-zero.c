#include <sys/types.h>
#include <sys/ioctl.h>

#include <net/bpf.h>

#include <atf-c.h>
#include <fcntl.h>

#include <rump/rump.h>
#include <rump/rump_syscalls.h>

ATF_TC(div_by_zero);
ATF_TC_HEAD(div_by_zero, tc)
{

	atf_tc_set_md_var(tc, "descr", "Check that BPF rejects a filter "
	    "which divides by 0");
}

ATF_TC_BODY(div_by_zero, tc)
{
	struct bpf_program bp;
	int fd;

	/*
	 * Source code for following program:
	 * link[0:4]/0 = 2
	 */
	struct bpf_insn bins[] = {
	    { 0x20, 0, 0, 0x00000000 },
	    { 0x34, 0, 0, 0x00000000 },
	    { 0x15, 0, 1, 0x00000002 },
	    { 0x6, 0, 0, 0x00000060 },
	    { 0x6, 0, 0, 0x00000000 },
	};

	bp.bf_len = __arraycount(bins);
	bp.bf_insns = bins;

	rump_init();
	fd = rump_sys_open("/dev/bpf", O_RDWR);
	ATF_CHECK(fd != -1);
	ATF_REQUIRE_EQ_MSG(rump_sys_ioctl(fd, BIOCSETF, &bp), -1,
	    "bpf accepted program with division by zero");
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, div_by_zero);
	return atf_no_error();
}
