/*-
 * Copyright (c) 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from the Stanford/CMU enet packet filter,
 * (net/enet.c) distributed as part of 4.3BSD, and code contributed
 * to Berkeley by Steven McCanne and Van Jacobson both of Lawrence
 * Berkeley Laboratory.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *      @(#)bpf.h       7.1 (Berkeley) 5/7/91
 */

/*
 * This is libpcap's cut-down version of bpf.h; it includes only
 * the stuff needed for the code generator and the userland BPF
 * interpreter, and the libpcap APIs for setting filters, etc..
 *
 * "pcap-bpf.c" will include the native OS version, as it deals with
 * the OS's BPF implementation.
 *
 * At least two programs found by Google Code Search explicitly includes
 * <pcap/bpf.h> (even though <pcap.h>/<pcap/pcap.h> includes it for you),
 * so moving that stuff to <pcap/pcap.h> would break the build for some
 * programs.
 */

/*
 * If we've already included <net/bpf.h>, don't re-define this stuff.
 * We assume BSD-style multiple-include protection in <net/bpf.h>,
 * which is true of all but the oldest versions of FreeBSD and NetBSD,
 * or Tru64 UNIX-style multiple-include protection (or, at least,
 * Tru64 UNIX 5.x-style; I don't have earlier versions available to check),
 * or AIX-style multiple-include protection (or, at least, AIX 5.x-style;
 * I don't have earlier versions available to check), or QNX-style
 * multiple-include protection (as per GitHub pull request #394).
 *
 * We do not check for BPF_MAJOR_VERSION, as that's defined by
 * <linux/filter.h>, which is directly or indirectly included in some
 * programs that also include pcap.h, and <linux/filter.h> doesn't
 * define stuff we need.
 *
 * This also provides our own multiple-include protection.
 */
#if !defined(_NET_BPF_H_) && !defined(_NET_BPF_H_INCLUDED) && !defined(_BPF_H_) && !defined(_H_BPF) && !defined(lib_pcap_bpf_h)
#define lib_pcap_bpf_h

#include <pcap/funcattrs.h>

#ifdef __cplusplus
extern "C" {
#endif

/* BSD style release date */
#define BPF_RELEASE 199606

#ifdef MSDOS /* must be 32-bit */
typedef long          bpf_int32;
typedef unsigned long bpf_u_int32;
#else
typedef	int bpf_int32;
typedef	u_int bpf_u_int32;
#endif

/*
 * Alignment macros.  BPF_WORDALIGN rounds up to the next
 * even multiple of BPF_ALIGNMENT.
 *
 * Tcpdump's print-pflog.c uses this, so we define it here.
 */
#ifndef __NetBSD__
#define BPF_ALIGNMENT sizeof(bpf_int32)
#else
#define BPF_ALIGNMENT sizeof(long)
#endif
#define BPF_WORDALIGN(x) (((x)+(BPF_ALIGNMENT-1))&~(BPF_ALIGNMENT-1))

/*
 * Structure for "pcap_compile()", "pcap_setfilter()", etc..
 */
struct bpf_program {
	u_int bf_len;
	struct bpf_insn *bf_insns;
};

#include <pcap/dlt.h>

/*
 * The instruction encodings.
 *
 * Please inform tcpdump-workers@lists.tcpdump.org if you use any
 * of the reserved values, so that we can note that they're used
 * (and perhaps implement it in the reference BPF implementation
 * and encourage its implementation elsewhere).
 */

/*
 * The upper 8 bits of the opcode aren't used. BSD/OS used 0x8000.
 */

/* instruction classes */
#define BPF_CLASS(code) ((code) & 0x07)
#define		BPF_LD		0x00
#define		BPF_LDX		0x01
#define		BPF_ST		0x02
#define		BPF_STX		0x03
#define		BPF_ALU		0x04
#define		BPF_JMP		0x05
#define		BPF_RET		0x06
#define		BPF_MISC	0x07

/* ld/ldx fields */
#define BPF_SIZE(code)	((code) & 0x18)
#define		BPF_W		0x00
#define		BPF_H		0x08
#define		BPF_B		0x10
/*				0x18	reserved; used by BSD/OS */
#define BPF_MODE(code)	((code) & 0xe0)
#define		BPF_IMM 	0x00
#define		BPF_ABS		0x20
#define		BPF_IND		0x40
#define		BPF_MEM		0x60
#define		BPF_LEN		0x80
#define		BPF_MSH		0xa0
/*				0xc0	reserved; used by BSD/OS */
/*				0xe0	reserved; used by BSD/OS */

/* alu/jmp fields */
#define BPF_OP(code)	((code) & 0xf0)
#define		BPF_ADD		0x00
#define		BPF_SUB		0x10
#define		BPF_MUL		0x20
#define		BPF_DIV		0x30
#define		BPF_OR		0x40
#define		BPF_AND		0x50
#define		BPF_LSH		0x60
#define		BPF_RSH		0x70
#define		BPF_NEG		0x80
#define		BPF_MOD		0x90
#define		BPF_XOR		0xa0
/*				0xb0	reserved */
/*				0xc0	reserved */
/*				0xd0	reserved */
/*				0xe0	reserved */
/*				0xf0	reserved */

#define		BPF_JA		0x00
#define		BPF_JEQ		0x10
#define		BPF_JGT		0x20
#define		BPF_JGE		0x30
#define		BPF_JSET	0x40
/*				0x50	reserved; used on BSD/OS */
/*				0x60	reserved */
/*				0x70	reserved */
/*				0x80	reserved */
/*				0x90	reserved */
/*				0xa0	reserved */
/*				0xb0	reserved */
/*				0xc0	reserved */
/*				0xd0	reserved */
/*				0xe0	reserved */
/*				0xf0	reserved */
#define BPF_SRC(code)	((code) & 0x08)
#define		BPF_K		0x00
#define		BPF_X		0x08

/* ret - BPF_K and BPF_X also apply */
#define BPF_RVAL(code)	((code) & 0x18)
#define		BPF_A		0x10
/*				0x18	reserved */

/* misc */
#define BPF_MISCOP(code) ((code) & 0xf8)
#define		BPF_TAX		0x00
/*				0x08	reserved */
/*				0x10	reserved */
/*				0x18	reserved */
/* #define	BPF_COP		0x20	NetBSD "coprocessor" extensions */
/*				0x28	reserved */
/*				0x30	reserved */
/*				0x38	reserved */
/* #define	BPF_COPX	0x40	NetBSD "coprocessor" extensions */
/*					also used on BSD/OS */
/*				0x48	reserved */
/*				0x50	reserved */
/*				0x58	reserved */
/*				0x60	reserved */
/*				0x68	reserved */
/*				0x70	reserved */
/*				0x78	reserved */
#define		BPF_TXA		0x80
/*				0x88	reserved */
/*				0x90	reserved */
/*				0x98	reserved */
/*				0xa0	reserved */
/*				0xa8	reserved */
/*				0xb0	reserved */
/*				0xb8	reserved */
/*				0xc0	reserved; used on BSD/OS */
/*				0xc8	reserved */
/*				0xd0	reserved */
/*				0xd8	reserved */
/*				0xe0	reserved */
/*				0xe8	reserved */
/*				0xf0	reserved */
/*				0xf8	reserved */

/*
 * The instruction data structure.
 */
struct bpf_insn {
	u_short	code;
	u_char 	jt;
	u_char 	jf;
	bpf_u_int32 k;
};

/*
 * Auxiliary data, for use when interpreting a filter intended for the
 * Linux kernel when the kernel rejects the filter (requiring us to
 * run it in userland).  It contains VLAN tag information.
 */
struct bpf_aux_data {
	u_short vlan_tag_present;
	u_short vlan_tag;
};

/*
 * Macros for insn array initializers.
 */
#define BPF_STMT(code, k) { (u_short)(code), 0, 0, k }
#define BPF_JUMP(code, k, jt, jf) { (u_short)(code), jt, jf, k }

PCAP_API int bpf_validate(const struct bpf_insn *, int);
PCAP_API u_int bpf_filter(const struct bpf_insn *, const u_char *, u_int, u_int);
extern u_int bpf_filter_with_aux_data(const struct bpf_insn *, const u_char *, u_int, u_int, const struct bpf_aux_data *);

/*
 * Number of scratch memory words (for BPF_LD|BPF_MEM and BPF_ST).
 */
#define BPF_MEMWORDS 16

#ifdef __cplusplus
}
#endif

#endif /* !defined(_NET_BPF_H_) && !defined(_BPF_H_) && !defined(_H_BPF) && !defined(lib_pcap_bpf_h) */
