/******************************************************************************
 * hal_init.c
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
 * Linux device driver for RTL8192SU
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
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
 * Modifications for inclusion into the Linux staging tree are
 * Copyright(c) 2010 Larry Finger. All rights reserved.
 *
 * Contact information:
 * WLAN FAE <wlanfae@realtek.com>.
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 ******************************************************************************/

#define _HAL_INIT_C_

#include "osdep_service.h"
#include "drv_types.h"
#include "rtl871x_byteorder.h"
#include "usb_osintf.h"

#define FWBUFF_ALIGN_SZ 512
#define MAX_DUMP_FWSZ	49152 /*default = 49152 (48k)*/

static u32 rtl871x_open_fw(struct _adapter *padapter, void **pphfwfile_hdl,
		    const u8 **ppmappedfw)
{
	int rc;
	const char firmware_file[] = "rtlwifi/rtl8712u.bin";
	const struct firmware **praw = (const struct firmware **)
				       (pphfwfile_hdl);
	struct dvobj_priv *pdvobjpriv = (struct dvobj_priv *)
					(&padapter->dvobjpriv);
	struct usb_device *pusbdev = pdvobjpriv->pusbdev;

	printk(KERN_INFO "r8712u: Loading firmware from \"%s\"\n",
	       firmware_file);
	rc = request_firmware(praw, firmware_file, &pusbdev->dev);
	if (rc < 0) {
		printk(KERN_ERR "r8712u: Unable to load firmware\n");
		printk(KERN_ERR "r8712u: Install latest linux-firmware\n");
		return 0;
	}
	*ppmappedfw = (u8 *)((*praw)->data);
	return (*praw)->size;
}
MODULE_FIRMWARE("rtlwifi/rtl8712u.bin");

static void fill_fwpriv(struct _adapter *padapter, struct fw_priv *pfwpriv)
{
	struct dvobj_priv *pdvobj = (struct dvobj_priv *)&padapter->dvobjpriv;
	struct registry_priv *pregpriv = &padapter->registrypriv;

	memset(pfwpriv, 0, sizeof(struct fw_priv));
	/* todo: check if needs endian conversion */
	pfwpriv->hci_sel =  RTL8712_HCI_TYPE_72USB;
	pfwpriv->usb_ep_num = (u8)pdvobj->nr_endpoint;
	pfwpriv->bw_40MHz_en = pregpriv->cbw40_enable;
	switch (pregpriv->rf_config) {
	case RTL8712_RF_1T1R:
		pfwpriv->rf_config = RTL8712_RFC_1T1R;
		break;
	case RTL8712_RF_2T2R:
		pfwpriv->rf_config = RTL8712_RFC_2T2R;
		break;
	case RTL8712_RF_1T2R:
	default:
		pfwpriv->rf_config = RTL8712_RFC_1T2R;
	}
	pfwpriv->mp_mode = (pregpriv->mp_mode == 1) ? 1 : 0;
	pfwpriv->vcsType = pregpriv->vrtl_carrier_sense; /* 0:off 1:on 2:auto */
	pfwpriv->vcsMode = pregpriv->vcs_type; /* 1:RTS/CTS 2:CTS to self */
	/* default enable turboMode */
	pfwpriv->turboMode = ((pregpriv->wifi_test == 1) ? 0 : 1);
	pfwpriv->lowPowerMode = pregpriv->low_power;
}

static void update_fwhdr(struct fw_hdr	*pfwhdr, const u8 *pmappedfw)
{
	pfwhdr->signature = le16_to_cpu(*(u16 *)pmappedfw);
	pfwhdr->version = le16_to_cpu(*(u16 *)(pmappedfw+2));
	/* define the size of boot loader */
	pfwhdr->dmem_size = le32_to_cpu(*(uint *)(pmappedfw+4));
	/* define the size of FW in IMEM */
	pfwhdr->img_IMEM_size = le32_to_cpu(*(uint *)(pmappedfw+8));
	/* define the size of FW in SRAM */
	pfwhdr->img_SRAM_size = le32_to_cpu(*(uint *)(pmappedfw+12));
	/* define the size of DMEM variable */
	pfwhdr->fw_priv_sz = le32_to_cpu(*(uint *)(pmappedfw+16));
}

static u8 chk_fwhdr(struct fw_hdr *pfwhdr, u32 ulfilelength)
{
	u32	fwhdrsz, fw_sz;
	u8 intf, rfconf;

	/* check signature */
	if ((pfwhdr->signature != 0x8712) && (pfwhdr->signature != 0x8192))
		return _FAIL;
	/* check interface */
	intf = (u8)((pfwhdr->version&0x3000) >> 12);
	/* check rf_conf */
	rfconf = (u8)((pfwhdr->version&0xC000) >> 14);
	/* check fw_priv_sze & sizeof(struct fw_priv) */
	if (pfwhdr->fw_priv_sz != sizeof(struct fw_priv))
		return _FAIL;
	/* check fw_sz & image_fw_sz */
	fwhdrsz = FIELD_OFFSET(struct fw_hdr, fwpriv) + pfwhdr->fw_priv_sz;
	fw_sz =  fwhdrsz + pfwhdr->img_IMEM_size + pfwhdr->img_SRAM_size +
		 pfwhdr->dmem_size;
	if (fw_sz != ulfilelength)
		return _FAIL;
	return _SUCCESS;
}

static u8 rtl8712_dl_fw(struct _adapter *padapter)
{
	sint i;
	u8 tmp8, tmp8_a;
	u16 tmp16;
	u32 maxlen = 0, tmp32; /* for compare usage */
	uint dump_imem_sz, imem_sz, dump_emem_sz, emem_sz; /* max = 49152; */
	struct fw_hdr fwhdr;
	u32 ulfilelength;	/* FW file size */
	void *phfwfile_hdl = NULL;
	const u8 *pmappedfw = NULL;
	u8 *ptmpchar = NULL, *ppayload, *ptr;
	struct tx_desc *ptx_desc;
	u32 txdscp_sz = sizeof(struct tx_desc);
	u8 ret = _FAIL;

	ulfilelength = rtl871x_open_fw(padapter, &phfwfile_hdl, &pmappedfw);
	if (pmappedfw && (ulfilelength > 0)) {
		update_fwhdr(&fwhdr, pmappedfw);
		if (chk_fwhdr(&fwhdr, ulfilelength) == _FAIL)
			goto firmware_rel;
		fill_fwpriv(padapter, &fwhdr.fwpriv);
		/* firmware check ok */
		maxlen = (fwhdr.img_IMEM_size > fwhdr.img_SRAM_size) ?
			  fwhdr.img_IMEM_size : fwhdr.img_SRAM_size;
		maxlen += txdscp_sz;
		ptmpchar = _malloc(maxlen + FWBUFF_ALIGN_SZ);
		if (ptmpchar == NULL)
			goto firmware_rel;

		ptx_desc = (struct tx_desc *)(ptmpchar + FWBUFF_ALIGN_SZ -
			    ((addr_t)(ptmpchar) & (FWBUFF_ALIGN_SZ - 1)));
		ppayload = (u8 *)(ptx_desc) + txdscp_sz;
		ptr = (u8 *)pmappedfw + FIELD_OFFSET(struct fw_hdr, fwpriv) +
		      fwhdr.fw_priv_sz;
		/* Download FirmWare */
		/* 1. determine IMEM code size and Load IMEM Code Section */
		imem_sz = fwhdr.img_IMEM_size;
		do {
			memset(ptx_desc, 0, TXDESC_SIZE);
			if (imem_sz >  MAX_DUMP_FWSZ/*49152*/)
				dump_imem_sz = MAX_DUMP_FWSZ;
			else {
				dump_imem_sz = imem_sz;
				ptx_desc->txdw0 |= cpu_to_le32(BIT(28));
			}
			ptx_desc->txdw0 |= cpu_to_le32(dump_imem_sz &
						       0x0000ffff);
			memcpy(ppayload, ptr, dump_imem_sz);
			r8712_write_mem(padapter, RTL8712_DMA_VOQ,
				  dump_imem_sz + TXDESC_SIZE,
				  (u8 *)ptx_desc);
			ptr += dump_imem_sz;
			imem_sz -= dump_imem_sz;
		} while (imem_sz > 0);
		i = 10;
		tmp16 = r8712_read16(padapter, TCR);
		while (((tmp16 & _IMEM_CODE_DONE) == 0) && (i > 0)) {
			udelay(10);
			tmp16 = r8712_read16(padapter, TCR);
			i--;
		}
		if (i == 0 || (tmp16 & _IMEM_CHK_RPT) == 0)
			goto exit_fail;

		/* 2.Download EMEM code size and Load EMEM Code Section */
		emem_sz = fwhdr.img_SRAM_size;
		do {
			memset(ptx_desc, 0, TXDESC_SIZE);
			if (emem_sz >  MAX_DUMP_FWSZ) /* max=48k */
				dump_emem_sz = MAX_DUMP_FWSZ;
			else {
				dump_emem_sz = emem_sz;
				ptx_desc->txdw0 |= cpu_to_le32(BIT(28));
			}
			ptx_desc->txdw0 |= cpu_to_le32(dump_emem_sz &
						       0x0000ffff);
			memcpy(ppayload, ptr, dump_emem_sz);
			r8712_write_mem(padapter, RTL8712_DMA_VOQ,
				  dump_emem_sz+TXDESC_SIZE, (u8 *)ptx_desc);
			ptr += dump_emem_sz;
			emem_sz -= dump_emem_sz;
		} while (emem_sz > 0);
		i = 5;
		tmp16 = r8712_read16(padapter, TCR);
		while (((tmp16 & _EMEM_CODE_DONE) == 0) && (i > 0)) {
			udelay(10);
			tmp16 = r8712_read16(padapter, TCR);
			i--;
		}
		if (i == 0 || (tmp16 & _EMEM_CHK_RPT) == 0)
			goto exit_fail;

		/* 3.Enable CPU */
		tmp8 = r8712_read8(padapter, SYS_CLKR);
		r8712_write8(padapter, SYS_CLKR, tmp8|BIT(2));
		tmp8_a = r8712_read8(padapter, SYS_CLKR);
		if (tmp8_a != (tmp8|BIT(2)))
			goto exit_fail;

		tmp8 = r8712_read8(padapter, SYS_FUNC_EN + 1);
		r8712_write8(padapter, SYS_FUNC_EN+1, tmp8|BIT(2));
		tmp8_a = r8712_read8(padapter, SYS_FUNC_EN + 1);
		if (tmp8_a != (tmp8|BIT(2)))
			goto exit_fail;

		tmp32 = r8712_read32(padapter, TCR);

		/* 4.polling IMEM Ready */
		i = 100;
		tmp16 = r8712_read16(padapter, TCR);
		while (((tmp16 & _IMEM_RDY) == 0) && (i > 0)) {
			msleep(20);
			tmp16 = r8712_read16(padapter, TCR);
			i--;
		}
		if (i == 0) {
			r8712_write16(padapter, 0x10250348, 0xc000);
			r8712_write16(padapter, 0x10250348, 0xc001);
			r8712_write16(padapter, 0x10250348, 0x2000);
			r8712_write16(padapter, 0x10250348, 0x2001);
			r8712_write16(padapter, 0x10250348, 0x2002);
			r8712_write16(padapter, 0x10250348, 0x2003);
			goto exit_fail;
		}
		/* 5.Download DMEM code size and Load EMEM Code Section */
		memset(ptx_desc, 0, TXDESC_SIZE);
		ptx_desc->txdw0 |= cpu_to_le32(fwhdr.fw_priv_sz&0x0000ffff);
		ptx_desc->txdw0 |= cpu_to_le32(BIT(28));
		memcpy(ppayload, &fwhdr.fwpriv, fwhdr.fw_priv_sz);
		r8712_write_mem(padapter, RTL8712_DMA_VOQ,
			  fwhdr.fw_priv_sz + TXDESC_SIZE, (u8 *)ptx_desc);

		/* polling dmem code done */
		i = 100;
		tmp16 = r8712_read16(padapter, TCR);
		while (((tmp16 & _DMEM_CODE_DONE) == 0) && (i > 0)) {
			msleep(20);
			tmp16 = r8712_read16(padapter, TCR);
			i--;
		}
		if (i == 0)
			goto exit_fail;

		tmp8 = r8712_read8(padapter, 0x1025000A);
		if (tmp8 & BIT(4)) /* When boot from EEPROM,
				    & FW need more time to read EEPROM */
			i = 60;
		else			/* boot from EFUSE */
			i = 30;
		tmp16 = r8712_read16(padapter, TCR);
		while (((tmp16 & _FWRDY) == 0) && (i > 0)) {
			msleep(100);
			tmp16 = r8712_read16(padapter, TCR);
			i--;
		}
		if (i == 0)
			goto exit_fail;
	} else
		goto exit_fail;
	ret = _SUCCESS;

exit_fail:
	kfree(ptmpchar);
firmware_rel:
	release_firmware((struct firmware *)phfwfile_hdl);
	return ret;
}

uint rtl8712_hal_init(struct _adapter *padapter)
{
	u32 val32;
	int i;

	/* r8712 firmware download */
	if (rtl8712_dl_fw(padapter) != _SUCCESS)
		return _FAIL;

	printk(KERN_INFO "r8712u: 1 RCR=0x%x\n",  r8712_read32(padapter, RCR));
	val32 = r8712_read32(padapter, RCR);
	r8712_write32(padapter, RCR, (val32 | BIT(26))); /* Enable RX TCP
							    Checksum offload */
	printk(KERN_INFO "r8712u: 2 RCR=0x%x\n", r8712_read32(padapter, RCR));
	val32 = r8712_read32(padapter, RCR);
	r8712_write32(padapter, RCR, (val32|BIT(25))); /* Append PHY status */
	val32 = 0;
	val32 = r8712_read32(padapter, 0x10250040);
	r8712_write32(padapter,  0x10250040, (val32&0x00FFFFFF));
	/* for usb rx aggregation */
	r8712_write8(padapter, 0x102500B5, r8712_read8(padapter, 0x102500B5) |
	       BIT(0)); /* page = 128bytes */
	r8712_write8(padapter, 0x102500BD, r8712_read8(padapter, 0x102500BD) |
	       BIT(7)); /* enable usb rx aggregation */
	r8712_write8(padapter, 0x102500D9, 1); /* TH=1 => means that invalidate
						*  usb rx aggregation */
	r8712_write8(padapter, 0x1025FE5B, 0x04); /* 1.7ms/4 */
	/* Fix the RX FIFO issue(USB error) */
	r8712_write8(padapter, 0x1025fe5C, r8712_read8(padapter, 0x1025fe5C)
		     | BIT(7));
	for (i = 0; i < 6; i++)
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
	r8712_write8(padapter, SYS_CLKR+1, 0x38); /* Switch Control Path */
	r8712_write8(padapter, SYS_FUNC_EN+1, 0x70);
	r8712_write8(padapter, PMC_FSM, 0x06);  /* Enable Loader Data Keep */
	r8712_write8(padapter, SYS_ISO_CTRL, 0xF9); /* Isolation signals from
						     * CORE, PLL */
	r8712_write8(padapter, SYS_ISO_CTRL+1, 0xe8); /* Enable EFUSE 1.2V */
	r8712_write8(padapter, AFE_PLL_CTRL, 0x00); /* Disable AFE PLL. */
	r8712_write8(padapter, LDOA15_CTRL, 0x54);  /* Disable A15V */
	r8712_write8(padapter, SYS_FUNC_EN+1, 0x50); /* Disable E-Fuse 1.2V */
	r8712_write8(padapter, LDOV12D_CTRL, 0x24); /* Disable LDO12(for CE) */
	r8712_write8(padapter, AFE_MISC, 0x30); /* Disable AFE BG&MB */
	/* Option for Disable 1.6V LDO.	*/
	r8712_write8(padapter, SPS0_CTRL, 0x56); /* Disable 1.6V LDO */
	r8712_write8(padapter, SPS0_CTRL+1, 0x43);  /* Set SW PFM */
	return _SUCCESS;
}

uint rtl871x_hal_init(struct _adapter *padapter)
{
	padapter->hw_init_completed = false;
	if (padapter->halpriv.hal_bus_init == NULL)
		return _FAIL;
	else {
		if (padapter->halpriv.hal_bus_init(padapter) != _SUCCESS)
			return _FAIL;
	}
	if (rtl8712_hal_init(padapter) == _SUCCESS)
		padapter->hw_init_completed = true;
	else {
		padapter->hw_init_completed = false;
		return _FAIL;
	}
	return _SUCCESS;
}
