/*
 * xsave/xrstor support.
 *
 * Author: Suresh Siddha <suresh.b.siddha@intel.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/bootmem.h>
#include <linux/compat.h>
#include <linux/cpu.h>
#include <asm/i387.h>
#include <asm/fpu-internal.h>
#include <asm/sigframe.h>
#include <asm/xcr.h>

/*
 * Supported feature mask by the CPU and the kernel.
 */
u64 pcntxt_mask;

/*
 * Represents init state for the supported extended state.
 */
struct xsave_struct *init_xstate_buf;

static struct _fpx_sw_bytes fx_sw_reserved, fx_sw_reserved_ia32;
static unsigned int *xstate_offsets, *xstate_sizes;
static unsigned int xstate_comp_offsets[sizeof(pcntxt_mask)*8];
static unsigned int xstate_features;

/*
 * If a processor implementation discern that a processor state component is
 * in its initialized state it may modify the corresponding bit in the
 * xsave_hdr.xstate_bv as '0', with out modifying the corresponding memory
 * layout in the case of xsaveopt. While presenting the xstate information to
 * the user, we always ensure that the memory layout of a feature will be in
 * the init state if the corresponding header bit is zero. This is to ensure
 * that the user doesn't see some stale state in the memory layout during
 * signal handling, debugging etc.
 */
void __sanitize_i387_state(struct task_struct *tsk)
{
	struct i387_fxsave_struct *fx = &tsk->thread.fpu.state->fxsave;
	int feature_bit = 0x2;
	u64 xstate_bv;

	if (!fx)
		return;

	xstate_bv = tsk->thread.fpu.state->xsave.xsave_hdr.xstate_bv;

	/*
	 * None of the feature bits are in init state. So nothing else
	 * to do for us, as the memory layout is up to date.
	 */
	if ((xstate_bv & pcntxt_mask) == pcntxt_mask)
		return;

	/*
	 * FP is in init state
	 */
	if (!(xstate_bv & XSTATE_FP)) {
		fx->cwd = 0x37f;
		fx->swd = 0;
		fx->twd = 0;
		fx->fop = 0;
		fx->rip = 0;
		fx->rdp = 0;
		memset(&fx->st_space[0], 0, 128);
	}

	/*
	 * SSE is in init state
	 */
	if (!(xstate_bv & XSTATE_SSE))
		memset(&fx->xmm_space[0], 0, 256);

	xstate_bv = (pcntxt_mask & ~xstate_bv) >> 2;

	/*
	 * Update all the other memory layouts for which the corresponding
	 * header bit is in the init state.
	 */
	while (xstate_bv) {
		if (xstate_bv & 0x1) {
			int offset = xstate_offsets[feature_bit];
			int size = xstate_sizes[feature_bit];

			memcpy(((void *) fx) + offset,
			       ((void *) init_xstate_buf) + offset,
			       size);
		}

		xstate_bv >>= 1;
		feature_bit++;
	}
}

/*
 * Check for the presence of extended state information in the
 * user fpstate pointer in the sigcontext.
 */
static inline int check_for_xstate(struct i387_fxsave_struct __user *buf,
				   void __user *fpstate,
				   struct _fpx_sw_bytes *fx_sw)
{
	int min_xstate_size = sizeof(struct i387_fxsave_struct) +
			      sizeof(struct xsave_hdr_struct);
	unsigned int magic2;

	if (__copy_from_user(fx_sw, &buf->sw_reserved[0], sizeof(*fx_sw)))
		return -1;

	/* Check for the first magic field and other error scenarios. */
	if (fx_sw->magic1 != FP_XSTATE_MAGIC1 ||
	    fx_sw->xstate_size < min_xstate_size ||
	    fx_sw->xstate_size > xstate_size ||
	    fx_sw->xstate_size > fx_sw->extended_size)
		return -1;

	/*
	 * Check for the presence of second magic word at the end of memory
	 * layout. This detects the case where the user just copied the legacy
	 * fpstate layout with out copying the extended state information
	 * in the memory layout.
	 */
	if (__get_user(magic2, (__u32 __user *)(fpstate + fx_sw->xstate_size))
	    || magic2 != FP_XSTATE_MAGIC2)
		return -1;

	return 0;
}

/*
 * Signal frame handlers.
 */
static inline int save_fsave_header(struct task_struct *tsk, void __user *buf)
{
	if (use_fxsr()) {
		struct xsave_struct *xsave = &tsk->thread.fpu.state->xsave;
		struct user_i387_ia32_struct env;
		struct _fpstate_ia32 __user *fp = buf;

		convert_from_fxsr(&env, tsk);

		if (__copy_to_user(buf, &env, sizeof(env)) ||
		    __put_user(xsave->i387.swd, &fp->status) ||
		    __put_user(X86_FXSR_MAGIC, &fp->magic))
			return -1;
	} else {
		struct i387_fsave_struct __user *fp = buf;
		u32 swd;
		if (__get_user(swd, &fp->swd) || __put_user(swd, &fp->status))
			return -1;
	}

	return 0;
}

static inline int save_xstate_epilog(void __user *buf, int ia32_frame)
{
	struct xsave_struct __user *x = buf;
	struct _fpx_sw_bytes *sw_bytes;
	u32 xstate_bv;
	int err;

	/* Setup the bytes not touched by the [f]xsave and reserved for SW. */
	sw_bytes = ia32_frame ? &fx_sw_reserved_ia32 : &fx_sw_reserved;
	err = __copy_to_user(&x->i387.sw_reserved, sw_bytes, sizeof(*sw_bytes));

	if (!use_xsave())
		return err;

	err |= __put_user(FP_XSTATE_MAGIC2, (__u32 *)(buf + xstate_size));

	/*
	 * Read the xstate_bv which we copied (directly from the cpu or
	 * from the state in task struct) to the user buffers.
	 */
	err |= __get_user(xstate_bv, (__u32 *)&x->xsave_hdr.xstate_bv);

	/*
	 * For legacy compatible, we always set FP/SSE bits in the bit
	 * vector while saving the state to the user context. This will
	 * enable us capturing any changes(during sigreturn) to
	 * the FP/SSE bits by the legacy applications which don't touch
	 * xstate_bv in the xsave header.
	 *
	 * xsave aware apps can change the xstate_bv in the xsave
	 * header as well as change any contents in the memory layout.
	 * xrestore as part of sigreturn will capture all the changes.
	 */
	xstate_bv |= XSTATE_FPSSE;

	err |= __put_user(xstate_bv, (__u32 *)&x->xsave_hdr.xstate_bv);

	return err;
}

static inline int save_user_xstate(struct xsave_struct __user *buf)
{
	int err;

	if (use_xsave())
		err = xsave_user(buf);
	else if (use_fxsr())
		err = fxsave_user((struct i387_fxsave_struct __user *) buf);
	else
		err = fsave_user((struct i387_fsave_struct __user *) buf);

	if (unlikely(err) && __clear_user(buf, xstate_size))
		err = -EFAULT;
	return err;
}

/*
 * Save the fpu, extended register state to the user signal frame.
 *
 * 'buf_fx' is the 64-byte aligned pointer at which the [f|fx|x]save
 *  state is copied.
 *  'buf' points to the 'buf_fx' or to the fsave header followed by 'buf_fx'.
 *
 *	buf == buf_fx for 64-bit frames and 32-bit fsave frame.
 *	buf != buf_fx for 32-bit frames with fxstate.
 *
 * If the fpu, extended register state is live, save the state directly
 * to the user frame pointed by the aligned pointer 'buf_fx'. Otherwise,
 * copy the thread's fpu state to the user frame starting at 'buf_fx'.
 *
 * If this is a 32-bit frame with fxstate, put a fsave header before
 * the aligned state at 'buf_fx'.
 *
 * For [f]xsave state, update the SW reserved fields in the [f]xsave frame
 * indicating the absence/presence of the extended state to the user.
 */
int save_xstate_sig(void __user *buf, void __user *buf_fx, int size)
{
	struct xsave_struct *xsave = &current->thread.fpu.state->xsave;
	struct task_struct *tsk = current;
	int ia32_fxstate = (buf != buf_fx);

	ia32_fxstate &= (config_enabled(CONFIG_X86_32) ||
			 config_enabled(CONFIG_IA32_EMULATION));

	if (!access_ok(VERIFY_WRITE, buf, size))
		return -EACCES;

	if (!static_cpu_has(X86_FEATURE_FPU))
		return fpregs_soft_get(current, NULL, 0,
			sizeof(struct user_i387_ia32_struct), NULL,
			(struct _fpstate_ia32 __user *) buf) ? -1 : 1;

	if (user_has_fpu()) {
		/* Save the live register state to the user directly. */
		if (save_user_xstate(buf_fx))
			return -1;
		/* Update the thread's fxstate to save the fsave header. */
		if (ia32_fxstate)
			fpu_fxsave(&tsk->thread.fpu);
	} else {
		sanitize_i387_state(tsk);
		if (__copy_to_user(buf_fx, xsave, xstate_size))
			return -1;
	}

	/* Save the fsave header for the 32-bit frames. */
	if ((ia32_fxstate || !use_fxsr()) && save_fsave_header(tsk, buf))
		return -1;

	if (use_fxsr() && save_xstate_epilog(buf_fx, ia32_fxstate))
		return -1;

	return 0;
}

static inline void
sanitize_restored_xstate(struct task_struct *tsk,
			 struct user_i387_ia32_struct *ia32_env,
			 u64 xstate_bv, int fx_only)
{
	struct xsave_struct *xsave = &tsk->thread.fpu.state->xsave;
	struct xsave_hdr_struct *xsave_hdr = &xsave->xsave_hdr;

	if (use_xsave()) {
		/* These bits must be zero. */
		memset(xsave_hdr->reserved, 0, 48);

		/*
		 * Init the state that is not present in the memory
		 * layout and not enabled by the OS.
		 */
		if (fx_only)
			xsave_hdr->xstate_bv = XSTATE_FPSSE;
		else
			xsave_hdr->xstate_bv &= (pcntxt_mask & xstate_bv);
	}

	if (use_fxsr()) {
		/*
		 * mscsr reserved bits must be masked to zero for security
		 * reasons.
		 */
		xsave->i387.mxcsr &= mxcsr_feature_mask;

		convert_to_fxsr(tsk, ia32_env);
	}
}

/*
 * Restore the extended state if present. Otherwise, restore the FP/SSE state.
 */
static inline int restore_user_xstate(void __user *buf, u64 xbv, int fx_only)
{
	if (use_xsave()) {
		if ((unsigned long)buf % 64 || fx_only) {
			u64 init_bv = pcntxt_mask & ~XSTATE_FPSSE;
			xrstor_state(init_xstate_buf, init_bv);
			return fxrstor_user(buf);
		} else {
			u64 init_bv = pcntxt_mask & ~xbv;
			if (unlikely(init_bv))
				xrstor_state(init_xstate_buf, init_bv);
			return xrestore_user(buf, xbv);
		}
	} else if (use_fxsr()) {
		return fxrstor_user(buf);
	} else
		return frstor_user(buf);
}

int __restore_xstate_sig(void __user *buf, void __user *buf_fx, int size)
{
	int ia32_fxstate = (buf != buf_fx);
	struct task_struct *tsk = current;
	int state_size = xstate_size;
	u64 xstate_bv = 0;
	int fx_only = 0;

	ia32_fxstate &= (config_enabled(CONFIG_X86_32) ||
			 config_enabled(CONFIG_IA32_EMULATION));

	if (!buf) {
		drop_init_fpu(tsk);
		return 0;
	}

	if (!access_ok(VERIFY_READ, buf, size))
		return -EACCES;

	if (!used_math() && init_fpu(tsk))
		return -1;

	if (!static_cpu_has(X86_FEATURE_FPU))
		return fpregs_soft_set(current, NULL,
				       0, sizeof(struct user_i387_ia32_struct),
				       NULL, buf) != 0;

	if (use_xsave()) {
		struct _fpx_sw_bytes fx_sw_user;
		if (unlikely(check_for_xstate(buf_fx, buf_fx, &fx_sw_user))) {
			/*
			 * Couldn't find the extended state information in the
			 * memory layout. Restore just the FP/SSE and init all
			 * the other extended state.
			 */
			state_size = sizeof(struct i387_fxsave_struct);
			fx_only = 1;
		} else {
			state_size = fx_sw_user.xstate_size;
			xstate_bv = fx_sw_user.xstate_bv;
		}
	}

	if (ia32_fxstate) {
		/*
		 * For 32-bit frames with fxstate, copy the user state to the
		 * thread's fpu state, reconstruct fxstate from the fsave
		 * header. Sanitize the copied state etc.
		 */
		struct xsave_struct *xsave = &tsk->thread.fpu.state->xsave;
		struct user_i387_ia32_struct env;
		int err = 0;

		/*
		 * Drop the current fpu which clears used_math(). This ensures
		 * that any context-switch during the copy of the new state,
		 * avoids the intermediate state from getting restored/saved.
		 * Thus avoiding the new restored state from getting corrupted.
		 * We will be ready to restore/save the state only after
		 * set_used_math() is again set.
		 */
		drop_fpu(tsk);

		if (__copy_from_user(xsave, buf_fx, state_size) ||
		    __copy_from_user(&env, buf, sizeof(env))) {
			err = -1;
		} else {
			sanitize_restored_xstate(tsk, &env, xstate_bv, fx_only);
			set_used_math();
		}

		if (use_eager_fpu()) {
			preempt_disable();
			math_state_restore();
			preempt_enable();
		}

		return err;
	} else {
		/*
		 * For 64-bit frames and 32-bit fsave frames, restore the user
		 * state to the registers directly (with exceptions handled).
		 */
		user_fpu_begin();
		if (restore_user_xstate(buf_fx, xstate_bv, fx_only)) {
			drop_init_fpu(tsk);
			return -1;
		}
	}

	return 0;
}

/*
 * Prepare the SW reserved portion of the fxsave memory layout, indicating
 * the presence of the extended state information in the memory layout
 * pointed by the fpstate pointer in the sigcontext.
 * This will be saved when ever the FP and extended state context is
 * saved on the user stack during the signal handler delivery to the user.
 */
static void prepare_fx_sw_frame(void)
{
	int fsave_header_size = sizeof(struct i387_fsave_struct);
	int size = xstate_size + FP_XSTATE_MAGIC2_SIZE;

	if (config_enabled(CONFIG_X86_32))
		size += fsave_header_size;

	fx_sw_reserved.magic1 = FP_XSTATE_MAGIC1;
	fx_sw_reserved.extended_size = size;
	fx_sw_reserved.xstate_bv = pcntxt_mask;
	fx_sw_reserved.xstate_size = xstate_size;

	if (config_enabled(CONFIG_IA32_EMULATION)) {
		fx_sw_reserved_ia32 = fx_sw_reserved;
		fx_sw_reserved_ia32.extended_size += fsave_header_size;
	}
}

/*
 * Enable the extended processor state save/restore feature
 */
static inline void xstate_enable(void)
{
	set_in_cr4(X86_CR4_OSXSAVE);
	xsetbv(XCR_XFEATURE_ENABLED_MASK, pcntxt_mask);
}

/*
 * Record the offsets and sizes of different state managed by the xsave
 * memory layout.
 */
static void __init setup_xstate_features(void)
{
	int eax, ebx, ecx, edx, leaf = 0x2;

	xstate_features = fls64(pcntxt_mask);
	xstate_offsets = alloc_bootmem(xstate_features * sizeof(int));
	xstate_sizes = alloc_bootmem(xstate_features * sizeof(int));

	do {
		cpuid_count(XSTATE_CPUID, leaf, &eax, &ebx, &ecx, &edx);

		if (eax == 0)
			break;

		xstate_offsets[leaf] = ebx;
		xstate_sizes[leaf] = eax;

		leaf++;
	} while (1);
}

/*
 * This function sets up offsets and sizes of all extended states in
 * xsave area. This supports both standard format and compacted format
 * of the xsave aread.
 *
 * Input: void
 * Output: void
 */
void setup_xstate_comp(void)
{
	unsigned int xstate_comp_sizes[sizeof(pcntxt_mask)*8];
	int i;

	/*
	 * The FP xstates and SSE xstates are legacy states. They are always
	 * in the fixed offsets in the xsave area in either compacted form
	 * or standard form.
	 */
	xstate_comp_offsets[0] = 0;
	xstate_comp_offsets[1] = offsetof(struct i387_fxsave_struct, xmm_space);

	if (!cpu_has_xsaves) {
		for (i = 2; i < xstate_features; i++) {
			if (test_bit(i, (unsigned long *)&pcntxt_mask)) {
				xstate_comp_offsets[i] = xstate_offsets[i];
				xstate_comp_sizes[i] = xstate_sizes[i];
			}
		}
		return;
	}

	xstate_comp_offsets[2] = FXSAVE_SIZE + XSAVE_HDR_SIZE;

	for (i = 2; i < xstate_features; i++) {
		if (test_bit(i, (unsigned long *)&pcntxt_mask))
			xstate_comp_sizes[i] = xstate_sizes[i];
		else
			xstate_comp_sizes[i] = 0;

		if (i > 2)
			xstate_comp_offsets[i] = xstate_comp_offsets[i-1]
					+ xstate_comp_sizes[i-1];

	}
}

/*
 * setup the xstate image representing the init state
 */
static void __init setup_init_fpu_buf(void)
{
	/*
	 * Setup init_xstate_buf to represent the init state of
	 * all the features managed by the xsave
	 */
	init_xstate_buf = alloc_bootmem_align(xstate_size,
					      __alignof__(struct xsave_struct));
	fx_finit(&init_xstate_buf->i387);

	if (!cpu_has_xsave)
		return;

	setup_xstate_features();

	if (cpu_has_xsaves) {
		init_xstate_buf->xsave_hdr.xcomp_bv =
						(u64)1 << 63 | pcntxt_mask;
		init_xstate_buf->xsave_hdr.xstate_bv = pcntxt_mask;
	}

	/*
	 * Init all the features state with header_bv being 0x0
	 */
	xrstor_state_booting(init_xstate_buf, -1);
	/*
	 * Dump the init state again. This is to identify the init state
	 * of any feature which is not represented by all zero's.
	 */
	xsave_state_booting(init_xstate_buf, -1);
}

static enum { AUTO, ENABLE, DISABLE } eagerfpu = AUTO;
static int __init eager_fpu_setup(char *s)
{
	if (!strcmp(s, "on"))
		eagerfpu = ENABLE;
	else if (!strcmp(s, "off"))
		eagerfpu = DISABLE;
	else if (!strcmp(s, "auto"))
		eagerfpu = AUTO;
	return 1;
}
__setup("eagerfpu=", eager_fpu_setup);


/*
 * Calculate total size of enabled xstates in XCR0/pcntxt_mask.
 */
static void __init init_xstate_size(void)
{
	unsigned int eax, ebx, ecx, edx;
	int i;

	if (!cpu_has_xsaves) {
		cpuid_count(XSTATE_CPUID, 0, &eax, &ebx, &ecx, &edx);
		xstate_size = ebx;
		return;
	}

	xstate_size = FXSAVE_SIZE + XSAVE_HDR_SIZE;
	for (i = 2; i < 64; i++) {
		if (test_bit(i, (unsigned long *)&pcntxt_mask)) {
			cpuid_count(XSTATE_CPUID, i, &eax, &ebx, &ecx, &edx);
			xstate_size += eax;
		}
	}
}

/*
 * Enable and initialize the xsave feature.
 */
static void __init xstate_enable_boot_cpu(void)
{
	unsigned int eax, ebx, ecx, edx;

	if (boot_cpu_data.cpuid_level < XSTATE_CPUID) {
		WARN(1, KERN_ERR "XSTATE_CPUID missing\n");
		return;
	}

	cpuid_count(XSTATE_CPUID, 0, &eax, &ebx, &ecx, &edx);
	pcntxt_mask = eax + ((u64)edx << 32);

	if ((pcntxt_mask & XSTATE_FPSSE) != XSTATE_FPSSE) {
		pr_err("FP/SSE not shown under xsave features 0x%llx\n",
		       pcntxt_mask);
		BUG();
	}

	/*
	 * Support only the state known to OS.
	 */
	pcntxt_mask = pcntxt_mask & XCNTXT_MASK;

	xstate_enable();

	/*
	 * Recompute the context size for enabled features
	 */
	init_xstate_size();

	update_regset_xstate_info(xstate_size, pcntxt_mask);
	prepare_fx_sw_frame();
	setup_init_fpu_buf();

	/* Auto enable eagerfpu for xsaveopt */
	if (cpu_has_xsaveopt && eagerfpu != DISABLE)
		eagerfpu = ENABLE;

	if (pcntxt_mask & XSTATE_EAGER) {
		if (eagerfpu == DISABLE) {
			pr_err("eagerfpu not present, disabling some xstate features: 0x%llx\n",
					pcntxt_mask & XSTATE_EAGER);
			pcntxt_mask &= ~XSTATE_EAGER;
		} else {
			eagerfpu = ENABLE;
		}
	}

	pr_info("enabled xstate_bv 0x%llx, cntxt size 0x%x using %s\n",
		pcntxt_mask, xstate_size,
		cpu_has_xsaves ? "compacted form" : "standard form");
}

/*
 * For the very first instance, this calls xstate_enable_boot_cpu();
 * for all subsequent instances, this calls xstate_enable().
 *
 * This is somewhat obfuscated due to the lack of powerful enough
 * overrides for the section checks.
 */
void xsave_init(void)
{
	static __refdata void (*next_func)(void) = xstate_enable_boot_cpu;
	void (*this_func)(void);

	if (!cpu_has_xsave)
		return;

	this_func = next_func;
	next_func = xstate_enable;
	this_func();
}

static inline void __init eager_fpu_init_bp(void)
{
	current->thread.fpu.state =
	    alloc_bootmem_align(xstate_size, __alignof__(struct xsave_struct));
	if (!init_xstate_buf)
		setup_init_fpu_buf();
}

void eager_fpu_init(void)
{
	static __refdata void (*boot_func)(void) = eager_fpu_init_bp;

	clear_used_math();
	current_thread_info()->status = 0;

	if (eagerfpu == ENABLE)
		setup_force_cpu_cap(X86_FEATURE_EAGER_FPU);

	if (!cpu_has_eager_fpu) {
		stts();
		return;
	}

	if (boot_func) {
		boot_func();
		boot_func = NULL;
	}

	/*
	 * This is same as math_state_restore(). But use_xsave() is
	 * not yet patched to use math_state_restore().
	 */
	init_fpu(current);
	__thread_fpu_begin(current);
	if (cpu_has_xsave)
		xrstor_state(init_xstate_buf, -1);
	else
		fxrstor_checking(&init_xstate_buf->i387);
}

/*
 * Given the xsave area and a state inside, this function returns the
 * address of the state.
 *
 * This is the API that is called to get xstate address in either
 * standard format or compacted format of xsave area.
 *
 * Inputs:
 *	xsave: base address of the xsave area;
 *	xstate: state which is defined in xsave.h (e.g. XSTATE_FP, XSTATE_SSE,
 *	etc.)
 * Output:
 *	address of the state in the xsave area.
 */
void *get_xsave_addr(struct xsave_struct *xsave, int xstate)
{
	int feature = fls64(xstate) - 1;
	if (!test_bit(feature, (unsigned long *)&pcntxt_mask))
		return NULL;

	return (void *)xsave + xstate_comp_offsets[feature];
}
