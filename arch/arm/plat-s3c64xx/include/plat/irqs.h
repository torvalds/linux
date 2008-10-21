/* linux/arch/arm/plat-s3c64xx/include/mach/irqs.h
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *      Ben Dooks <ben@simtec.co.uk>
 *      http://armlinux.simtec.co.uk/
 *
 * S3C64XX - Common IRQ support
 */

#ifndef __ASM_PLAT_S3C64XX_IRQS_H
#define __ASM_PLAT_S3C64XX_IRQS_H __FILE__

/* we keep the first set of CPU IRQs out of the range of
 * the ISA space, so that the PC104 has them to itself
 * and we don't end up having to do horrible things to the
 * standard ISA drivers....
 */

#define S3C_IRQ_OFFSET	(16)

#define S3C_IRQ(x)	((x) + S3C_IRQ_OFFSET)

/* Since the IRQ_EINT(x) are a linear mapping on current s3c64xx series
 * we just defined them as an IRQ_EINT(x) macro from S3C_IRQ_EINT_BASE
 * which we place after the pair of VICs. */

#define S3C_IRQ_EINT_BASE	S3C_IRQ(64)

#define S3C_EINT(x)	((x) + S3C_IRQ_EINT_BASE)

/* Define NR_IRQs here, machine specific can always re-define.
 * Currently the IRQ_EINT27 is the last one we can have. */

#define NR_IRQS	(S3C_EINT(27) + 1)

#endif /* __ASM_PLAT_S3C64XX_IRQS_H */

