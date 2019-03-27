/*	$NetBSD: t_bpfilter.c,v 1.11 2017/01/13 21:30:42 christos Exp $	*/

/*-
 * Copyright (c) 2012 The NetBSD Foundation, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <sys/cdefs.h>
__RCSID("$NetBSD: t_bpfilter.c,v 1.11 2017/01/13 21:30:42 christos Exp $");

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/mbuf.h>
#include <sys/sysctl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include <net/if.h>
#include <net/if_ether.h>
#include <net/bpf.h>

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <rump/rump.h>
#include <rump/rump_syscalls.h>

/* XXX: atf-c.h has collisions with mbuf */
#undef m_type
#undef m_data
#include <atf-c.h>

#include "h_macros.h"
#include "../config/netconfig.c"


#define SNAPLEN UINT32_MAX

#define BMAGIC UINT32_C(0x37)
#define HMAGIC UINT32_C(0xc2c2)
#define WMAGIC UINT32_C(0x7d7d7d7d)

static const char magic_echo_reply_tail[7] = {
	BMAGIC,
	HMAGIC & 0xff,
	HMAGIC & 0xff,
	WMAGIC & 0xff,
	WMAGIC & 0xff,
	WMAGIC & 0xff,
	WMAGIC & 0xff
};

/*
 * Match ICMP_ECHOREPLY packet with 7 magic bytes at the end.
 */
static struct bpf_insn magic_echo_reply_prog[] = {
	BPF_STMT(BPF_LD+BPF_ABS+BPF_B,
	    sizeof(struct ip) + offsetof(struct icmp, icmp_type)),
	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, ICMP_ECHOREPLY, 1, 0),
	BPF_STMT(BPF_RET+BPF_K, 0),

	BPF_STMT(BPF_LD+BPF_W+BPF_LEN, 0),  /* A <- len   */
	BPF_STMT(BPF_ALU+BPF_SUB+BPF_K, 7), /* A <- A - 7 */
	BPF_STMT(BPF_MISC+BPF_TAX, 0),      /* X <- A     */

	BPF_STMT(BPF_LD+BPF_IND+BPF_B, 0),
	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, BMAGIC, 1, 0),
	BPF_STMT(BPF_RET+BPF_K, 0),

	BPF_STMT(BPF_LD+BPF_IND+BPF_H, 1),
	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, HMAGIC, 1, 0),
	BPF_STMT(BPF_RET+BPF_K, 0),

	BPF_STMT(BPF_LD+BPF_IND+BPF_W, 3),
	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, WMAGIC, 1, 0),
	BPF_STMT(BPF_RET+BPF_K, 0),

	BPF_STMT(BPF_RET+BPF_K, SNAPLEN)
};

static struct bpf_insn badmem_prog[] = {
	BPF_STMT(BPF_LD+BPF_MEM, 5),
	BPF_STMT(BPF_RET+BPF_A, 0),
};

static struct bpf_insn noinitA_prog[] = {
	BPF_STMT(BPF_RET+BPF_A, 0),
};

static struct bpf_insn noinitX_prog[] = {
	BPF_STMT(BPF_MISC+BPF_TXA, 0),
	BPF_STMT(BPF_RET+BPF_A, 0),
};

static struct bpf_insn badjmp_prog[] = {
	BPF_STMT(BPF_JMP+BPF_JA, 5),
	BPF_STMT(BPF_RET+BPF_A, 0),
};

static struct bpf_insn negjmp_prog[] = {
	BPF_STMT(BPF_JMP+BPF_JA, 0),
	BPF_STMT(BPF_JMP+BPF_JA, UINT32_MAX - 1), // -2
	BPF_STMT(BPF_RET+BPF_A, 0),
};

static struct bpf_insn badret_prog[] = {
	BPF_STMT(BPF_RET+BPF_A+0x8000, 0),
};

static uint16_t
in_cksum(void *data, size_t len)
{
	uint16_t *buf = data;
	unsigned sum;

	for (sum = 0; len > 1; len -= 2)
		sum += *buf++;
	if (len)
		sum += *(uint8_t *)buf;

	sum = (sum >> 16) + (sum & 0xffff);
	sum += (sum >> 16);

	return ~sum;
}

/*
 * Based on netcfg_rump_pingtest().
 */
static bool __unused
pingtest(const char *dst, unsigned int wirelen, const char tail[7])
{
	struct timeval tv;
	struct sockaddr_in sin;
	struct icmp *icmp;
	char *pkt;
	unsigned int pktsize;
	socklen_t slen;
	int s;
	bool rv = false;

	if (wirelen < ETHER_HDR_LEN + sizeof(struct ip))
		return false;

	pktsize = wirelen - ETHER_HDR_LEN - sizeof(struct ip);
	if (pktsize < sizeof(struct icmp) + 7)
		return false;

	s = rump_sys_socket(PF_INET, SOCK_RAW, IPPROTO_ICMP);
	if (s == -1)
		return false;

	pkt = NULL;

	tv.tv_sec = 1;
	tv.tv_usec = 0;
	if (rump_sys_setsockopt(s, SOL_SOCKET, SO_RCVTIMEO,
	    &tv, sizeof(tv)) == -1)
		goto out;

	memset(&sin, 0, sizeof(sin));
	sin.sin_len = sizeof(sin);
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = inet_addr(dst);

	pkt = calloc(1, pktsize);
	icmp = (struct icmp *)pkt;
	if (pkt == NULL)
		goto out;

	memcpy(pkt + pktsize - 7, tail, 7);
	icmp->icmp_type = ICMP_ECHO;
	icmp->icmp_id = htons(37);
	icmp->icmp_seq = htons(1);
	icmp->icmp_cksum = in_cksum(pkt, pktsize);

	slen = sizeof(sin);
	if (rump_sys_sendto(s, pkt, pktsize, 0,
	    (struct sockaddr *)&sin, slen) == -1) {
		goto out;
	}

	if (rump_sys_recvfrom(s, pkt, pktsize, 0,
	    (struct sockaddr *)&sin, &slen) == -1)
		goto out;

	rv = true;
 out:
	if (pkt != NULL)
		free(pkt);
	rump_sys_close(s);
	return rv;
}

static void
magic_ping_test(const char *name, unsigned int wirelen)
{
	struct bpf_program prog;
	struct bpf_stat bstat;
	struct ifreq ifr;
	struct timeval tv;
	unsigned int bufsize;
	bool pinged;
	ssize_t n;
	char *buf;
	pid_t child;
	int bpfd;
	char token;
	int channel[2];

	struct bpf_hdr *hdr;

	RL(pipe(channel));

	prog.bf_len = __arraycount(magic_echo_reply_prog);
	prog.bf_insns = magic_echo_reply_prog;

	child = fork();
	RZ(rump_init());
	netcfg_rump_makeshmif(name, ifr.ifr_name);

	switch (child) {
	case -1:
		atf_tc_fail_errno("fork failed");
	case 0:
		netcfg_rump_if(ifr.ifr_name, "10.1.1.10", "255.0.0.0");
		close(channel[0]);
		ATF_CHECK(write(channel[1], "U", 1) == 1);
		close(channel[1]);
		pause();
		return;
	default:
		break;
	}

	netcfg_rump_if(ifr.ifr_name, "10.1.1.20", "255.0.0.0");

	RL(bpfd = rump_sys_open("/dev/bpf", O_RDONLY));

	tv.tv_sec = 0;
	tv.tv_usec = 500;
	RL(rump_sys_ioctl(bpfd, BIOCSRTIMEOUT, &tv));

	RL(rump_sys_ioctl(bpfd, BIOCGBLEN, &bufsize));
	RL(rump_sys_ioctl(bpfd, BIOCSETF, &prog));
	RL(rump_sys_ioctl(bpfd, BIOCSETIF, &ifr));

	close(channel[1]);
	ATF_CHECK(read(channel[0], &token, 1) == 1 && token == 'U');

	pinged = pingtest("10.1.1.10", wirelen, magic_echo_reply_tail);
	ATF_CHECK(pinged);

	buf = malloc(bufsize);
	hdr = (struct bpf_hdr *)buf;
	ATF_REQUIRE(buf != NULL);
	ATF_REQUIRE(bufsize > sizeof(struct bpf_hdr));

	n = rump_sys_read(bpfd, buf, bufsize);

	ATF_CHECK(n > (int)sizeof(struct bpf_hdr));
	ATF_CHECK(hdr->bh_caplen == MIN(SNAPLEN, wirelen));

	RL(rump_sys_ioctl(bpfd, BIOCGSTATS, &bstat));
	ATF_CHECK(bstat.bs_capt >= 1); /* XXX == 1 */

	rump_sys_close(bpfd);
	free(buf);

	close(channel[0]);

	kill(child, SIGKILL);
}

static int
send_bpf_prog(const char *ifname, struct bpf_program *prog)
{
	struct ifreq ifr;
	int bpfd, e, rv;

	RZ(rump_init());
	netcfg_rump_makeshmif(ifname, ifr.ifr_name);
	netcfg_rump_if(ifr.ifr_name, "10.1.1.20", "255.0.0.0");

	RL(bpfd = rump_sys_open("/dev/bpf", O_RDONLY));

	rv = rump_sys_ioctl(bpfd, BIOCSETF, prog);
	e = errno;

	rump_sys_close(bpfd);
	errno = e;

	return rv;
}

ATF_TC(bpfiltercontig);
ATF_TC_HEAD(bpfiltercontig, tc)
{

	atf_tc_set_md_var(tc, "descr", "Checks that bpf program "
	    "can read bytes from contiguous buffer.");
	atf_tc_set_md_var(tc, "timeout", "30");
}

ATF_TC_BODY(bpfiltercontig, tc)
{

	magic_ping_test("bpfiltercontig", 128);
}


ATF_TC(bpfiltermchain);
ATF_TC_HEAD(bpfiltermchain, tc)
{

	atf_tc_set_md_var(tc, "descr", "Checks that bpf program "
	    "can read bytes from mbuf chain.");
	atf_tc_set_md_var(tc, "timeout", "30");
}

ATF_TC_BODY(bpfiltermchain, tc)
{

	magic_ping_test("bpfiltermchain", MINCLSIZE + 1);
}


ATF_TC(bpfilterbadmem);
ATF_TC_HEAD(bpfilterbadmem, tc)
{

	atf_tc_set_md_var(tc, "descr", "Checks that bpf program that "
	    "doesn't initialize memomy store is rejected by the kernel");
	atf_tc_set_md_var(tc, "timeout", "30");
}

ATF_TC_BODY(bpfilterbadmem, tc)
{
	struct bpf_program prog;

	prog.bf_len = __arraycount(badmem_prog);
	prog.bf_insns = badmem_prog;
	ATF_CHECK_ERRNO(EINVAL, send_bpf_prog("bpfilterbadmem", &prog) == -1);
}

ATF_TC(bpfilternoinitA);
ATF_TC_HEAD(bpfilternoinitA, tc)
{

	atf_tc_set_md_var(tc, "descr", "Checks that bpf program that "
	    "doesn't initialize the A register is accepted by the kernel");
	atf_tc_set_md_var(tc, "timeout", "30");
}

ATF_TC_BODY(bpfilternoinitA, tc)
{
	struct bpf_program prog;

	prog.bf_len = __arraycount(noinitA_prog);
	prog.bf_insns = noinitA_prog;
	RL(send_bpf_prog("bpfilternoinitA", &prog));
}

ATF_TC(bpfilternoinitX);
ATF_TC_HEAD(bpfilternoinitX, tc)
{

	atf_tc_set_md_var(tc, "descr", "Checks that bpf program that "
	    "doesn't initialize the X register is accepted by the kernel");
	atf_tc_set_md_var(tc, "timeout", "30");
}

ATF_TC_BODY(bpfilternoinitX, tc)
{
	struct bpf_program prog;

	prog.bf_len = __arraycount(noinitX_prog);
	prog.bf_insns = noinitX_prog;
	RL(send_bpf_prog("bpfilternoinitX", &prog));
}

ATF_TC(bpfilterbadjmp);
ATF_TC_HEAD(bpfilterbadjmp, tc)
{

	atf_tc_set_md_var(tc, "descr", "Checks that bpf program that "
	    "jumps to invalid destination is rejected by the kernel");
	atf_tc_set_md_var(tc, "timeout", "30");
}

ATF_TC_BODY(bpfilterbadjmp, tc)
{
	struct bpf_program prog;

	prog.bf_len = __arraycount(badjmp_prog);
	prog.bf_insns = badjmp_prog;
	ATF_CHECK_ERRNO(EINVAL, send_bpf_prog("bpfilterbadjmp", &prog) == -1);
}

ATF_TC(bpfilternegjmp);
ATF_TC_HEAD(bpfilternegjmp, tc)
{

	atf_tc_set_md_var(tc, "descr", "Checks that bpf program that "
	    "jumps backwards is rejected by the kernel");
	atf_tc_set_md_var(tc, "timeout", "30");
}

ATF_TC_BODY(bpfilternegjmp, tc)
{
	struct bpf_program prog;

	prog.bf_len = __arraycount(negjmp_prog);
	prog.bf_insns = negjmp_prog;
	ATF_CHECK_ERRNO(EINVAL, send_bpf_prog("bpfilternegjmp", &prog) == -1);
}

ATF_TC(bpfilterbadret);
ATF_TC_HEAD(bpfilterbadret, tc)
{

	atf_tc_set_md_var(tc, "descr", "Checks that bpf program that "
	    "ends with invalid BPF_RET instruction is rejected by the kernel");
	atf_tc_set_md_var(tc, "timeout", "30");
}

ATF_TC_BODY(bpfilterbadret, tc)
{
	struct bpf_program prog;
	struct bpf_insn *last;

	prog.bf_len = __arraycount(badret_prog);
	prog.bf_insns = badret_prog;

	/*
	 * The point of this test is checking a bad instruction of
	 * a valid class and with a valid BPF_RVAL data.
	 */
	last = &prog.bf_insns[prog.bf_len - 1];
	ATF_CHECK(BPF_CLASS(last->code) == BPF_RET &&
	    (BPF_RVAL(last->code) == BPF_K || BPF_RVAL(last->code) == BPF_A));

	ATF_CHECK_ERRNO(EINVAL, send_bpf_prog("bpfilterbadret", &prog) == -1);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, bpfiltercontig);
	ATF_TP_ADD_TC(tp, bpfiltermchain);
	ATF_TP_ADD_TC(tp, bpfilterbadmem);
	ATF_TP_ADD_TC(tp, bpfilternoinitA);
	ATF_TP_ADD_TC(tp, bpfilternoinitX);
	ATF_TP_ADD_TC(tp, bpfilterbadjmp);
	ATF_TP_ADD_TC(tp, bpfilternegjmp);
	ATF_TP_ADD_TC(tp, bpfilterbadret);

	return atf_no_error();
}
