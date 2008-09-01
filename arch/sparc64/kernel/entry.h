#ifndef _ENTRY_H
#define _ENTRY_H

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>

extern const char *sparc_cpu_type;
extern const char *sparc_fpu_type;

extern void __init per_cpu_patch(void);
extern void __init sun4v_patch(void);
extern void __init boot_cpu_id_too_large(int cpu);
extern unsigned int dcache_parity_tl1_occurred;
extern unsigned int icache_parity_tl1_occurred;

extern asmlinkage void update_perfctrs(void);
extern asmlinkage void sparc_breakpoint(struct pt_regs *regs);
extern void timer_interrupt(int irq, struct pt_regs *regs);

extern void do_notify_resume(struct pt_regs *regs,
			     unsigned long orig_i0,
			     unsigned long thread_info_flags);

extern asmlinkage int syscall_trace_enter(struct pt_regs *regs);
extern asmlinkage void syscall_trace_leave(struct pt_regs *regs);

extern void bad_trap_tl1(struct pt_regs *regs, long lvl);

extern void do_fpe_common(struct pt_regs *regs);
extern void do_fpieee(struct pt_regs *regs);
extern void do_fpother(struct pt_regs *regs);
extern void do_tof(struct pt_regs *regs);
extern void do_div0(struct pt_regs *regs);
extern void do_illegal_instruction(struct pt_regs *regs);
extern void mem_address_unaligned(struct pt_regs *regs,
				  unsigned long sfar,
				  unsigned long sfsr);
extern void sun4v_do_mna(struct pt_regs *regs,
			 unsigned long addr,
			 unsigned long type_ctx);
extern void do_privop(struct pt_regs *regs);
extern void do_privact(struct pt_regs *regs);
extern void do_cee(struct pt_regs *regs);
extern void do_cee_tl1(struct pt_regs *regs);
extern void do_dae_tl1(struct pt_regs *regs);
extern void do_iae_tl1(struct pt_regs *regs);
extern void do_div0_tl1(struct pt_regs *regs);
extern void do_fpdis_tl1(struct pt_regs *regs);
extern void do_fpieee_tl1(struct pt_regs *regs);
extern void do_fpother_tl1(struct pt_regs *regs);
extern void do_ill_tl1(struct pt_regs *regs);
extern void do_irq_tl1(struct pt_regs *regs);
extern void do_lddfmna_tl1(struct pt_regs *regs);
extern void do_stdfmna_tl1(struct pt_regs *regs);
extern void do_paw(struct pt_regs *regs);
extern void do_paw_tl1(struct pt_regs *regs);
extern void do_vaw(struct pt_regs *regs);
extern void do_vaw_tl1(struct pt_regs *regs);
extern void do_tof_tl1(struct pt_regs *regs);
extern void do_getpsr(struct pt_regs *regs);

extern void spitfire_insn_access_exception(struct pt_regs *regs,
					   unsigned long sfsr,
					   unsigned long sfar);
extern void spitfire_insn_access_exception_tl1(struct pt_regs *regs,
					       unsigned long sfsr,
					       unsigned long sfar);
extern void spitfire_data_access_exception(struct pt_regs *regs,
					   unsigned long sfsr,
					   unsigned long sfar);
extern void spitfire_data_access_exception_tl1(struct pt_regs *regs,
					       unsigned long sfsr,
					       unsigned long sfar);
extern void spitfire_access_error(struct pt_regs *regs,
				  unsigned long status_encoded,
				  unsigned long afar);

extern void cheetah_fecc_handler(struct pt_regs *regs,
				 unsigned long afsr,
				 unsigned long afar);
extern void cheetah_cee_handler(struct pt_regs *regs,
				unsigned long afsr,
				unsigned long afar);
extern void cheetah_deferred_handler(struct pt_regs *regs,
				     unsigned long afsr,
				     unsigned long afar);
extern void cheetah_plus_parity_error(int type, struct pt_regs *regs);

extern void sun4v_insn_access_exception(struct pt_regs *regs,
					unsigned long addr,
					unsigned long type_ctx);
extern void sun4v_insn_access_exception_tl1(struct pt_regs *regs,
					    unsigned long addr,
					    unsigned long type_ctx);
extern void sun4v_data_access_exception(struct pt_regs *regs,
					unsigned long addr,
					unsigned long type_ctx);
extern void sun4v_data_access_exception_tl1(struct pt_regs *regs,
					    unsigned long addr,
					    unsigned long type_ctx);
extern void sun4v_resum_error(struct pt_regs *regs,
			      unsigned long offset);
extern void sun4v_resum_overflow(struct pt_regs *regs);
extern void sun4v_nonresum_error(struct pt_regs *regs,
				 unsigned long offset);
extern void sun4v_nonresum_overflow(struct pt_regs *regs);

extern unsigned long sun4v_err_itlb_vaddr;
extern unsigned long sun4v_err_itlb_ctx;
extern unsigned long sun4v_err_itlb_pte;
extern unsigned long sun4v_err_itlb_error;

extern void sun4v_itlb_error_report(struct pt_regs *regs, int tl);

extern unsigned long sun4v_err_dtlb_vaddr;
extern unsigned long sun4v_err_dtlb_ctx;
extern unsigned long sun4v_err_dtlb_pte;
extern unsigned long sun4v_err_dtlb_error;

extern void sun4v_dtlb_error_report(struct pt_regs *regs, int tl);
extern void hypervisor_tlbop_error(unsigned long err,
				   unsigned long op);
extern void hypervisor_tlbop_error_xcall(unsigned long err,
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

	/* Virtual interrupt number assigned to this INO.  */
/*0x08*/unsigned int __virt_irq;
/*0x0c*/unsigned int __pad;
};

extern struct ino_bucket *ivector_table;
extern unsigned long ivector_table_pa;

extern void handler_irq(int irq, struct pt_regs *regs);
extern void init_irqwork_curcpu(void);
extern void __cpuinit sun4v_register_mondo_queues(int this_cpu);

#endif /* _ENTRY_H */
