/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ENTRY_H
#define _ENTRY_H

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>

/* irq */
void handler_irq(int irq, struct pt_regs *regs);

#ifdef CONFIG_SPARC32
/* traps */
void do_hw_interrupt(struct pt_regs *regs, unsigned long type);
void do_illegal_instruction(struct pt_regs *regs, unsigned long pc,
                            unsigned long npc, unsigned long psr);

void do_priv_instruction(struct pt_regs *regs, unsigned long pc,
                         unsigned long npc, unsigned long psr);
void do_memaccess_unaligned(struct pt_regs *regs, unsigned long pc,
                            unsigned long npc, unsigned long psr);
void do_fpd_trap(struct pt_regs *regs, unsigned long pc,
                 unsigned long npc, unsigned long psr);
void do_fpe_trap(struct pt_regs *regs, unsigned long pc,
                 unsigned long npc, unsigned long psr);
void handle_tag_overflow(struct pt_regs *regs, unsigned long pc,
                         unsigned long npc, unsigned long psr);
void handle_watchpoint(struct pt_regs *regs, unsigned long pc,
                       unsigned long npc, unsigned long psr);
void handle_reg_access(struct pt_regs *regs, unsigned long pc,
                       unsigned long npc, unsigned long psr);
void handle_cp_disabled(struct pt_regs *regs, unsigned long pc,
                        unsigned long npc, unsigned long psr);
void handle_cp_exception(struct pt_regs *regs, unsigned long pc,
                         unsigned long npc, unsigned long psr);



/* entry.S */
void fpsave(unsigned long *fpregs, unsigned long *fsr,
            void *fpqueue, unsigned long *fpqdepth);
void fpload(unsigned long *fpregs, unsigned long *fsr);

#else /* CONFIG_SPARC32 */

#include <asm/trap_block.h>

struct popc_3insn_patch_entry {
	unsigned int	addr;
	unsigned int	insns[3];
};
extern struct popc_3insn_patch_entry __popc_3insn_patch,
	__popc_3insn_patch_end;

struct popc_6insn_patch_entry {
	unsigned int	addr;
	unsigned int	insns[6];
};
extern struct popc_6insn_patch_entry __popc_6insn_patch,
	__popc_6insn_patch_end;

struct pause_patch_entry {
	unsigned int	addr;
	unsigned int	insns[3];
};
extern struct pause_patch_entry __pause_3insn_patch,
	__pause_3insn_patch_end;

void sun4v_patch_1insn_range(struct sun4v_1insn_patch_entry *,
			     struct sun4v_1insn_patch_entry *);
void sun4v_patch_2insn_range(struct sun4v_2insn_patch_entry *,
			     struct sun4v_2insn_patch_entry *);
void sun_m7_patch_2insn_range(struct sun4v_2insn_patch_entry *,
			     struct sun4v_2insn_patch_entry *);
extern unsigned int dcache_parity_tl1_occurred;
extern unsigned int icache_parity_tl1_occurred;

asmlinkage void sparc_breakpoint(struct pt_regs *regs);
void timer_interrupt(int irq, struct pt_regs *regs);

void do_notify_resume(struct pt_regs *regs,
		      unsigned long orig_i0,
		      unsigned long thread_info_flags);

asmlinkage int syscall_trace_enter(struct pt_regs *regs);
asmlinkage void syscall_trace_leave(struct pt_regs *regs);

void bad_trap_tl1(struct pt_regs *regs, long lvl);

void do_fpieee(struct pt_regs *regs);
void do_fpother(struct pt_regs *regs);
void do_tof(struct pt_regs *regs);
void do_div0(struct pt_regs *regs);
void do_illegal_instruction(struct pt_regs *regs);
void mem_address_unaligned(struct pt_regs *regs,
			   unsigned long sfar,
			   unsigned long sfsr);
void sun4v_do_mna(struct pt_regs *regs,
		  unsigned long addr,
		  unsigned long type_ctx);
void do_privop(struct pt_regs *regs);
void do_privact(struct pt_regs *regs);
void do_cee(struct pt_regs *regs);
void do_div0_tl1(struct pt_regs *regs);
void do_fpieee_tl1(struct pt_regs *regs);
void do_fpother_tl1(struct pt_regs *regs);
void do_ill_tl1(struct pt_regs *regs);
void do_irq_tl1(struct pt_regs *regs);
void do_lddfmna_tl1(struct pt_regs *regs);
void do_stdfmna_tl1(struct pt_regs *regs);
void do_paw(struct pt_regs *regs);
void do_paw_tl1(struct pt_regs *regs);
void do_vaw(struct pt_regs *regs);
void do_vaw_tl1(struct pt_regs *regs);
void do_tof_tl1(struct pt_regs *regs);
void do_getpsr(struct pt_regs *regs);

void spitfire_insn_access_exception(struct pt_regs *regs,
				    unsigned long sfsr,
				    unsigned long sfar);
void spitfire_insn_access_exception_tl1(struct pt_regs *regs,
				        unsigned long sfsr,
				        unsigned long sfar);
void spitfire_data_access_exception(struct pt_regs *regs,
				    unsigned long sfsr,
				    unsigned long sfar);
void spitfire_data_access_exception_tl1(struct pt_regs *regs,
				        unsigned long sfsr,
				        unsigned long sfar);
void spitfire_access_error(struct pt_regs *regs,
			   unsigned long status_encoded,
			   unsigned long afar);

void cheetah_fecc_handler(struct pt_regs *regs,
			  unsigned long afsr,
			  unsigned long afar);
void cheetah_cee_handler(struct pt_regs *regs,
			 unsigned long afsr,
			 unsigned long afar);
void cheetah_deferred_handler(struct pt_regs *regs,
			      unsigned long afsr,
			      unsigned long afar);
void cheetah_plus_parity_error(int type, struct pt_regs *regs);

void sun4v_insn_access_exception(struct pt_regs *regs,
				 unsigned long addr,
				 unsigned long type_ctx);
void sun4v_insn_access_exception_tl1(struct pt_regs *regs,
				     unsigned long addr,
				     unsigned long type_ctx);
void sun4v_data_access_exception(struct pt_regs *regs,
				 unsigned long addr,
				 unsigned long type_ctx);
void sun4v_data_access_exception_tl1(struct pt_regs *regs,
				     unsigned long addr,
				     unsigned long type_ctx);
void sun4v_resum_error(struct pt_regs *regs,
		       unsigned long offset);
void sun4v_resum_overflow(struct pt_regs *regs);
void sun4v_nonresum_error(struct pt_regs *regs,
			  unsigned long offset);
void sun4v_nonresum_overflow(struct pt_regs *regs);
void sun4v_mem_corrupt_detect_precise(struct pt_regs *regs,
				      unsigned long addr,
				      unsigned long context);

extern unsigned long sun4v_err_itlb_vaddr;
extern unsigned long sun4v_err_itlb_ctx;
extern unsigned long sun4v_err_itlb_pte;
extern unsigned long sun4v_err_itlb_error;

void sun4v_itlb_error_report(struct pt_regs *regs, int tl);

extern unsigned long sun4v_err_dtlb_vaddr;
extern unsigned long sun4v_err_dtlb_ctx;
extern unsigned long sun4v_err_dtlb_pte;
extern unsigned long sun4v_err_dtlb_error;

void sun4v_dtlb_error_report(struct pt_regs *regs, int tl);
void hypervisor_tlbop_error(unsigned long err,
			    unsigned long op);
void hypervisor_tlbop_error_xcall(unsigned long err,
				  unsigned long op);

/* WARNING: The error trap handlers in assembly know the precise
 *	    layout of the following structure.
 *
 * C-level handlers in traps.c use this information to log the
 * error and then determine how to recover (if possible).
 */
struct cheetah_err_info {
/*0x00*/u64 afsr;
/*0x08*/u64 afar;

	/* D-cache state */
/*0x10*/u64 dcache_data[4];	/* The actual data	*/
/*0x30*/u64 dcache_index;	/* D-cache index	*/
/*0x38*/u64 dcache_tag;		/* D-cache tag/valid	*/
/*0x40*/u64 dcache_utag;	/* D-cache microtag	*/
/*0x48*/u64 dcache_stag;	/* D-cache snooptag	*/

	/* I-cache state */
/*0x50*/u64 icache_data[8];	/* The actual insns + predecode	*/
/*0x90*/u64 icache_index;	/* I-cache index	*/
/*0x98*/u64 icache_tag;		/* I-cache phys tag	*/
/*0xa0*/u64 icache_utag;	/* I-cache microtag	*/
/*0xa8*/u64 icache_stag;	/* I-cache snooptag	*/
/*0xb0*/u64 icache_upper;	/* I-cache upper-tag	*/
/*0xb8*/u64 icache_lower;	/* I-cache lower-tag	*/

	/* E-cache state */
/*0xc0*/u64 ecache_data[4];	/* 32 bytes from staging registers */
/*0xe0*/u64 ecache_index;	/* E-cache index	*/
/*0xe8*/u64 ecache_tag;		/* E-cache tag/state	*/

/*0xf0*/u64 __pad[32 - 30];
};
#define CHAFSR_INVALID		((u64)-1L)

/* This is allocated at boot time based upon the largest hardware
 * cpu ID in the system.  We allocate two entries per cpu, one for
 * TL==0 logging and one for TL >= 1 logging.
 */
extern struct cheetah_err_info *cheetah_error_log;

/* UPA nodes send interrupt packet to UltraSparc with first data reg
 * value low 5 (7 on Starfire) bits holding the IRQ identifier being
 * delivered.  We must translate this into a non-vector IRQ so we can
 * set the softint on this cpu.
 *
 * To make processing these packets efficient and race free we use
 * an array of irq buckets below.  The interrupt vector handler in
 * entry.S feeds incoming packets into per-cpu pil-indexed lists.
 *
 * If you make changes to ino_bucket, please update hand coded assembler
 * of the vectored interrupt trap handler(s) in entry.S and sun4v_ivec.S
 */
struct ino_bucket {
/*0x00*/unsigned long __irq_chain_pa;

	/* Interrupt number assigned to this INO.  */
/*0x08*/unsigned int __irq;
/*0x0c*/unsigned int __pad;
};

extern struct ino_bucket *ivector_table;
extern unsigned long ivector_table_pa;

void init_irqwork_curcpu(void);
void sun4v_register_mondo_queues(int this_cpu);

#endif /* CONFIG_SPARC32 */
#endif /* _ENTRY_H */
