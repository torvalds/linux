/*
 * vvar.h: Shared vDSO/kernel variable declarations
 * Copyright (c) 2011 Andy Lutomirski
 * Subject to the GNU General Public License, version 2
 *
 * A handful of variables are accessible (read-only) from userspace
 * code in the vsyscall page and the vdso.  They are declared here.
 * Some other file must define them with DEFINE_VVAR.
 *
 * In normal kernel code, they are used like any other variable.
 * In user code, they are accessed through the VVAR macro.
 *
 * Each of these variables lives in the vsyscall page, and each
 * one needs a unique offset within the little piece of the page
 * reserved for vvars.  Specify that offset in DECLARE_VVAR.
 * (There are 896 bytes available.  If you mess up, the linker will
 * catch it.)
 */

/* Offset of vars within vsyscall page */
#define VSYSCALL_VARS_OFFSET (3072 + 128)

#if defined(__VVAR_KERNEL_LDS)

/* The kernel linker script defines its own magic to put vvars in the
 * right place.
 */
#define DECLARE_VVAR(offset, type, name) \
	EMIT_VVAR(name, VSYSCALL_VARS_OFFSET + offset)

#else

#define DECLARE_VVAR(offset, type, name)				\
	static type const * const vvaraddr_ ## name =			\
		(void *)(VSYSCALL_START + VSYSCALL_VARS_OFFSET + (offset));

#define DEFINE_VVAR(type, name)						\
	type __vvar_ ## name						\
	__attribute__((section(".vsyscall_var_" #name), aligned(16)))

#define VVAR(name) (*vvaraddr_ ## name)

#endif

/* DECLARE_VVAR(offset, type, name) */

DECLARE_VVAR(0, volatile unsigned long, jiffies)
DECLARE_VVAR(8, int, vgetcpu_mode)
DECLARE_VVAR(128, struct vsyscall_gtod_data, vsyscall_gtod_data)

#undef DECLARE_VVAR
#undef VSYSCALL_VARS_OFFSET
