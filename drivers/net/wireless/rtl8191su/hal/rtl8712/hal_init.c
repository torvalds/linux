/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/ 

#define _HAL_INIT_C_
#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <rtl871x_byteorder.h>

#include <hal_init.h>


#ifdef CONFIG_SDIO_HCI
#include <sdio_hal.h>
#ifdef PLATFORM_LINUX
#include <linux/mmc/sdio_func.h>
#endif
#elif defined(CONFIG_USB_HCI)
#include <usb_hal.h>
#endif	

#ifdef PLATFORM_OS_CE
#define FWBUFF_ALIGN_SZ 4
#else
#define FWBUFF_ALIGN_SZ 512
#endif

#if defined(PLATFORM_OS_CE) && defined(CONFIG_USB_HCI)
#define MAX_DUMP_FWSZ	2000
#else
#define MAX_DUMP_FWSZ	3072  /*default = 49152 (48k)*/
#endif

void fill_fwpriv(_adapter * padapter, struct fw_priv *pfwpriv)
{
	struct dvobj_priv	*pdvobj = (struct dvobj_priv *)&padapter->dvobjpriv;
	struct registry_priv *pregpriv = &padapter->registrypriv;

	_memset(pfwpriv, 0, sizeof(struct fw_priv));

#ifdef CONFIG_USB_HCI
        //todo: check if needs endian conversion
	pfwpriv->hci_sel =  RTL8712_HCI_TYPE_72USB;
	pfwpriv->usb_ep_num = (u8)pdvobj->nr_endpoint;
#endif	

#ifdef CONFIG_SDIO_HCI
	pfwpriv->hci_sel =  RTL8712_HCI_TYPE_72SDIO;
#endif

#ifdef CONFIG_80211N_HT
	pfwpriv->bw_40MHz_en = pregpriv->cbw40_enable;
#else
	pfwpriv->bw_40MHz_en = 0;
#endif

	switch (pregpriv->rf_config) {
		case RTL8712_RF_1T1R:
			pfwpriv->rf_config = RTL8712_RFCONFIG_1T1R;
			RT_TRACE(_module_hal_init_c_,_drv_info_,("== fill_fwpriv: RF_CONFIG=1T1R \n"));
			break;
		case RTL8712_RF_2T2R:
			pfwpriv->rf_config = RTL8712_RFCONFIG_2T2R;
			RT_TRACE(_module_hal_init_c_,_drv_info_,("== fill_fwpriv: RF_CONFIG=2T2R \n"));
			break;
		case RTL8712_RF_1T2R:
		default:
			pfwpriv->rf_config = RTL8712_RFCONFIG_1T2R;
			RT_TRACE(_module_hal_init_c_,_drv_info_,("== fill_fwpriv: RF_CONFIG=1T2R \n"));
	}

	pfwpriv->mp_mode =  ( pregpriv->mp_mode == 1)?1 :0 ;	

	pfwpriv->vcsType = pregpriv->vrtl_carrier_sense; /* 0:off 1:on 2:auto */
	pfwpriv->vcsMode = pregpriv->vcs_type; /* 1:RTS/CTS 2:CTS to self */

	pfwpriv->turboMode = ( ( pregpriv->wifi_test == 1 ) ? 0 : 1 ) ;	//default enable it

	pfwpriv->lowPowerMode = pregpriv->low_power;
	pfwpriv->rsvd024 = 1;	//	F/W will issue two probe request. One is with ssid ( if exists ), another is with the wildcard ssid.
	RT_TRACE(_module_hal_init_c_,_drv_err_,("== fill_fwpriv: pfwpriv->lowPowerMode=%d [0:normal / 1:low power]\n",pfwpriv->lowPowerMode ));
}

void update_fwhdr(struct fw_hdr	* pfwhdr, u8* pmappedfw)
{
	pfwhdr->signature = le16_to_cpu(*(u16 *)pmappedfw);
	pfwhdr->version = le16_to_cpu(*(u16 *)(pmappedfw+2));
	
	pfwhdr->dmem_size = le32_to_cpu(*(uint *)(pmappedfw+4));    //define the size of boot loader
	
	pfwhdr->img_IMEM_size = le32_to_cpu(*(uint *)(pmappedfw+8));    //define the size of FW in IMEM
	
	pfwhdr->img_SRAM_size = le32_to_cpu(*(uint *)(pmappedfw+12));    //define the size of FW in SRAM
	
	pfwhdr->fw_priv_sz = le32_to_cpu(*(uint *)(pmappedfw+16));      //define the size of DMEM variable 
	
	
	//_memcpy(&pfwhdr->fwpriv, pmappedfw+16 + sizeof(uint)*4, sizeof(struct fw_priv));

	RT_TRACE(_module_hal_init_c_,_drv_info_,("update_fwhdr:sig=%x;ver=%x;dmem_size=%d;IMEMsz=%d;SRAMsz=%d;fwprivsz=%d;struct_fwprivsz=%d\n",
					pfwhdr->signature, pfwhdr->version,pfwhdr->dmem_size,pfwhdr->img_IMEM_size,pfwhdr->img_SRAM_size, pfwhdr->fw_priv_sz, sizeof(struct fw_priv)));

}

u8 chk_fwhdr(struct fw_hdr *pfwhdr, u32 ulfilelength)
{
	u32	fwhdrsz, fw_sz;
	u8 intf, rfconf;

	//check signature
	if ((pfwhdr->signature != 0x8712) && (pfwhdr->signature != 0x8192))
	{
		RT_TRACE(_module_hal_init_c_,_drv_err_,("Signature does not match (Signature %x != 8712)! Issue complaints for fw coder\n", pfwhdr->signature));
		return _FAIL;
	}

	//check fw_version
	RT_TRACE(_module_hal_init_c_,_drv_info_,("FW_VER=%X\n", pfwhdr->version&0x0FFF));
	
	//check interface
	intf = (u8)((pfwhdr->version&0x3000) >> 12);
	RT_TRACE(_module_hal_init_c_,_drv_info_,("Interface=%X", intf));		

	//check rf_conf
	rfconf = (u8)((pfwhdr->version&0xC000) >> 14);
	RT_TRACE(_module_hal_init_c_,_drv_info_,("chk_fwhdr RF_Configure=%X", rfconf));	

	//check fw_priv_sze & sizeof(struct fw_priv)
	if(pfwhdr->fw_priv_sz != sizeof(struct fw_priv))
	{
		RT_TRACE(_module_hal_init_c_,_drv_err_,("fw_priv size mismatch between fw(%d) and driver(%d)\n", pfwhdr->fw_priv_sz, sizeof(struct fw_priv)));		
		return _FAIL;
	}
	

	//check fw_sz & image_fw_sz
	fwhdrsz = FIELD_OFFSET(struct fw_hdr, fwpriv) + pfwhdr->fw_priv_sz;	
	fw_sz =  fwhdrsz + pfwhdr->img_IMEM_size +pfwhdr->img_SRAM_size + pfwhdr->dmem_size;	
	if (fw_sz != ulfilelength)
	{			
		RT_TRACE(_module_hal_init_c_,_drv_err_,("FW image size dismatch! fw_sz=%d != image_fw_sz = %d!\n", fw_sz, ulfilelength));		
		return _FAIL;
	}

	return _SUCCESS;

}

u8 rtl8712_dl_fw(_adapter *padapter)
{
	sint	i;
	u8	tmp8, tmp8_a;
	u16	tmp16;
	u32	maxlen = 0, tmp32; //for compare usage

	uint	dump_imem_sz, imem_sz, dump_emem_sz, emem_sz; // max = 49152;
	struct fw_hdr	fwhdr;
	u32	ulfilelength;	//FW file size
	void	*phfwfile_hdl = NULL;
	u8	*pmappedfw = NULL, *ptmpchar = NULL, *ppayload, *ptr;
	u8	ret8 = _SUCCESS;
	struct registry_priv	*pregistrypriv = &padapter->registrypriv;

	struct tx_desc	*ptx_desc;
	u32	txdscp_sz = sizeof(struct tx_desc);
	u32	addr = 0;
	struct dvobj_priv *pdvobjpriv = &padapter->dvobjpriv;

_func_enter_;

	RT_TRACE(_module_hal_init_c_, _drv_notice_, ("rtl8712_dl_fw \n"));

	ulfilelength = rtl871x_open_fw(padapter, &phfwfile_hdl, &pmappedfw);
	if(pmappedfw && (ulfilelength>0))
	{
		update_fwhdr(&fwhdr, pmappedfw);

		if(chk_fwhdr(&fwhdr, ulfilelength)== _FAIL)
		{
			RT_TRACE(_module_hal_init_c_,_drv_err_,("CHK FWHDR fail!\n"));
			ret8 = _FAIL;
			goto exit;
		}

		fill_fwpriv(padapter, &fwhdr.fwpriv);

		//firmware check ok
		//RT_TRACE(_module_hal_init_c_,_drv_info_,("Downloading RTL8712 firmware major(%d)/minor(%d) version...\n", fwhdr.version >>8, fwhdr.version & 0xff));

		maxlen = (fwhdr.img_IMEM_size > fwhdr.img_SRAM_size)? fwhdr.img_IMEM_size : fwhdr.img_SRAM_size;
		maxlen += txdscp_sz;
		//ptmpchar = _malloc(maxlen + FWBUFF_ALIGN_SZ);
		ptmpchar = _malloc(4*1024);

		if (ptmpchar==NULL) {
			RT_TRACE(_module_hal_init_c_,_drv_err_,("can't alloc resources when dl_fw\n"));
			ret8 = _FAIL;
			goto exit;
		}

		ptx_desc = (struct tx_desc *)(ptmpchar + FWBUFF_ALIGN_SZ - ((u32 )(ptmpchar )&(FWBUFF_ALIGN_SZ-1)));
		ppayload = (u8*)(ptx_desc) + txdscp_sz;
		//ptr = pmappedfw+sizeof(struct fw_hdr)+fwhdr.dmem_size;
		ptr = pmappedfw + FIELD_OFFSET(struct fw_hdr, fwpriv) + fwhdr.fw_priv_sz ;//+ fwhdr.dmem_size;

		//Download FirmWare

		// 1. determine IMEM code size and Load IMEM Code Section
		RT_TRACE(_module_hal_init_c_, _drv_notice_,("=============STEP1.================\n"));
		
		//_memcpy(ppayload, ptr, fwhdr.img_IMEM_size);
		//ptx_desc->linip=1;
		//ptx_desc->txpktsize = fwhdr.img_IMEM_size;
		//ptx_desc->qsel=3;//not necessary

		//_memset(ptx_desc, 0, TXDESC_SIZE);
		
#if 1
		imem_sz = fwhdr.img_IMEM_size;

		do {
			_memset(ptx_desc, 0, TXDESC_SIZE);

			if(imem_sz >  MAX_DUMP_FWSZ) {
				dump_imem_sz = MAX_DUMP_FWSZ;
			} else {
				dump_imem_sz = imem_sz;
				ptx_desc->txdw0 |= cpu_to_le32(BIT(28));	
			}

			ptx_desc->txdw0 |= cpu_to_le32(dump_imem_sz&0x0000ffff);

			_memcpy(ppayload, ptr, dump_imem_sz);
#ifdef CONFIG_USB_HCI
			write_mem(padapter, RTL8712_DMA_VOQ, dump_imem_sz+TXDESC_SIZE, (u8*)ptx_desc);
#endif
#ifdef CONFIG_SDIO_HCI
			write_port(padapter, RTL8712_DMA_VOQ,  dump_imem_sz+TXDESC_SIZE ,(u8*) ptx_desc);
#endif
			ptr += dump_imem_sz;

			imem_sz -= dump_imem_sz;

		}while (imem_sz > 0);
#endif

		//ptx_desc->txdw0 |= (fwhdr.img_IMEM_size&0x0000ffff);
		//ptx_desc->txdw0 |= BIT(28);
		//ptx_desc->txdw1 |= ((3<<8)&0x00001f00);

		RT_TRACE(_module_hal_init_c_, _drv_notice_,(" WT IMEM ; txpktsize = %x\n", ptx_desc->txdw0));
		//write_port(padapter, RTL8712_DMA_VOQ, ptx_desc->txpktsize+32 ,(u8*) ptx_desc);
		//write_mem(padapter, RTL8712_DMA_VOQ, ptx_desc->txpktsize+32 ,(u8*)ptx_desc);
		//write_mem(padapter, RTL8712_DMA_VOQ, fwhdr.img_IMEM_size+TXDESC_SIZE, (u8*)ptx_desc);

		//ptr = ptr + fwhdr.img_IMEM_size;
		i = 10;
		tmp16=read16(padapter,TCR);
		RT_TRACE(_module_hal_init_c_, _drv_notice_,("TCR val = %x\n", tmp16));
		while(((tmp16 & _IMEM_CODE_DONE)==0) && (i>0))
		{
			//delay
			udelay_os(10);
			tmp16=read16(padapter,TCR);
			RT_TRACE(_module_hal_init_c_, _drv_notice_, ("TCR val = %x\n", tmp16));						
			i--;
		}
		if (i == 0) {
			RT_TRACE(_module_hal_init_c_, _drv_err_, ("Error => Pollin _IMEM_CODE_DONE Fail\n"));
			ret8=_FAIL;
			goto exit;
		}

		if((tmp16 & _IMEM_CHK_RPT) == 0) {
			RT_TRACE(_module_hal_init_c_, _drv_err_, ("_IMEM_CHK_RPT = 0\n"));
			ret8 = _FAIL;
			goto exit;
		}

		// 2.Download EMEM code size and Load EMEM Code Section

		RT_TRACE(_module_hal_init_c_, _drv_notice_,("=============STEP2.================\n"));

		//ptr = ptr + fwhdr.img_IMEM_size;
		//ptx_desc->linip=1;
		//ptx_desc->txpktsize =  fwhdr.img_SRAM_size ;		

		emem_sz = fwhdr.img_SRAM_size;
		RT_TRACE(_module_hal_init_c_, _drv_notice_,("WT SRAM ; txpktsize = 0x%x  fwhdr.img_SRAM_size=0x%x\n", emem_sz,fwhdr.img_SRAM_size));
		
		do{
	
			_memset(ptx_desc, 0, TXDESC_SIZE);

			if(emem_sz >  MAX_DUMP_FWSZ/*49152*/) //max=48k
			{
				dump_emem_sz = MAX_DUMP_FWSZ;//49152
			}
			else
			{
				dump_emem_sz = emem_sz;
				ptx_desc->txdw0 |= cpu_to_le32(BIT(28));		
				
			}			

			ptx_desc->txdw0 |= cpu_to_le32(dump_emem_sz&0x0000ffff); 
			
			_memcpy(ppayload, ptr, dump_emem_sz);
			write_mem(padapter, RTL8712_DMA_VOQ, dump_emem_sz+TXDESC_SIZE, (u8*)ptx_desc);
			ptr +=dump_emem_sz;	
			
			emem_sz -= dump_emem_sz;

		}while(emem_sz>0);
			
		//ptx_desc->txdw1 |= ((3<<QSEL_SHT)&0x00001f00);
		
		RT_TRACE(_module_hal_init_c_, _drv_notice_,("WT SRAM; txpktsize = %x\n", ptx_desc->txdw0));	
		//_memcpy(ppayload, ptr, fwhdr.img_SRAM_size);
		//write_port(padapter, RTL8712_DMA_VOQ, ptx_desc->txpktsize+32, (u8*)ptx_desc);
		//write_mem(padapter, RTL8712_DMA_VOQ, ptx_desc->txpktsize+32 ,(u8*) ptx_desc);
		//write_mem(padapter, RTL8712_DMA_VOQ, fwhdr.img_SRAM_size+TXDESC_SIZE, (u8*)ptx_desc);
		

		i = 5;
		tmp16=read16(padapter,TCR);
			RT_TRACE(_module_hal_init_c_, _drv_notice_,("TCR val = %x\n", tmp16));
		while(((tmp16 & _EMEM_CODE_DONE)==0) && ( i>0))
		{
			udelay_os(10);
			tmp16=read16(padapter,TCR);
			RT_TRACE(_module_hal_init_c_, _drv_notice_,("TCR val = %x\n", tmp16));
			i--;
		}
		if (i == 0) {
			RT_TRACE(_module_hal_init_c_,_drv_err_,("Error => Pollin _EMEM_CODE_DONE Fail\n"));
			ret8=_FAIL;
			goto exit;
		}

		if ((tmp16 & _EMEM_CHK_RPT) == 0) {
			ret8 = _FAIL;
			RT_TRACE(_module_hal_init_c_,_drv_err_,("_EMEM_CHK_RPT = 0\n"));
			goto exit;
		}

		// 3.Enable CPU
		RT_TRACE(_module_hal_init_c_, _drv_notice_,("=============STEP3.================\n"));

		tmp8 = read8(padapter, SYS_CLKR);

		RT_TRACE(_module_hal_init_c_, _drv_notice_,("WT SYS_CLKR to 0x%x(ori=0x%x)\n", (u32)(tmp8|BIT(2)),tmp8) );

		write8(padapter, SYS_CLKR, tmp8|BIT(2));
		tmp8_a = read8(padapter, SYS_CLKR);

		if (tmp8_a != (tmp8|BIT(2))) {
			RT_TRACE(_module_hal_init_c_,_drv_err_,("Error=> WT SYS_FUNC_EN fail; SYS_CLKR = %x;  target_val = %x\n", tmp8_a, tmp8));
			ret8 = _FAIL;
			goto exit;
		}

		tmp8 = read8(padapter, SYS_FUNC_EN + 1);
		RT_TRACE(_module_hal_init_c_, _drv_notice_,("WT SYS_FUNC_EN+1 to 0x%x[ori=0x%x]\n",(u32)(tmp8|BIT(2)),tmp8));
		write8(padapter, SYS_FUNC_EN+1, tmp8|BIT(2)); 
		tmp8_a = read8(padapter, SYS_FUNC_EN + 1); 
		if (tmp8_a != (tmp8|BIT(2))) {
			RT_TRACE(_module_hal_init_c_,_drv_err_,("Error=> WT SYS_FUNC_EN fail; SYS_FUNC_EN=%x; target_val=%x\n", tmp8_a, tmp8));
			ret8=_FAIL;
			goto exit;
		}

		//---
		RT_TRACE(_module_hal_init_c_, _drv_notice_,("WT TCR |_BASECHG\n"));

		tmp32 = read32(padapter, TCR);
		RT_TRACE(_module_hal_init_c_, _drv_notice_,("RD TCR = %x\n", tmp32));

		// 4.polling IMEM Ready
		RT_TRACE(_module_hal_init_c_, _drv_notice_,("=============STEP4.================\n"));

		RT_TRACE(_module_hal_init_c_, _drv_notice_,(" polling IMEM Ready\n"));
#ifdef CONFIG_USB_HCI
		//write8(padapter, 0xb025003a, 0x3e);
		//RT_TRACE(_module_hal_init_c_,_drv_info_,("RD DBS = %x \n", read8(padapter, 0xb025003a)));
#endif
		i = 100;
		tmp16 = read16(padapter, TCR);
		while (((tmp16 & _IMEM_RDY) == 0) && (i > 0)) 
		{
			udelay_os(1000);
			tmp16 = read16(padapter,TCR);
			RT_TRACE(_module_hal_init_c_, _drv_notice_,("TCR val = %x\n", tmp16));
			i--;
		}
		if (i == 0)
		{
			RT_TRACE(_module_hal_init_c_,_drv_emerg_,("Error => Pollin _IMEM_RDY Fail\n"));
			RT_TRACE(_module_hal_init_c_,_drv_emerg_,("\nread  0x10250318=0x%x\n",read32(padapter,0x10250318)));
			write16(padapter, 0x10250348, 0xc000);
			RT_TRACE(_module_hal_init_c_,_drv_emerg_,("write 0x10250348 0xc000\n"));
			RT_TRACE(_module_hal_init_c_,_drv_emerg_,("read  0x10250340=0x%x\n",read32(padapter,0x10250340)));
			RT_TRACE(_module_hal_init_c_,_drv_emerg_,("read  0x10250344=0x%x\n",read32(padapter,0x10250344)));
			write16(padapter, 0x10250348, 0xc001);
			RT_TRACE(_module_hal_init_c_,_drv_emerg_,("write 0x10250348 0xc001\n"));
			RT_TRACE(_module_hal_init_c_,_drv_emerg_,("read  0x10250340=0x%x\n",read32(padapter,0x10250340)));
			RT_TRACE(_module_hal_init_c_,_drv_emerg_,("read  0x10250344=0x%x\n",read32(padapter,0x10250344)));
			write16(padapter, 0x10250348, 0x2000);
			RT_TRACE(_module_hal_init_c_,_drv_emerg_,("write 0x10250348 0x2000\n"));
			RT_TRACE(_module_hal_init_c_,_drv_emerg_,("read  0x10250340=0x%x\n",read32(padapter,0x10250340)));
			RT_TRACE(_module_hal_init_c_,_drv_emerg_,("read  0x10250344=0x%x\n",read32(padapter,0x10250344)));
			write16(padapter, 0x10250348, 0x2001);
			RT_TRACE(_module_hal_init_c_,_drv_emerg_,("write 0x10250348 0x2001\n"));
			RT_TRACE(_module_hal_init_c_,_drv_emerg_,("read  0x10250340=0x%x\n",read32(padapter,0x10250340)));
			RT_TRACE(_module_hal_init_c_,_drv_emerg_,("read  0x10250344=0x%x\n",read32(padapter,0x10250344)));
			write16(padapter, 0x10250348, 0x2002);
			RT_TRACE(_module_hal_init_c_,_drv_emerg_,("write 0x10250348 0x2002\n"));
			RT_TRACE(_module_hal_init_c_,_drv_emerg_,("read  0x10250340=0x%x\n",read32(padapter,0x10250340)));
			RT_TRACE(_module_hal_init_c_,_drv_emerg_,("read  0x10250344=0x%x\n",read32(padapter,0x10250344)));
			write16(padapter, 0x10250348, 0x2003);
			RT_TRACE(_module_hal_init_c_,_drv_emerg_,("write 0x10250348 0x2003\n"));
			RT_TRACE(_module_hal_init_c_,_drv_emerg_,("read  0x10250340=0x%x\n",read32(padapter,0x10250340)));
			RT_TRACE(_module_hal_init_c_,_drv_emerg_,("read  0x10250344=0x%x\n",read32(padapter,0x10250344)));
	{
			u32 i;
			RT_TRACE(_module_hal_init_c_,_drv_emerg_,("==dump register[0-44]===\n"));
			for (i = 0; i < 0x48; i = i+4)
				RT_TRACE(_module_hal_init_c_,_drv_emerg_,("[%x]=0x%x\n",i,read32(padapter,0x10250000+i)));
			RT_TRACE(_module_hal_init_c_,_drv_emerg_,("===dump end===\n"));
	}
			ret8 = _FAIL;
			goto exit;
		}

		//5.Download DMEM code size and Load EMEM Code Section
		//tx_desc.linip=1;
		//tx_desc.qsel=3;	//not necessary		
		RT_TRACE(_module_hal_init_c_, _drv_notice_,("=============STEP5.================\n"));

		_memset(ptx_desc, 0, TXDESC_SIZE);

		ptx_desc->txdw0 |= cpu_to_le32(fwhdr.fw_priv_sz&0x0000ffff);		
		ptx_desc->txdw0 |= cpu_to_le32(BIT(28));
		
		_memcpy(ppayload, &fwhdr.fwpriv, fwhdr.fw_priv_sz);

		RT_TRACE(_module_hal_init_c_, _drv_notice_,(" Download fwpriv; txpktsize=0x%x ptx_desc->txdw0=0x%x\n", fwhdr.fw_priv_sz,ptx_desc->txdw0));
#ifdef CONFIG_USB_HCI		
		write_mem(padapter, RTL8712_DMA_VOQ, fwhdr.fw_priv_sz+TXDESC_SIZE, (u8*)ptx_desc);
#endif
#ifdef CONFIG_SDIO_HCI
		write_port(padapter, RTL8712_DMA_VOQ,  fwhdr.fw_priv_sz+TXDESC_SIZE, (u8*)ptx_desc);
		RT_TRACE(_module_hal_init_c_,_drv_err_,(" Download fwpriv; fwhdr.fw_priv_sz+TXDESC_SIZE= 0x%x\n", fwhdr.fw_priv_sz+TXDESC_SIZE));

#endif
		//polling dmem code done
		i = 100;
		tmp16=read16(padapter,TCR);
		while(((tmp16 & _DMEM_CODE_DONE)==0) && ( i>0))
		{
			udelay_os(1000);
			tmp16=read16(padapter,TCR);
			RT_TRACE(_module_hal_init_c_, _drv_notice_,("TCR val=%x\n", tmp16));
			i--;
		}
		if(i==0)
		{
			RT_TRACE(_module_hal_init_c_,_drv_err_,("Error => Pollin _DMEM_CODE_DONE Fail\n"));
			ret8=_FAIL;
			goto exit;
		}

#if 1
	        RT_TRACE(_module_hal_init_c_, _drv_notice_,("====STEP6.==Polling _FWRDY if ready==\n"));

		tmp8 = read8(padapter, 0x1025000A);

		if(tmp8 & BIT(4))//When boot from EEPROM , FW need more time to read EEPROM 	
			i = 60;
		else 			//boot from EFUSE
			i = 30;
		
		tmp16=read16(padapter,TCR);
		while(((tmp16 & _FWRDY)==0) && ( i>0))
		{
			//udelay_os(1000);
			msleep_os(100);
			tmp16=read16(padapter,TCR);
			RT_TRACE(_module_hal_init_c_, _drv_notice_,("TCR val=%x\n", tmp16));
			i--;			
		}
		
		if(i==0)
		{
			RT_TRACE(_module_hal_init_c_,_drv_err_,("Error => Pollin _FWRDY Fail\n"));
			ret8=_FAIL;
			goto exit;
		}
		else
		{
			RT_TRACE(_module_hal_init_c_, _drv_notice_,("Polling _FWRDY r_cnt=%d\n", i));
		}
#endif	
		
	}
	else
	{
		RT_TRACE(_module_hal_init_c_,_drv_err_,("rtl8712_dl_fw=> can't open fwfile\n"));
		ret8 = _FAIL;
	}

exit:
	rtl871x_close_fw(padapter, phfwfile_hdl);

	if (ptmpchar != NULL)
		_mfree(ptmpchar, maxlen + FWBUFF_ALIGN_SZ);

_func_exit_;

	return ret8;
}	

extern void hw_init(_adapter *padapter);
uint rtl8712_hal_init(_adapter *padapter)
{
	u32	val32;
	u8	val8;
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	struct eeprom_priv *peeprompriv = &padapter->eeprompriv;	 
	uint status = _SUCCESS;

_func_enter_;

	//r8712 firmware download
	RT_TRACE(_module_hal_init_c_, _drv_alert_, ("rtl8712_hal_init #1 == SYS_FUNC_EN:0x%08x\n", read8(padapter,0x10250003)));
	val8 = rtl8712_dl_fw( padapter);
	if (val8 == _FAIL){
		RT_TRACE(_module_hal_init_c_, _drv_err_, ("FW Download fail!\n"));
		status = _FAIL;
		goto exit;
	}
        RT_TRACE(_module_hal_init_c_, _drv_alert_, ("rtl8712_hal_init #2 == SYS_FUNC_EN:0x%08x\n", read8(padapter,0x10250003)));

	//register setting after firmware download
	//val32 = 0;
	//val32 = read32(padapter, RCR);//RCR
	//write32(padapter, RCR, (val32|BIT(0)));
	//RT_TRACE(_module_hal_init_c_,_drv_err_,("RCR=0x%x \n",  read32(padapter, RCR)));

#ifdef CONFIG_RTL8712_TCP_CSUM_OFFLOAD_RX
	printk("1 RCR=0x%x\n",  read32(padapter, RCR));
	val32 = read32(padapter, RCR);//RCR
	write32(padapter, RCR, (val32|BIT(26))); //Enable RX TCP Checksum offload
	RT_TRACE(_module_hal_init_c_,_drv_err_,("RCR=0x%x\n",  read32(padapter, RCR)));
	printk("2 RCR=0x%x \n",  read32(padapter, RCR));
#endif	

#ifdef CONFIG_RTL8712_TCP_CSUM_OFFLOAD_TX
	printk("1 TCR=0x%x \n",  read32(padapter, TCR));
	val32 = read32(padapter, TCR);
	write32(padapter, TCR, (val32|BIT(25))); //Enable TX TCP Checksum offload
	RT_TRACE(_module_hal_init_c_,_drv_err_,("TCR=0x%x \n",  read32(padapter, TCR)));
	printk("2 TCR=0x%x \n",  read32(padapter, TCR));
#endif	
	val32 = read32(padapter, RCR);
	write32(padapter, RCR, (val32|BIT(25))); //Append PHY status

	val32 = 0;
	val32 = read32(padapter, 0x10250040);
	write32(padapter,  0x10250040, (val32&0x00FFFFFF));

#ifdef CONFIG_SDIO_HCI
	write8(padapter, 0x10250006, 0x3B);
//	write8(padapter, 0x1025003a, 0x72);
	write8(padapter, 0x10250040, 0xFC);

	write8(padapter, TXPAUSE, 0x00); // 0x10250042

	write8(padapter, SDIO_DBG_SEL, 0x0); // RTL8712_SDIO_LOCAL_BASE + 0xFF

	// enable interrupt
	write16(padapter, SDIO_HIMR, 0xF);
	RT_TRACE(_module_hal_init_c_, _drv_debug_, ("write SDIO_HIMR 0xF\n"));
#endif

#ifdef CONFIG_USB_HCI
	//for usb rx aggregation	
	RT_TRACE(_module_hal_init_c_,_drv_debug_,("0x102500B5=0x%x\n", read8(padapter, 0x102500B5)));
	RT_TRACE(_module_hal_init_c_,_drv_debug_,("0x102500D9=0x%x\n", read8(padapter, 0x102500D9)));
	RT_TRACE(_module_hal_init_c_,_drv_debug_,("0x102500BD=0x%x\n", read8(padapter, 0x102500BD)));
	RT_TRACE(_module_hal_init_c_,_drv_debug_,("0x1025FE5B=0x%x\n", read8(padapter, 0x1025FE5B)));
	
	write8(padapter, 0x102500B5, read8(padapter, 0x102500B5)|BIT(0));//page = 128bytes
#ifdef CONFIG_USB_RX_AGGREGATION
	write8(padapter, 0x102500BD, read8(padapter, 0x102500BD)|BIT(7));//enable usb rx aggregation
#else
	write8(padapter, 0x102500BD, read8(padapter, 0x102500BD) & (~ BIT(7) ) );//disable usb rx aggregation
#endif //CONFIG_USB_RX_AGGREGATION
	//write8(padapter, 0x102500D9, 48);//TH = 48 pages, 6k
	write8(padapter, 0x102500D9, 1);// TH=1 => means that invalidate  usb rx aggregation
	//write8(padapter, 0x1025FE5B, 0x02);// 1.7ms/2
	write8(padapter, 0x1025FE5B, 0x04);// 1.7ms/4

	RT_TRACE(_module_hal_init_c_,_drv_debug_,("0x102500B5=0x%x\n", read8(padapter, 0x102500B5)));
	RT_TRACE(_module_hal_init_c_,_drv_debug_,("0x102500D9=0x%x\n", read8(padapter, 0x102500D9)));
	RT_TRACE(_module_hal_init_c_,_drv_debug_,("0x102500BD=0x%x\n", read8(padapter, 0x102500BD)));
	RT_TRACE(_module_hal_init_c_,_drv_debug_,("0x1025FE5B=0x%x\n", read8(padapter, 0x1025FE5B)));

	// Fix the RX FIFO issue(USB error), Rivesed by Roger, 2008-06-14
	val8 = read8(padapter, 0x1025fe5C);
	write8(padapter, 0x1025fe5C, val8|BIT(7));
#endif

	//read hw_mac address
	//val32 = read32(padapter, MACID);
	//_memcpy(padapter->eeprompriv.mac_addr, &val32, 4);
	//val32 = read32(padapter, MACID+4);
	//_memcpy(padapter->eeprompriv.mac_addr+4, &val32, 2);

	{
		int i;
		u8 val8;
		for (i = 0; i < 6; i++) {
			val8 = read8(padapter, MACID+i);
			padapter->eeprompriv.mac_addr[i] = val8;			
		}
#if 0
		printk("MAC Address = %x-%x-%x-%x-%x-%x\n", 
				 peeprompriv->mac_addr[0],	peeprompriv->mac_addr[1],
				 peeprompriv->mac_addr[2],	peeprompriv->mac_addr[3],
			peeprompriv->mac_addr[4],	peeprompriv->mac_addr[5]);
#endif
	}
				 
	RT_TRACE(_module_hal_init_c_, _drv_debug_,
		 ("MAC Address=%02x:%02x:%02x:%02x:%02x:%02x\n",
		  peeprompriv->mac_addr[0], peeprompriv->mac_addr[1], peeprompriv->mac_addr[2],
		  peeprompriv->mac_addr[3], peeprompriv->mac_addr[4], peeprompriv->mac_addr[5]));

exit:

_func_exit_;

	return status;
}

uint rtl8712_hal_deinit(_adapter *padapter)
{
#ifdef CONFIG_USB_HCI
	RT_TRACE(_module_hal_init_c_,_drv_info_,("+rtl8712_hal_deinit\n"));

	//write8(padapter, 0x1025004c, 0x0);

	//write hw_mac address back
	
	//write32(padapter, MACID, *((u32*)padapter->eeprompriv.mac_addr));
	
	//write32(padapter, MACID+4, *((u32*)(padapter->eeprompriv.mac_addr+4)));
	
	//write32(padapter, 0x102502ec, 0x00ff0000);
	
#if 0
	// Turn off RF
	PHY_SetRFReg(Adapter, (RF90_RADIO_PATH_E)RF90_PATH_A, 
					0x00, bMask20Bits, 0x00000);
#else
	write8(padapter, RF_CTRL, 0x00);
#endif

	// Turn off BB	
	//write8(padapter, CR+1, 0x07);
	mdelay_os(5);

	// Turn off MAC	
	//write8(padapter, SYS_CLKR+1, 0x78); // Switch Control Path
	write8(padapter, SYS_CLKR+1, 0x38); // Switch Control Path	
	write8(padapter, SYS_FUNC_EN+1, 0x70); // Reset MACTOP, IOREG, 4181
	write8(padapter, PMC_FSM, 0x06);  // Enable Loader Data Keep
	write8(padapter, SYS_ISO_CTRL, 0xF9); // Isolation signals from CORE, PLL
	write8(padapter, SYS_ISO_CTRL+1, 0xe8); // Enable EFUSE 1.2V(LDO) 
	//write8(padapter, SYS_ISO_CTRL+1, 0x69); // Isolation signals from Loader
	write8(padapter, AFE_PLL_CTRL, 0x00); // Disable AFE PLL.
	write8(padapter, LDOA15_CTRL, 0x54);  // Disable A15V
	write8(padapter, SYS_FUNC_EN+1, 0x50); // Disable E-Fuse 1.2V

	//write8(padapter, SPS1_CTRL, 0x64); // Disable LD12 & SW12 (for IT)
	write8(padapter, LDOV12D_CTRL, 0x24); // Disable LDO12(for CE)
	
	write8(padapter, AFE_MISC, 0x30); // Disable AFE BG&MB


	// <Roger_Notes> The following  options are alternative.
	// Disable 1.6V LDO or  1.8V Switch. 2008.09.26.

	// Option for Disable 1.6V LDO.	
	write8(padapter, SPS0_CTRL, 0x56); // Disable 1.6V LDO
	write8(padapter, SPS0_CTRL+1, 0x43);  // Set SW PFM 

	// Option for Disable 1.8V Switch.	
	//write8(padapter, SPS0_CTRL, 0x55); // Disable SW 1.8V
	
#if 0
	//
	// HCT12.0
	//
	for(idx = 0; idx < 6; idx++)
	{
		PlatformEFIOWrite1Byte(padapter, (IDR0+idx), Adapter->PermanentAddress[idx]);
	}
#endif

	RT_TRACE(_module_hal_init_c_,_drv_info_,("-rtl8712_hal_deinit, success!\n"));

	//NdisMDeregisterAdapterShutdownHandler(padapter->hndis_adapter);

	return _SUCCESS;
#endif

#ifdef CONFIG_SDIO_HCI
	u8 i = 60;

	write16(padapter, SDIO_HIMR, 0x00);	// Disable interrupt

	write8(padapter, RF_CTRL, 0x00);
	usleep_os(100);
	write8(padapter, SYS_CLKR+1, 0x38); // Switch Control Path
	while (((read8(padapter, SYS_CLKR+1) & 0x40) != BIT(6)) && (i > 0)) {
		msleep_os(10);
		i--;
	}

	write8(padapter, SYS_FUNC_EN+1, 0x70); // Reset MACTOP, IOREG, 4181

	write8(padapter, PMC_FSM, 0x06);  // Enable Loader Data Keep

	write8(padapter, SYS_ISO_CTRL, 0xFF); // Isolation signals from CORE, PLL

	write8(padapter, SYS_ISO_CTRL+1, 0xF6); // Enable EFUSE 1.2V(LDO) (E8)


	write8(padapter, AFE_PLL_CTRL, 0x00); // Disable AFE PLL.

	write8(padapter, LDOA15_CTRL, 0x54);  // Disable A15V

	write8(padapter, SYS_FUNC_EN+1, 0x50); // Disable E-Fuse 1.2V

	write8(padapter, AFE_MISC, 0x0); // Disable AFE BG&MB  (30)

	RT_TRACE(_module_hal_init_c_, _drv_notice_, ("-rtl8712_hal_deinit, success!\n"));

	return _SUCCESS;

#endif	
}

//#define _DO_SRAM_TEST_

#ifdef _DO_SRAM_TEST_

/*
BISR Test flow:
1. write 0x310[3] = 1, Mbist clock enable
2. write 0x310[11:8] = 0000: Mbist report select
    0x310[3:0] = 1011           : Mbist mode = BISR
3. wait mbisr done ( about 10ms)
4. Read 0x310[31:16] to check report
    0x310[25]: iram_bisr_done
    0x310[26]: iram_bisr_fail
    0x310[27]: iram_bisr_repaired
    0x310[28]: iram_bisr_out_diff
    0x310[29]: iram_bisr_unrepairable
0x310[25] = 1 and 0x310[26] = 0,SRAM bisr pass
5. write 0x310[3:0] =  0000 : leave mbisr mode
*/

#define BIST_REG 0x10250310

#define IRAM_BISR_DONE				BIT(25)
#define IRAM_BISR_FAIL				BIT(26)
#define IRAM_BISR_REPAIRED			BIT(27)
#define IRAM_BISR_OUT_DIFF			BIT(28)
#define IRAM_BISR_UNREPAIRABLE	BIT(29)

static uint iram_test(_adapter *padapter) 
{
	u32 val32;
	u32 report = 0;

	RT_TRACE(_module_hal_init_c_,_drv_alert_,("@@@@@ iram_test !!!!!\n"));	

	//step 1.Mbist clock enable,write 0x310[3] = 1
	val32 = read32(padapter, BIST_REG);	
	val32|= BIT(3);
	RT_TRACE(_module_hal_init_c_,_drv_alert_,("@@@@@ iram_test !!!!! #step 1 - Mbist clock enable (0x%08x)\n",val32));	
	write32(padapter,  BIST_REG, val32);


	//step 2.Mbist report select,write 0x310[11:8] = 0000
	val32 = read32(padapter, BIST_REG);	
	val32 &= 0xFFFFF0FF;
	RT_TRACE(_module_hal_init_c_,_drv_alert_,("@@@@@ iram_test !!!!! #step 2 - Mbist report select (0x%08x)\n",val32));	
	write32(padapter,  BIST_REG, val32);	


	//step 3.Mbist mode = BISR,0x310[3:0] = 1011
	val32 = read32(padapter, BIST_REG);	
	val32 &= 0xFFFFFFFB;
	RT_TRACE(_module_hal_init_c_,_drv_alert_,("@@@@@ iram_test !!!!! #step 3 -Mbist mode (0x%08x)\n",val32));	
	write32(padapter,  BIST_REG, val32);		

	msleep_os(100);//delay 10 mini seconds

	//step 4.Read 0x310[31:16] to check report
	val32 = read32(padapter, BIST_REG);
		
	report =(val32 & 0xFFFF0000);
	
	if((report& IRAM_BISR_DONE ) && (!(report&IRAM_BISR_FAIL))){ 
		RT_TRACE(_module_hal_init_c_,_drv_alert_,("@@@@@  Internal SRAM- bisr test pass !!!!! (0x%08x)\n",val32));
	}
	if(report&IRAM_BISR_FAIL){ 
		RT_TRACE(_module_hal_init_c_,_drv_alert_,("@@@@@  Internal SRAM- bisr test fail !!!!!! (0x%08x)\n",val32));		
	}
	if(report&IRAM_BISR_REPAIRED){
		RT_TRACE(_module_hal_init_c_,_drv_alert_,("@@@@@  Internal SRAM - bisr test fail - repaired !!!!! (0x%08x)\n",val32));
	}
	if(report&IRAM_BISR_OUT_DIFF){
		RT_TRACE(_module_hal_init_c_,_drv_alert_,("@@@@@  Internal SRAM  bisr test fail - out diff !!!!! (0x%08x)\n",val32));
	}
	if(report&IRAM_BISR_UNREPAIRABLE){
		RT_TRACE(_module_hal_init_c_,_drv_alert_,("@@@@@ Internal SRAM - bisr test fail - unrepairable !!!!! (0x%08x)\n",val32));		
	}
	RT_TRACE(_module_hal_init_c_,_drv_alert_,("@@@@@ iram_test !!!!! #step 4 - check report (0x%08x) \n",report));		

	//step 5.leave mbisr mode: write 0x310[3:0] = 0000 
	val32 = read32(padapter, BIST_REG);	
	val32  &= 0xFFFFFFF0;
	RT_TRACE(_module_hal_init_c_,_drv_alert_,("@@@@@ iram_test !!!!! #step 5 - leave mbisr mode  (0x%08x)\n",val32));	
	write32(padapter,  BIST_REG, val32);

	return _SUCCESS;
}
#endif

uint rtl871x_hal_init(_adapter *padapter)
{
	u8 val8;	 
	uint status = _SUCCESS;
	
	 padapter->hw_init_completed=_FALSE;
	if(padapter->halpriv.hal_bus_init ==NULL)
	{
		RT_TRACE(_module_hal_init_c_,_drv_err_,("\nInitialize halpriv.hal_bus_init error!!!\n"));
		status = _FAIL;
		goto exit;
	}
	else
	{
		val8=padapter->halpriv.hal_bus_init(padapter);
		if(val8==_FAIL)
		{
			RT_TRACE(_module_hal_init_c_,_drv_err_,("rtl871x_hal_init: hal_bus_init fail\n"));
			status= _FAIL;
			goto exit;
		}
	}

#ifdef _DO_SRAM_TEST_
	iram_test(padapter) ;	
#endif

	status = rtl8712_hal_init(padapter);
	if( status==_SUCCESS)
		padapter->hw_init_completed=_TRUE;
	else
		padapter->hw_init_completed=_FALSE;
exit:

	RT_TRACE(_module_hal_init_c_,_drv_err_,("-rtl871x_hal_init:status=0x%x\n",status));

	return status;

}	

uint rtl871x_hal_deinit(_adapter *padapter)
{
	u8	val8;
	uint	res=_SUCCESS;

_func_enter_;

	if (padapter->halpriv.hal_bus_deinit == NULL) {
		RT_TRACE(_module_hal_init_c_,_drv_err_,("\nInitialize halpriv.hal_bus_init error!!!\n"));
		res = _FAIL;
		goto exit;
	} else {
		val8=padapter->halpriv.hal_bus_deinit(padapter);

		if (val8 ==_FAIL) {
			RT_TRACE(_module_hal_init_c_,_drv_err_,("\n rtl871x_hal_init: hal_bus_init fail\n"));		
			res= _FAIL;
			goto exit;

		}
	}

	res = rtl8712_hal_deinit(padapter);

	padapter->hw_init_completed = _FALSE;

exit:

_func_exit_;
	
	return res;
}

