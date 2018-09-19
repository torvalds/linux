// SPDX-License-Identifier: GPL-2.0+
// Copyright (c) 2016-2017 Hisilicon Limited.

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/etherdevice.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/if_vlan.h>
#include <net/rtnetlink.h>
#include "hclge_cmd.h"
#include "hclge_dcb.h"
#include "hclge_main.h"
#include "hclge_mbx.h"
#include "hclge_mdio.h"
#include "hclge_tm.h"
#include "hnae3.h"

#define HCLGE_NAME			"hclge"
#define HCLGE_STATS_READ(p, offset) (*((u64 *)((u8 *)(p) + (offset))))
#define HCLGE_MAC_STATS_FIELD_OFF(f) (offsetof(struct hclge_mac_stats, f))
#define HCLGE_64BIT_STATS_FIELD_OFF(f) (offsetof(struct hclge_64_bit_stats, f))
#define HCLGE_32BIT_STATS_FIELD_OFF(f) (offsetof(struct hclge_32_bit_stats, f))

static int hclge_set_mta_filter_mode(struct hclge_dev *hdev,
				     enum hclge_mta_dmac_sel_type mta_mac_sel,
				     bool enable);
static int hclge_set_mtu(struct hnae3_handle *handle, int new_mtu);
static int hclge_init_vlan_config(struct hclge_dev *hdev);
static int hclge_reset_ae_dev(struct hnae3_ae_dev *ae_dev);

static struct hnae3_ae_algo ae_algo;

static const struct pci_device_id ae_algo_pci_tbl[] = {
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_GE), 0},
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_25GE), 0},
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_25GE_RDMA), 0},
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_25GE_RDMA_MACSEC), 0},
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_50GE_RDMA), 0},
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_50GE_RDMA_MACSEC), 0},
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_100G_RDMA_MACSEC), 0},
	/* required last entry */
	{0, }
};

MODULE_DEVICE_TABLE(pci, ae_algo_pci_tbl);

static const char hns3_nic_test_strs[][ETH_GSTRING_LEN] = {
	"Mac    Loopback test",
	"Serdes Loopback test",
	"Phy    Loopback test"
};

static const struct hclge_comm_stats_str g_all_64bit_stats_string[] = {
	{"igu_rx_oversize_pkt",
		HCLGE_64BIT_STATS_FIELD_OFF(igu_rx_oversize_pkt)},
	{"igu_rx_undersize_pkt",
		HCLGE_64BIT_STATS_FIELD_OFF(igu_rx_undersize_pkt)},
	{"igu_rx_out_all_pkt",
		HCLGE_64BIT_STATS_FIELD_OFF(igu_rx_out_all_pkt)},
	{"igu_rx_uni_pkt",
		HCLGE_64BIT_STATS_FIELD_OFF(igu_rx_uni_pkt)},
	{"igu_rx_multi_pkt",
		HCLGE_64BIT_STATS_FIELD_OFF(igu_rx_multi_pkt)},
	{"igu_rx_broad_pkt",
		HCLGE_64BIT_STATS_FIELD_OFF(igu_rx_broad_pkt)},
	{"egu_tx_out_all_pkt",
		HCLGE_64BIT_STATS_FIELD_OFF(egu_tx_out_all_pkt)},
	{"egu_tx_uni_pkt",
		HCLGE_64BIT_STATS_FIELD_OFF(egu_tx_uni_pkt)},
	{"egu_tx_multi_pkt",
		HCLGE_64BIT_STATS_FIELD_OFF(egu_tx_multi_pkt)},
	{"egu_tx_broad_pkt",
		HCLGE_64BIT_STATS_FIELD_OFF(egu_tx_broad_pkt)},
	{"ssu_ppp_mac_key_num",
		HCLGE_64BIT_STATS_FIELD_OFF(ssu_ppp_mac_key_num)},
	{"ssu_ppp_host_key_num",
		HCLGE_64BIT_STATS_FIELD_OFF(ssu_ppp_host_key_num)},
	{"ppp_ssu_mac_rlt_num",
		HCLGE_64BIT_STATS_FIELD_OFF(ppp_ssu_mac_rlt_num)},
	{"ppp_ssu_host_rlt_num",
		HCLGE_64BIT_STATS_FIELD_OFF(ppp_ssu_host_rlt_num)},
	{"ssu_tx_in_num",
		HCLGE_64BIT_STATS_FIELD_OFF(ssu_tx_in_num)},
	{"ssu_tx_out_num",
		HCLGE_64BIT_STATS_FIELD_OFF(ssu_tx_out_num)},
	{"ssu_rx_in_num",
		HCLGE_64BIT_STATS_FIELD_OFF(ssu_rx_in_num)},
	{"ssu_rx_out_num",
		HCLGE_64BIT_STATS_FIELD_OFF(ssu_rx_out_num)}
};

static const struct hclge_comm_stats_str g_all_32bit_stats_string[] = {
	{"igu_rx_err_pkt",
		HCLGE_32BIT_STATS_FIELD_OFF(igu_rx_err_pkt)},
	{"igu_rx_no_eof_pkt",
		HCLGE_32BIT_STATS_FIELD_OFF(igu_rx_no_eof_pkt)},
	{"igu_rx_no_sof_pkt",
		HCLGE_32BIT_STATS_FIELD_OFF(igu_rx_no_sof_pkt)},
	{"egu_tx_1588_pkt",
		HCLGE_32BIT_STATS_FIELD_OFF(egu_tx_1588_pkt)},
	{"ssu_full_drop_num",
		HCLGE_32BIT_STATS_FIELD_OFF(ssu_full_drop_num)},
	{"ssu_part_drop_num",
		HCLGE_32BIT_STATS_FIELD_OFF(ssu_part_drop_num)},
	{"ppp_key_drop_num",
		HCLGE_32BIT_STATS_FIELD_OFF(ppp_key_drop_num)},
	{"ppp_rlt_drop_num",
		HCLGE_32BIT_STATS_FIELD_OFF(ppp_rlt_drop_num)},
	{"ssu_key_drop_num",
		HCLGE_32BIT_STATS_FIELD_OFF(ssu_key_drop_num)},
	{"pkt_curr_buf_cnt",
		HCLGE_32BIT_STATS_FIELD_OFF(pkt_curr_buf_cnt)},
	{"qcn_fb_rcv_cnt",
		HCLGE_32BIT_STATS_FIELD_OFF(qcn_fb_rcv_cnt)},
	{"qcn_fb_drop_cnt",
		HCLGE_32BIT_STATS_FIELD_OFF(qcn_fb_drop_cnt)},
	{"qcn_fb_invaild_cnt",
		HCLGE_32BIT_STATS_FIELD_OFF(qcn_fb_invaild_cnt)},
	{"rx_packet_tc0_in_cnt",
		HCLGE_32BIT_STATS_FIELD_OFF(rx_packet_tc0_in_cnt)},
	{"rx_packet_tc1_in_cnt",
		HCLGE_32BIT_STATS_FIELD_OFF(rx_packet_tc1_in_cnt)},
	{"rx_packet_tc2_in_cnt",
		HCLGE_32BIT_STATS_FIELD_OFF(rx_packet_tc2_in_cnt)},
	{"rx_packet_tc3_in_cnt",
		HCLGE_32BIT_STATS_FIELD_OFF(rx_packet_tc3_in_cnt)},
	{"rx_packet_tc4_in_cnt",
		HCLGE_32BIT_STATS_FIELD_OFF(rx_packet_tc4_in_cnt)},
	{"rx_packet_tc5_in_cnt",
		HCLGE_32BIT_STATS_FIELD_OFF(rx_packet_tc5_in_cnt)},
	{"rx_packet_tc6_in_cnt",
		HCLGE_32BIT_STATS_FIELD_OFF(rx_packet_tc6_in_cnt)},
	{"rx_packet_tc7_in_cnt",
		HCLGE_32BIT_STATS_FIELD_OFF(rx_packet_tc7_in_cnt)},
	{"rx_packet_tc0_out_cnt",
		HCLGE_32BIT_STATS_FIELD_OFF(rx_packet_tc0_out_cnt)},
	{"rx_packet_tc1_out_cnt",
		HCLGE_32BIT_STATS_FIELD_OFF(rx_packet_tc1_out_cnt)},
	{"rx_packet_tc2_out_cnt",
		HCLGE_32BIT_STATS_FIELD_OFF(rx_packet_tc2_out_cnt)},
	{"rx_packet_tc3_out_cnt",
		HCLGE_32BIT_STATS_FIELD_OFF(rx_packet_tc3_out_cnt)},
	{"rx_packet_tc4_out_cnt",
		HCLGE_32BIT_STATS_FIELD_OFF(rx_packet_tc4_out_cnt)},
	{"rx_packet_tc5_out_cnt",
		HCLGE_32BIT_STATS_FIELD_OFF(rx_packet_tc5_out_cnt)},
	{"rx_packet_tc6_out_cnt",
		HCLGE_32BIT_STATS_FIELD_OFF(rx_packet_tc6_out_cnt)},
	{"rx_packet_tc7_out_cnt",
		HCLGE_32BIT_STATS_FIELD_OFF(rx_packet_tc7_out_cnt)},
	{"tx_packet_tc0_in_cnt",
		HCLGE_32BIT_STATS_FIELD_OFF(tx_packet_tc0_in_cnt)},
	{"tx_packet_tc1_in_cnt",
		HCLGE_32BIT_STATS_FIELD_OFF(tx_packet_tc1_in_cnt)},
	{"tx_packet_tc2_in_cnt",
		HCLGE_32BIT_STATS_FIELD_OFF(tx_packet_tc2_in_cnt)},
	{"tx_packet_tc3_in_cnt",
		HCLGE_32BIT_STATS_FIELD_OFF(tx_packet_tc3_in_cnt)},
	{"tx_packet_tc4_in_cnt",
		HCLGE_32BIT_STATS_FIELD_OFF(tx_packet_tc4_in_cnt)},
	{"tx_packet_tc5_in_cnt",
		HCLGE_32BIT_STATS_FIELD_OFF(tx_packet_tc5_in_cnt)},
	{"tx_packet_tc6_in_cnt",
		HCLGE_32BIT_STATS_FIELD_OFF(tx_packet_tc6_in_cnt)},
	{"tx_packet_tc7_in_cnt",
		HCLGE_32BIT_STATS_FIELD_OFF(tx_packet_tc7_in_cnt)},
	{"tx_packet_tc0_out_cnt",
		HCLGE_32BIT_STATS_FIELD_OFF(tx_packet_tc0_out_cnt)},
	{"tx_packet_tc1_out_cnt",
		HCLGE_32BIT_STATS_FIELD_OFF(tx_packet_tc1_out_cnt)},
	{"tx_packet_tc2_out_cnt",
		HCLGE_32BIT_STATS_FIELD_OFF(tx_packet_tc2_out_cnt)},
	{"tx_packet_tc3_out_cnt",
		HCLGE_32BIT_STATS_FIELD_OFF(tx_packet_tc3_out_cnt)},
	{"tx_packet_tc4_out_cnt",
		HCLGE_32BIT_STATS_FIELD_OFF(tx_packet_tc4_out_cnt)},
	{"tx_packet_tc5_out_cnt",
		HCLGE_32BIT_STATS_FIELD_OFF(tx_packet_tc5_out_cnt)},
	{"tx_packet_tc6_out_cnt",
		HCLGE_32BIT_STATS_FIELD_OFF(tx_packet_tc6_out_cnt)},
	{"tx_packet_tc7_out_cnt",
		HCLGE_32BIT_STATS_FIELD_OFF(tx_packet_tc7_out_cnt)},
	{"pkt_curr_buf_tc0_cnt",
		HCLGE_32BIT_STATS_FIELD_OFF(pkt_curr_buf_tc0_cnt)},
	{"pkt_curr_buf_tc1_cnt",
		HCLGE_32BIT_STATS_FIELD_OFF(pkt_curr_buf_tc1_cnt)},
	{"pkt_curr_buf_tc2_cnt",
		HCLGE_32BIT_STATS_FIELD_OFF(pkt_curr_buf_tc2_cnt)},
	{"pkt_curr_buf_tc3_cnt",
		HCLGE_32BIT_STATS_FIELD_OFF(pkt_curr_buf_tc3_cnt)},
	{"pkt_curr_buf_tc4_cnt",
		HCLGE_32BIT_STATS_FIELD_OFF(pkt_curr_buf_tc4_cnt)},
	{"pkt_curr_buf_tc5_cnt",
		HCLGE_32BIT_STATS_FIELD_OFF(pkt_curr_buf_tc5_cnt)},
	{"pkt_curr_buf_tc6_cnt",
		HCLGE_32BIT_STATS_FIELD_OFF(pkt_curr_buf_tc6_cnt)},
	{"pkt_curr_buf_tc7_cnt",
		HCLGE_32BIT_STATS_FIELD_OFF(pkt_curr_buf_tc7_cnt)},
	{"mb_uncopy_num",
		HCLGE_32BIT_STATS_FIELD_OFF(mb_uncopy_num)},
	{"lo_pri_unicast_rlt_drop_num",
		HCLGE_32BIT_STATS_FIELD_OFF(lo_pri_unicast_rlt_drop_num)},
	{"hi_pri_multicast_rlt_drop_num",
		HCLGE_32BIT_STATS_FIELD_OFF(hi_pri_multicast_rlt_drop_num)},
	{"lo_pri_multicast_rlt_drop_num",
		HCLGE_32BIT_STATS_FIELD_OFF(lo_pri_multicast_rlt_drop_num)},
	{"rx_oq_drop_pkt_cnt",
		HCLGE_32BIT_STATS_FIELD_OFF(rx_oq_drop_pkt_cnt)},
	{"tx_oq_drop_pkt_cnt",
		HCLGE_32BIT_STATS_FIELD_OFF(tx_oq_drop_pkt_cnt)},
	{"nic_l2_err_drop_pkt_cnt",
		HCLGE_32BIT_STATS_FIELD_OFF(nic_l2_err_drop_pkt_cnt)},
	{"roc_l2_err_drop_pkt_cnt",
		HCLGE_32BIT_STATS_FIELD_OFF(roc_l2_err_drop_pkt_cnt)}
};

static const struct hclge_comm_stats_str g_mac_stats_string[] = {
	{"mac_tx_mac_pause_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_tx_mac_pause_num)},
	{"mac_rx_mac_pause_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_rx_mac_pause_num)},
	{"mac_tx_pfc_pri0_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_tx_pfc_pri0_pkt_num)},
	{"mac_tx_pfc_pri1_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_tx_pfc_pri1_pkt_num)},
	{"mac_tx_pfc_pri2_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_tx_pfc_pri2_pkt_num)},
	{"mac_tx_pfc_pri3_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_tx_pfc_pri3_pkt_num)},
	{"mac_tx_pfc_pri4_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_tx_pfc_pri4_pkt_num)},
	{"mac_tx_pfc_pri5_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_tx_pfc_pri5_pkt_num)},
	{"mac_tx_pfc_pri6_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_tx_pfc_pri6_pkt_num)},
	{"mac_tx_pfc_pri7_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_tx_pfc_pri7_pkt_num)},
	{"mac_rx_pfc_pri0_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_rx_pfc_pri0_pkt_num)},
	{"mac_rx_pfc_pri1_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_rx_pfc_pri1_pkt_num)},
	{"mac_rx_pfc_pri2_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_rx_pfc_pri2_pkt_num)},
	{"mac_rx_pfc_pri3_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_rx_pfc_pri3_pkt_num)},
	{"mac_rx_pfc_pri4_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_rx_pfc_pri4_pkt_num)},
	{"mac_rx_pfc_pri5_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_rx_pfc_pri5_pkt_num)},
	{"mac_rx_pfc_pri6_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_rx_pfc_pri6_pkt_num)},
	{"mac_rx_pfc_pri7_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_rx_pfc_pri7_pkt_num)},
	{"mac_tx_total_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_tx_total_pkt_num)},
	{"mac_tx_total_oct_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_tx_total_oct_num)},
	{"mac_tx_good_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_tx_good_pkt_num)},
	{"mac_tx_bad_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_tx_bad_pkt_num)},
	{"mac_tx_good_oct_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_tx_good_oct_num)},
	{"mac_tx_bad_oct_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_tx_bad_oct_num)},
	{"mac_tx_uni_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_tx_uni_pkt_num)},
	{"mac_tx_multi_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_tx_multi_pkt_num)},
	{"mac_tx_broad_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_tx_broad_pkt_num)},
	{"mac_tx_undersize_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_tx_undersize_pkt_num)},
	{"mac_tx_oversize_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_tx_oversize_pkt_num)},
	{"mac_tx_64_oct_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_tx_64_oct_pkt_num)},
	{"mac_tx_65_127_oct_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_tx_65_127_oct_pkt_num)},
	{"mac_tx_128_255_oct_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_tx_128_255_oct_pkt_num)},
	{"mac_tx_256_511_oct_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_tx_256_511_oct_pkt_num)},
	{"mac_tx_512_1023_oct_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_tx_512_1023_oct_pkt_num)},
	{"mac_tx_1024_1518_oct_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_tx_1024_1518_oct_pkt_num)},
	{"mac_tx_1519_2047_oct_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_tx_1519_2047_oct_pkt_num)},
	{"mac_tx_2048_4095_oct_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_tx_2048_4095_oct_pkt_num)},
	{"mac_tx_4096_8191_oct_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_tx_4096_8191_oct_pkt_num)},
	{"mac_tx_8192_9216_oct_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_tx_8192_9216_oct_pkt_num)},
	{"mac_tx_9217_12287_oct_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_tx_9217_12287_oct_pkt_num)},
	{"mac_tx_12288_16383_oct_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_tx_12288_16383_oct_pkt_num)},
	{"mac_tx_1519_max_good_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_tx_1519_max_good_oct_pkt_num)},
	{"mac_tx_1519_max_bad_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_tx_1519_max_bad_oct_pkt_num)},
	{"mac_rx_total_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_rx_total_pkt_num)},
	{"mac_rx_total_oct_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_rx_total_oct_num)},
	{"mac_rx_good_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_rx_good_pkt_num)},
	{"mac_rx_bad_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_rx_bad_pkt_num)},
	{"mac_rx_good_oct_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_rx_good_oct_num)},
	{"mac_rx_bad_oct_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_rx_bad_oct_num)},
	{"mac_rx_uni_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_rx_uni_pkt_num)},
	{"mac_rx_multi_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_rx_multi_pkt_num)},
	{"mac_rx_broad_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_rx_broad_pkt_num)},
	{"mac_rx_undersize_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_rx_undersize_pkt_num)},
	{"mac_rx_oversize_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_rx_oversize_pkt_num)},
	{"mac_rx_64_oct_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_rx_64_oct_pkt_num)},
	{"mac_rx_65_127_oct_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_rx_65_127_oct_pkt_num)},
	{"mac_rx_128_255_oct_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_rx_128_255_oct_pkt_num)},
	{"mac_rx_256_511_oct_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_rx_256_511_oct_pkt_num)},
	{"mac_rx_512_1023_oct_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_rx_512_1023_oct_pkt_num)},
	{"mac_rx_1024_1518_oct_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_rx_1024_1518_oct_pkt_num)},
	{"mac_rx_1519_2047_oct_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_rx_1519_2047_oct_pkt_num)},
	{"mac_rx_2048_4095_oct_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_rx_2048_4095_oct_pkt_num)},
	{"mac_rx_4096_8191_oct_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_rx_4096_8191_oct_pkt_num)},
	{"mac_rx_8192_9216_oct_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_rx_8192_9216_oct_pkt_num)},
	{"mac_rx_9217_12287_oct_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_rx_9217_12287_oct_pkt_num)},
	{"mac_rx_12288_16383_oct_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_rx_12288_16383_oct_pkt_num)},
	{"mac_rx_1519_max_good_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_rx_1519_max_good_oct_pkt_num)},
	{"mac_rx_1519_max_bad_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_rx_1519_max_bad_oct_pkt_num)},

	{"mac_tx_fragment_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_tx_fragment_pkt_num)},
	{"mac_tx_undermin_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_tx_undermin_pkt_num)},
	{"mac_tx_jabber_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_tx_jabber_pkt_num)},
	{"mac_tx_err_all_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_tx_err_all_pkt_num)},
	{"mac_tx_from_app_good_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_tx_from_app_good_pkt_num)},
	{"mac_tx_from_app_bad_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_tx_from_app_bad_pkt_num)},
	{"mac_rx_fragment_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_rx_fragment_pkt_num)},
	{"mac_rx_undermin_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_rx_undermin_pkt_num)},
	{"mac_rx_jabber_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_rx_jabber_pkt_num)},
	{"mac_rx_fcs_err_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_rx_fcs_err_pkt_num)},
	{"mac_rx_send_app_good_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_rx_send_app_good_pkt_num)},
	{"mac_rx_send_app_bad_pkt_num",
		HCLGE_MAC_STATS_FIELD_OFF(mac_rx_send_app_bad_pkt_num)}
};

static const struct hclge_mac_mgr_tbl_entry_cmd hclge_mgr_table[] = {
	{
		.flags = HCLGE_MAC_MGR_MASK_VLAN_B,
		.ethter_type = cpu_to_le16(HCLGE_MAC_ETHERTYPE_LLDP),
		.mac_addr_hi32 = cpu_to_le32(htonl(0x0180C200)),
		.mac_addr_lo16 = cpu_to_le16(htons(0x000E)),
		.i_port_bitmap = 0x1,
	},
};

static int hclge_64_bit_update_stats(struct hclge_dev *hdev)
{
#define HCLGE_64_BIT_CMD_NUM 5
#define HCLGE_64_BIT_RTN_DATANUM 4
	u64 *data = (u64 *)(&hdev->hw_stats.all_64_bit_stats);
	struct hclge_desc desc[HCLGE_64_BIT_CMD_NUM];
	__le64 *desc_data;
	int i, k, n;
	int ret;

	hclge_cmd_setup_basic_desc(&desc[0], HCLGE_OPC_STATS_64_BIT, true);
	ret = hclge_cmd_send(&hdev->hw, desc, HCLGE_64_BIT_CMD_NUM);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"Get 64 bit pkt stats fail, status = %d.\n", ret);
		return ret;
	}

	for (i = 0; i < HCLGE_64_BIT_CMD_NUM; i++) {
		if (unlikely(i == 0)) {
			desc_data = (__le64 *)(&desc[i].data[0]);
			n = HCLGE_64_BIT_RTN_DATANUM - 1;
		} else {
			desc_data = (__le64 *)(&desc[i]);
			n = HCLGE_64_BIT_RTN_DATANUM;
		}
		for (k = 0; k < n; k++) {
			*data++ += le64_to_cpu(*desc_data);
			desc_data++;
		}
	}

	return 0;
}

static void hclge_reset_partial_32bit_counter(struct hclge_32_bit_stats *stats)
{
	stats->pkt_curr_buf_cnt     = 0;
	stats->pkt_curr_buf_tc0_cnt = 0;
	stats->pkt_curr_buf_tc1_cnt = 0;
	stats->pkt_curr_buf_tc2_cnt = 0;
	stats->pkt_curr_buf_tc3_cnt = 0;
	stats->pkt_curr_buf_tc4_cnt = 0;
	stats->pkt_curr_buf_tc5_cnt = 0;
	stats->pkt_curr_buf_tc6_cnt = 0;
	stats->pkt_curr_buf_tc7_cnt = 0;
}

static int hclge_32_bit_update_stats(struct hclge_dev *hdev)
{
#define HCLGE_32_BIT_CMD_NUM 8
#define HCLGE_32_BIT_RTN_DATANUM 8

	struct hclge_desc desc[HCLGE_32_BIT_CMD_NUM];
	struct hclge_32_bit_stats *all_32_bit_stats;
	__le32 *desc_data;
	int i, k, n;
	u64 *data;
	int ret;

	all_32_bit_stats = &hdev->hw_stats.all_32_bit_stats;
	data = (u64 *)(&all_32_bit_stats->egu_tx_1588_pkt);

	hclge_cmd_setup_basic_desc(&desc[0], HCLGE_OPC_STATS_32_BIT, true);
	ret = hclge_cmd_send(&hdev->hw, desc, HCLGE_32_BIT_CMD_NUM);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"Get 32 bit pkt stats fail, status = %d.\n", ret);

		return ret;
	}

	hclge_reset_partial_32bit_counter(all_32_bit_stats);
	for (i = 0; i < HCLGE_32_BIT_CMD_NUM; i++) {
		if (unlikely(i == 0)) {
			__le16 *desc_data_16bit;

			all_32_bit_stats->igu_rx_err_pkt +=
				le32_to_cpu(desc[i].data[0]);

			desc_data_16bit = (__le16 *)&desc[i].data[1];
			all_32_bit_stats->igu_rx_no_eof_pkt +=
				le16_to_cpu(*desc_data_16bit);

			desc_data_16bit++;
			all_32_bit_stats->igu_rx_no_sof_pkt +=
				le16_to_cpu(*desc_data_16bit);

			desc_data = &desc[i].data[2];
			n = HCLGE_32_BIT_RTN_DATANUM - 4;
		} else {
			desc_data = (__le32 *)&desc[i];
			n = HCLGE_32_BIT_RTN_DATANUM;
		}
		for (k = 0; k < n; k++) {
			*data++ += le32_to_cpu(*desc_data);
			desc_data++;
		}
	}

	return 0;
}

static int hclge_mac_update_stats(struct hclge_dev *hdev)
{
#define HCLGE_MAC_CMD_NUM 21
#define HCLGE_RTN_DATA_NUM 4

	u64 *data = (u64 *)(&hdev->hw_stats.mac_stats);
	struct hclge_desc desc[HCLGE_MAC_CMD_NUM];
	__le64 *desc_data;
	int i, k, n;
	int ret;

	hclge_cmd_setup_basic_desc(&desc[0], HCLGE_OPC_STATS_MAC, true);
	ret = hclge_cmd_send(&hdev->hw, desc, HCLGE_MAC_CMD_NUM);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"Get MAC pkt stats fail, status = %d.\n", ret);

		return ret;
	}

	for (i = 0; i < HCLGE_MAC_CMD_NUM; i++) {
		if (unlikely(i == 0)) {
			desc_data = (__le64 *)(&desc[i].data[0]);
			n = HCLGE_RTN_DATA_NUM - 2;
		} else {
			desc_data = (__le64 *)(&desc[i]);
			n = HCLGE_RTN_DATA_NUM;
		}
		for (k = 0; k < n; k++) {
			*data++ += le64_to_cpu(*desc_data);
			desc_data++;
		}
	}

	return 0;
}

static int hclge_tqps_update_stats(struct hnae3_handle *handle)
{
	struct hnae3_knic_private_info *kinfo = &handle->kinfo;
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;
	struct hnae3_queue *queue;
	struct hclge_desc desc[1];
	struct hclge_tqp *tqp;
	int ret, i;

	for (i = 0; i < kinfo->num_tqps; i++) {
		queue = handle->kinfo.tqp[i];
		tqp = container_of(queue, struct hclge_tqp, q);
		/* command : HCLGE_OPC_QUERY_IGU_STAT */
		hclge_cmd_setup_basic_desc(&desc[0],
					   HCLGE_OPC_QUERY_RX_STATUS,
					   true);

		desc[0].data[0] = cpu_to_le32((tqp->index & 0x1ff));
		ret = hclge_cmd_send(&hdev->hw, desc, 1);
		if (ret) {
			dev_err(&hdev->pdev->dev,
				"Query tqp stat fail, status = %d,queue = %d\n",
				ret,	i);
			return ret;
		}
		tqp->tqp_stats.rcb_rx_ring_pktnum_rcd +=
			le32_to_cpu(desc[0].data[1]);
	}

	for (i = 0; i < kinfo->num_tqps; i++) {
		queue = handle->kinfo.tqp[i];
		tqp = container_of(queue, struct hclge_tqp, q);
		/* command : HCLGE_OPC_QUERY_IGU_STAT */
		hclge_cmd_setup_basic_desc(&desc[0],
					   HCLGE_OPC_QUERY_TX_STATUS,
					   true);

		desc[0].data[0] = cpu_to_le32((tqp->index & 0x1ff));
		ret = hclge_cmd_send(&hdev->hw, desc, 1);
		if (ret) {
			dev_err(&hdev->pdev->dev,
				"Query tqp stat fail, status = %d,queue = %d\n",
				ret, i);
			return ret;
		}
		tqp->tqp_stats.rcb_tx_ring_pktnum_rcd +=
			le32_to_cpu(desc[0].data[1]);
	}

	return 0;
}

static u64 *hclge_tqps_get_stats(struct hnae3_handle *handle, u64 *data)
{
	struct hnae3_knic_private_info *kinfo = &handle->kinfo;
	struct hclge_tqp *tqp;
	u64 *buff = data;
	int i;

	for (i = 0; i < kinfo->num_tqps; i++) {
		tqp = container_of(kinfo->tqp[i], struct hclge_tqp, q);
		*buff++ = tqp->tqp_stats.rcb_tx_ring_pktnum_rcd;
	}

	for (i = 0; i < kinfo->num_tqps; i++) {
		tqp = container_of(kinfo->tqp[i], struct hclge_tqp, q);
		*buff++ = tqp->tqp_stats.rcb_rx_ring_pktnum_rcd;
	}

	return buff;
}

static int hclge_tqps_get_sset_count(struct hnae3_handle *handle, int stringset)
{
	struct hnae3_knic_private_info *kinfo = &handle->kinfo;

	return kinfo->num_tqps * (2);
}

static u8 *hclge_tqps_get_strings(struct hnae3_handle *handle, u8 *data)
{
	struct hnae3_knic_private_info *kinfo = &handle->kinfo;
	u8 *buff = data;
	int i = 0;

	for (i = 0; i < kinfo->num_tqps; i++) {
		struct hclge_tqp *tqp = container_of(handle->kinfo.tqp[i],
			struct hclge_tqp, q);
		snprintf(buff, ETH_GSTRING_LEN, "txq#%d_pktnum_rcd",
			 tqp->index);
		buff = buff + ETH_GSTRING_LEN;
	}

	for (i = 0; i < kinfo->num_tqps; i++) {
		struct hclge_tqp *tqp = container_of(kinfo->tqp[i],
			struct hclge_tqp, q);
		snprintf(buff, ETH_GSTRING_LEN, "rxq#%d_pktnum_rcd",
			 tqp->index);
		buff = buff + ETH_GSTRING_LEN;
	}

	return buff;
}

static u64 *hclge_comm_get_stats(void *comm_stats,
				 const struct hclge_comm_stats_str strs[],
				 int size, u64 *data)
{
	u64 *buf = data;
	u32 i;

	for (i = 0; i < size; i++)
		buf[i] = HCLGE_STATS_READ(comm_stats, strs[i].offset);

	return buf + size;
}

static u8 *hclge_comm_get_strings(u32 stringset,
				  const struct hclge_comm_stats_str strs[],
				  int size, u8 *data)
{
	char *buff = (char *)data;
	u32 i;

	if (stringset != ETH_SS_STATS)
		return buff;

	for (i = 0; i < size; i++) {
		snprintf(buff, ETH_GSTRING_LEN, "%s", strs[i].desc);
		buff = buff + ETH_GSTRING_LEN;
	}

	return (u8 *)buff;
}

static void hclge_update_netstat(struct hclge_hw_stats *hw_stats,
				 struct net_device_stats *net_stats)
{
	net_stats->tx_dropped = 0;
	net_stats->rx_dropped = hw_stats->all_32_bit_stats.ssu_full_drop_num;
	net_stats->rx_dropped += hw_stats->all_32_bit_stats.ppp_key_drop_num;
	net_stats->rx_dropped += hw_stats->all_32_bit_stats.ssu_key_drop_num;

	net_stats->rx_errors = hw_stats->mac_stats.mac_rx_oversize_pkt_num;
	net_stats->rx_errors += hw_stats->mac_stats.mac_rx_undersize_pkt_num;
	net_stats->rx_errors += hw_stats->all_32_bit_stats.igu_rx_no_eof_pkt;
	net_stats->rx_errors += hw_stats->all_32_bit_stats.igu_rx_no_sof_pkt;
	net_stats->rx_errors += hw_stats->mac_stats.mac_rx_fcs_err_pkt_num;

	net_stats->multicast = hw_stats->mac_stats.mac_tx_multi_pkt_num;
	net_stats->multicast += hw_stats->mac_stats.mac_rx_multi_pkt_num;

	net_stats->rx_crc_errors = hw_stats->mac_stats.mac_rx_fcs_err_pkt_num;
	net_stats->rx_length_errors =
		hw_stats->mac_stats.mac_rx_undersize_pkt_num;
	net_stats->rx_length_errors +=
		hw_stats->mac_stats.mac_rx_oversize_pkt_num;
	net_stats->rx_over_errors =
		hw_stats->mac_stats.mac_rx_oversize_pkt_num;
}

static void hclge_update_stats_for_all(struct hclge_dev *hdev)
{
	struct hnae3_handle *handle;
	int status;

	handle = &hdev->vport[0].nic;
	if (handle->client) {
		status = hclge_tqps_update_stats(handle);
		if (status) {
			dev_err(&hdev->pdev->dev,
				"Update TQPS stats fail, status = %d.\n",
				status);
		}
	}

	status = hclge_mac_update_stats(hdev);
	if (status)
		dev_err(&hdev->pdev->dev,
			"Update MAC stats fail, status = %d.\n", status);

	status = hclge_32_bit_update_stats(hdev);
	if (status)
		dev_err(&hdev->pdev->dev,
			"Update 32 bit stats fail, status = %d.\n",
			status);

	hclge_update_netstat(&hdev->hw_stats, &handle->kinfo.netdev->stats);
}

static void hclge_update_stats(struct hnae3_handle *handle,
			       struct net_device_stats *net_stats)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;
	struct hclge_hw_stats *hw_stats = &hdev->hw_stats;
	int status;

	if (test_and_set_bit(HCLGE_STATE_STATISTICS_UPDATING, &hdev->state))
		return;

	status = hclge_mac_update_stats(hdev);
	if (status)
		dev_err(&hdev->pdev->dev,
			"Update MAC stats fail, status = %d.\n",
			status);

	status = hclge_32_bit_update_stats(hdev);
	if (status)
		dev_err(&hdev->pdev->dev,
			"Update 32 bit stats fail, status = %d.\n",
			status);

	status = hclge_64_bit_update_stats(hdev);
	if (status)
		dev_err(&hdev->pdev->dev,
			"Update 64 bit stats fail, status = %d.\n",
			status);

	status = hclge_tqps_update_stats(handle);
	if (status)
		dev_err(&hdev->pdev->dev,
			"Update TQPS stats fail, status = %d.\n",
			status);

	hclge_update_netstat(hw_stats, net_stats);

	clear_bit(HCLGE_STATE_STATISTICS_UPDATING, &hdev->state);
}

static int hclge_get_sset_count(struct hnae3_handle *handle, int stringset)
{
#define HCLGE_LOOPBACK_TEST_FLAGS 0x7

	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;
	int count = 0;

	/* Loopback test support rules:
	 * mac: only GE mode support
	 * serdes: all mac mode will support include GE/XGE/LGE/CGE
	 * phy: only support when phy device exist on board
	 */
	if (stringset == ETH_SS_TEST) {
		/* clear loopback bit flags at first */
		handle->flags = (handle->flags & (~HCLGE_LOOPBACK_TEST_FLAGS));
		if (hdev->hw.mac.speed == HCLGE_MAC_SPEED_10M ||
		    hdev->hw.mac.speed == HCLGE_MAC_SPEED_100M ||
		    hdev->hw.mac.speed == HCLGE_MAC_SPEED_1G) {
			count += 1;
			handle->flags |= HNAE3_SUPPORT_MAC_LOOPBACK;
		}

		count++;
		handle->flags |= HNAE3_SUPPORT_SERDES_LOOPBACK;
	} else if (stringset == ETH_SS_STATS) {
		count = ARRAY_SIZE(g_mac_stats_string) +
			ARRAY_SIZE(g_all_32bit_stats_string) +
			ARRAY_SIZE(g_all_64bit_stats_string) +
			hclge_tqps_get_sset_count(handle, stringset);
	}

	return count;
}

static void hclge_get_strings(struct hnae3_handle *handle,
			      u32 stringset,
			      u8 *data)
{
	u8 *p = (char *)data;
	int size;

	if (stringset == ETH_SS_STATS) {
		size = ARRAY_SIZE(g_mac_stats_string);
		p = hclge_comm_get_strings(stringset,
					   g_mac_stats_string,
					   size,
					   p);
		size = ARRAY_SIZE(g_all_32bit_stats_string);
		p = hclge_comm_get_strings(stringset,
					   g_all_32bit_stats_string,
					   size,
					   p);
		size = ARRAY_SIZE(g_all_64bit_stats_string);
		p = hclge_comm_get_strings(stringset,
					   g_all_64bit_stats_string,
					   size,
					   p);
		p = hclge_tqps_get_strings(handle, p);
	} else if (stringset == ETH_SS_TEST) {
		if (handle->flags & HNAE3_SUPPORT_MAC_LOOPBACK) {
			memcpy(p,
			       hns3_nic_test_strs[HNAE3_MAC_INTER_LOOP_MAC],
			       ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
		if (handle->flags & HNAE3_SUPPORT_SERDES_LOOPBACK) {
			memcpy(p,
			       hns3_nic_test_strs[HNAE3_MAC_INTER_LOOP_SERDES],
			       ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
		if (handle->flags & HNAE3_SUPPORT_PHY_LOOPBACK) {
			memcpy(p,
			       hns3_nic_test_strs[HNAE3_MAC_INTER_LOOP_PHY],
			       ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
	}
}

static void hclge_get_stats(struct hnae3_handle *handle, u64 *data)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;
	u64 *p;

	p = hclge_comm_get_stats(&hdev->hw_stats.mac_stats,
				 g_mac_stats_string,
				 ARRAY_SIZE(g_mac_stats_string),
				 data);
	p = hclge_comm_get_stats(&hdev->hw_stats.all_32_bit_stats,
				 g_all_32bit_stats_string,
				 ARRAY_SIZE(g_all_32bit_stats_string),
				 p);
	p = hclge_comm_get_stats(&hdev->hw_stats.all_64_bit_stats,
				 g_all_64bit_stats_string,
				 ARRAY_SIZE(g_all_64bit_stats_string),
				 p);
	p = hclge_tqps_get_stats(handle, p);
}

static int hclge_parse_func_status(struct hclge_dev *hdev,
				   struct hclge_func_status_cmd *status)
{
	if (!(status->pf_state & HCLGE_PF_STATE_DONE))
		return -EINVAL;

	/* Set the pf to main pf */
	if (status->pf_state & HCLGE_PF_STATE_MAIN)
		hdev->flag |= HCLGE_FLAG_MAIN;
	else
		hdev->flag &= ~HCLGE_FLAG_MAIN;

	return 0;
}

static int hclge_query_function_status(struct hclge_dev *hdev)
{
	struct hclge_func_status_cmd *req;
	struct hclge_desc desc;
	int timeout = 0;
	int ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_QUERY_FUNC_STATUS, true);
	req = (struct hclge_func_status_cmd *)desc.data;

	do {
		ret = hclge_cmd_send(&hdev->hw, &desc, 1);
		if (ret) {
			dev_err(&hdev->pdev->dev,
				"query function status failed %d.\n",
				ret);

			return ret;
		}

		/* Check pf reset is done */
		if (req->pf_state)
			break;
		usleep_range(1000, 2000);
	} while (timeout++ < 5);

	ret = hclge_parse_func_status(hdev, req);

	return ret;
}

static int hclge_query_pf_resource(struct hclge_dev *hdev)
{
	struct hclge_pf_res_cmd *req;
	struct hclge_desc desc;
	int ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_QUERY_PF_RSRC, true);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"query pf resource failed %d.\n", ret);
		return ret;
	}

	req = (struct hclge_pf_res_cmd *)desc.data;
	hdev->num_tqps = __le16_to_cpu(req->tqp_num);
	hdev->pkt_buf_size = __le16_to_cpu(req->buf_size) << HCLGE_BUF_UNIT_S;

	if (hnae3_dev_roce_supported(hdev)) {
		hdev->roce_base_msix_offset =
		hnae3_get_field(__le16_to_cpu(req->msixcap_localid_ba_rocee),
				HCLGE_MSIX_OFT_ROCEE_M, HCLGE_MSIX_OFT_ROCEE_S);
		hdev->num_roce_msi =
		hnae3_get_field(__le16_to_cpu(req->pf_intr_vector_number),
				HCLGE_PF_VEC_NUM_M, HCLGE_PF_VEC_NUM_S);

		/* PF should have NIC vectors and Roce vectors,
		 * NIC vectors are queued before Roce vectors.
		 */
		hdev->num_msi = hdev->num_roce_msi  +
				hdev->roce_base_msix_offset;
	} else {
		hdev->num_msi =
		hnae3_get_field(__le16_to_cpu(req->pf_intr_vector_number),
				HCLGE_PF_VEC_NUM_M, HCLGE_PF_VEC_NUM_S);
	}

	return 0;
}

static int hclge_parse_speed(int speed_cmd, int *speed)
{
	switch (speed_cmd) {
	case 6:
		*speed = HCLGE_MAC_SPEED_10M;
		break;
	case 7:
		*speed = HCLGE_MAC_SPEED_100M;
		break;
	case 0:
		*speed = HCLGE_MAC_SPEED_1G;
		break;
	case 1:
		*speed = HCLGE_MAC_SPEED_10G;
		break;
	case 2:
		*speed = HCLGE_MAC_SPEED_25G;
		break;
	case 3:
		*speed = HCLGE_MAC_SPEED_40G;
		break;
	case 4:
		*speed = HCLGE_MAC_SPEED_50G;
		break;
	case 5:
		*speed = HCLGE_MAC_SPEED_100G;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void hclge_parse_fiber_link_mode(struct hclge_dev *hdev,
					u8 speed_ability)
{
	unsigned long *supported = hdev->hw.mac.supported;

	if (speed_ability & HCLGE_SUPPORT_1G_BIT)
		set_bit(ETHTOOL_LINK_MODE_1000baseX_Full_BIT,
			supported);

	if (speed_ability & HCLGE_SUPPORT_10G_BIT)
		set_bit(ETHTOOL_LINK_MODE_10000baseSR_Full_BIT,
			supported);

	if (speed_ability & HCLGE_SUPPORT_25G_BIT)
		set_bit(ETHTOOL_LINK_MODE_25000baseSR_Full_BIT,
			supported);

	if (speed_ability & HCLGE_SUPPORT_50G_BIT)
		set_bit(ETHTOOL_LINK_MODE_50000baseSR2_Full_BIT,
			supported);

	if (speed_ability & HCLGE_SUPPORT_100G_BIT)
		set_bit(ETHTOOL_LINK_MODE_100000baseSR4_Full_BIT,
			supported);

	set_bit(ETHTOOL_LINK_MODE_FIBRE_BIT, supported);
	set_bit(ETHTOOL_LINK_MODE_Pause_BIT, supported);
}

static void hclge_parse_link_mode(struct hclge_dev *hdev, u8 speed_ability)
{
	u8 media_type = hdev->hw.mac.media_type;

	if (media_type != HNAE3_MEDIA_TYPE_FIBER)
		return;

	hclge_parse_fiber_link_mode(hdev, speed_ability);
}

static void hclge_parse_cfg(struct hclge_cfg *cfg, struct hclge_desc *desc)
{
	struct hclge_cfg_param_cmd *req;
	u64 mac_addr_tmp_high;
	u64 mac_addr_tmp;
	int i;

	req = (struct hclge_cfg_param_cmd *)desc[0].data;

	/* get the configuration */
	cfg->vmdq_vport_num = hnae3_get_field(__le32_to_cpu(req->param[0]),
					      HCLGE_CFG_VMDQ_M,
					      HCLGE_CFG_VMDQ_S);
	cfg->tc_num = hnae3_get_field(__le32_to_cpu(req->param[0]),
				      HCLGE_CFG_TC_NUM_M, HCLGE_CFG_TC_NUM_S);
	cfg->tqp_desc_num = hnae3_get_field(__le32_to_cpu(req->param[0]),
					    HCLGE_CFG_TQP_DESC_N_M,
					    HCLGE_CFG_TQP_DESC_N_S);

	cfg->phy_addr = hnae3_get_field(__le32_to_cpu(req->param[1]),
					HCLGE_CFG_PHY_ADDR_M,
					HCLGE_CFG_PHY_ADDR_S);
	cfg->media_type = hnae3_get_field(__le32_to_cpu(req->param[1]),
					  HCLGE_CFG_MEDIA_TP_M,
					  HCLGE_CFG_MEDIA_TP_S);
	cfg->rx_buf_len = hnae3_get_field(__le32_to_cpu(req->param[1]),
					  HCLGE_CFG_RX_BUF_LEN_M,
					  HCLGE_CFG_RX_BUF_LEN_S);
	/* get mac_address */
	mac_addr_tmp = __le32_to_cpu(req->param[2]);
	mac_addr_tmp_high = hnae3_get_field(__le32_to_cpu(req->param[3]),
					    HCLGE_CFG_MAC_ADDR_H_M,
					    HCLGE_CFG_MAC_ADDR_H_S);

	mac_addr_tmp |= (mac_addr_tmp_high << 31) << 1;

	cfg->default_speed = hnae3_get_field(__le32_to_cpu(req->param[3]),
					     HCLGE_CFG_DEFAULT_SPEED_M,
					     HCLGE_CFG_DEFAULT_SPEED_S);
	cfg->rss_size_max = hnae3_get_field(__le32_to_cpu(req->param[3]),
					    HCLGE_CFG_RSS_SIZE_M,
					    HCLGE_CFG_RSS_SIZE_S);

	for (i = 0; i < ETH_ALEN; i++)
		cfg->mac_addr[i] = (mac_addr_tmp >> (8 * i)) & 0xff;

	req = (struct hclge_cfg_param_cmd *)desc[1].data;
	cfg->numa_node_map = __le32_to_cpu(req->param[0]);

	cfg->speed_ability = hnae3_get_field(__le32_to_cpu(req->param[1]),
					     HCLGE_CFG_SPEED_ABILITY_M,
					     HCLGE_CFG_SPEED_ABILITY_S);
}

/* hclge_get_cfg: query the static parameter from flash
 * @hdev: pointer to struct hclge_dev
 * @hcfg: the config structure to be getted
 */
static int hclge_get_cfg(struct hclge_dev *hdev, struct hclge_cfg *hcfg)
{
	struct hclge_desc desc[HCLGE_PF_CFG_DESC_NUM];
	struct hclge_cfg_param_cmd *req;
	int i, ret;

	for (i = 0; i < HCLGE_PF_CFG_DESC_NUM; i++) {
		u32 offset = 0;

		req = (struct hclge_cfg_param_cmd *)desc[i].data;
		hclge_cmd_setup_basic_desc(&desc[i], HCLGE_OPC_GET_CFG_PARAM,
					   true);
		hnae3_set_field(offset, HCLGE_CFG_OFFSET_M,
				HCLGE_CFG_OFFSET_S, i * HCLGE_CFG_RD_LEN_BYTES);
		/* Len should be united by 4 bytes when send to hardware */
		hnae3_set_field(offset, HCLGE_CFG_RD_LEN_M, HCLGE_CFG_RD_LEN_S,
				HCLGE_CFG_RD_LEN_BYTES / HCLGE_CFG_RD_LEN_UNIT);
		req->offset = cpu_to_le32(offset);
	}

	ret = hclge_cmd_send(&hdev->hw, desc, HCLGE_PF_CFG_DESC_NUM);
	if (ret) {
		dev_err(&hdev->pdev->dev, "get config failed %d.\n", ret);
		return ret;
	}

	hclge_parse_cfg(hcfg, desc);

	return 0;
}

static int hclge_get_cap(struct hclge_dev *hdev)
{
	int ret;

	ret = hclge_query_function_status(hdev);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"query function status error %d.\n", ret);
		return ret;
	}

	/* get pf resource */
	ret = hclge_query_pf_resource(hdev);
	if (ret)
		dev_err(&hdev->pdev->dev, "query pf resource error %d.\n", ret);

	return ret;
}

static int hclge_configure(struct hclge_dev *hdev)
{
	struct hclge_cfg cfg;
	int ret, i;

	ret = hclge_get_cfg(hdev, &cfg);
	if (ret) {
		dev_err(&hdev->pdev->dev, "get mac mode error %d.\n", ret);
		return ret;
	}

	hdev->num_vmdq_vport = cfg.vmdq_vport_num;
	hdev->base_tqp_pid = 0;
	hdev->rss_size_max = cfg.rss_size_max;
	hdev->rx_buf_len = cfg.rx_buf_len;
	ether_addr_copy(hdev->hw.mac.mac_addr, cfg.mac_addr);
	hdev->hw.mac.media_type = cfg.media_type;
	hdev->hw.mac.phy_addr = cfg.phy_addr;
	hdev->num_desc = cfg.tqp_desc_num;
	hdev->tm_info.num_pg = 1;
	hdev->tc_max = cfg.tc_num;
	hdev->tm_info.hw_pfc_map = 0;

	ret = hclge_parse_speed(cfg.default_speed, &hdev->hw.mac.speed);
	if (ret) {
		dev_err(&hdev->pdev->dev, "Get wrong speed ret=%d.\n", ret);
		return ret;
	}

	hclge_parse_link_mode(hdev, cfg.speed_ability);

	if ((hdev->tc_max > HNAE3_MAX_TC) ||
	    (hdev->tc_max < 1)) {
		dev_warn(&hdev->pdev->dev, "TC num = %d.\n",
			 hdev->tc_max);
		hdev->tc_max = 1;
	}

	/* Dev does not support DCB */
	if (!hnae3_dev_dcb_supported(hdev)) {
		hdev->tc_max = 1;
		hdev->pfc_max = 0;
	} else {
		hdev->pfc_max = hdev->tc_max;
	}

	hdev->tm_info.num_tc = hdev->tc_max;

	/* Currently not support uncontiuous tc */
	for (i = 0; i < hdev->tm_info.num_tc; i++)
		hnae3_set_bit(hdev->hw_tc_map, i, 1);

	hdev->tx_sch_mode = HCLGE_FLAG_TC_BASE_SCH_MODE;

	return ret;
}

static int hclge_config_tso(struct hclge_dev *hdev, int tso_mss_min,
			    int tso_mss_max)
{
	struct hclge_cfg_tso_status_cmd *req;
	struct hclge_desc desc;
	u16 tso_mss;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_TSO_GENERIC_CONFIG, false);

	req = (struct hclge_cfg_tso_status_cmd *)desc.data;

	tso_mss = 0;
	hnae3_set_field(tso_mss, HCLGE_TSO_MSS_MIN_M,
			HCLGE_TSO_MSS_MIN_S, tso_mss_min);
	req->tso_mss_min = cpu_to_le16(tso_mss);

	tso_mss = 0;
	hnae3_set_field(tso_mss, HCLGE_TSO_MSS_MIN_M,
			HCLGE_TSO_MSS_MIN_S, tso_mss_max);
	req->tso_mss_max = cpu_to_le16(tso_mss);

	return hclge_cmd_send(&hdev->hw, &desc, 1);
}

static int hclge_alloc_tqps(struct hclge_dev *hdev)
{
	struct hclge_tqp *tqp;
	int i;

	hdev->htqp = devm_kcalloc(&hdev->pdev->dev, hdev->num_tqps,
				  sizeof(struct hclge_tqp), GFP_KERNEL);
	if (!hdev->htqp)
		return -ENOMEM;

	tqp = hdev->htqp;

	for (i = 0; i < hdev->num_tqps; i++) {
		tqp->dev = &hdev->pdev->dev;
		tqp->index = i;

		tqp->q.ae_algo = &ae_algo;
		tqp->q.buf_size = hdev->rx_buf_len;
		tqp->q.desc_num = hdev->num_desc;
		tqp->q.io_base = hdev->hw.io_base + HCLGE_TQP_REG_OFFSET +
			i * HCLGE_TQP_REG_SIZE;

		tqp++;
	}

	return 0;
}

static int hclge_map_tqps_to_func(struct hclge_dev *hdev, u16 func_id,
				  u16 tqp_pid, u16 tqp_vid, bool is_pf)
{
	struct hclge_tqp_map_cmd *req;
	struct hclge_desc desc;
	int ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_SET_TQP_MAP, false);

	req = (struct hclge_tqp_map_cmd *)desc.data;
	req->tqp_id = cpu_to_le16(tqp_pid);
	req->tqp_vf = func_id;
	req->tqp_flag = !is_pf << HCLGE_TQP_MAP_TYPE_B |
			1 << HCLGE_TQP_MAP_EN_B;
	req->tqp_vid = cpu_to_le16(tqp_vid);

	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		dev_err(&hdev->pdev->dev, "TQP map failed %d.\n", ret);

	return ret;
}

static int  hclge_assign_tqp(struct hclge_vport *vport)
{
	struct hnae3_knic_private_info *kinfo = &vport->nic.kinfo;
	struct hclge_dev *hdev = vport->back;
	int i, alloced;

	for (i = 0, alloced = 0; i < hdev->num_tqps &&
	     alloced < kinfo->num_tqps; i++) {
		if (!hdev->htqp[i].alloced) {
			hdev->htqp[i].q.handle = &vport->nic;
			hdev->htqp[i].q.tqp_index = alloced;
			hdev->htqp[i].q.desc_num = kinfo->num_desc;
			kinfo->tqp[alloced] = &hdev->htqp[i].q;
			hdev->htqp[i].alloced = true;
			alloced++;
		}
	}
	vport->alloc_tqps = kinfo->num_tqps;

	return 0;
}

static int hclge_knic_setup(struct hclge_vport *vport,
			    u16 num_tqps, u16 num_desc)
{
	struct hnae3_handle *nic = &vport->nic;
	struct hnae3_knic_private_info *kinfo = &nic->kinfo;
	struct hclge_dev *hdev = vport->back;
	int i, ret;

	kinfo->num_desc = num_desc;
	kinfo->rx_buf_len = hdev->rx_buf_len;
	kinfo->num_tc = min_t(u16, num_tqps, hdev->tm_info.num_tc);
	kinfo->rss_size
		= min_t(u16, hdev->rss_size_max, num_tqps / kinfo->num_tc);
	kinfo->num_tqps = kinfo->rss_size * kinfo->num_tc;

	for (i = 0; i < HNAE3_MAX_TC; i++) {
		if (hdev->hw_tc_map & BIT(i)) {
			kinfo->tc_info[i].enable = true;
			kinfo->tc_info[i].tqp_offset = i * kinfo->rss_size;
			kinfo->tc_info[i].tqp_count = kinfo->rss_size;
			kinfo->tc_info[i].tc = i;
		} else {
			/* Set to default queue if TC is disable */
			kinfo->tc_info[i].enable = false;
			kinfo->tc_info[i].tqp_offset = 0;
			kinfo->tc_info[i].tqp_count = 1;
			kinfo->tc_info[i].tc = 0;
		}
	}

	kinfo->tqp = devm_kcalloc(&hdev->pdev->dev, kinfo->num_tqps,
				  sizeof(struct hnae3_queue *), GFP_KERNEL);
	if (!kinfo->tqp)
		return -ENOMEM;

	ret = hclge_assign_tqp(vport);
	if (ret)
		dev_err(&hdev->pdev->dev, "fail to assign TQPs %d.\n", ret);

	return ret;
}

static int hclge_map_tqp_to_vport(struct hclge_dev *hdev,
				  struct hclge_vport *vport)
{
	struct hnae3_handle *nic = &vport->nic;
	struct hnae3_knic_private_info *kinfo;
	u16 i;

	kinfo = &nic->kinfo;
	for (i = 0; i < kinfo->num_tqps; i++) {
		struct hclge_tqp *q =
			container_of(kinfo->tqp[i], struct hclge_tqp, q);
		bool is_pf;
		int ret;

		is_pf = !(vport->vport_id);
		ret = hclge_map_tqps_to_func(hdev, vport->vport_id, q->index,
					     i, is_pf);
		if (ret)
			return ret;
	}

	return 0;
}

static int hclge_map_tqp(struct hclge_dev *hdev)
{
	struct hclge_vport *vport = hdev->vport;
	u16 i, num_vport;

	num_vport = hdev->num_vmdq_vport + hdev->num_req_vfs + 1;
	for (i = 0; i < num_vport; i++)	{
		int ret;

		ret = hclge_map_tqp_to_vport(hdev, vport);
		if (ret)
			return ret;

		vport++;
	}

	return 0;
}

static void hclge_unic_setup(struct hclge_vport *vport, u16 num_tqps)
{
	/* this would be initialized later */
}

static int hclge_vport_setup(struct hclge_vport *vport, u16 num_tqps)
{
	struct hnae3_handle *nic = &vport->nic;
	struct hclge_dev *hdev = vport->back;
	int ret;

	nic->pdev = hdev->pdev;
	nic->ae_algo = &ae_algo;
	nic->numa_node_mask = hdev->numa_node_mask;

	if (hdev->ae_dev->dev_type == HNAE3_DEV_KNIC) {
		ret = hclge_knic_setup(vport, num_tqps, hdev->num_desc);
		if (ret) {
			dev_err(&hdev->pdev->dev, "knic setup failed %d\n",
				ret);
			return ret;
		}
	} else {
		hclge_unic_setup(vport, num_tqps);
	}

	return 0;
}

static int hclge_alloc_vport(struct hclge_dev *hdev)
{
	struct pci_dev *pdev = hdev->pdev;
	struct hclge_vport *vport;
	u32 tqp_main_vport;
	u32 tqp_per_vport;
	int num_vport, i;
	int ret;

	/* We need to alloc a vport for main NIC of PF */
	num_vport = hdev->num_vmdq_vport + hdev->num_req_vfs + 1;

	if (hdev->num_tqps < num_vport) {
		dev_err(&hdev->pdev->dev, "tqps(%d) is less than vports(%d)",
			hdev->num_tqps, num_vport);
		return -EINVAL;
	}

	/* Alloc the same number of TQPs for every vport */
	tqp_per_vport = hdev->num_tqps / num_vport;
	tqp_main_vport = tqp_per_vport + hdev->num_tqps % num_vport;

	vport = devm_kcalloc(&pdev->dev, num_vport, sizeof(struct hclge_vport),
			     GFP_KERNEL);
	if (!vport)
		return -ENOMEM;

	hdev->vport = vport;
	hdev->num_alloc_vport = num_vport;

	if (IS_ENABLED(CONFIG_PCI_IOV))
		hdev->num_alloc_vfs = hdev->num_req_vfs;

	for (i = 0; i < num_vport; i++) {
		vport->back = hdev;
		vport->vport_id = i;

		if (i == 0)
			ret = hclge_vport_setup(vport, tqp_main_vport);
		else
			ret = hclge_vport_setup(vport, tqp_per_vport);
		if (ret) {
			dev_err(&pdev->dev,
				"vport setup failed for vport %d, %d\n",
				i, ret);
			return ret;
		}

		vport++;
	}

	return 0;
}

static int  hclge_cmd_alloc_tx_buff(struct hclge_dev *hdev,
				    struct hclge_pkt_buf_alloc *buf_alloc)
{
/* TX buffer size is unit by 128 byte */
#define HCLGE_BUF_SIZE_UNIT_SHIFT	7
#define HCLGE_BUF_SIZE_UPDATE_EN_MSK	BIT(15)
	struct hclge_tx_buff_alloc_cmd *req;
	struct hclge_desc desc;
	int ret;
	u8 i;

	req = (struct hclge_tx_buff_alloc_cmd *)desc.data;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_TX_BUFF_ALLOC, 0);
	for (i = 0; i < HCLGE_TC_NUM; i++) {
		u32 buf_size = buf_alloc->priv_buf[i].tx_buf_size;

		req->tx_pkt_buff[i] =
			cpu_to_le16((buf_size >> HCLGE_BUF_SIZE_UNIT_SHIFT) |
				     HCLGE_BUF_SIZE_UPDATE_EN_MSK);
	}

	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		dev_err(&hdev->pdev->dev, "tx buffer alloc cmd failed %d.\n",
			ret);

	return ret;
}

static int hclge_tx_buffer_alloc(struct hclge_dev *hdev,
				 struct hclge_pkt_buf_alloc *buf_alloc)
{
	int ret = hclge_cmd_alloc_tx_buff(hdev, buf_alloc);

	if (ret)
		dev_err(&hdev->pdev->dev, "tx buffer alloc failed %d\n", ret);

	return ret;
}

static int hclge_get_tc_num(struct hclge_dev *hdev)
{
	int i, cnt = 0;

	for (i = 0; i < HCLGE_MAX_TC_NUM; i++)
		if (hdev->hw_tc_map & BIT(i))
			cnt++;
	return cnt;
}

static int hclge_get_pfc_enalbe_num(struct hclge_dev *hdev)
{
	int i, cnt = 0;

	for (i = 0; i < HCLGE_MAX_TC_NUM; i++)
		if (hdev->hw_tc_map & BIT(i) &&
		    hdev->tm_info.hw_pfc_map & BIT(i))
			cnt++;
	return cnt;
}

/* Get the number of pfc enabled TCs, which have private buffer */
static int hclge_get_pfc_priv_num(struct hclge_dev *hdev,
				  struct hclge_pkt_buf_alloc *buf_alloc)
{
	struct hclge_priv_buf *priv;
	int i, cnt = 0;

	for (i = 0; i < HCLGE_MAX_TC_NUM; i++) {
		priv = &buf_alloc->priv_buf[i];
		if ((hdev->tm_info.hw_pfc_map & BIT(i)) &&
		    priv->enable)
			cnt++;
	}

	return cnt;
}

/* Get the number of pfc disabled TCs, which have private buffer */
static int hclge_get_no_pfc_priv_num(struct hclge_dev *hdev,
				     struct hclge_pkt_buf_alloc *buf_alloc)
{
	struct hclge_priv_buf *priv;
	int i, cnt = 0;

	for (i = 0; i < HCLGE_MAX_TC_NUM; i++) {
		priv = &buf_alloc->priv_buf[i];
		if (hdev->hw_tc_map & BIT(i) &&
		    !(hdev->tm_info.hw_pfc_map & BIT(i)) &&
		    priv->enable)
			cnt++;
	}

	return cnt;
}

static u32 hclge_get_rx_priv_buff_alloced(struct hclge_pkt_buf_alloc *buf_alloc)
{
	struct hclge_priv_buf *priv;
	u32 rx_priv = 0;
	int i;

	for (i = 0; i < HCLGE_MAX_TC_NUM; i++) {
		priv = &buf_alloc->priv_buf[i];
		if (priv->enable)
			rx_priv += priv->buf_size;
	}
	return rx_priv;
}

static u32 hclge_get_tx_buff_alloced(struct hclge_pkt_buf_alloc *buf_alloc)
{
	u32 i, total_tx_size = 0;

	for (i = 0; i < HCLGE_MAX_TC_NUM; i++)
		total_tx_size += buf_alloc->priv_buf[i].tx_buf_size;

	return total_tx_size;
}

static bool  hclge_is_rx_buf_ok(struct hclge_dev *hdev,
				struct hclge_pkt_buf_alloc *buf_alloc,
				u32 rx_all)
{
	u32 shared_buf_min, shared_buf_tc, shared_std;
	int tc_num, pfc_enable_num;
	u32 shared_buf;
	u32 rx_priv;
	int i;

	tc_num = hclge_get_tc_num(hdev);
	pfc_enable_num = hclge_get_pfc_enalbe_num(hdev);

	if (hnae3_dev_dcb_supported(hdev))
		shared_buf_min = 2 * hdev->mps + HCLGE_DEFAULT_DV;
	else
		shared_buf_min = 2 * hdev->mps + HCLGE_DEFAULT_NON_DCB_DV;

	shared_buf_tc = pfc_enable_num * hdev->mps +
			(tc_num - pfc_enable_num) * hdev->mps / 2 +
			hdev->mps;
	shared_std = max_t(u32, shared_buf_min, shared_buf_tc);

	rx_priv = hclge_get_rx_priv_buff_alloced(buf_alloc);
	if (rx_all <= rx_priv + shared_std)
		return false;

	shared_buf = rx_all - rx_priv;
	buf_alloc->s_buf.buf_size = shared_buf;
	buf_alloc->s_buf.self.high = shared_buf;
	buf_alloc->s_buf.self.low =  2 * hdev->mps;

	for (i = 0; i < HCLGE_MAX_TC_NUM; i++) {
		if ((hdev->hw_tc_map & BIT(i)) &&
		    (hdev->tm_info.hw_pfc_map & BIT(i))) {
			buf_alloc->s_buf.tc_thrd[i].low = hdev->mps;
			buf_alloc->s_buf.tc_thrd[i].high = 2 * hdev->mps;
		} else {
			buf_alloc->s_buf.tc_thrd[i].low = 0;
			buf_alloc->s_buf.tc_thrd[i].high = hdev->mps;
		}
	}

	return true;
}

static int hclge_tx_buffer_calc(struct hclge_dev *hdev,
				struct hclge_pkt_buf_alloc *buf_alloc)
{
	u32 i, total_size;

	total_size = hdev->pkt_buf_size;

	/* alloc tx buffer for all enabled tc */
	for (i = 0; i < HCLGE_MAX_TC_NUM; i++) {
		struct hclge_priv_buf *priv = &buf_alloc->priv_buf[i];

		if (total_size < HCLGE_DEFAULT_TX_BUF)
			return -ENOMEM;

		if (hdev->hw_tc_map & BIT(i))
			priv->tx_buf_size = HCLGE_DEFAULT_TX_BUF;
		else
			priv->tx_buf_size = 0;

		total_size -= priv->tx_buf_size;
	}

	return 0;
}

/* hclge_rx_buffer_calc: calculate the rx private buffer size for all TCs
 * @hdev: pointer to struct hclge_dev
 * @buf_alloc: pointer to buffer calculation data
 * @return: 0: calculate sucessful, negative: fail
 */
static int hclge_rx_buffer_calc(struct hclge_dev *hdev,
				struct hclge_pkt_buf_alloc *buf_alloc)
{
#define HCLGE_BUF_SIZE_UNIT	128
	u32 rx_all = hdev->pkt_buf_size, aligned_mps;
	int no_pfc_priv_num, pfc_priv_num;
	struct hclge_priv_buf *priv;
	int i;

	aligned_mps = round_up(hdev->mps, HCLGE_BUF_SIZE_UNIT);
	rx_all -= hclge_get_tx_buff_alloced(buf_alloc);

	/* When DCB is not supported, rx private
	 * buffer is not allocated.
	 */
	if (!hnae3_dev_dcb_supported(hdev)) {
		if (!hclge_is_rx_buf_ok(hdev, buf_alloc, rx_all))
			return -ENOMEM;

		return 0;
	}

	/* step 1, try to alloc private buffer for all enabled tc */
	for (i = 0; i < HCLGE_MAX_TC_NUM; i++) {
		priv = &buf_alloc->priv_buf[i];
		if (hdev->hw_tc_map & BIT(i)) {
			priv->enable = 1;
			if (hdev->tm_info.hw_pfc_map & BIT(i)) {
				priv->wl.low = aligned_mps;
				priv->wl.high = priv->wl.low + aligned_mps;
				priv->buf_size = priv->wl.high +
						HCLGE_DEFAULT_DV;
			} else {
				priv->wl.low = 0;
				priv->wl.high = 2 * aligned_mps;
				priv->buf_size = priv->wl.high;
			}
		} else {
			priv->enable = 0;
			priv->wl.low = 0;
			priv->wl.high = 0;
			priv->buf_size = 0;
		}
	}

	if (hclge_is_rx_buf_ok(hdev, buf_alloc, rx_all))
		return 0;

	/* step 2, try to decrease the buffer size of
	 * no pfc TC's private buffer
	 */
	for (i = 0; i < HCLGE_MAX_TC_NUM; i++) {
		priv = &buf_alloc->priv_buf[i];

		priv->enable = 0;
		priv->wl.low = 0;
		priv->wl.high = 0;
		priv->buf_size = 0;

		if (!(hdev->hw_tc_map & BIT(i)))
			continue;

		priv->enable = 1;

		if (hdev->tm_info.hw_pfc_map & BIT(i)) {
			priv->wl.low = 128;
			priv->wl.high = priv->wl.low + aligned_mps;
			priv->buf_size = priv->wl.high + HCLGE_DEFAULT_DV;
		} else {
			priv->wl.low = 0;
			priv->wl.high = aligned_mps;
			priv->buf_size = priv->wl.high;
		}
	}

	if (hclge_is_rx_buf_ok(hdev, buf_alloc, rx_all))
		return 0;

	/* step 3, try to reduce the number of pfc disabled TCs,
	 * which have private buffer
	 */
	/* get the total no pfc enable TC number, which have private buffer */
	no_pfc_priv_num = hclge_get_no_pfc_priv_num(hdev, buf_alloc);

	/* let the last to be cleared first */
	for (i = HCLGE_MAX_TC_NUM - 1; i >= 0; i--) {
		priv = &buf_alloc->priv_buf[i];

		if (hdev->hw_tc_map & BIT(i) &&
		    !(hdev->tm_info.hw_pfc_map & BIT(i))) {
			/* Clear the no pfc TC private buffer */
			priv->wl.low = 0;
			priv->wl.high = 0;
			priv->buf_size = 0;
			priv->enable = 0;
			no_pfc_priv_num--;
		}

		if (hclge_is_rx_buf_ok(hdev, buf_alloc, rx_all) ||
		    no_pfc_priv_num == 0)
			break;
	}

	if (hclge_is_rx_buf_ok(hdev, buf_alloc, rx_all))
		return 0;

	/* step 4, try to reduce the number of pfc enabled TCs
	 * which have private buffer.
	 */
	pfc_priv_num = hclge_get_pfc_priv_num(hdev, buf_alloc);

	/* let the last to be cleared first */
	for (i = HCLGE_MAX_TC_NUM - 1; i >= 0; i--) {
		priv = &buf_alloc->priv_buf[i];

		if (hdev->hw_tc_map & BIT(i) &&
		    hdev->tm_info.hw_pfc_map & BIT(i)) {
			/* Reduce the number of pfc TC with private buffer */
			priv->wl.low = 0;
			priv->enable = 0;
			priv->wl.high = 0;
			priv->buf_size = 0;
			pfc_priv_num--;
		}

		if (hclge_is_rx_buf_ok(hdev, buf_alloc, rx_all) ||
		    pfc_priv_num == 0)
			break;
	}
	if (hclge_is_rx_buf_ok(hdev, buf_alloc, rx_all))
		return 0;

	return -ENOMEM;
}

static int hclge_rx_priv_buf_alloc(struct hclge_dev *hdev,
				   struct hclge_pkt_buf_alloc *buf_alloc)
{
	struct hclge_rx_priv_buff_cmd *req;
	struct hclge_desc desc;
	int ret;
	int i;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_RX_PRIV_BUFF_ALLOC, false);
	req = (struct hclge_rx_priv_buff_cmd *)desc.data;

	/* Alloc private buffer TCs */
	for (i = 0; i < HCLGE_MAX_TC_NUM; i++) {
		struct hclge_priv_buf *priv = &buf_alloc->priv_buf[i];

		req->buf_num[i] =
			cpu_to_le16(priv->buf_size >> HCLGE_BUF_UNIT_S);
		req->buf_num[i] |=
			cpu_to_le16(1 << HCLGE_TC0_PRI_BUF_EN_B);
	}

	req->shared_buf =
		cpu_to_le16((buf_alloc->s_buf.buf_size >> HCLGE_BUF_UNIT_S) |
			    (1 << HCLGE_TC0_PRI_BUF_EN_B));

	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		dev_err(&hdev->pdev->dev,
			"rx private buffer alloc cmd failed %d\n", ret);

	return ret;
}

static int hclge_rx_priv_wl_config(struct hclge_dev *hdev,
				   struct hclge_pkt_buf_alloc *buf_alloc)
{
	struct hclge_rx_priv_wl_buf *req;
	struct hclge_priv_buf *priv;
	struct hclge_desc desc[2];
	int i, j;
	int ret;

	for (i = 0; i < 2; i++) {
		hclge_cmd_setup_basic_desc(&desc[i], HCLGE_OPC_RX_PRIV_WL_ALLOC,
					   false);
		req = (struct hclge_rx_priv_wl_buf *)desc[i].data;

		/* The first descriptor set the NEXT bit to 1 */
		if (i == 0)
			desc[i].flag |= cpu_to_le16(HCLGE_CMD_FLAG_NEXT);
		else
			desc[i].flag &= ~cpu_to_le16(HCLGE_CMD_FLAG_NEXT);

		for (j = 0; j < HCLGE_TC_NUM_ONE_DESC; j++) {
			u32 idx = i * HCLGE_TC_NUM_ONE_DESC + j;

			priv = &buf_alloc->priv_buf[idx];
			req->tc_wl[j].high =
				cpu_to_le16(priv->wl.high >> HCLGE_BUF_UNIT_S);
			req->tc_wl[j].high |=
				cpu_to_le16(BIT(HCLGE_RX_PRIV_EN_B));
			req->tc_wl[j].low =
				cpu_to_le16(priv->wl.low >> HCLGE_BUF_UNIT_S);
			req->tc_wl[j].low |=
				 cpu_to_le16(BIT(HCLGE_RX_PRIV_EN_B));
		}
	}

	/* Send 2 descriptor at one time */
	ret = hclge_cmd_send(&hdev->hw, desc, 2);
	if (ret)
		dev_err(&hdev->pdev->dev,
			"rx private waterline config cmd failed %d\n",
			ret);
	return ret;
}

static int hclge_common_thrd_config(struct hclge_dev *hdev,
				    struct hclge_pkt_buf_alloc *buf_alloc)
{
	struct hclge_shared_buf *s_buf = &buf_alloc->s_buf;
	struct hclge_rx_com_thrd *req;
	struct hclge_desc desc[2];
	struct hclge_tc_thrd *tc;
	int i, j;
	int ret;

	for (i = 0; i < 2; i++) {
		hclge_cmd_setup_basic_desc(&desc[i],
					   HCLGE_OPC_RX_COM_THRD_ALLOC, false);
		req = (struct hclge_rx_com_thrd *)&desc[i].data;

		/* The first descriptor set the NEXT bit to 1 */
		if (i == 0)
			desc[i].flag |= cpu_to_le16(HCLGE_CMD_FLAG_NEXT);
		else
			desc[i].flag &= ~cpu_to_le16(HCLGE_CMD_FLAG_NEXT);

		for (j = 0; j < HCLGE_TC_NUM_ONE_DESC; j++) {
			tc = &s_buf->tc_thrd[i * HCLGE_TC_NUM_ONE_DESC + j];

			req->com_thrd[j].high =
				cpu_to_le16(tc->high >> HCLGE_BUF_UNIT_S);
			req->com_thrd[j].high |=
				 cpu_to_le16(BIT(HCLGE_RX_PRIV_EN_B));
			req->com_thrd[j].low =
				cpu_to_le16(tc->low >> HCLGE_BUF_UNIT_S);
			req->com_thrd[j].low |=
				 cpu_to_le16(BIT(HCLGE_RX_PRIV_EN_B));
		}
	}

	/* Send 2 descriptors at one time */
	ret = hclge_cmd_send(&hdev->hw, desc, 2);
	if (ret)
		dev_err(&hdev->pdev->dev,
			"common threshold config cmd failed %d\n", ret);
	return ret;
}

static int hclge_common_wl_config(struct hclge_dev *hdev,
				  struct hclge_pkt_buf_alloc *buf_alloc)
{
	struct hclge_shared_buf *buf = &buf_alloc->s_buf;
	struct hclge_rx_com_wl *req;
	struct hclge_desc desc;
	int ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_RX_COM_WL_ALLOC, false);

	req = (struct hclge_rx_com_wl *)desc.data;
	req->com_wl.high = cpu_to_le16(buf->self.high >> HCLGE_BUF_UNIT_S);
	req->com_wl.high |=  cpu_to_le16(BIT(HCLGE_RX_PRIV_EN_B));

	req->com_wl.low = cpu_to_le16(buf->self.low >> HCLGE_BUF_UNIT_S);
	req->com_wl.low |=  cpu_to_le16(BIT(HCLGE_RX_PRIV_EN_B));

	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		dev_err(&hdev->pdev->dev,
			"common waterline config cmd failed %d\n", ret);

	return ret;
}

int hclge_buffer_alloc(struct hclge_dev *hdev)
{
	struct hclge_pkt_buf_alloc *pkt_buf;
	int ret;

	pkt_buf = kzalloc(sizeof(*pkt_buf), GFP_KERNEL);
	if (!pkt_buf)
		return -ENOMEM;

	ret = hclge_tx_buffer_calc(hdev, pkt_buf);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"could not calc tx buffer size for all TCs %d\n", ret);
		goto out;
	}

	ret = hclge_tx_buffer_alloc(hdev, pkt_buf);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"could not alloc tx buffers %d\n", ret);
		goto out;
	}

	ret = hclge_rx_buffer_calc(hdev, pkt_buf);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"could not calc rx priv buffer size for all TCs %d\n",
			ret);
		goto out;
	}

	ret = hclge_rx_priv_buf_alloc(hdev, pkt_buf);
	if (ret) {
		dev_err(&hdev->pdev->dev, "could not alloc rx priv buffer %d\n",
			ret);
		goto out;
	}

	if (hnae3_dev_dcb_supported(hdev)) {
		ret = hclge_rx_priv_wl_config(hdev, pkt_buf);
		if (ret) {
			dev_err(&hdev->pdev->dev,
				"could not configure rx private waterline %d\n",
				ret);
			goto out;
		}

		ret = hclge_common_thrd_config(hdev, pkt_buf);
		if (ret) {
			dev_err(&hdev->pdev->dev,
				"could not configure common threshold %d\n",
				ret);
			goto out;
		}
	}

	ret = hclge_common_wl_config(hdev, pkt_buf);
	if (ret)
		dev_err(&hdev->pdev->dev,
			"could not configure common waterline %d\n", ret);

out:
	kfree(pkt_buf);
	return ret;
}

static int hclge_init_roce_base_info(struct hclge_vport *vport)
{
	struct hnae3_handle *roce = &vport->roce;
	struct hnae3_handle *nic = &vport->nic;

	roce->rinfo.num_vectors = vport->back->num_roce_msi;

	if (vport->back->num_msi_left < vport->roce.rinfo.num_vectors ||
	    vport->back->num_msi_left == 0)
		return -EINVAL;

	roce->rinfo.base_vector = vport->back->roce_base_vector;

	roce->rinfo.netdev = nic->kinfo.netdev;
	roce->rinfo.roce_io_base = vport->back->hw.io_base;

	roce->pdev = nic->pdev;
	roce->ae_algo = nic->ae_algo;
	roce->numa_node_mask = nic->numa_node_mask;

	return 0;
}

static int hclge_init_msi(struct hclge_dev *hdev)
{
	struct pci_dev *pdev = hdev->pdev;
	int vectors;
	int i;

	vectors = pci_alloc_irq_vectors(pdev, 1, hdev->num_msi,
					PCI_IRQ_MSI | PCI_IRQ_MSIX);
	if (vectors < 0) {
		dev_err(&pdev->dev,
			"failed(%d) to allocate MSI/MSI-X vectors\n",
			vectors);
		return vectors;
	}
	if (vectors < hdev->num_msi)
		dev_warn(&hdev->pdev->dev,
			 "requested %d MSI/MSI-X, but allocated %d MSI/MSI-X\n",
			 hdev->num_msi, vectors);

	hdev->num_msi = vectors;
	hdev->num_msi_left = vectors;
	hdev->base_msi_vector = pdev->irq;
	hdev->roce_base_vector = hdev->base_msi_vector +
				hdev->roce_base_msix_offset;

	hdev->vector_status = devm_kcalloc(&pdev->dev, hdev->num_msi,
					   sizeof(u16), GFP_KERNEL);
	if (!hdev->vector_status) {
		pci_free_irq_vectors(pdev);
		return -ENOMEM;
	}

	for (i = 0; i < hdev->num_msi; i++)
		hdev->vector_status[i] = HCLGE_INVALID_VPORT;

	hdev->vector_irq = devm_kcalloc(&pdev->dev, hdev->num_msi,
					sizeof(int), GFP_KERNEL);
	if (!hdev->vector_irq) {
		pci_free_irq_vectors(pdev);
		return -ENOMEM;
	}

	return 0;
}

static void hclge_check_speed_dup(struct hclge_dev *hdev, int duplex, int speed)
{
	struct hclge_mac *mac = &hdev->hw.mac;

	if ((speed == HCLGE_MAC_SPEED_10M) || (speed == HCLGE_MAC_SPEED_100M))
		mac->duplex = (u8)duplex;
	else
		mac->duplex = HCLGE_MAC_FULL;

	mac->speed = speed;
}

int hclge_cfg_mac_speed_dup(struct hclge_dev *hdev, int speed, u8 duplex)
{
	struct hclge_config_mac_speed_dup_cmd *req;
	struct hclge_desc desc;
	int ret;

	req = (struct hclge_config_mac_speed_dup_cmd *)desc.data;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_CONFIG_SPEED_DUP, false);

	hnae3_set_bit(req->speed_dup, HCLGE_CFG_DUPLEX_B, !!duplex);

	switch (speed) {
	case HCLGE_MAC_SPEED_10M:
		hnae3_set_field(req->speed_dup, HCLGE_CFG_SPEED_M,
				HCLGE_CFG_SPEED_S, 6);
		break;
	case HCLGE_MAC_SPEED_100M:
		hnae3_set_field(req->speed_dup, HCLGE_CFG_SPEED_M,
				HCLGE_CFG_SPEED_S, 7);
		break;
	case HCLGE_MAC_SPEED_1G:
		hnae3_set_field(req->speed_dup, HCLGE_CFG_SPEED_M,
				HCLGE_CFG_SPEED_S, 0);
		break;
	case HCLGE_MAC_SPEED_10G:
		hnae3_set_field(req->speed_dup, HCLGE_CFG_SPEED_M,
				HCLGE_CFG_SPEED_S, 1);
		break;
	case HCLGE_MAC_SPEED_25G:
		hnae3_set_field(req->speed_dup, HCLGE_CFG_SPEED_M,
				HCLGE_CFG_SPEED_S, 2);
		break;
	case HCLGE_MAC_SPEED_40G:
		hnae3_set_field(req->speed_dup, HCLGE_CFG_SPEED_M,
				HCLGE_CFG_SPEED_S, 3);
		break;
	case HCLGE_MAC_SPEED_50G:
		hnae3_set_field(req->speed_dup, HCLGE_CFG_SPEED_M,
				HCLGE_CFG_SPEED_S, 4);
		break;
	case HCLGE_MAC_SPEED_100G:
		hnae3_set_field(req->speed_dup, HCLGE_CFG_SPEED_M,
				HCLGE_CFG_SPEED_S, 5);
		break;
	default:
		dev_err(&hdev->pdev->dev, "invalid speed (%d)\n", speed);
		return -EINVAL;
	}

	hnae3_set_bit(req->mac_change_fec_en, HCLGE_CFG_MAC_SPEED_CHANGE_EN_B,
		      1);

	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"mac speed/duplex config cmd failed %d.\n", ret);
		return ret;
	}

	hclge_check_speed_dup(hdev, duplex, speed);

	return 0;
}

static int hclge_cfg_mac_speed_dup_h(struct hnae3_handle *handle, int speed,
				     u8 duplex)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;

	return hclge_cfg_mac_speed_dup(hdev, speed, duplex);
}

static int hclge_query_mac_an_speed_dup(struct hclge_dev *hdev, int *speed,
					u8 *duplex)
{
	struct hclge_query_an_speed_dup_cmd *req;
	struct hclge_desc desc;
	int speed_tmp;
	int ret;

	req = (struct hclge_query_an_speed_dup_cmd *)desc.data;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_QUERY_AN_RESULT, true);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"mac speed/autoneg/duplex query cmd failed %d\n",
			ret);
		return ret;
	}

	*duplex = hnae3_get_bit(req->an_syn_dup_speed, HCLGE_QUERY_DUPLEX_B);
	speed_tmp = hnae3_get_field(req->an_syn_dup_speed, HCLGE_QUERY_SPEED_M,
				    HCLGE_QUERY_SPEED_S);

	ret = hclge_parse_speed(speed_tmp, speed);
	if (ret)
		dev_err(&hdev->pdev->dev,
			"could not parse speed(=%d), %d\n", speed_tmp, ret);

	return ret;
}

static int hclge_set_autoneg_en(struct hclge_dev *hdev, bool enable)
{
	struct hclge_config_auto_neg_cmd *req;
	struct hclge_desc desc;
	u32 flag = 0;
	int ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_CONFIG_AN_MODE, false);

	req = (struct hclge_config_auto_neg_cmd *)desc.data;
	hnae3_set_bit(flag, HCLGE_MAC_CFG_AN_EN_B, !!enable);
	req->cfg_an_cmd_flag = cpu_to_le32(flag);

	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		dev_err(&hdev->pdev->dev, "auto neg set cmd failed %d.\n",
			ret);

	return ret;
}

static int hclge_set_autoneg(struct hnae3_handle *handle, bool enable)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;

	return hclge_set_autoneg_en(hdev, enable);
}

static int hclge_get_autoneg(struct hnae3_handle *handle)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;
	struct phy_device *phydev = hdev->hw.mac.phydev;

	if (phydev)
		return phydev->autoneg;

	return hdev->hw.mac.autoneg;
}

static int hclge_set_default_mac_vlan_mask(struct hclge_dev *hdev,
					   bool mask_vlan,
					   u8 *mac_mask)
{
	struct hclge_mac_vlan_mask_entry_cmd *req;
	struct hclge_desc desc;
	int status;

	req = (struct hclge_mac_vlan_mask_entry_cmd *)desc.data;
	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_MAC_VLAN_MASK_SET, false);

	hnae3_set_bit(req->vlan_mask, HCLGE_VLAN_MASK_EN_B,
		      mask_vlan ? 1 : 0);
	ether_addr_copy(req->mac_mask, mac_mask);

	status = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (status)
		dev_err(&hdev->pdev->dev,
			"Config mac_vlan_mask failed for cmd_send, ret =%d\n",
			status);

	return status;
}

static int hclge_mac_init(struct hclge_dev *hdev)
{
	struct hnae3_handle *handle = &hdev->vport[0].nic;
	struct net_device *netdev = handle->kinfo.netdev;
	struct hclge_mac *mac = &hdev->hw.mac;
	u8 mac_mask[ETH_ALEN] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	struct hclge_vport *vport;
	int mtu;
	int ret;
	int i;

	ret = hclge_cfg_mac_speed_dup(hdev, hdev->hw.mac.speed, HCLGE_MAC_FULL);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"Config mac speed dup fail ret=%d\n", ret);
		return ret;
	}

	mac->link = 0;

	/* Initialize the MTA table work mode */
	hdev->enable_mta	= true;
	hdev->mta_mac_sel_type	= HCLGE_MAC_ADDR_47_36;

	ret = hclge_set_mta_filter_mode(hdev,
					hdev->mta_mac_sel_type,
					hdev->enable_mta);
	if (ret) {
		dev_err(&hdev->pdev->dev, "set mta filter mode failed %d\n",
			ret);
		return ret;
	}

	for (i = 0; i < hdev->num_alloc_vport; i++) {
		vport = &hdev->vport[i];
		vport->accept_mta_mc = false;

		memset(vport->mta_shadow, 0, sizeof(vport->mta_shadow));
		ret = hclge_cfg_func_mta_filter(hdev, vport->vport_id, false);
		if (ret) {
			dev_err(&hdev->pdev->dev,
				"set mta filter mode fail ret=%d\n", ret);
			return ret;
		}
	}

	ret = hclge_set_default_mac_vlan_mask(hdev, true, mac_mask);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"set default mac_vlan_mask fail ret=%d\n", ret);
		return ret;
	}

	if (netdev)
		mtu = netdev->mtu;
	else
		mtu = ETH_DATA_LEN;

	ret = hclge_set_mtu(handle, mtu);
	if (ret)
		dev_err(&hdev->pdev->dev,
			"set mtu failed ret=%d\n", ret);

	return ret;
}

static void hclge_mbx_task_schedule(struct hclge_dev *hdev)
{
	if (!test_and_set_bit(HCLGE_STATE_MBX_SERVICE_SCHED, &hdev->state))
		schedule_work(&hdev->mbx_service_task);
}

static void hclge_reset_task_schedule(struct hclge_dev *hdev)
{
	if (!test_and_set_bit(HCLGE_STATE_RST_SERVICE_SCHED, &hdev->state))
		schedule_work(&hdev->rst_service_task);
}

static void hclge_task_schedule(struct hclge_dev *hdev)
{
	if (!test_bit(HCLGE_STATE_DOWN, &hdev->state) &&
	    !test_bit(HCLGE_STATE_REMOVING, &hdev->state) &&
	    !test_and_set_bit(HCLGE_STATE_SERVICE_SCHED, &hdev->state))
		(void)schedule_work(&hdev->service_task);
}

static int hclge_get_mac_link_status(struct hclge_dev *hdev)
{
	struct hclge_link_status_cmd *req;
	struct hclge_desc desc;
	int link_status;
	int ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_QUERY_LINK_STATUS, true);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(&hdev->pdev->dev, "get link status cmd failed %d\n",
			ret);
		return ret;
	}

	req = (struct hclge_link_status_cmd *)desc.data;
	link_status = req->status & HCLGE_LINK_STATUS_UP_M;

	return !!link_status;
}

static int hclge_get_mac_phy_link(struct hclge_dev *hdev)
{
	int mac_state;
	int link_stat;

	if (test_bit(HCLGE_STATE_DOWN, &hdev->state))
		return 0;

	mac_state = hclge_get_mac_link_status(hdev);

	if (hdev->hw.mac.phydev) {
		if (hdev->hw.mac.phydev->state == PHY_RUNNING)
			link_stat = mac_state &
				hdev->hw.mac.phydev->link;
		else
			link_stat = 0;

	} else {
		link_stat = mac_state;
	}

	return !!link_stat;
}

static void hclge_update_link_status(struct hclge_dev *hdev)
{
	struct hnae3_client *client = hdev->nic_client;
	struct hnae3_handle *handle;
	int state;
	int i;

	if (!client)
		return;
	state = hclge_get_mac_phy_link(hdev);
	if (state != hdev->hw.mac.link) {
		for (i = 0; i < hdev->num_vmdq_vport + 1; i++) {
			handle = &hdev->vport[i].nic;
			client->ops->link_status_change(handle, state);
		}
		hdev->hw.mac.link = state;
	}
}

static int hclge_update_speed_duplex(struct hclge_dev *hdev)
{
	struct hclge_mac mac = hdev->hw.mac;
	u8 duplex;
	int speed;
	int ret;

	/* get the speed and duplex as autoneg'result from mac cmd when phy
	 * doesn't exit.
	 */
	if (mac.phydev || !mac.autoneg)
		return 0;

	ret = hclge_query_mac_an_speed_dup(hdev, &speed, &duplex);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"mac autoneg/speed/duplex query failed %d\n", ret);
		return ret;
	}

	if ((mac.speed != speed) || (mac.duplex != duplex)) {
		ret = hclge_cfg_mac_speed_dup(hdev, speed, duplex);
		if (ret) {
			dev_err(&hdev->pdev->dev,
				"mac speed/duplex config failed %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static int hclge_update_speed_duplex_h(struct hnae3_handle *handle)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;

	return hclge_update_speed_duplex(hdev);
}

static int hclge_get_status(struct hnae3_handle *handle)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;

	hclge_update_link_status(hdev);

	return hdev->hw.mac.link;
}

static void hclge_service_timer(struct timer_list *t)
{
	struct hclge_dev *hdev = from_timer(hdev, t, service_timer);

	mod_timer(&hdev->service_timer, jiffies + HZ);
	hdev->hw_stats.stats_timer++;
	hclge_task_schedule(hdev);
}

static void hclge_service_complete(struct hclge_dev *hdev)
{
	WARN_ON(!test_bit(HCLGE_STATE_SERVICE_SCHED, &hdev->state));

	/* Flush memory before next watchdog */
	smp_mb__before_atomic();
	clear_bit(HCLGE_STATE_SERVICE_SCHED, &hdev->state);
}

static u32 hclge_check_event_cause(struct hclge_dev *hdev, u32 *clearval)
{
	u32 rst_src_reg;
	u32 cmdq_src_reg;

	/* fetch the events from their corresponding regs */
	rst_src_reg = hclge_read_dev(&hdev->hw, HCLGE_MISC_VECTOR_INT_STS);
	cmdq_src_reg = hclge_read_dev(&hdev->hw, HCLGE_VECTOR0_CMDQ_SRC_REG);

	/* Assumption: If by any chance reset and mailbox events are reported
	 * together then we will only process reset event in this go and will
	 * defer the processing of the mailbox events. Since, we would have not
	 * cleared RX CMDQ event this time we would receive again another
	 * interrupt from H/W just for the mailbox.
	 */

	/* check for vector0 reset event sources */
	if (BIT(HCLGE_VECTOR0_GLOBALRESET_INT_B) & rst_src_reg) {
		set_bit(HCLGE_STATE_CMD_DISABLE, &hdev->state);
		set_bit(HNAE3_GLOBAL_RESET, &hdev->reset_pending);
		*clearval = BIT(HCLGE_VECTOR0_GLOBALRESET_INT_B);
		return HCLGE_VECTOR0_EVENT_RST;
	}

	if (BIT(HCLGE_VECTOR0_CORERESET_INT_B) & rst_src_reg) {
		set_bit(HCLGE_STATE_CMD_DISABLE, &hdev->state);
		set_bit(HNAE3_CORE_RESET, &hdev->reset_pending);
		*clearval = BIT(HCLGE_VECTOR0_CORERESET_INT_B);
		return HCLGE_VECTOR0_EVENT_RST;
	}

	if (BIT(HCLGE_VECTOR0_IMPRESET_INT_B) & rst_src_reg) {
		set_bit(HNAE3_IMP_RESET, &hdev->reset_pending);
		*clearval = BIT(HCLGE_VECTOR0_IMPRESET_INT_B);
		return HCLGE_VECTOR0_EVENT_RST;
	}

	/* check for vector0 mailbox(=CMDQ RX) event source */
	if (BIT(HCLGE_VECTOR0_RX_CMDQ_INT_B) & cmdq_src_reg) {
		cmdq_src_reg &= ~BIT(HCLGE_VECTOR0_RX_CMDQ_INT_B);
		*clearval = cmdq_src_reg;
		return HCLGE_VECTOR0_EVENT_MBX;
	}

	return HCLGE_VECTOR0_EVENT_OTHER;
}

static void hclge_clear_event_cause(struct hclge_dev *hdev, u32 event_type,
				    u32 regclr)
{
	switch (event_type) {
	case HCLGE_VECTOR0_EVENT_RST:
		hclge_write_dev(&hdev->hw, HCLGE_MISC_RESET_STS_REG, regclr);
		break;
	case HCLGE_VECTOR0_EVENT_MBX:
		hclge_write_dev(&hdev->hw, HCLGE_VECTOR0_CMDQ_SRC_REG, regclr);
		break;
	}
}

static void hclge_clear_all_event_cause(struct hclge_dev *hdev)
{
	hclge_clear_event_cause(hdev, HCLGE_VECTOR0_EVENT_RST,
				BIT(HCLGE_VECTOR0_GLOBALRESET_INT_B) |
				BIT(HCLGE_VECTOR0_CORERESET_INT_B) |
				BIT(HCLGE_VECTOR0_IMPRESET_INT_B));
	hclge_clear_event_cause(hdev, HCLGE_VECTOR0_EVENT_MBX, 0);
}

static void hclge_enable_vector(struct hclge_misc_vector *vector, bool enable)
{
	writel(enable ? 1 : 0, vector->addr);
}

static irqreturn_t hclge_misc_irq_handle(int irq, void *data)
{
	struct hclge_dev *hdev = data;
	u32 event_cause;
	u32 clearval;

	hclge_enable_vector(&hdev->misc_vector, false);
	event_cause = hclge_check_event_cause(hdev, &clearval);

	/* vector 0 interrupt is shared with reset and mailbox source events.*/
	switch (event_cause) {
	case HCLGE_VECTOR0_EVENT_RST:
		hclge_reset_task_schedule(hdev);
		break;
	case HCLGE_VECTOR0_EVENT_MBX:
		/* If we are here then,
		 * 1. Either we are not handling any mbx task and we are not
		 *    scheduled as well
		 *                        OR
		 * 2. We could be handling a mbx task but nothing more is
		 *    scheduled.
		 * In both cases, we should schedule mbx task as there are more
		 * mbx messages reported by this interrupt.
		 */
		hclge_mbx_task_schedule(hdev);
		break;
	default:
		dev_warn(&hdev->pdev->dev,
			 "received unknown or unhandled event of vector0\n");
		break;
	}

	/* clear the source of interrupt if it is not cause by reset */
	if (event_cause != HCLGE_VECTOR0_EVENT_RST) {
		hclge_clear_event_cause(hdev, event_cause, clearval);
		hclge_enable_vector(&hdev->misc_vector, true);
	}

	return IRQ_HANDLED;
}

static void hclge_free_vector(struct hclge_dev *hdev, int vector_id)
{
	if (hdev->vector_status[vector_id] == HCLGE_INVALID_VPORT) {
		dev_warn(&hdev->pdev->dev,
			 "vector(vector_id %d) has been freed.\n", vector_id);
		return;
	}

	hdev->vector_status[vector_id] = HCLGE_INVALID_VPORT;
	hdev->num_msi_left += 1;
	hdev->num_msi_used -= 1;
}

static void hclge_get_misc_vector(struct hclge_dev *hdev)
{
	struct hclge_misc_vector *vector = &hdev->misc_vector;

	vector->vector_irq = pci_irq_vector(hdev->pdev, 0);

	vector->addr = hdev->hw.io_base + HCLGE_MISC_VECTOR_REG_BASE;
	hdev->vector_status[0] = 0;

	hdev->num_msi_left -= 1;
	hdev->num_msi_used += 1;
}

static int hclge_misc_irq_init(struct hclge_dev *hdev)
{
	int ret;

	hclge_get_misc_vector(hdev);

	/* this would be explicitly freed in the end */
	ret = request_irq(hdev->misc_vector.vector_irq, hclge_misc_irq_handle,
			  0, "hclge_misc", hdev);
	if (ret) {
		hclge_free_vector(hdev, 0);
		dev_err(&hdev->pdev->dev, "request misc irq(%d) fail\n",
			hdev->misc_vector.vector_irq);
	}

	return ret;
}

static void hclge_misc_irq_uninit(struct hclge_dev *hdev)
{
	free_irq(hdev->misc_vector.vector_irq, hdev);
	hclge_free_vector(hdev, 0);
}

static int hclge_notify_client(struct hclge_dev *hdev,
			       enum hnae3_reset_notify_type type)
{
	struct hnae3_client *client = hdev->nic_client;
	u16 i;

	if (!client->ops->reset_notify)
		return -EOPNOTSUPP;

	for (i = 0; i < hdev->num_vmdq_vport + 1; i++) {
		struct hnae3_handle *handle = &hdev->vport[i].nic;
		int ret;

		ret = client->ops->reset_notify(handle, type);
		if (ret)
			return ret;
	}

	return 0;
}

static int hclge_reset_wait(struct hclge_dev *hdev)
{
#define HCLGE_RESET_WATI_MS	100
#define HCLGE_RESET_WAIT_CNT	5
	u32 val, reg, reg_bit;
	u32 cnt = 0;

	switch (hdev->reset_type) {
	case HNAE3_GLOBAL_RESET:
		reg = HCLGE_GLOBAL_RESET_REG;
		reg_bit = HCLGE_GLOBAL_RESET_BIT;
		break;
	case HNAE3_CORE_RESET:
		reg = HCLGE_GLOBAL_RESET_REG;
		reg_bit = HCLGE_CORE_RESET_BIT;
		break;
	case HNAE3_FUNC_RESET:
		reg = HCLGE_FUN_RST_ING;
		reg_bit = HCLGE_FUN_RST_ING_B;
		break;
	default:
		dev_err(&hdev->pdev->dev,
			"Wait for unsupported reset type: %d\n",
			hdev->reset_type);
		return -EINVAL;
	}

	val = hclge_read_dev(&hdev->hw, reg);
	while (hnae3_get_bit(val, reg_bit) && cnt < HCLGE_RESET_WAIT_CNT) {
		msleep(HCLGE_RESET_WATI_MS);
		val = hclge_read_dev(&hdev->hw, reg);
		cnt++;
	}

	if (cnt >= HCLGE_RESET_WAIT_CNT) {
		dev_warn(&hdev->pdev->dev,
			 "Wait for reset timeout: %d\n", hdev->reset_type);
		return -EBUSY;
	}

	return 0;
}

int hclge_func_reset_cmd(struct hclge_dev *hdev, int func_id)
{
	struct hclge_desc desc;
	struct hclge_reset_cmd *req = (struct hclge_reset_cmd *)desc.data;
	int ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_CFG_RST_TRIGGER, false);
	hnae3_set_bit(req->mac_func_reset, HCLGE_CFG_RESET_FUNC_B, 1);
	req->fun_reset_vfid = func_id;

	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		dev_err(&hdev->pdev->dev,
			"send function reset cmd fail, status =%d\n", ret);

	return ret;
}

static void hclge_do_reset(struct hclge_dev *hdev)
{
	struct pci_dev *pdev = hdev->pdev;
	u32 val;

	switch (hdev->reset_type) {
	case HNAE3_GLOBAL_RESET:
		val = hclge_read_dev(&hdev->hw, HCLGE_GLOBAL_RESET_REG);
		hnae3_set_bit(val, HCLGE_GLOBAL_RESET_BIT, 1);
		hclge_write_dev(&hdev->hw, HCLGE_GLOBAL_RESET_REG, val);
		dev_info(&pdev->dev, "Global Reset requested\n");
		break;
	case HNAE3_CORE_RESET:
		val = hclge_read_dev(&hdev->hw, HCLGE_GLOBAL_RESET_REG);
		hnae3_set_bit(val, HCLGE_CORE_RESET_BIT, 1);
		hclge_write_dev(&hdev->hw, HCLGE_GLOBAL_RESET_REG, val);
		dev_info(&pdev->dev, "Core Reset requested\n");
		break;
	case HNAE3_FUNC_RESET:
		dev_info(&pdev->dev, "PF Reset requested\n");
		hclge_func_reset_cmd(hdev, 0);
		/* schedule again to check later */
		set_bit(HNAE3_FUNC_RESET, &hdev->reset_pending);
		hclge_reset_task_schedule(hdev);
		break;
	default:
		dev_warn(&pdev->dev,
			 "Unsupported reset type: %d\n", hdev->reset_type);
		break;
	}
}

static enum hnae3_reset_type hclge_get_reset_level(struct hclge_dev *hdev,
						   unsigned long *addr)
{
	enum hnae3_reset_type rst_level = HNAE3_NONE_RESET;

	/* return the highest priority reset level amongst all */
	if (test_bit(HNAE3_GLOBAL_RESET, addr))
		rst_level = HNAE3_GLOBAL_RESET;
	else if (test_bit(HNAE3_CORE_RESET, addr))
		rst_level = HNAE3_CORE_RESET;
	else if (test_bit(HNAE3_IMP_RESET, addr))
		rst_level = HNAE3_IMP_RESET;
	else if (test_bit(HNAE3_FUNC_RESET, addr))
		rst_level = HNAE3_FUNC_RESET;

	/* now, clear all other resets */
	clear_bit(HNAE3_GLOBAL_RESET, addr);
	clear_bit(HNAE3_CORE_RESET, addr);
	clear_bit(HNAE3_IMP_RESET, addr);
	clear_bit(HNAE3_FUNC_RESET, addr);

	return rst_level;
}

static void hclge_clear_reset_cause(struct hclge_dev *hdev)
{
	u32 clearval = 0;

	switch (hdev->reset_type) {
	case HNAE3_IMP_RESET:
		clearval = BIT(HCLGE_VECTOR0_IMPRESET_INT_B);
		break;
	case HNAE3_GLOBAL_RESET:
		clearval = BIT(HCLGE_VECTOR0_GLOBALRESET_INT_B);
		break;
	case HNAE3_CORE_RESET:
		clearval = BIT(HCLGE_VECTOR0_CORERESET_INT_B);
		break;
	default:
		break;
	}

	if (!clearval)
		return;

	hclge_write_dev(&hdev->hw, HCLGE_MISC_RESET_STS_REG, clearval);
	hclge_enable_vector(&hdev->misc_vector, true);
}

static void hclge_reset(struct hclge_dev *hdev)
{
	struct hnae3_handle *handle;

	/* perform reset of the stack & ae device for a client */
	handle = &hdev->vport[0].nic;
	rtnl_lock();
	hclge_notify_client(hdev, HNAE3_DOWN_CLIENT);
	rtnl_unlock();

	if (!hclge_reset_wait(hdev)) {
		rtnl_lock();
		hclge_notify_client(hdev, HNAE3_UNINIT_CLIENT);
		hclge_reset_ae_dev(hdev->ae_dev);
		hclge_notify_client(hdev, HNAE3_INIT_CLIENT);

		hclge_clear_reset_cause(hdev);
	} else {
		rtnl_lock();
		/* schedule again to check pending resets later */
		set_bit(hdev->reset_type, &hdev->reset_pending);
		hclge_reset_task_schedule(hdev);
	}

	hclge_notify_client(hdev, HNAE3_UP_CLIENT);
	handle->last_reset_time = jiffies;
	rtnl_unlock();
}

static void hclge_reset_event(struct hnae3_handle *handle)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;

	/* check if this is a new reset request and we are not here just because
	 * last reset attempt did not succeed and watchdog hit us again. We will
	 * know this if last reset request did not occur very recently (watchdog
	 * timer = 5*HZ, let us check after sufficiently large time, say 4*5*Hz)
	 * In case of new request we reset the "reset level" to PF reset.
	 * And if it is a repeat reset request of the most recent one then we
	 * want to make sure we throttle the reset request. Therefore, we will
	 * not allow it again before 3*HZ times.
	 */
	if (time_before(jiffies, (handle->last_reset_time + 3 * HZ)))
		return;
	else if (time_after(jiffies, (handle->last_reset_time + 4 * 5 * HZ)))
		handle->reset_level = HNAE3_FUNC_RESET;

	dev_info(&hdev->pdev->dev, "received reset event , reset type is %d",
		 handle->reset_level);

	/* request reset & schedule reset task */
	set_bit(handle->reset_level, &hdev->reset_request);
	hclge_reset_task_schedule(hdev);

	if (handle->reset_level < HNAE3_GLOBAL_RESET)
		handle->reset_level++;
}

static void hclge_reset_subtask(struct hclge_dev *hdev)
{
	/* check if there is any ongoing reset in the hardware. This status can
	 * be checked from reset_pending. If there is then, we need to wait for
	 * hardware to complete reset.
	 *    a. If we are able to figure out in reasonable time that hardware
	 *       has fully resetted then, we can proceed with driver, client
	 *       reset.
	 *    b. else, we can come back later to check this status so re-sched
	 *       now.
	 */
	hdev->reset_type = hclge_get_reset_level(hdev, &hdev->reset_pending);
	if (hdev->reset_type != HNAE3_NONE_RESET)
		hclge_reset(hdev);

	/* check if we got any *new* reset requests to be honored */
	hdev->reset_type = hclge_get_reset_level(hdev, &hdev->reset_request);
	if (hdev->reset_type != HNAE3_NONE_RESET)
		hclge_do_reset(hdev);

	hdev->reset_type = HNAE3_NONE_RESET;
}

static void hclge_reset_service_task(struct work_struct *work)
{
	struct hclge_dev *hdev =
		container_of(work, struct hclge_dev, rst_service_task);

	if (test_and_set_bit(HCLGE_STATE_RST_HANDLING, &hdev->state))
		return;

	clear_bit(HCLGE_STATE_RST_SERVICE_SCHED, &hdev->state);

	hclge_reset_subtask(hdev);

	clear_bit(HCLGE_STATE_RST_HANDLING, &hdev->state);
}

static void hclge_mailbox_service_task(struct work_struct *work)
{
	struct hclge_dev *hdev =
		container_of(work, struct hclge_dev, mbx_service_task);

	if (test_and_set_bit(HCLGE_STATE_MBX_HANDLING, &hdev->state))
		return;

	clear_bit(HCLGE_STATE_MBX_SERVICE_SCHED, &hdev->state);

	hclge_mbx_handler(hdev);

	clear_bit(HCLGE_STATE_MBX_HANDLING, &hdev->state);
}

static void hclge_service_task(struct work_struct *work)
{
	struct hclge_dev *hdev =
		container_of(work, struct hclge_dev, service_task);

	if (hdev->hw_stats.stats_timer >= HCLGE_STATS_TIMER_INTERVAL) {
		hclge_update_stats_for_all(hdev);
		hdev->hw_stats.stats_timer = 0;
	}

	hclge_update_speed_duplex(hdev);
	hclge_update_link_status(hdev);
	hclge_service_complete(hdev);
}

struct hclge_vport *hclge_get_vport(struct hnae3_handle *handle)
{
	/* VF handle has no client */
	if (!handle->client)
		return container_of(handle, struct hclge_vport, nic);
	else if (handle->client->type == HNAE3_CLIENT_ROCE)
		return container_of(handle, struct hclge_vport, roce);
	else
		return container_of(handle, struct hclge_vport, nic);
}

static int hclge_get_vector(struct hnae3_handle *handle, u16 vector_num,
			    struct hnae3_vector_info *vector_info)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hnae3_vector_info *vector = vector_info;
	struct hclge_dev *hdev = vport->back;
	int alloc = 0;
	int i, j;

	vector_num = min(hdev->num_msi_left, vector_num);

	for (j = 0; j < vector_num; j++) {
		for (i = 1; i < hdev->num_msi; i++) {
			if (hdev->vector_status[i] == HCLGE_INVALID_VPORT) {
				vector->vector = pci_irq_vector(hdev->pdev, i);
				vector->io_addr = hdev->hw.io_base +
					HCLGE_VECTOR_REG_BASE +
					(i - 1) * HCLGE_VECTOR_REG_OFFSET +
					vport->vport_id *
					HCLGE_VECTOR_VF_OFFSET;
				hdev->vector_status[i] = vport->vport_id;
				hdev->vector_irq[i] = vector->vector;

				vector++;
				alloc++;

				break;
			}
		}
	}
	hdev->num_msi_left -= alloc;
	hdev->num_msi_used += alloc;

	return alloc;
}

static int hclge_get_vector_index(struct hclge_dev *hdev, int vector)
{
	int i;

	for (i = 0; i < hdev->num_msi; i++)
		if (vector == hdev->vector_irq[i])
			return i;

	return -EINVAL;
}

static int hclge_put_vector(struct hnae3_handle *handle, int vector)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;
	int vector_id;

	vector_id = hclge_get_vector_index(hdev, vector);
	if (vector_id < 0) {
		dev_err(&hdev->pdev->dev,
			"Get vector index fail. vector_id =%d\n", vector_id);
		return vector_id;
	}

	hclge_free_vector(hdev, vector_id);

	return 0;
}

static u32 hclge_get_rss_key_size(struct hnae3_handle *handle)
{
	return HCLGE_RSS_KEY_SIZE;
}

static u32 hclge_get_rss_indir_size(struct hnae3_handle *handle)
{
	return HCLGE_RSS_IND_TBL_SIZE;
}

static int hclge_set_rss_algo_key(struct hclge_dev *hdev,
				  const u8 hfunc, const u8 *key)
{
	struct hclge_rss_config_cmd *req;
	struct hclge_desc desc;
	int key_offset;
	int key_size;
	int ret;

	req = (struct hclge_rss_config_cmd *)desc.data;

	for (key_offset = 0; key_offset < 3; key_offset++) {
		hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_RSS_GENERIC_CONFIG,
					   false);

		req->hash_config |= (hfunc & HCLGE_RSS_HASH_ALGO_MASK);
		req->hash_config |= (key_offset << HCLGE_RSS_HASH_KEY_OFFSET_B);

		if (key_offset == 2)
			key_size =
			HCLGE_RSS_KEY_SIZE - HCLGE_RSS_HASH_KEY_NUM * 2;
		else
			key_size = HCLGE_RSS_HASH_KEY_NUM;

		memcpy(req->hash_key,
		       key + key_offset * HCLGE_RSS_HASH_KEY_NUM, key_size);

		ret = hclge_cmd_send(&hdev->hw, &desc, 1);
		if (ret) {
			dev_err(&hdev->pdev->dev,
				"Configure RSS config fail, status = %d\n",
				ret);
			return ret;
		}
	}
	return 0;
}

static int hclge_set_rss_indir_table(struct hclge_dev *hdev, const u8 *indir)
{
	struct hclge_rss_indirection_table_cmd *req;
	struct hclge_desc desc;
	int i, j;
	int ret;

	req = (struct hclge_rss_indirection_table_cmd *)desc.data;

	for (i = 0; i < HCLGE_RSS_CFG_TBL_NUM; i++) {
		hclge_cmd_setup_basic_desc
			(&desc, HCLGE_OPC_RSS_INDIR_TABLE, false);

		req->start_table_index =
			cpu_to_le16(i * HCLGE_RSS_CFG_TBL_SIZE);
		req->rss_set_bitmap = cpu_to_le16(HCLGE_RSS_SET_BITMAP_MSK);

		for (j = 0; j < HCLGE_RSS_CFG_TBL_SIZE; j++)
			req->rss_result[j] =
				indir[i * HCLGE_RSS_CFG_TBL_SIZE + j];

		ret = hclge_cmd_send(&hdev->hw, &desc, 1);
		if (ret) {
			dev_err(&hdev->pdev->dev,
				"Configure rss indir table fail,status = %d\n",
				ret);
			return ret;
		}
	}
	return 0;
}

static int hclge_set_rss_tc_mode(struct hclge_dev *hdev, u16 *tc_valid,
				 u16 *tc_size, u16 *tc_offset)
{
	struct hclge_rss_tc_mode_cmd *req;
	struct hclge_desc desc;
	int ret;
	int i;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_RSS_TC_MODE, false);
	req = (struct hclge_rss_tc_mode_cmd *)desc.data;

	for (i = 0; i < HCLGE_MAX_TC_NUM; i++) {
		u16 mode = 0;

		hnae3_set_bit(mode, HCLGE_RSS_TC_VALID_B, (tc_valid[i] & 0x1));
		hnae3_set_field(mode, HCLGE_RSS_TC_SIZE_M,
				HCLGE_RSS_TC_SIZE_S, tc_size[i]);
		hnae3_set_field(mode, HCLGE_RSS_TC_OFFSET_M,
				HCLGE_RSS_TC_OFFSET_S, tc_offset[i]);

		req->rss_tc_mode[i] = cpu_to_le16(mode);
	}

	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		dev_err(&hdev->pdev->dev,
			"Configure rss tc mode fail, status = %d\n", ret);

	return ret;
}

static int hclge_set_rss_input_tuple(struct hclge_dev *hdev)
{
	struct hclge_rss_input_tuple_cmd *req;
	struct hclge_desc desc;
	int ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_RSS_INPUT_TUPLE, false);

	req = (struct hclge_rss_input_tuple_cmd *)desc.data;

	/* Get the tuple cfg from pf */
	req->ipv4_tcp_en = hdev->vport[0].rss_tuple_sets.ipv4_tcp_en;
	req->ipv4_udp_en = hdev->vport[0].rss_tuple_sets.ipv4_udp_en;
	req->ipv4_sctp_en = hdev->vport[0].rss_tuple_sets.ipv4_sctp_en;
	req->ipv4_fragment_en = hdev->vport[0].rss_tuple_sets.ipv4_fragment_en;
	req->ipv6_tcp_en = hdev->vport[0].rss_tuple_sets.ipv6_tcp_en;
	req->ipv6_udp_en = hdev->vport[0].rss_tuple_sets.ipv6_udp_en;
	req->ipv6_sctp_en = hdev->vport[0].rss_tuple_sets.ipv6_sctp_en;
	req->ipv6_fragment_en = hdev->vport[0].rss_tuple_sets.ipv6_fragment_en;
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		dev_err(&hdev->pdev->dev,
			"Configure rss input fail, status = %d\n", ret);
	return ret;
}

static int hclge_get_rss(struct hnae3_handle *handle, u32 *indir,
			 u8 *key, u8 *hfunc)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	int i;

	/* Get hash algorithm */
	if (hfunc)
		*hfunc = vport->rss_algo;

	/* Get the RSS Key required by the user */
	if (key)
		memcpy(key, vport->rss_hash_key, HCLGE_RSS_KEY_SIZE);

	/* Get indirect table */
	if (indir)
		for (i = 0; i < HCLGE_RSS_IND_TBL_SIZE; i++)
			indir[i] =  vport->rss_indirection_tbl[i];

	return 0;
}

static int hclge_set_rss(struct hnae3_handle *handle, const u32 *indir,
			 const  u8 *key, const  u8 hfunc)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;
	u8 hash_algo;
	int ret, i;

	/* Set the RSS Hash Key if specififed by the user */
	if (key) {

		if (hfunc == ETH_RSS_HASH_TOP ||
		    hfunc == ETH_RSS_HASH_NO_CHANGE)
			hash_algo = HCLGE_RSS_HASH_ALGO_TOEPLITZ;
		else
			return -EINVAL;
		ret = hclge_set_rss_algo_key(hdev, hash_algo, key);
		if (ret)
			return ret;

		/* Update the shadow RSS key with user specified qids */
		memcpy(vport->rss_hash_key, key, HCLGE_RSS_KEY_SIZE);
		vport->rss_algo = hash_algo;
	}

	/* Update the shadow RSS table with user specified qids */
	for (i = 0; i < HCLGE_RSS_IND_TBL_SIZE; i++)
		vport->rss_indirection_tbl[i] = indir[i];

	/* Update the hardware */
	return hclge_set_rss_indir_table(hdev, vport->rss_indirection_tbl);
}

static u8 hclge_get_rss_hash_bits(struct ethtool_rxnfc *nfc)
{
	u8 hash_sets = nfc->data & RXH_L4_B_0_1 ? HCLGE_S_PORT_BIT : 0;

	if (nfc->data & RXH_L4_B_2_3)
		hash_sets |= HCLGE_D_PORT_BIT;
	else
		hash_sets &= ~HCLGE_D_PORT_BIT;

	if (nfc->data & RXH_IP_SRC)
		hash_sets |= HCLGE_S_IP_BIT;
	else
		hash_sets &= ~HCLGE_S_IP_BIT;

	if (nfc->data & RXH_IP_DST)
		hash_sets |= HCLGE_D_IP_BIT;
	else
		hash_sets &= ~HCLGE_D_IP_BIT;

	if (nfc->flow_type == SCTP_V4_FLOW || nfc->flow_type == SCTP_V6_FLOW)
		hash_sets |= HCLGE_V_TAG_BIT;

	return hash_sets;
}

static int hclge_set_rss_tuple(struct hnae3_handle *handle,
			       struct ethtool_rxnfc *nfc)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;
	struct hclge_rss_input_tuple_cmd *req;
	struct hclge_desc desc;
	u8 tuple_sets;
	int ret;

	if (nfc->data & ~(RXH_IP_SRC | RXH_IP_DST |
			  RXH_L4_B_0_1 | RXH_L4_B_2_3))
		return -EINVAL;

	req = (struct hclge_rss_input_tuple_cmd *)desc.data;
	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_RSS_INPUT_TUPLE, false);

	req->ipv4_tcp_en = vport->rss_tuple_sets.ipv4_tcp_en;
	req->ipv4_udp_en = vport->rss_tuple_sets.ipv4_udp_en;
	req->ipv4_sctp_en = vport->rss_tuple_sets.ipv4_sctp_en;
	req->ipv4_fragment_en = vport->rss_tuple_sets.ipv4_fragment_en;
	req->ipv6_tcp_en = vport->rss_tuple_sets.ipv6_tcp_en;
	req->ipv6_udp_en = vport->rss_tuple_sets.ipv6_udp_en;
	req->ipv6_sctp_en = vport->rss_tuple_sets.ipv6_sctp_en;
	req->ipv6_fragment_en = vport->rss_tuple_sets.ipv6_fragment_en;

	tuple_sets = hclge_get_rss_hash_bits(nfc);
	switch (nfc->flow_type) {
	case TCP_V4_FLOW:
		req->ipv4_tcp_en = tuple_sets;
		break;
	case TCP_V6_FLOW:
		req->ipv6_tcp_en = tuple_sets;
		break;
	case UDP_V4_FLOW:
		req->ipv4_udp_en = tuple_sets;
		break;
	case UDP_V6_FLOW:
		req->ipv6_udp_en = tuple_sets;
		break;
	case SCTP_V4_FLOW:
		req->ipv4_sctp_en = tuple_sets;
		break;
	case SCTP_V6_FLOW:
		if ((nfc->data & RXH_L4_B_0_1) ||
		    (nfc->data & RXH_L4_B_2_3))
			return -EINVAL;

		req->ipv6_sctp_en = tuple_sets;
		break;
	case IPV4_FLOW:
		req->ipv4_fragment_en = HCLGE_RSS_INPUT_TUPLE_OTHER;
		break;
	case IPV6_FLOW:
		req->ipv6_fragment_en = HCLGE_RSS_INPUT_TUPLE_OTHER;
		break;
	default:
		return -EINVAL;
	}

	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"Set rss tuple fail, status = %d\n", ret);
		return ret;
	}

	vport->rss_tuple_sets.ipv4_tcp_en = req->ipv4_tcp_en;
	vport->rss_tuple_sets.ipv4_udp_en = req->ipv4_udp_en;
	vport->rss_tuple_sets.ipv4_sctp_en = req->ipv4_sctp_en;
	vport->rss_tuple_sets.ipv4_fragment_en = req->ipv4_fragment_en;
	vport->rss_tuple_sets.ipv6_tcp_en = req->ipv6_tcp_en;
	vport->rss_tuple_sets.ipv6_udp_en = req->ipv6_udp_en;
	vport->rss_tuple_sets.ipv6_sctp_en = req->ipv6_sctp_en;
	vport->rss_tuple_sets.ipv6_fragment_en = req->ipv6_fragment_en;
	return 0;
}

static int hclge_get_rss_tuple(struct hnae3_handle *handle,
			       struct ethtool_rxnfc *nfc)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	u8 tuple_sets;

	nfc->data = 0;

	switch (nfc->flow_type) {
	case TCP_V4_FLOW:
		tuple_sets = vport->rss_tuple_sets.ipv4_tcp_en;
		break;
	case UDP_V4_FLOW:
		tuple_sets = vport->rss_tuple_sets.ipv4_udp_en;
		break;
	case TCP_V6_FLOW:
		tuple_sets = vport->rss_tuple_sets.ipv6_tcp_en;
		break;
	case UDP_V6_FLOW:
		tuple_sets = vport->rss_tuple_sets.ipv6_udp_en;
		break;
	case SCTP_V4_FLOW:
		tuple_sets = vport->rss_tuple_sets.ipv4_sctp_en;
		break;
	case SCTP_V6_FLOW:
		tuple_sets = vport->rss_tuple_sets.ipv6_sctp_en;
		break;
	case IPV4_FLOW:
	case IPV6_FLOW:
		tuple_sets = HCLGE_S_IP_BIT | HCLGE_D_IP_BIT;
		break;
	default:
		return -EINVAL;
	}

	if (!tuple_sets)
		return 0;

	if (tuple_sets & HCLGE_D_PORT_BIT)
		nfc->data |= RXH_L4_B_2_3;
	if (tuple_sets & HCLGE_S_PORT_BIT)
		nfc->data |= RXH_L4_B_0_1;
	if (tuple_sets & HCLGE_D_IP_BIT)
		nfc->data |= RXH_IP_DST;
	if (tuple_sets & HCLGE_S_IP_BIT)
		nfc->data |= RXH_IP_SRC;

	return 0;
}

static int hclge_get_tc_size(struct hnae3_handle *handle)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;

	return hdev->rss_size_max;
}

int hclge_rss_init_hw(struct hclge_dev *hdev)
{
	struct hclge_vport *vport = hdev->vport;
	u8 *rss_indir = vport[0].rss_indirection_tbl;
	u16 rss_size = vport[0].alloc_rss_size;
	u8 *key = vport[0].rss_hash_key;
	u8 hfunc = vport[0].rss_algo;
	u16 tc_offset[HCLGE_MAX_TC_NUM];
	u16 tc_valid[HCLGE_MAX_TC_NUM];
	u16 tc_size[HCLGE_MAX_TC_NUM];
	u16 roundup_size;
	int i, ret;

	ret = hclge_set_rss_indir_table(hdev, rss_indir);
	if (ret)
		return ret;

	ret = hclge_set_rss_algo_key(hdev, hfunc, key);
	if (ret)
		return ret;

	ret = hclge_set_rss_input_tuple(hdev);
	if (ret)
		return ret;

	/* Each TC have the same queue size, and tc_size set to hardware is
	 * the log2 of roundup power of two of rss_size, the acutal queue
	 * size is limited by indirection table.
	 */
	if (rss_size > HCLGE_RSS_TC_SIZE_7 || rss_size == 0) {
		dev_err(&hdev->pdev->dev,
			"Configure rss tc size failed, invalid TC_SIZE = %d\n",
			rss_size);
		return -EINVAL;
	}

	roundup_size = roundup_pow_of_two(rss_size);
	roundup_size = ilog2(roundup_size);

	for (i = 0; i < HCLGE_MAX_TC_NUM; i++) {
		tc_valid[i] = 0;

		if (!(hdev->hw_tc_map & BIT(i)))
			continue;

		tc_valid[i] = 1;
		tc_size[i] = roundup_size;
		tc_offset[i] = rss_size * i;
	}

	return hclge_set_rss_tc_mode(hdev, tc_valid, tc_size, tc_offset);
}

void hclge_rss_indir_init_cfg(struct hclge_dev *hdev)
{
	struct hclge_vport *vport = hdev->vport;
	int i, j;

	for (j = 0; j < hdev->num_vmdq_vport + 1; j++) {
		for (i = 0; i < HCLGE_RSS_IND_TBL_SIZE; i++)
			vport[j].rss_indirection_tbl[i] =
				i % vport[j].alloc_rss_size;
	}
}

static void hclge_rss_init_cfg(struct hclge_dev *hdev)
{
	struct hclge_vport *vport = hdev->vport;
	int i;

	for (i = 0; i < hdev->num_vmdq_vport + 1; i++) {
		vport[i].rss_tuple_sets.ipv4_tcp_en =
			HCLGE_RSS_INPUT_TUPLE_OTHER;
		vport[i].rss_tuple_sets.ipv4_udp_en =
			HCLGE_RSS_INPUT_TUPLE_OTHER;
		vport[i].rss_tuple_sets.ipv4_sctp_en =
			HCLGE_RSS_INPUT_TUPLE_SCTP;
		vport[i].rss_tuple_sets.ipv4_fragment_en =
			HCLGE_RSS_INPUT_TUPLE_OTHER;
		vport[i].rss_tuple_sets.ipv6_tcp_en =
			HCLGE_RSS_INPUT_TUPLE_OTHER;
		vport[i].rss_tuple_sets.ipv6_udp_en =
			HCLGE_RSS_INPUT_TUPLE_OTHER;
		vport[i].rss_tuple_sets.ipv6_sctp_en =
			HCLGE_RSS_INPUT_TUPLE_SCTP;
		vport[i].rss_tuple_sets.ipv6_fragment_en =
			HCLGE_RSS_INPUT_TUPLE_OTHER;

		vport[i].rss_algo = HCLGE_RSS_HASH_ALGO_TOEPLITZ;

		netdev_rss_key_fill(vport[i].rss_hash_key, HCLGE_RSS_KEY_SIZE);
	}

	hclge_rss_indir_init_cfg(hdev);
}

int hclge_bind_ring_with_vector(struct hclge_vport *vport,
				int vector_id, bool en,
				struct hnae3_ring_chain_node *ring_chain)
{
	struct hclge_dev *hdev = vport->back;
	struct hnae3_ring_chain_node *node;
	struct hclge_desc desc;
	struct hclge_ctrl_vector_chain_cmd *req
		= (struct hclge_ctrl_vector_chain_cmd *)desc.data;
	enum hclge_cmd_status status;
	enum hclge_opcode_type op;
	u16 tqp_type_and_id;
	int i;

	op = en ? HCLGE_OPC_ADD_RING_TO_VECTOR : HCLGE_OPC_DEL_RING_TO_VECTOR;
	hclge_cmd_setup_basic_desc(&desc, op, false);
	req->int_vector_id = vector_id;

	i = 0;
	for (node = ring_chain; node; node = node->next) {
		tqp_type_and_id = le16_to_cpu(req->tqp_type_and_id[i]);
		hnae3_set_field(tqp_type_and_id,  HCLGE_INT_TYPE_M,
				HCLGE_INT_TYPE_S,
				hnae3_get_bit(node->flag, HNAE3_RING_TYPE_B));
		hnae3_set_field(tqp_type_and_id, HCLGE_TQP_ID_M,
				HCLGE_TQP_ID_S, node->tqp_index);
		hnae3_set_field(tqp_type_and_id, HCLGE_INT_GL_IDX_M,
				HCLGE_INT_GL_IDX_S,
				hnae3_get_field(node->int_gl_idx,
						HNAE3_RING_GL_IDX_M,
						HNAE3_RING_GL_IDX_S));
		req->tqp_type_and_id[i] = cpu_to_le16(tqp_type_and_id);
		if (++i >= HCLGE_VECTOR_ELEMENTS_PER_CMD) {
			req->int_cause_num = HCLGE_VECTOR_ELEMENTS_PER_CMD;
			req->vfid = vport->vport_id;

			status = hclge_cmd_send(&hdev->hw, &desc, 1);
			if (status) {
				dev_err(&hdev->pdev->dev,
					"Map TQP fail, status is %d.\n",
					status);
				return -EIO;
			}
			i = 0;

			hclge_cmd_setup_basic_desc(&desc,
						   op,
						   false);
			req->int_vector_id = vector_id;
		}
	}

	if (i > 0) {
		req->int_cause_num = i;
		req->vfid = vport->vport_id;
		status = hclge_cmd_send(&hdev->hw, &desc, 1);
		if (status) {
			dev_err(&hdev->pdev->dev,
				"Map TQP fail, status is %d.\n", status);
			return -EIO;
		}
	}

	return 0;
}

static int hclge_map_ring_to_vector(struct hnae3_handle *handle,
				    int vector,
				    struct hnae3_ring_chain_node *ring_chain)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;
	int vector_id;

	vector_id = hclge_get_vector_index(hdev, vector);
	if (vector_id < 0) {
		dev_err(&hdev->pdev->dev,
			"Get vector index fail. vector_id =%d\n", vector_id);
		return vector_id;
	}

	return hclge_bind_ring_with_vector(vport, vector_id, true, ring_chain);
}

static int hclge_unmap_ring_frm_vector(struct hnae3_handle *handle,
				       int vector,
				       struct hnae3_ring_chain_node *ring_chain)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;
	int vector_id, ret;

	if (test_bit(HCLGE_STATE_RST_HANDLING, &hdev->state))
		return 0;

	vector_id = hclge_get_vector_index(hdev, vector);
	if (vector_id < 0) {
		dev_err(&handle->pdev->dev,
			"Get vector index fail. ret =%d\n", vector_id);
		return vector_id;
	}

	ret = hclge_bind_ring_with_vector(vport, vector_id, false, ring_chain);
	if (ret)
		dev_err(&handle->pdev->dev,
			"Unmap ring from vector fail. vectorid=%d, ret =%d\n",
			vector_id,
			ret);

	return ret;
}

int hclge_cmd_set_promisc_mode(struct hclge_dev *hdev,
			       struct hclge_promisc_param *param)
{
	struct hclge_promisc_cfg_cmd *req;
	struct hclge_desc desc;
	int ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_CFG_PROMISC_MODE, false);

	req = (struct hclge_promisc_cfg_cmd *)desc.data;
	req->vf_id = param->vf_id;

	/* HCLGE_PROMISC_TX_EN_B and HCLGE_PROMISC_RX_EN_B are not supported on
	 * pdev revision(0x20), new revision support them. The
	 * value of this two fields will not return error when driver
	 * send command to fireware in revision(0x20).
	 */
	req->flag = (param->enable << HCLGE_PROMISC_EN_B) |
		HCLGE_PROMISC_TX_EN_B | HCLGE_PROMISC_RX_EN_B;

	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		dev_err(&hdev->pdev->dev,
			"Set promisc mode fail, status is %d.\n", ret);

	return ret;
}

void hclge_promisc_param_init(struct hclge_promisc_param *param, bool en_uc,
			      bool en_mc, bool en_bc, int vport_id)
{
	if (!param)
		return;

	memset(param, 0, sizeof(struct hclge_promisc_param));
	if (en_uc)
		param->enable = HCLGE_PROMISC_EN_UC;
	if (en_mc)
		param->enable |= HCLGE_PROMISC_EN_MC;
	if (en_bc)
		param->enable |= HCLGE_PROMISC_EN_BC;
	param->vf_id = vport_id;
}

static void hclge_set_promisc_mode(struct hnae3_handle *handle, bool en_uc_pmc,
				   bool en_mc_pmc)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;
	struct hclge_promisc_param param;

	hclge_promisc_param_init(&param, en_uc_pmc, en_mc_pmc, true,
				 vport->vport_id);
	hclge_cmd_set_promisc_mode(hdev, &param);
}

static void hclge_cfg_mac_mode(struct hclge_dev *hdev, bool enable)
{
	struct hclge_desc desc;
	struct hclge_config_mac_mode_cmd *req =
		(struct hclge_config_mac_mode_cmd *)desc.data;
	u32 loop_en = 0;
	int ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_CONFIG_MAC_MODE, false);
	hnae3_set_bit(loop_en, HCLGE_MAC_TX_EN_B, enable);
	hnae3_set_bit(loop_en, HCLGE_MAC_RX_EN_B, enable);
	hnae3_set_bit(loop_en, HCLGE_MAC_PAD_TX_B, enable);
	hnae3_set_bit(loop_en, HCLGE_MAC_PAD_RX_B, enable);
	hnae3_set_bit(loop_en, HCLGE_MAC_1588_TX_B, 0);
	hnae3_set_bit(loop_en, HCLGE_MAC_1588_RX_B, 0);
	hnae3_set_bit(loop_en, HCLGE_MAC_APP_LP_B, 0);
	hnae3_set_bit(loop_en, HCLGE_MAC_LINE_LP_B, 0);
	hnae3_set_bit(loop_en, HCLGE_MAC_FCS_TX_B, enable);
	hnae3_set_bit(loop_en, HCLGE_MAC_RX_FCS_B, enable);
	hnae3_set_bit(loop_en, HCLGE_MAC_RX_FCS_STRIP_B, enable);
	hnae3_set_bit(loop_en, HCLGE_MAC_TX_OVERSIZE_TRUNCATE_B, enable);
	hnae3_set_bit(loop_en, HCLGE_MAC_RX_OVERSIZE_TRUNCATE_B, enable);
	hnae3_set_bit(loop_en, HCLGE_MAC_TX_UNDER_MIN_ERR_B, enable);
	req->txrx_pad_fcs_loop_en = cpu_to_le32(loop_en);

	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		dev_err(&hdev->pdev->dev,
			"mac enable fail, ret =%d.\n", ret);
}

static int hclge_set_mac_loopback(struct hclge_dev *hdev, bool en)
{
	struct hclge_config_mac_mode_cmd *req;
	struct hclge_desc desc;
	u32 loop_en;
	int ret;

	req = (struct hclge_config_mac_mode_cmd *)&desc.data[0];
	/* 1 Read out the MAC mode config at first */
	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_CONFIG_MAC_MODE, true);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"mac loopback get fail, ret =%d.\n", ret);
		return ret;
	}

	/* 2 Then setup the loopback flag */
	loop_en = le32_to_cpu(req->txrx_pad_fcs_loop_en);
	hnae3_set_bit(loop_en, HCLGE_MAC_APP_LP_B, en ? 1 : 0);
	hnae3_set_bit(loop_en, HCLGE_MAC_TX_EN_B, en ? 1 : 0);
	hnae3_set_bit(loop_en, HCLGE_MAC_RX_EN_B, en ? 1 : 0);

	req->txrx_pad_fcs_loop_en = cpu_to_le32(loop_en);

	/* 3 Config mac work mode with loopback flag
	 * and its original configure parameters
	 */
	hclge_cmd_reuse_desc(&desc, false);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		dev_err(&hdev->pdev->dev,
			"mac loopback set fail, ret =%d.\n", ret);
	return ret;
}

static int hclge_set_serdes_loopback(struct hclge_dev *hdev, bool en)
{
#define HCLGE_SERDES_RETRY_MS	10
#define HCLGE_SERDES_RETRY_NUM	100
	struct hclge_serdes_lb_cmd *req;
	struct hclge_desc desc;
	int ret, i = 0;

	req = (struct hclge_serdes_lb_cmd *)&desc.data[0];
	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_SERDES_LOOPBACK, false);

	if (en) {
		req->enable = HCLGE_CMD_SERDES_SERIAL_INNER_LOOP_B;
		req->mask = HCLGE_CMD_SERDES_SERIAL_INNER_LOOP_B;
	} else {
		req->mask = HCLGE_CMD_SERDES_SERIAL_INNER_LOOP_B;
	}

	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"serdes loopback set fail, ret = %d\n", ret);
		return ret;
	}

	do {
		msleep(HCLGE_SERDES_RETRY_MS);
		hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_SERDES_LOOPBACK,
					   true);
		ret = hclge_cmd_send(&hdev->hw, &desc, 1);
		if (ret) {
			dev_err(&hdev->pdev->dev,
				"serdes loopback get, ret = %d\n", ret);
			return ret;
		}
	} while (++i < HCLGE_SERDES_RETRY_NUM &&
		 !(req->result & HCLGE_CMD_SERDES_DONE_B));

	if (!(req->result & HCLGE_CMD_SERDES_DONE_B)) {
		dev_err(&hdev->pdev->dev, "serdes loopback set timeout\n");
		return -EBUSY;
	} else if (!(req->result & HCLGE_CMD_SERDES_SUCCESS_B)) {
		dev_err(&hdev->pdev->dev, "serdes loopback set failed in fw\n");
		return -EIO;
	}

	hclge_cfg_mac_mode(hdev, en);
	return 0;
}

static int hclge_tqp_enable(struct hclge_dev *hdev, int tqp_id,
			    int stream_id, bool enable)
{
	struct hclge_desc desc;
	struct hclge_cfg_com_tqp_queue_cmd *req =
		(struct hclge_cfg_com_tqp_queue_cmd *)desc.data;
	int ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_CFG_COM_TQP_QUEUE, false);
	req->tqp_id = cpu_to_le16(tqp_id & HCLGE_RING_ID_MASK);
	req->stream_id = cpu_to_le16(stream_id);
	req->enable |= enable << HCLGE_TQP_ENABLE_B;

	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		dev_err(&hdev->pdev->dev,
			"Tqp enable fail, status =%d.\n", ret);
	return ret;
}

static int hclge_set_loopback(struct hnae3_handle *handle,
			      enum hnae3_loop loop_mode, bool en)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;
	int i, ret;

	switch (loop_mode) {
	case HNAE3_MAC_INTER_LOOP_MAC:
		ret = hclge_set_mac_loopback(hdev, en);
		break;
	case HNAE3_MAC_INTER_LOOP_SERDES:
		ret = hclge_set_serdes_loopback(hdev, en);
		break;
	default:
		ret = -ENOTSUPP;
		dev_err(&hdev->pdev->dev,
			"loop_mode %d is not supported\n", loop_mode);
		break;
	}

	for (i = 0; i < vport->alloc_tqps; i++) {
		ret = hclge_tqp_enable(hdev, i, 0, en);
		if (ret)
			return ret;
	}

	return 0;
}

static void hclge_reset_tqp_stats(struct hnae3_handle *handle)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hnae3_queue *queue;
	struct hclge_tqp *tqp;
	int i;

	for (i = 0; i < vport->alloc_tqps; i++) {
		queue = handle->kinfo.tqp[i];
		tqp = container_of(queue, struct hclge_tqp, q);
		memset(&tqp->tqp_stats, 0, sizeof(tqp->tqp_stats));
	}
}

static int hclge_ae_start(struct hnae3_handle *handle)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;
	int i;

	for (i = 0; i < vport->alloc_tqps; i++)
		hclge_tqp_enable(hdev, i, 0, true);

	/* mac enable */
	hclge_cfg_mac_mode(hdev, true);
	clear_bit(HCLGE_STATE_DOWN, &hdev->state);
	mod_timer(&hdev->service_timer, jiffies + HZ);
	hdev->hw.mac.link = 0;

	/* reset tqp stats */
	hclge_reset_tqp_stats(handle);

	hclge_mac_start_phy(hdev);

	return 0;
}

static void hclge_ae_stop(struct hnae3_handle *handle)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;
	int i;

	set_bit(HCLGE_STATE_DOWN, &hdev->state);

	del_timer_sync(&hdev->service_timer);
	cancel_work_sync(&hdev->service_task);
	clear_bit(HCLGE_STATE_SERVICE_SCHED, &hdev->state);

	if (test_bit(HCLGE_STATE_RST_HANDLING, &hdev->state)) {
		hclge_mac_stop_phy(hdev);
		return;
	}

	for (i = 0; i < vport->alloc_tqps; i++)
		hclge_tqp_enable(hdev, i, 0, false);

	/* Mac disable */
	hclge_cfg_mac_mode(hdev, false);

	hclge_mac_stop_phy(hdev);

	/* reset tqp stats */
	hclge_reset_tqp_stats(handle);
	del_timer_sync(&hdev->service_timer);
	cancel_work_sync(&hdev->service_task);
	hclge_update_link_status(hdev);
}

static int hclge_get_mac_vlan_cmd_status(struct hclge_vport *vport,
					 u16 cmdq_resp, u8  resp_code,
					 enum hclge_mac_vlan_tbl_opcode op)
{
	struct hclge_dev *hdev = vport->back;
	int return_status = -EIO;

	if (cmdq_resp) {
		dev_err(&hdev->pdev->dev,
			"cmdq execute failed for get_mac_vlan_cmd_status,status=%d.\n",
			cmdq_resp);
		return -EIO;
	}

	if (op == HCLGE_MAC_VLAN_ADD) {
		if ((!resp_code) || (resp_code == 1)) {
			return_status = 0;
		} else if (resp_code == 2) {
			return_status = -ENOSPC;
			dev_err(&hdev->pdev->dev,
				"add mac addr failed for uc_overflow.\n");
		} else if (resp_code == 3) {
			return_status = -ENOSPC;
			dev_err(&hdev->pdev->dev,
				"add mac addr failed for mc_overflow.\n");
		} else {
			dev_err(&hdev->pdev->dev,
				"add mac addr failed for undefined, code=%d.\n",
				resp_code);
		}
	} else if (op == HCLGE_MAC_VLAN_REMOVE) {
		if (!resp_code) {
			return_status = 0;
		} else if (resp_code == 1) {
			return_status = -ENOENT;
			dev_dbg(&hdev->pdev->dev,
				"remove mac addr failed for miss.\n");
		} else {
			dev_err(&hdev->pdev->dev,
				"remove mac addr failed for undefined, code=%d.\n",
				resp_code);
		}
	} else if (op == HCLGE_MAC_VLAN_LKUP) {
		if (!resp_code) {
			return_status = 0;
		} else if (resp_code == 1) {
			return_status = -ENOENT;
			dev_dbg(&hdev->pdev->dev,
				"lookup mac addr failed for miss.\n");
		} else {
			dev_err(&hdev->pdev->dev,
				"lookup mac addr failed for undefined, code=%d.\n",
				resp_code);
		}
	} else {
		return_status = -EINVAL;
		dev_err(&hdev->pdev->dev,
			"unknown opcode for get_mac_vlan_cmd_status,opcode=%d.\n",
			op);
	}

	return return_status;
}

static int hclge_update_desc_vfid(struct hclge_desc *desc, int vfid, bool clr)
{
	int word_num;
	int bit_num;

	if (vfid > 255 || vfid < 0)
		return -EIO;

	if (vfid >= 0 && vfid <= 191) {
		word_num = vfid / 32;
		bit_num  = vfid % 32;
		if (clr)
			desc[1].data[word_num] &= cpu_to_le32(~(1 << bit_num));
		else
			desc[1].data[word_num] |= cpu_to_le32(1 << bit_num);
	} else {
		word_num = (vfid - 192) / 32;
		bit_num  = vfid % 32;
		if (clr)
			desc[2].data[word_num] &= cpu_to_le32(~(1 << bit_num));
		else
			desc[2].data[word_num] |= cpu_to_le32(1 << bit_num);
	}

	return 0;
}

static bool hclge_is_all_function_id_zero(struct hclge_desc *desc)
{
#define HCLGE_DESC_NUMBER 3
#define HCLGE_FUNC_NUMBER_PER_DESC 6
	int i, j;

	for (i = 1; i < HCLGE_DESC_NUMBER; i++)
		for (j = 0; j < HCLGE_FUNC_NUMBER_PER_DESC; j++)
			if (desc[i].data[j])
				return false;

	return true;
}

static void hclge_prepare_mac_addr(struct hclge_mac_vlan_tbl_entry_cmd *new_req,
				   const u8 *addr)
{
	const unsigned char *mac_addr = addr;
	u32 high_val = mac_addr[2] << 16 | (mac_addr[3] << 24) |
		       (mac_addr[0]) | (mac_addr[1] << 8);
	u32 low_val  = mac_addr[4] | (mac_addr[5] << 8);

	new_req->mac_addr_hi32 = cpu_to_le32(high_val);
	new_req->mac_addr_lo16 = cpu_to_le16(low_val & 0xffff);
}

static u16 hclge_get_mac_addr_to_mta_index(struct hclge_vport *vport,
					   const u8 *addr)
{
	u16 high_val = addr[1] | (addr[0] << 8);
	struct hclge_dev *hdev = vport->back;
	u32 rsh = 4 - hdev->mta_mac_sel_type;
	u16 ret_val = (high_val >> rsh) & 0xfff;

	return ret_val;
}

static int hclge_set_mta_filter_mode(struct hclge_dev *hdev,
				     enum hclge_mta_dmac_sel_type mta_mac_sel,
				     bool enable)
{
	struct hclge_mta_filter_mode_cmd *req;
	struct hclge_desc desc;
	int ret;

	req = (struct hclge_mta_filter_mode_cmd *)desc.data;
	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_MTA_MAC_MODE_CFG, false);

	hnae3_set_bit(req->dmac_sel_en, HCLGE_CFG_MTA_MAC_EN_B,
		      enable);
	hnae3_set_field(req->dmac_sel_en, HCLGE_CFG_MTA_MAC_SEL_M,
			HCLGE_CFG_MTA_MAC_SEL_S, mta_mac_sel);

	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		dev_err(&hdev->pdev->dev,
			"Config mat filter mode failed for cmd_send, ret =%d.\n",
			ret);

	return ret;
}

int hclge_cfg_func_mta_filter(struct hclge_dev *hdev,
			      u8 func_id,
			      bool enable)
{
	struct hclge_cfg_func_mta_filter_cmd *req;
	struct hclge_desc desc;
	int ret;

	req = (struct hclge_cfg_func_mta_filter_cmd *)desc.data;
	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_MTA_MAC_FUNC_CFG, false);

	hnae3_set_bit(req->accept, HCLGE_CFG_FUNC_MTA_ACCEPT_B,
		      enable);
	req->function_id = func_id;

	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		dev_err(&hdev->pdev->dev,
			"Config func_id enable failed for cmd_send, ret =%d.\n",
			ret);

	return ret;
}

static int hclge_set_mta_table_item(struct hclge_vport *vport,
				    u16 idx,
				    bool enable)
{
	struct hclge_dev *hdev = vport->back;
	struct hclge_cfg_func_mta_item_cmd *req;
	struct hclge_desc desc;
	u16 item_idx = 0;
	int ret;

	req = (struct hclge_cfg_func_mta_item_cmd *)desc.data;
	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_MTA_TBL_ITEM_CFG, false);
	hnae3_set_bit(req->accept, HCLGE_CFG_MTA_ITEM_ACCEPT_B, enable);

	hnae3_set_field(item_idx, HCLGE_CFG_MTA_ITEM_IDX_M,
			HCLGE_CFG_MTA_ITEM_IDX_S, idx);
	req->item_idx = cpu_to_le16(item_idx);

	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"Config mta table item failed for cmd_send, ret =%d.\n",
			ret);
		return ret;
	}

	if (enable)
		set_bit(idx, vport->mta_shadow);
	else
		clear_bit(idx, vport->mta_shadow);

	return 0;
}

static int hclge_update_mta_status(struct hnae3_handle *handle)
{
	unsigned long mta_status[BITS_TO_LONGS(HCLGE_MTA_TBL_SIZE)];
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct net_device *netdev = handle->kinfo.netdev;
	struct netdev_hw_addr *ha;
	u16 tbl_idx;

	memset(mta_status, 0, sizeof(mta_status));

	/* update mta_status from mc addr list */
	netdev_for_each_mc_addr(ha, netdev) {
		tbl_idx = hclge_get_mac_addr_to_mta_index(vport, ha->addr);
		set_bit(tbl_idx, mta_status);
	}

	return hclge_update_mta_status_common(vport, mta_status,
					0, HCLGE_MTA_TBL_SIZE, true);
}

int hclge_update_mta_status_common(struct hclge_vport *vport,
				   unsigned long *status,
				   u16 idx,
				   u16 count,
				   bool update_filter)
{
	struct hclge_dev *hdev = vport->back;
	u16 update_max = idx + count;
	u16 check_max;
	int ret = 0;
	bool used;
	u16 i;

	/* setup mta check range */
	if (update_filter) {
		i = 0;
		check_max = HCLGE_MTA_TBL_SIZE;
	} else {
		i = idx;
		check_max = update_max;
	}

	used = false;
	/* check and update all mta item */
	for (; i < check_max; i++) {
		/* ignore unused item */
		if (!test_bit(i, vport->mta_shadow))
			continue;

		/* if i in update range then update it */
		if (i >= idx && i < update_max)
			if (!test_bit(i - idx, status))
				hclge_set_mta_table_item(vport, i, false);

		if (!used && test_bit(i, vport->mta_shadow))
			used = true;
	}

	/* no longer use mta, disable it */
	if (vport->accept_mta_mc && update_filter && !used) {
		ret = hclge_cfg_func_mta_filter(hdev,
						vport->vport_id,
						false);
		if (ret)
			dev_err(&hdev->pdev->dev,
				"disable func mta filter fail ret=%d\n",
				ret);
		else
			vport->accept_mta_mc = false;
	}

	return ret;
}

static int hclge_remove_mac_vlan_tbl(struct hclge_vport *vport,
				     struct hclge_mac_vlan_tbl_entry_cmd *req)
{
	struct hclge_dev *hdev = vport->back;
	struct hclge_desc desc;
	u8 resp_code;
	u16 retval;
	int ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_MAC_VLAN_REMOVE, false);

	memcpy(desc.data, req, sizeof(struct hclge_mac_vlan_tbl_entry_cmd));

	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"del mac addr failed for cmd_send, ret =%d.\n",
			ret);
		return ret;
	}
	resp_code = (le32_to_cpu(desc.data[0]) >> 8) & 0xff;
	retval = le16_to_cpu(desc.retval);

	return hclge_get_mac_vlan_cmd_status(vport, retval, resp_code,
					     HCLGE_MAC_VLAN_REMOVE);
}

static int hclge_lookup_mac_vlan_tbl(struct hclge_vport *vport,
				     struct hclge_mac_vlan_tbl_entry_cmd *req,
				     struct hclge_desc *desc,
				     bool is_mc)
{
	struct hclge_dev *hdev = vport->back;
	u8 resp_code;
	u16 retval;
	int ret;

	hclge_cmd_setup_basic_desc(&desc[0], HCLGE_OPC_MAC_VLAN_ADD, true);
	if (is_mc) {
		desc[0].flag |= cpu_to_le16(HCLGE_CMD_FLAG_NEXT);
		memcpy(desc[0].data,
		       req,
		       sizeof(struct hclge_mac_vlan_tbl_entry_cmd));
		hclge_cmd_setup_basic_desc(&desc[1],
					   HCLGE_OPC_MAC_VLAN_ADD,
					   true);
		desc[1].flag |= cpu_to_le16(HCLGE_CMD_FLAG_NEXT);
		hclge_cmd_setup_basic_desc(&desc[2],
					   HCLGE_OPC_MAC_VLAN_ADD,
					   true);
		ret = hclge_cmd_send(&hdev->hw, desc, 3);
	} else {
		memcpy(desc[0].data,
		       req,
		       sizeof(struct hclge_mac_vlan_tbl_entry_cmd));
		ret = hclge_cmd_send(&hdev->hw, desc, 1);
	}
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"lookup mac addr failed for cmd_send, ret =%d.\n",
			ret);
		return ret;
	}
	resp_code = (le32_to_cpu(desc[0].data[0]) >> 8) & 0xff;
	retval = le16_to_cpu(desc[0].retval);

	return hclge_get_mac_vlan_cmd_status(vport, retval, resp_code,
					     HCLGE_MAC_VLAN_LKUP);
}

static int hclge_add_mac_vlan_tbl(struct hclge_vport *vport,
				  struct hclge_mac_vlan_tbl_entry_cmd *req,
				  struct hclge_desc *mc_desc)
{
	struct hclge_dev *hdev = vport->back;
	int cfg_status;
	u8 resp_code;
	u16 retval;
	int ret;

	if (!mc_desc) {
		struct hclge_desc desc;

		hclge_cmd_setup_basic_desc(&desc,
					   HCLGE_OPC_MAC_VLAN_ADD,
					   false);
		memcpy(desc.data, req,
		       sizeof(struct hclge_mac_vlan_tbl_entry_cmd));
		ret = hclge_cmd_send(&hdev->hw, &desc, 1);
		resp_code = (le32_to_cpu(desc.data[0]) >> 8) & 0xff;
		retval = le16_to_cpu(desc.retval);

		cfg_status = hclge_get_mac_vlan_cmd_status(vport, retval,
							   resp_code,
							   HCLGE_MAC_VLAN_ADD);
	} else {
		hclge_cmd_reuse_desc(&mc_desc[0], false);
		mc_desc[0].flag |= cpu_to_le16(HCLGE_CMD_FLAG_NEXT);
		hclge_cmd_reuse_desc(&mc_desc[1], false);
		mc_desc[1].flag |= cpu_to_le16(HCLGE_CMD_FLAG_NEXT);
		hclge_cmd_reuse_desc(&mc_desc[2], false);
		mc_desc[2].flag &= cpu_to_le16(~HCLGE_CMD_FLAG_NEXT);
		memcpy(mc_desc[0].data, req,
		       sizeof(struct hclge_mac_vlan_tbl_entry_cmd));
		ret = hclge_cmd_send(&hdev->hw, mc_desc, 3);
		resp_code = (le32_to_cpu(mc_desc[0].data[0]) >> 8) & 0xff;
		retval = le16_to_cpu(mc_desc[0].retval);

		cfg_status = hclge_get_mac_vlan_cmd_status(vport, retval,
							   resp_code,
							   HCLGE_MAC_VLAN_ADD);
	}

	if (ret) {
		dev_err(&hdev->pdev->dev,
			"add mac addr failed for cmd_send, ret =%d.\n",
			ret);
		return ret;
	}

	return cfg_status;
}

static int hclge_add_uc_addr(struct hnae3_handle *handle,
			     const unsigned char *addr)
{
	struct hclge_vport *vport = hclge_get_vport(handle);

	return hclge_add_uc_addr_common(vport, addr);
}

int hclge_add_uc_addr_common(struct hclge_vport *vport,
			     const unsigned char *addr)
{
	struct hclge_dev *hdev = vport->back;
	struct hclge_mac_vlan_tbl_entry_cmd req;
	struct hclge_desc desc;
	u16 egress_port = 0;
	int ret;

	/* mac addr check */
	if (is_zero_ether_addr(addr) ||
	    is_broadcast_ether_addr(addr) ||
	    is_multicast_ether_addr(addr)) {
		dev_err(&hdev->pdev->dev,
			"Set_uc mac err! invalid mac:%pM. is_zero:%d,is_br=%d,is_mul=%d\n",
			 addr,
			 is_zero_ether_addr(addr),
			 is_broadcast_ether_addr(addr),
			 is_multicast_ether_addr(addr));
		return -EINVAL;
	}

	memset(&req, 0, sizeof(req));
	hnae3_set_bit(req.flags, HCLGE_MAC_VLAN_BIT0_EN_B, 1);

	hnae3_set_field(egress_port, HCLGE_MAC_EPORT_VFID_M,
			HCLGE_MAC_EPORT_VFID_S, vport->vport_id);

	req.egress_port = cpu_to_le16(egress_port);

	hclge_prepare_mac_addr(&req, addr);

	/* Lookup the mac address in the mac_vlan table, and add
	 * it if the entry is inexistent. Repeated unicast entry
	 * is not allowed in the mac vlan table.
	 */
	ret = hclge_lookup_mac_vlan_tbl(vport, &req, &desc, false);
	if (ret == -ENOENT)
		return hclge_add_mac_vlan_tbl(vport, &req, NULL);

	/* check if we just hit the duplicate */
	if (!ret) {
		dev_warn(&hdev->pdev->dev, "VF %d mac(%pM) exists\n",
			 vport->vport_id, addr);
		return 0;
	}

	dev_err(&hdev->pdev->dev,
		"PF failed to add unicast entry(%pM) in the MAC table\n",
		addr);

	return ret;
}

static int hclge_rm_uc_addr(struct hnae3_handle *handle,
			    const unsigned char *addr)
{
	struct hclge_vport *vport = hclge_get_vport(handle);

	return hclge_rm_uc_addr_common(vport, addr);
}

int hclge_rm_uc_addr_common(struct hclge_vport *vport,
			    const unsigned char *addr)
{
	struct hclge_dev *hdev = vport->back;
	struct hclge_mac_vlan_tbl_entry_cmd req;
	int ret;

	/* mac addr check */
	if (is_zero_ether_addr(addr) ||
	    is_broadcast_ether_addr(addr) ||
	    is_multicast_ether_addr(addr)) {
		dev_dbg(&hdev->pdev->dev,
			"Remove mac err! invalid mac:%pM.\n",
			 addr);
		return -EINVAL;
	}

	memset(&req, 0, sizeof(req));
	hnae3_set_bit(req.flags, HCLGE_MAC_VLAN_BIT0_EN_B, 1);
	hnae3_set_bit(req.entry_type, HCLGE_MAC_VLAN_BIT0_EN_B, 0);
	hclge_prepare_mac_addr(&req, addr);
	ret = hclge_remove_mac_vlan_tbl(vport, &req);

	return ret;
}

static int hclge_add_mc_addr(struct hnae3_handle *handle,
			     const unsigned char *addr)
{
	struct hclge_vport *vport = hclge_get_vport(handle);

	return hclge_add_mc_addr_common(vport, addr);
}

int hclge_add_mc_addr_common(struct hclge_vport *vport,
			     const unsigned char *addr)
{
	struct hclge_dev *hdev = vport->back;
	struct hclge_mac_vlan_tbl_entry_cmd req;
	struct hclge_desc desc[3];
	u16 tbl_idx;
	int status;

	/* mac addr check */
	if (!is_multicast_ether_addr(addr)) {
		dev_err(&hdev->pdev->dev,
			"Add mc mac err! invalid mac:%pM.\n",
			 addr);
		return -EINVAL;
	}
	memset(&req, 0, sizeof(req));
	hnae3_set_bit(req.flags, HCLGE_MAC_VLAN_BIT0_EN_B, 1);
	hnae3_set_bit(req.entry_type, HCLGE_MAC_VLAN_BIT0_EN_B, 0);
	hnae3_set_bit(req.entry_type, HCLGE_MAC_VLAN_BIT1_EN_B, 1);
	hnae3_set_bit(req.mc_mac_en, HCLGE_MAC_VLAN_BIT0_EN_B, 1);
	hclge_prepare_mac_addr(&req, addr);
	status = hclge_lookup_mac_vlan_tbl(vport, &req, desc, true);
	if (!status) {
		/* This mac addr exist, update VFID for it */
		hclge_update_desc_vfid(desc, vport->vport_id, false);
		status = hclge_add_mac_vlan_tbl(vport, &req, desc);
	} else {
		/* This mac addr do not exist, add new entry for it */
		memset(desc[0].data, 0, sizeof(desc[0].data));
		memset(desc[1].data, 0, sizeof(desc[0].data));
		memset(desc[2].data, 0, sizeof(desc[0].data));
		hclge_update_desc_vfid(desc, vport->vport_id, false);
		status = hclge_add_mac_vlan_tbl(vport, &req, desc);
	}

	/* If mc mac vlan table is full, use MTA table */
	if (status == -ENOSPC) {
		if (!vport->accept_mta_mc) {
			status = hclge_cfg_func_mta_filter(hdev,
							   vport->vport_id,
							   true);
			if (status) {
				dev_err(&hdev->pdev->dev,
					"set mta filter mode fail ret=%d\n",
					status);
				return status;
			}
			vport->accept_mta_mc = true;
		}

		/* Set MTA table for this MAC address */
		tbl_idx = hclge_get_mac_addr_to_mta_index(vport, addr);
		status = hclge_set_mta_table_item(vport, tbl_idx, true);
	}

	return status;
}

static int hclge_rm_mc_addr(struct hnae3_handle *handle,
			    const unsigned char *addr)
{
	struct hclge_vport *vport = hclge_get_vport(handle);

	return hclge_rm_mc_addr_common(vport, addr);
}

int hclge_rm_mc_addr_common(struct hclge_vport *vport,
			    const unsigned char *addr)
{
	struct hclge_dev *hdev = vport->back;
	struct hclge_mac_vlan_tbl_entry_cmd req;
	enum hclge_cmd_status status;
	struct hclge_desc desc[3];

	/* mac addr check */
	if (!is_multicast_ether_addr(addr)) {
		dev_dbg(&hdev->pdev->dev,
			"Remove mc mac err! invalid mac:%pM.\n",
			 addr);
		return -EINVAL;
	}

	memset(&req, 0, sizeof(req));
	hnae3_set_bit(req.flags, HCLGE_MAC_VLAN_BIT0_EN_B, 1);
	hnae3_set_bit(req.entry_type, HCLGE_MAC_VLAN_BIT0_EN_B, 0);
	hnae3_set_bit(req.entry_type, HCLGE_MAC_VLAN_BIT1_EN_B, 1);
	hnae3_set_bit(req.mc_mac_en, HCLGE_MAC_VLAN_BIT0_EN_B, 1);
	hclge_prepare_mac_addr(&req, addr);
	status = hclge_lookup_mac_vlan_tbl(vport, &req, desc, true);
	if (!status) {
		/* This mac addr exist, remove this handle's VFID for it */
		hclge_update_desc_vfid(desc, vport->vport_id, true);

		if (hclge_is_all_function_id_zero(desc))
			/* All the vfid is zero, so need to delete this entry */
			status = hclge_remove_mac_vlan_tbl(vport, &req);
		else
			/* Not all the vfid is zero, update the vfid */
			status = hclge_add_mac_vlan_tbl(vport, &req, desc);

	} else {
		/* Maybe this mac address is in mta table, but it cannot be
		 * deleted here because an entry of mta represents an address
		 * range rather than a specific address. the delete action to
		 * all entries will take effect in update_mta_status called by
		 * hns3_nic_set_rx_mode.
		 */
		status = 0;
	}

	return status;
}

static int hclge_get_mac_ethertype_cmd_status(struct hclge_dev *hdev,
					      u16 cmdq_resp, u8 resp_code)
{
#define HCLGE_ETHERTYPE_SUCCESS_ADD		0
#define HCLGE_ETHERTYPE_ALREADY_ADD		1
#define HCLGE_ETHERTYPE_MGR_TBL_OVERFLOW	2
#define HCLGE_ETHERTYPE_KEY_CONFLICT		3

	int return_status;

	if (cmdq_resp) {
		dev_err(&hdev->pdev->dev,
			"cmdq execute failed for get_mac_ethertype_cmd_status, status=%d.\n",
			cmdq_resp);
		return -EIO;
	}

	switch (resp_code) {
	case HCLGE_ETHERTYPE_SUCCESS_ADD:
	case HCLGE_ETHERTYPE_ALREADY_ADD:
		return_status = 0;
		break;
	case HCLGE_ETHERTYPE_MGR_TBL_OVERFLOW:
		dev_err(&hdev->pdev->dev,
			"add mac ethertype failed for manager table overflow.\n");
		return_status = -EIO;
		break;
	case HCLGE_ETHERTYPE_KEY_CONFLICT:
		dev_err(&hdev->pdev->dev,
			"add mac ethertype failed for key conflict.\n");
		return_status = -EIO;
		break;
	default:
		dev_err(&hdev->pdev->dev,
			"add mac ethertype failed for undefined, code=%d.\n",
			resp_code);
		return_status = -EIO;
	}

	return return_status;
}

static int hclge_add_mgr_tbl(struct hclge_dev *hdev,
			     const struct hclge_mac_mgr_tbl_entry_cmd *req)
{
	struct hclge_desc desc;
	u8 resp_code;
	u16 retval;
	int ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_MAC_ETHTYPE_ADD, false);
	memcpy(desc.data, req, sizeof(struct hclge_mac_mgr_tbl_entry_cmd));

	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"add mac ethertype failed for cmd_send, ret =%d.\n",
			ret);
		return ret;
	}

	resp_code = (le32_to_cpu(desc.data[0]) >> 8) & 0xff;
	retval = le16_to_cpu(desc.retval);

	return hclge_get_mac_ethertype_cmd_status(hdev, retval, resp_code);
}

static int init_mgr_tbl(struct hclge_dev *hdev)
{
	int ret;
	int i;

	for (i = 0; i < ARRAY_SIZE(hclge_mgr_table); i++) {
		ret = hclge_add_mgr_tbl(hdev, &hclge_mgr_table[i]);
		if (ret) {
			dev_err(&hdev->pdev->dev,
				"add mac ethertype failed, ret =%d.\n",
				ret);
			return ret;
		}
	}

	return 0;
}

static void hclge_get_mac_addr(struct hnae3_handle *handle, u8 *p)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;

	ether_addr_copy(p, hdev->hw.mac.mac_addr);
}

static int hclge_set_mac_addr(struct hnae3_handle *handle, void *p,
			      bool is_first)
{
	const unsigned char *new_addr = (const unsigned char *)p;
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;
	int ret;

	/* mac addr check */
	if (is_zero_ether_addr(new_addr) ||
	    is_broadcast_ether_addr(new_addr) ||
	    is_multicast_ether_addr(new_addr)) {
		dev_err(&hdev->pdev->dev,
			"Change uc mac err! invalid mac:%p.\n",
			 new_addr);
		return -EINVAL;
	}

	if (!is_first && hclge_rm_uc_addr(handle, hdev->hw.mac.mac_addr))
		dev_warn(&hdev->pdev->dev,
			 "remove old uc mac address fail.\n");

	ret = hclge_add_uc_addr(handle, new_addr);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"add uc mac address fail, ret =%d.\n",
			ret);

		if (!is_first &&
		    hclge_add_uc_addr(handle, hdev->hw.mac.mac_addr))
			dev_err(&hdev->pdev->dev,
				"restore uc mac address fail.\n");

		return -EIO;
	}

	ret = hclge_pause_addr_cfg(hdev, new_addr);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"configure mac pause address fail, ret =%d.\n",
			ret);
		return -EIO;
	}

	ether_addr_copy(hdev->hw.mac.mac_addr, new_addr);

	return 0;
}

static int hclge_set_vlan_filter_ctrl(struct hclge_dev *hdev, u8 vlan_type,
				      bool filter_en)
{
	struct hclge_vlan_filter_ctrl_cmd *req;
	struct hclge_desc desc;
	int ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_VLAN_FILTER_CTRL, false);

	req = (struct hclge_vlan_filter_ctrl_cmd *)desc.data;
	req->vlan_type = vlan_type;
	req->vlan_fe = filter_en;

	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		dev_err(&hdev->pdev->dev, "set vlan filter fail, ret =%d.\n",
			ret);

	return ret;
}

#define HCLGE_FILTER_TYPE_VF		0
#define HCLGE_FILTER_TYPE_PORT		1

static void hclge_enable_vlan_filter(struct hnae3_handle *handle, bool enable)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;

	hclge_set_vlan_filter_ctrl(hdev, HCLGE_FILTER_TYPE_VF, enable);
}

static int hclge_set_vf_vlan_common(struct hclge_dev *hdev, int vfid,
				    bool is_kill, u16 vlan, u8 qos,
				    __be16 proto)
{
#define HCLGE_MAX_VF_BYTES  16
	struct hclge_vlan_filter_vf_cfg_cmd *req0;
	struct hclge_vlan_filter_vf_cfg_cmd *req1;
	struct hclge_desc desc[2];
	u8 vf_byte_val;
	u8 vf_byte_off;
	int ret;

	hclge_cmd_setup_basic_desc(&desc[0],
				   HCLGE_OPC_VLAN_FILTER_VF_CFG, false);
	hclge_cmd_setup_basic_desc(&desc[1],
				   HCLGE_OPC_VLAN_FILTER_VF_CFG, false);

	desc[0].flag |= cpu_to_le16(HCLGE_CMD_FLAG_NEXT);

	vf_byte_off = vfid / 8;
	vf_byte_val = 1 << (vfid % 8);

	req0 = (struct hclge_vlan_filter_vf_cfg_cmd *)desc[0].data;
	req1 = (struct hclge_vlan_filter_vf_cfg_cmd *)desc[1].data;

	req0->vlan_id  = cpu_to_le16(vlan);
	req0->vlan_cfg = is_kill;

	if (vf_byte_off < HCLGE_MAX_VF_BYTES)
		req0->vf_bitmap[vf_byte_off] = vf_byte_val;
	else
		req1->vf_bitmap[vf_byte_off - HCLGE_MAX_VF_BYTES] = vf_byte_val;

	ret = hclge_cmd_send(&hdev->hw, desc, 2);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"Send vf vlan command fail, ret =%d.\n",
			ret);
		return ret;
	}

	if (!is_kill) {
#define HCLGE_VF_VLAN_NO_ENTRY	2
		if (!req0->resp_code || req0->resp_code == 1)
			return 0;

		if (req0->resp_code == HCLGE_VF_VLAN_NO_ENTRY) {
			dev_warn(&hdev->pdev->dev,
				 "vf vlan table is full, vf vlan filter is disabled\n");
			return 0;
		}

		dev_err(&hdev->pdev->dev,
			"Add vf vlan filter fail, ret =%d.\n",
			req0->resp_code);
	} else {
#define HCLGE_VF_VLAN_DEL_NO_FOUND	1
		if (!req0->resp_code)
			return 0;

		if (req0->resp_code == HCLGE_VF_VLAN_DEL_NO_FOUND) {
			dev_warn(&hdev->pdev->dev,
				 "vlan %d filter is not in vf vlan table\n",
				 vlan);
			return 0;
		}

		dev_err(&hdev->pdev->dev,
			"Kill vf vlan filter fail, ret =%d.\n",
			req0->resp_code);
	}

	return -EIO;
}

static int hclge_set_port_vlan_filter(struct hclge_dev *hdev, __be16 proto,
				      u16 vlan_id, bool is_kill)
{
	struct hclge_vlan_filter_pf_cfg_cmd *req;
	struct hclge_desc desc;
	u8 vlan_offset_byte_val;
	u8 vlan_offset_byte;
	u8 vlan_offset_160;
	int ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_VLAN_FILTER_PF_CFG, false);

	vlan_offset_160 = vlan_id / 160;
	vlan_offset_byte = (vlan_id % 160) / 8;
	vlan_offset_byte_val = 1 << (vlan_id % 8);

	req = (struct hclge_vlan_filter_pf_cfg_cmd *)desc.data;
	req->vlan_offset = vlan_offset_160;
	req->vlan_cfg = is_kill;
	req->vlan_offset_bitmap[vlan_offset_byte] = vlan_offset_byte_val;

	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		dev_err(&hdev->pdev->dev,
			"port vlan command, send fail, ret =%d.\n", ret);
	return ret;
}

static int hclge_set_vlan_filter_hw(struct hclge_dev *hdev, __be16 proto,
				    u16 vport_id, u16 vlan_id, u8 qos,
				    bool is_kill)
{
	u16 vport_idx, vport_num = 0;
	int ret;

	if (is_kill && !vlan_id)
		return 0;

	ret = hclge_set_vf_vlan_common(hdev, vport_id, is_kill, vlan_id,
				       0, proto);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"Set %d vport vlan filter config fail, ret =%d.\n",
			vport_id, ret);
		return ret;
	}

	/* vlan 0 may be added twice when 8021q module is enabled */
	if (!is_kill && !vlan_id &&
	    test_bit(vport_id, hdev->vlan_table[vlan_id]))
		return 0;

	if (!is_kill && test_and_set_bit(vport_id, hdev->vlan_table[vlan_id])) {
		dev_err(&hdev->pdev->dev,
			"Add port vlan failed, vport %d is already in vlan %d\n",
			vport_id, vlan_id);
		return -EINVAL;
	}

	if (is_kill &&
	    !test_and_clear_bit(vport_id, hdev->vlan_table[vlan_id])) {
		dev_err(&hdev->pdev->dev,
			"Delete port vlan failed, vport %d is not in vlan %d\n",
			vport_id, vlan_id);
		return -EINVAL;
	}

	for_each_set_bit(vport_idx, hdev->vlan_table[vlan_id], HCLGE_VPORT_NUM)
		vport_num++;

	if ((is_kill && vport_num == 0) || (!is_kill && vport_num == 1))
		ret = hclge_set_port_vlan_filter(hdev, proto, vlan_id,
						 is_kill);

	return ret;
}

int hclge_set_vlan_filter(struct hnae3_handle *handle, __be16 proto,
			  u16 vlan_id, bool is_kill)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;

	return hclge_set_vlan_filter_hw(hdev, proto, vport->vport_id, vlan_id,
					0, is_kill);
}

static int hclge_set_vf_vlan_filter(struct hnae3_handle *handle, int vfid,
				    u16 vlan, u8 qos, __be16 proto)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;

	if ((vfid >= hdev->num_alloc_vfs) || (vlan > 4095) || (qos > 7))
		return -EINVAL;
	if (proto != htons(ETH_P_8021Q))
		return -EPROTONOSUPPORT;

	return hclge_set_vlan_filter_hw(hdev, proto, vfid, vlan, qos, false);
}

static int hclge_set_vlan_tx_offload_cfg(struct hclge_vport *vport)
{
	struct hclge_tx_vtag_cfg *vcfg = &vport->txvlan_cfg;
	struct hclge_vport_vtag_tx_cfg_cmd *req;
	struct hclge_dev *hdev = vport->back;
	struct hclge_desc desc;
	int status;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_VLAN_PORT_TX_CFG, false);

	req = (struct hclge_vport_vtag_tx_cfg_cmd *)desc.data;
	req->def_vlan_tag1 = cpu_to_le16(vcfg->default_tag1);
	req->def_vlan_tag2 = cpu_to_le16(vcfg->default_tag2);
	hnae3_set_bit(req->vport_vlan_cfg, HCLGE_ACCEPT_TAG1_B,
		      vcfg->accept_tag1 ? 1 : 0);
	hnae3_set_bit(req->vport_vlan_cfg, HCLGE_ACCEPT_UNTAG1_B,
		      vcfg->accept_untag1 ? 1 : 0);
	hnae3_set_bit(req->vport_vlan_cfg, HCLGE_ACCEPT_TAG2_B,
		      vcfg->accept_tag2 ? 1 : 0);
	hnae3_set_bit(req->vport_vlan_cfg, HCLGE_ACCEPT_UNTAG2_B,
		      vcfg->accept_untag2 ? 1 : 0);
	hnae3_set_bit(req->vport_vlan_cfg, HCLGE_PORT_INS_TAG1_EN_B,
		      vcfg->insert_tag1_en ? 1 : 0);
	hnae3_set_bit(req->vport_vlan_cfg, HCLGE_PORT_INS_TAG2_EN_B,
		      vcfg->insert_tag2_en ? 1 : 0);
	hnae3_set_bit(req->vport_vlan_cfg, HCLGE_CFG_NIC_ROCE_SEL_B, 0);

	req->vf_offset = vport->vport_id / HCLGE_VF_NUM_PER_CMD;
	req->vf_bitmap[req->vf_offset] =
		1 << (vport->vport_id % HCLGE_VF_NUM_PER_BYTE);

	status = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (status)
		dev_err(&hdev->pdev->dev,
			"Send port txvlan cfg command fail, ret =%d\n",
			status);

	return status;
}

static int hclge_set_vlan_rx_offload_cfg(struct hclge_vport *vport)
{
	struct hclge_rx_vtag_cfg *vcfg = &vport->rxvlan_cfg;
	struct hclge_vport_vtag_rx_cfg_cmd *req;
	struct hclge_dev *hdev = vport->back;
	struct hclge_desc desc;
	int status;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_VLAN_PORT_RX_CFG, false);

	req = (struct hclge_vport_vtag_rx_cfg_cmd *)desc.data;
	hnae3_set_bit(req->vport_vlan_cfg, HCLGE_REM_TAG1_EN_B,
		      vcfg->strip_tag1_en ? 1 : 0);
	hnae3_set_bit(req->vport_vlan_cfg, HCLGE_REM_TAG2_EN_B,
		      vcfg->strip_tag2_en ? 1 : 0);
	hnae3_set_bit(req->vport_vlan_cfg, HCLGE_SHOW_TAG1_EN_B,
		      vcfg->vlan1_vlan_prionly ? 1 : 0);
	hnae3_set_bit(req->vport_vlan_cfg, HCLGE_SHOW_TAG2_EN_B,
		      vcfg->vlan2_vlan_prionly ? 1 : 0);

	req->vf_offset = vport->vport_id / HCLGE_VF_NUM_PER_CMD;
	req->vf_bitmap[req->vf_offset] =
		1 << (vport->vport_id % HCLGE_VF_NUM_PER_BYTE);

	status = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (status)
		dev_err(&hdev->pdev->dev,
			"Send port rxvlan cfg command fail, ret =%d\n",
			status);

	return status;
}

static int hclge_set_vlan_protocol_type(struct hclge_dev *hdev)
{
	struct hclge_rx_vlan_type_cfg_cmd *rx_req;
	struct hclge_tx_vlan_type_cfg_cmd *tx_req;
	struct hclge_desc desc;
	int status;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_MAC_VLAN_TYPE_ID, false);
	rx_req = (struct hclge_rx_vlan_type_cfg_cmd *)desc.data;
	rx_req->ot_fst_vlan_type =
		cpu_to_le16(hdev->vlan_type_cfg.rx_ot_fst_vlan_type);
	rx_req->ot_sec_vlan_type =
		cpu_to_le16(hdev->vlan_type_cfg.rx_ot_sec_vlan_type);
	rx_req->in_fst_vlan_type =
		cpu_to_le16(hdev->vlan_type_cfg.rx_in_fst_vlan_type);
	rx_req->in_sec_vlan_type =
		cpu_to_le16(hdev->vlan_type_cfg.rx_in_sec_vlan_type);

	status = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (status) {
		dev_err(&hdev->pdev->dev,
			"Send rxvlan protocol type command fail, ret =%d\n",
			status);
		return status;
	}

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_MAC_VLAN_INSERT, false);

	tx_req = (struct hclge_tx_vlan_type_cfg_cmd *)&desc.data;
	tx_req->ot_vlan_type = cpu_to_le16(hdev->vlan_type_cfg.tx_ot_vlan_type);
	tx_req->in_vlan_type = cpu_to_le16(hdev->vlan_type_cfg.tx_in_vlan_type);

	status = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (status)
		dev_err(&hdev->pdev->dev,
			"Send txvlan protocol type command fail, ret =%d\n",
			status);

	return status;
}

static int hclge_init_vlan_config(struct hclge_dev *hdev)
{
#define HCLGE_DEF_VLAN_TYPE		0x8100

	struct hnae3_handle *handle;
	struct hclge_vport *vport;
	int ret;
	int i;

	ret = hclge_set_vlan_filter_ctrl(hdev, HCLGE_FILTER_TYPE_VF, true);
	if (ret)
		return ret;

	ret = hclge_set_vlan_filter_ctrl(hdev, HCLGE_FILTER_TYPE_PORT, true);
	if (ret)
		return ret;

	hdev->vlan_type_cfg.rx_in_fst_vlan_type = HCLGE_DEF_VLAN_TYPE;
	hdev->vlan_type_cfg.rx_in_sec_vlan_type = HCLGE_DEF_VLAN_TYPE;
	hdev->vlan_type_cfg.rx_ot_fst_vlan_type = HCLGE_DEF_VLAN_TYPE;
	hdev->vlan_type_cfg.rx_ot_sec_vlan_type = HCLGE_DEF_VLAN_TYPE;
	hdev->vlan_type_cfg.tx_ot_vlan_type = HCLGE_DEF_VLAN_TYPE;
	hdev->vlan_type_cfg.tx_in_vlan_type = HCLGE_DEF_VLAN_TYPE;

	ret = hclge_set_vlan_protocol_type(hdev);
	if (ret)
		return ret;

	for (i = 0; i < hdev->num_alloc_vport; i++) {
		vport = &hdev->vport[i];
		vport->txvlan_cfg.accept_tag1 = true;
		vport->txvlan_cfg.accept_untag1 = true;

		/* accept_tag2 and accept_untag2 are not supported on
		 * pdev revision(0x20), new revision support them. The
		 * value of this two fields will not return error when driver
		 * send command to fireware in revision(0x20).
		 * This two fields can not configured by user.
		 */
		vport->txvlan_cfg.accept_tag2 = true;
		vport->txvlan_cfg.accept_untag2 = true;

		vport->txvlan_cfg.insert_tag1_en = false;
		vport->txvlan_cfg.insert_tag2_en = false;
		vport->txvlan_cfg.default_tag1 = 0;
		vport->txvlan_cfg.default_tag2 = 0;

		ret = hclge_set_vlan_tx_offload_cfg(vport);
		if (ret)
			return ret;

		vport->rxvlan_cfg.strip_tag1_en = false;
		vport->rxvlan_cfg.strip_tag2_en = true;
		vport->rxvlan_cfg.vlan1_vlan_prionly = false;
		vport->rxvlan_cfg.vlan2_vlan_prionly = false;

		ret = hclge_set_vlan_rx_offload_cfg(vport);
		if (ret)
			return ret;
	}

	handle = &hdev->vport[0].nic;
	return hclge_set_vlan_filter(handle, htons(ETH_P_8021Q), 0, false);
}

int hclge_en_hw_strip_rxvtag(struct hnae3_handle *handle, bool enable)
{
	struct hclge_vport *vport = hclge_get_vport(handle);

	vport->rxvlan_cfg.strip_tag1_en = false;
	vport->rxvlan_cfg.strip_tag2_en = enable;
	vport->rxvlan_cfg.vlan1_vlan_prionly = false;
	vport->rxvlan_cfg.vlan2_vlan_prionly = false;

	return hclge_set_vlan_rx_offload_cfg(vport);
}

static int hclge_set_mac_mtu(struct hclge_dev *hdev, int new_mtu)
{
	struct hclge_config_max_frm_size_cmd *req;
	struct hclge_desc desc;
	int max_frm_size;
	int ret;

	max_frm_size = new_mtu + ETH_HLEN + ETH_FCS_LEN + VLAN_HLEN;

	if (max_frm_size < HCLGE_MAC_MIN_FRAME ||
	    max_frm_size > HCLGE_MAC_MAX_FRAME)
		return -EINVAL;

	max_frm_size = max(max_frm_size, HCLGE_MAC_DEFAULT_FRAME);

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_CONFIG_MAX_FRM_SIZE, false);

	req = (struct hclge_config_max_frm_size_cmd *)desc.data;
	req->max_frm_size = cpu_to_le16(max_frm_size);
	req->min_frm_size = HCLGE_MAC_MIN_FRAME;

	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		dev_err(&hdev->pdev->dev, "set mtu fail, ret =%d.\n", ret);
	else
		hdev->mps = max_frm_size;

	return ret;
}

static int hclge_set_mtu(struct hnae3_handle *handle, int new_mtu)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;
	int ret;

	ret = hclge_set_mac_mtu(hdev, new_mtu);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"Change mtu fail, ret =%d\n", ret);
		return ret;
	}

	ret = hclge_buffer_alloc(hdev);
	if (ret)
		dev_err(&hdev->pdev->dev,
			"Allocate buffer fail, ret =%d\n", ret);

	return ret;
}

static int hclge_send_reset_tqp_cmd(struct hclge_dev *hdev, u16 queue_id,
				    bool enable)
{
	struct hclge_reset_tqp_queue_cmd *req;
	struct hclge_desc desc;
	int ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_RESET_TQP_QUEUE, false);

	req = (struct hclge_reset_tqp_queue_cmd *)desc.data;
	req->tqp_id = cpu_to_le16(queue_id & HCLGE_RING_ID_MASK);
	hnae3_set_bit(req->reset_req, HCLGE_TQP_RESET_B, enable);

	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"Send tqp reset cmd error, status =%d\n", ret);
		return ret;
	}

	return 0;
}

static int hclge_get_reset_status(struct hclge_dev *hdev, u16 queue_id)
{
	struct hclge_reset_tqp_queue_cmd *req;
	struct hclge_desc desc;
	int ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_RESET_TQP_QUEUE, true);

	req = (struct hclge_reset_tqp_queue_cmd *)desc.data;
	req->tqp_id = cpu_to_le16(queue_id & HCLGE_RING_ID_MASK);

	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"Get reset status error, status =%d\n", ret);
		return ret;
	}

	return hnae3_get_bit(req->ready_to_reset, HCLGE_TQP_RESET_B);
}

static u16 hclge_covert_handle_qid_global(struct hnae3_handle *handle,
					  u16 queue_id)
{
	struct hnae3_queue *queue;
	struct hclge_tqp *tqp;

	queue = handle->kinfo.tqp[queue_id];
	tqp = container_of(queue, struct hclge_tqp, q);

	return tqp->index;
}

void hclge_reset_tqp(struct hnae3_handle *handle, u16 queue_id)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;
	int reset_try_times = 0;
	int reset_status;
	u16 queue_gid;
	int ret;

	if (test_bit(HCLGE_STATE_RST_HANDLING, &hdev->state))
		return;

	queue_gid = hclge_covert_handle_qid_global(handle, queue_id);

	ret = hclge_tqp_enable(hdev, queue_id, 0, false);
	if (ret) {
		dev_warn(&hdev->pdev->dev, "Disable tqp fail, ret = %d\n", ret);
		return;
	}

	ret = hclge_send_reset_tqp_cmd(hdev, queue_gid, true);
	if (ret) {
		dev_warn(&hdev->pdev->dev,
			 "Send reset tqp cmd fail, ret = %d\n", ret);
		return;
	}

	reset_try_times = 0;
	while (reset_try_times++ < HCLGE_TQP_RESET_TRY_TIMES) {
		/* Wait for tqp hw reset */
		msleep(20);
		reset_status = hclge_get_reset_status(hdev, queue_gid);
		if (reset_status)
			break;
	}

	if (reset_try_times >= HCLGE_TQP_RESET_TRY_TIMES) {
		dev_warn(&hdev->pdev->dev, "Reset TQP fail\n");
		return;
	}

	ret = hclge_send_reset_tqp_cmd(hdev, queue_gid, false);
	if (ret) {
		dev_warn(&hdev->pdev->dev,
			 "Deassert the soft reset fail, ret = %d\n", ret);
		return;
	}
}

void hclge_reset_vf_queue(struct hclge_vport *vport, u16 queue_id)
{
	struct hclge_dev *hdev = vport->back;
	int reset_try_times = 0;
	int reset_status;
	u16 queue_gid;
	int ret;

	queue_gid = hclge_covert_handle_qid_global(&vport->nic, queue_id);

	ret = hclge_send_reset_tqp_cmd(hdev, queue_gid, true);
	if (ret) {
		dev_warn(&hdev->pdev->dev,
			 "Send reset tqp cmd fail, ret = %d\n", ret);
		return;
	}

	reset_try_times = 0;
	while (reset_try_times++ < HCLGE_TQP_RESET_TRY_TIMES) {
		/* Wait for tqp hw reset */
		msleep(20);
		reset_status = hclge_get_reset_status(hdev, queue_gid);
		if (reset_status)
			break;
	}

	if (reset_try_times >= HCLGE_TQP_RESET_TRY_TIMES) {
		dev_warn(&hdev->pdev->dev, "Reset TQP fail\n");
		return;
	}

	ret = hclge_send_reset_tqp_cmd(hdev, queue_gid, false);
	if (ret)
		dev_warn(&hdev->pdev->dev,
			 "Deassert the soft reset fail, ret = %d\n", ret);
}

static u32 hclge_get_fw_version(struct hnae3_handle *handle)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;

	return hdev->fw_version;
}

static void hclge_get_flowctrl_adv(struct hnae3_handle *handle,
				   u32 *flowctrl_adv)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;
	struct phy_device *phydev = hdev->hw.mac.phydev;

	if (!phydev)
		return;

	*flowctrl_adv |= (phydev->advertising & ADVERTISED_Pause) |
			 (phydev->advertising & ADVERTISED_Asym_Pause);
}

static void hclge_set_flowctrl_adv(struct hclge_dev *hdev, u32 rx_en, u32 tx_en)
{
	struct phy_device *phydev = hdev->hw.mac.phydev;

	if (!phydev)
		return;

	phydev->advertising &= ~(ADVERTISED_Pause | ADVERTISED_Asym_Pause);

	if (rx_en)
		phydev->advertising |= ADVERTISED_Pause | ADVERTISED_Asym_Pause;

	if (tx_en)
		phydev->advertising ^= ADVERTISED_Asym_Pause;
}

static int hclge_cfg_pauseparam(struct hclge_dev *hdev, u32 rx_en, u32 tx_en)
{
	int ret;

	if (rx_en && tx_en)
		hdev->fc_mode_last_time = HCLGE_FC_FULL;
	else if (rx_en && !tx_en)
		hdev->fc_mode_last_time = HCLGE_FC_RX_PAUSE;
	else if (!rx_en && tx_en)
		hdev->fc_mode_last_time = HCLGE_FC_TX_PAUSE;
	else
		hdev->fc_mode_last_time = HCLGE_FC_NONE;

	if (hdev->tm_info.fc_mode == HCLGE_FC_PFC)
		return 0;

	ret = hclge_mac_pause_en_cfg(hdev, tx_en, rx_en);
	if (ret) {
		dev_err(&hdev->pdev->dev, "configure pauseparam error, ret = %d.\n",
			ret);
		return ret;
	}

	hdev->tm_info.fc_mode = hdev->fc_mode_last_time;

	return 0;
}

int hclge_cfg_flowctrl(struct hclge_dev *hdev)
{
	struct phy_device *phydev = hdev->hw.mac.phydev;
	u16 remote_advertising = 0;
	u16 local_advertising = 0;
	u32 rx_pause, tx_pause;
	u8 flowctl;

	if (!phydev->link || !phydev->autoneg)
		return 0;

	if (phydev->advertising & ADVERTISED_Pause)
		local_advertising = ADVERTISE_PAUSE_CAP;

	if (phydev->advertising & ADVERTISED_Asym_Pause)
		local_advertising |= ADVERTISE_PAUSE_ASYM;

	if (phydev->pause)
		remote_advertising = LPA_PAUSE_CAP;

	if (phydev->asym_pause)
		remote_advertising |= LPA_PAUSE_ASYM;

	flowctl = mii_resolve_flowctrl_fdx(local_advertising,
					   remote_advertising);
	tx_pause = flowctl & FLOW_CTRL_TX;
	rx_pause = flowctl & FLOW_CTRL_RX;

	if (phydev->duplex == HCLGE_MAC_HALF) {
		tx_pause = 0;
		rx_pause = 0;
	}

	return hclge_cfg_pauseparam(hdev, rx_pause, tx_pause);
}

static void hclge_get_pauseparam(struct hnae3_handle *handle, u32 *auto_neg,
				 u32 *rx_en, u32 *tx_en)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;

	*auto_neg = hclge_get_autoneg(handle);

	if (hdev->tm_info.fc_mode == HCLGE_FC_PFC) {
		*rx_en = 0;
		*tx_en = 0;
		return;
	}

	if (hdev->tm_info.fc_mode == HCLGE_FC_RX_PAUSE) {
		*rx_en = 1;
		*tx_en = 0;
	} else if (hdev->tm_info.fc_mode == HCLGE_FC_TX_PAUSE) {
		*tx_en = 1;
		*rx_en = 0;
	} else if (hdev->tm_info.fc_mode == HCLGE_FC_FULL) {
		*rx_en = 1;
		*tx_en = 1;
	} else {
		*rx_en = 0;
		*tx_en = 0;
	}
}

static int hclge_set_pauseparam(struct hnae3_handle *handle, u32 auto_neg,
				u32 rx_en, u32 tx_en)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;
	struct phy_device *phydev = hdev->hw.mac.phydev;
	u32 fc_autoneg;

	fc_autoneg = hclge_get_autoneg(handle);
	if (auto_neg != fc_autoneg) {
		dev_info(&hdev->pdev->dev,
			 "To change autoneg please use: ethtool -s <dev> autoneg <on|off>\n");
		return -EOPNOTSUPP;
	}

	if (hdev->tm_info.fc_mode == HCLGE_FC_PFC) {
		dev_info(&hdev->pdev->dev,
			 "Priority flow control enabled. Cannot set link flow control.\n");
		return -EOPNOTSUPP;
	}

	hclge_set_flowctrl_adv(hdev, rx_en, tx_en);

	if (!fc_autoneg)
		return hclge_cfg_pauseparam(hdev, rx_en, tx_en);

	/* Only support flow control negotiation for netdev with
	 * phy attached for now.
	 */
	if (!phydev)
		return -EOPNOTSUPP;

	return phy_start_aneg(phydev);
}

static void hclge_get_ksettings_an_result(struct hnae3_handle *handle,
					  u8 *auto_neg, u32 *speed, u8 *duplex)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;

	if (speed)
		*speed = hdev->hw.mac.speed;
	if (duplex)
		*duplex = hdev->hw.mac.duplex;
	if (auto_neg)
		*auto_neg = hdev->hw.mac.autoneg;
}

static void hclge_get_media_type(struct hnae3_handle *handle, u8 *media_type)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;

	if (media_type)
		*media_type = hdev->hw.mac.media_type;
}

static void hclge_get_mdix_mode(struct hnae3_handle *handle,
				u8 *tp_mdix_ctrl, u8 *tp_mdix)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;
	struct phy_device *phydev = hdev->hw.mac.phydev;
	int mdix_ctrl, mdix, retval, is_resolved;

	if (!phydev) {
		*tp_mdix_ctrl = ETH_TP_MDI_INVALID;
		*tp_mdix = ETH_TP_MDI_INVALID;
		return;
	}

	phy_write(phydev, HCLGE_PHY_PAGE_REG, HCLGE_PHY_PAGE_MDIX);

	retval = phy_read(phydev, HCLGE_PHY_CSC_REG);
	mdix_ctrl = hnae3_get_field(retval, HCLGE_PHY_MDIX_CTRL_M,
				    HCLGE_PHY_MDIX_CTRL_S);

	retval = phy_read(phydev, HCLGE_PHY_CSS_REG);
	mdix = hnae3_get_bit(retval, HCLGE_PHY_MDIX_STATUS_B);
	is_resolved = hnae3_get_bit(retval, HCLGE_PHY_SPEED_DUP_RESOLVE_B);

	phy_write(phydev, HCLGE_PHY_PAGE_REG, HCLGE_PHY_PAGE_COPPER);

	switch (mdix_ctrl) {
	case 0x0:
		*tp_mdix_ctrl = ETH_TP_MDI;
		break;
	case 0x1:
		*tp_mdix_ctrl = ETH_TP_MDI_X;
		break;
	case 0x3:
		*tp_mdix_ctrl = ETH_TP_MDI_AUTO;
		break;
	default:
		*tp_mdix_ctrl = ETH_TP_MDI_INVALID;
		break;
	}

	if (!is_resolved)
		*tp_mdix = ETH_TP_MDI_INVALID;
	else if (mdix)
		*tp_mdix = ETH_TP_MDI_X;
	else
		*tp_mdix = ETH_TP_MDI;
}

static int hclge_init_instance_hw(struct hclge_dev *hdev)
{
	return hclge_mac_connect_phy(hdev);
}

static void hclge_uninit_instance_hw(struct hclge_dev *hdev)
{
	hclge_mac_disconnect_phy(hdev);
}

static int hclge_init_client_instance(struct hnae3_client *client,
				      struct hnae3_ae_dev *ae_dev)
{
	struct hclge_dev *hdev = ae_dev->priv;
	struct hclge_vport *vport;
	int i, ret;

	for (i = 0; i <  hdev->num_vmdq_vport + 1; i++) {
		vport = &hdev->vport[i];

		switch (client->type) {
		case HNAE3_CLIENT_KNIC:

			hdev->nic_client = client;
			vport->nic.client = client;
			ret = client->ops->init_instance(&vport->nic);
			if (ret)
				goto clear_nic;

			ret = hclge_init_instance_hw(hdev);
			if (ret) {
			        client->ops->uninit_instance(&vport->nic,
			                                     0);
				goto clear_nic;
			}

			hnae3_set_client_init_flag(client, ae_dev, 1);

			if (hdev->roce_client &&
			    hnae3_dev_roce_supported(hdev)) {
				struct hnae3_client *rc = hdev->roce_client;

				ret = hclge_init_roce_base_info(vport);
				if (ret)
					goto clear_roce;

				ret = rc->ops->init_instance(&vport->roce);
				if (ret)
					goto clear_roce;

				hnae3_set_client_init_flag(hdev->roce_client,
							   ae_dev, 1);
			}

			break;
		case HNAE3_CLIENT_UNIC:
			hdev->nic_client = client;
			vport->nic.client = client;

			ret = client->ops->init_instance(&vport->nic);
			if (ret)
				goto clear_nic;

			hnae3_set_client_init_flag(client, ae_dev, 1);

			break;
		case HNAE3_CLIENT_ROCE:
			if (hnae3_dev_roce_supported(hdev)) {
				hdev->roce_client = client;
				vport->roce.client = client;
			}

			if (hdev->roce_client && hdev->nic_client) {
				ret = hclge_init_roce_base_info(vport);
				if (ret)
					goto clear_roce;

				ret = client->ops->init_instance(&vport->roce);
				if (ret)
					goto clear_roce;

				hnae3_set_client_init_flag(client, ae_dev, 1);
			}
		}
	}

	return 0;

clear_nic:
	hdev->nic_client = NULL;
	vport->nic.client = NULL;
	return ret;
clear_roce:
	hdev->roce_client = NULL;
	vport->roce.client = NULL;
	return ret;
}

static void hclge_uninit_client_instance(struct hnae3_client *client,
					 struct hnae3_ae_dev *ae_dev)
{
	struct hclge_dev *hdev = ae_dev->priv;
	struct hclge_vport *vport;
	int i;

	for (i = 0; i < hdev->num_vmdq_vport + 1; i++) {
		vport = &hdev->vport[i];
		if (hdev->roce_client) {
			hdev->roce_client->ops->uninit_instance(&vport->roce,
								0);
			hdev->roce_client = NULL;
			vport->roce.client = NULL;
		}
		if (client->type == HNAE3_CLIENT_ROCE)
			return;
		if (hdev->nic_client && client->ops->uninit_instance) {
			hclge_uninit_instance_hw(hdev);
			client->ops->uninit_instance(&vport->nic, 0);
			hdev->nic_client = NULL;
			vport->nic.client = NULL;
		}
	}
}

static int hclge_pci_init(struct hclge_dev *hdev)
{
	struct pci_dev *pdev = hdev->pdev;
	struct hclge_hw *hw;
	int ret;

	ret = pci_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable PCI device\n");
		return ret;
	}

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (ret) {
		ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
		if (ret) {
			dev_err(&pdev->dev,
				"can't set consistent PCI DMA");
			goto err_disable_device;
		}
		dev_warn(&pdev->dev, "set DMA mask to 32 bits\n");
	}

	ret = pci_request_regions(pdev, HCLGE_DRIVER_NAME);
	if (ret) {
		dev_err(&pdev->dev, "PCI request regions failed %d\n", ret);
		goto err_disable_device;
	}

	pci_set_master(pdev);
	hw = &hdev->hw;
	hw->io_base = pcim_iomap(pdev, 2, 0);
	if (!hw->io_base) {
		dev_err(&pdev->dev, "Can't map configuration register space\n");
		ret = -ENOMEM;
		goto err_clr_master;
	}

	hdev->num_req_vfs = pci_sriov_get_totalvfs(pdev);

	return 0;
err_clr_master:
	pci_clear_master(pdev);
	pci_release_regions(pdev);
err_disable_device:
	pci_disable_device(pdev);

	return ret;
}

static void hclge_pci_uninit(struct hclge_dev *hdev)
{
	struct pci_dev *pdev = hdev->pdev;

	pcim_iounmap(pdev, hdev->hw.io_base);
	pci_free_irq_vectors(pdev);
	pci_clear_master(pdev);
	pci_release_mem_regions(pdev);
	pci_disable_device(pdev);
}

static void hclge_state_init(struct hclge_dev *hdev)
{
	set_bit(HCLGE_STATE_SERVICE_INITED, &hdev->state);
	set_bit(HCLGE_STATE_DOWN, &hdev->state);
	clear_bit(HCLGE_STATE_RST_SERVICE_SCHED, &hdev->state);
	clear_bit(HCLGE_STATE_RST_HANDLING, &hdev->state);
	clear_bit(HCLGE_STATE_MBX_SERVICE_SCHED, &hdev->state);
	clear_bit(HCLGE_STATE_MBX_HANDLING, &hdev->state);
}

static void hclge_state_uninit(struct hclge_dev *hdev)
{
	set_bit(HCLGE_STATE_DOWN, &hdev->state);

	if (hdev->service_timer.function)
		del_timer_sync(&hdev->service_timer);
	if (hdev->service_task.func)
		cancel_work_sync(&hdev->service_task);
	if (hdev->rst_service_task.func)
		cancel_work_sync(&hdev->rst_service_task);
	if (hdev->mbx_service_task.func)
		cancel_work_sync(&hdev->mbx_service_task);
}

static int hclge_init_ae_dev(struct hnae3_ae_dev *ae_dev)
{
	struct pci_dev *pdev = ae_dev->pdev;
	struct hclge_dev *hdev;
	int ret;

	hdev = devm_kzalloc(&pdev->dev, sizeof(*hdev), GFP_KERNEL);
	if (!hdev) {
		ret = -ENOMEM;
		goto out;
	}

	hdev->pdev = pdev;
	hdev->ae_dev = ae_dev;
	hdev->reset_type = HNAE3_NONE_RESET;
	ae_dev->priv = hdev;

	ret = hclge_pci_init(hdev);
	if (ret) {
		dev_err(&pdev->dev, "PCI init failed\n");
		goto out;
	}

	/* Firmware command queue initialize */
	ret = hclge_cmd_queue_init(hdev);
	if (ret) {
		dev_err(&pdev->dev, "Cmd queue init failed, ret = %d.\n", ret);
		goto err_pci_uninit;
	}

	/* Firmware command initialize */
	ret = hclge_cmd_init(hdev);
	if (ret)
		goto err_cmd_uninit;

	ret = hclge_get_cap(hdev);
	if (ret) {
		dev_err(&pdev->dev, "get hw capability error, ret = %d.\n",
			ret);
		goto err_cmd_uninit;
	}

	ret = hclge_configure(hdev);
	if (ret) {
		dev_err(&pdev->dev, "Configure dev error, ret = %d.\n", ret);
		goto err_cmd_uninit;
	}

	ret = hclge_init_msi(hdev);
	if (ret) {
		dev_err(&pdev->dev, "Init MSI/MSI-X error, ret = %d.\n", ret);
		goto err_cmd_uninit;
	}

	ret = hclge_misc_irq_init(hdev);
	if (ret) {
		dev_err(&pdev->dev,
			"Misc IRQ(vector0) init error, ret = %d.\n",
			ret);
		goto err_msi_uninit;
	}

	ret = hclge_alloc_tqps(hdev);
	if (ret) {
		dev_err(&pdev->dev, "Allocate TQPs error, ret = %d.\n", ret);
		goto err_msi_irq_uninit;
	}

	ret = hclge_alloc_vport(hdev);
	if (ret) {
		dev_err(&pdev->dev, "Allocate vport error, ret = %d.\n", ret);
		goto err_msi_irq_uninit;
	}

	ret = hclge_map_tqp(hdev);
	if (ret) {
		dev_err(&pdev->dev, "Map tqp error, ret = %d.\n", ret);
		goto err_msi_irq_uninit;
	}

	if (hdev->hw.mac.media_type == HNAE3_MEDIA_TYPE_COPPER) {
		ret = hclge_mac_mdio_config(hdev);
		if (ret) {
			dev_err(&hdev->pdev->dev,
				"mdio config fail ret=%d\n", ret);
			goto err_msi_irq_uninit;
		}
	}

	ret = hclge_mac_init(hdev);
	if (ret) {
		dev_err(&pdev->dev, "Mac init error, ret = %d\n", ret);
		goto err_mdiobus_unreg;
	}

	ret = hclge_config_tso(hdev, HCLGE_TSO_MSS_MIN, HCLGE_TSO_MSS_MAX);
	if (ret) {
		dev_err(&pdev->dev, "Enable tso fail, ret =%d\n", ret);
		goto err_mdiobus_unreg;
	}

	ret = hclge_init_vlan_config(hdev);
	if (ret) {
		dev_err(&pdev->dev, "VLAN init fail, ret =%d\n", ret);
		goto err_mdiobus_unreg;
	}

	ret = hclge_tm_schd_init(hdev);
	if (ret) {
		dev_err(&pdev->dev, "tm schd init fail, ret =%d\n", ret);
		goto err_mdiobus_unreg;
	}

	hclge_rss_init_cfg(hdev);
	ret = hclge_rss_init_hw(hdev);
	if (ret) {
		dev_err(&pdev->dev, "Rss init fail, ret =%d\n", ret);
		goto err_mdiobus_unreg;
	}

	ret = init_mgr_tbl(hdev);
	if (ret) {
		dev_err(&pdev->dev, "manager table init fail, ret =%d\n", ret);
		goto err_mdiobus_unreg;
	}

	hclge_dcb_ops_set(hdev);

	timer_setup(&hdev->service_timer, hclge_service_timer, 0);
	INIT_WORK(&hdev->service_task, hclge_service_task);
	INIT_WORK(&hdev->rst_service_task, hclge_reset_service_task);
	INIT_WORK(&hdev->mbx_service_task, hclge_mailbox_service_task);

	hclge_clear_all_event_cause(hdev);

	/* Enable MISC vector(vector0) */
	hclge_enable_vector(&hdev->misc_vector, true);

	hclge_state_init(hdev);

	pr_info("%s driver initialization finished.\n", HCLGE_DRIVER_NAME);
	return 0;

err_mdiobus_unreg:
	if (hdev->hw.mac.phydev)
		mdiobus_unregister(hdev->hw.mac.mdio_bus);
err_msi_irq_uninit:
	hclge_misc_irq_uninit(hdev);
err_msi_uninit:
	pci_free_irq_vectors(pdev);
err_cmd_uninit:
	hclge_destroy_cmd_queue(&hdev->hw);
err_pci_uninit:
	pcim_iounmap(pdev, hdev->hw.io_base);
	pci_clear_master(pdev);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
out:
	return ret;
}

static void hclge_stats_clear(struct hclge_dev *hdev)
{
	memset(&hdev->hw_stats, 0, sizeof(hdev->hw_stats));
}

static int hclge_reset_ae_dev(struct hnae3_ae_dev *ae_dev)
{
	struct hclge_dev *hdev = ae_dev->priv;
	struct pci_dev *pdev = ae_dev->pdev;
	int ret;

	set_bit(HCLGE_STATE_DOWN, &hdev->state);

	hclge_stats_clear(hdev);
	memset(hdev->vlan_table, 0, sizeof(hdev->vlan_table));

	ret = hclge_cmd_init(hdev);
	if (ret) {
		dev_err(&pdev->dev, "Cmd queue init failed\n");
		return ret;
	}

	ret = hclge_get_cap(hdev);
	if (ret) {
		dev_err(&pdev->dev, "get hw capability error, ret = %d.\n",
			ret);
		return ret;
	}

	ret = hclge_configure(hdev);
	if (ret) {
		dev_err(&pdev->dev, "Configure dev error, ret = %d.\n", ret);
		return ret;
	}

	ret = hclge_map_tqp(hdev);
	if (ret) {
		dev_err(&pdev->dev, "Map tqp error, ret = %d.\n", ret);
		return ret;
	}

	ret = hclge_mac_init(hdev);
	if (ret) {
		dev_err(&pdev->dev, "Mac init error, ret = %d\n", ret);
		return ret;
	}

	ret = hclge_config_tso(hdev, HCLGE_TSO_MSS_MIN, HCLGE_TSO_MSS_MAX);
	if (ret) {
		dev_err(&pdev->dev, "Enable tso fail, ret =%d\n", ret);
		return ret;
	}

	ret = hclge_init_vlan_config(hdev);
	if (ret) {
		dev_err(&pdev->dev, "VLAN init fail, ret =%d\n", ret);
		return ret;
	}

	ret = hclge_tm_init_hw(hdev);
	if (ret) {
		dev_err(&pdev->dev, "tm init hw fail, ret =%d\n", ret);
		return ret;
	}

	ret = hclge_rss_init_hw(hdev);
	if (ret) {
		dev_err(&pdev->dev, "Rss init fail, ret =%d\n", ret);
		return ret;
	}

	dev_info(&pdev->dev, "Reset done, %s driver initialization finished.\n",
		 HCLGE_DRIVER_NAME);

	return 0;
}

static void hclge_uninit_ae_dev(struct hnae3_ae_dev *ae_dev)
{
	struct hclge_dev *hdev = ae_dev->priv;
	struct hclge_mac *mac = &hdev->hw.mac;

	hclge_state_uninit(hdev);

	if (mac->phydev)
		mdiobus_unregister(mac->mdio_bus);

	/* Disable MISC vector(vector0) */
	hclge_enable_vector(&hdev->misc_vector, false);
	synchronize_irq(hdev->misc_vector.vector_irq);

	hclge_destroy_cmd_queue(&hdev->hw);
	hclge_misc_irq_uninit(hdev);
	hclge_pci_uninit(hdev);
	ae_dev->priv = NULL;
}

static u32 hclge_get_max_channels(struct hnae3_handle *handle)
{
	struct hnae3_knic_private_info *kinfo = &handle->kinfo;
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;

	return min_t(u32, hdev->rss_size_max * kinfo->num_tc, hdev->num_tqps);
}

static void hclge_get_channels(struct hnae3_handle *handle,
			       struct ethtool_channels *ch)
{
	struct hclge_vport *vport = hclge_get_vport(handle);

	ch->max_combined = hclge_get_max_channels(handle);
	ch->other_count = 1;
	ch->max_other = 1;
	ch->combined_count = vport->alloc_tqps;
}

static void hclge_get_tqps_and_rss_info(struct hnae3_handle *handle,
					u16 *free_tqps, u16 *max_rss_size)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;
	u16 temp_tqps = 0;
	int i;

	for (i = 0; i < hdev->num_tqps; i++) {
		if (!hdev->htqp[i].alloced)
			temp_tqps++;
	}
	*free_tqps = temp_tqps;
	*max_rss_size = hdev->rss_size_max;
}

static void hclge_release_tqp(struct hclge_vport *vport)
{
	struct hnae3_knic_private_info *kinfo = &vport->nic.kinfo;
	struct hclge_dev *hdev = vport->back;
	int i;

	for (i = 0; i < kinfo->num_tqps; i++) {
		struct hclge_tqp *tqp =
			container_of(kinfo->tqp[i], struct hclge_tqp, q);

		tqp->q.handle = NULL;
		tqp->q.tqp_index = 0;
		tqp->alloced = false;
	}

	devm_kfree(&hdev->pdev->dev, kinfo->tqp);
	kinfo->tqp = NULL;
}

static int hclge_set_channels(struct hnae3_handle *handle, u32 new_tqps_num)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hnae3_knic_private_info *kinfo = &vport->nic.kinfo;
	struct hclge_dev *hdev = vport->back;
	int cur_rss_size = kinfo->rss_size;
	int cur_tqps = kinfo->num_tqps;
	u16 tc_offset[HCLGE_MAX_TC_NUM];
	u16 tc_valid[HCLGE_MAX_TC_NUM];
	u16 tc_size[HCLGE_MAX_TC_NUM];
	u16 roundup_size;
	u32 *rss_indir;
	int ret, i;

	/* Free old tqps, and reallocate with new tqp number when nic setup */
	hclge_release_tqp(vport);

	ret = hclge_knic_setup(vport, new_tqps_num, kinfo->num_desc);
	if (ret) {
		dev_err(&hdev->pdev->dev, "setup nic fail, ret =%d\n", ret);
		return ret;
	}

	ret = hclge_map_tqp_to_vport(hdev, vport);
	if (ret) {
		dev_err(&hdev->pdev->dev, "map vport tqp fail, ret =%d\n", ret);
		return ret;
	}

	ret = hclge_tm_schd_init(hdev);
	if (ret) {
		dev_err(&hdev->pdev->dev, "tm schd init fail, ret =%d\n", ret);
		return ret;
	}

	roundup_size = roundup_pow_of_two(kinfo->rss_size);
	roundup_size = ilog2(roundup_size);
	/* Set the RSS TC mode according to the new RSS size */
	for (i = 0; i < HCLGE_MAX_TC_NUM; i++) {
		tc_valid[i] = 0;

		if (!(hdev->hw_tc_map & BIT(i)))
			continue;

		tc_valid[i] = 1;
		tc_size[i] = roundup_size;
		tc_offset[i] = kinfo->rss_size * i;
	}
	ret = hclge_set_rss_tc_mode(hdev, tc_valid, tc_size, tc_offset);
	if (ret)
		return ret;

	/* Reinitializes the rss indirect table according to the new RSS size */
	rss_indir = kcalloc(HCLGE_RSS_IND_TBL_SIZE, sizeof(u32), GFP_KERNEL);
	if (!rss_indir)
		return -ENOMEM;

	for (i = 0; i < HCLGE_RSS_IND_TBL_SIZE; i++)
		rss_indir[i] = i % kinfo->rss_size;

	ret = hclge_set_rss(handle, rss_indir, NULL, 0);
	if (ret)
		dev_err(&hdev->pdev->dev, "set rss indir table fail, ret=%d\n",
			ret);

	kfree(rss_indir);

	if (!ret)
		dev_info(&hdev->pdev->dev,
			 "Channels changed, rss_size from %d to %d, tqps from %d to %d",
			 cur_rss_size, kinfo->rss_size,
			 cur_tqps, kinfo->rss_size * kinfo->num_tc);

	return ret;
}

static int hclge_get_regs_num(struct hclge_dev *hdev, u32 *regs_num_32_bit,
			      u32 *regs_num_64_bit)
{
	struct hclge_desc desc;
	u32 total_num;
	int ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_QUERY_REG_NUM, true);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"Query register number cmd failed, ret = %d.\n", ret);
		return ret;
	}

	*regs_num_32_bit = le32_to_cpu(desc.data[0]);
	*regs_num_64_bit = le32_to_cpu(desc.data[1]);

	total_num = *regs_num_32_bit + *regs_num_64_bit;
	if (!total_num)
		return -EINVAL;

	return 0;
}

static int hclge_get_32_bit_regs(struct hclge_dev *hdev, u32 regs_num,
				 void *data)
{
#define HCLGE_32_BIT_REG_RTN_DATANUM 8

	struct hclge_desc *desc;
	u32 *reg_val = data;
	__le32 *desc_data;
	int cmd_num;
	int i, k, n;
	int ret;

	if (regs_num == 0)
		return 0;

	cmd_num = DIV_ROUND_UP(regs_num + 2, HCLGE_32_BIT_REG_RTN_DATANUM);
	desc = kcalloc(cmd_num, sizeof(struct hclge_desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;

	hclge_cmd_setup_basic_desc(&desc[0], HCLGE_OPC_QUERY_32_BIT_REG, true);
	ret = hclge_cmd_send(&hdev->hw, desc, cmd_num);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"Query 32 bit register cmd failed, ret = %d.\n", ret);
		kfree(desc);
		return ret;
	}

	for (i = 0; i < cmd_num; i++) {
		if (i == 0) {
			desc_data = (__le32 *)(&desc[i].data[0]);
			n = HCLGE_32_BIT_REG_RTN_DATANUM - 2;
		} else {
			desc_data = (__le32 *)(&desc[i]);
			n = HCLGE_32_BIT_REG_RTN_DATANUM;
		}
		for (k = 0; k < n; k++) {
			*reg_val++ = le32_to_cpu(*desc_data++);

			regs_num--;
			if (!regs_num)
				break;
		}
	}

	kfree(desc);
	return 0;
}

static int hclge_get_64_bit_regs(struct hclge_dev *hdev, u32 regs_num,
				 void *data)
{
#define HCLGE_64_BIT_REG_RTN_DATANUM 4

	struct hclge_desc *desc;
	u64 *reg_val = data;
	__le64 *desc_data;
	int cmd_num;
	int i, k, n;
	int ret;

	if (regs_num == 0)
		return 0;

	cmd_num = DIV_ROUND_UP(regs_num + 1, HCLGE_64_BIT_REG_RTN_DATANUM);
	desc = kcalloc(cmd_num, sizeof(struct hclge_desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;

	hclge_cmd_setup_basic_desc(&desc[0], HCLGE_OPC_QUERY_64_BIT_REG, true);
	ret = hclge_cmd_send(&hdev->hw, desc, cmd_num);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"Query 64 bit register cmd failed, ret = %d.\n", ret);
		kfree(desc);
		return ret;
	}

	for (i = 0; i < cmd_num; i++) {
		if (i == 0) {
			desc_data = (__le64 *)(&desc[i].data[0]);
			n = HCLGE_64_BIT_REG_RTN_DATANUM - 1;
		} else {
			desc_data = (__le64 *)(&desc[i]);
			n = HCLGE_64_BIT_REG_RTN_DATANUM;
		}
		for (k = 0; k < n; k++) {
			*reg_val++ = le64_to_cpu(*desc_data++);

			regs_num--;
			if (!regs_num)
				break;
		}
	}

	kfree(desc);
	return 0;
}

static int hclge_get_regs_len(struct hnae3_handle *handle)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;
	u32 regs_num_32_bit, regs_num_64_bit;
	int ret;

	ret = hclge_get_regs_num(hdev, &regs_num_32_bit, &regs_num_64_bit);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"Get register number failed, ret = %d.\n", ret);
		return -EOPNOTSUPP;
	}

	return regs_num_32_bit * sizeof(u32) + regs_num_64_bit * sizeof(u64);
}

static void hclge_get_regs(struct hnae3_handle *handle, u32 *version,
			   void *data)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;
	u32 regs_num_32_bit, regs_num_64_bit;
	int ret;

	*version = hdev->fw_version;

	ret = hclge_get_regs_num(hdev, &regs_num_32_bit, &regs_num_64_bit);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"Get register number failed, ret = %d.\n", ret);
		return;
	}

	ret = hclge_get_32_bit_regs(hdev, regs_num_32_bit, data);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"Get 32 bit register failed, ret = %d.\n", ret);
		return;
	}

	data = (u32 *)data + regs_num_32_bit;
	ret = hclge_get_64_bit_regs(hdev, regs_num_64_bit,
				    data);
	if (ret)
		dev_err(&hdev->pdev->dev,
			"Get 64 bit register failed, ret = %d.\n", ret);
}

static int hclge_set_led_status(struct hclge_dev *hdev, u8 locate_led_status)
{
	struct hclge_set_led_state_cmd *req;
	struct hclge_desc desc;
	int ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_LED_STATUS_CFG, false);

	req = (struct hclge_set_led_state_cmd *)desc.data;
	hnae3_set_field(req->locate_led_config, HCLGE_LED_LOCATE_STATE_M,
			HCLGE_LED_LOCATE_STATE_S, locate_led_status);

	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		dev_err(&hdev->pdev->dev,
			"Send set led state cmd error, ret =%d\n", ret);

	return ret;
}

enum hclge_led_status {
	HCLGE_LED_OFF,
	HCLGE_LED_ON,
	HCLGE_LED_NO_CHANGE = 0xFF,
};

static int hclge_set_led_id(struct hnae3_handle *handle,
			    enum ethtool_phys_id_state status)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;

	switch (status) {
	case ETHTOOL_ID_ACTIVE:
		return hclge_set_led_status(hdev, HCLGE_LED_ON);
	case ETHTOOL_ID_INACTIVE:
		return hclge_set_led_status(hdev, HCLGE_LED_OFF);
	default:
		return -EINVAL;
	}
}

static void hclge_get_link_mode(struct hnae3_handle *handle,
				unsigned long *supported,
				unsigned long *advertising)
{
	unsigned int size = BITS_TO_LONGS(__ETHTOOL_LINK_MODE_MASK_NBITS);
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;
	unsigned int idx = 0;

	for (; idx < size; idx++) {
		supported[idx] = hdev->hw.mac.supported[idx];
		advertising[idx] = hdev->hw.mac.advertising[idx];
	}
}

static void hclge_get_port_type(struct hnae3_handle *handle,
				u8 *port_type)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;
	u8 media_type = hdev->hw.mac.media_type;

	switch (media_type) {
	case HNAE3_MEDIA_TYPE_FIBER:
		*port_type = PORT_FIBRE;
		break;
	case HNAE3_MEDIA_TYPE_COPPER:
		*port_type = PORT_TP;
		break;
	case HNAE3_MEDIA_TYPE_UNKNOWN:
	default:
		*port_type = PORT_OTHER;
		break;
	}
}

static const struct hnae3_ae_ops hclge_ops = {
	.init_ae_dev = hclge_init_ae_dev,
	.uninit_ae_dev = hclge_uninit_ae_dev,
	.init_client_instance = hclge_init_client_instance,
	.uninit_client_instance = hclge_uninit_client_instance,
	.map_ring_to_vector = hclge_map_ring_to_vector,
	.unmap_ring_from_vector = hclge_unmap_ring_frm_vector,
	.get_vector = hclge_get_vector,
	.put_vector = hclge_put_vector,
	.set_promisc_mode = hclge_set_promisc_mode,
	.set_loopback = hclge_set_loopback,
	.start = hclge_ae_start,
	.stop = hclge_ae_stop,
	.get_status = hclge_get_status,
	.get_ksettings_an_result = hclge_get_ksettings_an_result,
	.update_speed_duplex_h = hclge_update_speed_duplex_h,
	.cfg_mac_speed_dup_h = hclge_cfg_mac_speed_dup_h,
	.get_media_type = hclge_get_media_type,
	.get_rss_key_size = hclge_get_rss_key_size,
	.get_rss_indir_size = hclge_get_rss_indir_size,
	.get_rss = hclge_get_rss,
	.set_rss = hclge_set_rss,
	.set_rss_tuple = hclge_set_rss_tuple,
	.get_rss_tuple = hclge_get_rss_tuple,
	.get_tc_size = hclge_get_tc_size,
	.get_mac_addr = hclge_get_mac_addr,
	.set_mac_addr = hclge_set_mac_addr,
	.add_uc_addr = hclge_add_uc_addr,
	.rm_uc_addr = hclge_rm_uc_addr,
	.add_mc_addr = hclge_add_mc_addr,
	.rm_mc_addr = hclge_rm_mc_addr,
	.update_mta_status = hclge_update_mta_status,
	.set_autoneg = hclge_set_autoneg,
	.get_autoneg = hclge_get_autoneg,
	.get_pauseparam = hclge_get_pauseparam,
	.set_pauseparam = hclge_set_pauseparam,
	.set_mtu = hclge_set_mtu,
	.reset_queue = hclge_reset_tqp,
	.get_stats = hclge_get_stats,
	.update_stats = hclge_update_stats,
	.get_strings = hclge_get_strings,
	.get_sset_count = hclge_get_sset_count,
	.get_fw_version = hclge_get_fw_version,
	.get_mdix_mode = hclge_get_mdix_mode,
	.enable_vlan_filter = hclge_enable_vlan_filter,
	.set_vlan_filter = hclge_set_vlan_filter,
	.set_vf_vlan_filter = hclge_set_vf_vlan_filter,
	.enable_hw_strip_rxvtag = hclge_en_hw_strip_rxvtag,
	.reset_event = hclge_reset_event,
	.get_tqps_and_rss_info = hclge_get_tqps_and_rss_info,
	.set_channels = hclge_set_channels,
	.get_channels = hclge_get_channels,
	.get_flowctrl_adv = hclge_get_flowctrl_adv,
	.get_regs_len = hclge_get_regs_len,
	.get_regs = hclge_get_regs,
	.set_led_id = hclge_set_led_id,
	.get_link_mode = hclge_get_link_mode,
	.get_port_type = hclge_get_port_type,
};

static struct hnae3_ae_algo ae_algo = {
	.ops = &hclge_ops,
	.pdev_id_table = ae_algo_pci_tbl,
};

static int hclge_init(void)
{
	pr_info("%s is initializing\n", HCLGE_NAME);

	hnae3_register_ae_algo(&ae_algo);

	return 0;
}

static void hclge_exit(void)
{
	hnae3_unregister_ae_algo(&ae_algo);
}
module_init(hclge_init);
module_exit(hclge_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Huawei Tech. Co., Ltd.");
MODULE_DESCRIPTION("HCLGE Driver");
MODULE_VERSION(HCLGE_MOD_VERSION);
