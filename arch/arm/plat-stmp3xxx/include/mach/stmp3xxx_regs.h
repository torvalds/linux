/*
 * Freescale STMP37XX/STMP378X SoC register access interfaces
 *
 * The SoC registers may be accessed via:
 *
 * - single 32 bit address, or
 * - four 32 bit addresses - general purpose, set, clear and toggle bits
 *
 * Multiple IP blocks (e.g. SSP, UART) provide identical register sets per
 * each module
 *
 * Embedded Alley Solutions, Inc <source@embeddedalley.com>
 *
 * Copyright 2008 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Embedded Alley Solutions, Inc All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
#ifndef __ASM_PLAT_STMP3XXX_REGS_H
#define __ASM_PLAT_STMP3XXX_REGS_H

#ifndef __ASSEMBLER__
#include <linux/io.h>
#endif

#include "platform.h"

#define REGS_BASE STMP3XXX_REGS_BASE

#define HW_STMP3xxx_SET		0x04
#define HW_STMP3xxx_CLR		0x08
#define HW_STMP3xxx_TOG		0x0c

#ifndef __ASSEMBLER__
#define HW_REGISTER_FUNCS(id, base, offset, regset, rd, wr)		\
	static const u32 id##_OFFSET = offset;				\
	static inline u32 id##_RD_NB(const void __iomem *regbase) {	\
		if (!rd)						\
			printk(KERN_ERR"%s: cannot READ at %p+%x\n",	\
				#id, regbase, offset);			\
		return __raw_readl(regbase + offset);			\
	}								\
	static inline void id##_WR_NB(void __iomem *regbase, u32 v) {	\
		if (!wr)						\
			printk(KERN_ERR"%s: cannot WRITE at %p+%x\n",	\
				#id, regbase, offset);			\
		__raw_writel(v, regbase + offset);			\
	}								\
	static inline void id##_SET_NB(void __iomem *regbase, u32 v) {	\
		if (!wr)						\
			printk(KERN_ERR"%s: cannot SET at %p+%x\n",	\
				#id, regbase, offset);			\
		if (regset)						\
			__raw_writel(v, regbase + 			\
					offset + HW_STMP3xxx_SET);	\
		else							\
			__raw_writel(v | __raw_readl(regbase + offset),	\
				regbase + offset);			\
	}								\
	static inline void id##_CLR_NB(void __iomem *regbase, u32 v) {	\
		if (!wr) 						\
			printk(KERN_ERR"%s: cannot CLR at %p+%x\n",	\
				#id, regbase, offset);			\
		if (regset)						\
			__raw_writel(v, regbase + 			\
					offset + HW_STMP3xxx_CLR);	\
		else							\
			__raw_writel(					\
				~v & __raw_readl(regbase + offset),	\
				regbase + offset);			\
	}								\
	static inline void id##_TOG_NB(void __iomem *regbase, u32 v) {	\
		if (!wr) 						\
			printk(KERN_ERR"%s: cannot TOG at %p+%x\n",	\
				#id, regbase, offset);			\
		if (regset)						\
			__raw_writel(v, regbase + 			\
					offset + HW_STMP3xxx_TOG);	\
		else							\
			__raw_writel(v ^ __raw_readl(regbase + offset),	\
				regbase + offset);			\
	}								\
	static inline u32 id##_RD(void) { return id##_RD_NB(base); }	\
	static inline void id##_WR(u32 v) { id##_WR_NB(base, v); }	\
	static inline void id##_SET(u32 v) { id##_SET_NB(base, v); }	\
	static inline void id##_CLR(u32 v) { id##_CLR_NB(base, v); }	\
	static inline void id##_TOG(u32 v) { id##_TOG_NB(base, v); }

#define HW_REGISTER_FUNCS_INDEXED(id, base, offset, regset, rd, wr, step)\
	static inline u32 id##_OFFSET(int i) {				\
		return offset + i * step;				\
	}								\
	static inline u32 id##_RD_NB(const void __iomem *regbase, int i) {\
		if (!rd) 						\
			printk(KERN_ERR"%s(%d): can't READ at %p+%x\n",	\
				#id, i, regbase, offset + i * step);	\
		return __raw_readl(regbase + offset + i * step);	\
	}								\
	static inline void id##_WR_NB(void __iomem *regbase, int i, u32 v) {\
		if (!wr) 						\
			printk(KERN_ERR"%s(%d): can't WRITE at %p+%x\n",\
				#id, i, regbase, offset + i * step);	\
		__raw_writel(v, regbase + offset + i * step);		\
	}								\
	static inline void id##_SET_NB(void __iomem *regbase, int i, u32 v) {\
		if (!wr)						\
			printk(KERN_ERR"%s(%d): can't SET at %p+%x\n",	\
				#id, i, regbase, offset + i * step);	\
		if (regset)						\
			__raw_writel(v, regbase + offset + 		\
					i * step + HW_STMP3xxx_SET);	\
		else							\
			__raw_writel(v | __raw_readl(regbase + 		\
						offset + i * step),	\
				regbase + offset + i * step);		\
	}								\
	static inline void id##_CLR_NB(void __iomem *regbase, int i, u32 v) {\
		if (!wr) 						\
			printk(KERN_ERR"%s(%d): cannot CLR at %p+%x\n",	\
				#id, i, regbase, offset + i * step);	\
		if (regset)						\
			__raw_writel(v, regbase + offset + 		\
					i * step + HW_STMP3xxx_CLR);	\
		else							\
			__raw_writel(~v & __raw_readl(regbase + 	\
						offset + i * step),	\
				regbase + offset + i * step);		\
	}								\
	static inline void id##_TOG_NB(void __iomem *regbase, int i, u32 v) {\
		if (!wr) 						\
			printk(KERN_ERR"%s(%d): cannot TOG at %p+%x\n",	\
				#id, i, regbase, offset + i * step);	\
		if (regset)						\
			__raw_writel(v, regbase + offset + 		\
					i * step + HW_STMP3xxx_TOG);	\
		else							\
			__raw_writel(v ^ __raw_readl(regbase + offset 	\
						+ i * step),		\
				regbase + offset + i * step);		\
	}								\
	static inline u32 id##_RD(int i) 				\
	{ 								\
		return id##_RD_NB(base, i); 				\
	}								\
	static inline void id##_WR(int i, u32 v) 			\
	{ 								\
		id##_WR_NB(base, i, v); 				\
	}								\
	static inline void id##_SET(int i, u32 v) 			\
	{								\
		id##_SET_NB(base, i, v); 				\
	}								\
	static inline void id##_CLR(int i, u32 v) 			\
	{ 								\
		id##_CLR_NB(base, i, v);				\
	}								\
	static inline void id##_TOG(int i, u32 v) 			\
	{ 								\
		id##_TOG_NB(base, i, v); 				\
	}

#define HW_REGISTER_WO(id, base, offset)\
	HW_REGISTER_FUNCS(id, base, offset, 1,  0, 1)
#define HW_REGISTER_RO(id, base, offset)\
	HW_REGISTER_FUNCS(id, base, offset, 1,  1, 0)
#define HW_REGISTER(id, base, offset)	\
	HW_REGISTER_FUNCS(id, base, offset, 1,	1, 1)
#define HW_REGISTER_0(id, base, offset)	\
	HW_REGISTER_FUNCS(id, base, offset, 0,  1, 1)
#define HW_REGISTER_INDEXED(id, base, offset, step)	\
	HW_REGISTER_FUNCS_INDEXED(id, base, offset, 1,  1, 1, step)
#define HW_REGISTER_RO_INDEXED(id, base, offset, step)	\
	HW_REGISTER_FUNCS_INDEXED(id, base, offset, 1,  1, 0, step)
#define HW_REGISTER_0_INDEXED(id, base, offset, step)	\
	HW_REGISTER_FUNCS_INDEXED(id, base, offset, 0,  1, 1, step)
#else /* __ASSEMBLER__ */
#define HW_REGISTER_FUNCS(id, base, offset, regset, rd, wr)
#define HW_REGISTER_FUNCS_INDEXED(id, base, offset, regset, rd, wr, step)
#define HW_REGISTER_WO(id, base, offset)
#define HW_REGISTER_RO(id, base, offset)
#define HW_REGISTER(id, base, offset)
#define HW_REGISTER_0(id, base, offset)
#define HW_REGISTER_INDEXED(id, base, offset, step)
#define HW_REGISTER_RO_INDEXED(id, base, offset, step)
#define HW_REGISTER_0_INDEXED(id, base, offset, step)
#endif /* __ASSEMBLER__ */

#endif /* __ASM_PLAT_STMP3XXX_REGS_H */
