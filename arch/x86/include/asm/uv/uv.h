#ifndef _ASM_X86_UV_UV_H
#define _ASM_X86_UV_UV_H

enum uv_system_type {UV_NONE, UV_LEGACY_APIC, UV_X2APIC, UV_NON_UNIQUE_APIC};

struct cpumask;
struct mm_struct;

#ifdef CONFIG_X86_UV

extern enum uv_system_type get_uv_system_type(void);
extern int is_uv_system(void);
extern void uv_cpu_init(void);
extern void uv_system_init(void);
extern const struct cpumask *uv_flush_tlb_others(const struct cpumask *cpumask,
						 struct mm_struct *mm,
						 unsigned long va,
						 unsigned int cpu);

#else	/* X86_UV */

static inline enum uv_system_type get_uv_system_type(void) { return UV_NONE; }
static inline int is_uv_system(void)	{ return 0; }
static inline void uv_cpu_init(void)	{ }
static inline void uv_system_init(void)	{ }
static inline const struct cpumask *
uv_flush_tlb_others(const struct cpumask *cpumask, struct mm_struct *mm,
		    unsigned long va, unsigned int cpu)
{ return cpumask; }

#endif	/* X86_UV */

#endif	/* _ASM_X86_UV_UV_H */
