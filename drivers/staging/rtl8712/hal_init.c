// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 * hal_init.c
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
 * Linux device driver for RTL8192SU
 *
 * Modifications for inclusion into the Linux staging tree are
 * Copyright(c) 2010 Larry Finger. All rights reserved.
 *
 * Contact information:
 * WLAN FAE <wlanfae@realtek.com>.
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 ******************************************************************************/

#define _HAL_INIT_C_

#include <linux/usb.h>
#include <linux/device.h>
#include <linux/usb/ch9.h>
#include <linux/firmware.h>
#include <linux/module.h>

#include "osdep_service.h"
#include "drv_types.h"
#include "usb_osintf.h"

#define FWBUFF_ALIGN_SZ 512
#define MAX_DUMP_FWSZ (48 * 1024)

static void rtl871x_load_fw_fail(struct _adapter *adapter)
{
	struct usb_device *udev = adapter->dvobjpriv.pusbdev;
	struct device *dev = &udev->dev;
	struct device *parent = dev->parent;

	complete(&adapter->rtl8712_fw_ready);

	dev_err(&udev->dev, "r8712u: Firmware request failed\n");

	if (parent)
		device_lock(parent);

	device_release_driver(dev);

	if (parent)
		device_unlock(parent);
}

static void rtl871x_load_fw_cb(const struct firmware *firmware, void *context)
{
	struct _adapter *adapter = context;

	if (!firmware) {
		rtl871x_load_fw_fail(adapter);
		return;
	}
	adapter->fw = firmware;
	/* firmware available - start netdev */
	register_netdev(adapter->pnetdev);
	complete(&adapter->rtl8712_fw_ready);
}

static const char firmware_file[] = "rtlwifi/rtl8712u.bin";

int rtl871x_load_fw(struct _adapter *padapter)
{
	struct device *dev = &padapter->dvobjpriv.pusbdev->dev;
	int rc;

	init_completion(&padapter->rtl8712_fw_ready);
	dev_info(dev, "r8712u: Loading firmware from \"%s\"\n", firmware_file);
	rc = request_firmware_nowait(THIS_MODULE, 1, firmware_file, dev,
				     GFP_KERNEL, padapter, rtl871x_load_fw_cb);
	if (rc)
		dev_err(dev, "r8712u: Firmware request error %d\n", rc);
	return rc;
}
MODULE_FIRMWARE("rtlwifi/rtl8712u.bin");

static u32 rtl871x_open_fw(struct _adapter *adapter, const u8 **mappedfw)
{
	if (adapter->fw->size > 200000) {
		dev_err(&adapter->pnetdev->dev, "r8712u: Bad fw->size of %zu\n",
			adapter->fw->size);
		return 0;
	}
	*mappedfw = adapter->fw->data;
	return adapter->fw->size;
}

static void fill_fwpriv(struct _adapter *adapter, struct fw_priv *fwpriv)
{
	struct dvobj_priv *dvobj = &adapter->dvobjpriv;
	struct registry_priv *regpriv = &adapter->registrypriv;

	memset(fwpriv, 0, sizeof(struct fw_priv));
	/* todo: check if needs endian conversion */
	fwpriv->hci_sel =  RTL8712_HCI_TYPE_72USB;
	fwpriv->usb_ep_num = (u8)dvobj->nr_endpoint;
	fwpriv->bw_40MHz_en = regpriv->cbw40_enable;
	switch (regpriv->rf_config) {
	case RTL8712_RF_1T1R:
		fwpriv->rf_config = RTL8712_RFC_1T1R;
		break;
	case RTL8712_RF_2T2R:
		fwpriv->rf_config = RTL8712_RFC_2T2R;
		break;
	case RTL8712_RF_1T2R:
	default:
		fwpriv->rf_config = RTL8712_RFC_1T2R;
	}
	fwpriv->mp_mode = (regpriv->mp_mode == 1);
	/* 0:off 1:on 2:auto */
	fwpriv->vcs_type = regpriv->vrtl_carrier_sense;
	fwpriv->vcs_mode = regpriv->vcs_type; /* 1:RTS/CTS 2:CTS to self */
	/* default enable turbo_mode */
	fwpriv->turbo_mode = (regpriv->wifi_test != 1);
	fwpriv->low_power_mode = regpriv->low_power;
}

static void update_fwhdr(struct fw_hdr	*pfwhdr, const u8 *pmappedfw)
{
	pfwhdr->signature = le16_to_cpu(*(__le16 *)pmappedfw);
	pfwhdr->version = le16_to_cpu(*(__le16 *)(pmappedfw + 2));
	/* define the size of boot loader */
	pfwhdr->dmem_size = le32_to_cpu(*(__le32 *)(pmappedfw + 4));
	/* define the size of FW in IMEM */
	pfwhdr->img_IMEM_size = le32_to_cpu(*(__le32 *)(pmappedfw + 8));
	/* define the size of FW in SRAM */
	pfwhdr->img_SRAM_size = le32_to_cpu(*(__le32 *)(pmappedfw + 12));
	/* define the size of DMEM variable */
	pfwhdr->fw_priv_sz = le32_to_cpu(*(__le32 *)(pmappedfw + 16));
}

static u8 chk_fwhdr(struct fw_hdr *pfwhdr, u32 ulfilelength)
{
	u32	fwhdrsz, fw_sz;

	/* check signature */
	if ((pfwhdr->signature != 0x8712) && (pfwhdr->signature != 0x8192))
		return _FAIL;
	/* check fw_priv_sze & sizeof(struct fw_priv) */
	if (pfwhdr->fw_priv_sz != sizeof(struct fw_priv))
		return _FAIL;
	/* check fw_sz & image_fw_sz */
	fwhdrsz = offsetof(struct fw_hdr, fwpriv) + pfwhdr->fw_priv_sz;
	fw_sz =  fwhdrsz + pfwhdr->img_IMEM_size + pfwhdr->img_SRAM_size +
		 pfwhdr->dmem_size;
	if (fw_sz != ulfilelength)
		return _FAIL;
	return _SUCCESS;
}

static u8 rtl8712_dl_fw(struct _adapter *adapter)
{
	sint i;
	u8 tmp8, tmp8_a;
	u16 tmp16;
	u32 maxlen = 0; /* for compare usage */
	uint dump_imem_sz, imem_sz, dump_emem_sz, emem_sz; /* max = 49152; */
	struct fw_hdr fwhdr;
	u32 ulfilelength;	/* FW file size */
	const u8 *mappedfw = NULL;
	u8 *tmpchar = NULL, *payload, *ptr;
	struct tx_desc *txdesc;
	u32 txdscp_sz = sizeof(struct tx_desc);
	u8 ret = _FAIL;

	ulfilelength = rtl871x_open_fw(adapter, &mappedfw);
	if (mappedfw && (ulfilelength > 0)) {
		update_fwhdr(&fwhdr, mappedfw);
		if (chk_fwhdr(&fwhdr, ulfilelength) == _FAIL)
			return ret;
		fill_fwpriv(adapter, &fwhdr.fwpriv);
		/* firmware check ok */
		maxlen = (fwhdr.img_IMEM_size > fwhdr.img_SRAM_size) ?
			  fwhdr.img_IMEM_size : fwhdr.img_SRAM_size;
		maxlen += txdscp_sz;
		tmpchar = kmalloc(maxlen + FWBUFF_ALIGN_SZ, GFP_KERNEL);
		if (!tmpchar)
			return ret;

		txdesc = (struct tx_desc *)(tmpchar + FWBUFF_ALIGN_SZ -
			    ((addr_t)(tmpchar) & (FWBUFF_ALIGN_SZ - 1)));
		payload = (u8 *)(txdesc) + txdscp_sz;
		ptr = (u8 *)mappedfw + offsetof(struct fw_hdr, fwpriv) +
		      fwhdr.fw_priv_sz;
		/* Download FirmWare */
		/* 1. determine IMEM code size and Load IMEM Code Section */
		imem_sz = fwhdr.img_IMEM_size;
		do {
			memset(txdesc, 0, TXDESC_SIZE);
			if (imem_sz >  MAX_DUMP_FWSZ/*49152*/) {
				dump_imem_sz = MAX_DUMP_FWSZ;
			} else {
				dump_imem_sz = imem_sz;
				txdesc->txdw0 |= cpu_to_le32(BIT(28));
			}
			txdesc->txdw0 |= cpu_to_le32(dump_imem_sz &
						       0x0000ffff);
			memcpy(payload, ptr, dump_imem_sz);
			r8712_write_mem(adapter, RTL8712_DMA_VOQ,
					dump_imem_sz + TXDESC_SIZE,
					(u8 *)txdesc);
			ptr += dump_imem_sz;
			imem_sz -= dump_imem_sz;
		} while (imem_sz > 0);
		i = 10;
		tmp16 = r8712_read16(adapter, TCR);
		while (((tmp16 & _IMEM_CODE_DONE) == 0) && (i > 0)) {
			usleep_range(10, 1000);
			tmp16 = r8712_read16(adapter, TCR);
			i--;
		}
		if (i == 0 || (tmp16 & _IMEM_CHK_RPT) == 0)
			goto exit_fail;

		/* 2.Download EMEM code size and Load EMEM Code Section */
		emem_sz = fwhdr.img_SRAM_size;
		do {
			memset(txdesc, 0, TXDESC_SIZE);
			if (emem_sz >  MAX_DUMP_FWSZ) { /* max=48k */
				dump_emem_sz = MAX_DUMP_FWSZ;
			} else {
				dump_emem_sz = emem_sz;
				txdesc->txdw0 |= cpu_to_le32(BIT(28));
			}
			txdesc->txdw0 |= cpu_to_le32(dump_emem_sz &
						       0x0000ffff);
			memcpy(payload, ptr, dump_emem_sz);
			r8712_write_mem(adapter, RTL8712_DMA_VOQ,
					dump_emem_sz + TXDESC_SIZE,
					(u8 *)txdesc);
			ptr += dump_emem_sz;
			emem_sz -= dump_emem_sz;
		} while (emem_sz > 0);
		i = 5;
		tmp16 = r8712_read16(adapter, TCR);
		while (((tmp16 & _EMEM_CODE_DONE) == 0) && (i > 0)) {
			usleep_range(10, 1000);
			tmp16 = r8712_read16(adapter, TCR);
			i--;
		}
		if (i == 0 || (tmp16 & _EMEM_CHK_RPT) == 0)
			goto exit_fail;

		/* 3.Enable CPU */
		tmp8 = r8712_read8(adapter, SYS_CLKR);
		r8712_write8(adapter, SYS_CLKR, tmp8 | BIT(2));
		tmp8_a = r8712_read8(adapter, SYS_CLKR);
		if (tmp8_a != (tmp8 | BIT(2)))
			goto exit_fail;

		tmp8 = r8712_read8(adapter, SYS_FUNC_EN + 1);
		r8712_write8(adapter, SYS_FUNC_EN + 1, tmp8 | BIT(2));
		tmp8_a = r8712_read8(adapter, SYS_FUNC_EN + 1);
		if (tmp8_a != (tmp8 | BIT(2)))
			goto exit_fail;

		r8712_read32(adapter, TCR);

		/* 4.polling IMEM Ready */
		i = 100;
		tmp16 = r8712_read16(adapter, TCR);
		while (((tmp16 & _IMEM_RDY) == 0) && (i > 0)) {
			msleep(20);
			tmp16 = r8712_read16(adapter, TCR);
			i--;
		}
		if (i == 0) {
			r8712_write16(adapter, 0x10250348, 0xc000);
			r8712_write16(adapter, 0x10250348, 0xc001);
			r8712_write16(adapter, 0x10250348, 0x2000);
			r8712_write16(adapter, 0x10250348, 0x2001);
			r8712_write16(adapter, 0x10250348, 0x2002);
			r8712_write16(adapter, 0x10250348, 0x2003);
			goto exit_fail;
		}
		/* 5.Download DMEM code size and Load EMEM Code Section */
		memset(txdesc, 0, TXDESC_SIZE);
		txdesc->txdw0 |= cpu_to_le32(fwhdr.fw_priv_sz & 0x0000ffff);
		txdesc->txdw0 |= cpu_to_le32(BIT(28));
		memcpy(payload, &fwhdr.fwpriv, fwhdr.fw_priv_sz);
		r8712_write_mem(adapter, RTL8712_DMA_VOQ,
				fwhdr.fw_priv_sz + TXDESC_SIZE, (u8 *)txdesc);

		/* polling dmem code done */
		i = 100;
		tmp16 = r8712_read16(adapter, TCR);
		while (((tmp16 & _DMEM_CODE_DONE) == 0) && (i > 0)) {
			msleep(20);
			tmp16 = r8712_read16(adapter, TCR);
			i--;
		}
		if (i == 0)
			goto exit_fail;

		tmp8 = r8712_read8(adapter, 0x1025000A);
		if (tmp8 & BIT(4)) /* When boot from EEPROM,
				    * & FW need more time to read EEPROM
				    */
			i = 60;
		else			/* boot from EFUSE */
			i = 30;
		tmp16 = r8712_read16(adapter, TCR);
		while (((tmp16 & _FWRDY) == 0) && (i > 0)) {
			msleep(100);
			tmp16 = r8712_read16(adapter, TCR);
			i--;
		}
		if (i == 0)
			goto exit_fail;
	} else {
		goto exit_fail;
	}
	ret = _SUCCESS;

exit_fail:
	kfree(tmpchar);
	return ret;
}

uint rtl8712_hal_init(struct _adapter *padapter)
{
	u32 val32;
	int i;

	/* r8712 firmware download */
	if (rtl8712_dl_fw(padapter) != _SUCCESS)
		return _FAIL;

	netdev_info(padapter->pnetdev, "1 RCR=0x%x\n",
		    r8712_read32(padapter, RCR));
	val32 = r8712_read32(padapter, RCR);
	r8712_write32(padapter, RCR, (val32 | BIT(26))); /* Enable RX TCP
							  * Checksum offload
							  */
	netdev_info(padapter->pnetdev, "2 RCR=0x%x\n",
		    r8712_read32(padapter, RCR));
	val32 = r8712_read32(padapter, RCR);
	r8712_write32(padapter, RCR, (val32 | BIT(25))); /* Append PHY status */
	val32 = r8712_read32(padapter, 0x10250040);
	r8712_write32(padapter,  0x10250040, (val32 & 0x00FFFFFF));
	/* for usb rx aggregation */
	r8712_write8(padapter, 0x102500B5, r8712_read8(padapter, 0x102500B5) |
	       BIT(0)); /* page = 128bytes */
	r8712_write8(padapter, 0x102500BD, r8712_read8(padapter, 0x102500BD) |
	       BIT(7)); /* enable usb rx aggregation */
	r8712_write8(padapter, 0x102500D9, 1); /* TH=1 => means that invalidate
						*  usb rx aggregation
						*/
	r8712_write8(padapter, 0x1025FE5B, 0x04); /* 1.7ms/4 */
	/* Fix the RX FIFO issue(USB error) */
	r8712_write8(padapter, 0x1025fe5C, r8712_read8(padapter, 0x1025fe5C)
		     | BIT(7));
	for (i = 0; i < ETH_ALEN; i++)
		padapter->eeprompriv.mac_addr[i] = r8712_read8(padapter,
							       MACID + i);
	return _SUCCESS;
}

uint rtl8712_hal_deinit(struct _adapter *padapter)
{
	r8712_write8(padapter, RF_CTRL, 0x00);
	/* Turn off BB */
	msleep(20);
	/* Turn off MAC	*/
	r8712_write8(padapter, SYS_CLKR + 1, 0x38); /* Switch Control Path */
	r8712_write8(padapter, SYS_FUNC_EN + 1, 0x70);
	r8712_write8(padapter, PMC_FSM, 0x06);  /* Enable Loader Data Keep */
	r8712_write8(padapter, SYS_ISO_CTRL, 0xF9); /* Isolation signals from
						     * CORE, PLL
						     */
	r8712_write8(padapter, SYS_ISO_CTRL + 1, 0xe8); /* Enable EFUSE 1.2V */
	r8712_write8(padapter, AFE_PLL_CTRL, 0x00); /* Disable AFE PLL. */
	r8712_write8(padapter, LDOA15_CTRL, 0x54);  /* Disable A15V */
	r8712_write8(padapter, SYS_FUNC_EN + 1, 0x50); /* Disable E-Fuse 1.2V */
	r8712_write8(padapter, LDOV12D_CTRL, 0x24); /* Disable LDO12(for CE) */
	r8712_write8(padapter, AFE_MISC, 0x30); /* Disable AFE BG&MB */
	/* Option for Disable 1.6V LDO.	*/
	r8712_write8(padapter, SPS0_CTRL, 0x56); /* Disable 1.6V LDO */
	r8712_write8(padapter, SPS0_CTRL + 1, 0x43);  /* Set SW PFM */
	return _SUCCESS;
}

uint rtl871x_hal_init(struct _adapter *padapter)
{
	padapter->hw_init_completed = false;
	if (!padapter->halpriv.hal_bus_init)
		return _FAIL;
	if (padapter->halpriv.hal_bus_init(padapter) != _SUCCESS)
		return _FAIL;
	if (rtl8712_hal_init(padapter) == _SUCCESS) {
		padapter->hw_init_completed = true;
	} else {
		padapter->hw_init_completed = false;
		return _FAIL;
	}
	return _SUCCESS;
}
