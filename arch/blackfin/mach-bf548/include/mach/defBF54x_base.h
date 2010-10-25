/*
 * Copyright 2007-2008 Analog Devices Inc.
 *
 * Licensed under the ADI BSD license or the GPL-2 (or later)
 */

#ifndef _DEF_BF54X_H
#define _DEF_BF54X_H


/* ************************************************************** */
/*   SYSTEM & MMR ADDRESS DEFINITIONS COMMON TO ALL ADSP-BF54x    */
/* ************************************************************** */

/* PLL Registers */

#define                          PLL_CTL  0xffc00000   /* PLL Control Register */
#define                          PLL_DIV  0xffc00004   /* PLL Divisor Register */
#define                           VR_CTL  0xffc00008   /* Voltage Regulator Control Register */
#define                         PLL_STAT  0xffc0000c   /* PLL Status Register */
#define                      PLL_LOCKCNT  0xffc00010   /* PLL Lock Count Register */

/* Debug/MP/Emulation Registers (0xFFC00014 - 0xFFC00014) */

#define                           CHIPID  0xffc00014
/* CHIPID Masks */
#define                   CHIPID_VERSION  0xF0000000
#define                    CHIPID_FAMILY  0x0FFFF000
#define               CHIPID_MANUFACTURE  0x00000FFE

/* System Reset and Interrupt Controller (0xFFC00100 - 0xFFC00104) */

#define                            SWRST  0xffc00100   /* Software Reset Register */
#define                            SYSCR  0xffc00104   /* System Configuration register */

/* SIC Registers */

#define                        SIC_RVECT  0xffc00108
#define                       SIC_IMASK0  0xffc0010c   /* System Interrupt Mask Register 0 */
#define                       SIC_IMASK1  0xffc00110   /* System Interrupt Mask Register 1 */
#define                       SIC_IMASK2  0xffc00114   /* System Interrupt Mask Register 2 */
#define                         SIC_ISR0  0xffc00118   /* System Interrupt Status Register 0 */
#define                         SIC_ISR1  0xffc0011c   /* System Interrupt Status Register 1 */
#define                         SIC_ISR2  0xffc00120   /* System Interrupt Status Register 2 */
#define                         SIC_IWR0  0xffc00124   /* System Interrupt Wakeup Register 0 */
#define                         SIC_IWR1  0xffc00128   /* System Interrupt Wakeup Register 1 */
#define                         SIC_IWR2  0xffc0012c   /* System Interrupt Wakeup Register 2 */
#define                         SIC_IAR0  0xffc00130   /* System Interrupt Assignment Register 0 */
#define                         SIC_IAR1  0xffc00134   /* System Interrupt Assignment Register 1 */
#define                         SIC_IAR2  0xffc00138   /* System Interrupt Assignment Register 2 */
#define                         SIC_IAR3  0xffc0013c   /* System Interrupt Assignment Register 3 */
#define                         SIC_IAR4  0xffc00140   /* System Interrupt Assignment Register 4 */
#define                         SIC_IAR5  0xffc00144   /* System Interrupt Assignment Register 5 */
#define                         SIC_IAR6  0xffc00148   /* System Interrupt Assignment Register 6 */
#define                         SIC_IAR7  0xffc0014c   /* System Interrupt Assignment Register 7 */
#define                         SIC_IAR8  0xffc00150   /* System Interrupt Assignment Register 8 */
#define                         SIC_IAR9  0xffc00154   /* System Interrupt Assignment Register 9 */
#define                        SIC_IAR10  0xffc00158   /* System Interrupt Assignment Register 10 */
#define                        SIC_IAR11  0xffc0015c   /* System Interrupt Assignment Register 11 */

/* Watchdog Timer Registers */

#define                         WDOG_CTL  0xffc00200   /* Watchdog Control Register */
#define                         WDOG_CNT  0xffc00204   /* Watchdog Count Register */
#define                        WDOG_STAT  0xffc00208   /* Watchdog Status Register */

/* RTC Registers */

#define                         RTC_STAT  0xffc00300   /* RTC Status Register */
#define                         RTC_ICTL  0xffc00304   /* RTC Interrupt Control Register */
#define                        RTC_ISTAT  0xffc00308   /* RTC Interrupt Status Register */
#define                        RTC_SWCNT  0xffc0030c   /* RTC Stopwatch Count Register */
#define                        RTC_ALARM  0xffc00310   /* RTC Alarm Register */
#define                         RTC_PREN  0xffc00314   /* RTC Prescaler Enable Register */

/* UART0 Registers */

#define                        UART0_DLL  0xffc00400   /* Divisor Latch Low Byte */
#define                        UART0_DLH  0xffc00404   /* Divisor Latch High Byte */
#define                       UART0_GCTL  0xffc00408   /* Global Control Register */
#define                        UART0_LCR  0xffc0040c   /* Line Control Register */
#define                        UART0_MCR  0xffc00410   /* Modem Control Register */
#define                        UART0_LSR  0xffc00414   /* Line Status Register */
#define                        UART0_MSR  0xffc00418   /* Modem Status Register */
#define                        UART0_SCR  0xffc0041c   /* Scratch Register */
#define                    UART0_IER_SET  0xffc00420   /* Interrupt Enable Register Set */
#define                  UART0_IER_CLEAR  0xffc00424   /* Interrupt Enable Register Clear */
#define                        UART0_THR  0xffc00428   /* Transmit Hold Register */
#define                        UART0_RBR  0xffc0042c   /* Receive Buffer Register */

/* SPI0 Registers */

#define                     SPI0_REGBASE  0xffc00500
#define                         SPI0_CTL  0xffc00500   /* SPI0 Control Register */
#define                         SPI0_FLG  0xffc00504   /* SPI0 Flag Register */
#define                        SPI0_STAT  0xffc00508   /* SPI0 Status Register */
#define                        SPI0_TDBR  0xffc0050c   /* SPI0 Transmit Data Buffer Register */
#define                        SPI0_RDBR  0xffc00510   /* SPI0 Receive Data Buffer Register */
#define                        SPI0_BAUD  0xffc00514   /* SPI0 Baud Rate Register */
#define                      SPI0_SHADOW  0xffc00518   /* SPI0 Receive Data Buffer Shadow Register */

/* Timer Group of 3 registers are not defined in the shared file because they are not available on the ADSP-BF542 processor */

/* Two Wire Interface Registers (TWI0) */

#define                     TWI0_REGBASE  0xffc00700
#define                      TWI0_CLKDIV  0xffc00700   /* Clock Divider Register */
#define                     TWI0_CONTROL  0xffc00704   /* TWI Control Register */
#define                   TWI0_SLAVE_CTL  0xffc00708   /* TWI Slave Mode Control Register */
#define                  TWI0_SLAVE_STAT  0xffc0070c   /* TWI Slave Mode Status Register */
#define                  TWI0_SLAVE_ADDR  0xffc00710   /* TWI Slave Mode Address Register */
#define                  TWI0_MASTER_CTL  0xffc00714   /* TWI Master Mode Control Register */
#define                 TWI0_MASTER_STAT  0xffc00718   /* TWI Master Mode Status Register */
#define                 TWI0_MASTER_ADDR  0xffc0071c   /* TWI Master Mode Address Register */
#define                    TWI0_INT_STAT  0xffc00720   /* TWI Interrupt Status Register */
#define                    TWI0_INT_MASK  0xffc00724   /* TWI Interrupt Mask Register */
#define                    TWI0_FIFO_CTL  0xffc00728   /* TWI FIFO Control Register */
#define                   TWI0_FIFO_STAT  0xffc0072c   /* TWI FIFO Status Register */
#define                   TWI0_XMT_DATA8  0xffc00780   /* TWI FIFO Transmit Data Single Byte Register */
#define                  TWI0_XMT_DATA16  0xffc00784   /* TWI FIFO Transmit Data Double Byte Register */
#define                   TWI0_RCV_DATA8  0xffc00788   /* TWI FIFO Receive Data Single Byte Register */
#define                  TWI0_RCV_DATA16  0xffc0078c   /* TWI FIFO Receive Data Double Byte Register */

/* SPORT0 is not defined in the shared file because it is not available on the ADSP-BF542 and ADSP-BF544 processors */

/* SPORT1 Registers */

#define                      SPORT1_TCR1  0xffc00900   /* SPORT1 Transmit Configuration 1 Register */
#define                      SPORT1_TCR2  0xffc00904   /* SPORT1 Transmit Configuration 2 Register */
#define                   SPORT1_TCLKDIV  0xffc00908   /* SPORT1 Transmit Serial Clock Divider Register */
#define                    SPORT1_TFSDIV  0xffc0090c   /* SPORT1 Transmit Frame Sync Divider Register */
#define                        SPORT1_TX  0xffc00910   /* SPORT1 Transmit Data Register */
#define                        SPORT1_RX  0xffc00918   /* SPORT1 Receive Data Register */
#define                      SPORT1_RCR1  0xffc00920   /* SPORT1 Receive Configuration 1 Register */
#define                      SPORT1_RCR2  0xffc00924   /* SPORT1 Receive Configuration 2 Register */
#define                   SPORT1_RCLKDIV  0xffc00928   /* SPORT1 Receive Serial Clock Divider Register */
#define                    SPORT1_RFSDIV  0xffc0092c   /* SPORT1 Receive Frame Sync Divider Register */
#define                      SPORT1_STAT  0xffc00930   /* SPORT1 Status Register */
#define                      SPORT1_CHNL  0xffc00934   /* SPORT1 Current Channel Register */
#define                     SPORT1_MCMC1  0xffc00938   /* SPORT1 Multi channel Configuration Register 1 */
#define                     SPORT1_MCMC2  0xffc0093c   /* SPORT1 Multi channel Configuration Register 2 */
#define                     SPORT1_MTCS0  0xffc00940   /* SPORT1 Multi channel Transmit Select Register 0 */
#define                     SPORT1_MTCS1  0xffc00944   /* SPORT1 Multi channel Transmit Select Register 1 */
#define                     SPORT1_MTCS2  0xffc00948   /* SPORT1 Multi channel Transmit Select Register 2 */
#define                     SPORT1_MTCS3  0xffc0094c   /* SPORT1 Multi channel Transmit Select Register 3 */
#define                     SPORT1_MRCS0  0xffc00950   /* SPORT1 Multi channel Receive Select Register 0 */
#define                     SPORT1_MRCS1  0xffc00954   /* SPORT1 Multi channel Receive Select Register 1 */
#define                     SPORT1_MRCS2  0xffc00958   /* SPORT1 Multi channel Receive Select Register 2 */
#define                     SPORT1_MRCS3  0xffc0095c   /* SPORT1 Multi channel Receive Select Register 3 */

/* Asynchronous Memory Control Registers */

#define                      EBIU_AMGCTL  0xffc00a00   /* Asynchronous Memory Global Control Register */
#define                    EBIU_AMBCTL0   0xffc00a04   /* Asynchronous Memory Bank Control Register */
#define                    EBIU_AMBCTL1   0xffc00a08   /* Asynchronous Memory Bank Control Register */
#define                      EBIU_MBSCTL  0xffc00a0c   /* Asynchronous Memory Bank Select Control Register */
#define                     EBIU_ARBSTAT  0xffc00a10   /* Asynchronous Memory Arbiter Status Register */
#define                        EBIU_MODE  0xffc00a14   /* Asynchronous Mode Control Register */
#define                        EBIU_FCTL  0xffc00a18   /* Asynchronous Memory Flash Control Register */

/* DDR Memory Control Registers */

#define                     EBIU_DDRCTL0  0xffc00a20   /* DDR Memory Control 0 Register */
#define                     EBIU_DDRCTL1  0xffc00a24   /* DDR Memory Control 1 Register */
#define                     EBIU_DDRCTL2  0xffc00a28   /* DDR Memory Control 2 Register */
#define                     EBIU_DDRCTL3  0xffc00a2c   /* DDR Memory Control 3 Register */
#define                      EBIU_DDRQUE  0xffc00a30   /* DDR Queue Configuration Register */
#define                      EBIU_ERRADD  0xffc00a34   /* DDR Error Address Register */
#define                      EBIU_ERRMST  0xffc00a38   /* DDR Error Master Register */
#define                      EBIU_RSTCTL  0xffc00a3c   /* DDR Reset Control Register */

/* DDR BankRead and Write Count Registers */

#define                     EBIU_DDRBRC0  0xffc00a60   /* DDR Bank0 Read Count Register */
#define                     EBIU_DDRBRC1  0xffc00a64   /* DDR Bank1 Read Count Register */
#define                     EBIU_DDRBRC2  0xffc00a68   /* DDR Bank2 Read Count Register */
#define                     EBIU_DDRBRC3  0xffc00a6c   /* DDR Bank3 Read Count Register */
#define                     EBIU_DDRBRC4  0xffc00a70   /* DDR Bank4 Read Count Register */
#define                     EBIU_DDRBRC5  0xffc00a74   /* DDR Bank5 Read Count Register */
#define                     EBIU_DDRBRC6  0xffc00a78   /* DDR Bank6 Read Count Register */
#define                     EBIU_DDRBRC7  0xffc00a7c   /* DDR Bank7 Read Count Register */
#define                     EBIU_DDRBWC0  0xffc00a80   /* DDR Bank0 Write Count Register */
#define                     EBIU_DDRBWC1  0xffc00a84   /* DDR Bank1 Write Count Register */
#define                     EBIU_DDRBWC2  0xffc00a88   /* DDR Bank2 Write Count Register */
#define                     EBIU_DDRBWC3  0xffc00a8c   /* DDR Bank3 Write Count Register */
#define                     EBIU_DDRBWC4  0xffc00a90   /* DDR Bank4 Write Count Register */
#define                     EBIU_DDRBWC5  0xffc00a94   /* DDR Bank5 Write Count Register */
#define                     EBIU_DDRBWC6  0xffc00a98   /* DDR Bank6 Write Count Register */
#define                     EBIU_DDRBWC7  0xffc00a9c   /* DDR Bank7 Write Count Register */
#define                     EBIU_DDRACCT  0xffc00aa0   /* DDR Activation Count Register */
#define                     EBIU_DDRTACT  0xffc00aa8   /* DDR Turn Around Count Register */
#define                     EBIU_DDRARCT  0xffc00aac   /* DDR Auto-refresh Count Register */
#define                      EBIU_DDRGC0  0xffc00ab0   /* DDR Grant Count 0 Register */
#define                      EBIU_DDRGC1  0xffc00ab4   /* DDR Grant Count 1 Register */
#define                      EBIU_DDRGC2  0xffc00ab8   /* DDR Grant Count 2 Register */
#define                      EBIU_DDRGC3  0xffc00abc   /* DDR Grant Count 3 Register */
#define                     EBIU_DDRMCEN  0xffc00ac0   /* DDR Metrics Counter Enable Register */
#define                     EBIU_DDRMCCL  0xffc00ac4   /* DDR Metrics Counter Clear Register */

/* DMAC0 Registers */

#define                     DMAC0_TC_PER  0xffc00b0c   /* DMA Controller 0 Traffic Control Periods Register */
#define                     DMAC0_TC_CNT  0xffc00b10   /* DMA Controller 0 Current Counts Register */

/* DMA Channel 0 Registers */

#define               DMA0_NEXT_DESC_PTR  0xffc00c00   /* DMA Channel 0 Next Descriptor Pointer Register */
#define                  DMA0_START_ADDR  0xffc00c04   /* DMA Channel 0 Start Address Register */
#define                      DMA0_CONFIG  0xffc00c08   /* DMA Channel 0 Configuration Register */
#define                     DMA0_X_COUNT  0xffc00c10   /* DMA Channel 0 X Count Register */
#define                    DMA0_X_MODIFY  0xffc00c14   /* DMA Channel 0 X Modify Register */
#define                     DMA0_Y_COUNT  0xffc00c18   /* DMA Channel 0 Y Count Register */
#define                    DMA0_Y_MODIFY  0xffc00c1c   /* DMA Channel 0 Y Modify Register */
#define               DMA0_CURR_DESC_PTR  0xffc00c20   /* DMA Channel 0 Current Descriptor Pointer Register */
#define                   DMA0_CURR_ADDR  0xffc00c24   /* DMA Channel 0 Current Address Register */
#define                  DMA0_IRQ_STATUS  0xffc00c28   /* DMA Channel 0 Interrupt/Status Register */
#define              DMA0_PERIPHERAL_MAP  0xffc00c2c   /* DMA Channel 0 Peripheral Map Register */
#define                DMA0_CURR_X_COUNT  0xffc00c30   /* DMA Channel 0 Current X Count Register */
#define                DMA0_CURR_Y_COUNT  0xffc00c38   /* DMA Channel 0 Current Y Count Register */

/* DMA Channel 1 Registers */

#define               DMA1_NEXT_DESC_PTR  0xffc00c40   /* DMA Channel 1 Next Descriptor Pointer Register */
#define                  DMA1_START_ADDR  0xffc00c44   /* DMA Channel 1 Start Address Register */
#define                      DMA1_CONFIG  0xffc00c48   /* DMA Channel 1 Configuration Register */
#define                     DMA1_X_COUNT  0xffc00c50   /* DMA Channel 1 X Count Register */
#define                    DMA1_X_MODIFY  0xffc00c54   /* DMA Channel 1 X Modify Register */
#define                     DMA1_Y_COUNT  0xffc00c58   /* DMA Channel 1 Y Count Register */
#define                    DMA1_Y_MODIFY  0xffc00c5c   /* DMA Channel 1 Y Modify Register */
#define               DMA1_CURR_DESC_PTR  0xffc00c60   /* DMA Channel 1 Current Descriptor Pointer Register */
#define                   DMA1_CURR_ADDR  0xffc00c64   /* DMA Channel 1 Current Address Register */
#define                  DMA1_IRQ_STATUS  0xffc00c68   /* DMA Channel 1 Interrupt/Status Register */
#define              DMA1_PERIPHERAL_MAP  0xffc00c6c   /* DMA Channel 1 Peripheral Map Register */
#define                DMA1_CURR_X_COUNT  0xffc00c70   /* DMA Channel 1 Current X Count Register */
#define                DMA1_CURR_Y_COUNT  0xffc00c78   /* DMA Channel 1 Current Y Count Register */

/* DMA Channel 2 Registers */

#define               DMA2_NEXT_DESC_PTR  0xffc00c80   /* DMA Channel 2 Next Descriptor Pointer Register */
#define                  DMA2_START_ADDR  0xffc00c84   /* DMA Channel 2 Start Address Register */
#define                      DMA2_CONFIG  0xffc00c88   /* DMA Channel 2 Configuration Register */
#define                     DMA2_X_COUNT  0xffc00c90   /* DMA Channel 2 X Count Register */
#define                    DMA2_X_MODIFY  0xffc00c94   /* DMA Channel 2 X Modify Register */
#define                     DMA2_Y_COUNT  0xffc00c98   /* DMA Channel 2 Y Count Register */
#define                    DMA2_Y_MODIFY  0xffc00c9c   /* DMA Channel 2 Y Modify Register */
#define               DMA2_CURR_DESC_PTR  0xffc00ca0   /* DMA Channel 2 Current Descriptor Pointer Register */
#define                   DMA2_CURR_ADDR  0xffc00ca4   /* DMA Channel 2 Current Address Register */
#define                  DMA2_IRQ_STATUS  0xffc00ca8   /* DMA Channel 2 Interrupt/Status Register */
#define              DMA2_PERIPHERAL_MAP  0xffc00cac   /* DMA Channel 2 Peripheral Map Register */
#define                DMA2_CURR_X_COUNT  0xffc00cb0   /* DMA Channel 2 Current X Count Register */
#define                DMA2_CURR_Y_COUNT  0xffc00cb8   /* DMA Channel 2 Current Y Count Register */

/* DMA Channel 3 Registers */

#define               DMA3_NEXT_DESC_PTR  0xffc00cc0   /* DMA Channel 3 Next Descriptor Pointer Register */
#define                  DMA3_START_ADDR  0xffc00cc4   /* DMA Channel 3 Start Address Register */
#define                      DMA3_CONFIG  0xffc00cc8   /* DMA Channel 3 Configuration Register */
#define                     DMA3_X_COUNT  0xffc00cd0   /* DMA Channel 3 X Count Register */
#define                    DMA3_X_MODIFY  0xffc00cd4   /* DMA Channel 3 X Modify Register */
#define                     DMA3_Y_COUNT  0xffc00cd8   /* DMA Channel 3 Y Count Register */
#define                    DMA3_Y_MODIFY  0xffc00cdc   /* DMA Channel 3 Y Modify Register */
#define               DMA3_CURR_DESC_PTR  0xffc00ce0   /* DMA Channel 3 Current Descriptor Pointer Register */
#define                   DMA3_CURR_ADDR  0xffc00ce4   /* DMA Channel 3 Current Address Register */
#define                  DMA3_IRQ_STATUS  0xffc00ce8   /* DMA Channel 3 Interrupt/Status Register */
#define              DMA3_PERIPHERAL_MAP  0xffc00cec   /* DMA Channel 3 Peripheral Map Register */
#define                DMA3_CURR_X_COUNT  0xffc00cf0   /* DMA Channel 3 Current X Count Register */
#define                DMA3_CURR_Y_COUNT  0xffc00cf8   /* DMA Channel 3 Current Y Count Register */

/* DMA Channel 4 Registers */

#define               DMA4_NEXT_DESC_PTR  0xffc00d00   /* DMA Channel 4 Next Descriptor Pointer Register */
#define                  DMA4_START_ADDR  0xffc00d04   /* DMA Channel 4 Start Address Register */
#define                      DMA4_CONFIG  0xffc00d08   /* DMA Channel 4 Configuration Register */
#define                     DMA4_X_COUNT  0xffc00d10   /* DMA Channel 4 X Count Register */
#define                    DMA4_X_MODIFY  0xffc00d14   /* DMA Channel 4 X Modify Register */
#define                     DMA4_Y_COUNT  0xffc00d18   /* DMA Channel 4 Y Count Register */
#define                    DMA4_Y_MODIFY  0xffc00d1c   /* DMA Channel 4 Y Modify Register */
#define               DMA4_CURR_DESC_PTR  0xffc00d20   /* DMA Channel 4 Current Descriptor Pointer Register */
#define                   DMA4_CURR_ADDR  0xffc00d24   /* DMA Channel 4 Current Address Register */
#define                  DMA4_IRQ_STATUS  0xffc00d28   /* DMA Channel 4 Interrupt/Status Register */
#define              DMA4_PERIPHERAL_MAP  0xffc00d2c   /* DMA Channel 4 Peripheral Map Register */
#define                DMA4_CURR_X_COUNT  0xffc00d30   /* DMA Channel 4 Current X Count Register */
#define                DMA4_CURR_Y_COUNT  0xffc00d38   /* DMA Channel 4 Current Y Count Register */

/* DMA Channel 5 Registers */

#define               DMA5_NEXT_DESC_PTR  0xffc00d40   /* DMA Channel 5 Next Descriptor Pointer Register */
#define                  DMA5_START_ADDR  0xffc00d44   /* DMA Channel 5 Start Address Register */
#define                      DMA5_CONFIG  0xffc00d48   /* DMA Channel 5 Configuration Register */
#define                     DMA5_X_COUNT  0xffc00d50   /* DMA Channel 5 X Count Register */
#define                    DMA5_X_MODIFY  0xffc00d54   /* DMA Channel 5 X Modify Register */
#define                     DMA5_Y_COUNT  0xffc00d58   /* DMA Channel 5 Y Count Register */
#define                    DMA5_Y_MODIFY  0xffc00d5c   /* DMA Channel 5 Y Modify Register */
#define               DMA5_CURR_DESC_PTR  0xffc00d60   /* DMA Channel 5 Current Descriptor Pointer Register */
#define                   DMA5_CURR_ADDR  0xffc00d64   /* DMA Channel 5 Current Address Register */
#define                  DMA5_IRQ_STATUS  0xffc00d68   /* DMA Channel 5 Interrupt/Status Register */
#define              DMA5_PERIPHERAL_MAP  0xffc00d6c   /* DMA Channel 5 Peripheral Map Register */
#define                DMA5_CURR_X_COUNT  0xffc00d70   /* DMA Channel 5 Current X Count Register */
#define                DMA5_CURR_Y_COUNT  0xffc00d78   /* DMA Channel 5 Current Y Count Register */

/* DMA Channel 6 Registers */

#define               DMA6_NEXT_DESC_PTR  0xffc00d80   /* DMA Channel 6 Next Descriptor Pointer Register */
#define                  DMA6_START_ADDR  0xffc00d84   /* DMA Channel 6 Start Address Register */
#define                      DMA6_CONFIG  0xffc00d88   /* DMA Channel 6 Configuration Register */
#define                     DMA6_X_COUNT  0xffc00d90   /* DMA Channel 6 X Count Register */
#define                    DMA6_X_MODIFY  0xffc00d94   /* DMA Channel 6 X Modify Register */
#define                     DMA6_Y_COUNT  0xffc00d98   /* DMA Channel 6 Y Count Register */
#define                    DMA6_Y_MODIFY  0xffc00d9c   /* DMA Channel 6 Y Modify Register */
#define               DMA6_CURR_DESC_PTR  0xffc00da0   /* DMA Channel 6 Current Descriptor Pointer Register */
#define                   DMA6_CURR_ADDR  0xffc00da4   /* DMA Channel 6 Current Address Register */
#define                  DMA6_IRQ_STATUS  0xffc00da8   /* DMA Channel 6 Interrupt/Status Register */
#define              DMA6_PERIPHERAL_MAP  0xffc00dac   /* DMA Channel 6 Peripheral Map Register */
#define                DMA6_CURR_X_COUNT  0xffc00db0   /* DMA Channel 6 Current X Count Register */
#define                DMA6_CURR_Y_COUNT  0xffc00db8   /* DMA Channel 6 Current Y Count Register */

/* DMA Channel 7 Registers */

#define               DMA7_NEXT_DESC_PTR  0xffc00dc0   /* DMA Channel 7 Next Descriptor Pointer Register */
#define                  DMA7_START_ADDR  0xffc00dc4   /* DMA Channel 7 Start Address Register */
#define                      DMA7_CONFIG  0xffc00dc8   /* DMA Channel 7 Configuration Register */
#define                     DMA7_X_COUNT  0xffc00dd0   /* DMA Channel 7 X Count Register */
#define                    DMA7_X_MODIFY  0xffc00dd4   /* DMA Channel 7 X Modify Register */
#define                     DMA7_Y_COUNT  0xffc00dd8   /* DMA Channel 7 Y Count Register */
#define                    DMA7_Y_MODIFY  0xffc00ddc   /* DMA Channel 7 Y Modify Register */
#define               DMA7_CURR_DESC_PTR  0xffc00de0   /* DMA Channel 7 Current Descriptor Pointer Register */
#define                   DMA7_CURR_ADDR  0xffc00de4   /* DMA Channel 7 Current Address Register */
#define                  DMA7_IRQ_STATUS  0xffc00de8   /* DMA Channel 7 Interrupt/Status Register */
#define              DMA7_PERIPHERAL_MAP  0xffc00dec   /* DMA Channel 7 Peripheral Map Register */
#define                DMA7_CURR_X_COUNT  0xffc00df0   /* DMA Channel 7 Current X Count Register */
#define                DMA7_CURR_Y_COUNT  0xffc00df8   /* DMA Channel 7 Current Y Count Register */

/* DMA Channel 8 Registers */

#define               DMA8_NEXT_DESC_PTR  0xffc00e00   /* DMA Channel 8 Next Descriptor Pointer Register */
#define                  DMA8_START_ADDR  0xffc00e04   /* DMA Channel 8 Start Address Register */
#define                      DMA8_CONFIG  0xffc00e08   /* DMA Channel 8 Configuration Register */
#define                     DMA8_X_COUNT  0xffc00e10   /* DMA Channel 8 X Count Register */
#define                    DMA8_X_MODIFY  0xffc00e14   /* DMA Channel 8 X Modify Register */
#define                     DMA8_Y_COUNT  0xffc00e18   /* DMA Channel 8 Y Count Register */
#define                    DMA8_Y_MODIFY  0xffc00e1c   /* DMA Channel 8 Y Modify Register */
#define               DMA8_CURR_DESC_PTR  0xffc00e20   /* DMA Channel 8 Current Descriptor Pointer Register */
#define                   DMA8_CURR_ADDR  0xffc00e24   /* DMA Channel 8 Current Address Register */
#define                  DMA8_IRQ_STATUS  0xffc00e28   /* DMA Channel 8 Interrupt/Status Register */
#define              DMA8_PERIPHERAL_MAP  0xffc00e2c   /* DMA Channel 8 Peripheral Map Register */
#define                DMA8_CURR_X_COUNT  0xffc00e30   /* DMA Channel 8 Current X Count Register */
#define                DMA8_CURR_Y_COUNT  0xffc00e38   /* DMA Channel 8 Current Y Count Register */

/* DMA Channel 9 Registers */

#define               DMA9_NEXT_DESC_PTR  0xffc00e40   /* DMA Channel 9 Next Descriptor Pointer Register */
#define                  DMA9_START_ADDR  0xffc00e44   /* DMA Channel 9 Start Address Register */
#define                      DMA9_CONFIG  0xffc00e48   /* DMA Channel 9 Configuration Register */
#define                     DMA9_X_COUNT  0xffc00e50   /* DMA Channel 9 X Count Register */
#define                    DMA9_X_MODIFY  0xffc00e54   /* DMA Channel 9 X Modify Register */
#define                     DMA9_Y_COUNT  0xffc00e58   /* DMA Channel 9 Y Count Register */
#define                    DMA9_Y_MODIFY  0xffc00e5c   /* DMA Channel 9 Y Modify Register */
#define               DMA9_CURR_DESC_PTR  0xffc00e60   /* DMA Channel 9 Current Descriptor Pointer Register */
#define                   DMA9_CURR_ADDR  0xffc00e64   /* DMA Channel 9 Current Address Register */
#define                  DMA9_IRQ_STATUS  0xffc00e68   /* DMA Channel 9 Interrupt/Status Register */
#define              DMA9_PERIPHERAL_MAP  0xffc00e6c   /* DMA Channel 9 Peripheral Map Register */
#define                DMA9_CURR_X_COUNT  0xffc00e70   /* DMA Channel 9 Current X Count Register */
#define                DMA9_CURR_Y_COUNT  0xffc00e78   /* DMA Channel 9 Current Y Count Register */

/* DMA Channel 10 Registers */

#define              DMA10_NEXT_DESC_PTR  0xffc00e80   /* DMA Channel 10 Next Descriptor Pointer Register */
#define                 DMA10_START_ADDR  0xffc00e84   /* DMA Channel 10 Start Address Register */
#define                     DMA10_CONFIG  0xffc00e88   /* DMA Channel 10 Configuration Register */
#define                    DMA10_X_COUNT  0xffc00e90   /* DMA Channel 10 X Count Register */
#define                   DMA10_X_MODIFY  0xffc00e94   /* DMA Channel 10 X Modify Register */
#define                    DMA10_Y_COUNT  0xffc00e98   /* DMA Channel 10 Y Count Register */
#define                   DMA10_Y_MODIFY  0xffc00e9c   /* DMA Channel 10 Y Modify Register */
#define              DMA10_CURR_DESC_PTR  0xffc00ea0   /* DMA Channel 10 Current Descriptor Pointer Register */
#define                  DMA10_CURR_ADDR  0xffc00ea4   /* DMA Channel 10 Current Address Register */
#define                 DMA10_IRQ_STATUS  0xffc00ea8   /* DMA Channel 10 Interrupt/Status Register */
#define             DMA10_PERIPHERAL_MAP  0xffc00eac   /* DMA Channel 10 Peripheral Map Register */
#define               DMA10_CURR_X_COUNT  0xffc00eb0   /* DMA Channel 10 Current X Count Register */
#define               DMA10_CURR_Y_COUNT  0xffc00eb8   /* DMA Channel 10 Current Y Count Register */

/* DMA Channel 11 Registers */

#define              DMA11_NEXT_DESC_PTR  0xffc00ec0   /* DMA Channel 11 Next Descriptor Pointer Register */
#define                 DMA11_START_ADDR  0xffc00ec4   /* DMA Channel 11 Start Address Register */
#define                     DMA11_CONFIG  0xffc00ec8   /* DMA Channel 11 Configuration Register */
#define                    DMA11_X_COUNT  0xffc00ed0   /* DMA Channel 11 X Count Register */
#define                   DMA11_X_MODIFY  0xffc00ed4   /* DMA Channel 11 X Modify Register */
#define                    DMA11_Y_COUNT  0xffc00ed8   /* DMA Channel 11 Y Count Register */
#define                   DMA11_Y_MODIFY  0xffc00edc   /* DMA Channel 11 Y Modify Register */
#define              DMA11_CURR_DESC_PTR  0xffc00ee0   /* DMA Channel 11 Current Descriptor Pointer Register */
#define                  DMA11_CURR_ADDR  0xffc00ee4   /* DMA Channel 11 Current Address Register */
#define                 DMA11_IRQ_STATUS  0xffc00ee8   /* DMA Channel 11 Interrupt/Status Register */
#define             DMA11_PERIPHERAL_MAP  0xffc00eec   /* DMA Channel 11 Peripheral Map Register */
#define               DMA11_CURR_X_COUNT  0xffc00ef0   /* DMA Channel 11 Current X Count Register */
#define               DMA11_CURR_Y_COUNT  0xffc00ef8   /* DMA Channel 11 Current Y Count Register */

/* MDMA Stream 0 Registers */

#define            MDMA_D0_NEXT_DESC_PTR  0xffc00f00   /* Memory DMA Stream 0 Destination Next Descriptor Pointer Register */
#define               MDMA_D0_START_ADDR  0xffc00f04   /* Memory DMA Stream 0 Destination Start Address Register */
#define                   MDMA_D0_CONFIG  0xffc00f08   /* Memory DMA Stream 0 Destination Configuration Register */
#define                  MDMA_D0_X_COUNT  0xffc00f10   /* Memory DMA Stream 0 Destination X Count Register */
#define                 MDMA_D0_X_MODIFY  0xffc00f14   /* Memory DMA Stream 0 Destination X Modify Register */
#define                  MDMA_D0_Y_COUNT  0xffc00f18   /* Memory DMA Stream 0 Destination Y Count Register */
#define                 MDMA_D0_Y_MODIFY  0xffc00f1c   /* Memory DMA Stream 0 Destination Y Modify Register */
#define            MDMA_D0_CURR_DESC_PTR  0xffc00f20   /* Memory DMA Stream 0 Destination Current Descriptor Pointer Register */
#define                MDMA_D0_CURR_ADDR  0xffc00f24   /* Memory DMA Stream 0 Destination Current Address Register */
#define               MDMA_D0_IRQ_STATUS  0xffc00f28   /* Memory DMA Stream 0 Destination Interrupt/Status Register */
#define           MDMA_D0_PERIPHERAL_MAP  0xffc00f2c   /* Memory DMA Stream 0 Destination Peripheral Map Register */
#define             MDMA_D0_CURR_X_COUNT  0xffc00f30   /* Memory DMA Stream 0 Destination Current X Count Register */
#define             MDMA_D0_CURR_Y_COUNT  0xffc00f38   /* Memory DMA Stream 0 Destination Current Y Count Register */
#define            MDMA_S0_NEXT_DESC_PTR  0xffc00f40   /* Memory DMA Stream 0 Source Next Descriptor Pointer Register */
#define               MDMA_S0_START_ADDR  0xffc00f44   /* Memory DMA Stream 0 Source Start Address Register */
#define                   MDMA_S0_CONFIG  0xffc00f48   /* Memory DMA Stream 0 Source Configuration Register */
#define                  MDMA_S0_X_COUNT  0xffc00f50   /* Memory DMA Stream 0 Source X Count Register */
#define                 MDMA_S0_X_MODIFY  0xffc00f54   /* Memory DMA Stream 0 Source X Modify Register */
#define                  MDMA_S0_Y_COUNT  0xffc00f58   /* Memory DMA Stream 0 Source Y Count Register */
#define                 MDMA_S0_Y_MODIFY  0xffc00f5c   /* Memory DMA Stream 0 Source Y Modify Register */
#define            MDMA_S0_CURR_DESC_PTR  0xffc00f60   /* Memory DMA Stream 0 Source Current Descriptor Pointer Register */
#define                MDMA_S0_CURR_ADDR  0xffc00f64   /* Memory DMA Stream 0 Source Current Address Register */
#define               MDMA_S0_IRQ_STATUS  0xffc00f68   /* Memory DMA Stream 0 Source Interrupt/Status Register */
#define           MDMA_S0_PERIPHERAL_MAP  0xffc00f6c   /* Memory DMA Stream 0 Source Peripheral Map Register */
#define             MDMA_S0_CURR_X_COUNT  0xffc00f70   /* Memory DMA Stream 0 Source Current X Count Register */
#define             MDMA_S0_CURR_Y_COUNT  0xffc00f78   /* Memory DMA Stream 0 Source Current Y Count Register */

/* MDMA Stream 1 Registers */

#define            MDMA_D1_NEXT_DESC_PTR  0xffc00f80   /* Memory DMA Stream 1 Destination Next Descriptor Pointer Register */
#define               MDMA_D1_START_ADDR  0xffc00f84   /* Memory DMA Stream 1 Destination Start Address Register */
#define                   MDMA_D1_CONFIG  0xffc00f88   /* Memory DMA Stream 1 Destination Configuration Register */
#define                  MDMA_D1_X_COUNT  0xffc00f90   /* Memory DMA Stream 1 Destination X Count Register */
#define                 MDMA_D1_X_MODIFY  0xffc00f94   /* Memory DMA Stream 1 Destination X Modify Register */
#define                  MDMA_D1_Y_COUNT  0xffc00f98   /* Memory DMA Stream 1 Destination Y Count Register */
#define                 MDMA_D1_Y_MODIFY  0xffc00f9c   /* Memory DMA Stream 1 Destination Y Modify Register */
#define            MDMA_D1_CURR_DESC_PTR  0xffc00fa0   /* Memory DMA Stream 1 Destination Current Descriptor Pointer Register */
#define                MDMA_D1_CURR_ADDR  0xffc00fa4   /* Memory DMA Stream 1 Destination Current Address Register */
#define               MDMA_D1_IRQ_STATUS  0xffc00fa8   /* Memory DMA Stream 1 Destination Interrupt/Status Register */
#define           MDMA_D1_PERIPHERAL_MAP  0xffc00fac   /* Memory DMA Stream 1 Destination Peripheral Map Register */
#define             MDMA_D1_CURR_X_COUNT  0xffc00fb0   /* Memory DMA Stream 1 Destination Current X Count Register */
#define             MDMA_D1_CURR_Y_COUNT  0xffc00fb8   /* Memory DMA Stream 1 Destination Current Y Count Register */
#define            MDMA_S1_NEXT_DESC_PTR  0xffc00fc0   /* Memory DMA Stream 1 Source Next Descriptor Pointer Register */
#define               MDMA_S1_START_ADDR  0xffc00fc4   /* Memory DMA Stream 1 Source Start Address Register */
#define                   MDMA_S1_CONFIG  0xffc00fc8   /* Memory DMA Stream 1 Source Configuration Register */
#define                  MDMA_S1_X_COUNT  0xffc00fd0   /* Memory DMA Stream 1 Source X Count Register */
#define                 MDMA_S1_X_MODIFY  0xffc00fd4   /* Memory DMA Stream 1 Source X Modify Register */
#define                  MDMA_S1_Y_COUNT  0xffc00fd8   /* Memory DMA Stream 1 Source Y Count Register */
#define                 MDMA_S1_Y_MODIFY  0xffc00fdc   /* Memory DMA Stream 1 Source Y Modify Register */
#define            MDMA_S1_CURR_DESC_PTR  0xffc00fe0   /* Memory DMA Stream 1 Source Current Descriptor Pointer Register */
#define                MDMA_S1_CURR_ADDR  0xffc00fe4   /* Memory DMA Stream 1 Source Current Address Register */
#define               MDMA_S1_IRQ_STATUS  0xffc00fe8   /* Memory DMA Stream 1 Source Interrupt/Status Register */
#define           MDMA_S1_PERIPHERAL_MAP  0xffc00fec   /* Memory DMA Stream 1 Source Peripheral Map Register */
#define             MDMA_S1_CURR_X_COUNT  0xffc00ff0   /* Memory DMA Stream 1 Source Current X Count Register */
#define             MDMA_S1_CURR_Y_COUNT  0xffc00ff8   /* Memory DMA Stream 1 Source Current Y Count Register */

/* UART3 Registers */

#define                        UART3_DLL  0xffc03100   /* Divisor Latch Low Byte */
#define                        UART3_DLH  0xffc03104   /* Divisor Latch High Byte */
#define                       UART3_GCTL  0xffc03108   /* Global Control Register */
#define                        UART3_LCR  0xffc0310c   /* Line Control Register */
#define                        UART3_MCR  0xffc03110   /* Modem Control Register */
#define                        UART3_LSR  0xffc03114   /* Line Status Register */
#define                        UART3_MSR  0xffc03118   /* Modem Status Register */
#define                        UART3_SCR  0xffc0311c   /* Scratch Register */
#define                    UART3_IER_SET  0xffc03120   /* Interrupt Enable Register Set */
#define                  UART3_IER_CLEAR  0xffc03124   /* Interrupt Enable Register Clear */
#define                        UART3_THR  0xffc03128   /* Transmit Hold Register */
#define                        UART3_RBR  0xffc0312c   /* Receive Buffer Register */

/* EPPI1 Registers */

#define                     EPPI1_STATUS  0xffc01300   /* EPPI1 Status Register */
#define                     EPPI1_HCOUNT  0xffc01304   /* EPPI1 Horizontal Transfer Count Register */
#define                     EPPI1_HDELAY  0xffc01308   /* EPPI1 Horizontal Delay Count Register */
#define                     EPPI1_VCOUNT  0xffc0130c   /* EPPI1 Vertical Transfer Count Register */
#define                     EPPI1_VDELAY  0xffc01310   /* EPPI1 Vertical Delay Count Register */
#define                      EPPI1_FRAME  0xffc01314   /* EPPI1 Lines per Frame Register */
#define                       EPPI1_LINE  0xffc01318   /* EPPI1 Samples per Line Register */
#define                     EPPI1_CLKDIV  0xffc0131c   /* EPPI1 Clock Divide Register */
#define                    EPPI1_CONTROL  0xffc01320   /* EPPI1 Control Register */
#define                   EPPI1_FS1W_HBL  0xffc01324   /* EPPI1 FS1 Width Register / EPPI1 Horizontal Blanking Samples Per Line Register */
#define                  EPPI1_FS1P_AVPL  0xffc01328   /* EPPI1 FS1 Period Register / EPPI1 Active Video Samples Per Line Register */
#define                   EPPI1_FS2W_LVB  0xffc0132c   /* EPPI1 FS2 Width Register / EPPI1 Lines of Vertical Blanking Register */
#define                  EPPI1_FS2P_LAVF  0xffc01330   /* EPPI1 FS2 Period Register/ EPPI1 Lines of Active Video Per Field Register */
#define                       EPPI1_CLIP  0xffc01334   /* EPPI1 Clipping Register */

/* Port Interrupt 0 Registers (32-bit) */

#define                   PINT0_MASK_SET  0xffc01400   /* Pin Interrupt 0 Mask Set Register */
#define                 PINT0_MASK_CLEAR  0xffc01404   /* Pin Interrupt 0 Mask Clear Register */
#define                    PINT0_REQUEST  0xffc01408   /* Pin Interrupt 0 Interrupt Request Register */
#define                     PINT0_ASSIGN  0xffc0140c   /* Pin Interrupt 0 Port Assign Register */
#define                   PINT0_EDGE_SET  0xffc01410   /* Pin Interrupt 0 Edge-sensitivity Set Register */
#define                 PINT0_EDGE_CLEAR  0xffc01414   /* Pin Interrupt 0 Edge-sensitivity Clear Register */
#define                 PINT0_INVERT_SET  0xffc01418   /* Pin Interrupt 0 Inversion Set Register */
#define               PINT0_INVERT_CLEAR  0xffc0141c   /* Pin Interrupt 0 Inversion Clear Register */
#define                   PINT0_PINSTATE  0xffc01420   /* Pin Interrupt 0 Pin Status Register */
#define                      PINT0_LATCH  0xffc01424   /* Pin Interrupt 0 Latch Register */

/* Port Interrupt 1 Registers (32-bit) */

#define                   PINT1_MASK_SET  0xffc01430   /* Pin Interrupt 1 Mask Set Register */
#define                 PINT1_MASK_CLEAR  0xffc01434   /* Pin Interrupt 1 Mask Clear Register */
#define                    PINT1_REQUEST  0xffc01438   /* Pin Interrupt 1 Interrupt Request Register */
#define                     PINT1_ASSIGN  0xffc0143c   /* Pin Interrupt 1 Port Assign Register */
#define                   PINT1_EDGE_SET  0xffc01440   /* Pin Interrupt 1 Edge-sensitivity Set Register */
#define                 PINT1_EDGE_CLEAR  0xffc01444   /* Pin Interrupt 1 Edge-sensitivity Clear Register */
#define                 PINT1_INVERT_SET  0xffc01448   /* Pin Interrupt 1 Inversion Set Register */
#define               PINT1_INVERT_CLEAR  0xffc0144c   /* Pin Interrupt 1 Inversion Clear Register */
#define                   PINT1_PINSTATE  0xffc01450   /* Pin Interrupt 1 Pin Status Register */
#define                      PINT1_LATCH  0xffc01454   /* Pin Interrupt 1 Latch Register */

/* Port Interrupt 2 Registers (32-bit) */

#define                   PINT2_MASK_SET  0xffc01460   /* Pin Interrupt 2 Mask Set Register */
#define                 PINT2_MASK_CLEAR  0xffc01464   /* Pin Interrupt 2 Mask Clear Register */
#define                    PINT2_REQUEST  0xffc01468   /* Pin Interrupt 2 Interrupt Request Register */
#define                     PINT2_ASSIGN  0xffc0146c   /* Pin Interrupt 2 Port Assign Register */
#define                   PINT2_EDGE_SET  0xffc01470   /* Pin Interrupt 2 Edge-sensitivity Set Register */
#define                 PINT2_EDGE_CLEAR  0xffc01474   /* Pin Interrupt 2 Edge-sensitivity Clear Register */
#define                 PINT2_INVERT_SET  0xffc01478   /* Pin Interrupt 2 Inversion Set Register */
#define               PINT2_INVERT_CLEAR  0xffc0147c   /* Pin Interrupt 2 Inversion Clear Register */
#define                   PINT2_PINSTATE  0xffc01480   /* Pin Interrupt 2 Pin Status Register */
#define                      PINT2_LATCH  0xffc01484   /* Pin Interrupt 2 Latch Register */

/* Port Interrupt 3 Registers (32-bit) */

#define                   PINT3_MASK_SET  0xffc01490   /* Pin Interrupt 3 Mask Set Register */
#define                 PINT3_MASK_CLEAR  0xffc01494   /* Pin Interrupt 3 Mask Clear Register */
#define                    PINT3_REQUEST  0xffc01498   /* Pin Interrupt 3 Interrupt Request Register */
#define                     PINT3_ASSIGN  0xffc0149c   /* Pin Interrupt 3 Port Assign Register */
#define                   PINT3_EDGE_SET  0xffc014a0   /* Pin Interrupt 3 Edge-sensitivity Set Register */
#define                 PINT3_EDGE_CLEAR  0xffc014a4   /* Pin Interrupt 3 Edge-sensitivity Clear Register */
#define                 PINT3_INVERT_SET  0xffc014a8   /* Pin Interrupt 3 Inversion Set Register */
#define               PINT3_INVERT_CLEAR  0xffc014ac   /* Pin Interrupt 3 Inversion Clear Register */
#define                   PINT3_PINSTATE  0xffc014b0   /* Pin Interrupt 3 Pin Status Register */
#define                      PINT3_LATCH  0xffc014b4   /* Pin Interrupt 3 Latch Register */

/* Port A Registers */

#define                        PORTA_FER  0xffc014c0   /* Function Enable Register */
#define                            PORTA  0xffc014c4   /* GPIO Data Register */
#define                        PORTA_SET  0xffc014c8   /* GPIO Data Set Register */
#define                      PORTA_CLEAR  0xffc014cc   /* GPIO Data Clear Register */
#define                    PORTA_DIR_SET  0xffc014d0   /* GPIO Direction Set Register */
#define                  PORTA_DIR_CLEAR  0xffc014d4   /* GPIO Direction Clear Register */
#define                       PORTA_INEN  0xffc014d8   /* GPIO Input Enable Register */
#define                        PORTA_MUX  0xffc014dc   /* Multiplexer Control Register */

/* Port B Registers */

#define                        PORTB_FER  0xffc014e0   /* Function Enable Register */
#define                            PORTB  0xffc014e4   /* GPIO Data Register */
#define                        PORTB_SET  0xffc014e8   /* GPIO Data Set Register */
#define                      PORTB_CLEAR  0xffc014ec   /* GPIO Data Clear Register */
#define                    PORTB_DIR_SET  0xffc014f0   /* GPIO Direction Set Register */
#define                  PORTB_DIR_CLEAR  0xffc014f4   /* GPIO Direction Clear Register */
#define                       PORTB_INEN  0xffc014f8   /* GPIO Input Enable Register */
#define                        PORTB_MUX  0xffc014fc   /* Multiplexer Control Register */

/* Port C Registers */

#define                        PORTC_FER  0xffc01500   /* Function Enable Register */
#define                            PORTC  0xffc01504   /* GPIO Data Register */
#define                        PORTC_SET  0xffc01508   /* GPIO Data Set Register */
#define                      PORTC_CLEAR  0xffc0150c   /* GPIO Data Clear Register */
#define                    PORTC_DIR_SET  0xffc01510   /* GPIO Direction Set Register */
#define                  PORTC_DIR_CLEAR  0xffc01514   /* GPIO Direction Clear Register */
#define                       PORTC_INEN  0xffc01518   /* GPIO Input Enable Register */
#define                        PORTC_MUX  0xffc0151c   /* Multiplexer Control Register */

/* Port D Registers */

#define                        PORTD_FER  0xffc01520   /* Function Enable Register */
#define                            PORTD  0xffc01524   /* GPIO Data Register */
#define                        PORTD_SET  0xffc01528   /* GPIO Data Set Register */
#define                      PORTD_CLEAR  0xffc0152c   /* GPIO Data Clear Register */
#define                    PORTD_DIR_SET  0xffc01530   /* GPIO Direction Set Register */
#define                  PORTD_DIR_CLEAR  0xffc01534   /* GPIO Direction Clear Register */
#define                       PORTD_INEN  0xffc01538   /* GPIO Input Enable Register */
#define                        PORTD_MUX  0xffc0153c   /* Multiplexer Control Register */

/* Port E Registers */

#define                        PORTE_FER  0xffc01540   /* Function Enable Register */
#define                            PORTE  0xffc01544   /* GPIO Data Register */
#define                        PORTE_SET  0xffc01548   /* GPIO Data Set Register */
#define                      PORTE_CLEAR  0xffc0154c   /* GPIO Data Clear Register */
#define                    PORTE_DIR_SET  0xffc01550   /* GPIO Direction Set Register */
#define                  PORTE_DIR_CLEAR  0xffc01554   /* GPIO Direction Clear Register */
#define                       PORTE_INEN  0xffc01558   /* GPIO Input Enable Register */
#define                        PORTE_MUX  0xffc0155c   /* Multiplexer Control Register */

/* Port F Registers */

#define                        PORTF_FER  0xffc01560   /* Function Enable Register */
#define                            PORTF  0xffc01564   /* GPIO Data Register */
#define                        PORTF_SET  0xffc01568   /* GPIO Data Set Register */
#define                      PORTF_CLEAR  0xffc0156c   /* GPIO Data Clear Register */
#define                    PORTF_DIR_SET  0xffc01570   /* GPIO Direction Set Register */
#define                  PORTF_DIR_CLEAR  0xffc01574   /* GPIO Direction Clear Register */
#define                       PORTF_INEN  0xffc01578   /* GPIO Input Enable Register */
#define                        PORTF_MUX  0xffc0157c   /* Multiplexer Control Register */

/* Port G Registers */

#define                        PORTG_FER  0xffc01580   /* Function Enable Register */
#define                            PORTG  0xffc01584   /* GPIO Data Register */
#define                        PORTG_SET  0xffc01588   /* GPIO Data Set Register */
#define                      PORTG_CLEAR  0xffc0158c   /* GPIO Data Clear Register */
#define                    PORTG_DIR_SET  0xffc01590   /* GPIO Direction Set Register */
#define                  PORTG_DIR_CLEAR  0xffc01594   /* GPIO Direction Clear Register */
#define                       PORTG_INEN  0xffc01598   /* GPIO Input Enable Register */
#define                        PORTG_MUX  0xffc0159c   /* Multiplexer Control Register */

/* Port H Registers */

#define                        PORTH_FER  0xffc015a0   /* Function Enable Register */
#define                            PORTH  0xffc015a4   /* GPIO Data Register */
#define                        PORTH_SET  0xffc015a8   /* GPIO Data Set Register */
#define                      PORTH_CLEAR  0xffc015ac   /* GPIO Data Clear Register */
#define                    PORTH_DIR_SET  0xffc015b0   /* GPIO Direction Set Register */
#define                  PORTH_DIR_CLEAR  0xffc015b4   /* GPIO Direction Clear Register */
#define                       PORTH_INEN  0xffc015b8   /* GPIO Input Enable Register */
#define                        PORTH_MUX  0xffc015bc   /* Multiplexer Control Register */

/* Port I Registers */

#define                        PORTI_FER  0xffc015c0   /* Function Enable Register */
#define                            PORTI  0xffc015c4   /* GPIO Data Register */
#define                        PORTI_SET  0xffc015c8   /* GPIO Data Set Register */
#define                      PORTI_CLEAR  0xffc015cc   /* GPIO Data Clear Register */
#define                    PORTI_DIR_SET  0xffc015d0   /* GPIO Direction Set Register */
#define                  PORTI_DIR_CLEAR  0xffc015d4   /* GPIO Direction Clear Register */
#define                       PORTI_INEN  0xffc015d8   /* GPIO Input Enable Register */
#define                        PORTI_MUX  0xffc015dc   /* Multiplexer Control Register */

/* Port J Registers */

#define                        PORTJ_FER  0xffc015e0   /* Function Enable Register */
#define                            PORTJ  0xffc015e4   /* GPIO Data Register */
#define                        PORTJ_SET  0xffc015e8   /* GPIO Data Set Register */
#define                      PORTJ_CLEAR  0xffc015ec   /* GPIO Data Clear Register */
#define                    PORTJ_DIR_SET  0xffc015f0   /* GPIO Direction Set Register */
#define                  PORTJ_DIR_CLEAR  0xffc015f4   /* GPIO Direction Clear Register */
#define                       PORTJ_INEN  0xffc015f8   /* GPIO Input Enable Register */
#define                        PORTJ_MUX  0xffc015fc   /* Multiplexer Control Register */

/* PWM Timer Registers */

#define                    TIMER0_CONFIG  0xffc01600   /* Timer 0 Configuration Register */
#define                   TIMER0_COUNTER  0xffc01604   /* Timer 0 Counter Register */
#define                    TIMER0_PERIOD  0xffc01608   /* Timer 0 Period Register */
#define                     TIMER0_WIDTH  0xffc0160c   /* Timer 0 Width Register */
#define                    TIMER1_CONFIG  0xffc01610   /* Timer 1 Configuration Register */
#define                   TIMER1_COUNTER  0xffc01614   /* Timer 1 Counter Register */
#define                    TIMER1_PERIOD  0xffc01618   /* Timer 1 Period Register */
#define                     TIMER1_WIDTH  0xffc0161c   /* Timer 1 Width Register */
#define                    TIMER2_CONFIG  0xffc01620   /* Timer 2 Configuration Register */
#define                   TIMER2_COUNTER  0xffc01624   /* Timer 2 Counter Register */
#define                    TIMER2_PERIOD  0xffc01628   /* Timer 2 Period Register */
#define                     TIMER2_WIDTH  0xffc0162c   /* Timer 2 Width Register */
#define                    TIMER3_CONFIG  0xffc01630   /* Timer 3 Configuration Register */
#define                   TIMER3_COUNTER  0xffc01634   /* Timer 3 Counter Register */
#define                    TIMER3_PERIOD  0xffc01638   /* Timer 3 Period Register */
#define                     TIMER3_WIDTH  0xffc0163c   /* Timer 3 Width Register */
#define                    TIMER4_CONFIG  0xffc01640   /* Timer 4 Configuration Register */
#define                   TIMER4_COUNTER  0xffc01644   /* Timer 4 Counter Register */
#define                    TIMER4_PERIOD  0xffc01648   /* Timer 4 Period Register */
#define                     TIMER4_WIDTH  0xffc0164c   /* Timer 4 Width Register */
#define                    TIMER5_CONFIG  0xffc01650   /* Timer 5 Configuration Register */
#define                   TIMER5_COUNTER  0xffc01654   /* Timer 5 Counter Register */
#define                    TIMER5_PERIOD  0xffc01658   /* Timer 5 Period Register */
#define                     TIMER5_WIDTH  0xffc0165c   /* Timer 5 Width Register */
#define                    TIMER6_CONFIG  0xffc01660   /* Timer 6 Configuration Register */
#define                   TIMER6_COUNTER  0xffc01664   /* Timer 6 Counter Register */
#define                    TIMER6_PERIOD  0xffc01668   /* Timer 6 Period Register */
#define                     TIMER6_WIDTH  0xffc0166c   /* Timer 6 Width Register */
#define                    TIMER7_CONFIG  0xffc01670   /* Timer 7 Configuration Register */
#define                   TIMER7_COUNTER  0xffc01674   /* Timer 7 Counter Register */
#define                    TIMER7_PERIOD  0xffc01678   /* Timer 7 Period Register */
#define                     TIMER7_WIDTH  0xffc0167c   /* Timer 7 Width Register */

/* Timer Group of 8 */

#define                    TIMER_ENABLE0  0xffc01680   /* Timer Group of 8 Enable Register */
#define                   TIMER_DISABLE0  0xffc01684   /* Timer Group of 8 Disable Register */
#define                    TIMER_STATUS0  0xffc01688   /* Timer Group of 8 Status Register */

/* DMAC1 Registers */

#define                     DMAC1_TC_PER  0xffc01b0c   /* DMA Controller 1 Traffic Control Periods Register */
#define                     DMAC1_TC_CNT  0xffc01b10   /* DMA Controller 1 Current Counts Register */

/* DMA Channel 12 Registers */

#define              DMA12_NEXT_DESC_PTR  0xffc01c00   /* DMA Channel 12 Next Descriptor Pointer Register */
#define                 DMA12_START_ADDR  0xffc01c04   /* DMA Channel 12 Start Address Register */
#define                     DMA12_CONFIG  0xffc01c08   /* DMA Channel 12 Configuration Register */
#define                    DMA12_X_COUNT  0xffc01c10   /* DMA Channel 12 X Count Register */
#define                   DMA12_X_MODIFY  0xffc01c14   /* DMA Channel 12 X Modify Register */
#define                    DMA12_Y_COUNT  0xffc01c18   /* DMA Channel 12 Y Count Register */
#define                   DMA12_Y_MODIFY  0xffc01c1c   /* DMA Channel 12 Y Modify Register */
#define              DMA12_CURR_DESC_PTR  0xffc01c20   /* DMA Channel 12 Current Descriptor Pointer Register */
#define                  DMA12_CURR_ADDR  0xffc01c24   /* DMA Channel 12 Current Address Register */
#define                 DMA12_IRQ_STATUS  0xffc01c28   /* DMA Channel 12 Interrupt/Status Register */
#define             DMA12_PERIPHERAL_MAP  0xffc01c2c   /* DMA Channel 12 Peripheral Map Register */
#define               DMA12_CURR_X_COUNT  0xffc01c30   /* DMA Channel 12 Current X Count Register */
#define               DMA12_CURR_Y_COUNT  0xffc01c38   /* DMA Channel 12 Current Y Count Register */

/* DMA Channel 13 Registers */

#define              DMA13_NEXT_DESC_PTR  0xffc01c40   /* DMA Channel 13 Next Descriptor Pointer Register */
#define                 DMA13_START_ADDR  0xffc01c44   /* DMA Channel 13 Start Address Register */
#define                     DMA13_CONFIG  0xffc01c48   /* DMA Channel 13 Configuration Register */
#define                    DMA13_X_COUNT  0xffc01c50   /* DMA Channel 13 X Count Register */
#define                   DMA13_X_MODIFY  0xffc01c54   /* DMA Channel 13 X Modify Register */
#define                    DMA13_Y_COUNT  0xffc01c58   /* DMA Channel 13 Y Count Register */
#define                   DMA13_Y_MODIFY  0xffc01c5c   /* DMA Channel 13 Y Modify Register */
#define              DMA13_CURR_DESC_PTR  0xffc01c60   /* DMA Channel 13 Current Descriptor Pointer Register */
#define                  DMA13_CURR_ADDR  0xffc01c64   /* DMA Channel 13 Current Address Register */
#define                 DMA13_IRQ_STATUS  0xffc01c68   /* DMA Channel 13 Interrupt/Status Register */
#define             DMA13_PERIPHERAL_MAP  0xffc01c6c   /* DMA Channel 13 Peripheral Map Register */
#define               DMA13_CURR_X_COUNT  0xffc01c70   /* DMA Channel 13 Current X Count Register */
#define               DMA13_CURR_Y_COUNT  0xffc01c78   /* DMA Channel 13 Current Y Count Register */

/* DMA Channel 14 Registers */

#define              DMA14_NEXT_DESC_PTR  0xffc01c80   /* DMA Channel 14 Next Descriptor Pointer Register */
#define                 DMA14_START_ADDR  0xffc01c84   /* DMA Channel 14 Start Address Register */
#define                     DMA14_CONFIG  0xffc01c88   /* DMA Channel 14 Configuration Register */
#define                    DMA14_X_COUNT  0xffc01c90   /* DMA Channel 14 X Count Register */
#define                   DMA14_X_MODIFY  0xffc01c94   /* DMA Channel 14 X Modify Register */
#define                    DMA14_Y_COUNT  0xffc01c98   /* DMA Channel 14 Y Count Register */
#define                   DMA14_Y_MODIFY  0xffc01c9c   /* DMA Channel 14 Y Modify Register */
#define              DMA14_CURR_DESC_PTR  0xffc01ca0   /* DMA Channel 14 Current Descriptor Pointer Register */
#define                  DMA14_CURR_ADDR  0xffc01ca4   /* DMA Channel 14 Current Address Register */
#define                 DMA14_IRQ_STATUS  0xffc01ca8   /* DMA Channel 14 Interrupt/Status Register */
#define             DMA14_PERIPHERAL_MAP  0xffc01cac   /* DMA Channel 14 Peripheral Map Register */
#define               DMA14_CURR_X_COUNT  0xffc01cb0   /* DMA Channel 14 Current X Count Register */
#define               DMA14_CURR_Y_COUNT  0xffc01cb8   /* DMA Channel 14 Current Y Count Register */

/* DMA Channel 15 Registers */

#define              DMA15_NEXT_DESC_PTR  0xffc01cc0   /* DMA Channel 15 Next Descriptor Pointer Register */
#define                 DMA15_START_ADDR  0xffc01cc4   /* DMA Channel 15 Start Address Register */
#define                     DMA15_CONFIG  0xffc01cc8   /* DMA Channel 15 Configuration Register */
#define                    DMA15_X_COUNT  0xffc01cd0   /* DMA Channel 15 X Count Register */
#define                   DMA15_X_MODIFY  0xffc01cd4   /* DMA Channel 15 X Modify Register */
#define                    DMA15_Y_COUNT  0xffc01cd8   /* DMA Channel 15 Y Count Register */
#define                   DMA15_Y_MODIFY  0xffc01cdc   /* DMA Channel 15 Y Modify Register */
#define              DMA15_CURR_DESC_PTR  0xffc01ce0   /* DMA Channel 15 Current Descriptor Pointer Register */
#define                  DMA15_CURR_ADDR  0xffc01ce4   /* DMA Channel 15 Current Address Register */
#define                 DMA15_IRQ_STATUS  0xffc01ce8   /* DMA Channel 15 Interrupt/Status Register */
#define             DMA15_PERIPHERAL_MAP  0xffc01cec   /* DMA Channel 15 Peripheral Map Register */
#define               DMA15_CURR_X_COUNT  0xffc01cf0   /* DMA Channel 15 Current X Count Register */
#define               DMA15_CURR_Y_COUNT  0xffc01cf8   /* DMA Channel 15 Current Y Count Register */

/* DMA Channel 16 Registers */

#define              DMA16_NEXT_DESC_PTR  0xffc01d00   /* DMA Channel 16 Next Descriptor Pointer Register */
#define                 DMA16_START_ADDR  0xffc01d04   /* DMA Channel 16 Start Address Register */
#define                     DMA16_CONFIG  0xffc01d08   /* DMA Channel 16 Configuration Register */
#define                    DMA16_X_COUNT  0xffc01d10   /* DMA Channel 16 X Count Register */
#define                   DMA16_X_MODIFY  0xffc01d14   /* DMA Channel 16 X Modify Register */
#define                    DMA16_Y_COUNT  0xffc01d18   /* DMA Channel 16 Y Count Register */
#define                   DMA16_Y_MODIFY  0xffc01d1c   /* DMA Channel 16 Y Modify Register */
#define              DMA16_CURR_DESC_PTR  0xffc01d20   /* DMA Channel 16 Current Descriptor Pointer Register */
#define                  DMA16_CURR_ADDR  0xffc01d24   /* DMA Channel 16 Current Address Register */
#define                 DMA16_IRQ_STATUS  0xffc01d28   /* DMA Channel 16 Interrupt/Status Register */
#define             DMA16_PERIPHERAL_MAP  0xffc01d2c   /* DMA Channel 16 Peripheral Map Register */
#define               DMA16_CURR_X_COUNT  0xffc01d30   /* DMA Channel 16 Current X Count Register */
#define               DMA16_CURR_Y_COUNT  0xffc01d38   /* DMA Channel 16 Current Y Count Register */

/* DMA Channel 17 Registers */

#define              DMA17_NEXT_DESC_PTR  0xffc01d40   /* DMA Channel 17 Next Descriptor Pointer Register */
#define                 DMA17_START_ADDR  0xffc01d44   /* DMA Channel 17 Start Address Register */
#define                     DMA17_CONFIG  0xffc01d48   /* DMA Channel 17 Configuration Register */
#define                    DMA17_X_COUNT  0xffc01d50   /* DMA Channel 17 X Count Register */
#define                   DMA17_X_MODIFY  0xffc01d54   /* DMA Channel 17 X Modify Register */
#define                    DMA17_Y_COUNT  0xffc01d58   /* DMA Channel 17 Y Count Register */
#define                   DMA17_Y_MODIFY  0xffc01d5c   /* DMA Channel 17 Y Modify Register */
#define              DMA17_CURR_DESC_PTR  0xffc01d60   /* DMA Channel 17 Current Descriptor Pointer Register */
#define                  DMA17_CURR_ADDR  0xffc01d64   /* DMA Channel 17 Current Address Register */
#define                 DMA17_IRQ_STATUS  0xffc01d68   /* DMA Channel 17 Interrupt/Status Register */
#define             DMA17_PERIPHERAL_MAP  0xffc01d6c   /* DMA Channel 17 Peripheral Map Register */
#define               DMA17_CURR_X_COUNT  0xffc01d70   /* DMA Channel 17 Current X Count Register */
#define               DMA17_CURR_Y_COUNT  0xffc01d78   /* DMA Channel 17 Current Y Count Register */

/* DMA Channel 18 Registers */

#define              DMA18_NEXT_DESC_PTR  0xffc01d80   /* DMA Channel 18 Next Descriptor Pointer Register */
#define                 DMA18_START_ADDR  0xffc01d84   /* DMA Channel 18 Start Address Register */
#define                     DMA18_CONFIG  0xffc01d88   /* DMA Channel 18 Configuration Register */
#define                    DMA18_X_COUNT  0xffc01d90   /* DMA Channel 18 X Count Register */
#define                   DMA18_X_MODIFY  0xffc01d94   /* DMA Channel 18 X Modify Register */
#define                    DMA18_Y_COUNT  0xffc01d98   /* DMA Channel 18 Y Count Register */
#define                   DMA18_Y_MODIFY  0xffc01d9c   /* DMA Channel 18 Y Modify Register */
#define              DMA18_CURR_DESC_PTR  0xffc01da0   /* DMA Channel 18 Current Descriptor Pointer Register */
#define                  DMA18_CURR_ADDR  0xffc01da4   /* DMA Channel 18 Current Address Register */
#define                 DMA18_IRQ_STATUS  0xffc01da8   /* DMA Channel 18 Interrupt/Status Register */
#define             DMA18_PERIPHERAL_MAP  0xffc01dac   /* DMA Channel 18 Peripheral Map Register */
#define               DMA18_CURR_X_COUNT  0xffc01db0   /* DMA Channel 18 Current X Count Register */
#define               DMA18_CURR_Y_COUNT  0xffc01db8   /* DMA Channel 18 Current Y Count Register */

/* DMA Channel 19 Registers */

#define              DMA19_NEXT_DESC_PTR  0xffc01dc0   /* DMA Channel 19 Next Descriptor Pointer Register */
#define                 DMA19_START_ADDR  0xffc01dc4   /* DMA Channel 19 Start Address Register */
#define                     DMA19_CONFIG  0xffc01dc8   /* DMA Channel 19 Configuration Register */
#define                    DMA19_X_COUNT  0xffc01dd0   /* DMA Channel 19 X Count Register */
#define                   DMA19_X_MODIFY  0xffc01dd4   /* DMA Channel 19 X Modify Register */
#define                    DMA19_Y_COUNT  0xffc01dd8   /* DMA Channel 19 Y Count Register */
#define                   DMA19_Y_MODIFY  0xffc01ddc   /* DMA Channel 19 Y Modify Register */
#define              DMA19_CURR_DESC_PTR  0xffc01de0   /* DMA Channel 19 Current Descriptor Pointer Register */
#define                  DMA19_CURR_ADDR  0xffc01de4   /* DMA Channel 19 Current Address Register */
#define                 DMA19_IRQ_STATUS  0xffc01de8   /* DMA Channel 19 Interrupt/Status Register */
#define             DMA19_PERIPHERAL_MAP  0xffc01dec   /* DMA Channel 19 Peripheral Map Register */
#define               DMA19_CURR_X_COUNT  0xffc01df0   /* DMA Channel 19 Current X Count Register */
#define               DMA19_CURR_Y_COUNT  0xffc01df8   /* DMA Channel 19 Current Y Count Register */

/* DMA Channel 20 Registers */

#define              DMA20_NEXT_DESC_PTR  0xffc01e00   /* DMA Channel 20 Next Descriptor Pointer Register */
#define                 DMA20_START_ADDR  0xffc01e04   /* DMA Channel 20 Start Address Register */
#define                     DMA20_CONFIG  0xffc01e08   /* DMA Channel 20 Configuration Register */
#define                    DMA20_X_COUNT  0xffc01e10   /* DMA Channel 20 X Count Register */
#define                   DMA20_X_MODIFY  0xffc01e14   /* DMA Channel 20 X Modify Register */
#define                    DMA20_Y_COUNT  0xffc01e18   /* DMA Channel 20 Y Count Register */
#define                   DMA20_Y_MODIFY  0xffc01e1c   /* DMA Channel 20 Y Modify Register */
#define              DMA20_CURR_DESC_PTR  0xffc01e20   /* DMA Channel 20 Current Descriptor Pointer Register */
#define                  DMA20_CURR_ADDR  0xffc01e24   /* DMA Channel 20 Current Address Register */
#define                 DMA20_IRQ_STATUS  0xffc01e28   /* DMA Channel 20 Interrupt/Status Register */
#define             DMA20_PERIPHERAL_MAP  0xffc01e2c   /* DMA Channel 20 Peripheral Map Register */
#define               DMA20_CURR_X_COUNT  0xffc01e30   /* DMA Channel 20 Current X Count Register */
#define               DMA20_CURR_Y_COUNT  0xffc01e38   /* DMA Channel 20 Current Y Count Register */

/* DMA Channel 21 Registers */

#define              DMA21_NEXT_DESC_PTR  0xffc01e40   /* DMA Channel 21 Next Descriptor Pointer Register */
#define                 DMA21_START_ADDR  0xffc01e44   /* DMA Channel 21 Start Address Register */
#define                     DMA21_CONFIG  0xffc01e48   /* DMA Channel 21 Configuration Register */
#define                    DMA21_X_COUNT  0xffc01e50   /* DMA Channel 21 X Count Register */
#define                   DMA21_X_MODIFY  0xffc01e54   /* DMA Channel 21 X Modify Register */
#define                    DMA21_Y_COUNT  0xffc01e58   /* DMA Channel 21 Y Count Register */
#define                   DMA21_Y_MODIFY  0xffc01e5c   /* DMA Channel 21 Y Modify Register */
#define              DMA21_CURR_DESC_PTR  0xffc01e60   /* DMA Channel 21 Current Descriptor Pointer Register */
#define                  DMA21_CURR_ADDR  0xffc01e64   /* DMA Channel 21 Current Address Register */
#define                 DMA21_IRQ_STATUS  0xffc01e68   /* DMA Channel 21 Interrupt/Status Register */
#define             DMA21_PERIPHERAL_MAP  0xffc01e6c   /* DMA Channel 21 Peripheral Map Register */
#define               DMA21_CURR_X_COUNT  0xffc01e70   /* DMA Channel 21 Current X Count Register */
#define               DMA21_CURR_Y_COUNT  0xffc01e78   /* DMA Channel 21 Current Y Count Register */

/* DMA Channel 22 Registers */

#define              DMA22_NEXT_DESC_PTR  0xffc01e80   /* DMA Channel 22 Next Descriptor Pointer Register */
#define                 DMA22_START_ADDR  0xffc01e84   /* DMA Channel 22 Start Address Register */
#define                     DMA22_CONFIG  0xffc01e88   /* DMA Channel 22 Configuration Register */
#define                    DMA22_X_COUNT  0xffc01e90   /* DMA Channel 22 X Count Register */
#define                   DMA22_X_MODIFY  0xffc01e94   /* DMA Channel 22 X Modify Register */
#define                    DMA22_Y_COUNT  0xffc01e98   /* DMA Channel 22 Y Count Register */
#define                   DMA22_Y_MODIFY  0xffc01e9c   /* DMA Channel 22 Y Modify Register */
#define              DMA22_CURR_DESC_PTR  0xffc01ea0   /* DMA Channel 22 Current Descriptor Pointer Register */
#define                  DMA22_CURR_ADDR  0xffc01ea4   /* DMA Channel 22 Current Address Register */
#define                 DMA22_IRQ_STATUS  0xffc01ea8   /* DMA Channel 22 Interrupt/Status Register */
#define             DMA22_PERIPHERAL_MAP  0xffc01eac   /* DMA Channel 22 Peripheral Map Register */
#define               DMA22_CURR_X_COUNT  0xffc01eb0   /* DMA Channel 22 Current X Count Register */
#define               DMA22_CURR_Y_COUNT  0xffc01eb8   /* DMA Channel 22 Current Y Count Register */

/* DMA Channel 23 Registers */

#define              DMA23_NEXT_DESC_PTR  0xffc01ec0   /* DMA Channel 23 Next Descriptor Pointer Register */
#define                 DMA23_START_ADDR  0xffc01ec4   /* DMA Channel 23 Start Address Register */
#define                     DMA23_CONFIG  0xffc01ec8   /* DMA Channel 23 Configuration Register */
#define                    DMA23_X_COUNT  0xffc01ed0   /* DMA Channel 23 X Count Register */
#define                   DMA23_X_MODIFY  0xffc01ed4   /* DMA Channel 23 X Modify Register */
#define                    DMA23_Y_COUNT  0xffc01ed8   /* DMA Channel 23 Y Count Register */
#define                   DMA23_Y_MODIFY  0xffc01edc   /* DMA Channel 23 Y Modify Register */
#define              DMA23_CURR_DESC_PTR  0xffc01ee0   /* DMA Channel 23 Current Descriptor Pointer Register */
#define                  DMA23_CURR_ADDR  0xffc01ee4   /* DMA Channel 23 Current Address Register */
#define                 DMA23_IRQ_STATUS  0xffc01ee8   /* DMA Channel 23 Interrupt/Status Register */
#define             DMA23_PERIPHERAL_MAP  0xffc01eec   /* DMA Channel 23 Peripheral Map Register */
#define               DMA23_CURR_X_COUNT  0xffc01ef0   /* DMA Channel 23 Current X Count Register */
#define               DMA23_CURR_Y_COUNT  0xffc01ef8   /* DMA Channel 23 Current Y Count Register */

/* MDMA Stream 2 Registers */

#define            MDMA_D2_NEXT_DESC_PTR  0xffc01f00   /* Memory DMA Stream 2 Destination Next Descriptor Pointer Register */
#define               MDMA_D2_START_ADDR  0xffc01f04   /* Memory DMA Stream 2 Destination Start Address Register */
#define                   MDMA_D2_CONFIG  0xffc01f08   /* Memory DMA Stream 2 Destination Configuration Register */
#define                  MDMA_D2_X_COUNT  0xffc01f10   /* Memory DMA Stream 2 Destination X Count Register */
#define                 MDMA_D2_X_MODIFY  0xffc01f14   /* Memory DMA Stream 2 Destination X Modify Register */
#define                  MDMA_D2_Y_COUNT  0xffc01f18   /* Memory DMA Stream 2 Destination Y Count Register */
#define                 MDMA_D2_Y_MODIFY  0xffc01f1c   /* Memory DMA Stream 2 Destination Y Modify Register */
#define            MDMA_D2_CURR_DESC_PTR  0xffc01f20   /* Memory DMA Stream 2 Destination Current Descriptor Pointer Register */
#define                MDMA_D2_CURR_ADDR  0xffc01f24   /* Memory DMA Stream 2 Destination Current Address Register */
#define               MDMA_D2_IRQ_STATUS  0xffc01f28   /* Memory DMA Stream 2 Destination Interrupt/Status Register */
#define           MDMA_D2_PERIPHERAL_MAP  0xffc01f2c   /* Memory DMA Stream 2 Destination Peripheral Map Register */
#define             MDMA_D2_CURR_X_COUNT  0xffc01f30   /* Memory DMA Stream 2 Destination Current X Count Register */
#define             MDMA_D2_CURR_Y_COUNT  0xffc01f38   /* Memory DMA Stream 2 Destination Current Y Count Register */
#define            MDMA_S2_NEXT_DESC_PTR  0xffc01f40   /* Memory DMA Stream 2 Source Next Descriptor Pointer Register */
#define               MDMA_S2_START_ADDR  0xffc01f44   /* Memory DMA Stream 2 Source Start Address Register */
#define                   MDMA_S2_CONFIG  0xffc01f48   /* Memory DMA Stream 2 Source Configuration Register */
#define                  MDMA_S2_X_COUNT  0xffc01f50   /* Memory DMA Stream 2 Source X Count Register */
#define                 MDMA_S2_X_MODIFY  0xffc01f54   /* Memory DMA Stream 2 Source X Modify Register */
#define                  MDMA_S2_Y_COUNT  0xffc01f58   /* Memory DMA Stream 2 Source Y Count Register */
#define                 MDMA_S2_Y_MODIFY  0xffc01f5c   /* Memory DMA Stream 2 Source Y Modify Register */
#define            MDMA_S2_CURR_DESC_PTR  0xffc01f60   /* Memory DMA Stream 2 Source Current Descriptor Pointer Register */
#define                MDMA_S2_CURR_ADDR  0xffc01f64   /* Memory DMA Stream 2 Source Current Address Register */
#define               MDMA_S2_IRQ_STATUS  0xffc01f68   /* Memory DMA Stream 2 Source Interrupt/Status Register */
#define           MDMA_S2_PERIPHERAL_MAP  0xffc01f6c   /* Memory DMA Stream 2 Source Peripheral Map Register */
#define             MDMA_S2_CURR_X_COUNT  0xffc01f70   /* Memory DMA Stream 2 Source Current X Count Register */
#define             MDMA_S2_CURR_Y_COUNT  0xffc01f78   /* Memory DMA Stream 2 Source Current Y Count Register */

/* MDMA Stream 3 Registers */

#define            MDMA_D3_NEXT_DESC_PTR  0xffc01f80   /* Memory DMA Stream 3 Destination Next Descriptor Pointer Register */
#define               MDMA_D3_START_ADDR  0xffc01f84   /* Memory DMA Stream 3 Destination Start Address Register */
#define                   MDMA_D3_CONFIG  0xffc01f88   /* Memory DMA Stream 3 Destination Configuration Register */
#define                  MDMA_D3_X_COUNT  0xffc01f90   /* Memory DMA Stream 3 Destination X Count Register */
#define                 MDMA_D3_X_MODIFY  0xffc01f94   /* Memory DMA Stream 3 Destination X Modify Register */
#define                  MDMA_D3_Y_COUNT  0xffc01f98   /* Memory DMA Stream 3 Destination Y Count Register */
#define                 MDMA_D3_Y_MODIFY  0xffc01f9c   /* Memory DMA Stream 3 Destination Y Modify Register */
#define            MDMA_D3_CURR_DESC_PTR  0xffc01fa0   /* Memory DMA Stream 3 Destination Current Descriptor Pointer Register */
#define                MDMA_D3_CURR_ADDR  0xffc01fa4   /* Memory DMA Stream 3 Destination Current Address Register */
#define               MDMA_D3_IRQ_STATUS  0xffc01fa8   /* Memory DMA Stream 3 Destination Interrupt/Status Register */
#define           MDMA_D3_PERIPHERAL_MAP  0xffc01fac   /* Memory DMA Stream 3 Destination Peripheral Map Register */
#define             MDMA_D3_CURR_X_COUNT  0xffc01fb0   /* Memory DMA Stream 3 Destination Current X Count Register */
#define             MDMA_D3_CURR_Y_COUNT  0xffc01fb8   /* Memory DMA Stream 3 Destination Current Y Count Register */
#define            MDMA_S3_NEXT_DESC_PTR  0xffc01fc0   /* Memory DMA Stream 3 Source Next Descriptor Pointer Register */
#define               MDMA_S3_START_ADDR  0xffc01fc4   /* Memory DMA Stream 3 Source Start Address Register */
#define                   MDMA_S3_CONFIG  0xffc01fc8   /* Memory DMA Stream 3 Source Configuration Register */
#define                  MDMA_S3_X_COUNT  0xffc01fd0   /* Memory DMA Stream 3 Source X Count Register */
#define                 MDMA_S3_X_MODIFY  0xffc01fd4   /* Memory DMA Stream 3 Source X Modify Register */
#define                  MDMA_S3_Y_COUNT  0xffc01fd8   /* Memory DMA Stream 3 Source Y Count Register */
#define                 MDMA_S3_Y_MODIFY  0xffc01fdc   /* Memory DMA Stream 3 Source Y Modify Register */
#define            MDMA_S3_CURR_DESC_PTR  0xffc01fe0   /* Memory DMA Stream 3 Source Current Descriptor Pointer Register */
#define                MDMA_S3_CURR_ADDR  0xffc01fe4   /* Memory DMA Stream 3 Source Current Address Register */
#define               MDMA_S3_IRQ_STATUS  0xffc01fe8   /* Memory DMA Stream 3 Source Interrupt/Status Register */
#define           MDMA_S3_PERIPHERAL_MAP  0xffc01fec   /* Memory DMA Stream 3 Source Peripheral Map Register */
#define             MDMA_S3_CURR_X_COUNT  0xffc01ff0   /* Memory DMA Stream 3 Source Current X Count Register */
#define             MDMA_S3_CURR_Y_COUNT  0xffc01ff8   /* Memory DMA Stream 3 Source Current Y Count Register */

/* UART1 Registers */

#define                        UART1_DLL  0xffc02000   /* Divisor Latch Low Byte */
#define                        UART1_DLH  0xffc02004   /* Divisor Latch High Byte */
#define                       UART1_GCTL  0xffc02008   /* Global Control Register */
#define                        UART1_LCR  0xffc0200c   /* Line Control Register */
#define                        UART1_MCR  0xffc02010   /* Modem Control Register */
#define                        UART1_LSR  0xffc02014   /* Line Status Register */
#define                        UART1_MSR  0xffc02018   /* Modem Status Register */
#define                        UART1_SCR  0xffc0201c   /* Scratch Register */
#define                    UART1_IER_SET  0xffc02020   /* Interrupt Enable Register Set */
#define                  UART1_IER_CLEAR  0xffc02024   /* Interrupt Enable Register Clear */
#define                        UART1_THR  0xffc02028   /* Transmit Hold Register */
#define                        UART1_RBR  0xffc0202c   /* Receive Buffer Register */

/* UART2 is not defined in the shared file because it is not available on the ADSP-BF542 and ADSP-BF544 processors */

/* SPI1 Registers */

#define                     SPI1_REGBASE  0xffc02300
#define                         SPI1_CTL  0xffc02300   /* SPI1 Control Register */
#define                         SPI1_FLG  0xffc02304   /* SPI1 Flag Register */
#define                        SPI1_STAT  0xffc02308   /* SPI1 Status Register */
#define                        SPI1_TDBR  0xffc0230c   /* SPI1 Transmit Data Buffer Register */
#define                        SPI1_RDBR  0xffc02310   /* SPI1 Receive Data Buffer Register */
#define                        SPI1_BAUD  0xffc02314   /* SPI1 Baud Rate Register */
#define                      SPI1_SHADOW  0xffc02318   /* SPI1 Receive Data Buffer Shadow Register */

/* SPORT2 Registers */

#define                      SPORT2_TCR1  0xffc02500   /* SPORT2 Transmit Configuration 1 Register */
#define                      SPORT2_TCR2  0xffc02504   /* SPORT2 Transmit Configuration 2 Register */
#define                   SPORT2_TCLKDIV  0xffc02508   /* SPORT2 Transmit Serial Clock Divider Register */
#define                    SPORT2_TFSDIV  0xffc0250c   /* SPORT2 Transmit Frame Sync Divider Register */
#define                        SPORT2_TX  0xffc02510   /* SPORT2 Transmit Data Register */
#define                        SPORT2_RX  0xffc02518   /* SPORT2 Receive Data Register */
#define                      SPORT2_RCR1  0xffc02520   /* SPORT2 Receive Configuration 1 Register */
#define                      SPORT2_RCR2  0xffc02524   /* SPORT2 Receive Configuration 2 Register */
#define                   SPORT2_RCLKDIV  0xffc02528   /* SPORT2 Receive Serial Clock Divider Register */
#define                    SPORT2_RFSDIV  0xffc0252c   /* SPORT2 Receive Frame Sync Divider Register */
#define                      SPORT2_STAT  0xffc02530   /* SPORT2 Status Register */
#define                      SPORT2_CHNL  0xffc02534   /* SPORT2 Current Channel Register */
#define                     SPORT2_MCMC1  0xffc02538   /* SPORT2 Multi channel Configuration Register 1 */
#define                     SPORT2_MCMC2  0xffc0253c   /* SPORT2 Multi channel Configuration Register 2 */
#define                     SPORT2_MTCS0  0xffc02540   /* SPORT2 Multi channel Transmit Select Register 0 */
#define                     SPORT2_MTCS1  0xffc02544   /* SPORT2 Multi channel Transmit Select Register 1 */
#define                     SPORT2_MTCS2  0xffc02548   /* SPORT2 Multi channel Transmit Select Register 2 */
#define                     SPORT2_MTCS3  0xffc0254c   /* SPORT2 Multi channel Transmit Select Register 3 */
#define                     SPORT2_MRCS0  0xffc02550   /* SPORT2 Multi channel Receive Select Register 0 */
#define                     SPORT2_MRCS1  0xffc02554   /* SPORT2 Multi channel Receive Select Register 1 */
#define                     SPORT2_MRCS2  0xffc02558   /* SPORT2 Multi channel Receive Select Register 2 */
#define                     SPORT2_MRCS3  0xffc0255c   /* SPORT2 Multi channel Receive Select Register 3 */

/* SPORT3 Registers */

#define                      SPORT3_TCR1  0xffc02600   /* SPORT3 Transmit Configuration 1 Register */
#define                      SPORT3_TCR2  0xffc02604   /* SPORT3 Transmit Configuration 2 Register */
#define                   SPORT3_TCLKDIV  0xffc02608   /* SPORT3 Transmit Serial Clock Divider Register */
#define                    SPORT3_TFSDIV  0xffc0260c   /* SPORT3 Transmit Frame Sync Divider Register */
#define                        SPORT3_TX  0xffc02610   /* SPORT3 Transmit Data Register */
#define                        SPORT3_RX  0xffc02618   /* SPORT3 Receive Data Register */
#define                      SPORT3_RCR1  0xffc02620   /* SPORT3 Receive Configuration 1 Register */
#define                      SPORT3_RCR2  0xffc02624   /* SPORT3 Receive Configuration 2 Register */
#define                   SPORT3_RCLKDIV  0xffc02628   /* SPORT3 Receive Serial Clock Divider Register */
#define                    SPORT3_RFSDIV  0xffc0262c   /* SPORT3 Receive Frame Sync Divider Register */
#define                      SPORT3_STAT  0xffc02630   /* SPORT3 Status Register */
#define                      SPORT3_CHNL  0xffc02634   /* SPORT3 Current Channel Register */
#define                     SPORT3_MCMC1  0xffc02638   /* SPORT3 Multi channel Configuration Register 1 */
#define                     SPORT3_MCMC2  0xffc0263c   /* SPORT3 Multi channel Configuration Register 2 */
#define                     SPORT3_MTCS0  0xffc02640   /* SPORT3 Multi channel Transmit Select Register 0 */
#define                     SPORT3_MTCS1  0xffc02644   /* SPORT3 Multi channel Transmit Select Register 1 */
#define                     SPORT3_MTCS2  0xffc02648   /* SPORT3 Multi channel Transmit Select Register 2 */
#define                     SPORT3_MTCS3  0xffc0264c   /* SPORT3 Multi channel Transmit Select Register 3 */
#define                     SPORT3_MRCS0  0xffc02650   /* SPORT3 Multi channel Receive Select Register 0 */
#define                     SPORT3_MRCS1  0xffc02654   /* SPORT3 Multi channel Receive Select Register 1 */
#define                     SPORT3_MRCS2  0xffc02658   /* SPORT3 Multi channel Receive Select Register 2 */
#define                     SPORT3_MRCS3  0xffc0265c   /* SPORT3 Multi channel Receive Select Register 3 */

/* EPPI2 Registers */

#define                     EPPI2_STATUS  0xffc02900   /* EPPI2 Status Register */
#define                     EPPI2_HCOUNT  0xffc02904   /* EPPI2 Horizontal Transfer Count Register */
#define                     EPPI2_HDELAY  0xffc02908   /* EPPI2 Horizontal Delay Count Register */
#define                     EPPI2_VCOUNT  0xffc0290c   /* EPPI2 Vertical Transfer Count Register */
#define                     EPPI2_VDELAY  0xffc02910   /* EPPI2 Vertical Delay Count Register */
#define                      EPPI2_FRAME  0xffc02914   /* EPPI2 Lines per Frame Register */
#define                       EPPI2_LINE  0xffc02918   /* EPPI2 Samples per Line Register */
#define                     EPPI2_CLKDIV  0xffc0291c   /* EPPI2 Clock Divide Register */
#define                    EPPI2_CONTROL  0xffc02920   /* EPPI2 Control Register */
#define                   EPPI2_FS1W_HBL  0xffc02924   /* EPPI2 FS1 Width Register / EPPI2 Horizontal Blanking Samples Per Line Register */
#define                  EPPI2_FS1P_AVPL  0xffc02928   /* EPPI2 FS1 Period Register / EPPI2 Active Video Samples Per Line Register */
#define                   EPPI2_FS2W_LVB  0xffc0292c   /* EPPI2 FS2 Width Register / EPPI2 Lines of Vertical Blanking Register */
#define                  EPPI2_FS2P_LAVF  0xffc02930   /* EPPI2 FS2 Period Register/ EPPI2 Lines of Active Video Per Field Register */
#define                       EPPI2_CLIP  0xffc02934   /* EPPI2 Clipping Register */

/* CAN Controller 0 Config 1 Registers */

#define                         CAN0_MC1  0xffc02a00   /* CAN Controller 0 Mailbox Configuration Register 1 */
#define                         CAN0_MD1  0xffc02a04   /* CAN Controller 0 Mailbox Direction Register 1 */
#define                        CAN0_TRS1  0xffc02a08   /* CAN Controller 0 Transmit Request Set Register 1 */
#define                        CAN0_TRR1  0xffc02a0c   /* CAN Controller 0 Transmit Request Reset Register 1 */
#define                         CAN0_TA1  0xffc02a10   /* CAN Controller 0 Transmit Acknowledge Register 1 */
#define                         CAN0_AA1  0xffc02a14   /* CAN Controller 0 Abort Acknowledge Register 1 */
#define                        CAN0_RMP1  0xffc02a18   /* CAN Controller 0 Receive Message Pending Register 1 */
#define                        CAN0_RML1  0xffc02a1c   /* CAN Controller 0 Receive Message Lost Register 1 */
#define                      CAN0_MBTIF1  0xffc02a20   /* CAN Controller 0 Mailbox Transmit Interrupt Flag Register 1 */
#define                      CAN0_MBRIF1  0xffc02a24   /* CAN Controller 0 Mailbox Receive Interrupt Flag Register 1 */
#define                       CAN0_MBIM1  0xffc02a28   /* CAN Controller 0 Mailbox Interrupt Mask Register 1 */
#define                        CAN0_RFH1  0xffc02a2c   /* CAN Controller 0 Remote Frame Handling Enable Register 1 */
#define                       CAN0_OPSS1  0xffc02a30   /* CAN Controller 0 Overwrite Protection Single Shot Transmit Register 1 */

/* CAN Controller 0 Config 2 Registers */

#define                         CAN0_MC2  0xffc02a40   /* CAN Controller 0 Mailbox Configuration Register 2 */
#define                         CAN0_MD2  0xffc02a44   /* CAN Controller 0 Mailbox Direction Register 2 */
#define                        CAN0_TRS2  0xffc02a48   /* CAN Controller 0 Transmit Request Set Register 2 */
#define                        CAN0_TRR2  0xffc02a4c   /* CAN Controller 0 Transmit Request Reset Register 2 */
#define                         CAN0_TA2  0xffc02a50   /* CAN Controller 0 Transmit Acknowledge Register 2 */
#define                         CAN0_AA2  0xffc02a54   /* CAN Controller 0 Abort Acknowledge Register 2 */
#define                        CAN0_RMP2  0xffc02a58   /* CAN Controller 0 Receive Message Pending Register 2 */
#define                        CAN0_RML2  0xffc02a5c   /* CAN Controller 0 Receive Message Lost Register 2 */
#define                      CAN0_MBTIF2  0xffc02a60   /* CAN Controller 0 Mailbox Transmit Interrupt Flag Register 2 */
#define                      CAN0_MBRIF2  0xffc02a64   /* CAN Controller 0 Mailbox Receive Interrupt Flag Register 2 */
#define                       CAN0_MBIM2  0xffc02a68   /* CAN Controller 0 Mailbox Interrupt Mask Register 2 */
#define                        CAN0_RFH2  0xffc02a6c   /* CAN Controller 0 Remote Frame Handling Enable Register 2 */
#define                       CAN0_OPSS2  0xffc02a70   /* CAN Controller 0 Overwrite Protection Single Shot Transmit Register 2 */

/* CAN Controller 0 Clock/Interrupt/Counter Registers */

#define                       CAN0_CLOCK  0xffc02a80   /* CAN Controller 0 Clock Register */
#define                      CAN0_TIMING  0xffc02a84   /* CAN Controller 0 Timing Register */
#define                       CAN0_DEBUG  0xffc02a88   /* CAN Controller 0 Debug Register */
#define                      CAN0_STATUS  0xffc02a8c   /* CAN Controller 0 Global Status Register */
#define                         CAN0_CEC  0xffc02a90   /* CAN Controller 0 Error Counter Register */
#define                         CAN0_GIS  0xffc02a94   /* CAN Controller 0 Global Interrupt Status Register */
#define                         CAN0_GIM  0xffc02a98   /* CAN Controller 0 Global Interrupt Mask Register */
#define                         CAN0_GIF  0xffc02a9c   /* CAN Controller 0 Global Interrupt Flag Register */
#define                     CAN0_CONTROL  0xffc02aa0   /* CAN Controller 0 Master Control Register */
#define                        CAN0_INTR  0xffc02aa4   /* CAN Controller 0 Interrupt Pending Register */
#define                        CAN0_MBTD  0xffc02aac   /* CAN Controller 0 Mailbox Temporary Disable Register */
#define                         CAN0_EWR  0xffc02ab0   /* CAN Controller 0 Programmable Warning Level Register */
#define                         CAN0_ESR  0xffc02ab4   /* CAN Controller 0 Error Status Register */
#define                       CAN0_UCCNT  0xffc02ac4   /* CAN Controller 0 Universal Counter Register */
#define                        CAN0_UCRC  0xffc02ac8   /* CAN Controller 0 Universal Counter Force Reload Register */
#define                       CAN0_UCCNF  0xffc02acc   /* CAN Controller 0 Universal Counter Configuration Register */

/* CAN Controller 0 Acceptance Registers */

#define                       CAN0_AM00L  0xffc02b00   /* CAN Controller 0 Mailbox 0 Acceptance Mask High Register */
#define                       CAN0_AM00H  0xffc02b04   /* CAN Controller 0 Mailbox 0 Acceptance Mask Low Register */
#define                       CAN0_AM01L  0xffc02b08   /* CAN Controller 0 Mailbox 1 Acceptance Mask High Register */
#define                       CAN0_AM01H  0xffc02b0c   /* CAN Controller 0 Mailbox 1 Acceptance Mask Low Register */
#define                       CAN0_AM02L  0xffc02b10   /* CAN Controller 0 Mailbox 2 Acceptance Mask High Register */
#define                       CAN0_AM02H  0xffc02b14   /* CAN Controller 0 Mailbox 2 Acceptance Mask Low Register */
#define                       CAN0_AM03L  0xffc02b18   /* CAN Controller 0 Mailbox 3 Acceptance Mask High Register */
#define                       CAN0_AM03H  0xffc02b1c   /* CAN Controller 0 Mailbox 3 Acceptance Mask Low Register */
#define                       CAN0_AM04L  0xffc02b20   /* CAN Controller 0 Mailbox 4 Acceptance Mask High Register */
#define                       CAN0_AM04H  0xffc02b24   /* CAN Controller 0 Mailbox 4 Acceptance Mask Low Register */
#define                       CAN0_AM05L  0xffc02b28   /* CAN Controller 0 Mailbox 5 Acceptance Mask High Register */
#define                       CAN0_AM05H  0xffc02b2c   /* CAN Controller 0 Mailbox 5 Acceptance Mask Low Register */
#define                       CAN0_AM06L  0xffc02b30   /* CAN Controller 0 Mailbox 6 Acceptance Mask High Register */
#define                       CAN0_AM06H  0xffc02b34   /* CAN Controller 0 Mailbox 6 Acceptance Mask Low Register */
#define                       CAN0_AM07L  0xffc02b38   /* CAN Controller 0 Mailbox 7 Acceptance Mask High Register */
#define                       CAN0_AM07H  0xffc02b3c   /* CAN Controller 0 Mailbox 7 Acceptance Mask Low Register */
#define                       CAN0_AM08L  0xffc02b40   /* CAN Controller 0 Mailbox 8 Acceptance Mask High Register */
#define                       CAN0_AM08H  0xffc02b44   /* CAN Controller 0 Mailbox 8 Acceptance Mask Low Register */
#define                       CAN0_AM09L  0xffc02b48   /* CAN Controller 0 Mailbox 9 Acceptance Mask High Register */
#define                       CAN0_AM09H  0xffc02b4c   /* CAN Controller 0 Mailbox 9 Acceptance Mask Low Register */
#define                       CAN0_AM10L  0xffc02b50   /* CAN Controller 0 Mailbox 10 Acceptance Mask High Register */
#define                       CAN0_AM10H  0xffc02b54   /* CAN Controller 0 Mailbox 10 Acceptance Mask Low Register */
#define                       CAN0_AM11L  0xffc02b58   /* CAN Controller 0 Mailbox 11 Acceptance Mask High Register */
#define                       CAN0_AM11H  0xffc02b5c   /* CAN Controller 0 Mailbox 11 Acceptance Mask Low Register */
#define                       CAN0_AM12L  0xffc02b60   /* CAN Controller 0 Mailbox 12 Acceptance Mask High Register */
#define                       CAN0_AM12H  0xffc02b64   /* CAN Controller 0 Mailbox 12 Acceptance Mask Low Register */
#define                       CAN0_AM13L  0xffc02b68   /* CAN Controller 0 Mailbox 13 Acceptance Mask High Register */
#define                       CAN0_AM13H  0xffc02b6c   /* CAN Controller 0 Mailbox 13 Acceptance Mask Low Register */
#define                       CAN0_AM14L  0xffc02b70   /* CAN Controller 0 Mailbox 14 Acceptance Mask High Register */
#define                       CAN0_AM14H  0xffc02b74   /* CAN Controller 0 Mailbox 14 Acceptance Mask Low Register */
#define                       CAN0_AM15L  0xffc02b78   /* CAN Controller 0 Mailbox 15 Acceptance Mask High Register */
#define                       CAN0_AM15H  0xffc02b7c   /* CAN Controller 0 Mailbox 15 Acceptance Mask Low Register */

/* CAN Controller 0 Acceptance Registers */

#define                       CAN0_AM16L  0xffc02b80   /* CAN Controller 0 Mailbox 16 Acceptance Mask High Register */
#define                       CAN0_AM16H  0xffc02b84   /* CAN Controller 0 Mailbox 16 Acceptance Mask Low Register */
#define                       CAN0_AM17L  0xffc02b88   /* CAN Controller 0 Mailbox 17 Acceptance Mask High Register */
#define                       CAN0_AM17H  0xffc02b8c   /* CAN Controller 0 Mailbox 17 Acceptance Mask Low Register */
#define                       CAN0_AM18L  0xffc02b90   /* CAN Controller 0 Mailbox 18 Acceptance Mask High Register */
#define                       CAN0_AM18H  0xffc02b94   /* CAN Controller 0 Mailbox 18 Acceptance Mask Low Register */
#define                       CAN0_AM19L  0xffc02b98   /* CAN Controller 0 Mailbox 19 Acceptance Mask High Register */
#define                       CAN0_AM19H  0xffc02b9c   /* CAN Controller 0 Mailbox 19 Acceptance Mask Low Register */
#define                       CAN0_AM20L  0xffc02ba0   /* CAN Controller 0 Mailbox 20 Acceptance Mask High Register */
#define                       CAN0_AM20H  0xffc02ba4   /* CAN Controller 0 Mailbox 20 Acceptance Mask Low Register */
#define                       CAN0_AM21L  0xffc02ba8   /* CAN Controller 0 Mailbox 21 Acceptance Mask High Register */
#define                       CAN0_AM21H  0xffc02bac   /* CAN Controller 0 Mailbox 21 Acceptance Mask Low Register */
#define                       CAN0_AM22L  0xffc02bb0   /* CAN Controller 0 Mailbox 22 Acceptance Mask High Register */
#define                       CAN0_AM22H  0xffc02bb4   /* CAN Controller 0 Mailbox 22 Acceptance Mask Low Register */
#define                       CAN0_AM23L  0xffc02bb8   /* CAN Controller 0 Mailbox 23 Acceptance Mask High Register */
#define                       CAN0_AM23H  0xffc02bbc   /* CAN Controller 0 Mailbox 23 Acceptance Mask Low Register */
#define                       CAN0_AM24L  0xffc02bc0   /* CAN Controller 0 Mailbox 24 Acceptance Mask High Register */
#define                       CAN0_AM24H  0xffc02bc4   /* CAN Controller 0 Mailbox 24 Acceptance Mask Low Register */
#define                       CAN0_AM25L  0xffc02bc8   /* CAN Controller 0 Mailbox 25 Acceptance Mask High Register */
#define                       CAN0_AM25H  0xffc02bcc   /* CAN Controller 0 Mailbox 25 Acceptance Mask Low Register */
#define                       CAN0_AM26L  0xffc02bd0   /* CAN Controller 0 Mailbox 26 Acceptance Mask High Register */
#define                       CAN0_AM26H  0xffc02bd4   /* CAN Controller 0 Mailbox 26 Acceptance Mask Low Register */
#define                       CAN0_AM27L  0xffc02bd8   /* CAN Controller 0 Mailbox 27 Acceptance Mask High Register */
#define                       CAN0_AM27H  0xffc02bdc   /* CAN Controller 0 Mailbox 27 Acceptance Mask Low Register */
#define                       CAN0_AM28L  0xffc02be0   /* CAN Controller 0 Mailbox 28 Acceptance Mask High Register */
#define                       CAN0_AM28H  0xffc02be4   /* CAN Controller 0 Mailbox 28 Acceptance Mask Low Register */
#define                       CAN0_AM29L  0xffc02be8   /* CAN Controller 0 Mailbox 29 Acceptance Mask High Register */
#define                       CAN0_AM29H  0xffc02bec   /* CAN Controller 0 Mailbox 29 Acceptance Mask Low Register */
#define                       CAN0_AM30L  0xffc02bf0   /* CAN Controller 0 Mailbox 30 Acceptance Mask High Register */
#define                       CAN0_AM30H  0xffc02bf4   /* CAN Controller 0 Mailbox 30 Acceptance Mask Low Register */
#define                       CAN0_AM31L  0xffc02bf8   /* CAN Controller 0 Mailbox 31 Acceptance Mask High Register */
#define                       CAN0_AM31H  0xffc02bfc   /* CAN Controller 0 Mailbox 31 Acceptance Mask Low Register */

/* CAN Controller 0 Mailbox Data Registers */

#define                  CAN0_MB00_DATA0  0xffc02c00   /* CAN Controller 0 Mailbox 0 Data 0 Register */
#define                  CAN0_MB00_DATA1  0xffc02c04   /* CAN Controller 0 Mailbox 0 Data 1 Register */
#define                  CAN0_MB00_DATA2  0xffc02c08   /* CAN Controller 0 Mailbox 0 Data 2 Register */
#define                  CAN0_MB00_DATA3  0xffc02c0c   /* CAN Controller 0 Mailbox 0 Data 3 Register */
#define                 CAN0_MB00_LENGTH  0xffc02c10   /* CAN Controller 0 Mailbox 0 Length Register */
#define              CAN0_MB00_TIMESTAMP  0xffc02c14   /* CAN Controller 0 Mailbox 0 Timestamp Register */
#define                    CAN0_MB00_ID0  0xffc02c18   /* CAN Controller 0 Mailbox 0 ID0 Register */
#define                    CAN0_MB00_ID1  0xffc02c1c   /* CAN Controller 0 Mailbox 0 ID1 Register */
#define                  CAN0_MB01_DATA0  0xffc02c20   /* CAN Controller 0 Mailbox 1 Data 0 Register */
#define                  CAN0_MB01_DATA1  0xffc02c24   /* CAN Controller 0 Mailbox 1 Data 1 Register */
#define                  CAN0_MB01_DATA2  0xffc02c28   /* CAN Controller 0 Mailbox 1 Data 2 Register */
#define                  CAN0_MB01_DATA3  0xffc02c2c   /* CAN Controller 0 Mailbox 1 Data 3 Register */
#define                 CAN0_MB01_LENGTH  0xffc02c30   /* CAN Controller 0 Mailbox 1 Length Register */
#define              CAN0_MB01_TIMESTAMP  0xffc02c34   /* CAN Controller 0 Mailbox 1 Timestamp Register */
#define                    CAN0_MB01_ID0  0xffc02c38   /* CAN Controller 0 Mailbox 1 ID0 Register */
#define                    CAN0_MB01_ID1  0xffc02c3c   /* CAN Controller 0 Mailbox 1 ID1 Register */
#define                  CAN0_MB02_DATA0  0xffc02c40   /* CAN Controller 0 Mailbox 2 Data 0 Register */
#define                  CAN0_MB02_DATA1  0xffc02c44   /* CAN Controller 0 Mailbox 2 Data 1 Register */
#define                  CAN0_MB02_DATA2  0xffc02c48   /* CAN Controller 0 Mailbox 2 Data 2 Register */
#define                  CAN0_MB02_DATA3  0xffc02c4c   /* CAN Controller 0 Mailbox 2 Data 3 Register */
#define                 CAN0_MB02_LENGTH  0xffc02c50   /* CAN Controller 0 Mailbox 2 Length Register */
#define              CAN0_MB02_TIMESTAMP  0xffc02c54   /* CAN Controller 0 Mailbox 2 Timestamp Register */
#define                    CAN0_MB02_ID0  0xffc02c58   /* CAN Controller 0 Mailbox 2 ID0 Register */
#define                    CAN0_MB02_ID1  0xffc02c5c   /* CAN Controller 0 Mailbox 2 ID1 Register */
#define                  CAN0_MB03_DATA0  0xffc02c60   /* CAN Controller 0 Mailbox 3 Data 0 Register */
#define                  CAN0_MB03_DATA1  0xffc02c64   /* CAN Controller 0 Mailbox 3 Data 1 Register */
#define                  CAN0_MB03_DATA2  0xffc02c68   /* CAN Controller 0 Mailbox 3 Data 2 Register */
#define                  CAN0_MB03_DATA3  0xffc02c6c   /* CAN Controller 0 Mailbox 3 Data 3 Register */
#define                 CAN0_MB03_LENGTH  0xffc02c70   /* CAN Controller 0 Mailbox 3 Length Register */
#define              CAN0_MB03_TIMESTAMP  0xffc02c74   /* CAN Controller 0 Mailbox 3 Timestamp Register */
#define                    CAN0_MB03_ID0  0xffc02c78   /* CAN Controller 0 Mailbox 3 ID0 Register */
#define                    CAN0_MB03_ID1  0xffc02c7c   /* CAN Controller 0 Mailbox 3 ID1 Register */
#define                  CAN0_MB04_DATA0  0xffc02c80   /* CAN Controller 0 Mailbox 4 Data 0 Register */
#define                  CAN0_MB04_DATA1  0xffc02c84   /* CAN Controller 0 Mailbox 4 Data 1 Register */
#define                  CAN0_MB04_DATA2  0xffc02c88   /* CAN Controller 0 Mailbox 4 Data 2 Register */
#define                  CAN0_MB04_DATA3  0xffc02c8c   /* CAN Controller 0 Mailbox 4 Data 3 Register */
#define                 CAN0_MB04_LENGTH  0xffc02c90   /* CAN Controller 0 Mailbox 4 Length Register */
#define              CAN0_MB04_TIMESTAMP  0xffc02c94   /* CAN Controller 0 Mailbox 4 Timestamp Register */
#define                    CAN0_MB04_ID0  0xffc02c98   /* CAN Controller 0 Mailbox 4 ID0 Register */
#define                    CAN0_MB04_ID1  0xffc02c9c   /* CAN Controller 0 Mailbox 4 ID1 Register */
#define                  CAN0_MB05_DATA0  0xffc02ca0   /* CAN Controller 0 Mailbox 5 Data 0 Register */
#define                  CAN0_MB05_DATA1  0xffc02ca4   /* CAN Controller 0 Mailbox 5 Data 1 Register */
#define                  CAN0_MB05_DATA2  0xffc02ca8   /* CAN Controller 0 Mailbox 5 Data 2 Register */
#define                  CAN0_MB05_DATA3  0xffc02cac   /* CAN Controller 0 Mailbox 5 Data 3 Register */
#define                 CAN0_MB05_LENGTH  0xffc02cb0   /* CAN Controller 0 Mailbox 5 Length Register */
#define              CAN0_MB05_TIMESTAMP  0xffc02cb4   /* CAN Controller 0 Mailbox 5 Timestamp Register */
#define                    CAN0_MB05_ID0  0xffc02cb8   /* CAN Controller 0 Mailbox 5 ID0 Register */
#define                    CAN0_MB05_ID1  0xffc02cbc   /* CAN Controller 0 Mailbox 5 ID1 Register */
#define                  CAN0_MB06_DATA0  0xffc02cc0   /* CAN Controller 0 Mailbox 6 Data 0 Register */
#define                  CAN0_MB06_DATA1  0xffc02cc4   /* CAN Controller 0 Mailbox 6 Data 1 Register */
#define                  CAN0_MB06_DATA2  0xffc02cc8   /* CAN Controller 0 Mailbox 6 Data 2 Register */
#define                  CAN0_MB06_DATA3  0xffc02ccc   /* CAN Controller 0 Mailbox 6 Data 3 Register */
#define                 CAN0_MB06_LENGTH  0xffc02cd0   /* CAN Controller 0 Mailbox 6 Length Register */
#define              CAN0_MB06_TIMESTAMP  0xffc02cd4   /* CAN Controller 0 Mailbox 6 Timestamp Register */
#define                    CAN0_MB06_ID0  0xffc02cd8   /* CAN Controller 0 Mailbox 6 ID0 Register */
#define                    CAN0_MB06_ID1  0xffc02cdc   /* CAN Controller 0 Mailbox 6 ID1 Register */
#define                  CAN0_MB07_DATA0  0xffc02ce0   /* CAN Controller 0 Mailbox 7 Data 0 Register */
#define                  CAN0_MB07_DATA1  0xffc02ce4   /* CAN Controller 0 Mailbox 7 Data 1 Register */
#define                  CAN0_MB07_DATA2  0xffc02ce8   /* CAN Controller 0 Mailbox 7 Data 2 Register */
#define                  CAN0_MB07_DATA3  0xffc02cec   /* CAN Controller 0 Mailbox 7 Data 3 Register */
#define                 CAN0_MB07_LENGTH  0xffc02cf0   /* CAN Controller 0 Mailbox 7 Length Register */
#define              CAN0_MB07_TIMESTAMP  0xffc02cf4   /* CAN Controller 0 Mailbox 7 Timestamp Register */
#define                    CAN0_MB07_ID0  0xffc02cf8   /* CAN Controller 0 Mailbox 7 ID0 Register */
#define                    CAN0_MB07_ID1  0xffc02cfc   /* CAN Controller 0 Mailbox 7 ID1 Register */
#define                  CAN0_MB08_DATA0  0xffc02d00   /* CAN Controller 0 Mailbox 8 Data 0 Register */
#define                  CAN0_MB08_DATA1  0xffc02d04   /* CAN Controller 0 Mailbox 8 Data 1 Register */
#define                  CAN0_MB08_DATA2  0xffc02d08   /* CAN Controller 0 Mailbox 8 Data 2 Register */
#define                  CAN0_MB08_DATA3  0xffc02d0c   /* CAN Controller 0 Mailbox 8 Data 3 Register */
#define                 CAN0_MB08_LENGTH  0xffc02d10   /* CAN Controller 0 Mailbox 8 Length Register */
#define              CAN0_MB08_TIMESTAMP  0xffc02d14   /* CAN Controller 0 Mailbox 8 Timestamp Register */
#define                    CAN0_MB08_ID0  0xffc02d18   /* CAN Controller 0 Mailbox 8 ID0 Register */
#define                    CAN0_MB08_ID1  0xffc02d1c   /* CAN Controller 0 Mailbox 8 ID1 Register */
#define                  CAN0_MB09_DATA0  0xffc02d20   /* CAN Controller 0 Mailbox 9 Data 0 Register */
#define                  CAN0_MB09_DATA1  0xffc02d24   /* CAN Controller 0 Mailbox 9 Data 1 Register */
#define                  CAN0_MB09_DATA2  0xffc02d28   /* CAN Controller 0 Mailbox 9 Data 2 Register */
#define                  CAN0_MB09_DATA3  0xffc02d2c   /* CAN Controller 0 Mailbox 9 Data 3 Register */
#define                 CAN0_MB09_LENGTH  0xffc02d30   /* CAN Controller 0 Mailbox 9 Length Register */
#define              CAN0_MB09_TIMESTAMP  0xffc02d34   /* CAN Controller 0 Mailbox 9 Timestamp Register */
#define                    CAN0_MB09_ID0  0xffc02d38   /* CAN Controller 0 Mailbox 9 ID0 Register */
#define                    CAN0_MB09_ID1  0xffc02d3c   /* CAN Controller 0 Mailbox 9 ID1 Register */
#define                  CAN0_MB10_DATA0  0xffc02d40   /* CAN Controller 0 Mailbox 10 Data 0 Register */
#define                  CAN0_MB10_DATA1  0xffc02d44   /* CAN Controller 0 Mailbox 10 Data 1 Register */
#define                  CAN0_MB10_DATA2  0xffc02d48   /* CAN Controller 0 Mailbox 10 Data 2 Register */
#define                  CAN0_MB10_DATA3  0xffc02d4c   /* CAN Controller 0 Mailbox 10 Data 3 Register */
#define                 CAN0_MB10_LENGTH  0xffc02d50   /* CAN Controller 0 Mailbox 10 Length Register */
#define              CAN0_MB10_TIMESTAMP  0xffc02d54   /* CAN Controller 0 Mailbox 10 Timestamp Register */
#define                    CAN0_MB10_ID0  0xffc02d58   /* CAN Controller 0 Mailbox 10 ID0 Register */
#define                    CAN0_MB10_ID1  0xffc02d5c   /* CAN Controller 0 Mailbox 10 ID1 Register */
#define                  CAN0_MB11_DATA0  0xffc02d60   /* CAN Controller 0 Mailbox 11 Data 0 Register */
#define                  CAN0_MB11_DATA1  0xffc02d64   /* CAN Controller 0 Mailbox 11 Data 1 Register */
#define                  CAN0_MB11_DATA2  0xffc02d68   /* CAN Controller 0 Mailbox 11 Data 2 Register */
#define                  CAN0_MB11_DATA3  0xffc02d6c   /* CAN Controller 0 Mailbox 11 Data 3 Register */
#define                 CAN0_MB11_LENGTH  0xffc02d70   /* CAN Controller 0 Mailbox 11 Length Register */
#define              CAN0_MB11_TIMESTAMP  0xffc02d74   /* CAN Controller 0 Mailbox 11 Timestamp Register */
#define                    CAN0_MB11_ID0  0xffc02d78   /* CAN Controller 0 Mailbox 11 ID0 Register */
#define                    CAN0_MB11_ID1  0xffc02d7c   /* CAN Controller 0 Mailbox 11 ID1 Register */
#define                  CAN0_MB12_DATA0  0xffc02d80   /* CAN Controller 0 Mailbox 12 Data 0 Register */
#define                  CAN0_MB12_DATA1  0xffc02d84   /* CAN Controller 0 Mailbox 12 Data 1 Register */
#define                  CAN0_MB12_DATA2  0xffc02d88   /* CAN Controller 0 Mailbox 12 Data 2 Register */
#define                  CAN0_MB12_DATA3  0xffc02d8c   /* CAN Controller 0 Mailbox 12 Data 3 Register */
#define                 CAN0_MB12_LENGTH  0xffc02d90   /* CAN Controller 0 Mailbox 12 Length Register */
#define              CAN0_MB12_TIMESTAMP  0xffc02d94   /* CAN Controller 0 Mailbox 12 Timestamp Register */
#define                    CAN0_MB12_ID0  0xffc02d98   /* CAN Controller 0 Mailbox 12 ID0 Register */
#define                    CAN0_MB12_ID1  0xffc02d9c   /* CAN Controller 0 Mailbox 12 ID1 Register */
#define                  CAN0_MB13_DATA0  0xffc02da0   /* CAN Controller 0 Mailbox 13 Data 0 Register */
#define                  CAN0_MB13_DATA1  0xffc02da4   /* CAN Controller 0 Mailbox 13 Data 1 Register */
#define                  CAN0_MB13_DATA2  0xffc02da8   /* CAN Controller 0 Mailbox 13 Data 2 Register */
#define                  CAN0_MB13_DATA3  0xffc02dac   /* CAN Controller 0 Mailbox 13 Data 3 Register */
#define                 CAN0_MB13_LENGTH  0xffc02db0   /* CAN Controller 0 Mailbox 13 Length Register */
#define              CAN0_MB13_TIMESTAMP  0xffc02db4   /* CAN Controller 0 Mailbox 13 Timestamp Register */
#define                    CAN0_MB13_ID0  0xffc02db8   /* CAN Controller 0 Mailbox 13 ID0 Register */
#define                    CAN0_MB13_ID1  0xffc02dbc   /* CAN Controller 0 Mailbox 13 ID1 Register */
#define                  CAN0_MB14_DATA0  0xffc02dc0   /* CAN Controller 0 Mailbox 14 Data 0 Register */
#define                  CAN0_MB14_DATA1  0xffc02dc4   /* CAN Controller 0 Mailbox 14 Data 1 Register */
#define                  CAN0_MB14_DATA2  0xffc02dc8   /* CAN Controller 0 Mailbox 14 Data 2 Register */
#define                  CAN0_MB14_DATA3  0xffc02dcc   /* CAN Controller 0 Mailbox 14 Data 3 Register */
#define                 CAN0_MB14_LENGTH  0xffc02dd0   /* CAN Controller 0 Mailbox 14 Length Register */
#define              CAN0_MB14_TIMESTAMP  0xffc02dd4   /* CAN Controller 0 Mailbox 14 Timestamp Register */
#define                    CAN0_MB14_ID0  0xffc02dd8   /* CAN Controller 0 Mailbox 14 ID0 Register */
#define                    CAN0_MB14_ID1  0xffc02ddc   /* CAN Controller 0 Mailbox 14 ID1 Register */
#define                  CAN0_MB15_DATA0  0xffc02de0   /* CAN Controller 0 Mailbox 15 Data 0 Register */
#define                  CAN0_MB15_DATA1  0xffc02de4   /* CAN Controller 0 Mailbox 15 Data 1 Register */
#define                  CAN0_MB15_DATA2  0xffc02de8   /* CAN Controller 0 Mailbox 15 Data 2 Register */
#define                  CAN0_MB15_DATA3  0xffc02dec   /* CAN Controller 0 Mailbox 15 Data 3 Register */
#define                 CAN0_MB15_LENGTH  0xffc02df0   /* CAN Controller 0 Mailbox 15 Length Register */
#define              CAN0_MB15_TIMESTAMP  0xffc02df4   /* CAN Controller 0 Mailbox 15 Timestamp Register */
#define                    CAN0_MB15_ID0  0xffc02df8   /* CAN Controller 0 Mailbox 15 ID0 Register */
#define                    CAN0_MB15_ID1  0xffc02dfc   /* CAN Controller 0 Mailbox 15 ID1 Register */

/* CAN Controller 0 Mailbox Data Registers */

#define                  CAN0_MB16_DATA0  0xffc02e00   /* CAN Controller 0 Mailbox 16 Data 0 Register */
#define                  CAN0_MB16_DATA1  0xffc02e04   /* CAN Controller 0 Mailbox 16 Data 1 Register */
#define                  CAN0_MB16_DATA2  0xffc02e08   /* CAN Controller 0 Mailbox 16 Data 2 Register */
#define                  CAN0_MB16_DATA3  0xffc02e0c   /* CAN Controller 0 Mailbox 16 Data 3 Register */
#define                 CAN0_MB16_LENGTH  0xffc02e10   /* CAN Controller 0 Mailbox 16 Length Register */
#define              CAN0_MB16_TIMESTAMP  0xffc02e14   /* CAN Controller 0 Mailbox 16 Timestamp Register */
#define                    CAN0_MB16_ID0  0xffc02e18   /* CAN Controller 0 Mailbox 16 ID0 Register */
#define                    CAN0_MB16_ID1  0xffc02e1c   /* CAN Controller 0 Mailbox 16 ID1 Register */
#define                  CAN0_MB17_DATA0  0xffc02e20   /* CAN Controller 0 Mailbox 17 Data 0 Register */
#define                  CAN0_MB17_DATA1  0xffc02e24   /* CAN Controller 0 Mailbox 17 Data 1 Register */
#define                  CAN0_MB17_DATA2  0xffc02e28   /* CAN Controller 0 Mailbox 17 Data 2 Register */
#define                  CAN0_MB17_DATA3  0xffc02e2c   /* CAN Controller 0 Mailbox 17 Data 3 Register */
#define                 CAN0_MB17_LENGTH  0xffc02e30   /* CAN Controller 0 Mailbox 17 Length Register */
#define              CAN0_MB17_TIMESTAMP  0xffc02e34   /* CAN Controller 0 Mailbox 17 Timestamp Register */
#define                    CAN0_MB17_ID0  0xffc02e38   /* CAN Controller 0 Mailbox 17 ID0 Register */
#define                    CAN0_MB17_ID1  0xffc02e3c   /* CAN Controller 0 Mailbox 17 ID1 Register */
#define                  CAN0_MB18_DATA0  0xffc02e40   /* CAN Controller 0 Mailbox 18 Data 0 Register */
#define                  CAN0_MB18_DATA1  0xffc02e44   /* CAN Controller 0 Mailbox 18 Data 1 Register */
#define                  CAN0_MB18_DATA2  0xffc02e48   /* CAN Controller 0 Mailbox 18 Data 2 Register */
#define                  CAN0_MB18_DATA3  0xffc02e4c   /* CAN Controller 0 Mailbox 18 Data 3 Register */
#define                 CAN0_MB18_LENGTH  0xffc02e50   /* CAN Controller 0 Mailbox 18 Length Register */
#define              CAN0_MB18_TIMESTAMP  0xffc02e54   /* CAN Controller 0 Mailbox 18 Timestamp Register */
#define                    CAN0_MB18_ID0  0xffc02e58   /* CAN Controller 0 Mailbox 18 ID0 Register */
#define                    CAN0_MB18_ID1  0xffc02e5c   /* CAN Controller 0 Mailbox 18 ID1 Register */
#define                  CAN0_MB19_DATA0  0xffc02e60   /* CAN Controller 0 Mailbox 19 Data 0 Register */
#define                  CAN0_MB19_DATA1  0xffc02e64   /* CAN Controller 0 Mailbox 19 Data 1 Register */
#define                  CAN0_MB19_DATA2  0xffc02e68   /* CAN Controller 0 Mailbox 19 Data 2 Register */
#define                  CAN0_MB19_DATA3  0xffc02e6c   /* CAN Controller 0 Mailbox 19 Data 3 Register */
#define                 CAN0_MB19_LENGTH  0xffc02e70   /* CAN Controller 0 Mailbox 19 Length Register */
#define              CAN0_MB19_TIMESTAMP  0xffc02e74   /* CAN Controller 0 Mailbox 19 Timestamp Register */
#define                    CAN0_MB19_ID0  0xffc02e78   /* CAN Controller 0 Mailbox 19 ID0 Register */
#define                    CAN0_MB19_ID1  0xffc02e7c   /* CAN Controller 0 Mailbox 19 ID1 Register */
#define                  CAN0_MB20_DATA0  0xffc02e80   /* CAN Controller 0 Mailbox 20 Data 0 Register */
#define                  CAN0_MB20_DATA1  0xffc02e84   /* CAN Controller 0 Mailbox 20 Data 1 Register */
#define                  CAN0_MB20_DATA2  0xffc02e88   /* CAN Controller 0 Mailbox 20 Data 2 Register */
#define                  CAN0_MB20_DATA3  0xffc02e8c   /* CAN Controller 0 Mailbox 20 Data 3 Register */
#define                 CAN0_MB20_LENGTH  0xffc02e90   /* CAN Controller 0 Mailbox 20 Length Register */
#define              CAN0_MB20_TIMESTAMP  0xffc02e94   /* CAN Controller 0 Mailbox 20 Timestamp Register */
#define                    CAN0_MB20_ID0  0xffc02e98   /* CAN Controller 0 Mailbox 20 ID0 Register */
#define                    CAN0_MB20_ID1  0xffc02e9c   /* CAN Controller 0 Mailbox 20 ID1 Register */
#define                  CAN0_MB21_DATA0  0xffc02ea0   /* CAN Controller 0 Mailbox 21 Data 0 Register */
#define                  CAN0_MB21_DATA1  0xffc02ea4   /* CAN Controller 0 Mailbox 21 Data 1 Register */
#define                  CAN0_MB21_DATA2  0xffc02ea8   /* CAN Controller 0 Mailbox 21 Data 2 Register */
#define                  CAN0_MB21_DATA3  0xffc02eac   /* CAN Controller 0 Mailbox 21 Data 3 Register */
#define                 CAN0_MB21_LENGTH  0xffc02eb0   /* CAN Controller 0 Mailbox 21 Length Register */
#define              CAN0_MB21_TIMESTAMP  0xffc02eb4   /* CAN Controller 0 Mailbox 21 Timestamp Register */
#define                    CAN0_MB21_ID0  0xffc02eb8   /* CAN Controller 0 Mailbox 21 ID0 Register */
#define                    CAN0_MB21_ID1  0xffc02ebc   /* CAN Controller 0 Mailbox 21 ID1 Register */
#define                  CAN0_MB22_DATA0  0xffc02ec0   /* CAN Controller 0 Mailbox 22 Data 0 Register */
#define                  CAN0_MB22_DATA1  0xffc02ec4   /* CAN Controller 0 Mailbox 22 Data 1 Register */
#define                  CAN0_MB22_DATA2  0xffc02ec8   /* CAN Controller 0 Mailbox 22 Data 2 Register */
#define                  CAN0_MB22_DATA3  0xffc02ecc   /* CAN Controller 0 Mailbox 22 Data 3 Register */
#define                 CAN0_MB22_LENGTH  0xffc02ed0   /* CAN Controller 0 Mailbox 22 Length Register */
#define              CAN0_MB22_TIMESTAMP  0xffc02ed4   /* CAN Controller 0 Mailbox 22 Timestamp Register */
#define                    CAN0_MB22_ID0  0xffc02ed8   /* CAN Controller 0 Mailbox 22 ID0 Register */
#define                    CAN0_MB22_ID1  0xffc02edc   /* CAN Controller 0 Mailbox 22 ID1 Register */
#define                  CAN0_MB23_DATA0  0xffc02ee0   /* CAN Controller 0 Mailbox 23 Data 0 Register */
#define                  CAN0_MB23_DATA1  0xffc02ee4   /* CAN Controller 0 Mailbox 23 Data 1 Register */
#define                  CAN0_MB23_DATA2  0xffc02ee8   /* CAN Controller 0 Mailbox 23 Data 2 Register */
#define                  CAN0_MB23_DATA3  0xffc02eec   /* CAN Controller 0 Mailbox 23 Data 3 Register */
#define                 CAN0_MB23_LENGTH  0xffc02ef0   /* CAN Controller 0 Mailbox 23 Length Register */
#define              CAN0_MB23_TIMESTAMP  0xffc02ef4   /* CAN Controller 0 Mailbox 23 Timestamp Register */
#define                    CAN0_MB23_ID0  0xffc02ef8   /* CAN Controller 0 Mailbox 23 ID0 Register */
#define                    CAN0_MB23_ID1  0xffc02efc   /* CAN Controller 0 Mailbox 23 ID1 Register */
#define                  CAN0_MB24_DATA0  0xffc02f00   /* CAN Controller 0 Mailbox 24 Data 0 Register */
#define                  CAN0_MB24_DATA1  0xffc02f04   /* CAN Controller 0 Mailbox 24 Data 1 Register */
#define                  CAN0_MB24_DATA2  0xffc02f08   /* CAN Controller 0 Mailbox 24 Data 2 Register */
#define                  CAN0_MB24_DATA3  0xffc02f0c   /* CAN Controller 0 Mailbox 24 Data 3 Register */
#define                 CAN0_MB24_LENGTH  0xffc02f10   /* CAN Controller 0 Mailbox 24 Length Register */
#define              CAN0_MB24_TIMESTAMP  0xffc02f14   /* CAN Controller 0 Mailbox 24 Timestamp Register */
#define                    CAN0_MB24_ID0  0xffc02f18   /* CAN Controller 0 Mailbox 24 ID0 Register */
#define                    CAN0_MB24_ID1  0xffc02f1c   /* CAN Controller 0 Mailbox 24 ID1 Register */
#define                  CAN0_MB25_DATA0  0xffc02f20   /* CAN Controller 0 Mailbox 25 Data 0 Register */
#define                  CAN0_MB25_DATA1  0xffc02f24   /* CAN Controller 0 Mailbox 25 Data 1 Register */
#define                  CAN0_MB25_DATA2  0xffc02f28   /* CAN Controller 0 Mailbox 25 Data 2 Register */
#define                  CAN0_MB25_DATA3  0xffc02f2c   /* CAN Controller 0 Mailbox 25 Data 3 Register */
#define                 CAN0_MB25_LENGTH  0xffc02f30   /* CAN Controller 0 Mailbox 25 Length Register */
#define              CAN0_MB25_TIMESTAMP  0xffc02f34   /* CAN Controller 0 Mailbox 25 Timestamp Register */
#define                    CAN0_MB25_ID0  0xffc02f38   /* CAN Controller 0 Mailbox 25 ID0 Register */
#define                    CAN0_MB25_ID1  0xffc02f3c   /* CAN Controller 0 Mailbox 25 ID1 Register */
#define                  CAN0_MB26_DATA0  0xffc02f40   /* CAN Controller 0 Mailbox 26 Data 0 Register */
#define                  CAN0_MB26_DATA1  0xffc02f44   /* CAN Controller 0 Mailbox 26 Data 1 Register */
#define                  CAN0_MB26_DATA2  0xffc02f48   /* CAN Controller 0 Mailbox 26 Data 2 Register */
#define                  CAN0_MB26_DATA3  0xffc02f4c   /* CAN Controller 0 Mailbox 26 Data 3 Register */
#define                 CAN0_MB26_LENGTH  0xffc02f50   /* CAN Controller 0 Mailbox 26 Length Register */
#define              CAN0_MB26_TIMESTAMP  0xffc02f54   /* CAN Controller 0 Mailbox 26 Timestamp Register */
#define                    CAN0_MB26_ID0  0xffc02f58   /* CAN Controller 0 Mailbox 26 ID0 Register */
#define                    CAN0_MB26_ID1  0xffc02f5c   /* CAN Controller 0 Mailbox 26 ID1 Register */
#define                  CAN0_MB27_DATA0  0xffc02f60   /* CAN Controller 0 Mailbox 27 Data 0 Register */
#define                  CAN0_MB27_DATA1  0xffc02f64   /* CAN Controller 0 Mailbox 27 Data 1 Register */
#define                  CAN0_MB27_DATA2  0xffc02f68   /* CAN Controller 0 Mailbox 27 Data 2 Register */
#define                  CAN0_MB27_DATA3  0xffc02f6c   /* CAN Controller 0 Mailbox 27 Data 3 Register */
#define                 CAN0_MB27_LENGTH  0xffc02f70   /* CAN Controller 0 Mailbox 27 Length Register */
#define              CAN0_MB27_TIMESTAMP  0xffc02f74   /* CAN Controller 0 Mailbox 27 Timestamp Register */
#define                    CAN0_MB27_ID0  0xffc02f78   /* CAN Controller 0 Mailbox 27 ID0 Register */
#define                    CAN0_MB27_ID1  0xffc02f7c   /* CAN Controller 0 Mailbox 27 ID1 Register */
#define                  CAN0_MB28_DATA0  0xffc02f80   /* CAN Controller 0 Mailbox 28 Data 0 Register */
#define                  CAN0_MB28_DATA1  0xffc02f84   /* CAN Controller 0 Mailbox 28 Data 1 Register */
#define                  CAN0_MB28_DATA2  0xffc02f88   /* CAN Controller 0 Mailbox 28 Data 2 Register */
#define                  CAN0_MB28_DATA3  0xffc02f8c   /* CAN Controller 0 Mailbox 28 Data 3 Register */
#define                 CAN0_MB28_LENGTH  0xffc02f90   /* CAN Controller 0 Mailbox 28 Length Register */
#define              CAN0_MB28_TIMESTAMP  0xffc02f94   /* CAN Controller 0 Mailbox 28 Timestamp Register */
#define                    CAN0_MB28_ID0  0xffc02f98   /* CAN Controller 0 Mailbox 28 ID0 Register */
#define                    CAN0_MB28_ID1  0xffc02f9c   /* CAN Controller 0 Mailbox 28 ID1 Register */
#define                  CAN0_MB29_DATA0  0xffc02fa0   /* CAN Controller 0 Mailbox 29 Data 0 Register */
#define                  CAN0_MB29_DATA1  0xffc02fa4   /* CAN Controller 0 Mailbox 29 Data 1 Register */
#define                  CAN0_MB29_DATA2  0xffc02fa8   /* CAN Controller 0 Mailbox 29 Data 2 Register */
#define                  CAN0_MB29_DATA3  0xffc02fac   /* CAN Controller 0 Mailbox 29 Data 3 Register */
#define                 CAN0_MB29_LENGTH  0xffc02fb0   /* CAN Controller 0 Mailbox 29 Length Register */
#define              CAN0_MB29_TIMESTAMP  0xffc02fb4   /* CAN Controller 0 Mailbox 29 Timestamp Register */
#define                    CAN0_MB29_ID0  0xffc02fb8   /* CAN Controller 0 Mailbox 29 ID0 Register */
#define                    CAN0_MB29_ID1  0xffc02fbc   /* CAN Controller 0 Mailbox 29 ID1 Register */
#define                  CAN0_MB30_DATA0  0xffc02fc0   /* CAN Controller 0 Mailbox 30 Data 0 Register */
#define                  CAN0_MB30_DATA1  0xffc02fc4   /* CAN Controller 0 Mailbox 30 Data 1 Register */
#define                  CAN0_MB30_DATA2  0xffc02fc8   /* CAN Controller 0 Mailbox 30 Data 2 Register */
#define                  CAN0_MB30_DATA3  0xffc02fcc   /* CAN Controller 0 Mailbox 30 Data 3 Register */
#define                 CAN0_MB30_LENGTH  0xffc02fd0   /* CAN Controller 0 Mailbox 30 Length Register */
#define              CAN0_MB30_TIMESTAMP  0xffc02fd4   /* CAN Controller 0 Mailbox 30 Timestamp Register */
#define                    CAN0_MB30_ID0  0xffc02fd8   /* CAN Controller 0 Mailbox 30 ID0 Register */
#define                    CAN0_MB30_ID1  0xffc02fdc   /* CAN Controller 0 Mailbox 30 ID1 Register */
#define                  CAN0_MB31_DATA0  0xffc02fe0   /* CAN Controller 0 Mailbox 31 Data 0 Register */
#define                  CAN0_MB31_DATA1  0xffc02fe4   /* CAN Controller 0 Mailbox 31 Data 1 Register */
#define                  CAN0_MB31_DATA2  0xffc02fe8   /* CAN Controller 0 Mailbox 31 Data 2 Register */
#define                  CAN0_MB31_DATA3  0xffc02fec   /* CAN Controller 0 Mailbox 31 Data 3 Register */
#define                 CAN0_MB31_LENGTH  0xffc02ff0   /* CAN Controller 0 Mailbox 31 Length Register */
#define              CAN0_MB31_TIMESTAMP  0xffc02ff4   /* CAN Controller 0 Mailbox 31 Timestamp Register */
#define                    CAN0_MB31_ID0  0xffc02ff8   /* CAN Controller 0 Mailbox 31 ID0 Register */
#define                    CAN0_MB31_ID1  0xffc02ffc   /* CAN Controller 0 Mailbox 31 ID1 Register */

/* UART3 Registers */

#define                        UART3_DLL  0xffc03100   /* Divisor Latch Low Byte */
#define                        UART3_DLH  0xffc03104   /* Divisor Latch High Byte */
#define                       UART3_GCTL  0xffc03108   /* Global Control Register */
#define                        UART3_LCR  0xffc0310c   /* Line Control Register */
#define                        UART3_MCR  0xffc03110   /* Modem Control Register */
#define                        UART3_LSR  0xffc03114   /* Line Status Register */
#define                        UART3_MSR  0xffc03118   /* Modem Status Register */
#define                        UART3_SCR  0xffc0311c   /* Scratch Register */
#define                    UART3_IER_SET  0xffc03120   /* Interrupt Enable Register Set */
#define                  UART3_IER_CLEAR  0xffc03124   /* Interrupt Enable Register Clear */
#define                        UART3_THR  0xffc03128   /* Transmit Hold Register */
#define                        UART3_RBR  0xffc0312c   /* Receive Buffer Register */

/* NFC Registers */

#define                          NFC_CTL  0xffc03b00   /* NAND Control Register */
#define                         NFC_STAT  0xffc03b04   /* NAND Status Register */
#define                      NFC_IRQSTAT  0xffc03b08   /* NAND Interrupt Status Register */
#define                      NFC_IRQMASK  0xffc03b0c   /* NAND Interrupt Mask Register */
#define                         NFC_ECC0  0xffc03b10   /* NAND ECC Register 0 */
#define                         NFC_ECC1  0xffc03b14   /* NAND ECC Register 1 */
#define                         NFC_ECC2  0xffc03b18   /* NAND ECC Register 2 */
#define                         NFC_ECC3  0xffc03b1c   /* NAND ECC Register 3 */
#define                        NFC_COUNT  0xffc03b20   /* NAND ECC Count Register */
#define                          NFC_RST  0xffc03b24   /* NAND ECC Reset Register */
#define                        NFC_PGCTL  0xffc03b28   /* NAND Page Control Register */
#define                         NFC_READ  0xffc03b2c   /* NAND Read Data Register */
#define                         NFC_ADDR  0xffc03b40   /* NAND Address Register */
#define                          NFC_CMD  0xffc03b44   /* NAND Command Register */
#define                      NFC_DATA_WR  0xffc03b48   /* NAND Data Write Register */
#define                      NFC_DATA_RD  0xffc03b4c   /* NAND Data Read Register */

/* Counter Registers */

#define                       CNT_CONFIG  0xffc04200   /* Configuration Register */
#define                        CNT_IMASK  0xffc04204   /* Interrupt Mask Register */
#define                       CNT_STATUS  0xffc04208   /* Status Register */
#define                      CNT_COMMAND  0xffc0420c   /* Command Register */
#define                     CNT_DEBOUNCE  0xffc04210   /* Debounce Register */
#define                      CNT_COUNTER  0xffc04214   /* Counter Register */
#define                          CNT_MAX  0xffc04218   /* Maximal Count Register */
#define                          CNT_MIN  0xffc0421c   /* Minimal Count Register */

/* OTP/FUSE Registers */

#define                      OTP_CONTROL  0xffc04300   /* OTP/Fuse Control Register */
#define                          OTP_BEN  0xffc04304   /* OTP/Fuse Byte Enable */
#define                       OTP_STATUS  0xffc04308   /* OTP/Fuse Status */
#define                       OTP_TIMING  0xffc0430c   /* OTP/Fuse Access Timing */

/* Security Registers */

#define                    SECURE_SYSSWT  0xffc04320   /* Secure System Switches */
#define                   SECURE_CONTROL  0xffc04324   /* Secure Control */
#define                    SECURE_STATUS  0xffc04328   /* Secure Status */

/* DMA Peripheral Mux Register */

#define                    DMAC1_PERIMUX  0xffc04340   /* DMA Controller 1 Peripheral Multiplexer Register */

/* OTP Read/Write Data Buffer Registers */

#define                        OTP_DATA0  0xffc04380   /* OTP/Fuse Data (OTP_DATA0-3) accesses the fuse read write buffer */
#define                        OTP_DATA1  0xffc04384   /* OTP/Fuse Data (OTP_DATA0-3) accesses the fuse read write buffer */
#define                        OTP_DATA2  0xffc04388   /* OTP/Fuse Data (OTP_DATA0-3) accesses the fuse read write buffer */
#define                        OTP_DATA3  0xffc0438c   /* OTP/Fuse Data (OTP_DATA0-3) accesses the fuse read write buffer */

/* Handshake MDMA is not defined in the shared file because it is not available on the ADSP-BF542 processor */

/* ********************************************************** */
/*     SINGLE BIT MACRO PAIRS (bit mask and negated one)      */
/*     and MULTI BIT READ MACROS                              */
/* ********************************************************** */

/* SIC_IMASK Masks */
#define SIC_UNMASK_ALL         0x00000000	/* Unmask all peripheral interrupts */
#define SIC_MASK_ALL           0xFFFFFFFF	/* Mask all peripheral interrupts */
#define SIC_MASK(x)	       (1 << (x))	/* Mask Peripheral #x interrupt */
#define SIC_UNMASK(x) (0xFFFFFFFF ^ (1 << (x)))	/* Unmask Peripheral #x interrupt */

/* SIC_IWR Masks */
#define IWR_DISABLE_ALL        0x00000000	/* Wakeup Disable all peripherals */
#define IWR_ENABLE_ALL         0xFFFFFFFF	/* Wakeup Enable all peripherals */
#define IWR_ENABLE(x)	       (1 << (x))	/* Wakeup Enable Peripheral #x */
#define IWR_DISABLE(x) (0xFFFFFFFF ^ (1 << (x)))	/* Wakeup Disable Peripheral #x */

/* Bit masks for SIC_IAR0 */

#define            PLL_WAKEUP  0x1        /* PLL Wakeup */

/* Bit masks for SIC_IWR0, SIC_IMASK0, SIC_ISR0 */

#define              DMA0_ERR  0x2        /* DMA Controller 0 Error */
#define             EPPI0_ERR  0x4        /* EPPI0 Error */
#define            SPORT0_ERR  0x8        /* SPORT0 Error */
#define            SPORT1_ERR  0x10       /* SPORT1 Error */
#define              SPI0_ERR  0x20       /* SPI0 Error */
#define             UART0_ERR  0x40       /* UART0 Error */
#define                   RTC  0x80       /* Real-Time Clock */
#define                 DMA12  0x100      /* DMA Channel 12 */
#define                  DMA0  0x200      /* DMA Channel 0 */
#define                  DMA1  0x400      /* DMA Channel 1 */
#define                  DMA2  0x800      /* DMA Channel 2 */
#define                  DMA3  0x1000     /* DMA Channel 3 */
#define                  DMA4  0x2000     /* DMA Channel 4 */
#define                  DMA6  0x4000     /* DMA Channel 6 */
#define                  DMA7  0x8000     /* DMA Channel 7 */
#define                 PINT0  0x80000    /* Pin Interrupt 0 */
#define                 PINT1  0x100000   /* Pin Interrupt 1 */
#define                 MDMA0  0x200000   /* Memory DMA Stream 0 */
#define                 MDMA1  0x400000   /* Memory DMA Stream 1 */
#define                  WDOG  0x800000   /* Watchdog Timer */
#define              DMA1_ERR  0x1000000  /* DMA Controller 1 Error */
#define            SPORT2_ERR  0x2000000  /* SPORT2 Error */
#define            SPORT3_ERR  0x4000000  /* SPORT3 Error */
#define               MXVR_SD  0x8000000  /* MXVR Synchronous Data */
#define              SPI1_ERR  0x10000000 /* SPI1 Error */
#define              SPI2_ERR  0x20000000 /* SPI2 Error */
#define             UART1_ERR  0x40000000 /* UART1 Error */
#define             UART2_ERR  0x80000000 /* UART2 Error */

/* Bit masks for SIC_IWR1, SIC_IMASK1, SIC_ISR1 */

#define              CAN0_ERR  0x1        /* CAN0 Error */
#define                 DMA18  0x2        /* DMA Channel 18 */
#define                 DMA19  0x4        /* DMA Channel 19 */
#define                 DMA20  0x8        /* DMA Channel 20 */
#define                 DMA21  0x10       /* DMA Channel 21 */
#define                 DMA13  0x20       /* DMA Channel 13 */
#define                 DMA14  0x40       /* DMA Channel 14 */
#define                  DMA5  0x80       /* DMA Channel 5 */
#define                 DMA23  0x100      /* DMA Channel 23 */
#define                  DMA8  0x200      /* DMA Channel 8 */
#define                  DMA9  0x400      /* DMA Channel 9 */
#define                 DMA10  0x800      /* DMA Channel 10 */
#define                 DMA11  0x1000     /* DMA Channel 11 */
#define                  TWI0  0x2000     /* TWI0 */
#define                  TWI1  0x4000     /* TWI1 */
#define               CAN0_RX  0x8000     /* CAN0 Receive */
#define               CAN0_TX  0x10000    /* CAN0 Transmit */
#define                 MDMA2  0x20000    /* Memory DMA Stream 0 */
#define                 MDMA3  0x40000    /* Memory DMA Stream 1 */
#define             MXVR_STAT  0x80000    /* MXVR Status */
#define               MXVR_CM  0x100000   /* MXVR Control Message */
#define               MXVR_AP  0x200000   /* MXVR Asynchronous Packet */
#define             EPPI1_ERR  0x400000   /* EPPI1 Error */
#define             EPPI2_ERR  0x800000   /* EPPI2 Error */
#define             UART3_ERR  0x1000000  /* UART3 Error */
#define              HOST_ERR  0x2000000  /* Host DMA Port Error */
#define               USB_ERR  0x4000000  /* USB Error */
#define              PIXC_ERR  0x8000000  /* Pixel Compositor Error */
#define               NFC_ERR  0x10000000 /* Nand Flash Controller Error */
#define             ATAPI_ERR  0x20000000 /* ATAPI Error */
#define              CAN1_ERR  0x40000000 /* CAN1 Error */
#define             DMAR0_ERR  0x80000000 /* DMAR0 Overflow Error */
#define             DMAR1_ERR  0x80000000 /* DMAR1 Overflow Error */
#define                 DMAR0  0x80000000 /* DMAR0 Block */
#define                 DMAR1  0x80000000 /* DMAR1 Block */

/* Bit masks for SIC_IWR2, SIC_IMASK2, SIC_ISR2 */

#define                 DMA15  0x1        /* DMA Channel 15 */
#define                 DMA16  0x2        /* DMA Channel 16 */
#define                 DMA17  0x4        /* DMA Channel 17 */
#define                 DMA22  0x8        /* DMA Channel 22 */
#define                   CNT  0x10       /* Counter */
#define                   KEY  0x20       /* Keypad */
#define               CAN1_RX  0x40       /* CAN1 Receive */
#define               CAN1_TX  0x80       /* CAN1 Transmit */
#define             SDH_INT_MASK0  0x100      /* SDH Mask 0 */
#define             SDH_INT_MASK1  0x200      /* SDH Mask 1 */
#define              USB_EINT  0x400      /* USB Exception */
#define              USB_INT0  0x800      /* USB Interrupt 0 */
#define              USB_INT1  0x1000     /* USB Interrupt 1 */
#define              USB_INT2  0x2000     /* USB Interrupt 2 */
#define            USB_DMAINT  0x4000     /* USB DMA */
#define                OTPSEC  0x8000     /* OTP Access Complete */
#define                TIMER0  0x400000   /* Timer 0 */
#define                TIMER1  0x800000   /* Timer 1 */
#define                TIMER2  0x1000000  /* Timer 2 */
#define                TIMER3  0x2000000  /* Timer 3 */
#define                TIMER4  0x4000000  /* Timer 4 */
#define                TIMER5  0x8000000  /* Timer 5 */
#define                TIMER6  0x10000000 /* Timer 6 */
#define                TIMER7  0x20000000 /* Timer 7 */
#define                 PINT2  0x40000000 /* Pin Interrupt 2 */
#define                 PINT3  0x80000000 /* Pin Interrupt 3 */

/* Bit masks for DMAx_PERIPHERAL_MAP, MDMA_Sx_IRQ_STATUS, MDMA_Dx_IRQ_STATUS */

#define                     CTYPE  0x40       /* DMA Channel Type */
#define                      PMAP  0xf000     /* Peripheral Mapped To This Channel */

/* Bit masks for DMACx_TC_PER */

#define        DCB_TRAFFIC_PERIOD  0xf        /* DCB Traffic Control Period */
#define        DEB_TRAFFIC_PERIOD  0xf0       /* DEB Traffic Control Period */
#define        DAB_TRAFFIC_PERIOD  0x700      /* DAB Traffic Control Period */
#define   MDMA_ROUND_ROBIN_PERIOD  0xf800     /* MDMA Round Robin Period */

/* Bit masks for DMACx_TC_CNT */

#define         DCB_TRAFFIC_COUNT  0xf        /* DCB Traffic Control Count */
#define         DEB_TRAFFIC_COUNT  0xf0       /* DEB Traffic Control Count */
#define         DAB_TRAFFIC_COUNT  0x700      /* DAB Traffic Control Count */
#define    MDMA_ROUND_ROBIN_COUNT  0xf800     /* MDMA Round Robin Count */

/* Bit masks for DMAC1_PERIMUX */

#define                   PMUXSDH  0x1        /* Peripheral Select for DMA22 channel */

/* *********************  ASYNCHRONOUS MEMORY CONTROLLER MASKS  *************************/
/* EBIU_AMGCTL Masks																	*/
#define AMCKEN			0x0001		/* Enable CLKOUT									*/
#define	AMBEN_NONE		0x0000		/* All Banks Disabled								*/
#define AMBEN_B0		0x0002		/* Enable Async Memory Bank 0 only					*/
#define AMBEN_B0_B1		0x0004		/* Enable Async Memory Banks 0 & 1 only				*/
#define AMBEN_B0_B1_B2	0x0006		/* Enable Async Memory Banks 0, 1, and 2			*/
#define AMBEN_ALL		0x0008		/* Enable Async Memory Banks (all) 0, 1, 2, and 3	*/


/* Bit masks for EBIU_AMBCTL0 */

#define                   B0RDYEN  0x1        /* Bank 0 ARDY Enable */
#define                  B0RDYPOL  0x2        /* Bank 0 ARDY Polarity */
#define                      B0TT  0xc        /* Bank 0 transition time */
#define                      B0ST  0x30       /* Bank 0 Setup time */
#define                      B0HT  0xc0       /* Bank 0 Hold time */
#define                     B0RAT  0xf00      /* Bank 0 Read access time */
#define                     B0WAT  0xf000     /* Bank 0 write access time */
#define                   B1RDYEN  0x10000    /* Bank 1 ARDY Enable */
#define                  B1RDYPOL  0x20000    /* Bank 1 ARDY Polarity */
#define                      B1TT  0xc0000    /* Bank 1 transition time */
#define                      B1ST  0x300000   /* Bank 1 Setup time */
#define                      B1HT  0xc00000   /* Bank 1 Hold time */
#define                     B1RAT  0xf000000  /* Bank 1 Read access time */
#define                     B1WAT  0xf0000000 /* Bank 1 write access time */

/* Bit masks for EBIU_AMBCTL1 */

#define                   B2RDYEN  0x1        /* Bank 2 ARDY Enable */
#define                  B2RDYPOL  0x2        /* Bank 2 ARDY Polarity */
#define                      B2TT  0xc        /* Bank 2 transition time */
#define                      B2ST  0x30       /* Bank 2 Setup time */
#define                      B2HT  0xc0       /* Bank 2 Hold time */
#define                     B2RAT  0xf00      /* Bank 2 Read access time */
#define                     B2WAT  0xf000     /* Bank 2 write access time */
#define                   B3RDYEN  0x10000    /* Bank 3 ARDY Enable */
#define                  B3RDYPOL  0x20000    /* Bank 3 ARDY Polarity */
#define                      B3TT  0xc0000    /* Bank 3 transition time */
#define                      B3ST  0x300000   /* Bank 3 Setup time */
#define                      B3HT  0xc00000   /* Bank 3 Hold time */
#define                     B3RAT  0xf000000  /* Bank 3 Read access time */
#define                     B3WAT  0xf0000000 /* Bank 3 write access time */

/* Bit masks for EBIU_MBSCTL */

#define                  AMSB0CTL  0x3        /* Async Memory Bank 0 select */
#define                  AMSB1CTL  0xc        /* Async Memory Bank 1 select */
#define                  AMSB2CTL  0x30       /* Async Memory Bank 2 select */
#define                  AMSB3CTL  0xc0       /* Async Memory Bank 3 select */

/* Bit masks for EBIU_MODE */

#define                    B0MODE  0x3        /* Async Memory Bank 0 Access Mode */
#define                    B1MODE  0xc        /* Async Memory Bank 1 Access Mode */
#define                    B2MODE  0x30       /* Async Memory Bank 2 Access Mode */
#define                    B3MODE  0xc0       /* Async Memory Bank 3 Access Mode */

/* Bit masks for EBIU_FCTL */

#define               TESTSETLOCK  0x1        /* Test set lock */
#define                      BCLK  0x6        /* Burst clock frequency */
#define                      PGWS  0x38       /* Page wait states */
#define                      PGSZ  0x40       /* Page size */
#define                      RDDL  0x380      /* Read data delay */

/* Bit masks for EBIU_ARBSTAT */

#define                   ARBSTAT  0x1        /* Arbitration status */
#define                    BGSTAT  0x2        /* Bus grant status */

/* Bit masks for EBIU_DDRCTL0 */

#define                     TREFI  0x3fff     /* Refresh Interval */
#define                      TRFC  0x3c000    /* Auto-refresh command period */
#define                       TRP  0x3c0000   /* Pre charge-to-active command period */
#define                      TRAS  0x3c00000  /* Min Active-to-pre charge time */
#define                       TRC  0x3c000000 /* Active-to-active time */
#define DDR_TRAS(x)		((x<<22)&TRAS)	/* DDR tRAS = (1~15) cycles */
#define DDR_TRP(x)		((x<<18)&TRP)	/* DDR tRP = (1~15) cycles */
#define DDR_TRC(x)		((x<<26)&TRC)	/* DDR tRC = (1~15) cycles */
#define DDR_TRFC(x)		((x<<14)&TRFC)	/* DDR tRFC = (1~15) cycles */
#define DDR_TREFI(x)		(x&TREFI)	/* DDR tRFC = (1~15) cycles */

/* Bit masks for EBIU_DDRCTL1 */

#define                      TRCD  0xf        /* Active-to-Read/write delay */
#define                      TMRD  0xf0       /* Mode register set to active */
#define                       TWR  0x300      /* Write Recovery time */
#define               DDRDATWIDTH  0x3000     /* DDR data width */
#define                  EXTBANKS  0xc000     /* External banks */
#define               DDRDEVWIDTH  0x30000    /* DDR device width */
#define                DDRDEVSIZE  0xc0000    /* DDR device size */
#define                      TWTR  0xf0000000 /* Write-to-read delay */
#define DDR_TWTR(x)		((x<<28)&TWTR)	/* DDR tWTR = (1~15) cycles */
#define DDR_TMRD(x)		((x<<4)&TMRD)	/* DDR tMRD = (1~15) cycles */
#define DDR_TWR(x)		((x<<8)&TWR)	/* DDR tWR = (1~15) cycles */
#define DDR_TRCD(x)		(x&TRCD)	/* DDR tRCD = (1~15) cycles */
#define DDR_DATWIDTH		0x2000		/* DDR data width */
#define EXTBANK_1		0		/* 1 external bank */
#define EXTBANK_2		0x4000		/* 2 external banks */
#define DEVSZ_64		0x40000		/* DDR External Bank Size = 64MB */
#define DEVSZ_128		0x80000		/* DDR External Bank Size = 128MB */
#define DEVSZ_256		0xc0000		/* DDR External Bank Size = 256MB */
#define DEVSZ_512		0		/* DDR External Bank Size = 512MB */
#define DEVWD_4			0		/* DDR Device Width = 4 Bits    */
#define DEVWD_8			0x10000		/* DDR Device Width = 8 Bits    */
#define DEVWD_16		0x20000		/* DDR Device Width = 16 Bits    */

/* Bit masks for EBIU_DDRCTL2 */

#define               BURSTLENGTH  0x7        /* Burst length */
#define                CASLATENCY  0x70       /* CAS latency */
#define                  DLLRESET  0x100      /* DLL Reset */
#define                      REGE  0x1000     /* Register mode enable */
#define CL_1_5			0x50		/* DDR CAS Latency = 1.5 cycles */
#define CL_2			0x20		/* DDR CAS Latency = 2 cycles */
#define CL_2_5			0x60		/* DDR CAS Latency = 2.5 cycles */
#define CL_3			0x30		/* DDR CAS Latency = 3 cycles */

/* Bit masks for EBIU_DDRCTL3 */

#define                      PASR  0x7        /* Partial array self-refresh */

/* Bit masks for EBIU_DDRQUE */

#define                DEB1_PFLEN  0x3        /* Pre fetch length for DEB1 accesses */
#define                DEB2_PFLEN  0xc        /* Pre fetch length for DEB2 accesses */
#define                DEB3_PFLEN  0x30       /* Pre fetch length for DEB3 accesses */
#define          DEB_ARB_PRIORITY  0x700      /* Arbitration between DEB busses */
#define               DEB1_URGENT  0x1000     /* DEB1 Urgent */
#define               DEB2_URGENT  0x2000     /* DEB2 Urgent */
#define               DEB3_URGENT  0x4000     /* DEB3 Urgent */

/* Bit masks for EBIU_ERRMST */

#define                DEB1_ERROR  0x1        /* DEB1 Error */
#define                DEB2_ERROR  0x2        /* DEB2 Error */
#define                DEB3_ERROR  0x4        /* DEB3 Error */
#define                CORE_ERROR  0x8        /* Core error */
#define                DEB_MERROR  0x10       /* DEB1 Error (2nd) */
#define               DEB2_MERROR  0x20       /* DEB2 Error (2nd) */
#define               DEB3_MERROR  0x40       /* DEB3 Error (2nd) */
#define               CORE_MERROR  0x80       /* Core Error (2nd) */

/* Bit masks for EBIU_RSTCTL */

#define                 DDRSRESET  0x1        /* DDR soft reset */
#define               PFTCHSRESET  0x4        /* DDR prefetch reset */
#define                     SRREQ  0x8        /* Self-refresh request */
#define                     SRACK  0x10       /* Self-refresh acknowledge */
#define                MDDRENABLE  0x20       /* Mobile DDR enable */

/* Bit masks for EBIU_DDRMCEN */

#define                B0WCENABLE  0x1        /* Bank 0 write count enable */
#define                B1WCENABLE  0x2        /* Bank 1 write count enable */
#define                B2WCENABLE  0x4        /* Bank 2 write count enable */
#define                B3WCENABLE  0x8        /* Bank 3 write count enable */
#define                B4WCENABLE  0x10       /* Bank 4 write count enable */
#define                B5WCENABLE  0x20       /* Bank 5 write count enable */
#define                B6WCENABLE  0x40       /* Bank 6 write count enable */
#define                B7WCENABLE  0x80       /* Bank 7 write count enable */
#define                B0RCENABLE  0x100      /* Bank 0 read count enable */
#define                B1RCENABLE  0x200      /* Bank 1 read count enable */
#define                B2RCENABLE  0x400      /* Bank 2 read count enable */
#define                B3RCENABLE  0x800      /* Bank 3 read count enable */
#define                B4RCENABLE  0x1000     /* Bank 4 read count enable */
#define                B5RCENABLE  0x2000     /* Bank 5 read count enable */
#define                B6RCENABLE  0x4000     /* Bank 6 read count enable */
#define                B7RCENABLE  0x8000     /* Bank 7 read count enable */
#define             ROWACTCENABLE  0x10000    /* DDR Row activate count enable */
#define                RWTCENABLE  0x20000    /* DDR R/W Turn around count enable */
#define                 ARCENABLE  0x40000    /* DDR Auto-refresh count enable */
#define                 GC0ENABLE  0x100000   /* DDR Grant count 0 enable */
#define                 GC1ENABLE  0x200000   /* DDR Grant count 1 enable */
#define                 GC2ENABLE  0x400000   /* DDR Grant count 2 enable */
#define                 GC3ENABLE  0x800000   /* DDR Grant count 3 enable */
#define                 GCCONTROL  0x3000000  /* DDR Grant Count Control */

/* Bit masks for EBIU_DDRMCCL */

#define                 CB0WCOUNT  0x1        /* Clear write count 0 */
#define                 CB1WCOUNT  0x2        /* Clear write count 1 */
#define                 CB2WCOUNT  0x4        /* Clear write count 2 */
#define                 CB3WCOUNT  0x8        /* Clear write count 3 */
#define                 CB4WCOUNT  0x10       /* Clear write count 4 */
#define                 CB5WCOUNT  0x20       /* Clear write count 5 */
#define                 CB6WCOUNT  0x40       /* Clear write count 6 */
#define                 CB7WCOUNT  0x80       /* Clear write count 7 */
#define                  CBRCOUNT  0x100      /* Clear read count 0 */
#define                 CB1RCOUNT  0x200      /* Clear read count 1 */
#define                 CB2RCOUNT  0x400      /* Clear read count 2 */
#define                 CB3RCOUNT  0x800      /* Clear read count 3 */
#define                 CB4RCOUNT  0x1000     /* Clear read count 4 */
#define                 CB5RCOUNT  0x2000     /* Clear read count 5 */
#define                 CB6RCOUNT  0x4000     /* Clear read count 6 */
#define                 CB7RCOUNT  0x8000     /* Clear read count 7 */
#define                  CRACOUNT  0x10000    /* Clear row activation count */
#define                CRWTACOUNT  0x20000    /* Clear R/W turn-around count */
#define                  CARCOUNT  0x40000    /* Clear auto-refresh count */
#define                  CG0COUNT  0x100000   /* Clear grant count 0 */
#define                  CG1COUNT  0x200000   /* Clear grant count 1 */
#define                  CG2COUNT  0x400000   /* Clear grant count 2 */
#define                  CG3COUNT  0x800000   /* Clear grant count 3 */

/* Bit masks for (PORTx is PORTA - PORTJ) includes PORTx_FER, PORTx_SET, PORTx_CLEAR, PORTx_DIR_SET, PORTx_DIR_CLEAR, PORTx_INEN */

#define                       Px0  0x1        /* GPIO 0 */
#define                       Px1  0x2        /* GPIO 1 */
#define                       Px2  0x4        /* GPIO 2 */
#define                       Px3  0x8        /* GPIO 3 */
#define                       Px4  0x10       /* GPIO 4 */
#define                       Px5  0x20       /* GPIO 5 */
#define                       Px6  0x40       /* GPIO 6 */
#define                       Px7  0x80       /* GPIO 7 */
#define                       Px8  0x100      /* GPIO 8 */
#define                       Px9  0x200      /* GPIO 9 */
#define                      Px10  0x400      /* GPIO 10 */
#define                      Px11  0x800      /* GPIO 11 */
#define                      Px12  0x1000     /* GPIO 12 */
#define                      Px13  0x2000     /* GPIO 13 */
#define                      Px14  0x4000     /* GPIO 14 */
#define                      Px15  0x8000     /* GPIO 15 */

/* Bit masks for PORTA_MUX - PORTJ_MUX */

#define                      PxM0  0x3        /* GPIO Mux 0 */
#define                      PxM1  0xc        /* GPIO Mux 1 */
#define                      PxM2  0x30       /* GPIO Mux 2 */
#define                      PxM3  0xc0       /* GPIO Mux 3 */
#define                      PxM4  0x300      /* GPIO Mux 4 */
#define                      PxM5  0xc00      /* GPIO Mux 5 */
#define                      PxM6  0x3000     /* GPIO Mux 6 */
#define                      PxM7  0xc000     /* GPIO Mux 7 */
#define                      PxM8  0x30000    /* GPIO Mux 8 */
#define                      PxM9  0xc0000    /* GPIO Mux 9 */
#define                     PxM10  0x300000   /* GPIO Mux 10 */
#define                     PxM11  0xc00000   /* GPIO Mux 11 */
#define                     PxM12  0x3000000  /* GPIO Mux 12 */
#define                     PxM13  0xc000000  /* GPIO Mux 13 */
#define                     PxM14  0x30000000 /* GPIO Mux 14 */
#define                     PxM15  0xc0000000 /* GPIO Mux 15 */


/* Bit masks for PINTx_MASK_SET/CLEAR, PINTx_REQUEST, PINTx_LATCH, PINTx_EDGE_SET/CLEAR, PINTx_INVERT_SET/CLEAR, PINTx_PINTSTATE */

#define                       IB0  0x1        /* Interrupt Bit 0 */
#define                       IB1  0x2        /* Interrupt Bit 1 */
#define                       IB2  0x4        /* Interrupt Bit 2 */
#define                       IB3  0x8        /* Interrupt Bit 3 */
#define                       IB4  0x10       /* Interrupt Bit 4 */
#define                       IB5  0x20       /* Interrupt Bit 5 */
#define                       IB6  0x40       /* Interrupt Bit 6 */
#define                       IB7  0x80       /* Interrupt Bit 7 */
#define                       IB8  0x100      /* Interrupt Bit 8 */
#define                       IB9  0x200      /* Interrupt Bit 9 */
#define                      IB10  0x400      /* Interrupt Bit 10 */
#define                      IB11  0x800      /* Interrupt Bit 11 */
#define                      IB12  0x1000     /* Interrupt Bit 12 */
#define                      IB13  0x2000     /* Interrupt Bit 13 */
#define                      IB14  0x4000     /* Interrupt Bit 14 */
#define                      IB15  0x8000     /* Interrupt Bit 15 */

/* Bit masks for TIMERx_CONFIG */

#define                     TMODE  0x3        /* Timer Mode */
#define                  PULSE_HI  0x4        /* Pulse Polarity */
#define                PERIOD_CNT  0x8        /* Period Count */
#define                   IRQ_ENA  0x10       /* Interrupt Request Enable */
#define                   TIN_SEL  0x20       /* Timer Input Select */
#define                   OUT_DIS  0x40       /* Output Pad Disable */
#define                   CLK_SEL  0x80       /* Timer Clock Select */
#define                 TOGGLE_HI  0x100      /* Toggle Mode */
#define                   EMU_RUN  0x200      /* Emulation Behavior Select */
#define                   ERR_TYP  0xc000     /* Error Type */

/* Bit masks for TIMER_ENABLE0 */

#define                    TIMEN0  0x1        /* Timer 0 Enable */
#define                    TIMEN1  0x2        /* Timer 1 Enable */
#define                    TIMEN2  0x4        /* Timer 2 Enable */
#define                    TIMEN3  0x8        /* Timer 3 Enable */
#define                    TIMEN4  0x10       /* Timer 4 Enable */
#define                    TIMEN5  0x20       /* Timer 5 Enable */
#define                    TIMEN6  0x40       /* Timer 6 Enable */
#define                    TIMEN7  0x80       /* Timer 7 Enable */

/* Bit masks for TIMER_DISABLE0 */

#define                   TIMDIS0  0x1        /* Timer 0 Disable */
#define                   TIMDIS1  0x2        /* Timer 1 Disable */
#define                   TIMDIS2  0x4        /* Timer 2 Disable */
#define                   TIMDIS3  0x8        /* Timer 3 Disable */
#define                   TIMDIS4  0x10       /* Timer 4 Disable */
#define                   TIMDIS5  0x20       /* Timer 5 Disable */
#define                   TIMDIS6  0x40       /* Timer 6 Disable */
#define                   TIMDIS7  0x80       /* Timer 7 Disable */

/* Bit masks for TIMER_STATUS0 */

#define                    TIMIL0  0x1        /* Timer 0 Interrupt */
#define                    TIMIL1  0x2        /* Timer 1 Interrupt */
#define                    TIMIL2  0x4        /* Timer 2 Interrupt */
#define                    TIMIL3  0x8        /* Timer 3 Interrupt */
#define                 TOVF_ERR0  0x10       /* Timer 0 Counter Overflow */
#define                 TOVF_ERR1  0x20       /* Timer 1 Counter Overflow */
#define                 TOVF_ERR2  0x40       /* Timer 2 Counter Overflow */
#define                 TOVF_ERR3  0x80       /* Timer 3 Counter Overflow */
#define                     TRUN0  0x1000     /* Timer 0 Slave Enable Status */
#define                     TRUN1  0x2000     /* Timer 1 Slave Enable Status */
#define                     TRUN2  0x4000     /* Timer 2 Slave Enable Status */
#define                     TRUN3  0x8000     /* Timer 3 Slave Enable Status */
#define                    TIMIL4  0x10000    /* Timer 4 Interrupt */
#define                    TIMIL5  0x20000    /* Timer 5 Interrupt */
#define                    TIMIL6  0x40000    /* Timer 6 Interrupt */
#define                    TIMIL7  0x80000    /* Timer 7 Interrupt */
#define                 TOVF_ERR4  0x100000   /* Timer 4 Counter Overflow */
#define                 TOVF_ERR5  0x200000   /* Timer 5 Counter Overflow */
#define                 TOVF_ERR6  0x400000   /* Timer 6 Counter Overflow */
#define                 TOVF_ERR7  0x800000   /* Timer 7 Counter Overflow */
#define                     TRUN4  0x10000000 /* Timer 4 Slave Enable Status */
#define                     TRUN5  0x20000000 /* Timer 5 Slave Enable Status */
#define                     TRUN6  0x40000000 /* Timer 6 Slave Enable Status */
#define                     TRUN7  0x80000000 /* Timer 7 Slave Enable Status */

/* Bit masks for SECURE_SYSSWT */

#define                   EMUDABL  0x1        /* Emulation Disable. */
#define                   RSTDABL  0x2        /* Reset Disable */
#define                   L1IDABL  0x1c       /* L1 Instruction Memory Disable. */
#define                  L1DADABL  0xe0       /* L1 Data Bank A Memory Disable. */
#define                  L1DBDABL  0x700      /* L1 Data Bank B Memory Disable. */
#define                   DMA0OVR  0x800      /* DMA0 Memory Access Override */
#define                   DMA1OVR  0x1000     /* DMA1 Memory Access Override */
#define                    EMUOVR  0x4000     /* Emulation Override */
#define                    OTPSEN  0x8000     /* OTP Secrets Enable. */
#define                    L2DABL  0x70000    /* L2 Memory Disable. */

/* Bit masks for SECURE_CONTROL */

#define                   SECURE0  0x1        /* SECURE 0 */
#define                   SECURE1  0x2        /* SECURE 1 */
#define                   SECURE2  0x4        /* SECURE 2 */
#define                   SECURE3  0x8        /* SECURE 3 */

/* Bit masks for SECURE_STATUS */

#define                   SECMODE  0x3        /* Secured Mode Control State */
#define                       NMI  0x4        /* Non Maskable Interrupt */
#define                   AFVALID  0x8        /* Authentication Firmware Valid */
#define                    AFEXIT  0x10       /* Authentication Firmware Exit */
#define                   SECSTAT  0xe0       /* Secure Status */

/* SWRST Masks */
#define              SYSTEM_RESET 0x0007       /* Initiates A System Software Reset */
#define              DOUBLE_FAULT 0x0008       /* Core Double Fault Causes Reset */
#define              RESET_DOUBLE 0x2000       /* SW Reset Generated By Core Double-Fault */
#define                RESET_WDOG 0x4000       /* SW Reset Generated By Watchdog Timer */
#define            RESET_SOFTWARE 0x8000       /* SW Reset Occurred Since Last Read Of SWRST */

/* Bit masks for EPPIx_STATUS */

#define                 CFIFO_ERR  0x1        /* Chroma FIFO Error */
#define                 YFIFO_ERR  0x2        /* Luma FIFO Error */
#define                 LTERR_OVR  0x4        /* Line Track Overflow */
#define                LTERR_UNDR  0x8        /* Line Track Underflow */
#define                 FTERR_OVR  0x10       /* Frame Track Overflow */
#define                FTERR_UNDR  0x20       /* Frame Track Underflow */
#define                  ERR_NCOR  0x40       /* Preamble Error Not Corrected */
#define                   DMA1URQ  0x80       /* DMA1 Urgent Request */
#define                   DMA0URQ  0x100      /* DMA0 Urgent Request */
#define                   ERR_DET  0x4000     /* Preamble Error Detected */
#define                       FLD  0x8000     /* Field */

/* Bit masks for EPPIx_CONTROL */

#define                   EPPI_EN  0x1        /* Enable */
#define                  EPPI_DIR  0x2        /* Direction */
#define                  XFR_TYPE  0xc        /* Operating Mode */
#define                    FS_CFG  0x30       /* Frame Sync Configuration */
#define                   FLD_SEL  0x40       /* Field Select/Trigger */
#define                  ITU_TYPE  0x80       /* ITU Interlaced or Progressive */
#define                  BLANKGEN  0x100      /* ITU Output Mode with Internal Blanking Generation */
#define                   ICLKGEN  0x200      /* Internal Clock Generation */
#define                    IFSGEN  0x400      /* Internal Frame Sync Generation */
#define                      POLC  0x1800     /* Frame Sync and Data Driving/Sampling Edges */
#define                      POLS  0x6000     /* Frame Sync Polarity */
#define                   DLENGTH  0x38000    /* Data Length */
#define                   SKIP_EN  0x40000    /* Skip Enable */
#define                   SKIP_EO  0x80000    /* Skip Even or Odd */
#define                    PACKEN  0x100000   /* Packing/Unpacking Enable */
#define                    SWAPEN  0x200000   /* Swap Enable */
#define                  SIGN_EXT  0x400000   /* Sign Extension or Zero-filled / Data Split Format */
#define             SPLT_EVEN_ODD  0x800000   /* Split Even and Odd Data Samples */
#define               SUBSPLT_ODD  0x1000000  /* Sub-split Odd Samples */
#define                    DMACFG  0x2000000  /* One or Two DMA Channels Mode */
#define                RGB_FMT_EN  0x4000000  /* RGB Formatting Enable */
#define                  FIFO_RWM  0x18000000 /* FIFO Regular Watermarks */
#define                  FIFO_UWM  0x60000000 /* FIFO Urgent Watermarks */

#define DLEN_8		(0 << 15) /* 000 - 8 bits */
#define DLEN_10		(1 << 15) /* 001 - 10 bits */
#define DLEN_12		(2 << 15) /* 010 - 12 bits */
#define DLEN_14		(3 << 15) /* 011 - 14 bits */
#define DLEN_16		(4 << 15) /* 100 - 16 bits */
#define DLEN_18		(5 << 15) /* 101 - 18 bits */
#define DLEN_24		(6 << 15) /* 110 - 24 bits */


/* Bit masks for EPPIx_FS2W_LVB */

#define                   F1VB_BD  0xff       /* Vertical Blanking before Field 1 Active Data */
#define                   F1VB_AD  0xff00     /* Vertical Blanking after Field 1 Active Data */
#define                   F2VB_BD  0xff0000   /* Vertical Blanking before Field 2 Active Data */
#define                   F2VB_AD  0xff000000 /* Vertical Blanking after Field 2 Active Data */

/* Bit masks for EPPIx_FS2W_LAVF */

#define                    F1_ACT  0xffff     /* Number of Lines of Active Data in Field 1 */
#define                    F2_ACT  0xffff0000 /* Number of Lines of Active Data in Field 2 */

/* Bit masks for EPPIx_CLIP */

#define                   LOW_ODD  0xff       /* Lower Limit for Odd Bytes (Chroma) */
#define                  HIGH_ODD  0xff00     /* Upper Limit for Odd Bytes (Chroma) */
#define                  LOW_EVEN  0xff0000   /* Lower Limit for Even Bytes (Luma) */
#define                 HIGH_EVEN  0xff000000 /* Upper Limit for Even Bytes (Luma) */

/* ************************************************ */
/* The TWI bit masks fields are from the ADSP-BF538 */
/* and they have not been verified as the final     */
/* ones for the Moab processors ... bz 1/19/2007    */
/* ************************************************ */

/* Bit masks for TWIx_CONTROL */

#define                  PRESCALE  0x7f       /* Prescale Value */
#define                   TWI_ENA  0x80       /* TWI Enable */
#define                      SCCB  0x200      /* Serial Camera Control Bus */

/* Bit maskes for TWIx_CLKDIV */

#define                    CLKLOW  0xff       /* Clock Low */
#define                     CLKHI  0xff00     /* Clock High */

/* Bit maskes for TWIx_SLAVE_CTL */

#define                       SEN  0x1        /* Slave Enable */
#define                    STDVAL  0x4        /* Slave Transmit Data Valid */
#define                       NAK  0x8        /* Not Acknowledge */
#define                       GEN  0x10       /* General Call Enable */

/* Bit maskes for TWIx_SLAVE_ADDR */

#define                     SADDR  0x7f       /* Slave Mode Address */

/* Bit maskes for TWIx_SLAVE_STAT */

#define                      SDIR  0x1        /* Slave Transfer Direction */
#define                     GCALL  0x2        /* General Call */

/* Bit maskes for TWIx_MASTER_CTL */

#define                       MEN  0x1        /* Master Mode Enable */
#define                      MDIR  0x4        /* Master Transfer Direction */
#define                      FAST  0x8        /* Fast Mode */
#define                      STOP  0x10       /* Issue Stop Condition */
#define                    RSTART  0x20       /* Repeat Start */
#define                      DCNT  0x3fc0     /* Data Transfer Count */
#define                    SDAOVR  0x4000     /* Serial Data Override */
#define                    SCLOVR  0x8000     /* Serial Clock Override */

/* Bit maskes for TWIx_MASTER_ADDR */

#define                     MADDR  0x7f       /* Master Mode Address */

/* Bit maskes for TWIx_MASTER_STAT */

#define                     MPROG  0x1        /* Master Transfer in Progress */
#define                   LOSTARB  0x2        /* Lost Arbitration */
#define                      ANAK  0x4        /* Address Not Acknowledged */
#define                      DNAK  0x8        /* Data Not Acknowledged */
#define                  BUFRDERR  0x10       /* Buffer Read Error */
#define                  BUFWRERR  0x20       /* Buffer Write Error */
#define                    SDASEN  0x40       /* Serial Data Sense */
#define                    SCLSEN  0x80       /* Serial Clock Sense */
#define                   BUSBUSY  0x100      /* Bus Busy */

/* Bit maskes for TWIx_FIFO_CTL */

#define                  XMTFLUSH  0x1        /* Transmit Buffer Flush */
#define                  RCVFLUSH  0x2        /* Receive Buffer Flush */
#define                 XMTINTLEN  0x4        /* Transmit Buffer Interrupt Length */
#define                 RCVINTLEN  0x8        /* Receive Buffer Interrupt Length */

/* Bit maskes for TWIx_FIFO_STAT */

#define                   XMTSTAT  0x3        /* Transmit FIFO Status */
#define                   RCVSTAT  0xc        /* Receive FIFO Status */

/* Bit maskes for TWIx_INT_MASK */

#define                    SINITM  0x1        /* Slave Transfer Initiated Interrupt Mask */
#define                    SCOMPM  0x2        /* Slave Transfer Complete Interrupt Mask */
#define                     SERRM  0x4        /* Slave Transfer Error Interrupt Mask */
#define                     SOVFM  0x8        /* Slave Overflow Interrupt Mask */
#define                    MCOMPM  0x10       /* Master Transfer Complete Interrupt Mask */
#define                     MERRM  0x20       /* Master Transfer Error Interrupt Mask */
#define                  XMTSERVM  0x40       /* Transmit FIFO Service Interrupt Mask */
#define                  RCVSERVM  0x80       /* Receive FIFO Service Interrupt Mask */

/* Bit maskes for TWIx_INT_STAT */

#define                     SINIT  0x1        /* Slave Transfer Initiated */
#define                     SCOMP  0x2        /* Slave Transfer Complete */
#define                      SERR  0x4        /* Slave Transfer Error */
#define                      SOVF  0x8        /* Slave Overflow */
#define                     MCOMP  0x10       /* Master Transfer Complete */
#define                      MERR  0x20       /* Master Transfer Error */
#define                   XMTSERV  0x40       /* Transmit FIFO Service */
#define                   RCVSERV  0x80       /* Receive FIFO Service */

/* Bit maskes for TWIx_XMT_DATA8 */

#define                  XMTDATA8  0xff       /* Transmit FIFO 8-Bit Data */

/* Bit maskes for TWIx_XMT_DATA16 */

#define                 XMTDATA16  0xffff     /* Transmit FIFO 16-Bit Data */

/* Bit maskes for TWIx_RCV_DATA8 */

#define                  RCVDATA8  0xff       /* Receive FIFO 8-Bit Data */

/* Bit maskes for TWIx_RCV_DATA16 */

#define                 RCVDATA16  0xffff     /* Receive FIFO 16-Bit Data */

/* ******************************************* */
/*     MULTI BIT MACRO ENUMERATIONS            */
/* ******************************************* */

/* BCODE bit field options (SYSCFG register) */

#define BCODE_WAKEUP    0x0000  /* boot according to wake-up condition */
#define BCODE_FULLBOOT  0x0010  /* always perform full boot */
#define BCODE_QUICKBOOT 0x0020  /* always perform quick boot */
#define BCODE_NOBOOT    0x0030  /* always perform full boot */

/* TMODE in TIMERx_CONFIG bit field options */

#define PWM_OUT  0x0001
#define WDTH_CAP 0x0002
#define EXT_CLK  0x0003

/* PINTx Register Bit Definitions */

#define PIQ0 0x00000001
#define PIQ1 0x00000002
#define PIQ2 0x00000004
#define PIQ3 0x00000008

#define PIQ4 0x00000010
#define PIQ5 0x00000020
#define PIQ6 0x00000040
#define PIQ7 0x00000080

#define PIQ8 0x00000100
#define PIQ9 0x00000200
#define PIQ10 0x00000400
#define PIQ11 0x00000800

#define PIQ12 0x00001000
#define PIQ13 0x00002000
#define PIQ14 0x00004000
#define PIQ15 0x00008000

#define PIQ16 0x00010000
#define PIQ17 0x00020000
#define PIQ18 0x00040000
#define PIQ19 0x00080000

#define PIQ20 0x00100000
#define PIQ21 0x00200000
#define PIQ22 0x00400000
#define PIQ23 0x00800000

#define PIQ24 0x01000000
#define PIQ25 0x02000000
#define PIQ26 0x04000000
#define PIQ27 0x08000000

#define PIQ28 0x10000000
#define PIQ29 0x20000000
#define PIQ30 0x40000000
#define PIQ31 0x80000000

/* PORT A Bit Definitions for the registers
PORTA, PORTA_SET, PORTA_CLEAR,
PORTA_DIR_SET, PORTA_DIR_CLEAR, PORTA_INEN,
PORTA_FER registers
*/

#define PA0 0x0001
#define PA1 0x0002
#define PA2 0x0004
#define PA3 0x0008
#define PA4 0x0010
#define PA5 0x0020
#define PA6 0x0040
#define PA7 0x0080
#define PA8 0x0100
#define PA9 0x0200
#define PA10 0x0400
#define PA11 0x0800
#define PA12 0x1000
#define PA13 0x2000
#define PA14 0x4000
#define PA15 0x8000

/* PORT B Bit Definitions for the registers
PORTB, PORTB_SET, PORTB_CLEAR,
PORTB_DIR_SET, PORTB_DIR_CLEAR, PORTB_INEN,
PORTB_FER registers
*/

#define PB0 0x0001
#define PB1 0x0002
#define PB2 0x0004
#define PB3 0x0008
#define PB4 0x0010
#define PB5 0x0020
#define PB6 0x0040
#define PB7 0x0080
#define PB8 0x0100
#define PB9 0x0200
#define PB10 0x0400
#define PB11 0x0800
#define PB12 0x1000
#define PB13 0x2000
#define PB14 0x4000


/* PORT C Bit Definitions for the registers
PORTC, PORTC_SET, PORTC_CLEAR,
PORTC_DIR_SET, PORTC_DIR_CLEAR, PORTC_INEN,
PORTC_FER registers
*/


#define PC0 0x0001
#define PC1 0x0002
#define PC2 0x0004
#define PC3 0x0008
#define PC4 0x0010
#define PC5 0x0020
#define PC6 0x0040
#define PC7 0x0080
#define PC8 0x0100
#define PC9 0x0200
#define PC10 0x0400
#define PC11 0x0800
#define PC12 0x1000
#define PC13 0x2000


/* PORT D Bit Definitions for the registers
PORTD, PORTD_SET, PORTD_CLEAR,
PORTD_DIR_SET, PORTD_DIR_CLEAR, PORTD_INEN,
PORTD_FER registers
*/

#define PD0 0x0001
#define PD1 0x0002
#define PD2 0x0004
#define PD3 0x0008
#define PD4 0x0010
#define PD5 0x0020
#define PD6 0x0040
#define PD7 0x0080
#define PD8 0x0100
#define PD9 0x0200
#define PD10 0x0400
#define PD11 0x0800
#define PD12 0x1000
#define PD13 0x2000
#define PD14 0x4000
#define PD15 0x8000

/* PORT E Bit Definitions for the registers
PORTE, PORTE_SET, PORTE_CLEAR,
PORTE_DIR_SET, PORTE_DIR_CLEAR, PORTE_INEN,
PORTE_FER registers
*/


#define PE0 0x0001
#define PE1 0x0002
#define PE2 0x0004
#define PE3 0x0008
#define PE4 0x0010
#define PE5 0x0020
#define PE6 0x0040
#define PE7 0x0080
#define PE8 0x0100
#define PE9 0x0200
#define PE10 0x0400
#define PE11 0x0800
#define PE12 0x1000
#define PE13 0x2000
#define PE14 0x4000
#define PE15 0x8000

/* PORT F Bit Definitions for the registers
PORTF, PORTF_SET, PORTF_CLEAR,
PORTF_DIR_SET, PORTF_DIR_CLEAR, PORTF_INEN,
PORTF_FER registers
*/


#define PF0 0x0001
#define PF1 0x0002
#define PF2 0x0004
#define PF3 0x0008
#define PF4 0x0010
#define PF5 0x0020
#define PF6 0x0040
#define PF7 0x0080
#define PF8 0x0100
#define PF9 0x0200
#define PF10 0x0400
#define PF11 0x0800
#define PF12 0x1000
#define PF13 0x2000
#define PF14 0x4000
#define PF15 0x8000

/* PORT G Bit Definitions for the registers
PORTG, PORTG_SET, PORTG_CLEAR,
PORTG_DIR_SET, PORTG_DIR_CLEAR, PORTG_INEN,
PORTG_FER registers
*/


#define PG0 0x0001
#define PG1 0x0002
#define PG2 0x0004
#define PG3 0x0008
#define PG4 0x0010
#define PG5 0x0020
#define PG6 0x0040
#define PG7 0x0080
#define PG8 0x0100
#define PG9 0x0200
#define PG10 0x0400
#define PG11 0x0800
#define PG12 0x1000
#define PG13 0x2000
#define PG14 0x4000
#define PG15 0x8000

/* PORT H Bit Definitions for the registers
PORTH, PORTH_SET, PORTH_CLEAR,
PORTH_DIR_SET, PORTH_DIR_CLEAR, PORTH_INEN,
PORTH_FER registers
*/


#define PH0 0x0001
#define PH1 0x0002
#define PH2 0x0004
#define PH3 0x0008
#define PH4 0x0010
#define PH5 0x0020
#define PH6 0x0040
#define PH7 0x0080
#define PH8 0x0100
#define PH9 0x0200
#define PH10 0x0400
#define PH11 0x0800
#define PH12 0x1000
#define PH13 0x2000


/* PORT I Bit Definitions for the registers
PORTI, PORTI_SET, PORTI_CLEAR,
PORTI_DIR_SET, PORTI_DIR_CLEAR, PORTI_INEN,
PORTI_FER registers
*/


#define PI0 0x0001
#define PI1 0x0002
#define PI2 0x0004
#define PI3 0x0008
#define PI4 0x0010
#define PI5 0x0020
#define PI6 0x0040
#define PI7 0x0080
#define PI8 0x0100
#define PI9 0x0200
#define PI10 0x0400
#define PI11 0x0800
#define PI12 0x1000
#define PI13 0x2000
#define PI14 0x4000
#define PI15 0x8000

/* PORT J Bit Definitions for the registers
PORTJ, PORTJ_SET, PORTJ_CLEAR,
PORTJ_DIR_SET, PORTJ_DIR_CLEAR, PORTJ_INEN,
PORTJ_FER registers
*/


#define PJ0 0x0001
#define PJ1 0x0002
#define PJ2 0x0004
#define PJ3 0x0008
#define PJ4 0x0010
#define PJ5 0x0020
#define PJ6 0x0040
#define PJ7 0x0080
#define PJ8 0x0100
#define PJ9 0x0200
#define PJ10 0x0400
#define PJ11 0x0800
#define PJ12 0x1000
#define PJ13 0x2000


/* Port Muxing Bit Fields for PORTx_MUX Registers */

#define MUX0 0x00000003
#define MUX0_0 0x00000000
#define MUX0_1 0x00000001
#define MUX0_2 0x00000002
#define MUX0_3 0x00000003

#define MUX1 0x0000000C
#define MUX1_0 0x00000000
#define MUX1_1 0x00000004
#define MUX1_2 0x00000008
#define MUX1_3 0x0000000C

#define MUX2 0x00000030
#define MUX2_0 0x00000000
#define MUX2_1 0x00000010
#define MUX2_2 0x00000020
#define MUX2_3 0x00000030

#define MUX3 0x000000C0
#define MUX3_0 0x00000000
#define MUX3_1 0x00000040
#define MUX3_2 0x00000080
#define MUX3_3 0x000000C0

#define MUX4 0x00000300
#define MUX4_0 0x00000000
#define MUX4_1 0x00000100
#define MUX4_2 0x00000200
#define MUX4_3 0x00000300

#define MUX5 0x00000C00
#define MUX5_0 0x00000000
#define MUX5_1 0x00000400
#define MUX5_2 0x00000800
#define MUX5_3 0x00000C00

#define MUX6 0x00003000
#define MUX6_0 0x00000000
#define MUX6_1 0x00001000
#define MUX6_2 0x00002000
#define MUX6_3 0x00003000

#define MUX7 0x0000C000
#define MUX7_0 0x00000000
#define MUX7_1 0x00004000
#define MUX7_2 0x00008000
#define MUX7_3 0x0000C000

#define MUX8 0x00030000
#define MUX8_0 0x00000000
#define MUX8_1 0x00010000
#define MUX8_2 0x00020000
#define MUX8_3 0x00030000

#define MUX9 0x000C0000
#define MUX9_0 0x00000000
#define MUX9_1 0x00040000
#define MUX9_2 0x00080000
#define MUX9_3 0x000C0000

#define MUX10 0x00300000
#define MUX10_0 0x00000000
#define MUX10_1 0x00100000
#define MUX10_2 0x00200000
#define MUX10_3 0x00300000

#define MUX11 0x00C00000
#define MUX11_0 0x00000000
#define MUX11_1 0x00400000
#define MUX11_2 0x00800000
#define MUX11_3 0x00C00000

#define MUX12 0x03000000
#define MUX12_0 0x00000000
#define MUX12_1 0x01000000
#define MUX12_2 0x02000000
#define MUX12_3 0x03000000

#define MUX13 0x0C000000
#define MUX13_0 0x00000000
#define MUX13_1 0x04000000
#define MUX13_2 0x08000000
#define MUX13_3 0x0C000000

#define MUX14 0x30000000
#define MUX14_0 0x00000000
#define MUX14_1 0x10000000
#define MUX14_2 0x20000000
#define MUX14_3 0x30000000

#define MUX15 0xC0000000
#define MUX15_0 0x00000000
#define MUX15_1 0x40000000
#define MUX15_2 0x80000000
#define MUX15_3 0xC0000000

#define MUX(b15,b14,b13,b12,b11,b10,b9,b8,b7,b6,b5,b4,b3,b2,b1,b0) \
    ((((b15)&3) << 30) | \
     (((b14)&3) << 28) | \
     (((b13)&3) << 26) | \
     (((b12)&3) << 24) | \
     (((b11)&3) << 22) | \
     (((b10)&3) << 20) | \
     (((b9) &3) << 18) | \
     (((b8) &3) << 16) | \
     (((b7) &3) << 14) | \
     (((b6) &3) << 12) | \
     (((b5) &3) << 10) | \
     (((b4) &3) << 8)  | \
     (((b3) &3) << 6)  | \
     (((b2) &3) << 4)  | \
     (((b1) &3) << 2)  | \
     (((b0) &3)))

/* Bit fields for PINT0_ASSIGN and PINT1_ASSIGN registers */

#define B0MAP 0x000000FF     /* Byte 0 Lower Half Port Mapping */
#define B0MAP_PAL 0x00000000 /* Map Port A Low to Byte 0 */
#define B0MAP_PBL 0x00000001 /* Map Port B Low to Byte 0 */
#define B1MAP 0x0000FF00     /* Byte 1 Upper Half Port Mapping */
#define B1MAP_PAH 0x00000000 /* Map Port A High to Byte 1 */
#define B1MAP_PBH 0x00000100 /* Map Port B High to Byte 1 */
#define B2MAP 0x00FF0000     /* Byte 2 Lower Half Port Mapping */
#define B2MAP_PAL 0x00000000 /* Map Port A Low to Byte 2 */
#define B2MAP_PBL 0x00010000 /* Map Port B Low to Byte 2 */
#define B3MAP 0xFF000000     /* Byte 3 Upper Half Port Mapping */
#define B3MAP_PAH 0x00000000 /* Map Port A High to Byte 3 */
#define B3MAP_PBH 0x01000000 /* Map Port B High to Byte 3 */

/* Bit fields for PINT2_ASSIGN and PINT3_ASSIGN registers */

#define B0MAP_PCL 0x00000000 /* Map Port C Low to Byte 0 */
#define B0MAP_PDL 0x00000001 /* Map Port D Low to Byte 0 */
#define B0MAP_PEL 0x00000002 /* Map Port E Low to Byte 0 */
#define B0MAP_PFL 0x00000003 /* Map Port F Low to Byte 0 */
#define B0MAP_PGL 0x00000004 /* Map Port G Low to Byte 0 */
#define B0MAP_PHL 0x00000005 /* Map Port H Low to Byte 0 */
#define B0MAP_PIL 0x00000006 /* Map Port I Low to Byte 0 */
#define B0MAP_PJL 0x00000007 /* Map Port J Low to Byte 0 */

#define B1MAP_PCH 0x00000000 /* Map Port C High to Byte 1 */
#define B1MAP_PDH 0x00000100 /* Map Port D High to Byte 1 */
#define B1MAP_PEH 0x00000200 /* Map Port E High to Byte 1 */
#define B1MAP_PFH 0x00000300 /* Map Port F High to Byte 1 */
#define B1MAP_PGH 0x00000400 /* Map Port G High to Byte 1 */
#define B1MAP_PHH 0x00000500 /* Map Port H High to Byte 1 */
#define B1MAP_PIH 0x00000600 /* Map Port I High to Byte 1 */
#define B1MAP_PJH 0x00000700 /* Map Port J High to Byte 1 */

#define B2MAP_PCL 0x00000000 /* Map Port C Low to Byte 2 */
#define B2MAP_PDL 0x00010000 /* Map Port D Low to Byte 2 */
#define B2MAP_PEL 0x00020000 /* Map Port E Low to Byte 2 */
#define B2MAP_PFL 0x00030000 /* Map Port F Low to Byte 2 */
#define B2MAP_PGL 0x00040000 /* Map Port G Low to Byte 2 */
#define B2MAP_PHL 0x00050000 /* Map Port H Low to Byte 2 */
#define B2MAP_PIL 0x00060000 /* Map Port I Low to Byte 2 */
#define B2MAP_PJL 0x00070000 /* Map Port J Low to Byte 2 */

#define B3MAP_PCH 0x00000000 /* Map Port C High to Byte 3 */
#define B3MAP_PDH 0x01000000 /* Map Port D High to Byte 3 */
#define B3MAP_PEH 0x02000000 /* Map Port E High to Byte 3 */
#define B3MAP_PFH 0x03000000 /* Map Port F High to Byte 3 */
#define B3MAP_PGH 0x04000000 /* Map Port G High to Byte 3 */
#define B3MAP_PHH 0x05000000 /* Map Port H High to Byte 3 */
#define B3MAP_PIH 0x06000000 /* Map Port I High to Byte 3 */
#define B3MAP_PJH 0x07000000 /* Map Port J High to Byte 3 */


/* for legacy compatibility */

#define EBIU_AMCBCTL0 EBIU_AMBCTL0
#define EBIU_AMCBCTL1 EBIU_AMBCTL1
#define PINT0_IRQ PINT0_REQUEST
#define PINT1_IRQ PINT1_REQUEST
#define PINT2_IRQ PINT2_REQUEST
#define PINT3_IRQ PINT3_REQUEST

#endif /* _DEF_BF54X_H */
