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
#define _HCI_HAL_INIT_C_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <hal_init.h>

#if defined (PLATFORM_LINUX) && defined (PLATFORM_WINDOWS)

#error "Shall be Linux or Windows, but not both!\n"

#endif

#ifndef CONFIG_USB_HCI

#error "CONFIG_USB_HCI shall be on!\n"

#endif


#include <usb_ops.h>
#include <usb_hal.h>
#include <usb_osintf.h>


u8 usb_hal_bus_init(_adapter * padapter)
{
	u8	val8 = 0;
	u8	ret;
	u8			PollingCnt = 20;
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	
	ret =_SUCCESS;

	RT_TRACE(_module_hci_hal_init_c_, _drv_info_,("chip_version=%d\n", pregistrypriv->chip_version));	
	
	//pregistrypriv->chip_version = RTL8712_2ndCUT;//RTL8712_1stCUT;

	
	if(pregistrypriv->chip_version == RTL8712_FPGA)
	{
		val8 = 0x01;
		write8(padapter, SYS_CLKR, val8);//switch to 80M clock

		val8 = read8(padapter, SPS1_CTRL);
		val8 = val8 |0x01;
		write8(padapter, SPS1_CTRL, val8);//enable VSPS12 LDO Macro block

		val8 = read8(padapter, AFE_MISC);
		val8 = val8 |0x01;
		write8(padapter, AFE_MISC, val8);//Enable AFE Macro Block's Bandgap

		val8 = read8(padapter, LDOA15_CTRL);
		val8 = val8 |0x01;
		write8(padapter, LDOA15_CTRL, val8);//enable LDOA15 block

		val8 = read8(padapter, SPS1_CTRL);
		val8 = val8 |0x02;
		write8(padapter, SPS1_CTRL, val8);//Enable VSPS12_SW Macro Block

		val8 = read8(padapter, AFE_MISC);
		val8 = val8 |0x02;
		write8(padapter, AFE_MISC, val8);//Enable AFE Macro Block's Mbias


		val8 = read8(padapter, SYS_ISO_CTRL+1);
		val8 = val8 |0x08;
		write8(padapter, SYS_ISO_CTRL+1, val8);//isolate PCIe Analog 1.2V to PCIe 3.3V and PCIE Digital

		val8 = read8(padapter, SYS_ISO_CTRL+1);
		val8 = val8 & 0xEF;
		write8(padapter, SYS_ISO_CTRL+1, val8);//attatch AFE PLL to MACTOP/BB/PCIe Digital


		val8 = read8(padapter, AFE_XTAL_CTRL+1);
		val8 = val8 & 0xFB;
		write8(padapter, AFE_XTAL_CTRL+1, val8);//enable AFE clock

		val8 = read8(padapter, AFE_PLL_CTRL);
		val8 = val8 |0x01;
		write8(padapter, AFE_PLL_CTRL, val8);//Enable AFE PLL Macro Block

	
		val8 = 0xEE;
		write8(padapter, SYS_ISO_CTRL, val8);//release isolation AFE PLL & MD

		val8 = read8(padapter, SYS_CLKR+1);
		val8 = val8 |0x08;
		write8(padapter, SYS_CLKR+1, val8);//enable MAC clock

		val8 = read8(padapter, SYS_FUNC_EN+1);
		val8 = val8 |0x08;
		write8(padapter, SYS_FUNC_EN+1, val8);//enable Core digital and enable IOREG R/W

		val8 = val8 |0x80;
		write8(padapter, SYS_FUNC_EN+1, val8);//enable REG_EN
		
	
		val8 = read8(padapter, SYS_CLKR+1);
		val8 = (val8 |0x80)&0xBF;
		write8(padapter, SYS_CLKR + 1, val8);//switch the control path


		val8 = 0xFC;
		write8(padapter, CR, val8);	

		val8 = 0x37;
		write8(padapter, CR+1, val8);		

#define USE_SIX_USB_ENDPOINT		
#ifdef USE_SIX_USB_ENDPOINT
		//reduce EndPoint & init it
    	   	write8(padapter, 0x102500ab, read8(padapter, 0x102500ab)|BIT(6)|BIT(7));
#endif

		//consideration of power consumption - init
		write8(padapter, 0x10250008, read8(padapter, 0x10250008)&0xfffffffb);       

	

	}
	else if(pregistrypriv->chip_version == RTL8712_1stCUT)
	{
		//Initialization for power on sequence, Revised by Roger. 2008.09.03.

		//Revised POS, suggested by SD1 Alex, 2008.09.27.
		write8(padapter, SPS0_CTRL+1, 0x53);
		write8(padapter, SPS0_CTRL, 0x57);

		//Enable AFE Macro Block's Bandgap adn Enable AFE Macro Block's Mbias
		val8 = read8(padapter, AFE_MISC);	
		write8(padapter, AFE_MISC, (val8|AFE_MISC_BGEN|AFE_MISC_MBEN));

		//Enable LDOA15 block
		val8 = read8(padapter, LDOA15_CTRL);	
		write8(padapter, LDOA15_CTRL, (val8|LDA15_EN));

		val8 = read8(padapter, SPS1_CTRL);	
		write8(padapter, SPS1_CTRL, (val8|SPS1_LDEN));

		msleep_os(2);

		//Enable Switch Regulator Block
		val8 = read8(padapter, SPS1_CTRL);	
		write8(padapter, SPS1_CTRL, (val8|SPS1_SWEN));

		write32(padapter, SPS1_CTRL, 0x00a7b267);//?
 	
		val8 = read8(padapter, SYS_ISO_CTRL+1);	
		write8(padapter, SYS_ISO_CTRL+1, (val8|0x08));

		//Engineer Packet CP test Enable
		val8 = read8(padapter, SYS_FUNC_EN+1);	
		write8(padapter, SYS_FUNC_EN+1, (val8|0x20));

		val8 = read8(padapter, SYS_ISO_CTRL+1);	
		write8(padapter, SYS_ISO_CTRL+1, (val8& 0x6F));

		//Enable AFE clock
		val8 = read8(padapter, AFE_XTAL_CTRL+1);	
		write8(padapter, AFE_XTAL_CTRL+1, (val8& 0xfb));

		//Enable AFE PLL Macro Block
		val8 = read8(padapter, AFE_PLL_CTRL);	
		write8(padapter, AFE_PLL_CTRL, (val8|0x11));

		//Attatch AFE PLL to MACTOP/BB/PCIe Digital
		val8 = read8(padapter, SYS_ISO_CTRL);	
		write8(padapter, SYS_ISO_CTRL, (val8&0xEE));

		// Switch to 40M clock
		val8 = read8(padapter, SYS_CLKR);
		write8(padapter, SYS_CLKR, val8 & (~ SYS_CLKSEL));

		//SSC Disable
		val8 = read8(padapter, SYS_CLKR);	
		//write8(padapter, SYS_CLKR, (val8&0x5f));

		//Enable MAC clock
		val8 = read8(padapter, SYS_CLKR+1);	
		write8(padapter, SYS_CLKR+1, (val8|0x18));

		//Revised POS, suggested by SD1 Alex, 2008.09.27.
		write8(padapter, PMC_FSM, 0x02);
	
		//Enable Core digital and enable IOREG R/W
		val8 = read8(padapter, SYS_FUNC_EN+1);	
		write8(padapter, SYS_FUNC_EN+1, (val8|0x08));

		//Enable REG_EN
		val8 = read8(padapter, SYS_FUNC_EN+1);	
		write8(padapter, SYS_FUNC_EN+1, (val8|0x80));

		//Switch the control path to FW
		val8 = read8(padapter, SYS_CLKR+1);	
		write8(padapter, SYS_CLKR+1, (val8|0x80)& 0xBF);

		write8(padapter, CR, 0xFC);	
		
		write8(padapter, CR+1, 0x37);	

		//Fix the RX FIFO issue(usb error), 970410
		val8 = read8(padapter, 0x1025FE5c);	
		write8(padapter, 0x1025FE5c, (val8|BIT(7)));

#define USE_SIX_USB_ENDPOINT		
#ifdef USE_SIX_USB_ENDPOINT
		val8 = read8(padapter, 0x102500ab);	
		write8(padapter, 0x102500ab, (val8|BIT(6)|BIT(7)));
#endif

	 	//For power save, used this in the bit file after 970621
		val8 = read8(padapter, SYS_CLKR);	
		write8(padapter, SYS_CLKR, val8&(~CPU_CLKSEL));

	}
	else if(pregistrypriv->chip_version == RTL8712_2ndCUT || pregistrypriv->chip_version == RTL8712_3rdCUT)
	{
		//Initialization for power on sequence, Revised by Roger. 2008.09.03.

		//E-Fuse leakage prevention sequence
		write8(padapter, 0x37, 0xb0);
		msleep_os(10);
		write8(padapter, 0x37, 0x30);
		

		//
		//<Roger_Notes> Set control path switch to HW control and reset Digital Core,  CPU Core and 
		// MAC I/O to solve FW download fail when system from resume sate.
		// 2008.11.04.
		//
		val8 = read8(padapter, SYS_CLKR+1);
		//DbgPrint("SYS_CLKR+1=0x%x\n", val8);
		if(val8 & 0x80)
		{
       			val8 &= 0x3f;
              		write8(padapter, SYS_CLKR+1, val8);
		}
	   
      		val8 = read8(padapter, SYS_FUNC_EN+1);
		val8 &= 0x73;
		write8(padapter, SYS_FUNC_EN+1, val8);
		udelay_os(1000);

		//Revised POS, suggested by SD1 Alex, 2008.09.27.
		write8(padapter, SPS0_CTRL+1, 0x53); // Switching 18V to PWM.

		write8(padapter, SPS0_CTRL, 0x57);

		//Enable AFE Macro Block's Bandgap adn Enable AFE Macro Block's Mbias
		val8 = read8(padapter, AFE_MISC);	
		write8(padapter, AFE_MISC, (val8|AFE_MISC_BGEN)); //Bandgap
		//write8(padapter, AFE_MISC, (val8|AFE_MISC_BGEN|AFE_MISC_MBEN));
		write8(padapter, AFE_MISC, (val8|AFE_MISC_BGEN|AFE_MISC_MBEN | AFE_MISC_I32_EN)); //Mbios
		
		//Enable PLL Power (LDOA15V)
		val8 = read8(padapter, LDOA15_CTRL);	
		write8(padapter, LDOA15_CTRL, (val8|LDA15_EN));

		//Enable LDOV12D block
		val8 = read8(padapter, LDOV12D_CTRL);	
		write8(padapter, LDOV12D_CTRL, (val8|LDV12_EN));	
	
		val8 = read8(padapter, SYS_ISO_CTRL+1);	
		write8(padapter, SYS_ISO_CTRL+1, (val8|0x08));

		//Engineer Packet CP test Enable
		val8 = read8(padapter, SYS_FUNC_EN+1);	
		write8(padapter, SYS_FUNC_EN+1, (val8|0x20));

		//Support 64k IMEM, suggested by SD1 Alex.
		val8 = read8(padapter, SYS_ISO_CTRL+1);	
		write8(padapter, SYS_ISO_CTRL+1, (val8&0x68));

		//Enable AFE clock
		val8 = read8(padapter, AFE_XTAL_CTRL+1);	
		write8(padapter, AFE_XTAL_CTRL+1, (val8& 0xfb));

		//Enable AFE PLL Macro Block
		val8 = read8(padapter, AFE_PLL_CTRL);	
		write8(padapter, AFE_PLL_CTRL, (val8|0x11));

		//(20090928) for some sample will download fw failure
		// Added comment by Albert 2010/02/24
		// The clock will be stable with 500 us delay after reset the PLL
		udelay_os(500);
		write8(padapter, AFE_PLL_CTRL, (val8|0x51));
		udelay_os(500);
		write8(padapter, AFE_PLL_CTRL, (val8|0x11));
		udelay_os(500);		

		//Attatch AFE PLL to MACTOP/BB/PCIe Digital
		val8 = read8(padapter, SYS_ISO_CTRL);	
		write8(padapter, SYS_ISO_CTRL, (val8&0xEE));

		// Switch to 40M clock
		write8(padapter, SYS_CLKR, 0x00);

		//CPU Clock and 80M Clock SSC Disable to overcome FW download fail timing issue.
		val8 = read8(padapter, SYS_CLKR);	
		write8(padapter, SYS_CLKR, (val8|0xa0));

		//Enable MAC clock
		val8 = read8(padapter, SYS_CLKR+1);	
		write8(padapter, SYS_CLKR+1, (val8|0x18));

		//Revised POS, suggested by SD1 Alex, 2008.09.27.
		write8(padapter, PMC_FSM, 0x02);
	
		//Enable Core digital and enable IOREG R/W
		val8 = read8(padapter, SYS_FUNC_EN+1);	
		write8(padapter, SYS_FUNC_EN+1, (val8|0x08));

		//Enable REG_EN
		val8 = read8(padapter, SYS_FUNC_EN+1);	
		write8(padapter, SYS_FUNC_EN+1, (val8|0x80));

		//Switch the control path to FW
		val8 = read8(padapter, SYS_CLKR+1);	
		write8(padapter, SYS_CLKR+1, (val8|0x80)& 0xBF);

		write8(padapter, CR, 0xFC);	
		write8(padapter, CR+1, 0x37);	

		//Fix the RX FIFO issue(usb error), 970410
		val8 = read8(padapter, 0x1025FE5c);	
		write8(padapter, 0x1025FE5c, (val8|BIT(7)));

#if 0	//fw will help set it depending on the fwpriv.
#define USE_SIX_USB_ENDPOINT		
#ifdef USE_SIX_USB_ENDPOINT
		val8 = read8(padapter, 0x102500ab);	
		write8(padapter, 0x102500ab, (val8|BIT(6)|BIT(7)));
#endif
#endif
	 	//For power save, used this in the bit file after 970621
		val8 = read8(padapter, SYS_CLKR);	
		write8(padapter, SYS_CLKR, val8&(~CPU_CLKSEL));

		// Revised for 8051 ROM code wrong operation. Added by Roger. 2008.10.16. 
		write8(padapter, 0x1025fe1c, 0x80);

		//
		// <Roger_EXP> To make sure that TxDMA can ready to download FW.
		// We should reset TxDMA if IMEM RPT was not ready.
		// Suggested by SD1 Alex. 2008.10.23.
		//
		do
		{
			val8 = read8(padapter, TCR);
			if((val8 & _TXDMA_INIT_VALUE) == _TXDMA_INIT_VALUE)
				break;	
			
			udelay_os(5);//PlatformStallExecution(5);
		}while(PollingCnt--);	// Delay 1ms
	
		if(PollingCnt <= 0 )
		{
			//ERR_8712("MacConfigBeforeFwDownloadASIC(): Polling _TXDMA_INIT_VALUE timeout!! Current TCR(%#x)\n", val8);

			val8 = read8(padapter, CR);	
			
			write8(padapter, CR, val8&(~_TXDMA_EN));
			
			udelay_os(2);//PlatformStallExecution(2);
			
			write8(padapter, CR, val8|_TXDMA_EN);// Reset TxDMA
		}
	}
	else
	{
		ret = _FAIL;
	}
	
	return ret;

}
 u8 usb_hal_bus_deinit(_adapter * padapter)
 {

_func_enter_;

_func_exit_;
	
	return _SUCCESS;
 }

unsigned int usb_inirp_init(_adapter * padapter)
{	
	u8 i;	
	struct recv_buf *precvbuf;
	uint	status;
	struct dvobj_priv *pdev=&padapter->dvobjpriv;
	struct intf_hdl * pintfhdl=&padapter->pio_queue->intf;
	struct recv_priv *precvpriv = &(padapter->recvpriv);

_func_enter_;

	status = _SUCCESS;

	RT_TRACE(_module_hci_hal_init_c_,_drv_info_,("===> usb_inirp_init \n"));	
		
	precvpriv->ff_hwaddr = RTL8712_DMA_RX0FF;//mapping rx fifo address
	
	//issue Rx irp to receive data	
	precvbuf = (struct recv_buf *)precvpriv->precv_buf;	
	for(i=0; i<NR_RECVBUFF; i++)
	{
		if(usb_read_port(pintfhdl, precvpriv->ff_hwaddr, 0, (unsigned char *)precvbuf) == _FALSE )
		{
			RT_TRACE(_module_hci_hal_init_c_,_drv_err_,("usb_rx_init: usb_read_port error \n"));
			status = _FAIL;
			goto exit;
		}
		
		precvbuf++;		
		precvpriv->free_recv_buf_queue_cnt--;
	}
	
		
exit:
	
	RT_TRACE(_module_hci_hal_init_c_,_drv_info_,("<=== usb_inirp_init \n"));

_func_exit_;

	return status;

}

unsigned int usb_inirp_deinit(_adapter * padapter)
{	
	RT_TRACE(_module_hci_hal_init_c_,_drv_info_,("\n ===> usb_rx_deinit \n"));
	
	usb_read_port_cancel(padapter);


	RT_TRACE(_module_hci_hal_init_c_,_drv_info_,("\n <=== usb_rx_deinit \n"));

	return _SUCCESS;
}

