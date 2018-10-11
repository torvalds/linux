/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c)  2018 Intel Corporation */

#ifndef _IGC_DEFINES_H_
#define _IGC_DEFINES_H_

/* PCI Bus Info */
#define PCIE_DEVICE_CONTROL2		0x28
#define PCIE_DEVICE_CONTROL2_16ms	0x0005

/* Error Codes */
#define IGC_SUCCESS			0
#define IGC_ERR_NVM			1
#define IGC_ERR_PHY			2
#define IGC_ERR_CONFIG			3
#define IGC_ERR_PARAM			4
#define IGC_ERR_MAC_INIT		5
#define IGC_ERR_RESET			9

/* Device Status */
#define IGC_STATUS_FD		0x00000001      /* Full duplex.0=half,1=full */
#define IGC_STATUS_LU		0x00000002      /* Link up.0=no,1=link */
#define IGC_STATUS_FUNC_MASK	0x0000000C      /* PCI Function Mask */
#define IGC_STATUS_FUNC_SHIFT	2
#define IGC_STATUS_FUNC_1	0x00000004      /* Function 1 */
#define IGC_STATUS_TXOFF	0x00000010      /* transmission paused */
#define IGC_STATUS_SPEED_100	0x00000040      /* Speed 100Mb/s */
#define IGC_STATUS_SPEED_1000	0x00000080      /* Speed 1000Mb/s */

#endif /* _IGC_DEFINES_H_ */
