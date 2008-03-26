#ifndef _ENTRY_H
#define _ENTRY_H

#include <linux/init.h>

extern char *sparc_cpu_type;
extern char *sparc_fpu_type;

extern void __init per_cpu_patch(void);
extern void __init sun4v_patch(void);
extern void __init boot_cpu_id_too_large(int cpu);
extern unsigned int dcache_parity_tl1_occurred;
extern unsigned int icache_parity_tl1_occurred;

#endif /* _ENTRY_H */
