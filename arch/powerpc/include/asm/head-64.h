#ifndef _ASM_POWERPC_HEAD_64_H
#define _ASM_POWERPC_HEAD_64_H

#include <asm/cache.h>

/*
 * We can't do CPP stringification and concatination directly into the section
 * name for some reason, so these macros can do it for us.
 */
.macro define_ftsec name
	.section ".head.text.\name\()","ax",@progbits
.endm
.macro define_data_ftsec name
	.section ".head.data.\name\()","a",@progbits
.endm
.macro use_ftsec name
	.section ".head.text.\name\()"
.endm

/*
 * Fixed (location) sections are used by opening fixed sections and emitting
 * fixed section entries into them before closing them. Multiple fixed sections
 * can be open at any time.
 *
 * Each fixed section created in a .S file must have corresponding linkage
 * directives including location, added to  arch/powerpc/kernel/vmlinux.lds.S
 *
 * For each fixed section, code is generated into it in the order which it
 * appears in the source.  Fixed section entries can be placed at a fixed
 * location within the section using _LOCATION postifx variants. These must
 * be ordered according to their relative placements within the section.
 *
 * OPEN_FIXED_SECTION(section_name, start_address, end_address)
 * FIXED_SECTION_ENTRY_BEGIN(section_name, label1)
 *
 * USE_FIXED_SECTION(section_name)
 * label3:
 *     li  r10,128
 *     mv  r11,r10

 * FIXED_SECTION_ENTRY_BEGIN_LOCATION(section_name, label2, start_address)
 * FIXED_SECTION_ENTRY_END_LOCATION(section_name, label2, end_address)
 * CLOSE_FIXED_SECTION(section_name)
 *
 * ZERO_FIXED_SECTION can be used to emit zeroed data.
 *
 * Troubleshooting:
 * - If the build dies with "Error: attempt to move .org backwards" at
 *   CLOSE_FIXED_SECTION() or elsewhere, there may be something
 *   unexpected being added there. Remove the '. = x_len' line, rebuild, and
 *   check what is pushing the section down.
 * - If the build dies in linking, check arch/powerpc/kernel/vmlinux.lds.S
 *   for instructions.
 * - If the kernel crashes or hangs in very early boot, it could be linker
 *   stubs at the start of the main text.
 */

#define OPEN_FIXED_SECTION(sname, start, end)			\
	sname##_start = (start);				\
	sname##_end = (end);					\
	sname##_len = (end) - (start);				\
	define_ftsec sname;					\
	. = 0x0;						\
start_##sname:

#define OPEN_TEXT_SECTION(start)				\
	text_start = (start);					\
	.section ".text","ax",@progbits;			\
	. = 0x0;						\
start_text:

#define ZERO_FIXED_SECTION(sname, start, end)			\
	sname##_start = (start);				\
	sname##_end = (end);					\
	sname##_len = (end) - (start);				\
	define_data_ftsec sname;				\
	. = 0x0;						\
	. = sname##_len;

#define USE_FIXED_SECTION(sname)				\
	fs_label = start_##sname;				\
	fs_start = sname##_start;				\
	use_ftsec sname;

#define USE_TEXT_SECTION()					\
	fs_label = start_text;					\
	fs_start = text_start;					\
	.text

#define CLOSE_FIXED_SECTION(sname)				\
	USE_FIXED_SECTION(sname);				\
	. = sname##_len;					\
end_##sname:


#define __FIXED_SECTION_ENTRY_BEGIN(sname, name, __align)	\
	USE_FIXED_SECTION(sname);				\
	.align __align;						\
	.global name;						\
name:

#define FIXED_SECTION_ENTRY_BEGIN(sname, name)			\
	__FIXED_SECTION_ENTRY_BEGIN(sname, name, 0)

#define FIXED_SECTION_ENTRY_BEGIN_LOCATION(sname, name, start)		\
	USE_FIXED_SECTION(sname);				\
	name##_start = (start);					\
	.if (start) < sname##_start;				\
	.error "Fixed section underflow";			\
	.abort;							\
	.endif;							\
	. = (start) - sname##_start;				\
	.global name;						\
name:

#define FIXED_SECTION_ENTRY_END_LOCATION(sname, name, end)		\
	.if (end) > sname##_end;				\
	.error "Fixed section overflow";			\
	.abort;							\
	.endif;							\
	.if (. - name > end - name##_start);			\
	.error "Fixed entry overflow";				\
	.abort;							\
	.endif;							\
	. = ((end) - sname##_start);				\


/*
 * These macros are used to change symbols in other fixed sections to be
 * absolute or related to our current fixed section.
 *
 * - DEFINE_FIXED_SYMBOL / FIXED_SYMBOL_ABS_ADDR is used to find the
 *   absolute address of a symbol within a fixed section, from any section.
 *
 * - ABS_ADDR is used to find the absolute address of any symbol, from within
 *   a fixed section.
 */
#define DEFINE_FIXED_SYMBOL(label)				\
	label##_absolute = (label - fs_label + fs_start)

#define FIXED_SYMBOL_ABS_ADDR(label)				\
	(label##_absolute)

#define ABS_ADDR(label) (label - fs_label + fs_start)

/*
 * Following are the BOOK3S exception handler helper macros.
 * Handlers come in a number of types, and each type has a number of varieties.
 *
 * EXC_REAL_*        - real, unrelocated exception vectors
 * EXC_VIRT_*        - virt (AIL), unrelocated exception vectors
 * TRAMP_REAL_*   - real, unrelocated helpers (virt can call these)
 * TRAMP_VIRT_*  - virt, unreloc helpers (in practice, real can use)
 * TRAMP_KVM         - KVM handlers that get put into real, unrelocated
 * EXC_COMMON_*  - virt, relocated common handlers
 *
 * The EXC handlers are given a name, and branch to name_common, or the
 * appropriate KVM or masking function. Vector handler verieties are as
 * follows:
 *
 * EXC_{REAL|VIRT}_BEGIN/END - used to open-code the exception
 *
 * EXC_{REAL|VIRT}  - standard exception
 *
 * EXC_{REAL|VIRT}_suffix
 *     where _suffix is:
 *   - _MASKABLE               - maskable exception
 *   - _OOL                    - out of line with trampoline to common handler
 *   - _HV                     - HV exception
 *
 * There can be combinations, e.g., EXC_VIRT_OOL_MASKABLE_HV
 *
 * The one unusual case is __EXC_REAL_OOL_HV_DIRECT, which is
 * an OOL vector that branches to a specified handler rather than the usual
 * trampoline that goes to common. It, and other underscore macros, should
 * be used with care.
 *
 * KVM handlers come in the following verieties:
 * TRAMP_KVM
 * TRAMP_KVM_SKIP
 * TRAMP_KVM_HV
 * TRAMP_KVM_HV_SKIP
 *
 * COMMON handlers come in the following verieties:
 * EXC_COMMON_BEGIN/END - used to open-code the handler
 * EXC_COMMON
 * EXC_COMMON_ASYNC
 * EXC_COMMON_HV
 *
 * TRAMP_REAL and TRAMP_VIRT can be used with BEGIN/END. KVM
 * and OOL handlers are implemented as types of TRAMP and TRAMP_VIRT handlers.
 */

#define EXC_REAL_BEGIN(name, start, end)			\
	FIXED_SECTION_ENTRY_BEGIN_LOCATION(real_vectors, exc_real_##start##_##name, start)

#define EXC_REAL_END(name, start, end)			\
	FIXED_SECTION_ENTRY_END_LOCATION(real_vectors, exc_real_##start##_##name, end)

#define EXC_VIRT_BEGIN(name, start, end)			\
	FIXED_SECTION_ENTRY_BEGIN_LOCATION(virt_vectors, exc_virt_##start##_##name, start)

#define EXC_VIRT_END(name, start, end)			\
	FIXED_SECTION_ENTRY_END_LOCATION(virt_vectors, exc_virt_##start##_##name, end)

#define EXC_COMMON_BEGIN(name)					\
	USE_TEXT_SECTION();						\
	.align	7;							\
	.global name;							\
	DEFINE_FIXED_SYMBOL(name);					\
name:

#define TRAMP_REAL_BEGIN(name)					\
	FIXED_SECTION_ENTRY_BEGIN(real_trampolines, name)

#define TRAMP_VIRT_BEGIN(name)					\
	FIXED_SECTION_ENTRY_BEGIN(virt_trampolines, name)

#ifdef CONFIG_KVM_BOOK3S_64_HANDLER
#define TRAMP_KVM_BEGIN(name)						\
	TRAMP_REAL_BEGIN(name)
#else
#define TRAMP_KVM_BEGIN(name)
#endif

#define EXC_REAL_NONE(start, end)				\
	FIXED_SECTION_ENTRY_BEGIN_LOCATION(real_vectors, exc_real_##start##_##unused, start); \
	FIXED_SECTION_ENTRY_END_LOCATION(real_vectors, exc_real_##start##_##unused, end)

#define EXC_VIRT_NONE(start, end)				\
	FIXED_SECTION_ENTRY_BEGIN_LOCATION(virt_vectors, exc_virt_##start##_##unused, start); \
	FIXED_SECTION_ENTRY_END_LOCATION(virt_vectors, exc_virt_##start##_##unused, end);


#define EXC_REAL(name, start, end)				\
	EXC_REAL_BEGIN(name, start, end);			\
	STD_EXCEPTION_PSERIES(start, name##_common);			\
	EXC_REAL_END(name, start, end);

#define EXC_VIRT(name, start, end, realvec)			\
	EXC_VIRT_BEGIN(name, start, end);			\
	STD_RELON_EXCEPTION_PSERIES(start, realvec, name##_common);	\
	EXC_VIRT_END(name, start, end);

#define EXC_REAL_MASKABLE(name, start, end)			\
	EXC_REAL_BEGIN(name, start, end);			\
	MASKABLE_EXCEPTION_PSERIES(start, start, name##_common);	\
	EXC_REAL_END(name, start, end);

#define EXC_VIRT_MASKABLE(name, start, end, realvec)		\
	EXC_VIRT_BEGIN(name, start, end);			\
	MASKABLE_RELON_EXCEPTION_PSERIES(start, realvec, name##_common); \
	EXC_VIRT_END(name, start, end);

#define EXC_REAL_HV(name, start, end)			\
	EXC_REAL_BEGIN(name, start, end);			\
	STD_EXCEPTION_HV(start, start, name##_common);			\
	EXC_REAL_END(name, start, end);

#define EXC_VIRT_HV(name, start, end, realvec)		\
	EXC_VIRT_BEGIN(name, start, end);			\
	STD_RELON_EXCEPTION_HV(start, realvec, name##_common);		\
	EXC_VIRT_END(name, start, end);

#define __EXC_REAL_OOL(name, start, end)			\
	EXC_REAL_BEGIN(name, start, end);			\
	__OOL_EXCEPTION(start, label, tramp_real_##name);		\
	EXC_REAL_END(name, start, end);

#define __TRAMP_REAL_REAL_OOL(name, vec)				\
	TRAMP_REAL_BEGIN(tramp_real_##name);				\
	STD_EXCEPTION_PSERIES_OOL(vec, name##_common);			\

#define EXC_REAL_OOL(name, start, end)			\
	__EXC_REAL_OOL(name, start, end);			\
	__TRAMP_REAL_REAL_OOL(name, start);

#define __EXC_REAL_OOL_MASKABLE(name, start, end)		\
	__EXC_REAL_OOL(name, start, end);

#define __TRAMP_REAL_REAL_OOL_MASKABLE(name, vec)			\
	TRAMP_REAL_BEGIN(tramp_real_##name);				\
	MASKABLE_EXCEPTION_PSERIES_OOL(vec, name##_common);		\

#define EXC_REAL_OOL_MASKABLE(name, start, end)		\
	__EXC_REAL_OOL_MASKABLE(name, start, end);		\
	__TRAMP_REAL_REAL_OOL_MASKABLE(name, start);

#define __EXC_REAL_OOL_HV_DIRECT(name, start, end, handler)	\
	EXC_REAL_BEGIN(name, start, end);			\
	__OOL_EXCEPTION(start, label, handler);				\
	EXC_REAL_END(name, start, end);

#define __EXC_REAL_OOL_HV(name, start, end)			\
	__EXC_REAL_OOL(name, start, end);

#define __TRAMP_REAL_REAL_OOL_HV(name, vec)				\
	TRAMP_REAL_BEGIN(tramp_real_##name);				\
	STD_EXCEPTION_HV_OOL(vec, name##_common);			\

#define EXC_REAL_OOL_HV(name, start, end)			\
	__EXC_REAL_OOL_HV(name, start, end);			\
	__TRAMP_REAL_REAL_OOL_HV(name, start);

#define __EXC_REAL_OOL_MASKABLE_HV(name, start, end)		\
	__EXC_REAL_OOL(name, start, end);

#define __TRAMP_REAL_REAL_OOL_MASKABLE_HV(name, vec)			\
	TRAMP_REAL_BEGIN(tramp_real_##name);				\
	MASKABLE_EXCEPTION_HV_OOL(vec, name##_common);			\

#define EXC_REAL_OOL_MASKABLE_HV(name, start, end)		\
	__EXC_REAL_OOL_MASKABLE_HV(name, start, end);	\
	__TRAMP_REAL_REAL_OOL_MASKABLE_HV(name, start);

#define __EXC_VIRT_OOL(name, start, end)			\
	EXC_VIRT_BEGIN(name, start, end);			\
	__OOL_EXCEPTION(start, label, tramp_virt_##name);		\
	EXC_VIRT_END(name, start, end);

#define __TRAMP_REAL_VIRT_OOL(name, realvec)				\
	TRAMP_VIRT_BEGIN(tramp_virt_##name);			\
	STD_RELON_EXCEPTION_PSERIES_OOL(realvec, name##_common);	\

#define EXC_VIRT_OOL(name, start, end, realvec)		\
	__EXC_VIRT_OOL(name, start, end);			\
	__TRAMP_REAL_VIRT_OOL(name, realvec);

#define __EXC_VIRT_OOL_MASKABLE(name, start, end)		\
	__EXC_VIRT_OOL(name, start, end);

#define __TRAMP_REAL_VIRT_OOL_MASKABLE(name, realvec)		\
	TRAMP_VIRT_BEGIN(tramp_virt_##name);			\
	MASKABLE_RELON_EXCEPTION_PSERIES_OOL(realvec, name##_common);	\

#define EXC_VIRT_OOL_MASKABLE(name, start, end, realvec)	\
	__EXC_VIRT_OOL_MASKABLE(name, start, end);		\
	__TRAMP_REAL_VIRT_OOL_MASKABLE(name, realvec);

#define __EXC_VIRT_OOL_HV(name, start, end)			\
	__EXC_VIRT_OOL(name, start, end);

#define __TRAMP_REAL_VIRT_OOL_HV(name, realvec)			\
	TRAMP_VIRT_BEGIN(tramp_virt_##name);			\
	STD_RELON_EXCEPTION_HV_OOL(realvec, name##_common);		\

#define EXC_VIRT_OOL_HV(name, start, end, realvec)		\
	__EXC_VIRT_OOL_HV(name, start, end);			\
	__TRAMP_REAL_VIRT_OOL_HV(name, realvec);

#define __EXC_VIRT_OOL_MASKABLE_HV(name, start, end)		\
	__EXC_VIRT_OOL(name, start, end);

#define __TRAMP_REAL_VIRT_OOL_MASKABLE_HV(name, realvec)		\
	TRAMP_VIRT_BEGIN(tramp_virt_##name);			\
	MASKABLE_RELON_EXCEPTION_HV_OOL(realvec, name##_common);	\

#define EXC_VIRT_OOL_MASKABLE_HV(name, start, end, realvec)	\
	__EXC_VIRT_OOL_MASKABLE_HV(name, start, end);	\
	__TRAMP_REAL_VIRT_OOL_MASKABLE_HV(name, realvec);

#define TRAMP_KVM(area, n)						\
	TRAMP_KVM_BEGIN(do_kvm_##n);					\
	KVM_HANDLER(area, EXC_STD, n);					\

#define TRAMP_KVM_SKIP(area, n)						\
	TRAMP_KVM_BEGIN(do_kvm_##n);					\
	KVM_HANDLER_SKIP(area, EXC_STD, n);				\

/*
 * HV variant exceptions get the 0x2 bit added to their trap number.
 */
#define TRAMP_KVM_HV(area, n)						\
	TRAMP_KVM_BEGIN(do_kvm_H##n);					\
	KVM_HANDLER(area, EXC_HV, n + 0x2);				\

#define TRAMP_KVM_HV_SKIP(area, n)					\
	TRAMP_KVM_BEGIN(do_kvm_H##n);					\
	KVM_HANDLER_SKIP(area, EXC_HV, n + 0x2);			\

#define EXC_COMMON(name, realvec, hdlr)				\
	EXC_COMMON_BEGIN(name);					\
	STD_EXCEPTION_COMMON(realvec, name, hdlr);			\

#define EXC_COMMON_ASYNC(name, realvec, hdlr)			\
	EXC_COMMON_BEGIN(name);					\
	STD_EXCEPTION_COMMON_ASYNC(realvec, name, hdlr);		\

#define EXC_COMMON_HV(name, realvec, hdlr)				\
	EXC_COMMON_BEGIN(name);					\
	STD_EXCEPTION_COMMON(realvec + 0x2, name, hdlr);		\

#endif	/* _ASM_POWERPC_HEAD_64_H */
