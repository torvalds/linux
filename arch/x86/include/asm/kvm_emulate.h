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

#include <asm/desc_defs.h>

struct x86_emulate_ctxt;
enum x86_intercept;
enum x86_intercept_stage;

struct x86_exception {
	u8 vector;
	bool error_code_valid;
	u16 error_code;
	bool nested_page_fault;
	u64 address; /* cr2 or nested page fault gpa */
};

/*
 * This struct is used to carry enough information from the instruction
 * decoder to main KVM so that a decision can be made whether the
 * instruction needs to be intercepted or not.
 */
struct x86_instruction_info {
	u8  intercept;          /* which intercept                      */
	u8  rep_prefix;         /* rep prefix?                          */
	u8  modrm_mod;		/* mod part of modrm			*/
	u8  modrm_reg;          /* index of register used               */
	u8  modrm_rm;		/* rm part of modrm			*/
	u64 src_val;            /* value of source operand              */
	u64 dst_val;            /* value of destination operand         */
	u8  src_bytes;          /* size of source operand               */
	u8  dst_bytes;          /* size of destination operand          */
	u8  ad_bytes;           /* size of src/dst address              */
	u64 next_rip;           /* rip following the instruction        */
};

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
#define X86EMUL_RETRY_INSTR     3 /* retry the instruction for some reason */
#define X86EMUL_CMPXCHG_FAILED  4 /* cmpxchg did not see expected value */
#define X86EMUL_IO_NEEDED       5 /* IO is needed to complete emulation */
#define X86EMUL_INTERCEPTED     6 /* Intercepted by nested VMCB/VMCS */

struct x86_emulate_ops {
	/*
	 * read_gpr: read a general purpose register (rax - r15)
	 *
	 * @reg: gpr number.
	 */
	ulong (*read_gpr)(struct x86_emulate_ctxt *ctxt, unsigned reg);
	/*
	 * write_gpr: write a general purpose register (rax - r15)
	 *
	 * @reg: gpr number.
	 * @val: value to write.
	 */
	void (*write_gpr)(struct x86_emulate_ctxt *ctxt, unsigned reg, ulong val);
	/*
	 * read_std: Read bytes of standard (non-emulated/special) memory.
	 *           Used for descriptor reading.
	 *  @addr:  [IN ] Linear address from which to read.
	 *  @val:   [OUT] Value read from memory, zero-extended to 'u_long'.
	 *  @bytes: [IN ] Number of bytes to read from memory.
	 */
	int (*read_std)(struct x86_emulate_ctxt *ctxt,
			unsigned long addr, void *val,
			unsigned int bytes,
			struct x86_exception *fault);

	/*
	 * read_phys: Read bytes of standard (non-emulated/special) memory.
	 *            Used for descriptor reading.
	 *  @addr:  [IN ] Physical address from which to read.
	 *  @val:   [OUT] Value read from memory.
	 *  @bytes: [IN ] Number of bytes to read from memory.
	 */
	int (*read_phys)(struct x86_emulate_ctxt *ctxt, unsigned long addr,
			void *val, unsigned int bytes);

	/*
	 * write_std: Write bytes of standard (non-emulated/special) memory.
	 *            Used for descriptor writing.
	 *  @addr:  [IN ] Linear address to which to write.
	 *  @val:   [OUT] Value write to memory, zero-extended to 'u_long'.
	 *  @bytes: [IN ] Number of bytes to write to memory.
	 */
	int (*write_std)(struct x86_emulate_ctxt *ctxt,
			 unsigned long addr, void *val, unsigned int bytes,
			 struct x86_exception *fault);
	/*
	 * fetch: Read bytes of standard (non-emulated/special) memory.
	 *        Used for instruction fetch.
	 *  @addr:  [IN ] Linear address from which to read.
	 *  @val:   [OUT] Value read from memory, zero-extended to 'u_long'.
	 *  @bytes: [IN ] Number of bytes to read from memory.
	 */
	int (*fetch)(struct x86_emulate_ctxt *ctxt,
		     unsigned long addr, void *val, unsigned int bytes,
		     struct x86_exception *fault);

	/*
	 * read_emulated: Read bytes from emulated/special memory area.
	 *  @addr:  [IN ] Linear address from which to read.
	 *  @val:   [OUT] Value read from memory, zero-extended to 'u_long'.
	 *  @bytes: [IN ] Number of bytes to read from memory.
	 */
	int (*read_emulated)(struct x86_emulate_ctxt *ctxt,
			     unsigned long addr, void *val, unsigned int bytes,
			     struct x86_exception *fault);

	/*
	 * write_emulated: Write bytes to emulated/special memory area.
	 *  @addr:  [IN ] Linear address to which to write.
	 *  @val:   [IN ] Value to write to memory (low-order bytes used as
	 *                required).
	 *  @bytes: [IN ] Number of bytes to write to memory.
	 */
	int (*write_emulated)(struct x86_emulate_ctxt *ctxt,
			      unsigned long addr, const void *val,
			      unsigned int bytes,
			      struct x86_exception *fault);

	/*
	 * cmpxchg_emulated: Emulate an atomic (LOCKed) CMPXCHG operation on an
	 *                   emulated/special memory area.
	 *  @addr:  [IN ] Linear address to access.
	 *  @old:   [IN ] Value expected to be current at @addr.
	 *  @new:   [IN ] Value to write to @addr.
	 *  @bytes: [IN ] Number of bytes to access using CMPXCHG.
	 */
	int (*cmpxchg_emulated)(struct x86_emulate_ctxt *ctxt,
				unsigned long addr,
				const void *old,
				const void *new,
				unsigned int bytes,
				struct x86_exception *fault);
	void (*invlpg)(struct x86_emulate_ctxt *ctxt, ulong addr);

	int (*pio_in_emulated)(struct x86_emulate_ctxt *ctxt,
			       int size, unsigned short port, void *val,
			       unsigned int count);

	int (*pio_out_emulated)(struct x86_emulate_ctxt *ctxt,
				int size, unsigned short port, const void *val,
				unsigned int count);

	bool (*get_segment)(struct x86_emulate_ctxt *ctxt, u16 *selector,
			    struct desc_struct *desc, u32 *base3, int seg);
	void (*set_segment)(struct x86_emulate_ctxt *ctxt, u16 selector,
			    struct desc_struct *desc, u32 base3, int seg);
	unsigned long (*get_cached_segment_base)(struct x86_emulate_ctxt *ctxt,
						 int seg);
	void (*get_gdt)(struct x86_emulate_ctxt *ctxt, struct desc_ptr *dt);
	void (*get_idt)(struct x86_emulate_ctxt *ctxt, struct desc_ptr *dt);
	void (*set_gdt)(struct x86_emulate_ctxt *ctxt, struct desc_ptr *dt);
	void (*set_idt)(struct x86_emulate_ctxt *ctxt, struct desc_ptr *dt);
	ulong (*get_cr)(struct x86_emulate_ctxt *ctxt, int cr);
	int (*set_cr)(struct x86_emulate_ctxt *ctxt, int cr, ulong val);
	int (*cpl)(struct x86_emulate_ctxt *ctxt);
	int (*get_dr)(struct x86_emulate_ctxt *ctxt, int dr, ulong *dest);
	int (*set_dr)(struct x86_emulate_ctxt *ctxt, int dr, ulong value);
	u64 (*get_smbase)(struct x86_emulate_ctxt *ctxt);
	void (*set_smbase)(struct x86_emulate_ctxt *ctxt, u64 smbase);
	int (*set_msr)(struct x86_emulate_ctxt *ctxt, u32 msr_index, u64 data);
	int (*get_msr)(struct x86_emulate_ctxt *ctxt, u32 msr_index, u64 *pdata);
	int (*check_pmc)(struct x86_emulate_ctxt *ctxt, u32 pmc);
	int (*read_pmc)(struct x86_emulate_ctxt *ctxt, u32 pmc, u64 *pdata);
	void (*halt)(struct x86_emulate_ctxt *ctxt);
	void (*wbinvd)(struct x86_emulate_ctxt *ctxt);
	int (*fix_hypercall)(struct x86_emulate_ctxt *ctxt);
	void (*get_fpu)(struct x86_emulate_ctxt *ctxt); /* disables preempt */
	void (*put_fpu)(struct x86_emulate_ctxt *ctxt); /* reenables preempt */
	int (*intercept)(struct x86_emulate_ctxt *ctxt,
			 struct x86_instruction_info *info,
			 enum x86_intercept_stage stage);

	void (*get_cpuid)(struct x86_emulate_ctxt *ctxt,
			  u32 *eax, u32 *ebx, u32 *ecx, u32 *edx);
	void (*set_nmi_mask)(struct x86_emulate_ctxt *ctxt, bool masked);

	unsigned (*get_hflags)(struct x86_emulate_ctxt *ctxt);
	void (*set_hflags)(struct x86_emulate_ctxt *ctxt, unsigned hflags);
};

typedef u32 __attribute__((vector_size(16))) sse128_t;

/* Type, address-of, and value of an instruction's operand. */
struct operand {
	enum { OP_REG, OP_MEM, OP_MEM_STR, OP_IMM, OP_XMM, OP_MM, OP_NONE } type;
	unsigned int bytes;
	unsigned int count;
	union {
		unsigned long orig_val;
		u64 orig_val64;
	};
	union {
		unsigned long *reg;
		struct segmented_address {
			ulong ea;
			unsigned seg;
		} mem;
		unsigned xmm;
		unsigned mm;
	} addr;
	union {
		unsigned long val;
		u64 val64;
		char valptr[sizeof(sse128_t)];
		sse128_t vec_val;
		u64 mm_val;
		void *data;
	};
};

struct fetch_cache {
	u8 data[15];
	u8 *ptr;
	u8 *end;
};

struct read_cache {
	u8 data[1024];
	unsigned long pos;
	unsigned long end;
};

/* Execution mode, passed to the emulator. */
enum x86emul_mode {
	X86EMUL_MODE_REAL,	/* Real mode.             */
	X86EMUL_MODE_VM86,	/* Virtual 8086 mode.     */
	X86EMUL_MODE_PROT16,	/* 16-bit protected mode. */
	X86EMUL_MODE_PROT32,	/* 32-bit protected mode. */
	X86EMUL_MODE_PROT64,	/* 64-bit (long) mode.    */
};

/* These match some of the HF_* flags defined in kvm_host.h  */
#define X86EMUL_GUEST_MASK           (1 << 5) /* VCPU is in guest-mode */
#define X86EMUL_SMM_MASK             (1 << 6)
#define X86EMUL_SMM_INSIDE_NMI_MASK  (1 << 7)

struct x86_emulate_ctxt {
	const struct x86_emulate_ops *ops;

	/* Register state before/after emulation. */
	unsigned long eflags;
	unsigned long eip; /* eip before instruction emulation */
	/* Emulated execution mode, represented by an X86EMUL_MODE value. */
	enum x86emul_mode mode;

	/* interruptibility state, as a result of execution of STI or MOV SS */
	int interruptibility;

	bool perm_ok; /* do not check permissions if true */
	bool ud;	/* inject an #UD if host doesn't support insn */
	bool tf;	/* TF value before instruction (after for syscall/sysret) */

	bool have_exception;
	struct x86_exception exception;

	/*
	 * decode cache
	 */

	/* current opcode length in bytes */
	u8 opcode_len;
	u8 b;
	u8 intercept;
	u8 op_bytes;
	u8 ad_bytes;
	struct operand src;
	struct operand src2;
	struct operand dst;
	int (*execute)(struct x86_emulate_ctxt *ctxt);
	int (*check_perm)(struct x86_emulate_ctxt *ctxt);
	/*
	 * The following six fields are cleared together,
	 * the rest are initialized unconditionally in x86_decode_insn
	 * or elsewhere
	 */
	bool rip_relative;
	u8 rex_prefix;
	u8 lock_prefix;
	u8 rep_prefix;
	/* bitmaps of registers in _regs[] that can be read */
	u32 regs_valid;
	/* bitmaps of registers in _regs[] that have been written */
	u32 regs_dirty;
	/* modrm */
	u8 modrm;
	u8 modrm_mod;
	u8 modrm_reg;
	u8 modrm_rm;
	u8 modrm_seg;
	u8 seg_override;
	u64 d;
	unsigned long _eip;
	struct operand memop;
	/* Fields above regs are cleared together. */
	unsigned long _regs[NR_VCPU_REGS];
	struct operand *memopp;
	struct fetch_cache fetch;
	struct read_cache io_read;
	struct read_cache mem_read;
};

/* Repeat String Operation Prefix */
#define REPE_PREFIX	0xf3
#define REPNE_PREFIX	0xf2

/* CPUID vendors */
#define X86EMUL_CPUID_VENDOR_AuthenticAMD_ebx 0x68747541
#define X86EMUL_CPUID_VENDOR_AuthenticAMD_ecx 0x444d4163
#define X86EMUL_CPUID_VENDOR_AuthenticAMD_edx 0x69746e65

#define X86EMUL_CPUID_VENDOR_AMDisbetterI_ebx 0x69444d41
#define X86EMUL_CPUID_VENDOR_AMDisbetterI_ecx 0x21726574
#define X86EMUL_CPUID_VENDOR_AMDisbetterI_edx 0x74656273

#define X86EMUL_CPUID_VENDOR_GenuineIntel_ebx 0x756e6547
#define X86EMUL_CPUID_VENDOR_GenuineIntel_ecx 0x6c65746e
#define X86EMUL_CPUID_VENDOR_GenuineIntel_edx 0x49656e69

enum x86_intercept_stage {
	X86_ICTP_NONE = 0,   /* Allow zero-init to not match anything */
	X86_ICPT_PRE_EXCEPT,
	X86_ICPT_POST_EXCEPT,
	X86_ICPT_POST_MEMACCESS,
};

enum x86_intercept {
	x86_intercept_none,
	x86_intercept_cr_read,
	x86_intercept_cr_write,
	x86_intercept_clts,
	x86_intercept_lmsw,
	x86_intercept_smsw,
	x86_intercept_dr_read,
	x86_intercept_dr_write,
	x86_intercept_lidt,
	x86_intercept_sidt,
	x86_intercept_lgdt,
	x86_intercept_sgdt,
	x86_intercept_lldt,
	x86_intercept_sldt,
	x86_intercept_ltr,
	x86_intercept_str,
	x86_intercept_rdtsc,
	x86_intercept_rdpmc,
	x86_intercept_pushf,
	x86_intercept_popf,
	x86_intercept_cpuid,
	x86_intercept_rsm,
	x86_intercept_iret,
	x86_intercept_intn,
	x86_intercept_invd,
	x86_intercept_pause,
	x86_intercept_hlt,
	x86_intercept_invlpg,
	x86_intercept_invlpga,
	x86_intercept_vmrun,
	x86_intercept_vmload,
	x86_intercept_vmsave,
	x86_intercept_vmmcall,
	x86_intercept_stgi,
	x86_intercept_clgi,
	x86_intercept_skinit,
	x86_intercept_rdtscp,
	x86_intercept_icebp,
	x86_intercept_wbinvd,
	x86_intercept_monitor,
	x86_intercept_mwait,
	x86_intercept_rdmsr,
	x86_intercept_wrmsr,
	x86_intercept_in,
	x86_intercept_ins,
	x86_intercept_out,
	x86_intercept_outs,

	nr_x86_intercepts
};

/* Host execution mode. */
#if defined(CONFIG_X86_32)
#define X86EMUL_MODE_HOST X86EMUL_MODE_PROT32
#elif defined(CONFIG_X86_64)
#define X86EMUL_MODE_HOST X86EMUL_MODE_PROT64
#endif

int x86_decode_insn(struct x86_emulate_ctxt *ctxt, void *insn, int insn_len);
bool x86_page_table_writing_insn(struct x86_emulate_ctxt *ctxt);
#define EMULATION_FAILED -1
#define EMULATION_OK 0
#define EMULATION_RESTART 1
#define EMULATION_INTERCEPTED 2
void init_decode_cache(struct x86_emulate_ctxt *ctxt);
int x86_emulate_insn(struct x86_emulate_ctxt *ctxt);
int emulator_task_switch(struct x86_emulate_ctxt *ctxt,
			 u16 tss_selector, int idt_index, int reason,
			 bool has_error_code, u32 error_code);
int emulate_int_real(struct x86_emulate_ctxt *ctxt, int irq);
void emulator_invalidate_register_cache(struct x86_emulate_ctxt *ctxt);
void emulator_writeback_register_cache(struct x86_emulate_ctxt *ctxt);
bool emulator_can_use_gpa(struct x86_emulate_ctxt *ctxt);

#endif /* _ASM_X86_KVM_X86_EMULATE_H */
