#include <linux/init.h>
#include <asm/mce.h>

#ifdef CONFIG_X86_OLD_MCE
void amd_mcheck_init(struct cpuinfo_x86 *c);
void intel_p4_mcheck_init(struct cpuinfo_x86 *c);
void intel_p6_mcheck_init(struct cpuinfo_x86 *c);
#endif

#ifdef CONFIG_X86_ANCIENT_MCE
void intel_p5_mcheck_init(struct cpuinfo_x86 *c);
void winchip_mcheck_init(struct cpuinfo_x86 *c);
extern int mce_p5_enable;
static inline int mce_p5_enabled(void) { return mce_p5_enable; }
static inline void enable_p5_mce(void) { mce_p5_enable = 1; }
#else
static inline void intel_p5_mcheck_init(struct cpuinfo_x86 *c) {}
static inline void winchip_mcheck_init(struct cpuinfo_x86 *c) {}
static inline int mce_p5_enabled(void) { return 0; }
static inline void enable_p5_mce(void) { }
#endif

/* Call the installed machine check handler for this CPU setup. */
extern void (*machine_check_vector)(struct pt_regs *, long error_code);

#ifdef CONFIG_X86_OLD_MCE

extern int nr_mce_banks;

void intel_set_thermal_handler(void);

#else

static inline void intel_set_thermal_handler(void) { }

#endif

void intel_init_thermal(struct cpuinfo_x86 *c);
