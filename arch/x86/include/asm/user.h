#ifndef _ASM_X86_USER_H
#define _ASM_X86_USER_H

#ifdef CONFIG_X86_32
# include <asm/user_32.h>
#else
# include <asm/user_64.h>
#endif

#include <asm/types.h>

struct user_ymmh_regs {
	/* 16 * 16 bytes for each YMMH-reg */
	__u32 ymmh_space[64];
};

struct user_xstate_header {
	__u64 xfeatures;
	__u64 reserved1[2];
	__u64 reserved2[5];
};

/*
 * The structure layout of user_xstateregs, used for exporting the
 * extended register state through ptrace and core-dump (NT_X86_XSTATE note)
 * interfaces will be same as the memory layout of xsave used by the processor
 * (except for the bytes 464..511, which can be used by the software) and hence
 * the size of this structure varies depending on the features supported by the
 * processor and OS. The size of the structure that users need to use can be
 * obtained by doing:
 *     cpuid_count(0xd, 0, &eax, &ptrace_xstateregs_struct_size, &ecx, &edx);
 * i.e., cpuid.(eax=0xd,ecx=0).ebx will be the size that user (debuggers, etc.)
 * need to use.
 *
 * For now, only the first 8 bytes of the software usable bytes[464..471] will
 * be used and will be set to OS enabled xstate mask (which is same as the
 * 64bit mask returned by the xgetbv's xCR0).  Users (analyzing core dump
 * remotely, etc.) can use this mask as well as the mask saved in the
 * xstate_hdr bytes and interpret what states the processor/OS supports
 * and what states are in modified/initialized conditions for the
 * particular process/thread.
 *
 * Also when the user modifies certain state FP/SSE/etc through the
 * ptrace interface, they must ensure that the header.xfeatures
 * bytes[512..519] of the memory layout are updated correspondingly.
 * i.e., for example when FP state is modified to a non-init state,
 * header.xfeatures's bit 0 must be set to '1', when SSE is modified to
 * non-init state, header.xfeatures's bit 1 must to be set to '1', etc.
 */
#define USER_XSTATE_FX_SW_WORDS 6
#define USER_XSTATE_XCR0_WORD	0

struct user_xstateregs {
	struct {
		__u64 fpx_space[58];
		__u64 xstate_fx_sw[USER_XSTATE_FX_SW_WORDS];
	} i387;
	struct user_xstate_header header;
	struct user_ymmh_regs ymmh;
	/* further processor state extensions go here */
};

#endif /* _ASM_X86_USER_H */
