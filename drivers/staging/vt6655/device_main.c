/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * File: device_main.c
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
 *   vt6655_init_info - device structure resource allocation function
 *   device_free_info - device structure resource free function
 *   device_get_pci_info - get allocated pci io/mem resource
 *   device_print_info - print out resource
 *   device_intr - interrupt handle function
 *   device_rx_srv - rx service function
 *   device_alloc_rx_buf - rx buffer pre-allocated function
 *   device_free_tx_buf - free tx buffer function
 *   device_init_rd0_ring- initial rd dma0 ring
 *   device_init_rd1_ring- initial rd dma1 ring
 *   device_init_td0_ring- initial tx dma0 ring buffer
 *   device_init_td1_ring- initial tx dma1 ring buffer
 *   device_init_registers- initial MAC & BBP & RF internal registers.
 *   device_init_rings- initial tx/rx ring buffer
 *   device_free_rings- free all allocated ring buffer
 *   device_tx_srv- tx interrupt service function
 *
 * Revision History:
 */
#undef __NO_VERSION__

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
//
// Define module options
//
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
   0: indicate 802.11a type
   1: indicate 802.11b type
   2: indicate 802.11g type
*/
#define BBP_TYPE_MIN     0
#define BBP_TYPE_MAX     2
#define BBP_TYPE_DEF     2

DEVICE_PARAM(BasebandType, "baseband type");

//
// Static vars definitions
//
static CHIP_INFO chip_info_table[] = {
	{ VT3253,       "VIA Networking Solomon-A/B/G Wireless LAN Adapter ",
	  256, 1,     DEVICE_FLAGS_IP_ALIGN|DEVICE_FLAGS_TX_ALIGN },
	{0, NULL}
};

static const struct pci_device_id vt6655_pci_id_table[] = {
	{ PCI_VDEVICE(VIA, 0x3253), (kernel_ulong_t)chip_info_table},
	{ 0, }
};

/*---------------------  Static Functions  --------------------------*/

static int  vt6655_probe(struct pci_dev *pcid, const struct pci_device_id *ent);
static void vt6655_init_info(struct pci_dev *pcid,
			     struct vnt_private **ppDevice, PCHIP_INFO);
static void device_free_info(struct vnt_private *pDevice);
static bool device_get_pci_info(struct vnt_private *, struct pci_dev *pcid);
static void device_print_info(struct vnt_private *pDevice);
static  irqreturn_t  device_intr(int irq,  void *dev_instance);

#ifdef CONFIG_PM
static int device_notify_reboot(struct notifier_block *, unsigned long event, void *ptr);
static struct notifier_block device_notifier = {
	.notifier_call = device_notify_reboot,
	.next = NULL,
	.priority = 0,
};
#endif

static void device_init_rd0_ring(struct vnt_private *pDevice);
static void device_init_rd1_ring(struct vnt_private *pDevice);
static void device_init_td0_ring(struct vnt_private *pDevice);
static void device_init_td1_ring(struct vnt_private *pDevice);

static int  device_rx_srv(struct vnt_private *pDevice, unsigned int uIdx);
static int  device_tx_srv(struct vnt_private *pDevice, unsigned int uIdx);
static bool device_alloc_rx_buf(struct vnt_private *pDevice, PSRxDesc pDesc);
static void device_init_registers(struct vnt_private *pDevice);
static void device_free_tx_buf(struct vnt_private *pDevice, PSTxDesc pDesc);
static void device_free_td0_ring(struct vnt_private *pDevice);
static void device_free_td1_ring(struct vnt_private *pDevice);
static void device_free_rd0_ring(struct vnt_private *pDevice);
static void device_free_rd1_ring(struct vnt_private *pDevice);
static void device_free_rings(struct vnt_private *pDevice);

/*---------------------  Export Variables  --------------------------*/

/*---------------------  Export Functions  --------------------------*/

static char *get_chip_name(int chip_id)
{
	int i;

	for (i = 0; chip_info_table[i].name != NULL; i++)
		if (chip_info_table[i].chip_id == chip_id)
			break;
	return chip_info_table[i].name;
}

static void vt6655_remove(struct pci_dev *pcid)
{
	struct vnt_private *pDevice = pci_get_drvdata(pcid);

	if (pDevice == NULL)
		return;
	device_free_info(pDevice);
}

static void device_get_options(struct vnt_private *pDevice)
{
	POPTIONS pOpts = &(pDevice->sOpts);

	pOpts->nRxDescs0 = RX_DESC_DEF0;
	pOpts->nRxDescs1 = RX_DESC_DEF1;
	pOpts->nTxDescs[0] = TX_DESC_DEF0;
	pOpts->nTxDescs[1] = TX_DESC_DEF1;
	pOpts->int_works = INT_WORKS_DEF;

	pOpts->short_retry = SHORT_RETRY_DEF;
	pOpts->long_retry = LONG_RETRY_DEF;
	pOpts->bbp_type = BBP_TYPE_DEF;
}

static void
device_set_options(struct vnt_private *pDevice)
{
	pDevice->byShortRetryLimit = pDevice->sOpts.short_retry;
	pDevice->byLongRetryLimit = pDevice->sOpts.long_retry;
	pDevice->byBBType = pDevice->sOpts.bbp_type;
	pDevice->byPacketType = pDevice->byBBType;
	pDevice->byAutoFBCtrl = AUTO_FB_0;
	pDevice->bUpdateBBVGA = true;
	pDevice->byPreambleType = 0;

	pr_debug(" byShortRetryLimit= %d\n", (int)pDevice->byShortRetryLimit);
	pr_debug(" byLongRetryLimit= %d\n", (int)pDevice->byLongRetryLimit);
	pr_debug(" byPreambleType= %d\n", (int)pDevice->byPreambleType);
	pr_debug(" byShortPreamble= %d\n", (int)pDevice->byShortPreamble);
	pr_debug(" byBBType= %d\n", (int)pDevice->byBBType);
}

//
// Initialisation of MAC & BBP registers
//

static void device_init_registers(struct vnt_private *pDevice)
{
	unsigned long flags;
	unsigned int ii;
	unsigned char byValue;
	unsigned char byCCKPwrdBm = 0;
	unsigned char byOFDMPwrdBm = 0;

	MACbShutdown(pDevice->PortOffset);
	BBvSoftwareReset(pDevice);

	/* Do MACbSoftwareReset in MACvInitialize */
	MACbSoftwareReset(pDevice->PortOffset);

	pDevice->bAES = false;

	/* Only used in 11g type, sync with ERP IE */
	pDevice->bProtectMode = false;

	pDevice->bNonERPPresent = false;
	pDevice->bBarkerPreambleMd = false;
	pDevice->wCurrentRate = RATE_1M;
	pDevice->byTopOFDMBasicRate = RATE_24M;
	pDevice->byTopCCKBasicRate = RATE_1M;

	/* Target to IF pin while programming to RF chip. */
	pDevice->byRevId = 0;

	/* init MAC */
	MACvInitialize(pDevice->PortOffset);

	/* Get Local ID */
	VNSvInPortB(pDevice->PortOffset + MAC_REG_LOCALID, &pDevice->byLocalID);

	spin_lock_irqsave(&pDevice->lock, flags);

	SROMvReadAllContents(pDevice->PortOffset, pDevice->abyEEPROM);

	spin_unlock_irqrestore(&pDevice->lock, flags);

	/* Get Channel range */
	pDevice->byMinChannel = 1;
	pDevice->byMaxChannel = CB_MAX_CHANNEL;

	/* Get Antena */
	byValue = SROMbyReadEmbedded(pDevice->PortOffset, EEP_OFS_ANTENNA);
	if (byValue & EEP_ANTINV)
		pDevice->bTxRxAntInv = true;
	else
		pDevice->bTxRxAntInv = false;

	byValue &= (EEP_ANTENNA_AUX | EEP_ANTENNA_MAIN);
	/* if not set default is All */
	if (byValue == 0)
		byValue = (EEP_ANTENNA_AUX | EEP_ANTENNA_MAIN);

	if (byValue == (EEP_ANTENNA_AUX | EEP_ANTENNA_MAIN)) {
		pDevice->byAntennaCount = 2;
		pDevice->byTxAntennaMode = ANT_B;
		pDevice->dwTxAntennaSel = 1;
		pDevice->dwRxAntennaSel = 1;

		if (pDevice->bTxRxAntInv)
			pDevice->byRxAntennaMode = ANT_A;
		else
			pDevice->byRxAntennaMode = ANT_B;
	} else  {
		pDevice->byAntennaCount = 1;
		pDevice->dwTxAntennaSel = 0;
		pDevice->dwRxAntennaSel = 0;

		if (byValue & EEP_ANTENNA_AUX) {
			pDevice->byTxAntennaMode = ANT_A;

			if (pDevice->bTxRxAntInv)
				pDevice->byRxAntennaMode = ANT_B;
			else
				pDevice->byRxAntennaMode = ANT_A;
		} else {
			pDevice->byTxAntennaMode = ANT_B;

			if (pDevice->bTxRxAntInv)
				pDevice->byRxAntennaMode = ANT_A;
			else
				pDevice->byRxAntennaMode = ANT_B;
		}
	}

	/* Set initial antenna mode */
	BBvSetTxAntennaMode(pDevice, pDevice->byTxAntennaMode);
	BBvSetRxAntennaMode(pDevice, pDevice->byRxAntennaMode);

	/* zonetype initial */
	pDevice->byOriginalZonetype = pDevice->abyEEPROM[EEP_OFS_ZONETYPE];

	/* Get RFType */
	pDevice->byRFType = SROMbyReadEmbedded(pDevice->PortOffset, EEP_OFS_RFTYPE);

	/* force change RevID for VT3253 emu */
	if ((pDevice->byRFType & RF_EMU) != 0)
			pDevice->byRevId = 0x80;

	pDevice->byRFType &= RF_MASK;
	pr_debug("pDevice->byRFType = %x\n", pDevice->byRFType);

	if (!pDevice->bZoneRegExist)
		pDevice->byZoneType = pDevice->abyEEPROM[EEP_OFS_ZONETYPE];

	pr_debug("pDevice->byZoneType = %x\n", pDevice->byZoneType);

	/* Init RF module */
	RFbInit(pDevice);

	/* Get Desire Power Value */
	pDevice->byCurPwr = 0xFF;
	pDevice->byCCKPwr = SROMbyReadEmbedded(pDevice->PortOffset, EEP_OFS_PWR_CCK);
	pDevice->byOFDMPwrG = SROMbyReadEmbedded(pDevice->PortOffset, EEP_OFS_PWR_OFDMG);

	/* Load power Table */
	for (ii = 0; ii < CB_MAX_CHANNEL_24G; ii++) {
		pDevice->abyCCKPwrTbl[ii + 1] =
			SROMbyReadEmbedded(pDevice->PortOffset,
					   (unsigned char)(ii + EEP_OFS_CCK_PWR_TBL));
		if (pDevice->abyCCKPwrTbl[ii + 1] == 0)
			pDevice->abyCCKPwrTbl[ii+1] = pDevice->byCCKPwr;

		pDevice->abyOFDMPwrTbl[ii + 1] =
			SROMbyReadEmbedded(pDevice->PortOffset,
					   (unsigned char)(ii + EEP_OFS_OFDM_PWR_TBL));
		if (pDevice->abyOFDMPwrTbl[ii + 1] == 0)
			pDevice->abyOFDMPwrTbl[ii + 1] = pDevice->byOFDMPwrG;

		pDevice->abyCCKDefaultPwr[ii + 1] = byCCKPwrdBm;
		pDevice->abyOFDMDefaultPwr[ii + 1] = byOFDMPwrdBm;
	}

	/* recover 12,13 ,14channel for EUROPE by 11 channel */
	for (ii = 11; ii < 14; ii++) {
		pDevice->abyCCKPwrTbl[ii] = pDevice->abyCCKPwrTbl[10];
		pDevice->abyOFDMPwrTbl[ii] = pDevice->abyOFDMPwrTbl[10];
	}

	/* Load OFDM A Power Table */
	for (ii = 0; ii < CB_MAX_CHANNEL_5G; ii++) {
		pDevice->abyOFDMPwrTbl[ii + CB_MAX_CHANNEL_24G + 1] =
			SROMbyReadEmbedded(pDevice->PortOffset,
					   (unsigned char)(ii + EEP_OFS_OFDMA_PWR_TBL));

		pDevice->abyOFDMDefaultPwr[ii + CB_MAX_CHANNEL_24G + 1] =
			SROMbyReadEmbedded(pDevice->PortOffset,
					   (unsigned char)(ii + EEP_OFS_OFDMA_PWR_dBm));
	}

	if (pDevice->byLocalID > REV_ID_VT3253_B1) {
		MACvSelectPage1(pDevice->PortOffset);

		VNSvOutPortB(pDevice->PortOffset + MAC_REG_MSRCTL + 1,
			     (MSRCTL1_TXPWR | MSRCTL1_CSAPAREN));

		MACvSelectPage0(pDevice->PortOffset);
	}

	/* use relative tx timeout and 802.11i D4 */
	MACvWordRegBitsOn(pDevice->PortOffset,
			  MAC_REG_CFG, (CFG_TKIPOPT | CFG_NOTXTIMEOUT));

	/* set performance parameter by registry */
	MACvSetShortRetryLimit(pDevice->PortOffset, pDevice->byShortRetryLimit);
	MACvSetLongRetryLimit(pDevice->PortOffset, pDevice->byLongRetryLimit);

	/* reset TSF counter */
	VNSvOutPortB(pDevice->PortOffset + MAC_REG_TFTCTL, TFTCTL_TSFCNTRST);
	/* enable TSF counter */
	VNSvOutPortB(pDevice->PortOffset + MAC_REG_TFTCTL, TFTCTL_TSFCNTREN);

	/* initialize BBP registers */
	BBbVT3253Init(pDevice);

	if (pDevice->bUpdateBBVGA) {
		pDevice->byBBVGACurrent = pDevice->abyBBVGA[0];
		pDevice->byBBVGANew = pDevice->byBBVGACurrent;
		BBvSetVGAGainOffset(pDevice, pDevice->abyBBVGA[0]);
	}

	BBvSetRxAntennaMode(pDevice, pDevice->byRxAntennaMode);
	BBvSetTxAntennaMode(pDevice, pDevice->byTxAntennaMode);

	/* Set BB and packet type at the same time. */
	/* Set Short Slot Time, xIFS, and RSPINF. */
	pDevice->wCurrentRate = RATE_54M;

	pDevice->bRadioOff = false;

	pDevice->byRadioCtl = SROMbyReadEmbedded(pDevice->PortOffset,
						 EEP_OFS_RADIOCTL);
	pDevice->bHWRadioOff = false;

	if (pDevice->byRadioCtl & EEP_RADIOCTL_ENABLE) {
		/* Get GPIO */
		MACvGPIOIn(pDevice->PortOffset, &pDevice->byGPIO);

		if (((pDevice->byGPIO & GPIO0_DATA) &&
		     !(pDevice->byRadioCtl & EEP_RADIOCTL_INV)) ||
		     (!(pDevice->byGPIO & GPIO0_DATA) &&
		     (pDevice->byRadioCtl & EEP_RADIOCTL_INV)))
			pDevice->bHWRadioOff = true;
	}

	if (pDevice->bHWRadioOff || pDevice->bRadioControlOff)
		CARDbRadioPowerOff(pDevice);

	/* get Permanent network address */
	SROMvReadEtherAddress(pDevice->PortOffset, pDevice->abyCurrentNetAddr);
	pr_debug("Network address = %pM\n", pDevice->abyCurrentNetAddr);

	/* reset Tx pointer */
	CARDvSafeResetRx(pDevice);
	/* reset Rx pointer */
	CARDvSafeResetTx(pDevice);

	if (pDevice->byLocalID <= REV_ID_VT3253_A1)
		MACvRegBitsOn(pDevice->PortOffset, MAC_REG_RCR, RCR_WPAERR);

	/* Turn On Rx DMA */
	MACvReceive0(pDevice->PortOffset);
	MACvReceive1(pDevice->PortOffset);

	/* start the adapter */
	MACvStart(pDevice->PortOffset);
}

static void device_print_info(struct vnt_private *pDevice)
{
	dev_info(&pDevice->pcid->dev, "%s\n", get_chip_name(pDevice->chip_id));

	dev_info(&pDevice->pcid->dev, "MAC=%pM IO=0x%lx Mem=0x%lx IRQ=%d\n",
		 pDevice->abyCurrentNetAddr, (unsigned long)pDevice->ioaddr,
		 (unsigned long)pDevice->PortOffset, pDevice->pcid->irq);
}

static void vt6655_init_info(struct pci_dev *pcid,
			     struct vnt_private **ppDevice,
			     PCHIP_INFO pChip_info)
{
	memset(*ppDevice, 0, sizeof(**ppDevice));

	(*ppDevice)->pcid = pcid;
	(*ppDevice)->chip_id = pChip_info->chip_id;
	(*ppDevice)->io_size = pChip_info->io_size;
	(*ppDevice)->nTxQueues = pChip_info->nTxQueue;
	(*ppDevice)->multicast_limit = 32;

	spin_lock_init(&((*ppDevice)->lock));
}

static bool device_get_pci_info(struct vnt_private *pDevice,
				struct pci_dev *pcid)
{
	u16 pci_cmd;
	u8  b;
	unsigned int cis_addr;

	pci_read_config_byte(pcid, PCI_REVISION_ID, &pDevice->byRevId);
	pci_read_config_word(pcid, PCI_SUBSYSTEM_ID, &pDevice->SubSystemID);
	pci_read_config_word(pcid, PCI_SUBSYSTEM_VENDOR_ID, &pDevice->SubVendorID);
	pci_read_config_word(pcid, PCI_COMMAND, (u16 *)&(pci_cmd));

	pci_set_master(pcid);

	pDevice->memaddr = pci_resource_start(pcid, 0);
	pDevice->ioaddr = pci_resource_start(pcid, 1);

	cis_addr = pci_resource_start(pcid, 2);

	pDevice->pcid = pcid;

	pci_read_config_byte(pcid, PCI_COMMAND, &b);
	pci_write_config_byte(pcid, PCI_COMMAND, (b|PCI_COMMAND_MASTER));

	return true;
}

static void device_free_info(struct vnt_private *pDevice)
{
	if (!pDevice)
		return;

	if (pDevice->mac_hw)
		ieee80211_unregister_hw(pDevice->hw);

	if (pDevice->PortOffset)
		iounmap(pDevice->PortOffset);

	if (pDevice->pcid)
		pci_release_regions(pDevice->pcid);

	if (pDevice->hw)
		ieee80211_free_hw(pDevice->hw);
}

static bool device_init_rings(struct vnt_private *pDevice)
{
	void *vir_pool;

	/*allocate all RD/TD rings a single pool*/
	vir_pool = pci_zalloc_consistent(pDevice->pcid,
					 pDevice->sOpts.nRxDescs0 * sizeof(SRxDesc) +
					 pDevice->sOpts.nRxDescs1 * sizeof(SRxDesc) +
					 pDevice->sOpts.nTxDescs[0] * sizeof(STxDesc) +
					 pDevice->sOpts.nTxDescs[1] * sizeof(STxDesc),
					 &pDevice->pool_dma);
	if (vir_pool == NULL) {
		dev_err(&pDevice->pcid->dev, "allocate desc dma memory failed\n");
		return false;
	}

	pDevice->aRD0Ring = vir_pool;
	pDevice->aRD1Ring = vir_pool +
		pDevice->sOpts.nRxDescs0 * sizeof(SRxDesc);

	pDevice->rd0_pool_dma = pDevice->pool_dma;
	pDevice->rd1_pool_dma = pDevice->rd0_pool_dma +
		pDevice->sOpts.nRxDescs0 * sizeof(SRxDesc);

	pDevice->tx0_bufs = pci_zalloc_consistent(pDevice->pcid,
						  pDevice->sOpts.nTxDescs[0] * PKT_BUF_SZ +
						  pDevice->sOpts.nTxDescs[1] * PKT_BUF_SZ +
						  CB_BEACON_BUF_SIZE +
						  CB_MAX_BUF_SIZE,
						  &pDevice->tx_bufs_dma0);
	if (pDevice->tx0_bufs == NULL) {
		dev_err(&pDevice->pcid->dev, "allocate buf dma memory failed\n");

		pci_free_consistent(pDevice->pcid,
				    pDevice->sOpts.nRxDescs0 * sizeof(SRxDesc) +
				    pDevice->sOpts.nRxDescs1 * sizeof(SRxDesc) +
				    pDevice->sOpts.nTxDescs[0] * sizeof(STxDesc) +
				    pDevice->sOpts.nTxDescs[1] * sizeof(STxDesc),
				    vir_pool, pDevice->pool_dma
			);
		return false;
	}

	pDevice->td0_pool_dma = pDevice->rd1_pool_dma +
		pDevice->sOpts.nRxDescs1 * sizeof(SRxDesc);

	pDevice->td1_pool_dma = pDevice->td0_pool_dma +
		pDevice->sOpts.nTxDescs[0] * sizeof(STxDesc);

	// vir_pool: pvoid type
	pDevice->apTD0Rings = vir_pool
		+ pDevice->sOpts.nRxDescs0 * sizeof(SRxDesc)
		+ pDevice->sOpts.nRxDescs1 * sizeof(SRxDesc);

	pDevice->apTD1Rings = vir_pool
		+ pDevice->sOpts.nRxDescs0 * sizeof(SRxDesc)
		+ pDevice->sOpts.nRxDescs1 * sizeof(SRxDesc)
		+ pDevice->sOpts.nTxDescs[0] * sizeof(STxDesc);

	pDevice->tx1_bufs = pDevice->tx0_bufs +
		pDevice->sOpts.nTxDescs[0] * PKT_BUF_SZ;

	pDevice->tx_beacon_bufs = pDevice->tx1_bufs +
		pDevice->sOpts.nTxDescs[1] * PKT_BUF_SZ;

	pDevice->pbyTmpBuff = pDevice->tx_beacon_bufs +
		CB_BEACON_BUF_SIZE;

	pDevice->tx_bufs_dma1 = pDevice->tx_bufs_dma0 +
		pDevice->sOpts.nTxDescs[0] * PKT_BUF_SZ;

	pDevice->tx_beacon_dma = pDevice->tx_bufs_dma1 +
		pDevice->sOpts.nTxDescs[1] * PKT_BUF_SZ;

	return true;
}

static void device_free_rings(struct vnt_private *pDevice)
{
	pci_free_consistent(pDevice->pcid,
			    pDevice->sOpts.nRxDescs0 * sizeof(SRxDesc) +
			    pDevice->sOpts.nRxDescs1 * sizeof(SRxDesc) +
			    pDevice->sOpts.nTxDescs[0] * sizeof(STxDesc) +
			    pDevice->sOpts.nTxDescs[1] * sizeof(STxDesc)
			    ,
			    pDevice->aRD0Ring, pDevice->pool_dma
		);

	if (pDevice->tx0_bufs)
		pci_free_consistent(pDevice->pcid,
				    pDevice->sOpts.nTxDescs[0] * PKT_BUF_SZ +
				    pDevice->sOpts.nTxDescs[1] * PKT_BUF_SZ +
				    CB_BEACON_BUF_SIZE +
				    CB_MAX_BUF_SIZE,
				    pDevice->tx0_bufs, pDevice->tx_bufs_dma0
			);
}

static void device_init_rd0_ring(struct vnt_private *pDevice)
{
	int i;
	dma_addr_t      curr = pDevice->rd0_pool_dma;
	PSRxDesc        pDesc;

	/* Init the RD0 ring entries */
	for (i = 0; i < pDevice->sOpts.nRxDescs0; i ++, curr += sizeof(SRxDesc)) {
		pDesc = &(pDevice->aRD0Ring[i]);
		pDesc->pRDInfo = alloc_rd_info();
		ASSERT(pDesc->pRDInfo);
		if (!device_alloc_rx_buf(pDevice, pDesc))
			dev_err(&pDevice->pcid->dev, "can not alloc rx bufs\n");

		pDesc->next = &(pDevice->aRD0Ring[(i+1) % pDevice->sOpts.nRxDescs0]);
		pDesc->pRDInfo->curr_desc = cpu_to_le32(curr);
		pDesc->next_desc = cpu_to_le32(curr + sizeof(SRxDesc));
	}

	if (i > 0)
		pDevice->aRD0Ring[i-1].next_desc = cpu_to_le32(pDevice->rd0_pool_dma);
	pDevice->pCurrRD[0] = &(pDevice->aRD0Ring[0]);
}

static void device_init_rd1_ring(struct vnt_private *pDevice)
{
	int i;
	dma_addr_t      curr = pDevice->rd1_pool_dma;
	PSRxDesc        pDesc;

	/* Init the RD1 ring entries */
	for (i = 0; i < pDevice->sOpts.nRxDescs1; i ++, curr += sizeof(SRxDesc)) {
		pDesc = &(pDevice->aRD1Ring[i]);
		pDesc->pRDInfo = alloc_rd_info();
		ASSERT(pDesc->pRDInfo);
		if (!device_alloc_rx_buf(pDevice, pDesc))
			dev_err(&pDevice->pcid->dev, "can not alloc rx bufs\n");

		pDesc->next = &(pDevice->aRD1Ring[(i+1) % pDevice->sOpts.nRxDescs1]);
		pDesc->pRDInfo->curr_desc = cpu_to_le32(curr);
		pDesc->next_desc = cpu_to_le32(curr + sizeof(SRxDesc));
	}

	if (i > 0)
		pDevice->aRD1Ring[i-1].next_desc = cpu_to_le32(pDevice->rd1_pool_dma);
	pDevice->pCurrRD[1] = &(pDevice->aRD1Ring[0]);
}

static void device_free_rd0_ring(struct vnt_private *pDevice)
{
	int i;

	for (i = 0; i < pDevice->sOpts.nRxDescs0; i++) {
		PSRxDesc        pDesc = &(pDevice->aRD0Ring[i]);
		PDEVICE_RD_INFO  pRDInfo = pDesc->pRDInfo;

		pci_unmap_single(pDevice->pcid, pRDInfo->skb_dma,
				 pDevice->rx_buf_sz, PCI_DMA_FROMDEVICE);

		dev_kfree_skb(pRDInfo->skb);

		kfree(pDesc->pRDInfo);
	}
}

static void device_free_rd1_ring(struct vnt_private *pDevice)
{
	int i;

	for (i = 0; i < pDevice->sOpts.nRxDescs1; i++) {
		PSRxDesc        pDesc = &(pDevice->aRD1Ring[i]);
		PDEVICE_RD_INFO  pRDInfo = pDesc->pRDInfo;

		pci_unmap_single(pDevice->pcid, pRDInfo->skb_dma,
				 pDevice->rx_buf_sz, PCI_DMA_FROMDEVICE);

		dev_kfree_skb(pRDInfo->skb);

		kfree(pDesc->pRDInfo);
	}
}

static void device_init_td0_ring(struct vnt_private *pDevice)
{
	int i;
	dma_addr_t  curr;
	PSTxDesc        pDesc;

	curr = pDevice->td0_pool_dma;
	for (i = 0; i < pDevice->sOpts.nTxDescs[0]; i++, curr += sizeof(STxDesc)) {
		pDesc = &(pDevice->apTD0Rings[i]);
		pDesc->pTDInfo = alloc_td_info();
		ASSERT(pDesc->pTDInfo);
		if (pDevice->flags & DEVICE_FLAGS_TX_ALIGN) {
			pDesc->pTDInfo->buf = pDevice->tx0_bufs + (i)*PKT_BUF_SZ;
			pDesc->pTDInfo->buf_dma = pDevice->tx_bufs_dma0 + (i)*PKT_BUF_SZ;
		}
		pDesc->next = &(pDevice->apTD0Rings[(i+1) % pDevice->sOpts.nTxDescs[0]]);
		pDesc->pTDInfo->curr_desc = cpu_to_le32(curr);
		pDesc->next_desc = cpu_to_le32(curr+sizeof(STxDesc));
	}

	if (i > 0)
		pDevice->apTD0Rings[i-1].next_desc = cpu_to_le32(pDevice->td0_pool_dma);
	pDevice->apTailTD[0] = pDevice->apCurrTD[0] = &(pDevice->apTD0Rings[0]);
}

static void device_init_td1_ring(struct vnt_private *pDevice)
{
	int i;
	dma_addr_t  curr;
	PSTxDesc    pDesc;

	/* Init the TD ring entries */
	curr = pDevice->td1_pool_dma;
	for (i = 0; i < pDevice->sOpts.nTxDescs[1]; i++, curr += sizeof(STxDesc)) {
		pDesc = &(pDevice->apTD1Rings[i]);
		pDesc->pTDInfo = alloc_td_info();
		ASSERT(pDesc->pTDInfo);
		if (pDevice->flags & DEVICE_FLAGS_TX_ALIGN) {
			pDesc->pTDInfo->buf = pDevice->tx1_bufs + (i) * PKT_BUF_SZ;
			pDesc->pTDInfo->buf_dma = pDevice->tx_bufs_dma1 + (i) * PKT_BUF_SZ;
		}
		pDesc->next = &(pDevice->apTD1Rings[(i + 1) % pDevice->sOpts.nTxDescs[1]]);
		pDesc->pTDInfo->curr_desc = cpu_to_le32(curr);
		pDesc->next_desc = cpu_to_le32(curr+sizeof(STxDesc));
	}

	if (i > 0)
		pDevice->apTD1Rings[i-1].next_desc = cpu_to_le32(pDevice->td1_pool_dma);
	pDevice->apTailTD[1] = pDevice->apCurrTD[1] = &(pDevice->apTD1Rings[0]);
}

static void device_free_td0_ring(struct vnt_private *pDevice)
{
	int i;

	for (i = 0; i < pDevice->sOpts.nTxDescs[0]; i++) {
		PSTxDesc        pDesc = &(pDevice->apTD0Rings[i]);
		PDEVICE_TD_INFO  pTDInfo = pDesc->pTDInfo;

		if (pTDInfo->skb_dma && (pTDInfo->skb_dma != pTDInfo->buf_dma))
			pci_unmap_single(pDevice->pcid, pTDInfo->skb_dma,
					 pTDInfo->skb->len, PCI_DMA_TODEVICE);

		if (pTDInfo->skb)
			dev_kfree_skb(pTDInfo->skb);

		kfree(pDesc->pTDInfo);
	}
}

static void device_free_td1_ring(struct vnt_private *pDevice)
{
	int i;

	for (i = 0; i < pDevice->sOpts.nTxDescs[1]; i++) {
		PSTxDesc        pDesc = &(pDevice->apTD1Rings[i]);
		PDEVICE_TD_INFO  pTDInfo = pDesc->pTDInfo;

		if (pTDInfo->skb_dma && (pTDInfo->skb_dma != pTDInfo->buf_dma))
			pci_unmap_single(pDevice->pcid, pTDInfo->skb_dma,
					 pTDInfo->skb->len, PCI_DMA_TODEVICE);

		if (pTDInfo->skb)
			dev_kfree_skb(pTDInfo->skb);

		kfree(pDesc->pTDInfo);
	}
}

/*-----------------------------------------------------------------*/

static int device_rx_srv(struct vnt_private *pDevice, unsigned int uIdx)
{
	PSRxDesc    pRD;
	int works = 0;

	for (pRD = pDevice->pCurrRD[uIdx];
	     pRD->m_rd0RD0.f1Owner == OWNED_BY_HOST;
	     pRD = pRD->next) {
		if (works++ > 15)
			break;
		if (vnt_receive_frame(pDevice, pRD)) {
			if (!device_alloc_rx_buf(pDevice, pRD)) {
				dev_err(&pDevice->pcid->dev,
					"can not allocate rx buf\n");
				break;
			}
		}
		pRD->m_rd0RD0.f1Owner = OWNED_BY_NIC;
	}

	pDevice->pCurrRD[uIdx] = pRD;

	return works;
}

static bool device_alloc_rx_buf(struct vnt_private *pDevice, PSRxDesc pRD)
{
	PDEVICE_RD_INFO pRDInfo = pRD->pRDInfo;

	pRDInfo->skb = dev_alloc_skb((int)pDevice->rx_buf_sz);
	if (pRDInfo->skb == NULL)
		return false;
	ASSERT(pRDInfo->skb);

	pRDInfo->skb_dma =
		pci_map_single(pDevice->pcid,
			       skb_put(pRDInfo->skb, skb_tailroom(pRDInfo->skb)),
			       pDevice->rx_buf_sz, PCI_DMA_FROMDEVICE);

	*((unsigned int *)&(pRD->m_rd0RD0)) = 0; /* FIX cast */

	pRD->m_rd0RD0.wResCount = cpu_to_le16(pDevice->rx_buf_sz);
	pRD->m_rd0RD0.f1Owner = OWNED_BY_NIC;
	pRD->m_rd1RD1.wReqCount = cpu_to_le16(pDevice->rx_buf_sz);
	pRD->buff_addr = cpu_to_le32(pRDInfo->skb_dma);

	return true;
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
			       PDEVICE_TD_INFO context, u8 tsr0, u8 tsr1)
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

		if (info->band == IEEE80211_BAND_5GHZ)
			idx = tx_rate - RATE_6M;
		else
			idx = tx_rate;
	}

	ieee80211_tx_info_clear_status(info);

	info->status.rates[0].count = tx_retry;

	if (!(tsr1 & TSR1_TERR)) {
		info->status.rates[0].idx = idx;
		info->flags |= IEEE80211_TX_STAT_ACK;
	}

	return 0;
}

static int device_tx_srv(struct vnt_private *pDevice, unsigned int uIdx)
{
	PSTxDesc                 pTD;
	int                      works = 0;
	unsigned char byTsr0;
	unsigned char byTsr1;

	for (pTD = pDevice->apTailTD[uIdx]; pDevice->iTDUsed[uIdx] > 0; pTD = pTD->next) {
		if (pTD->m_td0TD0.f1Owner == OWNED_BY_NIC)
			break;
		if (works++ > 15)
			break;

		byTsr0 = pTD->m_td0TD0.byTSR0;
		byTsr1 = pTD->m_td0TD0.byTSR1;

		//Only the status of first TD in the chain is correct
		if (pTD->m_td1TD1.byTCR & TCR_STP) {
			if ((pTD->pTDInfo->byFlags & TD_FLAGS_NETIF_SKB) != 0) {

				vnt_int_report_rate(pDevice, pTD->pTDInfo, byTsr0, byTsr1);

				if (!(byTsr1 & TSR1_TERR)) {
					if (byTsr0 != 0) {
						pr_debug(" Tx[%d] OK but has error. tsr1[%02X] tsr0[%02X]\n",
							 (int)uIdx, byTsr1,
							 byTsr0);
					}
				} else {
					pr_debug(" Tx[%d] dropped & tsr1[%02X] tsr0[%02X]\n",
						 (int)uIdx, byTsr1, byTsr0);
				}
			}

			if (byTsr1 & TSR1_TERR) {
				if ((pTD->pTDInfo->byFlags & TD_FLAGS_PRIV_SKB) != 0) {
					pr_debug(" Tx[%d] fail has error. tsr1[%02X] tsr0[%02X]\n",
						 (int)uIdx, byTsr1, byTsr0);
				}
			}
			device_free_tx_buf(pDevice, pTD);
			pDevice->iTDUsed[uIdx]--;
		}
	}

	pDevice->apTailTD[uIdx] = pTD;

	return works;
}

static void device_error(struct vnt_private *pDevice, unsigned short status)
{
	if (status & ISR_FETALERR) {
		dev_err(&pDevice->pcid->dev, "Hardware fatal error\n");

		MACbShutdown(pDevice->PortOffset);
		return;
	}
}

static void device_free_tx_buf(struct vnt_private *pDevice, PSTxDesc pDesc)
{
	PDEVICE_TD_INFO  pTDInfo = pDesc->pTDInfo;
	struct sk_buff *skb = pTDInfo->skb;

	// pre-allocated buf_dma can't be unmapped.
	if (pTDInfo->skb_dma && (pTDInfo->skb_dma != pTDInfo->buf_dma)) {
		pci_unmap_single(pDevice->pcid, pTDInfo->skb_dma, skb->len,
				 PCI_DMA_TODEVICE);
	}

	if (pTDInfo->byFlags & TD_FLAGS_NETIF_SKB)
		ieee80211_tx_status_irqsafe(pDevice->hw, skb);
	else
		dev_kfree_skb_irq(skb);

	pTDInfo->skb_dma = 0;
	pTDInfo->skb = NULL;
	pTDInfo->byFlags = 0;
}

static void vnt_check_bb_vga(struct vnt_private *priv)
{
	long dbm;
	int i;

	if (!priv->bUpdateBBVGA)
		return;

	if (priv->hw->conf.flags & IEEE80211_CONF_OFFCHANNEL)
		return;

	if (!(priv->vif->bss_conf.assoc && priv->uCurrRSSI))
		return;

	RFvRSSITodBm(priv, (u8)priv->uCurrRSSI, &dbm);

	for (i = 0; i < BB_VGA_LEVEL; i++) {
		if (dbm < priv->ldBmThreshold[i]) {
			priv->byBBVGANew = priv->abyBBVGA[i];
			break;
		}
	}

	if (priv->byBBVGANew == priv->byBBVGACurrent) {
		priv->uBBVGADiffCount = 1;
		return;
	}

	priv->uBBVGADiffCount++;

	if (priv->uBBVGADiffCount == 1) {
		/* first VGA diff gain */
		BBvSetVGAGainOffset(priv, priv->byBBVGANew);

		dev_dbg(&priv->pcid->dev,
			"First RSSI[%d] NewGain[%d] OldGain[%d] Count[%d]\n",
			(int)dbm, priv->byBBVGANew,
			priv->byBBVGACurrent,
			(int)priv->uBBVGADiffCount);
	}

	if (priv->uBBVGADiffCount >= BB_VGA_CHANGE_THRESHOLD) {
		dev_dbg(&priv->pcid->dev,
			"RSSI[%d] NewGain[%d] OldGain[%d] Count[%d]\n",
			(int)dbm, priv->byBBVGANew,
			priv->byBBVGACurrent,
			(int)priv->uBBVGADiffCount);

		BBvSetVGAGainOffset(priv, priv->byBBVGANew);
	}
}

static  irqreturn_t  device_intr(int irq,  void *dev_instance)
{
	struct vnt_private *pDevice = dev_instance;
	int             max_count = 0;
	unsigned long dwMIBCounter = 0;
	unsigned char byOrgPageSel = 0;
	int             handled = 0;
	unsigned long flags;

	MACvReadISR(pDevice->PortOffset, &pDevice->dwIsr);

	if (pDevice->dwIsr == 0)
		return IRQ_RETVAL(handled);

	if (pDevice->dwIsr == 0xffffffff) {
		pr_debug("dwIsr = 0xffff\n");
		return IRQ_RETVAL(handled);
	}

	handled = 1;
	MACvIntDisable(pDevice->PortOffset);

	spin_lock_irqsave(&pDevice->lock, flags);

	//Make sure current page is 0
	VNSvInPortB(pDevice->PortOffset + MAC_REG_PAGE1SEL, &byOrgPageSel);
	if (byOrgPageSel == 1)
		MACvSelectPage0(pDevice->PortOffset);
	else
		byOrgPageSel = 0;

	MACvReadMIBCounter(pDevice->PortOffset, &dwMIBCounter);
	// TBD....
	// Must do this after doing rx/tx, cause ISR bit is slow
	// than RD/TD write back
	// update ISR counter
	STAvUpdate802_11Counter(&pDevice->s802_11Counter, &pDevice->scStatistic, dwMIBCounter);
	while (pDevice->dwIsr != 0) {
		STAvUpdateIsrStatCounter(&pDevice->scStatistic, pDevice->dwIsr);
		MACvWriteISR(pDevice->PortOffset, pDevice->dwIsr);

		if (pDevice->dwIsr & ISR_FETALERR) {
			pr_debug(" ISR_FETALERR\n");
			VNSvOutPortB(pDevice->PortOffset + MAC_REG_SOFTPWRCTL, 0);
			VNSvOutPortW(pDevice->PortOffset + MAC_REG_SOFTPWRCTL, SOFTPWRCTL_SWPECTI);
			device_error(pDevice, pDevice->dwIsr);
		}

		if (pDevice->dwIsr & ISR_TBTT) {
			if (pDevice->vif &&
			    pDevice->op_mode != NL80211_IFTYPE_ADHOC)
				vnt_check_bb_vga(pDevice);

			pDevice->bBeaconSent = false;
			if (pDevice->bEnablePSMode)
				PSbIsNextTBTTWakeUp((void *)pDevice);

			if ((pDevice->op_mode == NL80211_IFTYPE_AP ||
			    pDevice->op_mode == NL80211_IFTYPE_ADHOC) &&
			    pDevice->vif->bss_conf.enable_beacon) {
				MACvOneShotTimer1MicroSec(pDevice->PortOffset,
							  (pDevice->vif->bss_conf.beacon_int - MAKE_BEACON_RESERVED) << 10);
			}

			/* TODO: adhoc PS mode */

		}

		if (pDevice->dwIsr & ISR_BNTX) {
			if (pDevice->op_mode == NL80211_IFTYPE_ADHOC) {
				pDevice->bIsBeaconBufReadySet = false;
				pDevice->cbBeaconBufReadySetCnt = 0;
			}

			pDevice->bBeaconSent = true;
		}

		if (pDevice->dwIsr & ISR_RXDMA0)
			max_count += device_rx_srv(pDevice, TYPE_RXDMA0);

		if (pDevice->dwIsr & ISR_RXDMA1)
			max_count += device_rx_srv(pDevice, TYPE_RXDMA1);

		if (pDevice->dwIsr & ISR_TXDMA0)
			max_count += device_tx_srv(pDevice, TYPE_TXDMA0);

		if (pDevice->dwIsr & ISR_AC0DMA)
			max_count += device_tx_srv(pDevice, TYPE_AC0DMA);

		if (pDevice->dwIsr & ISR_SOFTTIMER1) {
			if (pDevice->vif) {
				if (pDevice->vif->bss_conf.enable_beacon)
					vnt_beacon_make(pDevice, pDevice->vif);
			}
		}

		/* If both buffers available wake the queue */
		if (pDevice->vif) {
			if (AVAIL_TD(pDevice, TYPE_TXDMA0) &&
			    AVAIL_TD(pDevice, TYPE_AC0DMA) &&
			    ieee80211_queue_stopped(pDevice->hw, 0))
				ieee80211_wake_queues(pDevice->hw);
		}

		MACvReadISR(pDevice->PortOffset, &pDevice->dwIsr);

		MACvReceive0(pDevice->PortOffset);
		MACvReceive1(pDevice->PortOffset);

		if (max_count > pDevice->sOpts.int_works)
			break;
	}

	if (byOrgPageSel == 1)
		MACvSelectPage1(pDevice->PortOffset);

	spin_unlock_irqrestore(&pDevice->lock, flags);

	MACvIntEnable(pDevice->PortOffset, IMR_MASK_VALUE);

	return IRQ_RETVAL(handled);
}

static int vnt_tx_packet(struct vnt_private *priv, struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	PSTxDesc head_td;
	u32 dma_idx = TYPE_AC0DMA;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);

	if (!ieee80211_is_data(hdr->frame_control))
		dma_idx = TYPE_TXDMA0;

	if (AVAIL_TD(priv, dma_idx) < 1) {
		spin_unlock_irqrestore(&priv->lock, flags);
		return -ENOMEM;
	}

	head_td = priv->apCurrTD[dma_idx];

	head_td->m_td1TD1.byTCR = 0;

	head_td->pTDInfo->skb = skb;

	priv->iTDUsed[dma_idx]++;

	/* Take ownership */
	wmb();
	head_td->m_td0TD0.f1Owner = OWNED_BY_NIC;

	/* get Next */
	wmb();
	priv->apCurrTD[dma_idx] = head_td->next;

	spin_unlock_irqrestore(&priv->lock, flags);

	vnt_generate_fifo_header(priv, dma_idx, head_td, skb);

	if (MACbIsRegBitsOn(priv->PortOffset, MAC_REG_PSCTL, PSCTL_PS))
		MACbPSWakeup(priv->PortOffset);

	spin_lock_irqsave(&priv->lock, flags);

	priv->bPWBitOn = false;

	/* Set TSR1 & ReqCount in TxDescHead */
	head_td->m_td1TD1.byTCR |= (TCR_STP | TCR_EDP | EDMSDU);
	head_td->m_td1TD1.wReqCount =
			cpu_to_le16((u16)head_td->pTDInfo->dwReqCount);

	head_td->buff_addr = cpu_to_le32(head_td->pTDInfo->skb_dma);

	if (dma_idx == TYPE_AC0DMA) {
		head_td->pTDInfo->byFlags = TD_FLAGS_NETIF_SKB;

		MACvTransmitAC0(priv->PortOffset);
	} else {
		MACvTransmit0(priv->PortOffset);
	}

	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static void vnt_tx_80211(struct ieee80211_hw *hw,
			 struct ieee80211_tx_control *control,
			 struct sk_buff *skb)
{
	struct vnt_private *priv = hw->priv;

	ieee80211_stop_queues(hw);

	if (vnt_tx_packet(priv, skb)) {
		ieee80211_free_txskb(hw, skb);

		ieee80211_wake_queues(hw);
	}
}

static int vnt_start(struct ieee80211_hw *hw)
{
	struct vnt_private *priv = hw->priv;
	int ret;

	priv->rx_buf_sz = PKT_BUF_SZ;
	if (!device_init_rings(priv))
		return -ENOMEM;

	ret = request_irq(priv->pcid->irq, &device_intr,
			  IRQF_SHARED, "vt6655", priv);
	if (ret) {
		dev_dbg(&priv->pcid->dev, "failed to start irq\n");
		return ret;
	}

	dev_dbg(&priv->pcid->dev, "call device init rd0 ring\n");
	device_init_rd0_ring(priv);
	device_init_rd1_ring(priv);
	device_init_td0_ring(priv);
	device_init_td1_ring(priv);

	device_init_registers(priv);

	dev_dbg(&priv->pcid->dev, "call MACvIntEnable\n");
	MACvIntEnable(priv->PortOffset, IMR_MASK_VALUE);

	ieee80211_wake_queues(hw);

	return 0;
}

static void vnt_stop(struct ieee80211_hw *hw)
{
	struct vnt_private *priv = hw->priv;

	ieee80211_stop_queues(hw);

	MACbShutdown(priv->PortOffset);
	MACbSoftwareReset(priv->PortOffset);
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
		MACvRegBitsOff(priv->PortOffset, MAC_REG_RCR, RCR_UNICAST);

		MACvRegBitsOn(priv->PortOffset, MAC_REG_HOSTCR, HOSTCR_ADHOC);

		break;
	case NL80211_IFTYPE_AP:
		MACvRegBitsOff(priv->PortOffset, MAC_REG_RCR, RCR_UNICAST);

		MACvRegBitsOn(priv->PortOffset, MAC_REG_HOSTCR, HOSTCR_AP);

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
		MACvRegBitsOff(priv->PortOffset, MAC_REG_TCR, TCR_AUTOBCNTX);
		MACvRegBitsOff(priv->PortOffset,
			       MAC_REG_TFTCTL, TFTCTL_TSFCNTREN);
		MACvRegBitsOff(priv->PortOffset, MAC_REG_HOSTCR, HOSTCR_ADHOC);
		break;
	case NL80211_IFTYPE_AP:
		MACvRegBitsOff(priv->PortOffset, MAC_REG_TCR, TCR_AUTOBCNTX);
		MACvRegBitsOff(priv->PortOffset,
			       MAC_REG_TFTCTL, TFTCTL_TSFCNTREN);
		MACvRegBitsOff(priv->PortOffset, MAC_REG_HOSTCR, HOSTCR_AP);
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

		if (conf->chandef.chan->band == IEEE80211_BAND_5GHZ)
			bb_type = BB_TYPE_11A;
		else
			bb_type = BB_TYPE_11G;

		if (priv->byBBType != bb_type) {
			priv->byBBType = bb_type;

			CARDbSetPhyParameter(priv, priv->byBBType);
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
		struct ieee80211_vif *vif, struct ieee80211_bss_conf *conf,
		u32 changed)
{
	struct vnt_private *priv = hw->priv;

	priv->current_aid = conf->aid;

	if (changed & BSS_CHANGED_BSSID)
		MACvWriteBSSIDAddress(priv->PortOffset, (u8 *)conf->bssid);

	if (changed & BSS_CHANGED_BASIC_RATES) {
		priv->basic_rates = conf->basic_rates;

		CARDvUpdateBasicTopRate(priv);

		dev_dbg(&priv->pcid->dev,
			"basic rates %x\n", conf->basic_rates);
	}

	if (changed & BSS_CHANGED_ERP_PREAMBLE) {
		if (conf->use_short_preamble) {
			MACvEnableBarkerPreambleMd(priv->PortOffset);
			priv->byPreambleType = true;
		} else {
			MACvDisableBarkerPreambleMd(priv->PortOffset);
			priv->byPreambleType = false;
		}
	}

	if (changed & BSS_CHANGED_ERP_CTS_PROT) {
		if (conf->use_cts_prot)
			MACvEnableProtectMD(priv->PortOffset);
		else
			MACvDisableProtectMD(priv->PortOffset);
	}

	if (changed & BSS_CHANGED_ERP_SLOT) {
		if (conf->use_short_slot)
			priv->bShortSlotTime = true;
		else
			priv->bShortSlotTime = false;

		CARDbSetPhyParameter(priv, priv->byBBType);
		BBvSetVGAGainOffset(priv, priv->abyBBVGA[0]);
	}

	if (changed & BSS_CHANGED_TXPOWER)
		RFbSetPower(priv, priv->wCurrentRate,
			    conf->chandef.chan->hw_value);

	if (changed & BSS_CHANGED_BEACON_ENABLED) {
		dev_dbg(&priv->pcid->dev,
			"Beacon enable %d\n", conf->enable_beacon);

		if (conf->enable_beacon) {
			vnt_beacon_enable(priv, vif, conf);

			MACvRegBitsOn(priv->PortOffset, MAC_REG_TCR,
				      TCR_AUTOBCNTX);
		} else {
			MACvRegBitsOff(priv->PortOffset, MAC_REG_TCR,
				       TCR_AUTOBCNTX);
		}
	}

	if (changed & BSS_CHANGED_ASSOC && priv->op_mode != NL80211_IFTYPE_AP) {
		if (conf->assoc) {
			CARDbUpdateTSF(priv, conf->beacon_rate->hw_value,
				       conf->sync_device_ts, conf->sync_tsf);

			CARDbSetBeaconPeriod(priv, conf->beacon_int);

			CARDvSetFirstNextTBTT(priv, conf->beacon_int);
		} else {
			VNSvOutPortB(priv->PortOffset + MAC_REG_TFTCTL,
				     TFTCTL_TSFCNTRST);
			VNSvOutPortB(priv->PortOffset + MAC_REG_TFTCTL,
				     TFTCTL_TSFCNTREN);
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
	unsigned int changed_flags, unsigned int *total_flags, u64 multicast)
{
	struct vnt_private *priv = hw->priv;
	u8 rx_mode = 0;

	*total_flags &= FIF_ALLMULTI | FIF_OTHER_BSS | FIF_PROMISC_IN_BSS |
		FIF_BCN_PRBRESP_PROMISC;

	VNSvInPortB(priv->PortOffset + MAC_REG_RCR, &rx_mode);

	dev_dbg(&priv->pcid->dev, "rx mode in = %x\n", rx_mode);

	if (changed_flags & FIF_PROMISC_IN_BSS) {
		/* unconditionally log net taps */
		if (*total_flags & FIF_PROMISC_IN_BSS)
			rx_mode |= RCR_UNICAST;
		else
			rx_mode &= ~RCR_UNICAST;
	}

	if (changed_flags & FIF_ALLMULTI) {
		if (*total_flags & FIF_ALLMULTI) {
			unsigned long flags;

			spin_lock_irqsave(&priv->lock, flags);

			if (priv->mc_list_count > 2) {
				MACvSelectPage1(priv->PortOffset);

				VNSvOutPortD(priv->PortOffset +
					     MAC_REG_MAR0, 0xffffffff);
				VNSvOutPortD(priv->PortOffset +
					    MAC_REG_MAR0 + 4, 0xffffffff);

				MACvSelectPage0(priv->PortOffset);
			} else {
				MACvSelectPage1(priv->PortOffset);

				VNSvOutPortD(priv->PortOffset +
					     MAC_REG_MAR0, (u32)multicast);
				VNSvOutPortD(priv->PortOffset +
					     MAC_REG_MAR0 + 4,
					     (u32)(multicast >> 32));

				MACvSelectPage0(priv->PortOffset);
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

	VNSvOutPortB(priv->PortOffset + MAC_REG_RCR, rx_mode);

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
	default:
		break;
	}

	return 0;
}

static u64 vnt_get_tsf(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct vnt_private *priv = hw->priv;
	u64 tsf;

	CARDbGetCurrentTSF(priv, &tsf);

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
	VNSvOutPortB(priv->PortOffset + MAC_REG_TFTCTL, TFTCTL_TSFCNTRST);
}

static const struct ieee80211_ops vnt_mac_ops = {
	.tx			= vnt_tx_80211,
	.start			= vnt_start,
	.stop			= vnt_stop,
	.add_interface		= vnt_add_interface,
	.remove_interface	= vnt_remove_interface,
	.config			= vnt_config,
	.bss_info_changed	= vnt_bss_info_changed,
	.prepare_multicast	= vnt_prepare_multicast,
	.configure_filter	= vnt_configure,
	.set_key		= vnt_set_key,
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
	PCHIP_INFO  pChip_info = (PCHIP_INFO)ent->driver_data;
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

	vt6655_init_info(pcid, &priv, pChip_info);

	priv->hw = hw;

	SET_IEEE80211_DEV(priv->hw, &pcid->dev);

	if (pci_enable_device(pcid)) {
		device_free_info(priv);
		return -ENODEV;
	}

	dev_dbg(&pcid->dev,
		"Before get pci_info memaddr is %x\n", priv->memaddr);

	if (!device_get_pci_info(priv, pcid)) {
		dev_err(&pcid->dev, ": Failed to find PCI device.\n");
		device_free_info(priv);
		return -ENODEV;
	}

#ifdef	DEBUG
	dev_dbg(&pcid->dev,
		"after get pci_info memaddr is %x, io addr is %x,io_size is %d\n",
		priv->memaddr, priv->ioaddr, priv->io_size);
	{
		int i;
		u32 bar, len;
		u32 address[] = {
			PCI_BASE_ADDRESS_0,
			PCI_BASE_ADDRESS_1,
			PCI_BASE_ADDRESS_2,
			PCI_BASE_ADDRESS_3,
			PCI_BASE_ADDRESS_4,
			PCI_BASE_ADDRESS_5,
			0};
		for (i = 0; address[i]; i++) {
			pci_read_config_dword(pcid, address[i], &bar);

			dev_dbg(&pcid->dev, "bar %d is %x\n", i, bar);

			if (!bar) {
				dev_dbg(&pcid->dev,
					"bar %d not implemented\n", i);
				continue;
			}

			if (bar & PCI_BASE_ADDRESS_SPACE_IO) {
				/* This is IO */

				len = bar & (PCI_BASE_ADDRESS_IO_MASK & 0xffff);
				len = len & ~(len - 1);

				dev_dbg(&pcid->dev,
					"IO space:  len in IO %x, BAR %d\n",
					len, i);
			} else {
				len = bar & 0xfffffff0;
				len = ~len + 1;

				dev_dbg(&pcid->dev,
					"len in MEM %x, BAR %d\n", len, i);
			}
		}
	}
#endif

	priv->PortOffset = ioremap(priv->memaddr & PCI_BASE_ADDRESS_MEM_MASK,
				   priv->io_size);
	if (!priv->PortOffset) {
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

	/* do reset */
	if (!MACbSoftwareReset(priv->PortOffset)) {
		dev_err(&pcid->dev, ": Failed to access MAC hardware..\n");
		device_free_info(priv);
		return -ENODEV;
	}
	/* initial to reload eeprom */
	MACvInitialize(priv->PortOffset);
	MACvReadEtherAddress(priv->PortOffset, priv->abyCurrentNetAddr);

	device_get_options(priv);
	device_set_options(priv);
	/* Mask out the options cannot be set to the chip */
	priv->sOpts.flags &= pChip_info->flags;

	/* Enable the chip specified capabilities */
	priv->flags = priv->sOpts.flags | (pChip_info->flags & 0xff000000UL);

	wiphy = priv->hw->wiphy;

	wiphy->frag_threshold = FRAG_THRESH_DEF;
	wiphy->rts_threshold = RTS_THRESH_DEF;
	wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) |
		BIT(NL80211_IFTYPE_ADHOC) | BIT(NL80211_IFTYPE_AP);

	priv->hw->flags = IEEE80211_HW_RX_INCLUDES_FCS |
		IEEE80211_HW_REPORTS_TX_ACK_STATUS |
		IEEE80211_HW_SIGNAL_DBM |
		IEEE80211_HW_TIMING_BEACON_ONLY;

	priv->hw->max_signal = 100;

	if (vnt_init(priv))
		return -ENODEV;

	device_print_info(priv);
	pci_set_drvdata(pcid, priv);

	return 0;
}

/*------------------------------------------------------------------*/

#ifdef CONFIG_PM
static int vt6655_suspend(struct pci_dev *pcid, pm_message_t state)
{
	struct vnt_private *priv = pci_get_drvdata(pcid);
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);

	pci_save_state(pcid);

	MACbShutdown(priv->PortOffset);

	pci_disable_device(pcid);
	pci_set_power_state(pcid, pci_choose_state(pcid, state));

	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static int vt6655_resume(struct pci_dev *pcid)
{

	pci_set_power_state(pcid, PCI_D0);
	pci_enable_wake(pcid, PCI_D0, 0);
	pci_restore_state(pcid);

	return 0;
}
#endif

MODULE_DEVICE_TABLE(pci, vt6655_pci_id_table);

static struct pci_driver device_driver = {
	.name = DEVICE_NAME,
	.id_table = vt6655_pci_id_table,
	.probe = vt6655_probe,
	.remove = vt6655_remove,
#ifdef CONFIG_PM
	.suspend = vt6655_suspend,
	.resume = vt6655_resume,
#endif
};

static int __init vt6655_init_module(void)
{
	int ret;

	ret = pci_register_driver(&device_driver);
#ifdef CONFIG_PM
	if (ret >= 0)
		register_reboot_notifier(&device_notifier);
#endif

	return ret;
}

static void __exit vt6655_cleanup_module(void)
{
#ifdef CONFIG_PM
	unregister_reboot_notifier(&device_notifier);
#endif
	pci_unregister_driver(&device_driver);
}

module_init(vt6655_init_module);
module_exit(vt6655_cleanup_module);

#ifdef CONFIG_PM
static int
device_notify_reboot(struct notifier_block *nb, unsigned long event, void *p)
{
	struct pci_dev *pdev = NULL;

	switch (event) {
	case SYS_DOWN:
	case SYS_HALT:
	case SYS_POWER_OFF:
		for_each_pci_dev(pdev) {
			if (pci_dev_driver(pdev) == &device_driver) {
				if (pci_get_drvdata(pdev))
					vt6655_suspend(pdev, PMSG_HIBERNATE);
			}
		}
	}
	return NOTIFY_DONE;
}
#endif
