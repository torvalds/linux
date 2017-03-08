/******************************************************************************
 *
 * Copyright(c) 2015 - 2016 Realtek Corporation. All rights reserved.
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

#define _HCI_OPS_OS_C_

#include <drv_types.h>		/* PADAPTER, basic_types.h and etc. */
#include <hal_data.h>		/* HAL_DATA_TYPE, GET_HAL_DATA() and etc. */
#include <hal_intf.h>		/* struct hal_ops */
#include "../rtl8822b.h"
#include "rtl8822be.h"

static void init_bd_ring_var(_adapter *padapter)
{
	struct recv_priv *r_priv = &padapter->recvpriv;
	struct xmit_priv *t_priv = &padapter->xmitpriv;
	u8 i = 0;

	for (i = 0; i < HW_QUEUE_ENTRY; i++)
		t_priv->txringcount[i] = TX_BD_NUM_8822BE;

	/*
	 * we just alloc 2 desc for beacon queue,
	 * because we just need first desc in hw beacon.
	 */
	t_priv->txringcount[BCN_QUEUE_INX] = TX_BD_NUM_8822BE_BCN;
	t_priv->txringcount[TXCMD_QUEUE_INX] = TX_BD_NUM_8822BE_CMD;

	/*
	 * BE queue need more descriptor for performance consideration
	 * or, No more tx desc will happen, and may cause mac80211 mem leakage.
	 */
	r_priv->rxbuffersize = MAX_RECVBUF_SZ;
	r_priv->rxringcount = PCI_MAX_RX_COUNT;
}

static void rtl8822be_reset_bd(_adapter *padapter)
{
	_irqL	irqL;
	struct xmit_priv *t_priv = &padapter->xmitpriv;
	struct recv_priv *r_priv = &padapter->recvpriv;
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(padapter);
	struct xmit_buf	*pxmitbuf = NULL;
	u8 *tx_bd, *rx_bd;
	int i, rx_queue_idx;

	for (rx_queue_idx = 0; rx_queue_idx < 1; rx_queue_idx++) {
		if (r_priv->rx_ring[rx_queue_idx].buf_desc) {
			rx_bd = NULL;
			for (i = 0; i < r_priv->rxringcount; i++) {
				rx_bd = (u8 *)
					&r_priv->rx_ring[rx_queue_idx].buf_desc[i];
			}
			r_priv->rx_ring[rx_queue_idx].idx = 0;
		}
	}

	_enter_critical(&pdvobjpriv->irq_th_lock, &irqL);
	for (i = 0; i < PCI_MAX_TX_QUEUE_COUNT; i++) {
		if (t_priv->tx_ring[i].buf_desc) {
			struct rtw_tx_ring *ring = &t_priv->tx_ring[i];

			while (ring->qlen) {
				tx_bd = (u8 *)(&ring->buf_desc[ring->idx]);
				SET_TX_BD_OWN(tx_bd, 0);

				if (i != BCN_QUEUE_INX)
					ring->idx =
						(ring->idx + 1) % ring->entries;

				pxmitbuf = rtl8822be_dequeue_xmitbuf(ring);
				if (pxmitbuf) {
					pci_unmap_single(pdvobjpriv->ppcidev,
						GET_TX_BD_PHYSICAL_ADDR0_LOW(tx_bd),
						pxmitbuf->len, PCI_DMA_TODEVICE);
					rtw_free_xmitbuf(t_priv, pxmitbuf);
				} else {
					RTW_INFO("%s(): qlen(%d) is not zero, but have xmitbuf in pending queue\n",
						 __func__, ring->qlen);
					break;
				}
			}
			ring->idx = 0;
		}
	}
	_exit_critical(&pdvobjpriv->irq_th_lock, &irqL);
}

static void intf_chip_configure(PADAPTER Adapter)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(Adapter);
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(Adapter);
	struct pwrctrl_priv *pwrpriv = dvobj_to_pwrctl(pdvobjpriv);


	/* close ASPM for AMD defaultly */
	pdvobjpriv->const_amdpci_aspm = 0;

	/* ASPM PS mode. */
	/* 0 - Disable ASPM, 1 - Enable ASPM without Clock Req, */
	/* 2 - Enable ASPM with Clock Req, 3- Alwyas Enable ASPM with Clock Req, */
	/* 4-  Always Enable ASPM without Clock Req. */
	/* set defult to rtl8188ee:3 RTL8192E:2 */
	pdvobjpriv->const_pci_aspm = 0;

	/* Setting for PCI-E device */
	pdvobjpriv->const_devicepci_aspm_setting = 0x03;

	/* Setting for PCI-E bridge */
	pdvobjpriv->const_hostpci_aspm_setting = 0x03;

	/* In Hw/Sw Radio Off situation. */
	/* 0 - Default, 1 - From ASPM setting without low Mac Pwr, */
	/* 2 - From ASPM setting with low Mac Pwr, 3 - Bus D3 */
	/* set default to RTL8192CE:0 RTL8192SE:2 */
	pdvobjpriv->const_hwsw_rfoff_d3 = 0;

	/* This setting works for those device with backdoor ASPM setting such as EPHY setting. */
	/* 0: Not support ASPM, 1: Support ASPM, 2: According to chipset. */
	pdvobjpriv->const_support_pciaspm = 1;

	pwrpriv->reg_rfoff = 0;
	pwrpriv->rfoff_reason = 0;

	pHalData->bL1OffSupport = _FALSE;
}

/*
 * Description:
 *	Collect all hardware information, fill "HAL_DATA_TYPE".
 *	Sometimes this would be used to read MAC address.
 *	This function will do
 *	1. Read Efuse/EEPROM to initialize
 *	2. Read registers to initialize
 *	3. Other vaiables initialization
 */
static u8 read_adapter_info(PADAPTER padapter)
{
	/*
	 * 1. Read Efuse/EEPROM to initialize
	 */
	if (rtl8822b_read_efuse(padapter) == _FAIL)
		return _FAIL;

	/*
	 * 2. Read registers to initialize
	 */

	/*
	 * 3. Other Initialization
	 */
	return _SUCCESS;
}

#ifndef CONFIG_NAPI
static BOOLEAN rtl8822be_InterruptRecognized(PADAPTER Adapter)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(Adapter);
	BOOLEAN bRecognized = _FALSE;

	/* 2013.11.18 Glayrainx suggests that turn off IMR and
	 * restore after cleaning ISR.
	 */
	rtw_write32(Adapter, REG_HIMR0, 0);
	rtw_write32(Adapter, REG_HIMR1, 0);
	rtw_write32(Adapter, REG_HIMR3, 0);

	pHalData->IntArray[0] = rtw_read32(Adapter, REG_HISR0);
	pHalData->IntArray[0] &= pHalData->IntrMask[0];
	rtw_write32(Adapter, REG_HISR0, pHalData->IntArray[0]);

	/* For HISR extension. Added by tynli. 2009.10.07. */
	pHalData->IntArray[1] = rtw_read32(Adapter, REG_HISR1);
	pHalData->IntArray[1] &= pHalData->IntrMask[1];
	rtw_write32(Adapter, REG_HISR1, pHalData->IntArray[1]);

	/* for H2C cmd queue */
	pHalData->IntArray[3] = rtw_read32(Adapter, REG_HISR3);
	pHalData->IntArray[3] &= pHalData->IntrMask[3];
	rtw_write32(Adapter, REG_HISR3, pHalData->IntArray[3]);

	if (((pHalData->IntArray[0]) & pHalData->IntrMask[0]) != 0 ||
	    ((pHalData->IntArray[1]) & pHalData->IntrMask[1]) != 0)
		bRecognized = _TRUE;

	/* restore IMR */
	rtw_write32(Adapter, REG_HIMR0, pHalData->IntrMask[0] & 0xFFFFFFFF);
	rtw_write32(Adapter, REG_HIMR1, pHalData->IntrMask[1] & 0xFFFFFFFF);
	rtw_write32(Adapter, REG_HIMR3, pHalData->IntrMask[3] & 0xFFFFFFFF);

	return bRecognized;
}
#endif

static VOID DisableInterrupt8822be(PADAPTER Adapter)
{
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(Adapter);

	rtw_write32(Adapter, REG_HIMR0, 0x0);
	rtw_write32(Adapter, REG_HIMR1, 0x0);
	rtw_write32(Adapter, REG_HIMR3, 0x0);
	pdvobjpriv->irq_enabled = 0;
}

static VOID rtl8822be_enable_interrupt(PADAPTER Adapter)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(Adapter);
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(Adapter);

	pdvobjpriv->irq_enabled = 1;

	rtw_write32(Adapter, REG_HIMR0, pHalData->IntrMask[0] & 0xFFFFFFFF);
	rtw_write32(Adapter, REG_HIMR1, pHalData->IntrMask[1] & 0xFFFFFFFF);
	rtw_write32(Adapter, REG_HIMR3, pHalData->IntrMask[3] & 0xFFFFFFFF);

}

static VOID rtl8822be_clear_interrupt(PADAPTER Adapter)
{
	u32 u32b;
	HAL_DATA_TYPE   *pHalData = GET_HAL_DATA(Adapter);

	u32b = rtw_read32(Adapter, REG_HISR0_8822B);
	rtw_write32(Adapter, REG_HISR0_8822B, u32b);
	pHalData->IntArray[0] = 0;

	u32b = rtw_read32(Adapter, REG_HISR1_8822B);
	rtw_write32(Adapter, REG_HISR1_8822B, u32b);
	pHalData->IntArray[1] = 0;
}

static VOID rtl8822be_disable_interrupt(PADAPTER Adapter)
{
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(Adapter);

	rtw_write32(Adapter, REG_HIMR0, 0x0);
	rtw_write32(Adapter, REG_HIMR1, 0x0);	/* by tynli */
	pdvobjpriv->irq_enabled = 0;
}

VOID UpdateInterruptMask8822BE(PADAPTER Adapter, u32 AddMSR, u32 AddMSR1,
			       u32 RemoveMSR, u32 RemoveMSR1)
{
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(Adapter);

	DisableInterrupt8822be(Adapter);

	if (AddMSR)
		pHalData->IntrMask[0] |= AddMSR;

	if (AddMSR1)
		pHalData->IntrMask[1] |= AddMSR1;

	if (RemoveMSR)
		pHalData->IntrMask[0] &= (~RemoveMSR);

	if (RemoveMSR1)
		pHalData->IntrMask[1] &= (~RemoveMSR1);

#if 0 /* TODO */
	if (RemoveMSR3)
		pHalData->IntrMask[3] &= (~RemoveMSR3);
#endif

	rtl8822be_enable_interrupt(Adapter);
}

static void rtl8822be_bcn_handler(PADAPTER Adapter, u32 handled[])
{
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(Adapter);

	if (pHalData->IntArray[0] & BIT_TXBCN0OK_MSK) {
		DBG_COUNTER(Adapter->int_logs.tbdok);
#ifdef CONFIG_BCN_ICF
		/* do nothing */
#else
		/* Modify for MI temporary,
		 * this processor cannot apply to multi-ap */
		PADAPTER bcn_adapter = rtw_mi_get_ap_adapter(Adapter);

		if (bcn_adapter->xmitpriv.beaconDMAing) {
			bcn_adapter->xmitpriv.beaconDMAing = _FAIL;
			rtl8822be_tx_isr(Adapter, BCN_QUEUE_INX);
		}
#endif /* CONFIG_BCN_ICF */
		handled[0] |= BIT_TXBCN0OK_MSK;
	}

	if (pHalData->IntArray[0] & BIT_TXBCN0ERR_MSK) {
		DBG_COUNTER(Adapter->int_logs.tbder);
#ifdef CONFIG_BCN_ICF
		RTW_INFO("IMR_TXBCN0ERR isr!\n");
#else /* !CONFIG_BCN_ICF */
		/* Modify for MI temporary,
		 * this processor cannot apply to multi-ap */
		PADAPTER bcn_adapter = rtw_mi_get_ap_adapter(Adapter);

		if (bcn_adapter->xmitpriv.beaconDMAing) {
			bcn_adapter->xmitpriv.beaconDMAing = _FAIL;
			rtl8822be_tx_isr(Adapter, BCN_QUEUE_INX);
		}
#endif /* CONFIG_BCN_ICF */
		handled[0] |= BIT_TXBCN0ERR_MSK;
	}

	if (pHalData->IntArray[0] & BIT_BCNDERR0_MSK) {
		DBG_COUNTER(Adapter->int_logs.bcnderr);
#ifdef CONFIG_BCN_ICF
		RTW_INFO("BIT_BCNDERR0_MSK isr!\n");
#else /* !CONFIG_BCN_ICF */
		/* Release resource and re-transmit beacon to HW */
		struct tasklet_struct  *bcn_tasklet;
		/* Modify for MI temporary,
		 * this processor cannot apply to multi-ap */
		PADAPTER bcn_adapter = rtw_mi_get_ap_adapter(Adapter);

		rtl8822be_tx_isr(Adapter, BCN_QUEUE_INX);
		bcn_adapter->mlmepriv.update_bcn = _TRUE;
		bcn_tasklet = &bcn_adapter->recvpriv.irq_prepare_beacon_tasklet;
		tasklet_hi_schedule(bcn_tasklet);
#endif /* CONFIG_BCN_ICF */
		handled[0] |= BIT_BCNDERR0_MSK;
	}

	if (pHalData->IntArray[0] & BIT_BCNDMAINT0_MSK) {
		struct tasklet_struct  *bcn_tasklet;
		/* Modify for MI temporary,
		  this processor cannot apply to multi-ap */
		PADAPTER bcn_adapter = rtw_mi_get_ap_adapter(Adapter);

		DBG_COUNTER(Adapter->int_logs.bcndma);
		bcn_tasklet = &bcn_adapter->recvpriv.irq_prepare_beacon_tasklet;
		tasklet_hi_schedule(bcn_tasklet);
		handled[0] |= BIT_BCNDMAINT0_MSK;
	}
}

#ifndef CONFIG_NAPI
static void rtl8822be_rx_handler(PADAPTER Adapter, u32 handled[])
{
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(Adapter);

	if ((pHalData->IntArray[0] & (BIT_RXOK | BIT_RDU)) ||
	    (pHalData->IntArray[1] & (BIT_FOVW | BIT_RXERR_INT))) {
		DBG_COUNTER(Adapter->int_logs.rx);

		if (pHalData->IntArray[0] & BIT_RDU) {
			DBG_COUNTER(Adapter->int_logs.rx_rdu);
		}

		if (pHalData->IntArray[1] & BIT_FOVW) {
			DBG_COUNTER(Adapter->int_logs.rx_fovw);
		}

		pHalData->IntrMask[0] &= (~(BIT_RXOK_MSK | BIT_RDU_MSK));
		pHalData->IntrMask[1] &= (~(BIT_FOVW_MSK | BIT_RXERR_MSK));
		rtw_write32(Adapter, REG_HIMR0, pHalData->IntrMask[0]);
		rtw_write32(Adapter, REG_HIMR1, pHalData->IntrMask[1]);
		tasklet_hi_schedule(&Adapter->recvpriv.recv_tasklet);
		handled[0] |= pHalData->IntArray[0] & (BIT_RXOK | BIT_RDU);
		handled[1] |= pHalData->IntArray[1] & (BIT_FOVW | BIT_RXERR_INT);
	}
}
#endif

static void rtl8822be_tx_handler(PADAPTER Adapter, u32 events[], u32 handled[])
{
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(Adapter);

	if (events[0] & BIT_MGTDOK_MSK) {
		DBG_COUNTER(Adapter->int_logs.mgntok);
		rtl8822be_tx_isr(Adapter, MGT_QUEUE_INX);
		handled[0] |= BIT_MGTDOK_MSK;
	}

	if (events[0] & BIT_HIGHDOK_MSK) {
		DBG_COUNTER(Adapter->int_logs.highdok);
		rtl8822be_tx_isr(Adapter, HIGH_QUEUE_INX);
		handled[0] |= BIT_HIGHDOK_MSK;
	}

	if (events[0] & BIT_BKDOK_MSK) {
		DBG_COUNTER(Adapter->int_logs.bkdok);
		rtl8822be_tx_isr(Adapter, BK_QUEUE_INX);
		handled[0] |= BIT_BKDOK_MSK;
	}

	if (events[0] & BIT_BEDOK_MSK) {
		DBG_COUNTER(Adapter->int_logs.bedok);
		rtl8822be_tx_isr(Adapter, BE_QUEUE_INX);
		handled[0] |= BIT_BEDOK_MSK;
	}

	if (events[0] & BIT_VIDOK_MSK) {
		DBG_COUNTER(Adapter->int_logs.vidok);
		rtl8822be_tx_isr(Adapter, VI_QUEUE_INX);
		handled[0] |= BIT_VIDOK_MSK;
	}

	if (events[0] & BIT_VODOK_MSK) {
		DBG_COUNTER(Adapter->int_logs.vodok);
		rtl8822be_tx_isr(Adapter, VO_QUEUE_INX);
		handled[0] |= BIT_VODOK_MSK;
	}
}

static void rtl8822be_cmd_handler(PADAPTER Adapter, u32 handled[])
{
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(Adapter);

	if (pHalData->IntArray[3] & BIT_SETH2CDOK_MASK) {
		rtl8822be_tx_isr(Adapter, TXCMD_QUEUE_INX);
		handled[3] |= BIT_SETH2CDOK_MASK;
	}
}

#ifndef CONFIG_NAPI
static s32 rtl8822be_interrupt(PADAPTER Adapter)
{
	_irqL irqL;
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(Adapter);
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(Adapter);
	struct xmit_priv *t_priv = &Adapter->xmitpriv;
	int ret = _SUCCESS;
	u32 handled[4] = {0};

	_enter_critical(&pdvobjpriv->irq_th_lock, &irqL);

	DBG_COUNTER(Adapter->int_logs.all);

	/* read ISR: 4/8bytes */
	if (rtl8822be_InterruptRecognized(Adapter) == _FALSE) {
		DBG_COUNTER(Adapter->int_logs.err);
		ret = _FAIL;
		goto done;
	}

	/* <1> beacon related */
	rtl8822be_bcn_handler(Adapter, handled);

	/* <2> Rx related */
	rtl8822be_rx_handler(Adapter, handled);

	/* <3> Tx related */
	rtl8822be_tx_handler(Adapter, pHalData->IntArray, handled);

	if (pHalData->IntArray[1] & BIT_TXFOVW) {
		DBG_COUNTER(Adapter->int_logs.txfovw);
		if (printk_ratelimit())
			RTW_WARN("[TXFOVW]\n");
		handled[1] |= BIT_TXFOVW;
	}

	/* <4> Cmd related */
	rtl8822be_cmd_handler(Adapter, handled);

	if ((pHalData->IntArray[0] & (~handled[0])) ||
		(pHalData->IntArray[1] & (~handled[1])) ||
		(pHalData->IntArray[3] & (~handled[3]))) {

		if (printk_ratelimit()) {
			RTW_WARN("Unhandled ISR = %x, %x, %x\n",
				(pHalData->IntArray[0] & (~handled[0])),
				(pHalData->IntArray[1] & (~handled[1])),
				(pHalData->IntArray[3] & (~handled[3])));
		}
	}
done:
	_exit_critical(&pdvobjpriv->irq_th_lock, &irqL);
	return ret;
}
#endif

u32 rtl8822be_init_bd(_adapter *padapter)
{
	struct xmit_priv *t_priv = &padapter->xmitpriv;
	int	i, ret = _SUCCESS;

	_func_enter_;

	init_bd_ring_var(padapter);
	ret = rtl8822be_init_rxbd_ring(padapter);

	if (ret == _FAIL)
		return ret;

	/* general process for other queue */
	for (i = 0; i < PCI_MAX_TX_QUEUE_COUNT; i++) {
		ret = rtl8822be_init_txbd_ring(padapter, i,
					       t_priv->txringcount[i]);
		if (ret == _FAIL)
			goto err_free_rings;
	}

	return ret;

err_free_rings:

	rtl8822be_free_rxbd_ring(padapter);

	for (i = 0; i < PCI_MAX_TX_QUEUE_COUNT; i++)
		if (t_priv->tx_ring[i].buf_desc)
			rtl8822be_free_txbd_ring(padapter, i);

	_func_exit_;

	return ret;
}

u32 rtl8822be_free_bd(_adapter *padapter)
{
	struct xmit_priv	*t_priv = &padapter->xmitpriv;
	u32 i;

	_func_enter_;

	/* free rxbd rings */
	rtl8822be_free_rxbd_ring(padapter);

	/* free txbd rings */
	for (i = 0; i < HW_QUEUE_ENTRY; i++)
		rtl8822be_free_txbd_ring(padapter, i);

	_func_exit_;

	return _SUCCESS;
}

u8 rtl8822be_sethaldefvar(PADAPTER adapter, HAL_DEF_VARIABLE variable, void *pval)
{
	PHAL_DATA_TYPE hal;
	u8 bResult;


	hal = GET_HAL_DATA(adapter);
	bResult = _SUCCESS;

	switch (variable) {

	case HAL_DEF_PCI_SUUPORT_L1_BACKDOOR:
		hal->bSupportBackDoor = *((BOOLEAN *)pval);
		break;

	default:
		bResult = rtl8822b_sethaldefvar(adapter, variable, pval);
		break;
	}

	return bResult;
}

/*
	Description:
		Query setting of specified variable.
*/
static u8 rtl8822be_gethaldefvar(PADAPTER	padapter, HAL_DEF_VARIABLE eVariable, PVOID pValue)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(padapter);
	u8 bResult = _SUCCESS;

	switch (eVariable) {

	case HAL_DEF_PCI_SUUPORT_L1_BACKDOOR:
		*((PBOOLEAN)pValue) = pHalData->bSupportBackDoor;
		break;

	case HAL_DEF_PCI_AMD_L1_SUPPORT:
		*((PBOOLEAN)pValue) = _TRUE;/* Support L1 patch on AMD platform in default, added by Roger, 2012.04.30. */
		break;

	case HAL_DEF_MAX_RECVBUF_SZ:
		*((u32 *)pValue) = MAX_RECVBUF_SZ;
		break;

	case HW_VAR_MAX_RX_AMPDU_FACTOR:
		*(HT_CAP_AMPDU_FACTOR *)pValue = MAX_AMPDU_FACTOR_64K;
		break;
	default:
		bResult = rtl8822b_gethaldefvar(padapter, eVariable, pValue);
		break;
	}

	return bResult;
}

#ifdef CONFIG_NAPI

#define NAPI_EVENT_BCN0	( \
			BIT_TXBCN0OK_MSK | \
			BIT_TXBCN0ERR_MSK | \
			BIT_BCNDERR0_MSK | \
			BIT_BCNDMAINT0_MSK \
			)

#define NAPI_EVENT_RX0	( \
			BIT_RXOK | \
			BIT_RDU \
			)

#define NAPI_EVENT_RX1	( \
			BIT_FOVW | \
			BIT_RXERR_INT \
			)

#define NAPI_EVENT_TX0	( \
			BIT_MGTDOK_MSK | \
			BIT_HIGHDOK_MSK | \
			BIT_BKDOK_MSK | \
			BIT_BEDOK_MSK | \
			BIT_VIDOK_MSK | \
			BIT_VODOK_MSK \
			)

#define NAPI_EVENT_TX1	( \
			BIT_TXFOVW | \
			BIT_TXERR_INT \
			)

#define NAPI_EVENT_CMD3	( \
			BIT_SETH2CDOK_MASK \
			)

#define NAPI_EVENT0	(BIT_RXOK)

#define NAPI_EVENT1	(0)

static void rtl8822be_napi_irq_disable(PADAPTER adapter)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(adapter);

	pHalData->IntrMask[0] &= (~NAPI_EVENT0);
/*	pHalData->IntrMask[1] &= (~NAPI_EVENT1);	*/
	rtw_write32(adapter, REG_HIMR0, pHalData->IntrMask[0]);
/*	rtw_write32(adapter, REG_HIMR1, pHalData->IntrMask[1]); */
}

static void rtl8822be_napi_irq_enable(PADAPTER adapter)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(adapter);

	pHalData->IntrMask[0] |= NAPI_EVENT0;
/*	pHalData->IntrMask[1] |= NAPI_EVENT1;	*/
	rtw_write32(adapter, REG_HIMR0, pHalData->IntrMask[0]);
/*	rtw_write32(adapter, REG_HIMR1, pHalData->IntrMask[1]);	*/
}

static void rtl8822be_napi_get_events(PADAPTER adapter)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(adapter);

	pHalData->IntArray[0] = rtw_read32(adapter, REG_HISR0);
	pHalData->IntArray[1] = rtw_read32(adapter, REG_HISR1);
	pHalData->IntArray[3] = rtw_read32(adapter, REG_HISR3);
}

static s32 rtl8822be_napi_interrupt(PADAPTER adapter)
{
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(adapter);
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(adapter);
	_irqL irqL;
	u32 handled[4] = {0}; /* no interrupt handled on default */

	_enter_critical(&pdvobjpriv->irq_th_lock, &irqL);

	DBG_COUNTER(adapter->int_logs.all);

#if 1
	/* 2013.11.18 Glayrainx suggests that turn off IMR and
	 * restore after cleaning ISR.
	 */
	rtw_write32(adapter, REG_HIMR0, 0);
	rtw_write32(adapter, REG_HIMR1, 0);
	rtw_write32(adapter, REG_HIMR3, 0);
#endif

	rtl8822be_napi_get_events(adapter);
	pHalData->IntArray[0] &= pHalData->IntrMask[0];
	pHalData->IntArray[1] &= pHalData->IntrMask[1];
	pHalData->IntArray[3] &= pHalData->IntrMask[3];
#if 0
	RTW_INFO("[I %x, %x, %x]\n",
			pHalData->IntArray[0],
			pHalData->IntArray[1],
			pHalData->IntArray[3]);
#endif

	/* ack non-NAPI event directly */
	if (pHalData->IntArray[3])
		rtw_write32(adapter, REG_HISR3, pHalData->IntArray[3]);
	if (pHalData->IntArray[0])
		rtw_write32(adapter, REG_HISR0, pHalData->IntArray[0]);
	if (pHalData->IntArray[1])
		rtw_write32(adapter, REG_HISR1, pHalData->IntArray[1]);

#if 1
	/* restore IMR */
	rtw_write32(adapter, REG_HIMR0, pHalData->IntrMask[0] & 0xFFFFFFFF);
	rtw_write32(adapter, REG_HIMR1, pHalData->IntrMask[1] & 0xFFFFFFFF);
	rtw_write32(adapter, REG_HIMR3, pHalData->IntrMask[3] & 0xFFFFFFFF);
#endif

	/* <0> handle cmd directly */
	if (pHalData->IntArray[3] & NAPI_EVENT_CMD3)
		rtl8822be_cmd_handler(adapter, handled);

	/* <1> handle beacon directly */
	if (pHalData->IntArray[0] & NAPI_EVENT_BCN0)
		rtl8822be_bcn_handler(adapter, handled);

	/* <2> Rx first */
	if (pHalData->IntArray[0] & BIT_RDU) {
		DBG_COUNTER(adapter->int_logs.rx_rdu);

		if (printk_ratelimit())
			RTW_WARN("[RDU]\n");
		handled[0] |= BIT_RDU;
	}

	if (pHalData->IntArray[1] & BIT_FOVW) {
		DBG_COUNTER(adapter->int_logs.rx_fovw);

		if (printk_ratelimit())
			RTW_WARN("[FOVW]\n");
		handled[1] |= BIT_FOVW;
	}

	if (pHalData->IntArray[1] & BIT_RXERR_INT) {
		DBG_COUNTER(adapter->int_logs.err);
		RTW_WARN("[RXERR]\n");
		handled[1] |= BIT_RXERR_INT;
	}

	/* <2> handle rx in napi poll */
	if (pHalData->IntArray[0] & NAPI_EVENT0) {
		DBG_COUNTER(adapter->int_logs.rx);

		if (napi_schedule_prep(&adapter->napi)) {
			handled[0] |= (pHalData->IntArray[0] & NAPI_EVENT0);
			/* disable irq */
			rtl8822be_napi_irq_disable(adapter);
			/* tell system we have work to be done */
			__napi_schedule(&adapter->napi);
		} else {
			RTW_WARN("driver bug! interrupt while in poll\n");
			 /* FIX by disabling interrupts  */
			rtl8822be_napi_irq_disable(adapter);
		}
	}

	/* <3> handle tx directly */
	if (pHalData->IntArray[1] & BIT_TXFOVW) {
		DBG_COUNTER(adapter->int_logs.txfovw);

		if (printk_ratelimit())
			RTW_WARN("[TXFOVW]\n");
		handled[1] |= BIT_TXFOVW;
	}

	if (pHalData->IntArray[1] & BIT_TXERR_INT) {
		DBG_COUNTER(adapter->int_logs.err);
		RTW_WARN("[TXERR]\n");
		handled[1] |= BIT_TXERR_INT;
	}

	if (pHalData->IntArray[0] & NAPI_EVENT_TX0)
		rtl8822be_tx_handler(adapter, pHalData->IntArray, handled);

	/* check un-handled interrupt */
	if ((pHalData->IntArray[0] & (~handled[0])) ||
		(pHalData->IntArray[1] & (~handled[1])) ||
		(pHalData->IntArray[3] & (~handled[3]))) {

		RTW_WARN("Unhandled ISR = %x, %x, %x\n",
			(pHalData->IntArray[0] & (~handled[0])),
			(pHalData->IntArray[1] & (~handled[1])),
			(pHalData->IntArray[3] & (~handled[3])));
	}

	_exit_critical(&pdvobjpriv->irq_th_lock, &irqL);

	return IRQ_RETVAL(handled[0] || handled[1] || handled[3]);
}

static int rtl8822be_napi_poll(PADAPTER adapter, int budget)
{
	int remaing_rxdesc;
	int work_done = 0;
	struct registry_priv *reg = &adapter->registrypriv;

	remaing_rxdesc = rtl8822be_check_rxdesc_remain(adapter, RX_MPDU_QUEUE);
	do {
		work_done += rtl8822be_rx_mpdu(adapter, remaing_rxdesc,
				budget);
		budget -= work_done;

		/* check rx again */
		remaing_rxdesc = rtl8822be_check_rxdesc_remain(adapter,
				RX_MPDU_QUEUE);

		if (remaing_rxdesc) {
			if (reg->napi_debug && printk_ratelimit())
				RTW_INFO("[RX %d]\n", remaing_rxdesc);
			break;
		}

	} while (remaing_rxdesc > 0 && budget > 0);

	return work_done;
}
#endif /* CONFIG_NAPI */

void rtl8822be_set_hal_ops(PADAPTER padapter)
{
	struct hal_ops *ops;
	int err;

	err = rtl8822be_halmac_init_adapter(padapter);
	if (err) {
		RTW_INFO("%s: [ERROR]HALMAC initialize FAIL!\n", __func__);
		return;
	}

	rtl8822b_set_hal_ops(padapter);

	ops = &padapter->HalFunc;

	ops->hal_init = rtl8822be_init;
	ops->inirp_init = rtl8822be_init_bd;
	ops->inirp_deinit = rtl8822be_free_bd;
	ops->irp_reset = rtl8822be_reset_bd;
	ops->init_xmit_priv = rtl8822be_init_xmit_priv;
	ops->free_xmit_priv = rtl8822be_free_xmit_priv;
	ops->init_recv_priv = rtl8822be_init_recv_priv;
	ops->free_recv_priv = rtl8822be_free_recv_priv;

#ifdef CONFIG_SW_LED
	ops->InitSwLeds = rtl8822be_InitSwLeds;
	ops->DeInitSwLeds = rtl8822be_DeInitSwLeds;
#else /* case of hw led or no led */
	ops->InitSwLeds = NULL;
	ops->DeInitSwLeds = NULL;
#endif

	ops->init_default_value = rtl8822be_init_default_value;
	ops->intf_chip_configure = intf_chip_configure;
	ops->read_adapter_info = read_adapter_info;

	ops->enable_interrupt = rtl8822be_enable_interrupt;
	ops->disable_interrupt = rtl8822be_disable_interrupt;
#ifdef CONFIG_NAPI
	ops->interrupt_handler = rtl8822be_napi_interrupt;
#else
	ops->interrupt_handler = rtl8822be_interrupt;
#endif
	/*
		ops->check_ips_status = check_ips_status;
	*/
	ops->clear_interrupt = rtl8822be_clear_interrupt;
#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN) ||\
	defined(CONFIG_PCI_HCI)
	/*
		ops->clear_interrupt = clear_interrupt_all;
	*/
#endif
	ops->GetHalDefVarHandler = rtl8822be_gethaldefvar;
	ops->SetHalDefVarHandler = rtl8822be_sethaldefvar;
	ops->hal_xmit = rtl8822be_hal_xmit;
	ops->mgnt_xmit = rtl8822be_mgnt_xmit;
	ops->hal_xmitframe_enqueue = rtl8822be_hal_xmitframe_enqueue;
#ifdef CONFIG_HOSTAPD_MLME
	ops->hostap_mgnt_xmit_entry = rtl8822be_hostap_mgnt_xmit_entry;
#endif

#ifdef CONFIG_XMIT_THREAD_MODE
	/* vincent TODO */
	ops->xmit_thread_handler = rtl8822be_xmit_buf_handler;
#endif

#ifdef CONFIG_NAPI
	ops->napi_irq_disable = rtl8822be_napi_irq_disable;
	ops->napi_irq_enable = rtl8822be_napi_irq_enable;
	ops->napi_poll = rtl8822be_napi_poll;
#endif
}
