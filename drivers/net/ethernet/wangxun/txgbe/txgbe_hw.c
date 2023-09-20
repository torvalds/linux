// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2015 - 2022 Beijing WangXun Technology Co., Ltd. */

#include <linux/etherdevice.h>
#include <linux/if_ether.h>
#include <linux/string.h>
#include <linux/iopoll.h>
#include <linux/types.h>
#include <linux/pci.h>

#include "../libwx/wx_type.h"
#include "../libwx/wx_hw.h"
#include "txgbe_type.h"
#include "txgbe_hw.h"

/**
 *  txgbe_init_thermal_sensor_thresh - Inits thermal sensor thresholds
 *  @wx: pointer to hardware structure
 *
 *  Inits the thermal sensor thresholds according to the NVM map
 *  and save off the threshold and location values into mac.thermal_sensor_data
 **/
static void txgbe_init_thermal_sensor_thresh(struct wx *wx)
{
	struct wx_thermal_sensor_data *data = &wx->mac.sensor;

	memset(data, 0, sizeof(struct wx_thermal_sensor_data));

	/* Only support thermal sensors attached to SP physical port 0 */
	if (wx->bus.func)
		return;

	wr32(wx, TXGBE_TS_CTL, TXGBE_TS_CTL_EVAL_MD);

	wr32(wx, WX_TS_INT_EN,
	     WX_TS_INT_EN_ALARM_INT_EN | WX_TS_INT_EN_DALARM_INT_EN);
	wr32(wx, WX_TS_EN, WX_TS_EN_ENA);

	data->alarm_thresh = 100;
	wr32(wx, WX_TS_ALARM_THRE, 677);
	data->dalarm_thresh = 90;
	wr32(wx, WX_TS_DALARM_THRE, 614);
}

/**
 *  txgbe_read_pba_string - Reads part number string from EEPROM
 *  @wx: pointer to hardware structure
 *  @pba_num: stores the part number string from the EEPROM
 *  @pba_num_size: part number string buffer length
 *
 *  Reads the part number string from the EEPROM.
 **/
int txgbe_read_pba_string(struct wx *wx, u8 *pba_num, u32 pba_num_size)
{
	u16 pba_ptr, offset, length, data;
	int ret_val;

	if (!pba_num) {
		wx_err(wx, "PBA string buffer was null\n");
		return -EINVAL;
	}

	ret_val = wx_read_ee_hostif(wx,
				    wx->eeprom.sw_region_offset + TXGBE_PBANUM0_PTR,
				    &data);
	if (ret_val != 0) {
		wx_err(wx, "NVM Read Error\n");
		return ret_val;
	}

	ret_val = wx_read_ee_hostif(wx,
				    wx->eeprom.sw_region_offset + TXGBE_PBANUM1_PTR,
				    &pba_ptr);
	if (ret_val != 0) {
		wx_err(wx, "NVM Read Error\n");
		return ret_val;
	}

	/* if data is not ptr guard the PBA must be in legacy format which
	 * means pba_ptr is actually our second data word for the PBA number
	 * and we can decode it into an ascii string
	 */
	if (data != TXGBE_PBANUM_PTR_GUARD) {
		wx_err(wx, "NVM PBA number is not stored as string\n");

		/* we will need 11 characters to store the PBA */
		if (pba_num_size < 11) {
			wx_err(wx, "PBA string buffer too small\n");
			return -ENOMEM;
		}

		/* extract hex string from data and pba_ptr */
		pba_num[0] = (data >> 12) & 0xF;
		pba_num[1] = (data >> 8) & 0xF;
		pba_num[2] = (data >> 4) & 0xF;
		pba_num[3] = data & 0xF;
		pba_num[4] = (pba_ptr >> 12) & 0xF;
		pba_num[5] = (pba_ptr >> 8) & 0xF;
		pba_num[6] = '-';
		pba_num[7] = 0;
		pba_num[8] = (pba_ptr >> 4) & 0xF;
		pba_num[9] = pba_ptr & 0xF;

		/* put a null character on the end of our string */
		pba_num[10] = '\0';

		/* switch all the data but the '-' to hex char */
		for (offset = 0; offset < 10; offset++) {
			if (pba_num[offset] < 0xA)
				pba_num[offset] += '0';
			else if (pba_num[offset] < 0x10)
				pba_num[offset] += 'A' - 0xA;
		}

		return 0;
	}

	ret_val = wx_read_ee_hostif(wx, pba_ptr, &length);
	if (ret_val != 0) {
		wx_err(wx, "NVM Read Error\n");
		return ret_val;
	}

	if (length == 0xFFFF || length == 0) {
		wx_err(wx, "NVM PBA number section invalid length\n");
		return -EINVAL;
	}

	/* check if pba_num buffer is big enough */
	if (pba_num_size  < (((u32)length * 2) - 1)) {
		wx_err(wx, "PBA string buffer too small\n");
		return -ENOMEM;
	}

	/* trim pba length from start of string */
	pba_ptr++;
	length--;

	for (offset = 0; offset < length; offset++) {
		ret_val = wx_read_ee_hostif(wx, pba_ptr + offset, &data);
		if (ret_val != 0) {
			wx_err(wx, "NVM Read Error\n");
			return ret_val;
		}
		pba_num[offset * 2] = (u8)(data >> 8);
		pba_num[(offset * 2) + 1] = (u8)(data & 0xFF);
	}
	pba_num[offset * 2] = '\0';

	return 0;
}

/**
 *  txgbe_calc_eeprom_checksum - Calculates and returns the checksum
 *  @wx: pointer to hardware structure
 *  @checksum: pointer to cheksum
 *
 *  Returns a negative error code on error
 **/
static int txgbe_calc_eeprom_checksum(struct wx *wx, u16 *checksum)
{
	u16 *eeprom_ptrs = NULL;
	u16 *local_buffer;
	int status;
	u16 i;

	wx_init_eeprom_params(wx);

	eeprom_ptrs = kvmalloc_array(TXGBE_EEPROM_LAST_WORD, sizeof(u16),
				     GFP_KERNEL);
	if (!eeprom_ptrs)
		return -ENOMEM;
	/* Read pointer area */
	status = wx_read_ee_hostif_buffer(wx, 0, TXGBE_EEPROM_LAST_WORD, eeprom_ptrs);
	if (status != 0) {
		wx_err(wx, "Failed to read EEPROM image\n");
		kvfree(eeprom_ptrs);
		return status;
	}
	local_buffer = eeprom_ptrs;

	for (i = 0; i < TXGBE_EEPROM_LAST_WORD; i++)
		if (i != wx->eeprom.sw_region_offset + TXGBE_EEPROM_CHECKSUM)
			*checksum += local_buffer[i];

	if (eeprom_ptrs)
		kvfree(eeprom_ptrs);

	if (*checksum > TXGBE_EEPROM_SUM)
		return -EINVAL;

	*checksum = TXGBE_EEPROM_SUM - *checksum;

	return 0;
}

/**
 *  txgbe_validate_eeprom_checksum - Validate EEPROM checksum
 *  @wx: pointer to hardware structure
 *  @checksum_val: calculated checksum
 *
 *  Performs checksum calculation and validates the EEPROM checksum.  If the
 *  caller does not need checksum_val, the value can be NULL.
 **/
int txgbe_validate_eeprom_checksum(struct wx *wx, u16 *checksum_val)
{
	u16 read_checksum = 0;
	u16 checksum;
	int status;

	/* Read the first word from the EEPROM. If this times out or fails, do
	 * not continue or we could be in for a very long wait while every
	 * EEPROM read fails
	 */
	status = wx_read_ee_hostif(wx, 0, &checksum);
	if (status) {
		wx_err(wx, "EEPROM read failed\n");
		return status;
	}

	checksum = 0;
	status = txgbe_calc_eeprom_checksum(wx, &checksum);
	if (status != 0)
		return status;

	status = wx_read_ee_hostif(wx, wx->eeprom.sw_region_offset +
				   TXGBE_EEPROM_CHECKSUM, &read_checksum);
	if (status != 0)
		return status;

	/* Verify read checksum from EEPROM is the same as
	 * calculated checksum
	 */
	if (read_checksum != checksum) {
		status = -EIO;
		wx_err(wx, "Invalid EEPROM checksum\n");
	}

	/* If the user cares, return the calculated checksum */
	if (checksum_val)
		*checksum_val = checksum;

	return status;
}

static void txgbe_reset_misc(struct wx *wx)
{
	wx_reset_misc(wx);
	txgbe_init_thermal_sensor_thresh(wx);
}

/**
 *  txgbe_reset_hw - Perform hardware reset
 *  @wx: pointer to wx structure
 *
 *  Resets the hardware by resetting the transmit and receive units, masks
 *  and clears all interrupts, perform a PHY reset, and perform a link (MAC)
 *  reset.
 **/
int txgbe_reset_hw(struct wx *wx)
{
	int status;

	/* Call adapter stop to disable tx/rx and clear interrupts */
	status = wx_stop_adapter(wx);
	if (status != 0)
		return status;

	if (!(((wx->subsystem_device_id & WX_NCSI_MASK) == WX_NCSI_SUP) ||
	      ((wx->subsystem_device_id & WX_WOL_MASK) == WX_WOL_SUP)))
		wx_reset_hostif(wx);

	usleep_range(10, 100);

	status = wx_check_flash_load(wx, TXGBE_SPI_ILDR_STATUS_LAN_SW_RST(wx->bus.func));
	if (status != 0)
		return status;

	txgbe_reset_misc(wx);

	/* Store the permanent mac address */
	wx_get_mac_addr(wx, wx->mac.perm_addr);

	/* Store MAC address from RAR0, clear receive address registers, and
	 * clear the multicast table.  Also reset num_rar_entries to 128,
	 * since we modify this value when programming the SAN MAC address.
	 */
	wx->mac.num_rar_entries = TXGBE_SP_RAR_ENTRIES;
	wx_init_rx_addrs(wx);

	pci_set_master(wx->pdev);

	return 0;
}
