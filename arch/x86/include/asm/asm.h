#ifndef _ASM_X86_ASM_H
#define _ASM_X86_ASM_H

#ifdef __ASSEMBLY__
# define __ASM_FORM(x)	x
# define __ASM_FORM_COMMA(x) x,
# define __ASM_EX_SEC	.section __ex_table, "a"
#else
# define __ASM_FORM(x)	" " #x " "
# define __ASM_FORM_COMMA(x) " " #x ","
# define __ASM_EX_SEC	" .section __ex_table,\"a\"\n"
#endif

#ifdef CONFIG_X86_32
# define __ASM_SEL(a,b) __ASM_FORM(a)
#else
# define __ASM_SEL(a,b) __ASM_FORM(b)
#endif

#define __ASM_SIZE(inst, ...)	__ASM_SEL(inst##l##__VA_ARGS__, \
					  inst##q##__VA_ARGS__)
#define __ASM_REG(reg)		__ASM_SEL(e##reg, r##reg)

#define _ASM_PTR	__ASM_SEL(.long, .quad)
#define _ASM_ALIGN	__ASM_SEL(.balign 4, .balign 8)

#define _ASM_MOV	__ASM_SIZE(mov)
#define _ASM_INC	__ASM_SIZE(inc)
#define _ASM_DEC	__ASM_SIZE(dec)
#define _ASM_ADD	__ASM_SIZE(add)
#define _ASM_SUB	__ASM_SIZE(sub)
#define _ASM_XADD	__ASM_SIZE(xadd)

#define _ASM_AX		__ASM_REG(ax)
#define _ASM_BX		__ASM_REG(bx)
#define _ASM_CX		__ASM_REG(cx)
#define _ASM_DX		__ASM_REG(dx)
#define _ASM_SP		__ASM_REG(sp)
#define _ASM_BP		__ASM_REG(bp)
#define _ASM_SI		__ASM_REG(si)
#define _ASM_DI		__ASM_REG(di)

/* Exception table entry */
#ifdef __ASSEMBLY__
# define _ASM_EXTABLE(from,to)	    \
	__ASM_EX_SEC ;		    \
	_ASM_ALIGN ;		    \
	_ASM_PTR from , to ;	    \
	.previous
#else
# define _ASM_EXTABLE(from,to) \
	__ASM_EX_SEC	\
	_ASM_ALIGN "\n" \
	_ASM_PTR #from "," #to "\n" \
	" .previous\n"
#endif

#endif /* _ASM_X86_ASM_H */
