#ifndef _ASM_SH_SUSPEND_H
#define _ASM_SH_SUSPEND_H

#ifndef __ASSEMBLY__
#include <linux/notifier.h>
static inline int arch_prepare_suspend(void) { return 0; }

#include <asm/ptrace.h>

struct swsusp_arch_regs {
	struct pt_regs user_regs;
	unsigned long bank1_regs[8];
};

void sh_mobile_call_standby(unsigned long mode);

#ifdef CONFIG_CPU_IDLE
void sh_mobile_setup_cpuidle(void);
#else
static inline void sh_mobile_setup_cpuidle(void) {}
#endif

/* notifier chains for pre/post sleep hooks */
extern struct atomic_notifier_head sh_mobile_pre_sleep_notifier_list;
extern struct atomic_notifier_head sh_mobile_post_sleep_notifier_list;

/* priority levels for notifiers */
#define SH_MOBILE_SLEEP_BOARD	0
#define SH_MOBILE_SLEEP_CPU	1
#define SH_MOBILE_PRE(x)	(x)
#define SH_MOBILE_POST(x)	(-(x))

/* board code registration function for self-refresh assembly snippets */
void sh_mobile_register_self_refresh(unsigned long flags,
				     void *pre_start, void *pre_end,
				     void *post_start, void *post_end);
#endif

/* flags passed to assembly suspend code */
#define SUSP_SH_SLEEP		(1 << 0) /* Regular sleep mode */
#define SUSP_SH_STANDBY		(1 << 1) /* SH-Mobile Software standby mode */
#define SUSP_SH_RSTANDBY	(1 << 2) /* SH-Mobile R-standby mode */
#define SUSP_SH_USTANDBY	(1 << 3) /* SH-Mobile U-standby mode */
#define SUSP_SH_SF		(1 << 4) /* Enable self-refresh */

#endif /* _ASM_SH_SUSPEND_H */
