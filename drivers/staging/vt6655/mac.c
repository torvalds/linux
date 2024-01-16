// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 * Purpose:  MAC routines
 *
 * Author: Tevin Chen
 *
 * Date: May 21, 1996
 *
 * Functions:
 *      vt6655_mac_is_reg_bits_off - Test if All test Bits Off
 *      vt6655_mac_set_short_retry_limit - Set 802.11 Short Retry limit
 *      MACvSetLongRetryLimit - Set 802.11 Long Retry limit
 *      vt6655_mac_set_loopback_mode - Set MAC Loopback Mode
 *      vt6655_mac_save_context - Save Context of MAC Registers
 *      vt6655_mac_restore_context - Restore Context of MAC Registers
 *      MACbSoftwareReset - Software Reset MAC
 *      vt6655_mac_safe_rx_off - Turn Off MAC Rx
 *      vt6655_mac_safe_tx_off - Turn Off MAC Tx
 *      vt6655_mac_safe_stop - Stop MAC function
 *      MACbShutdown - Shut down MAC
 *      MACvInitialize - Initialize MAC
 *      MACvSetCurrRxDescAddr - Set Rx Descriptors Address
 *      MACvSetCurrTx0DescAddr - Set Tx0 Descriptors Address
 *      MACvSetCurrTx1DescAddr - Set Tx1 Descriptors Address
 *      MACvTimer0MicroSDelay - Micro Second Delay Loop by MAC
 *
 * Revision History:
 *      08-22-2003 Kyle Hsu     :  Porting MAC functions from sim53
 *      09-03-2003 Bryan YC Fan :  Add MACvClearBusSusInd()&
 *				   MACvEnableBusSusEn()
 *      09-18-2003 Jerry Chen   :  Add MACvSetKeyEntry & MACvDisableKeyEntry
 *
 */

#include "mac.h"

void vt6655_mac_reg_bits_on(void __iomem *iobase, const u8 reg_offset, const u8 bit_mask)
{
	unsigned char reg_value;

	reg_value = ioread8(iobase + reg_offset);
	iowrite8(reg_value | bit_mask, iobase + reg_offset);
}

void vt6655_mac_word_reg_bits_on(void __iomem *iobase, const u8 reg_offset, const u16 bit_mask)
{
	unsigned short reg_value;

	reg_value = ioread16(iobase + reg_offset);
	iowrite16(reg_value | (bit_mask), iobase + reg_offset);
}

void vt6655_mac_reg_bits_off(void __iomem *iobase, const u8 reg_offset, const u8 bit_mask)
{
	unsigned char reg_value;

	reg_value = ioread8(iobase + reg_offset);
	iowrite8(reg_value & ~(bit_mask), iobase + reg_offset);
}

void vt6655_mac_word_reg_bits_off(void __iomem *iobase, const u8 reg_offset, const u16 bit_mask)
{
	unsigned short reg_value;

	reg_value = ioread16(iobase + reg_offset);
	iowrite16(reg_value & ~(bit_mask), iobase + reg_offset);
}

static void vt6655_mac_clear_stck_ds(void __iomem *iobase)
{
	u8 reg_value;

	reg_value = ioread8(iobase + MAC_REG_STICKHW);
	reg_value = reg_value & 0xFC;
	iowrite8(reg_value, iobase + MAC_REG_STICKHW);
}

/*
 * Description:
 *      Test if all test bits off
 *
 * Parameters:
 *  In:
 *      io_base    - Base Address for MAC
 *      reg_offset - Offset of MAC Register
 *      mask       - Test bits
 *  Out:
 *      none
 *
 * Return Value: true if all test bits Off; otherwise false
 *
 */
static bool vt6655_mac_is_reg_bits_off(struct vnt_private *priv,
				       unsigned char reg_offset,
				       unsigned char mask)
{
	void __iomem *io_base = priv->port_offset;

	return !(ioread8(io_base + reg_offset) & mask);
}

/*
 * Description:
 *      Set 802.11 Short Retry Limit
 *
 * Parameters:
 *  In:
 *      io_base    - Base Address for MAC
 *      retry_limit - Retry Limit
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void vt6655_mac_set_short_retry_limit(struct vnt_private *priv, unsigned char retry_limit)
{
	void __iomem *io_base = priv->port_offset;
	/* set SRT */
	iowrite8(retry_limit, io_base + MAC_REG_SRT);
}

/*
 * Description:
 *      Set 802.11 Long Retry Limit
 *
 * Parameters:
 *  In:
 *      io_base    - Base Address for MAC
 *      byRetryLimit- Retry Limit
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void MACvSetLongRetryLimit(struct vnt_private *priv,
			   unsigned char byRetryLimit)
{
	void __iomem *io_base = priv->port_offset;
	/* set LRT */
	iowrite8(byRetryLimit, io_base + MAC_REG_LRT);
}

/*
 * Description:
 *      Set MAC Loopback mode
 *
 * Parameters:
 *  In:
 *      io_base        - Base Address for MAC
 *      loopback_mode  - Loopback Mode
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
static void vt6655_mac_set_loopback_mode(struct vnt_private *priv, u8 loopback_mode)
{
	void __iomem *io_base = priv->port_offset;

	loopback_mode <<= 6;
	/* set TCR */
	iowrite8((ioread8(io_base + MAC_REG_TEST) & 0x3f) | loopback_mode, io_base + MAC_REG_TEST);
}

/*
 * Description:
 *      Save MAC registers to context buffer
 *
 * Parameters:
 *  In:
 *      io_base    - Base Address for MAC
 *  Out:
 *      cxt_buf   - Context buffer
 *
 * Return Value: none
 *
 */
static void vt6655_mac_save_context(struct vnt_private *priv, u8 *cxt_buf)
{
	void __iomem *io_base = priv->port_offset;

	/* read page0 register */
	memcpy_fromio(cxt_buf, io_base, MAC_MAX_CONTEXT_SIZE_PAGE0);

	VT6655_MAC_SELECT_PAGE1(io_base);

	/* read page1 register */
	memcpy_fromio(cxt_buf + MAC_MAX_CONTEXT_SIZE_PAGE0, io_base,
		      MAC_MAX_CONTEXT_SIZE_PAGE1);

	VT6655_MAC_SELECT_PAGE0(io_base);
}

/*
 * Description:
 *      Restore MAC registers from context buffer
 *
 * Parameters:
 *  In:
 *      io_base    - Base Address for MAC
 *      cxt_buf   - Context buffer
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
static void vt6655_mac_restore_context(struct vnt_private *priv, u8 *cxt_buf)
{
	void __iomem *io_base = priv->port_offset;

	VT6655_MAC_SELECT_PAGE1(io_base);
	/* restore page1 */
	memcpy_toio(io_base, cxt_buf + MAC_MAX_CONTEXT_SIZE_PAGE0,
		    MAC_MAX_CONTEXT_SIZE_PAGE1);

	VT6655_MAC_SELECT_PAGE0(io_base);

	/* restore RCR,TCR,IMR... */
	memcpy_toio(io_base + MAC_REG_RCR, cxt_buf + MAC_REG_RCR,
		    MAC_REG_ISR - MAC_REG_RCR);

	/* restore MAC Config. */
	memcpy_toio(io_base + MAC_REG_LRT, cxt_buf + MAC_REG_LRT,
		    MAC_REG_PAGE1SEL - MAC_REG_LRT);

	iowrite8(*(cxt_buf + MAC_REG_CFG), io_base + MAC_REG_CFG);

	/* restore PS Config. */
	memcpy_toio(io_base + MAC_REG_PSCFG, cxt_buf + MAC_REG_PSCFG,
		    MAC_REG_BBREGCTL - MAC_REG_PSCFG);

	/* restore CURR_RX_DESC_ADDR, CURR_TX_DESC_ADDR */
	iowrite32(*(u32 *)(cxt_buf + MAC_REG_TXDMAPTR0),
		  io_base + MAC_REG_TXDMAPTR0);
	iowrite32(*(u32 *)(cxt_buf + MAC_REG_AC0DMAPTR),
		  io_base + MAC_REG_AC0DMAPTR);
	iowrite32(*(u32 *)(cxt_buf + MAC_REG_BCNDMAPTR),
		  io_base + MAC_REG_BCNDMAPTR);
	iowrite32(*(u32 *)(cxt_buf + MAC_REG_RXDMAPTR0),
		  io_base + MAC_REG_RXDMAPTR0);
	iowrite32(*(u32 *)(cxt_buf + MAC_REG_RXDMAPTR1),
		  io_base + MAC_REG_RXDMAPTR1);
}

/*
 * Description:
 *      Software Reset MAC
 *
 * Parameters:
 *  In:
 *      io_base    - Base Address for MAC
 *  Out:
 *      none
 *
 * Return Value: true if Reset Success; otherwise false
 *
 */
bool MACbSoftwareReset(struct vnt_private *priv)
{
	void __iomem *io_base = priv->port_offset;
	unsigned short ww;

	/* turn on HOSTCR_SOFTRST, just write 0x01 to reset */
	iowrite8(0x01, io_base + MAC_REG_HOSTCR);

	for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
		if (!(ioread8(io_base + MAC_REG_HOSTCR) & HOSTCR_SOFTRST))
			break;
	}
	if (ww == W_MAX_TIMEOUT)
		return false;
	return true;
}

/*
 * Description:
 *      save some important register's value, then do reset, then restore
 *	register's value
 *
 * Parameters:
 *  In:
 *      io_base    - Base Address for MAC
 *  Out:
 *      none
 *
 * Return Value: true if success; otherwise false
 *
 */
static void vt6655_mac_save_soft_reset(struct vnt_private *priv)
{
	u8 tmp_reg_data[MAC_MAX_CONTEXT_SIZE_PAGE0 + MAC_MAX_CONTEXT_SIZE_PAGE1];

	/* PATCH....
	 * save some important register's value, then do
	 * reset, then restore register's value
	 */
	/* save MAC context */
	vt6655_mac_save_context(priv, tmp_reg_data);
	/* do reset */
	MACbSoftwareReset(priv);
	/* restore MAC context, except CR0 */
	vt6655_mac_restore_context(priv, tmp_reg_data);
}

/*
 * Description:
 *      Turn Off MAC Rx
 *
 * Parameters:
 *  In:
 *      io_base    - Base Address for MAC
 *  Out:
 *      none
 *
 * Return Value: true if success; otherwise false
 *
 */
static bool vt6655_mac_safe_rx_off(struct vnt_private *priv)
{
	void __iomem *io_base = priv->port_offset;
	unsigned short ww;

	/* turn off wow temp for turn off Rx safely */

	/* Clear RX DMA0,1 */
	iowrite32(DMACTL_CLRRUN, io_base + MAC_REG_RXDMACTL0);
	iowrite32(DMACTL_CLRRUN, io_base + MAC_REG_RXDMACTL1);
	for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
		if (!(ioread32(io_base + MAC_REG_RXDMACTL0) & DMACTL_RUN))
			break;
	}
	if (ww == W_MAX_TIMEOUT) {
		pr_debug(" DBG_PORT80(0x10)\n");
		return false;
	}
	for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
		if (!(ioread32(io_base + MAC_REG_RXDMACTL1) & DMACTL_RUN))
			break;
	}
	if (ww == W_MAX_TIMEOUT) {
		pr_debug(" DBG_PORT80(0x11)\n");
		return false;
	}

	/* try to safe shutdown RX */
	vt6655_mac_reg_bits_off(io_base, MAC_REG_HOSTCR, HOSTCR_RXON);
	/* W_MAX_TIMEOUT is the timeout period */
	for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
		if (!(ioread8(io_base + MAC_REG_HOSTCR) & HOSTCR_RXONST))
			break;
	}
	if (ww == W_MAX_TIMEOUT) {
		pr_debug(" DBG_PORT80(0x12)\n");
		return false;
	}
	return true;
}

/*
 * Description:
 *      Turn Off MAC Tx
 *
 * Parameters:
 *  In:
 *      io_base    - Base Address for MAC
 *  Out:
 *      none
 *
 * Return Value: true if success; otherwise false
 *
 */
static bool vt6655_mac_safe_tx_off(struct vnt_private *priv)
{
	void __iomem *io_base = priv->port_offset;
	unsigned short ww;

	/* Clear TX DMA */
	/* Tx0 */
	iowrite32(DMACTL_CLRRUN, io_base + MAC_REG_TXDMACTL0);
	/* AC0 */
	iowrite32(DMACTL_CLRRUN, io_base + MAC_REG_AC0DMACTL);

	for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
		if (!(ioread32(io_base + MAC_REG_TXDMACTL0) & DMACTL_RUN))
			break;
	}
	if (ww == W_MAX_TIMEOUT) {
		pr_debug(" DBG_PORT80(0x20)\n");
		return false;
	}
	for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
		if (!(ioread32(io_base + MAC_REG_AC0DMACTL) & DMACTL_RUN))
			break;
	}
	if (ww == W_MAX_TIMEOUT) {
		pr_debug(" DBG_PORT80(0x21)\n");
		return false;
	}

	/* try to safe shutdown TX */
	vt6655_mac_reg_bits_off(io_base, MAC_REG_HOSTCR, HOSTCR_TXON);

	/* W_MAX_TIMEOUT is the timeout period */
	for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
		if (!(ioread8(io_base + MAC_REG_HOSTCR) & HOSTCR_TXONST))
			break;
	}
	if (ww == W_MAX_TIMEOUT) {
		pr_debug(" DBG_PORT80(0x24)\n");
		return false;
	}
	return true;
}

/*
 * Description:
 *      Stop MAC function
 *
 * Parameters:
 *  In:
 *      io_base    - Base Address for MAC
 *  Out:
 *      none
 *
 * Return Value: true if success; otherwise false
 *
 */
static bool vt6655_mac_safe_stop(struct vnt_private *priv)
{
	void __iomem *io_base = priv->port_offset;

	vt6655_mac_reg_bits_off(io_base, MAC_REG_TCR, TCR_AUTOBCNTX);

	if (!vt6655_mac_safe_rx_off(priv)) {
		pr_debug(" vt6655_mac_safe_rx_off == false)\n");
		vt6655_mac_save_soft_reset(priv);
		return false;
	}
	if (!vt6655_mac_safe_tx_off(priv)) {
		pr_debug(" vt6655_mac_safe_tx_off == false)\n");
		vt6655_mac_save_soft_reset(priv);
		return false;
	}

	vt6655_mac_reg_bits_off(io_base, MAC_REG_HOSTCR, HOSTCR_MACEN);

	return true;
}

/*
 * Description:
 *      Shut Down MAC
 *
 * Parameters:
 *  In:
 *      io_base    - Base Address for MAC
 *  Out:
 *      none
 *
 * Return Value: true if success; otherwise false
 *
 */
bool MACbShutdown(struct vnt_private *priv)
{
	void __iomem *io_base = priv->port_offset;
	/* disable MAC IMR */
	iowrite32(0, io_base + MAC_REG_IMR);
	vt6655_mac_set_loopback_mode(priv, MAC_LB_INTERNAL);
	/* stop the adapter */
	if (!vt6655_mac_safe_stop(priv)) {
		vt6655_mac_set_loopback_mode(priv, MAC_LB_NONE);
		return false;
	}
	vt6655_mac_set_loopback_mode(priv, MAC_LB_NONE);
	return true;
}

/*
 * Description:
 *      Initialize MAC
 *
 * Parameters:
 *  In:
 *      io_base    - Base Address for MAC
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void MACvInitialize(struct vnt_private *priv)
{
	void __iomem *io_base = priv->port_offset;
	/* clear sticky bits */
	vt6655_mac_clear_stck_ds(io_base);
	/* disable force PME-enable */
	iowrite8(PME_OVR, io_base + MAC_REG_PMC1);
	/* only 3253 A */

	/* do reset */
	MACbSoftwareReset(priv);

	/* reset TSF counter */
	iowrite8(TFTCTL_TSFCNTRST, io_base + MAC_REG_TFTCTL);
	/* enable TSF counter */
	iowrite8(TFTCTL_TSFCNTREN, io_base + MAC_REG_TFTCTL);
}

/*
 * Description:
 *      Set the chip with current rx descriptor address
 *
 * Parameters:
 *  In:
 *      io_base        - Base Address for MAC
 *      curr_desc_addr  - Descriptor Address
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void vt6655_mac_set_curr_rx_0_desc_addr(struct vnt_private *priv, u32 curr_desc_addr)
{
	void __iomem *io_base = priv->port_offset;
	unsigned short ww;
	unsigned char org_dma_ctl;

	org_dma_ctl = ioread8(io_base + MAC_REG_RXDMACTL0);
	if (org_dma_ctl & DMACTL_RUN)
		iowrite8(DMACTL_RUN, io_base + MAC_REG_RXDMACTL0 + 2);

	for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
		if (!(ioread8(io_base + MAC_REG_RXDMACTL0) & DMACTL_RUN))
			break;
	}

	iowrite32(curr_desc_addr, io_base + MAC_REG_RXDMAPTR0);
	if (org_dma_ctl & DMACTL_RUN)
		iowrite8(DMACTL_RUN, io_base + MAC_REG_RXDMACTL0);
}

/*
 * Description:
 *      Set the chip with current rx descriptor address
 *
 * Parameters:
 *  In:
 *      io_base        - Base Address for MAC
 *      curr_desc_addr  - Descriptor Address
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void vt6655_mac_set_curr_rx_1_desc_addr(struct vnt_private *priv, u32 curr_desc_addr)
{
	void __iomem *io_base = priv->port_offset;
	unsigned short ww;
	unsigned char org_dma_ctl;

	org_dma_ctl = ioread8(io_base + MAC_REG_RXDMACTL1);
	if (org_dma_ctl & DMACTL_RUN)
		iowrite8(DMACTL_RUN, io_base + MAC_REG_RXDMACTL1 + 2);

	for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
		if (!(ioread8(io_base + MAC_REG_RXDMACTL1) & DMACTL_RUN))
			break;
	}

	iowrite32(curr_desc_addr, io_base + MAC_REG_RXDMAPTR1);
	if (org_dma_ctl & DMACTL_RUN)
		iowrite8(DMACTL_RUN, io_base + MAC_REG_RXDMACTL1);
}

/*
 * Description:
 *      Set the chip with current tx0 descriptor address
 *
 * Parameters:
 *  In:
 *      io_base        - Base Address for MAC
 *      curr_desc_addr  - Descriptor Address
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
static void vt6655_mac_set_curr_tx_0_desc_addr_ex(struct vnt_private *priv, u32 curr_desc_addr)
{
	void __iomem *io_base = priv->port_offset;
	unsigned short ww;
	unsigned char org_dma_ctl;

	org_dma_ctl = ioread8(io_base + MAC_REG_TXDMACTL0);
	if (org_dma_ctl & DMACTL_RUN)
		iowrite8(DMACTL_RUN, io_base + MAC_REG_TXDMACTL0 + 2);

	for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
		if (!(ioread8(io_base + MAC_REG_TXDMACTL0) & DMACTL_RUN))
			break;
	}

	iowrite32(curr_desc_addr, io_base + MAC_REG_TXDMAPTR0);
	if (org_dma_ctl & DMACTL_RUN)
		iowrite8(DMACTL_RUN, io_base + MAC_REG_TXDMACTL0);
}

/*
 * Description:
 *      Set the chip with current AC0 descriptor address
 *
 * Parameters:
 *  In:
 *      io_base        - Base Address for MAC
 *      curr_desc_addr  - Descriptor Address
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
/* TxDMA1 = AC0DMA */
static void vt6655_mac_set_curr_ac_0_desc_addr_ex(struct vnt_private *priv, u32 curr_desc_addr)
{
	void __iomem *io_base = priv->port_offset;
	unsigned short ww;
	unsigned char org_dma_ctl;

	org_dma_ctl = ioread8(io_base + MAC_REG_AC0DMACTL);
	if (org_dma_ctl & DMACTL_RUN)
		iowrite8(DMACTL_RUN, io_base + MAC_REG_AC0DMACTL + 2);

	for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
		if (!(ioread8(io_base + MAC_REG_AC0DMACTL) & DMACTL_RUN))
			break;
	}
	if (ww == W_MAX_TIMEOUT)
		pr_debug(" DBG_PORT80(0x26)\n");
	iowrite32(curr_desc_addr, io_base + MAC_REG_AC0DMAPTR);
	if (org_dma_ctl & DMACTL_RUN)
		iowrite8(DMACTL_RUN, io_base + MAC_REG_AC0DMACTL);
}

void vt6655_mac_set_curr_tx_desc_addr(int tx_type, struct vnt_private *priv, u32 curr_desc_addr)
{
	if (tx_type == TYPE_AC0DMA)
		vt6655_mac_set_curr_ac_0_desc_addr_ex(priv, curr_desc_addr);
	else if (tx_type == TYPE_TXDMA0)
		vt6655_mac_set_curr_tx_0_desc_addr_ex(priv, curr_desc_addr);
}

/*
 * Description:
 *      Micro Second Delay via MAC
 *
 * Parameters:
 *  In:
 *      io_base    - Base Address for MAC
 *      uDelay      - Delay time (timer resolution is 4 us)
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void MACvTimer0MicroSDelay(struct vnt_private *priv, unsigned int uDelay)
{
	void __iomem *io_base = priv->port_offset;
	unsigned char byValue;
	unsigned int uu, ii;

	iowrite8(0, io_base + MAC_REG_TMCTL0);
	iowrite32(uDelay, io_base + MAC_REG_TMDATA0);
	iowrite8((TMCTL_TMD | TMCTL_TE), io_base + MAC_REG_TMCTL0);
	for (ii = 0; ii < 66; ii++) {  /* assume max PCI clock is 66Mhz */
		for (uu = 0; uu < uDelay; uu++) {
			byValue = ioread8(io_base + MAC_REG_TMCTL0);
			if ((byValue == 0) ||
			    (byValue & TMCTL_TSUSP)) {
				iowrite8(0, io_base + MAC_REG_TMCTL0);
				return;
			}
		}
	}
	iowrite8(0, io_base + MAC_REG_TMCTL0);
}

/*
 * Description:
 *      Micro Second One shot timer via MAC
 *
 * Parameters:
 *  In:
 *      io_base    - Base Address for MAC
 *      uDelay      - Delay time
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void MACvOneShotTimer1MicroSec(struct vnt_private *priv,
			       unsigned int uDelayTime)
{
	void __iomem *io_base = priv->port_offset;

	iowrite8(0, io_base + MAC_REG_TMCTL1);
	iowrite32(uDelayTime, io_base + MAC_REG_TMDATA1);
	iowrite8((TMCTL_TMD | TMCTL_TE), io_base + MAC_REG_TMCTL1);
}

void MACvSetMISCFifo(struct vnt_private *priv, unsigned short offset,
		     u32 data)
{
	void __iomem *io_base = priv->port_offset;

	if (offset > 273)
		return;
	iowrite16(offset, io_base + MAC_REG_MISCFFNDEX);
	iowrite32(data, io_base + MAC_REG_MISCFFDATA);
	iowrite16(MISCFFCTL_WRITE, io_base + MAC_REG_MISCFFCTL);
}

bool MACbPSWakeup(struct vnt_private *priv)
{
	void __iomem *io_base = priv->port_offset;
	unsigned int ww;
	/* Read PSCTL */
	if (vt6655_mac_is_reg_bits_off(priv, MAC_REG_PSCTL, PSCTL_PS))
		return true;

	/* Disable PS */
	vt6655_mac_reg_bits_off(io_base, MAC_REG_PSCTL, PSCTL_PSEN);

	/* Check if SyncFlushOK */
	for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
		if (ioread8(io_base + MAC_REG_PSCTL) & PSCTL_WAKEDONE)
			break;
	}
	if (ww == W_MAX_TIMEOUT) {
		pr_debug(" DBG_PORT80(0x33)\n");
		return false;
	}
	return true;
}

/*
 * Description:
 *      Set the Key by MISCFIFO
 *
 * Parameters:
 *  In:
 *      io_base        - Base Address for MAC
 *
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */

void MACvSetKeyEntry(struct vnt_private *priv, unsigned short wKeyCtl,
		     unsigned int uEntryIdx, unsigned int uKeyIdx,
		     unsigned char *pbyAddr, u32 *pdwKey,
		     unsigned char local_id)
{
	void __iomem *io_base = priv->port_offset;
	unsigned short offset;
	u32 data;
	int     ii;

	if (local_id <= 1)
		return;

	offset = MISCFIFO_KEYETRY0;
	offset += (uEntryIdx * MISCFIFO_KEYENTRYSIZE);

	data = 0;
	data |= wKeyCtl;
	data <<= 16;
	data |= MAKEWORD(*(pbyAddr + 4), *(pbyAddr + 5));
	pr_debug("1. offset: %d, Data: %X, KeyCtl:%X\n",
		 offset, data, wKeyCtl);

	iowrite16(offset, io_base + MAC_REG_MISCFFNDEX);
	iowrite32(data, io_base + MAC_REG_MISCFFDATA);
	iowrite16(MISCFFCTL_WRITE, io_base + MAC_REG_MISCFFCTL);
	offset++;

	data = 0;
	data |= *(pbyAddr + 3);
	data <<= 8;
	data |= *(pbyAddr + 2);
	data <<= 8;
	data |= *(pbyAddr + 1);
	data <<= 8;
	data |= *pbyAddr;
	pr_debug("2. offset: %d, Data: %X\n", offset, data);

	iowrite16(offset, io_base + MAC_REG_MISCFFNDEX);
	iowrite32(data, io_base + MAC_REG_MISCFFDATA);
	iowrite16(MISCFFCTL_WRITE, io_base + MAC_REG_MISCFFCTL);
	offset++;

	offset += (uKeyIdx * 4);
	for (ii = 0; ii < 4; ii++) {
		/* always push 128 bits */
		pr_debug("3.(%d) offset: %d, Data: %X\n",
			 ii, offset + ii, *pdwKey);
		iowrite16(offset + ii, io_base + MAC_REG_MISCFFNDEX);
		iowrite32(*pdwKey++, io_base + MAC_REG_MISCFFDATA);
		iowrite16(MISCFFCTL_WRITE, io_base + MAC_REG_MISCFFCTL);
	}
}

/*
 * Description:
 *      Disable the Key Entry by MISCFIFO
 *
 * Parameters:
 *  In:
 *      io_base        - Base Address for MAC
 *
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void MACvDisableKeyEntry(struct vnt_private *priv, unsigned int uEntryIdx)
{
	void __iomem *io_base = priv->port_offset;
	unsigned short offset;

	offset = MISCFIFO_KEYETRY0;
	offset += (uEntryIdx * MISCFIFO_KEYENTRYSIZE);

	iowrite16(offset, io_base + MAC_REG_MISCFFNDEX);
	iowrite32(0, io_base + MAC_REG_MISCFFDATA);
	iowrite16(MISCFFCTL_WRITE, io_base + MAC_REG_MISCFFCTL);
}
