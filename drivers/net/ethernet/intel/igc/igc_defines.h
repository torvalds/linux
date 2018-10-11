/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c)  2018 Intel Corporation */

#ifndef _IGC_DEFINES_H_
#define _IGC_DEFINES_H_

#define IGC_CTRL_EXT_DRV_LOAD	0x10000000 /* Drv loaded bit for FW */

/* PCI Bus Info */
#define PCIE_DEVICE_CONTROL2		0x28
#define PCIE_DEVICE_CONTROL2_16ms	0x0005

/* Receive Address
 * Number of high/low register pairs in the RAR. The RAR (Receive Address
 * Registers) holds the directed and multicast addresses that we monitor.
 * Technically, we have 16 spots.  However, we reserve one of these spots
 * (RAR[15]) for our directed address used by controllers with
 * manageability enabled, allowing us room for 15 multicast addresses.
 */
#define IGC_RAH_AV		0x80000000 /* Receive descriptor valid */
#define IGC_RAH_POOL_1		0x00040000

/* Error Codes */
#define IGC_SUCCESS			0
#define IGC_ERR_NVM			1
#define IGC_ERR_PHY			2
#define IGC_ERR_CONFIG			3
#define IGC_ERR_PARAM			4
#define IGC_ERR_MAC_INIT		5
#define IGC_ERR_RESET			9

/* PBA constants */
#define IGC_PBA_34K		0x0022

/* Device Status */
#define IGC_STATUS_FD		0x00000001      /* Full duplex.0=half,1=full */
#define IGC_STATUS_LU		0x00000002      /* Link up.0=no,1=link */
#define IGC_STATUS_FUNC_MASK	0x0000000C      /* PCI Function Mask */
#define IGC_STATUS_FUNC_SHIFT	2
#define IGC_STATUS_FUNC_1	0x00000004      /* Function 1 */
#define IGC_STATUS_TXOFF	0x00000010      /* transmission paused */
#define IGC_STATUS_SPEED_100	0x00000040      /* Speed 100Mb/s */
#define IGC_STATUS_SPEED_1000	0x00000080      /* Speed 1000Mb/s */

/* Interrupt Cause Read */
#define IGC_ICR_TXDW		BIT(0)	/* Transmit desc written back */
#define IGC_ICR_TXQE		BIT(1)	/* Transmit Queue empty */
#define IGC_ICR_LSC		BIT(2)	/* Link Status Change */
#define IGC_ICR_RXSEQ		BIT(3)	/* Rx sequence error */
#define IGC_ICR_RXDMT0		BIT(4)	/* Rx desc min. threshold (0) */
#define IGC_ICR_RXO		BIT(6)	/* Rx overrun */
#define IGC_ICR_RXT0		BIT(7)	/* Rx timer intr (ring 0) */
#define IGC_ICR_DRSTA		BIT(30)	/* Device Reset Asserted */
#define IGC_ICS_RXT0		IGC_ICR_RXT0 /* Rx timer intr */

#define IMS_ENABLE_MASK ( \
	IGC_IMS_RXT0   |    \
	IGC_IMS_TXDW   |    \
	IGC_IMS_RXDMT0 |    \
	IGC_IMS_RXSEQ  |    \
	IGC_IMS_LSC)

/* Interrupt Mask Set */
#define IGC_IMS_TXDW		IGC_ICR_TXDW	/* Tx desc written back */
#define IGC_IMS_RXSEQ		IGC_ICR_RXSEQ	/* Rx sequence error */
#define IGC_IMS_LSC		IGC_ICR_LSC	/* Link Status Change */
#define IGC_IMS_DOUTSYNC	IGC_ICR_DOUTSYNC /* NIC DMA out of sync */
#define IGC_IMS_DRSTA		IGC_ICR_DRSTA	/* Device Reset Asserted */
#define IGC_IMS_RXT0		IGC_ICR_RXT0	/* Rx timer intr */
#define IGC_IMS_RXDMT0		IGC_ICR_RXDMT0	/* Rx desc min. threshold */

#define IGC_QVECTOR_MASK	0x7FFC		/* Q-vector mask */
#define IGC_ITR_VAL_MASK	0x04		/* ITR value mask */

#define IGC_ICR_DOUTSYNC	0x10000000 /* NIC DMA out of sync */
#define IGC_EITR_CNT_IGNR	0x80000000 /* Don't reset counters on write */
#define IGC_IVAR_VALID		0x80
#define IGC_GPIE_NSICR		0x00000001
#define IGC_GPIE_MSIX_MODE	0x00000010
#define IGC_GPIE_EIAME		0x40000000
#define IGC_GPIE_PBA		0x80000000

#define IGC_N0_QUEUE -1

#endif /* _IGC_DEFINES_H_ */
