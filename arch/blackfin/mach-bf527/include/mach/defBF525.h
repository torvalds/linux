/*
 * Copyright 2007-2010 Analog Devices Inc.
 *
 * Licensed under the ADI BSD license or the GPL-2 (or later)
 */

#ifndef _DEF_BF525_H
#define _DEF_BF525_H

/* BF525 is BF522 + USB */
#include "defBF522.h"

/* USB Control Registers */

#define                        USB_FADDR  0xffc03800   /* Function address register */
#define                        USB_POWER  0xffc03804   /* Power management register */
#define                       USB_INTRTX  0xffc03808   /* Interrupt register for endpoint 0 and Tx endpoint 1 to 7 */
#define                       USB_INTRRX  0xffc0380c   /* Interrupt register for Rx endpoints 1 to 7 */
#define                      USB_INTRTXE  0xffc03810   /* Interrupt enable register for IntrTx */
#define                      USB_INTRRXE  0xffc03814   /* Interrupt enable register for IntrRx */
#define                      USB_INTRUSB  0xffc03818   /* Interrupt register for common USB interrupts */
#define                     USB_INTRUSBE  0xffc0381c   /* Interrupt enable register for IntrUSB */
#define                        USB_FRAME  0xffc03820   /* USB frame number */
#define                        USB_INDEX  0xffc03824   /* Index register for selecting the indexed endpoint registers */
#define                     USB_TESTMODE  0xffc03828   /* Enabled USB 20 test modes */
#define                     USB_GLOBINTR  0xffc0382c   /* Global Interrupt Mask register and Wakeup Exception Interrupt */
#define                   USB_GLOBAL_CTL  0xffc03830   /* Global Clock Control for the core */

/* USB Packet Control Registers */

#define                USB_TX_MAX_PACKET  0xffc03840   /* Maximum packet size for Host Tx endpoint */
#define                         USB_CSR0  0xffc03844   /* Control Status register for endpoint 0 and Control Status register for Host Tx endpoint */
#define                        USB_TXCSR  0xffc03844   /* Control Status register for endpoint 0 and Control Status register for Host Tx endpoint */
#define                USB_RX_MAX_PACKET  0xffc03848   /* Maximum packet size for Host Rx endpoint */
#define                        USB_RXCSR  0xffc0384c   /* Control Status register for Host Rx endpoint */
#define                       USB_COUNT0  0xffc03850   /* Number of bytes received in endpoint 0 FIFO and Number of bytes received in Host Tx endpoint */
#define                      USB_RXCOUNT  0xffc03850   /* Number of bytes received in endpoint 0 FIFO and Number of bytes received in Host Tx endpoint */
#define                       USB_TXTYPE  0xffc03854   /* Sets the transaction protocol and peripheral endpoint number for the Host Tx endpoint */
#define                    USB_NAKLIMIT0  0xffc03858   /* Sets the NAK response timeout on Endpoint 0 and on Bulk transfers for Host Tx endpoint */
#define                   USB_TXINTERVAL  0xffc03858   /* Sets the NAK response timeout on Endpoint 0 and on Bulk transfers for Host Tx endpoint */
#define                       USB_RXTYPE  0xffc0385c   /* Sets the transaction protocol and peripheral endpoint number for the Host Rx endpoint */
#define                   USB_RXINTERVAL  0xffc03860   /* Sets the polling interval for Interrupt and Isochronous transfers or the NAK response timeout on Bulk transfers */
#define                      USB_TXCOUNT  0xffc03868   /* Number of bytes to be written to the selected endpoint Tx FIFO */

/* USB Endpoint FIFO Registers */

#define                     USB_EP0_FIFO  0xffc03880   /* Endpoint 0 FIFO */
#define                     USB_EP1_FIFO  0xffc03888   /* Endpoint 1 FIFO */
#define                     USB_EP2_FIFO  0xffc03890   /* Endpoint 2 FIFO */
#define                     USB_EP3_FIFO  0xffc03898   /* Endpoint 3 FIFO */
#define                     USB_EP4_FIFO  0xffc038a0   /* Endpoint 4 FIFO */
#define                     USB_EP5_FIFO  0xffc038a8   /* Endpoint 5 FIFO */
#define                     USB_EP6_FIFO  0xffc038b0   /* Endpoint 6 FIFO */
#define                     USB_EP7_FIFO  0xffc038b8   /* Endpoint 7 FIFO */

/* USB OTG Control Registers */

#define                  USB_OTG_DEV_CTL  0xffc03900   /* OTG Device Control Register */
#define                 USB_OTG_VBUS_IRQ  0xffc03904   /* OTG VBUS Control Interrupts */
#define                USB_OTG_VBUS_MASK  0xffc03908   /* VBUS Control Interrupt Enable */

/* USB Phy Control Registers */

#define                     USB_LINKINFO  0xffc03948   /* Enables programming of some PHY-side delays */
#define                        USB_VPLEN  0xffc0394c   /* Determines duration of VBUS pulse for VBUS charging */
#define                      USB_HS_EOF1  0xffc03950   /* Time buffer for High-Speed transactions */
#define                      USB_FS_EOF1  0xffc03954   /* Time buffer for Full-Speed transactions */
#define                      USB_LS_EOF1  0xffc03958   /* Time buffer for Low-Speed transactions */

/* (APHY_CNTRL is for ADI usage only) */

#define                   USB_APHY_CNTRL  0xffc039e0   /* Register that increases visibility of Analog PHY */

/* (APHY_CALIB is for ADI usage only) */

#define                   USB_APHY_CALIB  0xffc039e4   /* Register used to set some calibration values */

#define                  USB_APHY_CNTRL2  0xffc039e8   /* Register used to prevent re-enumeration once Moab goes into hibernate mode */

/* (PHY_TEST is for ADI usage only) */

#define                     USB_PHY_TEST  0xffc039ec   /* Used for reducing simulation time and simplifies FIFO testability */

#define                  USB_PLLOSC_CTRL  0xffc039f0   /* Used to program different parameters for USB PLL and Oscillator */
#define                   USB_SRP_CLKDIV  0xffc039f4   /* Used to program clock divide value for the clock fed to the SRP detection logic */

/* USB Endpoint 0 Control Registers */

#define                USB_EP_NI0_TXMAXP  0xffc03a00   /* Maximum packet size for Host Tx endpoint0 */
#define                 USB_EP_NI0_TXCSR  0xffc03a04   /* Control Status register for endpoint 0 */
#define                USB_EP_NI0_RXMAXP  0xffc03a08   /* Maximum packet size for Host Rx endpoint0 */
#define                 USB_EP_NI0_RXCSR  0xffc03a0c   /* Control Status register for Host Rx endpoint0 */
#define               USB_EP_NI0_RXCOUNT  0xffc03a10   /* Number of bytes received in endpoint 0 FIFO */
#define                USB_EP_NI0_TXTYPE  0xffc03a14   /* Sets the transaction protocol and peripheral endpoint number for the Host Tx endpoint0 */
#define            USB_EP_NI0_TXINTERVAL  0xffc03a18   /* Sets the NAK response timeout on Endpoint 0 */
#define                USB_EP_NI0_RXTYPE  0xffc03a1c   /* Sets the transaction protocol and peripheral endpoint number for the Host Rx endpoint0 */
#define            USB_EP_NI0_RXINTERVAL  0xffc03a20   /* Sets the polling interval for Interrupt/Isochronous transfers or the NAK response timeout on Bulk transfers for Host Rx endpoint0 */
#define               USB_EP_NI0_TXCOUNT  0xffc03a28   /* Number of bytes to be written to the endpoint0 Tx FIFO */

/* USB Endpoint 1 Control Registers */

#define                USB_EP_NI1_TXMAXP  0xffc03a40   /* Maximum packet size for Host Tx endpoint1 */
#define                 USB_EP_NI1_TXCSR  0xffc03a44   /* Control Status register for endpoint1 */
#define                USB_EP_NI1_RXMAXP  0xffc03a48   /* Maximum packet size for Host Rx endpoint1 */
#define                 USB_EP_NI1_RXCSR  0xffc03a4c   /* Control Status register for Host Rx endpoint1 */
#define               USB_EP_NI1_RXCOUNT  0xffc03a50   /* Number of bytes received in endpoint1 FIFO */
#define                USB_EP_NI1_TXTYPE  0xffc03a54   /* Sets the transaction protocol and peripheral endpoint number for the Host Tx endpoint1 */
#define            USB_EP_NI1_TXINTERVAL  0xffc03a58   /* Sets the NAK response timeout on Endpoint1 */
#define                USB_EP_NI1_RXTYPE  0xffc03a5c   /* Sets the transaction protocol and peripheral endpoint number for the Host Rx endpoint1 */
#define            USB_EP_NI1_RXINTERVAL  0xffc03a60   /* Sets the polling interval for Interrupt/Isochronous transfers or the NAK response timeout on Bulk transfers for Host Rx endpoint1 */
#define               USB_EP_NI1_TXCOUNT  0xffc03a68   /* Number of bytes to be written to the+H102 endpoint1 Tx FIFO */

/* USB Endpoint 2 Control Registers */

#define                USB_EP_NI2_TXMAXP  0xffc03a80   /* Maximum packet size for Host Tx endpoint2 */
#define                 USB_EP_NI2_TXCSR  0xffc03a84   /* Control Status register for endpoint2 */
#define                USB_EP_NI2_RXMAXP  0xffc03a88   /* Maximum packet size for Host Rx endpoint2 */
#define                 USB_EP_NI2_RXCSR  0xffc03a8c   /* Control Status register for Host Rx endpoint2 */
#define               USB_EP_NI2_RXCOUNT  0xffc03a90   /* Number of bytes received in endpoint2 FIFO */
#define                USB_EP_NI2_TXTYPE  0xffc03a94   /* Sets the transaction protocol and peripheral endpoint number for the Host Tx endpoint2 */
#define            USB_EP_NI2_TXINTERVAL  0xffc03a98   /* Sets the NAK response timeout on Endpoint2 */
#define                USB_EP_NI2_RXTYPE  0xffc03a9c   /* Sets the transaction protocol and peripheral endpoint number for the Host Rx endpoint2 */
#define            USB_EP_NI2_RXINTERVAL  0xffc03aa0   /* Sets the polling interval for Interrupt/Isochronous transfers or the NAK response timeout on Bulk transfers for Host Rx endpoint2 */
#define               USB_EP_NI2_TXCOUNT  0xffc03aa8   /* Number of bytes to be written to the endpoint2 Tx FIFO */

/* USB Endpoint 3 Control Registers */

#define                USB_EP_NI3_TXMAXP  0xffc03ac0   /* Maximum packet size for Host Tx endpoint3 */
#define                 USB_EP_NI3_TXCSR  0xffc03ac4   /* Control Status register for endpoint3 */
#define                USB_EP_NI3_RXMAXP  0xffc03ac8   /* Maximum packet size for Host Rx endpoint3 */
#define                 USB_EP_NI3_RXCSR  0xffc03acc   /* Control Status register for Host Rx endpoint3 */
#define               USB_EP_NI3_RXCOUNT  0xffc03ad0   /* Number of bytes received in endpoint3 FIFO */
#define                USB_EP_NI3_TXTYPE  0xffc03ad4   /* Sets the transaction protocol and peripheral endpoint number for the Host Tx endpoint3 */
#define            USB_EP_NI3_TXINTERVAL  0xffc03ad8   /* Sets the NAK response timeout on Endpoint3 */
#define                USB_EP_NI3_RXTYPE  0xffc03adc   /* Sets the transaction protocol and peripheral endpoint number for the Host Rx endpoint3 */
#define            USB_EP_NI3_RXINTERVAL  0xffc03ae0   /* Sets the polling interval for Interrupt/Isochronous transfers or the NAK response timeout on Bulk transfers for Host Rx endpoint3 */
#define               USB_EP_NI3_TXCOUNT  0xffc03ae8   /* Number of bytes to be written to the H124endpoint3 Tx FIFO */

/* USB Endpoint 4 Control Registers */

#define                USB_EP_NI4_TXMAXP  0xffc03b00   /* Maximum packet size for Host Tx endpoint4 */
#define                 USB_EP_NI4_TXCSR  0xffc03b04   /* Control Status register for endpoint4 */
#define                USB_EP_NI4_RXMAXP  0xffc03b08   /* Maximum packet size for Host Rx endpoint4 */
#define                 USB_EP_NI4_RXCSR  0xffc03b0c   /* Control Status register for Host Rx endpoint4 */
#define               USB_EP_NI4_RXCOUNT  0xffc03b10   /* Number of bytes received in endpoint4 FIFO */
#define                USB_EP_NI4_TXTYPE  0xffc03b14   /* Sets the transaction protocol and peripheral endpoint number for the Host Tx endpoint4 */
#define            USB_EP_NI4_TXINTERVAL  0xffc03b18   /* Sets the NAK response timeout on Endpoint4 */
#define                USB_EP_NI4_RXTYPE  0xffc03b1c   /* Sets the transaction protocol and peripheral endpoint number for the Host Rx endpoint4 */
#define            USB_EP_NI4_RXINTERVAL  0xffc03b20   /* Sets the polling interval for Interrupt/Isochronous transfers or the NAK response timeout on Bulk transfers for Host Rx endpoint4 */
#define               USB_EP_NI4_TXCOUNT  0xffc03b28   /* Number of bytes to be written to the endpoint4 Tx FIFO */

/* USB Endpoint 5 Control Registers */

#define                USB_EP_NI5_TXMAXP  0xffc03b40   /* Maximum packet size for Host Tx endpoint5 */
#define                 USB_EP_NI5_TXCSR  0xffc03b44   /* Control Status register for endpoint5 */
#define                USB_EP_NI5_RXMAXP  0xffc03b48   /* Maximum packet size for Host Rx endpoint5 */
#define                 USB_EP_NI5_RXCSR  0xffc03b4c   /* Control Status register for Host Rx endpoint5 */
#define               USB_EP_NI5_RXCOUNT  0xffc03b50   /* Number of bytes received in endpoint5 FIFO */
#define                USB_EP_NI5_TXTYPE  0xffc03b54   /* Sets the transaction protocol and peripheral endpoint number for the Host Tx endpoint5 */
#define            USB_EP_NI5_TXINTERVAL  0xffc03b58   /* Sets the NAK response timeout on Endpoint5 */
#define                USB_EP_NI5_RXTYPE  0xffc03b5c   /* Sets the transaction protocol and peripheral endpoint number for the Host Rx endpoint5 */
#define            USB_EP_NI5_RXINTERVAL  0xffc03b60   /* Sets the polling interval for Interrupt/Isochronous transfers or the NAK response timeout on Bulk transfers for Host Rx endpoint5 */
#define               USB_EP_NI5_TXCOUNT  0xffc03b68   /* Number of bytes to be written to the endpoint5 Tx FIFO */

/* USB Endpoint 6 Control Registers */

#define                USB_EP_NI6_TXMAXP  0xffc03b80   /* Maximum packet size for Host Tx endpoint6 */
#define                 USB_EP_NI6_TXCSR  0xffc03b84   /* Control Status register for endpoint6 */
#define                USB_EP_NI6_RXMAXP  0xffc03b88   /* Maximum packet size for Host Rx endpoint6 */
#define                 USB_EP_NI6_RXCSR  0xffc03b8c   /* Control Status register for Host Rx endpoint6 */
#define               USB_EP_NI6_RXCOUNT  0xffc03b90   /* Number of bytes received in endpoint6 FIFO */
#define                USB_EP_NI6_TXTYPE  0xffc03b94   /* Sets the transaction protocol and peripheral endpoint number for the Host Tx endpoint6 */
#define            USB_EP_NI6_TXINTERVAL  0xffc03b98   /* Sets the NAK response timeout on Endpoint6 */
#define                USB_EP_NI6_RXTYPE  0xffc03b9c   /* Sets the transaction protocol and peripheral endpoint number for the Host Rx endpoint6 */
#define            USB_EP_NI6_RXINTERVAL  0xffc03ba0   /* Sets the polling interval for Interrupt/Isochronous transfers or the NAK response timeout on Bulk transfers for Host Rx endpoint6 */
#define               USB_EP_NI6_TXCOUNT  0xffc03ba8   /* Number of bytes to be written to the endpoint6 Tx FIFO */

/* USB Endpoint 7 Control Registers */

#define                USB_EP_NI7_TXMAXP  0xffc03bc0   /* Maximum packet size for Host Tx endpoint7 */
#define                 USB_EP_NI7_TXCSR  0xffc03bc4   /* Control Status register for endpoint7 */
#define                USB_EP_NI7_RXMAXP  0xffc03bc8   /* Maximum packet size for Host Rx endpoint7 */
#define                 USB_EP_NI7_RXCSR  0xffc03bcc   /* Control Status register for Host Rx endpoint7 */
#define               USB_EP_NI7_RXCOUNT  0xffc03bd0   /* Number of bytes received in endpoint7 FIFO */
#define                USB_EP_NI7_TXTYPE  0xffc03bd4   /* Sets the transaction protocol and peripheral endpoint number for the Host Tx endpoint7 */
#define            USB_EP_NI7_TXINTERVAL  0xffc03bd8   /* Sets the NAK response timeout on Endpoint7 */
#define                USB_EP_NI7_RXTYPE  0xffc03bdc   /* Sets the transaction protocol and peripheral endpoint number for the Host Rx endpoint7 */
#define            USB_EP_NI7_RXINTERVAL  0xffc03be0   /* Sets the polling interval for Interrupt/Isochronous transfers or the NAK response timeout on Bulk transfers for Host Rx endpoint7 */
#define               USB_EP_NI7_TXCOUNT  0xffc03be8   /* Number of bytes to be written to the endpoint7 Tx FIFO */

#define                USB_DMA_INTERRUPT  0xffc03c00   /* Indicates pending interrupts for the DMA channels */

/* USB Channel 0 Config Registers */

#define                  USB_DMA0CONTROL  0xffc03c04   /* DMA master channel 0 configuration */
#define                  USB_DMA0ADDRLOW  0xffc03c08   /* Lower 16-bits of memory source/destination address for DMA master channel 0 */
#define                 USB_DMA0ADDRHIGH  0xffc03c0c   /* Upper 16-bits of memory source/destination address for DMA master channel 0 */
#define                 USB_DMA0COUNTLOW  0xffc03c10   /* Lower 16-bits of byte count of DMA transfer for DMA master channel 0 */
#define                USB_DMA0COUNTHIGH  0xffc03c14   /* Upper 16-bits of byte count of DMA transfer for DMA master channel 0 */

/* USB Channel 1 Config Registers */

#define                  USB_DMA1CONTROL  0xffc03c24   /* DMA master channel 1 configuration */
#define                  USB_DMA1ADDRLOW  0xffc03c28   /* Lower 16-bits of memory source/destination address for DMA master channel 1 */
#define                 USB_DMA1ADDRHIGH  0xffc03c2c   /* Upper 16-bits of memory source/destination address for DMA master channel 1 */
#define                 USB_DMA1COUNTLOW  0xffc03c30   /* Lower 16-bits of byte count of DMA transfer for DMA master channel 1 */
#define                USB_DMA1COUNTHIGH  0xffc03c34   /* Upper 16-bits of byte count of DMA transfer for DMA master channel 1 */

/* USB Channel 2 Config Registers */

#define                  USB_DMA2CONTROL  0xffc03c44   /* DMA master channel 2 configuration */
#define                  USB_DMA2ADDRLOW  0xffc03c48   /* Lower 16-bits of memory source/destination address for DMA master channel 2 */
#define                 USB_DMA2ADDRHIGH  0xffc03c4c   /* Upper 16-bits of memory source/destination address for DMA master channel 2 */
#define                 USB_DMA2COUNTLOW  0xffc03c50   /* Lower 16-bits of byte count of DMA transfer for DMA master channel 2 */
#define                USB_DMA2COUNTHIGH  0xffc03c54   /* Upper 16-bits of byte count of DMA transfer for DMA master channel 2 */

/* USB Channel 3 Config Registers */

#define                  USB_DMA3CONTROL  0xffc03c64   /* DMA master channel 3 configuration */
#define                  USB_DMA3ADDRLOW  0xffc03c68   /* Lower 16-bits of memory source/destination address for DMA master channel 3 */
#define                 USB_DMA3ADDRHIGH  0xffc03c6c   /* Upper 16-bits of memory source/destination address for DMA master channel 3 */
#define                 USB_DMA3COUNTLOW  0xffc03c70   /* Lower 16-bits of byte count of DMA transfer for DMA master channel 3 */
#define                USB_DMA3COUNTHIGH  0xffc03c74   /* Upper 16-bits of byte count of DMA transfer for DMA master channel 3 */

/* USB Channel 4 Config Registers */

#define                  USB_DMA4CONTROL  0xffc03c84   /* DMA master channel 4 configuration */
#define                  USB_DMA4ADDRLOW  0xffc03c88   /* Lower 16-bits of memory source/destination address for DMA master channel 4 */
#define                 USB_DMA4ADDRHIGH  0xffc03c8c   /* Upper 16-bits of memory source/destination address for DMA master channel 4 */
#define                 USB_DMA4COUNTLOW  0xffc03c90   /* Lower 16-bits of byte count of DMA transfer for DMA master channel 4 */
#define                USB_DMA4COUNTHIGH  0xffc03c94   /* Upper 16-bits of byte count of DMA transfer for DMA master channel 4 */

/* USB Channel 5 Config Registers */

#define                  USB_DMA5CONTROL  0xffc03ca4   /* DMA master channel 5 configuration */
#define                  USB_DMA5ADDRLOW  0xffc03ca8   /* Lower 16-bits of memory source/destination address for DMA master channel 5 */
#define                 USB_DMA5ADDRHIGH  0xffc03cac   /* Upper 16-bits of memory source/destination address for DMA master channel 5 */
#define                 USB_DMA5COUNTLOW  0xffc03cb0   /* Lower 16-bits of byte count of DMA transfer for DMA master channel 5 */
#define                USB_DMA5COUNTHIGH  0xffc03cb4   /* Upper 16-bits of byte count of DMA transfer for DMA master channel 5 */

/* USB Channel 6 Config Registers */

#define                  USB_DMA6CONTROL  0xffc03cc4   /* DMA master channel 6 configuration */
#define                  USB_DMA6ADDRLOW  0xffc03cc8   /* Lower 16-bits of memory source/destination address for DMA master channel 6 */
#define                 USB_DMA6ADDRHIGH  0xffc03ccc   /* Upper 16-bits of memory source/destination address for DMA master channel 6 */
#define                 USB_DMA6COUNTLOW  0xffc03cd0   /* Lower 16-bits of byte count of DMA transfer for DMA master channel 6 */
#define                USB_DMA6COUNTHIGH  0xffc03cd4   /* Upper 16-bits of byte count of DMA transfer for DMA master channel 6 */

/* USB Channel 7 Config Registers */

#define                  USB_DMA7CONTROL  0xffc03ce4   /* DMA master channel 7 configuration */
#define                  USB_DMA7ADDRLOW  0xffc03ce8   /* Lower 16-bits of memory source/destination address for DMA master channel 7 */
#define                 USB_DMA7ADDRHIGH  0xffc03cec   /* Upper 16-bits of memory source/destination address for DMA master channel 7 */
#define                 USB_DMA7COUNTLOW  0xffc03cf0   /* Lower 16-bits of byte count of DMA transfer for DMA master channel 7 */
#define                USB_DMA7COUNTHIGH  0xffc03cf4   /* Upper 16-bits of byte count of DMA transfer for DMA master channel 7 */

/* Bit masks for USB_FADDR */

#define          FUNCTION_ADDRESS  0x7f       /* Function address */

/* Bit masks for USB_POWER */

#define           ENABLE_SUSPENDM  0x1        /* enable SuspendM output */
#define          nENABLE_SUSPENDM  0x0       
#define              SUSPEND_MODE  0x2        /* Suspend Mode indicator */
#define             nSUSPEND_MODE  0x0       
#define               RESUME_MODE  0x4        /* DMA Mode */
#define              nRESUME_MODE  0x0       
#define                     RESET  0x8        /* Reset indicator */
#define                    nRESET  0x0       
#define                   HS_MODE  0x10       /* High Speed mode indicator */
#define                  nHS_MODE  0x0       
#define                 HS_ENABLE  0x20       /* high Speed Enable */
#define                nHS_ENABLE  0x0       
#define                 SOFT_CONN  0x40       /* Soft connect */
#define                nSOFT_CONN  0x0       
#define                ISO_UPDATE  0x80       /* Isochronous update */
#define               nISO_UPDATE  0x0       

/* Bit masks for USB_INTRTX */

#define                    EP0_TX  0x1        /* Tx Endpoint 0 interrupt */
#define                   nEP0_TX  0x0       
#define                    EP1_TX  0x2        /* Tx Endpoint 1 interrupt */
#define                   nEP1_TX  0x0       
#define                    EP2_TX  0x4        /* Tx Endpoint 2 interrupt */
#define                   nEP2_TX  0x0       
#define                    EP3_TX  0x8        /* Tx Endpoint 3 interrupt */
#define                   nEP3_TX  0x0       
#define                    EP4_TX  0x10       /* Tx Endpoint 4 interrupt */
#define                   nEP4_TX  0x0       
#define                    EP5_TX  0x20       /* Tx Endpoint 5 interrupt */
#define                   nEP5_TX  0x0       
#define                    EP6_TX  0x40       /* Tx Endpoint 6 interrupt */
#define                   nEP6_TX  0x0       
#define                    EP7_TX  0x80       /* Tx Endpoint 7 interrupt */
#define                   nEP7_TX  0x0       

/* Bit masks for USB_INTRRX */

#define                    EP1_RX  0x2        /* Rx Endpoint 1 interrupt */
#define                   nEP1_RX  0x0       
#define                    EP2_RX  0x4        /* Rx Endpoint 2 interrupt */
#define                   nEP2_RX  0x0       
#define                    EP3_RX  0x8        /* Rx Endpoint 3 interrupt */
#define                   nEP3_RX  0x0       
#define                    EP4_RX  0x10       /* Rx Endpoint 4 interrupt */
#define                   nEP4_RX  0x0       
#define                    EP5_RX  0x20       /* Rx Endpoint 5 interrupt */
#define                   nEP5_RX  0x0       
#define                    EP6_RX  0x40       /* Rx Endpoint 6 interrupt */
#define                   nEP6_RX  0x0       
#define                    EP7_RX  0x80       /* Rx Endpoint 7 interrupt */
#define                   nEP7_RX  0x0       

/* Bit masks for USB_INTRTXE */

#define                  EP0_TX_E  0x1        /* Endpoint 0 interrupt Enable */
#define                 nEP0_TX_E  0x0       
#define                  EP1_TX_E  0x2        /* Tx Endpoint 1 interrupt  Enable */
#define                 nEP1_TX_E  0x0       
#define                  EP2_TX_E  0x4        /* Tx Endpoint 2 interrupt  Enable */
#define                 nEP2_TX_E  0x0       
#define                  EP3_TX_E  0x8        /* Tx Endpoint 3 interrupt  Enable */
#define                 nEP3_TX_E  0x0       
#define                  EP4_TX_E  0x10       /* Tx Endpoint 4 interrupt  Enable */
#define                 nEP4_TX_E  0x0       
#define                  EP5_TX_E  0x20       /* Tx Endpoint 5 interrupt  Enable */
#define                 nEP5_TX_E  0x0       
#define                  EP6_TX_E  0x40       /* Tx Endpoint 6 interrupt  Enable */
#define                 nEP6_TX_E  0x0       
#define                  EP7_TX_E  0x80       /* Tx Endpoint 7 interrupt  Enable */
#define                 nEP7_TX_E  0x0       

/* Bit masks for USB_INTRRXE */

#define                  EP1_RX_E  0x2        /* Rx Endpoint 1 interrupt  Enable */
#define                 nEP1_RX_E  0x0       
#define                  EP2_RX_E  0x4        /* Rx Endpoint 2 interrupt  Enable */
#define                 nEP2_RX_E  0x0       
#define                  EP3_RX_E  0x8        /* Rx Endpoint 3 interrupt  Enable */
#define                 nEP3_RX_E  0x0       
#define                  EP4_RX_E  0x10       /* Rx Endpoint 4 interrupt  Enable */
#define                 nEP4_RX_E  0x0       
#define                  EP5_RX_E  0x20       /* Rx Endpoint 5 interrupt  Enable */
#define                 nEP5_RX_E  0x0       
#define                  EP6_RX_E  0x40       /* Rx Endpoint 6 interrupt  Enable */
#define                 nEP6_RX_E  0x0       
#define                  EP7_RX_E  0x80       /* Rx Endpoint 7 interrupt  Enable */
#define                 nEP7_RX_E  0x0       

/* Bit masks for USB_INTRUSB */

#define                 SUSPEND_B  0x1        /* Suspend indicator */
#define                nSUSPEND_B  0x0       
#define                  RESUME_B  0x2        /* Resume indicator */
#define                 nRESUME_B  0x0       
#define          RESET_OR_BABLE_B  0x4        /* Reset/babble indicator */
#define         nRESET_OR_BABLE_B  0x0       
#define                     SOF_B  0x8        /* Start of frame */
#define                    nSOF_B  0x0       
#define                    CONN_B  0x10       /* Connection indicator */
#define                   nCONN_B  0x0       
#define                  DISCON_B  0x20       /* Disconnect indicator */
#define                 nDISCON_B  0x0       
#define             SESSION_REQ_B  0x40       /* Session Request */
#define            nSESSION_REQ_B  0x0       
#define              VBUS_ERROR_B  0x80       /* Vbus threshold indicator */
#define             nVBUS_ERROR_B  0x0       

/* Bit masks for USB_INTRUSBE */

#define                SUSPEND_BE  0x1        /* Suspend indicator int enable */
#define               nSUSPEND_BE  0x0       
#define                 RESUME_BE  0x2        /* Resume indicator int enable */
#define                nRESUME_BE  0x0       
#define         RESET_OR_BABLE_BE  0x4        /* Reset/babble indicator int enable */
#define        nRESET_OR_BABLE_BE  0x0       
#define                    SOF_BE  0x8        /* Start of frame int enable */
#define                   nSOF_BE  0x0       
#define                   CONN_BE  0x10       /* Connection indicator int enable */
#define                  nCONN_BE  0x0       
#define                 DISCON_BE  0x20       /* Disconnect indicator int enable */
#define                nDISCON_BE  0x0       
#define            SESSION_REQ_BE  0x40       /* Session Request int enable */
#define           nSESSION_REQ_BE  0x0       
#define             VBUS_ERROR_BE  0x80       /* Vbus threshold indicator int enable */
#define            nVBUS_ERROR_BE  0x0       

/* Bit masks for USB_FRAME */

#define              FRAME_NUMBER  0x7ff      /* Frame number */

/* Bit masks for USB_INDEX */

#define         SELECTED_ENDPOINT  0xf        /* selected endpoint */

/* Bit masks for USB_GLOBAL_CTL */

#define                GLOBAL_ENA  0x1        /* enables USB module */
#define               nGLOBAL_ENA  0x0       
#define                EP1_TX_ENA  0x2        /* Transmit endpoint 1 enable */
#define               nEP1_TX_ENA  0x0       
#define                EP2_TX_ENA  0x4        /* Transmit endpoint 2 enable */
#define               nEP2_TX_ENA  0x0       
#define                EP3_TX_ENA  0x8        /* Transmit endpoint 3 enable */
#define               nEP3_TX_ENA  0x0       
#define                EP4_TX_ENA  0x10       /* Transmit endpoint 4 enable */
#define               nEP4_TX_ENA  0x0       
#define                EP5_TX_ENA  0x20       /* Transmit endpoint 5 enable */
#define               nEP5_TX_ENA  0x0       
#define                EP6_TX_ENA  0x40       /* Transmit endpoint 6 enable */
#define               nEP6_TX_ENA  0x0       
#define                EP7_TX_ENA  0x80       /* Transmit endpoint 7 enable */
#define               nEP7_TX_ENA  0x0       
#define                EP1_RX_ENA  0x100      /* Receive endpoint 1 enable */
#define               nEP1_RX_ENA  0x0       
#define                EP2_RX_ENA  0x200      /* Receive endpoint 2 enable */
#define               nEP2_RX_ENA  0x0       
#define                EP3_RX_ENA  0x400      /* Receive endpoint 3 enable */
#define               nEP3_RX_ENA  0x0       
#define                EP4_RX_ENA  0x800      /* Receive endpoint 4 enable */
#define               nEP4_RX_ENA  0x0       
#define                EP5_RX_ENA  0x1000     /* Receive endpoint 5 enable */
#define               nEP5_RX_ENA  0x0       
#define                EP6_RX_ENA  0x2000     /* Receive endpoint 6 enable */
#define               nEP6_RX_ENA  0x0       
#define                EP7_RX_ENA  0x4000     /* Receive endpoint 7 enable */
#define               nEP7_RX_ENA  0x0       

/* Bit masks for USB_OTG_DEV_CTL */

#define                   SESSION  0x1        /* session indicator */
#define                  nSESSION  0x0       
#define                  HOST_REQ  0x2        /* Host negotiation request */
#define                 nHOST_REQ  0x0       
#define                 HOST_MODE  0x4        /* indicates USBDRC is a host */
#define                nHOST_MODE  0x0       
#define                     VBUS0  0x8        /* Vbus level indicator[0] */
#define                    nVBUS0  0x0       
#define                     VBUS1  0x10       /* Vbus level indicator[1] */
#define                    nVBUS1  0x0       
#define                     LSDEV  0x20       /* Low-speed indicator */
#define                    nLSDEV  0x0       
#define                     FSDEV  0x40       /* Full or High-speed indicator */
#define                    nFSDEV  0x0       
#define                  B_DEVICE  0x80       /* A' or 'B' device indicator */
#define                 nB_DEVICE  0x0       

/* Bit masks for USB_OTG_VBUS_IRQ */

#define             DRIVE_VBUS_ON  0x1        /* indicator to drive VBUS control circuit */
#define            nDRIVE_VBUS_ON  0x0       
#define            DRIVE_VBUS_OFF  0x2        /* indicator to shut off charge pump */
#define           nDRIVE_VBUS_OFF  0x0       
#define           CHRG_VBUS_START  0x4        /* indicator for external circuit to start charging VBUS */
#define          nCHRG_VBUS_START  0x0       
#define             CHRG_VBUS_END  0x8        /* indicator for external circuit to end charging VBUS */
#define            nCHRG_VBUS_END  0x0       
#define        DISCHRG_VBUS_START  0x10       /* indicator to start discharging VBUS */
#define       nDISCHRG_VBUS_START  0x0       
#define          DISCHRG_VBUS_END  0x20       /* indicator to stop discharging VBUS */
#define         nDISCHRG_VBUS_END  0x0       

/* Bit masks for USB_OTG_VBUS_MASK */

#define         DRIVE_VBUS_ON_ENA  0x1        /* enable DRIVE_VBUS_ON interrupt */
#define        nDRIVE_VBUS_ON_ENA  0x0       
#define        DRIVE_VBUS_OFF_ENA  0x2        /* enable DRIVE_VBUS_OFF interrupt */
#define       nDRIVE_VBUS_OFF_ENA  0x0       
#define       CHRG_VBUS_START_ENA  0x4        /* enable CHRG_VBUS_START interrupt */
#define      nCHRG_VBUS_START_ENA  0x0       
#define         CHRG_VBUS_END_ENA  0x8        /* enable CHRG_VBUS_END interrupt */
#define        nCHRG_VBUS_END_ENA  0x0       
#define    DISCHRG_VBUS_START_ENA  0x10       /* enable DISCHRG_VBUS_START interrupt */
#define   nDISCHRG_VBUS_START_ENA  0x0       
#define      DISCHRG_VBUS_END_ENA  0x20       /* enable DISCHRG_VBUS_END interrupt */
#define     nDISCHRG_VBUS_END_ENA  0x0       

/* Bit masks for USB_CSR0 */

#define                  RXPKTRDY  0x1        /* data packet receive indicator */
#define                 nRXPKTRDY  0x0       
#define                  TXPKTRDY  0x2        /* data packet in FIFO indicator */
#define                 nTXPKTRDY  0x0       
#define                STALL_SENT  0x4        /* STALL handshake sent */
#define               nSTALL_SENT  0x0       
#define                   DATAEND  0x8        /* Data end indicator */
#define                  nDATAEND  0x0       
#define                  SETUPEND  0x10       /* Setup end */
#define                 nSETUPEND  0x0       
#define                 SENDSTALL  0x20       /* Send STALL handshake */
#define                nSENDSTALL  0x0       
#define         SERVICED_RXPKTRDY  0x40       /* used to clear the RxPktRdy bit */
#define        nSERVICED_RXPKTRDY  0x0       
#define         SERVICED_SETUPEND  0x80       /* used to clear the SetupEnd bit */
#define        nSERVICED_SETUPEND  0x0       
#define                 FLUSHFIFO  0x100      /* flush endpoint FIFO */
#define                nFLUSHFIFO  0x0       
#define          STALL_RECEIVED_H  0x4        /* STALL handshake received host mode */
#define         nSTALL_RECEIVED_H  0x0       
#define                SETUPPKT_H  0x8        /* send Setup token host mode */
#define               nSETUPPKT_H  0x0       
#define                   ERROR_H  0x10       /* timeout error indicator host mode */
#define                  nERROR_H  0x0       
#define                  REQPKT_H  0x20       /* Request an IN transaction host mode */
#define                 nREQPKT_H  0x0       
#define               STATUSPKT_H  0x40       /* Status stage transaction host mode */
#define              nSTATUSPKT_H  0x0       
#define             NAK_TIMEOUT_H  0x80       /* EP0 halted after a NAK host mode */
#define            nNAK_TIMEOUT_H  0x0       

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
#define               nTXPKTRDY_T  0x0       
#define          FIFO_NOT_EMPTY_T  0x2        /* FIFO not empty */
#define         nFIFO_NOT_EMPTY_T  0x0       
#define                UNDERRUN_T  0x4        /* TxPktRdy not set  for an IN token */
#define               nUNDERRUN_T  0x0       
#define               FLUSHFIFO_T  0x8        /* flush endpoint FIFO */
#define              nFLUSHFIFO_T  0x0       
#define              STALL_SEND_T  0x10       /* issue a Stall handshake */
#define             nSTALL_SEND_T  0x0       
#define              STALL_SENT_T  0x20       /* Stall handshake transmitted */
#define             nSTALL_SENT_T  0x0       
#define        CLEAR_DATATOGGLE_T  0x40       /* clear endpoint data toggle */
#define       nCLEAR_DATATOGGLE_T  0x0       
#define                INCOMPTX_T  0x80       /* indicates that a large packet is split */
#define               nINCOMPTX_T  0x0       
#define              DMAREQMODE_T  0x400      /* DMA mode (0 or 1) selection */
#define             nDMAREQMODE_T  0x0       
#define        FORCE_DATATOGGLE_T  0x800      /* Force data toggle */
#define       nFORCE_DATATOGGLE_T  0x0       
#define              DMAREQ_ENA_T  0x1000     /* Enable DMA request for Tx EP */
#define             nDMAREQ_ENA_T  0x0       
#define                     ISO_T  0x4000     /* enable Isochronous transfers */
#define                    nISO_T  0x0       
#define                 AUTOSET_T  0x8000     /* allows TxPktRdy to be set automatically */
#define                nAUTOSET_T  0x0       
#define                  ERROR_TH  0x4        /* error condition host mode */
#define                 nERROR_TH  0x0       
#define         STALL_RECEIVED_TH  0x20       /* Stall handshake received host mode */
#define        nSTALL_RECEIVED_TH  0x0       
#define            NAK_TIMEOUT_TH  0x80       /* NAK timeout host mode */
#define           nNAK_TIMEOUT_TH  0x0       

/* Bit masks for USB_TXCOUNT */

#define                  TX_COUNT  0x1fff     /* Number of bytes to be written to the selected endpoint Tx FIFO */

/* Bit masks for USB_RXCSR */

#define                RXPKTRDY_R  0x1        /* data packet in FIFO indicator */
#define               nRXPKTRDY_R  0x0       
#define               FIFO_FULL_R  0x2        /* FIFO not empty */
#define              nFIFO_FULL_R  0x0       
#define                 OVERRUN_R  0x4        /* TxPktRdy not set  for an IN token */
#define                nOVERRUN_R  0x0       
#define               DATAERROR_R  0x8        /* Out packet cannot be loaded into Rx  FIFO */
#define              nDATAERROR_R  0x0       
#define               FLUSHFIFO_R  0x10       /* flush endpoint FIFO */
#define              nFLUSHFIFO_R  0x0       
#define              STALL_SEND_R  0x20       /* issue a Stall handshake */
#define             nSTALL_SEND_R  0x0       
#define              STALL_SENT_R  0x40       /* Stall handshake transmitted */
#define             nSTALL_SENT_R  0x0       
#define        CLEAR_DATATOGGLE_R  0x80       /* clear endpoint data toggle */
#define       nCLEAR_DATATOGGLE_R  0x0       
#define                INCOMPRX_R  0x100      /* indicates that a large packet is split */
#define               nINCOMPRX_R  0x0       
#define              DMAREQMODE_R  0x800      /* DMA mode (0 or 1) selection */
#define             nDMAREQMODE_R  0x0       
#define                 DISNYET_R  0x1000     /* disable Nyet handshakes */
#define                nDISNYET_R  0x0       
#define              DMAREQ_ENA_R  0x2000     /* Enable DMA request for Tx EP */
#define             nDMAREQ_ENA_R  0x0       
#define                     ISO_R  0x4000     /* enable Isochronous transfers */
#define                    nISO_R  0x0       
#define               AUTOCLEAR_R  0x8000     /* allows TxPktRdy to be set automatically */
#define              nAUTOCLEAR_R  0x0       
#define                  ERROR_RH  0x4        /* TxPktRdy not set  for an IN token host mode */
#define                 nERROR_RH  0x0       
#define                 REQPKT_RH  0x20       /* request an IN transaction host mode */
#define                nREQPKT_RH  0x0       
#define         STALL_RECEIVED_RH  0x40       /* Stall handshake received host mode */
#define        nSTALL_RECEIVED_RH  0x0       
#define               INCOMPRX_RH  0x100      /* indicates that a large packet is split host mode */
#define              nINCOMPRX_RH  0x0       
#define             DMAREQMODE_RH  0x800      /* DMA mode (0 or 1) selection host mode */
#define            nDMAREQMODE_RH  0x0       
#define                AUTOREQ_RH  0x4000     /* sets ReqPkt automatically host mode */
#define               nAUTOREQ_RH  0x0       

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
#define                 nDMA0_INT  0x0       
#define                  DMA1_INT  0x2        /* DMA1 pending interrupt */
#define                 nDMA1_INT  0x0       
#define                  DMA2_INT  0x4        /* DMA2 pending interrupt */
#define                 nDMA2_INT  0x0       
#define                  DMA3_INT  0x8        /* DMA3 pending interrupt */
#define                 nDMA3_INT  0x0       
#define                  DMA4_INT  0x10       /* DMA4 pending interrupt */
#define                 nDMA4_INT  0x0       
#define                  DMA5_INT  0x20       /* DMA5 pending interrupt */
#define                 nDMA5_INT  0x0       
#define                  DMA6_INT  0x40       /* DMA6 pending interrupt */
#define                 nDMA6_INT  0x0       
#define                  DMA7_INT  0x80       /* DMA7 pending interrupt */
#define                 nDMA7_INT  0x0       

/* Bit masks for USB_DMAxCONTROL */

#define                   DMA_ENA  0x1        /* DMA enable */
#define                  nDMA_ENA  0x0       
#define                 DIRECTION  0x2        /* direction of DMA transfer */
#define                nDIRECTION  0x0       
#define                      MODE  0x4        /* DMA Bus error */
#define                     nMODE  0x0       
#define                   INT_ENA  0x8        /* Interrupt enable */
#define                  nINT_ENA  0x0       
#define                     EPNUM  0xf0       /* EP number */
#define                  BUSERROR  0x100      /* DMA Bus error */
#define                 nBUSERROR  0x0       

/* Bit masks for USB_DMAxADDRHIGH */

#define             DMA_ADDR_HIGH  0xffff     /* Upper 16-bits of memory source/destination address for the DMA master channel */

/* Bit masks for USB_DMAxADDRLOW */

#define              DMA_ADDR_LOW  0xffff     /* Lower 16-bits of memory source/destination address for the DMA master channel */

/* Bit masks for USB_DMAxCOUNTHIGH */

#define            DMA_COUNT_HIGH  0xffff     /* Upper 16-bits of byte count of DMA transfer for DMA master channel */

/* Bit masks for USB_DMAxCOUNTLOW */

#define             DMA_COUNT_LOW  0xffff     /* Lower 16-bits of byte count of DMA transfer for DMA master channel */

#endif /* _DEF_BF525_H */
