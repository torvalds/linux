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

/* register structure for address/data information */
struct sh_sleep_regs {
	unsigned long stbcr;

	/* MMU */
	unsigned long pteh;
	unsigned long ptel;
	unsigned long ttb;
	unsigned long tea;
	unsigned long mmucr;
	unsigned long ptea;
	unsigned long pascr;
	unsigned long irmcr;

	/* Cache */
	unsigned long ccr;
	unsigned long ramcr;
};

/* data area for low-level sleep code */
struct sh_sleep_data {
	/* current sleep mode (SUSP_SH_...) */
	unsigned long mode;

	/* addresses of board specific self-refresh snippets */
	unsigned long sf_pre;
	unsigned long sf_post;

	/* register state saved and restored by the assembly code */
	unsigned long vbr;
	unsigned long spc;
	unsigned long sr;

	/* structure for keeping register addresses */
	struct sh_sleep_regs addr;

	/* structure for saving/restoring register state */
	struct sh_sleep_regs data;
};

/* a bitmap of supported sleep modes (SUSP_SH..) */
extern unsigned long sh_mobile_sleep_supported;

#endif

/* flags passed to assembly suspend code */
#define SUSP_SH_SLEEP		(1 << 0) /* Regular sleep mode */
#define SUSP_SH_STANDBY		(1 << 1) /* SH-Mobile Software standby mode */
#define SUSP_SH_RSTANDBY	(1 << 2) /* SH-Mobile R-standby mode */
#define SUSP_SH_USTANDBY	(1 << 3) /* SH-Mobile U-standby mode */
#define SUSP_SH_SF		(1 << 4) /* Enable self-refresh */
#define SUSP_SH_MMU		(1 << 5) /* Save/restore MMU and cache */

#endif /* _ASM_SH_SUSPEND_H */
