/*
 * arch/arm/mach-iop32x/include/mach/n2100.h
 *
 * Thecus N2100 board registers
 */

#ifndef __N2100_H
#define __N2100_H

#define N2100_UART		0xfe800000	/* UART */

#define N2100_COPY_BUTTON	IOP3XX_GPIO_LINE(0)
#define N2100_PCA9532_RESET	IOP3XX_GPIO_LINE(2)
#define N2100_RESET_BUTTON	IOP3XX_GPIO_LINE(3)
#define N2100_HARDWARE_RESET	IOP3XX_GPIO_LINE(4)
#define N2100_POWER_BUTTON	IOP3XX_GPIO_LINE(5)


#endif
