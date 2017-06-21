/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
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
#if defined(CONFIG_MP_INCLUDED)

#include <drv_types.h>
#include <rtw_mp.h>
#include <rtw_mp_ioctl.h>
#include "../../hal/phydm/phydm_precomp.h"


#if defined(CONFIG_RTL8723B)
#include <rtw_bt_mp.h>
#endif

/*
 * Input Format: %s,%d,%d
 *	%s is width, could be
 *		"b" for 1 byte
 *		"w" for WORD (2 bytes)
 *		"dw" for DWORD (4 bytes)
 *	1st %d is address(offset)
 *	2st %d is data to write
 */
int rtw_mp_write_reg(struct net_device *dev,
					 struct iw_request_info *info,
					 struct iw_point *wrqu, char *extra)
{
	char *pch, *pnext, *ptmp;
	char *width_str;
	char width, buf[5];
	u32 addr, data;
	int ret;
	PADAPTER padapter = rtw_netdev_priv(dev);
	char input[wrqu->length];

	if (copy_from_user(input, wrqu->pointer, wrqu->length))
		return -EFAULT;

	_rtw_memset(extra, 0, wrqu->length);

	pch = input;

	pnext = strpbrk(pch, " ,.-");
	if (pnext == NULL)
		return -EINVAL;
	*pnext = 0;
	width_str = pch;

	pch = pnext + 1;
	pnext = strpbrk(pch, " ,.-");
	if (pnext == NULL)
		return -EINVAL;
	*pnext = 0;
	/*addr = simple_strtoul(pch, &ptmp, 16);
	_rtw_memset(buf, '\0', sizeof(buf));
	_rtw_memcpy(buf, pch, pnext-pch);
	ret = kstrtoul(buf, 16, &addr);*/
	ret = sscanf(pch, "%x", &addr);
	if (addr > 0x3FFF)
		return -EINVAL;

	pch = pnext + 1;
	pnext = strpbrk(pch, " ,.-");
	if ((pch - input) >= wrqu->length)
		return -EINVAL;
	/*data = simple_strtoul(pch, &ptmp, 16);*/
	ret = sscanf(pch, "%x", &data);
	DBG_871X("data=%x,addr=%x\n", (u32)data, (u32)addr);
	ret = 0;
	width = width_str[0];
	switch (width) {
	case 'b':
		/* 1 byte*/
		if (data > 0xFF) {
			ret = -EINVAL;
			break;
		}
		rtw_write8(padapter, addr, data);
		break;
	case 'w':
		/* 2 bytes*/
		if (data > 0xFFFF) {
			ret = -EINVAL;
			break;
		}
		rtw_write16(padapter, addr, data);
		break;
	case 'd':
		/* 4 bytes*/
		rtw_write32(padapter, addr, data);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}


/*
 * Input Format: %s,%d
 *	%s is width, could be
 *		"b" for 1 byte
 *		"w" for WORD (2 bytes)
 *		"dw" for DWORD (4 bytes)
 *	%d is address(offset)
 *
 * Return:
 *	%d for data readed
 */
int rtw_mp_read_reg(struct net_device *dev,
					struct iw_request_info *info,
					struct iw_point *wrqu, char *extra)
{
	char input[wrqu->length];
	char *pch, *pnext, *ptmp;
	char *width_str;
	char width;
	char data[20], tmp[20], buf[3];
	u32 addr = 0, strtout = 0;
	u32 i = 0, j = 0, ret = 0, data32 = 0;
	PADAPTER padapter = rtw_netdev_priv(dev);


	if (wrqu->length > 128)
		return -EFAULT;

	if (copy_from_user(input, wrqu->pointer, wrqu->length))
		return -EFAULT;

	_rtw_memset(extra, 0, wrqu->length);

	pch = input;
	pnext = strpbrk(pch, " ,.-");
	if (pnext == NULL)
		return -EINVAL;
	*pnext = 0;
	width_str = pch;

	pch = pnext + 1;

	ret = sscanf(pch, "%x", &addr);
	if (addr > 0x3FFF)
		return -EINVAL;

	ret = 0;
	width = width_str[0];

	switch (width) {
	case 'b':
			data32 = rtw_read8(padapter, addr);
			DBG_871X("%x\n", data32);
			sprintf(extra, "%d", data32);
			wrqu->length = strlen(extra);
			break;
	case 'w':
			/* 2 bytes*/
			sprintf(data, "%04x\n", rtw_read16(padapter, addr));

			for (i = 0 ; i <= strlen(data) ; i++) {
				if (i % 2 == 0) {
					tmp[j] = ' ';
					j++;
				}
				if (data[i] != '\0')
					tmp[j] = data[i];

				j++;
			}
			pch = tmp;
			DBG_871X("pch=%s", pch);

			while (*pch != '\0') {
				pnext = strpbrk(pch, " ");
				if (!pnext || ((pnext - tmp) > 4))
					break;
					
				pnext++;
				if (*pnext != '\0') {
					/*strtout = simple_strtoul(pnext , &ptmp, 16);*/
					ret = sscanf(pnext, "%x", &strtout);
					sprintf(extra, "%s %d" , extra , strtout);
				} else
					break;
				pch = pnext;
			}
			wrqu->length = strlen(extra);
			break;
	case 'd':
			/* 4 bytes */
			sprintf(data, "%08x", rtw_read32(padapter, addr));
			/*add read data format blank*/
			for (i = 0 ; i <= strlen(data) ; i++) {
				if (i % 2 == 0) {
					tmp[j] = ' ';
					j++;
				}
				if (data[i] != '\0')
					tmp[j] = data[i];

				j++;
			}
			pch = tmp;
			DBG_871X("pch=%s", pch);

			while (*pch != '\0') {
				pnext = strpbrk(pch, " ");
				if (!pnext)
					break;

				pnext++;
				if (*pnext != '\0') {
					ret = sscanf(pnext, "%x", &strtout);
					sprintf(extra, "%s %d" , extra , strtout);
				} else
					break;
				pch = pnext;
			}
			wrqu->length = strlen(extra) - 1;
			break;

	default:
			wrqu->length = 0;
			ret = -EINVAL;
			break;
		}

	return ret;
}


/*
 * Input Format: %d,%x,%x
 *	%d is RF path, should be smaller than MAX_RF_PATH_NUMS
 *	1st %x is address(offset)
 *	2st %x is data to write
 */
int rtw_mp_write_rf(struct net_device *dev,
					struct iw_request_info *info,
					struct iw_point *wrqu, char *extra)
{

	u32 path, addr, data;
	int ret;
	PADAPTER padapter = rtw_netdev_priv(dev);
	char input[wrqu->length];


	_rtw_memset(input, 0, wrqu->length);
	if (copy_from_user(input, wrqu->pointer, wrqu->length))
		return -EFAULT;


	ret = sscanf(input, "%d,%x,%x", &path, &addr, &data);
	if (ret < 3)
		return -EINVAL;

	if (path >= GET_HAL_RFPATH_NUM(padapter))
		return -EINVAL;
	if (addr > 0xFF)
		return -EINVAL;
	if (data > 0xFFFFF)
		return -EINVAL;

	_rtw_memset(extra, 0, wrqu->length);

	write_rfreg(padapter, path, addr, data);

	sprintf(extra, "write_rf completed\n");
	wrqu->length = strlen(extra);

	return 0;
}


/*
 * Input Format: %d,%x
 *	%d is RF path, should be smaller than MAX_RF_PATH_NUMS
 *	%x is address(offset)
 *
 * Return:
 *	%d for data readed
 */
int rtw_mp_read_rf(struct net_device *dev,
				   struct iw_request_info *info,
				   struct iw_point *wrqu, char *extra)
{
	char input[wrqu->length];
	char *pch, *pnext, *ptmp;
	char data[20], tmp[20], buf[3];
	u32 path, addr, strtou;
	u32 ret, i = 0 , j = 0;
	PADAPTER padapter = rtw_netdev_priv(dev);
	
	if (wrqu->length > 128)
		return -EFAULT;
	_rtw_memset(input, 0, wrqu->length);
	if (copy_from_user(input, wrqu->pointer, wrqu->length))
		return -EFAULT;

	ret = sscanf(input, "%d,%x", &path, &addr);
	if (ret < 2)
		return -EINVAL;

	if (path >= GET_HAL_RFPATH_NUM(padapter))
		return -EINVAL;
	if (addr > 0xFF)
		return -EINVAL;

	_rtw_memset(extra, 0, wrqu->length);

	sprintf(data, "%08x", read_rfreg(padapter, path, addr));
	/*add read data format blank*/
	for (i = 0 ; i <= strlen(data) ; i++) {
		if (i % 2 == 0) {
			tmp[j] = ' ';
			j++;
		}
		tmp[j] = data[i];
		j++;
	}
	pch = tmp;
	DBG_871X("pch=%s", pch);

	while (*pch != '\0') {
		pnext = strpbrk(pch, " ");
		if (!pnext)
			break;
		pnext++;
		if (*pnext != '\0') {
			/*strtou =simple_strtoul(pnext , &ptmp, 16);*/
			ret = sscanf(pnext, "%x", &strtou);
			sprintf(extra, "%s %d" , extra , strtou);
		} else
			break;
		pch = pnext;
	}
	wrqu->length = strlen(extra);

	return 0;
}


int rtw_mp_start(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_point *wrqu, char *extra)
{
	u8 val8;
	PADAPTER padapter = rtw_netdev_priv(dev);
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct hal_ops *pHalFunc = &padapter->HalFunc;

	rtw_pm_set_ips(padapter, IPS_NONE);
	LeaveAllPowerSaveMode(padapter);

	if (padapter->registrypriv.mp_mode == 0) {
		pHalFunc->hal_deinit(padapter);
		padapter->registrypriv.mp_mode = 1;
		pHalFunc->hal_init(padapter);

		rtw_pm_set_ips(padapter, IPS_NONE);
		LeaveAllPowerSaveMode(padapter);
	}

	if (padapter->registrypriv.mp_mode == 0)
		return -EPERM;

	if (padapter->mppriv.mode == MP_OFF) {
		if (mp_start_test(padapter) == _FAIL)
			return -EPERM;
		padapter->mppriv.mode = MP_ON;
		MPT_PwrCtlDM(padapter, 0);
	}
	padapter->mppriv.bmac_filter = _FALSE;
#ifdef CONFIG_RTL8723B
#ifdef CONFIG_USB_HCI
	rtw_write32(padapter, 0x765, 0x0000);
	rtw_write32(padapter, 0x948, 0x0280);
#else
	rtw_write32(padapter, 0x765, 0x0000);
	rtw_write32(padapter, 0x948, 0x0000);
#endif
#ifdef CONFIG_FOR_RTL8723BS_VQ0
	rtw_write32(padapter, 0x765, 0x0000);
	rtw_write32(padapter, 0x948, 0x0280);
#endif
	rtw_write8(padapter, 0x66, 0x27); /*Open BT uart Log*/
	rtw_write8(padapter, 0xc50, 0x20); /*for RX init Gain*/
#endif
	ODM_Write_DIG(&pHalData->odmpriv, 0x20);

	return 0;
}



int rtw_mp_stop(struct net_device *dev,
				struct iw_request_info *info,
				struct iw_point *wrqu, char *extra)
{
	PADAPTER padapter = rtw_netdev_priv(dev);
	struct hal_ops *pHalFunc = &padapter->HalFunc;

	if (padapter->registrypriv.mp_mode == 1) {

		MPT_DeInitAdapter(padapter);
		pHalFunc->hal_deinit(padapter);
		padapter->registrypriv.mp_mode = 0;
		pHalFunc->hal_init(padapter);
	}

	if (padapter->mppriv.mode != MP_OFF) {
		mp_stop_test(padapter);
		padapter->mppriv.mode = MP_OFF;
	}

	return 0;
}


int rtw_mp_rate(struct net_device *dev,
				struct iw_request_info *info,
				struct iw_point *wrqu, char *extra)
{
	u32 rate = MPT_RATE_1M;
	u8		input[wrqu->length];
	PADAPTER padapter = rtw_netdev_priv(dev);
	PMPT_CONTEXT		pMptCtx = &(padapter->mppriv.MptCtx);

	if (copy_from_user(input, wrqu->pointer, wrqu->length))
		return -EFAULT;

	rate = rtw_mpRateParseFunc(padapter, input);

	if (rate == 0 && strcmp(input, "1M") != 0) {
		rate = rtw_atoi(input);
		if (rate <= 0x7f)
			rate = wifirate2_ratetbl_inx((u8)rate);
		else if (rate < 0xC8)
			rate = (rate - 0x80 + MPT_RATE_MCS0);
		/*HT  rate 0x80(MCS0)  ~ 0x8F(MCS15) ~ 0x9F(MCS31) 128~159
		VHT1SS~2SS rate 0xA0 (VHT1SS_MCS0 44) ~ 0xB3 (VHT2SS_MCS9 #63) 160~179
		VHT rate 0xB4 (VHT3SS_MCS0 64) ~ 0xC7 (VHT2SS_MCS9 #83) 180~199
		else
		VHT rate 0x90(VHT1SS_MCS0) ~ 0x99(VHT1SS_MCS9) 144~153
		rate =(rate - MPT_RATE_VHT1SS_MCS0);
		*/
	}
	_rtw_memset(extra, 0, wrqu->length);

	sprintf(extra, "Set data rate to %s index %d" , input, rate);
	DBG_871X("%s: %s rate index=%d\n", __func__, input, rate);

	if (rate >= MPT_RATE_LAST)
		return -EINVAL;

	padapter->mppriv.rateidx = rate;
	pMptCtx->MptRateIndex = rate;
	SetDataRate(padapter);

	wrqu->length = strlen(extra);
	return 0;
}


int rtw_mp_channel(struct net_device *dev,
				   struct iw_request_info *info,
				   struct iw_point *wrqu, char *extra)
{

	PADAPTER padapter = rtw_netdev_priv(dev);
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(padapter);
	u8		input[wrqu->length];
	u32	channel = 1;
	int cur_ch_offset;

	if (copy_from_user(input, wrqu->pointer, wrqu->length))
		return -EFAULT;

	channel = rtw_atoi(input);
	/*DBG_871X("%s: channel=%d\n", __func__, channel);*/
	_rtw_memset(extra, 0, wrqu->length);
	sprintf(extra, "Change channel %d to channel %d", padapter->mppriv.channel , channel);
	padapter->mppriv.channel = channel;
	SetChannel(padapter);
	pHalData->CurrentChannel = channel;

	wrqu->length = strlen(extra);
	return 0;
}


int rtw_mp_bandwidth(struct net_device *dev,
					 struct iw_request_info *info,
					 struct iw_point *wrqu, char *extra)
{
	u32 bandwidth = 0, sg = 0;
	int cur_ch_offset;
	PADAPTER padapter = rtw_netdev_priv(dev);
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(padapter);

	if (sscanf(extra, "40M=%d,shortGI=%d", &bandwidth, &sg) > 0)
		DBG_871X("%s: bw=%d sg=%d\n", __func__, bandwidth , sg);

	if (bandwidth == 1)
		bandwidth = CHANNEL_WIDTH_40;
	else if (bandwidth == 2)
		bandwidth = CHANNEL_WIDTH_80;

	padapter->mppriv.bandwidth = (u8)bandwidth;
	padapter->mppriv.preamble = sg;

	SetBandwidth(padapter);
	pHalData->CurrentChannelBW = bandwidth;
	/*cur_ch_offset =  rtw_get_offset_by_ch(padapter->mppriv.channel);*/
	/*set_channel_bwmode(padapter, padapter->mppriv.channel, cur_ch_offset, bandwidth);*/

	return 0;
}


int rtw_mp_txpower_index(struct net_device *dev,
						 struct iw_request_info *info,
						 struct iw_point *wrqu, char *extra)
{
	PADAPTER padapter = rtw_netdev_priv(dev);
	char input[wrqu->length];
	u32 rfpath;
	u32 txpower_inx;

	if (wrqu->length > 128)
		return -EFAULT;

	if (copy_from_user(input, wrqu->pointer, wrqu->length))
		return -EFAULT;

	rfpath = rtw_atoi(input);
	txpower_inx = mpt_ProQueryCalTxPower(padapter, rfpath);
	sprintf(extra, " %d", txpower_inx);
	wrqu->length = strlen(extra);

	return 0;
}


int rtw_mp_txpower(struct net_device *dev,
				   struct iw_request_info *info,
				   struct iw_point *wrqu, char *extra)
{
	u32 idx_a = 0, idx_b = 0, idx_c = 0, idx_d = 0, status = 0;
	int MsetPower = 1;
	u8		input[wrqu->length];

	PADAPTER padapter = rtw_netdev_priv(dev);
	PMPT_CONTEXT		pMptCtx = &(padapter->mppriv.MptCtx);

	if (copy_from_user(input, wrqu->pointer, wrqu->length))
		return -EFAULT;

	MsetPower = strncmp(input, "off", 3);
	if (MsetPower == 0) {
		padapter->mppriv.bSetTxPower = 0;
		sprintf(extra, "MP Set power off");
	} else {
		if (sscanf(input, "patha=%d,pathb=%d,pathc=%d,pathd=%d", &idx_a, &idx_b, &idx_c, &idx_d) < 3)
			DBG_871X("Invalid format on line %s ,patha=%d,pathb=%d,pathc=%d,pathd=%d\n", input , idx_a , idx_b , idx_c , idx_d);

		sprintf(extra, "Set power level path_A:%d path_B:%d path_C:%d path_D:%d", idx_a , idx_b , idx_c , idx_d);
		padapter->mppriv.txpoweridx = (u8)idx_a;

		pMptCtx->TxPwrLevel[ODM_RF_PATH_A] = (u8)idx_a;
		pMptCtx->TxPwrLevel[ODM_RF_PATH_B] = (u8)idx_b;
		pMptCtx->TxPwrLevel[ODM_RF_PATH_C] = (u8)idx_c;
		pMptCtx->TxPwrLevel[ODM_RF_PATH_D]  = (u8)idx_d;
		padapter->mppriv.bSetTxPower = 1;

		SetTxPower(padapter);
	}

	wrqu->length = strlen(extra);
	return 0;
}


int rtw_mp_ant_tx(struct net_device *dev,
				  struct iw_request_info *info,
				  struct iw_point *wrqu, char *extra)
{
	u8 i;
	u8		input[wrqu->length];
	u16 antenna = 0;
	PADAPTER padapter = rtw_netdev_priv(dev);
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	if (copy_from_user(input, wrqu->pointer, wrqu->length))
		return -EFAULT;

	sprintf(extra, "switch Tx antenna to %s", input);

	for (i = 0; i < strlen(input); i++) {
		switch (input[i]) {
		case 'a':
			antenna |= ANTENNA_A;
			break;
		case 'b':
			antenna |= ANTENNA_B;
			break;
		case 'c':
			antenna |= ANTENNA_C;
			break;
		case 'd':
			antenna |= ANTENNA_D;
			break;
		}
	}
	/*antenna |= BIT(extra[i]-'a');*/
	DBG_871X("%s: antenna=0x%x\n", __func__, antenna);
	padapter->mppriv.antenna_tx = antenna;
	padapter->mppriv.antenna_rx = antenna;
	/*DBG_871X("%s:mppriv.antenna_rx=%d\n", __func__, padapter->mppriv.antenna_tx);*/
	pHalData->AntennaTxPath = antenna;

	SetAntenna(padapter);

	wrqu->length = strlen(extra);
	return 0;
}


int rtw_mp_ant_rx(struct net_device *dev,
				  struct iw_request_info *info,
				  struct iw_point *wrqu, char *extra)
{
	u8 i;
	u16 antenna = 0;
	u8		input[wrqu->length];
	PADAPTER padapter = rtw_netdev_priv(dev);
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	if (copy_from_user(input, wrqu->pointer, wrqu->length))
		return -EFAULT;
	/*DBG_871X("%s: input=%s\n", __func__, input);*/
	_rtw_memset(extra, 0, wrqu->length);

	sprintf(extra, "switch Rx antenna to %s", input);

	for (i = 0; i < strlen(input); i++) {
		switch (input[i]) {
		case 'a':
			antenna |= ANTENNA_A;
			break;
		case 'b':
			antenna |= ANTENNA_B;
			break;
		case 'c':
			antenna |= ANTENNA_C;
			break;
		case 'd':
			antenna |= ANTENNA_D;
			break;
		}
	}

	DBG_871X("%s: antenna=0x%x\n", __func__, antenna);
	padapter->mppriv.antenna_tx = antenna;
	padapter->mppriv.antenna_rx = antenna;
	pHalData->AntennaRxPath = antenna;
	/*DBG_871X("%s:mppriv.antenna_rx=%d\n", __func__, padapter->mppriv.antenna_rx);*/
	SetAntenna(padapter);
	wrqu->length = strlen(extra);

	return 0;
}


int rtw_set_ctx_destAddr(struct net_device *dev,
						 struct iw_request_info *info,
						 struct iw_point *wrqu, char *extra)
{
	int jj, kk = 0;

	struct pkt_attrib *pattrib;
	struct mp_priv *pmp_priv;
	PADAPTER padapter = rtw_netdev_priv(dev);
	
	pmp_priv = &padapter->mppriv;
	pattrib = &pmp_priv->tx.attrib;

	if (strlen(extra) < 5)
		return _FAIL;

	DBG_871X("%s: in=%s\n", __func__, extra);
	for (jj = 0, kk = 0; jj < ETH_ALEN; jj++, kk += 3)
		pattrib->dst[jj] = key_2char2num(extra[kk], extra[kk + 1]);

	DBG_871X("pattrib->dst:%x %x %x %x %x %x\n", pattrib->dst[0], pattrib->dst[1], pattrib->dst[2], pattrib->dst[3], pattrib->dst[4], pattrib->dst[5]);
	return 0;
}



int rtw_mp_ctx(struct net_device *dev,
			   struct iw_request_info *info,
			   struct iw_point *wrqu, char *extra)
{
	u32 pkTx = 1;
	int countPkTx = 1, cotuTx = 1, CarrSprTx = 1, scTx = 1, sgleTx = 1, stop = 1;
	u32 bStartTest = 1;
	u32 count = 0, pktinterval = 0, pktlen = 0;
	u8 status;
	struct mp_priv *pmp_priv;
	struct pkt_attrib *pattrib;
	PADAPTER padapter = rtw_netdev_priv(dev);
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	pmp_priv = &padapter->mppriv;
	pattrib = &pmp_priv->tx.attrib;

	if (copy_from_user(extra, wrqu->pointer, wrqu->length))
		return -EFAULT;

	DBG_871X("%s: in=%s\n", __func__, extra);

	countPkTx = strncmp(extra, "count=", 5); /* strncmp TRUE is 0*/
	cotuTx = strncmp(extra, "background", 20);
	CarrSprTx = strncmp(extra, "background,cs", 20);
	scTx = strncmp(extra, "background,sc", 20);
	sgleTx = strncmp(extra, "background,stone", 20);
	pkTx = strncmp(extra, "background,pkt", 20);
	stop = strncmp(extra, "stop", 4);
	if (sscanf(extra, "count=%d,pkt", &count) > 0)
		DBG_871X("count= %d\n", count);
	if (sscanf(extra, "pktinterval=%d", &pktinterval) > 0)
		DBG_871X("pktinterval= %d\n", pktinterval);

	if (sscanf(extra, "pktlen=%d", &pktlen) > 0)
		DBG_871X("pktlen= %d\n", pktlen);

	if (_rtw_memcmp(extra, "destmac=", 8)) {
		wrqu->length -= 8;
		rtw_set_ctx_destAddr(dev, info, wrqu, &extra[8]);
		sprintf(extra, "Set dest mac OK !\n");
		return 0;
	}

	/*DBG_871X("%s: count=%d countPkTx=%d cotuTx=%d CarrSprTx=%d scTx=%d sgleTx=%d pkTx=%d stop=%d\n", __func__, count, countPkTx, cotuTx, CarrSprTx, pkTx, sgleTx, scTx, stop);*/
	_rtw_memset(extra, '\0', strlen(extra));

	if (pktinterval != 0) {
		sprintf(extra, "Pkt Interval = %d", pktinterval);
		padapter->mppriv.pktInterval = pktinterval;
		wrqu->length = strlen(extra);
		return 0;
	}
	if (pktlen != 0) {
		sprintf(extra, "Pkt len = %d", pktlen);
		pattrib->pktlen = pktlen;
		wrqu->length = strlen(extra);
		return 0;
	}
	if (stop == 0) {
		bStartTest = 0; /* To set Stop*/
		pmp_priv->tx.stop = 1;
		sprintf(extra, "Stop continuous Tx");
	} else {
		bStartTest = 1;
		if (pmp_priv->mode != MP_ON) {
			if (pmp_priv->tx.stop != 1) {
				DBG_871X("%s: MP_MODE != ON %d\n", __func__, pmp_priv->mode);
				return	-EFAULT;
			}
		}
	}

	pmp_priv->tx.count = count;

	if (pkTx == 0 || countPkTx == 0)
		pmp_priv->mode = MP_PACKET_TX;
	if (sgleTx == 0)
		pmp_priv->mode = MP_SINGLE_TONE_TX;
	if (cotuTx == 0)
		pmp_priv->mode = MP_CONTINUOUS_TX;
	if (CarrSprTx == 0)
		pmp_priv->mode = MP_CARRIER_SUPPRISSION_TX;
	if (scTx == 0)
		pmp_priv->mode = MP_SINGLE_CARRIER_TX;

	status = rtw_mp_pretx_proc(padapter, bStartTest, extra);

	wrqu->length = strlen(extra);
	return status;
}



int rtw_mp_disable_bt_coexist(struct net_device *dev,
							  struct iw_request_info *info,
							  union iwreq_data *wrqu, char *extra)
{
	PADAPTER padapter = (PADAPTER)rtw_netdev_priv(dev);
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct hal_ops *pHalFunc = &padapter->HalFunc;

	u8 input[wrqu->data.length];
	u32 bt_coexist;

	if (copy_from_user(input, wrqu->data.pointer, wrqu->data.length))
		return -EFAULT;

	bt_coexist = rtw_atoi(input);

	if (bt_coexist == 0) {
		RT_TRACE(_module_mp_, _drv_info_,
				 ("Set OID_RT_SET_DISABLE_BT_COEXIST: disable BT_COEXIST\n"));
		DBG_871X("Set OID_RT_SET_DISABLE_BT_COEXIST: disable BT_COEXIST\n");
#ifdef CONFIG_BT_COEXIST
		rtw_btcoex_HaltNotify(padapter);
		rtw_btcoex_SetManualControl(padapter, _TRUE);
		/* Force to switch Antenna to WiFi*/
		rtw_write16(padapter, 0x870, 0x300);
		rtw_write16(padapter, 0x860, 0x110);
#endif 
		/* CONFIG_BT_COEXIST */
	} else {
		RT_TRACE(_module_mp_, _drv_info_,
				 ("Set OID_RT_SET_DISABLE_BT_COEXIST: enable BT_COEXIST\n"));
#ifdef CONFIG_BT_COEXIST
		rtw_btcoex_SetManualControl(padapter, _FALSE);
#endif
	}

	return 0;
}


int rtw_mp_arx(struct net_device *dev,
			   struct iw_request_info *info,
			   struct iw_point *wrqu, char *extra)
{
	int bStartRx = 0, bStopRx = 0, bQueryPhy = 0, bQueryMac = 0, bSetBssid = 0;
	int bmac_filter = 0, bfilter_init = 0, bmon = 0, bSmpCfg = 0;
	u8		input[wrqu->length];
	char *pch, *ptmp, *token, *tmp[2] = {0x00, 0x00};
	u32 i = 0, ii = 0, jj = 0, kk = 0, cnts = 0, ret;
	PADAPTER padapter = rtw_netdev_priv(dev);
	struct mp_priv *pmppriv = &padapter->mppriv;
	struct dbg_rx_counter rx_counter;

	if (copy_from_user(input, wrqu->pointer, wrqu->length))
		return -EFAULT;

	DBG_871X("%s: %s\n", __func__, input);

	bStartRx = (strncmp(input, "start", 5) == 0) ? 1 : 0; /* strncmp TRUE is 0*/
	bStopRx = (strncmp(input, "stop", 5) == 0) ? 1 : 0; /* strncmp TRUE is 0*/
	bQueryPhy = (strncmp(input, "phy", 3) == 0) ? 1 : 0; /* strncmp TRUE is 0*/
	bQueryMac = (strncmp(input, "mac", 3) == 0) ? 1 : 0; /* strncmp TRUE is 0*/
	bSetBssid = (strncmp(input, "setbssid=", 8) == 0) ? 1 : 0; /* strncmp TRUE is 0*/
	/*bfilter_init = (strncmp(input, "filter_init",11)==0)?1:0;*/
	bmac_filter = (strncmp(input, "accept_mac", 10) == 0) ? 1 : 0;
	bmon = (strncmp(input, "mon=", 4) == 0) ? 1 : 0;
	bSmpCfg = (strncmp(input , "smpcfg=" , 7) == 0) ? 1 : 0;

	if (bSetBssid == 1) {
		pch = input;
		while ((token = strsep(&pch, "=")) != NULL) {
			if (i > 1)
				break;
			tmp[i] = token;
			i++;
		}
		if ((tmp[0] != NULL) && (tmp[1] != NULL)) {
			cnts = strlen(tmp[1]) / 2;
			if (cnts < 1)
				return -EFAULT;
			DBG_871X("%s: cnts=%d\n", __func__, cnts);
			DBG_871X("%s: data=%s\n", __func__, tmp[1]);
			for (jj = 0, kk = 0; jj < cnts ; jj++, kk += 2) {
				pmppriv->network_macaddr[jj] = key_2char2num(tmp[1][kk], tmp[1][kk + 1]);
				DBG_871X("network_macaddr[%d]=%x\n", jj, pmppriv->network_macaddr[jj]);
			} 
		} else
			return -EFAULT;
			
		pmppriv->bSetRxBssid = _TRUE;
	}

	if (bmac_filter) {
		pmppriv->bmac_filter = bmac_filter;
		pch = input;
		while ((token = strsep(&pch, "=")) != NULL) {
			if (i > 1)
				break;
			tmp[i] = token;
			i++;
		}
		if ((tmp[0] != NULL) && (tmp[1] != NULL)) {
			cnts = strlen(tmp[1]) / 2;
			if (cnts < 1)
				return -EFAULT;
			DBG_871X("%s: cnts=%d\n", __func__, cnts);
			DBG_871X("%s: data=%s\n", __func__, tmp[1]);
			for (jj = 0, kk = 0; jj < cnts ; jj++, kk += 2) {
				pmppriv->mac_filter[jj] = key_2char2num(tmp[1][kk], tmp[1][kk + 1]);
				DBG_871X("%s mac_filter[%d]=%x\n", __func__, jj, pmppriv->mac_filter[jj]);
			} 
		} else 
			return -EFAULT;

	}

	if (bStartRx) {
		sprintf(extra, "start");
		SetPacketRx(padapter, bStartRx, _FALSE);
	} else if (bStopRx) {
		SetPacketRx(padapter, bStartRx, _FALSE);
		pmppriv->bmac_filter = _FALSE;
		sprintf(extra, "Received packet OK:%d CRC error:%d ,Filter out:%d", padapter->mppriv.rx_pktcount, padapter->mppriv.rx_crcerrpktcount, padapter->mppriv.rx_pktcount_filter_out);
	} else if (bQueryPhy) {
		_rtw_memset(&rx_counter, 0, sizeof(struct dbg_rx_counter));
		rtw_dump_phy_rx_counters(padapter, &rx_counter);

		DBG_871X("%s: OFDM_FA =%d\n", __func__, rx_counter.rx_ofdm_fa);
		DBG_871X("%s: CCK_FA =%d\n", __func__, rx_counter.rx_cck_fa);
		sprintf(extra, "Phy Received packet OK:%d CRC error:%d FA Counter: %d", rx_counter.rx_pkt_ok, rx_counter.rx_pkt_crc_error, rx_counter.rx_cck_fa + rx_counter.rx_ofdm_fa);


	} else if (bQueryMac) {
		_rtw_memset(&rx_counter, 0, sizeof(struct dbg_rx_counter));
		rtw_dump_mac_rx_counters(padapter, &rx_counter);
		sprintf(extra, "Mac Received packet OK: %d , CRC error: %d , Drop Packets: %d\n",
				rx_counter.rx_pkt_ok, rx_counter.rx_pkt_crc_error, rx_counter.rx_pkt_drop);

	}

	if (bmon == 1) {
		ret = sscanf(input, "mon=%d", &bmon);

		if (bmon == 1) {
			pmppriv->rx_bindicatePkt = _TRUE;
			sprintf(extra, "Indicating Receive Packet to network start\n");
		} else {
			pmppriv->rx_bindicatePkt = _FALSE;
			sprintf(extra, "Indicating Receive Packet to network Stop\n");
		}
	}
	if (bSmpCfg == 1) {
		ret = sscanf(input, "smpcfg=%d", &bSmpCfg);

		if (bSmpCfg == 1) {
			pmppriv->bRTWSmbCfg = _TRUE;
			sprintf(extra , "Indicate By Simple Config Format\n");
			SetPacketRx(padapter, _TRUE, _TRUE);
		} else {
			pmppriv->bRTWSmbCfg = _FALSE;
			sprintf(extra , "Indicate By Normal Format\n");
			SetPacketRx(padapter, _TRUE, _FALSE);
		}
	}

	wrqu->length = strlen(extra) + 1;

	return 0;
}


int rtw_mp_trx_query(struct net_device *dev,
					 struct iw_request_info *info,
					 struct iw_point *wrqu, char *extra)
{
	u32 txok, txfail, rxok, rxfail, rxfilterout;
	PADAPTER padapter = rtw_netdev_priv(dev);

	txok = padapter->mppriv.tx.sended;
	txfail = 0;
	rxok = padapter->mppriv.rx_pktcount;
	rxfail = padapter->mppriv.rx_crcerrpktcount;
	rxfilterout = padapter->mppriv.rx_pktcount_filter_out;

	_rtw_memset(extra, '\0', 128);

	sprintf(extra, "Tx OK:%d, Tx Fail:%d, Rx OK:%d, CRC error:%d ,Rx Filter out:%d\n", txok, txfail, rxok, rxfail, rxfilterout);

	wrqu->length = strlen(extra) + 1;

	return 0;
}


int rtw_mp_pwrtrk(struct net_device *dev,
				  struct iw_request_info *info,
				  struct iw_point *wrqu, char *extra)
{
	u8 enable;
	u32 thermal;
	s32 ret;
	PADAPTER padapter = rtw_netdev_priv(dev);
	HAL_DATA_TYPE			*pHalData = GET_HAL_DATA(padapter);
	u8		input[wrqu->length];

	if (copy_from_user(input, wrqu->pointer, wrqu->length))
		return -EFAULT;

	_rtw_memset(extra, 0, wrqu->length);

	enable = 1;
	if (wrqu->length > 1) {
		/* not empty string*/
		if (strncmp(input, "stop", 4) == 0) {
			enable = 0;
			sprintf(extra, "mp tx power tracking stop");
		} else if (sscanf(input, "ther=%d", &thermal) == 1) {
			ret = SetThermalMeter(padapter, (u8)thermal);
			if (ret == _FAIL)
				return -EPERM;
			sprintf(extra, "mp tx power tracking start,target value=%d ok", thermal);
		} else
			return -EINVAL;
	}

	ret = SetPowerTracking(padapter, enable);
	if (ret == _FAIL)
		return -EPERM;

	wrqu->length = strlen(extra);

	return 0;
}



int rtw_mp_psd(struct net_device *dev,
			   struct iw_request_info *info,
			   struct iw_point *wrqu, char *extra)
{
	PADAPTER padapter = rtw_netdev_priv(dev);
	u8		input[wrqu->length];

	if (copy_from_user(input, wrqu->pointer, wrqu->length))
		return -EFAULT;

	strcpy(extra, input);

	wrqu->length = mp_query_psd(padapter, extra);

	return 0;
}


int rtw_mp_thermal(struct net_device *dev,
				   struct iw_request_info *info,
				   struct iw_point *wrqu, char *extra)
{
	u8 val;
	int bwrite = 1;

#ifdef CONFIG_RTL8188E
	u16 addr = EEPROM_THERMAL_METER_88E;
#endif
#if defined(CONFIG_RTL8812A) || defined(CONFIG_RTL8821A) || defined(CONFIG_RTL8814A)
	u16 addr = EEPROM_THERMAL_METER_8812;
#endif
#ifdef CONFIG_RTL8192E
	u16 addr = EEPROM_THERMAL_METER_8192E;
#endif
#ifdef CONFIG_RTL8723B
	u16 addr = EEPROM_THERMAL_METER_8723B;
#endif
#ifdef CONFIG_RTL8703B
	u16 addr = EEPROM_THERMAL_METER_8703B;
#endif
#ifdef CONFIG_RTL8188F
	u16 addr = EEPROM_THERMAL_METER_8188F;
#endif
	u16 cnt = 1;
	u16 max_available_size = 0;
	PADAPTER padapter = rtw_netdev_priv(dev);

	if (copy_from_user(extra, wrqu->pointer, wrqu->length))
		return -EFAULT;

	bwrite = strncmp(extra, "write", 6);/* strncmp TRUE is 0*/

	GetThermalMeter(padapter, &val);

	if (bwrite == 0) {
		/*DBG_871X("to write val:%d",val);*/
		EFUSE_GetEfuseDefinition(padapter, EFUSE_WIFI, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, (PVOID)&max_available_size, _FALSE);
		if (2 > max_available_size) {
			DBG_871X("no available efuse!\n");
			return -EFAULT;
		}
		if (rtw_efuse_map_write(padapter, addr, cnt, &val) == _FAIL) {
			DBG_871X("rtw_efuse_map_write error\n");
			return -EFAULT;
		}
		sprintf(extra, " efuse write ok :%d", val);
	} else
		sprintf(extra, "%d", val);
	wrqu->length = strlen(extra);

	return 0;
}



int rtw_mp_reset_stats(struct net_device *dev,
					   struct iw_request_info *info,
					   struct iw_point *wrqu, char *extra)
{
	struct mp_priv *pmp_priv;
	struct pkt_attrib *pattrib;
	PADAPTER padapter = rtw_netdev_priv(dev);

	pmp_priv = &padapter->mppriv;

	pmp_priv->tx.sended = 0;
	pmp_priv->tx_pktcount = 0;
	pmp_priv->rx_pktcount = 0;
	pmp_priv->rx_pktcount_filter_out = 0;
	pmp_priv->rx_crcerrpktcount = 0;

	rtw_reset_phy_rx_counters(padapter);
	rtw_reset_mac_rx_counters(padapter);

	return 0;
}


int rtw_mp_dump(struct net_device *dev,
				struct iw_request_info *info,
				struct iw_point *wrqu, char *extra)
{
	struct mp_priv *pmp_priv;
	struct pkt_attrib *pattrib;
	u32 value;
	u8		input[wrqu->length];
	u8 rf_type, path_nums = 0;
	u32 i, j = 1, path;
	PADAPTER padapter = rtw_netdev_priv(dev);

	pmp_priv = &padapter->mppriv;

	if (copy_from_user(input, wrqu->pointer, wrqu->length))
		return -EFAULT;

	if (strncmp(input, "all", 4) == 0) {
		mac_reg_dump(RTW_DBGDUMP, padapter);
		bb_reg_dump(RTW_DBGDUMP, padapter);
		rf_reg_dump(RTW_DBGDUMP, padapter);
	}
	return 0;
}


int rtw_mp_phypara(struct net_device *dev,
				   struct iw_request_info *info,
				   struct iw_point *wrqu, char *extra)
{

	PADAPTER padapter = rtw_netdev_priv(dev);
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(padapter);
	char	input[wrqu->length];
	u32		valxcap, ret;

	if (copy_from_user(input, wrqu->pointer, wrqu->length))
		return -EFAULT;

	DBG_871X("%s:iwpriv in=%s\n", __func__, input);

	ret = sscanf(input, "xcap=%d", &valxcap);

	pHalData->CrystalCap = (u8)valxcap;
	Hal_ProSetCrystalCap(padapter , valxcap);

	sprintf(extra, "Set xcap=%d", valxcap);
	wrqu->length = strlen(extra) + 1;

	return 0;

}


int rtw_mp_SetRFPath(struct net_device *dev,
					 struct iw_request_info *info,
					 union iwreq_data *wrqu, char *extra)
{
	PADAPTER padapter = rtw_netdev_priv(dev);
	char	input[wrqu->data.length];
	int		bMain = 1, bTurnoff = 1;

	if (copy_from_user(input, wrqu->data.pointer, wrqu->data.length))
		return -EFAULT;
	DBG_871X("%s:iwpriv in=%s\n", __func__, input);

	bMain = strncmp(input, "1", 2); /* strncmp TRUE is 0*/
	bTurnoff = strncmp(input, "0", 3); /* strncmp TRUE is 0*/

	if (bMain == 0) {
		MP_PHY_SetRFPathSwitch(padapter, _TRUE);
		DBG_871X("%s:PHY_SetRFPathSwitch=TRUE\n", __func__);
	} else if (bTurnoff == 0) {
		MP_PHY_SetRFPathSwitch(padapter, _FALSE);
		DBG_871X("%s:PHY_SetRFPathSwitch=FALSE\n", __func__);
	}

	return 0;
}


int rtw_mp_QueryDrv(struct net_device *dev,
					struct iw_request_info *info,
					union iwreq_data *wrqu, char *extra)
{
	PADAPTER padapter = rtw_netdev_priv(dev);
	char	input[wrqu->data.length];
	int	qAutoLoad = 1;

	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);

	if (copy_from_user(input, wrqu->data.pointer, wrqu->data.length))
		return -EFAULT;
	DBG_871X("%s:iwpriv in=%s\n", __func__, input);

	qAutoLoad = strncmp(input, "autoload", 8); /* strncmp TRUE is 0*/

	if (qAutoLoad == 0) {
		DBG_871X("%s:qAutoLoad\n", __func__);

		if (pHalData->bautoload_fail_flag)
			sprintf(extra, "fail");
		else
			sprintf(extra, "ok");
	}
	wrqu->data.length = strlen(extra) + 1;
	return 0;
}


int rtw_mp_PwrCtlDM(struct net_device *dev,
					struct iw_request_info *info,
					struct iw_point *wrqu, char *extra)
{
	PADAPTER padapter = rtw_netdev_priv(dev);
	u8		input[wrqu->length];
	int		bstart = 1;

	if (copy_from_user(input, wrqu->pointer, wrqu->length))
		return -EFAULT;

	bstart = strncmp(input, "start", 5); /* strncmp TRUE is 0*/
	if (bstart == 0) {
		sprintf(extra, "PwrCtlDM start\n");
		MPT_PwrCtlDM(padapter, 1);
	} else {
		sprintf(extra, "PwrCtlDM stop\n");
		MPT_PwrCtlDM(padapter, 0);
	}
	wrqu->length = strlen(extra);

	return 0;
}


int rtw_mp_getver(struct net_device *dev,
				  struct iw_request_info *info,
				  union iwreq_data *wrqu, char *extra)
{
	PADAPTER padapter = rtw_netdev_priv(dev);
	struct mp_priv *pmp_priv;

	pmp_priv = &padapter->mppriv;

	if (copy_from_user(extra, wrqu->data.pointer, wrqu->data.length))
		return -EFAULT;

	sprintf(extra, "rtwpriv=%d\n", RTWPRIV_VER_INFO);
	wrqu->data.length = strlen(extra);
	return 0;
}


int rtw_mp_mon(struct net_device *dev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	PADAPTER padapter = rtw_netdev_priv(dev);
	struct mp_priv *pmp_priv = &padapter->mppriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct hal_ops *pHalFunc = &padapter->HalFunc;
	NDIS_802_11_NETWORK_INFRASTRUCTURE networkType;
	int bstart = 1, bstop = 1;
	
	networkType = Ndis802_11Infrastructure;
	if (copy_from_user(extra, wrqu->data.pointer, wrqu->data.length))
		return -EFAULT;

	rtw_pm_set_ips(padapter, IPS_NONE);
	LeaveAllPowerSaveMode(padapter);

#ifdef CONFIG_MP_INCLUDED
	if (init_mp_priv(padapter) == _FAIL)
		DBG_871X("%s: initialize MP private data Fail!\n", __func__);
	padapter->mppriv.channel = 6;

	bstart = strncmp(extra, "start", 5); /* strncmp TRUE is 0*/
	bstop = strncmp(extra, "stop", 4); /* strncmp TRUE is 0*/
	if (bstart == 0) {
		mp_join(padapter, WIFI_FW_ADHOC_STATE);
		SetPacketRx(padapter, _TRUE, _FALSE);
		SetChannel(padapter);
		pmp_priv->rx_bindicatePkt = _TRUE;
		pmp_priv->bRTWSmbCfg = _TRUE;
		sprintf(extra, "monitor mode start\n");
	} else if (bstop == 0) {
		SetPacketRx(padapter, _FALSE, _FALSE);
		pmp_priv->rx_bindicatePkt = _FALSE;
		pmp_priv->bRTWSmbCfg = _FALSE;
		padapter->registrypriv.mp_mode = 1;
		pHalFunc->hal_deinit(padapter);
		padapter->registrypriv.mp_mode = 0;
		pHalFunc->hal_init(padapter);
		/*rtw_disassoc_cmd(padapter, 0, _TRUE);*/
		if (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE) {
			rtw_disassoc_cmd(padapter, 500, _TRUE);
			rtw_indicate_disconnect(padapter);
			/*rtw_free_assoc_resources(padapter, 1);*/
		}
		rtw_pm_set_ips(padapter, IPS_NORMAL);
		sprintf(extra, "monitor mode Stop\n");
	}
#endif
	wrqu->data.length = strlen(extra);
	return 0;
}

int rtw_mp_pretx_proc(PADAPTER padapter, u8 bStartTest, char *extra)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(padapter);
	struct mp_priv *pmp_priv = &padapter->mppriv;
	PMPT_CONTEXT		pMptCtx = &(padapter->mppriv.MptCtx);

		switch (pmp_priv->mode) {

		case MP_PACKET_TX:
						if (bStartTest == 0) {
							pmp_priv->tx.stop = 1;
							pmp_priv->mode = MP_ON;
							sprintf(extra, "Stop continuous Tx");
						} else if (pmp_priv->tx.stop == 1) {
							sprintf(extra, "%s\nStart continuous DA=ffffffffffff len=1500 count=%u\n", extra, pmp_priv->tx.count);
							pmp_priv->tx.stop = 0;
							SetPacketTx(padapter);
						} else {
							return -EFAULT;
						}
						return 0;
		case MP_SINGLE_TONE_TX:
						if (bStartTest != 0)
							sprintf(extra, "%s\nStart continuous DA=ffffffffffff len=1500\n infinite=yes.", extra);
						SetSingleToneTx(padapter, (u8)bStartTest);
						break;
		case MP_CONTINUOUS_TX:
						if (bStartTest != 0)
							sprintf(extra, "%s\nStart continuous DA=ffffffffffff len=1500\n infinite=yes.", extra);
						SetContinuousTx(padapter, (u8)bStartTest);
						break;
		case MP_CARRIER_SUPPRISSION_TX:
						if (bStartTest != 0) {
							if (pmp_priv->rateidx <= MPT_RATE_11M)
								sprintf(extra, "%s\nStart continuous DA=ffffffffffff len=1500\n infinite=yes.", extra);
							else
								sprintf(extra, "%s\nSpecify carrier suppression but not CCK rate", extra);
						}
						SetCarrierSuppressionTx(padapter, (u8)bStartTest);
						break;
		case MP_SINGLE_CARRIER_TX:
					if (bStartTest != 0)
							sprintf(extra, "%s\nStart continuous DA=ffffffffffff len=1500\n infinite=yes.", extra);
						SetSingleCarrierTx(padapter, (u8)bStartTest);
						break;

		default:
						sprintf(extra, "Error! Continuous-Tx is not on-going.");
						return -EFAULT;
		}

		if (bStartTest == 1 && pmp_priv->mode != MP_ON) {
			struct mp_priv *pmp_priv = &padapter->mppriv;

			if (pmp_priv->tx.stop == 0) {
				pmp_priv->tx.stop = 1;
				rtw_msleep_os(5);
			}
#ifdef CONFIG_80211N_HT
			pmp_priv->tx.attrib.ht_en = 1;
#endif
			pmp_priv->tx.stop = 0;
			pmp_priv->tx.count = 1;
			SetPacketTx(padapter);
		} else
			pmp_priv->mode = MP_ON;

#if defined(CONFIG_RTL8812A)
			if (IS_HARDWARE_TYPE_8812AU(padapter)) {
				/* <20130425, Kordan> Turn off OFDM Rx to prevent from CCA causing Tx hang.*/
				if (pmp_priv->mode == MP_PACKET_TX)
					PHY_SetBBReg(padapter, rCCAonSec_Jaguar, BIT3, 1);
				else
					PHY_SetBBReg(padapter, rCCAonSec_Jaguar, BIT3, 0);
			}
#endif

	return 0;
}


int rtw_mp_tx(struct net_device *dev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
		PADAPTER padapter = rtw_netdev_priv(dev);
		HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(padapter);
		struct mp_priv *pmp_priv = &padapter->mppriv;
		PMPT_CONTEXT		pMptCtx = &(padapter->mppriv.MptCtx);

		u32 bandwidth = 0, sg = 0, channel = 6, txpower = 40, rate = 108, ant = 0, txmode = 1, count = 0;
		u8 i = 0, j = 0, bStartTest = 1, status = 0;
		u16 antenna = 0;

		if (copy_from_user(extra, wrqu->data.pointer, wrqu->data.length))
			return -EFAULT;
		DBG_871X("extra = %s\n", extra);

		if (strncmp(extra, "stop", 3) == 0) {
			bStartTest = 0; /* To set Stop*/
			pmp_priv->tx.stop = 1;
			sprintf(extra, "Stop continuous Tx");
			status = rtw_mp_pretx_proc(padapter, bStartTest, extra);
			wrqu->data.length = strlen(extra);
			return status;
		} else if (strncmp(extra, "count", 5) == 0) {
				if (sscanf(extra, "count=%d", &count) < 1)
					DBG_871X("Got Count=%d]\n", count);
				pmp_priv->tx.count = count;
				return 0;
		} else if (strncmp(extra, "setting", 5) == 0) {
				_rtw_memset(extra, 0, wrqu->data.length);
				sprintf(extra, "Current Setting :\n Channel:%d", pmp_priv->channel);
				sprintf(extra, "%s\n Bandwidth:%d", extra, pmp_priv->bandwidth);
				sprintf(extra, "%s\n Rate index:%d", extra, pmp_priv->rateidx);
				sprintf(extra, "%s\n TxPower index:%d", extra, pmp_priv->txpoweridx);
				sprintf(extra, "%s\n Antenna TxPath:%d", extra, pmp_priv->antenna_tx);
				sprintf(extra, "%s\n Antenna RxPath:%d", extra, pmp_priv->antenna_rx);
				sprintf(extra, "%s\n MP Mode:%d", extra, pmp_priv->mode);
				wrqu->data.length = strlen(extra);
				return 0;
		} else {

			if (sscanf(extra, "ch=%d,bw=%d,rate=%d,pwr=%d,ant=%d,tx=%d", &channel, &bandwidth, &rate, &txpower, &ant, &txmode) < 6) {
					DBG_871X("Invalid format [ch=%d,bw=%d,rate=%d,pwr=%d,ant=%d,tx=%d]\n", channel, bandwidth, rate, txpower, ant, txmode);
					_rtw_memset(extra, 0, wrqu->data.length);
					sprintf(extra, "\n Please input correct format as bleow:\n");
					sprintf(extra, "%s\t ch=%d,bw=%d,rate=%d,pwr=%d,ant=%d,tx=%d\n", extra, channel, bandwidth, rate, txpower, ant, txmode);
					sprintf(extra, "%s\n [ ch : BGN = <1~14> , A or AC = <36~165> ]", extra);
					sprintf(extra, "%s\n [ bw : Bandwidth: 0 = 20M, 1 = 40M, 2 = 80M ]", extra);
					sprintf(extra, "%s\n [ rate :	CCK: 1 2 5.5 11M X 2 = < 2 4 11 22 >]", extra);
					sprintf(extra, "%s\n [		OFDM: 6 9 12 18 24 36 48 54M X 2 = < 12 18 24 36 48 72 96 108>", extra);
					sprintf(extra, "%s\n [		HT 1S2SS MCS0 ~ MCS15 : < [MCS0]=128 ~ [MCS7]=135 ~ [MCS15]=143 >", extra);
					sprintf(extra, "%s\n [		HT 3SS MCS16 ~ MCS32 : < [MCS16]=144 ~ [MCS23]=151 ~ [MCS32]=159 >", extra);
					sprintf(extra, "%s\n [		VHT 1SS MCS0 ~ MCS9 : < [MCS0]=160 ~ [MCS9]=169 >", extra);
					sprintf(extra, "%s\n [ txpower : 1~63 power index", extra);
					sprintf(extra, "%s\n [ ant : <A = 1, B = 2, C = 4, D = 8> ,2T ex: AB=3 BC=6 CD=12", extra);
					sprintf(extra, "%s\n [ txmode : < 0 = CONTINUOUS_TX, 1 = PACKET_TX, 2 = SINGLE_TONE_TX, 3 = CARRIER_SUPPRISSION_TX, 4 = SINGLE_CARRIER_TX>\n", extra);
					wrqu->data.length = strlen(extra);
					return status;

			} else {
				DBG_871X("Got format [ch=%d,bw=%d,rate=%d,pwr=%d,ant=%d,tx=%d]\n", channel, bandwidth, rate, txpower, ant, txmode);
				_rtw_memset(extra, 0, wrqu->data.length);
				sprintf(extra, "Change Current channel %d to channel %d", padapter->mppriv.channel , channel);
				padapter->mppriv.channel = channel;
				SetChannel(padapter);
				pHalData->CurrentChannel = channel;

				if (bandwidth == 1)
					bandwidth = CHANNEL_WIDTH_40;
				else if (bandwidth == 2)
					bandwidth = CHANNEL_WIDTH_80;
				sprintf(extra, "%s\nChange Current Bandwidth %d to Bandwidth %d", extra, padapter->mppriv.bandwidth , bandwidth);
				padapter->mppriv.bandwidth = (u8)bandwidth;
				padapter->mppriv.preamble = sg;
				SetBandwidth(padapter);
				pHalData->CurrentChannelBW = bandwidth;

				sprintf(extra, "%s\nSet power level :%d", extra, txpower);
				padapter->mppriv.txpoweridx = (u8)txpower;
				pMptCtx->TxPwrLevel[ODM_RF_PATH_A] = (u8)txpower;
				pMptCtx->TxPwrLevel[ODM_RF_PATH_B] = (u8)txpower;
				pMptCtx->TxPwrLevel[ODM_RF_PATH_C] = (u8)txpower;
				pMptCtx->TxPwrLevel[ODM_RF_PATH_D]  = (u8)txpower;

				DBG_871X("%s: bw=%d sg=%d\n", __func__, bandwidth, sg);

				if (rate <= 0x7f)
					rate = wifirate2_ratetbl_inx((u8)rate);
				else if (rate < 0xC8)
					rate = (rate - 0x80 + MPT_RATE_MCS0);
					/*HT  rate 0x80(MCS0)  ~ 0x8F(MCS15) ~ 0x9F(MCS31) 128~159
					VHT1SS~2SS rate 0xA0 (VHT1SS_MCS0 44) ~ 0xB3 (VHT2SS_MCS9 #63) 160~179
					VHT rate 0xB4 (VHT3SS_MCS0 64) ~ 0xC7 (VHT2SS_MCS9 #83) 180~199
					else
					VHT rate 0x90(VHT1SS_MCS0) ~ 0x99(VHT1SS_MCS9) 144~153
					rate =(rate - MPT_RATE_VHT1SS_MCS0);
					*/
				DBG_871X("%s: rate index=%d\n", __func__, rate);
				if (rate >= MPT_RATE_LAST)
					return -EINVAL;
				sprintf(extra, "%s\nSet data rate to %d index %d", extra, padapter->mppriv.rateidx, rate);

				padapter->mppriv.rateidx = rate;
				pMptCtx->MptRateIndex = rate;
				SetDataRate(padapter);

				sprintf(extra, "%s\nSet Antenna Path :%d",  extra, ant);
				switch (ant) {
				case 1:
					antenna = ANTENNA_A;
					break;
				case 2:
					antenna = ANTENNA_B;
					break;
				case 4:
					antenna = ANTENNA_C;
					break;
				case 8:
					antenna = ANTENNA_D;
					break;
				case 3:
					antenna = ANTENNA_AB;
					break;
				case 5:
					antenna = ANTENNA_AC;
					break;
				case 9:
					antenna = ANTENNA_AD;
					break;
				case 6:
					antenna = ANTENNA_BC;
					break;
				case 10:
					antenna = ANTENNA_BD;
					break;
				case 12:
					antenna = ANTENNA_CD;
					break;
				case 7:
					antenna = ANTENNA_ABC;
					break;
				case 14:
					antenna = ANTENNA_BCD;
					break;
				case 11:
					antenna = ANTENNA_ABD;
					break;
				case 15:
					antenna = ANTENNA_ABCD;
					break;
				}
				DBG_871X("%s: antenna=0x%x\n", __func__, antenna);
				padapter->mppriv.antenna_tx = antenna;
				padapter->mppriv.antenna_rx = antenna;
				pHalData->AntennaTxPath = antenna;
				SetAntenna(padapter);

				if (txmode == 0) {
					pmp_priv->mode = MP_CONTINUOUS_TX;
				} else if (txmode == 1) {
					pmp_priv->mode = MP_PACKET_TX;
					pmp_priv->tx.count = count;
				} else if (txmode == 2) {
					pmp_priv->mode = MP_SINGLE_TONE_TX;
				} else if (txmode == 3) {
					pmp_priv->mode = MP_CARRIER_SUPPRISSION_TX;
				} else if (txmode == 4) {
					pmp_priv->mode = MP_SINGLE_CARRIER_TX;
				}

			status = rtw_mp_pretx_proc(padapter, bStartTest, extra);
			}

		}

		wrqu->data.length = strlen(extra);
		return status;
}


int rtw_mp_rx(struct net_device *dev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	PADAPTER padapter = rtw_netdev_priv(dev);
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(padapter);
	struct mp_priv *pmp_priv = &padapter->mppriv;
	PMPT_CONTEXT		pMptCtx = &(padapter->mppriv.MptCtx);

	u32 bandwidth = 0, sg = 0, channel = 6, ant = 0;
	u16 antenna = 0;
	u8 bStartRx = 0;

	if (copy_from_user(extra, wrqu->data.pointer, wrqu->data.length))
			return -EFAULT;

	if (strncmp(extra, "stop", 4) == 0) {
		_rtw_memset(extra, 0, wrqu->data.length);
		SetPacketRx(padapter, bStartRx, _FALSE);
		pmp_priv->bmac_filter = _FALSE;
		sprintf(extra, "Received packet OK:%d CRC error:%d ,Filter out:%d", padapter->mppriv.rx_pktcount, padapter->mppriv.rx_crcerrpktcount, padapter->mppriv.rx_pktcount_filter_out);
		wrqu->data.length = strlen(extra);
		return 0;

	} else if (sscanf(extra, "ch=%d,bw=%d,ant=%d", &channel, &bandwidth, &ant) < 3) {
		DBG_871X("Invalid format [ch=%d,bw=%d,ant=%d]\n", channel, bandwidth, ant);
		_rtw_memset(extra, 0, wrqu->data.length);
		sprintf(extra, "\n Please input correct format as bleow:\n");
		sprintf(extra, "%s\t ch=%d,bw=%d,ant=%d\n", extra, channel, bandwidth, ant);
		sprintf(extra, "%s\n [ ch : BGN = <1~14> , A or AC = <36~165> ]", extra);
		sprintf(extra, "%s\n [ bw : Bandwidth: 0 = 20M, 1 = 40M, 2 = 80M ]", extra);
		sprintf(extra, "%s\n [ ant : <A = 1, B = 2, C = 4, D = 8> ,2T ex: AB=3 BC=6 CD=12", extra);
		wrqu->data.length = strlen(extra);
		return 0;

	} else {
		bStartRx = 1;
		DBG_871X("Got format [ch=%d,bw=%d,ant=%d]\n", channel, bandwidth, ant);
		_rtw_memset(extra, 0, wrqu->data.length);
		sprintf(extra, "Change Current channel %d to channel %d", padapter->mppriv.channel , channel);
		padapter->mppriv.channel = channel;
		SetChannel(padapter);
		pHalData->CurrentChannel = channel;

		if (bandwidth == 1)
			bandwidth = CHANNEL_WIDTH_40;
		else if (bandwidth == 2)
			bandwidth = CHANNEL_WIDTH_80;
		sprintf(extra, "%s\nChange Current Bandwidth %d to Bandwidth %d", extra, padapter->mppriv.bandwidth , bandwidth);
		padapter->mppriv.bandwidth = (u8)bandwidth;
		padapter->mppriv.preamble = sg;
		SetBandwidth(padapter);
		pHalData->CurrentChannelBW = bandwidth;

		sprintf(extra, "%s\nSet Antenna Path :%d",  extra, ant);
		switch (ant) {
		case 1:
			antenna = ANTENNA_A;
			break;
		case 2:
			antenna = ANTENNA_B;
			break;
		case 4:
			antenna = ANTENNA_C;
			break;
		case 8:
			antenna = ANTENNA_D;
			break;
		case 3:
			antenna = ANTENNA_AB;
			break;
		case 5:
			antenna = ANTENNA_AC;
			break;
		case 9:
			antenna = ANTENNA_AD;
			break;
		case 6:
			antenna = ANTENNA_BC;
			break;
		case 10:
			antenna = ANTENNA_BD;
			break;
		case 12:
			antenna = ANTENNA_CD;
			break;
		case 7:
			antenna = ANTENNA_ABC;
			break;
		case 14:
			antenna = ANTENNA_BCD;
			break;
		case 11:
			antenna = ANTENNA_ABD;
			break;
		case 15:
			antenna = ANTENNA_ABCD;
			break;
		}
		DBG_871X("%s: antenna=0x%x\n", __func__, antenna);
		padapter->mppriv.antenna_tx = antenna;
		padapter->mppriv.antenna_rx = antenna;
		pHalData->AntennaTxPath = antenna;
		SetAntenna(padapter);

		sprintf(extra, "%s\nstart Rx", extra);
		SetPacketRx(padapter, bStartRx, _FALSE);
	}
	wrqu->data.length = strlen(extra);
	return 0;
}

int rtw_mp_hwtx(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	PADAPTER padapter = rtw_netdev_priv(dev);
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(padapter);
	struct mp_priv *pmp_priv = &padapter->mppriv;
	PMPT_CONTEXT		pMptCtx = &(padapter->mppriv.MptCtx);

#if defined(CONFIG_RTL8814A) || defined(CONFIG_RTL8821B) || defined(CONFIG_RTL8822B) || defined(CONFIG_RTL8821C)
	u8		input[wrqu->data.length];

	if (copy_from_user(input, wrqu->data.pointer, wrqu->data.length))
			return -EFAULT;

	_rtw_memset(&pMptCtx->PMacTxInfo, 0, sizeof(RT_PMAC_TX_INFO));
	_rtw_memcpy((void *)&pMptCtx->PMacTxInfo, (void *)input, sizeof(RT_PMAC_TX_INFO));

	mpt_ProSetPMacTx(padapter);
	sprintf(extra, "Set PMac Tx Mode start\n");

	wrqu->data.length = strlen(extra);
#endif
	return 0;

}


int rtw_efuse_mask_file(struct net_device *dev,
						struct iw_request_info *info,
						union iwreq_data *wrqu, char *extra)
{
	char *rtw_efuse_mask_file_path;
	u8 Status;
	PADAPTER padapter = rtw_netdev_priv(dev);
	
	_rtw_memset(maskfileBuffer, 0x00, sizeof(maskfileBuffer));

	if (copy_from_user(extra, wrqu->data.pointer, wrqu->data.length))
		return -EFAULT;

	if (strncmp(extra, "off", 3) == 0 && strlen(extra) < 4) {
		padapter->registrypriv.boffefusemask = 1;
		sprintf(extra, "Turn off Efuse Mask\n");
		wrqu->data.length = strlen(extra);
		return 0;
	}
	if (strncmp(extra, "on", 2) == 0 && strlen(extra) < 3) {
		padapter->registrypriv.boffefusemask = 0;
		sprintf(extra, "Turn on Efuse Mask\n");
		wrqu->data.length = strlen(extra);
		return 0;
	}
	rtw_efuse_mask_file_path = extra;

	if (rtw_is_file_readable(rtw_efuse_mask_file_path) == _TRUE) {
		DBG_871X("%s do rtw_efuse_mask_file_read = %s! ,sizeof maskfileBuffer %zu\n", __func__, rtw_efuse_mask_file_path, sizeof(maskfileBuffer));
		Status = rtw_efuse_file_read(padapter, rtw_efuse_mask_file_path, maskfileBuffer, sizeof(maskfileBuffer));
		if (Status == _TRUE)
			padapter->registrypriv.bFileMaskEfuse = _TRUE;
		sprintf(extra, "efuse mask file read OK\n");
	} else {
		padapter->registrypriv.bFileMaskEfuse = _FALSE;
		sprintf(extra, "efuse mask file readable FAIL\n");
		DBG_871X("%s rtw_is_file_readable fail!\n", __func__);
	}
	wrqu->data.length = strlen(extra);
	return 0;
}


int rtw_efuse_file_map(struct net_device *dev,
					   struct iw_request_info *info,
					   union iwreq_data *wrqu, char *extra)
{
	char *rtw_efuse_file_map_path;
	u8 Status;
	PEFUSE_HAL pEfuseHal;
	PADAPTER padapter = rtw_netdev_priv(dev);
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	
	pEfuseHal = &pHalData->EfuseHal;
	if (copy_from_user(extra, wrqu->data.pointer, wrqu->data.length))
		return -EFAULT;

	rtw_efuse_file_map_path = extra;

	if (rtw_is_file_readable(rtw_efuse_file_map_path) == _TRUE) {
		DBG_871X("%s do rtw_efuse_mask_file_read = %s!\n", __func__, rtw_efuse_file_map_path);
		Status = rtw_efuse_file_read(padapter, rtw_efuse_file_map_path, pEfuseHal->fakeEfuseModifiedMap, sizeof(pEfuseHal->fakeEfuseModifiedMap));
		if (Status == _TRUE)
			sprintf(extra, "efuse file file_read OK\n");
		else
			sprintf(extra, "efuse file file_read FAIL\n");
	} else {
		sprintf(extra, "efuse file readable FAIL\n");
		DBG_871X("%s rtw_is_file_readable fail!\n", __func__);
	}
	wrqu->data.length = strlen(extra);
	return 0;
}

#if defined(CONFIG_RTL8723B)
int rtw_mp_SetBT(struct net_device *dev,
				 struct iw_request_info *info,
				 union iwreq_data *wrqu, char *extra)
{
	PADAPTER padapter = rtw_netdev_priv(dev);
	struct hal_ops *pHalFunc = &padapter->HalFunc;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	BT_REQ_CMD	BtReq;
	PMPT_CONTEXT	pMptCtx = &(padapter->mppriv.MptCtx);
	PBT_RSP_CMD	pBtRsp = (PBT_RSP_CMD)&pMptCtx->mptOutBuf[0];
	char	input[128];
	char *pch, *ptmp, *token, *tmp[2] = {0x00, 0x00};
	u8 setdata[100];
	u8 resetbt = 0x00;
	u8 tempval, BTStatus;
	u8 H2cSetbtmac[6];
	u8 u1H2CBtMpOperParm[4] = {0x01};
	int testmode = 1, ready = 1, trxparam = 1, setgen = 1, getgen = 1, testctrl = 1, testbt = 1, readtherm = 1, setbtmac = 1;
	u32 i = 0, ii = 0, jj = 0, kk = 0, cnts = 0, status = 0;
	PRT_MP_FIRMWARE pBTFirmware = NULL;

	if (copy_from_user(extra, wrqu->data.pointer, wrqu->data.length))
		return -EFAULT;
	if (strlen(extra) < 1)
		return -EFAULT;

	DBG_871X("%s:iwpriv in=%s\n", __func__, extra);
	ready = strncmp(extra, "ready", 5);
	testmode = strncmp(extra, "testmode", 8); /* strncmp TRUE is 0*/
	trxparam = strncmp(extra, "trxparam", 8);
	setgen = strncmp(extra, "setgen", 6);
	getgen = strncmp(extra, "getgen", 6);
	testctrl = strncmp(extra, "testctrl", 8);
	testbt = strncmp(extra, "testbt", 6);
	readtherm = strncmp(extra, "readtherm", 9);
	setbtmac = strncmp(extra, "setbtmac", 8);

	if (strncmp(extra, "dlbt", 4) == 0) {
		pHalData->LastHMEBoxNum = 0;
		padapter->bBTFWReady = _FALSE;
		rtw_write8(padapter, 0xa3, 0x05);
		BTStatus = rtw_read8(padapter, 0xa0);
		DBG_871X("%s: btwmap before read 0xa0 BT Status =0x%x\n", __func__, BTStatus);
		if (BTStatus != 0x04) {
			sprintf(extra, "BT Status not Active DLFW FAIL\n");
			goto exit;
		}

		tempval = rtw_read8(padapter, 0x6B);
		tempval |= BIT7;
		rtw_write8(padapter, 0x6B, tempval);

		/* Attention!! Between 0x6A[14] and 0x6A[15] setting need 100us delay*/
		/* So don't write 0x6A[14]=1 and 0x6A[15]=0 together!*/
		rtw_usleep_os(100);
		/* disable BT power cut*/
		/* 0x6A[14] = 0*/
		tempval = rtw_read8(padapter, 0x6B);
		tempval &= ~BIT6;
		rtw_write8(padapter, 0x6B, tempval);
		rtw_usleep_os(100);
		MPT_PwrCtlDM(padapter, 0);
		rtw_write32(padapter, 0xcc, (rtw_read32(padapter, 0xcc) | 0x00000004));
		rtw_write32(padapter, 0x6b, (rtw_read32(padapter, 0x6b) & 0xFFFFFFEF));
		rtw_msleep_os(600);
		rtw_write32(padapter, 0x6b, (rtw_read32(padapter, 0x6b) | 0x00000010));
		rtw_write32(padapter, 0xcc, (rtw_read32(padapter, 0xcc) & 0xFFFFFFFB));
		rtw_msleep_os(1200);
		pBTFirmware = (PRT_MP_FIRMWARE)rtw_zmalloc(sizeof(RT_MP_FIRMWARE));
		if (pBTFirmware == NULL)
			goto exit;
		padapter->bBTFWReady = _FALSE;
		FirmwareDownloadBT(padapter, pBTFirmware);
		if (pBTFirmware)
			rtw_mfree((u8 *)pBTFirmware, sizeof(RT_MP_FIRMWARE));

		DBG_871X("Wait for FirmwareDownloadBT fw boot!\n");
		rtw_msleep_os(2000);
		_rtw_memset(extra, '\0', wrqu->data.length);
		BtReq.opCodeVer = 1;
		BtReq.OpCode = 0;
		BtReq.paraLength = 0;
		mptbt_BtControlProcess(padapter, &BtReq);
		rtw_msleep_os(100);

		DBG_8192C("FirmwareDownloadBT ready = 0x%x 0x%x", pMptCtx->mptOutBuf[4], pMptCtx->mptOutBuf[5]);
		if ((pMptCtx->mptOutBuf[4] == 0x00) && (pMptCtx->mptOutBuf[5] == 0x00)) {

			if (padapter->mppriv.bTxBufCkFail == _TRUE)
				sprintf(extra, "check TxBuf Fail.\n");
			else
				sprintf(extra, "download FW Fail.\n");
		} else {
			sprintf(extra, "download FW OK.\n");
			goto exit;
		}
		goto exit;
	}
	if (strncmp(extra, "dlfw", 4) == 0) {
		pHalData->LastHMEBoxNum = 0;
		padapter->bBTFWReady = _FALSE;
		rtw_write8(padapter, 0xa3, 0x05);
		BTStatus = rtw_read8(padapter, 0xa0);
		DBG_871X("%s: btwmap before read 0xa0 BT Status =0x%x\n", __func__, BTStatus);
		if (BTStatus != 0x04) {
			sprintf(extra, "BT Status not Active DLFW FAIL\n");
			goto exit;
		}

		tempval = rtw_read8(padapter, 0x6B);
		tempval |= BIT7;
		rtw_write8(padapter, 0x6B, tempval);

		/* Attention!! Between 0x6A[14] and 0x6A[15] setting need 100us delay*/
		/* So don't write 0x6A[14]=1 and 0x6A[15]=0 together!*/
		rtw_usleep_os(100);
		/* disable BT power cut*/
		/* 0x6A[14] = 0*/
		tempval = rtw_read8(padapter, 0x6B);
		tempval &= ~BIT6;
		rtw_write8(padapter, 0x6B, tempval);
		rtw_usleep_os(100);

		MPT_PwrCtlDM(padapter, 0);
		rtw_write32(padapter, 0xcc, (rtw_read32(padapter, 0xcc) | 0x00000004));
		rtw_write32(padapter, 0x6b, (rtw_read32(padapter, 0x6b) & 0xFFFFFFEF));
		rtw_msleep_os(600);
		rtw_write32(padapter, 0x6b, (rtw_read32(padapter, 0x6b) | 0x00000010));
		rtw_write32(padapter, 0xcc, (rtw_read32(padapter, 0xcc) & 0xFFFFFFFB));
		rtw_msleep_os(1200);

#if defined(CONFIG_PLATFORM_SPRD) && (MP_DRIVER == 1)
		/* Pull up BT reset pin.*/
		DBG_871X("%s: pull up BT reset pin when bt start mp test\n", __func__);
		rtw_wifi_gpio_wlan_ctrl(WLAN_BT_PWDN_ON);
#endif
		DBG_871X(" FirmwareDownload!\n");

#if defined(CONFIG_RTL8723B)
		status = rtl8723b_FirmwareDownload(padapter, _FALSE);
#endif
		DBG_871X("Wait for FirmwareDownloadBT fw boot!\n");
		rtw_msleep_os(1000);
#ifdef CONFIG_BT_COEXIST
		rtw_btcoex_HaltNotify(padapter);
		DBG_871X("SetBT btcoex HaltNotify !\n");
		/*hal_btcoex1ant_SetAntPath(padapter);*/
		rtw_btcoex_SetManualControl(padapter, _TRUE);
#endif
		_rtw_memset(extra, '\0', wrqu->data.length);
		BtReq.opCodeVer = 1;
		BtReq.OpCode = 0;
		BtReq.paraLength = 0;
		mptbt_BtControlProcess(padapter, &BtReq);
		rtw_msleep_os(200);

		DBG_8192C("FirmwareDownloadBT ready = 0x%x 0x%x", pMptCtx->mptOutBuf[4], pMptCtx->mptOutBuf[5]);
		if ((pMptCtx->mptOutBuf[4] == 0x00) && (pMptCtx->mptOutBuf[5] == 0x00)) {
			if (padapter->mppriv.bTxBufCkFail == _TRUE)
				sprintf(extra, "check TxBuf Fail.\n");
			else
				sprintf(extra, "download FW Fail.\n");
		} else {
#ifdef CONFIG_BT_COEXIST
			rtw_btcoex_SwitchBtTRxMask(padapter);
#endif
			rtw_msleep_os(200);
			sprintf(extra, "download FW OK.\n");
			goto exit;
		}
		goto exit;
	}

	if (strncmp(extra, "down", 4) == 0) {
		DBG_871X("SetBT down for to hal_init !\n");
#ifdef CONFIG_BT_COEXIST
		rtw_btcoex_SetManualControl(padapter, _FALSE);
		rtw_btcoex_Initialize(padapter);
#endif
		pHalFunc->read_adapter_info(padapter);
		pHalFunc->hal_deinit(padapter);
		pHalFunc->hal_init(padapter);
		rtw_pm_set_ips(padapter, IPS_NONE);
		LeaveAllPowerSaveMode(padapter);
		MPT_PwrCtlDM(padapter, 0);
		rtw_write32(padapter, 0xcc, (rtw_read32(padapter, 0xcc) | 0x00000004));
		rtw_write32(padapter, 0x6b, (rtw_read32(padapter, 0x6b) & 0xFFFFFFEF));
		rtw_msleep_os(600);
		/*rtw_write32(padapter, 0x6a, (rtw_read32(padapter, 0x6a)& 0xFFFFFFFE));*/
		rtw_write32(padapter, 0x6b, (rtw_read32(padapter, 0x6b) | 0x00000010));
		rtw_write32(padapter, 0xcc, (rtw_read32(padapter, 0xcc) & 0xFFFFFFFB));
		rtw_msleep_os(1200);
		goto exit;
	}
	if (strncmp(extra, "disable", 7) == 0) {
		DBG_871X("SetBT disable !\n");
		rtw_write32(padapter, 0x6a, (rtw_read32(padapter, 0x6a) & 0xFFFFFFFB));
		rtw_msleep_os(500);
		goto exit;
	}
	if (strncmp(extra, "enable", 6) == 0) {
		DBG_871X("SetBT enable !\n");
		rtw_write32(padapter, 0x6a, (rtw_read32(padapter, 0x6a) | 0x00000004));
		rtw_msleep_os(500);
		goto exit;
	}
	if (strncmp(extra, "h2c", 3) == 0) {
		DBG_871X("SetBT h2c !\n");
		padapter->bBTFWReady = _TRUE;
		rtw_hal_fill_h2c_cmd(padapter, 0x63, 1, u1H2CBtMpOperParm);
		goto exit;
	}
	if (strncmp(extra, "2ant", 4) == 0) {
		DBG_871X("Set BT 2ant use!\n");
		PHY_SetMacReg(padapter, 0x67, BIT5, 0x1);
		rtw_write32(padapter, 0x948, 0000);

		goto exit;
	}

	if (ready != 0 && testmode != 0 && trxparam != 0 && setgen != 0 && getgen != 0 && testctrl != 0 && testbt != 0 && readtherm != 0 && setbtmac != 0)
		return -EFAULT;

	if (testbt == 0) {
		BtReq.opCodeVer = 1;
		BtReq.OpCode = 6;
		BtReq.paraLength = cnts / 2;
		goto todo;
	}
	if (ready == 0) {
		BtReq.opCodeVer = 1;
		BtReq.OpCode = 0;
		BtReq.paraLength = 0;
		goto todo;
	}

	pch = extra;
	i = 0;
	while ((token = strsep(&pch, ",")) != NULL) {
		if (i > 1)
			break;
		tmp[i] = token;
		i++;
	}

	if ((tmp[0] != NULL) && (tmp[1] != NULL)) {
		cnts = strlen(tmp[1]);
		if (cnts < 1)
			return -EFAULT;

		DBG_871X("%s: cnts=%d\n", __func__, cnts);
		DBG_871X("%s: data=%s\n", __func__, tmp[1]);

		for (jj = 0, kk = 0; jj < cnts; jj++, kk += 2) {
			BtReq.pParamStart[jj] = key_2char2num(tmp[1][kk], tmp[1][kk + 1]);
			/*			DBG_871X("BtReq.pParamStart[%d]=0x%02x\n", jj, BtReq.pParamStart[jj]);*/
		}
	} else 
		return -EFAULT;
		
	if (testmode == 0) {
		BtReq.opCodeVer = 1;
		BtReq.OpCode = 1;
		BtReq.paraLength = 1;
	}
	if (trxparam == 0) {
		BtReq.opCodeVer = 1;
		BtReq.OpCode = 2;
		BtReq.paraLength = cnts / 2;
	}
	if (setgen == 0) {
		DBG_871X("%s: BT_SET_GENERAL\n", __func__);
		BtReq.opCodeVer = 1;
		BtReq.OpCode = 3;/*BT_SET_GENERAL	3*/
		BtReq.paraLength = cnts / 2;
	}
	if (getgen == 0) {
		DBG_871X("%s: BT_GET_GENERAL\n", __func__);
		BtReq.opCodeVer = 1;
		BtReq.OpCode = 4;/*BT_GET_GENERAL	4*/
		BtReq.paraLength = cnts / 2;
	}
	if (readtherm == 0) {
		DBG_871X("%s: BT_GET_GENERAL\n", __func__);
		BtReq.opCodeVer = 1;
		BtReq.OpCode = 4;/*BT_GET_GENERAL	4*/
		BtReq.paraLength = cnts / 2;
	}

	if (testctrl == 0) {
		DBG_871X("%s: BT_TEST_CTRL\n", __func__);
		BtReq.opCodeVer = 1;
		BtReq.OpCode = 5;/*BT_TEST_CTRL	5*/
		BtReq.paraLength = cnts / 2;
	}

	DBG_871X("%s: Req opCodeVer=%d OpCode=%d paraLength=%d\n",
			 __func__, BtReq.opCodeVer, BtReq.OpCode, BtReq.paraLength);

	if (BtReq.paraLength < 1)
		goto todo;
	for (i = 0; i < BtReq.paraLength; i++) {
		DBG_871X("%s: BtReq.pParamStart[%d] = 0x%02x\n",
				 __func__, i, BtReq.pParamStart[i]);
	}

todo:
	_rtw_memset(extra, '\0', wrqu->data.length);

	if (padapter->bBTFWReady == _FALSE) {
		sprintf(extra, "BTFWReady = FALSE.\n");
		goto exit;
	}

	mptbt_BtControlProcess(padapter, &BtReq);

	if (readtherm == 0) {
		sprintf(extra, "BT thermal=");
		for (i = 4; i < pMptCtx->mptOutLen; i++) {
			if ((pMptCtx->mptOutBuf[i] == 0x00) && (pMptCtx->mptOutBuf[i + 1] == 0x00))
				goto exit;

			sprintf(extra, "%s %d ", extra, (pMptCtx->mptOutBuf[i] & 0x1f));
		}
	} else {
		for (i = 4; i < pMptCtx->mptOutLen; i++)
			sprintf(extra, "%s 0x%x ", extra, pMptCtx->mptOutBuf[i]);
	}

exit:
	wrqu->data.length = strlen(extra) + 1;
	DBG_871X("-%s: output len=%d data=%s\n", __func__, wrqu->data.length, extra);

	return status;
}

#endif /*#ifdef CONFIG_RTL8723B*/

#endif
