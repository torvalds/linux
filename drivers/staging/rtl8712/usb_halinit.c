// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 * usb_halinit.c
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
 * Linux device driver for RTL8192SU
 *
 * Modifications for inclusion into the Linux staging tree are
 * Copyright(c) 2010 Larry Finger. All rights reserved.
 *
 * Contact information:
 * WLAN FAE <wlanfae@realtek.com>
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 ******************************************************************************/

#define _HCI_HAL_INIT_C_

#include "osdep_service.h"
#include "drv_types.h"
#include "usb_ops.h"
#include "usb_osintf.h"

u8 r8712_usb_hal_bus_init(struct _adapter *adapter)
{
	u8 val8 = 0;
	u8 ret = _SUCCESS;
	int PollingCnt = 20;
	struct registry_priv *registrypriv = &adapter->registrypriv;

	if (registrypriv->chip_version == RTL8712_FPGA) {
		val8 = 0x01;
		/* switch to 80M clock */
		r8712_write8(adapter, SYS_CLKR, val8);
		val8 = r8712_read8(adapter, SPS1_CTRL);
		val8 = val8 | 0x01;
		/* enable VSPS12 LDO Macro block */
		r8712_write8(adapter, SPS1_CTRL, val8);
		val8 = r8712_read8(adapter, AFE_MISC);
		val8 = val8 | 0x01;
		/* Enable AFE Macro Block's Bandgap */
		r8712_write8(adapter, AFE_MISC, val8);
		val8 = r8712_read8(adapter, LDOA15_CTRL);
		val8 = val8 | 0x01;
		/* enable LDOA15 block */
		r8712_write8(adapter, LDOA15_CTRL, val8);
		val8 = r8712_read8(adapter, SPS1_CTRL);
		val8 = val8 | 0x02;
		/* Enable VSPS12_SW Macro Block */
		r8712_write8(adapter, SPS1_CTRL, val8);
		val8 = r8712_read8(adapter, AFE_MISC);
		val8 = val8 | 0x02;
		/* Enable AFE Macro Block's Mbias */
		r8712_write8(adapter, AFE_MISC, val8);
		val8 = r8712_read8(adapter, SYS_ISO_CTRL + 1);
		val8 = val8 | 0x08;
		/* isolate PCIe Analog 1.2V to PCIe 3.3V and PCIE Digital */
		r8712_write8(adapter, SYS_ISO_CTRL + 1, val8);
		val8 = r8712_read8(adapter, SYS_ISO_CTRL + 1);
		val8 = val8 & 0xEF;
		/* attatch AFE PLL to MACTOP/BB/PCIe Digital */
		r8712_write8(adapter, SYS_ISO_CTRL + 1, val8);
		val8 = r8712_read8(adapter, AFE_XTAL_CTRL + 1);
		val8 = val8 & 0xFB;
		/* enable AFE clock */
		r8712_write8(adapter, AFE_XTAL_CTRL + 1, val8);
		val8 = r8712_read8(adapter, AFE_PLL_CTRL);
		val8 = val8 | 0x01;
		/* Enable AFE PLL Macro Block */
		r8712_write8(adapter, AFE_PLL_CTRL, val8);
		val8 = 0xEE;
		/* release isolation AFE PLL & MD */
		r8712_write8(adapter, SYS_ISO_CTRL, val8);
		val8 = r8712_read8(adapter, SYS_CLKR + 1);
		val8 = val8 | 0x08;
		/* enable MAC clock */
		r8712_write8(adapter, SYS_CLKR + 1, val8);
		val8 = r8712_read8(adapter, SYS_FUNC_EN + 1);
		val8 = val8 | 0x08;
		/* enable Core digital and enable IOREG R/W */
		r8712_write8(adapter, SYS_FUNC_EN + 1, val8);
		val8 = val8 | 0x80;
		/* enable REG_EN */
		r8712_write8(adapter, SYS_FUNC_EN + 1, val8);
		val8 = r8712_read8(adapter, SYS_CLKR + 1);
		val8 = (val8 | 0x80) & 0xBF;
		/* switch the control path */
		r8712_write8(adapter, SYS_CLKR + 1, val8);
		val8 = 0xFC;
		r8712_write8(adapter, CR, val8);
		val8 = 0x37;
		r8712_write8(adapter, CR + 1, val8);
		/* reduce EndPoint & init it */
		r8712_write8(adapter, 0x102500ab, r8712_read8(adapter,
			     0x102500ab) | BIT(6) | BIT(7));
		/* consideration of power consumption - init */
		r8712_write8(adapter, 0x10250008, r8712_read8(adapter,
			     0x10250008) & 0xfffffffb);
	} else if (registrypriv->chip_version == RTL8712_1stCUT) {
		/* Initialization for power on sequence, */
		r8712_write8(adapter, SPS0_CTRL + 1, 0x53);
		r8712_write8(adapter, SPS0_CTRL, 0x57);
		/* Enable AFE Macro Block's Bandgap and Enable AFE Macro
		 * Block's Mbias
		 */
		val8 = r8712_read8(adapter, AFE_MISC);
		r8712_write8(adapter, AFE_MISC, (val8 | AFE_MISC_BGEN |
			     AFE_MISC_MBEN));
		/* Enable LDOA15 block */
		val8 = r8712_read8(adapter, LDOA15_CTRL);
		r8712_write8(adapter, LDOA15_CTRL, (val8 | LDA15_EN));
		val8 = r8712_read8(adapter, SPS1_CTRL);
		r8712_write8(adapter, SPS1_CTRL, (val8 | SPS1_LDEN));
		msleep(20);
		/* Enable Switch Regulator Block */
		val8 = r8712_read8(adapter, SPS1_CTRL);
		r8712_write8(adapter, SPS1_CTRL, (val8 | SPS1_SWEN));
		r8712_write32(adapter, SPS1_CTRL, 0x00a7b267);
		val8 = r8712_read8(adapter, SYS_ISO_CTRL + 1);
		r8712_write8(adapter, SYS_ISO_CTRL + 1, (val8 | 0x08));
		/* Engineer Packet CP test Enable */
		val8 = r8712_read8(adapter, SYS_FUNC_EN + 1);
		r8712_write8(adapter, SYS_FUNC_EN + 1, (val8 | 0x20));
		val8 = r8712_read8(adapter, SYS_ISO_CTRL + 1);
		r8712_write8(adapter, SYS_ISO_CTRL + 1, (val8 & 0x6F));
		/* Enable AFE clock */
		val8 = r8712_read8(adapter, AFE_XTAL_CTRL + 1);
		r8712_write8(adapter, AFE_XTAL_CTRL + 1, (val8 & 0xfb));
		/* Enable AFE PLL Macro Block */
		val8 = r8712_read8(adapter, AFE_PLL_CTRL);
		r8712_write8(adapter, AFE_PLL_CTRL, (val8 | 0x11));
		/* Attach AFE PLL to MACTOP/BB/PCIe Digital */
		val8 = r8712_read8(adapter, SYS_ISO_CTRL);
		r8712_write8(adapter, SYS_ISO_CTRL, (val8 & 0xEE));
		/* Switch to 40M clock */
		val8 = r8712_read8(adapter, SYS_CLKR);
		r8712_write8(adapter, SYS_CLKR, val8 & (~SYS_CLKSEL));
		/* SSC Disable */
		val8 = r8712_read8(adapter, SYS_CLKR);
		/* Enable MAC clock */
		val8 = r8712_read8(adapter, SYS_CLKR + 1);
		r8712_write8(adapter, SYS_CLKR + 1, (val8 | 0x18));
		/* Revised POS, */
		r8712_write8(adapter, PMC_FSM, 0x02);
		/* Enable Core digital and enable IOREG R/W */
		val8 = r8712_read8(adapter, SYS_FUNC_EN + 1);
		r8712_write8(adapter, SYS_FUNC_EN + 1, (val8 | 0x08));
		/* Enable REG_EN */
		val8 = r8712_read8(adapter, SYS_FUNC_EN + 1);
		r8712_write8(adapter, SYS_FUNC_EN + 1, (val8 | 0x80));
		/* Switch the control path to FW */
		val8 = r8712_read8(adapter, SYS_CLKR + 1);
		r8712_write8(adapter, SYS_CLKR + 1, (val8 | 0x80) & 0xBF);
		r8712_write8(adapter, CR, 0xFC);
		r8712_write8(adapter, CR + 1, 0x37);
		/* Fix the RX FIFO issue(usb error), */
		val8 = r8712_read8(adapter, 0x1025FE5c);
		r8712_write8(adapter, 0x1025FE5c, (val8 | BIT(7)));
		val8 = r8712_read8(adapter, 0x102500ab);
		r8712_write8(adapter, 0x102500ab, (val8 | BIT(6) | BIT(7)));
		/* For power save, used this in the bit file after 970621 */
		val8 = r8712_read8(adapter, SYS_CLKR);
		r8712_write8(adapter, SYS_CLKR, val8 & (~CPU_CLKSEL));
	} else if (registrypriv->chip_version == RTL8712_2ndCUT ||
		   registrypriv->chip_version == RTL8712_3rdCUT) {
		/* Initialization for power on sequence,
		 * E-Fuse leakage prevention sequence
		 */
		r8712_write8(adapter, 0x37, 0xb0);
		msleep(20);
		r8712_write8(adapter, 0x37, 0x30);
		/* Set control path switch to HW control and reset Digital Core,
		 * CPU Core and MAC I/O to solve FW download fail when system
		 * from resume sate.
		 */
		val8 = r8712_read8(adapter, SYS_CLKR + 1);
		if (val8 & 0x80) {
			val8 &= 0x3f;
			r8712_write8(adapter, SYS_CLKR + 1, val8);
		}
		val8 = r8712_read8(adapter, SYS_FUNC_EN + 1);
		val8 &= 0x73;
		r8712_write8(adapter, SYS_FUNC_EN + 1, val8);
		msleep(20);
		/* Revised POS, */
		/* Enable AFE Macro Block's Bandgap and Enable AFE Macro
		 * Block's Mbias
		 */
		r8712_write8(adapter, SPS0_CTRL + 1, 0x53);
		r8712_write8(adapter, SPS0_CTRL, 0x57);
		val8 = r8712_read8(adapter, AFE_MISC);
		/*Bandgap*/
		r8712_write8(adapter, AFE_MISC, (val8 | AFE_MISC_BGEN));
		r8712_write8(adapter, AFE_MISC, (val8 | AFE_MISC_BGEN |
			     AFE_MISC_MBEN | AFE_MISC_I32_EN));
		/* Enable PLL Power (LDOA15V) */
		val8 = r8712_read8(adapter, LDOA15_CTRL);
		r8712_write8(adapter, LDOA15_CTRL, (val8 | LDA15_EN));
		/* Enable LDOV12D block */
		val8 = r8712_read8(adapter, LDOV12D_CTRL);
		r8712_write8(adapter, LDOV12D_CTRL, (val8 | LDV12_EN));
		val8 = r8712_read8(adapter, SYS_ISO_CTRL + 1);
		r8712_write8(adapter, SYS_ISO_CTRL + 1, (val8 | 0x08));
		/* Engineer Packet CP test Enable */
		val8 = r8712_read8(adapter, SYS_FUNC_EN + 1);
		r8712_write8(adapter, SYS_FUNC_EN + 1, (val8 | 0x20));
		/* Support 64k IMEM */
		val8 = r8712_read8(adapter, SYS_ISO_CTRL + 1);
		r8712_write8(adapter, SYS_ISO_CTRL + 1, (val8 & 0x68));
		/* Enable AFE clock */
		val8 = r8712_read8(adapter, AFE_XTAL_CTRL + 1);
		r8712_write8(adapter, AFE_XTAL_CTRL + 1, (val8 & 0xfb));
		/* Enable AFE PLL Macro Block */
		val8 = r8712_read8(adapter, AFE_PLL_CTRL);
		r8712_write8(adapter, AFE_PLL_CTRL, (val8 | 0x11));
		/* Some sample will download fw failure. The clock will be
		 * stable with 500 us delay after reset the PLL
		 * TODO: When usleep is added to kernel, change next 3
		 * udelay(500) to usleep(500)
		 */
		udelay(500);
		r8712_write8(adapter, AFE_PLL_CTRL, (val8 | 0x51));
		udelay(500);
		r8712_write8(adapter, AFE_PLL_CTRL, (val8 | 0x11));
		udelay(500);
		/* Attach AFE PLL to MACTOP/BB/PCIe Digital */
		val8 = r8712_read8(adapter, SYS_ISO_CTRL);
		r8712_write8(adapter, SYS_ISO_CTRL, (val8 & 0xEE));
		/* Switch to 40M clock */
		r8712_write8(adapter, SYS_CLKR, 0x00);
		/* CPU Clock and 80M Clock SSC Disable to overcome FW download
		 * fail timing issue.
		 */
		val8 = r8712_read8(adapter, SYS_CLKR);
		r8712_write8(adapter, SYS_CLKR, (val8 | 0xa0));
		/* Enable MAC clock */
		val8 = r8712_read8(adapter, SYS_CLKR + 1);
		r8712_write8(adapter, SYS_CLKR + 1, (val8 | 0x18));
		/* Revised POS, */
		r8712_write8(adapter, PMC_FSM, 0x02);
		/* Enable Core digital and enable IOREG R/W */
		val8 = r8712_read8(adapter, SYS_FUNC_EN + 1);
		r8712_write8(adapter, SYS_FUNC_EN + 1, (val8 | 0x08));
		/* Enable REG_EN */
		val8 = r8712_read8(adapter, SYS_FUNC_EN + 1);
		r8712_write8(adapter, SYS_FUNC_EN + 1, (val8 | 0x80));
		/* Switch the control path to FW */
		val8 = r8712_read8(adapter, SYS_CLKR + 1);
		r8712_write8(adapter, SYS_CLKR + 1, (val8 | 0x80) & 0xBF);
		r8712_write8(adapter, CR, 0xFC);
		r8712_write8(adapter, CR + 1, 0x37);
		/* Fix the RX FIFO issue(usb error), 970410 */
		val8 = r8712_read8(adapter, 0x1025FE5c);
		r8712_write8(adapter, 0x1025FE5c, (val8 | BIT(7)));
		/* For power save, used this in the bit file after 970621 */
		val8 = r8712_read8(adapter, SYS_CLKR);
		r8712_write8(adapter, SYS_CLKR, val8 & (~CPU_CLKSEL));
		/* Revised for 8051 ROM code wrong operation. */
		r8712_write8(adapter, 0x1025fe1c, 0x80);
		/* To make sure that TxDMA can ready to download FW.
		 * We should reset TxDMA if IMEM RPT was not ready.
		 */
		do {
			val8 = r8712_read8(adapter, TCR);
			if ((val8 & _TXDMA_INIT_VALUE) == _TXDMA_INIT_VALUE)
				break;
			udelay(5); /* PlatformStallExecution(5); */
		} while (PollingCnt--);	/* Delay 1ms */

		if (PollingCnt <= 0) {
			val8 = r8712_read8(adapter, CR);
			r8712_write8(adapter, CR, val8 & (~_TXDMA_EN));
			udelay(2); /* PlatformStallExecution(2); */
			/* Reset TxDMA */
			r8712_write8(adapter, CR, val8 | _TXDMA_EN);
		}
	} else {
		ret = _FAIL;
	}
	return ret;
}

unsigned int r8712_usb_inirp_init(struct _adapter *adapter)
{
	u8 i;
	struct recv_buf *recvbuf;
	struct intf_hdl *intfhdl = &adapter->pio_queue->intf;
	struct recv_priv *recvpriv = &(adapter->recvpriv);

	recvpriv->ff_hwaddr = RTL8712_DMA_RX0FF; /* mapping rx fifo address */
	/* issue Rx irp to receive data */
	recvbuf = (struct recv_buf *)recvpriv->precv_buf;
	for (i = 0; i < NR_RECVBUFF; i++) {
		if (r8712_usb_read_port(intfhdl, recvpriv->ff_hwaddr, 0,
					(unsigned char *)recvbuf) == false)
			return _FAIL;
		recvbuf++;
		recvpriv->free_recv_buf_queue_cnt--;
	}
	return _SUCCESS;
}

unsigned int r8712_usb_inirp_deinit(struct _adapter *adapter)
{
	r8712_usb_read_port_cancel(adapter);
	return _SUCCESS;
}
