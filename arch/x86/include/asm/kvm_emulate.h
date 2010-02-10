/******************************************************************************
 * x86_emulate.h
 *
 * Generic x86 (32-bit and 64-bit) instruction decoder and emulator.
 *
 * Copyright (c) 2005 Keir Fraser
 *
 * From: xen-unstable 10676:af9809f51f81a3c43f276f00c81a52ef558afda4
 */

#ifndef _ASM_X86_KVM_X86_EMULATE_H
#define _ASM_X86_KVM_X86_EMULATE_H

struct x86_emulate_ctxt;

/*
 * x86_emulate_ops:
 *
 * These operations represent the instruction emulator's interface to memory.
 * There are two categories of operation: those that act on ordinary memory
 * regions (*_std), and those that act on memory regions known to require
 * special treatment or emulation (*_emulated).
 *
 * The emulator assumes that an instruction accesses only one 'emulated memory'
 * location, that this location is the given linear faulting address (cr2), and
 * that this is one of the instruction's data operands. Instruction fetches and
 * stack operations are assumed never to access emulated memory. The emulator
 * automatically deduces which operand of a string-move operation is accessing
 * emulated memory, and assumes that the other operand accesses normal memory.
 *
 * NOTES:
 *  1. The emulator isn't very smart about emulated vs. standard memory.
 *     'Emulated memory' access addresses should be checked for sanity.
 *     'Normal memory' accesses may fault, and the caller must arrange to
 *     detect and handle reentrancy into the emulator via recursive faults.
 *     Accesses may be unaligned and may cross page boundaries.
 *  2. If the access fails (cannot emulate, or a standard access faults) then
 *     it is up to the memop to propagate the fault to the guest VM via
 *     some out-of-band mechanism, unknown to the emulator. The memop signals
 *     failure by returning X86EMUL_PROPAGATE_FAULT to the emulator, which will
 *     then immediately bail.
 *  3. Valid access sizes are 1, 2, 4 and 8 bytes. On x86/32 systems only
 *     cmpxchg8b_emulated need support 8-byte accesses.
 *  4. The emulator cannot handle 64-bit mode emulation on an x86/32 system.
 */
/* Access completed successfully: continue emulation as normal. */
#define X86EMUL_CONTINUE        0
/* Access is unhandleable: bail from emulation and return error to caller. */
#define X86EMUL_UNHANDLEABLE    1
/* Terminate emulation but return success to the caller. */
#define X86EMUL_PROPAGATE_FAULT 2 /* propagate a generated fault to guest */
#define X86EMUL_RETRY_INSTR     2 /* retry the instruction for some reason */
#define X86EMUL_CMPXCHG_FAILED  2 /* cmpxchg did not see expected value */
struct x86_emulate_ops {
	/*
	 * read_std: Read bytes of standard (non-emulated/special) memory.
	 *           Used for instruction fetch, stack operations, and others.
	 *  @addr:  [IN ] Linear address from which to read.
	 *  @val:   [OUT] Value read from memory, zero-extended to 'u_long'.
	 *  @bytes: [IN ] Number of bytes to read from memory.
	 */
	int (*read_std)(unsigned long addr, void *val,
			unsigned int bytes, struct kvm_vcpu *vcpu);

	/*
	 * read_emulated: Read bytes from emulated/special memory area.
	 *  @addr:  [IN ] Linear address from which to read.
	 *  @val:   [OUT] Value read from memory, zero-extended to 'u_long'.
	 *  @bytes: [IN ] Number of bytes to read from memory.
	 */
	int (*read_emulated)(unsigned long addr,
			     void *val,
			     unsigned int bytes,
			     struct kvm_vcpu *vcpu);

	/*
	 * write_emulated: Write bytes to emulated/special memory area.
	 *  @addr:  [IN ] Linear address to which to write.
	 *  @val:   [IN ] Value to write to memory (low-order bytes used as
	 *                required).
	 *  @bytes: [IN ] Number of bytes to write to memory.
	 */
	int (*write_emulated)(unsigned long addr,
			      const void *val,
			      unsigned int bytes,
			      struct kvm_vcpu *vcpu);

	/*
	 * cmpxchg_emulated: Emulate an atomic (LOCKed) CMPXCHG operation on an
	 *                   emulated/special memory area.
	 *  @addr:  [IN ] Linear address to access.
	 *  @old:   [IN ] Value expected to be current at @addr.
	 *  @new:   [IN ] Value to write to @addr.
	 *  @bytes: [IN ] Number of bytes to access using CMPXCHG.
	 */
	int (*cmpxchg_emulated)(unsigned long addr,
				const void *old,
				const void *new,
				unsigned int bytes,
				struct kvm_vcpu *vcpu);

};

/* Type, address-of, and value of an instruction's operand. */
struct operand {
	enum { OP_REG, OP_MEM, OP_IMM, OP_NONE } type;
	unsigned int bytes;
	unsigned long val, orig_val, *ptr;
};

struct fetch_cache {
	u8 data[15];
	unsigned long start;
	unsigned long end;
};

struct decode_cache {
	u8 twobyte;
	u8 b;
	u8 lock_prefix;
	u8 rep_prefix;
	u8 op_bytes;
	u8 ad_bytes;
	u8 rex_prefix;
	struct operand src;
	struct operand src2;
	struct operand dst;
	bool has_seg_override;
	u8 seg_override;
	unsigned int d;
	unsigned long regs[NR_VCPU_REGS];
	unsigned long eip, eip_orig;
	/* modrm */
	u8 modrm;
	u8 modrm_mod;
	u8 modrm_reg;
	u8 modrm_rm;
	u8 use_modrm_ea;
	bool rip_relative;
	unsigned long modrm_ea;
	void *modrm_ptr;
	unsigned long modrm_val;
	struct fetch_cache fetch;
};

#define X86_SHADOW_INT_MOV_SS  1
#define X86_SHADOW_INT_STI     2

struct x86_emulate_ctxt {
	/* Register state before/after emulation. */
	struct kvm_vcpu *vcpu;

	unsigned long eflags;
	/* Emulated execution mode, represented by an X86EMUL_MODE value. */
	int mode;
	u32 cs_base;

	/* interruptibility state, as a result of execution of STI or MOV SS */
	int interruptibility;

	/* decode cache */
	struct decode_cache decode;
};

/* Repeat String Operation Prefix */
#define REPE_PREFIX	1
#define REPNE_PREFIX	2

/* Execution mode, passed to the emulator. */
#define X86EMUL_MODE_REAL     0	/* Real mode.             */
#define X86EMUL_MODE_VM86     1	/* Virtual 8086 mode.     */
#define X86EMUL_MODE_PROT16   2	/* 16-bit protected mode. */
#define X86EMUL_MODE_PROT32   4	/* 32-bit protected mode. */
#define X86EMUL_MODE_PROT64   8	/* 64-bit (long) mode.    */

/* Host execution mode. */
#if defined(CONFIG_X86_32)
#define X86EMUL_MODE_HOST X86EMUL_MODE_PROT32
#elif defined(CONFIG_X86_64)
#define X86EMUL_MODE_HOST X86EMUL_MODE_PROT64
#endif

int x86_decode_insn(struct x86_emulate_ctxt *ctxt,
		    struct x86_emulate_ops *ops);
int x86_emulate_insn(struct x86_emulate_ctxt *ctxt,
		     struct x86_emulate_ops *ops);

#endif /* _ASM_X86_KVM_X86_EMULATE_H */
