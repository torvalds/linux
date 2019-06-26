/*
 * Copyright (c) 2012-2017 Qualcomm Atheros, Inc.
 * Copyright (c) 2018-2019, The Linux Foundation. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/moduleparam.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>

#include "wil6210.h"
#include "txrx.h"
#include "wmi.h"
#include "trace.h"

/* set the default max assoc sta to max supported by driver */
uint max_assoc_sta = WIL6210_MAX_CID;
module_param(max_assoc_sta, uint, 0444);
MODULE_PARM_DESC(max_assoc_sta, " Max number of stations associated to the AP");

int agg_wsize; /* = 0; */
module_param(agg_wsize, int, 0644);
MODULE_PARM_DESC(agg_wsize, " Window size for Tx Block Ack after connect;"
		 " 0 - use default; < 0 - don't auto-establish");

u8 led_id = WIL_LED_INVALID_ID;
module_param(led_id, byte, 0444);
MODULE_PARM_DESC(led_id,
		 " 60G device led enablement. Set the led ID (0-2) to enable");

#define WIL_WAIT_FOR_SUSPEND_RESUME_COMP 200
#define WIL_WMI_CALL_GENERAL_TO_MS 100
#define WIL_WMI_PCP_STOP_TO_MS 5000

/**
 * WMI event receiving - theory of operations
 *
 * When firmware about to report WMI event, it fills memory area
 * in the mailbox and raises misc. IRQ. Thread interrupt handler invoked for
 * the misc IRQ, function @wmi_recv_cmd called by thread IRQ handler.
 *
 * @wmi_recv_cmd reads event, allocates memory chunk  and attaches it to the
 * event list @wil->pending_wmi_ev. Then, work queue @wil->wmi_wq wakes up
 * and handles events within the @wmi_event_worker. Every event get detached
 * from list, processed and deleted.
 *
 * Purpose for this mechanism is to release IRQ thread; otherwise,
 * if WMI event handling involves another WMI command flow, this 2-nd flow
 * won't be completed because of blocked IRQ thread.
 */

/**
 * Addressing - theory of operations
 *
 * There are several buses present on the WIL6210 card.
 * Same memory areas are visible at different address on
 * the different busses. There are 3 main bus masters:
 *  - MAC CPU (ucode)
 *  - User CPU (firmware)
 *  - AHB (host)
 *
 * On the PCI bus, there is one BAR (BAR0) of 2Mb size, exposing
 * AHB addresses starting from 0x880000
 *
 * Internally, firmware uses addresses that allow faster access but
 * are invisible from the host. To read from these addresses, alternative
 * AHB address must be used.
 */

/**
 * @sparrow_fw_mapping provides memory remapping table for sparrow
 *
 * array size should be in sync with the declaration in the wil6210.h
 *
 * Sparrow memory mapping:
 * Linker address         PCI/Host address
 *                        0x880000 .. 0xa80000  2Mb BAR0
 * 0x800000 .. 0x808000   0x900000 .. 0x908000  32k DCCM
 * 0x840000 .. 0x860000   0x908000 .. 0x928000  128k PERIPH
 */
const struct fw_map sparrow_fw_mapping[] = {
	/* FW code RAM 256k */
	{0x000000, 0x040000, 0x8c0000, "fw_code", true, true},
	/* FW data RAM 32k */
	{0x800000, 0x808000, 0x900000, "fw_data", true, true},
	/* periph data 128k */
	{0x840000, 0x860000, 0x908000, "fw_peri", true, true},
	/* various RGF 40k */
	{0x880000, 0x88a000, 0x880000, "rgf", true, true},
	/* AGC table   4k */
	{0x88a000, 0x88b000, 0x88a000, "AGC_tbl", true, true},
	/* Pcie_ext_rgf 4k */
	{0x88b000, 0x88c000, 0x88b000, "rgf_ext", true, true},
	/* mac_ext_rgf 512b */
	{0x88c000, 0x88c200, 0x88c000, "mac_rgf_ext", true, true},
	/* upper area 548k */
	{0x8c0000, 0x949000, 0x8c0000, "upper", true, true},
	/* UCODE areas - accessible by debugfs blobs but not by
	 * wmi_addr_remap. UCODE areas MUST be added AFTER FW areas!
	 */
	/* ucode code RAM 128k */
	{0x000000, 0x020000, 0x920000, "uc_code", false, false},
	/* ucode data RAM 16k */
	{0x800000, 0x804000, 0x940000, "uc_data", false, false},
};

/**
 * @sparrow_d0_mac_rgf_ext - mac_rgf_ext section for Sparrow D0
 * it is a bit larger to support extra features
 */
const struct fw_map sparrow_d0_mac_rgf_ext = {
	0x88c000, 0x88c500, 0x88c000, "mac_rgf_ext", true, true
};

/**
 * @talyn_fw_mapping provides memory remapping table for Talyn
 *
 * array size should be in sync with the declaration in the wil6210.h
 *
 * Talyn memory mapping:
 * Linker address         PCI/Host address
 *                        0x880000 .. 0xc80000  4Mb BAR0
 * 0x800000 .. 0x820000   0xa00000 .. 0xa20000  128k DCCM
 * 0x840000 .. 0x858000   0xa20000 .. 0xa38000  96k PERIPH
 */
const struct fw_map talyn_fw_mapping[] = {
	/* FW code RAM 1M */
	{0x000000, 0x100000, 0x900000, "fw_code", true, true},
	/* FW data RAM 128k */
	{0x800000, 0x820000, 0xa00000, "fw_data", true, true},
	/* periph. data RAM 96k */
	{0x840000, 0x858000, 0xa20000, "fw_peri", true, true},
	/* various RGF 40k */
	{0x880000, 0x88a000, 0x880000, "rgf", true, true},
	/* AGC table 4k */
	{0x88a000, 0x88b000, 0x88a000, "AGC_tbl", true, true},
	/* Pcie_ext_rgf 4k */
	{0x88b000, 0x88c000, 0x88b000, "rgf_ext", true, true},
	/* mac_ext_rgf 1344b */
	{0x88c000, 0x88c540, 0x88c000, "mac_rgf_ext", true, true},
	/* ext USER RGF 4k */
	{0x88d000, 0x88e000, 0x88d000, "ext_user_rgf", true, true},
	/* OTP 4k */
	{0x8a0000, 0x8a1000, 0x8a0000, "otp", true, false},
	/* DMA EXT RGF 64k */
	{0x8b0000, 0x8c0000, 0x8b0000, "dma_ext_rgf", true, true},
	/* upper area 1536k */
	{0x900000, 0xa80000, 0x900000, "upper", true, true},
	/* UCODE areas - accessible by debugfs blobs but not by
	 * wmi_addr_remap. UCODE areas MUST be added AFTER FW areas!
	 */
	/* ucode code RAM 256k */
	{0x000000, 0x040000, 0xa38000, "uc_code", false, false},
	/* ucode data RAM 32k */
	{0x800000, 0x808000, 0xa78000, "uc_data", false, false},
};

/**
 * @talyn_mb_fw_mapping provides memory remapping table for Talyn-MB
 *
 * array size should be in sync with the declaration in the wil6210.h
 *
 * Talyn MB memory mapping:
 * Linker address         PCI/Host address
 *                        0x880000 .. 0xc80000  4Mb BAR0
 * 0x800000 .. 0x820000   0xa00000 .. 0xa20000  128k DCCM
 * 0x840000 .. 0x858000   0xa20000 .. 0xa38000  96k PERIPH
 */
const struct fw_map talyn_mb_fw_mapping[] = {
	/* FW code RAM 768k */
	{0x000000, 0x0c0000, 0x900000, "fw_code", true, true},
	/* FW data RAM 128k */
	{0x800000, 0x820000, 0xa00000, "fw_data", true, true},
	/* periph. data RAM 96k */
	{0x840000, 0x858000, 0xa20000, "fw_peri", true, true},
	/* various RGF 40k */
	{0x880000, 0x88a000, 0x880000, "rgf", true, true},
	/* AGC table 4k */
	{0x88a000, 0x88b000, 0x88a000, "AGC_tbl", true, true},
	/* Pcie_ext_rgf 4k */
	{0x88b000, 0x88c000, 0x88b000, "rgf_ext", true, true},
	/* mac_ext_rgf 2256b */
	{0x88c000, 0x88c8d0, 0x88c000, "mac_rgf_ext", true, true},
	/* ext USER RGF 4k */
	{0x88d000, 0x88e000, 0x88d000, "ext_user_rgf", true, true},
	/* SEC PKA 16k */
	{0x890000, 0x894000, 0x890000, "sec_pka", true, true},
	/* SEC KDF RGF 3096b */
	{0x898000, 0x898c18, 0x898000, "sec_kdf_rgf", true, true},
	/* SEC MAIN 2124b */
	{0x89a000, 0x89a84c, 0x89a000, "sec_main", true, true},
	/* OTP 4k */
	{0x8a0000, 0x8a1000, 0x8a0000, "otp", true, false},
	/* DMA EXT RGF 64k */
	{0x8b0000, 0x8c0000, 0x8b0000, "dma_ext_rgf", true, true},
	/* DUM USER RGF 528b */
	{0x8c0000, 0x8c0210, 0x8c0000, "dum_user_rgf", true, true},
	/* DMA OFU 296b */
	{0x8c2000, 0x8c2128, 0x8c2000, "dma_ofu", true, true},
	/* ucode debug 4k */
	{0x8c3000, 0x8c4000, 0x8c3000, "ucode_debug", true, true},
	/* upper area 1536k */
	{0x900000, 0xa80000, 0x900000, "upper", true, true},
	/* UCODE areas - accessible by debugfs blobs but not by
	 * wmi_addr_remap. UCODE areas MUST be added AFTER FW areas!
	 */
	/* ucode code RAM 256k */
	{0x000000, 0x040000, 0xa38000, "uc_code", false, false},
	/* ucode data RAM 32k */
	{0x800000, 0x808000, 0xa78000, "uc_data", false, false},
};

struct fw_map fw_mapping[MAX_FW_MAPPING_TABLE_SIZE];

struct blink_on_off_time led_blink_time[] = {
	{WIL_LED_BLINK_ON_SLOW_MS, WIL_LED_BLINK_OFF_SLOW_MS},
	{WIL_LED_BLINK_ON_MED_MS, WIL_LED_BLINK_OFF_MED_MS},
	{WIL_LED_BLINK_ON_FAST_MS, WIL_LED_BLINK_OFF_FAST_MS},
};

struct auth_no_hdr {
	__le16 auth_alg;
	__le16 auth_transaction;
	__le16 status_code;
	/* possibly followed by Challenge text */
	u8 variable[0];
} __packed;

u8 led_polarity = LED_POLARITY_LOW_ACTIVE;

/**
 * return AHB address for given firmware internal (linker) address
 * @x - internal address
 * If address have no valid AHB mapping, return 0
 */
static u32 wmi_addr_remap(u32 x)
{
	uint i;

	for (i = 0; i < ARRAY_SIZE(fw_mapping); i++) {
		if (fw_mapping[i].fw &&
		    ((x >= fw_mapping[i].from) && (x < fw_mapping[i].to)))
			return x + fw_mapping[i].host - fw_mapping[i].from;
	}

	return 0;
}

/**
 * find fw_mapping entry by section name
 * @section - section name
 *
 * Return pointer to section or NULL if not found
 */
struct fw_map *wil_find_fw_mapping(const char *section)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(fw_mapping); i++)
		if (fw_mapping[i].name &&
		    !strcmp(section, fw_mapping[i].name))
			return &fw_mapping[i];

	return NULL;
}

/**
 * Check address validity for WMI buffer; remap if needed
 * @ptr - internal (linker) fw/ucode address
 * @size - if non zero, validate the block does not
 *  exceed the device memory (bar)
 *
 * Valid buffer should be DWORD aligned
 *
 * return address for accessing buffer from the host;
 * if buffer is not valid, return NULL.
 */
void __iomem *wmi_buffer_block(struct wil6210_priv *wil, __le32 ptr_, u32 size)
{
	u32 off;
	u32 ptr = le32_to_cpu(ptr_);

	if (ptr % 4)
		return NULL;

	ptr = wmi_addr_remap(ptr);
	if (ptr < WIL6210_FW_HOST_OFF)
		return NULL;

	off = HOSTADDR(ptr);
	if (off > wil->bar_size - 4)
		return NULL;
	if (size && ((off + size > wil->bar_size) || (off + size < off)))
		return NULL;

	return wil->csr + off;
}

void __iomem *wmi_buffer(struct wil6210_priv *wil, __le32 ptr_)
{
	return wmi_buffer_block(wil, ptr_, 0);
}

/**
 * Check address validity
 */
void __iomem *wmi_addr(struct wil6210_priv *wil, u32 ptr)
{
	u32 off;

	if (ptr % 4)
		return NULL;

	if (ptr < WIL6210_FW_HOST_OFF)
		return NULL;

	off = HOSTADDR(ptr);
	if (off > wil->bar_size - 4)
		return NULL;

	return wil->csr + off;
}

int wmi_read_hdr(struct wil6210_priv *wil, __le32 ptr,
		 struct wil6210_mbox_hdr *hdr)
{
	void __iomem *src = wmi_buffer(wil, ptr);

	if (!src)
		return -EINVAL;

	wil_memcpy_fromio_32(hdr, src, sizeof(*hdr));

	return 0;
}

static const char *cmdid2name(u16 cmdid)
{
	switch (cmdid) {
	case WMI_NOTIFY_REQ_CMDID:
		return "WMI_NOTIFY_REQ_CMD";
	case WMI_START_SCAN_CMDID:
		return "WMI_START_SCAN_CMD";
	case WMI_CONNECT_CMDID:
		return "WMI_CONNECT_CMD";
	case WMI_DISCONNECT_CMDID:
		return "WMI_DISCONNECT_CMD";
	case WMI_SW_TX_REQ_CMDID:
		return "WMI_SW_TX_REQ_CMD";
	case WMI_GET_RF_SECTOR_PARAMS_CMDID:
		return "WMI_GET_RF_SECTOR_PARAMS_CMD";
	case WMI_SET_RF_SECTOR_PARAMS_CMDID:
		return "WMI_SET_RF_SECTOR_PARAMS_CMD";
	case WMI_GET_SELECTED_RF_SECTOR_INDEX_CMDID:
		return "WMI_GET_SELECTED_RF_SECTOR_INDEX_CMD";
	case WMI_SET_SELECTED_RF_SECTOR_INDEX_CMDID:
		return "WMI_SET_SELECTED_RF_SECTOR_INDEX_CMD";
	case WMI_BRP_SET_ANT_LIMIT_CMDID:
		return "WMI_BRP_SET_ANT_LIMIT_CMD";
	case WMI_TOF_SESSION_START_CMDID:
		return "WMI_TOF_SESSION_START_CMD";
	case WMI_AOA_MEAS_CMDID:
		return "WMI_AOA_MEAS_CMD";
	case WMI_PMC_CMDID:
		return "WMI_PMC_CMD";
	case WMI_TOF_GET_TX_RX_OFFSET_CMDID:
		return "WMI_TOF_GET_TX_RX_OFFSET_CMD";
	case WMI_TOF_SET_TX_RX_OFFSET_CMDID:
		return "WMI_TOF_SET_TX_RX_OFFSET_CMD";
	case WMI_VRING_CFG_CMDID:
		return "WMI_VRING_CFG_CMD";
	case WMI_BCAST_VRING_CFG_CMDID:
		return "WMI_BCAST_VRING_CFG_CMD";
	case WMI_TRAFFIC_SUSPEND_CMDID:
		return "WMI_TRAFFIC_SUSPEND_CMD";
	case WMI_TRAFFIC_RESUME_CMDID:
		return "WMI_TRAFFIC_RESUME_CMD";
	case WMI_ECHO_CMDID:
		return "WMI_ECHO_CMD";
	case WMI_SET_MAC_ADDRESS_CMDID:
		return "WMI_SET_MAC_ADDRESS_CMD";
	case WMI_LED_CFG_CMDID:
		return "WMI_LED_CFG_CMD";
	case WMI_PCP_START_CMDID:
		return "WMI_PCP_START_CMD";
	case WMI_PCP_STOP_CMDID:
		return "WMI_PCP_STOP_CMD";
	case WMI_SET_SSID_CMDID:
		return "WMI_SET_SSID_CMD";
	case WMI_GET_SSID_CMDID:
		return "WMI_GET_SSID_CMD";
	case WMI_SET_PCP_CHANNEL_CMDID:
		return "WMI_SET_PCP_CHANNEL_CMD";
	case WMI_GET_PCP_CHANNEL_CMDID:
		return "WMI_GET_PCP_CHANNEL_CMD";
	case WMI_P2P_CFG_CMDID:
		return "WMI_P2P_CFG_CMD";
	case WMI_PORT_ALLOCATE_CMDID:
		return "WMI_PORT_ALLOCATE_CMD";
	case WMI_PORT_DELETE_CMDID:
		return "WMI_PORT_DELETE_CMD";
	case WMI_START_LISTEN_CMDID:
		return "WMI_START_LISTEN_CMD";
	case WMI_START_SEARCH_CMDID:
		return "WMI_START_SEARCH_CMD";
	case WMI_DISCOVERY_STOP_CMDID:
		return "WMI_DISCOVERY_STOP_CMD";
	case WMI_DELETE_CIPHER_KEY_CMDID:
		return "WMI_DELETE_CIPHER_KEY_CMD";
	case WMI_ADD_CIPHER_KEY_CMDID:
		return "WMI_ADD_CIPHER_KEY_CMD";
	case WMI_SET_APPIE_CMDID:
		return "WMI_SET_APPIE_CMD";
	case WMI_CFG_RX_CHAIN_CMDID:
		return "WMI_CFG_RX_CHAIN_CMD";
	case WMI_TEMP_SENSE_CMDID:
		return "WMI_TEMP_SENSE_CMD";
	case WMI_DEL_STA_CMDID:
		return "WMI_DEL_STA_CMD";
	case WMI_DISCONNECT_STA_CMDID:
		return "WMI_DISCONNECT_STA_CMD";
	case WMI_RING_BA_EN_CMDID:
		return "WMI_RING_BA_EN_CMD";
	case WMI_RING_BA_DIS_CMDID:
		return "WMI_RING_BA_DIS_CMD";
	case WMI_RCP_DELBA_CMDID:
		return "WMI_RCP_DELBA_CMD";
	case WMI_RCP_ADDBA_RESP_CMDID:
		return "WMI_RCP_ADDBA_RESP_CMD";
	case WMI_RCP_ADDBA_RESP_EDMA_CMDID:
		return "WMI_RCP_ADDBA_RESP_EDMA_CMD";
	case WMI_PS_DEV_PROFILE_CFG_CMDID:
		return "WMI_PS_DEV_PROFILE_CFG_CMD";
	case WMI_SET_MGMT_RETRY_LIMIT_CMDID:
		return "WMI_SET_MGMT_RETRY_LIMIT_CMD";
	case WMI_GET_MGMT_RETRY_LIMIT_CMDID:
		return "WMI_GET_MGMT_RETRY_LIMIT_CMD";
	case WMI_ABORT_SCAN_CMDID:
		return "WMI_ABORT_SCAN_CMD";
	case WMI_NEW_STA_CMDID:
		return "WMI_NEW_STA_CMD";
	case WMI_SET_THERMAL_THROTTLING_CFG_CMDID:
		return "WMI_SET_THERMAL_THROTTLING_CFG_CMD";
	case WMI_GET_THERMAL_THROTTLING_CFG_CMDID:
		return "WMI_GET_THERMAL_THROTTLING_CFG_CMD";
	case WMI_LINK_MAINTAIN_CFG_WRITE_CMDID:
		return "WMI_LINK_MAINTAIN_CFG_WRITE_CMD";
	case WMI_LO_POWER_CALIB_FROM_OTP_CMDID:
		return "WMI_LO_POWER_CALIB_FROM_OTP_CMD";
	case WMI_START_SCHED_SCAN_CMDID:
		return "WMI_START_SCHED_SCAN_CMD";
	case WMI_STOP_SCHED_SCAN_CMDID:
		return "WMI_STOP_SCHED_SCAN_CMD";
	case WMI_TX_STATUS_RING_ADD_CMDID:
		return "WMI_TX_STATUS_RING_ADD_CMD";
	case WMI_RX_STATUS_RING_ADD_CMDID:
		return "WMI_RX_STATUS_RING_ADD_CMD";
	case WMI_TX_DESC_RING_ADD_CMDID:
		return "WMI_TX_DESC_RING_ADD_CMD";
	case WMI_RX_DESC_RING_ADD_CMDID:
		return "WMI_RX_DESC_RING_ADD_CMD";
	case WMI_BCAST_DESC_RING_ADD_CMDID:
		return "WMI_BCAST_DESC_RING_ADD_CMD";
	case WMI_CFG_DEF_RX_OFFLOAD_CMDID:
		return "WMI_CFG_DEF_RX_OFFLOAD_CMD";
	case WMI_LINK_STATS_CMDID:
		return "WMI_LINK_STATS_CMD";
	case WMI_SW_TX_REQ_EXT_CMDID:
		return "WMI_SW_TX_REQ_EXT_CMDID";
	case WMI_FT_AUTH_CMDID:
		return "WMI_FT_AUTH_CMD";
	case WMI_FT_REASSOC_CMDID:
		return "WMI_FT_REASSOC_CMD";
	case WMI_UPDATE_FT_IES_CMDID:
		return "WMI_UPDATE_FT_IES_CMD";
	default:
		return "Untracked CMD";
	}
}

static const char *eventid2name(u16 eventid)
{
	switch (eventid) {
	case WMI_NOTIFY_REQ_DONE_EVENTID:
		return "WMI_NOTIFY_REQ_DONE_EVENT";
	case WMI_DISCONNECT_EVENTID:
		return "WMI_DISCONNECT_EVENT";
	case WMI_SW_TX_COMPLETE_EVENTID:
		return "WMI_SW_TX_COMPLETE_EVENT";
	case WMI_GET_RF_SECTOR_PARAMS_DONE_EVENTID:
		return "WMI_GET_RF_SECTOR_PARAMS_DONE_EVENT";
	case WMI_SET_RF_SECTOR_PARAMS_DONE_EVENTID:
		return "WMI_SET_RF_SECTOR_PARAMS_DONE_EVENT";
	case WMI_GET_SELECTED_RF_SECTOR_INDEX_DONE_EVENTID:
		return "WMI_GET_SELECTED_RF_SECTOR_INDEX_DONE_EVENT";
	case WMI_SET_SELECTED_RF_SECTOR_INDEX_DONE_EVENTID:
		return "WMI_SET_SELECTED_RF_SECTOR_INDEX_DONE_EVENT";
	case WMI_BRP_SET_ANT_LIMIT_EVENTID:
		return "WMI_BRP_SET_ANT_LIMIT_EVENT";
	case WMI_FW_READY_EVENTID:
		return "WMI_FW_READY_EVENT";
	case WMI_TRAFFIC_RESUME_EVENTID:
		return "WMI_TRAFFIC_RESUME_EVENT";
	case WMI_TOF_GET_TX_RX_OFFSET_EVENTID:
		return "WMI_TOF_GET_TX_RX_OFFSET_EVENT";
	case WMI_TOF_SET_TX_RX_OFFSET_EVENTID:
		return "WMI_TOF_SET_TX_RX_OFFSET_EVENT";
	case WMI_VRING_CFG_DONE_EVENTID:
		return "WMI_VRING_CFG_DONE_EVENT";
	case WMI_READY_EVENTID:
		return "WMI_READY_EVENT";
	case WMI_RX_MGMT_PACKET_EVENTID:
		return "WMI_RX_MGMT_PACKET_EVENT";
	case WMI_TX_MGMT_PACKET_EVENTID:
		return "WMI_TX_MGMT_PACKET_EVENT";
	case WMI_SCAN_COMPLETE_EVENTID:
		return "WMI_SCAN_COMPLETE_EVENT";
	case WMI_ACS_PASSIVE_SCAN_COMPLETE_EVENTID:
		return "WMI_ACS_PASSIVE_SCAN_COMPLETE_EVENT";
	case WMI_CONNECT_EVENTID:
		return "WMI_CONNECT_EVENT";
	case WMI_EAPOL_RX_EVENTID:
		return "WMI_EAPOL_RX_EVENT";
	case WMI_BA_STATUS_EVENTID:
		return "WMI_BA_STATUS_EVENT";
	case WMI_RCP_ADDBA_REQ_EVENTID:
		return "WMI_RCP_ADDBA_REQ_EVENT";
	case WMI_DELBA_EVENTID:
		return "WMI_DELBA_EVENT";
	case WMI_RING_EN_EVENTID:
		return "WMI_RING_EN_EVENT";
	case WMI_DATA_PORT_OPEN_EVENTID:
		return "WMI_DATA_PORT_OPEN_EVENT";
	case WMI_AOA_MEAS_EVENTID:
		return "WMI_AOA_MEAS_EVENT";
	case WMI_TOF_SESSION_END_EVENTID:
		return "WMI_TOF_SESSION_END_EVENT";
	case WMI_TOF_GET_CAPABILITIES_EVENTID:
		return "WMI_TOF_GET_CAPABILITIES_EVENT";
	case WMI_TOF_SET_LCR_EVENTID:
		return "WMI_TOF_SET_LCR_EVENT";
	case WMI_TOF_SET_LCI_EVENTID:
		return "WMI_TOF_SET_LCI_EVENT";
	case WMI_TOF_FTM_PER_DEST_RES_EVENTID:
		return "WMI_TOF_FTM_PER_DEST_RES_EVENT";
	case WMI_TOF_CHANNEL_INFO_EVENTID:
		return "WMI_TOF_CHANNEL_INFO_EVENT";
	case WMI_TRAFFIC_SUSPEND_EVENTID:
		return "WMI_TRAFFIC_SUSPEND_EVENT";
	case WMI_ECHO_RSP_EVENTID:
		return "WMI_ECHO_RSP_EVENT";
	case WMI_LED_CFG_DONE_EVENTID:
		return "WMI_LED_CFG_DONE_EVENT";
	case WMI_PCP_STARTED_EVENTID:
		return "WMI_PCP_STARTED_EVENT";
	case WMI_PCP_STOPPED_EVENTID:
		return "WMI_PCP_STOPPED_EVENT";
	case WMI_GET_SSID_EVENTID:
		return "WMI_GET_SSID_EVENT";
	case WMI_GET_PCP_CHANNEL_EVENTID:
		return "WMI_GET_PCP_CHANNEL_EVENT";
	case WMI_P2P_CFG_DONE_EVENTID:
		return "WMI_P2P_CFG_DONE_EVENT";
	case WMI_PORT_ALLOCATED_EVENTID:
		return "WMI_PORT_ALLOCATED_EVENT";
	case WMI_PORT_DELETED_EVENTID:
		return "WMI_PORT_DELETED_EVENT";
	case WMI_LISTEN_STARTED_EVENTID:
		return "WMI_LISTEN_STARTED_EVENT";
	case WMI_SEARCH_STARTED_EVENTID:
		return "WMI_SEARCH_STARTED_EVENT";
	case WMI_DISCOVERY_STOPPED_EVENTID:
		return "WMI_DISCOVERY_STOPPED_EVENT";
	case WMI_CFG_RX_CHAIN_DONE_EVENTID:
		return "WMI_CFG_RX_CHAIN_DONE_EVENT";
	case WMI_TEMP_SENSE_DONE_EVENTID:
		return "WMI_TEMP_SENSE_DONE_EVENT";
	case WMI_RCP_ADDBA_RESP_SENT_EVENTID:
		return "WMI_RCP_ADDBA_RESP_SENT_EVENT";
	case WMI_PS_DEV_PROFILE_CFG_EVENTID:
		return "WMI_PS_DEV_PROFILE_CFG_EVENT";
	case WMI_SET_MGMT_RETRY_LIMIT_EVENTID:
		return "WMI_SET_MGMT_RETRY_LIMIT_EVENT";
	case WMI_GET_MGMT_RETRY_LIMIT_EVENTID:
		return "WMI_GET_MGMT_RETRY_LIMIT_EVENT";
	case WMI_SET_THERMAL_THROTTLING_CFG_EVENTID:
		return "WMI_SET_THERMAL_THROTTLING_CFG_EVENT";
	case WMI_GET_THERMAL_THROTTLING_CFG_EVENTID:
		return "WMI_GET_THERMAL_THROTTLING_CFG_EVENT";
	case WMI_LINK_MAINTAIN_CFG_WRITE_DONE_EVENTID:
		return "WMI_LINK_MAINTAIN_CFG_WRITE_DONE_EVENT";
	case WMI_LO_POWER_CALIB_FROM_OTP_EVENTID:
		return "WMI_LO_POWER_CALIB_FROM_OTP_EVENT";
	case WMI_START_SCHED_SCAN_EVENTID:
		return "WMI_START_SCHED_SCAN_EVENT";
	case WMI_STOP_SCHED_SCAN_EVENTID:
		return "WMI_STOP_SCHED_SCAN_EVENT";
	case WMI_SCHED_SCAN_RESULT_EVENTID:
		return "WMI_SCHED_SCAN_RESULT_EVENT";
	case WMI_TX_STATUS_RING_CFG_DONE_EVENTID:
		return "WMI_TX_STATUS_RING_CFG_DONE_EVENT";
	case WMI_RX_STATUS_RING_CFG_DONE_EVENTID:
		return "WMI_RX_STATUS_RING_CFG_DONE_EVENT";
	case WMI_TX_DESC_RING_CFG_DONE_EVENTID:
		return "WMI_TX_DESC_RING_CFG_DONE_EVENT";
	case WMI_RX_DESC_RING_CFG_DONE_EVENTID:
		return "WMI_RX_DESC_RING_CFG_DONE_EVENT";
	case WMI_CFG_DEF_RX_OFFLOAD_DONE_EVENTID:
		return "WMI_CFG_DEF_RX_OFFLOAD_DONE_EVENT";
	case WMI_LINK_STATS_CONFIG_DONE_EVENTID:
		return "WMI_LINK_STATS_CONFIG_DONE_EVENT";
	case WMI_LINK_STATS_EVENTID:
		return "WMI_LINK_STATS_EVENT";
	case WMI_COMMAND_NOT_SUPPORTED_EVENTID:
		return "WMI_COMMAND_NOT_SUPPORTED_EVENT";
	case WMI_FT_AUTH_STATUS_EVENTID:
		return "WMI_FT_AUTH_STATUS_EVENT";
	case WMI_FT_REASSOC_STATUS_EVENTID:
		return "WMI_FT_REASSOC_STATUS_EVENT";
	default:
		return "Untracked EVENT";
	}
}

static int __wmi_send(struct wil6210_priv *wil, u16 cmdid, u8 mid,
		      void *buf, u16 len)
{
	struct {
		struct wil6210_mbox_hdr hdr;
		struct wmi_cmd_hdr wmi;
	} __packed cmd = {
		.hdr = {
			.type = WIL_MBOX_HDR_TYPE_WMI,
			.flags = 0,
			.len = cpu_to_le16(sizeof(cmd.wmi) + len),
		},
		.wmi = {
			.mid = mid,
			.command_id = cpu_to_le16(cmdid),
		},
	};
	struct wil6210_mbox_ring *r = &wil->mbox_ctl.tx;
	struct wil6210_mbox_ring_desc d_head;
	u32 next_head;
	void __iomem *dst;
	void __iomem *head = wmi_addr(wil, r->head);
	uint retry;
	int rc = 0;

	if (len > r->entry_size - sizeof(cmd)) {
		wil_err(wil, "WMI size too large: %d bytes, max is %d\n",
			(int)(sizeof(cmd) + len), r->entry_size);
		return -ERANGE;
	}

	might_sleep();

	if (!test_bit(wil_status_fwready, wil->status)) {
		wil_err(wil, "WMI: cannot send command while FW not ready\n");
		return -EAGAIN;
	}

	/* Allow sending only suspend / resume commands during susepnd flow */
	if ((test_bit(wil_status_suspending, wil->status) ||
	     test_bit(wil_status_suspended, wil->status) ||
	     test_bit(wil_status_resuming, wil->status)) &&
	     ((cmdid != WMI_TRAFFIC_SUSPEND_CMDID) &&
	      (cmdid != WMI_TRAFFIC_RESUME_CMDID))) {
		wil_err(wil, "WMI: reject send_command during suspend\n");
		return -EINVAL;
	}

	if (!head) {
		wil_err(wil, "WMI head is garbage: 0x%08x\n", r->head);
		return -EINVAL;
	}

	wil_halp_vote(wil);

	/* read Tx head till it is not busy */
	for (retry = 5; retry > 0; retry--) {
		wil_memcpy_fromio_32(&d_head, head, sizeof(d_head));
		if (d_head.sync == 0)
			break;
		msleep(20);
	}
	if (d_head.sync != 0) {
		wil_err(wil, "WMI head busy\n");
		rc = -EBUSY;
		goto out;
	}
	/* next head */
	next_head = r->base + ((r->head - r->base + sizeof(d_head)) % r->size);
	wil_dbg_wmi(wil, "Head 0x%08x -> 0x%08x\n", r->head, next_head);
	/* wait till FW finish with previous command */
	for (retry = 5; retry > 0; retry--) {
		if (!test_bit(wil_status_fwready, wil->status)) {
			wil_err(wil, "WMI: cannot send command while FW not ready\n");
			rc = -EAGAIN;
			goto out;
		}
		r->tail = wil_r(wil, RGF_MBOX +
				offsetof(struct wil6210_mbox_ctl, tx.tail));
		if (next_head != r->tail)
			break;
		msleep(20);
	}
	if (next_head == r->tail) {
		wil_err(wil, "WMI ring full\n");
		rc = -EBUSY;
		goto out;
	}
	dst = wmi_buffer(wil, d_head.addr);
	if (!dst) {
		wil_err(wil, "invalid WMI buffer: 0x%08x\n",
			le32_to_cpu(d_head.addr));
		rc = -EAGAIN;
		goto out;
	}
	cmd.hdr.seq = cpu_to_le16(++wil->wmi_seq);
	/* set command */
	wil_dbg_wmi(wil, "sending %s (0x%04x) [%d] mid %d\n",
		    cmdid2name(cmdid), cmdid, len, mid);
	wil_hex_dump_wmi("Cmd ", DUMP_PREFIX_OFFSET, 16, 1, &cmd,
			 sizeof(cmd), true);
	wil_hex_dump_wmi("cmd ", DUMP_PREFIX_OFFSET, 16, 1, buf,
			 len, true);
	wil_memcpy_toio_32(dst, &cmd, sizeof(cmd));
	wil_memcpy_toio_32(dst + sizeof(cmd), buf, len);
	/* mark entry as full */
	wil_w(wil, r->head + offsetof(struct wil6210_mbox_ring_desc, sync), 1);
	/* advance next ptr */
	wil_w(wil, RGF_MBOX + offsetof(struct wil6210_mbox_ctl, tx.head),
	      r->head = next_head);

	trace_wil6210_wmi_cmd(&cmd.wmi, buf, len);

	/* interrupt to FW */
	wil_w(wil, RGF_USER_USER_ICR + offsetof(struct RGF_ICR, ICS),
	      SW_INT_MBOX);

out:
	wil_halp_unvote(wil);
	return rc;
}

int wmi_send(struct wil6210_priv *wil, u16 cmdid, u8 mid, void *buf, u16 len)
{
	int rc;

	mutex_lock(&wil->wmi_mutex);
	rc = __wmi_send(wil, cmdid, mid, buf, len);
	mutex_unlock(&wil->wmi_mutex);

	return rc;
}

/*=== Event handlers ===*/
static void wmi_evt_ready(struct wil6210_vif *vif, int id, void *d, int len)
{
	struct wil6210_priv *wil = vif_to_wil(vif);
	struct wiphy *wiphy = wil_to_wiphy(wil);
	struct wmi_ready_event *evt = d;
	u8 fw_max_assoc_sta;

	wil_info(wil, "FW ver. %s(SW %d); MAC %pM; %d MID's\n",
		 wil->fw_version, le32_to_cpu(evt->sw_version),
		 evt->mac, evt->numof_additional_mids);
	if (evt->numof_additional_mids + 1 < wil->max_vifs) {
		wil_err(wil, "FW does not support enough MIDs (need %d)",
			wil->max_vifs - 1);
		return; /* FW load will fail after timeout */
	}
	/* ignore MAC address, we already have it from the boot loader */
	strlcpy(wiphy->fw_version, wil->fw_version, sizeof(wiphy->fw_version));

	if (len > offsetof(struct wmi_ready_event, rfc_read_calib_result)) {
		wil_dbg_wmi(wil, "rfc calibration result %d\n",
			    evt->rfc_read_calib_result);
		wil->fw_calib_result = evt->rfc_read_calib_result;
	}

	fw_max_assoc_sta = WIL6210_RX_DESC_MAX_CID;
	if (len > offsetof(struct wmi_ready_event, max_assoc_sta) &&
	    evt->max_assoc_sta > 0) {
		fw_max_assoc_sta = evt->max_assoc_sta;
		wil_dbg_wmi(wil, "fw reported max assoc sta %d\n",
			    fw_max_assoc_sta);

		if (fw_max_assoc_sta > WIL6210_MAX_CID) {
			wil_dbg_wmi(wil,
				    "fw max assoc sta %d exceeds max driver supported %d\n",
				    fw_max_assoc_sta, WIL6210_MAX_CID);
			fw_max_assoc_sta = WIL6210_MAX_CID;
		}
	}

	wil->max_assoc_sta = min_t(uint, max_assoc_sta, fw_max_assoc_sta);
	wil_dbg_wmi(wil, "setting max assoc sta to %d\n", wil->max_assoc_sta);

	wil_set_recovery_state(wil, fw_recovery_idle);
	set_bit(wil_status_fwready, wil->status);
	/* let the reset sequence continue */
	complete(&wil->wmi_ready);
}

static void wmi_evt_rx_mgmt(struct wil6210_vif *vif, int id, void *d, int len)
{
	struct wil6210_priv *wil = vif_to_wil(vif);
	struct wmi_rx_mgmt_packet_event *data = d;
	struct wiphy *wiphy = wil_to_wiphy(wil);
	struct ieee80211_mgmt *rx_mgmt_frame =
			(struct ieee80211_mgmt *)data->payload;
	int flen = len - offsetof(struct wmi_rx_mgmt_packet_event, payload);
	int ch_no;
	u32 freq;
	struct ieee80211_channel *channel;
	s32 signal;
	__le16 fc;
	u32 d_len;
	u16 d_status;

	if (flen < 0) {
		wil_err(wil, "MGMT Rx: short event, len %d\n", len);
		return;
	}

	d_len = le32_to_cpu(data->info.len);
	if (d_len != flen) {
		wil_err(wil,
			"MGMT Rx: length mismatch, d_len %d should be %d\n",
			d_len, flen);
		return;
	}

	ch_no = data->info.channel + 1;
	freq = ieee80211_channel_to_frequency(ch_no, NL80211_BAND_60GHZ);
	channel = ieee80211_get_channel(wiphy, freq);
	if (test_bit(WMI_FW_CAPABILITY_RSSI_REPORTING, wil->fw_capabilities))
		signal = 100 * data->info.rssi;
	else
		signal = data->info.sqi;
	d_status = le16_to_cpu(data->info.status);
	fc = rx_mgmt_frame->frame_control;

	wil_dbg_wmi(wil, "MGMT Rx: channel %d MCS %d RSSI %d SQI %d%%\n",
		    data->info.channel, data->info.mcs, data->info.rssi,
		    data->info.sqi);
	wil_dbg_wmi(wil, "status 0x%04x len %d fc 0x%04x\n", d_status, d_len,
		    le16_to_cpu(fc));
	wil_dbg_wmi(wil, "qid %d mid %d cid %d\n",
		    data->info.qid, data->info.mid, data->info.cid);
	wil_hex_dump_wmi("MGMT Rx ", DUMP_PREFIX_OFFSET, 16, 1, rx_mgmt_frame,
			 d_len, true);

	if (!channel) {
		wil_err(wil, "Frame on unsupported channel\n");
		return;
	}

	if (ieee80211_is_beacon(fc) || ieee80211_is_probe_resp(fc)) {
		struct cfg80211_bss *bss;
		u64 tsf = le64_to_cpu(rx_mgmt_frame->u.beacon.timestamp);
		u16 cap = le16_to_cpu(rx_mgmt_frame->u.beacon.capab_info);
		u16 bi = le16_to_cpu(rx_mgmt_frame->u.beacon.beacon_int);
		const u8 *ie_buf = rx_mgmt_frame->u.beacon.variable;
		size_t ie_len = d_len - offsetof(struct ieee80211_mgmt,
						 u.beacon.variable);
		wil_dbg_wmi(wil, "Capability info : 0x%04x\n", cap);
		wil_dbg_wmi(wil, "TSF : 0x%016llx\n", tsf);
		wil_dbg_wmi(wil, "Beacon interval : %d\n", bi);
		wil_hex_dump_wmi("IE ", DUMP_PREFIX_OFFSET, 16, 1, ie_buf,
				 ie_len, true);

		wil_dbg_wmi(wil, "Capability info : 0x%04x\n", cap);

		bss = cfg80211_inform_bss_frame(wiphy, channel, rx_mgmt_frame,
						d_len, signal, GFP_KERNEL);
		if (bss) {
			wil_dbg_wmi(wil, "Added BSS %pM\n",
				    rx_mgmt_frame->bssid);
			cfg80211_put_bss(wiphy, bss);
		} else {
			wil_err(wil, "cfg80211_inform_bss_frame() failed\n");
		}
	} else {
		mutex_lock(&wil->vif_mutex);
		cfg80211_rx_mgmt(vif_to_radio_wdev(wil, vif), freq, signal,
				 (void *)rx_mgmt_frame, d_len, 0);
		mutex_unlock(&wil->vif_mutex);
	}
}

static void wmi_evt_tx_mgmt(struct wil6210_vif *vif, int id, void *d, int len)
{
	struct wmi_tx_mgmt_packet_event *data = d;
	struct ieee80211_mgmt *mgmt_frame =
			(struct ieee80211_mgmt *)data->payload;
	int flen = len - offsetof(struct wmi_tx_mgmt_packet_event, payload);

	wil_hex_dump_wmi("MGMT Tx ", DUMP_PREFIX_OFFSET, 16, 1, mgmt_frame,
			 flen, true);
}

static void wmi_evt_scan_complete(struct wil6210_vif *vif, int id,
				  void *d, int len)
{
	struct wil6210_priv *wil = vif_to_wil(vif);

	mutex_lock(&wil->vif_mutex);
	if (vif->scan_request) {
		struct wmi_scan_complete_event *data = d;
		int status = le32_to_cpu(data->status);
		struct cfg80211_scan_info info = {
			.aborted = ((status != WMI_SCAN_SUCCESS) &&
				(status != WMI_SCAN_ABORT_REJECTED)),
		};

		wil_dbg_wmi(wil, "SCAN_COMPLETE(0x%08x)\n", status);
		wil_dbg_misc(wil, "Complete scan_request 0x%p aborted %d\n",
			     vif->scan_request, info.aborted);
		del_timer_sync(&vif->scan_timer);
		cfg80211_scan_done(vif->scan_request, &info);
		if (vif->mid == 0)
			wil->radio_wdev = wil->main_ndev->ieee80211_ptr;
		vif->scan_request = NULL;
		wake_up_interruptible(&wil->wq);
		if (vif->p2p.pending_listen_wdev) {
			wil_dbg_misc(wil, "Scheduling delayed listen\n");
			schedule_work(&vif->p2p.delayed_listen_work);
		}
	} else {
		wil_err(wil, "SCAN_COMPLETE while not scanning\n");
	}
	mutex_unlock(&wil->vif_mutex);
}

static void wmi_evt_connect(struct wil6210_vif *vif, int id, void *d, int len)
{
	struct wil6210_priv *wil = vif_to_wil(vif);
	struct net_device *ndev = vif_to_ndev(vif);
	struct wireless_dev *wdev = vif_to_wdev(vif);
	struct wmi_connect_event *evt = d;
	int ch; /* channel number */
	struct station_info *sinfo;
	u8 *assoc_req_ie, *assoc_resp_ie;
	size_t assoc_req_ielen, assoc_resp_ielen;
	/* capinfo(u16) + listen_interval(u16) + IEs */
	const size_t assoc_req_ie_offset = sizeof(u16) * 2;
	/* capinfo(u16) + status_code(u16) + associd(u16) + IEs */
	const size_t assoc_resp_ie_offset = sizeof(u16) * 3;
	int rc;

	if (len < sizeof(*evt)) {
		wil_err(wil, "Connect event too short : %d bytes\n", len);
		return;
	}
	if (len != sizeof(*evt) + evt->beacon_ie_len + evt->assoc_req_len +
		   evt->assoc_resp_len) {
		wil_err(wil,
			"Connect event corrupted : %d != %d + %d + %d + %d\n",
			len, (int)sizeof(*evt), evt->beacon_ie_len,
			evt->assoc_req_len, evt->assoc_resp_len);
		return;
	}
	if (evt->cid >= wil->max_assoc_sta) {
		wil_err(wil, "Connect CID invalid : %d\n", evt->cid);
		return;
	}

	ch = evt->channel + 1;
	wil_info(wil, "Connect %pM channel [%d] cid %d aid %d\n",
		 evt->bssid, ch, evt->cid, evt->aid);
	wil_hex_dump_wmi("connect AI : ", DUMP_PREFIX_OFFSET, 16, 1,
			 evt->assoc_info, len - sizeof(*evt), true);

	/* figure out IE's */
	assoc_req_ie = &evt->assoc_info[evt->beacon_ie_len +
					assoc_req_ie_offset];
	assoc_req_ielen = evt->assoc_req_len - assoc_req_ie_offset;
	if (evt->assoc_req_len <= assoc_req_ie_offset) {
		assoc_req_ie = NULL;
		assoc_req_ielen = 0;
	}

	assoc_resp_ie = &evt->assoc_info[evt->beacon_ie_len +
					 evt->assoc_req_len +
					 assoc_resp_ie_offset];
	assoc_resp_ielen = evt->assoc_resp_len - assoc_resp_ie_offset;
	if (evt->assoc_resp_len <= assoc_resp_ie_offset) {
		assoc_resp_ie = NULL;
		assoc_resp_ielen = 0;
	}

	if (test_bit(wil_status_resetting, wil->status) ||
	    !test_bit(wil_status_fwready, wil->status)) {
		wil_err(wil, "status_resetting, cancel connect event, CID %d\n",
			evt->cid);
		/* no need for cleanup, wil_reset will do that */
		return;
	}

	mutex_lock(&wil->mutex);

	if ((wdev->iftype == NL80211_IFTYPE_STATION) ||
	    (wdev->iftype == NL80211_IFTYPE_P2P_CLIENT)) {
		if (!test_bit(wil_vif_fwconnecting, vif->status)) {
			wil_err(wil, "Not in connecting state\n");
			mutex_unlock(&wil->mutex);
			return;
		}
		del_timer_sync(&vif->connect_timer);
	} else if ((wdev->iftype == NL80211_IFTYPE_AP) ||
		   (wdev->iftype == NL80211_IFTYPE_P2P_GO)) {
		if (wil->sta[evt->cid].status != wil_sta_unused) {
			wil_err(wil, "AP: Invalid status %d for CID %d\n",
				wil->sta[evt->cid].status, evt->cid);
			mutex_unlock(&wil->mutex);
			return;
		}
	}

	ether_addr_copy(wil->sta[evt->cid].addr, evt->bssid);
	wil->sta[evt->cid].mid = vif->mid;
	wil->sta[evt->cid].status = wil_sta_conn_pending;

	rc = wil_ring_init_tx(vif, evt->cid);
	if (rc) {
		wil_err(wil, "config tx vring failed for CID %d, rc (%d)\n",
			evt->cid, rc);
		wmi_disconnect_sta(vif, wil->sta[evt->cid].addr,
				   WLAN_REASON_UNSPECIFIED, false);
	} else {
		wil_info(wil, "successful connection to CID %d\n", evt->cid);
	}

	if ((wdev->iftype == NL80211_IFTYPE_STATION) ||
	    (wdev->iftype == NL80211_IFTYPE_P2P_CLIENT)) {
		if (rc) {
			netif_carrier_off(ndev);
			wil6210_bus_request(wil, WIL_DEFAULT_BUS_REQUEST_KBPS);
			wil_err(wil, "cfg80211_connect_result with failure\n");
			cfg80211_connect_result(ndev, evt->bssid, NULL, 0,
						NULL, 0,
						WLAN_STATUS_UNSPECIFIED_FAILURE,
						GFP_KERNEL);
			goto out;
		} else {
			struct wiphy *wiphy = wil_to_wiphy(wil);

			cfg80211_ref_bss(wiphy, vif->bss);
			cfg80211_connect_bss(ndev, evt->bssid, vif->bss,
					     assoc_req_ie, assoc_req_ielen,
					     assoc_resp_ie, assoc_resp_ielen,
					     WLAN_STATUS_SUCCESS, GFP_KERNEL,
					     NL80211_TIMEOUT_UNSPECIFIED);
		}
		vif->bss = NULL;
	} else if ((wdev->iftype == NL80211_IFTYPE_AP) ||
		   (wdev->iftype == NL80211_IFTYPE_P2P_GO)) {

		if (rc) {
			if (disable_ap_sme)
				/* notify new_sta has failed */
				cfg80211_del_sta(ndev, evt->bssid, GFP_KERNEL);
			goto out;
		}

		sinfo = kzalloc(sizeof(*sinfo), GFP_KERNEL);
		if (!sinfo) {
			rc = -ENOMEM;
			goto out;
		}

		sinfo->generation = wil->sinfo_gen++;

		if (assoc_req_ie) {
			sinfo->assoc_req_ies = assoc_req_ie;
			sinfo->assoc_req_ies_len = assoc_req_ielen;
		}

		cfg80211_new_sta(ndev, evt->bssid, sinfo, GFP_KERNEL);

		kfree(sinfo);
	} else {
		wil_err(wil, "unhandled iftype %d for CID %d\n", wdev->iftype,
			evt->cid);
		goto out;
	}

	wil->sta[evt->cid].status = wil_sta_connected;
	wil->sta[evt->cid].aid = evt->aid;
	if (!test_and_set_bit(wil_vif_fwconnected, vif->status))
		atomic_inc(&wil->connected_vifs);
	wil_update_net_queues_bh(wil, vif, NULL, false);

out:
	if (rc) {
		wil->sta[evt->cid].status = wil_sta_unused;
		wil->sta[evt->cid].mid = U8_MAX;
	}
	clear_bit(wil_vif_fwconnecting, vif->status);
	mutex_unlock(&wil->mutex);
}

static void wmi_evt_disconnect(struct wil6210_vif *vif, int id,
			       void *d, int len)
{
	struct wil6210_priv *wil = vif_to_wil(vif);
	struct wmi_disconnect_event *evt = d;
	u16 reason_code = le16_to_cpu(evt->protocol_reason_status);

	wil_info(wil, "Disconnect %pM reason [proto %d wmi %d]\n",
		 evt->bssid, reason_code, evt->disconnect_reason);

	wil->sinfo_gen++;

	if (test_bit(wil_status_resetting, wil->status) ||
	    !test_bit(wil_status_fwready, wil->status)) {
		wil_err(wil, "status_resetting, cancel disconnect event\n");
		/* no need for cleanup, wil_reset will do that */
		return;
	}

	mutex_lock(&wil->mutex);
	wil6210_disconnect_complete(vif, evt->bssid, reason_code);
	if (disable_ap_sme) {
		struct wireless_dev *wdev = vif_to_wdev(vif);
		struct net_device *ndev = vif_to_ndev(vif);

		/* disconnect event in disable_ap_sme mode means link loss */
		switch (wdev->iftype) {
		/* AP-like interface */
		case NL80211_IFTYPE_AP:
		case NL80211_IFTYPE_P2P_GO:
			/* notify hostapd about link loss */
			cfg80211_cqm_pktloss_notify(ndev, evt->bssid, 0,
						    GFP_KERNEL);
			break;
		default:
			break;
		}
	}
	mutex_unlock(&wil->mutex);
}

/*
 * Firmware reports EAPOL frame using WME event.
 * Reconstruct Ethernet frame and deliver it via normal Rx
 */
static void wmi_evt_eapol_rx(struct wil6210_vif *vif, int id, void *d, int len)
{
	struct wil6210_priv *wil = vif_to_wil(vif);
	struct net_device *ndev = vif_to_ndev(vif);
	struct wmi_eapol_rx_event *evt = d;
	u16 eapol_len = le16_to_cpu(evt->eapol_len);
	int sz = eapol_len + ETH_HLEN;
	struct sk_buff *skb;
	struct ethhdr *eth;
	int cid;
	struct wil_net_stats *stats = NULL;

	wil_dbg_wmi(wil, "EAPOL len %d from %pM MID %d\n", eapol_len,
		    evt->src_mac, vif->mid);

	cid = wil_find_cid(wil, vif->mid, evt->src_mac);
	if (cid >= 0)
		stats = &wil->sta[cid].stats;

	if (eapol_len > 196) { /* TODO: revisit size limit */
		wil_err(wil, "EAPOL too large\n");
		return;
	}

	skb = alloc_skb(sz, GFP_KERNEL);
	if (!skb) {
		wil_err(wil, "Failed to allocate skb\n");
		return;
	}

	eth = skb_put(skb, ETH_HLEN);
	ether_addr_copy(eth->h_dest, ndev->dev_addr);
	ether_addr_copy(eth->h_source, evt->src_mac);
	eth->h_proto = cpu_to_be16(ETH_P_PAE);
	skb_put_data(skb, evt->eapol, eapol_len);
	skb->protocol = eth_type_trans(skb, ndev);
	if (likely(netif_rx_ni(skb) == NET_RX_SUCCESS)) {
		ndev->stats.rx_packets++;
		ndev->stats.rx_bytes += sz;
		if (stats) {
			stats->rx_packets++;
			stats->rx_bytes += sz;
		}
	} else {
		ndev->stats.rx_dropped++;
		if (stats)
			stats->rx_dropped++;
	}
}

static void wmi_evt_ring_en(struct wil6210_vif *vif, int id, void *d, int len)
{
	struct wil6210_priv *wil = vif_to_wil(vif);
	struct wmi_ring_en_event *evt = d;
	u8 vri = evt->ring_index;
	struct wireless_dev *wdev = vif_to_wdev(vif);
	struct wil_sta_info *sta;
	u8 cid;
	struct key_params params;

	wil_dbg_wmi(wil, "Enable vring %d MID %d\n", vri, vif->mid);

	if (vri >= ARRAY_SIZE(wil->ring_tx)) {
		wil_err(wil, "Enable for invalid vring %d\n", vri);
		return;
	}

	if (wdev->iftype != NL80211_IFTYPE_AP || !disable_ap_sme ||
	    test_bit(wil_vif_ft_roam, vif->status))
		/* in AP mode with disable_ap_sme that is not FT,
		 * this is done by wil_cfg80211_change_station()
		 */
		wil->ring_tx_data[vri].dot1x_open = true;
	if (vri == vif->bcast_ring) /* no BA for bcast */
		return;

	cid = wil->ring2cid_tid[vri][0];
	if (!wil_cid_valid(wil, cid)) {
		wil_err(wil, "invalid cid %d for vring %d\n", cid, vri);
		return;
	}

	/* In FT mode we get key but not store it as it is received
	 * before WMI_CONNECT_EVENT received from FW.
	 * wil_set_crypto_rx is called here to reset the security PN
	 */
	sta = &wil->sta[cid];
	if (test_bit(wil_vif_ft_roam, vif->status)) {
		memset(&params, 0, sizeof(params));
		wil_set_crypto_rx(0, WMI_KEY_USE_PAIRWISE, sta, &params);
		if (wdev->iftype != NL80211_IFTYPE_AP)
			clear_bit(wil_vif_ft_roam, vif->status);
	}

	if (agg_wsize >= 0)
		wil_addba_tx_request(wil, vri, agg_wsize);
}

static void wmi_evt_ba_status(struct wil6210_vif *vif, int id,
			      void *d, int len)
{
	struct wil6210_priv *wil = vif_to_wil(vif);
	struct wmi_ba_status_event *evt = d;
	struct wil_ring_tx_data *txdata;

	wil_dbg_wmi(wil, "BACK[%d] %s {%d} timeout %d AMSDU%s\n",
		    evt->ringid,
		    evt->status == WMI_BA_AGREED ? "OK" : "N/A",
		    evt->agg_wsize, __le16_to_cpu(evt->ba_timeout),
		    evt->amsdu ? "+" : "-");

	if (evt->ringid >= WIL6210_MAX_TX_RINGS) {
		wil_err(wil, "invalid ring id %d\n", evt->ringid);
		return;
	}

	if (evt->status != WMI_BA_AGREED) {
		evt->ba_timeout = 0;
		evt->agg_wsize = 0;
		evt->amsdu = 0;
	}

	txdata = &wil->ring_tx_data[evt->ringid];

	txdata->agg_timeout = le16_to_cpu(evt->ba_timeout);
	txdata->agg_wsize = evt->agg_wsize;
	txdata->agg_amsdu = evt->amsdu;
	txdata->addba_in_progress = false;
}

static void wmi_evt_addba_rx_req(struct wil6210_vif *vif, int id,
				 void *d, int len)
{
	struct wil6210_priv *wil = vif_to_wil(vif);
	u8 cid, tid;
	struct wmi_rcp_addba_req_event *evt = d;

	if (evt->cidxtid != CIDXTID_EXTENDED_CID_TID) {
		parse_cidxtid(evt->cidxtid, &cid, &tid);
	} else {
		cid = evt->cid;
		tid = evt->tid;
	}
	wil_addba_rx_request(wil, vif->mid, cid, tid, evt->dialog_token,
			     evt->ba_param_set, evt->ba_timeout,
			     evt->ba_seq_ctrl);
}

static void wmi_evt_delba(struct wil6210_vif *vif, int id, void *d, int len)
__acquires(&sta->tid_rx_lock) __releases(&sta->tid_rx_lock)
{
	struct wil6210_priv *wil = vif_to_wil(vif);
	struct wmi_delba_event *evt = d;
	u8 cid, tid;
	u16 reason = __le16_to_cpu(evt->reason);
	struct wil_sta_info *sta;
	struct wil_tid_ampdu_rx *r;

	might_sleep();

	if (evt->cidxtid != CIDXTID_EXTENDED_CID_TID) {
		parse_cidxtid(evt->cidxtid, &cid, &tid);
	} else {
		cid = evt->cid;
		tid = evt->tid;
	}
	wil_dbg_wmi(wil, "DELBA MID %d CID %d TID %d from %s reason %d\n",
		    vif->mid, cid, tid,
		    evt->from_initiator ? "originator" : "recipient",
		    reason);
	if (!evt->from_initiator) {
		int i;
		/* find Tx vring it belongs to */
		for (i = 0; i < ARRAY_SIZE(wil->ring2cid_tid); i++) {
			if (wil->ring2cid_tid[i][0] == cid &&
			    wil->ring2cid_tid[i][1] == tid) {
				struct wil_ring_tx_data *txdata =
					&wil->ring_tx_data[i];

				wil_dbg_wmi(wil, "DELBA Tx vring %d\n", i);
				txdata->agg_timeout = 0;
				txdata->agg_wsize = 0;
				txdata->addba_in_progress = false;

				break; /* max. 1 matching ring */
			}
		}
		if (i >= ARRAY_SIZE(wil->ring2cid_tid))
			wil_err(wil, "DELBA: unable to find Tx vring\n");
		return;
	}

	sta = &wil->sta[cid];

	spin_lock_bh(&sta->tid_rx_lock);

	r = sta->tid_rx[tid];
	sta->tid_rx[tid] = NULL;
	wil_tid_ampdu_rx_free(wil, r);

	spin_unlock_bh(&sta->tid_rx_lock);
}

static void
wmi_evt_sched_scan_result(struct wil6210_vif *vif, int id, void *d, int len)
{
	struct wil6210_priv *wil = vif_to_wil(vif);
	struct wmi_sched_scan_result_event *data = d;
	struct wiphy *wiphy = wil_to_wiphy(wil);
	struct ieee80211_mgmt *rx_mgmt_frame =
		(struct ieee80211_mgmt *)data->payload;
	int flen = len - offsetof(struct wmi_sched_scan_result_event, payload);
	int ch_no;
	u32 freq;
	struct ieee80211_channel *channel;
	s32 signal;
	__le16 fc;
	u32 d_len;
	struct cfg80211_bss *bss;

	if (flen < 0) {
		wil_err(wil, "sched scan result event too short, len %d\n",
			len);
		return;
	}

	d_len = le32_to_cpu(data->info.len);
	if (d_len != flen) {
		wil_err(wil,
			"sched scan result length mismatch, d_len %d should be %d\n",
			d_len, flen);
		return;
	}

	fc = rx_mgmt_frame->frame_control;
	if (!ieee80211_is_probe_resp(fc)) {
		wil_err(wil, "sched scan result invalid frame, fc 0x%04x\n",
			fc);
		return;
	}

	ch_no = data->info.channel + 1;
	freq = ieee80211_channel_to_frequency(ch_no, NL80211_BAND_60GHZ);
	channel = ieee80211_get_channel(wiphy, freq);
	if (test_bit(WMI_FW_CAPABILITY_RSSI_REPORTING, wil->fw_capabilities))
		signal = 100 * data->info.rssi;
	else
		signal = data->info.sqi;

	wil_dbg_wmi(wil, "sched scan result: channel %d MCS %d RSSI %d\n",
		    data->info.channel, data->info.mcs, data->info.rssi);
	wil_dbg_wmi(wil, "len %d qid %d mid %d cid %d\n",
		    d_len, data->info.qid, data->info.mid, data->info.cid);
	wil_hex_dump_wmi("PROBE ", DUMP_PREFIX_OFFSET, 16, 1, rx_mgmt_frame,
			 d_len, true);

	if (!channel) {
		wil_err(wil, "Frame on unsupported channel\n");
		return;
	}

	bss = cfg80211_inform_bss_frame(wiphy, channel, rx_mgmt_frame,
					d_len, signal, GFP_KERNEL);
	if (bss) {
		wil_dbg_wmi(wil, "Added BSS %pM\n", rx_mgmt_frame->bssid);
		cfg80211_put_bss(wiphy, bss);
	} else {
		wil_err(wil, "cfg80211_inform_bss_frame() failed\n");
	}

	cfg80211_sched_scan_results(wiphy, 0);
}

static void wil_link_stats_store_basic(struct wil6210_vif *vif,
				       struct wmi_link_stats_basic *basic)
{
	struct wil6210_priv *wil = vif_to_wil(vif);
	u8 cid = basic->cid;
	struct wil_sta_info *sta;

	if (cid < 0 || cid >= wil->max_assoc_sta) {
		wil_err(wil, "invalid cid %d\n", cid);
		return;
	}

	sta = &wil->sta[cid];
	sta->fw_stats_basic = *basic;
}

static void wil_link_stats_store_global(struct wil6210_vif *vif,
					struct wmi_link_stats_global *global)
{
	struct wil6210_priv *wil = vif_to_wil(vif);

	wil->fw_stats_global.stats = *global;
}

static void wmi_link_stats_parse(struct wil6210_vif *vif, u64 tsf,
				 bool has_next, void *payload,
				 size_t payload_size)
{
	struct wil6210_priv *wil = vif_to_wil(vif);
	size_t hdr_size = sizeof(struct wmi_link_stats_record);
	size_t stats_size, record_size, expected_size;
	struct wmi_link_stats_record *hdr;

	if (payload_size < hdr_size) {
		wil_err(wil, "link stats wrong event size %zu\n", payload_size);
		return;
	}

	while (payload_size >= hdr_size) {
		hdr = payload;
		stats_size = le16_to_cpu(hdr->record_size);
		record_size = hdr_size + stats_size;

		if (payload_size < record_size) {
			wil_err(wil, "link stats payload ended unexpectedly, size %zu < %zu\n",
				payload_size, record_size);
			return;
		}

		switch (hdr->record_type_id) {
		case WMI_LINK_STATS_TYPE_BASIC:
			expected_size = sizeof(struct wmi_link_stats_basic);
			if (stats_size < expected_size) {
				wil_err(wil, "link stats invalid basic record size %zu < %zu\n",
					stats_size, expected_size);
				return;
			}
			if (vif->fw_stats_ready) {
				/* clean old statistics */
				vif->fw_stats_tsf = 0;
				vif->fw_stats_ready = 0;
			}

			wil_link_stats_store_basic(vif, payload + hdr_size);

			if (!has_next) {
				vif->fw_stats_tsf = tsf;
				vif->fw_stats_ready = 1;
			}

			break;
		case WMI_LINK_STATS_TYPE_GLOBAL:
			expected_size = sizeof(struct wmi_link_stats_global);
			if (stats_size < sizeof(struct wmi_link_stats_global)) {
				wil_err(wil, "link stats invalid global record size %zu < %zu\n",
					stats_size, expected_size);
				return;
			}

			if (wil->fw_stats_global.ready) {
				/* clean old statistics */
				wil->fw_stats_global.tsf = 0;
				wil->fw_stats_global.ready = 0;
			}

			wil_link_stats_store_global(vif, payload + hdr_size);

			if (!has_next) {
				wil->fw_stats_global.tsf = tsf;
				wil->fw_stats_global.ready = 1;
			}

			break;
		default:
			break;
		}

		/* skip to next record */
		payload += record_size;
		payload_size -= record_size;
	}
}

static void
wmi_evt_link_stats(struct wil6210_vif *vif, int id, void *d, int len)
{
	struct wil6210_priv *wil = vif_to_wil(vif);
	struct wmi_link_stats_event *evt = d;
	size_t payload_size;

	if (len < offsetof(struct wmi_link_stats_event, payload)) {
		wil_err(wil, "stats event way too short %d\n", len);
		return;
	}
	payload_size = le16_to_cpu(evt->payload_size);
	if (len < sizeof(struct wmi_link_stats_event) + payload_size) {
		wil_err(wil, "stats event too short %d\n", len);
		return;
	}

	wmi_link_stats_parse(vif, le64_to_cpu(evt->tsf), evt->has_next,
			     evt->payload, payload_size);
}

/**
 * find cid and ringid for the station vif
 *
 * return error, if other interfaces are used or ring was not found
 */
static int wil_find_cid_ringid_sta(struct wil6210_priv *wil,
				   struct wil6210_vif *vif,
				   int *cid,
				   int *ringid)
{
	struct wil_ring *ring;
	struct wil_ring_tx_data *txdata;
	int min_ring_id = wil_get_min_tx_ring_id(wil);
	int i;
	u8 lcid;

	if (!(vif->wdev.iftype == NL80211_IFTYPE_STATION ||
	      vif->wdev.iftype == NL80211_IFTYPE_P2P_CLIENT)) {
		wil_err(wil, "invalid interface type %d\n", vif->wdev.iftype);
		return -EINVAL;
	}

	/* In the STA mode, it is expected to have only one ring
	 * for the AP we are connected to.
	 * find it and return the cid associated with it.
	 */
	for (i = min_ring_id; i < WIL6210_MAX_TX_RINGS; i++) {
		ring = &wil->ring_tx[i];
		txdata = &wil->ring_tx_data[i];
		if (!ring->va || !txdata->enabled || txdata->mid != vif->mid)
			continue;

		lcid = wil->ring2cid_tid[i][0];
		if (lcid >= wil->max_assoc_sta) /* skip BCAST */
			continue;

		wil_dbg_wmi(wil, "find sta -> ringid %d cid %d\n", i, lcid);
		*cid = lcid;
		*ringid = i;
		return 0;
	}

	wil_dbg_wmi(wil, "find sta cid while no rings active?\n");

	return -ENOENT;
}

static void
wmi_evt_auth_status(struct wil6210_vif *vif, int id, void *d, int len)
{
	struct wil6210_priv *wil = vif_to_wil(vif);
	struct net_device *ndev = vif_to_ndev(vif);
	struct wmi_ft_auth_status_event *data = d;
	int ie_len = len - offsetof(struct wmi_ft_auth_status_event, ie_info);
	int rc, cid = 0, ringid = 0;
	struct cfg80211_ft_event_params ft;
	u16 d_len;
	/* auth_alg(u16) + auth_transaction(u16) + status_code(u16) */
	const size_t auth_ie_offset = sizeof(u16) * 3;
	struct auth_no_hdr *auth = (struct auth_no_hdr *)data->ie_info;

	/* check the status */
	if (ie_len >= 0 && data->status != WMI_FW_STATUS_SUCCESS) {
		wil_err(wil, "FT: auth failed. status %d\n", data->status);
		goto fail;
	}

	if (ie_len < auth_ie_offset) {
		wil_err(wil, "FT: auth event too short, len %d\n", len);
		goto fail;
	}

	d_len = le16_to_cpu(data->ie_len);
	if (d_len != ie_len) {
		wil_err(wil,
			"FT: auth ie length mismatch, d_len %d should be %d\n",
			d_len, ie_len);
		goto fail;
	}

	if (!test_bit(wil_vif_ft_roam, wil->status)) {
		wil_err(wil, "FT: Not in roaming state\n");
		goto fail;
	}

	if (le16_to_cpu(auth->auth_transaction) != 2) {
		wil_err(wil, "FT: auth error. auth_transaction %d\n",
			le16_to_cpu(auth->auth_transaction));
		goto fail;
	}

	if (le16_to_cpu(auth->auth_alg) != WLAN_AUTH_FT) {
		wil_err(wil, "FT: auth error. auth_alg %d\n",
			le16_to_cpu(auth->auth_alg));
		goto fail;
	}

	wil_dbg_wmi(wil, "FT: Auth to %pM successfully\n", data->mac_addr);
	wil_hex_dump_wmi("FT Auth ies : ", DUMP_PREFIX_OFFSET, 16, 1,
			 data->ie_info, d_len, true);

	/* find cid and ringid */
	rc = wil_find_cid_ringid_sta(wil, vif, &cid, &ringid);
	if (rc) {
		wil_err(wil, "No valid cid found\n");
		goto fail;
	}

	if (vif->privacy) {
		/* For secure assoc, remove old keys */
		rc = wmi_del_cipher_key(vif, 0, wil->sta[cid].addr,
					WMI_KEY_USE_PAIRWISE);
		if (rc) {
			wil_err(wil, "WMI_DELETE_CIPHER_KEY_CMD(PTK) failed\n");
			goto fail;
		}
		rc = wmi_del_cipher_key(vif, 0, wil->sta[cid].addr,
					WMI_KEY_USE_RX_GROUP);
		if (rc) {
			wil_err(wil, "WMI_DELETE_CIPHER_KEY_CMD(GTK) failed\n");
			goto fail;
		}
	}

	memset(&ft, 0, sizeof(ft));
	ft.ies = data->ie_info + auth_ie_offset;
	ft.ies_len = d_len - auth_ie_offset;
	ft.target_ap = data->mac_addr;
	cfg80211_ft_event(ndev, &ft);

	return;

fail:
	wil6210_disconnect(vif, NULL, WLAN_REASON_PREV_AUTH_NOT_VALID);
}

static void
wmi_evt_reassoc_status(struct wil6210_vif *vif, int id, void *d, int len)
{
	struct wil6210_priv *wil = vif_to_wil(vif);
	struct net_device *ndev = vif_to_ndev(vif);
	struct wiphy *wiphy = wil_to_wiphy(wil);
	struct wmi_ft_reassoc_status_event *data = d;
	int ies_len = len - offsetof(struct wmi_ft_reassoc_status_event,
				     ie_info);
	int rc = -ENOENT, cid = 0, ringid = 0;
	int ch; /* channel number (primary) */
	size_t assoc_req_ie_len = 0, assoc_resp_ie_len = 0;
	u8 *assoc_req_ie = NULL, *assoc_resp_ie = NULL;
	/* capinfo(u16) + listen_interval(u16) + current_ap mac addr + IEs */
	const size_t assoc_req_ie_offset = sizeof(u16) * 2 + ETH_ALEN;
	/* capinfo(u16) + status_code(u16) + associd(u16) + IEs */
	const size_t assoc_resp_ie_offset = sizeof(u16) * 3;
	u16 d_len;
	int freq;
	struct cfg80211_roam_info info;

	if (ies_len < 0) {
		wil_err(wil, "ft reassoc event too short, len %d\n", len);
		goto fail;
	}

	wil_dbg_wmi(wil, "Reasoc Status event: status=%d, aid=%d",
		    data->status, data->aid);
	wil_dbg_wmi(wil, "    mac_addr=%pM, beacon_ie_len=%d",
		    data->mac_addr, data->beacon_ie_len);
	wil_dbg_wmi(wil, "    reassoc_req_ie_len=%d, reassoc_resp_ie_len=%d",
		    le16_to_cpu(data->reassoc_req_ie_len),
		    le16_to_cpu(data->reassoc_resp_ie_len));

	d_len = le16_to_cpu(data->beacon_ie_len) +
		le16_to_cpu(data->reassoc_req_ie_len) +
		le16_to_cpu(data->reassoc_resp_ie_len);
	if (d_len != ies_len) {
		wil_err(wil,
			"ft reassoc ie length mismatch, d_len %d should be %d\n",
			d_len, ies_len);
		goto fail;
	}

	/* check the status */
	if (data->status != WMI_FW_STATUS_SUCCESS) {
		wil_err(wil, "ft reassoc failed. status %d\n", data->status);
		goto fail;
	}

	/* find cid and ringid */
	rc = wil_find_cid_ringid_sta(wil, vif, &cid, &ringid);
	if (rc) {
		wil_err(wil, "No valid cid found\n");
		goto fail;
	}

	ch = data->channel + 1;
	wil_info(wil, "FT: Roam %pM channel [%d] cid %d aid %d\n",
		 data->mac_addr, ch, cid, data->aid);

	wil_hex_dump_wmi("reassoc AI : ", DUMP_PREFIX_OFFSET, 16, 1,
			 data->ie_info, len - sizeof(*data), true);

	/* figure out IE's */
	if (le16_to_cpu(data->reassoc_req_ie_len) > assoc_req_ie_offset) {
		assoc_req_ie = &data->ie_info[assoc_req_ie_offset];
		assoc_req_ie_len = le16_to_cpu(data->reassoc_req_ie_len) -
			assoc_req_ie_offset;
	}
	if (le16_to_cpu(data->reassoc_resp_ie_len) <= assoc_resp_ie_offset) {
		wil_err(wil, "FT: reassoc resp ie len is too short, len %d\n",
			le16_to_cpu(data->reassoc_resp_ie_len));
		goto fail;
	}

	assoc_resp_ie = &data->ie_info[le16_to_cpu(data->reassoc_req_ie_len) +
		assoc_resp_ie_offset];
	assoc_resp_ie_len = le16_to_cpu(data->reassoc_resp_ie_len) -
		assoc_resp_ie_offset;

	if (test_bit(wil_status_resetting, wil->status) ||
	    !test_bit(wil_status_fwready, wil->status)) {
		wil_err(wil, "FT: status_resetting, cancel reassoc event\n");
		/* no need for cleanup, wil_reset will do that */
		return;
	}

	mutex_lock(&wil->mutex);

	/* ring modify to set the ring for the roamed AP settings */
	wil_dbg_wmi(wil,
		    "ft modify tx config for connection CID %d ring %d\n",
		    cid, ringid);

	rc = wil->txrx_ops.tx_ring_modify(vif, ringid, cid, 0);
	if (rc) {
		wil_err(wil, "modify TX for CID %d MID %d ring %d failed (%d)\n",
			cid, vif->mid, ringid, rc);
		mutex_unlock(&wil->mutex);
		goto fail;
	}

	/* Update the driver STA members with the new bss */
	wil->sta[cid].aid = data->aid;
	wil->sta[cid].stats.ft_roams++;
	ether_addr_copy(wil->sta[cid].addr, vif->bss->bssid);
	mutex_unlock(&wil->mutex);
	del_timer_sync(&vif->connect_timer);

	cfg80211_ref_bss(wiphy, vif->bss);
	freq = ieee80211_channel_to_frequency(ch, NL80211_BAND_60GHZ);

	memset(&info, 0, sizeof(info));
	info.channel = ieee80211_get_channel(wiphy, freq);
	info.bss = vif->bss;
	info.req_ie = assoc_req_ie;
	info.req_ie_len = assoc_req_ie_len;
	info.resp_ie = assoc_resp_ie;
	info.resp_ie_len = assoc_resp_ie_len;
	cfg80211_roamed(ndev, &info, GFP_KERNEL);
	vif->bss = NULL;

	return;

fail:
	wil6210_disconnect(vif, NULL, WLAN_REASON_PREV_AUTH_NOT_VALID);
}

/**
 * Some events are ignored for purpose; and need not be interpreted as
 * "unhandled events"
 */
static void wmi_evt_ignore(struct wil6210_vif *vif, int id, void *d, int len)
{
	struct wil6210_priv *wil = vif_to_wil(vif);

	wil_dbg_wmi(wil, "Ignore event 0x%04x len %d\n", id, len);
}

static const struct {
	int eventid;
	void (*handler)(struct wil6210_vif *vif,
			int eventid, void *data, int data_len);
} wmi_evt_handlers[] = {
	{WMI_READY_EVENTID,		wmi_evt_ready},
	{WMI_FW_READY_EVENTID,			wmi_evt_ignore},
	{WMI_RX_MGMT_PACKET_EVENTID,	wmi_evt_rx_mgmt},
	{WMI_TX_MGMT_PACKET_EVENTID,		wmi_evt_tx_mgmt},
	{WMI_SCAN_COMPLETE_EVENTID,	wmi_evt_scan_complete},
	{WMI_CONNECT_EVENTID,		wmi_evt_connect},
	{WMI_DISCONNECT_EVENTID,	wmi_evt_disconnect},
	{WMI_EAPOL_RX_EVENTID,		wmi_evt_eapol_rx},
	{WMI_BA_STATUS_EVENTID,		wmi_evt_ba_status},
	{WMI_RCP_ADDBA_REQ_EVENTID,	wmi_evt_addba_rx_req},
	{WMI_DELBA_EVENTID,		wmi_evt_delba},
	{WMI_RING_EN_EVENTID,		wmi_evt_ring_en},
	{WMI_DATA_PORT_OPEN_EVENTID,		wmi_evt_ignore},
	{WMI_SCHED_SCAN_RESULT_EVENTID,		wmi_evt_sched_scan_result},
	{WMI_LINK_STATS_EVENTID,		wmi_evt_link_stats},
	{WMI_FT_AUTH_STATUS_EVENTID,		wmi_evt_auth_status},
	{WMI_FT_REASSOC_STATUS_EVENTID,		wmi_evt_reassoc_status},
};

/*
 * Run in IRQ context
 * Extract WMI command from mailbox. Queue it to the @wil->pending_wmi_ev
 * that will be eventually handled by the @wmi_event_worker in the thread
 * context of thread "wil6210_wmi"
 */
void wmi_recv_cmd(struct wil6210_priv *wil)
{
	struct wil6210_mbox_ring_desc d_tail;
	struct wil6210_mbox_hdr hdr;
	struct wil6210_mbox_ring *r = &wil->mbox_ctl.rx;
	struct pending_wmi_event *evt;
	u8 *cmd;
	void __iomem *src;
	ulong flags;
	unsigned n;
	unsigned int num_immed_reply = 0;

	if (!test_bit(wil_status_mbox_ready, wil->status)) {
		wil_err(wil, "Reset in progress. Cannot handle WMI event\n");
		return;
	}

	if (test_bit(wil_status_suspended, wil->status)) {
		wil_err(wil, "suspended. cannot handle WMI event\n");
		return;
	}

	for (n = 0;; n++) {
		u16 len;
		bool q;
		bool immed_reply = false;

		r->head = wil_r(wil, RGF_MBOX +
				offsetof(struct wil6210_mbox_ctl, rx.head));
		if (r->tail == r->head)
			break;

		wil_dbg_wmi(wil, "Mbox head %08x tail %08x\n",
			    r->head, r->tail);
		/* read cmd descriptor from tail */
		wil_memcpy_fromio_32(&d_tail, wil->csr + HOSTADDR(r->tail),
				     sizeof(struct wil6210_mbox_ring_desc));
		if (d_tail.sync == 0) {
			wil_err(wil, "Mbox evt not owned by FW?\n");
			break;
		}

		/* read cmd header from descriptor */
		if (0 != wmi_read_hdr(wil, d_tail.addr, &hdr)) {
			wil_err(wil, "Mbox evt at 0x%08x?\n",
				le32_to_cpu(d_tail.addr));
			break;
		}
		len = le16_to_cpu(hdr.len);
		wil_dbg_wmi(wil, "Mbox evt %04x %04x %04x %02x\n",
			    le16_to_cpu(hdr.seq), len, le16_to_cpu(hdr.type),
			    hdr.flags);

		/* read cmd buffer from descriptor */
		src = wmi_buffer(wil, d_tail.addr) +
		      sizeof(struct wil6210_mbox_hdr);
		evt = kmalloc(ALIGN(offsetof(struct pending_wmi_event,
					     event.wmi) + len, 4),
			      GFP_KERNEL);
		if (!evt)
			break;

		evt->event.hdr = hdr;
		cmd = (void *)&evt->event.wmi;
		wil_memcpy_fromio_32(cmd, src, len);
		/* mark entry as empty */
		wil_w(wil, r->tail +
		      offsetof(struct wil6210_mbox_ring_desc, sync), 0);
		/* indicate */
		if ((hdr.type == WIL_MBOX_HDR_TYPE_WMI) &&
		    (len >= sizeof(struct wmi_cmd_hdr))) {
			struct wmi_cmd_hdr *wmi = &evt->event.wmi;
			u16 id = le16_to_cpu(wmi->command_id);
			u8 mid = wmi->mid;
			u32 tstamp = le32_to_cpu(wmi->fw_timestamp);
			if (test_bit(wil_status_resuming, wil->status)) {
				if (id == WMI_TRAFFIC_RESUME_EVENTID)
					clear_bit(wil_status_resuming,
						  wil->status);
				else
					wil_err(wil,
						"WMI evt %d while resuming\n",
						id);
			}
			spin_lock_irqsave(&wil->wmi_ev_lock, flags);
			if (wil->reply_id && wil->reply_id == id &&
			    wil->reply_mid == mid) {
				if (wil->reply_buf) {
					memcpy(wil->reply_buf, wmi,
					       min(len, wil->reply_size));
					immed_reply = true;
				}
				if (id == WMI_TRAFFIC_SUSPEND_EVENTID) {
					wil_dbg_wmi(wil,
						    "set suspend_resp_rcvd\n");
					wil->suspend_resp_rcvd = true;
				}
			}
			spin_unlock_irqrestore(&wil->wmi_ev_lock, flags);

			wil_dbg_wmi(wil, "recv %s (0x%04x) MID %d @%d msec\n",
				    eventid2name(id), id, wmi->mid, tstamp);
			trace_wil6210_wmi_event(wmi, &wmi[1],
						len - sizeof(*wmi));
		}
		wil_hex_dump_wmi("evt ", DUMP_PREFIX_OFFSET, 16, 1,
				 &evt->event.hdr, sizeof(hdr) + len, true);

		/* advance tail */
		r->tail = r->base + ((r->tail - r->base +
			  sizeof(struct wil6210_mbox_ring_desc)) % r->size);
		wil_w(wil, RGF_MBOX +
		      offsetof(struct wil6210_mbox_ctl, rx.tail), r->tail);

		if (immed_reply) {
			wil_dbg_wmi(wil, "recv_cmd: Complete WMI 0x%04x\n",
				    wil->reply_id);
			kfree(evt);
			num_immed_reply++;
			complete(&wil->wmi_call);
		} else {
			/* add to the pending list */
			spin_lock_irqsave(&wil->wmi_ev_lock, flags);
			list_add_tail(&evt->list, &wil->pending_wmi_ev);
			spin_unlock_irqrestore(&wil->wmi_ev_lock, flags);
			q = queue_work(wil->wmi_wq, &wil->wmi_event_worker);
			wil_dbg_wmi(wil, "queue_work -> %d\n", q);
		}
	}
	/* normally, 1 event per IRQ should be processed */
	wil_dbg_wmi(wil, "recv_cmd: -> %d events queued, %d completed\n",
		    n - num_immed_reply, num_immed_reply);
}

int wmi_call(struct wil6210_priv *wil, u16 cmdid, u8 mid, void *buf, u16 len,
	     u16 reply_id, void *reply, u16 reply_size, int to_msec)
{
	int rc;
	unsigned long remain;
	ulong flags;

	mutex_lock(&wil->wmi_mutex);

	spin_lock_irqsave(&wil->wmi_ev_lock, flags);
	wil->reply_id = reply_id;
	wil->reply_mid = mid;
	wil->reply_buf = reply;
	wil->reply_size = reply_size;
	reinit_completion(&wil->wmi_call);
	spin_unlock_irqrestore(&wil->wmi_ev_lock, flags);

	rc = __wmi_send(wil, cmdid, mid, buf, len);
	if (rc)
		goto out;

	remain = wait_for_completion_timeout(&wil->wmi_call,
					     msecs_to_jiffies(to_msec));
	if (0 == remain) {
		wil_err(wil, "wmi_call(0x%04x->0x%04x) timeout %d msec\n",
			cmdid, reply_id, to_msec);
		rc = -ETIME;
	} else {
		wil_dbg_wmi(wil,
			    "wmi_call(0x%04x->0x%04x) completed in %d msec\n",
			    cmdid, reply_id,
			    to_msec - jiffies_to_msecs(remain));
	}

out:
	spin_lock_irqsave(&wil->wmi_ev_lock, flags);
	wil->reply_id = 0;
	wil->reply_mid = U8_MAX;
	wil->reply_buf = NULL;
	wil->reply_size = 0;
	spin_unlock_irqrestore(&wil->wmi_ev_lock, flags);

	mutex_unlock(&wil->wmi_mutex);

	return rc;
}

int wmi_echo(struct wil6210_priv *wil)
{
	struct wil6210_vif *vif = ndev_to_vif(wil->main_ndev);
	struct wmi_echo_cmd cmd = {
		.value = cpu_to_le32(0x12345678),
	};

	return wmi_call(wil, WMI_ECHO_CMDID, vif->mid, &cmd, sizeof(cmd),
			WMI_ECHO_RSP_EVENTID, NULL, 0, 50);
}

int wmi_set_mac_address(struct wil6210_priv *wil, void *addr)
{
	struct wil6210_vif *vif = ndev_to_vif(wil->main_ndev);
	struct wmi_set_mac_address_cmd cmd;

	ether_addr_copy(cmd.mac, addr);

	wil_dbg_wmi(wil, "Set MAC %pM\n", addr);

	return wmi_send(wil, WMI_SET_MAC_ADDRESS_CMDID, vif->mid,
			&cmd, sizeof(cmd));
}

int wmi_led_cfg(struct wil6210_priv *wil, bool enable)
{
	struct wil6210_vif *vif = ndev_to_vif(wil->main_ndev);
	int rc = 0;
	struct wmi_led_cfg_cmd cmd = {
		.led_mode = enable,
		.id = led_id,
		.slow_blink_cfg.blink_on =
			cpu_to_le32(led_blink_time[WIL_LED_TIME_SLOW].on_ms),
		.slow_blink_cfg.blink_off =
			cpu_to_le32(led_blink_time[WIL_LED_TIME_SLOW].off_ms),
		.medium_blink_cfg.blink_on =
			cpu_to_le32(led_blink_time[WIL_LED_TIME_MED].on_ms),
		.medium_blink_cfg.blink_off =
			cpu_to_le32(led_blink_time[WIL_LED_TIME_MED].off_ms),
		.fast_blink_cfg.blink_on =
			cpu_to_le32(led_blink_time[WIL_LED_TIME_FAST].on_ms),
		.fast_blink_cfg.blink_off =
			cpu_to_le32(led_blink_time[WIL_LED_TIME_FAST].off_ms),
		.led_polarity = led_polarity,
	};
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_led_cfg_done_event evt;
	} __packed reply = {
		.evt = {.status = cpu_to_le32(WMI_FW_STATUS_FAILURE)},
	};

	if (led_id == WIL_LED_INVALID_ID)
		goto out;

	if (led_id > WIL_LED_MAX_ID) {
		wil_err(wil, "Invalid led id %d\n", led_id);
		rc = -EINVAL;
		goto out;
	}

	wil_dbg_wmi(wil,
		    "%s led %d\n",
		    enable ? "enabling" : "disabling", led_id);

	rc = wmi_call(wil, WMI_LED_CFG_CMDID, vif->mid, &cmd, sizeof(cmd),
		      WMI_LED_CFG_DONE_EVENTID, &reply, sizeof(reply),
		      100);
	if (rc)
		goto out;

	if (reply.evt.status) {
		wil_err(wil, "led %d cfg failed with status %d\n",
			led_id, le32_to_cpu(reply.evt.status));
		rc = -EINVAL;
	}

out:
	return rc;
}

int wmi_pcp_start(struct wil6210_vif *vif,
		  int bi, u8 wmi_nettype, u8 chan, u8 hidden_ssid, u8 is_go)
{
	struct wil6210_priv *wil = vif_to_wil(vif);
	int rc;

	struct wmi_pcp_start_cmd cmd = {
		.bcon_interval = cpu_to_le16(bi),
		.network_type = wmi_nettype,
		.disable_sec_offload = 1,
		.channel = chan - 1,
		.pcp_max_assoc_sta = wil->max_assoc_sta,
		.hidden_ssid = hidden_ssid,
		.is_go = is_go,
		.ap_sme_offload_mode = disable_ap_sme ?
				       WMI_AP_SME_OFFLOAD_PARTIAL :
				       WMI_AP_SME_OFFLOAD_FULL,
		.abft_len = wil->abft_len,
	};
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_pcp_started_event evt;
	} __packed reply = {
		.evt = {.status = WMI_FW_STATUS_FAILURE},
	};

	if (!vif->privacy)
		cmd.disable_sec = 1;

	if ((cmd.pcp_max_assoc_sta > WIL6210_MAX_CID) ||
	    (cmd.pcp_max_assoc_sta <= 0)) {
		wil_err(wil, "unexpected max_assoc_sta %d\n",
			cmd.pcp_max_assoc_sta);
		return -EOPNOTSUPP;
	}

	if (disable_ap_sme &&
	    !test_bit(WMI_FW_CAPABILITY_AP_SME_OFFLOAD_PARTIAL,
		      wil->fw_capabilities)) {
		wil_err(wil, "disable_ap_sme not supported by FW\n");
		return -EOPNOTSUPP;
	}

	/*
	 * Processing time may be huge, in case of secure AP it takes about
	 * 3500ms for FW to start AP
	 */
	rc = wmi_call(wil, WMI_PCP_START_CMDID, vif->mid, &cmd, sizeof(cmd),
		      WMI_PCP_STARTED_EVENTID, &reply, sizeof(reply), 5000);
	if (rc)
		return rc;

	if (reply.evt.status != WMI_FW_STATUS_SUCCESS)
		rc = -EINVAL;

	if (wmi_nettype != WMI_NETTYPE_P2P)
		/* Don't fail due to error in the led configuration */
		wmi_led_cfg(wil, true);

	return rc;
}

int wmi_pcp_stop(struct wil6210_vif *vif)
{
	struct wil6210_priv *wil = vif_to_wil(vif);
	int rc;

	rc = wmi_led_cfg(wil, false);
	if (rc)
		return rc;

	return wmi_call(wil, WMI_PCP_STOP_CMDID, vif->mid, NULL, 0,
			WMI_PCP_STOPPED_EVENTID, NULL, 0,
			WIL_WMI_PCP_STOP_TO_MS);
}

int wmi_set_ssid(struct wil6210_vif *vif, u8 ssid_len, const void *ssid)
{
	struct wil6210_priv *wil = vif_to_wil(vif);
	struct wmi_set_ssid_cmd cmd = {
		.ssid_len = cpu_to_le32(ssid_len),
	};

	if (ssid_len > sizeof(cmd.ssid))
		return -EINVAL;

	memcpy(cmd.ssid, ssid, ssid_len);

	return wmi_send(wil, WMI_SET_SSID_CMDID, vif->mid, &cmd, sizeof(cmd));
}

int wmi_get_ssid(struct wil6210_vif *vif, u8 *ssid_len, void *ssid)
{
	struct wil6210_priv *wil = vif_to_wil(vif);
	int rc;
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_set_ssid_cmd cmd;
	} __packed reply;
	int len; /* reply.cmd.ssid_len in CPU order */

	memset(&reply, 0, sizeof(reply));

	rc = wmi_call(wil, WMI_GET_SSID_CMDID, vif->mid, NULL, 0,
		      WMI_GET_SSID_EVENTID, &reply, sizeof(reply), 20);
	if (rc)
		return rc;

	len = le32_to_cpu(reply.cmd.ssid_len);
	if (len > sizeof(reply.cmd.ssid))
		return -EINVAL;

	*ssid_len = len;
	memcpy(ssid, reply.cmd.ssid, len);

	return 0;
}

int wmi_set_channel(struct wil6210_priv *wil, int channel)
{
	struct wil6210_vif *vif = ndev_to_vif(wil->main_ndev);
	struct wmi_set_pcp_channel_cmd cmd = {
		.channel = channel - 1,
	};

	return wmi_send(wil, WMI_SET_PCP_CHANNEL_CMDID, vif->mid,
			&cmd, sizeof(cmd));
}

int wmi_get_channel(struct wil6210_priv *wil, int *channel)
{
	struct wil6210_vif *vif = ndev_to_vif(wil->main_ndev);
	int rc;
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_set_pcp_channel_cmd cmd;
	} __packed reply;

	memset(&reply, 0, sizeof(reply));

	rc = wmi_call(wil, WMI_GET_PCP_CHANNEL_CMDID, vif->mid, NULL, 0,
		      WMI_GET_PCP_CHANNEL_EVENTID, &reply, sizeof(reply), 20);
	if (rc)
		return rc;

	if (reply.cmd.channel > 3)
		return -EINVAL;

	*channel = reply.cmd.channel + 1;

	return 0;
}

int wmi_p2p_cfg(struct wil6210_vif *vif, int channel, int bi)
{
	struct wil6210_priv *wil = vif_to_wil(vif);
	int rc;
	struct wmi_p2p_cfg_cmd cmd = {
		.discovery_mode = WMI_DISCOVERY_MODE_PEER2PEER,
		.bcon_interval = cpu_to_le16(bi),
		.channel = channel - 1,
	};
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_p2p_cfg_done_event evt;
	} __packed reply = {
		.evt = {.status = WMI_FW_STATUS_FAILURE},
	};

	wil_dbg_wmi(wil, "sending WMI_P2P_CFG_CMDID\n");

	rc = wmi_call(wil, WMI_P2P_CFG_CMDID, vif->mid, &cmd, sizeof(cmd),
		      WMI_P2P_CFG_DONE_EVENTID, &reply, sizeof(reply), 300);
	if (!rc && reply.evt.status != WMI_FW_STATUS_SUCCESS) {
		wil_err(wil, "P2P_CFG failed. status %d\n", reply.evt.status);
		rc = -EINVAL;
	}

	return rc;
}

int wmi_start_listen(struct wil6210_vif *vif)
{
	struct wil6210_priv *wil = vif_to_wil(vif);
	int rc;
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_listen_started_event evt;
	} __packed reply = {
		.evt = {.status = WMI_FW_STATUS_FAILURE},
	};

	wil_dbg_wmi(wil, "sending WMI_START_LISTEN_CMDID\n");

	rc = wmi_call(wil, WMI_START_LISTEN_CMDID, vif->mid, NULL, 0,
		      WMI_LISTEN_STARTED_EVENTID, &reply, sizeof(reply), 300);
	if (!rc && reply.evt.status != WMI_FW_STATUS_SUCCESS) {
		wil_err(wil, "device failed to start listen. status %d\n",
			reply.evt.status);
		rc = -EINVAL;
	}

	return rc;
}

int wmi_start_search(struct wil6210_vif *vif)
{
	struct wil6210_priv *wil = vif_to_wil(vif);
	int rc;
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_search_started_event evt;
	} __packed reply = {
		.evt = {.status = WMI_FW_STATUS_FAILURE},
	};

	wil_dbg_wmi(wil, "sending WMI_START_SEARCH_CMDID\n");

	rc = wmi_call(wil, WMI_START_SEARCH_CMDID, vif->mid, NULL, 0,
		      WMI_SEARCH_STARTED_EVENTID, &reply, sizeof(reply), 300);
	if (!rc && reply.evt.status != WMI_FW_STATUS_SUCCESS) {
		wil_err(wil, "device failed to start search. status %d\n",
			reply.evt.status);
		rc = -EINVAL;
	}

	return rc;
}

int wmi_stop_discovery(struct wil6210_vif *vif)
{
	struct wil6210_priv *wil = vif_to_wil(vif);
	int rc;

	wil_dbg_wmi(wil, "sending WMI_DISCOVERY_STOP_CMDID\n");

	rc = wmi_call(wil, WMI_DISCOVERY_STOP_CMDID, vif->mid, NULL, 0,
		      WMI_DISCOVERY_STOPPED_EVENTID, NULL, 0, 100);

	if (rc)
		wil_err(wil, "Failed to stop discovery\n");

	return rc;
}

int wmi_del_cipher_key(struct wil6210_vif *vif, u8 key_index,
		       const void *mac_addr, int key_usage)
{
	struct wil6210_priv *wil = vif_to_wil(vif);
	struct wmi_delete_cipher_key_cmd cmd = {
		.key_index = key_index,
	};

	if (mac_addr)
		memcpy(cmd.mac, mac_addr, WMI_MAC_LEN);

	return wmi_send(wil, WMI_DELETE_CIPHER_KEY_CMDID, vif->mid,
			&cmd, sizeof(cmd));
}

int wmi_add_cipher_key(struct wil6210_vif *vif, u8 key_index,
		       const void *mac_addr, int key_len, const void *key,
		       int key_usage)
{
	struct wil6210_priv *wil = vif_to_wil(vif);
	struct wmi_add_cipher_key_cmd cmd = {
		.key_index = key_index,
		.key_usage = key_usage,
		.key_len = key_len,
	};

	if (!key || (key_len > sizeof(cmd.key)))
		return -EINVAL;

	memcpy(cmd.key, key, key_len);
	if (mac_addr)
		memcpy(cmd.mac, mac_addr, WMI_MAC_LEN);

	return wmi_send(wil, WMI_ADD_CIPHER_KEY_CMDID, vif->mid,
			&cmd, sizeof(cmd));
}

int wmi_set_ie(struct wil6210_vif *vif, u8 type, u16 ie_len, const void *ie)
{
	struct wil6210_priv *wil = vif_to_wil(vif);
	static const char *const names[] = {
		[WMI_FRAME_BEACON]	= "BEACON",
		[WMI_FRAME_PROBE_REQ]	= "PROBE_REQ",
		[WMI_FRAME_PROBE_RESP]	= "WMI_FRAME_PROBE_RESP",
		[WMI_FRAME_ASSOC_REQ]	= "WMI_FRAME_ASSOC_REQ",
		[WMI_FRAME_ASSOC_RESP]	= "WMI_FRAME_ASSOC_RESP",
	};
	int rc;
	u16 len = sizeof(struct wmi_set_appie_cmd) + ie_len;
	struct wmi_set_appie_cmd *cmd;

	if (len < ie_len) {
		rc = -EINVAL;
		goto out;
	}

	cmd = kzalloc(len, GFP_KERNEL);
	if (!cmd) {
		rc = -ENOMEM;
		goto out;
	}
	if (!ie)
		ie_len = 0;

	cmd->mgmt_frm_type = type;
	/* BUG: FW API define ieLen as u8. Will fix FW */
	cmd->ie_len = cpu_to_le16(ie_len);
	memcpy(cmd->ie_info, ie, ie_len);
	rc = wmi_send(wil, WMI_SET_APPIE_CMDID, vif->mid, cmd, len);
	kfree(cmd);
out:
	if (rc) {
		const char *name = type < ARRAY_SIZE(names) ?
				   names[type] : "??";
		wil_err(wil, "set_ie(%d %s) failed : %d\n", type, name, rc);
	}

	return rc;
}

int wmi_update_ft_ies(struct wil6210_vif *vif, u16 ie_len, const void *ie)
{
	struct wil6210_priv *wil = vif_to_wil(vif);
	u16 len;
	struct wmi_update_ft_ies_cmd *cmd;
	int rc;

	if (!ie)
		ie_len = 0;

	len = sizeof(struct wmi_update_ft_ies_cmd) + ie_len;
	if (len < ie_len) {
		wil_err(wil, "wraparound. ie len %d\n", ie_len);
		return -EINVAL;
	}

	cmd = kzalloc(len, GFP_KERNEL);
	if (!cmd) {
		rc = -ENOMEM;
		goto out;
	}

	cmd->ie_len = cpu_to_le16(ie_len);
	memcpy(cmd->ie_info, ie, ie_len);
	rc = wmi_send(wil, WMI_UPDATE_FT_IES_CMDID, vif->mid, cmd, len);
	kfree(cmd);

out:
	if (rc)
		wil_err(wil, "update ft ies failed : %d\n", rc);

	return rc;
}

/**
 * wmi_rxon - turn radio on/off
 * @on:		turn on if true, off otherwise
 *
 * Only switch radio. Channel should be set separately.
 * No timeout for rxon - radio turned on forever unless some other call
 * turns it off
 */
int wmi_rxon(struct wil6210_priv *wil, bool on)
{
	struct wil6210_vif *vif = ndev_to_vif(wil->main_ndev);
	int rc;
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_listen_started_event evt;
	} __packed reply = {
		.evt = {.status = WMI_FW_STATUS_FAILURE},
	};

	wil_info(wil, "(%s)\n", on ? "on" : "off");

	if (on) {
		rc = wmi_call(wil, WMI_START_LISTEN_CMDID, vif->mid, NULL, 0,
			      WMI_LISTEN_STARTED_EVENTID,
			      &reply, sizeof(reply), 100);
		if ((rc == 0) && (reply.evt.status != WMI_FW_STATUS_SUCCESS))
			rc = -EINVAL;
	} else {
		rc = wmi_call(wil, WMI_DISCOVERY_STOP_CMDID, vif->mid, NULL, 0,
			      WMI_DISCOVERY_STOPPED_EVENTID, NULL, 0, 20);
	}

	return rc;
}

int wmi_rx_chain_add(struct wil6210_priv *wil, struct wil_ring *vring)
{
	struct net_device *ndev = wil->main_ndev;
	struct wireless_dev *wdev = ndev->ieee80211_ptr;
	struct wil6210_vif *vif = ndev_to_vif(ndev);
	struct wmi_cfg_rx_chain_cmd cmd = {
		.action = WMI_RX_CHAIN_ADD,
		.rx_sw_ring = {
			.max_mpdu_size = cpu_to_le16(
				wil_mtu2macbuf(wil->rx_buf_len)),
			.ring_mem_base = cpu_to_le64(vring->pa),
			.ring_size = cpu_to_le16(vring->size),
		},
		.mid = 0, /* TODO - what is it? */
		.decap_trans_type = WMI_DECAP_TYPE_802_3,
		.reorder_type = WMI_RX_SW_REORDER,
		.host_thrsh = cpu_to_le16(rx_ring_overflow_thrsh),
	};
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_cfg_rx_chain_done_event evt;
	} __packed evt;
	int rc;

	memset(&evt, 0, sizeof(evt));

	if (wdev->iftype == NL80211_IFTYPE_MONITOR) {
		struct ieee80211_channel *ch = wil->monitor_chandef.chan;

		cmd.sniffer_cfg.mode = cpu_to_le32(WMI_SNIFFER_ON);
		if (ch)
			cmd.sniffer_cfg.channel = ch->hw_value - 1;
		cmd.sniffer_cfg.phy_info_mode =
			cpu_to_le32(WMI_SNIFFER_PHY_INFO_DISABLED);
		cmd.sniffer_cfg.phy_support =
			cpu_to_le32((wil->monitor_flags & MONITOR_FLAG_CONTROL)
				    ? WMI_SNIFFER_CP : WMI_SNIFFER_BOTH_PHYS);
	} else {
		/* Initialize offload (in non-sniffer mode).
		 * Linux IP stack always calculates IP checksum
		 * HW always calculate TCP/UDP checksum
		 */
		cmd.l3_l4_ctrl |= (1 << L3_L4_CTRL_TCPIP_CHECKSUM_EN_POS);
	}

	if (rx_align_2)
		cmd.l2_802_3_offload_ctrl |=
				L2_802_3_OFFLOAD_CTRL_SNAP_KEEP_MSK;

	/* typical time for secure PCP is 840ms */
	rc = wmi_call(wil, WMI_CFG_RX_CHAIN_CMDID, vif->mid, &cmd, sizeof(cmd),
		      WMI_CFG_RX_CHAIN_DONE_EVENTID, &evt, sizeof(evt), 2000);
	if (rc)
		return rc;

	if (le32_to_cpu(evt.evt.status) != WMI_CFG_RX_CHAIN_SUCCESS)
		rc = -EINVAL;

	vring->hwtail = le32_to_cpu(evt.evt.rx_ring_tail_ptr);

	wil_dbg_misc(wil, "Rx init: status %d tail 0x%08x\n",
		     le32_to_cpu(evt.evt.status), vring->hwtail);

	return rc;
}

int wmi_get_temperature(struct wil6210_priv *wil, u32 *t_bb, u32 *t_rf)
{
	struct wil6210_vif *vif = ndev_to_vif(wil->main_ndev);
	int rc;
	struct wmi_temp_sense_cmd cmd = {
		.measure_baseband_en = cpu_to_le32(!!t_bb),
		.measure_rf_en = cpu_to_le32(!!t_rf),
		.measure_mode = cpu_to_le32(TEMPERATURE_MEASURE_NOW),
	};
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_temp_sense_done_event evt;
	} __packed reply;

	memset(&reply, 0, sizeof(reply));

	rc = wmi_call(wil, WMI_TEMP_SENSE_CMDID, vif->mid, &cmd, sizeof(cmd),
		      WMI_TEMP_SENSE_DONE_EVENTID, &reply, sizeof(reply), 100);
	if (rc)
		return rc;

	if (t_bb)
		*t_bb = le32_to_cpu(reply.evt.baseband_t1000);
	if (t_rf)
		*t_rf = le32_to_cpu(reply.evt.rf_t1000);

	return 0;
}

int wmi_disconnect_sta(struct wil6210_vif *vif, const u8 *mac, u16 reason,
		       bool del_sta)
{
	struct wil6210_priv *wil = vif_to_wil(vif);
	int rc;
	struct wmi_disconnect_sta_cmd disc_sta_cmd = {
		.disconnect_reason = cpu_to_le16(reason),
	};
	struct wmi_del_sta_cmd del_sta_cmd = {
		.disconnect_reason = cpu_to_le16(reason),
	};
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_disconnect_event evt;
	} __packed reply;

	wil_dbg_wmi(wil, "disconnect_sta: (%pM, reason %d)\n", mac, reason);

	memset(&reply, 0, sizeof(reply));
	vif->locally_generated_disc = true;
	if (del_sta) {
		ether_addr_copy(del_sta_cmd.dst_mac, mac);
		rc = wmi_call(wil, WMI_DEL_STA_CMDID, vif->mid, &del_sta_cmd,
			      sizeof(del_sta_cmd), WMI_DISCONNECT_EVENTID,
			      &reply, sizeof(reply), 1000);
	} else {
		ether_addr_copy(disc_sta_cmd.dst_mac, mac);
		rc = wmi_call(wil, WMI_DISCONNECT_STA_CMDID, vif->mid,
			      &disc_sta_cmd, sizeof(disc_sta_cmd),
			      WMI_DISCONNECT_EVENTID,
			      &reply, sizeof(reply), 1000);
	}
	/* failure to disconnect in reasonable time treated as FW error */
	if (rc) {
		wil_fw_error_recovery(wil);
		return rc;
	}
	wil->sinfo_gen++;

	return 0;
}

int wmi_addba(struct wil6210_priv *wil, u8 mid,
	      u8 ringid, u8 size, u16 timeout)
{
	u8 amsdu = wil->use_enhanced_dma_hw && wil->use_rx_hw_reordering &&
		test_bit(WMI_FW_CAPABILITY_AMSDU, wil->fw_capabilities) &&
		wil->amsdu_en;
	struct wmi_ring_ba_en_cmd cmd = {
		.ring_id = ringid,
		.agg_max_wsize = size,
		.ba_timeout = cpu_to_le16(timeout),
		.amsdu = amsdu,
	};

	wil_dbg_wmi(wil, "addba: (ring %d size %d timeout %d amsdu %d)\n",
		    ringid, size, timeout, amsdu);

	return wmi_send(wil, WMI_RING_BA_EN_CMDID, mid, &cmd, sizeof(cmd));
}

int wmi_delba_tx(struct wil6210_priv *wil, u8 mid, u8 ringid, u16 reason)
{
	struct wmi_ring_ba_dis_cmd cmd = {
		.ring_id = ringid,
		.reason = cpu_to_le16(reason),
	};

	wil_dbg_wmi(wil, "delba_tx: (ring %d reason %d)\n", ringid, reason);

	return wmi_send(wil, WMI_RING_BA_DIS_CMDID, mid, &cmd, sizeof(cmd));
}

int wmi_delba_rx(struct wil6210_priv *wil, u8 mid, u8 cid, u8 tid, u16 reason)
{
	struct wmi_rcp_delba_cmd cmd = {
		.reason = cpu_to_le16(reason),
	};

	if (cid >= WIL6210_RX_DESC_MAX_CID) {
		cmd.cidxtid = CIDXTID_EXTENDED_CID_TID;
		cmd.cid = cid;
		cmd.tid = tid;
	} else {
		cmd.cidxtid = mk_cidxtid(cid, tid);
	}

	wil_dbg_wmi(wil, "delba_rx: (CID %d TID %d reason %d)\n", cid,
		    tid, reason);

	return wmi_send(wil, WMI_RCP_DELBA_CMDID, mid, &cmd, sizeof(cmd));
}

int wmi_addba_rx_resp(struct wil6210_priv *wil,
		      u8 mid, u8 cid, u8 tid, u8 token,
		      u16 status, bool amsdu, u16 agg_wsize, u16 timeout)
{
	int rc;
	struct wmi_rcp_addba_resp_cmd cmd = {
		.dialog_token = token,
		.status_code = cpu_to_le16(status),
		/* bit 0: A-MSDU supported
		 * bit 1: policy (should be 0 for us)
		 * bits 2..5: TID
		 * bits 6..15: buffer size
		 */
		.ba_param_set = cpu_to_le16((amsdu ? 1 : 0) | (tid << 2) |
					    (agg_wsize << 6)),
		.ba_timeout = cpu_to_le16(timeout),
	};
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_rcp_addba_resp_sent_event evt;
	} __packed reply = {
		.evt = {.status = cpu_to_le16(WMI_FW_STATUS_FAILURE)},
	};

	if (cid >= WIL6210_RX_DESC_MAX_CID) {
		cmd.cidxtid = CIDXTID_EXTENDED_CID_TID;
		cmd.cid = cid;
		cmd.tid = tid;
	} else {
		cmd.cidxtid = mk_cidxtid(cid, tid);
	}

	wil_dbg_wmi(wil,
		    "ADDBA response for MID %d CID %d TID %d size %d timeout %d status %d AMSDU%s\n",
		    mid, cid, tid, agg_wsize,
		    timeout, status, amsdu ? "+" : "-");

	rc = wmi_call(wil, WMI_RCP_ADDBA_RESP_CMDID, mid, &cmd, sizeof(cmd),
		      WMI_RCP_ADDBA_RESP_SENT_EVENTID, &reply, sizeof(reply),
		      100);
	if (rc)
		return rc;

	if (reply.evt.status) {
		wil_err(wil, "ADDBA response failed with status %d\n",
			le16_to_cpu(reply.evt.status));
		rc = -EINVAL;
	}

	return rc;
}

int wmi_addba_rx_resp_edma(struct wil6210_priv *wil, u8 mid, u8 cid, u8 tid,
			   u8 token, u16 status, bool amsdu, u16 agg_wsize,
			   u16 timeout)
{
	int rc;
	struct wmi_rcp_addba_resp_edma_cmd cmd = {
		.cid = cid,
		.tid = tid,
		.dialog_token = token,
		.status_code = cpu_to_le16(status),
		/* bit 0: A-MSDU supported
		 * bit 1: policy (should be 0 for us)
		 * bits 2..5: TID
		 * bits 6..15: buffer size
		 */
		.ba_param_set = cpu_to_le16((amsdu ? 1 : 0) | (tid << 2) |
					    (agg_wsize << 6)),
		.ba_timeout = cpu_to_le16(timeout),
		/* route all the connections to status ring 0 */
		.status_ring_id = WIL_DEFAULT_RX_STATUS_RING_ID,
	};
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_rcp_addba_resp_sent_event evt;
	} __packed reply = {
		.evt = {.status = cpu_to_le16(WMI_FW_STATUS_FAILURE)},
	};

	wil_dbg_wmi(wil,
		    "ADDBA response for CID %d TID %d size %d timeout %d status %d AMSDU%s, sring_id %d\n",
		    cid, tid, agg_wsize, timeout, status, amsdu ? "+" : "-",
		    WIL_DEFAULT_RX_STATUS_RING_ID);

	rc = wmi_call(wil, WMI_RCP_ADDBA_RESP_EDMA_CMDID, mid, &cmd,
		      sizeof(cmd), WMI_RCP_ADDBA_RESP_SENT_EVENTID, &reply,
		      sizeof(reply), WIL_WMI_CALL_GENERAL_TO_MS);
	if (rc)
		return rc;

	if (reply.evt.status) {
		wil_err(wil, "ADDBA response failed with status %d\n",
			le16_to_cpu(reply.evt.status));
		rc = -EINVAL;
	}

	return rc;
}

int wmi_ps_dev_profile_cfg(struct wil6210_priv *wil,
			   enum wmi_ps_profile_type ps_profile)
{
	struct wil6210_vif *vif = ndev_to_vif(wil->main_ndev);
	int rc;
	struct wmi_ps_dev_profile_cfg_cmd cmd = {
		.ps_profile = ps_profile,
	};
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_ps_dev_profile_cfg_event evt;
	} __packed reply = {
		.evt = {.status = cpu_to_le32(WMI_PS_CFG_CMD_STATUS_ERROR)},
	};
	u32 status;

	wil_dbg_wmi(wil, "Setting ps dev profile %d\n", ps_profile);

	rc = wmi_call(wil, WMI_PS_DEV_PROFILE_CFG_CMDID, vif->mid,
		      &cmd, sizeof(cmd),
		      WMI_PS_DEV_PROFILE_CFG_EVENTID, &reply, sizeof(reply),
		      100);
	if (rc)
		return rc;

	status = le32_to_cpu(reply.evt.status);

	if (status != WMI_PS_CFG_CMD_STATUS_SUCCESS) {
		wil_err(wil, "ps dev profile cfg failed with status %d\n",
			status);
		rc = -EINVAL;
	}

	return rc;
}

int wmi_set_mgmt_retry(struct wil6210_priv *wil, u8 retry_short)
{
	struct wil6210_vif *vif = ndev_to_vif(wil->main_ndev);
	int rc;
	struct wmi_set_mgmt_retry_limit_cmd cmd = {
		.mgmt_retry_limit = retry_short,
	};
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_set_mgmt_retry_limit_event evt;
	} __packed reply = {
		.evt = {.status = WMI_FW_STATUS_FAILURE},
	};

	wil_dbg_wmi(wil, "Setting mgmt retry short %d\n", retry_short);

	if (!test_bit(WMI_FW_CAPABILITY_MGMT_RETRY_LIMIT, wil->fw_capabilities))
		return -ENOTSUPP;

	rc = wmi_call(wil, WMI_SET_MGMT_RETRY_LIMIT_CMDID, vif->mid,
		      &cmd, sizeof(cmd),
		      WMI_SET_MGMT_RETRY_LIMIT_EVENTID, &reply, sizeof(reply),
		      100);
	if (rc)
		return rc;

	if (reply.evt.status != WMI_FW_STATUS_SUCCESS) {
		wil_err(wil, "set mgmt retry limit failed with status %d\n",
			reply.evt.status);
		rc = -EINVAL;
	}

	return rc;
}

int wmi_get_mgmt_retry(struct wil6210_priv *wil, u8 *retry_short)
{
	struct wil6210_vif *vif = ndev_to_vif(wil->main_ndev);
	int rc;
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_get_mgmt_retry_limit_event evt;
	} __packed reply;

	wil_dbg_wmi(wil, "getting mgmt retry short\n");

	if (!test_bit(WMI_FW_CAPABILITY_MGMT_RETRY_LIMIT, wil->fw_capabilities))
		return -ENOTSUPP;

	memset(&reply, 0, sizeof(reply));
	rc = wmi_call(wil, WMI_GET_MGMT_RETRY_LIMIT_CMDID, vif->mid, NULL, 0,
		      WMI_GET_MGMT_RETRY_LIMIT_EVENTID, &reply, sizeof(reply),
		      100);
	if (rc)
		return rc;

	if (retry_short)
		*retry_short = reply.evt.mgmt_retry_limit;

	return 0;
}

int wmi_abort_scan(struct wil6210_vif *vif)
{
	struct wil6210_priv *wil = vif_to_wil(vif);
	int rc;

	wil_dbg_wmi(wil, "sending WMI_ABORT_SCAN_CMDID\n");

	rc = wmi_send(wil, WMI_ABORT_SCAN_CMDID, vif->mid, NULL, 0);
	if (rc)
		wil_err(wil, "Failed to abort scan (%d)\n", rc);

	return rc;
}

int wmi_new_sta(struct wil6210_vif *vif, const u8 *mac, u8 aid)
{
	struct wil6210_priv *wil = vif_to_wil(vif);
	int rc;
	struct wmi_new_sta_cmd cmd = {
		.aid = aid,
	};

	wil_dbg_wmi(wil, "new sta %pM, aid %d\n", mac, aid);

	ether_addr_copy(cmd.dst_mac, mac);

	rc = wmi_send(wil, WMI_NEW_STA_CMDID, vif->mid, &cmd, sizeof(cmd));
	if (rc)
		wil_err(wil, "Failed to send new sta (%d)\n", rc);

	return rc;
}

void wmi_event_flush(struct wil6210_priv *wil)
{
	ulong flags;
	struct pending_wmi_event *evt, *t;

	wil_dbg_wmi(wil, "event_flush\n");

	spin_lock_irqsave(&wil->wmi_ev_lock, flags);

	list_for_each_entry_safe(evt, t, &wil->pending_wmi_ev, list) {
		list_del(&evt->list);
		kfree(evt);
	}

	spin_unlock_irqrestore(&wil->wmi_ev_lock, flags);
}

static const char *suspend_status2name(u8 status)
{
	switch (status) {
	case WMI_TRAFFIC_SUSPEND_REJECTED_LINK_NOT_IDLE:
		return "LINK_NOT_IDLE";
	case WMI_TRAFFIC_SUSPEND_REJECTED_DISCONNECT:
		return "DISCONNECT";
	case WMI_TRAFFIC_SUSPEND_REJECTED_OTHER:
		return "OTHER";
	default:
		return "Untracked status";
	}
}

int wmi_suspend(struct wil6210_priv *wil)
{
	struct wil6210_vif *vif = ndev_to_vif(wil->main_ndev);
	int rc;
	struct wmi_traffic_suspend_cmd cmd = {
		.wakeup_trigger = wil->wakeup_trigger,
	};
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_traffic_suspend_event evt;
	} __packed reply = {
		.evt = {.status = WMI_TRAFFIC_SUSPEND_REJECTED_LINK_NOT_IDLE},
	};

	u32 suspend_to = WIL_WAIT_FOR_SUSPEND_RESUME_COMP;

	wil->suspend_resp_rcvd = false;
	wil->suspend_resp_comp = false;

	rc = wmi_call(wil, WMI_TRAFFIC_SUSPEND_CMDID, vif->mid,
		      &cmd, sizeof(cmd),
		      WMI_TRAFFIC_SUSPEND_EVENTID, &reply, sizeof(reply),
		      suspend_to);
	if (rc) {
		wil_err(wil, "wmi_call for suspend req failed, rc=%d\n", rc);
		if (rc == -ETIME)
			/* wmi_call TO */
			wil->suspend_stats.rejected_by_device++;
		else
			wil->suspend_stats.rejected_by_host++;
		goto out;
	}

	wil_dbg_wmi(wil, "waiting for suspend_response_completed\n");

	rc = wait_event_interruptible_timeout(wil->wq,
					      wil->suspend_resp_comp,
					      msecs_to_jiffies(suspend_to));
	if (rc == 0) {
		wil_err(wil, "TO waiting for suspend_response_completed\n");
		if (wil->suspend_resp_rcvd)
			/* Device responded but we TO due to another reason */
			wil->suspend_stats.rejected_by_host++;
		else
			wil->suspend_stats.rejected_by_device++;
		rc = -EBUSY;
		goto out;
	}

	wil_dbg_wmi(wil, "suspend_response_completed rcvd\n");
	if (reply.evt.status != WMI_TRAFFIC_SUSPEND_APPROVED) {
		wil_dbg_pm(wil, "device rejected the suspend, %s\n",
			   suspend_status2name(reply.evt.status));
		wil->suspend_stats.rejected_by_device++;
	}
	rc = reply.evt.status;

out:
	wil->suspend_resp_rcvd = false;
	wil->suspend_resp_comp = false;

	return rc;
}

static void resume_triggers2string(u32 triggers, char *string, int str_size)
{
	string[0] = '\0';

	if (!triggers) {
		strlcat(string, " UNKNOWN", str_size);
		return;
	}

	if (triggers & WMI_RESUME_TRIGGER_HOST)
		strlcat(string, " HOST", str_size);

	if (triggers & WMI_RESUME_TRIGGER_UCAST_RX)
		strlcat(string, " UCAST_RX", str_size);

	if (triggers & WMI_RESUME_TRIGGER_BCAST_RX)
		strlcat(string, " BCAST_RX", str_size);

	if (triggers & WMI_RESUME_TRIGGER_WMI_EVT)
		strlcat(string, " WMI_EVT", str_size);

	if (triggers & WMI_RESUME_TRIGGER_DISCONNECT)
		strlcat(string, " DISCONNECT", str_size);
}

int wmi_resume(struct wil6210_priv *wil)
{
	struct wil6210_vif *vif = ndev_to_vif(wil->main_ndev);
	int rc;
	char string[100];
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_traffic_resume_event evt;
	} __packed reply = {
		.evt = {.status = WMI_TRAFFIC_RESUME_FAILED,
			.resume_triggers =
				cpu_to_le32(WMI_RESUME_TRIGGER_UNKNOWN)},
	};

	rc = wmi_call(wil, WMI_TRAFFIC_RESUME_CMDID, vif->mid, NULL, 0,
		      WMI_TRAFFIC_RESUME_EVENTID, &reply, sizeof(reply),
		      WIL_WAIT_FOR_SUSPEND_RESUME_COMP);
	if (rc)
		return rc;
	resume_triggers2string(le32_to_cpu(reply.evt.resume_triggers), string,
			       sizeof(string));
	wil_dbg_pm(wil, "device resume %s, resume triggers:%s (0x%x)\n",
		   reply.evt.status ? "failed" : "passed", string,
		   le32_to_cpu(reply.evt.resume_triggers));

	return reply.evt.status;
}

int wmi_port_allocate(struct wil6210_priv *wil, u8 mid,
		      const u8 *mac, enum nl80211_iftype iftype)
{
	int rc;
	struct wmi_port_allocate_cmd cmd = {
		.mid = mid,
	};
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_port_allocated_event evt;
	} __packed reply = {
		.evt = {.status = WMI_FW_STATUS_FAILURE},
	};

	wil_dbg_misc(wil, "port allocate, mid %d iftype %d, mac %pM\n",
		     mid, iftype, mac);

	ether_addr_copy(cmd.mac, mac);
	switch (iftype) {
	case NL80211_IFTYPE_STATION:
		cmd.port_role = WMI_PORT_STA;
		break;
	case NL80211_IFTYPE_AP:
		cmd.port_role = WMI_PORT_AP;
		break;
	case NL80211_IFTYPE_P2P_CLIENT:
		cmd.port_role = WMI_PORT_P2P_CLIENT;
		break;
	case NL80211_IFTYPE_P2P_GO:
		cmd.port_role = WMI_PORT_P2P_GO;
		break;
	/* what about monitor??? */
	default:
		wil_err(wil, "unsupported iftype: %d\n", iftype);
		return -EINVAL;
	}

	rc = wmi_call(wil, WMI_PORT_ALLOCATE_CMDID, mid,
		      &cmd, sizeof(cmd),
		      WMI_PORT_ALLOCATED_EVENTID, &reply,
		      sizeof(reply), 300);
	if (rc) {
		wil_err(wil, "failed to allocate port, status %d\n", rc);
		return rc;
	}
	if (reply.evt.status != WMI_FW_STATUS_SUCCESS) {
		wil_err(wil, "WMI_PORT_ALLOCATE returned status %d\n",
			reply.evt.status);
		return -EINVAL;
	}

	return 0;
}

int wmi_port_delete(struct wil6210_priv *wil, u8 mid)
{
	int rc;
	struct wmi_port_delete_cmd cmd = {
		.mid = mid,
	};
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_port_deleted_event evt;
	} __packed reply = {
		.evt = {.status = WMI_FW_STATUS_FAILURE},
	};

	wil_dbg_misc(wil, "port delete, mid %d\n", mid);

	rc = wmi_call(wil, WMI_PORT_DELETE_CMDID, mid,
		      &cmd, sizeof(cmd),
		      WMI_PORT_DELETED_EVENTID, &reply,
		      sizeof(reply), 2000);
	if (rc) {
		wil_err(wil, "failed to delete port, status %d\n", rc);
		return rc;
	}
	if (reply.evt.status != WMI_FW_STATUS_SUCCESS) {
		wil_err(wil, "WMI_PORT_DELETE returned status %d\n",
			reply.evt.status);
		return -EINVAL;
	}

	return 0;
}

static bool wmi_evt_call_handler(struct wil6210_vif *vif, int id,
				 void *d, int len)
{
	uint i;

	for (i = 0; i < ARRAY_SIZE(wmi_evt_handlers); i++) {
		if (wmi_evt_handlers[i].eventid == id) {
			wmi_evt_handlers[i].handler(vif, id, d, len);
			return true;
		}
	}

	return false;
}

static void wmi_event_handle(struct wil6210_priv *wil,
			     struct wil6210_mbox_hdr *hdr)
{
	u16 len = le16_to_cpu(hdr->len);
	struct wil6210_vif *vif;

	if ((hdr->type == WIL_MBOX_HDR_TYPE_WMI) &&
	    (len >= sizeof(struct wmi_cmd_hdr))) {
		struct wmi_cmd_hdr *wmi = (void *)(&hdr[1]);
		void *evt_data = (void *)(&wmi[1]);
		u16 id = le16_to_cpu(wmi->command_id);
		u8 mid = wmi->mid;

		wil_dbg_wmi(wil, "Handle %s (0x%04x) (reply_id 0x%04x,%d)\n",
			    eventid2name(id), id, wil->reply_id,
			    wil->reply_mid);

		if (mid == MID_BROADCAST)
			mid = 0;
		if (mid >= GET_MAX_VIFS(wil)) {
			wil_dbg_wmi(wil, "invalid mid %d, event skipped\n",
				    mid);
			return;
		}
		vif = wil->vifs[mid];
		if (!vif) {
			wil_dbg_wmi(wil, "event for empty VIF(%d), skipped\n",
				    mid);
			return;
		}

		/* check if someone waits for this event */
		if (wil->reply_id && wil->reply_id == id &&
		    wil->reply_mid == mid) {
			WARN_ON(wil->reply_buf);

			wmi_evt_call_handler(vif, id, evt_data,
					     len - sizeof(*wmi));
			wil_dbg_wmi(wil, "event_handle: Complete WMI 0x%04x\n",
				    id);
			complete(&wil->wmi_call);
			return;
		}
		/* unsolicited event */
		/* search for handler */
		if (!wmi_evt_call_handler(vif, id, evt_data,
					  len - sizeof(*wmi))) {
			wil_info(wil, "Unhandled event 0x%04x\n", id);
		}
	} else {
		wil_err(wil, "Unknown event type\n");
		print_hex_dump(KERN_ERR, "evt?? ", DUMP_PREFIX_OFFSET, 16, 1,
			       hdr, sizeof(*hdr) + len, true);
	}
}

/*
 * Retrieve next WMI event from the pending list
 */
static struct list_head *next_wmi_ev(struct wil6210_priv *wil)
{
	ulong flags;
	struct list_head *ret = NULL;

	spin_lock_irqsave(&wil->wmi_ev_lock, flags);

	if (!list_empty(&wil->pending_wmi_ev)) {
		ret = wil->pending_wmi_ev.next;
		list_del(ret);
	}

	spin_unlock_irqrestore(&wil->wmi_ev_lock, flags);

	return ret;
}

/*
 * Handler for the WMI events
 */
void wmi_event_worker(struct work_struct *work)
{
	struct wil6210_priv *wil = container_of(work, struct wil6210_priv,
						 wmi_event_worker);
	struct pending_wmi_event *evt;
	struct list_head *lh;

	wil_dbg_wmi(wil, "event_worker: Start\n");
	while ((lh = next_wmi_ev(wil)) != NULL) {
		evt = list_entry(lh, struct pending_wmi_event, list);
		wmi_event_handle(wil, &evt->event.hdr);
		kfree(evt);
	}
	wil_dbg_wmi(wil, "event_worker: Finished\n");
}

bool wil_is_wmi_idle(struct wil6210_priv *wil)
{
	ulong flags;
	struct wil6210_mbox_ring *r = &wil->mbox_ctl.rx;
	bool rc = false;

	spin_lock_irqsave(&wil->wmi_ev_lock, flags);

	/* Check if there are pending WMI events in the events queue */
	if (!list_empty(&wil->pending_wmi_ev)) {
		wil_dbg_pm(wil, "Pending WMI events in queue\n");
		goto out;
	}

	/* Check if there is a pending WMI call */
	if (wil->reply_id) {
		wil_dbg_pm(wil, "Pending WMI call\n");
		goto out;
	}

	/* Check if there are pending RX events in mbox */
	r->head = wil_r(wil, RGF_MBOX +
			offsetof(struct wil6210_mbox_ctl, rx.head));
	if (r->tail != r->head)
		wil_dbg_pm(wil, "Pending WMI mbox events\n");
	else
		rc = true;

out:
	spin_unlock_irqrestore(&wil->wmi_ev_lock, flags);
	return rc;
}

static void
wmi_sched_scan_set_ssids(struct wil6210_priv *wil,
			 struct wmi_start_sched_scan_cmd *cmd,
			 struct cfg80211_ssid *ssids, int n_ssids,
			 struct cfg80211_match_set *match_sets,
			 int n_match_sets)
{
	int i;

	if (n_match_sets > WMI_MAX_PNO_SSID_NUM) {
		wil_dbg_wmi(wil, "too many match sets (%d), use first %d\n",
			    n_match_sets, WMI_MAX_PNO_SSID_NUM);
		n_match_sets = WMI_MAX_PNO_SSID_NUM;
	}
	cmd->num_of_ssids = n_match_sets;

	for (i = 0; i < n_match_sets; i++) {
		struct wmi_sched_scan_ssid_match *wmi_match =
			&cmd->ssid_for_match[i];
		struct cfg80211_match_set *cfg_match = &match_sets[i];
		int j;

		wmi_match->ssid_len = cfg_match->ssid.ssid_len;
		memcpy(wmi_match->ssid, cfg_match->ssid.ssid,
		       min_t(u8, wmi_match->ssid_len, WMI_MAX_SSID_LEN));
		wmi_match->rssi_threshold = S8_MIN;
		if (cfg_match->rssi_thold >= S8_MIN &&
		    cfg_match->rssi_thold <= S8_MAX)
			wmi_match->rssi_threshold = cfg_match->rssi_thold;

		for (j = 0; j < n_ssids; j++)
			if (wmi_match->ssid_len == ssids[j].ssid_len &&
			    memcmp(wmi_match->ssid, ssids[j].ssid,
				   wmi_match->ssid_len) == 0)
				wmi_match->add_ssid_to_probe = true;
	}
}

static void
wmi_sched_scan_set_channels(struct wil6210_priv *wil,
			    struct wmi_start_sched_scan_cmd *cmd,
			    u32 n_channels,
			    struct ieee80211_channel **channels)
{
	int i;

	if (n_channels > WMI_MAX_CHANNEL_NUM) {
		wil_dbg_wmi(wil, "too many channels (%d), use first %d\n",
			    n_channels, WMI_MAX_CHANNEL_NUM);
		n_channels = WMI_MAX_CHANNEL_NUM;
	}
	cmd->num_of_channels = n_channels;

	for (i = 0; i < n_channels; i++) {
		struct ieee80211_channel *cfg_chan = channels[i];

		cmd->channel_list[i] = cfg_chan->hw_value - 1;
	}
}

static void
wmi_sched_scan_set_plans(struct wil6210_priv *wil,
			 struct wmi_start_sched_scan_cmd *cmd,
			 struct cfg80211_sched_scan_plan *scan_plans,
			 int n_scan_plans)
{
	int i;

	if (n_scan_plans > WMI_MAX_PLANS_NUM) {
		wil_dbg_wmi(wil, "too many plans (%d), use first %d\n",
			    n_scan_plans, WMI_MAX_PLANS_NUM);
		n_scan_plans = WMI_MAX_PLANS_NUM;
	}

	for (i = 0; i < n_scan_plans; i++) {
		struct cfg80211_sched_scan_plan *cfg_plan = &scan_plans[i];

		cmd->scan_plans[i].interval_sec =
			cpu_to_le16(cfg_plan->interval);
		cmd->scan_plans[i].num_of_iterations =
			cpu_to_le16(cfg_plan->iterations);
	}
}

int wmi_start_sched_scan(struct wil6210_priv *wil,
			 struct cfg80211_sched_scan_request *request)
{
	struct wil6210_vif *vif = ndev_to_vif(wil->main_ndev);
	int rc;
	struct wmi_start_sched_scan_cmd cmd = {
		.min_rssi_threshold = S8_MIN,
		.initial_delay_sec = cpu_to_le16(request->delay),
	};
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_start_sched_scan_event evt;
	} __packed reply = {
		.evt = {.result = WMI_PNO_REJECT},
	};

	if (!test_bit(WMI_FW_CAPABILITY_PNO, wil->fw_capabilities))
		return -ENOTSUPP;

	if (request->min_rssi_thold >= S8_MIN &&
	    request->min_rssi_thold <= S8_MAX)
		cmd.min_rssi_threshold = request->min_rssi_thold;

	wmi_sched_scan_set_ssids(wil, &cmd, request->ssids, request->n_ssids,
				 request->match_sets, request->n_match_sets);
	wmi_sched_scan_set_channels(wil, &cmd,
				    request->n_channels, request->channels);
	wmi_sched_scan_set_plans(wil, &cmd,
				 request->scan_plans, request->n_scan_plans);

	rc = wmi_call(wil, WMI_START_SCHED_SCAN_CMDID, vif->mid,
		      &cmd, sizeof(cmd),
		      WMI_START_SCHED_SCAN_EVENTID, &reply, sizeof(reply),
		      WIL_WMI_CALL_GENERAL_TO_MS);
	if (rc)
		return rc;

	if (reply.evt.result != WMI_PNO_SUCCESS) {
		wil_err(wil, "start sched scan failed, result %d\n",
			reply.evt.result);
		return -EINVAL;
	}

	return 0;
}

int wmi_stop_sched_scan(struct wil6210_priv *wil)
{
	struct wil6210_vif *vif = ndev_to_vif(wil->main_ndev);
	int rc;
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_stop_sched_scan_event evt;
	} __packed reply = {
		.evt = {.result = WMI_PNO_REJECT},
	};

	if (!test_bit(WMI_FW_CAPABILITY_PNO, wil->fw_capabilities))
		return -ENOTSUPP;

	rc = wmi_call(wil, WMI_STOP_SCHED_SCAN_CMDID, vif->mid, NULL, 0,
		      WMI_STOP_SCHED_SCAN_EVENTID, &reply, sizeof(reply),
		      WIL_WMI_CALL_GENERAL_TO_MS);
	if (rc)
		return rc;

	if (reply.evt.result != WMI_PNO_SUCCESS) {
		wil_err(wil, "stop sched scan failed, result %d\n",
			reply.evt.result);
		return -EINVAL;
	}

	return 0;
}

int wmi_mgmt_tx(struct wil6210_vif *vif, const u8 *buf, size_t len)
{
	size_t total;
	struct wil6210_priv *wil = vif_to_wil(vif);
	struct ieee80211_mgmt *mgmt_frame = (void *)buf;
	struct wmi_sw_tx_req_cmd *cmd;
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_sw_tx_complete_event evt;
	} __packed evt = {
		.evt = {.status = WMI_FW_STATUS_FAILURE},
	};
	int rc;

	wil_dbg_misc(wil, "mgmt_tx mid %d\n", vif->mid);
	wil_hex_dump_misc("mgmt tx frame ", DUMP_PREFIX_OFFSET, 16, 1, buf,
			  len, true);

	if (len < sizeof(struct ieee80211_hdr_3addr))
		return -EINVAL;

	total = sizeof(*cmd) + len;
	if (total < len) {
		wil_err(wil, "mgmt_tx invalid len %zu\n", len);
		return -EINVAL;
	}

	cmd = kmalloc(total, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	memcpy(cmd->dst_mac, mgmt_frame->da, WMI_MAC_LEN);
	cmd->len = cpu_to_le16(len);
	memcpy(cmd->payload, buf, len);

	rc = wmi_call(wil, WMI_SW_TX_REQ_CMDID, vif->mid, cmd, total,
		      WMI_SW_TX_COMPLETE_EVENTID, &evt, sizeof(evt), 2000);
	if (!rc && evt.evt.status != WMI_FW_STATUS_SUCCESS) {
		wil_dbg_wmi(wil, "mgmt_tx failed with status %d\n",
			    evt.evt.status);
		rc = -EAGAIN;
	}

	kfree(cmd);

	return rc;
}

int wmi_mgmt_tx_ext(struct wil6210_vif *vif, const u8 *buf, size_t len,
		    u8 channel, u16 duration_ms)
{
	size_t total;
	struct wil6210_priv *wil = vif_to_wil(vif);
	struct ieee80211_mgmt *mgmt_frame = (void *)buf;
	struct wmi_sw_tx_req_ext_cmd *cmd;
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_sw_tx_complete_event evt;
	} __packed evt = {
		.evt = {.status = WMI_FW_STATUS_FAILURE},
	};
	int rc;

	wil_dbg_wmi(wil, "mgmt_tx_ext mid %d channel %d duration %d\n",
		    vif->mid, channel, duration_ms);
	wil_hex_dump_wmi("mgmt_tx_ext frame ", DUMP_PREFIX_OFFSET, 16, 1, buf,
			 len, true);

	if (len < sizeof(struct ieee80211_hdr_3addr)) {
		wil_err(wil, "short frame. len %zu\n", len);
		return -EINVAL;
	}

	total = sizeof(*cmd) + len;
	if (total < len) {
		wil_err(wil, "mgmt_tx_ext invalid len %zu\n", len);
		return -EINVAL;
	}

	cmd = kzalloc(total, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	memcpy(cmd->dst_mac, mgmt_frame->da, WMI_MAC_LEN);
	cmd->len = cpu_to_le16(len);
	memcpy(cmd->payload, buf, len);
	cmd->channel = channel - 1;
	cmd->duration_ms = cpu_to_le16(duration_ms);

	rc = wmi_call(wil, WMI_SW_TX_REQ_EXT_CMDID, vif->mid, cmd, total,
		      WMI_SW_TX_COMPLETE_EVENTID, &evt, sizeof(evt), 2000);
	if (!rc && evt.evt.status != WMI_FW_STATUS_SUCCESS) {
		wil_dbg_wmi(wil, "mgmt_tx_ext failed with status %d\n",
			    evt.evt.status);
		rc = -EAGAIN;
	}

	kfree(cmd);

	return rc;
}

int wil_wmi_tx_sring_cfg(struct wil6210_priv *wil, int ring_id)
{
	int rc;
	struct wil6210_vif *vif = ndev_to_vif(wil->main_ndev);
	struct wil_status_ring *sring = &wil->srings[ring_id];
	struct wmi_tx_status_ring_add_cmd cmd = {
		.ring_cfg = {
			.ring_size = cpu_to_le16(sring->size),
		},
		.irq_index = WIL_TX_STATUS_IRQ_IDX
	};
	struct {
		struct wmi_cmd_hdr hdr;
		struct wmi_tx_status_ring_cfg_done_event evt;
	} __packed reply = {
		.evt = {.status = WMI_FW_STATUS_FAILURE},
	};

	cmd.ring_cfg.ring_id = ring_id;

	cmd.ring_cfg.ring_mem_base = cpu_to_le64(sring->pa);
	rc = wmi_call(wil, WMI_TX_STATUS_RING_ADD_CMDID, vif->mid, &cmd,
		      sizeof(cmd), WMI_TX_STATUS_RING_CFG_DONE_EVENTID,
		      &reply, sizeof(reply), WIL_WMI_CALL_GENERAL_TO_MS);
	if (rc) {
		wil_err(wil, "TX_STATUS_RING_ADD_CMD failed, rc %d\n", rc);
		return rc;
	}

	if (reply.evt.status != WMI_FW_STATUS_SUCCESS) {
		wil_err(wil, "TX_STATUS_RING_ADD_CMD failed, status %d\n",
			reply.evt.status);
		return -EINVAL;
	}

	sring->hwtail = le32_to_cpu(reply.evt.ring_tail_ptr);

	return 0;
}

int wil_wmi_cfg_def_rx_offload(struct wil6210_priv *wil, u16 max_rx_pl_per_desc)
{
	struct net_device *ndev = wil->main_ndev;
	struct wil6210_vif *vif = ndev_to_vif(ndev);
	int rc;
	struct wmi_cfg_def_rx_offload_cmd cmd = {
		.max_msdu_size = cpu_to_le16(wil_mtu2macbuf(WIL_MAX_ETH_MTU)),
		.max_rx_pl_per_desc = cpu_to_le16(max_rx_pl_per_desc),
		.decap_trans_type = WMI_DECAP_TYPE_802_3,
		.l2_802_3_offload_ctrl = 0,
		.l3_l4_ctrl = 1 << L3_L4_CTRL_TCPIP_CHECKSUM_EN_POS,
	};
	struct {
		struct wmi_cmd_hdr hdr;
		struct wmi_cfg_def_rx_offload_done_event evt;
	} __packed reply = {
		.evt = {.status = WMI_FW_STATUS_FAILURE},
	};

	rc = wmi_call(wil, WMI_CFG_DEF_RX_OFFLOAD_CMDID, vif->mid, &cmd,
		      sizeof(cmd), WMI_CFG_DEF_RX_OFFLOAD_DONE_EVENTID, &reply,
		      sizeof(reply), WIL_WMI_CALL_GENERAL_TO_MS);
	if (rc) {
		wil_err(wil, "WMI_CFG_DEF_RX_OFFLOAD_CMD failed, rc %d\n", rc);
		return rc;
	}

	if (reply.evt.status != WMI_FW_STATUS_SUCCESS) {
		wil_err(wil, "WMI_CFG_DEF_RX_OFFLOAD_CMD failed, status %d\n",
			reply.evt.status);
		return -EINVAL;
	}

	return 0;
}

int wil_wmi_rx_sring_add(struct wil6210_priv *wil, u16 ring_id)
{
	struct net_device *ndev = wil->main_ndev;
	struct wil6210_vif *vif = ndev_to_vif(ndev);
	struct wil_status_ring *sring = &wil->srings[ring_id];
	int rc;
	struct wmi_rx_status_ring_add_cmd cmd = {
		.ring_cfg = {
			.ring_size = cpu_to_le16(sring->size),
			.ring_id = ring_id,
		},
		.rx_msg_type = wil->use_compressed_rx_status ?
			WMI_RX_MSG_TYPE_COMPRESSED :
			WMI_RX_MSG_TYPE_EXTENDED,
		.irq_index = WIL_RX_STATUS_IRQ_IDX,
	};
	struct {
		struct wmi_cmd_hdr hdr;
		struct wmi_rx_status_ring_cfg_done_event evt;
	} __packed reply = {
		.evt = {.status = WMI_FW_STATUS_FAILURE},
	};

	cmd.ring_cfg.ring_mem_base = cpu_to_le64(sring->pa);
	rc = wmi_call(wil, WMI_RX_STATUS_RING_ADD_CMDID, vif->mid, &cmd,
		      sizeof(cmd), WMI_RX_STATUS_RING_CFG_DONE_EVENTID, &reply,
		      sizeof(reply), WIL_WMI_CALL_GENERAL_TO_MS);
	if (rc) {
		wil_err(wil, "RX_STATUS_RING_ADD_CMD failed, rc %d\n", rc);
		return rc;
	}

	if (reply.evt.status != WMI_FW_STATUS_SUCCESS) {
		wil_err(wil, "RX_STATUS_RING_ADD_CMD failed, status %d\n",
			reply.evt.status);
		return -EINVAL;
	}

	sring->hwtail = le32_to_cpu(reply.evt.ring_tail_ptr);

	return 0;
}

int wil_wmi_rx_desc_ring_add(struct wil6210_priv *wil, int status_ring_id)
{
	struct net_device *ndev = wil->main_ndev;
	struct wil6210_vif *vif = ndev_to_vif(ndev);
	struct wil_ring *ring = &wil->ring_rx;
	int rc;
	struct wmi_rx_desc_ring_add_cmd cmd = {
		.ring_cfg = {
			.ring_size = cpu_to_le16(ring->size),
			.ring_id = WIL_RX_DESC_RING_ID,
		},
		.status_ring_id = status_ring_id,
		.irq_index = WIL_RX_STATUS_IRQ_IDX,
	};
	struct {
		struct wmi_cmd_hdr hdr;
		struct wmi_rx_desc_ring_cfg_done_event evt;
	} __packed reply = {
		.evt = {.status = WMI_FW_STATUS_FAILURE},
	};

	cmd.ring_cfg.ring_mem_base = cpu_to_le64(ring->pa);
	cmd.sw_tail_host_addr = cpu_to_le64(ring->edma_rx_swtail.pa);
	rc = wmi_call(wil, WMI_RX_DESC_RING_ADD_CMDID, vif->mid, &cmd,
		      sizeof(cmd), WMI_RX_DESC_RING_CFG_DONE_EVENTID, &reply,
		      sizeof(reply), WIL_WMI_CALL_GENERAL_TO_MS);
	if (rc) {
		wil_err(wil, "WMI_RX_DESC_RING_ADD_CMD failed, rc %d\n", rc);
		return rc;
	}

	if (reply.evt.status != WMI_FW_STATUS_SUCCESS) {
		wil_err(wil, "WMI_RX_DESC_RING_ADD_CMD failed, status %d\n",
			reply.evt.status);
		return -EINVAL;
	}

	ring->hwtail = le32_to_cpu(reply.evt.ring_tail_ptr);

	return 0;
}

int wil_wmi_tx_desc_ring_add(struct wil6210_vif *vif, int ring_id, int cid,
			     int tid)
{
	struct wil6210_priv *wil = vif_to_wil(vif);
	int sring_id = wil->tx_sring_idx; /* there is only one TX sring */
	int rc;
	struct wil_ring *ring = &wil->ring_tx[ring_id];
	struct wil_ring_tx_data *txdata = &wil->ring_tx_data[ring_id];
	struct wmi_tx_desc_ring_add_cmd cmd = {
		.ring_cfg = {
			.ring_size = cpu_to_le16(ring->size),
			.ring_id = ring_id,
		},
		.status_ring_id = sring_id,
		.cid = cid,
		.tid = tid,
		.encap_trans_type = WMI_VRING_ENC_TYPE_802_3,
		.max_msdu_size = cpu_to_le16(wil_mtu2macbuf(mtu_max)),
		.schd_params = {
			.priority = cpu_to_le16(0),
			.timeslot_us = cpu_to_le16(0xfff),
		}
	};
	struct {
		struct wmi_cmd_hdr hdr;
		struct wmi_tx_desc_ring_cfg_done_event evt;
	} __packed reply = {
		.evt = {.status = WMI_FW_STATUS_FAILURE},
	};

	cmd.ring_cfg.ring_mem_base = cpu_to_le64(ring->pa);
	rc = wmi_call(wil, WMI_TX_DESC_RING_ADD_CMDID, vif->mid, &cmd,
		      sizeof(cmd), WMI_TX_DESC_RING_CFG_DONE_EVENTID, &reply,
		      sizeof(reply), WIL_WMI_CALL_GENERAL_TO_MS);
	if (rc) {
		wil_err(wil, "WMI_TX_DESC_RING_ADD_CMD failed, rc %d\n", rc);
		return rc;
	}

	if (reply.evt.status != WMI_FW_STATUS_SUCCESS) {
		wil_err(wil, "WMI_TX_DESC_RING_ADD_CMD failed, status %d\n",
			reply.evt.status);
		return -EINVAL;
	}

	spin_lock_bh(&txdata->lock);
	ring->hwtail = le32_to_cpu(reply.evt.ring_tail_ptr);
	txdata->mid = vif->mid;
	txdata->enabled = 1;
	spin_unlock_bh(&txdata->lock);

	return 0;
}

int wil_wmi_bcast_desc_ring_add(struct wil6210_vif *vif, int ring_id)
{
	struct wil6210_priv *wil = vif_to_wil(vif);
	struct wil_ring *ring = &wil->ring_tx[ring_id];
	int rc;
	struct wmi_bcast_desc_ring_add_cmd cmd = {
		.ring_cfg = {
			.ring_size = cpu_to_le16(ring->size),
			.ring_id = ring_id,
		},
		.status_ring_id = wil->tx_sring_idx,
		.encap_trans_type = WMI_VRING_ENC_TYPE_802_3,
	};
	struct {
		struct wmi_cmd_hdr hdr;
		struct wmi_rx_desc_ring_cfg_done_event evt;
	} __packed reply = {
		.evt = {.status = WMI_FW_STATUS_FAILURE},
	};
	struct wil_ring_tx_data *txdata = &wil->ring_tx_data[ring_id];

	cmd.ring_cfg.ring_mem_base = cpu_to_le64(ring->pa);
	rc = wmi_call(wil, WMI_BCAST_DESC_RING_ADD_CMDID, vif->mid, &cmd,
		      sizeof(cmd), WMI_TX_DESC_RING_CFG_DONE_EVENTID, &reply,
		      sizeof(reply), WIL_WMI_CALL_GENERAL_TO_MS);
	if (rc) {
		wil_err(wil, "WMI_BCAST_DESC_RING_ADD_CMD failed, rc %d\n", rc);
		return rc;
	}

	if (reply.evt.status != WMI_FW_STATUS_SUCCESS) {
		wil_err(wil, "Broadcast Tx config failed, status %d\n",
			reply.evt.status);
		return -EINVAL;
	}

	spin_lock_bh(&txdata->lock);
	ring->hwtail = le32_to_cpu(reply.evt.ring_tail_ptr);
	txdata->mid = vif->mid;
	txdata->enabled = 1;
	spin_unlock_bh(&txdata->lock);

	return 0;
}

int wmi_link_stats_cfg(struct wil6210_vif *vif, u32 type, u8 cid, u32 interval)
{
	struct wil6210_priv *wil = vif_to_wil(vif);
	struct wmi_link_stats_cmd cmd = {
		.record_type_mask = cpu_to_le32(type),
		.cid = cid,
		.action = WMI_LINK_STATS_SNAPSHOT,
		.interval_msec = cpu_to_le32(interval),
	};
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_link_stats_config_done_event evt;
	} __packed reply = {
		.evt = {.status = WMI_FW_STATUS_FAILURE},
	};
	int rc;

	rc = wmi_call(wil, WMI_LINK_STATS_CMDID, vif->mid, &cmd, sizeof(cmd),
		      WMI_LINK_STATS_CONFIG_DONE_EVENTID, &reply,
		      sizeof(reply), WIL_WMI_CALL_GENERAL_TO_MS);
	if (rc) {
		wil_err(wil, "WMI_LINK_STATS_CMDID failed, rc %d\n", rc);
		return rc;
	}

	if (reply.evt.status != WMI_FW_STATUS_SUCCESS) {
		wil_err(wil, "Link statistics config failed, status %d\n",
			reply.evt.status);
		return -EINVAL;
	}

	return 0;
}
