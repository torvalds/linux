/*****************************************************************************
* Copyright 2003 - 2008 Broadcom Corporation.  All rights reserved.
*
* Unless you and Broadcom execute a separate written software license
* agreement governing use of this software, this software is licensed to you
* under the terms of the GNU General Public License version 2, available at
* http://www.broadcom.com/licenses/GPLv2.php (the "GPL").
*
* Notwithstanding the above, under no circumstances may you combine this
* software in any way with any other Broadcom software provided under a
* license other than the GPL, without Broadcom's express prior written
* consent.
*****************************************************************************/

/****************************************************************************/
/**
*  @file    intcHw_reg.h
*
*  @brief   platform specific interrupt controller bit assignments
*
*  @note
*     None
*/
/****************************************************************************/

#ifndef _INTCHW_REG_H
#define _INTCHW_REG_H

/* ---- Include Files ---------------------------------------------------- */
#include <linux/types.h>
#include <mach/csp/reg.h>
#include <mach/csp/mm_io.h>

/* ---- Public Constants and Types --------------------------------------- */

#define INTCHW_NUM_IRQ_PER_INTC   32	/* Maximum number of interrupt controllers */
#define INTCHW_NUM_INTC           3

/* Defines for interrupt controllers. This simplifies and cleans up the function calls. */
#define INTCHW_INTC0    (MM_IO_BASE_INTC0)
#define INTCHW_INTC1    (MM_IO_BASE_INTC1)
#define INTCHW_SINTC    (MM_IO_BASE_SINTC)

/* INTC0 - interrupt controller 0 */
#define INTCHW_INTC0_PIF_BITNUM           31	/* Peripheral interface interrupt */
#define INTCHW_INTC0_CLCD_BITNUM          30	/* LCD Controller interrupt */
#define INTCHW_INTC0_GE_BITNUM            29	/* Graphic engine interrupt */
#define INTCHW_INTC0_APM_BITNUM           28	/* Audio process module interrupt */
#define INTCHW_INTC0_ESW_BITNUM           27	/* Ethernet switch interrupt */
#define INTCHW_INTC0_SPIH_BITNUM          26	/* SPI host interrupt */
#define INTCHW_INTC0_TIMER3_BITNUM        25	/* Timer3 interrupt */
#define INTCHW_INTC0_TIMER2_BITNUM        24	/* Timer2 interrupt */
#define INTCHW_INTC0_TIMER1_BITNUM        23	/* Timer1 interrupt */
#define INTCHW_INTC0_TIMER0_BITNUM        22	/* Timer0 interrupt */
#define INTCHW_INTC0_SDIOH1_BITNUM        21	/* SDIO1 host interrupt */
#define INTCHW_INTC0_SDIOH0_BITNUM        20	/* SDIO0 host interrupt */
#define INTCHW_INTC0_USBD_BITNUM          19	/* USB device interrupt */
#define INTCHW_INTC0_USBH1_BITNUM         18	/* USB1 host interrupt */
#define INTCHW_INTC0_USBHD2_BITNUM        17	/* USB host2/device2 interrupt */
#define INTCHW_INTC0_VPM_BITNUM           16	/* Voice process module interrupt */
#define INTCHW_INTC0_DMA1C7_BITNUM        15	/* DMA1 channel 7 interrupt */
#define INTCHW_INTC0_DMA1C6_BITNUM        14	/* DMA1 channel 6 interrupt */
#define INTCHW_INTC0_DMA1C5_BITNUM        13	/* DMA1 channel 5 interrupt */
#define INTCHW_INTC0_DMA1C4_BITNUM        12	/* DMA1 channel 4 interrupt */
#define INTCHW_INTC0_DMA1C3_BITNUM        11	/* DMA1 channel 3 interrupt */
#define INTCHW_INTC0_DMA1C2_BITNUM        10	/* DMA1 channel 2 interrupt */
#define INTCHW_INTC0_DMA1C1_BITNUM         9	/* DMA1 channel 1 interrupt */
#define INTCHW_INTC0_DMA1C0_BITNUM         8	/* DMA1 channel 0 interrupt */
#define INTCHW_INTC0_DMA0C7_BITNUM         7	/* DMA0 channel 7 interrupt */
#define INTCHW_INTC0_DMA0C6_BITNUM         6	/* DMA0 channel 6 interrupt */
#define INTCHW_INTC0_DMA0C5_BITNUM         5	/* DMA0 channel 5 interrupt */
#define INTCHW_INTC0_DMA0C4_BITNUM         4	/* DMA0 channel 4 interrupt */
#define INTCHW_INTC0_DMA0C3_BITNUM         3	/* DMA0 channel 3 interrupt */
#define INTCHW_INTC0_DMA0C2_BITNUM         2	/* DMA0 channel 2 interrupt */
#define INTCHW_INTC0_DMA0C1_BITNUM         1	/* DMA0 channel 1 interrupt */
#define INTCHW_INTC0_DMA0C0_BITNUM         0	/* DMA0 channel 0 interrupt */

#define INTCHW_INTC0_PIF                  (1<<INTCHW_INTC0_PIF_BITNUM)
#define INTCHW_INTC0_CLCD                 (1<<INTCHW_INTC0_CLCD_BITNUM)
#define INTCHW_INTC0_GE                   (1<<INTCHW_INTC0_GE_BITNUM)
#define INTCHW_INTC0_APM                  (1<<INTCHW_INTC0_APM_BITNUM)
#define INTCHW_INTC0_ESW                  (1<<INTCHW_INTC0_ESW_BITNUM)
#define INTCHW_INTC0_SPIH                 (1<<INTCHW_INTC0_SPIH_BITNUM)
#define INTCHW_INTC0_TIMER3               (1<<INTCHW_INTC0_TIMER3_BITNUM)
#define INTCHW_INTC0_TIMER2               (1<<INTCHW_INTC0_TIMER2_BITNUM)
#define INTCHW_INTC0_TIMER1               (1<<INTCHW_INTC0_TIMER1_BITNUM)
#define INTCHW_INTC0_TIMER0               (1<<INTCHW_INTC0_TIMER0_BITNUM)
#define INTCHW_INTC0_SDIOH1               (1<<INTCHW_INTC0_SDIOH1_BITNUM)
#define INTCHW_INTC0_SDIOH0               (1<<INTCHW_INTC0_SDIOH0_BITNUM)
#define INTCHW_INTC0_USBD                 (1<<INTCHW_INTC0_USBD_BITNUM)
#define INTCHW_INTC0_USBH1                (1<<INTCHW_INTC0_USBH1_BITNUM)
#define INTCHW_INTC0_USBHD2               (1<<INTCHW_INTC0_USBHD2_BITNUM)
#define INTCHW_INTC0_VPM                  (1<<INTCHW_INTC0_VPM_BITNUM)
#define INTCHW_INTC0_DMA1C7               (1<<INTCHW_INTC0_DMA1C7_BITNUM)
#define INTCHW_INTC0_DMA1C6               (1<<INTCHW_INTC0_DMA1C6_BITNUM)
#define INTCHW_INTC0_DMA1C5               (1<<INTCHW_INTC0_DMA1C5_BITNUM)
#define INTCHW_INTC0_DMA1C4               (1<<INTCHW_INTC0_DMA1C4_BITNUM)
#define INTCHW_INTC0_DMA1C3               (1<<INTCHW_INTC0_DMA1C3_BITNUM)
#define INTCHW_INTC0_DMA1C2               (1<<INTCHW_INTC0_DMA1C2_BITNUM)
#define INTCHW_INTC0_DMA1C1               (1<<INTCHW_INTC0_DMA1C1_BITNUM)
#define INTCHW_INTC0_DMA1C0               (1<<INTCHW_INTC0_DMA1C0_BITNUM)
#define INTCHW_INTC0_DMA0C7               (1<<INTCHW_INTC0_DMA0C7_BITNUM)
#define INTCHW_INTC0_DMA0C6               (1<<INTCHW_INTC0_DMA0C6_BITNUM)
#define INTCHW_INTC0_DMA0C5               (1<<INTCHW_INTC0_DMA0C5_BITNUM)
#define INTCHW_INTC0_DMA0C4               (1<<INTCHW_INTC0_DMA0C4_BITNUM)
#define INTCHW_INTC0_DMA0C3               (1<<INTCHW_INTC0_DMA0C3_BITNUM)
#define INTCHW_INTC0_DMA0C2               (1<<INTCHW_INTC0_DMA0C2_BITNUM)
#define INTCHW_INTC0_DMA0C1               (1<<INTCHW_INTC0_DMA0C1_BITNUM)
#define INTCHW_INTC0_DMA0C0               (1<<INTCHW_INTC0_DMA0C0_BITNUM)

/* INTC1 - interrupt controller 1 */
#define INTCHW_INTC1_DDRVPMP_BITNUM       27	/* DDR and VPM PLL clock phase relationship interrupt (Not for A0) */
#define INTCHW_INTC1_DDRVPMT_BITNUM       26	/* DDR and VPM HW phase align timeout interrupt (Not for A0) */
#define INTCHW_INTC1_DDRP_BITNUM          26	/* DDR and PLL clock phase relationship interrupt (For A0 only)) */
#define INTCHW_INTC1_RTC2_BITNUM          25	/* Real time clock tamper interrupt */
#define INTCHW_INTC1_VDEC_BITNUM          24	/* Hantro Video Decoder interrupt */
/* Bits 13-23 are non-secure versions of the corresponding secure bits in SINTC bits 0-10. */
#define INTCHW_INTC1_SPUM_BITNUM          23	/* Secure process module interrupt */
#define INTCHW_INTC1_RTC1_BITNUM          22	/* Real time clock one-shot interrupt */
#define INTCHW_INTC1_RTC0_BITNUM          21	/* Real time clock periodic interrupt */
#define INTCHW_INTC1_RNG_BITNUM           20	/* Random number generator interrupt */
#define INTCHW_INTC1_FMPU_BITNUM          19	/* Flash memory parition unit interrupt */
#define INTCHW_INTC1_VMPU_BITNUM          18	/* VRAM memory partition interrupt */
#define INTCHW_INTC1_DMPU_BITNUM          17	/* DDR2 memory partition interrupt */
#define INTCHW_INTC1_KEYC_BITNUM          16	/* Key pad controller interrupt */
#define INTCHW_INTC1_TSC_BITNUM           15	/* Touch screen controller interrupt */
#define INTCHW_INTC1_UART0_BITNUM         14	/* UART 0 */
#define INTCHW_INTC1_WDOG_BITNUM          13	/* Watchdog timer interrupt */

#define INTCHW_INTC1_UART1_BITNUM         12	/* UART 1 */
#define INTCHW_INTC1_PMUIRQ_BITNUM        11	/* ARM performance monitor interrupt */
#define INTCHW_INTC1_COMMRX_BITNUM        10	/* ARM DDC receive interrupt */
#define INTCHW_INTC1_COMMTX_BITNUM         9	/* ARM DDC transmit interrupt */
#define INTCHW_INTC1_FLASHC_BITNUM         8	/* Flash controller interrupt */
#define INTCHW_INTC1_GPHY_BITNUM           7	/* Gigabit Phy interrupt */
#define INTCHW_INTC1_SPIS_BITNUM           6	/* SPI slave interrupt */
#define INTCHW_INTC1_I2CS_BITNUM           5	/* I2C slave interrupt */
#define INTCHW_INTC1_I2CH_BITNUM           4	/* I2C host interrupt */
#define INTCHW_INTC1_I2S1_BITNUM           3	/* I2S1 interrupt */
#define INTCHW_INTC1_I2S0_BITNUM           2	/* I2S0 interrupt */
#define INTCHW_INTC1_GPIO1_BITNUM          1	/* GPIO bit 64//32 combined interrupt */
#define INTCHW_INTC1_GPIO0_BITNUM          0	/* GPIO bit 31//0 combined interrupt */

#define INTCHW_INTC1_DDRVPMT              (1<<INTCHW_INTC1_DDRVPMT_BITNUM)
#define INTCHW_INTC1_DDRVPMP              (1<<INTCHW_INTC1_DDRVPMP_BITNUM)
#define INTCHW_INTC1_DDRP                 (1<<INTCHW_INTC1_DDRP_BITNUM)
#define INTCHW_INTC1_VDEC                 (1<<INTCHW_INTC1_VDEC_BITNUM)
#define INTCHW_INTC1_SPUM                 (1<<INTCHW_INTC1_SPUM_BITNUM)
#define INTCHW_INTC1_RTC2                 (1<<INTCHW_INTC1_RTC2_BITNUM)
#define INTCHW_INTC1_RTC1                 (1<<INTCHW_INTC1_RTC1_BITNUM)
#define INTCHW_INTC1_RTC0                 (1<<INTCHW_INTC1_RTC0_BITNUM)
#define INTCHW_INTC1_RNG                  (1<<INTCHW_INTC1_RNG_BITNUM)
#define INTCHW_INTC1_FMPU                 (1<<INTCHW_INTC1_FMPU_BITNUM)
#define INTCHW_INTC1_IMPU                 (1<<INTCHW_INTC1_IMPU_BITNUM)
#define INTCHW_INTC1_DMPU                 (1<<INTCHW_INTC1_DMPU_BITNUM)
#define INTCHW_INTC1_KEYC                 (1<<INTCHW_INTC1_KEYC_BITNUM)
#define INTCHW_INTC1_TSC                  (1<<INTCHW_INTC1_TSC_BITNUM)
#define INTCHW_INTC1_UART0                (1<<INTCHW_INTC1_UART0_BITNUM)
#define INTCHW_INTC1_WDOG                 (1<<INTCHW_INTC1_WDOG_BITNUM)
#define INTCHW_INTC1_UART1                (1<<INTCHW_INTC1_UART1_BITNUM)
#define INTCHW_INTC1_PMUIRQ               (1<<INTCHW_INTC1_PMUIRQ_BITNUM)
#define INTCHW_INTC1_COMMRX               (1<<INTCHW_INTC1_COMMRX_BITNUM)
#define INTCHW_INTC1_COMMTX               (1<<INTCHW_INTC1_COMMTX_BITNUM)
#define INTCHW_INTC1_FLASHC               (1<<INTCHW_INTC1_FLASHC_BITNUM)
#define INTCHW_INTC1_GPHY                 (1<<INTCHW_INTC1_GPHY_BITNUM)
#define INTCHW_INTC1_SPIS                 (1<<INTCHW_INTC1_SPIS_BITNUM)
#define INTCHW_INTC1_I2CS                 (1<<INTCHW_INTC1_I2CS_BITNUM)
#define INTCHW_INTC1_I2CH                 (1<<INTCHW_INTC1_I2CH_BITNUM)
#define INTCHW_INTC1_I2S1                 (1<<INTCHW_INTC1_I2S1_BITNUM)
#define INTCHW_INTC1_I2S0                 (1<<INTCHW_INTC1_I2S0_BITNUM)
#define INTCHW_INTC1_GPIO1                (1<<INTCHW_INTC1_GPIO1_BITNUM)
#define INTCHW_INTC1_GPIO0                (1<<INTCHW_INTC1_GPIO0_BITNUM)

/* SINTC secure int controller */
#define INTCHW_SINTC_RTC2_BITNUM          15	/* Real time clock tamper interrupt */
#define INTCHW_SINTC_TIMER3_BITNUM        14	/* Secure timer3 interrupt */
#define INTCHW_SINTC_TIMER2_BITNUM        13	/* Secure timer2 interrupt */
#define INTCHW_SINTC_TIMER1_BITNUM        12	/* Secure timer1 interrupt */
#define INTCHW_SINTC_TIMER0_BITNUM        11	/* Secure timer0 interrupt */
#define INTCHW_SINTC_SPUM_BITNUM          10	/* Secure process module interrupt */
#define INTCHW_SINTC_RTC1_BITNUM           9	/* Real time clock one-shot interrupt */
#define INTCHW_SINTC_RTC0_BITNUM           8	/* Real time clock periodic interrupt */
#define INTCHW_SINTC_RNG_BITNUM            7	/* Random number generator interrupt */
#define INTCHW_SINTC_FMPU_BITNUM           6	/* Flash memory parition unit interrupt */
#define INTCHW_SINTC_VMPU_BITNUM           5	/* VRAM memory partition interrupt */
#define INTCHW_SINTC_DMPU_BITNUM           4	/* DDR2 memory partition interrupt */
#define INTCHW_SINTC_KEYC_BITNUM           3	/* Key pad controller interrupt */
#define INTCHW_SINTC_TSC_BITNUM            2	/* Touch screen controller interrupt */
#define INTCHW_SINTC_UART0_BITNUM          1	/* UART0 interrupt */
#define INTCHW_SINTC_WDOG_BITNUM           0	/* Watchdog timer interrupt */

#define INTCHW_SINTC_TIMER3               (1<<INTCHW_SINTC_TIMER3_BITNUM)
#define INTCHW_SINTC_TIMER2               (1<<INTCHW_SINTC_TIMER2_BITNUM)
#define INTCHW_SINTC_TIMER1               (1<<INTCHW_SINTC_TIMER1_BITNUM)
#define INTCHW_SINTC_TIMER0               (1<<INTCHW_SINTC_TIMER0_BITNUM)
#define INTCHW_SINTC_SPUM                 (1<<INTCHW_SINTC_SPUM_BITNUM)
#define INTCHW_SINTC_RTC2                 (1<<INTCHW_SINTC_RTC2_BITNUM)
#define INTCHW_SINTC_RTC1                 (1<<INTCHW_SINTC_RTC1_BITNUM)
#define INTCHW_SINTC_RTC0                 (1<<INTCHW_SINTC_RTC0_BITNUM)
#define INTCHW_SINTC_RNG                  (1<<INTCHW_SINTC_RNG_BITNUM)
#define INTCHW_SINTC_FMPU                 (1<<INTCHW_SINTC_FMPU_BITNUM)
#define INTCHW_SINTC_IMPU                 (1<<INTCHW_SINTC_IMPU_BITNUM)
#define INTCHW_SINTC_DMPU                 (1<<INTCHW_SINTC_DMPU_BITNUM)
#define INTCHW_SINTC_KEYC                 (1<<INTCHW_SINTC_KEYC_BITNUM)
#define INTCHW_SINTC_TSC                  (1<<INTCHW_SINTC_TSC_BITNUM)
#define INTCHW_SINTC_UART0                (1<<INTCHW_SINTC_UART0_BITNUM)
#define INTCHW_SINTC_WDOG                 (1<<INTCHW_SINTC_WDOG_BITNUM)

/* PL192 Vectored Interrupt Controller (VIC) layout */
#define INTCHW_IRQSTATUS      0x00	/* IRQ status register */
#define INTCHW_FIQSTATUS      0x04	/* FIQ status register */
#define INTCHW_RAWINTR        0x08	/* Raw Interrupt Status register */
#define INTCHW_INTSELECT      0x0c	/* Interrupt Select Register */
#define INTCHW_INTENABLE      0x10	/* Interrupt Enable Register */
#define INTCHW_INTENCLEAR     0x14	/* Interrupt Enable Clear Register */
#define INTCHW_SOFTINT        0x18	/* Soft Interrupt Register */
#define INTCHW_SOFTINTCLEAR   0x1c	/* Soft Interrupt Clear Register */
#define INTCHW_PROTECTION     0x20	/* Protection Enable Register */
#define INTCHW_SWPRIOMASK     0x24	/* Software Priority Mask Register */
#define INTCHW_PRIODAISY      0x28	/* Priority Daisy Chain Register */
#define INTCHW_VECTADDR0      0x100	/* Vector Address Registers */
#define INTCHW_VECTPRIO0      0x200	/* Vector Priority Registers 0-31 */
#define INTCHW_ADDRESS        0xf00	/* Vector Address Register 0-31 */
#define INTCHW_PID            0xfe0	/* Peripheral ID Register 0-3 */
#define INTCHW_PCELLID        0xff0	/* PrimeCell ID Register 0-3 */

/* Example Usage: intcHw_irq_enable(INTCHW_INTC0, INTCHW_INTC0_TIMER0); */
/*                intcHw_irq_clear(INTCHW_INTC0, INTCHW_INTC0_TIMER0); */
/*                uint32_t bits = intcHw_irq_status(INTCHW_INTC0); */
/*                uint32_t bits = intcHw_irq_raw_status(INTCHW_INTC0); */

/* ---- Public Variable Externs ------------------------------------------ */
/* ---- Public Function Prototypes --------------------------------------- */
/* Clear one or more IRQ interrupts. */
static inline void intcHw_irq_disable(void __iomem *basep, uint32_t mask)
{
	writel(mask, basep + INTCHW_INTENCLEAR);
}

/* Enables one or more IRQ interrupts. */
static inline void intcHw_irq_enable(void __iomem *basep, uint32_t mask)
{
	writel(mask, basep + INTCHW_INTENABLE);
}

#endif /* _INTCHW_REG_H */
