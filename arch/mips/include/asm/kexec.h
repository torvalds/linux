/*
 * kexec.h for kexec
 * Created by <nschichan@corp.free.fr> on Thu Oct 12 14:59:34 2006
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2.  See the file COPYING for more details.
 */

#ifndef _MIPS_KEXEC
# define _MIPS_KEXEC

#include <asm/stacktrace.h>

/* Maximum physical address we can use pages from */
#define KEXEC_SOURCE_MEMORY_LIMIT (-1UL)
/* Maximum address we can reach in physical address mode */
#define KEXEC_DESTINATION_MEMORY_LIMIT (-1UL)
 /* Maximum address we can use for the control code buffer */
#define KEXEC_CONTROL_MEMORY_LIMIT (-1UL)
/* Reserve 3*4096 bytes for board-specific info */
#define KEXEC_CONTROL_PAGE_SIZE (4096 + 3*4096)

/* The native architecture */
#define KEXEC_ARCH KEXEC_ARCH_MIPS
#define MAX_NOTE_BYTES 1024

static inline void crash_setup_regs(struct pt_regs *newregs,
				    struct pt_regs *oldregs)
{
	if (oldregs)
		memcpy(newregs, oldregs, sizeof(*newregs));
	else
		prepare_frametrace(newregs);
}

#ifdef CONFIG_KEXEC
struct kimage;
extern unsigned long kexec_args[4];
extern int (*_machine_kexec_prepare)(struct kimage *);
extern void (*_machine_kexec_shutdown)(void);
extern void (*_machine_crash_shutdown)(struct pt_regs *regs);
extern void default_machine_crash_shutdown(struct pt_regs *regs);
#ifdef CONFIG_SMP
extern const unsigned char kexec_smp_wait[];
extern unsigned long secondary_kexec_args[4];
extern void (*relocated_kexec_smp_wait) (void *);
extern atomic_t kexec_ready_to_reboot;
extern void (*_crash_smp_send_stop)(void);
#endif
#endif

#endif /* !_MIPS_KEXEC */
