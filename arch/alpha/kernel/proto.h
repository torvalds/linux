/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/interrupt.h>
#include <linux/io.h>

#include <asm/pgtable.h>

/* Prototypes of functions used across modules here in this directory.  */

#define vucp	volatile unsigned char  *
#define vusp	volatile unsigned short *
#define vip	volatile int *
#define vuip	volatile unsigned int   *
#define vulp	volatile unsigned long  *

struct pt_regs;
struct task_struct;
struct pci_dev;
struct pci_controller;

/* core_apecs.c */
extern struct pci_ops apecs_pci_ops;
extern void apecs_init_arch(void);
extern void apecs_pci_clr_err(void);
extern void apecs_machine_check(unsigned long vector, unsigned long la_ptr);
extern void apecs_pci_tbi(struct pci_controller *, dma_addr_t, dma_addr_t);

/* core_cia.c */
extern struct pci_ops cia_pci_ops;
extern void cia_init_pci(void);
extern void cia_init_arch(void);
extern void pyxis_init_arch(void);
extern void cia_kill_arch(int);
extern void cia_machine_check(unsigned long vector, unsigned long la_ptr);
extern void cia_pci_tbi(struct pci_controller *, dma_addr_t, dma_addr_t);

/* core_irongate.c */
extern struct pci_ops irongate_pci_ops;
extern int irongate_pci_clr_err(void);
extern void irongate_init_arch(void);
#define irongate_pci_tbi ((void *)0)

/* core_lca.c */
extern struct pci_ops lca_pci_ops;
extern void lca_init_arch(void);
extern void lca_machine_check(unsigned long vector, unsigned long la_ptr);
extern void lca_pci_tbi(struct pci_controller *, dma_addr_t, dma_addr_t);

/* core_marvel.c */
extern struct pci_ops marvel_pci_ops;
extern void marvel_init_arch(void);
extern void marvel_kill_arch(int);
extern void marvel_machine_check(unsigned long, unsigned long);
extern void marvel_pci_tbi(struct pci_controller *, dma_addr_t, dma_addr_t);
extern int marvel_pa_to_nid(unsigned long);
extern int marvel_cpuid_to_nid(int);
extern unsigned long marvel_node_mem_start(int);
extern unsigned long marvel_node_mem_size(int);
extern struct _alpha_agp_info *marvel_agp_info(void);
struct io7 *marvel_find_io7(int pe);
struct io7 *marvel_next_io7(struct io7 *prev);
void io7_clear_errors(struct io7 *io7);

/* core_mcpcia.c */
extern struct pci_ops mcpcia_pci_ops;
extern void mcpcia_init_arch(void);
extern void mcpcia_init_hoses(void);
extern void mcpcia_machine_check(unsigned long vector, unsigned long la_ptr);
extern void mcpcia_pci_tbi(struct pci_controller *, dma_addr_t, dma_addr_t);

/* core_polaris.c */
extern struct pci_ops polaris_pci_ops;
extern int polaris_read_config_dword(struct pci_dev *, int, u32 *);
extern int polaris_write_config_dword(struct pci_dev *, int, u32);
extern void polaris_init_arch(void);
extern void polaris_machine_check(unsigned long vector, unsigned long la_ptr);
#define polaris_pci_tbi ((void *)0)

/* core_t2.c */
extern struct pci_ops t2_pci_ops;
extern void t2_init_arch(void);
extern void t2_kill_arch(int);
extern void t2_machine_check(unsigned long vector, unsigned long la_ptr);
extern void t2_pci_tbi(struct pci_controller *, dma_addr_t, dma_addr_t);

/* core_titan.c */
extern struct pci_ops titan_pci_ops;
extern void titan_init_arch(void);
extern void titan_kill_arch(int);
extern void titan_machine_check(unsigned long, unsigned long);
extern void titan_pci_tbi(struct pci_controller *, dma_addr_t, dma_addr_t);
extern struct _alpha_agp_info *titan_agp_info(void);

/* core_tsunami.c */
extern struct pci_ops tsunami_pci_ops;
extern void tsunami_init_arch(void);
extern void tsunami_kill_arch(int);
extern void tsunami_machine_check(unsigned long vector, unsigned long la_ptr);
extern void tsunami_pci_tbi(struct pci_controller *, dma_addr_t, dma_addr_t);

/* core_wildfire.c */
extern struct pci_ops wildfire_pci_ops;
extern void wildfire_init_arch(void);
extern void wildfire_kill_arch(int);
extern void wildfire_machine_check(unsigned long vector, unsigned long la_ptr);
extern void wildfire_pci_tbi(struct pci_controller *, dma_addr_t, dma_addr_t);
extern int wildfire_pa_to_nid(unsigned long);
extern int wildfire_cpuid_to_nid(int);
extern unsigned long wildfire_node_mem_start(int);
extern unsigned long wildfire_node_mem_size(int);

/* console.c */
#ifdef CONFIG_VGA_HOSE
extern void find_console_vga_hose(void);
extern void locate_and_init_vga(void *(*)(void *, void *));
#else
static inline void find_console_vga_hose(void) { }
static inline void locate_and_init_vga(void *(*sel_func)(void *, void *)) { }
#endif

/* setup.c */
extern unsigned long srm_hae;
extern int boot_cpuid;
#ifdef CONFIG_VERBOSE_MCHECK
extern unsigned long alpha_verbose_mcheck;
#endif

/* srmcons.c */
#if defined(CONFIG_ALPHA_GENERIC) || defined(CONFIG_ALPHA_SRM)
extern void register_srm_console(void);
extern void unregister_srm_console(void);
#else
#define register_srm_console()
#define unregister_srm_console()
#endif

/* smp.c */
extern void setup_smp(void);
extern void handle_ipi(struct pt_regs *);

/* bios32.c */
/* extern void reset_for_srm(void); */

/* time.c */
extern irqreturn_t rtc_timer_interrupt(int irq, void *dev);
extern void init_clockevent(void);
extern void common_init_rtc(void);
extern unsigned long est_cycle_freq;

/* smc37c93x.c */
extern void SMC93x_Init(void);

/* smc37c669.c */
extern void SMC669_Init(int);

/* es1888.c */
extern void es1888_init(void);

/* ../lib/fpreg.c */
extern void alpha_write_fp_reg (unsigned long reg, unsigned long val);
extern unsigned long alpha_read_fp_reg (unsigned long reg);

/* head.S */
extern void wrmces(unsigned long mces);
extern void cserve_ena(unsigned long);
extern void cserve_dis(unsigned long);
extern void __smp_callin(unsigned long);

/* entry.S */
extern void entArith(void);
extern void entIF(void);
extern void entInt(void);
extern void entMM(void);
extern void entSys(void);
extern void entUna(void);
extern void entDbg(void);

/* ptrace.c */
extern int ptrace_set_bpt (struct task_struct *child);
extern int ptrace_cancel_bpt (struct task_struct *child);

/* traps.c */
extern void dik_show_regs(struct pt_regs *regs, unsigned long *r9_15);
extern void die_if_kernel(char *, struct pt_regs *, long, unsigned long *);

/* sys_titan.c */
extern void titan_dispatch_irqs(u64);

/* ../mm/init.c */
extern void switch_to_system_map(void);
extern void srm_paging_stop(void);

static inline int
__alpha_remap_area_pages(unsigned long address, unsigned long phys_addr,
			 unsigned long size, unsigned long flags)
{
	pgprot_t prot;

	prot = __pgprot(_PAGE_VALID | _PAGE_ASM | _PAGE_KRE
			| _PAGE_KWE | flags);
	return ioremap_page_range(address, address + size, phys_addr, prot);
}

/* irq.c */

#ifdef CONFIG_SMP
#define mcheck_expected(cpu)	(cpu_data[cpu].mcheck_expected)
#define mcheck_taken(cpu)	(cpu_data[cpu].mcheck_taken)
#define mcheck_extra(cpu)	(cpu_data[cpu].mcheck_extra)
#else
extern struct mcheck_info
{
	unsigned char expected __attribute__((aligned(8)));
	unsigned char taken;
	unsigned char extra;
} __mcheck_info;

#define mcheck_expected(cpu)	(*((void)(cpu), &__mcheck_info.expected))
#define mcheck_taken(cpu)	(*((void)(cpu), &__mcheck_info.taken))
#define mcheck_extra(cpu)	(*((void)(cpu), &__mcheck_info.extra))
#endif

extern void process_mcheck_info(unsigned long vector, unsigned long la_ptr,
				const char *machine, int expected);
