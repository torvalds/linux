/******************************************************************************
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
******************************************************************************/

#include "r8192U.h"
#include "r8192S_firmware.h"
#include <linux/unistd.h>

#include "r8192S_hw.h"
#include "r8192SU_HWImg.h"

#include <linux/firmware.h>

#define   byte(x,n)  ( (x >> (8 * n)) & 0xff  )

//
// Description:   This routine will intialize firmware. If any error occurs during the initialization
//				process, the routine shall terminate immediately and return fail.
//
// Arguments:   The pointer of the adapter
//			   Code address (Virtual address, should fill descriptor with physical address)
//			   Code size
// Created by Roger, 2008.04.10.
//
bool FirmwareDownloadCode(struct net_device *dev,
				u8 *code_virtual_address,
				u32 buffer_len)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	bool rt_status = true;
	/* Fragmentation might be required in 90/92 but not in 92S */
	u16 frag_threshold = MAX_FIRMWARE_CODE_SIZE;
	u16 frag_length, frag_offset = 0;
	struct sk_buff *skb;
	unsigned char *seg_ptr;
	cb_desc *tcb_desc;
	u8 bLastIniPkt = 0;
	u16 ExtraDescOffset = 0;

	if (buffer_len >= MAX_FIRMWARE_CODE_SIZE - USB_HWDESC_HEADER_LEN) {
		RT_TRACE(COMP_ERR, "(%s): Firmware exceeds"
					" MAX_FIRMWARE_CODE_SIZE\n", __func__);
		goto cmdsend_downloadcode_fail;
	}
	ExtraDescOffset = USB_HWDESC_HEADER_LEN;
	do {
		if((buffer_len-frag_offset) > frag_threshold)
			frag_length = frag_threshold + ExtraDescOffset;
		else {
			frag_length = (u16)(buffer_len -
						frag_offset + ExtraDescOffset);
			bLastIniPkt = 1;
		}
		/*
		 * Allocate skb buffer to contain firmware info
		 * and tx descriptor info.
		 */
		skb  = dev_alloc_skb(frag_length);
		if (skb == NULL) {
			RT_TRACE(COMP_ERR, "(%s): unable to alloc skb buffer\n",
								__func__);
			goto cmdsend_downloadcode_fail;
		}
		memcpy((unsigned char *)(skb->cb), &dev, sizeof(dev));

		tcb_desc = (cb_desc*)(skb->cb + MAX_DEV_ADDR_SIZE);
		tcb_desc->queue_index = TXCMD_QUEUE;
		tcb_desc->bCmdOrInit = DESC_PACKET_TYPE_INIT;
		tcb_desc->bLastIniPkt = bLastIniPkt;

		skb_reserve(skb, ExtraDescOffset);

		seg_ptr = (u8 *)skb_put(skb,
					(u32)(frag_length - ExtraDescOffset));

		memcpy(seg_ptr, code_virtual_address + frag_offset,
					(u32)(frag_length-ExtraDescOffset));

		tcb_desc->txbuf_size = frag_length;

		if (!priv->ieee80211->check_nic_enough_desc(dev, tcb_desc->queue_index) ||
			(!skb_queue_empty(&priv->ieee80211->skb_waitQ[tcb_desc->queue_index])) ||
			(priv->ieee80211->queue_stop)) {
			RT_TRACE(COMP_FIRMWARE,"=====================================================> tx full!\n");
			skb_queue_tail(&priv->ieee80211->skb_waitQ[tcb_desc->queue_index], skb);
		} else
			priv->ieee80211->softmac_hard_start_xmit(skb, dev);

		frag_offset += (frag_length - ExtraDescOffset);

	} while (frag_offset < buffer_len);
	return rt_status ;

cmdsend_downloadcode_fail:
	rt_status = false;
	RT_TRACE(COMP_ERR, "(%s): failed\n", __func__);
	return rt_status;
}


bool FirmwareEnableCPU(struct net_device *dev)
{
	bool rtStatus = true;
	u8 tmpU1b, CPUStatus = 0;
	u16 tmpU2b;
	u32 iCheckTime = 200;

	/* Enable CPU. */
	tmpU1b = read_nic_byte(dev, SYS_CLKR);
	/* AFE source */
	write_nic_byte(dev,  SYS_CLKR, (tmpU1b|SYS_CPU_CLKSEL));
	tmpU2b = read_nic_word(dev, SYS_FUNC_EN);
	write_nic_word(dev, SYS_FUNC_EN, (tmpU2b|FEN_CPUEN));
	/* Poll IMEM Ready after CPU has refilled. */
	do {
		CPUStatus = read_nic_byte(dev, TCR);
		if (CPUStatus & IMEM_RDY)
			/* success */
			break;
		udelay(100);
	} while (iCheckTime--);
	if (!(CPUStatus & IMEM_RDY)) {
		RT_TRACE(COMP_ERR, "%s(): failed to enable CPU", __func__);
		rtStatus = false;
	}
	return rtStatus;
}

FIRMWARE_8192S_STATUS
FirmwareGetNextStatus(FIRMWARE_8192S_STATUS FWCurrentStatus)
{
	FIRMWARE_8192S_STATUS	NextFWStatus = 0;

	switch(FWCurrentStatus)
	{
		case FW_STATUS_INIT:
			NextFWStatus = FW_STATUS_LOAD_IMEM;
			break;

		case FW_STATUS_LOAD_IMEM:
			NextFWStatus = FW_STATUS_LOAD_EMEM;
			break;

		case FW_STATUS_LOAD_EMEM:
			NextFWStatus = FW_STATUS_LOAD_DMEM;
			break;

		case FW_STATUS_LOAD_DMEM:
			NextFWStatus = FW_STATUS_READY;
			break;

		default:
			RT_TRACE(COMP_ERR,"Invalid FW Status(%#x)!!\n", FWCurrentStatus);
			break;
	}
	return	NextFWStatus;
}

bool FirmwareCheckReady(struct net_device *dev, u8 LoadFWStatus)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	bool rtStatus = true;
	rt_firmware *pFirmware = priv->pFirmware;
	int PollingCnt = 1000;
	u8 CPUStatus = 0;
	u32 tmpU4b;

	pFirmware->FWStatus = (FIRMWARE_8192S_STATUS)LoadFWStatus;
	switch (LoadFWStatus) {
	case FW_STATUS_LOAD_IMEM:
		do { /* Polling IMEM code done. */
			CPUStatus = read_nic_byte(dev, TCR);
			if(CPUStatus& IMEM_CODE_DONE)
				break;
			udelay(5);
		} while (PollingCnt--);
		if (!(CPUStatus & IMEM_CHK_RPT) || PollingCnt <= 0) {
			RT_TRACE(COMP_ERR, "FW_STATUS_LOAD_IMEM FAIL CPU, Status=%x\r\n", CPUStatus);
			goto FirmwareCheckReadyFail;
		}
		break;
	case FW_STATUS_LOAD_EMEM: /* Check Put Code OK and Turn On CPU */
		do { /* Polling EMEM code done. */
			CPUStatus = read_nic_byte(dev, TCR);
			if(CPUStatus& EMEM_CODE_DONE)
				break;
			udelay(5);
		} while (PollingCnt--);
		if (!(CPUStatus & EMEM_CHK_RPT)) {
			RT_TRACE(COMP_ERR, "FW_STATUS_LOAD_EMEM FAIL CPU, Status=%x\r\n", CPUStatus);
			goto FirmwareCheckReadyFail;
		}
		/* Turn On CPU */
		if (FirmwareEnableCPU(dev) != true) {
			RT_TRACE(COMP_ERR, "%s(): failed to enable CPU",
								__func__);
			goto FirmwareCheckReadyFail;
		}
		break;
	case FW_STATUS_LOAD_DMEM:
		do { /* Polling DMEM code done */
			CPUStatus = read_nic_byte(dev, TCR);
			if(CPUStatus& DMEM_CODE_DONE)
				break;

			udelay(5);
		} while (PollingCnt--);

		if (!(CPUStatus & DMEM_CODE_DONE)) {
			RT_TRACE(COMP_ERR, "Polling  DMEM code done fail ! CPUStatus(%#x)\n", CPUStatus);
			goto FirmwareCheckReadyFail;
		}

		RT_TRACE(COMP_FIRMWARE, "%s(): DMEM code download success, "
					"CPUStatus(%#x)",
					__func__, CPUStatus);

		PollingCnt = 10000; /* Set polling cycle to 10ms. */

		do { /* Polling Load Firmware ready */
			CPUStatus = read_nic_byte(dev, TCR);
			if(CPUStatus & FWRDY)
				break;
			udelay(100);
		} while (PollingCnt--);

		RT_TRACE(COMP_FIRMWARE, "%s(): polling load firmware ready, "
					"CPUStatus(%x)",
					__func__, CPUStatus);

		if ((CPUStatus & LOAD_FW_READY) != LOAD_FW_READY) {
			RT_TRACE(COMP_ERR, "Polling Load Firmware ready failed "
						"CPUStatus(%x)\n", CPUStatus);
			goto FirmwareCheckReadyFail;
		}
		/*
		 * USB interface will update
		 * reserved followings parameters later
		 */

	       //
              // <Roger_Notes> If right here, we can set TCR/RCR to desired value
              // and config MAC lookback mode to normal mode. 2008.08.28.
              //
              tmpU4b = read_nic_dword(dev,TCR);
		write_nic_dword(dev, TCR, (tmpU4b&(~TCR_ICV)));

		tmpU4b = read_nic_dword(dev, RCR);
		write_nic_dword(dev, RCR,
			(tmpU4b|RCR_APPFCS|RCR_APP_ICV|RCR_APP_MIC));

		RT_TRACE(COMP_FIRMWARE, "%s(): Current RCR settings(%#x)",
							__func__, tmpU4b);
		// Set to normal mode.
		write_nic_byte(dev, LBKMD_SEL, LBK_NORMAL);
		break;
	default:
		break;
	}
	RT_TRACE(COMP_FIRMWARE, "%s(): LoadFWStatus(%d), success",
							__func__, LoadFWStatus);
	return rtStatus;

FirmwareCheckReadyFail:
	rtStatus = false;
	RT_TRACE(COMP_FIRMWARE, "%s(): LoadFWStatus(%d), failed",
							__func__, LoadFWStatus);
	return rtStatus;
}

//
// Description:   This routine is to update the RF types in FW header partially.
//
// Created by Roger, 2008.12.24.
//
u8 FirmwareHeaderMapRfType(struct net_device *dev)
{
	struct r8192_priv 	*priv = ieee80211_priv(dev);
	switch(priv->rf_type)
	{
		case RF_1T1R: 	return 0x11;
		case RF_1T2R: 	return 0x12;
		case RF_2T2R: 	return 0x22;
		case RF_2T2R_GREEN: 	return 0x92;
		default:
			RT_TRACE(COMP_INIT, "Unknown RF type(%x)\n",priv->rf_type);
			break;
	}
	return 0x22;
}


//
// Description:   This routine is to update the private parts in FW header partially.
//
// Created by Roger, 2008.12.18.
//
void FirmwareHeaderPriveUpdate(struct net_device *dev, PRT_8192S_FIRMWARE_PRIV 	pFwPriv)
{
	struct r8192_priv 	*priv = ieee80211_priv(dev);
	// Update USB endpoint number for RQPN settings.
	pFwPriv->usb_ep_num = priv->EEPROMUsbEndPointNumber; // endpoint number: 4, 6 and 11.
	RT_TRACE(COMP_INIT, "FirmwarePriveUpdate(): usb_ep_num(%#x)\n", pFwPriv->usb_ep_num);

	// Update RF types for RATR settings.
	pFwPriv->rf_config = FirmwareHeaderMapRfType(dev);
}

bool FirmwareRequest92S(struct net_device *dev, rt_firmware *pFirmware)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	bool rtStatus = true;
	const char *pFwImageFileName[1] = {"RTL8192SU/rtl8192sfw.bin"};
	u8 *pucMappedFile = NULL;
	u32 ulInitStep = 0;
	u8 FwHdrSize = RT_8192S_FIRMWARE_HDR_SIZE;
	PRT_8192S_FIRMWARE_HDR pFwHdr = NULL;
	u32 file_length = 0;
	int rc;
	const struct firmware *fw_entry;

	rc = request_firmware(&fw_entry,
				pFwImageFileName[ulInitStep],
				&priv->udev->dev);
	if (rc < 0)
		goto RequestFirmware_Fail;

	if (fw_entry->size > sizeof(pFirmware->szFwTmpBuffer)) {
		RT_TRACE(COMP_ERR, "%s(): image file too large"
					"for container buffer", __func__);
		release_firmware(fw_entry);
		goto RequestFirmware_Fail;
	}

	memcpy(pFirmware->szFwTmpBuffer, fw_entry->data, fw_entry->size);
	pFirmware->szFwTmpBufferLen = fw_entry->size;
	release_firmware(fw_entry);

	pucMappedFile = pFirmware->szFwTmpBuffer;
	file_length = pFirmware->szFwTmpBufferLen;

	/* Retrieve FW header. */
	pFirmware->pFwHeader = (PRT_8192S_FIRMWARE_HDR) pucMappedFile;
	pFwHdr = pFirmware->pFwHeader;

	RT_TRACE(COMP_FIRMWARE, "%s(): signature: %x, version: %x, "
				"size: %x, imemsize: %x, sram size: %x",
				__func__, pFwHdr->Signature, pFwHdr->Version,
				pFwHdr->DMEMSize, pFwHdr->IMG_IMEM_SIZE,
				pFwHdr->IMG_SRAM_SIZE);

	pFirmware->FirmwareVersion =  byte(pFwHdr->Version , 0);

	if ((pFwHdr->IMG_IMEM_SIZE == 0) ||
			(pFwHdr->IMG_IMEM_SIZE > sizeof(pFirmware->FwIMEM))) {
		RT_TRACE(COMP_ERR, "%s(): memory for data image is less than"
						" IMEM requires", __func__);
		goto RequestFirmware_Fail;
	} else {
		pucMappedFile += FwHdrSize;
		/* Retrieve IMEM image. */
		memcpy(pFirmware->FwIMEM, pucMappedFile, pFwHdr->IMG_IMEM_SIZE);
		pFirmware->FwIMEMLen = pFwHdr->IMG_IMEM_SIZE;
	}

	if (pFwHdr->IMG_SRAM_SIZE > sizeof(pFirmware->FwEMEM)) {
		RT_TRACE(COMP_ERR, "%s(): memory for data image is less than"
						" EMEM requires", __func__);
		goto RequestFirmware_Fail;
	} else {
		pucMappedFile += pFirmware->FwIMEMLen;
		/* Retriecve EMEM image */
		memcpy(pFirmware->FwEMEM, pucMappedFile, pFwHdr->IMG_SRAM_SIZE);
		pFirmware->FwEMEMLen = pFwHdr->IMG_SRAM_SIZE;
	}
	return rtStatus;

RequestFirmware_Fail:
	RT_TRACE(COMP_ERR, "%s(): failed with TCR-Status: %x\n",
					__func__, read_nic_word(dev, TCR));
	rtStatus = false;
	return rtStatus;
}

bool FirmwareDownload92S(struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	bool rtStatus = true;
	u8 *pucMappedFile = NULL;
	u32 ulFileLength;
	u8 FwHdrSize = RT_8192S_FIRMWARE_HDR_SIZE;
	rt_firmware *pFirmware = priv->pFirmware;
	u8 FwStatus = FW_STATUS_INIT;
	PRT_8192S_FIRMWARE_HDR pFwHdr = NULL;
	PRT_8192S_FIRMWARE_PRIV pFwPriv = NULL;

	pFirmware->FWStatus = FW_STATUS_INIT;
	/*
	 * Load the firmware from RTL8192SU/rtl8192sfw.bin if necessary
	 */
	if (pFirmware->szFwTmpBufferLen == 0) {
		if (FirmwareRequest92S(dev, pFirmware) != true)
			goto DownloadFirmware_Fail;
	}
	FwStatus = FirmwareGetNextStatus(pFirmware->FWStatus);
	while (FwStatus != FW_STATUS_READY) {
		/* Image buffer redirection. */
		switch (FwStatus) {
		case FW_STATUS_LOAD_IMEM:
			pucMappedFile = pFirmware->FwIMEM;
			ulFileLength = pFirmware->FwIMEMLen;
			break;

		case FW_STATUS_LOAD_EMEM:
			pucMappedFile = pFirmware->FwEMEM;
			ulFileLength = pFirmware->FwEMEMLen;
			break;

		case FW_STATUS_LOAD_DMEM:
			/* Partial update the content of private header */
			pFwHdr = pFirmware->pFwHeader;
			pFwPriv = (PRT_8192S_FIRMWARE_PRIV)&pFwHdr->FWPriv;
			FirmwareHeaderPriveUpdate(dev, pFwPriv);
			pucMappedFile = (u8 *)(pFirmware->pFwHeader) +
					RT_8192S_FIRMWARE_HDR_EXCLUDE_PRI_SIZE;

			ulFileLength = FwHdrSize -
					RT_8192S_FIRMWARE_HDR_EXCLUDE_PRI_SIZE;
			break;

		default:
			RT_TRACE(COMP_ERR, "Unexpected Download step!!\n");
			goto DownloadFirmware_Fail;
			break;
		}

		/* <2> Download image file */

		rtStatus = FirmwareDownloadCode(dev,
						pucMappedFile,
						ulFileLength);

		if(rtStatus != true)
			goto DownloadFirmware_Fail;

		/* <3> Check whether load FW process is ready */

		rtStatus = FirmwareCheckReady(dev, FwStatus);

		if(rtStatus != true)
			goto DownloadFirmware_Fail;

		FwStatus = FirmwareGetNextStatus(pFirmware->FWStatus);
	}

	RT_TRACE(COMP_FIRMWARE, "%s(): Firmware Download Success", __func__);
	return rtStatus;

DownloadFirmware_Fail:
	RT_TRACE(COMP_ERR, "%s(): failed with TCR-Status: %x\n",
					__func__, read_nic_word(dev, TCR));
	rtStatus = false;
	return rtStatus;
}

MODULE_FIRMWARE("RTL8192SU/rtl8192sfw.bin");
