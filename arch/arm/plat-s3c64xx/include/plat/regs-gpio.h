/* linux/arch/arm/plat-s3c64xx/include/mach/regs-gpio.h
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *      Ben Dooks <ben@simtec.co.uk>
 *      http://armlinux.simtec.co.uk/
 *
 * S3C64XX - GPIO register definitions
 */

#ifndef __ASM_PLAT_S3C64XX_REGS_GPIO_H
#define __ASM_PLAT_S3C64XX_REGS_GPIO_H __FILE__

/* Base addresses for each of the banks */

#define S3C64XX_GPA_BASE	(S3C64XX_VA_GPIO + 0x0000)
#define S3C64XX_GPB_BASE	(S3C64XX_VA_GPIO + 0x0020)
#define S3C64XX_GPC_BASE	(S3C64XX_VA_GPIO + 0x0040)
#define S3C64XX_GPD_BASE	(S3C64XX_VA_GPIO + 0x0060)
#define S3C64XX_GPE_BASE	(S3C64XX_VA_GPIO + 0x0080)
#define S3C64XX_GPF_BASE	(S3C64XX_VA_GPIO + 0x00A0)
#define S3C64XX_GPG_BASE	(S3C64XX_VA_GPIO + 0x00C0)
#define S3C64XX_GPH_BASE	(S3C64XX_VA_GPIO + 0x00E0)
#define S3C64XX_GPI_BASE	(S3C64XX_VA_GPIO + 0x0100)
#define S3C64XX_GPJ_BASE	(S3C64XX_VA_GPIO + 0x0120)
#define S3C64XX_GPK_BASE	(S3C64XX_VA_GPIO + 0x0800)
#define S3C64XX_GPL_BASE	(S3C64XX_VA_GPIO + 0x0810)
#define S3C64XX_GPM_BASE	(S3C64XX_VA_GPIO + 0x0820)
#define S3C64XX_GPN_BASE	(S3C64XX_VA_GPIO + 0x0830)
#define S3C64XX_GPO_BASE	(S3C64XX_VA_GPIO + 0x0140)
#define S3C64XX_GPP_BASE	(S3C64XX_VA_GPIO + 0x0160)
#define S3C64XX_GPQ_BASE	(S3C64XX_VA_GPIO + 0x0180)

#endif /* __ASM_PLAT_S3C64XX_REGS_GPIO_H */

