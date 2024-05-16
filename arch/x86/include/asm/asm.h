/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_ASM_H
#define _ASM_X86_ASM_H

#ifdef __ASSEMBLY__
# define __ASM_FORM(x, ...)		x,## __VA_ARGS__
# define __ASM_FORM_RAW(x, ...)		x,## __VA_ARGS__
# define __ASM_FORM_COMMA(x, ...)	x,## __VA_ARGS__,
# define __ASM_REGPFX			%
#else
#include <linux/stringify.h>
# define __ASM_FORM(x, ...)		" " __stringify(x,##__VA_ARGS__) " "
# define __ASM_FORM_RAW(x, ...)		    __stringify(x,##__VA_ARGS__)
# define __ASM_FORM_COMMA(x, ...)	" " __stringify(x,##__VA_ARGS__) ","
# define __ASM_REGPFX			%%
#endif

#define _ASM_BYTES(x, ...)	__ASM_FORM(.byte x,##__VA_ARGS__ ;)

#ifndef __x86_64__
/* 32 bit */
# define __ASM_SEL(a,b)		__ASM_FORM(a)
# define __ASM_SEL_RAW(a,b)	__ASM_FORM_RAW(a)
#else
/* 64 bit */
# define __ASM_SEL(a,b)		__ASM_FORM(b)
# define __ASM_SEL_RAW(a,b)	__ASM_FORM_RAW(b)
#endif

#define __ASM_SIZE(inst, ...)	__ASM_SEL(inst##l##__VA_ARGS__, \
					  inst##q##__VA_ARGS__)
#define __ASM_REG(reg)         __ASM_SEL_RAW(e##reg, r##reg)

#define _ASM_PTR	__ASM_SEL(.long, .quad)
#define _ASM_ALIGN	__ASM_SEL(.balign 4, .balign 8)

#define _ASM_MOV	__ASM_SIZE(mov)
#define _ASM_INC	__ASM_SIZE(inc)
#define _ASM_DEC	__ASM_SIZE(dec)
#define _ASM_ADD	__ASM_SIZE(add)
#define _ASM_SUB	__ASM_SIZE(sub)
#define _ASM_XADD	__ASM_SIZE(xadd)
#define _ASM_MUL	__ASM_SIZE(mul)

#define _ASM_AX		__ASM_REG(ax)
#define _ASM_BX		__ASM_REG(bx)
#define _ASM_CX		__ASM_REG(cx)
#define _ASM_DX		__ASM_REG(dx)
#define _ASM_SP		__ASM_REG(sp)
#define _ASM_BP		__ASM_REG(bp)
#define _ASM_SI		__ASM_REG(si)
#define _ASM_DI		__ASM_REG(di)

/* Adds a (%rip) suffix on 64 bits only; for immediate memory references */
#define _ASM_RIP(x)	__ASM_SEL_RAW(x, x (__ASM_REGPFX rip))

#ifndef __x86_64__
/* 32 bit */

#define _ASM_ARG1	_ASM_AX
#define _ASM_ARG2	_ASM_DX
#define _ASM_ARG3	_ASM_CX

#define _ASM_ARG1L	eax
#define _ASM_ARG2L	edx
#define _ASM_ARG3L	ecx

#define _ASM_ARG1W	ax
#define _ASM_ARG2W	dx
#define _ASM_ARG3W	cx

#define _ASM_ARG1B	al
#define _ASM_ARG2B	dl
#define _ASM_ARG3B	cl

#else
/* 64 bit */

#define _ASM_ARG1	_ASM_DI
#define _ASM_ARG2	_ASM_SI
#define _ASM_ARG3	_ASM_DX
#define _ASM_ARG4	_ASM_CX
#define _ASM_ARG5	r8
#define _ASM_ARG6	r9

#define _ASM_ARG1Q	rdi
#define _ASM_ARG2Q	rsi
#define _ASM_ARG3Q	rdx
#define _ASM_ARG4Q	rcx
#define _ASM_ARG5Q	r8
#define _ASM_ARG6Q	r9

#define _ASM_ARG1L	edi
#define _ASM_ARG2L	esi
#define _ASM_ARG3L	edx
#define _ASM_ARG4L	ecx
#define _ASM_ARG5L	r8d
#define _ASM_ARG6L	r9d

#define _ASM_ARG1W	di
#define _ASM_ARG2W	si
#define _ASM_ARG3W	dx
#define _ASM_ARG4W	cx
#define _ASM_ARG5W	r8w
#define _ASM_ARG6W	r9w

#define _ASM_ARG1B	dil
#define _ASM_ARG2B	sil
#define _ASM_ARG3B	dl
#define _ASM_ARG4B	cl
#define _ASM_ARG5B	r8b
#define _ASM_ARG6B	r9b

#endif

#ifndef __ASSEMBLY__
#ifndef __pic__
static __always_inline __pure void *rip_rel_ptr(void *p)
{
	asm("leaq %c1(%%rip), %0" : "=r"(p) : "i"(p));

	return p;
}
#define RIP_REL_REF(var)	(*(typeof(&(var)))rip_rel_ptr(&(var)))
#else
#define RIP_REL_REF(var)	(var)
#endif
#endif

/*
 * Macros to generate condition code outputs from inline assembly,
 * The output operand must be type "bool".
 */
#ifdef __GCC_ASM_FLAG_OUTPUTS__
# define CC_SET(c) "\n\t/* output condition code " #c "*/\n"
# define CC_OUT(c) "=@cc" #c
#else
# define CC_SET(c) "\n\tset" #c " %[_cc_" #c "]\n"
# define CC_OUT(c) [_cc_ ## c] "=qm"
#endif

#ifdef __KERNEL__

# include <asm/extable_fixup_types.h>

/* Exception table entry */
#ifdef __ASSEMBLY__

# define _ASM_EXTABLE_TYPE(from, to, type)			\
	.pushsection "__ex_table","a" ;				\
	.balign 4 ;						\
	.long (from) - . ;					\
	.long (to) - . ;					\
	.long type ;						\
	.popsection

# ifdef CONFIG_KPROBES
#  define _ASM_NOKPROBE(entry)					\
	.pushsection "_kprobe_blacklist","aw" ;			\
	_ASM_ALIGN ;						\
	_ASM_PTR (entry);					\
	.popsection
# else
#  define _ASM_NOKPROBE(entry)
# endif

#else /* ! __ASSEMBLY__ */

# define DEFINE_EXTABLE_TYPE_REG \
	".macro extable_type_reg type:req reg:req\n"						\
	".set .Lfound, 0\n"									\
	".set .Lregnr, 0\n"									\
	".irp rs,rax,rcx,rdx,rbx,rsp,rbp,rsi,rdi,r8,r9,r10,r11,r12,r13,r14,r15\n"		\
	".ifc \\reg, %%\\rs\n"									\
	".set .Lfound, .Lfound+1\n"								\
	".long \\type + (.Lregnr << 8)\n"							\
	".endif\n"										\
	".set .Lregnr, .Lregnr+1\n"								\
	".endr\n"										\
	".set .Lregnr, 0\n"									\
	".irp rs,eax,ecx,edx,ebx,esp,ebp,esi,edi,r8d,r9d,r10d,r11d,r12d,r13d,r14d,r15d\n"	\
	".ifc \\reg, %%\\rs\n"									\
	".set .Lfound, .Lfound+1\n"								\
	".long \\type + (.Lregnr << 8)\n"							\
	".endif\n"										\
	".set .Lregnr, .Lregnr+1\n"								\
	".endr\n"										\
	".if (.Lfound != 1)\n"									\
	".error \"extable_type_reg: bad register argument\"\n"					\
	".endif\n"										\
	".endm\n"

# define UNDEFINE_EXTABLE_TYPE_REG \
	".purgem extable_type_reg\n"

# define _ASM_EXTABLE_TYPE(from, to, type)			\
	" .pushsection \"__ex_table\",\"a\"\n"			\
	" .balign 4\n"						\
	" .long (" #from ") - .\n"				\
	" .long (" #to ") - .\n"				\
	" .long " __stringify(type) " \n"			\
	" .popsection\n"

# define _ASM_EXTABLE_TYPE_REG(from, to, type, reg)				\
	" .pushsection \"__ex_table\",\"a\"\n"					\
	" .balign 4\n"								\
	" .long (" #from ") - .\n"						\
	" .long (" #to ") - .\n"						\
	DEFINE_EXTABLE_TYPE_REG							\
	"extable_type_reg reg=" __stringify(reg) ", type=" __stringify(type) " \n"\
	UNDEFINE_EXTABLE_TYPE_REG						\
	" .popsection\n"

/* For C file, we already have NOKPROBE_SYMBOL macro */

/*
 * This output constraint should be used for any inline asm which has a "call"
 * instruction.  Otherwise the asm may be inserted before the frame pointer
 * gets set up by the containing function.  If you forget to do this, objtool
 * may print a "call without frame pointer save/setup" warning.
 */
register unsigned long current_stack_pointer asm(_ASM_SP);
#define ASM_CALL_CONSTRAINT "+r" (current_stack_pointer)
#endif /* __ASSEMBLY__ */

#define _ASM_EXTABLE(from, to)					\
	_ASM_EXTABLE_TYPE(from, to, EX_TYPE_DEFAULT)

#define _ASM_EXTABLE_UA(from, to)				\
	_ASM_EXTABLE_TYPE(from, to, EX_TYPE_UACCESS)

#define _ASM_EXTABLE_CPY(from, to)				\
	_ASM_EXTABLE_TYPE(from, to, EX_TYPE_COPY)

#define _ASM_EXTABLE_FAULT(from, to)				\
	_ASM_EXTABLE_TYPE(from, to, EX_TYPE_FAULT)

#endif /* __KERNEL__ */
#endif /* _ASM_X86_ASM_H */
