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
 *  txgbe_disable_sec_tx_path - Stops the transmit data path
 *  @wx: pointer to hardware structure
 *
 *  Stops the transmit data path and waits for the HW to internally empty
 *  the tx security block
 **/
int txgbe_disable_sec_tx_path(struct wx *wx)
{
	int val;

	wr32m(wx, WX_TSC_CTL, WX_TSC_CTL_TX_DIS, WX_TSC_CTL_TX_DIS);
	return read_poll_timeout(rd32, val, val & WX_TSC_ST_SECTX_RDY,
				 1000, 20000, false, wx, WX_TSC_ST);
}

/**
 *  txgbe_enable_sec_tx_path - Enables the transmit data path
 *  @wx: pointer to hardware structure
 *
 *  Enables the transmit data path.
 **/
void txgbe_enable_sec_tx_path(struct wx *wx)
{
	wr32m(wx, WX_TSC_CTL, WX_TSC_CTL_TX_DIS, 0);
	WX_WRITE_FLUSH(wx);
}

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

	kvfree(eeprom_ptrs);

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

	if (wx->media_type != sp_media_copper) {
		u32 val;

		val = WX_MIS_RST_LAN_RST(wx->bus.func);
		wr32(wx, WX_MIS_RST, val | rd32(wx, WX_MIS_RST));
		WX_WRITE_FLUSH(wx);
		usleep_range(10, 100);
	}

	status = wx_check_flash_load(wx, TXGBE_SPI_ILDR_STATUS_LAN_SW_RST(wx->bus.func));
	if (status != 0)
		return status;

	txgbe_reset_misc(wx);

	if (wx->mac.type != wx_mac_sp) {
		wr32(wx, TXGBE_PX_PF_BME, 0x1);
		wr32m(wx, TXGBE_RDM_RSC_CTL, TXGBE_RDM_RSC_CTL_FREE_CTL,
		      TXGBE_RDM_RSC_CTL_FREE_CTL);
	}

	wx_clear_hw_cntrs(wx);

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
