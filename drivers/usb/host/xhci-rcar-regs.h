/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __XHCI_RCAR_H
#define __XHCI_RCAR_H

/*** Register Offset ***/
#define RCAR_USB3_AXH_STA	0x104	/* AXI Host Control Status */
#define RCAR_USB3_INT_ENA	0x224	/* Interrupt Enable */
#define RCAR_USB3_DL_CTRL	0x250	/* FW Download Control & Status */
#define RCAR_USB3_FW_DATA0	0x258	/* FW Data0 */

#define RCAR_USB3_LCLK		0xa44	/* LCLK Select */
#define RCAR_USB3_CONF1		0xa48	/* USB3.0 Configuration1 */
#define RCAR_USB3_CONF2		0xa5c	/* USB3.0 Configuration2 */
#define RCAR_USB3_CONF3		0xaa8	/* USB3.0 Configuration3 */
#define RCAR_USB3_RX_POL	0xab0	/* USB3.0 RX Polarity */
#define RCAR_USB3_TX_POL	0xab8	/* USB3.0 TX Polarity */

/*** Register Settings ***/
/* AXI Host Control Status */
#define RCAR_USB3_AXH_STA_B3_PLL_ACTIVE		0x00010000
#define RCAR_USB3_AXH_STA_B2_PLL_ACTIVE		0x00000001
#define RCAR_USB3_AXH_STA_PLL_ACTIVE_MASK (RCAR_USB3_AXH_STA_B3_PLL_ACTIVE | \
					   RCAR_USB3_AXH_STA_B2_PLL_ACTIVE)

/* Interrupt Enable */
#define RCAR_USB3_INT_XHC_ENA	0x00000001
#define RCAR_USB3_INT_PME_ENA	0x00000002
#define RCAR_USB3_INT_HSE_ENA	0x00000004
#define RCAR_USB3_INT_ENA_VAL	(RCAR_USB3_INT_XHC_ENA | \
				RCAR_USB3_INT_PME_ENA | RCAR_USB3_INT_HSE_ENA)

/* FW Download Control & Status */
#define RCAR_USB3_DL_CTRL_ENABLE	0x00000001
#define RCAR_USB3_DL_CTRL_FW_SUCCESS	0x00000010
#define RCAR_USB3_DL_CTRL_FW_SET_DATA0	0x00000100

/* LCLK Select */
#define RCAR_USB3_LCLK_ENA_VAL	0x01030001

/* USB3.0 Configuration */
#define RCAR_USB3_CONF1_VAL	0x00030204
#define RCAR_USB3_CONF2_VAL	0x00030300
#define RCAR_USB3_CONF3_VAL	0x13802007

/* USB3.0 Polarity */
#define RCAR_USB3_RX_POL_VAL	BIT(21)
#define RCAR_USB3_TX_POL_VAL	BIT(4)

#endif /* __XHCI_RCAR_H */
