/*
 *  Copyright (C) 2007 Broadcom
 *  Copyright (C) 1999 ARM Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#if !defined(ARCH_BCMRING_IRQS_H)
#define ARCH_BCMRING_IRQS_H

/* INTC0 - interrupt controller 0 */
#define IRQ_INTC0_START     0
#define IRQ_DMA0C0          0	/* DMA0 channel 0 interrupt */
#define IRQ_DMA0C1          1	/* DMA0 channel 1 interrupt */
#define IRQ_DMA0C2          2	/* DMA0 channel 2 interrupt */
#define IRQ_DMA0C3          3	/* DMA0 channel 3 interrupt */
#define IRQ_DMA0C4          4	/* DMA0 channel 4 interrupt */
#define IRQ_DMA0C5          5	/* DMA0 channel 5 interrupt */
#define IRQ_DMA0C6          6	/* DMA0 channel 6 interrupt */
#define IRQ_DMA0C7          7	/* DMA0 channel 7 interrupt */
#define IRQ_DMA1C0          8	/* DMA1 channel 0 interrupt */
#define IRQ_DMA1C1          9	/* DMA1 channel 1 interrupt */
#define IRQ_DMA1C2         10	/* DMA1 channel 2 interrupt */
#define IRQ_DMA1C3         11	/* DMA1 channel 3 interrupt */
#define IRQ_DMA1C4         12	/* DMA1 channel 4 interrupt */
#define IRQ_DMA1C5         13	/* DMA1 channel 5 interrupt */
#define IRQ_DMA1C6         14	/* DMA1 channel 6 interrupt */
#define IRQ_DMA1C7         15	/* DMA1 channel 7 interrupt */
#define IRQ_VPM            16	/* Voice process module interrupt */
#define IRQ_USBHD2         17	/* USB host2/device2 interrupt */
#define IRQ_USBH1          18	/* USB1 host interrupt */
#define IRQ_USBD           19	/* USB device interrupt */
#define IRQ_SDIOH0         20	/* SDIO0 host interrupt */
#define IRQ_SDIOH1         21	/* SDIO1 host interrupt */
#define IRQ_TIMER0         22	/* Timer0 interrupt */
#define IRQ_TIMER1         23	/* Timer1 interrupt */
#define IRQ_TIMER2         24	/* Timer2 interrupt */
#define IRQ_TIMER3         25	/* Timer3 interrupt */
#define IRQ_SPIH           26	/* SPI host interrupt */
#define IRQ_ESW            27	/* Ethernet switch interrupt */
#define IRQ_APM            28	/* Audio process module interrupt */
#define IRQ_GE             29	/* Graphic engine interrupt */
#define IRQ_CLCD           30	/* LCD Controller interrupt */
#define IRQ_PIF            31	/* Peripheral interface interrupt */
#define IRQ_INTC0_END      31

/* INTC1 - interrupt controller 1 */
#define IRQ_INTC1_START    32
#define IRQ_GPIO0          32	/*  0 GPIO bit 31//0 combined interrupt */
#define IRQ_GPIO1          33	/*  1 GPIO bit 64//32 combined interrupt */
#define IRQ_I2S0           34	/*  2 I2S0 interrupt */
#define IRQ_I2S1           35	/*  3 I2S1 interrupt */
#define IRQ_I2CH           36	/*  4 I2C host interrupt */
#define IRQ_I2CS           37	/*  5 I2C slave interrupt */
#define IRQ_SPIS           38	/*  6 SPI slave interrupt */
#define IRQ_GPHY           39	/*  7 Gigabit Phy interrupt */
#define IRQ_FLASHC         40	/*  8 Flash controller interrupt */
#define IRQ_COMMTX         41	/*  9 ARM DDC transmit interrupt */
#define IRQ_COMMRX         42	/* 10 ARM DDC receive interrupt */
#define IRQ_PMUIRQ         43	/* 11 ARM performance monitor interrupt */
#define IRQ_UARTB          44	/* 12 UARTB */
#define IRQ_WATCHDOG       45	/* 13 Watchdog timer interrupt */
#define IRQ_UARTA          46	/* 14 UARTA */
#define IRQ_TSC            47	/* 15 Touch screen controller interrupt */
#define IRQ_KEYC           48	/* 16 Key pad controller interrupt */
#define IRQ_DMPU           49	/* 17 DDR2 memory partition interrupt */
#define IRQ_VMPU           50	/* 18 VRAM memory partition interrupt */
#define IRQ_FMPU           51	/* 19 Flash memory parition unit interrupt */
#define IRQ_RNG            52	/* 20 Random number generator interrupt */
#define IRQ_RTC0           53	/* 21 Real time clock periodic interrupt */
#define IRQ_RTC1           54	/* 22 Real time clock one-shot interrupt */
#define IRQ_SPUM           55	/* 23 Secure process module interrupt */
#define IRQ_VDEC           56	/* 24 Hantro video decoder interrupt */
#define IRQ_RTC2           57	/* 25 Real time clock tamper interrupt */
#define IRQ_DDRP           58	/* 26 DDR Panic interrupt */
#define IRQ_INTC1_END      58

/* SINTC secure int controller */
#define IRQ_SINTC_START    59
#define IRQ_SEC_WATCHDOG   59	/*  0 Watchdog timer interrupt */
#define IRQ_SEC_UARTA      60	/*  1 UARTA interrupt */
#define IRQ_SEC_TSC        61	/*  2 Touch screen controller interrupt */
#define IRQ_SEC_KEYC       62	/*  3 Key pad controller interrupt */
#define IRQ_SEC_DMPU       63	/*  4 DDR2 memory partition interrupt */
#define IRQ_SEC_VMPU       64	/*  5 VRAM memory partition interrupt */
#define IRQ_SEC_FMPU       65	/*  6 Flash memory parition unit interrupt */
#define IRQ_SEC_RNG        66	/*  7 Random number generator interrupt */
#define IRQ_SEC_RTC0       67	/*  8 Real time clock periodic interrupt */
#define IRQ_SEC_RTC1       68	/*  9 Real time clock one-shot interrupt */
#define IRQ_SEC_SPUM       69	/* 10 Secure process module interrupt */
#define IRQ_SEC_TIMER0     70	/* 11 Secure timer0 interrupt */
#define IRQ_SEC_TIMER1     71	/* 12 Secure timer1 interrupt */
#define IRQ_SEC_TIMER2     72	/* 13 Secure timer2 interrupt */
#define IRQ_SEC_TIMER3     73	/* 14 Secure timer3 interrupt */
#define IRQ_SEC_RTC2       74	/* 15 Real time clock tamper interrupt */

#define IRQ_SINTC_END      74

/* Note: there are 3 INTC registers of 32 bits each. So internal IRQs could go from 0-95 */
/*       Since IRQs are typically viewed in decimal, we start the gpio based IRQs off at 100 */
/*       to make the mapping easy for humans to decipher. */

#define IRQ_GPIO_0                  100

#define NUM_INTERNAL_IRQS          (IRQ_SINTC_END+1)

/* I couldn't get the gpioHw_reg.h file to be included cleanly, so I hardcoded it */
/* define NUM_GPIO_IRQS               GPIOHW_TOTAL_NUM_PINS */
#define NUM_GPIO_IRQS               62

#define NR_IRQS                     (IRQ_GPIO_0 + NUM_GPIO_IRQS)

#define IRQ_UNKNOWN                 -1

/* Tune these bits to preclude noisy or unsupported interrupt sources as required. */
#define IRQ_INTC0_VALID_MASK        0xffffffff
#define IRQ_INTC1_VALID_MASK        0x07ffffff
#define IRQ_SINTC_VALID_MASK        0x0000ffff

#endif /* ARCH_BCMRING_IRQS_H */
