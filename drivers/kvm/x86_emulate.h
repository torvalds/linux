/******************************************************************************
 * x86_emulate.h
 *
 * Generic x86 (32-bit and 64-bit) instruction decoder and emulator.
 *
 * Copyright (c) 2005 Keir Fraser
 *
 * From: xen-unstable 10676:af9809f51f81a3c43f276f00c81a52ef558afda4
 */

#ifndef __X86_EMULATE_H__
#define __X86_EMULATE_H__

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
	 * write_std: Write bytes of standard (non-emulated/special) memory.
	 *            Used for stack operations, and others.
	 *  @addr:  [IN ] Linear address to which to write.
	 *  @val:   [IN ] Value to write to memory (low-order bytes used as
	 *                required).
	 *  @bytes: [IN ] Number of bytes to write to memory.
	 */
	int (*write_std)(unsigned long addr, const void *val,
			 unsigned int bytes, struct kvm_vcpu *vcpu);

	/*
	 * read_emulated: Read bytes from emulated/special memory area.
	 *  @addr:  [IN ] Linear address from which to read.
	 *  @val:   [OUT] Value read from memory, zero-extended to 'u_long'.
	 *  @bytes: [IN ] Number of bytes to read from memory.
	 */
	int (*read_emulated) (unsigned long addr,
			      void *val,
			      unsigned int bytes,
			      struct kvm_vcpu *vcpu);

	/*
	 * write_emulated: Read bytes from emulated/special memory area.
	 *  @addr:  [IN ] Linear address to which to write.
	 *  @val:   [IN ] Value to write to memory (low-order bytes used as
	 *                required).
	 *  @bytes: [IN ] Number of bytes to write to memory.
	 */
	int (*write_emulated) (unsigned long addr,
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
	int (*cmpxchg_emulated) (unsigned long addr,
				 const void *old,
				 const void *new,
				 unsigned int bytes,
				 struct kvm_vcpu *vcpu);

};

struct x86_emulate_ctxt {
	/* Register state before/after emulation. */
	struct kvm_vcpu *vcpu;

	/* Linear faulting address (if emulating a page-faulting instruction). */
	unsigned long eflags;
	unsigned long cr2;

	/* Emulated execution mode, represented by an X86EMUL_MODE value. */
	int mode;

	unsigned long cs_base;
	unsigned long ds_base;
	unsigned long es_base;
	unsigned long ss_base;
	unsigned long gs_base;
	unsigned long fs_base;
};

/* Execution mode, passed to the emulator. */
#define X86EMUL_MODE_REAL     0	/* Real mode.             */
#define X86EMUL_MODE_PROT16   2	/* 16-bit protected mode. */
#define X86EMUL_MODE_PROT32   4	/* 32-bit protected mode. */
#define X86EMUL_MODE_PROT64   8	/* 64-bit (long) mode.    */

/* Host execution mode. */
#if defined(__i386__)
#define X86EMUL_MODE_HOST X86EMUL_MODE_PROT32
#elif defined(CONFIG_X86_64)
#define X86EMUL_MODE_HOST X86EMUL_MODE_PROT64
#endif

/*
 * x86_emulate_memop: Emulate an instruction that faulted attempting to
 *                    read/write a 'special' memory area.
 * Returns -1 on failure, 0 on success.
 */
int x86_emulate_memop(struct x86_emulate_ctxt *ctxt,
		      struct x86_emulate_ops *ops);

#endif				/* __X86_EMULATE_H__ */
