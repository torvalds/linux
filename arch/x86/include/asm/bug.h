/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_BUG_H
#define _ASM_X86_BUG_H

#include <linux/stringify.h>
#include <linux/instrumentation.h>
#include <linux/objtool.h>
#include <asm/asm.h>

#ifndef __ASSEMBLY__
struct bug_entry;
extern void __WARN_trap(struct bug_entry *bug, ...);
#endif

/*
 * Despite that some emulators terminate on UD2, we use it for WARN().
 */
#define ASM_UD2		__ASM_FORM(ud2)
#define INSN_UD2	0x0b0f
#define LEN_UD2		2

#define ASM_UDB		_ASM_BYTES(0xd6)
#define INSN_UDB	0xd6
#define LEN_UDB		1

/*
 * In clang we have UD1s reporting UBSAN failures on X86, 64 and 32bit.
 */
#define INSN_ASOP		0x67
#define INSN_LOCK		0xf0
#define OPCODE_ESCAPE		0x0f
#define SECOND_BYTE_OPCODE_UD1	0xb9
#define SECOND_BYTE_OPCODE_UD2	0x0b

#define BUG_NONE		0xffff
#define BUG_UD2			0xfffe
#define BUG_UD1			0xfffd
#define BUG_UD1_UBSAN		0xfffc
#define BUG_UD1_WARN		0xfffb
#define BUG_UDB			0xffd6
#define BUG_LOCK		0xfff0

#ifdef CONFIG_GENERIC_BUG

#ifdef CONFIG_DEBUG_BUGVERBOSE
#define __BUG_ENTRY_VERBOSE(file, line)					\
	"\t.long " file " - .\t# bug_entry::file\n"			\
	"\t.word " line     "\t# bug_entry::line\n"
#else
#define __BUG_ENTRY_VERBOSE(file, line)
#endif

#if defined(CONFIG_X86_64) || defined(CONFIG_DEBUG_BUGVERBOSE_DETAILED)
#define HAVE_ARCH_BUG_FORMAT
#define __BUG_ENTRY_FORMAT(format)					\
	"\t.long " format " - .\t# bug_entry::format\n"
#else
#define __BUG_ENTRY_FORMAT(format)
#endif

#ifdef CONFIG_X86_64
#define HAVE_ARCH_BUG_FORMAT_ARGS
#endif

#define __BUG_ENTRY(format, file, line, flags)				\
	"\t.long 1b - ."	"\t# bug_entry::bug_addr\n"		\
	__BUG_ENTRY_FORMAT(format)					\
	__BUG_ENTRY_VERBOSE(file, line)					\
	"\t.word " flags	"\t# bug_entry::flags\n"

#define _BUG_FLAGS_ASM(format, file, line, flags, size, extra)		\
	".pushsection __bug_table,\"aw\"\n\t"				\
	ANNOTATE_DATA_SPECIAL "\n\t"					\
	"2:\n\t"							\
	__BUG_ENTRY(format, file, line, flags)				\
	"\t.org 2b + " size "\n"					\
	".popsection\n"							\
	extra

#ifdef CONFIG_DEBUG_BUGVERBOSE_DETAILED
#define WARN_CONDITION_STR(cond_str) cond_str
#else
#define WARN_CONDITION_STR(cond_str) ""
#endif

#define _BUG_FLAGS(cond_str, ins, flags, extra)				\
do {									\
	asm_inline volatile("1:\t" ins "\n"				\
			    _BUG_FLAGS_ASM("%c[fmt]", "%c[file]",	\
					   "%c[line]", "%c[fl]",	\
					   "%c[size]", extra)		\
		   : : [fmt] "i" (WARN_CONDITION_STR(cond_str)),	\
		       [file] "i" (__FILE__),				\
		       [line] "i" (__LINE__),				\
		       [fl] "i" (flags),				\
		       [size] "i" (sizeof(struct bug_entry)));		\
} while (0)

#define ARCH_WARN_ASM(file, line, flags, size)				\
	".pushsection .rodata.str1.1, \"aMS\", @progbits, 1\n"		\
	"99:\n"								\
	"\t.string \"\"\n"						\
	".popsection\n"							\
	"1:\t " ASM_UD2 "\n"						\
	_BUG_FLAGS_ASM("99b", file, line, flags, size, "")

#else

#define _BUG_FLAGS(cond_str, ins, flags, extra)  asm volatile(ins)

#endif /* CONFIG_GENERIC_BUG */

#define HAVE_ARCH_BUG
#define BUG()							\
do {								\
	instrumentation_begin();				\
	_BUG_FLAGS("", ASM_UD2, 0, "");				\
	__builtin_unreachable();				\
} while (0)

/*
 * This instrumentation_begin() is strictly speaking incorrect; but it
 * suppresses the complaints from WARN()s in noinstr code. If such a WARN()
 * were to trigger, we'd rather wreck the machine in an attempt to get the
 * message out than not know about it.
 */

#define ARCH_WARN_REACHABLE	ANNOTATE_REACHABLE(1b)

#define __WARN_FLAGS(cond_str, flags)					\
do {									\
	auto __flags = BUGFLAG_WARNING|(flags);				\
	instrumentation_begin();					\
	_BUG_FLAGS(cond_str, ASM_UD2, __flags, ARCH_WARN_REACHABLE);	\
	instrumentation_end();						\
} while (0)

#ifdef HAVE_ARCH_BUG_FORMAT_ARGS

#ifndef __ASSEMBLY__
#include <linux/static_call_types.h>
DECLARE_STATIC_CALL(WARN_trap, __WARN_trap);

struct pt_regs;
struct sysv_va_list { /* from AMD64 System V ABI */
	unsigned int gp_offset;
	unsigned int fp_offset;
	void *overflow_arg_area;
	void *reg_save_area;
};
struct arch_va_list {
	unsigned long regs[6];
	struct sysv_va_list args;
};
extern void *__warn_args(struct arch_va_list *args, struct pt_regs *regs);
#endif /* __ASSEMBLY__ */

#define __WARN_bug_entry(flags, format) ({				\
	struct bug_entry *bug;						\
	asm_inline volatile("lea (2f)(%%rip), %[addr]\n1:\n"		\
			    _BUG_FLAGS_ASM("%c[fmt]", "%c[file]",	\
					   "%c[line]", "%c[fl]",	\
					   "%c[size]", "")		\
		   : [addr] "=r" (bug)					\
		   : [fmt] "i" (format),				\
		     [file] "i" (__FILE__),				\
		     [line] "i" (__LINE__),				\
		     [fl] "i" (flags),					\
		     [size] "i" (sizeof(struct bug_entry)));		\
	bug; })

#define __WARN_print_arg(flags, format, arg...)				\
do {									\
	int __flags = (flags) | BUGFLAG_WARNING | BUGFLAG_ARGS ;	\
	static_call_mod(WARN_trap)(__WARN_bug_entry(__flags, format), ## arg); \
	asm (""); /* inhibit tail-call optimization */			\
} while (0)

#define __WARN_printf(taint, fmt, arg...) \
	__WARN_print_arg(BUGFLAG_TAINT(taint), fmt, ## arg)

#define WARN_ONCE(cond, format, arg...) ({				\
	int __ret_warn_on = !!(cond);					\
	if (unlikely(__ret_warn_on)) {					\
		__WARN_print_arg(BUGFLAG_ONCE|BUGFLAG_TAINT(TAINT_WARN),\
				format, ## arg);			\
	}								\
	__ret_warn_on;							\
})

#endif /* HAVE_ARCH_BUG_FORMAT_ARGS */

#include <asm-generic/bug.h>

#endif /* _ASM_X86_BUG_H */
