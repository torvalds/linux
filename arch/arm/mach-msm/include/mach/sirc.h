/* Copyright (c) 2008-2009, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __ASM_ARCH_MSM_SIRC_H
#define __ASM_ARCH_MSM_SIRC_H

struct sirc_regs_t {
	void    *int_enable;
	void    *int_enable_clear;
	void    *int_enable_set;
	void    *int_type;
	void    *int_polarity;
	void    *int_clear;
};

struct sirc_cascade_regs {
	void    *int_status;
	unsigned int    cascade_irq;
};

void msm_init_sirc(void);
void msm_sirc_enter_sleep(void);
void msm_sirc_exit_sleep(void);

#if defined(CONFIG_ARCH_MSM_SCORPION)

#include <mach/msm_iomap.h>

/*
 * Secondary interrupt controller interrupts
 */

#define FIRST_SIRC_IRQ (NR_MSM_IRQS + NR_GPIO_IRQS)

#define INT_UART1                     (FIRST_SIRC_IRQ + 0)
#define INT_UART2                     (FIRST_SIRC_IRQ + 1)
#define INT_UART3                     (FIRST_SIRC_IRQ + 2)
#define INT_UART1_RX                  (FIRST_SIRC_IRQ + 3)
#define INT_UART2_RX                  (FIRST_SIRC_IRQ + 4)
#define INT_UART3_RX                  (FIRST_SIRC_IRQ + 5)
#define INT_SPI_INPUT                 (FIRST_SIRC_IRQ + 6)
#define INT_SPI_OUTPUT                (FIRST_SIRC_IRQ + 7)
#define INT_SPI_ERROR                 (FIRST_SIRC_IRQ + 8)
#define INT_GPIO_GROUP1               (FIRST_SIRC_IRQ + 9)
#define INT_GPIO_GROUP2               (FIRST_SIRC_IRQ + 10)
#define INT_GPIO_GROUP1_SECURE        (FIRST_SIRC_IRQ + 11)
#define INT_GPIO_GROUP2_SECURE        (FIRST_SIRC_IRQ + 12)
#define INT_AVS_SVIC                  (FIRST_SIRC_IRQ + 13)
#define INT_AVS_REQ_UP                (FIRST_SIRC_IRQ + 14)
#define INT_AVS_REQ_DOWN              (FIRST_SIRC_IRQ + 15)
#define INT_PBUS_ERR                  (FIRST_SIRC_IRQ + 16)
#define INT_AXI_ERR                   (FIRST_SIRC_IRQ + 17)
#define INT_SMI_ERR                   (FIRST_SIRC_IRQ + 18)
#define INT_EBI1_ERR                  (FIRST_SIRC_IRQ + 19)
#define INT_IMEM_ERR                  (FIRST_SIRC_IRQ + 20)
#define INT_TEMP_SENSOR               (FIRST_SIRC_IRQ + 21)
#define INT_TV_ENC                    (FIRST_SIRC_IRQ + 22)
#define INT_GRP2D                     (FIRST_SIRC_IRQ + 23)
#define INT_GSBI_QUP                  (FIRST_SIRC_IRQ + 24)
#define INT_SC_ACG                    (FIRST_SIRC_IRQ + 25)
#define INT_WDT0                      (FIRST_SIRC_IRQ + 26)
#define INT_WDT1                      (FIRST_SIRC_IRQ + 27)

#if defined(CONFIG_MSM_SOC_REV_A)
#define NR_SIRC_IRQS                  28
#define SIRC_MASK                     0x0FFFFFFF
#else
#define NR_SIRC_IRQS                  23
#define SIRC_MASK                     0x007FFFFF
#endif

#define LAST_SIRC_IRQ                 (FIRST_SIRC_IRQ + NR_SIRC_IRQS - 1)

#define SPSS_SIRC_INT_SELECT          (MSM_SIRC_BASE + 0x00)
#define SPSS_SIRC_INT_ENABLE          (MSM_SIRC_BASE + 0x04)
#define SPSS_SIRC_INT_ENABLE_CLEAR    (MSM_SIRC_BASE + 0x08)
#define SPSS_SIRC_INT_ENABLE_SET      (MSM_SIRC_BASE + 0x0C)
#define SPSS_SIRC_INT_TYPE            (MSM_SIRC_BASE + 0x10)
#define SPSS_SIRC_INT_POLARITY        (MSM_SIRC_BASE + 0x14)
#define SPSS_SIRC_SECURITY            (MSM_SIRC_BASE + 0x18)
#define SPSS_SIRC_IRQ_STATUS          (MSM_SIRC_BASE + 0x1C)
#define SPSS_SIRC_IRQ1_STATUS         (MSM_SIRC_BASE + 0x20)
#define SPSS_SIRC_RAW_STATUS          (MSM_SIRC_BASE + 0x24)
#define SPSS_SIRC_INT_CLEAR           (MSM_SIRC_BASE + 0x28)
#define SPSS_SIRC_SOFT_INT            (MSM_SIRC_BASE + 0x2C)

#endif

#endif
