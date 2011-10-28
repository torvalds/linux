#ifndef _ASM_PROC_INTCTL_REGS_H
#define _ASM_PROC_INTCTL_REGS_H

#ifndef _ASM_INTCTL_REGS_H
# error "please don't include this file directly"
#endif

/* intr acceptance group reg */
#define IAGR			__SYSREG(0xd4000100, u16)

/* group number register */
#define IAGR_GN			0x003fc

#define __GET_XIRQ_TRIGGER(X, Z) (((Z) >> ((X) * 2)) & 3)

#define __SET_XIRQ_TRIGGER(X, Y, Z)		\
({						\
	typeof(Z) x = (Z);			\
	x &= ~(3 << ((X) * 2));			\
	x |= ((Y) & 3) << ((X) * 2);		\
	(Z) = x;				\
})

/* external pin intr spec reg */
#define EXTMD0			__SYSREG(0xd4000200, u32)
#define GET_XIRQ_TRIGGER(X)	__GET_XIRQ_TRIGGER(X, EXTMD0)
#define SET_XIRQ_TRIGGER(X, Y)	__SET_XIRQ_TRIGGER(X, Y, EXTMD0)

#endif /* _ASM_PROC_INTCTL_REGS_H */
