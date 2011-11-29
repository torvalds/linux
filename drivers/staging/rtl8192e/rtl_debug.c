/******************************************************************************
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * Based on the r8180 driver, which is:
 * Copyright 2004-2005 Andrea Merello <andreamrl@tiscali.it>, et al.
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
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
******************************************************************************/
#include "rtl_core.h"
#include "r8192E_phy.h"
#include "r8192E_phyreg.h"
#include "r8190P_rtl8256.h" /* RTL8225 Radio frontend */
#include "r8192E_cmdpkt.h"

/****************************************************************************
   -----------------------------PROCFS STUFF-------------------------
*****************************************************************************/
/*This part is related to PROC, which will record some statistics. */
static struct proc_dir_entry *rtl8192_proc;

static int proc_get_stats_ap(char *page, char **start,
			  off_t offset, int count,
			  int *eof, void *data)
{
	struct net_device *dev = data;
	struct r8192_priv *priv = (struct r8192_priv *)rtllib_priv(dev);
	struct rtllib_device *ieee = priv->rtllib;
	struct rtllib_network *target;
	int len = 0;

	list_for_each_entry(target, &ieee->network_list, list) {

		len += snprintf(page + len, count - len,
				"%s ", target->ssid);

		if (target->wpa_ie_len > 0 || target->rsn_ie_len > 0)
			len += snprintf(page + len, count - len,
					"WPA\n");
		else
			len += snprintf(page + len, count - len,
					"non_WPA\n");

	}

	*eof = 1;
	return len;
}

static int proc_get_registers_0(char *page, char **start,
			  off_t offset, int count,
			  int *eof, void *data)
{
	struct net_device *dev = data;

	int len = 0;
	int i, n, page0;

	int max = 0xff;
	page0 = 0x000;

	len += snprintf(page + len, count - len,
			"\n####################page %x##################\n ",
			(page0>>8));
	len += snprintf(page + len, count - len,
			"\nD:  OF > 00 01 02 03 04 05 06 07 08 09 0A 0B "
			"0C 0D 0E 0F");
	for (n = 0; n <= max;) {
		len += snprintf(page + len, count - len, "\nD:  %2x > ", n);
		for (i = 0; i < 16 && n <= max; n++, i++)
			len += snprintf(page + len, count - len,
					"%2.2x ", read_nic_byte(dev,
					(page0 | n)));
	}
	len += snprintf(page + len, count - len, "\n");
	*eof = 1;
	return len;

}
static int proc_get_registers_1(char *page, char **start,
			  off_t offset, int count,
			  int *eof, void *data)
{
	struct net_device *dev = data;

	int len = 0;
	int i, n, page0;

	int max = 0xff;
	page0 = 0x100;

	/* This dump the current register page */
	len += snprintf(page + len, count - len,
			"\n####################page %x##################\n ",
			(page0>>8));
	len += snprintf(page + len, count - len,
			"\nD:  OF > 00 01 02 03 04 05 06 07 08 09 0A 0B "
			"0C 0D 0E 0F");
	for (n = 0; n <= max;) {
		len += snprintf(page + len, count - len,
				"\nD:  %2x > ", n);
		for (i = 0; i < 16 && n <= max; i++, n++)
			len += snprintf(page + len, count - len,
					"%2.2x ", read_nic_byte(dev,
					(page0 | n)));
	}
	len += snprintf(page + len, count - len, "\n");
	*eof = 1;
	return len;

}
static int proc_get_registers_2(char *page, char **start,
			  off_t offset, int count,
			  int *eof, void *data)
{
	struct net_device *dev = data;

	int len = 0;
	int i, n, page0;

	int max = 0xff;
	page0 = 0x200;

	/* This dump the current register page */
	len += snprintf(page + len, count - len,
			"\n####################page %x##################\n ",
			(page0 >> 8));
	len += snprintf(page + len, count - len,
			"\nD:  OF > 00 01 02 03 04 05 06 07 08 09 0A 0B 0C "
			"0D 0E 0F");
	for (n = 0; n <= max;) {
		len += snprintf(page + len, count - len,
				"\nD:  %2x > ", n);
		for (i = 0; i < 16 && n <= max; i++, n++)
			len += snprintf(page + len, count - len,
					"%2.2x ", read_nic_byte(dev,
					(page0 | n)));
	}
	len += snprintf(page + len, count - len, "\n");
	*eof = 1;
	return len;

}
static int proc_get_registers_3(char *page, char **start,
			  off_t offset, int count,
			  int *eof, void *data)
{
	struct net_device *dev = data;

	int len = 0;
	int i, n, page0;

	int max = 0xff;
	page0 = 0x300;

	/* This dump the current register page */
	len += snprintf(page + len, count - len,
			"\n####################page %x##################\n ",
			(page0>>8));
	len += snprintf(page + len, count - len,
			"\nD:  OF > 00 01 02 03 04 05 06 07 08 09 0A 0B "
			"0C 0D 0E 0F");
	for (n = 0; n <= max;) {
		len += snprintf(page + len, count - len,
				"\nD:  %2x > ", n);
		for (i = 0; i < 16 && n <= max; i++, n++)
			len += snprintf(page + len, count - len,
					"%2.2x ", read_nic_byte(dev,
					(page0 | n)));
	}
	len += snprintf(page + len, count - len, "\n");
	*eof = 1;
	return len;

}
static int proc_get_registers_4(char *page, char **start,
			  off_t offset, int count,
			  int *eof, void *data)
{
	struct net_device *dev = data;

	int len = 0;
	int i, n, page0;

	int max = 0xff;
	page0 = 0x400;

	/* This dump the current register page */
	len += snprintf(page + len, count - len,
			"\n####################page %x##################\n ",
			(page0>>8));
	len += snprintf(page + len, count - len,
			"\nD:  OF > 00 01 02 03 04 05 06 07 08 09 0A 0B "
			"0C 0D 0E 0F");
	for (n = 0; n <= max;) {
		len += snprintf(page + len, count - len,
				"\nD:  %2x > ", n);
		for (i = 0; i < 16 && n <= max; i++, n++)
			len += snprintf(page + len, count - len,
					"%2.2x ", read_nic_byte(dev,
					(page0 | n)));
	}
	len += snprintf(page + len, count - len, "\n");
	*eof = 1;
	return len;

}
static int proc_get_registers_5(char *page, char **start,
			  off_t offset, int count,
			  int *eof, void *data)
{
	struct net_device *dev = data;

	int len = 0;
	int i, n, page0;

	int max = 0xff;
	page0 = 0x500;

	/* This dump the current register page */
	len += snprintf(page + len, count - len,
			"\n####################page %x##################\n ",
			(page0 >> 8));
	len += snprintf(page + len, count - len,
			"\nD:  OF > 00 01 02 03 04 05 06 07 08 09 0A 0B "
			"0C 0D 0E 0F");
	for (n = 0; n <= max;) {
		len += snprintf(page + len, count - len,
				"\nD:  %2x > ", n);
		for (i = 0; i < 16 && n <= max; i++, n++)
			len += snprintf(page + len, count - len,
					"%2.2x ", read_nic_byte(dev,
					(page0 | n)));
	}
	len += snprintf(page + len, count - len, "\n");
	*eof = 1;
	return len;

}
static int proc_get_registers_6(char *page, char **start,
			  off_t offset, int count,
			  int *eof, void *data)
{
	struct net_device *dev = data;

	int len = 0;
	int i, n, page0;

	int max = 0xff;
	page0 = 0x600;

	/* This dump the current register page */
	len += snprintf(page + len, count - len,
			"\n####################page %x##################\n ",
			(page0>>8));
	len += snprintf(page + len, count - len,
			"\nD:  OF > 00 01 02 03 04 05 06 07 08 09 0A 0B "
			"0C 0D 0E 0F");
	for (n = 0; n <= max;) {
		len += snprintf(page + len, count - len,
				"\nD:  %2x > ", n);
		for (i = 0; i < 16 && n <= max; i++, n++)
			len += snprintf(page + len, count - len,
					"%2.2x ", read_nic_byte(dev,
					(page0 | n)));
	}
	len += snprintf(page + len, count - len, "\n");
	*eof = 1;
	return len;

}
static int proc_get_registers_7(char *page, char **start,
			  off_t offset, int count,
			  int *eof, void *data)
{
	struct net_device *dev = data;

	int len = 0;
	int i, n, page0;

	int max = 0xff;
	page0 = 0x700;

	/* This dump the current register page */
	len += snprintf(page + len, count - len,
			"\n####################page %x##################\n ",
			(page0 >> 8));
	len += snprintf(page + len, count - len,
			"\nD:  OF > 00 01 02 03 04 05 06 07 08 09 0A 0B 0C "
			"0D 0E 0F");
	for (n = 0; n <= max;) {
		len += snprintf(page + len, count - len,
				"\nD:  %2x > ", n);
		for (i = 0; i < 16 && n <= max; i++, n++)
			len += snprintf(page + len, count - len,
					"%2.2x ", read_nic_byte(dev,
					(page0 | n)));
	}
	len += snprintf(page + len, count - len, "\n");
	*eof = 1;
	return len;

}
static int proc_get_registers_8(char *page, char **start,
			  off_t offset, int count,
			  int *eof, void *data)
{
	struct net_device *dev = data;

	int len = 0;
	int i, n, page0;

	int max = 0xff;
	page0 = 0x800;

	/* This dump the current register page */
	len += snprintf(page + len, count - len,
			"\n####################page %x##################\n",
			(page0 >> 8));
	for (n = 0; n <= max;) {
		len += snprintf(page + len, count - len, "\nD:  %2x > ", n);
		for (i = 0; i < 4 && n <= max; n += 4, i++)
			len += snprintf(page + len, count - len,
					"%8.8x ", rtl8192_QueryBBReg(dev,
					(page0 | n), bMaskDWord));
	}
	len += snprintf(page + len, count - len, "\n");
	*eof = 1;
	return len;

}
static int proc_get_registers_9(char *page, char **start,
			  off_t offset, int count,
			  int *eof, void *data)
{
	struct net_device *dev = data;

	int len = 0;
	int i, n, page0;

	int max = 0xff;
	page0 = 0x900;

	/* This dump the current register page */
	len += snprintf(page + len, count - len,
			"\n####################page %x##################\n",
			(page0>>8));
	for (n = 0; n <= max;) {
		len += snprintf(page + len, count - len, "\nD:  %2x > ", n);
		for (i = 0; i < 4 && n <= max; n += 4, i++)
			len += snprintf(page + len, count - len,
					"%8.8x ", rtl8192_QueryBBReg(dev,
					(page0 | n), bMaskDWord));
	}
	len += snprintf(page + len, count - len, "\n");
	*eof = 1;
	return len;
}
static int proc_get_registers_a(char *page, char **start,
			  off_t offset, int count,
			  int *eof, void *data)
{
	struct net_device *dev = data;

	int len = 0;
	int i, n, page0;

	int max = 0xff;
	page0 = 0xa00;

	/* This dump the current register page */
	len += snprintf(page + len, count - len,
			"\n####################page %x##################\n",
			(page0>>8));
	for (n = 0; n <= max;) {
		len += snprintf(page + len, count - len, "\nD:  %2x > ", n);
		for (i = 0; i < 4 && n <= max; n += 4, i++)
			len += snprintf(page + len, count - len,
					"%8.8x ", rtl8192_QueryBBReg(dev,
					(page0 | n), bMaskDWord));
	}
	len += snprintf(page + len, count - len, "\n");
	*eof = 1;
	return len;
}
static int proc_get_registers_b(char *page, char **start,
			  off_t offset, int count,
			  int *eof, void *data)
{
	struct net_device *dev = data;

	int len = 0;
	int i, n, page0;

	int max = 0xff;
	page0 = 0xb00;

	/* This dump the current register page */
	len += snprintf(page + len, count - len,
			"\n####################page %x##################\n",
			(page0 >> 8));
	for (n = 0; n <= max;) {
		len += snprintf(page + len, count - len, "\nD:  %2x > ", n);
		for (i = 0; i < 4 && n <= max; n += 4, i++)
			len += snprintf(page + len, count - len,
					"%8.8x ", rtl8192_QueryBBReg(dev,
					(page0 | n), bMaskDWord));
	}
	len += snprintf(page + len, count - len, "\n");
	*eof = 1;
	return len;
}
static int proc_get_registers_c(char *page, char **start,
			  off_t offset, int count,
			  int *eof, void *data)
{
	struct net_device *dev = data;

	int len = 0;
	int i, n, page0;

	int max = 0xff;
	page0 = 0xc00;

	/* This dump the current register page */
	len += snprintf(page + len, count - len,
			"\n####################page %x##################\n",
			(page0>>8));
	for (n = 0; n <= max;) {
		len += snprintf(page + len, count - len, "\nD:  %2x > ", n);
		for (i = 0; i < 4 && n <= max; n += 4, i++)
			len += snprintf(page + len, count - len,
					"%8.8x ", rtl8192_QueryBBReg(dev,
					(page0 | n), bMaskDWord));
	}
	len += snprintf(page + len, count - len, "\n");
	*eof = 1;
	return len;
}
static int proc_get_registers_d(char *page, char **start,
			  off_t offset, int count,
			  int *eof, void *data)
{
	struct net_device *dev = data;

	int len = 0;
	int i, n, page0;

	int max = 0xff;
	page0 = 0xd00;

	/* This dump the current register page */
	len += snprintf(page + len, count - len,
			"\n####################page %x##################\n",
			(page0>>8));
	for (n = 0; n <= max;) {
		len += snprintf(page + len, count - len, "\nD:  %2x > ", n);
		for (i = 0; i < 4 && n <= max; n += 4, i++)
			len += snprintf(page + len, count - len,
					"%8.8x ", rtl8192_QueryBBReg(dev,
					(page0 | n), bMaskDWord));
	}
	len += snprintf(page + len, count - len, "\n");
	*eof = 1;
	return len;
}
static int proc_get_registers_e(char *page, char **start,
			  off_t offset, int count,
			  int *eof, void *data)
{
	struct net_device *dev = data;

	int len = 0;
	int i, n, page0;

	int max = 0xff;
	page0 = 0xe00;

	/* This dump the current register page */
	len += snprintf(page + len, count - len,
			"\n####################page %x##################\n",
			(page0>>8));
	for (n = 0; n <= max;) {
		len += snprintf(page + len, count - len, "\nD:  %2x > ", n);
		for (i = 0; i < 4 && n <= max; n += 4, i++)
			len += snprintf(page + len, count - len,
					"%8.8x ", rtl8192_QueryBBReg(dev,
					(page0 | n), bMaskDWord));
	}
	len += snprintf(page + len, count - len, "\n");
	*eof = 1;
	return len;
}

static int proc_get_reg_rf_a(char *page, char **start,
			  off_t offset, int count,
			  int *eof, void *data)
{
	struct net_device *dev = data;

	int len = 0;
	int i, n;

	int max = 0xff;

	/* This dump the current register page */
	len += snprintf(page + len, count - len,
			"\n#################### RF-A ##################\n ");
	for (n = 0; n <= max;) {
		len += snprintf(page + len, count - len, "\nD:  %2x > ", n);
		for (i = 0; i < 4 && n <= max; n += 4, i++)
			len += snprintf(page + len, count - len,
					"%8.8x ", rtl8192_phy_QueryRFReg(dev,
					(enum rf90_radio_path)RF90_PATH_A, n,
					bMaskDWord));
	}
	len += snprintf(page + len, count - len, "\n");
	*eof = 1;
	return len;
}

static int proc_get_reg_rf_b(char *page, char **start,
			  off_t offset, int count,
			  int *eof, void *data)
{
	struct net_device *dev = data;

	int len = 0;
	int i, n;

	int max = 0xff;

	/* This dump the current register page */
	len += snprintf(page + len, count - len,
			"\n#################### RF-B ##################\n ");
	for (n = 0; n <= max;) {
		len += snprintf(page + len, count - len, "\nD:  %2x > ", n);
		for (i = 0; i < 4 && n <= max; n += 4, i++)
			len += snprintf(page + len, count - len,
					"%8.8x ", rtl8192_phy_QueryRFReg(dev,
					(enum rf90_radio_path)RF90_PATH_B, n,
					bMaskDWord));
	}
	len += snprintf(page + len, count - len, "\n");
	*eof = 1;
	return len;
}

static int proc_get_reg_rf_c(char *page, char **start,
			  off_t offset, int count,
			  int *eof, void *data)
{
	struct net_device *dev = data;

	int len = 0;
	int i, n;

	int max = 0xff;

	/* This dump the current register page */
	len += snprintf(page + len, count - len,
			"\n#################### RF-C ##################\n");
	for (n = 0; n <= max;) {
		len += snprintf(page + len, count - len, "\nD:  %2x > ", n);
		for (i = 0; i < 4 && n <= max; n += 4, i++)
			len += snprintf(page + len, count - len,
					"%8.8x ", rtl8192_phy_QueryRFReg(dev,
					(enum rf90_radio_path)RF90_PATH_C, n,
					bMaskDWord));
	}
	len += snprintf(page + len, count - len, "\n");
	*eof = 1;
	return len;
}

static int proc_get_reg_rf_d(char *page, char **start,
			  off_t offset, int count,
			  int *eof, void *data)
{
	struct net_device *dev = data;

	int len = 0;
	int i, n;

	int max = 0xff;

	/* This dump the current register page */
	len += snprintf(page + len, count - len,
			"\n#################### RF-D ##################\n ");
	for (n = 0; n <= max;) {
		len += snprintf(page + len, count - len, "\nD:  %2x > ", n);
		for (i = 0; i < 4 && n <= max; n += 4, i++)
			len += snprintf(page + len, count - len,
					"%8.8x ", rtl8192_phy_QueryRFReg(dev,
					(enum rf90_radio_path)RF90_PATH_D, n,
					bMaskDWord));
	}
	len += snprintf(page + len, count - len, "\n");
	*eof = 1;
	return len;
}

static int proc_get_cam_register_1(char *page, char **start,
			  off_t offset, int count,
			  int *eof, void *data)
{
	struct net_device *dev = data;
	u32 target_command = 0;
	u32 target_content = 0;
	u8 entry_i = 0;
	u32 ulStatus;
	int len = 0;
	int i = 100, j = 0;

	/* This dump the current register page */
	len += snprintf(page + len, count - len,
			"\n#################### SECURITY CAM (0-10) ######"
			"############\n ");
	for (j = 0; j < 11; j++) {
		len += snprintf(page + len, count - len, "\nD:  %2x > ", j);
		for (entry_i = 0; entry_i < CAM_CONTENT_COUNT; entry_i++) {
			target_command = entry_i+CAM_CONTENT_COUNT*j;
			target_command = target_command | BIT31;

			while ((i--) >= 0) {
				ulStatus = read_nic_dword(dev, RWCAM);
				if (ulStatus & BIT31)
					continue;
				else
					break;
			}
			write_nic_dword(dev, RWCAM, target_command);
			target_content = read_nic_dword(dev, RCAMO);
			len += snprintf(page + len, count - len, "%8.8x ",
					target_content);
		}
	}

	len += snprintf(page + len, count - len, "\n");
	*eof = 1;
	return len;
}

static int proc_get_cam_register_2(char *page, char **start,
			  off_t offset, int count,
			  int *eof, void *data)
{
	struct net_device *dev = data;
	u32 target_command = 0;
	u32 target_content = 0;
	u8 entry_i = 0;
	u32 ulStatus;
	int len = 0;
	int i = 100, j = 0;

	/* This dump the current register page */
	len += snprintf(page + len, count - len,
			"\n#################### SECURITY CAM (11-21) "
			"##################\n ");
	for (j = 11; j < 22; j++) {
		len += snprintf(page + len, count - len, "\nD:  %2x > ", j);
		for (entry_i = 0; entry_i < CAM_CONTENT_COUNT; entry_i++) {
			target_command = entry_i + CAM_CONTENT_COUNT * j;
			target_command = target_command | BIT31;

			while ((i--) >= 0) {
				ulStatus = read_nic_dword(dev, RWCAM);
				if (ulStatus & BIT31)
					continue;
				else
					break;
			}
			write_nic_dword(dev, RWCAM, target_command);
			target_content = read_nic_dword(dev, RCAMO);
			len += snprintf(page + len, count - len, "%8.8x ",
					target_content);
		}
	}

	len += snprintf(page + len, count - len, "\n");
	*eof = 1;
	return len;
}

static int proc_get_cam_register_3(char *page, char **start,
			  off_t offset, int count,
			  int *eof, void *data)
{
	struct net_device *dev = data;
	u32 target_command = 0;
	u32 target_content = 0;
	u8 entry_i = 0;
	u32 ulStatus;
	int len = 0;
	int i = 100, j = 0;

	/* This dump the current register page */
	len += snprintf(page + len, count - len,
			"\n#################### SECURITY CAM (22-31) ######"
			"############\n ");
	for (j = 22; j < TOTAL_CAM_ENTRY; j++) {
		len += snprintf(page + len, count - len, "\nD:  %2x > ", j);
		for (entry_i = 0; entry_i < CAM_CONTENT_COUNT; entry_i++) {
			target_command = entry_i + CAM_CONTENT_COUNT * j;
			target_command = target_command | BIT31;

			while ((i--) >= 0) {
				ulStatus = read_nic_dword(dev, RWCAM);
				if (ulStatus & BIT31)
					continue;
				else
					break;
			}
			write_nic_dword(dev, RWCAM, target_command);
			target_content = read_nic_dword(dev, RCAMO);
			len += snprintf(page + len, count - len, "%8.8x ",
					target_content);
		}
	}

	len += snprintf(page + len, count - len, "\n");
	*eof = 1;
	return len;
}
static int proc_get_stats_tx(char *page, char **start,
			  off_t offset, int count,
			  int *eof, void *data)
{
	struct net_device *dev = data;
	struct r8192_priv *priv = (struct r8192_priv *)rtllib_priv(dev);

	int len = 0;

	len += snprintf(page + len, count - len,
		"TX VI priority ok int: %lu\n"
		"TX VO priority ok int: %lu\n"
		"TX BE priority ok int: %lu\n"
		"TX BK priority ok int: %lu\n"
		"TX MANAGE priority ok int: %lu\n"
		"TX BEACON priority ok int: %lu\n"
		"TX BEACON priority error int: %lu\n"
		"TX CMDPKT priority ok int: %lu\n"
		"TX queue stopped?: %d\n"
		"TX fifo overflow: %lu\n"
		"TX total data packets %lu\n"
		"TX total data bytes :%lu\n",
		priv->stats.txviokint,
		priv->stats.txvookint,
		priv->stats.txbeokint,
		priv->stats.txbkokint,
		priv->stats.txmanageokint,
		priv->stats.txbeaconokint,
		priv->stats.txbeaconerr,
		priv->stats.txcmdpktokint,
		netif_queue_stopped(dev),
		priv->stats.txoverflow,
		priv->rtllib->stats.tx_packets,
		priv->rtllib->stats.tx_bytes


		);

	*eof = 1;
	return len;
}



static int proc_get_stats_rx(char *page, char **start,
			  off_t offset, int count,
			  int *eof, void *data)
{
	struct net_device *dev = data;
	struct r8192_priv *priv = (struct r8192_priv *)rtllib_priv(dev);

	int len = 0;

	len += snprintf(page + len, count - len,
		"RX packets: %lu\n"
		"RX data crc err: %lu\n"
		"RX mgmt crc err: %lu\n"
		"RX desc err: %lu\n"
		"RX rx overflow error: %lu\n",
		priv->stats.rxint,
		priv->stats.rxdatacrcerr,
		priv->stats.rxmgmtcrcerr,
		priv->stats.rxrdu,
		priv->stats.rxoverflow);

	*eof = 1;
	return len;
}

void rtl8192_proc_module_init(void)
{
	RT_TRACE(COMP_INIT, "Initializing proc filesystem");
	rtl8192_proc = create_proc_entry(DRV_NAME, S_IFDIR, init_net.proc_net);
}


void rtl8192_proc_module_remove(void)
{
	remove_proc_entry(DRV_NAME, init_net.proc_net);
}


void rtl8192_proc_remove_one(struct net_device *dev)
{
	struct r8192_priv *priv = (struct r8192_priv *)rtllib_priv(dev);

	printk(KERN_INFO "dev name %s\n", dev->name);

	if (priv->dir_dev) {
		remove_proc_entry("stats-tx", priv->dir_dev);
		remove_proc_entry("stats-rx", priv->dir_dev);
		remove_proc_entry("stats-ap", priv->dir_dev);
		remove_proc_entry("registers-0", priv->dir_dev);
		remove_proc_entry("registers-1", priv->dir_dev);
		remove_proc_entry("registers-2", priv->dir_dev);
		remove_proc_entry("registers-3", priv->dir_dev);
		remove_proc_entry("registers-4", priv->dir_dev);
		remove_proc_entry("registers-5", priv->dir_dev);
		remove_proc_entry("registers-6", priv->dir_dev);
		remove_proc_entry("registers-7", priv->dir_dev);
		remove_proc_entry("registers-8", priv->dir_dev);
		remove_proc_entry("registers-9", priv->dir_dev);
		remove_proc_entry("registers-a", priv->dir_dev);
		remove_proc_entry("registers-b", priv->dir_dev);
		remove_proc_entry("registers-c", priv->dir_dev);
		remove_proc_entry("registers-d", priv->dir_dev);
		remove_proc_entry("registers-e", priv->dir_dev);
		remove_proc_entry("RF-A", priv->dir_dev);
		remove_proc_entry("RF-B", priv->dir_dev);
		remove_proc_entry("RF-C", priv->dir_dev);
		remove_proc_entry("RF-D", priv->dir_dev);
		remove_proc_entry("SEC-CAM-1", priv->dir_dev);
		remove_proc_entry("SEC-CAM-2", priv->dir_dev);
		remove_proc_entry("SEC-CAM-3", priv->dir_dev);
		remove_proc_entry("wlan0", rtl8192_proc);
		priv->dir_dev = NULL;
	}
}


void rtl8192_proc_init_one(struct net_device *dev)
{
	struct proc_dir_entry *e;
	struct r8192_priv *priv = (struct r8192_priv *)rtllib_priv(dev);

	priv->dir_dev = create_proc_entry(dev->name,
					  S_IFDIR | S_IRUGO | S_IXUGO,
					  rtl8192_proc);
	if (!priv->dir_dev) {
		RT_TRACE(COMP_ERR, "Unable to initialize /proc/net/rtl8192"
			 "/%s\n", dev->name);
		return;
	}
	e = create_proc_read_entry("stats-rx", S_IFREG | S_IRUGO,
				   priv->dir_dev, proc_get_stats_rx, dev);

	if (!e)
		RT_TRACE(COMP_ERR, "Unable to initialize "
		      "/proc/net/rtl8192/%s/stats-rx\n",
		      dev->name);

	e = create_proc_read_entry("stats-tx", S_IFREG | S_IRUGO,
				   priv->dir_dev, proc_get_stats_tx, dev);

	if (!e)
		RT_TRACE(COMP_ERR, "Unable to initialize "
		      "/proc/net/rtl8192/%s/stats-tx\n",
		      dev->name);

	e = create_proc_read_entry("stats-ap", S_IFREG | S_IRUGO,
				   priv->dir_dev, proc_get_stats_ap, dev);

	if (!e)
		RT_TRACE(COMP_ERR, "Unable to initialize "
		      "/proc/net/rtl8192/%s/stats-ap\n",
		      dev->name);

	e = create_proc_read_entry("registers-0", S_IFREG | S_IRUGO,
				   priv->dir_dev, proc_get_registers_0, dev);
	if (!e)
		RT_TRACE(COMP_ERR, "Unable to initialize "
		      "/proc/net/rtl8192/%s/registers-0\n",
		      dev->name);
	e = create_proc_read_entry("registers-1", S_IFREG | S_IRUGO,
				   priv->dir_dev, proc_get_registers_1, dev);
	if (!e)
		RT_TRACE(COMP_ERR, "Unable to initialize "
		      "/proc/net/rtl8192/%s/registers-1\n",
		      dev->name);
	e = create_proc_read_entry("registers-2", S_IFREG | S_IRUGO,
				   priv->dir_dev, proc_get_registers_2, dev);
	if (!e)
		RT_TRACE(COMP_ERR, "Unable to initialize "
		      "/proc/net/rtl8192/%s/registers-2\n",
		      dev->name);
	e = create_proc_read_entry("registers-3", S_IFREG | S_IRUGO,
				   priv->dir_dev, proc_get_registers_3, dev);
	if (!e)
		RT_TRACE(COMP_ERR, "Unable to initialize "
		      "/proc/net/rtl8192/%s/registers-3\n",
		      dev->name);
	e = create_proc_read_entry("registers-4", S_IFREG | S_IRUGO,
				   priv->dir_dev, proc_get_registers_4, dev);
	if (!e)
		RT_TRACE(COMP_ERR, "Unable to initialize "
		      "/proc/net/rtl8192/%s/registers-4\n",
		      dev->name);
	e = create_proc_read_entry("registers-5", S_IFREG | S_IRUGO,
				   priv->dir_dev, proc_get_registers_5, dev);
	if (!e)
		RT_TRACE(COMP_ERR, "Unable to initialize "
		      "/proc/net/rtl8192/%s/registers-5\n",
		      dev->name);
	e = create_proc_read_entry("registers-6", S_IFREG | S_IRUGO,
				   priv->dir_dev, proc_get_registers_6, dev);
	if (!e)
		RT_TRACE(COMP_ERR, "Unable to initialize "
		      "/proc/net/rtl8192/%s/registers-6\n",
		      dev->name);
	e = create_proc_read_entry("registers-7", S_IFREG | S_IRUGO,
				   priv->dir_dev, proc_get_registers_7, dev);
	if (!e)
		RT_TRACE(COMP_ERR, "Unable to initialize "
		      "/proc/net/rtl8192/%s/registers-7\n",
		      dev->name);
	e = create_proc_read_entry("registers-8", S_IFREG | S_IRUGO,
				   priv->dir_dev, proc_get_registers_8, dev);
	if (!e)
		RT_TRACE(COMP_ERR, "Unable to initialize "
		      "/proc/net/rtl8192/%s/registers-8\n",
		      dev->name);
	e = create_proc_read_entry("registers-9", S_IFREG | S_IRUGO,
				   priv->dir_dev, proc_get_registers_9, dev);
	if (!e)
		RT_TRACE(COMP_ERR, "Unable to initialize "
		      "/proc/net/rtl8192/%s/registers-9\n",
		      dev->name);
	e = create_proc_read_entry("registers-a", S_IFREG | S_IRUGO,
				   priv->dir_dev, proc_get_registers_a, dev);
	if (!e)
		RT_TRACE(COMP_ERR, "Unable to initialize "
		      "/proc/net/rtl8192/%s/registers-a\n",
		      dev->name);
	e = create_proc_read_entry("registers-b", S_IFREG | S_IRUGO,
				   priv->dir_dev, proc_get_registers_b, dev);
	if (!e)
		RT_TRACE(COMP_ERR, "Unable to initialize "
		      "/proc/net/rtl8192/%s/registers-b\n",
		      dev->name);
	e = create_proc_read_entry("registers-c", S_IFREG | S_IRUGO,
				   priv->dir_dev, proc_get_registers_c, dev);
	if (!e)
		RT_TRACE(COMP_ERR, "Unable to initialize "
		      "/proc/net/rtl8192/%s/registers-c\n",
		      dev->name);
	e = create_proc_read_entry("registers-d", S_IFREG | S_IRUGO,
				   priv->dir_dev, proc_get_registers_d, dev);
	if (!e)
		RT_TRACE(COMP_ERR, "Unable to initialize "
		      "/proc/net/rtl8192/%s/registers-d\n",
		      dev->name);
	e = create_proc_read_entry("registers-e", S_IFREG | S_IRUGO,
				   priv->dir_dev, proc_get_registers_e, dev);
	if (!e)
		RT_TRACE(COMP_ERR, "Unable to initialize "
		      "/proc/net/rtl8192/%s/registers-e\n",
		      dev->name);
	e = create_proc_read_entry("RF-A", S_IFREG | S_IRUGO,
				   priv->dir_dev, proc_get_reg_rf_a, dev);
	if (!e)
		RT_TRACE(COMP_ERR, "Unable to initialize "
		      "/proc/net/rtl8192/%s/RF-A\n",
		      dev->name);
	e = create_proc_read_entry("RF-B", S_IFREG | S_IRUGO,
				   priv->dir_dev, proc_get_reg_rf_b, dev);
	if (!e)
		RT_TRACE(COMP_ERR, "Unable to initialize "
		      "/proc/net/rtl8192/%s/RF-B\n",
		      dev->name);
	e = create_proc_read_entry("RF-C", S_IFREG | S_IRUGO,
				   priv->dir_dev, proc_get_reg_rf_c, dev);
	if (!e)
		RT_TRACE(COMP_ERR, "Unable to initialize "
		      "/proc/net/rtl8192/%s/RF-C\n",
		      dev->name);
	e = create_proc_read_entry("RF-D", S_IFREG | S_IRUGO,
				   priv->dir_dev, proc_get_reg_rf_d, dev);
	if (!e)
		RT_TRACE(COMP_ERR, "Unable to initialize "
		      "/proc/net/rtl8192/%s/RF-D\n",
		      dev->name);
	e = create_proc_read_entry("SEC-CAM-1", S_IFREG | S_IRUGO,
				   priv->dir_dev, proc_get_cam_register_1, dev);
	if (!e)
		RT_TRACE(COMP_ERR, "Unable to initialize "
		      "/proc/net/rtl8192/%s/SEC-CAM-1\n",
		      dev->name);
	e = create_proc_read_entry("SEC-CAM-2", S_IFREG | S_IRUGO,
				   priv->dir_dev, proc_get_cam_register_2, dev);
	if (!e)
		RT_TRACE(COMP_ERR, "Unable to initialize "
		      "/proc/net/rtl8192/%s/SEC-CAM-2\n",
		      dev->name);
	e = create_proc_read_entry("SEC-CAM-3", S_IFREG | S_IRUGO,
				   priv->dir_dev, proc_get_cam_register_3, dev);
	if (!e)
		RT_TRACE(COMP_ERR, "Unable to initialize "
		      "/proc/net/rtl8192/%s/SEC-CAM-3\n",
		      dev->name);
}
