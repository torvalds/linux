/* SPDX-License-Identifier: GPL-2.0 or MIT */
#ifndef _ASM_X86_VMWARE_H
#define _ASM_X86_VMWARE_H

#include <asm/cpufeatures.h>
#include <asm/alternative.h>
#include <linux/stringify.h>

/*
 * VMware hypercall ABI.
 *
 * - Low bandwidth (LB) hypercalls (I/O port based, vmcall and vmmcall)
 * have up to 6 input and 6 output arguments passed and returned using
 * registers: %eax (arg0), %ebx (arg1), %ecx (arg2), %edx (arg3),
 * %esi (arg4), %edi (arg5).
 * The following input arguments must be initialized by the caller:
 * arg0 - VMWARE_HYPERVISOR_MAGIC
 * arg2 - Hypercall command
 * arg3 bits [15:0] - Port number, LB and direction flags
 *
 * - Low bandwidth TDX hypercalls (x86_64 only) are similar to LB
 * hypercalls. They also have up to 6 input and 6 output on registers
 * arguments, with different argument to register mapping:
 * %r12 (arg0), %rbx (arg1), %r13 (arg2), %rdx (arg3),
 * %rsi (arg4), %rdi (arg5).
 *
 * - High bandwidth (HB) hypercalls are I/O port based only. They have
 * up to 7 input and 7 output arguments passed and returned using
 * registers: %eax (arg0), %ebx (arg1), %ecx (arg2), %edx (arg3),
 * %esi (arg4), %edi (arg5), %ebp (arg6).
 * The following input arguments must be initialized by the caller:
 * arg0 - VMWARE_HYPERVISOR_MAGIC
 * arg1 - Hypercall command
 * arg3 bits [15:0] - Port number, HB and direction flags
 *
 * For compatibility purposes, x86_64 systems use only lower 32 bits
 * for input and output arguments.
 *
 * The hypercall definitions differ in the low word of the %edx (arg3)
 * in the following way: the old I/O port based interface uses the port
 * number to distinguish between high- and low bandwidth versions, and
 * uses IN/OUT instructions to define transfer direction.
 *
 * The new vmcall interface instead uses a set of flags to select
 * bandwidth mode and transfer direction. The flags should be loaded
 * into arg3 by any user and are automatically replaced by the port
 * number if the I/O port method is used.
 */

#define VMWARE_HYPERVISOR_HB		BIT(0)
#define VMWARE_HYPERVISOR_OUT		BIT(1)

#define VMWARE_HYPERVISOR_PORT		0x5658
#define VMWARE_HYPERVISOR_PORT_HB	(VMWARE_HYPERVISOR_PORT | \
					 VMWARE_HYPERVISOR_HB)

#define VMWARE_HYPERVISOR_MAGIC		0x564d5868U

#define VMWARE_CMD_GETVERSION		10
#define VMWARE_CMD_GETHZ		45
#define VMWARE_CMD_GETVCPU_INFO		68
#define VMWARE_CMD_STEALCLOCK		91
/*
 * Hypercall command mask:
 *   bits [6:0] command, range [0, 127]
 *   bits [19:16] sub-command, range [0, 15]
 */
#define VMWARE_CMD_MASK			0xf007fU

#define CPUID_VMWARE_FEATURES_ECX_VMMCALL	BIT(0)
#define CPUID_VMWARE_FEATURES_ECX_VMCALL	BIT(1)

extern unsigned long vmware_hypercall_slow(unsigned long cmd,
					   unsigned long in1, unsigned long in3,
					   unsigned long in4, unsigned long in5,
					   u32 *out1, u32 *out2, u32 *out3,
					   u32 *out4, u32 *out5);

#define VMWARE_TDX_VENDOR_LEAF 0x1af7e4909ULL
#define VMWARE_TDX_HCALL_FUNC  1

extern unsigned long vmware_tdx_hypercall(unsigned long cmd,
					  unsigned long in1, unsigned long in3,
					  unsigned long in4, unsigned long in5,
					  u32 *out1, u32 *out2, u32 *out3,
					  u32 *out4, u32 *out5);

/*
 * The low bandwidth call. The low word of %edx is presumed to have OUT bit
 * set. The high word of %edx may contain input data from the caller.
 */
#define VMWARE_HYPERCALL					\
	ALTERNATIVE_2("movw %[port], %%dx\n\t"			\
		      "inl (%%dx), %%eax",			\
		      "vmcall", X86_FEATURE_VMCALL,		\
		      "vmmcall", X86_FEATURE_VMW_VMMCALL)

static inline
unsigned long vmware_hypercall1(unsigned long cmd, unsigned long in1)
{
	unsigned long out0;

	if (cpu_feature_enabled(X86_FEATURE_TDX_GUEST))
		return vmware_tdx_hypercall(cmd, in1, 0, 0, 0,
					    NULL, NULL, NULL, NULL, NULL);

	if (unlikely(!alternatives_patched) && !__is_defined(MODULE))
		return vmware_hypercall_slow(cmd, in1, 0, 0, 0,
					     NULL, NULL, NULL, NULL, NULL);

	asm_inline volatile (VMWARE_HYPERCALL
		: "=a" (out0)
		: [port] "i" (VMWARE_HYPERVISOR_PORT),
		  "a" (VMWARE_HYPERVISOR_MAGIC),
		  "b" (in1),
		  "c" (cmd),
		  "d" (0)
		: "cc", "memory");
	return out0;
}

static inline
unsigned long vmware_hypercall3(unsigned long cmd, unsigned long in1,
				u32 *out1, u32 *out2)
{
	unsigned long out0;

	if (cpu_feature_enabled(X86_FEATURE_TDX_GUEST))
		return vmware_tdx_hypercall(cmd, in1, 0, 0, 0,
					    out1, out2, NULL, NULL, NULL);

	if (unlikely(!alternatives_patched) && !__is_defined(MODULE))
		return vmware_hypercall_slow(cmd, in1, 0, 0, 0,
					     out1, out2, NULL, NULL, NULL);

	asm_inline volatile (VMWARE_HYPERCALL
		: "=a" (out0), "=b" (*out1), "=c" (*out2)
		: [port] "i" (VMWARE_HYPERVISOR_PORT),
		  "a" (VMWARE_HYPERVISOR_MAGIC),
		  "b" (in1),
		  "c" (cmd),
		  "d" (0)
		: "cc", "memory");
	return out0;
}

static inline
unsigned long vmware_hypercall4(unsigned long cmd, unsigned long in1,
				u32 *out1, u32 *out2, u32 *out3)
{
	unsigned long out0;

	if (cpu_feature_enabled(X86_FEATURE_TDX_GUEST))
		return vmware_tdx_hypercall(cmd, in1, 0, 0, 0,
					    out1, out2, out3, NULL, NULL);

	if (unlikely(!alternatives_patched) && !__is_defined(MODULE))
		return vmware_hypercall_slow(cmd, in1, 0, 0, 0,
					     out1, out2, out3, NULL, NULL);

	asm_inline volatile (VMWARE_HYPERCALL
		: "=a" (out0), "=b" (*out1), "=c" (*out2), "=d" (*out3)
		: [port] "i" (VMWARE_HYPERVISOR_PORT),
		  "a" (VMWARE_HYPERVISOR_MAGIC),
		  "b" (in1),
		  "c" (cmd),
		  "d" (0)
		: "cc", "memory");
	return out0;
}

static inline
unsigned long vmware_hypercall5(unsigned long cmd, unsigned long in1,
				unsigned long in3, unsigned long in4,
				unsigned long in5, u32 *out2)
{
	unsigned long out0;

	if (cpu_feature_enabled(X86_FEATURE_TDX_GUEST))
		return vmware_tdx_hypercall(cmd, in1, in3, in4, in5,
					    NULL, out2, NULL, NULL, NULL);

	if (unlikely(!alternatives_patched) && !__is_defined(MODULE))
		return vmware_hypercall_slow(cmd, in1, in3, in4, in5,
					     NULL, out2, NULL, NULL, NULL);

	asm_inline volatile (VMWARE_HYPERCALL
		: "=a" (out0), "=c" (*out2)
		: [port] "i" (VMWARE_HYPERVISOR_PORT),
		  "a" (VMWARE_HYPERVISOR_MAGIC),
		  "b" (in1),
		  "c" (cmd),
		  "d" (in3),
		  "S" (in4),
		  "D" (in5)
		: "cc", "memory");
	return out0;
}

static inline
unsigned long vmware_hypercall6(unsigned long cmd, unsigned long in1,
				unsigned long in3, u32 *out2,
				u32 *out3, u32 *out4, u32 *out5)
{
	unsigned long out0;

	if (cpu_feature_enabled(X86_FEATURE_TDX_GUEST))
		return vmware_tdx_hypercall(cmd, in1, in3, 0, 0,
					    NULL, out2, out3, out4, out5);

	if (unlikely(!alternatives_patched) && !__is_defined(MODULE))
		return vmware_hypercall_slow(cmd, in1, in3, 0, 0,
					     NULL, out2, out3, out4, out5);

	asm_inline volatile (VMWARE_HYPERCALL
		: "=a" (out0), "=c" (*out2), "=d" (*out3), "=S" (*out4),
		  "=D" (*out5)
		: [port] "i" (VMWARE_HYPERVISOR_PORT),
		  "a" (VMWARE_HYPERVISOR_MAGIC),
		  "b" (in1),
		  "c" (cmd),
		  "d" (in3)
		: "cc", "memory");
	return out0;
}

static inline
unsigned long vmware_hypercall7(unsigned long cmd, unsigned long in1,
				unsigned long in3, unsigned long in4,
				unsigned long in5, u32 *out1,
				u32 *out2, u32 *out3)
{
	unsigned long out0;

	if (cpu_feature_enabled(X86_FEATURE_TDX_GUEST))
		return vmware_tdx_hypercall(cmd, in1, in3, in4, in5,
					    out1, out2, out3, NULL, NULL);

	if (unlikely(!alternatives_patched) && !__is_defined(MODULE))
		return vmware_hypercall_slow(cmd, in1, in3, in4, in5,
					     out1, out2, out3, NULL, NULL);

	asm_inline volatile (VMWARE_HYPERCALL
		: "=a" (out0), "=b" (*out1), "=c" (*out2), "=d" (*out3)
		: [port] "i" (VMWARE_HYPERVISOR_PORT),
		  "a" (VMWARE_HYPERVISOR_MAGIC),
		  "b" (in1),
		  "c" (cmd),
		  "d" (in3),
		  "S" (in4),
		  "D" (in5)
		: "cc", "memory");
	return out0;
}

#ifdef CONFIG_X86_64
#define VMW_BP_CONSTRAINT "r"
#else
#define VMW_BP_CONSTRAINT "m"
#endif

/*
 * High bandwidth calls are not supported on encrypted memory guests.
 * The caller should check cc_platform_has(CC_ATTR_MEM_ENCRYPT) and use
 * low bandwidth hypercall if memory encryption is set.
 * This assumption simplifies HB hypercall implementation to just I/O port
 * based approach without alternative patching.
 */
static inline
unsigned long vmware_hypercall_hb_out(unsigned long cmd, unsigned long in2,
				      unsigned long in3, unsigned long in4,
				      unsigned long in5, unsigned long in6,
				      u32 *out1)
{
	unsigned long out0;

	asm_inline volatile (
		UNWIND_HINT_SAVE
		"push %%" _ASM_BP "\n\t"
		UNWIND_HINT_UNDEFINED
		"mov %[in6], %%" _ASM_BP "\n\t"
		"rep outsb\n\t"
		"pop %%" _ASM_BP "\n\t"
		UNWIND_HINT_RESTORE
		: "=a" (out0), "=b" (*out1)
		: "a" (VMWARE_HYPERVISOR_MAGIC),
		  "b" (cmd),
		  "c" (in2),
		  "d" (in3 | VMWARE_HYPERVISOR_PORT_HB),
		  "S" (in4),
		  "D" (in5),
		  [in6] VMW_BP_CONSTRAINT (in6)
		: "cc", "memory");
	return out0;
}

static inline
unsigned long vmware_hypercall_hb_in(unsigned long cmd, unsigned long in2,
				     unsigned long in3, unsigned long in4,
				     unsigned long in5, unsigned long in6,
				     u32 *out1)
{
	unsigned long out0;

	asm_inline volatile (
		UNWIND_HINT_SAVE
		"push %%" _ASM_BP "\n\t"
		UNWIND_HINT_UNDEFINED
		"mov %[in6], %%" _ASM_BP "\n\t"
		"rep insb\n\t"
		"pop %%" _ASM_BP "\n\t"
		UNWIND_HINT_RESTORE
		: "=a" (out0), "=b" (*out1)
		: "a" (VMWARE_HYPERVISOR_MAGIC),
		  "b" (cmd),
		  "c" (in2),
		  "d" (in3 | VMWARE_HYPERVISOR_PORT_HB),
		  "S" (in4),
		  "D" (in5),
		  [in6] VMW_BP_CONSTRAINT (in6)
		: "cc", "memory");
	return out0;
}
#undef VMW_BP_CONSTRAINT
#undef VMWARE_HYPERCALL

#endif
