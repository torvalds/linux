#include <linux/init.h>
#include <asm/mce.h>

void amd_mcheck_init(struct cpuinfo_x86 *c);
void intel_p4_mcheck_init(struct cpuinfo_x86 *c);
void intel_p5_mcheck_init(struct cpuinfo_x86 *c);
void intel_p6_mcheck_init(struct cpuinfo_x86 *c);
void winchip_mcheck_init(struct cpuinfo_x86 *c);


/* Call the installed machine check handler for this CPU setup. */
extern void (*machine_check_vector)(struct pt_regs *, long error_code);

#ifdef CONFIG_X86_32

extern int nr_mce_banks;

void intel_set_thermal_handler(void);

#else

static inline void intel_set_thermal_handler(void) { }

#endif

void intel_init_thermal(struct cpuinfo_x86 *c);
