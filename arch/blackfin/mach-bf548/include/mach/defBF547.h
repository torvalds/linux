/*
 * Copyright 2008-2010 Analog Devices Inc.
 *
 * Licensed under the ADI BSD license or the GPL-2 (or later)
 */

#ifndef _DEF_BF547_H
#define _DEF_BF547_H

/* Include defBF54x_base.h for the set of #defines that are common to all ADSP-BF54x processors */
#include "defBF54x_base.h"

/* The following are the #defines needed by ADSP-BF547 that are not in the common header */

/* Timer Registers */

#define                    TIMER8_CONFIG  0xffc00600   /* Timer 8 Configuration Register */
#define                   TIMER8_COUNTER  0xffc00604   /* Timer 8 Counter Register */
#define                    TIMER8_PERIOD  0xffc00608   /* Timer 8 Period Register */
#define                     TIMER8_WIDTH  0xffc0060c   /* Timer 8 Width Register */
#define                    TIMER9_CONFIG  0xffc00610   /* Timer 9 Configuration Register */
#define                   TIMER9_COUNTER  0xffc00614   /* Timer 9 Counter Register */
#define                    TIMER9_PERIOD  0xffc00618   /* Timer 9 Period Register */
#define                     TIMER9_WIDTH  0xffc0061c   /* Timer 9 Width Register */
#define                   TIMER10_CONFIG  0xffc00620   /* Timer 10 Configuration Register */
#define                  TIMER10_COUNTER  0xffc00624   /* Timer 10 Counter Register */
#define                   TIMER10_PERIOD  0xffc00628   /* Timer 10 Period Register */
#define                    TIMER10_WIDTH  0xffc0062c   /* Timer 10 Width Register */

/* Timer Group of 3 Registers */

#define                    TIMER_ENABLE1  0xffc00640   /* Timer Group of 3 Enable Register */
#define                   TIMER_DISABLE1  0xffc00644   /* Timer Group of 3 Disable Register */
#define                    TIMER_STATUS1  0xffc00648   /* Timer Group of 3 Status Register */

/* SPORT0 Registers */

#define                      SPORT0_TCR1  0xffc00800   /* SPORT0 Transmit Configuration 1 Register */
#define                      SPORT0_TCR2  0xffc00804   /* SPORT0 Transmit Configuration 2 Register */
#define                   SPORT0_TCLKDIV  0xffc00808   /* SPORT0 Transmit Serial Clock Divider Register */
#define                    SPORT0_TFSDIV  0xffc0080c   /* SPORT0 Transmit Frame Sync Divider Register */
#define                        SPORT0_TX  0xffc00810   /* SPORT0 Transmit Data Register */
#define                        SPORT0_RX  0xffc00818   /* SPORT0 Receive Data Register */
#define                      SPORT0_RCR1  0xffc00820   /* SPORT0 Receive Configuration 1 Register */
#define                      SPORT0_RCR2  0xffc00824   /* SPORT0 Receive Configuration 2 Register */
#define                   SPORT0_RCLKDIV  0xffc00828   /* SPORT0 Receive Serial Clock Divider Register */
#define                    SPORT0_RFSDIV  0xffc0082c   /* SPORT0 Receive Frame Sync Divider Register */
#define                      SPORT0_STAT  0xffc00830   /* SPORT0 Status Register */
#define                      SPORT0_CHNL  0xffc00834   /* SPORT0 Current Channel Register */
#define                     SPORT0_MCMC1  0xffc00838   /* SPORT0 Multi channel Configuration Register 1 */
#define                     SPORT0_MCMC2  0xffc0083c   /* SPORT0 Multi channel Configuration Register 2 */
#define                     SPORT0_MTCS0  0xffc00840   /* SPORT0 Multi channel Transmit Select Register 0 */
#define                     SPORT0_MTCS1  0xffc00844   /* SPORT0 Multi channel Transmit Select Register 1 */
#define                     SPORT0_MTCS2  0xffc00848   /* SPORT0 Multi channel Transmit Select Register 2 */
#define                     SPORT0_MTCS3  0xffc0084c   /* SPORT0 Multi channel Transmit Select Register 3 */
#define                     SPORT0_MRCS0  0xffc00850   /* SPORT0 Multi channel Receive Select Register 0 */
#define                     SPORT0_MRCS1  0xffc00854   /* SPORT0 Multi channel Receive Select Register 1 */
#define                     SPORT0_MRCS2  0xffc00858   /* SPORT0 Multi channel Receive Select Register 2 */
#define                     SPORT0_MRCS3  0xffc0085c   /* SPORT0 Multi channel Receive Select Register 3 */

/* EPPI0 Registers */

#define                     EPPI0_STATUS  0xffc01000   /* EPPI0 Status Register */
#define                     EPPI0_HCOUNT  0xffc01004   /* EPPI0 Horizontal Transfer Count Register */
#define                     EPPI0_HDELAY  0xffc01008   /* EPPI0 Horizontal Delay Count Register */
#define                     EPPI0_VCOUNT  0xffc0100c   /* EPPI0 Vertical Transfer Count Register */
#define                     EPPI0_VDELAY  0xffc01010   /* EPPI0 Vertical Delay Count Register */
#define                      EPPI0_FRAME  0xffc01014   /* EPPI0 Lines per Frame Register */
#define                       EPPI0_LINE  0xffc01018   /* EPPI0 Samples per Line Register */
#define                     EPPI0_CLKDIV  0xffc0101c   /* EPPI0 Clock Divide Register */
#define                    EPPI0_CONTROL  0xffc01020   /* EPPI0 Control Register */
#define                   EPPI0_FS1W_HBL  0xffc01024   /* EPPI0 FS1 Width Register / EPPI0 Horizontal Blanking Samples Per Line Register */
#define                  EPPI0_FS1P_AVPL  0xffc01028   /* EPPI0 FS1 Period Register / EPPI0 Active Video Samples Per Line Register */
#define                   EPPI0_FS2W_LVB  0xffc0102c   /* EPPI0 FS2 Width Register / EPPI0 Lines of Vertical Blanking Register */
#define                  EPPI0_FS2P_LAVF  0xffc01030   /* EPPI0 FS2 Period Register/ EPPI0 Lines of Active Video Per Field Register */
#define                       EPPI0_CLIP  0xffc01034   /* EPPI0 Clipping Register */

/* UART2 Registers */

#define                        UART2_DLL  0xffc02100   /* Divisor Latch Low Byte */
#define                        UART2_DLH  0xffc02104   /* Divisor Latch High Byte */
#define                       UART2_GCTL  0xffc02108   /* Global Control Register */
#define                        UART2_LCR  0xffc0210c   /* Line Control Register */
#define                        UART2_MCR  0xffc02110   /* Modem Control Register */
#define                        UART2_LSR  0xffc02114   /* Line Status Register */
#define                        UART2_MSR  0xffc02118   /* Modem Status Register */
#define                        UART2_SCR  0xffc0211c   /* Scratch Register */
#define                    UART2_IER_SET  0xffc02120   /* Interrupt Enable Register Set */
#define                  UART2_IER_CLEAR  0xffc02124   /* Interrupt Enable Register Clear */
#define                        UART2_RBR  0xffc0212c   /* Receive Buffer Register */

/* Two Wire Interface Registers (TWI1) */

#define                     TWI1_REGBASE  0xffc02200
#define                      TWI1_CLKDIV  0xffc02200   /* Clock Divider Register */
#define                     TWI1_CONTROL  0xffc02204   /* TWI Control Register */
#define                   TWI1_SLAVE_CTL  0xffc02208   /* TWI Slave Mode Control Register */
#define                  TWI1_SLAVE_STAT  0xffc0220c   /* TWI Slave Mode Status Register */
#define                  TWI1_SLAVE_ADDR  0xffc02210   /* TWI Slave Mode Address Register */
#define                  TWI1_MASTER_CTL  0xffc02214   /* TWI Master Mode Control Register */
#define                 TWI1_MASTER_STAT  0xffc02218   /* TWI Master Mode Status Register */
#define                 TWI1_MASTER_ADDR  0xffc0221c   /* TWI Master Mode Address Register */
#define                    TWI1_INT_STAT  0xffc02220   /* TWI Interrupt Status Register */
#define                    TWI1_INT_MASK  0xffc02224   /* TWI Interrupt Mask Register */
#define                    TWI1_FIFO_CTL  0xffc02228   /* TWI FIFO Control Register */
#define                   TWI1_FIFO_STAT  0xffc0222c   /* TWI FIFO Status Register */
#define                   TWI1_XMT_DATA8  0xffc02280   /* TWI FIFO Transmit Data Single Byte Register */
#define                  TWI1_XMT_DATA16  0xffc02284   /* TWI FIFO Transmit Data Double Byte Register */
#define                   TWI1_RCV_DATA8  0xffc02288   /* TWI FIFO Receive Data Single Byte Register */
#define                  TWI1_RCV_DATA16  0xffc0228c   /* TWI FIFO Receive Data Double Byte Register */

/* SPI2  Registers */

#define                     SPI2_REGBASE  0xffc02400
#define                         SPI2_CTL  0xffc02400   /* SPI2 Control Register */
#define                         SPI2_FLG  0xffc02404   /* SPI2 Flag Register */
#define                        SPI2_STAT  0xffc02408   /* SPI2 Status Register */
#define                        SPI2_TDBR  0xffc0240c   /* SPI2 Transmit Data Buffer Register */
#define                        SPI2_RDBR  0xffc02410   /* SPI2 Receive Data Buffer Register */
#define                        SPI2_BAUD  0xffc02414   /* SPI2 Baud Rate Register */
#define                      SPI2_SHADOW  0xffc02418   /* SPI2 Receive Data Buffer Shadow Register */

/* ATAPI Registers */

#define                    ATAPI_CONTROL  0xffc03800   /* ATAPI Control Register */
#define                     ATAPI_STATUS  0xffc03804   /* ATAPI Status Register */
#define                   ATAPI_DEV_ADDR  0xffc03808   /* ATAPI Device Register Address */
#define                  ATAPI_DEV_TXBUF  0xffc0380c   /* ATAPI Device Register Write Data */
#define                  ATAPI_DEV_RXBUF  0xffc03810   /* ATAPI Device Register Read Data */
#define                   ATAPI_INT_MASK  0xffc03814   /* ATAPI Interrupt Mask Register */
#define                 ATAPI_INT_STATUS  0xffc03818   /* ATAPI Interrupt Status Register */
#define                   ATAPI_XFER_LEN  0xffc0381c   /* ATAPI Length of Transfer */
#define                ATAPI_LINE_STATUS  0xffc03820   /* ATAPI Line Status */
#define                   ATAPI_SM_STATE  0xffc03824   /* ATAPI State Machine Status */
#define                  ATAPI_TERMINATE  0xffc03828   /* ATAPI Host Terminate */
#define                 ATAPI_PIO_TFRCNT  0xffc0382c   /* ATAPI PIO mode transfer count */
#define                 ATAPI_DMA_TFRCNT  0xffc03830   /* ATAPI DMA mode transfer count */
#define               ATAPI_UMAIN_TFRCNT  0xffc03834   /* ATAPI UDMAIN transfer count */
#define             ATAPI_UDMAOUT_TFRCNT  0xffc03838   /* ATAPI UDMAOUT transfer count */
#define                  ATAPI_REG_TIM_0  0xffc03840   /* ATAPI Register Transfer Timing 0 */
#define                  ATAPI_PIO_TIM_0  0xffc03844   /* ATAPI PIO Timing 0 Register */
#define                  ATAPI_PIO_TIM_1  0xffc03848   /* ATAPI PIO Timing 1 Register */
#define                ATAPI_MULTI_TIM_0  0xffc03850   /* ATAPI Multi-DMA Timing 0 Register */
#define                ATAPI_MULTI_TIM_1  0xffc03854   /* ATAPI Multi-DMA Timing 1 Register */
#define                ATAPI_MULTI_TIM_2  0xffc03858   /* ATAPI Multi-DMA Timing 2 Register */
#define                ATAPI_ULTRA_TIM_0  0xffc03860   /* ATAPI Ultra-DMA Timing 0 Register */
#define                ATAPI_ULTRA_TIM_1  0xffc03864   /* ATAPI Ultra-DMA Timing 1 Register */
#define                ATAPI_ULTRA_TIM_2  0xffc03868   /* ATAPI Ultra-DMA Timing 2 Register */
#define                ATAPI_ULTRA_TIM_3  0xffc0386c   /* ATAPI Ultra-DMA Timing 3 Register */

/* SDH Registers */

#define                      SDH_PWR_CTL  0xffc03900   /* SDH Power Control */
#define                      SDH_CLK_CTL  0xffc03904   /* SDH Clock Control */
#define                     SDH_ARGUMENT  0xffc03908   /* SDH Argument */
#define                      SDH_COMMAND  0xffc0390c   /* SDH Command */
#define                     SDH_RESP_CMD  0xffc03910   /* SDH Response Command */
#define                    SDH_RESPONSE0  0xffc03914   /* SDH Response0 */
#define                    SDH_RESPONSE1  0xffc03918   /* SDH Response1 */
#define                    SDH_RESPONSE2  0xffc0391c   /* SDH Response2 */
#define                    SDH_RESPONSE3  0xffc03920   /* SDH Response3 */
#define                   SDH_DATA_TIMER  0xffc03924   /* SDH Data Timer */
#define                    SDH_DATA_LGTH  0xffc03928   /* SDH Data Length */
#define                     SDH_DATA_CTL  0xffc0392c   /* SDH Data Control */
#define                     SDH_DATA_CNT  0xffc03930   /* SDH Data Counter */
#define                       SDH_STATUS  0xffc03934   /* SDH Status */
#define                   SDH_STATUS_CLR  0xffc03938   /* SDH Status Clear */
#define                        SDH_MASK0  0xffc0393c   /* SDH Interrupt0 Mask */
#define                        SDH_MASK1  0xffc03940   /* SDH Interrupt1 Mask */
#define                     SDH_FIFO_CNT  0xffc03948   /* SDH FIFO Counter */
#define                         SDH_FIFO  0xffc03980   /* SDH Data FIFO */
#define                     SDH_E_STATUS  0xffc039c0   /* SDH Exception Status */
#define                       SDH_E_MASK  0xffc039c4   /* SDH Exception Mask */
#define                          SDH_CFG  0xffc039c8   /* SDH Configuration */
#define                   SDH_RD_WAIT_EN  0xffc039cc   /* SDH Read Wait Enable */
#define                         SDH_PID0  0xffc039d0   /* SDH Peripheral Identification0 */
#define                         SDH_PID1  0xffc039d4   /* SDH Peripheral Identification1 */
#define                         SDH_PID2  0xffc039d8   /* SDH Peripheral Identification2 */
#define                         SDH_PID3  0xffc039dc   /* SDH Peripheral Identification3 */
#define                         SDH_PID4  0xffc039e0   /* SDH Peripheral Identification4 */
#define                         SDH_PID5  0xffc039e4   /* SDH Peripheral Identification5 */
#define                         SDH_PID6  0xffc039e8   /* SDH Peripheral Identification6 */
#define                         SDH_PID7  0xffc039ec   /* SDH Peripheral Identification7 */

/* HOST Port Registers */

#define                     HOST_CONTROL  0xffc03a00   /* HOST Control Register */
#define                      HOST_STATUS  0xffc03a04   /* HOST Status Register */
#define                     HOST_TIMEOUT  0xffc03a08   /* HOST Acknowledge Mode Timeout Register */

/* USB Control Registers */

#define                        USB_FADDR  0xffc03c00   /* Function address register */
#define                        USB_POWER  0xffc03c04   /* Power management register */
#define                       USB_INTRTX  0xffc03c08   /* Interrupt register for endpoint 0 and Tx endpoint 1 to 7 */
#define                       USB_INTRRX  0xffc03c0c   /* Interrupt register for Rx endpoints 1 to 7 */
#define                      USB_INTRTXE  0xffc03c10   /* Interrupt enable register for IntrTx */
#define                      USB_INTRRXE  0xffc03c14   /* Interrupt enable register for IntrRx */
#define                      USB_INTRUSB  0xffc03c18   /* Interrupt register for common USB interrupts */
#define                     USB_INTRUSBE  0xffc03c1c   /* Interrupt enable register for IntrUSB */
#define                        USB_FRAME  0xffc03c20   /* USB frame number */
#define                        USB_INDEX  0xffc03c24   /* Index register for selecting the indexed endpoint registers */
#define                     USB_TESTMODE  0xffc03c28   /* Enabled USB 20 test modes */
#define                     USB_GLOBINTR  0xffc03c2c   /* Global Interrupt Mask register and Wakeup Exception Interrupt */
#define                   USB_GLOBAL_CTL  0xffc03c30   /* Global Clock Control for the core */

/* USB Packet Control Registers */

#define                USB_TX_MAX_PACKET  0xffc03c40   /* Maximum packet size for Host Tx endpoint */
#define                         USB_CSR0  0xffc03c44   /* Control Status register for endpoint 0 and Control Status register for Host Tx endpoint */
#define                        USB_TXCSR  0xffc03c44   /* Control Status register for endpoint 0 and Control Status register for Host Tx endpoint */
#define                USB_RX_MAX_PACKET  0xffc03c48   /* Maximum packet size for Host Rx endpoint */
#define                        USB_RXCSR  0xffc03c4c   /* Control Status register for Host Rx endpoint */
#define                       USB_COUNT0  0xffc03c50   /* Number of bytes received in endpoint 0 FIFO and Number of bytes received in Host Tx endpoint */
#define                      USB_RXCOUNT  0xffc03c50   /* Number of bytes received in endpoint 0 FIFO and Number of bytes received in Host Tx endpoint */
#define                       USB_TXTYPE  0xffc03c54   /* Sets the transaction protocol and peripheral endpoint number for the Host Tx endpoint */
#define                    USB_NAKLIMIT0  0xffc03c58   /* Sets the NAK response timeout on Endpoint 0 and on Bulk transfers for Host Tx endpoint */
#define                   USB_TXINTERVAL  0xffc03c58   /* Sets the NAK response timeout on Endpoint 0 and on Bulk transfers for Host Tx endpoint */
#define                       USB_RXTYPE  0xffc03c5c   /* Sets the transaction protocol and peripheral endpoint number for the Host Rx endpoint */
#define                   USB_RXINTERVAL  0xffc03c60   /* Sets the polling interval for Interrupt and Isochronous transfers or the NAK response timeout on Bulk transfers */
#define                      USB_TXCOUNT  0xffc03c68   /* Number of bytes to be written to the selected endpoint Tx FIFO */

/* USB Endpoint FIFO Registers */

#define                     USB_EP0_FIFO  0xffc03c80   /* Endpoint 0 FIFO */
#define                     USB_EP1_FIFO  0xffc03c88   /* Endpoint 1 FIFO */
#define                     USB_EP2_FIFO  0xffc03c90   /* Endpoint 2 FIFO */
#define                     USB_EP3_FIFO  0xffc03c98   /* Endpoint 3 FIFO */
#define                     USB_EP4_FIFO  0xffc03ca0   /* Endpoint 4 FIFO */
#define                     USB_EP5_FIFO  0xffc03ca8   /* Endpoint 5 FIFO */
#define                     USB_EP6_FIFO  0xffc03cb0   /* Endpoint 6 FIFO */
#define                     USB_EP7_FIFO  0xffc03cb8   /* Endpoint 7 FIFO */

/* USB OTG Control Registers */

#define                  USB_OTG_DEV_CTL  0xffc03d00   /* OTG Device Control Register */
#define                 USB_OTG_VBUS_IRQ  0xffc03d04   /* OTG VBUS Control Interrupts */
#define                USB_OTG_VBUS_MASK  0xffc03d08   /* VBUS Control Interrupt Enable */

/* USB Phy Control Registers */

#define                     USB_LINKINFO  0xffc03d48   /* Enables programming of some PHY-side delays */
#define                        USB_VPLEN  0xffc03d4c   /* Determines duration of VBUS pulse for VBUS charging */
#define                      USB_HS_EOF1  0xffc03d50   /* Time buffer for High-Speed transactions */
#define                      USB_FS_EOF1  0xffc03d54   /* Time buffer for Full-Speed transactions */
#define                      USB_LS_EOF1  0xffc03d58   /* Time buffer for Low-Speed transactions */

/* (APHY_CNTRL is for ADI usage only) */

#define                   USB_APHY_CNTRL  0xffc03de0   /* Register that increases visibility of Analog PHY */

/* (APHY_CALIB is for ADI usage only) */

#define                   USB_APHY_CALIB  0xffc03de4   /* Register used to set some calibration values */
#define                  USB_APHY_CNTRL2  0xffc03de8   /* Register used to prevent re-enumeration once Moab goes into hibernate mode */

/* (PHY_TEST is for ADI usage only) */

#define                     USB_PHY_TEST  0xffc03dec   /* Used for reducing simulation time and simplifies FIFO testability */
#define                  USB_PLLOSC_CTRL  0xffc03df0   /* Used to program different parameters for USB PLL and Oscillator */
#define                   USB_SRP_CLKDIV  0xffc03df4   /* Used to program clock divide value for the clock fed to the SRP detection logic */

/* USB Endpoint 0 Control Registers */

#define                USB_EP_NI0_TXMAXP  0xffc03e00   /* Maximum packet size for Host Tx endpoint0 */
#define                 USB_EP_NI0_TXCSR  0xffc03e04   /* Control Status register for endpoint 0 */
#define                USB_EP_NI0_RXMAXP  0xffc03e08   /* Maximum packet size for Host Rx endpoint0 */
#define                 USB_EP_NI0_RXCSR  0xffc03e0c   /* Control Status register for Host Rx endpoint0 */
#define               USB_EP_NI0_RXCOUNT  0xffc03e10   /* Number of bytes received in endpoint 0 FIFO */
#define                USB_EP_NI0_TXTYPE  0xffc03e14   /* Sets the transaction protocol and peripheral endpoint number for the Host Tx endpoint0 */
#define            USB_EP_NI0_TXINTERVAL  0xffc03e18   /* Sets the NAK response timeout on Endpoint 0 */
#define                USB_EP_NI0_RXTYPE  0xffc03e1c   /* Sets the transaction protocol and peripheral endpoint number for the Host Rx endpoint0 */
#define            USB_EP_NI0_RXINTERVAL  0xffc03e20   /* Sets the polling interval for Interrupt/Isochronous transfers or the NAK response timeout on Bulk transfers for Host Rx endpoint0 */
#define               USB_EP_NI0_TXCOUNT  0xffc03e28   /* Number of bytes to be written to the endpoint0 Tx FIFO */

/* USB Endpoint 1 Control Registers */

#define                USB_EP_NI1_TXMAXP  0xffc03e40   /* Maximum packet size for Host Tx endpoint1 */
#define                 USB_EP_NI1_TXCSR  0xffc03e44   /* Control Status register for endpoint1 */
#define                USB_EP_NI1_RXMAXP  0xffc03e48   /* Maximum packet size for Host Rx endpoint1 */
#define                 USB_EP_NI1_RXCSR  0xffc03e4c   /* Control Status register for Host Rx endpoint1 */
#define               USB_EP_NI1_RXCOUNT  0xffc03e50   /* Number of bytes received in endpoint1 FIFO */
#define                USB_EP_NI1_TXTYPE  0xffc03e54   /* Sets the transaction protocol and peripheral endpoint number for the Host Tx endpoint1 */
#define            USB_EP_NI1_TXINTERVAL  0xffc03e58   /* Sets the NAK response timeout on Endpoint1 */
#define                USB_EP_NI1_RXTYPE  0xffc03e5c   /* Sets the transaction protocol and peripheral endpoint number for the Host Rx endpoint1 */
#define            USB_EP_NI1_RXINTERVAL  0xffc03e60   /* Sets the polling interval for Interrupt/Isochronous transfers or the NAK response timeout on Bulk transfers for Host Rx endpoint1 */
#define               USB_EP_NI1_TXCOUNT  0xffc03e68   /* Number of bytes to be written to the+H102 endpoint1 Tx FIFO */

/* USB Endpoint 2 Control Registers */

#define                USB_EP_NI2_TXMAXP  0xffc03e80   /* Maximum packet size for Host Tx endpoint2 */
#define                 USB_EP_NI2_TXCSR  0xffc03e84   /* Control Status register for endpoint2 */
#define                USB_EP_NI2_RXMAXP  0xffc03e88   /* Maximum packet size for Host Rx endpoint2 */
#define                 USB_EP_NI2_RXCSR  0xffc03e8c   /* Control Status register for Host Rx endpoint2 */
#define               USB_EP_NI2_RXCOUNT  0xffc03e90   /* Number of bytes received in endpoint2 FIFO */
#define                USB_EP_NI2_TXTYPE  0xffc03e94   /* Sets the transaction protocol and peripheral endpoint number for the Host Tx endpoint2 */
#define            USB_EP_NI2_TXINTERVAL  0xffc03e98   /* Sets the NAK response timeout on Endpoint2 */
#define                USB_EP_NI2_RXTYPE  0xffc03e9c   /* Sets the transaction protocol and peripheral endpoint number for the Host Rx endpoint2 */
#define            USB_EP_NI2_RXINTERVAL  0xffc03ea0   /* Sets the polling interval for Interrupt/Isochronous transfers or the NAK response timeout on Bulk transfers for Host Rx endpoint2 */
#define               USB_EP_NI2_TXCOUNT  0xffc03ea8   /* Number of bytes to be written to the endpoint2 Tx FIFO */

/* USB Endpoint 3 Control Registers */

#define                USB_EP_NI3_TXMAXP  0xffc03ec0   /* Maximum packet size for Host Tx endpoint3 */
#define                 USB_EP_NI3_TXCSR  0xffc03ec4   /* Control Status register for endpoint3 */
#define                USB_EP_NI3_RXMAXP  0xffc03ec8   /* Maximum packet size for Host Rx endpoint3 */
#define                 USB_EP_NI3_RXCSR  0xffc03ecc   /* Control Status register for Host Rx endpoint3 */
#define               USB_EP_NI3_RXCOUNT  0xffc03ed0   /* Number of bytes received in endpoint3 FIFO */
#define                USB_EP_NI3_TXTYPE  0xffc03ed4   /* Sets the transaction protocol and peripheral endpoint number for the Host Tx endpoint3 */
#define            USB_EP_NI3_TXINTERVAL  0xffc03ed8   /* Sets the NAK response timeout on Endpoint3 */
#define                USB_EP_NI3_RXTYPE  0xffc03edc   /* Sets the transaction protocol and peripheral endpoint number for the Host Rx endpoint3 */
#define            USB_EP_NI3_RXINTERVAL  0xffc03ee0   /* Sets the polling interval for Interrupt/Isochronous transfers or the NAK response timeout on Bulk transfers for Host Rx endpoint3 */
#define               USB_EP_NI3_TXCOUNT  0xffc03ee8   /* Number of bytes to be written to the H124endpoint3 Tx FIFO */

/* USB Endpoint 4 Control Registers */

#define                USB_EP_NI4_TXMAXP  0xffc03f00   /* Maximum packet size for Host Tx endpoint4 */
#define                 USB_EP_NI4_TXCSR  0xffc03f04   /* Control Status register for endpoint4 */
#define                USB_EP_NI4_RXMAXP  0xffc03f08   /* Maximum packet size for Host Rx endpoint4 */
#define                 USB_EP_NI4_RXCSR  0xffc03f0c   /* Control Status register for Host Rx endpoint4 */
#define               USB_EP_NI4_RXCOUNT  0xffc03f10   /* Number of bytes received in endpoint4 FIFO */
#define                USB_EP_NI4_TXTYPE  0xffc03f14   /* Sets the transaction protocol and peripheral endpoint number for the Host Tx endpoint4 */
#define            USB_EP_NI4_TXINTERVAL  0xffc03f18   /* Sets the NAK response timeout on Endpoint4 */
#define                USB_EP_NI4_RXTYPE  0xffc03f1c   /* Sets the transaction protocol and peripheral endpoint number for the Host Rx endpoint4 */
#define            USB_EP_NI4_RXINTERVAL  0xffc03f20   /* Sets the polling interval for Interrupt/Isochronous transfers or the NAK response timeout on Bulk transfers for Host Rx endpoint4 */
#define               USB_EP_NI4_TXCOUNT  0xffc03f28   /* Number of bytes to be written to the endpoint4 Tx FIFO */

/* USB Endpoint 5 Control Registers */

#define                USB_EP_NI5_TXMAXP  0xffc03f40   /* Maximum packet size for Host Tx endpoint5 */
#define                 USB_EP_NI5_TXCSR  0xffc03f44   /* Control Status register for endpoint5 */
#define                USB_EP_NI5_RXMAXP  0xffc03f48   /* Maximum packet size for Host Rx endpoint5 */
#define                 USB_EP_NI5_RXCSR  0xffc03f4c   /* Control Status register for Host Rx endpoint5 */
#define               USB_EP_NI5_RXCOUNT  0xffc03f50   /* Number of bytes received in endpoint5 FIFO */
#define                USB_EP_NI5_TXTYPE  0xffc03f54   /* Sets the transaction protocol and peripheral endpoint number for the Host Tx endpoint5 */
#define            USB_EP_NI5_TXINTERVAL  0xffc03f58   /* Sets the NAK response timeout on Endpoint5 */
#define                USB_EP_NI5_RXTYPE  0xffc03f5c   /* Sets the transaction protocol and peripheral endpoint number for the Host Rx endpoint5 */
#define            USB_EP_NI5_RXINTERVAL  0xffc03f60   /* Sets the polling interval for Interrupt/Isochronous transfers or the NAK response timeout on Bulk transfers for Host Rx endpoint5 */
#define               USB_EP_NI5_TXCOUNT  0xffc03f68   /* Number of bytes to be written to the H145endpoint5 Tx FIFO */

/* USB Endpoint 6 Control Registers */

#define                USB_EP_NI6_TXMAXP  0xffc03f80   /* Maximum packet size for Host Tx endpoint6 */
#define                 USB_EP_NI6_TXCSR  0xffc03f84   /* Control Status register for endpoint6 */
#define                USB_EP_NI6_RXMAXP  0xffc03f88   /* Maximum packet size for Host Rx endpoint6 */
#define                 USB_EP_NI6_RXCSR  0xffc03f8c   /* Control Status register for Host Rx endpoint6 */
#define               USB_EP_NI6_RXCOUNT  0xffc03f90   /* Number of bytes received in endpoint6 FIFO */
#define                USB_EP_NI6_TXTYPE  0xffc03f94   /* Sets the transaction protocol and peripheral endpoint number for the Host Tx endpoint6 */
#define            USB_EP_NI6_TXINTERVAL  0xffc03f98   /* Sets the NAK response timeout on Endpoint6 */
#define                USB_EP_NI6_RXTYPE  0xffc03f9c   /* Sets the transaction protocol and peripheral endpoint number for the Host Rx endpoint6 */
#define            USB_EP_NI6_RXINTERVAL  0xffc03fa0   /* Sets the polling interval for Interrupt/Isochronous transfers or the NAK response timeout on Bulk transfers for Host Rx endpoint6 */
#define               USB_EP_NI6_TXCOUNT  0xffc03fa8   /* Number of bytes to be written to the endpoint6 Tx FIFO */

/* USB Endpoint 7 Control Registers */

#define                USB_EP_NI7_TXMAXP  0xffc03fc0   /* Maximum packet size for Host Tx endpoint7 */
#define                 USB_EP_NI7_TXCSR  0xffc03fc4   /* Control Status register for endpoint7 */
#define                USB_EP_NI7_RXMAXP  0xffc03fc8   /* Maximum packet size for Host Rx endpoint7 */
#define                 USB_EP_NI7_RXCSR  0xffc03fcc   /* Control Status register for Host Rx endpoint7 */
#define               USB_EP_NI7_RXCOUNT  0xffc03fd0   /* Number of bytes received in endpoint7 FIFO */
#define                USB_EP_NI7_TXTYPE  0xffc03fd4   /* Sets the transaction protocol and peripheral endpoint number for the Host Tx endpoint7 */
#define            USB_EP_NI7_TXINTERVAL  0xffc03fd8   /* Sets the NAK response timeout on Endpoint7 */
#define                USB_EP_NI7_RXTYPE  0xffc03fdc   /* Sets the transaction protocol and peripheral endpoint number for the Host Rx endpoint7 */
#define            USB_EP_NI7_RXINTERVAL  0xffc03fe0   /* Sets the polling interval for Interrupt/Isochronous transfers or the NAK response timeout on Bulk transfers for Host Rx endpoint7 */
#define               USB_EP_NI7_TXCOUNT  0xffc03fe8   /* Number of bytes to be written to the endpoint7 Tx FIFO */

#define                USB_DMA_INTERRUPT  0xffc04000   /* Indicates pending interrupts for the DMA channels */

/* USB Channel 0 Config Registers */

#define                  USB_DMA0CONTROL  0xffc04004   /* DMA master channel 0 configuration */
#define                  USB_DMA0ADDRLOW  0xffc04008   /* Lower 16-bits of memory source/destination address for DMA master channel 0 */
#define                 USB_DMA0ADDRHIGH  0xffc0400c   /* Upper 16-bits of memory source/destination address for DMA master channel 0 */
#define                 USB_DMA0COUNTLOW  0xffc04010   /* Lower 16-bits of byte count of DMA transfer for DMA master channel 0 */
#define                USB_DMA0COUNTHIGH  0xffc04014   /* Upper 16-bits of byte count of DMA transfer for DMA master channel 0 */

/* USB Channel 1 Config Registers */

#define                  USB_DMA1CONTROL  0xffc04024   /* DMA master channel 1 configuration */
#define                  USB_DMA1ADDRLOW  0xffc04028   /* Lower 16-bits of memory source/destination address for DMA master channel 1 */
#define                 USB_DMA1ADDRHIGH  0xffc0402c   /* Upper 16-bits of memory source/destination address for DMA master channel 1 */
#define                 USB_DMA1COUNTLOW  0xffc04030   /* Lower 16-bits of byte count of DMA transfer for DMA master channel 1 */
#define                USB_DMA1COUNTHIGH  0xffc04034   /* Upper 16-bits of byte count of DMA transfer for DMA master channel 1 */

/* USB Channel 2 Config Registers */

#define                  USB_DMA2CONTROL  0xffc04044   /* DMA master channel 2 configuration */
#define                  USB_DMA2ADDRLOW  0xffc04048   /* Lower 16-bits of memory source/destination address for DMA master channel 2 */
#define                 USB_DMA2ADDRHIGH  0xffc0404c   /* Upper 16-bits of memory source/destination address for DMA master channel 2 */
#define                 USB_DMA2COUNTLOW  0xffc04050   /* Lower 16-bits of byte count of DMA transfer for DMA master channel 2 */
#define                USB_DMA2COUNTHIGH  0xffc04054   /* Upper 16-bits of byte count of DMA transfer for DMA master channel 2 */

/* USB Channel 3 Config Registers */

#define                  USB_DMA3CONTROL  0xffc04064   /* DMA master channel 3 configuration */
#define                  USB_DMA3ADDRLOW  0xffc04068   /* Lower 16-bits of memory source/destination address for DMA master channel 3 */
#define                 USB_DMA3ADDRHIGH  0xffc0406c   /* Upper 16-bits of memory source/destination address for DMA master channel 3 */
#define                 USB_DMA3COUNTLOW  0xffc04070   /* Lower 16-bits of byte count of DMA transfer for DMA master channel 3 */
#define                USB_DMA3COUNTHIGH  0xffc04074   /* Upper 16-bits of byte count of DMA transfer for DMA master channel 3 */

/* USB Channel 4 Config Registers */

#define                  USB_DMA4CONTROL  0xffc04084   /* DMA master channel 4 configuration */
#define                  USB_DMA4ADDRLOW  0xffc04088   /* Lower 16-bits of memory source/destination address for DMA master channel 4 */
#define                 USB_DMA4ADDRHIGH  0xffc0408c   /* Upper 16-bits of memory source/destination address for DMA master channel 4 */
#define                 USB_DMA4COUNTLOW  0xffc04090   /* Lower 16-bits of byte count of DMA transfer for DMA master channel 4 */
#define                USB_DMA4COUNTHIGH  0xffc04094   /* Upper 16-bits of byte count of DMA transfer for DMA master channel 4 */

/* USB Channel 5 Config Registers */

#define                  USB_DMA5CONTROL  0xffc040a4   /* DMA master channel 5 configuration */
#define                  USB_DMA5ADDRLOW  0xffc040a8   /* Lower 16-bits of memory source/destination address for DMA master channel 5 */
#define                 USB_DMA5ADDRHIGH  0xffc040ac   /* Upper 16-bits of memory source/destination address for DMA master channel 5 */
#define                 USB_DMA5COUNTLOW  0xffc040b0   /* Lower 16-bits of byte count of DMA transfer for DMA master channel 5 */
#define                USB_DMA5COUNTHIGH  0xffc040b4   /* Upper 16-bits of byte count of DMA transfer for DMA master channel 5 */

/* USB Channel 6 Config Registers */

#define                  USB_DMA6CONTROL  0xffc040c4   /* DMA master channel 6 configuration */
#define                  USB_DMA6ADDRLOW  0xffc040c8   /* Lower 16-bits of memory source/destination address for DMA master channel 6 */
#define                 USB_DMA6ADDRHIGH  0xffc040cc   /* Upper 16-bits of memory source/destination address for DMA master channel 6 */
#define                 USB_DMA6COUNTLOW  0xffc040d0   /* Lower 16-bits of byte count of DMA transfer for DMA master channel 6 */
#define                USB_DMA6COUNTHIGH  0xffc040d4   /* Upper 16-bits of byte count of DMA transfer for DMA master channel 6 */

/* USB Channel 7 Config Registers */

#define                  USB_DMA7CONTROL  0xffc040e4   /* DMA master channel 7 configuration */
#define                  USB_DMA7ADDRLOW  0xffc040e8   /* Lower 16-bits of memory source/destination address for DMA master channel 7 */
#define                 USB_DMA7ADDRHIGH  0xffc040ec   /* Upper 16-bits of memory source/destination address for DMA master channel 7 */
#define                 USB_DMA7COUNTLOW  0xffc040f0   /* Lower 16-bits of byte count of DMA transfer for DMA master channel 7 */
#define                USB_DMA7COUNTHIGH  0xffc040f4   /* Upper 16-bits of byte count of DMA transfer for DMA master channel 7 */

/* Keypad Registers */

#define                         KPAD_CTL  0xffc04100   /* Controls keypad module enable and disable */
#define                    KPAD_PRESCALE  0xffc04104   /* Establish a time base for programing the KPAD_MSEL register */
#define                        KPAD_MSEL  0xffc04108   /* Selects delay parameters for keypad interface sensitivity */
#define                      KPAD_ROWCOL  0xffc0410c   /* Captures the row and column output values of the keys pressed */
#define                        KPAD_STAT  0xffc04110   /* Holds and clears the status of the keypad interface interrupt */
#define                    KPAD_SOFTEVAL  0xffc04114   /* Lets software force keypad interface to check for keys being pressed */

/* Pixel Compositor (PIXC) Registers */

#define                         PIXC_CTL  0xffc04400   /* Overlay enable, resampling mode, I/O data format, transparency enable, watermark level, FIFO status */
#define                         PIXC_PPL  0xffc04404   /* Holds the number of pixels per line of the display */
#define                         PIXC_LPF  0xffc04408   /* Holds the number of lines per frame of the display */
#define                     PIXC_AHSTART  0xffc0440c   /* Contains horizontal start pixel information of the overlay data (set A) */
#define                       PIXC_AHEND  0xffc04410   /* Contains horizontal end pixel information of the overlay data (set A) */
#define                     PIXC_AVSTART  0xffc04414   /* Contains vertical start pixel information of the overlay data (set A) */
#define                       PIXC_AVEND  0xffc04418   /* Contains vertical end pixel information of the overlay data (set A) */
#define                     PIXC_ATRANSP  0xffc0441c   /* Contains the transparency ratio (set A) */
#define                     PIXC_BHSTART  0xffc04420   /* Contains horizontal start pixel information of the overlay data (set B) */
#define                       PIXC_BHEND  0xffc04424   /* Contains horizontal end pixel information of the overlay data (set B) */
#define                     PIXC_BVSTART  0xffc04428   /* Contains vertical start pixel information of the overlay data (set B) */
#define                       PIXC_BVEND  0xffc0442c   /* Contains vertical end pixel information of the overlay data (set B) */
#define                     PIXC_BTRANSP  0xffc04430   /* Contains the transparency ratio (set B) */
#define                    PIXC_INTRSTAT  0xffc0443c   /* Overlay interrupt configuration/status */
#define                       PIXC_RYCON  0xffc04440   /* Color space conversion matrix register. Contains the R/Y conversion coefficients */
#define                       PIXC_GUCON  0xffc04444   /* Color space conversion matrix register. Contains the G/U conversion coefficients */
#define                       PIXC_BVCON  0xffc04448   /* Color space conversion matrix register. Contains the B/V conversion coefficients */
#define                      PIXC_CCBIAS  0xffc0444c   /* Bias values for the color space conversion matrix */
#define                          PIXC_TC  0xffc04450   /* Holds the transparent color value */

/* Handshake MDMA 0 Registers */

#define                   HMDMA0_CONTROL  0xffc04500   /* Handshake MDMA0 Control Register */
#define                    HMDMA0_ECINIT  0xffc04504   /* Handshake MDMA0 Initial Edge Count Register */
#define                    HMDMA0_BCINIT  0xffc04508   /* Handshake MDMA0 Initial Block Count Register */
#define                  HMDMA0_ECURGENT  0xffc0450c   /* Handshake MDMA0 Urgent Edge Count Threshold Register */
#define                HMDMA0_ECOVERFLOW  0xffc04510   /* Handshake MDMA0 Edge Count Overflow Interrupt Register */
#define                    HMDMA0_ECOUNT  0xffc04514   /* Handshake MDMA0 Current Edge Count Register */
#define                    HMDMA0_BCOUNT  0xffc04518   /* Handshake MDMA0 Current Block Count Register */

/* Handshake MDMA 1 Registers */

#define                   HMDMA1_CONTROL  0xffc04540   /* Handshake MDMA1 Control Register */
#define                    HMDMA1_ECINIT  0xffc04544   /* Handshake MDMA1 Initial Edge Count Register */
#define                    HMDMA1_BCINIT  0xffc04548   /* Handshake MDMA1 Initial Block Count Register */
#define                  HMDMA1_ECURGENT  0xffc0454c   /* Handshake MDMA1 Urgent Edge Count Threshold Register */
#define                HMDMA1_ECOVERFLOW  0xffc04550   /* Handshake MDMA1 Edge Count Overflow Interrupt Register */
#define                    HMDMA1_ECOUNT  0xffc04554   /* Handshake MDMA1 Current Edge Count Register */
#define                    HMDMA1_BCOUNT  0xffc04558   /* Handshake MDMA1 Current Block Count Register */


/* ********************************************************** */
/*     SINGLE BIT MACRO PAIRS (bit mask and negated one)      */
/*     and MULTI BIT READ MACROS                              */
/* ********************************************************** */

/* Bit masks for PIXC_CTL */

#define                   PIXC_EN  0x1        /* Pixel Compositor Enable */
#define                  OVR_A_EN  0x2        /* Overlay A Enable */
#define                  OVR_B_EN  0x4        /* Overlay B Enable */
#define                  IMG_FORM  0x8        /* Image Data Format */
#define                  OVR_FORM  0x10       /* Overlay Data Format */
#define                  OUT_FORM  0x20       /* Output Data Format */
#define                   UDS_MOD  0x40       /* Resampling Mode */
#define                     TC_EN  0x80       /* Transparent Color Enable */
#define                  IMG_STAT  0x300      /* Image FIFO Status */
#define                  OVR_STAT  0xc00      /* Overlay FIFO Status */
#define                    WM_LVL  0x3000     /* FIFO Watermark Level */

/* Bit masks for PIXC_AHSTART */

#define                  A_HSTART  0xfff      /* Horizontal Start Coordinates */

/* Bit masks for PIXC_AHEND */

#define                    A_HEND  0xfff      /* Horizontal End Coordinates */

/* Bit masks for PIXC_AVSTART */

#define                  A_VSTART  0x3ff      /* Vertical Start Coordinates */

/* Bit masks for PIXC_AVEND */

#define                    A_VEND  0x3ff      /* Vertical End Coordinates */

/* Bit masks for PIXC_ATRANSP */

#define                  A_TRANSP  0xf        /* Transparency Value */

/* Bit masks for PIXC_BHSTART */

#define                  B_HSTART  0xfff      /* Horizontal Start Coordinates */

/* Bit masks for PIXC_BHEND */

#define                    B_HEND  0xfff      /* Horizontal End Coordinates */

/* Bit masks for PIXC_BVSTART */

#define                  B_VSTART  0x3ff      /* Vertical Start Coordinates */

/* Bit masks for PIXC_BVEND */

#define                    B_VEND  0x3ff      /* Vertical End Coordinates */

/* Bit masks for PIXC_BTRANSP */

#define                  B_TRANSP  0xf        /* Transparency Value */

/* Bit masks for PIXC_INTRSTAT */

#define                OVR_INT_EN  0x1        /* Interrupt at End of Last Valid Overlay */
#define                FRM_INT_EN  0x2        /* Interrupt at End of Frame */
#define              OVR_INT_STAT  0x4        /* Overlay Interrupt Status */
#define              FRM_INT_STAT  0x8        /* Frame Interrupt Status */

/* Bit masks for PIXC_RYCON */

#define                       A11  0x3ff      /* A11 in the Coefficient Matrix */
#define                       A12  0xffc00    /* A12 in the Coefficient Matrix */
#define                       A13  0x3ff00000 /* A13 in the Coefficient Matrix */
#define                  RY_MULT4  0x40000000 /* Multiply Row by 4 */

/* Bit masks for PIXC_GUCON */

#define                       A21  0x3ff      /* A21 in the Coefficient Matrix */
#define                       A22  0xffc00    /* A22 in the Coefficient Matrix */
#define                       A23  0x3ff00000 /* A23 in the Coefficient Matrix */
#define                  GU_MULT4  0x40000000 /* Multiply Row by 4 */

/* Bit masks for PIXC_BVCON */

#define                       A31  0x3ff      /* A31 in the Coefficient Matrix */
#define                       A32  0xffc00    /* A32 in the Coefficient Matrix */
#define                       A33  0x3ff00000 /* A33 in the Coefficient Matrix */
#define                  BV_MULT4  0x40000000 /* Multiply Row by 4 */

/* Bit masks for PIXC_CCBIAS */

#define                       A14  0x3ff      /* A14 in the Bias Vector */
#define                       A24  0xffc00    /* A24 in the Bias Vector */
#define                       A34  0x3ff00000 /* A34 in the Bias Vector */

/* Bit masks for PIXC_TC */

#define                  RY_TRANS  0xff       /* Transparent Color - R/Y Component */
#define                  GU_TRANS  0xff00     /* Transparent Color - G/U Component */
#define                  BV_TRANS  0xff0000   /* Transparent Color - B/V Component */

/* Bit masks for HOST_CONTROL */

#define                   HOST_EN  0x1        /* Host Enable */
#define                  HOST_END  0x2        /* Host Endianess */
#define                 DATA_SIZE  0x4        /* Data Size */
#define                  HOST_RST  0x8        /* Host Reset */
#define                  HRDY_OVR  0x20       /* Host Ready Override */
#define                  INT_MODE  0x40       /* Interrupt Mode */
#define                     BT_EN  0x80       /* Bus Timeout Enable */
#define                       EHW  0x100      /* Enable Host Write */
#define                       EHR  0x200      /* Enable Host Read */
#define                       BDR  0x400      /* Burst DMA Requests */

/* Bit masks for HOST_STATUS */

#define                 DMA_READY  0x1        /* DMA Ready */
#define                  FIFOFULL  0x2        /* FIFO Full */
#define                 FIFOEMPTY  0x4        /* FIFO Empty */
#define              DMA_COMPLETE  0x8        /* DMA Complete */
#define                      HSHK  0x10       /* Host Handshake */
#define                 HSTIMEOUT  0x20       /* Host Timeout */
#define                      HIRQ  0x40       /* Host Interrupt Request */
#define                ALLOW_CNFG  0x80       /* Allow New Configuration */
#define                   DMA_DIR  0x100      /* DMA Direction */
#define                       BTE  0x200      /* Bus Timeout Enabled */

/* Bit masks for HOST_TIMEOUT */

#define             COUNT_TIMEOUT  0x7ff      /* Host Timeout count */

/* Bit masks for KPAD_CTL */

#define                   KPAD_EN  0x1        /* Keypad Enable */
#define              KPAD_IRQMODE  0x6        /* Key Press Interrupt Enable */
#define                KPAD_ROWEN  0x1c00     /* Row Enable Width */
#define                KPAD_COLEN  0xe000     /* Column Enable Width */

/* Bit masks for KPAD_PRESCALE */

#define         KPAD_PRESCALE_VAL  0x3f       /* Key Prescale Value */

/* Bit masks for KPAD_MSEL */

#define                DBON_SCALE  0xff       /* Debounce Scale Value */
#define              COLDRV_SCALE  0xff00     /* Column Driver Scale Value */

/* Bit masks for KPAD_ROWCOL */

#define                  KPAD_ROW  0xff       /* Rows Pressed */
#define                  KPAD_COL  0xff00     /* Columns Pressed */

/* Bit masks for KPAD_STAT */

#define                  KPAD_IRQ  0x1        /* Keypad Interrupt Status */
#define              KPAD_MROWCOL  0x6        /* Multiple Row/Column Keypress Status */
#define              KPAD_PRESSED  0x8        /* Key press current status */

/* Bit masks for KPAD_SOFTEVAL */

#define           KPAD_SOFTEVAL_E  0x2        /* Software Programmable Force Evaluate */

/* Bit masks for ATAPI_CONTROL */

#define                 PIO_START  0x1        /* Start PIO/Reg Op */
#define               MULTI_START  0x2        /* Start Multi-DMA Op */
#define               ULTRA_START  0x4        /* Start Ultra-DMA Op */
#define                  XFER_DIR  0x8        /* Transfer Direction */
#define                  IORDY_EN  0x10       /* IORDY Enable */
#define                FIFO_FLUSH  0x20       /* Flush FIFOs */
#define                  SOFT_RST  0x40       /* Soft Reset */
#define                   DEV_RST  0x80       /* Device Reset */
#define                TFRCNT_RST  0x100      /* Trans Count Reset */
#define               END_ON_TERM  0x200      /* End/Terminate Select */
#define               PIO_USE_DMA  0x400      /* PIO-DMA Enable */
#define          UDMAIN_FIFO_THRS  0xf000     /* Ultra DMA-IN FIFO Threshold */

/* Bit masks for ATAPI_STATUS */

#define               PIO_XFER_ON  0x1        /* PIO transfer in progress */
#define             MULTI_XFER_ON  0x2        /* Multi-word DMA transfer in progress */
#define             ULTRA_XFER_ON  0x4        /* Ultra DMA transfer in progress */
#define               ULTRA_IN_FL  0xf0       /* Ultra DMA Input FIFO Level */

/* Bit masks for ATAPI_DEV_ADDR */

#define                  DEV_ADDR  0x1f       /* Device Address */

/* Bit masks for ATAPI_INT_MASK */

#define        ATAPI_DEV_INT_MASK  0x1        /* Device interrupt mask */
#define             PIO_DONE_MASK  0x2        /* PIO transfer done interrupt mask */
#define           MULTI_DONE_MASK  0x4        /* Multi-DMA transfer done interrupt mask */
#define          UDMAIN_DONE_MASK  0x8        /* Ultra-DMA in transfer done interrupt mask */
#define         UDMAOUT_DONE_MASK  0x10       /* Ultra-DMA out transfer done interrupt mask */
#define       HOST_TERM_XFER_MASK  0x20       /* Host terminate current transfer interrupt mask */
#define           MULTI_TERM_MASK  0x40       /* Device terminate Multi-DMA transfer interrupt mask */
#define          UDMAIN_TERM_MASK  0x80       /* Device terminate Ultra-DMA-in transfer interrupt mask */
#define         UDMAOUT_TERM_MASK  0x100      /* Device terminate Ultra-DMA-out transfer interrupt mask */

/* Bit masks for ATAPI_INT_STATUS */

#define             ATAPI_DEV_INT  0x1        /* Device interrupt status */
#define              PIO_DONE_INT  0x2        /* PIO transfer done interrupt status */
#define            MULTI_DONE_INT  0x4        /* Multi-DMA transfer done interrupt status */
#define           UDMAIN_DONE_INT  0x8        /* Ultra-DMA in transfer done interrupt status */
#define          UDMAOUT_DONE_INT  0x10       /* Ultra-DMA out transfer done interrupt status */
#define        HOST_TERM_XFER_INT  0x20       /* Host terminate current transfer interrupt status */
#define            MULTI_TERM_INT  0x40       /* Device terminate Multi-DMA transfer interrupt status */
#define           UDMAIN_TERM_INT  0x80       /* Device terminate Ultra-DMA-in transfer interrupt status */
#define          UDMAOUT_TERM_INT  0x100      /* Device terminate Ultra-DMA-out transfer interrupt status */

/* Bit masks for ATAPI_LINE_STATUS */

#define                ATAPI_INTR  0x1        /* Device interrupt to host line status */
#define                ATAPI_DASP  0x2        /* Device dasp to host line status */
#define                ATAPI_CS0N  0x4        /* ATAPI chip select 0 line status */
#define                ATAPI_CS1N  0x8        /* ATAPI chip select 1 line status */
#define                ATAPI_ADDR  0x70       /* ATAPI address line status */
#define              ATAPI_DMAREQ  0x80       /* ATAPI DMA request line status */
#define             ATAPI_DMAACKN  0x100      /* ATAPI DMA acknowledge line status */
#define               ATAPI_DIOWN  0x200      /* ATAPI write line status */
#define               ATAPI_DIORN  0x400      /* ATAPI read line status */
#define               ATAPI_IORDY  0x800      /* ATAPI IORDY line status */

/* Bit masks for ATAPI_SM_STATE */

#define                PIO_CSTATE  0xf        /* PIO mode state machine current state */
#define                DMA_CSTATE  0xf0       /* DMA mode state machine current state */
#define             UDMAIN_CSTATE  0xf00      /* Ultra DMA-In mode state machine current state */
#define            UDMAOUT_CSTATE  0xf000     /* ATAPI IORDY line status */

/* Bit masks for ATAPI_TERMINATE */

#define           ATAPI_HOST_TERM  0x1        /* Host terminationation */

/* Bit masks for ATAPI_REG_TIM_0 */

#define                    T2_REG  0xff       /* End of cycle time for register access transfers */
#define                  TEOC_REG  0xff00     /* Selects DIOR/DIOW pulsewidth */

/* Bit masks for ATAPI_PIO_TIM_0 */

#define                    T1_REG  0xf        /* Time from address valid to DIOR/DIOW */
#define                T2_REG_PIO  0xff0      /* DIOR/DIOW pulsewidth */
#define                    T4_REG  0xf000     /* DIOW data hold */

/* Bit masks for ATAPI_PIO_TIM_1 */

#define              TEOC_REG_PIO  0xff       /* End of cycle time for PIO access transfers. */

/* Bit masks for ATAPI_MULTI_TIM_0 */

#define                        TD  0xff       /* DIOR/DIOW asserted pulsewidth */
#define                        TM  0xff00     /* Time from address valid to DIOR/DIOW */

/* Bit masks for ATAPI_MULTI_TIM_1 */

#define                       TKW  0xff       /* Selects DIOW negated pulsewidth */
#define                       TKR  0xff00     /* Selects DIOR negated pulsewidth */

/* Bit masks for ATAPI_MULTI_TIM_2 */

#define                        TH  0xff       /* Selects DIOW data hold */
#define                      TEOC  0xff00     /* Selects end of cycle for DMA */

/* Bit masks for ATAPI_ULTRA_TIM_0 */

#define                      TACK  0xff       /* Selects setup and hold times for TACK */
#define                      TENV  0xff00     /* Selects envelope time */

/* Bit masks for ATAPI_ULTRA_TIM_1 */

#define                      TDVS  0xff       /* Selects data valid setup time */
#define                 TCYC_TDVS  0xff00     /* Selects cycle time - TDVS time */

/* Bit masks for ATAPI_ULTRA_TIM_2 */

#define                       TSS  0xff       /* Selects time from STROBE edge to negation of DMARQ or assertion of STOP */
#define                      TMLI  0xff00     /* Selects interlock time */

/* Bit masks for ATAPI_ULTRA_TIM_3 */

#define                      TZAH  0xff       /* Selects minimum delay required for output */
#define               READY_PAUSE  0xff00     /* Selects ready to pause */

/* Bit masks for TIMER_ENABLE1 */

#define                    TIMEN8  0x1        /* Timer 8 Enable */
#define                    TIMEN9  0x2        /* Timer 9 Enable */
#define                   TIMEN10  0x4        /* Timer 10 Enable */

/* Bit masks for TIMER_DISABLE1 */

#define                   TIMDIS8  0x1        /* Timer 8 Disable */
#define                   TIMDIS9  0x2        /* Timer 9 Disable */
#define                  TIMDIS10  0x4        /* Timer 10 Disable */

/* Bit masks for TIMER_STATUS1 */

#define                    TIMIL8  0x1        /* Timer 8 Interrupt */
#define                    TIMIL9  0x2        /* Timer 9 Interrupt */
#define                   TIMIL10  0x4        /* Timer 10 Interrupt */
#define                 TOVF_ERR8  0x10       /* Timer 8 Counter Overflow */
#define                 TOVF_ERR9  0x20       /* Timer 9 Counter Overflow */
#define                TOVF_ERR10  0x40       /* Timer 10 Counter Overflow */
#define                     TRUN8  0x1000     /* Timer 8 Slave Enable Status */
#define                     TRUN9  0x2000     /* Timer 9 Slave Enable Status */
#define                    TRUN10  0x4000     /* Timer 10 Slave Enable Status */

/* Bit masks for EPPI0 are obtained from common base header for EPPIx (EPPI1 and EPPI2) */

/* Bit masks for USB_FADDR */

#define          FUNCTION_ADDRESS  0x7f       /* Function address */

/* Bit masks for USB_POWER */

#define           ENABLE_SUSPENDM  0x1        /* enable SuspendM output */
#define              SUSPEND_MODE  0x2        /* Suspend Mode indicator */
#define               RESUME_MODE  0x4        /* DMA Mode */
#define                     RESET  0x8        /* Reset indicator */
#define                   HS_MODE  0x10       /* High Speed mode indicator */
#define                 HS_ENABLE  0x20       /* high Speed Enable */
#define                 SOFT_CONN  0x40       /* Soft connect */
#define                ISO_UPDATE  0x80       /* Isochronous update */

/* Bit masks for USB_INTRTX */

#define                    EP0_TX  0x1        /* Tx Endpoint 0 interrupt */
#define                    EP1_TX  0x2        /* Tx Endpoint 1 interrupt */
#define                    EP2_TX  0x4        /* Tx Endpoint 2 interrupt */
#define                    EP3_TX  0x8        /* Tx Endpoint 3 interrupt */
#define                    EP4_TX  0x10       /* Tx Endpoint 4 interrupt */
#define                    EP5_TX  0x20       /* Tx Endpoint 5 interrupt */
#define                    EP6_TX  0x40       /* Tx Endpoint 6 interrupt */
#define                    EP7_TX  0x80       /* Tx Endpoint 7 interrupt */

/* Bit masks for USB_INTRRX */

#define                    EP1_RX  0x2        /* Rx Endpoint 1 interrupt */
#define                    EP2_RX  0x4        /* Rx Endpoint 2 interrupt */
#define                    EP3_RX  0x8        /* Rx Endpoint 3 interrupt */
#define                    EP4_RX  0x10       /* Rx Endpoint 4 interrupt */
#define                    EP5_RX  0x20       /* Rx Endpoint 5 interrupt */
#define                    EP6_RX  0x40       /* Rx Endpoint 6 interrupt */
#define                    EP7_RX  0x80       /* Rx Endpoint 7 interrupt */

/* Bit masks for USB_INTRTXE */

#define                  EP0_TX_E  0x1        /* Endpoint 0 interrupt Enable */
#define                  EP1_TX_E  0x2        /* Tx Endpoint 1 interrupt  Enable */
#define                  EP2_TX_E  0x4        /* Tx Endpoint 2 interrupt  Enable */
#define                  EP3_TX_E  0x8        /* Tx Endpoint 3 interrupt  Enable */
#define                  EP4_TX_E  0x10       /* Tx Endpoint 4 interrupt  Enable */
#define                  EP5_TX_E  0x20       /* Tx Endpoint 5 interrupt  Enable */
#define                  EP6_TX_E  0x40       /* Tx Endpoint 6 interrupt  Enable */
#define                  EP7_TX_E  0x80       /* Tx Endpoint 7 interrupt  Enable */

/* Bit masks for USB_INTRRXE */

#define                  EP1_RX_E  0x2        /* Rx Endpoint 1 interrupt  Enable */
#define                  EP2_RX_E  0x4        /* Rx Endpoint 2 interrupt  Enable */
#define                  EP3_RX_E  0x8        /* Rx Endpoint 3 interrupt  Enable */
#define                  EP4_RX_E  0x10       /* Rx Endpoint 4 interrupt  Enable */
#define                  EP5_RX_E  0x20       /* Rx Endpoint 5 interrupt  Enable */
#define                  EP6_RX_E  0x40       /* Rx Endpoint 6 interrupt  Enable */
#define                  EP7_RX_E  0x80       /* Rx Endpoint 7 interrupt  Enable */

/* Bit masks for USB_INTRUSB */

#define                 SUSPEND_B  0x1        /* Suspend indicator */
#define                  RESUME_B  0x2        /* Resume indicator */
#define          RESET_OR_BABLE_B  0x4        /* Reset/babble indicator */
#define                     SOF_B  0x8        /* Start of frame */
#define                    CONN_B  0x10       /* Connection indicator */
#define                  DISCON_B  0x20       /* Disconnect indicator */
#define             SESSION_REQ_B  0x40       /* Session Request */
#define              VBUS_ERROR_B  0x80       /* Vbus threshold indicator */

/* Bit masks for USB_INTRUSBE */

#define                SUSPEND_BE  0x1        /* Suspend indicator int enable */
#define                 RESUME_BE  0x2        /* Resume indicator int enable */
#define         RESET_OR_BABLE_BE  0x4        /* Reset/babble indicator int enable */
#define                    SOF_BE  0x8        /* Start of frame int enable */
#define                   CONN_BE  0x10       /* Connection indicator int enable */
#define                 DISCON_BE  0x20       /* Disconnect indicator int enable */
#define            SESSION_REQ_BE  0x40       /* Session Request int enable */
#define             VBUS_ERROR_BE  0x80       /* Vbus threshold indicator int enable */

/* Bit masks for USB_FRAME */

#define              FRAME_NUMBER  0x7ff      /* Frame number */

/* Bit masks for USB_INDEX */

#define         SELECTED_ENDPOINT  0xf        /* selected endpoint */

/* Bit masks for USB_GLOBAL_CTL */

#define                GLOBAL_ENA  0x1        /* enables USB module */
#define                EP1_TX_ENA  0x2        /* Transmit endpoint 1 enable */
#define                EP2_TX_ENA  0x4        /* Transmit endpoint 2 enable */
#define                EP3_TX_ENA  0x8        /* Transmit endpoint 3 enable */
#define                EP4_TX_ENA  0x10       /* Transmit endpoint 4 enable */
#define                EP5_TX_ENA  0x20       /* Transmit endpoint 5 enable */
#define                EP6_TX_ENA  0x40       /* Transmit endpoint 6 enable */
#define                EP7_TX_ENA  0x80       /* Transmit endpoint 7 enable */
#define                EP1_RX_ENA  0x100      /* Receive endpoint 1 enable */
#define                EP2_RX_ENA  0x200      /* Receive endpoint 2 enable */
#define                EP3_RX_ENA  0x400      /* Receive endpoint 3 enable */
#define                EP4_RX_ENA  0x800      /* Receive endpoint 4 enable */
#define                EP5_RX_ENA  0x1000     /* Receive endpoint 5 enable */
#define                EP6_RX_ENA  0x2000     /* Receive endpoint 6 enable */
#define                EP7_RX_ENA  0x4000     /* Receive endpoint 7 enable */

/* Bit masks for USB_OTG_DEV_CTL */

#define                   SESSION  0x1        /* session indicator */
#define                  HOST_REQ  0x2        /* Host negotiation request */
#define                 HOST_MODE  0x4        /* indicates USBDRC is a host */
#define                     VBUS0  0x8        /* Vbus level indicator[0] */
#define                     VBUS1  0x10       /* Vbus level indicator[1] */
#define                     LSDEV  0x20       /* Low-speed indicator */
#define                     FSDEV  0x40       /* Full or High-speed indicator */
#define                  B_DEVICE  0x80       /* A' or 'B' device indicator */

/* Bit masks for USB_OTG_VBUS_IRQ */

#define             DRIVE_VBUS_ON  0x1        /* indicator to drive VBUS control circuit */
#define            DRIVE_VBUS_OFF  0x2        /* indicator to shut off charge pump */
#define           CHRG_VBUS_START  0x4        /* indicator for external circuit to start charging VBUS */
#define             CHRG_VBUS_END  0x8        /* indicator for external circuit to end charging VBUS */
#define        DISCHRG_VBUS_START  0x10       /* indicator to start discharging VBUS */
#define          DISCHRG_VBUS_END  0x20       /* indicator to stop discharging VBUS */

/* Bit masks for USB_OTG_VBUS_MASK */

#define         DRIVE_VBUS_ON_ENA  0x1        /* enable DRIVE_VBUS_ON interrupt */
#define        DRIVE_VBUS_OFF_ENA  0x2        /* enable DRIVE_VBUS_OFF interrupt */
#define       CHRG_VBUS_START_ENA  0x4        /* enable CHRG_VBUS_START interrupt */
#define         CHRG_VBUS_END_ENA  0x8        /* enable CHRG_VBUS_END interrupt */
#define    DISCHRG_VBUS_START_ENA  0x10       /* enable DISCHRG_VBUS_START interrupt */
#define      DISCHRG_VBUS_END_ENA  0x20       /* enable DISCHRG_VBUS_END interrupt */

/* Bit masks for USB_CSR0 */

#define                  RXPKTRDY  0x1        /* data packet receive indicator */
#define                  TXPKTRDY  0x2        /* data packet in FIFO indicator */
#define                STALL_SENT  0x4        /* STALL handshake sent */
#define                   DATAEND  0x8        /* Data end indicator */
#define                  SETUPEND  0x10       /* Setup end */
#define                 SENDSTALL  0x20       /* Send STALL handshake */
#define         SERVICED_RXPKTRDY  0x40       /* used to clear the RxPktRdy bit */
#define         SERVICED_SETUPEND  0x80       /* used to clear the SetupEnd bit */
#define                 FLUSHFIFO  0x100      /* flush endpoint FIFO */
#define          STALL_RECEIVED_H  0x4        /* STALL handshake received host mode */
#define                SETUPPKT_H  0x8        /* send Setup token host mode */
#define                   ERROR_H  0x10       /* timeout error indicator host mode */
#define                  REQPKT_H  0x20       /* Request an IN transaction host mode */
#define               STATUSPKT_H  0x40       /* Status stage transaction host mode */
#define             NAK_TIMEOUT_H  0x80       /* EP0 halted after a NAK host mode */

/* Bit masks for USB_COUNT0 */

#define              EP0_RX_COUNT  0x7f       /* number of received bytes in EP0 FIFO */

/* Bit masks for USB_NAKLIMIT0 */

#define             EP0_NAK_LIMIT  0x1f       /* number of frames/micro frames after which EP0 timeouts */

/* Bit masks for USB_TX_MAX_PACKET */

#define         MAX_PACKET_SIZE_T  0x7ff      /* maximum data pay load in a frame */

/* Bit masks for USB_RX_MAX_PACKET */

#define         MAX_PACKET_SIZE_R  0x7ff      /* maximum data pay load in a frame */

/* Bit masks for USB_TXCSR */

#define                TXPKTRDY_T  0x1        /* data packet in FIFO indicator */
#define          FIFO_NOT_EMPTY_T  0x2        /* FIFO not empty */
#define                UNDERRUN_T  0x4        /* TxPktRdy not set  for an IN token */
#define               FLUSHFIFO_T  0x8        /* flush endpoint FIFO */
#define              STALL_SEND_T  0x10       /* issue a Stall handshake */
#define              STALL_SENT_T  0x20       /* Stall handshake transmitted */
#define        CLEAR_DATATOGGLE_T  0x40       /* clear endpoint data toggle */
#define                INCOMPTX_T  0x80       /* indicates that a large packet is split */
#define              DMAREQMODE_T  0x400      /* DMA mode (0 or 1) selection */
#define        FORCE_DATATOGGLE_T  0x800      /* Force data toggle */
#define              DMAREQ_ENA_T  0x1000     /* Enable DMA request for Tx EP */
#define                     ISO_T  0x4000     /* enable Isochronous transfers */
#define                 AUTOSET_T  0x8000     /* allows TxPktRdy to be set automatically */
#define                  ERROR_TH  0x4        /* error condition host mode */
#define         STALL_RECEIVED_TH  0x20       /* Stall handshake received host mode */
#define            NAK_TIMEOUT_TH  0x80       /* NAK timeout host mode */

/* Bit masks for USB_TXCOUNT */

#define                  TX_COUNT  0x1fff     /* Number of bytes to be written to the selected endpoint Tx FIFO */

/* Bit masks for USB_RXCSR */

#define                RXPKTRDY_R  0x1        /* data packet in FIFO indicator */
#define               FIFO_FULL_R  0x2        /* FIFO not empty */
#define                 OVERRUN_R  0x4        /* TxPktRdy not set  for an IN token */
#define               DATAERROR_R  0x8        /* Out packet cannot be loaded into Rx  FIFO */
#define               FLUSHFIFO_R  0x10       /* flush endpoint FIFO */
#define              STALL_SEND_R  0x20       /* issue a Stall handshake */
#define              STALL_SENT_R  0x40       /* Stall handshake transmitted */
#define        CLEAR_DATATOGGLE_R  0x80       /* clear endpoint data toggle */
#define                INCOMPRX_R  0x100      /* indicates that a large packet is split */
#define              DMAREQMODE_R  0x800      /* DMA mode (0 or 1) selection */
#define                 DISNYET_R  0x1000     /* disable Nyet handshakes */
#define              DMAREQ_ENA_R  0x2000     /* Enable DMA request for Tx EP */
#define                     ISO_R  0x4000     /* enable Isochronous transfers */
#define               AUTOCLEAR_R  0x8000     /* allows TxPktRdy to be set automatically */
#define                  ERROR_RH  0x4        /* TxPktRdy not set  for an IN token host mode */
#define                 REQPKT_RH  0x20       /* request an IN transaction host mode */
#define         STALL_RECEIVED_RH  0x40       /* Stall handshake received host mode */
#define               INCOMPRX_RH  0x100      /* indicates that a large packet is split host mode */
#define             DMAREQMODE_RH  0x800      /* DMA mode (0 or 1) selection host mode */
#define                AUTOREQ_RH  0x4000     /* sets ReqPkt automatically host mode */

/* Bit masks for USB_RXCOUNT */

#define                  RX_COUNT  0x1fff     /* Number of received bytes in the packet in the Rx FIFO */

/* Bit masks for USB_TXTYPE */

#define            TARGET_EP_NO_T  0xf        /* EP number */
#define                PROTOCOL_T  0xc        /* transfer type */

/* Bit masks for USB_TXINTERVAL */

#define          TX_POLL_INTERVAL  0xff       /* polling interval for selected Tx EP */

/* Bit masks for USB_RXTYPE */

#define            TARGET_EP_NO_R  0xf        /* EP number */
#define                PROTOCOL_R  0xc        /* transfer type */

/* Bit masks for USB_RXINTERVAL */

#define          RX_POLL_INTERVAL  0xff       /* polling interval for selected Rx EP */

/* Bit masks for USB_DMA_INTERRUPT */

#define                  DMA0_INT  0x1        /* DMA0 pending interrupt */
#define                  DMA1_INT  0x2        /* DMA1 pending interrupt */
#define                  DMA2_INT  0x4        /* DMA2 pending interrupt */
#define                  DMA3_INT  0x8        /* DMA3 pending interrupt */
#define                  DMA4_INT  0x10       /* DMA4 pending interrupt */
#define                  DMA5_INT  0x20       /* DMA5 pending interrupt */
#define                  DMA6_INT  0x40       /* DMA6 pending interrupt */
#define                  DMA7_INT  0x80       /* DMA7 pending interrupt */

/* Bit masks for USB_DMAxCONTROL */

#define                   DMA_ENA  0x1        /* DMA enable */
#define                 DIRECTION  0x2        /* direction of DMA transfer */
#define                      MODE  0x4        /* DMA Bus error */
#define                   INT_ENA  0x8        /* Interrupt enable */
#define                     EPNUM  0xf0       /* EP number */
#define                  BUSERROR  0x100      /* DMA Bus error */

/* Bit masks for USB_DMAxADDRHIGH */

#define             DMA_ADDR_HIGH  0xffff     /* Upper 16-bits of memory source/destination address for the DMA master channel */

/* Bit masks for USB_DMAxADDRLOW */

#define              DMA_ADDR_LOW  0xffff     /* Lower 16-bits of memory source/destination address for the DMA master channel */

/* Bit masks for USB_DMAxCOUNTHIGH */

#define            DMA_COUNT_HIGH  0xffff     /* Upper 16-bits of byte count of DMA transfer for DMA master channel */

/* Bit masks for USB_DMAxCOUNTLOW */

#define             DMA_COUNT_LOW  0xffff     /* Lower 16-bits of byte count of DMA transfer for DMA master channel */

#endif /* _DEF_BF547_H */
