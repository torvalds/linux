/* linux/arch/arm/plat-s5pc100/include/plat/regs-gpio.h
 *
 * Copyright 2009 Samsung Electronics Co.
 *      Byungho Min <bhmin@samsung.com>
 *
 * S5PC100 - GPIO register definitions
 */

#ifndef __ASM_MACH_S5PC100_REGS_GPIO_H
#define __ASM_MACH_S5PC100_REGS_GPIO_H __FILE__

#include <mach/map.h>

#define S5PC100EINT30CON		(S5P_VA_GPIO + 0xE00)
#define S5P_EINT_CON(x)			(S5PC100EINT30CON + ((x) * 0x4))

#define S5PC100EINT30FLTCON0		(S5P_VA_GPIO + 0xE80)
#define S5P_EINT_FLTCON(x)		(S5PC100EINT30FLTCON0 + ((x) * 0x4))

#define S5PC100EINT30MASK		(S5P_VA_GPIO + 0xF00)
#define S5P_EINT_MASK(x)		(S5PC100EINT30MASK + ((x) * 0x4))

#define S5PC100EINT30PEND		(S5P_VA_GPIO + 0xF40)
#define S5P_EINT_PEND(x)		(S5PC100EINT30PEND + ((x) * 0x4))

#define EINT_REG_NR(x)			(EINT_OFFSET(x) >> 3)

#define eint_irq_to_bit(irq)		(1 << (EINT_OFFSET(irq) & 0x7))

#define EINT_MODE		S3C_GPIO_SFN(0x2)

#define EINT_GPIO_0(x)		S5PC100_GPH0(x)
#define EINT_GPIO_1(x)		S5PC100_GPH1(x)
#define EINT_GPIO_2(x)		S5PC100_GPH2(x)
#define EINT_GPIO_3(x)		S5PC100_GPH3(x)

#endif /* __ASM_MACH_S5PC100_REGS_GPIO_H */

