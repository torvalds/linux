/*
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __ASM_FP_H
#define __ASM_FP_H

#include <asm/errno.h>
#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/sigcontext.h>
#include <asm/sysreg.h>

#ifndef __ASSEMBLY__

#include <linux/build_bug.h>
#include <linux/cache.h>
#include <linux/init.h>
#include <linux/stddef.h>

#if defined(__KERNEL__) && defined(CONFIG_COMPAT)
/* Masks for extracting the FPSR and FPCR from the FPSCR */
#define VFP_FPSCR_STAT_MASK	0xf800009f
#define VFP_FPSCR_CTRL_MASK	0x07f79f00
/*
 * The VFP state has 32x64-bit registers and a single 32-bit
 * control/status register.
 */
#define VFP_STATE_SIZE		((32 * 8) + 4)
#endif

struct task_struct;

extern void fpsimd_save_state(struct user_fpsimd_state *state);
extern void fpsimd_load_state(struct user_fpsimd_state *state);

extern void fpsimd_save(void);

extern void fpsimd_thread_switch(struct task_struct *next);
extern void fpsimd_flush_thread(void);

extern void fpsimd_signal_preserve_current_state(void);
extern void fpsimd_preserve_current_state(void);
extern void fpsimd_restore_current_state(void);
extern void fpsimd_update_current_state(struct user_fpsimd_state const *state);

extern void fpsimd_bind_task_to_cpu(void);
extern void fpsimd_bind_state_to_cpu(struct user_fpsimd_state *state);

extern void fpsimd_flush_task_state(struct task_struct *target);
extern void fpsimd_flush_cpu_state(void);
extern void sve_flush_cpu_state(void);

/* Maximum VL that SVE VL-agnostic software can transparently support */
#define SVE_VL_ARCH_MAX 0x100

/* Offset of FFR in the SVE register dump */
static inline size_t sve_ffr_offset(int vl)
{
	return SVE_SIG_FFR_OFFSET(sve_vq_from_vl(vl)) - SVE_SIG_REGS_OFFSET;
}

static inline void *sve_pffr(struct thread_struct *thread)
{
	return (char *)thread->sve_state + sve_ffr_offset(thread->sve_vl);
}

extern void sve_save_state(void *state, u32 *pfpsr);
extern void sve_load_state(void const *state, u32 const *pfpsr,
			   unsigned long vq_minus_1);
extern unsigned int sve_get_vl(void);

struct arm64_cpu_capabilities;
extern void sve_kernel_enable(const struct arm64_cpu_capabilities *__unused);

extern u64 read_zcr_features(void);

extern int __ro_after_init sve_max_vl;

#ifdef CONFIG_ARM64_SVE

extern size_t sve_state_size(struct task_struct const *task);

extern void sve_alloc(struct task_struct *task);
extern void fpsimd_release_task(struct task_struct *task);
extern void fpsimd_sync_to_sve(struct task_struct *task);
extern void sve_sync_to_fpsimd(struct task_struct *task);
extern void sve_sync_from_fpsimd_zeropad(struct task_struct *task);

extern int sve_set_vector_length(struct task_struct *task,
				 unsigned long vl, unsigned long flags);

extern int sve_set_current_vl(unsigned long arg);
extern int sve_get_current_vl(void);

static inline void sve_user_disable(void)
{
	sysreg_clear_set(cpacr_el1, CPACR_EL1_ZEN_EL0EN, 0);
}

static inline void sve_user_enable(void)
{
	sysreg_clear_set(cpacr_el1, 0, CPACR_EL1_ZEN_EL0EN);
}

/*
 * Probing and setup functions.
 * Calls to these functions must be serialised with one another.
 */
extern void __init sve_init_vq_map(void);
extern void sve_update_vq_map(void);
extern int sve_verify_vq_map(void);
extern void __init sve_setup(void);

#else /* ! CONFIG_ARM64_SVE */

static inline void sve_alloc(struct task_struct *task) { }
static inline void fpsimd_release_task(struct task_struct *task) { }
static inline void sve_sync_to_fpsimd(struct task_struct *task) { }
static inline void sve_sync_from_fpsimd_zeropad(struct task_struct *task) { }

static inline int sve_set_current_vl(unsigned long arg)
{
	return -EINVAL;
}

static inline int sve_get_current_vl(void)
{
	return -EINVAL;
}

static inline void sve_user_disable(void) { BUILD_BUG(); }
static inline void sve_user_enable(void) { BUILD_BUG(); }

static inline void sve_init_vq_map(void) { }
static inline void sve_update_vq_map(void) { }
static inline int sve_verify_vq_map(void) { return 0; }
static inline void sve_setup(void) { }

#endif /* ! CONFIG_ARM64_SVE */

/* For use by EFI runtime services calls only */
extern void __efi_fpsimd_begin(void);
extern void __efi_fpsimd_end(void);

#endif

#endif
