/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_S390_BUG_H
#define _ASM_S390_BUG_H

#include <linux/compiler.h>
#include <linux/const.h>

#define	MONCODE_BUG	_AC(0, U)
#define	MONCODE_BUG_ARG _AC(1, U)

#ifndef __ASSEMBLER__
#if defined(CONFIG_BUG) && defined(CONFIG_CC_HAS_ASM_IMMEDIATE_STRINGS)

#ifdef CONFIG_DEBUG_BUGVERBOSE
#define __BUG_ENTRY_VERBOSE(format, file, line)				\
	"	.long	" format " - .	# bug_entry::format\n"		\
	"	.long	" file " - .	# bug_entry::file\n"		\
	"	.short	" line "	# bug_entry::line\n"
#else
#define __BUG_ENTRY_VERBOSE(format, file, line)
#endif

#ifdef CONFIG_DEBUG_BUGVERBOSE_DETAILED
#define WARN_CONDITION_STR(cond_str) cond_str
#else
#define WARN_CONDITION_STR(cond_str) ""
#endif

#define __BUG_ENTRY(format, file, line, flags, size)			\
		"	.section __bug_table,\"aw\"\n"			\
		"1:	.long	0b - .	# bug_entry::bug_addr\n"	\
		__BUG_ENTRY_VERBOSE(format, file, line)			\
		"	.short	"flags"	# bug_entry::flags\n"		\
		"	.org	1b+"size"\n"				\
		"	.previous"

#define __BUG_ASM(cond_str, flags)					\
do {									\
	asm_inline volatile("\n"					\
		"0:	mc	%[monc](%%r0),0\n"			\
		__BUG_ENTRY("%[frmt]", "%[file]", "%[line]",		\
			    "%[flgs]", "%[size]")			\
		:							\
		: [monc] "i" (MONCODE_BUG),				\
		  [frmt] "i" (WARN_CONDITION_STR(cond_str)),		\
		  [file] "i" (__FILE__),				\
		  [line] "i" (__LINE__),				\
		  [flgs] "i" (flags),					\
		  [size] "i" (sizeof(struct bug_entry)));		\
} while (0)

#define BUG()								\
do {									\
	__BUG_ASM("", 0);						\
	unreachable();							\
} while (0)

#define __WARN_FLAGS(cond_str, flags)					\
do {									\
	__BUG_ASM(cond_str, BUGFLAG_WARNING | (flags));			\
} while (0)

#define __WARN_bug_entry(flags, format)					\
({									\
	struct bug_entry *bug;						\
									\
	asm_inline volatile("\n"					\
		"0:	larl	%[bug],1f\n"				\
		__BUG_ENTRY("%[frmt]", "%[file]", "%[line]",		\
			    "%[flgs]", "%[size]")			\
		: [bug] "=d" (bug)					\
		: [frmt] "i" (format),					\
		  [file] "i" (__FILE__),				\
		  [line] "i" (__LINE__),				\
		  [flgs] "i" (flags),					\
		  [size] "i" (sizeof(struct bug_entry)));		\
	bug;								\
})

/*
 * Variable Argument List (va_list) as defined in ELF Application
 * Binary Interface s390x Supplement documentation.
 */
struct arch_va_list {
	long __gpr;
	long __fpr;
	void *__overflow_arg_area;
	void *__reg_save_area;
};

struct bug_entry;
struct pt_regs;

void *__warn_args(struct arch_va_list *args, struct pt_regs *regs);
void __WARN_trap(struct bug_entry *bug, ...);

#define __WARN_print_arg(flags, format, arg...)				\
do {									\
	int __flags = (flags) | BUGFLAG_WARNING | BUGFLAG_ARGS;		\
									\
	__WARN_trap(__WARN_bug_entry(__flags, format), ## arg);		\
	/* prevent tail-call optimization */				\
	asm("");							\
} while (0)

#define __WARN_printf(taint, fmt, arg...) \
	__WARN_print_arg(BUGFLAG_TAINT(taint), fmt, ## arg)

#define WARN_ONCE(cond, format, arg...)					\
({									\
	int __ret_warn_on = !!(cond);					\
									\
	if (unlikely(__ret_warn_on)) {					\
		__WARN_print_arg(BUGFLAG_ONCE|BUGFLAG_TAINT(TAINT_WARN),\
				format, ## arg);			\
	}								\
	__ret_warn_on;							\
})

#define HAVE_ARCH_BUG
#define HAVE_ARCH_BUG_FORMAT
#define HAVE_ARCH_BUG_FORMAT_ARGS

#endif /* CONFIG_BUG && CONFIG_CC_HAS_ASM_IMMEDIATE_STRINGS */
#endif /* __ASSEMBLER__ */

#include <asm-generic/bug.h>

#endif /* _ASM_S390_BUG_H */
