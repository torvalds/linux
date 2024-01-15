// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 * Purpose: driver entry for initial, open, close, tx and rx.
 *
 * Author: Lyndon Chen
 *
 * Date: Jan 8, 2003
 *
 * Functions:
 *
 *   vt6655_probe - module initial (insmod) driver entry
 *   vt6655_remove - module remove entry
 *   device_free_info - device structure resource free function
 *   device_print_info - print out resource
 *   device_rx_srv - rx service function
 *   device_alloc_rx_buf - rx buffer pre-allocated function
 *   device_free_rx_buf - free rx buffer function
 *   device_free_tx_buf - free tx buffer function
 *   device_init_rd0_ring - initial rd dma0 ring
 *   device_init_rd1_ring - initial rd dma1 ring
 *   device_init_td0_ring - initial tx dma0 ring buffer
 *   device_init_td1_ring - initial tx dma1 ring buffer
 *   device_init_registers - initial MAC & BBP & RF internal registers.
 *   device_init_rings - initial tx/rx ring buffer
 *   device_free_rings - free all allocated ring buffer
 *   device_tx_srv - tx interrupt service function
 *
 * Revision History:
 */

#include <linux/file.h>
#include "device.h"
#include "card.h"
#include "channel.h"
#include "baseband.h"
#include "mac.h"
#include "power.h"
#include "rxtx.h"
#include "dpc.h"
#include "rf.h"
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/slab.h>

/*---------------------  Static Definitions -------------------------*/
/*
 * Define module options
 */
MODULE_AUTHOR("VIA Networking Technologies, Inc., <lyndonchen@vntek.com.tw>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("VIA Networking Solomon-A/B/G Wireless LAN Adapter Driver");

#define DEVICE_PARAM(N, D)

#define RX_DESC_MIN0     16
#define RX_DESC_MAX0     128
#define RX_DESC_DEF0     32
DEVICE_PARAM(RxDescriptors0, "Number of receive descriptors0");

#define RX_DESC_MIN1     16
#define RX_DESC_MAX1     128
#define RX_DESC_DEF1     32
DEVICE_PARAM(RxDescriptors1, "Number of receive descriptors1");

#define TX_DESC_MIN0     16
#define TX_DESC_MAX0     128
#define TX_DESC_DEF0     32
DEVICE_PARAM(TxDescriptors0, "Number of transmit descriptors0");

#define TX_DESC_MIN1     16
#define TX_DESC_MAX1     128
#define TX_DESC_DEF1     64
DEVICE_PARAM(TxDescriptors1, "Number of transmit descriptors1");

#define INT_WORKS_DEF   20
#define INT_WORKS_MIN   10
#define INT_WORKS_MAX   64

DEVICE_PARAM(int_works, "Number of packets per interrupt services");

#define RTS_THRESH_DEF     2347

#define FRAG_THRESH_DEF     2346

#define SHORT_RETRY_MIN     0
#define SHORT_RETRY_MAX     31
#define SHORT_RETRY_DEF     8

DEVICE_PARAM(ShortRetryLimit, "Short frame retry limits");

#define LONG_RETRY_MIN     0
#define LONG_RETRY_MAX     15
#define LONG_RETRY_DEF     4

DEVICE_PARAM(LongRetryLimit, "long frame retry limits");

/* BasebandType[] baseband type selected
 * 0: indicate 802.11a type
 * 1: indicate 802.11b type
 * 2: indicate 802.11g type
 */
#define BBP_TYPE_MIN     0
#define BBP_TYPE_MAX     2
#define BBP_TYPE_DEF     2

DEVICE_PARAM(BasebandType, "baseband type");

/*
 * Static vars definitions
 */
static const struct pci_device_id vt6655_pci_id_table[] = {
	{ PCI_VDEVICE(VIA, 0x3253) },
	{ 0, }
};

/*---------------------  Static Functions  --------------------------*/

static int  vt6655_probe(struct pci_dev *pcid, const struct pci_device_id *ent);
static void device_free_info(struct vnt_private *priv);
static void device_print_info(struct vnt_private *priv);

static void vt6655_mac_write_bssid_addr(void __iomem *iobase, const u8 *mac_addr);
static void vt6655_mac_read_ether_addr(void __iomem *iobase, u8 *mac_addr);

static int device_init_rd0_ring(struct vnt_private *priv);
static int device_init_rd1_ring(struct vnt_private *priv);
static int device_init_td0_ring(struct vnt_private *priv);
static int device_init_td1_ring(struct vnt_private *priv);

static int  device_rx_srv(struct vnt_private *priv, unsigned int idx);
static int  device_tx_srv(struct vnt_private *priv, unsigned int idx);
static bool device_alloc_rx_buf(struct vnt_private *, struct vnt_rx_desc *);
static void device_free_rx_buf(struct vnt_private *priv,
			       struct vnt_rx_desc *rd);
static void device_init_registers(struct vnt_private *priv);
static void device_free_tx_buf(struct vnt_private *, struct vnt_tx_desc *);
static void device_free_td0_ring(struct vnt_private *priv);
static void device_free_td1_ring(struct vnt_private *priv);
static void device_free_rd0_ring(struct vnt_private *priv);
static void device_free_rd1_ring(struct vnt_private *priv);
static void device_free_rings(struct vnt_private *priv);

/*---------------------  Export Variables  --------------------------*/

/*---------------------  Export Functions  --------------------------*/

static void vt6655_remove(struct pci_dev *pcid)
{
	struct vnt_private *priv = pci_get_drvdata(pcid);

	if (!priv)
		return;
	device_free_info(priv);
}

static void device_get_options(struct vnt_private *priv)
{
	struct vnt_options *opts = &priv->opts;

	opts->rx_descs0 = RX_DESC_DEF0;
	opts->rx_descs1 = RX_DESC_DEF1;
	opts->tx_descs[0] = TX_DESC_DEF0;
	opts->tx_descs[1] = TX_DESC_DEF1;
	opts->int_works = INT_WORKS_DEF;

	opts->short_retry = SHORT_RETRY_DEF;
	opts->long_retry = LONG_RETRY_DEF;
	opts->bbp_type = BBP_TYPE_DEF;
}

static void
device_set_options(struct vnt_private *priv)
{
	priv->byShortRetryLimit = priv->opts.short_retry;
	priv->byLongRetryLimit = priv->opts.long_retry;
	priv->byBBType = priv->opts.bbp_type;
	priv->packet_type = priv->byBBType;
	priv->byAutoFBCtrl = AUTO_FB_0;
	priv->update_bbvga = true;
	priv->preamble_type = 0;

	pr_debug(" byShortRetryLimit= %d\n", (int)priv->byShortRetryLimit);
	pr_debug(" byLongRetryLimit= %d\n", (int)priv->byLongRetryLimit);
	pr_debug(" preamble_type= %d\n", (int)priv->preamble_type);
	pr_debug(" byShortPreamble= %d\n", (int)priv->byShortPreamble);
	pr_debug(" byBBType= %d\n", (int)priv->byBBType);
}

static void vt6655_mac_write_bssid_addr(void __iomem *iobase, const u8 *mac_addr)
{
	iowrite8(1, iobase + MAC_REG_PAGE1SEL);
	for (int i = 0; i < 6; i++)
		iowrite8(mac_addr[i], iobase + MAC_REG_BSSID0 + i);
	iowrite8(0, iobase + MAC_REG_PAGE1SEL);
}

static void vt6655_mac_read_ether_addr(void __iomem *iobase, u8 *mac_addr)
{
	iowrite8(1, iobase + MAC_REG_PAGE1SEL);
	for (int i = 0; i < 6; i++)
		mac_addr[i] = ioread8(iobase + MAC_REG_PAR0 + i);
	iowrite8(0, iobase + MAC_REG_PAGE1SEL);
}

static void vt6655_mac_dma_ctl(void __iomem *iobase, u8 reg_index)
{
	u32 reg_value;

	reg_value = ioread32(iobase + reg_index);
	if (reg_value & DMACTL_RUN)
		iowrite32(DMACTL_WAKE, iobase + reg_index);
	else
		iowrite32(DMACTL_RUN, iobase + reg_index);
}

static void vt6655_mac_set_bits(void __iomem *iobase, u32 mask)
{
	u32 reg_value;

	reg_value = ioread32(iobase + MAC_REG_ENCFG);
	reg_value = reg_value | mask;
	iowrite32(reg_value, iobase + MAC_REG_ENCFG);
}

static void vt6655_mac_clear_bits(void __iomem *iobase, u32 mask)
{
	u32 reg_value;

	reg_value = ioread32(iobase + MAC_REG_ENCFG);
	reg_value = reg_value & ~mask;
	iowrite32(reg_value, iobase + MAC_REG_ENCFG);
}

static void vt6655_mac_en_protect_md(void __iomem *iobase)
{
	vt6655_mac_set_bits(iobase, ENCFG_PROTECTMD);
}

static void vt6655_mac_dis_protect_md(void __iomem *iobase)
{
	vt6655_mac_clear_bits(iobase, ENCFG_PROTECTMD);
}

static void vt6655_mac_en_barker_preamble_md(void __iomem *iobase)
{
	vt6655_mac_set_bits(iobase, ENCFG_BARKERPREAM);
}

static void vt6655_mac_dis_barker_preamble_md(void __iomem *iobase)
{
	vt6655_mac_clear_bits(iobase, ENCFG_BARKERPREAM);
}

/*
 * Initialisation of MAC & BBP registers
 */

static void device_init_registers(struct vnt_private *priv)
{
	unsigned long flags;
	unsigned int ii;
	unsigned char byValue;
	unsigned char byCCKPwrdBm = 0;
	unsigned char byOFDMPwrdBm = 0;

	MACbShutdown(priv);
	bb_software_reset(priv);

	/* Do MACbSoftwareReset in MACvInitialize */
	MACbSoftwareReset(priv);

	priv->bAES = false;

	/* Only used in 11g type, sync with ERP IE */
	priv->bProtectMode = false;

	priv->bNonERPPresent = false;
	priv->bBarkerPreambleMd = false;
	priv->wCurrentRate = RATE_1M;
	priv->byTopOFDMBasicRate = RATE_24M;
	priv->byTopCCKBasicRate = RATE_1M;

	/* init MAC */
	MACvInitialize(priv);

	/* Get Local ID */
	priv->local_id = ioread8(priv->port_offset + MAC_REG_LOCALID);

	spin_lock_irqsave(&priv->lock, flags);

	SROMvReadAllContents(priv->port_offset, priv->abyEEPROM);

	spin_unlock_irqrestore(&priv->lock, flags);

	/* Get Channel range */
	priv->byMinChannel = 1;
	priv->byMaxChannel = CB_MAX_CHANNEL;

	/* Get Antena */
	byValue = SROMbyReadEmbedded(priv->port_offset, EEP_OFS_ANTENNA);
	if (byValue & EEP_ANTINV)
		priv->bTxRxAntInv = true;
	else
		priv->bTxRxAntInv = false;

	byValue &= (EEP_ANTENNA_AUX | EEP_ANTENNA_MAIN);
	/* if not set default is All */
	if (byValue == 0)
		byValue = (EEP_ANTENNA_AUX | EEP_ANTENNA_MAIN);

	if (byValue == (EEP_ANTENNA_AUX | EEP_ANTENNA_MAIN)) {
		priv->byAntennaCount = 2;
		priv->byTxAntennaMode = ANT_B;
		priv->dwTxAntennaSel = 1;
		priv->dwRxAntennaSel = 1;

		if (priv->bTxRxAntInv)
			priv->byRxAntennaMode = ANT_A;
		else
			priv->byRxAntennaMode = ANT_B;
	} else  {
		priv->byAntennaCount = 1;
		priv->dwTxAntennaSel = 0;
		priv->dwRxAntennaSel = 0;

		if (byValue & EEP_ANTENNA_AUX) {
			priv->byTxAntennaMode = ANT_A;

			if (priv->bTxRxAntInv)
				priv->byRxAntennaMode = ANT_B;
			else
				priv->byRxAntennaMode = ANT_A;
		} else {
			priv->byTxAntennaMode = ANT_B;

			if (priv->bTxRxAntInv)
				priv->byRxAntennaMode = ANT_A;
			else
				priv->byRxAntennaMode = ANT_B;
		}
	}

	/* Set initial antenna mode */
	bb_set_tx_antenna_mode(priv, priv->byTxAntennaMode);
	bb_set_rx_antenna_mode(priv, priv->byRxAntennaMode);

	/* zonetype initial */
	priv->byOriginalZonetype = priv->abyEEPROM[EEP_OFS_ZONETYPE];

	if (!priv->bZoneRegExist)
		priv->byZoneType = priv->abyEEPROM[EEP_OFS_ZONETYPE];

	pr_debug("priv->byZoneType = %x\n", priv->byZoneType);

	/* Init RF module */
	RFbInit(priv);

	/* Get Desire Power Value */
	priv->cur_pwr = 0xFF;
	priv->byCCKPwr = SROMbyReadEmbedded(priv->port_offset, EEP_OFS_PWR_CCK);
	priv->byOFDMPwrG = SROMbyReadEmbedded(priv->port_offset,
					      EEP_OFS_PWR_OFDMG);

	/* Load power Table */
	for (ii = 0; ii < CB_MAX_CHANNEL_24G; ii++) {
		priv->abyCCKPwrTbl[ii + 1] =
			SROMbyReadEmbedded(priv->port_offset,
					   (unsigned char)(ii + EEP_OFS_CCK_PWR_TBL));
		if (priv->abyCCKPwrTbl[ii + 1] == 0)
			priv->abyCCKPwrTbl[ii + 1] = priv->byCCKPwr;

		priv->abyOFDMPwrTbl[ii + 1] =
			SROMbyReadEmbedded(priv->port_offset,
					   (unsigned char)(ii + EEP_OFS_OFDM_PWR_TBL));
		if (priv->abyOFDMPwrTbl[ii + 1] == 0)
			priv->abyOFDMPwrTbl[ii + 1] = priv->byOFDMPwrG;

		priv->abyCCKDefaultPwr[ii + 1] = byCCKPwrdBm;
		priv->abyOFDMDefaultPwr[ii + 1] = byOFDMPwrdBm;
	}

	/* recover 12,13 ,14channel for EUROPE by 11 channel */
	for (ii = 11; ii < 14; ii++) {
		priv->abyCCKPwrTbl[ii] = priv->abyCCKPwrTbl[10];
		priv->abyOFDMPwrTbl[ii] = priv->abyOFDMPwrTbl[10];
	}

	/* Load OFDM A Power Table */
	for (ii = 0; ii < CB_MAX_CHANNEL_5G; ii++) {
		priv->abyOFDMPwrTbl[ii + CB_MAX_CHANNEL_24G + 1] =
			SROMbyReadEmbedded(priv->port_offset,
					   (unsigned char)(ii + EEP_OFS_OFDMA_PWR_TBL));

		priv->abyOFDMDefaultPwr[ii + CB_MAX_CHANNEL_24G + 1] =
			SROMbyReadEmbedded(priv->port_offset,
					   (unsigned char)(ii + EEP_OFS_OFDMA_PWR_dBm));
	}

	if (priv->local_id > REV_ID_VT3253_B1) {
		VT6655_MAC_SELECT_PAGE1(priv->port_offset);

		iowrite8(MSRCTL1_TXPWR | MSRCTL1_CSAPAREN, priv->port_offset + MAC_REG_MSRCTL + 1);

		VT6655_MAC_SELECT_PAGE0(priv->port_offset);
	}

	/* use relative tx timeout and 802.11i D4 */
	vt6655_mac_word_reg_bits_on(priv->port_offset, MAC_REG_CFG,
				    (CFG_TKIPOPT | CFG_NOTXTIMEOUT));

	/* set performance parameter by registry */
	vt6655_mac_set_short_retry_limit(priv, priv->byShortRetryLimit);
	MACvSetLongRetryLimit(priv, priv->byLongRetryLimit);

	/* reset TSF counter */
	iowrite8(TFTCTL_TSFCNTRST, priv->port_offset + MAC_REG_TFTCTL);
	/* enable TSF counter */
	iowrite8(TFTCTL_TSFCNTREN, priv->port_offset + MAC_REG_TFTCTL);

	/* initialize BBP registers */
	bb_vt3253_init(priv);

	if (priv->update_bbvga) {
		priv->bbvga_current = priv->bbvga[0];
		priv->bbvga_new = priv->bbvga_current;
		bb_set_vga_gain_offset(priv, priv->bbvga[0]);
	}

	bb_set_rx_antenna_mode(priv, priv->byRxAntennaMode);
	bb_set_tx_antenna_mode(priv, priv->byTxAntennaMode);

	/* Set BB and packet type at the same time. */
	/* Set Short Slot Time, xIFS, and RSPINF. */
	priv->wCurrentRate = RATE_54M;

	priv->radio_off = false;

	priv->byRadioCtl = SROMbyReadEmbedded(priv->port_offset,
					      EEP_OFS_RADIOCTL);
	priv->hw_radio_off = false;

	if (priv->byRadioCtl & EEP_RADIOCTL_ENABLE) {
		/* Get GPIO */
		priv->byGPIO = ioread8(priv->port_offset + MAC_REG_GPIOCTL1);

		if (((priv->byGPIO & GPIO0_DATA) &&
		     !(priv->byRadioCtl & EEP_RADIOCTL_INV)) ||
		     (!(priv->byGPIO & GPIO0_DATA) &&
		     (priv->byRadioCtl & EEP_RADIOCTL_INV)))
			priv->hw_radio_off = true;
	}

	if (priv->hw_radio_off || priv->bRadioControlOff)
		CARDbRadioPowerOff(priv);

	/* get Permanent network address */
	SROMvReadEtherAddress(priv->port_offset, priv->abyCurrentNetAddr);
	pr_debug("Network address = %pM\n", priv->abyCurrentNetAddr);

	/* reset Tx pointer */
	CARDvSafeResetRx(priv);
	/* reset Rx pointer */
	CARDvSafeResetTx(priv);

	if (priv->local_id <= REV_ID_VT3253_A1)
		vt6655_mac_reg_bits_on(priv->port_offset, MAC_REG_RCR, RCR_WPAERR);

	/* Turn On Rx DMA */
	vt6655_mac_dma_ctl(priv->port_offset, MAC_REG_RXDMACTL0);
	vt6655_mac_dma_ctl(priv->port_offset, MAC_REG_RXDMACTL1);

	/* start the adapter */
	iowrite8(HOSTCR_MACEN | HOSTCR_RXON | HOSTCR_TXON, priv->port_offset + MAC_REG_HOSTCR);
}

static void device_print_info(struct vnt_private *priv)
{
	dev_info(&priv->pcid->dev, "MAC=%pM IO=0x%lx Mem=0x%lx IRQ=%d\n",
		 priv->abyCurrentNetAddr, (unsigned long)priv->ioaddr,
		 (unsigned long)priv->port_offset, priv->pcid->irq);
}

static void device_free_info(struct vnt_private *priv)
{
	if (!priv)
		return;

	if (priv->mac_hw)
		ieee80211_unregister_hw(priv->hw);

	if (priv->port_offset)
		iounmap(priv->port_offset);

	if (priv->pcid)
		pci_release_regions(priv->pcid);

	if (priv->hw)
		ieee80211_free_hw(priv->hw);
}

static bool device_init_rings(struct vnt_private *priv)
{
	void *vir_pool;

	/*allocate all RD/TD rings a single pool*/
	vir_pool = dma_alloc_coherent(&priv->pcid->dev,
				      priv->opts.rx_descs0 * sizeof(struct vnt_rx_desc) +
				      priv->opts.rx_descs1 * sizeof(struct vnt_rx_desc) +
				      priv->opts.tx_descs[0] * sizeof(struct vnt_tx_desc) +
				      priv->opts.tx_descs[1] * sizeof(struct vnt_tx_desc),
				      &priv->pool_dma, GFP_ATOMIC);
	if (!vir_pool) {
		dev_err(&priv->pcid->dev, "allocate desc dma memory failed\n");
		return false;
	}

	priv->aRD0Ring = vir_pool;
	priv->aRD1Ring = vir_pool +
		priv->opts.rx_descs0 * sizeof(struct vnt_rx_desc);

	priv->rd0_pool_dma = priv->pool_dma;
	priv->rd1_pool_dma = priv->rd0_pool_dma +
		priv->opts.rx_descs0 * sizeof(struct vnt_rx_desc);

	priv->tx0_bufs = dma_alloc_coherent(&priv->pcid->dev,
					    priv->opts.tx_descs[0] * PKT_BUF_SZ +
					    priv->opts.tx_descs[1] * PKT_BUF_SZ +
					    CB_BEACON_BUF_SIZE +
					    CB_MAX_BUF_SIZE,
					    &priv->tx_bufs_dma0, GFP_ATOMIC);
	if (!priv->tx0_bufs) {
		dev_err(&priv->pcid->dev, "allocate buf dma memory failed\n");

		dma_free_coherent(&priv->pcid->dev,
				  priv->opts.rx_descs0 * sizeof(struct vnt_rx_desc) +
				  priv->opts.rx_descs1 * sizeof(struct vnt_rx_desc) +
				  priv->opts.tx_descs[0] * sizeof(struct vnt_tx_desc) +
				  priv->opts.tx_descs[1] * sizeof(struct vnt_tx_desc),
				  vir_pool, priv->pool_dma);
		return false;
	}

	priv->td0_pool_dma = priv->rd1_pool_dma +
		priv->opts.rx_descs1 * sizeof(struct vnt_rx_desc);

	priv->td1_pool_dma = priv->td0_pool_dma +
		priv->opts.tx_descs[0] * sizeof(struct vnt_tx_desc);

	/* vir_pool: pvoid type */
	priv->apTD0Rings = vir_pool
		+ priv->opts.rx_descs0 * sizeof(struct vnt_rx_desc)
		+ priv->opts.rx_descs1 * sizeof(struct vnt_rx_desc);

	priv->apTD1Rings = vir_pool
		+ priv->opts.rx_descs0 * sizeof(struct vnt_rx_desc)
		+ priv->opts.rx_descs1 * sizeof(struct vnt_rx_desc)
		+ priv->opts.tx_descs[0] * sizeof(struct vnt_tx_desc);

	priv->tx1_bufs = priv->tx0_bufs +
		priv->opts.tx_descs[0] * PKT_BUF_SZ;

	priv->tx_beacon_bufs = priv->tx1_bufs +
		priv->opts.tx_descs[1] * PKT_BUF_SZ;

	priv->pbyTmpBuff = priv->tx_beacon_bufs +
		CB_BEACON_BUF_SIZE;

	priv->tx_bufs_dma1 = priv->tx_bufs_dma0 +
		priv->opts.tx_descs[0] * PKT_BUF_SZ;

	priv->tx_beacon_dma = priv->tx_bufs_dma1 +
		priv->opts.tx_descs[1] * PKT_BUF_SZ;

	return true;
}

static void device_free_rings(struct vnt_private *priv)
{
	dma_free_coherent(&priv->pcid->dev,
			  priv->opts.rx_descs0 * sizeof(struct vnt_rx_desc) +
			  priv->opts.rx_descs1 * sizeof(struct vnt_rx_desc) +
			  priv->opts.tx_descs[0] * sizeof(struct vnt_tx_desc) +
			  priv->opts.tx_descs[1] * sizeof(struct vnt_tx_desc),
			  priv->aRD0Ring, priv->pool_dma);

	dma_free_coherent(&priv->pcid->dev,
			  priv->opts.tx_descs[0] * PKT_BUF_SZ +
			  priv->opts.tx_descs[1] * PKT_BUF_SZ +
			  CB_BEACON_BUF_SIZE +
			  CB_MAX_BUF_SIZE,
			  priv->tx0_bufs, priv->tx_bufs_dma0);
}

static int device_init_rd0_ring(struct vnt_private *priv)
{
	int i;
	dma_addr_t      curr = priv->rd0_pool_dma;
	struct vnt_rx_desc *desc;
	int ret;

	/* Init the RD0 ring entries */
	for (i = 0; i < priv->opts.rx_descs0;
	     i ++, curr += sizeof(struct vnt_rx_desc)) {
		desc = &priv->aRD0Ring[i];
		desc->rd_info = kzalloc(sizeof(*desc->rd_info), GFP_KERNEL);
		if (!desc->rd_info) {
			ret = -ENOMEM;
			goto err_free_desc;
		}

		if (!device_alloc_rx_buf(priv, desc)) {
			dev_err(&priv->pcid->dev, "can not alloc rx bufs\n");
			ret = -ENOMEM;
			goto err_free_rd;
		}

		desc->next = &priv->aRD0Ring[(i + 1) % priv->opts.rx_descs0];
		desc->next_desc = cpu_to_le32(curr + sizeof(struct vnt_rx_desc));
	}

	if (i > 0)
		priv->aRD0Ring[i - 1].next_desc = cpu_to_le32(priv->rd0_pool_dma);
	priv->pCurrRD[0] = &priv->aRD0Ring[0];

	return 0;

err_free_rd:
	kfree(desc->rd_info);

err_free_desc:
	while (i--) {
		desc = &priv->aRD0Ring[i];
		device_free_rx_buf(priv, desc);
		kfree(desc->rd_info);
	}

	return ret;
}

static int device_init_rd1_ring(struct vnt_private *priv)
{
	int i;
	dma_addr_t      curr = priv->rd1_pool_dma;
	struct vnt_rx_desc *desc;
	int ret;

	/* Init the RD1 ring entries */
	for (i = 0; i < priv->opts.rx_descs1;
	     i ++, curr += sizeof(struct vnt_rx_desc)) {
		desc = &priv->aRD1Ring[i];
		desc->rd_info = kzalloc(sizeof(*desc->rd_info), GFP_KERNEL);
		if (!desc->rd_info) {
			ret = -ENOMEM;
			goto err_free_desc;
		}

		if (!device_alloc_rx_buf(priv, desc)) {
			dev_err(&priv->pcid->dev, "can not alloc rx bufs\n");
			ret = -ENOMEM;
			goto err_free_rd;
		}

		desc->next = &priv->aRD1Ring[(i + 1) % priv->opts.rx_descs1];
		desc->next_desc = cpu_to_le32(curr + sizeof(struct vnt_rx_desc));
	}

	if (i > 0)
		priv->aRD1Ring[i - 1].next_desc = cpu_to_le32(priv->rd1_pool_dma);
	priv->pCurrRD[1] = &priv->aRD1Ring[0];

	return 0;

err_free_rd:
	kfree(desc->rd_info);

err_free_desc:
	while (i--) {
		desc = &priv->aRD1Ring[i];
		device_free_rx_buf(priv, desc);
		kfree(desc->rd_info);
	}

	return ret;
}

static void device_free_rd0_ring(struct vnt_private *priv)
{
	int i;

	for (i = 0; i < priv->opts.rx_descs0; i++) {
		struct vnt_rx_desc *desc = &priv->aRD0Ring[i];

		device_free_rx_buf(priv, desc);
		kfree(desc->rd_info);
	}
}

static void device_free_rd1_ring(struct vnt_private *priv)
{
	int i;

	for (i = 0; i < priv->opts.rx_descs1; i++) {
		struct vnt_rx_desc *desc = &priv->aRD1Ring[i];

		device_free_rx_buf(priv, desc);
		kfree(desc->rd_info);
	}
}

static int device_init_td0_ring(struct vnt_private *priv)
{
	int i;
	dma_addr_t  curr;
	struct vnt_tx_desc *desc;
	int ret;

	curr = priv->td0_pool_dma;
	for (i = 0; i < priv->opts.tx_descs[0];
	     i++, curr += sizeof(struct vnt_tx_desc)) {
		desc = &priv->apTD0Rings[i];
		desc->td_info = kzalloc(sizeof(*desc->td_info), GFP_KERNEL);
		if (!desc->td_info) {
			ret = -ENOMEM;
			goto err_free_desc;
		}

		desc->td_info->buf = priv->tx0_bufs + i * PKT_BUF_SZ;
		desc->td_info->buf_dma = priv->tx_bufs_dma0 + i * PKT_BUF_SZ;

		desc->next = &(priv->apTD0Rings[(i + 1) % priv->opts.tx_descs[0]]);
		desc->next_desc = cpu_to_le32(curr +
					      sizeof(struct vnt_tx_desc));
	}

	if (i > 0)
		priv->apTD0Rings[i - 1].next_desc = cpu_to_le32(priv->td0_pool_dma);
	priv->apTailTD[0] = priv->apCurrTD[0] = &priv->apTD0Rings[0];

	return 0;

err_free_desc:
	while (i--) {
		desc = &priv->apTD0Rings[i];
		kfree(desc->td_info);
	}

	return ret;
}

static int device_init_td1_ring(struct vnt_private *priv)
{
	int i;
	dma_addr_t  curr;
	struct vnt_tx_desc *desc;
	int ret;

	/* Init the TD ring entries */
	curr = priv->td1_pool_dma;
	for (i = 0; i < priv->opts.tx_descs[1];
	     i++, curr += sizeof(struct vnt_tx_desc)) {
		desc = &priv->apTD1Rings[i];
		desc->td_info = kzalloc(sizeof(*desc->td_info), GFP_KERNEL);
		if (!desc->td_info) {
			ret = -ENOMEM;
			goto err_free_desc;
		}

		desc->td_info->buf = priv->tx1_bufs + i * PKT_BUF_SZ;
		desc->td_info->buf_dma = priv->tx_bufs_dma1 + i * PKT_BUF_SZ;

		desc->next = &(priv->apTD1Rings[(i + 1) % priv->opts.tx_descs[1]]);
		desc->next_desc = cpu_to_le32(curr + sizeof(struct vnt_tx_desc));
	}

	if (i > 0)
		priv->apTD1Rings[i - 1].next_desc = cpu_to_le32(priv->td1_pool_dma);
	priv->apTailTD[1] = priv->apCurrTD[1] = &priv->apTD1Rings[0];

	return 0;

err_free_desc:
	while (i--) {
		desc = &priv->apTD1Rings[i];
		kfree(desc->td_info);
	}

	return ret;
}

static void device_free_td0_ring(struct vnt_private *priv)
{
	int i;

	for (i = 0; i < priv->opts.tx_descs[0]; i++) {
		struct vnt_tx_desc *desc = &priv->apTD0Rings[i];
		struct vnt_td_info *td_info = desc->td_info;

		dev_kfree_skb(td_info->skb);
		kfree(desc->td_info);
	}
}

static void device_free_td1_ring(struct vnt_private *priv)
{
	int i;

	for (i = 0; i < priv->opts.tx_descs[1]; i++) {
		struct vnt_tx_desc *desc = &priv->apTD1Rings[i];
		struct vnt_td_info *td_info = desc->td_info;

		dev_kfree_skb(td_info->skb);
		kfree(desc->td_info);
	}
}

/*-----------------------------------------------------------------*/

static int device_rx_srv(struct vnt_private *priv, unsigned int idx)
{
	struct vnt_rx_desc *rd;
	int works = 0;

	for (rd = priv->pCurrRD[idx];
	     rd->rd0.owner == OWNED_BY_HOST;
	     rd = rd->next) {
		if (works++ > 15)
			break;

		if (!rd->rd_info->skb)
			break;

		if (vnt_receive_frame(priv, rd)) {
			if (!device_alloc_rx_buf(priv, rd)) {
				dev_err(&priv->pcid->dev,
					"can not allocate rx buf\n");
				break;
			}
		}
		rd->rd0.owner = OWNED_BY_NIC;
	}

	priv->pCurrRD[idx] = rd;

	return works;
}

static bool device_alloc_rx_buf(struct vnt_private *priv,
				struct vnt_rx_desc *rd)
{
	struct vnt_rd_info *rd_info = rd->rd_info;

	rd_info->skb = dev_alloc_skb((int)priv->rx_buf_sz);
	if (!rd_info->skb)
		return false;

	rd_info->skb_dma =
		dma_map_single(&priv->pcid->dev,
			       skb_put(rd_info->skb, skb_tailroom(rd_info->skb)),
			       priv->rx_buf_sz, DMA_FROM_DEVICE);
	if (dma_mapping_error(&priv->pcid->dev, rd_info->skb_dma)) {
		dev_kfree_skb(rd_info->skb);
		rd_info->skb = NULL;
		return false;
	}

	*((unsigned int *)&rd->rd0) = 0; /* FIX cast */

	rd->rd0.res_count = cpu_to_le16(priv->rx_buf_sz);
	rd->rd0.owner = OWNED_BY_NIC;
	rd->rd1.req_count = cpu_to_le16(priv->rx_buf_sz);
	rd->buff_addr = cpu_to_le32(rd_info->skb_dma);

	return true;
}

static void device_free_rx_buf(struct vnt_private *priv,
			       struct vnt_rx_desc *rd)
{
	struct vnt_rd_info *rd_info = rd->rd_info;

	dma_unmap_single(&priv->pcid->dev, rd_info->skb_dma,
			 priv->rx_buf_sz, DMA_FROM_DEVICE);
	dev_kfree_skb(rd_info->skb);
}

static const u8 fallback_rate0[5][5] = {
	{RATE_18M, RATE_18M, RATE_12M, RATE_12M, RATE_12M},
	{RATE_24M, RATE_24M, RATE_18M, RATE_12M, RATE_12M},
	{RATE_36M, RATE_36M, RATE_24M, RATE_18M, RATE_18M},
	{RATE_48M, RATE_48M, RATE_36M, RATE_24M, RATE_24M},
	{RATE_54M, RATE_54M, RATE_48M, RATE_36M, RATE_36M}
};

static const u8 fallback_rate1[5][5] = {
	{RATE_18M, RATE_18M, RATE_12M, RATE_6M, RATE_6M},
	{RATE_24M, RATE_24M, RATE_18M, RATE_6M, RATE_6M},
	{RATE_36M, RATE_36M, RATE_24M, RATE_12M, RATE_12M},
	{RATE_48M, RATE_48M, RATE_24M, RATE_12M, RATE_12M},
	{RATE_54M, RATE_54M, RATE_36M, RATE_18M, RATE_18M}
};

static int vnt_int_report_rate(struct vnt_private *priv,
			       struct vnt_td_info *context, u8 tsr0, u8 tsr1)
{
	struct vnt_tx_fifo_head *fifo_head;
	struct ieee80211_tx_info *info;
	struct ieee80211_rate *rate;
	u16 fb_option;
	u8 tx_retry = (tsr0 & TSR0_NCR);
	s8 idx;

	if (!context)
		return -ENOMEM;

	if (!context->skb)
		return -EINVAL;

	fifo_head = (struct vnt_tx_fifo_head *)context->buf;
	fb_option = (le16_to_cpu(fifo_head->fifo_ctl) &
			(FIFOCTL_AUTO_FB_0 | FIFOCTL_AUTO_FB_1));

	info = IEEE80211_SKB_CB(context->skb);
	idx = info->control.rates[0].idx;

	if (fb_option && !(tsr1 & TSR1_TERR)) {
		u8 tx_rate;
		u8 retry = tx_retry;

		rate = ieee80211_get_tx_rate(priv->hw, info);
		tx_rate = rate->hw_value - RATE_18M;

		if (retry > 4)
			retry = 4;

		if (fb_option & FIFOCTL_AUTO_FB_0)
			tx_rate = fallback_rate0[tx_rate][retry];
		else if (fb_option & FIFOCTL_AUTO_FB_1)
			tx_rate = fallback_rate1[tx_rate][retry];

		if (info->band == NL80211_BAND_5GHZ)
			idx = tx_rate - RATE_6M;
		else
			idx = tx_rate;
	}

	ieee80211_tx_info_clear_status(info);

	info->status.rates[0].count = tx_retry;

	if (!(tsr1 & TSR1_TERR)) {
		info->status.rates[0].idx = idx;

		if (info->flags & IEEE80211_TX_CTL_NO_ACK)
			info->flags |= IEEE80211_TX_STAT_NOACK_TRANSMITTED;
		else
			info->flags |= IEEE80211_TX_STAT_ACK;
	}

	return 0;
}

static int device_tx_srv(struct vnt_private *priv, unsigned int idx)
{
	struct vnt_tx_desc *desc;
	int                      works = 0;
	unsigned char byTsr0;
	unsigned char byTsr1;

	for (desc = priv->apTailTD[idx]; priv->iTDUsed[idx] > 0; desc = desc->next) {
		if (desc->td0.owner == OWNED_BY_NIC)
			break;
		if (works++ > 15)
			break;

		byTsr0 = desc->td0.tsr0;
		byTsr1 = desc->td0.tsr1;

		/* Only the status of first TD in the chain is correct */
		if (desc->td1.tcr & TCR_STP) {
			if ((desc->td_info->flags & TD_FLAGS_NETIF_SKB) != 0) {
				if (!(byTsr1 & TSR1_TERR)) {
					if (byTsr0 != 0) {
						pr_debug(" Tx[%d] OK but has error. tsr1[%02X] tsr0[%02X]\n",
							 (int)idx, byTsr1,
							 byTsr0);
					}
				} else {
					pr_debug(" Tx[%d] dropped & tsr1[%02X] tsr0[%02X]\n",
						 (int)idx, byTsr1, byTsr0);
				}
			}

			if (byTsr1 & TSR1_TERR) {
				if ((desc->td_info->flags & TD_FLAGS_PRIV_SKB) != 0) {
					pr_debug(" Tx[%d] fail has error. tsr1[%02X] tsr0[%02X]\n",
						 (int)idx, byTsr1, byTsr0);
				}
			}

			vnt_int_report_rate(priv, desc->td_info, byTsr0, byTsr1);

			device_free_tx_buf(priv, desc);
			priv->iTDUsed[idx]--;
		}
	}

	priv->apTailTD[idx] = desc;

	return works;
}

static void device_error(struct vnt_private *priv, unsigned short status)
{
	if (status & ISR_FETALERR) {
		dev_err(&priv->pcid->dev, "Hardware fatal error\n");

		MACbShutdown(priv);
		return;
	}
}

static void device_free_tx_buf(struct vnt_private *priv,
			       struct vnt_tx_desc *desc)
{
	struct vnt_td_info *td_info = desc->td_info;
	struct sk_buff *skb = td_info->skb;

	if (skb)
		ieee80211_tx_status_irqsafe(priv->hw, skb);

	td_info->skb = NULL;
	td_info->flags = 0;
}

static void vnt_check_bb_vga(struct vnt_private *priv)
{
	long dbm;
	int i;

	if (!priv->update_bbvga)
		return;

	if (priv->hw->conf.flags & IEEE80211_CONF_OFFCHANNEL)
		return;

	if (!(priv->vif->cfg.assoc && priv->current_rssi))
		return;

	RFvRSSITodBm(priv, (u8)priv->current_rssi, &dbm);

	for (i = 0; i < BB_VGA_LEVEL; i++) {
		if (dbm < priv->dbm_threshold[i]) {
			priv->bbvga_new = priv->bbvga[i];
			break;
		}
	}

	if (priv->bbvga_new == priv->bbvga_current) {
		priv->uBBVGADiffCount = 1;
		return;
	}

	priv->uBBVGADiffCount++;

	if (priv->uBBVGADiffCount == 1) {
		/* first VGA diff gain */
		bb_set_vga_gain_offset(priv, priv->bbvga_new);

		dev_dbg(&priv->pcid->dev,
			"First RSSI[%d] NewGain[%d] OldGain[%d] Count[%d]\n",
			(int)dbm, priv->bbvga_new,
			priv->bbvga_current,
			(int)priv->uBBVGADiffCount);
	}

	if (priv->uBBVGADiffCount >= BB_VGA_CHANGE_THRESHOLD) {
		dev_dbg(&priv->pcid->dev,
			"RSSI[%d] NewGain[%d] OldGain[%d] Count[%d]\n",
			(int)dbm, priv->bbvga_new,
			priv->bbvga_current,
			(int)priv->uBBVGADiffCount);

		bb_set_vga_gain_offset(priv, priv->bbvga_new);
	}
}

static void vnt_interrupt_process(struct vnt_private *priv)
{
	struct ieee80211_low_level_stats *low_stats = &priv->low_stats;
	int             max_count = 0;
	u32 mib_counter;
	u32 isr;
	unsigned long flags;

	isr = ioread32(priv->port_offset + MAC_REG_ISR);

	if (isr == 0)
		return;

	if (isr == 0xffffffff) {
		pr_debug("isr = 0xffff\n");
		return;
	}

	spin_lock_irqsave(&priv->lock, flags);

	/* Read low level stats */
	mib_counter = ioread32(priv->port_offset + MAC_REG_MIBCNTR);

	low_stats->dot11RTSSuccessCount += mib_counter & 0xff;
	low_stats->dot11RTSFailureCount += (mib_counter >> 8) & 0xff;
	low_stats->dot11ACKFailureCount += (mib_counter >> 16) & 0xff;
	low_stats->dot11FCSErrorCount += (mib_counter >> 24) & 0xff;

	/*
	 * TBD....
	 * Must do this after doing rx/tx, cause ISR bit is slow
	 * than RD/TD write back
	 * update ISR counter
	 */
	while (isr && priv->vif) {
		iowrite32(isr, priv->port_offset + MAC_REG_ISR);

		if (isr & ISR_FETALERR) {
			pr_debug(" ISR_FETALERR\n");
			iowrite8(0, priv->port_offset + MAC_REG_SOFTPWRCTL);
			iowrite16(SOFTPWRCTL_SWPECTI, priv->port_offset + MAC_REG_SOFTPWRCTL);
			device_error(priv, isr);
		}

		if (isr & ISR_TBTT) {
			if (priv->op_mode != NL80211_IFTYPE_ADHOC)
				vnt_check_bb_vga(priv);

			priv->bBeaconSent = false;
			if (priv->bEnablePSMode)
				PSbIsNextTBTTWakeUp((void *)priv);

			if ((priv->op_mode == NL80211_IFTYPE_AP ||
			    priv->op_mode == NL80211_IFTYPE_ADHOC) &&
			    priv->vif->bss_conf.enable_beacon)
				MACvOneShotTimer1MicroSec(priv,
							  (priv->vif->bss_conf.beacon_int -
							   MAKE_BEACON_RESERVED) << 10);

			/* TODO: adhoc PS mode */
		}

		if (isr & ISR_BNTX) {
			if (priv->op_mode == NL80211_IFTYPE_ADHOC) {
				priv->bIsBeaconBufReadySet = false;
				priv->cbBeaconBufReadySetCnt = 0;
			}

			priv->bBeaconSent = true;
		}

		if (isr & ISR_RXDMA0)
			max_count += device_rx_srv(priv, TYPE_RXDMA0);

		if (isr & ISR_RXDMA1)
			max_count += device_rx_srv(priv, TYPE_RXDMA1);

		if (isr & ISR_TXDMA0)
			max_count += device_tx_srv(priv, TYPE_TXDMA0);

		if (isr & ISR_AC0DMA)
			max_count += device_tx_srv(priv, TYPE_AC0DMA);

		if (isr & ISR_SOFTTIMER1) {
			if (priv->vif->bss_conf.enable_beacon)
				vnt_beacon_make(priv, priv->vif);
		}

		/* If both buffers available wake the queue */
		if (AVAIL_TD(priv, TYPE_TXDMA0) &&
		    AVAIL_TD(priv, TYPE_AC0DMA) &&
		    ieee80211_queue_stopped(priv->hw, 0))
			ieee80211_wake_queues(priv->hw);

		isr = ioread32(priv->port_offset + MAC_REG_ISR);

		vt6655_mac_dma_ctl(priv->port_offset, MAC_REG_RXDMACTL0);
		vt6655_mac_dma_ctl(priv->port_offset, MAC_REG_RXDMACTL1);

		if (max_count > priv->opts.int_works)
			break;
	}

	spin_unlock_irqrestore(&priv->lock, flags);
}

static void vnt_interrupt_work(struct work_struct *work)
{
	struct vnt_private *priv =
		container_of(work, struct vnt_private, interrupt_work);

	if (priv->vif)
		vnt_interrupt_process(priv);

	iowrite32(IMR_MASK_VALUE, priv->port_offset + MAC_REG_IMR);
}

static irqreturn_t vnt_interrupt(int irq,  void *arg)
{
	struct vnt_private *priv = arg;

	schedule_work(&priv->interrupt_work);

	iowrite32(0, priv->port_offset + MAC_REG_IMR);

	return IRQ_HANDLED;
}

static int vnt_tx_packet(struct vnt_private *priv, struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct vnt_tx_desc *head_td;
	u32 dma_idx;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);

	if (ieee80211_is_data(hdr->frame_control))
		dma_idx = TYPE_AC0DMA;
	else
		dma_idx = TYPE_TXDMA0;

	if (AVAIL_TD(priv, dma_idx) < 1) {
		spin_unlock_irqrestore(&priv->lock, flags);
		ieee80211_stop_queues(priv->hw);
		return -ENOMEM;
	}

	head_td = priv->apCurrTD[dma_idx];

	head_td->td1.tcr = 0;

	head_td->td_info->skb = skb;

	if (dma_idx == TYPE_AC0DMA)
		head_td->td_info->flags = TD_FLAGS_NETIF_SKB;

	priv->apCurrTD[dma_idx] = head_td->next;

	spin_unlock_irqrestore(&priv->lock, flags);

	vnt_generate_fifo_header(priv, dma_idx, head_td, skb);

	spin_lock_irqsave(&priv->lock, flags);

	priv->bPWBitOn = false;

	/* Set TSR1 & ReqCount in TxDescHead */
	head_td->td1.tcr |= (TCR_STP | TCR_EDP | EDMSDU);
	head_td->td1.req_count = cpu_to_le16(head_td->td_info->req_count);

	head_td->buff_addr = cpu_to_le32(head_td->td_info->buf_dma);

	/* Poll Transmit the adapter */
	wmb();
	head_td->td0.owner = OWNED_BY_NIC;
	wmb(); /* second memory barrier */

	if (head_td->td_info->flags & TD_FLAGS_NETIF_SKB)
		vt6655_mac_dma_ctl(priv->port_offset, MAC_REG_AC0DMACTL);
	else
		vt6655_mac_dma_ctl(priv->port_offset, MAC_REG_TXDMACTL0);

	priv->iTDUsed[dma_idx]++;

	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static void vnt_tx_80211(struct ieee80211_hw *hw,
			 struct ieee80211_tx_control *control,
			 struct sk_buff *skb)
{
	struct vnt_private *priv = hw->priv;

	if (vnt_tx_packet(priv, skb))
		ieee80211_free_txskb(hw, skb);
}

static int vnt_start(struct ieee80211_hw *hw)
{
	struct vnt_private *priv = hw->priv;
	int ret;

	priv->rx_buf_sz = PKT_BUF_SZ;
	if (!device_init_rings(priv))
		return -ENOMEM;

	ret = request_irq(priv->pcid->irq, vnt_interrupt,
			  IRQF_SHARED, "vt6655", priv);
	if (ret) {
		dev_dbg(&priv->pcid->dev, "failed to start irq\n");
		goto err_free_rings;
	}

	dev_dbg(&priv->pcid->dev, "call device init rd0 ring\n");
	ret = device_init_rd0_ring(priv);
	if (ret)
		goto err_free_irq;
	ret = device_init_rd1_ring(priv);
	if (ret)
		goto err_free_rd0_ring;
	ret = device_init_td0_ring(priv);
	if (ret)
		goto err_free_rd1_ring;
	ret = device_init_td1_ring(priv);
	if (ret)
		goto err_free_td0_ring;

	device_init_registers(priv);

	dev_dbg(&priv->pcid->dev, "enable MAC interrupt\n");
	iowrite32(IMR_MASK_VALUE, priv->port_offset + MAC_REG_IMR);

	ieee80211_wake_queues(hw);

	return 0;

err_free_td0_ring:
	device_free_td0_ring(priv);
err_free_rd1_ring:
	device_free_rd1_ring(priv);
err_free_rd0_ring:
	device_free_rd0_ring(priv);
err_free_irq:
	free_irq(priv->pcid->irq, priv);
err_free_rings:
	device_free_rings(priv);
	return ret;
}

static void vnt_stop(struct ieee80211_hw *hw)
{
	struct vnt_private *priv = hw->priv;

	ieee80211_stop_queues(hw);

	cancel_work_sync(&priv->interrupt_work);

	MACbShutdown(priv);
	MACbSoftwareReset(priv);
	CARDbRadioPowerOff(priv);

	device_free_td0_ring(priv);
	device_free_td1_ring(priv);
	device_free_rd0_ring(priv);
	device_free_rd1_ring(priv);
	device_free_rings(priv);

	free_irq(priv->pcid->irq, priv);
}

static int vnt_add_interface(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct vnt_private *priv = hw->priv;

	priv->vif = vif;

	switch (vif->type) {
	case NL80211_IFTYPE_STATION:
		break;
	case NL80211_IFTYPE_ADHOC:
		vt6655_mac_reg_bits_off(priv->port_offset, MAC_REG_RCR, RCR_UNICAST);

		vt6655_mac_reg_bits_on(priv->port_offset, MAC_REG_HOSTCR, HOSTCR_ADHOC);

		break;
	case NL80211_IFTYPE_AP:
		vt6655_mac_reg_bits_off(priv->port_offset, MAC_REG_RCR, RCR_UNICAST);

		vt6655_mac_reg_bits_on(priv->port_offset, MAC_REG_HOSTCR, HOSTCR_AP);

		break;
	default:
		return -EOPNOTSUPP;
	}

	priv->op_mode = vif->type;

	return 0;
}

static void vnt_remove_interface(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif)
{
	struct vnt_private *priv = hw->priv;

	switch (vif->type) {
	case NL80211_IFTYPE_STATION:
		break;
	case NL80211_IFTYPE_ADHOC:
		vt6655_mac_reg_bits_off(priv->port_offset, MAC_REG_TCR, TCR_AUTOBCNTX);
		vt6655_mac_reg_bits_off(priv->port_offset,
					MAC_REG_TFTCTL, TFTCTL_TSFCNTREN);
		vt6655_mac_reg_bits_off(priv->port_offset, MAC_REG_HOSTCR, HOSTCR_ADHOC);
		break;
	case NL80211_IFTYPE_AP:
		vt6655_mac_reg_bits_off(priv->port_offset, MAC_REG_TCR, TCR_AUTOBCNTX);
		vt6655_mac_reg_bits_off(priv->port_offset,
					MAC_REG_TFTCTL, TFTCTL_TSFCNTREN);
		vt6655_mac_reg_bits_off(priv->port_offset, MAC_REG_HOSTCR, HOSTCR_AP);
		break;
	default:
		break;
	}

	priv->op_mode = NL80211_IFTYPE_UNSPECIFIED;
}

static int vnt_config(struct ieee80211_hw *hw, u32 changed)
{
	struct vnt_private *priv = hw->priv;
	struct ieee80211_conf *conf = &hw->conf;
	u8 bb_type;

	if (changed & IEEE80211_CONF_CHANGE_PS) {
		if (conf->flags & IEEE80211_CONF_PS)
			PSvEnablePowerSaving(priv, conf->listen_interval);
		else
			PSvDisablePowerSaving(priv);
	}

	if ((changed & IEEE80211_CONF_CHANGE_CHANNEL) ||
	    (conf->flags & IEEE80211_CONF_OFFCHANNEL)) {
		set_channel(priv, conf->chandef.chan);

		if (conf->chandef.chan->band == NL80211_BAND_5GHZ)
			bb_type = BB_TYPE_11A;
		else
			bb_type = BB_TYPE_11G;

		if (priv->byBBType != bb_type) {
			priv->byBBType = bb_type;

			card_set_phy_parameter(priv, priv->byBBType);
		}
	}

	if (changed & IEEE80211_CONF_CHANGE_POWER) {
		if (priv->byBBType == BB_TYPE_11B)
			priv->wCurrentRate = RATE_1M;
		else
			priv->wCurrentRate = RATE_54M;

		RFbSetPower(priv, priv->wCurrentRate,
			    conf->chandef.chan->hw_value);
	}

	return 0;
}

static void vnt_bss_info_changed(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif,
				 struct ieee80211_bss_conf *conf, u64 changed)
{
	struct vnt_private *priv = hw->priv;

	priv->current_aid = vif->cfg.aid;

	if (changed & BSS_CHANGED_BSSID && conf->bssid) {
		unsigned long flags;

		spin_lock_irqsave(&priv->lock, flags);

		vt6655_mac_write_bssid_addr(priv->port_offset, conf->bssid);

		spin_unlock_irqrestore(&priv->lock, flags);
	}

	if (changed & BSS_CHANGED_BASIC_RATES) {
		priv->basic_rates = conf->basic_rates;

		CARDvUpdateBasicTopRate(priv);

		dev_dbg(&priv->pcid->dev,
			"basic rates %x\n", conf->basic_rates);
	}

	if (changed & BSS_CHANGED_ERP_PREAMBLE) {
		if (conf->use_short_preamble) {
			vt6655_mac_en_barker_preamble_md(priv->port_offset);
			priv->preamble_type = true;
		} else {
			vt6655_mac_dis_barker_preamble_md(priv->port_offset);
			priv->preamble_type = false;
		}
	}

	if (changed & BSS_CHANGED_ERP_CTS_PROT) {
		if (conf->use_cts_prot)
			vt6655_mac_en_protect_md(priv->port_offset);
		else
			vt6655_mac_dis_protect_md(priv->port_offset);
	}

	if (changed & BSS_CHANGED_ERP_SLOT) {
		if (conf->use_short_slot)
			priv->short_slot_time = true;
		else
			priv->short_slot_time = false;

		card_set_phy_parameter(priv, priv->byBBType);
		bb_set_vga_gain_offset(priv, priv->bbvga[0]);
	}

	if (changed & BSS_CHANGED_TXPOWER)
		RFbSetPower(priv, priv->wCurrentRate,
			    conf->chandef.chan->hw_value);

	if (changed & BSS_CHANGED_BEACON_ENABLED) {
		dev_dbg(&priv->pcid->dev,
			"Beacon enable %d\n", conf->enable_beacon);

		if (conf->enable_beacon) {
			vnt_beacon_enable(priv, vif, conf);

			vt6655_mac_reg_bits_on(priv->port_offset, MAC_REG_TCR, TCR_AUTOBCNTX);
		} else {
			vt6655_mac_reg_bits_off(priv->port_offset, MAC_REG_TCR,
						TCR_AUTOBCNTX);
		}
	}

	if (changed & (BSS_CHANGED_ASSOC | BSS_CHANGED_BEACON_INFO) &&
	    priv->op_mode != NL80211_IFTYPE_AP) {
		if (vif->cfg.assoc && conf->beacon_rate) {
			card_update_tsf(priv, conf->beacon_rate->hw_value,
				       conf->sync_tsf);

			CARDbSetBeaconPeriod(priv, conf->beacon_int);

			CARDvSetFirstNextTBTT(priv, conf->beacon_int);
		} else {
			iowrite8(TFTCTL_TSFCNTRST, priv->port_offset + MAC_REG_TFTCTL);
			iowrite8(TFTCTL_TSFCNTREN, priv->port_offset + MAC_REG_TFTCTL);
		}
	}
}

static u64 vnt_prepare_multicast(struct ieee80211_hw *hw,
				 struct netdev_hw_addr_list *mc_list)
{
	struct vnt_private *priv = hw->priv;
	struct netdev_hw_addr *ha;
	u64 mc_filter = 0;
	u32 bit_nr = 0;

	netdev_hw_addr_list_for_each(ha, mc_list) {
		bit_nr = ether_crc(ETH_ALEN, ha->addr) >> 26;

		mc_filter |= 1ULL << (bit_nr & 0x3f);
	}

	priv->mc_list_count = mc_list->count;

	return mc_filter;
}

static void vnt_configure(struct ieee80211_hw *hw,
			  unsigned int changed_flags,
			  unsigned int *total_flags, u64 multicast)
{
	struct vnt_private *priv = hw->priv;
	u8 rx_mode = 0;

	*total_flags &= FIF_ALLMULTI | FIF_OTHER_BSS | FIF_BCN_PRBRESP_PROMISC;

	rx_mode = ioread8(priv->port_offset + MAC_REG_RCR);

	dev_dbg(&priv->pcid->dev, "rx mode in = %x\n", rx_mode);

	if (changed_flags & FIF_ALLMULTI) {
		if (*total_flags & FIF_ALLMULTI) {
			unsigned long flags;

			spin_lock_irqsave(&priv->lock, flags);

			if (priv->mc_list_count > 2) {
				VT6655_MAC_SELECT_PAGE1(priv->port_offset);

				iowrite32(0xffffffff, priv->port_offset + MAC_REG_MAR0);
				iowrite32(0xffffffff, priv->port_offset + MAC_REG_MAR0 + 4);

				VT6655_MAC_SELECT_PAGE0(priv->port_offset);
			} else {
				VT6655_MAC_SELECT_PAGE1(priv->port_offset);

				multicast =  le64_to_cpu(multicast);
				iowrite32((u32)multicast, priv->port_offset +  MAC_REG_MAR0);
				iowrite32((u32)(multicast >> 32),
					  priv->port_offset + MAC_REG_MAR0 + 4);

				VT6655_MAC_SELECT_PAGE0(priv->port_offset);
			}

			spin_unlock_irqrestore(&priv->lock, flags);

			rx_mode |= RCR_MULTICAST | RCR_BROADCAST;
		} else {
			rx_mode &= ~(RCR_MULTICAST | RCR_BROADCAST);
		}
	}

	if (changed_flags & (FIF_OTHER_BSS | FIF_BCN_PRBRESP_PROMISC)) {
		rx_mode |= RCR_MULTICAST | RCR_BROADCAST;

		if (*total_flags & (FIF_OTHER_BSS | FIF_BCN_PRBRESP_PROMISC))
			rx_mode &= ~RCR_BSSID;
		else
			rx_mode |= RCR_BSSID;
	}

	iowrite8(rx_mode, priv->port_offset + MAC_REG_RCR);

	dev_dbg(&priv->pcid->dev, "rx mode out= %x\n", rx_mode);
}

static int vnt_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
		       struct ieee80211_vif *vif, struct ieee80211_sta *sta,
		       struct ieee80211_key_conf *key)
{
	struct vnt_private *priv = hw->priv;

	switch (cmd) {
	case SET_KEY:
		if (vnt_set_keys(hw, sta, vif, key))
			return -EOPNOTSUPP;
		break;
	case DISABLE_KEY:
		if (test_bit(key->hw_key_idx, &priv->key_entry_inuse))
			clear_bit(key->hw_key_idx, &priv->key_entry_inuse);
		break;
	default:
		break;
	}

	return 0;
}

static int vnt_get_stats(struct ieee80211_hw *hw,
			 struct ieee80211_low_level_stats *stats)
{
	struct vnt_private *priv = hw->priv;

	memcpy(stats, &priv->low_stats, sizeof(*stats));

	return 0;
}

static u64 vnt_get_tsf(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct vnt_private *priv = hw->priv;
	u64 tsf;

	tsf = vt6655_get_current_tsf(priv);

	return tsf;
}

static void vnt_set_tsf(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			u64 tsf)
{
	struct vnt_private *priv = hw->priv;

	CARDvUpdateNextTBTT(priv, tsf, vif->bss_conf.beacon_int);
}

static void vnt_reset_tsf(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct vnt_private *priv = hw->priv;

	/* reset TSF counter */
	iowrite8(TFTCTL_TSFCNTRST, priv->port_offset + MAC_REG_TFTCTL);
}

static const struct ieee80211_ops vnt_mac_ops = {
	.tx			= vnt_tx_80211,
	.wake_tx_queue		= ieee80211_handle_wake_tx_queue,
	.start			= vnt_start,
	.stop			= vnt_stop,
	.add_interface		= vnt_add_interface,
	.remove_interface	= vnt_remove_interface,
	.config			= vnt_config,
	.bss_info_changed	= vnt_bss_info_changed,
	.prepare_multicast	= vnt_prepare_multicast,
	.configure_filter	= vnt_configure,
	.set_key		= vnt_set_key,
	.get_stats		= vnt_get_stats,
	.get_tsf		= vnt_get_tsf,
	.set_tsf		= vnt_set_tsf,
	.reset_tsf		= vnt_reset_tsf,
};

static int vnt_init(struct vnt_private *priv)
{
	SET_IEEE80211_PERM_ADDR(priv->hw, priv->abyCurrentNetAddr);

	vnt_init_bands(priv);

	if (ieee80211_register_hw(priv->hw))
		return -ENODEV;

	priv->mac_hw = true;

	CARDbRadioPowerOff(priv);

	return 0;
}

static int
vt6655_probe(struct pci_dev *pcid, const struct pci_device_id *ent)
{
	struct vnt_private *priv;
	struct ieee80211_hw *hw;
	struct wiphy *wiphy;
	int         rc;

	dev_notice(&pcid->dev,
		   "%s Ver. %s\n", DEVICE_FULL_DRV_NAM, DEVICE_VERSION);

	dev_notice(&pcid->dev,
		   "Copyright (c) 2003 VIA Networking Technologies, Inc.\n");

	hw = ieee80211_alloc_hw(sizeof(*priv), &vnt_mac_ops);
	if (!hw) {
		dev_err(&pcid->dev, "could not register ieee80211_hw\n");
		return -ENOMEM;
	}

	priv = hw->priv;
	priv->pcid = pcid;

	spin_lock_init(&priv->lock);

	priv->hw = hw;

	SET_IEEE80211_DEV(priv->hw, &pcid->dev);

	if (pci_enable_device(pcid)) {
		device_free_info(priv);
		return -ENODEV;
	}

	dev_dbg(&pcid->dev,
		"Before get pci_info memaddr is %x\n", priv->memaddr);

	pci_set_master(pcid);

	priv->memaddr = pci_resource_start(pcid, 0);
	priv->ioaddr = pci_resource_start(pcid, 1);
	priv->port_offset = ioremap(priv->memaddr & PCI_BASE_ADDRESS_MEM_MASK,
				   256);
	if (!priv->port_offset) {
		dev_err(&pcid->dev, ": Failed to IO remapping ..\n");
		device_free_info(priv);
		return -ENODEV;
	}

	rc = pci_request_regions(pcid, DEVICE_NAME);
	if (rc) {
		dev_err(&pcid->dev, ": Failed to find PCI device\n");
		device_free_info(priv);
		return -ENODEV;
	}

	if (dma_set_mask(&pcid->dev, DMA_BIT_MASK(32))) {
		dev_err(&pcid->dev, ": Failed to set dma 32 bit mask\n");
		device_free_info(priv);
		return -ENODEV;
	}

	INIT_WORK(&priv->interrupt_work, vnt_interrupt_work);

	/* do reset */
	if (!MACbSoftwareReset(priv)) {
		dev_err(&pcid->dev, ": Failed to access MAC hardware..\n");
		device_free_info(priv);
		return -ENODEV;
	}
	/* initial to reload eeprom */
	MACvInitialize(priv);
	vt6655_mac_read_ether_addr(priv->port_offset, priv->abyCurrentNetAddr);

	/* Get RFType */
	priv->rf_type = SROMbyReadEmbedded(priv->port_offset, EEP_OFS_RFTYPE);
	priv->rf_type &= RF_MASK;

	dev_dbg(&pcid->dev, "RF Type = %x\n", priv->rf_type);

	device_get_options(priv);
	device_set_options(priv);

	wiphy = priv->hw->wiphy;

	wiphy->frag_threshold = FRAG_THRESH_DEF;
	wiphy->rts_threshold = RTS_THRESH_DEF;
	wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) |
		BIT(NL80211_IFTYPE_ADHOC) | BIT(NL80211_IFTYPE_AP);

	ieee80211_hw_set(priv->hw, TIMING_BEACON_ONLY);
	ieee80211_hw_set(priv->hw, SIGNAL_DBM);
	ieee80211_hw_set(priv->hw, RX_INCLUDES_FCS);
	ieee80211_hw_set(priv->hw, REPORTS_TX_ACK_STATUS);
	ieee80211_hw_set(priv->hw, SUPPORTS_PS);

	priv->hw->max_signal = 100;

	if (vnt_init(priv)) {
		device_free_info(priv);
		return -ENODEV;
	}

	device_print_info(priv);
	pci_set_drvdata(pcid, priv);

	return 0;
}

/*------------------------------------------------------------------*/

static int __maybe_unused vt6655_suspend(struct device *dev_d)
{
	struct vnt_private *priv = dev_get_drvdata(dev_d);
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);

	MACbShutdown(priv);

	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static int __maybe_unused vt6655_resume(struct device *dev_d)
{
	device_wakeup_disable(dev_d);

	return 0;
}

MODULE_DEVICE_TABLE(pci, vt6655_pci_id_table);

static SIMPLE_DEV_PM_OPS(vt6655_pm_ops, vt6655_suspend, vt6655_resume);

static struct pci_driver device_driver = {
	.name = DEVICE_NAME,
	.id_table = vt6655_pci_id_table,
	.probe = vt6655_probe,
	.remove = vt6655_remove,
	.driver.pm = &vt6655_pm_ops,
};

module_pci_driver(device_driver);
