// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#define _RTW_DEBUG_C_

#include <rtw_debug.h>
#include <usb_ops_linux.h>

int proc_get_drv_version(char *page, char **start,
			 off_t offset, int count,
			 int *eof, void *data)
{
	int len = 0;

	len += scnprintf(page + len, count - len, "%s\n", DRIVERVERSION);

	*eof = 1;
	return len;
}

int proc_get_write_reg(char *page, char **start,
		       off_t offset, int count,
		       int *eof, void *data)
{
	*eof = 1;
	return 0;
}

int proc_set_write_reg(struct file *file, const char __user *buffer,
		       unsigned long count, void *data)
{
	struct net_device *dev = data;
	struct adapter *padapter = netdev_priv(dev);
	char tmp[32];
	u32 addr, val, len;

	if (count < 3) {
		DBG_88E("argument size is less than 3\n");
		return -EFAULT;
	}

	if (buffer && !copy_from_user(tmp, buffer, sizeof(tmp))) {
		int num = sscanf(tmp, "%x %x %x", &addr, &val, &len);

		if (num !=  3) {
			DBG_88E("invalid write_reg parameter!\n");
			return count;
		}
		switch (len) {
		case 1:
			usb_write8(padapter, addr, (u8)val);
			break;
		case 2:
			usb_write16(padapter, addr, (u16)val);
			break;
		case 4:
			usb_write32(padapter, addr, val);
			break;
		default:
			DBG_88E("error write length =%d", len);
			break;
		}
	}
	return count;
}

static u32 proc_get_read_addr = 0xeeeeeeee;
static u32 proc_get_read_len = 0x4;

int proc_get_read_reg(char *page, char **start,
		      off_t offset, int count,
		      int *eof, void *data)
{
	struct net_device *dev = data;
	struct adapter *padapter = netdev_priv(dev);

	int len = 0;

	if (proc_get_read_addr == 0xeeeeeeee) {
		*eof = 1;
		return len;
	}

	switch (proc_get_read_len) {
	case 1:
		len += scnprintf(page + len, count - len, "usb_read8(0x%x)=0x%x\n",
				 proc_get_read_addr, usb_read8(padapter, proc_get_read_addr));
		break;
	case 2:
		len += scnprintf(page + len, count - len, "usb_read16(0x%x)=0x%x\n",
				 proc_get_read_addr, usb_read16(padapter, proc_get_read_addr));
		break;
	case 4:
		len += scnprintf(page + len, count - len, "usb_read32(0x%x)=0x%x\n",
				 proc_get_read_addr, usb_read32(padapter, proc_get_read_addr));
		break;
	default:
		len += scnprintf(page + len, count - len, "error read length=%d\n",
				 proc_get_read_len);
		break;
	}

	*eof = 1;
	return len;
}

int proc_set_read_reg(struct file *file, const char __user *buffer,
		      unsigned long count, void *data)
{
	char tmp[16];
	u32 addr, len;

	if (count < 2) {
		DBG_88E("argument size is less than 2\n");
		return -EFAULT;
	}

	if (buffer && !copy_from_user(tmp, buffer, sizeof(tmp))) {
		int num = sscanf(tmp, "%x %x", &addr, &len);

		if (num !=  2) {
			DBG_88E("invalid read_reg parameter!\n");
			return count;
		}

		proc_get_read_addr = addr;

		proc_get_read_len = len;
	}

	return count;
}

int proc_get_adapter_state(char *page, char **start,
			   off_t offset, int count,
			   int *eof, void *data)
{
	struct net_device *dev = data;
	struct adapter *padapter = netdev_priv(dev);
	int len = 0;

	len += scnprintf(page + len, count - len, "bSurpriseRemoved=%d, bDriverStopped=%d\n",
			 padapter->bSurpriseRemoved,
			 padapter->bDriverStopped);

	*eof = 1;
	return len;
}

int proc_get_best_channel(char *page, char **start,
			  off_t offset, int count,
			  int *eof, void *data)
{
	struct net_device *dev = data;
	struct adapter *padapter = netdev_priv(dev);
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	int len = 0;
	u32 i, best_channel_24G = 1, index_24G = 0;

	for (i = 0; pmlmeext->channel_set[i].ChannelNum != 0; i++) {
		if (pmlmeext->channel_set[i].ChannelNum == 1)
			index_24G = i;
	}

	for (i = 0; pmlmeext->channel_set[i].ChannelNum != 0; i++) {
		/*  2.4G */
		if (pmlmeext->channel_set[i].ChannelNum == 6) {
			if (pmlmeext->channel_set[i].rx_count < pmlmeext->channel_set[index_24G].rx_count) {
				index_24G = i;
				best_channel_24G = pmlmeext->channel_set[i].ChannelNum;
			}
		}

		/*  debug */
		len += scnprintf(page + len, count - len, "The rx cnt of channel %3d = %d\n",
				 pmlmeext->channel_set[i].ChannelNum,
				 pmlmeext->channel_set[i].rx_count);
	}

	len += scnprintf(page + len, count - len, "best_channel_24G = %d\n", best_channel_24G);

	*eof = 1;
	return len;
}
