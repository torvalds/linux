#ifndef _ASM_ARCH_PXA25X_UDC_H
#define _ASM_ARCH_PXA25X_UDC_H

#ifdef _ASM_ARCH_PXA27X_UDC_H
#error "You can't include both PXA25x and PXA27x UDC support"
#endif

#define UDC_RES1	__REG(0x40600004)  /* UDC Undocumented - Reserved1 */
#define UDC_RES2	__REG(0x40600008)  /* UDC Undocumented - Reserved2 */
#define UDC_RES3	__REG(0x4060000C)  /* UDC Undocumented - Reserved3 */

#define UDCCR		__REG(0x40600000)  /* UDC Control Register */
#define UDCCR_UDE	(1 << 0)	/* UDC enable */
#define UDCCR_UDA	(1 << 1)	/* UDC active */
#define UDCCR_RSM	(1 << 2)	/* Device resume */
#define UDCCR_RESIR	(1 << 3)	/* Resume interrupt request */
#define UDCCR_SUSIR	(1 << 4)	/* Suspend interrupt request */
#define UDCCR_SRM	(1 << 5)	/* Suspend/resume interrupt mask */
#define UDCCR_RSTIR	(1 << 6)	/* Reset interrupt request */
#define UDCCR_REM	(1 << 7)	/* Reset interrupt mask */

#define UDCCS0		__REG(0x40600010)  /* UDC Endpoint 0 Control/Status Register */
#define UDCCS0_OPR	(1 << 0)	/* OUT packet ready */
#define UDCCS0_IPR	(1 << 1)	/* IN packet ready */
#define UDCCS0_FTF	(1 << 2)	/* Flush Tx FIFO */
#define UDCCS0_DRWF	(1 << 3)	/* Device remote wakeup feature */
#define UDCCS0_SST	(1 << 4)	/* Sent stall */
#define UDCCS0_FST	(1 << 5)	/* Force stall */
#define UDCCS0_RNE	(1 << 6)	/* Receive FIFO no empty */
#define UDCCS0_SA	(1 << 7)	/* Setup active */

/* Bulk IN - Endpoint 1,6,11 */
#define UDCCS1		__REG(0x40600014)  /* UDC Endpoint 1 (IN) Control/Status Register */
#define UDCCS6		__REG(0x40600028)  /* UDC Endpoint 6 (IN) Control/Status Register */
#define UDCCS11		__REG(0x4060003C)  /* UDC Endpoint 11 (IN) Control/Status Register */

#define UDCCS_BI_TFS	(1 << 0)	/* Transmit FIFO service */
#define UDCCS_BI_TPC	(1 << 1)	/* Transmit packet complete */
#define UDCCS_BI_FTF	(1 << 2)	/* Flush Tx FIFO */
#define UDCCS_BI_TUR	(1 << 3)	/* Transmit FIFO underrun */
#define UDCCS_BI_SST	(1 << 4)	/* Sent stall */
#define UDCCS_BI_FST	(1 << 5)	/* Force stall */
#define UDCCS_BI_TSP	(1 << 7)	/* Transmit short packet */

/* Bulk OUT - Endpoint 2,7,12 */
#define UDCCS2		__REG(0x40600018)  /* UDC Endpoint 2 (OUT) Control/Status Register */
#define UDCCS7		__REG(0x4060002C)  /* UDC Endpoint 7 (OUT) Control/Status Register */
#define UDCCS12		__REG(0x40600040)  /* UDC Endpoint 12 (OUT) Control/Status Register */

#define UDCCS_BO_RFS	(1 << 0)	/* Receive FIFO service */
#define UDCCS_BO_RPC	(1 << 1)	/* Receive packet complete */
#define UDCCS_BO_DME	(1 << 3)	/* DMA enable */
#define UDCCS_BO_SST	(1 << 4)	/* Sent stall */
#define UDCCS_BO_FST	(1 << 5)	/* Force stall */
#define UDCCS_BO_RNE	(1 << 6)	/* Receive FIFO not empty */
#define UDCCS_BO_RSP	(1 << 7)	/* Receive short packet */

/* Isochronous IN - Endpoint 3,8,13 */
#define UDCCS3		__REG(0x4060001C)  /* UDC Endpoint 3 (IN) Control/Status Register */
#define UDCCS8		__REG(0x40600030)  /* UDC Endpoint 8 (IN) Control/Status Register */
#define UDCCS13		__REG(0x40600044)  /* UDC Endpoint 13 (IN) Control/Status Register */

#define UDCCS_II_TFS	(1 << 0)	/* Transmit FIFO service */
#define UDCCS_II_TPC	(1 << 1)	/* Transmit packet complete */
#define UDCCS_II_FTF	(1 << 2)	/* Flush Tx FIFO */
#define UDCCS_II_TUR	(1 << 3)	/* Transmit FIFO underrun */
#define UDCCS_II_TSP	(1 << 7)	/* Transmit short packet */

/* Isochronous OUT - Endpoint 4,9,14 */
#define UDCCS4		__REG(0x40600020)  /* UDC Endpoint 4 (OUT) Control/Status Register */
#define UDCCS9		__REG(0x40600034)  /* UDC Endpoint 9 (OUT) Control/Status Register */
#define UDCCS14		__REG(0x40600048)  /* UDC Endpoint 14 (OUT) Control/Status Register */

#define UDCCS_IO_RFS	(1 << 0)	/* Receive FIFO service */
#define UDCCS_IO_RPC	(1 << 1)	/* Receive packet complete */
#define UDCCS_IO_ROF	(1 << 2)	/* Receive overflow */
#define UDCCS_IO_DME	(1 << 3)	/* DMA enable */
#define UDCCS_IO_RNE	(1 << 6)	/* Receive FIFO not empty */
#define UDCCS_IO_RSP	(1 << 7)	/* Receive short packet */

/* Interrupt IN - Endpoint 5,10,15 */
#define UDCCS5		__REG(0x40600024)  /* UDC Endpoint 5 (Interrupt) Control/Status Register */
#define UDCCS10		__REG(0x40600038)  /* UDC Endpoint 10 (Interrupt) Control/Status Register */
#define UDCCS15		__REG(0x4060004C)  /* UDC Endpoint 15 (Interrupt) Control/Status Register */

#define UDCCS_INT_TFS	(1 << 0)	/* Transmit FIFO service */
#define UDCCS_INT_TPC	(1 << 1)	/* Transmit packet complete */
#define UDCCS_INT_FTF	(1 << 2)	/* Flush Tx FIFO */
#define UDCCS_INT_TUR	(1 << 3)	/* Transmit FIFO underrun */
#define UDCCS_INT_SST	(1 << 4)	/* Sent stall */
#define UDCCS_INT_FST	(1 << 5)	/* Force stall */
#define UDCCS_INT_TSP	(1 << 7)	/* Transmit short packet */

#define UFNRH		__REG(0x40600060)  /* UDC Frame Number Register High */
#define UFNRL		__REG(0x40600064)  /* UDC Frame Number Register Low */
#define UBCR2		__REG(0x40600068)  /* UDC Byte Count Reg 2 */
#define UBCR4		__REG(0x4060006c)  /* UDC Byte Count Reg 4 */
#define UBCR7		__REG(0x40600070)  /* UDC Byte Count Reg 7 */
#define UBCR9		__REG(0x40600074)  /* UDC Byte Count Reg 9 */
#define UBCR12		__REG(0x40600078)  /* UDC Byte Count Reg 12 */
#define UBCR14		__REG(0x4060007c)  /* UDC Byte Count Reg 14 */
#define UDDR0		__REG(0x40600080)  /* UDC Endpoint 0 Data Register */
#define UDDR1		__REG(0x40600100)  /* UDC Endpoint 1 Data Register */
#define UDDR2		__REG(0x40600180)  /* UDC Endpoint 2 Data Register */
#define UDDR3		__REG(0x40600200)  /* UDC Endpoint 3 Data Register */
#define UDDR4		__REG(0x40600400)  /* UDC Endpoint 4 Data Register */
#define UDDR5		__REG(0x406000A0)  /* UDC Endpoint 5 Data Register */
#define UDDR6		__REG(0x40600600)  /* UDC Endpoint 6 Data Register */
#define UDDR7		__REG(0x40600680)  /* UDC Endpoint 7 Data Register */
#define UDDR8		__REG(0x40600700)  /* UDC Endpoint 8 Data Register */
#define UDDR9		__REG(0x40600900)  /* UDC Endpoint 9 Data Register */
#define UDDR10		__REG(0x406000C0)  /* UDC Endpoint 10 Data Register */
#define UDDR11		__REG(0x40600B00)  /* UDC Endpoint 11 Data Register */
#define UDDR12		__REG(0x40600B80)  /* UDC Endpoint 12 Data Register */
#define UDDR13		__REG(0x40600C00)  /* UDC Endpoint 13 Data Register */
#define UDDR14		__REG(0x40600E00)  /* UDC Endpoint 14 Data Register */
#define UDDR15		__REG(0x406000E0)  /* UDC Endpoint 15 Data Register */

#define UICR0		__REG(0x40600050)  /* UDC Interrupt Control Register 0 */

#define UICR0_IM0	(1 << 0)	/* Interrupt mask ep 0 */
#define UICR0_IM1	(1 << 1)	/* Interrupt mask ep 1 */
#define UICR0_IM2	(1 << 2)	/* Interrupt mask ep 2 */
#define UICR0_IM3	(1 << 3)	/* Interrupt mask ep 3 */
#define UICR0_IM4	(1 << 4)	/* Interrupt mask ep 4 */
#define UICR0_IM5	(1 << 5)	/* Interrupt mask ep 5 */
#define UICR0_IM6	(1 << 6)	/* Interrupt mask ep 6 */
#define UICR0_IM7	(1 << 7)	/* Interrupt mask ep 7 */

#define UICR1		__REG(0x40600054)  /* UDC Interrupt Control Register 1 */

#define UICR1_IM8	(1 << 0)	/* Interrupt mask ep 8 */
#define UICR1_IM9	(1 << 1)	/* Interrupt mask ep 9 */
#define UICR1_IM10	(1 << 2)	/* Interrupt mask ep 10 */
#define UICR1_IM11	(1 << 3)	/* Interrupt mask ep 11 */
#define UICR1_IM12	(1 << 4)	/* Interrupt mask ep 12 */
#define UICR1_IM13	(1 << 5)	/* Interrupt mask ep 13 */
#define UICR1_IM14	(1 << 6)	/* Interrupt mask ep 14 */
#define UICR1_IM15	(1 << 7)	/* Interrupt mask ep 15 */

#define USIR0		__REG(0x40600058)  /* UDC Status Interrupt Register 0 */

#define USIR0_IR0	(1 << 0)	/* Interrupt request ep 0 */
#define USIR0_IR1	(1 << 1)	/* Interrupt request ep 1 */
#define USIR0_IR2	(1 << 2)	/* Interrupt request ep 2 */
#define USIR0_IR3	(1 << 3)	/* Interrupt request ep 3 */
#define USIR0_IR4	(1 << 4)	/* Interrupt request ep 4 */
#define USIR0_IR5	(1 << 5)	/* Interrupt request ep 5 */
#define USIR0_IR6	(1 << 6)	/* Interrupt request ep 6 */
#define USIR0_IR7	(1 << 7)	/* Interrupt request ep 7 */

#define USIR1		__REG(0x4060005C)  /* UDC Status Interrupt Register 1 */

#define USIR1_IR8	(1 << 0)	/* Interrupt request ep 8 */
#define USIR1_IR9	(1 << 1)	/* Interrupt request ep 9 */
#define USIR1_IR10	(1 << 2)	/* Interrupt request ep 10 */
#define USIR1_IR11	(1 << 3)	/* Interrupt request ep 11 */
#define USIR1_IR12	(1 << 4)	/* Interrupt request ep 12 */
#define USIR1_IR13	(1 << 5)	/* Interrupt request ep 13 */
#define USIR1_IR14	(1 << 6)	/* Interrupt request ep 14 */
#define USIR1_IR15	(1 << 7)	/* Interrupt request ep 15 */

#endif
