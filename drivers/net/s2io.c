/************************************************************************
 * s2io.c: A Linux PCI-X Ethernet driver for Neterion 10GbE Server NIC
 * Copyright(c) 2002-2007 Neterion Inc.

 * This software may be used and distributed according to the terms of
 * the GNU General Public License (GPL), incorporated herein by reference.
 * Drivers based on or derived from this code fall under the GPL and must
 * retain the authorship, copyright and license notice.  This file is not
 * a complete program and may only be used when the entire operating
 * system is licensed under the GPL.
 * See the file COPYING in this distribution for more information.
 *
 * Credits:
 * Jeff Garzik		: For pointing out the improper error condition
 *			  check in the s2io_xmit routine and also some
 *			  issues in the Tx watch dog function. Also for
 *			  patiently answering all those innumerable
 *			  questions regaring the 2.6 porting issues.
 * Stephen Hemminger	: Providing proper 2.6 porting mechanism for some
 *			  macros available only in 2.6 Kernel.
 * Francois Romieu	: For pointing out all code part that were
 *			  deprecated and also styling related comments.
 * Grant Grundler	: For helping me get rid of some Architecture
 *			  dependent code.
 * Christopher Hellwig	: Some more 2.6 specific issues in the driver.
 *
 * The module loadable parameters that are supported by the driver and a brief
 * explaination of all the variables.
 *
 * rx_ring_num : This can be used to program the number of receive rings used
 * in the driver.
 * rx_ring_sz: This defines the number of receive blocks each ring can have.
 *     This is also an array of size 8.
 * rx_ring_mode: This defines the operation mode of all 8 rings. The valid
 *		values are 1, 2 and 3.
 * tx_fifo_num: This defines the number of Tx FIFOs thats used int the driver.
 * tx_fifo_len: This too is an array of 8. Each element defines the number of
 * Tx descriptors that can be associated with each corresponding FIFO.
 * intr_type: This defines the type of interrupt. The values can be 0(INTA),
 *     1(MSI), 2(MSI_X). Default value is '0(INTA)'
 * lro: Specifies whether to enable Large Receive Offload (LRO) or not.
 *     Possible values '1' for enable '0' for disable. Default is '0'
 * lro_max_pkts: This parameter defines maximum number of packets can be
 *     aggregated as a single large packet
 * napi: This parameter used to enable/disable NAPI (polling Rx)
 *     Possible values '1' for enable and '0' for disable. Default is '1'
 * ufo: This parameter used to enable/disable UDP Fragmentation Offload(UFO)
 *      Possible values '1' for enable and '0' for disable. Default is '0'
 * vlan_tag_strip: This can be used to enable or disable vlan stripping.
 *                 Possible values '1' for enable , '0' for disable.
 *                 Default is '2' - which means disable in promisc mode
 *                 and enable in non-promiscuous mode.
 ************************************************************************/

#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/stddef.h>
#include <linux/ioctl.h>
#include <linux/timex.h>
#include <linux/ethtool.h>
#include <linux/workqueue.h>
#include <linux/if_vlan.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <net/tcp.h>

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/div64.h>
#include <asm/irq.h>

/* local include */
#include "s2io.h"
#include "s2io-regs.h"

#define DRV_VERSION "2.0.23.1"

/* S2io Driver name & version. */
static char s2io_driver_name[] = "Neterion";
static char s2io_driver_version[] = DRV_VERSION;

static int rxd_size[4] = {32,48,48,64};
static int rxd_count[4] = {127,85,85,63};

static inline int RXD_IS_UP2DT(struct RxD_t *rxdp)
{
	int ret;

	ret = ((!(rxdp->Control_1 & RXD_OWN_XENA)) &&
		(GET_RXD_MARKER(rxdp->Control_2) != THE_RXD_MARK));

	return ret;
}

/*
 * Cards with following subsystem_id have a link state indication
 * problem, 600B, 600C, 600D, 640B, 640C and 640D.
 * macro below identifies these cards given the subsystem_id.
 */
#define CARDS_WITH_FAULTY_LINK_INDICATORS(dev_type, subid) \
	(dev_type == XFRAME_I_DEVICE) ?			\
		((((subid >= 0x600B) && (subid <= 0x600D)) || \
		 ((subid >= 0x640B) && (subid <= 0x640D))) ? 1 : 0) : 0

#define LINK_IS_UP(val64) (!(val64 & (ADAPTER_STATUS_RMAC_REMOTE_FAULT | \
				      ADAPTER_STATUS_RMAC_LOCAL_FAULT)))
#define TASKLET_IN_USE test_and_set_bit(0, (&sp->tasklet_status))
#define PANIC	1
#define LOW	2
static inline int rx_buffer_level(struct s2io_nic * sp, int rxb_size, int ring)
{
	struct mac_info *mac_control;

	mac_control = &sp->mac_control;
	if (rxb_size <= rxd_count[sp->rxd_mode])
		return PANIC;
	else if ((mac_control->rings[ring].pkt_cnt - rxb_size) > 16)
		return  LOW;
	return 0;
}

/* Ethtool related variables and Macros. */
static char s2io_gstrings[][ETH_GSTRING_LEN] = {
	"Register test\t(offline)",
	"Eeprom test\t(offline)",
	"Link test\t(online)",
	"RLDRAM test\t(offline)",
	"BIST Test\t(offline)"
};

static char ethtool_xena_stats_keys[][ETH_GSTRING_LEN] = {
	{"tmac_frms"},
	{"tmac_data_octets"},
	{"tmac_drop_frms"},
	{"tmac_mcst_frms"},
	{"tmac_bcst_frms"},
	{"tmac_pause_ctrl_frms"},
	{"tmac_ttl_octets"},
	{"tmac_ucst_frms"},
	{"tmac_nucst_frms"},
	{"tmac_any_err_frms"},
	{"tmac_ttl_less_fb_octets"},
	{"tmac_vld_ip_octets"},
	{"tmac_vld_ip"},
	{"tmac_drop_ip"},
	{"tmac_icmp"},
	{"tmac_rst_tcp"},
	{"tmac_tcp"},
	{"tmac_udp"},
	{"rmac_vld_frms"},
	{"rmac_data_octets"},
	{"rmac_fcs_err_frms"},
	{"rmac_drop_frms"},
	{"rmac_vld_mcst_frms"},
	{"rmac_vld_bcst_frms"},
	{"rmac_in_rng_len_err_frms"},
	{"rmac_out_rng_len_err_frms"},
	{"rmac_long_frms"},
	{"rmac_pause_ctrl_frms"},
	{"rmac_unsup_ctrl_frms"},
	{"rmac_ttl_octets"},
	{"rmac_accepted_ucst_frms"},
	{"rmac_accepted_nucst_frms"},
	{"rmac_discarded_frms"},
	{"rmac_drop_events"},
	{"rmac_ttl_less_fb_octets"},
	{"rmac_ttl_frms"},
	{"rmac_usized_frms"},
	{"rmac_osized_frms"},
	{"rmac_frag_frms"},
	{"rmac_jabber_frms"},
	{"rmac_ttl_64_frms"},
	{"rmac_ttl_65_127_frms"},
	{"rmac_ttl_128_255_frms"},
	{"rmac_ttl_256_511_frms"},
	{"rmac_ttl_512_1023_frms"},
	{"rmac_ttl_1024_1518_frms"},
	{"rmac_ip"},
	{"rmac_ip_octets"},
	{"rmac_hdr_err_ip"},
	{"rmac_drop_ip"},
	{"rmac_icmp"},
	{"rmac_tcp"},
	{"rmac_udp"},
	{"rmac_err_drp_udp"},
	{"rmac_xgmii_err_sym"},
	{"rmac_frms_q0"},
	{"rmac_frms_q1"},
	{"rmac_frms_q2"},
	{"rmac_frms_q3"},
	{"rmac_frms_q4"},
	{"rmac_frms_q5"},
	{"rmac_frms_q6"},
	{"rmac_frms_q7"},
	{"rmac_full_q0"},
	{"rmac_full_q1"},
	{"rmac_full_q2"},
	{"rmac_full_q3"},
	{"rmac_full_q4"},
	{"rmac_full_q5"},
	{"rmac_full_q6"},
	{"rmac_full_q7"},
	{"rmac_pause_cnt"},
	{"rmac_xgmii_data_err_cnt"},
	{"rmac_xgmii_ctrl_err_cnt"},
	{"rmac_accepted_ip"},
	{"rmac_err_tcp"},
	{"rd_req_cnt"},
	{"new_rd_req_cnt"},
	{"new_rd_req_rtry_cnt"},
	{"rd_rtry_cnt"},
	{"wr_rtry_rd_ack_cnt"},
	{"wr_req_cnt"},
	{"new_wr_req_cnt"},
	{"new_wr_req_rtry_cnt"},
	{"wr_rtry_cnt"},
	{"wr_disc_cnt"},
	{"rd_rtry_wr_ack_cnt"},
	{"txp_wr_cnt"},
	{"txd_rd_cnt"},
	{"txd_wr_cnt"},
	{"rxd_rd_cnt"},
	{"rxd_wr_cnt"},
	{"txf_rd_cnt"},
	{"rxf_wr_cnt"}
};

static char ethtool_enhanced_stats_keys[][ETH_GSTRING_LEN] = {
	{"rmac_ttl_1519_4095_frms"},
	{"rmac_ttl_4096_8191_frms"},
	{"rmac_ttl_8192_max_frms"},
	{"rmac_ttl_gt_max_frms"},
	{"rmac_osized_alt_frms"},
	{"rmac_jabber_alt_frms"},
	{"rmac_gt_max_alt_frms"},
	{"rmac_vlan_frms"},
	{"rmac_len_discard"},
	{"rmac_fcs_discard"},
	{"rmac_pf_discard"},
	{"rmac_da_discard"},
	{"rmac_red_discard"},
	{"rmac_rts_discard"},
	{"rmac_ingm_full_discard"},
	{"link_fault_cnt"}
};

static char ethtool_driver_stats_keys[][ETH_GSTRING_LEN] = {
	{"\n DRIVER STATISTICS"},
	{"single_bit_ecc_errs"},
	{"double_bit_ecc_errs"},
	{"parity_err_cnt"},
	{"serious_err_cnt"},
	{"soft_reset_cnt"},
	{"fifo_full_cnt"},
	{"ring_full_cnt"},
	("alarm_transceiver_temp_high"),
	("alarm_transceiver_temp_low"),
	("alarm_laser_bias_current_high"),
	("alarm_laser_bias_current_low"),
	("alarm_laser_output_power_high"),
	("alarm_laser_output_power_low"),
	("warn_transceiver_temp_high"),
	("warn_transceiver_temp_low"),
	("warn_laser_bias_current_high"),
	("warn_laser_bias_current_low"),
	("warn_laser_output_power_high"),
	("warn_laser_output_power_low"),
	("lro_aggregated_pkts"),
	("lro_flush_both_count"),
	("lro_out_of_sequence_pkts"),
	("lro_flush_due_to_max_pkts"),
	("lro_avg_aggr_pkts"),
	("mem_alloc_fail_cnt"),
	("watchdog_timer_cnt"),
	("mem_allocated"),
	("mem_freed"),
	("link_up_cnt"),
	("link_down_cnt"),
	("link_up_time"),
	("link_down_time"),
	("tx_tcode_buf_abort_cnt"),
	("tx_tcode_desc_abort_cnt"),
	("tx_tcode_parity_err_cnt"),
	("tx_tcode_link_loss_cnt"),
	("tx_tcode_list_proc_err_cnt"),
	("rx_tcode_parity_err_cnt"),
	("rx_tcode_abort_cnt"),
	("rx_tcode_parity_abort_cnt"),
	("rx_tcode_rda_fail_cnt"),
	("rx_tcode_unkn_prot_cnt"),
	("rx_tcode_fcs_err_cnt"),
	("rx_tcode_buf_size_err_cnt"),
	("rx_tcode_rxd_corrupt_cnt"),
	("rx_tcode_unkn_err_cnt")
};

#define S2IO_XENA_STAT_LEN sizeof(ethtool_xena_stats_keys)/ ETH_GSTRING_LEN
#define S2IO_ENHANCED_STAT_LEN sizeof(ethtool_enhanced_stats_keys)/ \
					ETH_GSTRING_LEN
#define S2IO_DRIVER_STAT_LEN sizeof(ethtool_driver_stats_keys)/ ETH_GSTRING_LEN

#define XFRAME_I_STAT_LEN (S2IO_XENA_STAT_LEN + S2IO_DRIVER_STAT_LEN )
#define XFRAME_II_STAT_LEN (XFRAME_I_STAT_LEN + S2IO_ENHANCED_STAT_LEN )

#define XFRAME_I_STAT_STRINGS_LEN ( XFRAME_I_STAT_LEN * ETH_GSTRING_LEN )
#define XFRAME_II_STAT_STRINGS_LEN ( XFRAME_II_STAT_LEN * ETH_GSTRING_LEN )

#define S2IO_TEST_LEN	sizeof(s2io_gstrings) / ETH_GSTRING_LEN
#define S2IO_STRINGS_LEN	S2IO_TEST_LEN * ETH_GSTRING_LEN

#define S2IO_TIMER_CONF(timer, handle, arg, exp)		\
			init_timer(&timer);			\
			timer.function = handle;		\
			timer.data = (unsigned long) arg;	\
			mod_timer(&timer, (jiffies + exp))	\

/* Add the vlan */
static void s2io_vlan_rx_register(struct net_device *dev,
					struct vlan_group *grp)
{
	struct s2io_nic *nic = dev->priv;
	unsigned long flags;

	spin_lock_irqsave(&nic->tx_lock, flags);
	nic->vlgrp = grp;
	spin_unlock_irqrestore(&nic->tx_lock, flags);
}

/* A flag indicating whether 'RX_PA_CFG_STRIP_VLAN_TAG' bit is set or not */
static int vlan_strip_flag;

/* Unregister the vlan */
static void s2io_vlan_rx_kill_vid(struct net_device *dev, unsigned long vid)
{
	struct s2io_nic *nic = dev->priv;
	unsigned long flags;

	spin_lock_irqsave(&nic->tx_lock, flags);
	vlan_group_set_device(nic->vlgrp, vid, NULL);
	spin_unlock_irqrestore(&nic->tx_lock, flags);
}

/*
 * Constants to be programmed into the Xena's registers, to configure
 * the XAUI.
 */

#define	END_SIGN	0x0
static const u64 herc_act_dtx_cfg[] = {
	/* Set address */
	0x8000051536750000ULL, 0x80000515367500E0ULL,
	/* Write data */
	0x8000051536750004ULL, 0x80000515367500E4ULL,
	/* Set address */
	0x80010515003F0000ULL, 0x80010515003F00E0ULL,
	/* Write data */
	0x80010515003F0004ULL, 0x80010515003F00E4ULL,
	/* Set address */
	0x801205150D440000ULL, 0x801205150D4400E0ULL,
	/* Write data */
	0x801205150D440004ULL, 0x801205150D4400E4ULL,
	/* Set address */
	0x80020515F2100000ULL, 0x80020515F21000E0ULL,
	/* Write data */
	0x80020515F2100004ULL, 0x80020515F21000E4ULL,
	/* Done */
	END_SIGN
};

static const u64 xena_dtx_cfg[] = {
	/* Set address */
	0x8000051500000000ULL, 0x80000515000000E0ULL,
	/* Write data */
	0x80000515D9350004ULL, 0x80000515D93500E4ULL,
	/* Set address */
	0x8001051500000000ULL, 0x80010515000000E0ULL,
	/* Write data */
	0x80010515001E0004ULL, 0x80010515001E00E4ULL,
	/* Set address */
	0x8002051500000000ULL, 0x80020515000000E0ULL,
	/* Write data */
	0x80020515F2100004ULL, 0x80020515F21000E4ULL,
	END_SIGN
};

/*
 * Constants for Fixing the MacAddress problem seen mostly on
 * Alpha machines.
 */
static const u64 fix_mac[] = {
	0x0060000000000000ULL, 0x0060600000000000ULL,
	0x0040600000000000ULL, 0x0000600000000000ULL,
	0x0020600000000000ULL, 0x0060600000000000ULL,
	0x0020600000000000ULL, 0x0060600000000000ULL,
	0x0020600000000000ULL, 0x0060600000000000ULL,
	0x0020600000000000ULL, 0x0060600000000000ULL,
	0x0020600000000000ULL, 0x0060600000000000ULL,
	0x0020600000000000ULL, 0x0060600000000000ULL,
	0x0020600000000000ULL, 0x0060600000000000ULL,
	0x0020600000000000ULL, 0x0060600000000000ULL,
	0x0020600000000000ULL, 0x0060600000000000ULL,
	0x0020600000000000ULL, 0x0060600000000000ULL,
	0x0020600000000000ULL, 0x0000600000000000ULL,
	0x0040600000000000ULL, 0x0060600000000000ULL,
	END_SIGN
};

MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);


/* Module Loadable parameters. */
S2IO_PARM_INT(tx_fifo_num, 1);
S2IO_PARM_INT(rx_ring_num, 1);


S2IO_PARM_INT(rx_ring_mode, 1);
S2IO_PARM_INT(use_continuous_tx_intrs, 1);
S2IO_PARM_INT(rmac_pause_time, 0x100);
S2IO_PARM_INT(mc_pause_threshold_q0q3, 187);
S2IO_PARM_INT(mc_pause_threshold_q4q7, 187);
S2IO_PARM_INT(shared_splits, 0);
S2IO_PARM_INT(tmac_util_period, 5);
S2IO_PARM_INT(rmac_util_period, 5);
S2IO_PARM_INT(bimodal, 0);
S2IO_PARM_INT(l3l4hdr_size, 128);
/* Frequency of Rx desc syncs expressed as power of 2 */
S2IO_PARM_INT(rxsync_frequency, 3);
/* Interrupt type. Values can be 0(INTA), 1(MSI), 2(MSI_X) */
S2IO_PARM_INT(intr_type, 0);
/* Large receive offload feature */
S2IO_PARM_INT(lro, 0);
/* Max pkts to be aggregated by LRO at one time. If not specified,
 * aggregation happens until we hit max IP pkt size(64K)
 */
S2IO_PARM_INT(lro_max_pkts, 0xFFFF);
S2IO_PARM_INT(indicate_max_pkts, 0);

S2IO_PARM_INT(napi, 1);
S2IO_PARM_INT(ufo, 0);
S2IO_PARM_INT(vlan_tag_strip, NO_STRIP_IN_PROMISC);

static unsigned int tx_fifo_len[MAX_TX_FIFOS] =
    {DEFAULT_FIFO_0_LEN, [1 ...(MAX_TX_FIFOS - 1)] = DEFAULT_FIFO_1_7_LEN};
static unsigned int rx_ring_sz[MAX_RX_RINGS] =
    {[0 ...(MAX_RX_RINGS - 1)] = SMALL_BLK_CNT};
static unsigned int rts_frm_len[MAX_RX_RINGS] =
    {[0 ...(MAX_RX_RINGS - 1)] = 0 };

module_param_array(tx_fifo_len, uint, NULL, 0);
module_param_array(rx_ring_sz, uint, NULL, 0);
module_param_array(rts_frm_len, uint, NULL, 0);

/*
 * S2IO device table.
 * This table lists all the devices that this driver supports.
 */
static struct pci_device_id s2io_tbl[] __devinitdata = {
	{PCI_VENDOR_ID_S2IO, PCI_DEVICE_ID_S2IO_WIN,
	 PCI_ANY_ID, PCI_ANY_ID},
	{PCI_VENDOR_ID_S2IO, PCI_DEVICE_ID_S2IO_UNI,
	 PCI_ANY_ID, PCI_ANY_ID},
	{PCI_VENDOR_ID_S2IO, PCI_DEVICE_ID_HERC_WIN,
         PCI_ANY_ID, PCI_ANY_ID},
        {PCI_VENDOR_ID_S2IO, PCI_DEVICE_ID_HERC_UNI,
         PCI_ANY_ID, PCI_ANY_ID},
	{0,}
};

MODULE_DEVICE_TABLE(pci, s2io_tbl);

static struct pci_driver s2io_driver = {
      .name = "S2IO",
      .id_table = s2io_tbl,
      .probe = s2io_init_nic,
      .remove = __devexit_p(s2io_rem_nic),
};

/* A simplifier macro used both by init and free shared_mem Fns(). */
#define TXD_MEM_PAGE_CNT(len, per_each) ((len+per_each - 1) / per_each)

/**
 * init_shared_mem - Allocation and Initialization of Memory
 * @nic: Device private variable.
 * Description: The function allocates all the memory areas shared
 * between the NIC and the driver. This includes Tx descriptors,
 * Rx descriptors and the statistics block.
 */

static int init_shared_mem(struct s2io_nic *nic)
{
	u32 size;
	void *tmp_v_addr, *tmp_v_addr_next;
	dma_addr_t tmp_p_addr, tmp_p_addr_next;
	struct RxD_block *pre_rxd_blk = NULL;
	int i, j, blk_cnt;
	int lst_size, lst_per_page;
	struct net_device *dev = nic->dev;
	unsigned long tmp;
	struct buffAdd *ba;

	struct mac_info *mac_control;
	struct config_param *config;
	unsigned long long mem_allocated = 0;

	mac_control = &nic->mac_control;
	config = &nic->config;


	/* Allocation and initialization of TXDLs in FIOFs */
	size = 0;
	for (i = 0; i < config->tx_fifo_num; i++) {
		size += config->tx_cfg[i].fifo_len;
	}
	if (size > MAX_AVAILABLE_TXDS) {
		DBG_PRINT(ERR_DBG, "s2io: Requested TxDs too high, ");
		DBG_PRINT(ERR_DBG, "Requested: %d, max supported: 8192\n", size);
		return -EINVAL;
	}

	lst_size = (sizeof(struct TxD) * config->max_txds);
	lst_per_page = PAGE_SIZE / lst_size;

	for (i = 0; i < config->tx_fifo_num; i++) {
		int fifo_len = config->tx_cfg[i].fifo_len;
		int list_holder_size = fifo_len * sizeof(struct list_info_hold);
		mac_control->fifos[i].list_info = kmalloc(list_holder_size,
							  GFP_KERNEL);
		if (!mac_control->fifos[i].list_info) {
			DBG_PRINT(INFO_DBG,
				  "Malloc failed for list_info\n");
			return -ENOMEM;
		}
		mem_allocated += list_holder_size;
		memset(mac_control->fifos[i].list_info, 0, list_holder_size);
	}
	for (i = 0; i < config->tx_fifo_num; i++) {
		int page_num = TXD_MEM_PAGE_CNT(config->tx_cfg[i].fifo_len,
						lst_per_page);
		mac_control->fifos[i].tx_curr_put_info.offset = 0;
		mac_control->fifos[i].tx_curr_put_info.fifo_len =
		    config->tx_cfg[i].fifo_len - 1;
		mac_control->fifos[i].tx_curr_get_info.offset = 0;
		mac_control->fifos[i].tx_curr_get_info.fifo_len =
		    config->tx_cfg[i].fifo_len - 1;
		mac_control->fifos[i].fifo_no = i;
		mac_control->fifos[i].nic = nic;
		mac_control->fifos[i].max_txds = MAX_SKB_FRAGS + 2;

		for (j = 0; j < page_num; j++) {
			int k = 0;
			dma_addr_t tmp_p;
			void *tmp_v;
			tmp_v = pci_alloc_consistent(nic->pdev,
						     PAGE_SIZE, &tmp_p);
			if (!tmp_v) {
				DBG_PRINT(INFO_DBG,
					  "pci_alloc_consistent ");
				DBG_PRINT(INFO_DBG, "failed for TxDL\n");
				return -ENOMEM;
			}
			/* If we got a zero DMA address(can happen on
			 * certain platforms like PPC), reallocate.
			 * Store virtual address of page we don't want,
			 * to be freed later.
			 */
			if (!tmp_p) {
				mac_control->zerodma_virt_addr = tmp_v;
				DBG_PRINT(INIT_DBG,
				"%s: Zero DMA address for TxDL. ", dev->name);
				DBG_PRINT(INIT_DBG,
				"Virtual address %p\n", tmp_v);
				tmp_v = pci_alloc_consistent(nic->pdev,
						     PAGE_SIZE, &tmp_p);
				if (!tmp_v) {
					DBG_PRINT(INFO_DBG,
					  "pci_alloc_consistent ");
					DBG_PRINT(INFO_DBG, "failed for TxDL\n");
					return -ENOMEM;
				}
				mem_allocated += PAGE_SIZE;
			}
			while (k < lst_per_page) {
				int l = (j * lst_per_page) + k;
				if (l == config->tx_cfg[i].fifo_len)
					break;
				mac_control->fifos[i].list_info[l].list_virt_addr =
				    tmp_v + (k * lst_size);
				mac_control->fifos[i].list_info[l].list_phy_addr =
				    tmp_p + (k * lst_size);
				k++;
			}
		}
	}

	nic->ufo_in_band_v = kcalloc(size, sizeof(u64), GFP_KERNEL);
	if (!nic->ufo_in_band_v)
		return -ENOMEM;
	 mem_allocated += (size * sizeof(u64));

	/* Allocation and initialization of RXDs in Rings */
	size = 0;
	for (i = 0; i < config->rx_ring_num; i++) {
		if (config->rx_cfg[i].num_rxd %
		    (rxd_count[nic->rxd_mode] + 1)) {
			DBG_PRINT(ERR_DBG, "%s: RxD count of ", dev->name);
			DBG_PRINT(ERR_DBG, "Ring%d is not a multiple of ",
				  i);
			DBG_PRINT(ERR_DBG, "RxDs per Block");
			return FAILURE;
		}
		size += config->rx_cfg[i].num_rxd;
		mac_control->rings[i].block_count =
			config->rx_cfg[i].num_rxd /
			(rxd_count[nic->rxd_mode] + 1 );
		mac_control->rings[i].pkt_cnt = config->rx_cfg[i].num_rxd -
			mac_control->rings[i].block_count;
	}
	if (nic->rxd_mode == RXD_MODE_1)
		size = (size * (sizeof(struct RxD1)));
	else
		size = (size * (sizeof(struct RxD3)));

	for (i = 0; i < config->rx_ring_num; i++) {
		mac_control->rings[i].rx_curr_get_info.block_index = 0;
		mac_control->rings[i].rx_curr_get_info.offset = 0;
		mac_control->rings[i].rx_curr_get_info.ring_len =
		    config->rx_cfg[i].num_rxd - 1;
		mac_control->rings[i].rx_curr_put_info.block_index = 0;
		mac_control->rings[i].rx_curr_put_info.offset = 0;
		mac_control->rings[i].rx_curr_put_info.ring_len =
		    config->rx_cfg[i].num_rxd - 1;
		mac_control->rings[i].nic = nic;
		mac_control->rings[i].ring_no = i;

		blk_cnt = config->rx_cfg[i].num_rxd /
				(rxd_count[nic->rxd_mode] + 1);
		/*  Allocating all the Rx blocks */
		for (j = 0; j < blk_cnt; j++) {
			struct rx_block_info *rx_blocks;
			int l;

			rx_blocks = &mac_control->rings[i].rx_blocks[j];
			size = SIZE_OF_BLOCK; //size is always page size
			tmp_v_addr = pci_alloc_consistent(nic->pdev, size,
							  &tmp_p_addr);
			if (tmp_v_addr == NULL) {
				/*
				 * In case of failure, free_shared_mem()
				 * is called, which should free any
				 * memory that was alloced till the
				 * failure happened.
				 */
				rx_blocks->block_virt_addr = tmp_v_addr;
				return -ENOMEM;
			}
			mem_allocated += size;
			memset(tmp_v_addr, 0, size);
			rx_blocks->block_virt_addr = tmp_v_addr;
			rx_blocks->block_dma_addr = tmp_p_addr;
			rx_blocks->rxds = kmalloc(sizeof(struct rxd_info)*
						  rxd_count[nic->rxd_mode],
						  GFP_KERNEL);
			if (!rx_blocks->rxds)
				return -ENOMEM;
			mem_allocated += 
			(sizeof(struct rxd_info)* rxd_count[nic->rxd_mode]);
			for (l=0; l<rxd_count[nic->rxd_mode];l++) {
				rx_blocks->rxds[l].virt_addr =
					rx_blocks->block_virt_addr +
					(rxd_size[nic->rxd_mode] * l);
				rx_blocks->rxds[l].dma_addr =
					rx_blocks->block_dma_addr +
					(rxd_size[nic->rxd_mode] * l);
			}
		}
		/* Interlinking all Rx Blocks */
		for (j = 0; j < blk_cnt; j++) {
			tmp_v_addr =
				mac_control->rings[i].rx_blocks[j].block_virt_addr;
			tmp_v_addr_next =
				mac_control->rings[i].rx_blocks[(j + 1) %
					      blk_cnt].block_virt_addr;
			tmp_p_addr =
				mac_control->rings[i].rx_blocks[j].block_dma_addr;
			tmp_p_addr_next =
				mac_control->rings[i].rx_blocks[(j + 1) %
					      blk_cnt].block_dma_addr;

			pre_rxd_blk = (struct RxD_block *) tmp_v_addr;
			pre_rxd_blk->reserved_2_pNext_RxD_block =
			    (unsigned long) tmp_v_addr_next;
			pre_rxd_blk->pNext_RxD_Blk_physical =
			    (u64) tmp_p_addr_next;
		}
	}
	if (nic->rxd_mode >= RXD_MODE_3A) {
		/*
		 * Allocation of Storages for buffer addresses in 2BUFF mode
		 * and the buffers as well.
		 */
		for (i = 0; i < config->rx_ring_num; i++) {
			blk_cnt = config->rx_cfg[i].num_rxd /
			   (rxd_count[nic->rxd_mode]+ 1);
			mac_control->rings[i].ba =
				kmalloc((sizeof(struct buffAdd *) * blk_cnt),
				     GFP_KERNEL);
			if (!mac_control->rings[i].ba)
				return -ENOMEM;
			mem_allocated +=(sizeof(struct buffAdd *) * blk_cnt);
			for (j = 0; j < blk_cnt; j++) {
				int k = 0;
				mac_control->rings[i].ba[j] =
					kmalloc((sizeof(struct buffAdd) *
						(rxd_count[nic->rxd_mode] + 1)),
						GFP_KERNEL);
				if (!mac_control->rings[i].ba[j])
					return -ENOMEM;
				mem_allocated += (sizeof(struct buffAdd) *  \
					(rxd_count[nic->rxd_mode] + 1));
				while (k != rxd_count[nic->rxd_mode]) {
					ba = &mac_control->rings[i].ba[j][k];

					ba->ba_0_org = (void *) kmalloc
					    (BUF0_LEN + ALIGN_SIZE, GFP_KERNEL);
					if (!ba->ba_0_org)
						return -ENOMEM;
					mem_allocated += 
						(BUF0_LEN + ALIGN_SIZE);
					tmp = (unsigned long)ba->ba_0_org;
					tmp += ALIGN_SIZE;
					tmp &= ~((unsigned long) ALIGN_SIZE);
					ba->ba_0 = (void *) tmp;

					ba->ba_1_org = (void *) kmalloc
					    (BUF1_LEN + ALIGN_SIZE, GFP_KERNEL);
					if (!ba->ba_1_org)
						return -ENOMEM;
					mem_allocated 
						+= (BUF1_LEN + ALIGN_SIZE);
					tmp = (unsigned long) ba->ba_1_org;
					tmp += ALIGN_SIZE;
					tmp &= ~((unsigned long) ALIGN_SIZE);
					ba->ba_1 = (void *) tmp;
					k++;
				}
			}
		}
	}

	/* Allocation and initialization of Statistics block */
	size = sizeof(struct stat_block);
	mac_control->stats_mem = pci_alloc_consistent
	    (nic->pdev, size, &mac_control->stats_mem_phy);

	if (!mac_control->stats_mem) {
		/*
		 * In case of failure, free_shared_mem() is called, which
		 * should free any memory that was alloced till the
		 * failure happened.
		 */
		return -ENOMEM;
	}
	mem_allocated += size;
	mac_control->stats_mem_sz = size;

	tmp_v_addr = mac_control->stats_mem;
	mac_control->stats_info = (struct stat_block *) tmp_v_addr;
	memset(tmp_v_addr, 0, size);
	DBG_PRINT(INIT_DBG, "%s:Ring Mem PHY: 0x%llx\n", dev->name,
		  (unsigned long long) tmp_p_addr);
	mac_control->stats_info->sw_stat.mem_allocated += mem_allocated;
	return SUCCESS;
}

/**
 * free_shared_mem - Free the allocated Memory
 * @nic:  Device private variable.
 * Description: This function is to free all memory locations allocated by
 * the init_shared_mem() function and return it to the kernel.
 */

static void free_shared_mem(struct s2io_nic *nic)
{
	int i, j, blk_cnt, size;
	u32 ufo_size = 0;
	void *tmp_v_addr;
	dma_addr_t tmp_p_addr;
	struct mac_info *mac_control;
	struct config_param *config;
	int lst_size, lst_per_page;
	struct net_device *dev = nic->dev;
	int page_num = 0;

	if (!nic)
		return;

	mac_control = &nic->mac_control;
	config = &nic->config;

	lst_size = (sizeof(struct TxD) * config->max_txds);
	lst_per_page = PAGE_SIZE / lst_size;

	for (i = 0; i < config->tx_fifo_num; i++) {
		ufo_size += config->tx_cfg[i].fifo_len;
		page_num = TXD_MEM_PAGE_CNT(config->tx_cfg[i].fifo_len,
							lst_per_page);
		for (j = 0; j < page_num; j++) {
			int mem_blks = (j * lst_per_page);
			if (!mac_control->fifos[i].list_info)
				return;
			if (!mac_control->fifos[i].list_info[mem_blks].
				 list_virt_addr)
				break;
			pci_free_consistent(nic->pdev, PAGE_SIZE,
					    mac_control->fifos[i].
					    list_info[mem_blks].
					    list_virt_addr,
					    mac_control->fifos[i].
					    list_info[mem_blks].
					    list_phy_addr);
			nic->mac_control.stats_info->sw_stat.mem_freed 
						+= PAGE_SIZE;
		}
		/* If we got a zero DMA address during allocation,
		 * free the page now
		 */
		if (mac_control->zerodma_virt_addr) {
			pci_free_consistent(nic->pdev, PAGE_SIZE,
					    mac_control->zerodma_virt_addr,
					    (dma_addr_t)0);
			DBG_PRINT(INIT_DBG,
			  	"%s: Freeing TxDL with zero DMA addr. ",
				dev->name);
			DBG_PRINT(INIT_DBG, "Virtual address %p\n",
				mac_control->zerodma_virt_addr);
			nic->mac_control.stats_info->sw_stat.mem_freed 
						+= PAGE_SIZE;
		}
		kfree(mac_control->fifos[i].list_info);
		nic->mac_control.stats_info->sw_stat.mem_freed += 
		(nic->config.tx_cfg[i].fifo_len *sizeof(struct list_info_hold));
	}

	size = SIZE_OF_BLOCK;
	for (i = 0; i < config->rx_ring_num; i++) {
		blk_cnt = mac_control->rings[i].block_count;
		for (j = 0; j < blk_cnt; j++) {
			tmp_v_addr = mac_control->rings[i].rx_blocks[j].
				block_virt_addr;
			tmp_p_addr = mac_control->rings[i].rx_blocks[j].
				block_dma_addr;
			if (tmp_v_addr == NULL)
				break;
			pci_free_consistent(nic->pdev, size,
					    tmp_v_addr, tmp_p_addr);
			nic->mac_control.stats_info->sw_stat.mem_freed += size;
			kfree(mac_control->rings[i].rx_blocks[j].rxds);
			nic->mac_control.stats_info->sw_stat.mem_freed += 
			( sizeof(struct rxd_info)* rxd_count[nic->rxd_mode]);
		}
	}

	if (nic->rxd_mode >= RXD_MODE_3A) {
		/* Freeing buffer storage addresses in 2BUFF mode. */
		for (i = 0; i < config->rx_ring_num; i++) {
			blk_cnt = config->rx_cfg[i].num_rxd /
			    (rxd_count[nic->rxd_mode] + 1);
			for (j = 0; j < blk_cnt; j++) {
				int k = 0;
				if (!mac_control->rings[i].ba[j])
					continue;
				while (k != rxd_count[nic->rxd_mode]) {
					struct buffAdd *ba =
						&mac_control->rings[i].ba[j][k];
					kfree(ba->ba_0_org);
					nic->mac_control.stats_info->sw_stat.\
					mem_freed += (BUF0_LEN + ALIGN_SIZE);
					kfree(ba->ba_1_org);
					nic->mac_control.stats_info->sw_stat.\
					mem_freed += (BUF1_LEN + ALIGN_SIZE);
					k++;
				}
				kfree(mac_control->rings[i].ba[j]);
				nic->mac_control.stats_info->sw_stat.mem_freed 				+= (sizeof(struct buffAdd) * 
				(rxd_count[nic->rxd_mode] + 1));
			}
			kfree(mac_control->rings[i].ba);
			nic->mac_control.stats_info->sw_stat.mem_freed += 
			(sizeof(struct buffAdd *) * blk_cnt);
		}
	}

	if (mac_control->stats_mem) {
		pci_free_consistent(nic->pdev,
				    mac_control->stats_mem_sz,
				    mac_control->stats_mem,
				    mac_control->stats_mem_phy);
		nic->mac_control.stats_info->sw_stat.mem_freed += 
			mac_control->stats_mem_sz;
	}
	if (nic->ufo_in_band_v) {
		kfree(nic->ufo_in_band_v);
		nic->mac_control.stats_info->sw_stat.mem_freed 
			+= (ufo_size * sizeof(u64));
	}
}

/**
 * s2io_verify_pci_mode -
 */

static int s2io_verify_pci_mode(struct s2io_nic *nic)
{
	struct XENA_dev_config __iomem *bar0 = nic->bar0;
	register u64 val64 = 0;
	int     mode;

	val64 = readq(&bar0->pci_mode);
	mode = (u8)GET_PCI_MODE(val64);

	if ( val64 & PCI_MODE_UNKNOWN_MODE)
		return -1;      /* Unknown PCI mode */
	return mode;
}

#define NEC_VENID   0x1033
#define NEC_DEVID   0x0125
static int s2io_on_nec_bridge(struct pci_dev *s2io_pdev)
{
	struct pci_dev *tdev = NULL;
	while ((tdev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, tdev)) != NULL) {
		if (tdev->vendor == NEC_VENID && tdev->device == NEC_DEVID) {
			if (tdev->bus == s2io_pdev->bus->parent)
				pci_dev_put(tdev);
				return 1;
		}
	}
	return 0;
}

static int bus_speed[8] = {33, 133, 133, 200, 266, 133, 200, 266};
/**
 * s2io_print_pci_mode -
 */
static int s2io_print_pci_mode(struct s2io_nic *nic)
{
	struct XENA_dev_config __iomem *bar0 = nic->bar0;
	register u64 val64 = 0;
	int	mode;
	struct config_param *config = &nic->config;

	val64 = readq(&bar0->pci_mode);
	mode = (u8)GET_PCI_MODE(val64);

	if ( val64 & PCI_MODE_UNKNOWN_MODE)
		return -1;	/* Unknown PCI mode */

	config->bus_speed = bus_speed[mode];

	if (s2io_on_nec_bridge(nic->pdev)) {
		DBG_PRINT(ERR_DBG, "%s: Device is on PCI-E bus\n",
							nic->dev->name);
		return mode;
	}

	if (val64 & PCI_MODE_32_BITS) {
		DBG_PRINT(ERR_DBG, "%s: Device is on 32 bit ", nic->dev->name);
	} else {
		DBG_PRINT(ERR_DBG, "%s: Device is on 64 bit ", nic->dev->name);
	}

	switch(mode) {
		case PCI_MODE_PCI_33:
			DBG_PRINT(ERR_DBG, "33MHz PCI bus\n");
			break;
		case PCI_MODE_PCI_66:
			DBG_PRINT(ERR_DBG, "66MHz PCI bus\n");
			break;
		case PCI_MODE_PCIX_M1_66:
			DBG_PRINT(ERR_DBG, "66MHz PCIX(M1) bus\n");
			break;
		case PCI_MODE_PCIX_M1_100:
			DBG_PRINT(ERR_DBG, "100MHz PCIX(M1) bus\n");
			break;
		case PCI_MODE_PCIX_M1_133:
			DBG_PRINT(ERR_DBG, "133MHz PCIX(M1) bus\n");
			break;
		case PCI_MODE_PCIX_M2_66:
			DBG_PRINT(ERR_DBG, "133MHz PCIX(M2) bus\n");
			break;
		case PCI_MODE_PCIX_M2_100:
			DBG_PRINT(ERR_DBG, "200MHz PCIX(M2) bus\n");
			break;
		case PCI_MODE_PCIX_M2_133:
			DBG_PRINT(ERR_DBG, "266MHz PCIX(M2) bus\n");
			break;
		default:
			return -1;	/* Unsupported bus speed */
	}

	return mode;
}

/**
 *  init_nic - Initialization of hardware
 *  @nic: device peivate variable
 *  Description: The function sequentially configures every block
 *  of the H/W from their reset values.
 *  Return Value:  SUCCESS on success and
 *  '-1' on failure (endian settings incorrect).
 */

static int init_nic(struct s2io_nic *nic)
{
	struct XENA_dev_config __iomem *bar0 = nic->bar0;
	struct net_device *dev = nic->dev;
	register u64 val64 = 0;
	void __iomem *add;
	u32 time;
	int i, j;
	struct mac_info *mac_control;
	struct config_param *config;
	int dtx_cnt = 0;
	unsigned long long mem_share;
	int mem_size;

	mac_control = &nic->mac_control;
	config = &nic->config;

	/* to set the swapper controle on the card */
	if(s2io_set_swapper(nic)) {
		DBG_PRINT(ERR_DBG,"ERROR: Setting Swapper failed\n");
		return -1;
	}

	/*
	 * Herc requires EOI to be removed from reset before XGXS, so..
	 */
	if (nic->device_type & XFRAME_II_DEVICE) {
		val64 = 0xA500000000ULL;
		writeq(val64, &bar0->sw_reset);
		msleep(500);
		val64 = readq(&bar0->sw_reset);
	}

	/* Remove XGXS from reset state */
	val64 = 0;
	writeq(val64, &bar0->sw_reset);
	msleep(500);
	val64 = readq(&bar0->sw_reset);

	/*  Enable Receiving broadcasts */
	add = &bar0->mac_cfg;
	val64 = readq(&bar0->mac_cfg);
	val64 |= MAC_RMAC_BCAST_ENABLE;
	writeq(RMAC_CFG_KEY(0x4C0D), &bar0->rmac_cfg_key);
	writel((u32) val64, add);
	writeq(RMAC_CFG_KEY(0x4C0D), &bar0->rmac_cfg_key);
	writel((u32) (val64 >> 32), (add + 4));

	/* Read registers in all blocks */
	val64 = readq(&bar0->mac_int_mask);
	val64 = readq(&bar0->mc_int_mask);
	val64 = readq(&bar0->xgxs_int_mask);

	/*  Set MTU */
	val64 = dev->mtu;
	writeq(vBIT(val64, 2, 14), &bar0->rmac_max_pyld_len);

	if (nic->device_type & XFRAME_II_DEVICE) {
		while (herc_act_dtx_cfg[dtx_cnt] != END_SIGN) {
			SPECIAL_REG_WRITE(herc_act_dtx_cfg[dtx_cnt],
					  &bar0->dtx_control, UF);
			if (dtx_cnt & 0x1)
				msleep(1); /* Necessary!! */
			dtx_cnt++;
		}
	} else {
		while (xena_dtx_cfg[dtx_cnt] != END_SIGN) {
			SPECIAL_REG_WRITE(xena_dtx_cfg[dtx_cnt],
					  &bar0->dtx_control, UF);
			val64 = readq(&bar0->dtx_control);
			dtx_cnt++;
		}
	}

	/*  Tx DMA Initialization */
	val64 = 0;
	writeq(val64, &bar0->tx_fifo_partition_0);
	writeq(val64, &bar0->tx_fifo_partition_1);
	writeq(val64, &bar0->tx_fifo_partition_2);
	writeq(val64, &bar0->tx_fifo_partition_3);


	for (i = 0, j = 0; i < config->tx_fifo_num; i++) {
		val64 |=
		    vBIT(config->tx_cfg[i].fifo_len - 1, ((i * 32) + 19),
			 13) | vBIT(config->tx_cfg[i].fifo_priority,
				    ((i * 32) + 5), 3);

		if (i == (config->tx_fifo_num - 1)) {
			if (i % 2 == 0)
				i++;
		}

		switch (i) {
		case 1:
			writeq(val64, &bar0->tx_fifo_partition_0);
			val64 = 0;
			break;
		case 3:
			writeq(val64, &bar0->tx_fifo_partition_1);
			val64 = 0;
			break;
		case 5:
			writeq(val64, &bar0->tx_fifo_partition_2);
			val64 = 0;
			break;
		case 7:
			writeq(val64, &bar0->tx_fifo_partition_3);
			break;
		}
	}

	/*
	 * Disable 4 PCCs for Xena1, 2 and 3 as per H/W bug
	 * SXE-008 TRANSMIT DMA ARBITRATION ISSUE.
	 */
	if ((nic->device_type == XFRAME_I_DEVICE) &&
		(get_xena_rev_id(nic->pdev) < 4))
		writeq(PCC_ENABLE_FOUR, &bar0->pcc_enable);

	val64 = readq(&bar0->tx_fifo_partition_0);
	DBG_PRINT(INIT_DBG, "Fifo partition at: 0x%p is: 0x%llx\n",
		  &bar0->tx_fifo_partition_0, (unsigned long long) val64);

	/*
	 * Initialization of Tx_PA_CONFIG register to ignore packet
	 * integrity checking.
	 */
	val64 = readq(&bar0->tx_pa_cfg);
	val64 |= TX_PA_CFG_IGNORE_FRM_ERR | TX_PA_CFG_IGNORE_SNAP_OUI |
	    TX_PA_CFG_IGNORE_LLC_CTRL | TX_PA_CFG_IGNORE_L2_ERR;
	writeq(val64, &bar0->tx_pa_cfg);

	/* Rx DMA intialization. */
	val64 = 0;
	for (i = 0; i < config->rx_ring_num; i++) {
		val64 |=
		    vBIT(config->rx_cfg[i].ring_priority, (5 + (i * 8)),
			 3);
	}
	writeq(val64, &bar0->rx_queue_priority);

	/*
	 * Allocating equal share of memory to all the
	 * configured Rings.
	 */
	val64 = 0;
	if (nic->device_type & XFRAME_II_DEVICE)
		mem_size = 32;
	else
		mem_size = 64;

	for (i = 0; i < config->rx_ring_num; i++) {
		switch (i) {
		case 0:
			mem_share = (mem_size / config->rx_ring_num +
				     mem_size % config->rx_ring_num);
			val64 |= RX_QUEUE_CFG_Q0_SZ(mem_share);
			continue;
		case 1:
			mem_share = (mem_size / config->rx_ring_num);
			val64 |= RX_QUEUE_CFG_Q1_SZ(mem_share);
			continue;
		case 2:
			mem_share = (mem_size / config->rx_ring_num);
			val64 |= RX_QUEUE_CFG_Q2_SZ(mem_share);
			continue;
		case 3:
			mem_share = (mem_size / config->rx_ring_num);
			val64 |= RX_QUEUE_CFG_Q3_SZ(mem_share);
			continue;
		case 4:
			mem_share = (mem_size / config->rx_ring_num);
			val64 |= RX_QUEUE_CFG_Q4_SZ(mem_share);
			continue;
		case 5:
			mem_share = (mem_size / config->rx_ring_num);
			val64 |= RX_QUEUE_CFG_Q5_SZ(mem_share);
			continue;
		case 6:
			mem_share = (mem_size / config->rx_ring_num);
			val64 |= RX_QUEUE_CFG_Q6_SZ(mem_share);
			continue;
		case 7:
			mem_share = (mem_size / config->rx_ring_num);
			val64 |= RX_QUEUE_CFG_Q7_SZ(mem_share);
			continue;
		}
	}
	writeq(val64, &bar0->rx_queue_cfg);

	/*
	 * Filling Tx round robin registers
	 * as per the number of FIFOs
	 */
	switch (config->tx_fifo_num) {
	case 1:
		val64 = 0x0000000000000000ULL;
		writeq(val64, &bar0->tx_w_round_robin_0);
		writeq(val64, &bar0->tx_w_round_robin_1);
		writeq(val64, &bar0->tx_w_round_robin_2);
		writeq(val64, &bar0->tx_w_round_robin_3);
		writeq(val64, &bar0->tx_w_round_robin_4);
		break;
	case 2:
		val64 = 0x0000010000010000ULL;
		writeq(val64, &bar0->tx_w_round_robin_0);
		val64 = 0x0100000100000100ULL;
		writeq(val64, &bar0->tx_w_round_robin_1);
		val64 = 0x0001000001000001ULL;
		writeq(val64, &bar0->tx_w_round_robin_2);
		val64 = 0x0000010000010000ULL;
		writeq(val64, &bar0->tx_w_round_robin_3);
		val64 = 0x0100000000000000ULL;
		writeq(val64, &bar0->tx_w_round_robin_4);
		break;
	case 3:
		val64 = 0x0001000102000001ULL;
		writeq(val64, &bar0->tx_w_round_robin_0);
		val64 = 0x0001020000010001ULL;
		writeq(val64, &bar0->tx_w_round_robin_1);
		val64 = 0x0200000100010200ULL;
		writeq(val64, &bar0->tx_w_round_robin_2);
		val64 = 0x0001000102000001ULL;
		writeq(val64, &bar0->tx_w_round_robin_3);
		val64 = 0x0001020000000000ULL;
		writeq(val64, &bar0->tx_w_round_robin_4);
		break;
	case 4:
		val64 = 0x0001020300010200ULL;
		writeq(val64, &bar0->tx_w_round_robin_0);
		val64 = 0x0100000102030001ULL;
		writeq(val64, &bar0->tx_w_round_robin_1);
		val64 = 0x0200010000010203ULL;
		writeq(val64, &bar0->tx_w_round_robin_2);
		val64 = 0x0001020001000001ULL;
		writeq(val64, &bar0->tx_w_round_robin_3);
		val64 = 0x0203000100000000ULL;
		writeq(val64, &bar0->tx_w_round_robin_4);
		break;
	case 5:
		val64 = 0x0001000203000102ULL;
		writeq(val64, &bar0->tx_w_round_robin_0);
		val64 = 0x0001020001030004ULL;
		writeq(val64, &bar0->tx_w_round_robin_1);
		val64 = 0x0001000203000102ULL;
		writeq(val64, &bar0->tx_w_round_robin_2);
		val64 = 0x0001020001030004ULL;
		writeq(val64, &bar0->tx_w_round_robin_3);
		val64 = 0x0001000000000000ULL;
		writeq(val64, &bar0->tx_w_round_robin_4);
		break;
	case 6:
		val64 = 0x0001020304000102ULL;
		writeq(val64, &bar0->tx_w_round_robin_0);
		val64 = 0x0304050001020001ULL;
		writeq(val64, &bar0->tx_w_round_robin_1);
		val64 = 0x0203000100000102ULL;
		writeq(val64, &bar0->tx_w_round_robin_2);
		val64 = 0x0304000102030405ULL;
		writeq(val64, &bar0->tx_w_round_robin_3);
		val64 = 0x0001000200000000ULL;
		writeq(val64, &bar0->tx_w_round_robin_4);
		break;
	case 7:
		val64 = 0x0001020001020300ULL;
		writeq(val64, &bar0->tx_w_round_robin_0);
		val64 = 0x0102030400010203ULL;
		writeq(val64, &bar0->tx_w_round_robin_1);
		val64 = 0x0405060001020001ULL;
		writeq(val64, &bar0->tx_w_round_robin_2);
		val64 = 0x0304050000010200ULL;
		writeq(val64, &bar0->tx_w_round_robin_3);
		val64 = 0x0102030000000000ULL;
		writeq(val64, &bar0->tx_w_round_robin_4);
		break;
	case 8:
		val64 = 0x0001020300040105ULL;
		writeq(val64, &bar0->tx_w_round_robin_0);
		val64 = 0x0200030106000204ULL;
		writeq(val64, &bar0->tx_w_round_robin_1);
		val64 = 0x0103000502010007ULL;
		writeq(val64, &bar0->tx_w_round_robin_2);
		val64 = 0x0304010002060500ULL;
		writeq(val64, &bar0->tx_w_round_robin_3);
		val64 = 0x0103020400000000ULL;
		writeq(val64, &bar0->tx_w_round_robin_4);
		break;
	}

	/* Enable all configured Tx FIFO partitions */
	val64 = readq(&bar0->tx_fifo_partition_0);
	val64 |= (TX_FIFO_PARTITION_EN);
	writeq(val64, &bar0->tx_fifo_partition_0);

	/* Filling the Rx round robin registers as per the
	 * number of Rings and steering based on QoS.
         */
	switch (config->rx_ring_num) {
	case 1:
		val64 = 0x8080808080808080ULL;
		writeq(val64, &bar0->rts_qos_steering);
		break;
	case 2:
		val64 = 0x0000010000010000ULL;
		writeq(val64, &bar0->rx_w_round_robin_0);
		val64 = 0x0100000100000100ULL;
		writeq(val64, &bar0->rx_w_round_robin_1);
		val64 = 0x0001000001000001ULL;
		writeq(val64, &bar0->rx_w_round_robin_2);
		val64 = 0x0000010000010000ULL;
		writeq(val64, &bar0->rx_w_round_robin_3);
		val64 = 0x0100000000000000ULL;
		writeq(val64, &bar0->rx_w_round_robin_4);

		val64 = 0x8080808040404040ULL;
		writeq(val64, &bar0->rts_qos_steering);
		break;
	case 3:
		val64 = 0x0001000102000001ULL;
		writeq(val64, &bar0->rx_w_round_robin_0);
		val64 = 0x0001020000010001ULL;
		writeq(val64, &bar0->rx_w_round_robin_1);
		val64 = 0x0200000100010200ULL;
		writeq(val64, &bar0->rx_w_round_robin_2);
		val64 = 0x0001000102000001ULL;
		writeq(val64, &bar0->rx_w_round_robin_3);
		val64 = 0x0001020000000000ULL;
		writeq(val64, &bar0->rx_w_round_robin_4);

		val64 = 0x8080804040402020ULL;
		writeq(val64, &bar0->rts_qos_steering);
		break;
	case 4:
		val64 = 0x0001020300010200ULL;
		writeq(val64, &bar0->rx_w_round_robin_0);
		val64 = 0x0100000102030001ULL;
		writeq(val64, &bar0->rx_w_round_robin_1);
		val64 = 0x0200010000010203ULL;
		writeq(val64, &bar0->rx_w_round_robin_2);
		val64 = 0x0001020001000001ULL;
		writeq(val64, &bar0->rx_w_round_robin_3);
		val64 = 0x0203000100000000ULL;
		writeq(val64, &bar0->rx_w_round_robin_4);

		val64 = 0x8080404020201010ULL;
		writeq(val64, &bar0->rts_qos_steering);
		break;
	case 5:
		val64 = 0x0001000203000102ULL;
		writeq(val64, &bar0->rx_w_round_robin_0);
		val64 = 0x0001020001030004ULL;
		writeq(val64, &bar0->rx_w_round_robin_1);
		val64 = 0x0001000203000102ULL;
		writeq(val64, &bar0->rx_w_round_robin_2);
		val64 = 0x0001020001030004ULL;
		writeq(val64, &bar0->rx_w_round_robin_3);
		val64 = 0x0001000000000000ULL;
		writeq(val64, &bar0->rx_w_round_robin_4);

		val64 = 0x8080404020201008ULL;
		writeq(val64, &bar0->rts_qos_steering);
		break;
	case 6:
		val64 = 0x0001020304000102ULL;
		writeq(val64, &bar0->rx_w_round_robin_0);
		val64 = 0x0304050001020001ULL;
		writeq(val64, &bar0->rx_w_round_robin_1);
		val64 = 0x0203000100000102ULL;
		writeq(val64, &bar0->rx_w_round_robin_2);
		val64 = 0x0304000102030405ULL;
		writeq(val64, &bar0->rx_w_round_robin_3);
		val64 = 0x0001000200000000ULL;
		writeq(val64, &bar0->rx_w_round_robin_4);

		val64 = 0x8080404020100804ULL;
		writeq(val64, &bar0->rts_qos_steering);
		break;
	case 7:
		val64 = 0x0001020001020300ULL;
		writeq(val64, &bar0->rx_w_round_robin_0);
		val64 = 0x0102030400010203ULL;
		writeq(val64, &bar0->rx_w_round_robin_1);
		val64 = 0x0405060001020001ULL;
		writeq(val64, &bar0->rx_w_round_robin_2);
		val64 = 0x0304050000010200ULL;
		writeq(val64, &bar0->rx_w_round_robin_3);
		val64 = 0x0102030000000000ULL;
		writeq(val64, &bar0->rx_w_round_robin_4);

		val64 = 0x8080402010080402ULL;
		writeq(val64, &bar0->rts_qos_steering);
		break;
	case 8:
		val64 = 0x0001020300040105ULL;
		writeq(val64, &bar0->rx_w_round_robin_0);
		val64 = 0x0200030106000204ULL;
		writeq(val64, &bar0->rx_w_round_robin_1);
		val64 = 0x0103000502010007ULL;
		writeq(val64, &bar0->rx_w_round_robin_2);
		val64 = 0x0304010002060500ULL;
		writeq(val64, &bar0->rx_w_round_robin_3);
		val64 = 0x0103020400000000ULL;
		writeq(val64, &bar0->rx_w_round_robin_4);

		val64 = 0x8040201008040201ULL;
		writeq(val64, &bar0->rts_qos_steering);
		break;
	}

	/* UDP Fix */
	val64 = 0;
	for (i = 0; i < 8; i++)
		writeq(val64, &bar0->rts_frm_len_n[i]);

	/* Set the default rts frame length for the rings configured */
	val64 = MAC_RTS_FRM_LEN_SET(dev->mtu+22);
	for (i = 0 ; i < config->rx_ring_num ; i++)
		writeq(val64, &bar0->rts_frm_len_n[i]);

	/* Set the frame length for the configured rings
	 * desired by the user
	 */
	for (i = 0; i < config->rx_ring_num; i++) {
		/* If rts_frm_len[i] == 0 then it is assumed that user not
		 * specified frame length steering.
		 * If the user provides the frame length then program
		 * the rts_frm_len register for those values or else
		 * leave it as it is.
		 */
		if (rts_frm_len[i] != 0) {
			writeq(MAC_RTS_FRM_LEN_SET(rts_frm_len[i]),
				&bar0->rts_frm_len_n[i]);
		}
	}
	
	/* Disable differentiated services steering logic */
	for (i = 0; i < 64; i++) {
		if (rts_ds_steer(nic, i, 0) == FAILURE) {
			DBG_PRINT(ERR_DBG, "%s: failed rts ds steering",
				dev->name);
			DBG_PRINT(ERR_DBG, "set on codepoint %d\n", i);
			return FAILURE;
		}
	}

	/* Program statistics memory */
	writeq(mac_control->stats_mem_phy, &bar0->stat_addr);

	if (nic->device_type == XFRAME_II_DEVICE) {
		val64 = STAT_BC(0x320);
		writeq(val64, &bar0->stat_byte_cnt);
	}

	/*
	 * Initializing the sampling rate for the device to calculate the
	 * bandwidth utilization.
	 */
	val64 = MAC_TX_LINK_UTIL_VAL(tmac_util_period) |
	    MAC_RX_LINK_UTIL_VAL(rmac_util_period);
	writeq(val64, &bar0->mac_link_util);


	/*
	 * Initializing the Transmit and Receive Traffic Interrupt
	 * Scheme.
	 */
	/*
	 * TTI Initialization. Default Tx timer gets us about
	 * 250 interrupts per sec. Continuous interrupts are enabled
	 * by default.
	 */
	if (nic->device_type == XFRAME_II_DEVICE) {
		int count = (nic->config.bus_speed * 125)/2;
		val64 = TTI_DATA1_MEM_TX_TIMER_VAL(count);
	} else {

		val64 = TTI_DATA1_MEM_TX_TIMER_VAL(0x2078);
	}
	val64 |= TTI_DATA1_MEM_TX_URNG_A(0xA) |
	    TTI_DATA1_MEM_TX_URNG_B(0x10) |
	    TTI_DATA1_MEM_TX_URNG_C(0x30) | TTI_DATA1_MEM_TX_TIMER_AC_EN;
		if (use_continuous_tx_intrs)
			val64 |= TTI_DATA1_MEM_TX_TIMER_CI_EN;
	writeq(val64, &bar0->tti_data1_mem);

	val64 = TTI_DATA2_MEM_TX_UFC_A(0x10) |
	    TTI_DATA2_MEM_TX_UFC_B(0x20) |
	    TTI_DATA2_MEM_TX_UFC_C(0x40) | TTI_DATA2_MEM_TX_UFC_D(0x80);
	writeq(val64, &bar0->tti_data2_mem);

	val64 = TTI_CMD_MEM_WE | TTI_CMD_MEM_STROBE_NEW_CMD;
	writeq(val64, &bar0->tti_command_mem);

	/*
	 * Once the operation completes, the Strobe bit of the command
	 * register will be reset. We poll for this particular condition
	 * We wait for a maximum of 500ms for the operation to complete,
	 * if it's not complete by then we return error.
	 */
	time = 0;
	while (TRUE) {
		val64 = readq(&bar0->tti_command_mem);
		if (!(val64 & TTI_CMD_MEM_STROBE_NEW_CMD)) {
			break;
		}
		if (time > 10) {
			DBG_PRINT(ERR_DBG, "%s: TTI init Failed\n",
				  dev->name);
			return -1;
		}
		msleep(50);
		time++;
	}

	if (nic->config.bimodal) {
		int k = 0;
		for (k = 0; k < config->rx_ring_num; k++) {
			val64 = TTI_CMD_MEM_WE | TTI_CMD_MEM_STROBE_NEW_CMD;
			val64 |= TTI_CMD_MEM_OFFSET(0x38+k);
			writeq(val64, &bar0->tti_command_mem);

		/*
		 * Once the operation completes, the Strobe bit of the command
		 * register will be reset. We poll for this particular condition
		 * We wait for a maximum of 500ms for the operation to complete,
		 * if it's not complete by then we return error.
		*/
			time = 0;
			while (TRUE) {
				val64 = readq(&bar0->tti_command_mem);
				if (!(val64 & TTI_CMD_MEM_STROBE_NEW_CMD)) {
					break;
				}
				if (time > 10) {
					DBG_PRINT(ERR_DBG,
						"%s: TTI init Failed\n",
					dev->name);
					return -1;
				}
				time++;
				msleep(50);
			}
		}
	} else {

		/* RTI Initialization */
		if (nic->device_type == XFRAME_II_DEVICE) {
			/*
			 * Programmed to generate Apprx 500 Intrs per
			 * second
			 */
			int count = (nic->config.bus_speed * 125)/4;
			val64 = RTI_DATA1_MEM_RX_TIMER_VAL(count);
		} else {
			val64 = RTI_DATA1_MEM_RX_TIMER_VAL(0xFFF);
		}
		val64 |= RTI_DATA1_MEM_RX_URNG_A(0xA) |
		    RTI_DATA1_MEM_RX_URNG_B(0x10) |
		    RTI_DATA1_MEM_RX_URNG_C(0x30) | RTI_DATA1_MEM_RX_TIMER_AC_EN;

		writeq(val64, &bar0->rti_data1_mem);

		val64 = RTI_DATA2_MEM_RX_UFC_A(0x1) |
		    RTI_DATA2_MEM_RX_UFC_B(0x2) ;
		if (nic->intr_type == MSI_X)
		    val64 |= (RTI_DATA2_MEM_RX_UFC_C(0x20) | \
				RTI_DATA2_MEM_RX_UFC_D(0x40));
		else
		    val64 |= (RTI_DATA2_MEM_RX_UFC_C(0x40) | \
				RTI_DATA2_MEM_RX_UFC_D(0x80));
		writeq(val64, &bar0->rti_data2_mem);

		for (i = 0; i < config->rx_ring_num; i++) {
			val64 = RTI_CMD_MEM_WE | RTI_CMD_MEM_STROBE_NEW_CMD
					| RTI_CMD_MEM_OFFSET(i);
			writeq(val64, &bar0->rti_command_mem);

			/*
			 * Once the operation completes, the Strobe bit of the
			 * command register will be reset. We poll for this
			 * particular condition. We wait for a maximum of 500ms
			 * for the operation to complete, if it's not complete
			 * by then we return error.
			 */
			time = 0;
			while (TRUE) {
				val64 = readq(&bar0->rti_command_mem);
				if (!(val64 & RTI_CMD_MEM_STROBE_NEW_CMD)) {
					break;
				}
				if (time > 10) {
					DBG_PRINT(ERR_DBG, "%s: RTI init Failed\n",
						  dev->name);
					return -1;
				}
				time++;
				msleep(50);
			}
		}
	}

	/*
	 * Initializing proper values as Pause threshold into all
	 * the 8 Queues on Rx side.
	 */
	writeq(0xffbbffbbffbbffbbULL, &bar0->mc_pause_thresh_q0q3);
	writeq(0xffbbffbbffbbffbbULL, &bar0->mc_pause_thresh_q4q7);

	/* Disable RMAC PAD STRIPPING */
	add = &bar0->mac_cfg;
	val64 = readq(&bar0->mac_cfg);
	val64 &= ~(MAC_CFG_RMAC_STRIP_PAD);
	writeq(RMAC_CFG_KEY(0x4C0D), &bar0->rmac_cfg_key);
	writel((u32) (val64), add);
	writeq(RMAC_CFG_KEY(0x4C0D), &bar0->rmac_cfg_key);
	writel((u32) (val64 >> 32), (add + 4));
	val64 = readq(&bar0->mac_cfg);

	/* Enable FCS stripping by adapter */
	add = &bar0->mac_cfg;
	val64 = readq(&bar0->mac_cfg);
	val64 |= MAC_CFG_RMAC_STRIP_FCS;
	if (nic->device_type == XFRAME_II_DEVICE)
		writeq(val64, &bar0->mac_cfg);
	else {
		writeq(RMAC_CFG_KEY(0x4C0D), &bar0->rmac_cfg_key);
		writel((u32) (val64), add);
		writeq(RMAC_CFG_KEY(0x4C0D), &bar0->rmac_cfg_key);
		writel((u32) (val64 >> 32), (add + 4));
	}

	/*
	 * Set the time value to be inserted in the pause frame
	 * generated by xena.
	 */
	val64 = readq(&bar0->rmac_pause_cfg);
	val64 &= ~(RMAC_PAUSE_HG_PTIME(0xffff));
	val64 |= RMAC_PAUSE_HG_PTIME(nic->mac_control.rmac_pause_time);
	writeq(val64, &bar0->rmac_pause_cfg);

	/*
	 * Set the Threshold Limit for Generating the pause frame
	 * If the amount of data in any Queue exceeds ratio of
	 * (mac_control.mc_pause_threshold_q0q3 or q4q7)/256
	 * pause frame is generated
	 */
	val64 = 0;
	for (i = 0; i < 4; i++) {
		val64 |=
		    (((u64) 0xFF00 | nic->mac_control.
		      mc_pause_threshold_q0q3)
		     << (i * 2 * 8));
	}
	writeq(val64, &bar0->mc_pause_thresh_q0q3);

	val64 = 0;
	for (i = 0; i < 4; i++) {
		val64 |=
		    (((u64) 0xFF00 | nic->mac_control.
		      mc_pause_threshold_q4q7)
		     << (i * 2 * 8));
	}
	writeq(val64, &bar0->mc_pause_thresh_q4q7);

	/*
	 * TxDMA will stop Read request if the number of read split has
	 * exceeded the limit pointed by shared_splits
	 */
	val64 = readq(&bar0->pic_control);
	val64 |= PIC_CNTL_SHARED_SPLITS(shared_splits);
	writeq(val64, &bar0->pic_control);

	if (nic->config.bus_speed == 266) {
		writeq(TXREQTO_VAL(0x7f) | TXREQTO_EN, &bar0->txreqtimeout);
		writeq(0x0, &bar0->read_retry_delay);
		writeq(0x0, &bar0->write_retry_delay);
	}

	/*
	 * Programming the Herc to split every write transaction
	 * that does not start on an ADB to reduce disconnects.
	 */
	if (nic->device_type == XFRAME_II_DEVICE) {
		val64 = FAULT_BEHAVIOUR | EXT_REQ_EN |
			MISC_LINK_STABILITY_PRD(3);
		writeq(val64, &bar0->misc_control);
		val64 = readq(&bar0->pic_control2);
		val64 &= ~(BIT(13)|BIT(14)|BIT(15));
		writeq(val64, &bar0->pic_control2);
	}
	if (strstr(nic->product_name, "CX4")) {
		val64 = TMAC_AVG_IPG(0x17);
		writeq(val64, &bar0->tmac_avg_ipg);
	}

	return SUCCESS;
}
#define LINK_UP_DOWN_INTERRUPT		1
#define MAC_RMAC_ERR_TIMER		2

static int s2io_link_fault_indication(struct s2io_nic *nic)
{
	if (nic->intr_type != INTA)
		return MAC_RMAC_ERR_TIMER;
	if (nic->device_type == XFRAME_II_DEVICE)
		return LINK_UP_DOWN_INTERRUPT;
	else
		return MAC_RMAC_ERR_TIMER;
}

/**
 *  en_dis_able_nic_intrs - Enable or Disable the interrupts
 *  @nic: device private variable,
 *  @mask: A mask indicating which Intr block must be modified and,
 *  @flag: A flag indicating whether to enable or disable the Intrs.
 *  Description: This function will either disable or enable the interrupts
 *  depending on the flag argument. The mask argument can be used to
 *  enable/disable any Intr block.
 *  Return Value: NONE.
 */

static void en_dis_able_nic_intrs(struct s2io_nic *nic, u16 mask, int flag)
{
	struct XENA_dev_config __iomem *bar0 = nic->bar0;
	register u64 val64 = 0, temp64 = 0;

	/*  Top level interrupt classification */
	/*  PIC Interrupts */
	if ((mask & (TX_PIC_INTR | RX_PIC_INTR))) {
		/*  Enable PIC Intrs in the general intr mask register */
		val64 = TXPIC_INT_M;
		if (flag == ENABLE_INTRS) {
			temp64 = readq(&bar0->general_int_mask);
			temp64 &= ~((u64) val64);
			writeq(temp64, &bar0->general_int_mask);
			/*
			 * If Hercules adapter enable GPIO otherwise
			 * disable all PCIX, Flash, MDIO, IIC and GPIO
			 * interrupts for now.
			 * TODO
			 */
			if (s2io_link_fault_indication(nic) ==
					LINK_UP_DOWN_INTERRUPT ) {
				temp64 = readq(&bar0->pic_int_mask);
				temp64 &= ~((u64) PIC_INT_GPIO);
				writeq(temp64, &bar0->pic_int_mask);
				temp64 = readq(&bar0->gpio_int_mask);
				temp64 &= ~((u64) GPIO_INT_MASK_LINK_UP);
				writeq(temp64, &bar0->gpio_int_mask);
			} else {
				writeq(DISABLE_ALL_INTRS, &bar0->pic_int_mask);
			}
			/*
			 * No MSI Support is available presently, so TTI and
			 * RTI interrupts are also disabled.
			 */
		} else if (flag == DISABLE_INTRS) {
			/*
			 * Disable PIC Intrs in the general
			 * intr mask register
			 */
			writeq(DISABLE_ALL_INTRS, &bar0->pic_int_mask);
			temp64 = readq(&bar0->general_int_mask);
			val64 |= temp64;
			writeq(val64, &bar0->general_int_mask);
		}
	}

	/*  MAC Interrupts */
	/*  Enabling/Disabling MAC interrupts */
	if (mask & (TX_MAC_INTR | RX_MAC_INTR)) {
		val64 = TXMAC_INT_M | RXMAC_INT_M;
		if (flag == ENABLE_INTRS) {
			temp64 = readq(&bar0->general_int_mask);
			temp64 &= ~((u64) val64);
			writeq(temp64, &bar0->general_int_mask);
			/*
			 * All MAC block error interrupts are disabled for now
			 * TODO
			 */
		} else if (flag == DISABLE_INTRS) {
			/*
			 * Disable MAC Intrs in the general intr mask register
			 */
			writeq(DISABLE_ALL_INTRS, &bar0->mac_int_mask);
			writeq(DISABLE_ALL_INTRS,
			       &bar0->mac_rmac_err_mask);

			temp64 = readq(&bar0->general_int_mask);
			val64 |= temp64;
			writeq(val64, &bar0->general_int_mask);
		}
	}

	/*  Tx traffic interrupts */
	if (mask & TX_TRAFFIC_INTR) {
		val64 = TXTRAFFIC_INT_M;
		if (flag == ENABLE_INTRS) {
			temp64 = readq(&bar0->general_int_mask);
			temp64 &= ~((u64) val64);
			writeq(temp64, &bar0->general_int_mask);
			/*
			 * Enable all the Tx side interrupts
			 * writing 0 Enables all 64 TX interrupt levels
			 */
			writeq(0x0, &bar0->tx_traffic_mask);
		} else if (flag == DISABLE_INTRS) {
			/*
			 * Disable Tx Traffic Intrs in the general intr mask
			 * register.
			 */
			writeq(DISABLE_ALL_INTRS, &bar0->tx_traffic_mask);
			temp64 = readq(&bar0->general_int_mask);
			val64 |= temp64;
			writeq(val64, &bar0->general_int_mask);
		}
	}

	/*  Rx traffic interrupts */
	if (mask & RX_TRAFFIC_INTR) {
		val64 = RXTRAFFIC_INT_M;
		if (flag == ENABLE_INTRS) {
			temp64 = readq(&bar0->general_int_mask);
			temp64 &= ~((u64) val64);
			writeq(temp64, &bar0->general_int_mask);
			/* writing 0 Enables all 8 RX interrupt levels */
			writeq(0x0, &bar0->rx_traffic_mask);
		} else if (flag == DISABLE_INTRS) {
			/*
			 * Disable Rx Traffic Intrs in the general intr mask
			 * register.
			 */
			writeq(DISABLE_ALL_INTRS, &bar0->rx_traffic_mask);
			temp64 = readq(&bar0->general_int_mask);
			val64 |= temp64;
			writeq(val64, &bar0->general_int_mask);
		}
	}
}

/**
 *  verify_pcc_quiescent- Checks for PCC quiescent state
 *  Return: 1 If PCC is quiescence
 *          0 If PCC is not quiescence
 */
static int verify_pcc_quiescent(struct s2io_nic *sp, int flag)
{
	int ret = 0, herc;
	struct XENA_dev_config __iomem *bar0 = sp->bar0;
	u64 val64 = readq(&bar0->adapter_status);
	
	herc = (sp->device_type == XFRAME_II_DEVICE);

	if (flag == FALSE) {
		if ((!herc && (get_xena_rev_id(sp->pdev) >= 4)) || herc) {
			if (!(val64 & ADAPTER_STATUS_RMAC_PCC_IDLE))
				ret = 1;
		} else {
			if (!(val64 & ADAPTER_STATUS_RMAC_PCC_FOUR_IDLE))
				ret = 1;
		}
	} else {
		if ((!herc && (get_xena_rev_id(sp->pdev) >= 4)) || herc) {
			if (((val64 & ADAPTER_STATUS_RMAC_PCC_IDLE) ==
			     ADAPTER_STATUS_RMAC_PCC_IDLE))
				ret = 1;
		} else {
			if (((val64 & ADAPTER_STATUS_RMAC_PCC_FOUR_IDLE) ==
			     ADAPTER_STATUS_RMAC_PCC_FOUR_IDLE))
				ret = 1;
		}
	}

	return ret;
}
/**
 *  verify_xena_quiescence - Checks whether the H/W is ready
 *  Description: Returns whether the H/W is ready to go or not. Depending
 *  on whether adapter enable bit was written or not the comparison
 *  differs and the calling function passes the input argument flag to
 *  indicate this.
 *  Return: 1 If xena is quiescence
 *          0 If Xena is not quiescence
 */

static int verify_xena_quiescence(struct s2io_nic *sp)
{
	int  mode;
	struct XENA_dev_config __iomem *bar0 = sp->bar0;
	u64 val64 = readq(&bar0->adapter_status);
	mode = s2io_verify_pci_mode(sp);

	if (!(val64 & ADAPTER_STATUS_TDMA_READY)) {
		DBG_PRINT(ERR_DBG, "%s", "TDMA is not ready!");
		return 0;
	}
	if (!(val64 & ADAPTER_STATUS_RDMA_READY)) {
	DBG_PRINT(ERR_DBG, "%s", "RDMA is not ready!");
		return 0;
	}
	if (!(val64 & ADAPTER_STATUS_PFC_READY)) {
		DBG_PRINT(ERR_DBG, "%s", "PFC is not ready!");
		return 0;
	}
	if (!(val64 & ADAPTER_STATUS_TMAC_BUF_EMPTY)) {
		DBG_PRINT(ERR_DBG, "%s", "TMAC BUF is not empty!");
		return 0;
	}
	if (!(val64 & ADAPTER_STATUS_PIC_QUIESCENT)) {
		DBG_PRINT(ERR_DBG, "%s", "PIC is not QUIESCENT!");
		return 0;
	}
	if (!(val64 & ADAPTER_STATUS_MC_DRAM_READY)) {
		DBG_PRINT(ERR_DBG, "%s", "MC_DRAM is not ready!");
		return 0;
	}
	if (!(val64 & ADAPTER_STATUS_MC_QUEUES_READY)) {
		DBG_PRINT(ERR_DBG, "%s", "MC_QUEUES is not ready!");
		return 0;
	}
	if (!(val64 & ADAPTER_STATUS_M_PLL_LOCK)) {
		DBG_PRINT(ERR_DBG, "%s", "M_PLL is not locked!");
		return 0;
	}

	/*
	 * In PCI 33 mode, the P_PLL is not used, and therefore,
	 * the the P_PLL_LOCK bit in the adapter_status register will
	 * not be asserted.
	 */
	if (!(val64 & ADAPTER_STATUS_P_PLL_LOCK) &&
		sp->device_type == XFRAME_II_DEVICE && mode !=
		PCI_MODE_PCI_33) {
		DBG_PRINT(ERR_DBG, "%s", "P_PLL is not locked!");
		return 0;
	}
	if (!((val64 & ADAPTER_STATUS_RC_PRC_QUIESCENT) ==
			ADAPTER_STATUS_RC_PRC_QUIESCENT)) {
		DBG_PRINT(ERR_DBG, "%s", "RC_PRC is not QUIESCENT!");
		return 0;
	}
	return 1;
}

/**
 * fix_mac_address -  Fix for Mac addr problem on Alpha platforms
 * @sp: Pointer to device specifc structure
 * Description :
 * New procedure to clear mac address reading  problems on Alpha platforms
 *
 */

static void fix_mac_address(struct s2io_nic * sp)
{
	struct XENA_dev_config __iomem *bar0 = sp->bar0;
	u64 val64;
	int i = 0;

	while (fix_mac[i] != END_SIGN) {
		writeq(fix_mac[i++], &bar0->gpio_control);
		udelay(10);
		val64 = readq(&bar0->gpio_control);
	}
}

/**
 *  start_nic - Turns the device on
 *  @nic : device private variable.
 *  Description:
 *  This function actually turns the device on. Before this  function is
 *  called,all Registers are configured from their reset states
 *  and shared memory is allocated but the NIC is still quiescent. On
 *  calling this function, the device interrupts are cleared and the NIC is
 *  literally switched on by writing into the adapter control register.
 *  Return Value:
 *  SUCCESS on success and -1 on failure.
 */

static int start_nic(struct s2io_nic *nic)
{
	struct XENA_dev_config __iomem *bar0 = nic->bar0;
	struct net_device *dev = nic->dev;
	register u64 val64 = 0;
	u16 subid, i;
	struct mac_info *mac_control;
	struct config_param *config;

	mac_control = &nic->mac_control;
	config = &nic->config;

	/*  PRC Initialization and configuration */
	for (i = 0; i < config->rx_ring_num; i++) {
		writeq((u64) mac_control->rings[i].rx_blocks[0].block_dma_addr,
		       &bar0->prc_rxd0_n[i]);

		val64 = readq(&bar0->prc_ctrl_n[i]);
		if (nic->config.bimodal)
			val64 |= PRC_CTRL_BIMODAL_INTERRUPT;
		if (nic->rxd_mode == RXD_MODE_1)
			val64 |= PRC_CTRL_RC_ENABLED;
		else
			val64 |= PRC_CTRL_RC_ENABLED | PRC_CTRL_RING_MODE_3;
		if (nic->device_type == XFRAME_II_DEVICE)
			val64 |= PRC_CTRL_GROUP_READS;
		val64 &= ~PRC_CTRL_RXD_BACKOFF_INTERVAL(0xFFFFFF);
		val64 |= PRC_CTRL_RXD_BACKOFF_INTERVAL(0x1000);
		writeq(val64, &bar0->prc_ctrl_n[i]);
	}

	if (nic->rxd_mode == RXD_MODE_3B) {
		/* Enabling 2 buffer mode by writing into Rx_pa_cfg reg. */
		val64 = readq(&bar0->rx_pa_cfg);
		val64 |= RX_PA_CFG_IGNORE_L2_ERR;
		writeq(val64, &bar0->rx_pa_cfg);
	}

	if (vlan_tag_strip == 0) {
		val64 = readq(&bar0->rx_pa_cfg);
		val64 &= ~RX_PA_CFG_STRIP_VLAN_TAG;
		writeq(val64, &bar0->rx_pa_cfg);
		vlan_strip_flag = 0;
	}

	/*
	 * Enabling MC-RLDRAM. After enabling the device, we timeout
	 * for around 100ms, which is approximately the time required
	 * for the device to be ready for operation.
	 */
	val64 = readq(&bar0->mc_rldram_mrs);
	val64 |= MC_RLDRAM_QUEUE_SIZE_ENABLE | MC_RLDRAM_MRS_ENABLE;
	SPECIAL_REG_WRITE(val64, &bar0->mc_rldram_mrs, UF);
	val64 = readq(&bar0->mc_rldram_mrs);

	msleep(100);	/* Delay by around 100 ms. */

	/* Enabling ECC Protection. */
	val64 = readq(&bar0->adapter_control);
	val64 &= ~ADAPTER_ECC_EN;
	writeq(val64, &bar0->adapter_control);

	/*
	 * Clearing any possible Link state change interrupts that
	 * could have popped up just before Enabling the card.
	 */
	val64 = readq(&bar0->mac_rmac_err_reg);
	if (val64)
		writeq(val64, &bar0->mac_rmac_err_reg);

	/*
	 * Verify if the device is ready to be enabled, if so enable
	 * it.
	 */
	val64 = readq(&bar0->adapter_status);
	if (!verify_xena_quiescence(nic)) {
		DBG_PRINT(ERR_DBG, "%s: device is not ready, ", dev->name);
		DBG_PRINT(ERR_DBG, "Adapter status reads: 0x%llx\n",
			  (unsigned long long) val64);
		return FAILURE;
	}

	/*
	 * With some switches, link might be already up at this point.
	 * Because of this weird behavior, when we enable laser,
	 * we may not get link. We need to handle this. We cannot
	 * figure out which switch is misbehaving. So we are forced to
	 * make a global change.
	 */

	/* Enabling Laser. */
	val64 = readq(&bar0->adapter_control);
	val64 |= ADAPTER_EOI_TX_ON;
	writeq(val64, &bar0->adapter_control);

	if (s2io_link_fault_indication(nic) == MAC_RMAC_ERR_TIMER) {
		/*
		 * Dont see link state interrupts initally on some switches,
		 * so directly scheduling the link state task here.
		 */
		schedule_work(&nic->set_link_task);
	}
	/* SXE-002: Initialize link and activity LED */
	subid = nic->pdev->subsystem_device;
	if (((subid & 0xFF) >= 0x07) &&
	    (nic->device_type == XFRAME_I_DEVICE)) {
		val64 = readq(&bar0->gpio_control);
		val64 |= 0x0000800000000000ULL;
		writeq(val64, &bar0->gpio_control);
		val64 = 0x0411040400000000ULL;
		writeq(val64, (void __iomem *)bar0 + 0x2700);
	}

	return SUCCESS;
}
/**
 * s2io_txdl_getskb - Get the skb from txdl, unmap and return skb
 */
static struct sk_buff *s2io_txdl_getskb(struct fifo_info *fifo_data, struct \
					TxD *txdlp, int get_off)
{
	struct s2io_nic *nic = fifo_data->nic;
	struct sk_buff *skb;
	struct TxD *txds;
	u16 j, frg_cnt;

	txds = txdlp;
	if (txds->Host_Control == (u64)(long)nic->ufo_in_band_v) {
		pci_unmap_single(nic->pdev, (dma_addr_t)
			txds->Buffer_Pointer, sizeof(u64),
			PCI_DMA_TODEVICE);
		txds++;
	}

	skb = (struct sk_buff *) ((unsigned long)
			txds->Host_Control);
	if (!skb) {
		memset(txdlp, 0, (sizeof(struct TxD) * fifo_data->max_txds));
		return NULL;
	}
	pci_unmap_single(nic->pdev, (dma_addr_t)
			 txds->Buffer_Pointer,
			 skb->len - skb->data_len,
			 PCI_DMA_TODEVICE);
	frg_cnt = skb_shinfo(skb)->nr_frags;
	if (frg_cnt) {
		txds++;
		for (j = 0; j < frg_cnt; j++, txds++) {
			skb_frag_t *frag = &skb_shinfo(skb)->frags[j];
			if (!txds->Buffer_Pointer)
				break;
			pci_unmap_page(nic->pdev, (dma_addr_t)
					txds->Buffer_Pointer,
				       frag->size, PCI_DMA_TODEVICE);
		}
	}
	memset(txdlp,0, (sizeof(struct TxD) * fifo_data->max_txds));
	return(skb);
}

/**
 *  free_tx_buffers - Free all queued Tx buffers
 *  @nic : device private variable.
 *  Description:
 *  Free all queued Tx buffers.
 *  Return Value: void
*/

static void free_tx_buffers(struct s2io_nic *nic)
{
	struct net_device *dev = nic->dev;
	struct sk_buff *skb;
	struct TxD *txdp;
	int i, j;
	struct mac_info *mac_control;
	struct config_param *config;
	int cnt = 0;

	mac_control = &nic->mac_control;
	config = &nic->config;

	for (i = 0; i < config->tx_fifo_num; i++) {
		for (j = 0; j < config->tx_cfg[i].fifo_len - 1; j++) {
			txdp = (struct TxD *) \
			mac_control->fifos[i].list_info[j].list_virt_addr;
			skb = s2io_txdl_getskb(&mac_control->fifos[i], txdp, j);
			if (skb) {
				nic->mac_control.stats_info->sw_stat.mem_freed 
					+= skb->truesize;
				dev_kfree_skb(skb);
				cnt++;
			}
		}
		DBG_PRINT(INTR_DBG,
			  "%s:forcibly freeing %d skbs on FIFO%d\n",
			  dev->name, cnt, i);
		mac_control->fifos[i].tx_curr_get_info.offset = 0;
		mac_control->fifos[i].tx_curr_put_info.offset = 0;
	}
}

/**
 *   stop_nic -  To stop the nic
 *   @nic ; device private variable.
 *   Description:
 *   This function does exactly the opposite of what the start_nic()
 *   function does. This function is called to stop the device.
 *   Return Value:
 *   void.
 */

static void stop_nic(struct s2io_nic *nic)
{
	struct XENA_dev_config __iomem *bar0 = nic->bar0;
	register u64 val64 = 0;
	u16 interruptible;
	struct mac_info *mac_control;
	struct config_param *config;

	mac_control = &nic->mac_control;
	config = &nic->config;

	/*  Disable all interrupts */
	interruptible = TX_TRAFFIC_INTR | RX_TRAFFIC_INTR;
	interruptible |= TX_PIC_INTR | RX_PIC_INTR;
	interruptible |= TX_MAC_INTR | RX_MAC_INTR;
	en_dis_able_nic_intrs(nic, interruptible, DISABLE_INTRS);

	/* Clearing Adapter_En bit of ADAPTER_CONTROL Register */
	val64 = readq(&bar0->adapter_control);
	val64 &= ~(ADAPTER_CNTL_EN);
	writeq(val64, &bar0->adapter_control);
}

static int fill_rxd_3buf(struct s2io_nic *nic, struct RxD_t *rxdp, struct \
				sk_buff *skb)
{
	struct net_device *dev = nic->dev;
	struct sk_buff *frag_list;
	void *tmp;

	/* Buffer-1 receives L3/L4 headers */
	((struct RxD3*)rxdp)->Buffer1_ptr = pci_map_single
			(nic->pdev, skb->data, l3l4hdr_size + 4,
			PCI_DMA_FROMDEVICE);

	/* skb_shinfo(skb)->frag_list will have L4 data payload */
	skb_shinfo(skb)->frag_list = dev_alloc_skb(dev->mtu + ALIGN_SIZE);
	if (skb_shinfo(skb)->frag_list == NULL) {
		nic->mac_control.stats_info->sw_stat.mem_alloc_fail_cnt++;
		DBG_PRINT(INFO_DBG, "%s: dev_alloc_skb failed\n ", dev->name);
		return -ENOMEM ;
	}
	frag_list = skb_shinfo(skb)->frag_list;
	skb->truesize += frag_list->truesize;
	nic->mac_control.stats_info->sw_stat.mem_allocated 
		+= frag_list->truesize;
	frag_list->next = NULL;
	tmp = (void *)ALIGN((long)frag_list->data, ALIGN_SIZE + 1);
	frag_list->data = tmp;
	skb_reset_tail_pointer(frag_list);

	/* Buffer-2 receives L4 data payload */
	((struct RxD3*)rxdp)->Buffer2_ptr = pci_map_single(nic->pdev,
				frag_list->data, dev->mtu,
				PCI_DMA_FROMDEVICE);
	rxdp->Control_2 |= SET_BUFFER1_SIZE_3(l3l4hdr_size + 4);
	rxdp->Control_2 |= SET_BUFFER2_SIZE_3(dev->mtu);

	return SUCCESS;
}

/**
 *  fill_rx_buffers - Allocates the Rx side skbs
 *  @nic:  device private variable
 *  @ring_no: ring number
 *  Description:
 *  The function allocates Rx side skbs and puts the physical
 *  address of these buffers into the RxD buffer pointers, so that the NIC
 *  can DMA the received frame into these locations.
 *  The NIC supports 3 receive modes, viz
 *  1. single buffer,
 *  2. three buffer and
 *  3. Five buffer modes.
 *  Each mode defines how many fragments the received frame will be split
 *  up into by the NIC. The frame is split into L3 header, L4 Header,
 *  L4 payload in three buffer mode and in 5 buffer mode, L4 payload itself
 *  is split into 3 fragments. As of now only single buffer mode is
 *  supported.
 *   Return Value:
 *  SUCCESS on success or an appropriate -ve value on failure.
 */

static int fill_rx_buffers(struct s2io_nic *nic, int ring_no)
{
	struct net_device *dev = nic->dev;
	struct sk_buff *skb;
	struct RxD_t *rxdp;
	int off, off1, size, block_no, block_no1;
	u32 alloc_tab = 0;
	u32 alloc_cnt;
	struct mac_info *mac_control;
	struct config_param *config;
	u64 tmp;
	struct buffAdd *ba;
	unsigned long flags;
	struct RxD_t *first_rxdp = NULL;
	u64 Buffer0_ptr = 0, Buffer1_ptr = 0;

	mac_control = &nic->mac_control;
	config = &nic->config;
	alloc_cnt = mac_control->rings[ring_no].pkt_cnt -
	    atomic_read(&nic->rx_bufs_left[ring_no]);

	block_no1 = mac_control->rings[ring_no].rx_curr_get_info.block_index;
	off1 = mac_control->rings[ring_no].rx_curr_get_info.offset;
	while (alloc_tab < alloc_cnt) {
		block_no = mac_control->rings[ring_no].rx_curr_put_info.
		    block_index;
		off = mac_control->rings[ring_no].rx_curr_put_info.offset;

		rxdp = mac_control->rings[ring_no].
				rx_blocks[block_no].rxds[off].virt_addr;

		if ((block_no == block_no1) && (off == off1) &&
					(rxdp->Host_Control)) {
			DBG_PRINT(INTR_DBG, "%s: Get and Put",
				  dev->name);
			DBG_PRINT(INTR_DBG, " info equated\n");
			goto end;
		}
		if (off && (off == rxd_count[nic->rxd_mode])) {
			mac_control->rings[ring_no].rx_curr_put_info.
			    block_index++;
			if (mac_control->rings[ring_no].rx_curr_put_info.
			    block_index == mac_control->rings[ring_no].
					block_count)
				mac_control->rings[ring_no].rx_curr_put_info.
					block_index = 0;
			block_no = mac_control->rings[ring_no].
					rx_curr_put_info.block_index;
			if (off == rxd_count[nic->rxd_mode])
				off = 0;
			mac_control->rings[ring_no].rx_curr_put_info.
				offset = off;
			rxdp = mac_control->rings[ring_no].
				rx_blocks[block_no].block_virt_addr;
			DBG_PRINT(INTR_DBG, "%s: Next block at: %p\n",
				  dev->name, rxdp);
		}
		if(!napi) {
			spin_lock_irqsave(&nic->put_lock, flags);
			mac_control->rings[ring_no].put_pos =
			(block_no * (rxd_count[nic->rxd_mode] + 1)) + off;
			spin_unlock_irqrestore(&nic->put_lock, flags);
		} else {
			mac_control->rings[ring_no].put_pos =
			(block_no * (rxd_count[nic->rxd_mode] + 1)) + off;
		}
		if ((rxdp->Control_1 & RXD_OWN_XENA) &&
			((nic->rxd_mode >= RXD_MODE_3A) &&
				(rxdp->Control_2 & BIT(0)))) {
			mac_control->rings[ring_no].rx_curr_put_info.
					offset = off;
			goto end;
		}
		/* calculate size of skb based on ring mode */
		size = dev->mtu + HEADER_ETHERNET_II_802_3_SIZE +
				HEADER_802_2_SIZE + HEADER_SNAP_SIZE;
		if (nic->rxd_mode == RXD_MODE_1)
			size += NET_IP_ALIGN;
		else if (nic->rxd_mode == RXD_MODE_3B)
			size = dev->mtu + ALIGN_SIZE + BUF0_LEN + 4;
		else
			size = l3l4hdr_size + ALIGN_SIZE + BUF0_LEN + 4;

		/* allocate skb */
		skb = dev_alloc_skb(size);
		if(!skb) {
			DBG_PRINT(INFO_DBG, "%s: Out of ", dev->name);
			DBG_PRINT(INFO_DBG, "memory to allocate SKBs\n");
			if (first_rxdp) {
				wmb();
				first_rxdp->Control_1 |= RXD_OWN_XENA;
			}
			nic->mac_control.stats_info->sw_stat. \
				mem_alloc_fail_cnt++;
			return -ENOMEM ;
		}
		nic->mac_control.stats_info->sw_stat.mem_allocated 
			+= skb->truesize;
		if (nic->rxd_mode == RXD_MODE_1) {
			/* 1 buffer mode - normal operation mode */
			memset(rxdp, 0, sizeof(struct RxD1));
			skb_reserve(skb, NET_IP_ALIGN);
			((struct RxD1*)rxdp)->Buffer0_ptr = pci_map_single
			    (nic->pdev, skb->data, size - NET_IP_ALIGN,
				PCI_DMA_FROMDEVICE);
			rxdp->Control_2 = 
				SET_BUFFER0_SIZE_1(size - NET_IP_ALIGN);

		} else if (nic->rxd_mode >= RXD_MODE_3A) {
			/*
			 * 2 or 3 buffer mode -
			 * Both 2 buffer mode and 3 buffer mode provides 128
			 * byte aligned receive buffers.
			 *
			 * 3 buffer mode provides header separation where in
			 * skb->data will have L3/L4 headers where as
			 * skb_shinfo(skb)->frag_list will have the L4 data
			 * payload
			 */

			/* save buffer pointers to avoid frequent dma mapping */
			Buffer0_ptr = ((struct RxD3*)rxdp)->Buffer0_ptr;
			Buffer1_ptr = ((struct RxD3*)rxdp)->Buffer1_ptr;
			memset(rxdp, 0, sizeof(struct RxD3));
			/* restore the buffer pointers for dma sync*/
			((struct RxD3*)rxdp)->Buffer0_ptr = Buffer0_ptr;
			((struct RxD3*)rxdp)->Buffer1_ptr = Buffer1_ptr;

			ba = &mac_control->rings[ring_no].ba[block_no][off];
			skb_reserve(skb, BUF0_LEN);
			tmp = (u64)(unsigned long) skb->data;
			tmp += ALIGN_SIZE;
			tmp &= ~ALIGN_SIZE;
			skb->data = (void *) (unsigned long)tmp;
			skb_reset_tail_pointer(skb);

			if (!(((struct RxD3*)rxdp)->Buffer0_ptr))
				((struct RxD3*)rxdp)->Buffer0_ptr =
				   pci_map_single(nic->pdev, ba->ba_0, BUF0_LEN,
					   PCI_DMA_FROMDEVICE);
			else
				pci_dma_sync_single_for_device(nic->pdev,
				(dma_addr_t) ((struct RxD3*)rxdp)->Buffer0_ptr,
				    BUF0_LEN, PCI_DMA_FROMDEVICE);
			rxdp->Control_2 = SET_BUFFER0_SIZE_3(BUF0_LEN);
			if (nic->rxd_mode == RXD_MODE_3B) {
				/* Two buffer mode */

				/*
				 * Buffer2 will have L3/L4 header plus
				 * L4 payload
				 */
				((struct RxD3*)rxdp)->Buffer2_ptr = pci_map_single
				(nic->pdev, skb->data, dev->mtu + 4,
						PCI_DMA_FROMDEVICE);

				/* Buffer-1 will be dummy buffer. Not used */
				if (!(((struct RxD3*)rxdp)->Buffer1_ptr)) {
					((struct RxD3*)rxdp)->Buffer1_ptr =
						pci_map_single(nic->pdev,
						ba->ba_1, BUF1_LEN,
						PCI_DMA_FROMDEVICE);
				}
				rxdp->Control_2 |= SET_BUFFER1_SIZE_3(1);
				rxdp->Control_2 |= SET_BUFFER2_SIZE_3
								(dev->mtu + 4);
			} else {
				/* 3 buffer mode */
				if (fill_rxd_3buf(nic, rxdp, skb) == -ENOMEM) {
					nic->mac_control.stats_info->sw_stat.\
					mem_freed += skb->truesize;
					dev_kfree_skb_irq(skb);
					if (first_rxdp) {
						wmb();
						first_rxdp->Control_1 |=
							RXD_OWN_XENA;
					}
					return -ENOMEM ;
				}
			}
			rxdp->Control_2 |= BIT(0);
		}
		rxdp->Host_Control = (unsigned long) (skb);
		if (alloc_tab & ((1 << rxsync_frequency) - 1))
			rxdp->Control_1 |= RXD_OWN_XENA;
		off++;
		if (off == (rxd_count[nic->rxd_mode] + 1))
			off = 0;
		mac_control->rings[ring_no].rx_curr_put_info.offset = off;

		rxdp->Control_2 |= SET_RXD_MARKER;
		if (!(alloc_tab & ((1 << rxsync_frequency) - 1))) {
			if (first_rxdp) {
				wmb();
				first_rxdp->Control_1 |= RXD_OWN_XENA;
			}
			first_rxdp = rxdp;
		}
		atomic_inc(&nic->rx_bufs_left[ring_no]);
		alloc_tab++;
	}

      end:
	/* Transfer ownership of first descriptor to adapter just before
	 * exiting. Before that, use memory barrier so that ownership
	 * and other fields are seen by adapter correctly.
	 */
	if (first_rxdp) {
		wmb();
		first_rxdp->Control_1 |= RXD_OWN_XENA;
	}

	return SUCCESS;
}

static void free_rxd_blk(struct s2io_nic *sp, int ring_no, int blk)
{
	struct net_device *dev = sp->dev;
	int j;
	struct sk_buff *skb;
	struct RxD_t *rxdp;
	struct mac_info *mac_control;
	struct buffAdd *ba;

	mac_control = &sp->mac_control;
	for (j = 0 ; j < rxd_count[sp->rxd_mode]; j++) {
		rxdp = mac_control->rings[ring_no].
                                rx_blocks[blk].rxds[j].virt_addr;
		skb = (struct sk_buff *)
			((unsigned long) rxdp->Host_Control);
		if (!skb) {
			continue;
		}
		if (sp->rxd_mode == RXD_MODE_1) {
			pci_unmap_single(sp->pdev, (dma_addr_t)
				 ((struct RxD1*)rxdp)->Buffer0_ptr,
				 dev->mtu +
				 HEADER_ETHERNET_II_802_3_SIZE
				 + HEADER_802_2_SIZE +
				 HEADER_SNAP_SIZE,
				 PCI_DMA_FROMDEVICE);
			memset(rxdp, 0, sizeof(struct RxD1));
		} else if(sp->rxd_mode == RXD_MODE_3B) {
			ba = &mac_control->rings[ring_no].
				ba[blk][j];
			pci_unmap_single(sp->pdev, (dma_addr_t)
				 ((struct RxD3*)rxdp)->Buffer0_ptr,
				 BUF0_LEN,
				 PCI_DMA_FROMDEVICE);
			pci_unmap_single(sp->pdev, (dma_addr_t)
				 ((struct RxD3*)rxdp)->Buffer1_ptr,
				 BUF1_LEN,
				 PCI_DMA_FROMDEVICE);
			pci_unmap_single(sp->pdev, (dma_addr_t)
				 ((struct RxD3*)rxdp)->Buffer2_ptr,
				 dev->mtu + 4,
				 PCI_DMA_FROMDEVICE);
			memset(rxdp, 0, sizeof(struct RxD3));
		} else {
			pci_unmap_single(sp->pdev, (dma_addr_t)
				((struct RxD3*)rxdp)->Buffer0_ptr, BUF0_LEN,
				PCI_DMA_FROMDEVICE);
			pci_unmap_single(sp->pdev, (dma_addr_t)
				((struct RxD3*)rxdp)->Buffer1_ptr,
				l3l4hdr_size + 4,
				PCI_DMA_FROMDEVICE);
			pci_unmap_single(sp->pdev, (dma_addr_t)
				((struct RxD3*)rxdp)->Buffer2_ptr, dev->mtu,
				PCI_DMA_FROMDEVICE);
			memset(rxdp, 0, sizeof(struct RxD3));
		}
		sp->mac_control.stats_info->sw_stat.mem_freed += skb->truesize;
		dev_kfree_skb(skb);
		atomic_dec(&sp->rx_bufs_left[ring_no]);
	}
}

/**
 *  free_rx_buffers - Frees all Rx buffers
 *  @sp: device private variable.
 *  Description:
 *  This function will free all Rx buffers allocated by host.
 *  Return Value:
 *  NONE.
 */

static void free_rx_buffers(struct s2io_nic *sp)
{
	struct net_device *dev = sp->dev;
	int i, blk = 0, buf_cnt = 0;
	struct mac_info *mac_control;
	struct config_param *config;

	mac_control = &sp->mac_control;
	config = &sp->config;

	for (i = 0; i < config->rx_ring_num; i++) {
		for (blk = 0; blk < rx_ring_sz[i]; blk++)
			free_rxd_blk(sp,i,blk);

		mac_control->rings[i].rx_curr_put_info.block_index = 0;
		mac_control->rings[i].rx_curr_get_info.block_index = 0;
		mac_control->rings[i].rx_curr_put_info.offset = 0;
		mac_control->rings[i].rx_curr_get_info.offset = 0;
		atomic_set(&sp->rx_bufs_left[i], 0);
		DBG_PRINT(INIT_DBG, "%s:Freed 0x%x Rx Buffers on ring%d\n",
			  dev->name, buf_cnt, i);
	}
}

/**
 * s2io_poll - Rx interrupt handler for NAPI support
 * @dev : pointer to the device structure.
 * @budget : The number of packets that were budgeted to be processed
 * during  one pass through the 'Poll" function.
 * Description:
 * Comes into picture only if NAPI support has been incorporated. It does
 * the same thing that rx_intr_handler does, but not in a interrupt context
 * also It will process only a given number of packets.
 * Return value:
 * 0 on success and 1 if there are No Rx packets to be processed.
 */

static int s2io_poll(struct net_device *dev, int *budget)
{
	struct s2io_nic *nic = dev->priv;
	int pkt_cnt = 0, org_pkts_to_process;
	struct mac_info *mac_control;
	struct config_param *config;
	struct XENA_dev_config __iomem *bar0 = nic->bar0;
	int i;

	atomic_inc(&nic->isr_cnt);
	mac_control = &nic->mac_control;
	config = &nic->config;

	nic->pkts_to_process = *budget;
	if (nic->pkts_to_process > dev->quota)
		nic->pkts_to_process = dev->quota;
	org_pkts_to_process = nic->pkts_to_process;

	writeq(S2IO_MINUS_ONE, &bar0->rx_traffic_int);
	readl(&bar0->rx_traffic_int);

	for (i = 0; i < config->rx_ring_num; i++) {
		rx_intr_handler(&mac_control->rings[i]);
		pkt_cnt = org_pkts_to_process - nic->pkts_to_process;
		if (!nic->pkts_to_process) {
			/* Quota for the current iteration has been met */
			goto no_rx;
		}
	}
	if (!pkt_cnt)
		pkt_cnt = 1;

	dev->quota -= pkt_cnt;
	*budget -= pkt_cnt;
	netif_rx_complete(dev);

	for (i = 0; i < config->rx_ring_num; i++) {
		if (fill_rx_buffers(nic, i) == -ENOMEM) {
			DBG_PRINT(INFO_DBG, "%s:Out of memory", dev->name);
			DBG_PRINT(INFO_DBG, " in Rx Poll!!\n");
			break;
		}
	}
	/* Re enable the Rx interrupts. */
	writeq(0x0, &bar0->rx_traffic_mask);
	readl(&bar0->rx_traffic_mask);
	atomic_dec(&nic->isr_cnt);
	return 0;

no_rx:
	dev->quota -= pkt_cnt;
	*budget -= pkt_cnt;

	for (i = 0; i < config->rx_ring_num; i++) {
		if (fill_rx_buffers(nic, i) == -ENOMEM) {
			DBG_PRINT(INFO_DBG, "%s:Out of memory", dev->name);
			DBG_PRINT(INFO_DBG, " in Rx Poll!!\n");
			break;
		}
	}
	atomic_dec(&nic->isr_cnt);
	return 1;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
/**
 * s2io_netpoll - netpoll event handler entry point
 * @dev : pointer to the device structure.
 * Description:
 * 	This function will be called by upper layer to check for events on the
 * interface in situations where interrupts are disabled. It is used for
 * specific in-kernel networking tasks, such as remote consoles and kernel
 * debugging over the network (example netdump in RedHat).
 */
static void s2io_netpoll(struct net_device *dev)
{
	struct s2io_nic *nic = dev->priv;
	struct mac_info *mac_control;
	struct config_param *config;
	struct XENA_dev_config __iomem *bar0 = nic->bar0;
	u64 val64 = 0xFFFFFFFFFFFFFFFFULL;
	int i;

	disable_irq(dev->irq);

	atomic_inc(&nic->isr_cnt);
	mac_control = &nic->mac_control;
	config = &nic->config;

	writeq(val64, &bar0->rx_traffic_int);
	writeq(val64, &bar0->tx_traffic_int);

	/* we need to free up the transmitted skbufs or else netpoll will
	 * run out of skbs and will fail and eventually netpoll application such
	 * as netdump will fail.
	 */
	for (i = 0; i < config->tx_fifo_num; i++)
		tx_intr_handler(&mac_control->fifos[i]);

	/* check for received packet and indicate up to network */
	for (i = 0; i < config->rx_ring_num; i++)
		rx_intr_handler(&mac_control->rings[i]);

	for (i = 0; i < config->rx_ring_num; i++) {
		if (fill_rx_buffers(nic, i) == -ENOMEM) {
			DBG_PRINT(INFO_DBG, "%s:Out of memory", dev->name);
			DBG_PRINT(INFO_DBG, " in Rx Netpoll!!\n");
			break;
		}
	}
	atomic_dec(&nic->isr_cnt);
	enable_irq(dev->irq);
	return;
}
#endif

/**
 *  rx_intr_handler - Rx interrupt handler
 *  @nic: device private variable.
 *  Description:
 *  If the interrupt is because of a received frame or if the
 *  receive ring contains fresh as yet un-processed frames,this function is
 *  called. It picks out the RxD at which place the last Rx processing had
 *  stopped and sends the skb to the OSM's Rx handler and then increments
 *  the offset.
 *  Return Value:
 *  NONE.
 */
static void rx_intr_handler(struct ring_info *ring_data)
{
	struct s2io_nic *nic = ring_data->nic;
	struct net_device *dev = (struct net_device *) nic->dev;
	int get_block, put_block, put_offset;
	struct rx_curr_get_info get_info, put_info;
	struct RxD_t *rxdp;
	struct sk_buff *skb;
	int pkt_cnt = 0;
	int i;

	spin_lock(&nic->rx_lock);
	if (atomic_read(&nic->card_state) == CARD_DOWN) {
		DBG_PRINT(INTR_DBG, "%s: %s going down for reset\n",
			  __FUNCTION__, dev->name);
		spin_unlock(&nic->rx_lock);
		return;
	}

	get_info = ring_data->rx_curr_get_info;
	get_block = get_info.block_index;
	memcpy(&put_info, &ring_data->rx_curr_put_info, sizeof(put_info));
	put_block = put_info.block_index;
	rxdp = ring_data->rx_blocks[get_block].rxds[get_info.offset].virt_addr;
	if (!napi) {
		spin_lock(&nic->put_lock);
		put_offset = ring_data->put_pos;
		spin_unlock(&nic->put_lock);
	} else
		put_offset = ring_data->put_pos;

	while (RXD_IS_UP2DT(rxdp)) {
		/*
		 * If your are next to put index then it's
		 * FIFO full condition
		 */
		if ((get_block == put_block) &&
		    (get_info.offset + 1) == put_info.offset) {
			DBG_PRINT(INTR_DBG, "%s: Ring Full\n",dev->name);
			break;
		}
		skb = (struct sk_buff *) ((unsigned long)rxdp->Host_Control);
		if (skb == NULL) {
			DBG_PRINT(ERR_DBG, "%s: The skb is ",
				  dev->name);
			DBG_PRINT(ERR_DBG, "Null in Rx Intr\n");
			spin_unlock(&nic->rx_lock);
			return;
		}
		if (nic->rxd_mode == RXD_MODE_1) {
			pci_unmap_single(nic->pdev, (dma_addr_t)
				 ((struct RxD1*)rxdp)->Buffer0_ptr,
				 dev->mtu +
				 HEADER_ETHERNET_II_802_3_SIZE +
				 HEADER_802_2_SIZE +
				 HEADER_SNAP_SIZE,
				 PCI_DMA_FROMDEVICE);
		} else if (nic->rxd_mode == RXD_MODE_3B) {
			pci_dma_sync_single_for_cpu(nic->pdev, (dma_addr_t)
				 ((struct RxD3*)rxdp)->Buffer0_ptr,
				 BUF0_LEN, PCI_DMA_FROMDEVICE);
			pci_unmap_single(nic->pdev, (dma_addr_t)
				 ((struct RxD3*)rxdp)->Buffer2_ptr,
				 dev->mtu + 4,
				 PCI_DMA_FROMDEVICE);
		} else {
			pci_dma_sync_single_for_cpu(nic->pdev, (dma_addr_t)
					 ((struct RxD3*)rxdp)->Buffer0_ptr, BUF0_LEN,
					 PCI_DMA_FROMDEVICE);
			pci_unmap_single(nic->pdev, (dma_addr_t)
					 ((struct RxD3*)rxdp)->Buffer1_ptr,
					 l3l4hdr_size + 4,
					 PCI_DMA_FROMDEVICE);
			pci_unmap_single(nic->pdev, (dma_addr_t)
					 ((struct RxD3*)rxdp)->Buffer2_ptr,
					 dev->mtu, PCI_DMA_FROMDEVICE);
		}
		prefetch(skb->data);
		rx_osm_handler(ring_data, rxdp);
		get_info.offset++;
		ring_data->rx_curr_get_info.offset = get_info.offset;
		rxdp = ring_data->rx_blocks[get_block].
				rxds[get_info.offset].virt_addr;
		if (get_info.offset == rxd_count[nic->rxd_mode]) {
			get_info.offset = 0;
			ring_data->rx_curr_get_info.offset = get_info.offset;
			get_block++;
			if (get_block == ring_data->block_count)
				get_block = 0;
			ring_data->rx_curr_get_info.block_index = get_block;
			rxdp = ring_data->rx_blocks[get_block].block_virt_addr;
		}

		nic->pkts_to_process -= 1;
		if ((napi) && (!nic->pkts_to_process))
			break;
		pkt_cnt++;
		if ((indicate_max_pkts) && (pkt_cnt > indicate_max_pkts))
			break;
	}
	if (nic->lro) {
		/* Clear all LRO sessions before exiting */
		for (i=0; i<MAX_LRO_SESSIONS; i++) {
			struct lro *lro = &nic->lro0_n[i];
			if (lro->in_use) {
				update_L3L4_header(nic, lro);
				queue_rx_frame(lro->parent);
				clear_lro_session(lro);
			}
		}
	}

	spin_unlock(&nic->rx_lock);
}

/**
 *  tx_intr_handler - Transmit interrupt handler
 *  @nic : device private variable
 *  Description:
 *  If an interrupt was raised to indicate DMA complete of the
 *  Tx packet, this function is called. It identifies the last TxD
 *  whose buffer was freed and frees all skbs whose data have already
 *  DMA'ed into the NICs internal memory.
 *  Return Value:
 *  NONE
 */

static void tx_intr_handler(struct fifo_info *fifo_data)
{
	struct s2io_nic *nic = fifo_data->nic;
	struct net_device *dev = (struct net_device *) nic->dev;
	struct tx_curr_get_info get_info, put_info;
	struct sk_buff *skb;
	struct TxD *txdlp;

	get_info = fifo_data->tx_curr_get_info;
	memcpy(&put_info, &fifo_data->tx_curr_put_info, sizeof(put_info));
	txdlp = (struct TxD *) fifo_data->list_info[get_info.offset].
	    list_virt_addr;
	while ((!(txdlp->Control_1 & TXD_LIST_OWN_XENA)) &&
	       (get_info.offset != put_info.offset) &&
	       (txdlp->Host_Control)) {
		/* Check for TxD errors */
		if (txdlp->Control_1 & TXD_T_CODE) {
			unsigned long long err;
			err = txdlp->Control_1 & TXD_T_CODE;
			if (err & 0x1) {
				nic->mac_control.stats_info->sw_stat.
						parity_err_cnt++;
			}

			/* update t_code statistics */
			err >>= 48;
			switch(err) {
				case 2:
					nic->mac_control.stats_info->sw_stat.
							tx_buf_abort_cnt++;
				break;

				case 3:
					nic->mac_control.stats_info->sw_stat.
							tx_desc_abort_cnt++;
				break;

				case 7:
					nic->mac_control.stats_info->sw_stat.
							tx_parity_err_cnt++;
				break;

				case 10:
					nic->mac_control.stats_info->sw_stat.
							tx_link_loss_cnt++;
				break;

				case 15:
					nic->mac_control.stats_info->sw_stat.
							tx_list_proc_err_cnt++;
				break;
                        }
		}

		skb = s2io_txdl_getskb(fifo_data, txdlp, get_info.offset);
		if (skb == NULL) {
			DBG_PRINT(ERR_DBG, "%s: Null skb ",
			__FUNCTION__);
			DBG_PRINT(ERR_DBG, "in Tx Free Intr\n");
			return;
		}

		/* Updating the statistics block */
		nic->stats.tx_bytes += skb->len;
		nic->mac_control.stats_info->sw_stat.mem_freed += skb->truesize;
		dev_kfree_skb_irq(skb);

		get_info.offset++;
		if (get_info.offset == get_info.fifo_len + 1)
			get_info.offset = 0;
		txdlp = (struct TxD *) fifo_data->list_info
		    [get_info.offset].list_virt_addr;
		fifo_data->tx_curr_get_info.offset =
		    get_info.offset;
	}

	spin_lock(&nic->tx_lock);
	if (netif_queue_stopped(dev))
		netif_wake_queue(dev);
	spin_unlock(&nic->tx_lock);
}

/**
 *  s2io_mdio_write - Function to write in to MDIO registers
 *  @mmd_type : MMD type value (PMA/PMD/WIS/PCS/PHYXS)
 *  @addr     : address value
 *  @value    : data value
 *  @dev      : pointer to net_device structure
 *  Description:
 *  This function is used to write values to the MDIO registers
 *  NONE
 */
static void s2io_mdio_write(u32 mmd_type, u64 addr, u16 value, struct net_device *dev)
{
	u64 val64 = 0x0;
	struct s2io_nic *sp = dev->priv;
	struct XENA_dev_config __iomem *bar0 = sp->bar0;

	//address transaction
	val64 = val64 | MDIO_MMD_INDX_ADDR(addr)
			| MDIO_MMD_DEV_ADDR(mmd_type)
			| MDIO_MMS_PRT_ADDR(0x0);
	writeq(val64, &bar0->mdio_control);
	val64 = val64 | MDIO_CTRL_START_TRANS(0xE);
	writeq(val64, &bar0->mdio_control);
	udelay(100);

	//Data transaction
	val64 = 0x0;
	val64 = val64 | MDIO_MMD_INDX_ADDR(addr)
			| MDIO_MMD_DEV_ADDR(mmd_type)
			| MDIO_MMS_PRT_ADDR(0x0)
			| MDIO_MDIO_DATA(value)
			| MDIO_OP(MDIO_OP_WRITE_TRANS);
	writeq(val64, &bar0->mdio_control);
	val64 = val64 | MDIO_CTRL_START_TRANS(0xE);
	writeq(val64, &bar0->mdio_control);
	udelay(100);

	val64 = 0x0;
	val64 = val64 | MDIO_MMD_INDX_ADDR(addr)
	| MDIO_MMD_DEV_ADDR(mmd_type)
	| MDIO_MMS_PRT_ADDR(0x0)
	| MDIO_OP(MDIO_OP_READ_TRANS);
	writeq(val64, &bar0->mdio_control);
	val64 = val64 | MDIO_CTRL_START_TRANS(0xE);
	writeq(val64, &bar0->mdio_control);
	udelay(100);

}

/**
 *  s2io_mdio_read - Function to write in to MDIO registers
 *  @mmd_type : MMD type value (PMA/PMD/WIS/PCS/PHYXS)
 *  @addr     : address value
 *  @dev      : pointer to net_device structure
 *  Description:
 *  This function is used to read values to the MDIO registers
 *  NONE
 */
static u64 s2io_mdio_read(u32 mmd_type, u64 addr, struct net_device *dev)
{
	u64 val64 = 0x0;
	u64 rval64 = 0x0;
	struct s2io_nic *sp = dev->priv;
	struct XENA_dev_config __iomem *bar0 = sp->bar0;

	/* address transaction */
	val64 = val64 | MDIO_MMD_INDX_ADDR(addr)
			| MDIO_MMD_DEV_ADDR(mmd_type)
			| MDIO_MMS_PRT_ADDR(0x0);
	writeq(val64, &bar0->mdio_control);
	val64 = val64 | MDIO_CTRL_START_TRANS(0xE);
	writeq(val64, &bar0->mdio_control);
	udelay(100);

	/* Data transaction */
	val64 = 0x0;
	val64 = val64 | MDIO_MMD_INDX_ADDR(addr)
			| MDIO_MMD_DEV_ADDR(mmd_type)
			| MDIO_MMS_PRT_ADDR(0x0)
			| MDIO_OP(MDIO_OP_READ_TRANS);
	writeq(val64, &bar0->mdio_control);
	val64 = val64 | MDIO_CTRL_START_TRANS(0xE);
	writeq(val64, &bar0->mdio_control);
	udelay(100);

	/* Read the value from regs */
	rval64 = readq(&bar0->mdio_control);
	rval64 = rval64 & 0xFFFF0000;
	rval64 = rval64 >> 16;
	return rval64;
}
/**
 *  s2io_chk_xpak_counter - Function to check the status of the xpak counters
 *  @counter      : couter value to be updated
 *  @flag         : flag to indicate the status
 *  @type         : counter type
 *  Description:
 *  This function is to check the status of the xpak counters value
 *  NONE
 */

static void s2io_chk_xpak_counter(u64 *counter, u64 * regs_stat, u32 index, u16 flag, u16 type)
{
	u64 mask = 0x3;
	u64 val64;
	int i;
	for(i = 0; i <index; i++)
		mask = mask << 0x2;

	if(flag > 0)
	{
		*counter = *counter + 1;
		val64 = *regs_stat & mask;
		val64 = val64 >> (index * 0x2);
		val64 = val64 + 1;
		if(val64 == 3)
		{
			switch(type)
			{
			case 1:
				DBG_PRINT(ERR_DBG, "Take Xframe NIC out of "
					  "service. Excessive temperatures may "
					  "result in premature transceiver "
					  "failure \n");
			break;
			case 2:
				DBG_PRINT(ERR_DBG, "Take Xframe NIC out of "
					  "service Excessive bias currents may "
					  "indicate imminent laser diode "
					  "failure \n");
			break;
			case 3:
				DBG_PRINT(ERR_DBG, "Take Xframe NIC out of "
					  "service Excessive laser output "
					  "power may saturate far-end "
					  "receiver\n");
			break;
			default:
				DBG_PRINT(ERR_DBG, "Incorrect XPAK Alarm "
					  "type \n");
			}
			val64 = 0x0;
		}
		val64 = val64 << (index * 0x2);
		*regs_stat = (*regs_stat & (~mask)) | (val64);

	} else {
		*regs_stat = *regs_stat & (~mask);
	}
}

/**
 *  s2io_updt_xpak_counter - Function to update the xpak counters
 *  @dev         : pointer to net_device struct
 *  Description:
 *  This function is to upate the status of the xpak counters value
 *  NONE
 */
static void s2io_updt_xpak_counter(struct net_device *dev)
{
	u16 flag  = 0x0;
	u16 type  = 0x0;
	u16 val16 = 0x0;
	u64 val64 = 0x0;
	u64 addr  = 0x0;

	struct s2io_nic *sp = dev->priv;
	struct stat_block *stat_info = sp->mac_control.stats_info;

	/* Check the communication with the MDIO slave */
	addr = 0x0000;
	val64 = 0x0;
	val64 = s2io_mdio_read(MDIO_MMD_PMA_DEV_ADDR, addr, dev);
	if((val64 == 0xFFFF) || (val64 == 0x0000))
	{
		DBG_PRINT(ERR_DBG, "ERR: MDIO slave access failed - "
			  "Returned %llx\n", (unsigned long long)val64);
		return;
	}

	/* Check for the expecte value of 2040 at PMA address 0x0000 */
	if(val64 != 0x2040)
	{
		DBG_PRINT(ERR_DBG, "Incorrect value at PMA address 0x0000 - ");
		DBG_PRINT(ERR_DBG, "Returned: %llx- Expected: 0x2040\n",
			  (unsigned long long)val64);
		return;
	}

	/* Loading the DOM register to MDIO register */
	addr = 0xA100;
	s2io_mdio_write(MDIO_MMD_PMA_DEV_ADDR, addr, val16, dev);
	val64 = s2io_mdio_read(MDIO_MMD_PMA_DEV_ADDR, addr, dev);

	/* Reading the Alarm flags */
	addr = 0xA070;
	val64 = 0x0;
	val64 = s2io_mdio_read(MDIO_MMD_PMA_DEV_ADDR, addr, dev);

	flag = CHECKBIT(val64, 0x7);
	type = 1;
	s2io_chk_xpak_counter(&stat_info->xpak_stat.alarm_transceiver_temp_high,
				&stat_info->xpak_stat.xpak_regs_stat,
				0x0, flag, type);

	if(CHECKBIT(val64, 0x6))
		stat_info->xpak_stat.alarm_transceiver_temp_low++;

	flag = CHECKBIT(val64, 0x3);
	type = 2;
	s2io_chk_xpak_counter(&stat_info->xpak_stat.alarm_laser_bias_current_high,
				&stat_info->xpak_stat.xpak_regs_stat,
				0x2, flag, type);

	if(CHECKBIT(val64, 0x2))
		stat_info->xpak_stat.alarm_laser_bias_current_low++;

	flag = CHECKBIT(val64, 0x1);
	type = 3;
	s2io_chk_xpak_counter(&stat_info->xpak_stat.alarm_laser_output_power_high,
				&stat_info->xpak_stat.xpak_regs_stat,
				0x4, flag, type);

	if(CHECKBIT(val64, 0x0))
		stat_info->xpak_stat.alarm_laser_output_power_low++;

	/* Reading the Warning flags */
	addr = 0xA074;
	val64 = 0x0;
	val64 = s2io_mdio_read(MDIO_MMD_PMA_DEV_ADDR, addr, dev);

	if(CHECKBIT(val64, 0x7))
		stat_info->xpak_stat.warn_transceiver_temp_high++;

	if(CHECKBIT(val64, 0x6))
		stat_info->xpak_stat.warn_transceiver_temp_low++;

	if(CHECKBIT(val64, 0x3))
		stat_info->xpak_stat.warn_laser_bias_current_high++;

	if(CHECKBIT(val64, 0x2))
		stat_info->xpak_stat.warn_laser_bias_current_low++;

	if(CHECKBIT(val64, 0x1))
		stat_info->xpak_stat.warn_laser_output_power_high++;

	if(CHECKBIT(val64, 0x0))
		stat_info->xpak_stat.warn_laser_output_power_low++;
}

/**
 *  alarm_intr_handler - Alarm Interrrupt handler
 *  @nic: device private variable
 *  Description: If the interrupt was neither because of Rx packet or Tx
 *  complete, this function is called. If the interrupt was to indicate
 *  a loss of link, the OSM link status handler is invoked for any other
 *  alarm interrupt the block that raised the interrupt is displayed
 *  and a H/W reset is issued.
 *  Return Value:
 *  NONE
*/

static void alarm_intr_handler(struct s2io_nic *nic)
{
	struct net_device *dev = (struct net_device *) nic->dev;
	struct XENA_dev_config __iomem *bar0 = nic->bar0;
	register u64 val64 = 0, err_reg = 0;
	u64 cnt;
	int i;
	if (atomic_read(&nic->card_state) == CARD_DOWN)
		return;
	nic->mac_control.stats_info->sw_stat.ring_full_cnt = 0;
	/* Handling the XPAK counters update */
	if(nic->mac_control.stats_info->xpak_stat.xpak_timer_count < 72000) {
		/* waiting for an hour */
		nic->mac_control.stats_info->xpak_stat.xpak_timer_count++;
	} else {
		s2io_updt_xpak_counter(dev);
		/* reset the count to zero */
		nic->mac_control.stats_info->xpak_stat.xpak_timer_count = 0;
	}

	/* Handling link status change error Intr */
	if (s2io_link_fault_indication(nic) == MAC_RMAC_ERR_TIMER) {
		err_reg = readq(&bar0->mac_rmac_err_reg);
		writeq(err_reg, &bar0->mac_rmac_err_reg);
		if (err_reg & RMAC_LINK_STATE_CHANGE_INT) {
			schedule_work(&nic->set_link_task);
		}
	}

	/* Handling Ecc errors */
	val64 = readq(&bar0->mc_err_reg);
	writeq(val64, &bar0->mc_err_reg);
	if (val64 & (MC_ERR_REG_ECC_ALL_SNG | MC_ERR_REG_ECC_ALL_DBL)) {
		if (val64 & MC_ERR_REG_ECC_ALL_DBL) {
			nic->mac_control.stats_info->sw_stat.
				double_ecc_errs++;
			DBG_PRINT(INIT_DBG, "%s: Device indicates ",
				  dev->name);
			DBG_PRINT(INIT_DBG, "double ECC error!!\n");
			if (nic->device_type != XFRAME_II_DEVICE) {
				/* Reset XframeI only if critical error */
				if (val64 & (MC_ERR_REG_MIRI_ECC_DB_ERR_0 |
					     MC_ERR_REG_MIRI_ECC_DB_ERR_1)) {
					netif_stop_queue(dev);
					schedule_work(&nic->rst_timer_task);
					nic->mac_control.stats_info->sw_stat.
							soft_reset_cnt++;
				}
			}
		} else {
			nic->mac_control.stats_info->sw_stat.
				single_ecc_errs++;
		}
	}

	/* In case of a serious error, the device will be Reset. */
	val64 = readq(&bar0->serr_source);
	if (val64 & SERR_SOURCE_ANY) {
		nic->mac_control.stats_info->sw_stat.serious_err_cnt++;
		DBG_PRINT(ERR_DBG, "%s: Device indicates ", dev->name);
		DBG_PRINT(ERR_DBG, "serious error %llx!!\n",
			  (unsigned long long)val64);
		netif_stop_queue(dev);
		schedule_work(&nic->rst_timer_task);
		nic->mac_control.stats_info->sw_stat.soft_reset_cnt++;
	}

	/*
	 * Also as mentioned in the latest Errata sheets if the PCC_FB_ECC
	 * Error occurs, the adapter will be recycled by disabling the
	 * adapter enable bit and enabling it again after the device
	 * becomes Quiescent.
	 */
	val64 = readq(&bar0->pcc_err_reg);
	writeq(val64, &bar0->pcc_err_reg);
	if (val64 & PCC_FB_ECC_DB_ERR) {
		u64 ac = readq(&bar0->adapter_control);
		ac &= ~(ADAPTER_CNTL_EN);
		writeq(ac, &bar0->adapter_control);
		ac = readq(&bar0->adapter_control);
		schedule_work(&nic->set_link_task);
	}
	/* Check for data parity error */
	val64 = readq(&bar0->pic_int_status);
	if (val64 & PIC_INT_GPIO) {
		val64 = readq(&bar0->gpio_int_reg);
		if (val64 & GPIO_INT_REG_DP_ERR_INT) {
			nic->mac_control.stats_info->sw_stat.parity_err_cnt++;
			schedule_work(&nic->rst_timer_task);
			nic->mac_control.stats_info->sw_stat.soft_reset_cnt++;
		}
	}

	/* Check for ring full counter */
	if (nic->device_type & XFRAME_II_DEVICE) {
		val64 = readq(&bar0->ring_bump_counter1);
		for (i=0; i<4; i++) {
			cnt = ( val64 & vBIT(0xFFFF,(i*16),16));
			cnt >>= 64 - ((i+1)*16);
			nic->mac_control.stats_info->sw_stat.ring_full_cnt
				+= cnt;
		}

		val64 = readq(&bar0->ring_bump_counter2);
		for (i=0; i<4; i++) {
			cnt = ( val64 & vBIT(0xFFFF,(i*16),16));
			cnt >>= 64 - ((i+1)*16);
			nic->mac_control.stats_info->sw_stat.ring_full_cnt
				+= cnt;
		}
	}

	/* Other type of interrupts are not being handled now,  TODO */
}

/**
 *  wait_for_cmd_complete - waits for a command to complete.
 *  @sp : private member of the device structure, which is a pointer to the
 *  s2io_nic structure.
 *  Description: Function that waits for a command to Write into RMAC
 *  ADDR DATA registers to be completed and returns either success or
 *  error depending on whether the command was complete or not.
 *  Return value:
 *   SUCCESS on success and FAILURE on failure.
 */

static int wait_for_cmd_complete(void __iomem *addr, u64 busy_bit,
				int bit_state)
{
	int ret = FAILURE, cnt = 0, delay = 1;
	u64 val64;

	if ((bit_state != S2IO_BIT_RESET) && (bit_state != S2IO_BIT_SET))
		return FAILURE;

	do {
		val64 = readq(addr);
		if (bit_state == S2IO_BIT_RESET) {
			if (!(val64 & busy_bit)) {
				ret = SUCCESS;
				break;
			}
		} else {
			if (!(val64 & busy_bit)) {
				ret = SUCCESS;
				break;
			}
		}

		if(in_interrupt())
			mdelay(delay);
		else
			msleep(delay);

		if (++cnt >= 10)
			delay = 50;
	} while (cnt < 20);
	return ret;
}
/*
 * check_pci_device_id - Checks if the device id is supported
 * @id : device id
 * Description: Function to check if the pci device id is supported by driver.
 * Return value: Actual device id if supported else PCI_ANY_ID
 */
static u16 check_pci_device_id(u16 id)
{
	switch (id) {
	case PCI_DEVICE_ID_HERC_WIN:
	case PCI_DEVICE_ID_HERC_UNI:
		return XFRAME_II_DEVICE;
	case PCI_DEVICE_ID_S2IO_UNI:
	case PCI_DEVICE_ID_S2IO_WIN:
		return XFRAME_I_DEVICE;
	default:
		return PCI_ANY_ID;
	}
}

/**
 *  s2io_reset - Resets the card.
 *  @sp : private member of the device structure.
 *  Description: Function to Reset the card. This function then also
 *  restores the previously saved PCI configuration space registers as
 *  the card reset also resets the configuration space.
 *  Return value:
 *  void.
 */

static void s2io_reset(struct s2io_nic * sp)
{
	struct XENA_dev_config __iomem *bar0 = sp->bar0;
	u64 val64;
	u16 subid, pci_cmd;
	int i;
	u16 val16;
	unsigned long long up_cnt, down_cnt, up_time, down_time, reset_cnt;
	unsigned long long mem_alloc_cnt, mem_free_cnt, watchdog_cnt;

	DBG_PRINT(INIT_DBG,"%s - Resetting XFrame card %s\n",
			__FUNCTION__, sp->dev->name);

	/* Back up  the PCI-X CMD reg, dont want to lose MMRBC, OST settings */
	pci_read_config_word(sp->pdev, PCIX_COMMAND_REGISTER, &(pci_cmd));

	if (sp->device_type == XFRAME_II_DEVICE) {
		int ret;
		ret = pci_set_power_state(sp->pdev, 3);
		if (!ret)
			ret = pci_set_power_state(sp->pdev, 0);
		else {
			DBG_PRINT(ERR_DBG,"%s PME based SW_Reset failed!\n",
					__FUNCTION__);
			goto old_way;
		}
		msleep(20);
		goto new_way;
	}
old_way:
	val64 = SW_RESET_ALL;
	writeq(val64, &bar0->sw_reset);
new_way:
	if (strstr(sp->product_name, "CX4")) {
		msleep(750);
	}
	msleep(250);
	for (i = 0; i < S2IO_MAX_PCI_CONFIG_SPACE_REINIT; i++) {

		/* Restore the PCI state saved during initialization. */
		pci_restore_state(sp->pdev);
		pci_read_config_word(sp->pdev, 0x2, &val16);
		if (check_pci_device_id(val16) != (u16)PCI_ANY_ID)
			break;
		msleep(200);
	}

	if (check_pci_device_id(val16) == (u16)PCI_ANY_ID) {
		DBG_PRINT(ERR_DBG,"%s SW_Reset failed!\n", __FUNCTION__);
	}

	pci_write_config_word(sp->pdev, PCIX_COMMAND_REGISTER, pci_cmd);

	s2io_init_pci(sp);

	/* Set swapper to enable I/O register access */
	s2io_set_swapper(sp);

	/* Restore the MSIX table entries from local variables */
	restore_xmsi_data(sp);

	/* Clear certain PCI/PCI-X fields after reset */
	if (sp->device_type == XFRAME_II_DEVICE) {
		/* Clear "detected parity error" bit */
		pci_write_config_word(sp->pdev, PCI_STATUS, 0x8000);

		/* Clearing PCIX Ecc status register */
		pci_write_config_dword(sp->pdev, 0x68, 0x7C);

		/* Clearing PCI_STATUS error reflected here */
		writeq(BIT(62), &bar0->txpic_int_reg);
	}

	/* Reset device statistics maintained by OS */
	memset(&sp->stats, 0, sizeof (struct net_device_stats));
	
	up_cnt = sp->mac_control.stats_info->sw_stat.link_up_cnt;
	down_cnt = sp->mac_control.stats_info->sw_stat.link_down_cnt;
	up_time = sp->mac_control.stats_info->sw_stat.link_up_time;
	down_time = sp->mac_control.stats_info->sw_stat.link_down_time;
	reset_cnt = sp->mac_control.stats_info->sw_stat.soft_reset_cnt;
	mem_alloc_cnt = sp->mac_control.stats_info->sw_stat.mem_allocated;
	mem_free_cnt = sp->mac_control.stats_info->sw_stat.mem_freed;
	watchdog_cnt = sp->mac_control.stats_info->sw_stat.watchdog_timer_cnt;
	/* save link up/down time/cnt, reset/memory/watchdog cnt */
	memset(sp->mac_control.stats_info, 0, sizeof(struct stat_block));
	/* restore link up/down time/cnt, reset/memory/watchdog cnt */
	sp->mac_control.stats_info->sw_stat.link_up_cnt = up_cnt;
	sp->mac_control.stats_info->sw_stat.link_down_cnt = down_cnt;
	sp->mac_control.stats_info->sw_stat.link_up_time = up_time;
	sp->mac_control.stats_info->sw_stat.link_down_time = down_time;
	sp->mac_control.stats_info->sw_stat.soft_reset_cnt = reset_cnt;
	sp->mac_control.stats_info->sw_stat.mem_allocated = mem_alloc_cnt;
	sp->mac_control.stats_info->sw_stat.mem_freed = mem_free_cnt;
	sp->mac_control.stats_info->sw_stat.watchdog_timer_cnt = watchdog_cnt;

	/* SXE-002: Configure link and activity LED to turn it off */
	subid = sp->pdev->subsystem_device;
	if (((subid & 0xFF) >= 0x07) &&
	    (sp->device_type == XFRAME_I_DEVICE)) {
		val64 = readq(&bar0->gpio_control);
		val64 |= 0x0000800000000000ULL;
		writeq(val64, &bar0->gpio_control);
		val64 = 0x0411040400000000ULL;
		writeq(val64, (void __iomem *)bar0 + 0x2700);
	}

	/*
	 * Clear spurious ECC interrupts that would have occured on
	 * XFRAME II cards after reset.
	 */
	if (sp->device_type == XFRAME_II_DEVICE) {
		val64 = readq(&bar0->pcc_err_reg);
		writeq(val64, &bar0->pcc_err_reg);
	}

	/* restore the previously assigned mac address */
	s2io_set_mac_addr(sp->dev, (u8 *)&sp->def_mac_addr[0].mac_addr);

	sp->device_enabled_once = FALSE;
}

/**
 *  s2io_set_swapper - to set the swapper controle on the card
 *  @sp : private member of the device structure,
 *  pointer to the s2io_nic structure.
 *  Description: Function to set the swapper control on the card
 *  correctly depending on the 'endianness' of the system.
 *  Return value:
 *  SUCCESS on success and FAILURE on failure.
 */

static int s2io_set_swapper(struct s2io_nic * sp)
{
	struct net_device *dev = sp->dev;
	struct XENA_dev_config __iomem *bar0 = sp->bar0;
	u64 val64, valt, valr;

	/*
	 * Set proper endian settings and verify the same by reading
	 * the PIF Feed-back register.
	 */

	val64 = readq(&bar0->pif_rd_swapper_fb);
	if (val64 != 0x0123456789ABCDEFULL) {
		int i = 0;
		u64 value[] = { 0xC30000C3C30000C3ULL,   /* FE=1, SE=1 */
				0x8100008181000081ULL,  /* FE=1, SE=0 */
				0x4200004242000042ULL,  /* FE=0, SE=1 */
				0};                     /* FE=0, SE=0 */

		while(i<4) {
			writeq(value[i], &bar0->swapper_ctrl);
			val64 = readq(&bar0->pif_rd_swapper_fb);
			if (val64 == 0x0123456789ABCDEFULL)
				break;
			i++;
		}
		if (i == 4) {
			DBG_PRINT(ERR_DBG, "%s: Endian settings are wrong, ",
				dev->name);
			DBG_PRINT(ERR_DBG, "feedback read %llx\n",
				(unsigned long long) val64);
			return FAILURE;
		}
		valr = value[i];
	} else {
		valr = readq(&bar0->swapper_ctrl);
	}

	valt = 0x0123456789ABCDEFULL;
	writeq(valt, &bar0->xmsi_address);
	val64 = readq(&bar0->xmsi_address);

	if(val64 != valt) {
		int i = 0;
		u64 value[] = { 0x00C3C30000C3C300ULL,  /* FE=1, SE=1 */
				0x0081810000818100ULL,  /* FE=1, SE=0 */
				0x0042420000424200ULL,  /* FE=0, SE=1 */
				0};                     /* FE=0, SE=0 */

		while(i<4) {
			writeq((value[i] | valr), &bar0->swapper_ctrl);
			writeq(valt, &bar0->xmsi_address);
			val64 = readq(&bar0->xmsi_address);
			if(val64 == valt)
				break;
			i++;
		}
		if(i == 4) {
			unsigned long long x = val64;
			DBG_PRINT(ERR_DBG, "Write failed, Xmsi_addr ");
			DBG_PRINT(ERR_DBG, "reads:0x%llx\n", x);
			return FAILURE;
		}
	}
	val64 = readq(&bar0->swapper_ctrl);
	val64 &= 0xFFFF000000000000ULL;

#ifdef  __BIG_ENDIAN
	/*
	 * The device by default set to a big endian format, so a
	 * big endian driver need not set anything.
	 */
	val64 |= (SWAPPER_CTRL_TXP_FE |
		 SWAPPER_CTRL_TXP_SE |
		 SWAPPER_CTRL_TXD_R_FE |
		 SWAPPER_CTRL_TXD_W_FE |
		 SWAPPER_CTRL_TXF_R_FE |
		 SWAPPER_CTRL_RXD_R_FE |
		 SWAPPER_CTRL_RXD_W_FE |
		 SWAPPER_CTRL_RXF_W_FE |
		 SWAPPER_CTRL_XMSI_FE |
		 SWAPPER_CTRL_STATS_FE | SWAPPER_CTRL_STATS_SE);
	if (sp->intr_type == INTA)
		val64 |= SWAPPER_CTRL_XMSI_SE;
	writeq(val64, &bar0->swapper_ctrl);
#else
	/*
	 * Initially we enable all bits to make it accessible by the
	 * driver, then we selectively enable only those bits that
	 * we want to set.
	 */
	val64 |= (SWAPPER_CTRL_TXP_FE |
		 SWAPPER_CTRL_TXP_SE |
		 SWAPPER_CTRL_TXD_R_FE |
		 SWAPPER_CTRL_TXD_R_SE |
		 SWAPPER_CTRL_TXD_W_FE |
		 SWAPPER_CTRL_TXD_W_SE |
		 SWAPPER_CTRL_TXF_R_FE |
		 SWAPPER_CTRL_RXD_R_FE |
		 SWAPPER_CTRL_RXD_R_SE |
		 SWAPPER_CTRL_RXD_W_FE |
		 SWAPPER_CTRL_RXD_W_SE |
		 SWAPPER_CTRL_RXF_W_FE |
		 SWAPPER_CTRL_XMSI_FE |
		 SWAPPER_CTRL_STATS_FE | SWAPPER_CTRL_STATS_SE);
	if (sp->intr_type == INTA)
		val64 |= SWAPPER_CTRL_XMSI_SE;
	writeq(val64, &bar0->swapper_ctrl);
#endif
	val64 = readq(&bar0->swapper_ctrl);

	/*
	 * Verifying if endian settings are accurate by reading a
	 * feedback register.
	 */
	val64 = readq(&bar0->pif_rd_swapper_fb);
	if (val64 != 0x0123456789ABCDEFULL) {
		/* Endian settings are incorrect, calls for another dekko. */
		DBG_PRINT(ERR_DBG, "%s: Endian settings are wrong, ",
			  dev->name);
		DBG_PRINT(ERR_DBG, "feedback read %llx\n",
			  (unsigned long long) val64);
		return FAILURE;
	}

	return SUCCESS;
}

static int wait_for_msix_trans(struct s2io_nic *nic, int i)
{
	struct XENA_dev_config __iomem *bar0 = nic->bar0;
	u64 val64;
	int ret = 0, cnt = 0;

	do {
		val64 = readq(&bar0->xmsi_access);
		if (!(val64 & BIT(15)))
			break;
		mdelay(1);
		cnt++;
	} while(cnt < 5);
	if (cnt == 5) {
		DBG_PRINT(ERR_DBG, "XMSI # %d Access failed\n", i);
		ret = 1;
	}

	return ret;
}

static void restore_xmsi_data(struct s2io_nic *nic)
{
	struct XENA_dev_config __iomem *bar0 = nic->bar0;
	u64 val64;
	int i;

	for (i=0; i < MAX_REQUESTED_MSI_X; i++) {
		writeq(nic->msix_info[i].addr, &bar0->xmsi_address);
		writeq(nic->msix_info[i].data, &bar0->xmsi_data);
		val64 = (BIT(7) | BIT(15) | vBIT(i, 26, 6));
		writeq(val64, &bar0->xmsi_access);
		if (wait_for_msix_trans(nic, i)) {
			DBG_PRINT(ERR_DBG, "failed in %s\n", __FUNCTION__);
			continue;
		}
	}
}

static void store_xmsi_data(struct s2io_nic *nic)
{
	struct XENA_dev_config __iomem *bar0 = nic->bar0;
	u64 val64, addr, data;
	int i;

	/* Store and display */
	for (i=0; i < MAX_REQUESTED_MSI_X; i++) {
		val64 = (BIT(15) | vBIT(i, 26, 6));
		writeq(val64, &bar0->xmsi_access);
		if (wait_for_msix_trans(nic, i)) {
			DBG_PRINT(ERR_DBG, "failed in %s\n", __FUNCTION__);
			continue;
		}
		addr = readq(&bar0->xmsi_address);
		data = readq(&bar0->xmsi_data);
		if (addr && data) {
			nic->msix_info[i].addr = addr;
			nic->msix_info[i].data = data;
		}
	}
}

int s2io_enable_msi(struct s2io_nic *nic)
{
	struct XENA_dev_config __iomem *bar0 = nic->bar0;
	u16 msi_ctrl, msg_val;
	struct config_param *config = &nic->config;
	struct net_device *dev = nic->dev;
	u64 val64, tx_mat, rx_mat;
	int i, err;

	val64 = readq(&bar0->pic_control);
	val64 &= ~BIT(1);
	writeq(val64, &bar0->pic_control);

	err = pci_enable_msi(nic->pdev);
	if (err) {
		DBG_PRINT(ERR_DBG, "%s: enabling MSI failed\n",
			  nic->dev->name);
		return err;
	}

	/*
	 * Enable MSI and use MSI-1 in stead of the standard MSI-0
	 * for interrupt handling.
	 */
	pci_read_config_word(nic->pdev, 0x4c, &msg_val);
	msg_val ^= 0x1;
	pci_write_config_word(nic->pdev, 0x4c, msg_val);
	pci_read_config_word(nic->pdev, 0x4c, &msg_val);

	pci_read_config_word(nic->pdev, 0x42, &msi_ctrl);
	msi_ctrl |= 0x10;
	pci_write_config_word(nic->pdev, 0x42, msi_ctrl);

	/* program MSI-1 into all usable Tx_Mat and Rx_Mat fields */
	tx_mat = readq(&bar0->tx_mat0_n[0]);
	for (i=0; i<config->tx_fifo_num; i++) {
		tx_mat |= TX_MAT_SET(i, 1);
	}
	writeq(tx_mat, &bar0->tx_mat0_n[0]);

	rx_mat = readq(&bar0->rx_mat);
	for (i=0; i<config->rx_ring_num; i++) {
		rx_mat |= RX_MAT_SET(i, 1);
	}
	writeq(rx_mat, &bar0->rx_mat);

	dev->irq = nic->pdev->irq;
	return 0;
}

static int s2io_enable_msi_x(struct s2io_nic *nic)
{
	struct XENA_dev_config __iomem *bar0 = nic->bar0;
	u64 tx_mat, rx_mat;
	u16 msi_control; /* Temp variable */
	int ret, i, j, msix_indx = 1;

	nic->entries = kmalloc(MAX_REQUESTED_MSI_X * sizeof(struct msix_entry),
			       GFP_KERNEL);
	if (nic->entries == NULL) {
		DBG_PRINT(INFO_DBG, "%s: Memory allocation failed\n", \
			__FUNCTION__);
		nic->mac_control.stats_info->sw_stat.mem_alloc_fail_cnt++;
		return -ENOMEM;
	}
	nic->mac_control.stats_info->sw_stat.mem_allocated 
		+= (MAX_REQUESTED_MSI_X * sizeof(struct msix_entry));
	memset(nic->entries, 0,MAX_REQUESTED_MSI_X * sizeof(struct msix_entry));

	nic->s2io_entries =
		kmalloc(MAX_REQUESTED_MSI_X * sizeof(struct s2io_msix_entry),
				   GFP_KERNEL);
	if (nic->s2io_entries == NULL) {
		DBG_PRINT(INFO_DBG, "%s: Memory allocation failed\n", 
			__FUNCTION__);
		nic->mac_control.stats_info->sw_stat.mem_alloc_fail_cnt++;
		kfree(nic->entries);
		nic->mac_control.stats_info->sw_stat.mem_freed 
			+= (MAX_REQUESTED_MSI_X * sizeof(struct msix_entry));
		return -ENOMEM;
	}
	 nic->mac_control.stats_info->sw_stat.mem_allocated 
		+= (MAX_REQUESTED_MSI_X * sizeof(struct s2io_msix_entry));
	memset(nic->s2io_entries, 0,
	       MAX_REQUESTED_MSI_X * sizeof(struct s2io_msix_entry));

	for (i=0; i< MAX_REQUESTED_MSI_X; i++) {
		nic->entries[i].entry = i;
		nic->s2io_entries[i].entry = i;
		nic->s2io_entries[i].arg = NULL;
		nic->s2io_entries[i].in_use = 0;
	}

	tx_mat = readq(&bar0->tx_mat0_n[0]);
	for (i=0; i<nic->config.tx_fifo_num; i++, msix_indx++) {
		tx_mat |= TX_MAT_SET(i, msix_indx);
		nic->s2io_entries[msix_indx].arg = &nic->mac_control.fifos[i];
		nic->s2io_entries[msix_indx].type = MSIX_FIFO_TYPE;
		nic->s2io_entries[msix_indx].in_use = MSIX_FLG;
	}
	writeq(tx_mat, &bar0->tx_mat0_n[0]);

	if (!nic->config.bimodal) {
		rx_mat = readq(&bar0->rx_mat);
		for (j=0; j<nic->config.rx_ring_num; j++, msix_indx++) {
			rx_mat |= RX_MAT_SET(j, msix_indx);
			nic->s2io_entries[msix_indx].arg 
				= &nic->mac_control.rings[j];
			nic->s2io_entries[msix_indx].type = MSIX_RING_TYPE;
			nic->s2io_entries[msix_indx].in_use = MSIX_FLG;
		}
		writeq(rx_mat, &bar0->rx_mat);
	} else {
		tx_mat = readq(&bar0->tx_mat0_n[7]);
		for (j=0; j<nic->config.rx_ring_num; j++, msix_indx++) {
			tx_mat |= TX_MAT_SET(i, msix_indx);
			nic->s2io_entries[msix_indx].arg 
				= &nic->mac_control.rings[j];
			nic->s2io_entries[msix_indx].type = MSIX_RING_TYPE;
			nic->s2io_entries[msix_indx].in_use = MSIX_FLG;
		}
		writeq(tx_mat, &bar0->tx_mat0_n[7]);
	}

	nic->avail_msix_vectors = 0;
	ret = pci_enable_msix(nic->pdev, nic->entries, MAX_REQUESTED_MSI_X);
	/* We fail init if error or we get less vectors than min required */
	if (ret >= (nic->config.tx_fifo_num + nic->config.rx_ring_num + 1)) {
		nic->avail_msix_vectors = ret;
		ret = pci_enable_msix(nic->pdev, nic->entries, ret);
	}
	if (ret) {
		DBG_PRINT(ERR_DBG, "%s: Enabling MSIX failed\n", nic->dev->name);
		kfree(nic->entries);
		nic->mac_control.stats_info->sw_stat.mem_freed 
			+= (MAX_REQUESTED_MSI_X * sizeof(struct msix_entry));
		kfree(nic->s2io_entries);
		nic->mac_control.stats_info->sw_stat.mem_freed 
		+= (MAX_REQUESTED_MSI_X * sizeof(struct s2io_msix_entry));
		nic->entries = NULL;
		nic->s2io_entries = NULL;
		nic->avail_msix_vectors = 0;
		return -ENOMEM;
	}
	if (!nic->avail_msix_vectors)
		nic->avail_msix_vectors = MAX_REQUESTED_MSI_X;

	/*
	 * To enable MSI-X, MSI also needs to be enabled, due to a bug
	 * in the herc NIC. (Temp change, needs to be removed later)
	 */
	pci_read_config_word(nic->pdev, 0x42, &msi_control);
	msi_control |= 0x1; /* Enable MSI */
	pci_write_config_word(nic->pdev, 0x42, msi_control);

	return 0;
}

/* ********************************************************* *
 * Functions defined below concern the OS part of the driver *
 * ********************************************************* */

/**
 *  s2io_open - open entry point of the driver
 *  @dev : pointer to the device structure.
 *  Description:
 *  This function is the open entry point of the driver. It mainly calls a
 *  function to allocate Rx buffers and inserts them into the buffer
 *  descriptors and then enables the Rx part of the NIC.
 *  Return value:
 *  0 on success and an appropriate (-)ve integer as defined in errno.h
 *   file on failure.
 */

static int s2io_open(struct net_device *dev)
{
	struct s2io_nic *sp = dev->priv;
	int err = 0;

	/*
	 * Make sure you have link off by default every time
	 * Nic is initialized
	 */
	netif_carrier_off(dev);
	sp->last_link_state = 0;

	/* Initialize H/W and enable interrupts */
	err = s2io_card_up(sp);
	if (err) {
		DBG_PRINT(ERR_DBG, "%s: H/W initialization failed\n",
			  dev->name);
		goto hw_init_failed;
	}

	if (s2io_set_mac_addr(dev, dev->dev_addr) == FAILURE) {
		DBG_PRINT(ERR_DBG, "Set Mac Address Failed\n");
		s2io_card_down(sp);
		err = -ENODEV;
		goto hw_init_failed;
	}

	netif_start_queue(dev);
	return 0;

hw_init_failed:
	if (sp->intr_type == MSI_X) {
		if (sp->entries) {
			kfree(sp->entries);
			sp->mac_control.stats_info->sw_stat.mem_freed 
			+= (MAX_REQUESTED_MSI_X * sizeof(struct msix_entry));
		}
		if (sp->s2io_entries) {
			kfree(sp->s2io_entries);
			sp->mac_control.stats_info->sw_stat.mem_freed 
			+= (MAX_REQUESTED_MSI_X * sizeof(struct s2io_msix_entry));
		}
	}
	return err;
}

/**
 *  s2io_close -close entry point of the driver
 *  @dev : device pointer.
 *  Description:
 *  This is the stop entry point of the driver. It needs to undo exactly
 *  whatever was done by the open entry point,thus it's usually referred to
 *  as the close function.Among other things this function mainly stops the
 *  Rx side of the NIC and frees all the Rx buffers in the Rx rings.
 *  Return value:
 *  0 on success and an appropriate (-)ve integer as defined in errno.h
 *  file on failure.
 */

static int s2io_close(struct net_device *dev)
{
	struct s2io_nic *sp = dev->priv;

	netif_stop_queue(dev);
	/* Reset card, kill tasklet and free Tx and Rx buffers. */
	s2io_card_down(sp);

	sp->device_close_flag = TRUE;	/* Device is shut down. */
	return 0;
}

/**
 *  s2io_xmit - Tx entry point of te driver
 *  @skb : the socket buffer containing the Tx data.
 *  @dev : device pointer.
 *  Description :
 *  This function is the Tx entry point of the driver. S2IO NIC supports
 *  certain protocol assist features on Tx side, namely  CSO, S/G, LSO.
 *  NOTE: when device cant queue the pkt,just the trans_start variable will
 *  not be upadted.
 *  Return value:
 *  0 on success & 1 on failure.
 */

static int s2io_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct s2io_nic *sp = dev->priv;
	u16 frg_cnt, frg_len, i, queue, queue_len, put_off, get_off;
	register u64 val64;
	struct TxD *txdp;
	struct TxFIFO_element __iomem *tx_fifo;
	unsigned long flags;
	u16 vlan_tag = 0;
	int vlan_priority = 0;
	struct mac_info *mac_control;
	struct config_param *config;
	int offload_type;

	mac_control = &sp->mac_control;
	config = &sp->config;

	DBG_PRINT(TX_DBG, "%s: In Neterion Tx routine\n", dev->name);

	if (unlikely(skb->len <= 0)) {
		DBG_PRINT(TX_DBG, "%s:Buffer has no data..\n", dev->name);
		dev_kfree_skb_any(skb);
		return 0;
}

	spin_lock_irqsave(&sp->tx_lock, flags);
	if (atomic_read(&sp->card_state) == CARD_DOWN) {
		DBG_PRINT(TX_DBG, "%s: Card going down for reset\n",
			  dev->name);
		spin_unlock_irqrestore(&sp->tx_lock, flags);
		dev_kfree_skb(skb);
		return 0;
	}

	queue = 0;
	/* Get Fifo number to Transmit based on vlan priority */
	if (sp->vlgrp && vlan_tx_tag_present(skb)) {
		vlan_tag = vlan_tx_tag_get(skb);
		vlan_priority = vlan_tag >> 13;
		queue = config->fifo_mapping[vlan_priority];
	}

	put_off = (u16) mac_control->fifos[queue].tx_curr_put_info.offset;
	get_off = (u16) mac_control->fifos[queue].tx_curr_get_info.offset;
	txdp = (struct TxD *) mac_control->fifos[queue].list_info[put_off].
		list_virt_addr;

	queue_len = mac_control->fifos[queue].tx_curr_put_info.fifo_len + 1;
	/* Avoid "put" pointer going beyond "get" pointer */
	if (txdp->Host_Control ||
		   ((put_off+1) == queue_len ? 0 : (put_off+1)) == get_off) {
		DBG_PRINT(TX_DBG, "Error in xmit, No free TXDs.\n");
		netif_stop_queue(dev);
		dev_kfree_skb(skb);
		spin_unlock_irqrestore(&sp->tx_lock, flags);
		return 0;
	}

	offload_type = s2io_offload_type(skb);
	if (offload_type & (SKB_GSO_TCPV4 | SKB_GSO_TCPV6)) {
		txdp->Control_1 |= TXD_TCP_LSO_EN;
		txdp->Control_1 |= TXD_TCP_LSO_MSS(s2io_tcp_mss(skb));
	}
	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		txdp->Control_2 |=
		    (TXD_TX_CKO_IPV4_EN | TXD_TX_CKO_TCP_EN |
		     TXD_TX_CKO_UDP_EN);
	}
	txdp->Control_1 |= TXD_GATHER_CODE_FIRST;
	txdp->Control_1 |= TXD_LIST_OWN_XENA;
	txdp->Control_2 |= config->tx_intr_type;

	if (sp->vlgrp && vlan_tx_tag_present(skb)) {
		txdp->Control_2 |= TXD_VLAN_ENABLE;
		txdp->Control_2 |= TXD_VLAN_TAG(vlan_tag);
	}

	frg_len = skb->len - skb->data_len;
	if (offload_type == SKB_GSO_UDP) {
		int ufo_size;

		ufo_size = s2io_udp_mss(skb);
		ufo_size &= ~7;
		txdp->Control_1 |= TXD_UFO_EN;
		txdp->Control_1 |= TXD_UFO_MSS(ufo_size);
		txdp->Control_1 |= TXD_BUFFER0_SIZE(8);
#ifdef __BIG_ENDIAN
		sp->ufo_in_band_v[put_off] =
				(u64)skb_shinfo(skb)->ip6_frag_id;
#else
		sp->ufo_in_band_v[put_off] =
				(u64)skb_shinfo(skb)->ip6_frag_id << 32;
#endif
		txdp->Host_Control = (unsigned long)sp->ufo_in_band_v;
		txdp->Buffer_Pointer = pci_map_single(sp->pdev,
					sp->ufo_in_band_v,
					sizeof(u64), PCI_DMA_TODEVICE);
		txdp++;
	}

	txdp->Buffer_Pointer = pci_map_single
	    (sp->pdev, skb->data, frg_len, PCI_DMA_TODEVICE);
	txdp->Host_Control = (unsigned long) skb;
	txdp->Control_1 |= TXD_BUFFER0_SIZE(frg_len);
	if (offload_type == SKB_GSO_UDP)
		txdp->Control_1 |= TXD_UFO_EN;

	frg_cnt = skb_shinfo(skb)->nr_frags;
	/* For fragmented SKB. */
	for (i = 0; i < frg_cnt; i++) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[i];
		/* A '0' length fragment will be ignored */
		if (!frag->size)
			continue;
		txdp++;
		txdp->Buffer_Pointer = (u64) pci_map_page
		    (sp->pdev, frag->page, frag->page_offset,
		     frag->size, PCI_DMA_TODEVICE);
		txdp->Control_1 = TXD_BUFFER0_SIZE(frag->size);
		if (offload_type == SKB_GSO_UDP)
			txdp->Control_1 |= TXD_UFO_EN;
	}
	txdp->Control_1 |= TXD_GATHER_CODE_LAST;

	if (offload_type == SKB_GSO_UDP)
		frg_cnt++; /* as Txd0 was used for inband header */

	tx_fifo = mac_control->tx_FIFO_start[queue];
	val64 = mac_control->fifos[queue].list_info[put_off].list_phy_addr;
	writeq(val64, &tx_fifo->TxDL_Pointer);

	val64 = (TX_FIFO_LAST_TXD_NUM(frg_cnt) | TX_FIFO_FIRST_LIST |
		 TX_FIFO_LAST_LIST);
	if (offload_type)
		val64 |= TX_FIFO_SPECIAL_FUNC;

	writeq(val64, &tx_fifo->List_Control);

	mmiowb();

	put_off++;
	if (put_off == mac_control->fifos[queue].tx_curr_put_info.fifo_len + 1)
		put_off = 0;
	mac_control->fifos[queue].tx_curr_put_info.offset = put_off;

	/* Avoid "put" pointer going beyond "get" pointer */
	if (((put_off+1) == queue_len ? 0 : (put_off+1)) == get_off) {
		sp->mac_control.stats_info->sw_stat.fifo_full_cnt++;
		DBG_PRINT(TX_DBG,
			  "No free TxDs for xmit, Put: 0x%x Get:0x%x\n",
			  put_off, get_off);
		netif_stop_queue(dev);
	}
	mac_control->stats_info->sw_stat.mem_allocated += skb->truesize;
	dev->trans_start = jiffies;
	spin_unlock_irqrestore(&sp->tx_lock, flags);

	return 0;
}

static void
s2io_alarm_handle(unsigned long data)
{
	struct s2io_nic *sp = (struct s2io_nic *)data;

	alarm_intr_handler(sp);
	mod_timer(&sp->alarm_timer, jiffies + HZ / 2);
}

static int s2io_chk_rx_buffers(struct s2io_nic *sp, int rng_n)
{
	int rxb_size, level;

	if (!sp->lro) {
		rxb_size = atomic_read(&sp->rx_bufs_left[rng_n]);
		level = rx_buffer_level(sp, rxb_size, rng_n);

		if ((level == PANIC) && (!TASKLET_IN_USE)) {
			int ret;
			DBG_PRINT(INTR_DBG, "%s: Rx BD hit ", __FUNCTION__);
			DBG_PRINT(INTR_DBG, "PANIC levels\n");
			if ((ret = fill_rx_buffers(sp, rng_n)) == -ENOMEM) {
				DBG_PRINT(INFO_DBG, "Out of memory in %s",
					  __FUNCTION__);
				clear_bit(0, (&sp->tasklet_status));
				return -1;
			}
			clear_bit(0, (&sp->tasklet_status));
		} else if (level == LOW)
			tasklet_schedule(&sp->task);

	} else if (fill_rx_buffers(sp, rng_n) == -ENOMEM) {
			DBG_PRINT(INFO_DBG, "%s:Out of memory", sp->dev->name);
			DBG_PRINT(INFO_DBG, " in Rx Intr!!\n");
	}
	return 0;
}

static irqreturn_t s2io_msi_handle(int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *) dev_id;
	struct s2io_nic *sp = dev->priv;
	int i;
	struct mac_info *mac_control;
	struct config_param *config;

	atomic_inc(&sp->isr_cnt);
	mac_control = &sp->mac_control;
	config = &sp->config;
	DBG_PRINT(INTR_DBG, "%s: MSI handler\n", __FUNCTION__);

	/* If Intr is because of Rx Traffic */
	for (i = 0; i < config->rx_ring_num; i++)
		rx_intr_handler(&mac_control->rings[i]);

	/* If Intr is because of Tx Traffic */
	for (i = 0; i < config->tx_fifo_num; i++)
		tx_intr_handler(&mac_control->fifos[i]);

	/*
	 * If the Rx buffer count is below the panic threshold then
	 * reallocate the buffers from the interrupt handler itself,
	 * else schedule a tasklet to reallocate the buffers.
	 */
	for (i = 0; i < config->rx_ring_num; i++)
		s2io_chk_rx_buffers(sp, i);

	atomic_dec(&sp->isr_cnt);
	return IRQ_HANDLED;
}

static irqreturn_t s2io_msix_ring_handle(int irq, void *dev_id)
{
	struct ring_info *ring = (struct ring_info *)dev_id;
	struct s2io_nic *sp = ring->nic;

	atomic_inc(&sp->isr_cnt);

	rx_intr_handler(ring);
	s2io_chk_rx_buffers(sp, ring->ring_no);

	atomic_dec(&sp->isr_cnt);
	return IRQ_HANDLED;
}

static irqreturn_t s2io_msix_fifo_handle(int irq, void *dev_id)
{
	struct fifo_info *fifo = (struct fifo_info *)dev_id;
	struct s2io_nic *sp = fifo->nic;

	atomic_inc(&sp->isr_cnt);
	tx_intr_handler(fifo);
	atomic_dec(&sp->isr_cnt);
	return IRQ_HANDLED;
}
static void s2io_txpic_intr_handle(struct s2io_nic *sp)
{
	struct XENA_dev_config __iomem *bar0 = sp->bar0;
	u64 val64;

	val64 = readq(&bar0->pic_int_status);
	if (val64 & PIC_INT_GPIO) {
		val64 = readq(&bar0->gpio_int_reg);
		if ((val64 & GPIO_INT_REG_LINK_DOWN) &&
		    (val64 & GPIO_INT_REG_LINK_UP)) {
			/*
			 * This is unstable state so clear both up/down
			 * interrupt and adapter to re-evaluate the link state.
			 */
			val64 |=  GPIO_INT_REG_LINK_DOWN;
			val64 |= GPIO_INT_REG_LINK_UP;
			writeq(val64, &bar0->gpio_int_reg);
			val64 = readq(&bar0->gpio_int_mask);
			val64 &= ~(GPIO_INT_MASK_LINK_UP |
				   GPIO_INT_MASK_LINK_DOWN);
			writeq(val64, &bar0->gpio_int_mask);
		}
		else if (val64 & GPIO_INT_REG_LINK_UP) {
			val64 = readq(&bar0->adapter_status);
				/* Enable Adapter */
			val64 = readq(&bar0->adapter_control);
			val64 |= ADAPTER_CNTL_EN;
			writeq(val64, &bar0->adapter_control);
			val64 |= ADAPTER_LED_ON;
			writeq(val64, &bar0->adapter_control);
			if (!sp->device_enabled_once)
				sp->device_enabled_once = 1;

			s2io_link(sp, LINK_UP);
			/*
			 * unmask link down interrupt and mask link-up
			 * intr
			 */
			val64 = readq(&bar0->gpio_int_mask);
			val64 &= ~GPIO_INT_MASK_LINK_DOWN;
			val64 |= GPIO_INT_MASK_LINK_UP;
			writeq(val64, &bar0->gpio_int_mask);

		}else if (val64 & GPIO_INT_REG_LINK_DOWN) {
			val64 = readq(&bar0->adapter_status);
			s2io_link(sp, LINK_DOWN);
			/* Link is down so unmaks link up interrupt */
			val64 = readq(&bar0->gpio_int_mask);
			val64 &= ~GPIO_INT_MASK_LINK_UP;
			val64 |= GPIO_INT_MASK_LINK_DOWN;
			writeq(val64, &bar0->gpio_int_mask);

			/* turn off LED */
			val64 = readq(&bar0->adapter_control);
			val64 = val64 &(~ADAPTER_LED_ON);
			writeq(val64, &bar0->adapter_control);
		}
	}
	val64 = readq(&bar0->gpio_int_mask);
}

/**
 *  s2io_isr - ISR handler of the device .
 *  @irq: the irq of the device.
 *  @dev_id: a void pointer to the dev structure of the NIC.
 *  Description:  This function is the ISR handler of the device. It
 *  identifies the reason for the interrupt and calls the relevant
 *  service routines. As a contongency measure, this ISR allocates the
 *  recv buffers, if their numbers are below the panic value which is
 *  presently set to 25% of the original number of rcv buffers allocated.
 *  Return value:
 *   IRQ_HANDLED: will be returned if IRQ was handled by this routine
 *   IRQ_NONE: will be returned if interrupt is not from our device
 */
static irqreturn_t s2io_isr(int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *) dev_id;
	struct s2io_nic *sp = dev->priv;
	struct XENA_dev_config __iomem *bar0 = sp->bar0;
	int i;
	u64 reason = 0;
	struct mac_info *mac_control;
	struct config_param *config;

	atomic_inc(&sp->isr_cnt);
	mac_control = &sp->mac_control;
	config = &sp->config;

	/*
	 * Identify the cause for interrupt and call the appropriate
	 * interrupt handler. Causes for the interrupt could be;
	 * 1. Rx of packet.
	 * 2. Tx complete.
	 * 3. Link down.
	 * 4. Error in any functional blocks of the NIC.
	 */
	reason = readq(&bar0->general_int_status);

	if (!reason) {
		/* The interrupt was not raised by us. */
		atomic_dec(&sp->isr_cnt);
		return IRQ_NONE;
	}
	else if (unlikely(reason == S2IO_MINUS_ONE) ) {
		/* Disable device and get out */
		atomic_dec(&sp->isr_cnt);
		return IRQ_NONE;
	}

	if (napi) {
		if (reason & GEN_INTR_RXTRAFFIC) {
			if ( likely ( netif_rx_schedule_prep(dev)) ) {
				__netif_rx_schedule(dev);
				writeq(S2IO_MINUS_ONE, &bar0->rx_traffic_mask);
			}
			else
				writeq(S2IO_MINUS_ONE, &bar0->rx_traffic_int);
		}
	} else {
		/*
		 * Rx handler is called by default, without checking for the
		 * cause of interrupt.
		 * rx_traffic_int reg is an R1 register, writing all 1's
		 * will ensure that the actual interrupt causing bit get's
		 * cleared and hence a read can be avoided.
		 */
		if (reason & GEN_INTR_RXTRAFFIC)
			writeq(S2IO_MINUS_ONE, &bar0->rx_traffic_int);

		for (i = 0; i < config->rx_ring_num; i++) {
			rx_intr_handler(&mac_control->rings[i]);
		}
	}

	/*
	 * tx_traffic_int reg is an R1 register, writing all 1's
	 * will ensure that the actual interrupt causing bit get's
	 * cleared and hence a read can be avoided.
	 */
	if (reason & GEN_INTR_TXTRAFFIC)
		writeq(S2IO_MINUS_ONE, &bar0->tx_traffic_int);

	for (i = 0; i < config->tx_fifo_num; i++)
		tx_intr_handler(&mac_control->fifos[i]);

	if (reason & GEN_INTR_TXPIC)
		s2io_txpic_intr_handle(sp);
	/*
	 * If the Rx buffer count is below the panic threshold then
	 * reallocate the buffers from the interrupt handler itself,
	 * else schedule a tasklet to reallocate the buffers.
	 */
	if (!napi) {
		for (i = 0; i < config->rx_ring_num; i++)
			s2io_chk_rx_buffers(sp, i);
	}

	writeq(0, &bar0->general_int_mask);
	readl(&bar0->general_int_status);

	atomic_dec(&sp->isr_cnt);
	return IRQ_HANDLED;
}

/**
 * s2io_updt_stats -
 */
static void s2io_updt_stats(struct s2io_nic *sp)
{
	struct XENA_dev_config __iomem *bar0 = sp->bar0;
	u64 val64;
	int cnt = 0;

	if (atomic_read(&sp->card_state) == CARD_UP) {
		/* Apprx 30us on a 133 MHz bus */
		val64 = SET_UPDT_CLICKS(10) |
			STAT_CFG_ONE_SHOT_EN | STAT_CFG_STAT_EN;
		writeq(val64, &bar0->stat_cfg);
		do {
			udelay(100);
			val64 = readq(&bar0->stat_cfg);
			if (!(val64 & BIT(0)))
				break;
			cnt++;
			if (cnt == 5)
				break; /* Updt failed */
		} while(1);
	} 
}

/**
 *  s2io_get_stats - Updates the device statistics structure.
 *  @dev : pointer to the device structure.
 *  Description:
 *  This function updates the device statistics structure in the s2io_nic
 *  structure and returns a pointer to the same.
 *  Return value:
 *  pointer to the updated net_device_stats structure.
 */

static struct net_device_stats *s2io_get_stats(struct net_device *dev)
{
	struct s2io_nic *sp = dev->priv;
	struct mac_info *mac_control;
	struct config_param *config;


	mac_control = &sp->mac_control;
	config = &sp->config;

	/* Configure Stats for immediate updt */
	s2io_updt_stats(sp);

	sp->stats.tx_packets =
		le32_to_cpu(mac_control->stats_info->tmac_frms);
	sp->stats.tx_errors =
		le32_to_cpu(mac_control->stats_info->tmac_any_err_frms);
	sp->stats.rx_errors =
		le64_to_cpu(mac_control->stats_info->rmac_drop_frms);
	sp->stats.multicast =
		le32_to_cpu(mac_control->stats_info->rmac_vld_mcst_frms);
	sp->stats.rx_length_errors =
		le64_to_cpu(mac_control->stats_info->rmac_long_frms);

	return (&sp->stats);
}

/**
 *  s2io_set_multicast - entry point for multicast address enable/disable.
 *  @dev : pointer to the device structure
 *  Description:
 *  This function is a driver entry point which gets called by the kernel
 *  whenever multicast addresses must be enabled/disabled. This also gets
 *  called to set/reset promiscuous mode. Depending on the deivce flag, we
 *  determine, if multicast address must be enabled or if promiscuous mode
 *  is to be disabled etc.
 *  Return value:
 *  void.
 */

static void s2io_set_multicast(struct net_device *dev)
{
	int i, j, prev_cnt;
	struct dev_mc_list *mclist;
	struct s2io_nic *sp = dev->priv;
	struct XENA_dev_config __iomem *bar0 = sp->bar0;
	u64 val64 = 0, multi_mac = 0x010203040506ULL, mask =
	    0xfeffffffffffULL;
	u64 dis_addr = 0xffffffffffffULL, mac_addr = 0;
	void __iomem *add;

	if ((dev->flags & IFF_ALLMULTI) && (!sp->m_cast_flg)) {
		/*  Enable all Multicast addresses */
		writeq(RMAC_ADDR_DATA0_MEM_ADDR(multi_mac),
		       &bar0->rmac_addr_data0_mem);
		writeq(RMAC_ADDR_DATA1_MEM_MASK(mask),
		       &bar0->rmac_addr_data1_mem);
		val64 = RMAC_ADDR_CMD_MEM_WE |
		    RMAC_ADDR_CMD_MEM_STROBE_NEW_CMD |
		    RMAC_ADDR_CMD_MEM_OFFSET(MAC_MC_ALL_MC_ADDR_OFFSET);
		writeq(val64, &bar0->rmac_addr_cmd_mem);
		/* Wait till command completes */
		wait_for_cmd_complete(&bar0->rmac_addr_cmd_mem,
					RMAC_ADDR_CMD_MEM_STROBE_CMD_EXECUTING,
					S2IO_BIT_RESET);

		sp->m_cast_flg = 1;
		sp->all_multi_pos = MAC_MC_ALL_MC_ADDR_OFFSET;
	} else if ((dev->flags & IFF_ALLMULTI) && (sp->m_cast_flg)) {
		/*  Disable all Multicast addresses */
		writeq(RMAC_ADDR_DATA0_MEM_ADDR(dis_addr),
		       &bar0->rmac_addr_data0_mem);
		writeq(RMAC_ADDR_DATA1_MEM_MASK(0x0),
		       &bar0->rmac_addr_data1_mem);
		val64 = RMAC_ADDR_CMD_MEM_WE |
		    RMAC_ADDR_CMD_MEM_STROBE_NEW_CMD |
		    RMAC_ADDR_CMD_MEM_OFFSET(sp->all_multi_pos);
		writeq(val64, &bar0->rmac_addr_cmd_mem);
		/* Wait till command completes */
		wait_for_cmd_complete(&bar0->rmac_addr_cmd_mem,
					RMAC_ADDR_CMD_MEM_STROBE_CMD_EXECUTING,
					S2IO_BIT_RESET);

		sp->m_cast_flg = 0;
		sp->all_multi_pos = 0;
	}

	if ((dev->flags & IFF_PROMISC) && (!sp->promisc_flg)) {
		/*  Put the NIC into promiscuous mode */
		add = &bar0->mac_cfg;
		val64 = readq(&bar0->mac_cfg);
		val64 |= MAC_CFG_RMAC_PROM_ENABLE;

		writeq(RMAC_CFG_KEY(0x4C0D), &bar0->rmac_cfg_key);
		writel((u32) val64, add);
		writeq(RMAC_CFG_KEY(0x4C0D), &bar0->rmac_cfg_key);
		writel((u32) (val64 >> 32), (add + 4));

		if (vlan_tag_strip != 1) {
			val64 = readq(&bar0->rx_pa_cfg);
			val64 &= ~RX_PA_CFG_STRIP_VLAN_TAG;
			writeq(val64, &bar0->rx_pa_cfg);
			vlan_strip_flag = 0;
		}

		val64 = readq(&bar0->mac_cfg);
		sp->promisc_flg = 1;
		DBG_PRINT(INFO_DBG, "%s: entered promiscuous mode\n",
			  dev->name);
	} else if (!(dev->flags & IFF_PROMISC) && (sp->promisc_flg)) {
		/*  Remove the NIC from promiscuous mode */
		add = &bar0->mac_cfg;
		val64 = readq(&bar0->mac_cfg);
		val64 &= ~MAC_CFG_RMAC_PROM_ENABLE;

		writeq(RMAC_CFG_KEY(0x4C0D), &bar0->rmac_cfg_key);
		writel((u32) val64, add);
		writeq(RMAC_CFG_KEY(0x4C0D), &bar0->rmac_cfg_key);
		writel((u32) (val64 >> 32), (add + 4));

		if (vlan_tag_strip != 0) {
			val64 = readq(&bar0->rx_pa_cfg);
			val64 |= RX_PA_CFG_STRIP_VLAN_TAG;
			writeq(val64, &bar0->rx_pa_cfg);
			vlan_strip_flag = 1;
		}

		val64 = readq(&bar0->mac_cfg);
		sp->promisc_flg = 0;
		DBG_PRINT(INFO_DBG, "%s: left promiscuous mode\n",
			  dev->name);
	}

	/*  Update individual M_CAST address list */
	if ((!sp->m_cast_flg) && dev->mc_count) {
		if (dev->mc_count >
		    (MAX_ADDRS_SUPPORTED - MAC_MC_ADDR_START_OFFSET - 1)) {
			DBG_PRINT(ERR_DBG, "%s: No more Rx filters ",
				  dev->name);
			DBG_PRINT(ERR_DBG, "can be added, please enable ");
			DBG_PRINT(ERR_DBG, "ALL_MULTI instead\n");
			return;
		}

		prev_cnt = sp->mc_addr_count;
		sp->mc_addr_count = dev->mc_count;

		/* Clear out the previous list of Mc in the H/W. */
		for (i = 0; i < prev_cnt; i++) {
			writeq(RMAC_ADDR_DATA0_MEM_ADDR(dis_addr),
			       &bar0->rmac_addr_data0_mem);
			writeq(RMAC_ADDR_DATA1_MEM_MASK(0ULL),
				&bar0->rmac_addr_data1_mem);
			val64 = RMAC_ADDR_CMD_MEM_WE |
			    RMAC_ADDR_CMD_MEM_STROBE_NEW_CMD |
			    RMAC_ADDR_CMD_MEM_OFFSET
			    (MAC_MC_ADDR_START_OFFSET + i);
			writeq(val64, &bar0->rmac_addr_cmd_mem);

			/* Wait for command completes */
			if (wait_for_cmd_complete(&bar0->rmac_addr_cmd_mem,
					RMAC_ADDR_CMD_MEM_STROBE_CMD_EXECUTING,
					S2IO_BIT_RESET)) {
				DBG_PRINT(ERR_DBG, "%s: Adding ",
					  dev->name);
				DBG_PRINT(ERR_DBG, "Multicasts failed\n");
				return;
			}
		}

		/* Create the new Rx filter list and update the same in H/W. */
		for (i = 0, mclist = dev->mc_list; i < dev->mc_count;
		     i++, mclist = mclist->next) {
			memcpy(sp->usr_addrs[i].addr, mclist->dmi_addr,
			       ETH_ALEN);
			mac_addr = 0;
			for (j = 0; j < ETH_ALEN; j++) {
				mac_addr |= mclist->dmi_addr[j];
				mac_addr <<= 8;
			}
			mac_addr >>= 8;
			writeq(RMAC_ADDR_DATA0_MEM_ADDR(mac_addr),
			       &bar0->rmac_addr_data0_mem);
			writeq(RMAC_ADDR_DATA1_MEM_MASK(0ULL),
				&bar0->rmac_addr_data1_mem);
			val64 = RMAC_ADDR_CMD_MEM_WE |
			    RMAC_ADDR_CMD_MEM_STROBE_NEW_CMD |
			    RMAC_ADDR_CMD_MEM_OFFSET
			    (i + MAC_MC_ADDR_START_OFFSET);
			writeq(val64, &bar0->rmac_addr_cmd_mem);

			/* Wait for command completes */
			if (wait_for_cmd_complete(&bar0->rmac_addr_cmd_mem,
					RMAC_ADDR_CMD_MEM_STROBE_CMD_EXECUTING,
					S2IO_BIT_RESET)) {
				DBG_PRINT(ERR_DBG, "%s: Adding ",
					  dev->name);
				DBG_PRINT(ERR_DBG, "Multicasts failed\n");
				return;
			}
		}
	}
}

/**
 *  s2io_set_mac_addr - Programs the Xframe mac address
 *  @dev : pointer to the device structure.
 *  @addr: a uchar pointer to the new mac address which is to be set.
 *  Description : This procedure will program the Xframe to receive
 *  frames with new Mac Address
 *  Return value: SUCCESS on success and an appropriate (-)ve integer
 *  as defined in errno.h file on failure.
 */

static int s2io_set_mac_addr(struct net_device *dev, u8 * addr)
{
	struct s2io_nic *sp = dev->priv;
	struct XENA_dev_config __iomem *bar0 = sp->bar0;
	register u64 val64, mac_addr = 0;
	int i;
	u64 old_mac_addr = 0;

	/*
	 * Set the new MAC address as the new unicast filter and reflect this
	 * change on the device address registered with the OS. It will be
	 * at offset 0.
	 */
	for (i = 0; i < ETH_ALEN; i++) {
		mac_addr <<= 8;
		mac_addr |= addr[i];
		old_mac_addr <<= 8;
		old_mac_addr |= sp->def_mac_addr[0].mac_addr[i];
	}

	if(0 == mac_addr)
		return SUCCESS;

	/* Update the internal structure with this new mac address */
	if(mac_addr != old_mac_addr) {
		memset(sp->def_mac_addr[0].mac_addr, 0, sizeof(ETH_ALEN));
		sp->def_mac_addr[0].mac_addr[5] = (u8) (mac_addr);
		sp->def_mac_addr[0].mac_addr[4] = (u8) (mac_addr >> 8);
		sp->def_mac_addr[0].mac_addr[3] = (u8) (mac_addr >> 16);
		sp->def_mac_addr[0].mac_addr[2] = (u8) (mac_addr >> 24);
		sp->def_mac_addr[0].mac_addr[1] = (u8) (mac_addr >> 32);
		sp->def_mac_addr[0].mac_addr[0] = (u8) (mac_addr >> 40);
	}

	writeq(RMAC_ADDR_DATA0_MEM_ADDR(mac_addr),
	       &bar0->rmac_addr_data0_mem);

	val64 =
	    RMAC_ADDR_CMD_MEM_WE | RMAC_ADDR_CMD_MEM_STROBE_NEW_CMD |
	    RMAC_ADDR_CMD_MEM_OFFSET(0);
	writeq(val64, &bar0->rmac_addr_cmd_mem);
	/* Wait till command completes */
	if (wait_for_cmd_complete(&bar0->rmac_addr_cmd_mem,
		      RMAC_ADDR_CMD_MEM_STROBE_CMD_EXECUTING, S2IO_BIT_RESET)) {
		DBG_PRINT(ERR_DBG, "%s: set_mac_addr failed\n", dev->name);
		return FAILURE;
	}

	return SUCCESS;
}

/**
 * s2io_ethtool_sset - Sets different link parameters.
 * @sp : private member of the device structure, which is a pointer to the  * s2io_nic structure.
 * @info: pointer to the structure with parameters given by ethtool to set
 * link information.
 * Description:
 * The function sets different link parameters provided by the user onto
 * the NIC.
 * Return value:
 * 0 on success.
*/

static int s2io_ethtool_sset(struct net_device *dev,
			     struct ethtool_cmd *info)
{
	struct s2io_nic *sp = dev->priv;
	if ((info->autoneg == AUTONEG_ENABLE) ||
	    (info->speed != SPEED_10000) || (info->duplex != DUPLEX_FULL))
		return -EINVAL;
	else {
		s2io_close(sp->dev);
		s2io_open(sp->dev);
	}

	return 0;
}

/**
 * s2io_ethtol_gset - Return link specific information.
 * @sp : private member of the device structure, pointer to the
 *      s2io_nic structure.
 * @info : pointer to the structure with parameters given by ethtool
 * to return link information.
 * Description:
 * Returns link specific information like speed, duplex etc.. to ethtool.
 * Return value :
 * return 0 on success.
 */

static int s2io_ethtool_gset(struct net_device *dev, struct ethtool_cmd *info)
{
	struct s2io_nic *sp = dev->priv;
	info->supported = (SUPPORTED_10000baseT_Full | SUPPORTED_FIBRE);
	info->advertising = (SUPPORTED_10000baseT_Full | SUPPORTED_FIBRE);
	info->port = PORT_FIBRE;
	/* info->transceiver?? TODO */

	if (netif_carrier_ok(sp->dev)) {
		info->speed = 10000;
		info->duplex = DUPLEX_FULL;
	} else {
		info->speed = -1;
		info->duplex = -1;
	}

	info->autoneg = AUTONEG_DISABLE;
	return 0;
}

/**
 * s2io_ethtool_gdrvinfo - Returns driver specific information.
 * @sp : private member of the device structure, which is a pointer to the
 * s2io_nic structure.
 * @info : pointer to the structure with parameters given by ethtool to
 * return driver information.
 * Description:
 * Returns driver specefic information like name, version etc.. to ethtool.
 * Return value:
 *  void
 */

static void s2io_ethtool_gdrvinfo(struct net_device *dev,
				  struct ethtool_drvinfo *info)
{
	struct s2io_nic *sp = dev->priv;

	strncpy(info->driver, s2io_driver_name, sizeof(info->driver));
	strncpy(info->version, s2io_driver_version, sizeof(info->version));
	strncpy(info->fw_version, "", sizeof(info->fw_version));
	strncpy(info->bus_info, pci_name(sp->pdev), sizeof(info->bus_info));
	info->regdump_len = XENA_REG_SPACE;
	info->eedump_len = XENA_EEPROM_SPACE;
	info->testinfo_len = S2IO_TEST_LEN;

	if (sp->device_type == XFRAME_I_DEVICE)
		info->n_stats = XFRAME_I_STAT_LEN;
	else
		info->n_stats = XFRAME_II_STAT_LEN;
}

/**
 *  s2io_ethtool_gregs - dumps the entire space of Xfame into the buffer.
 *  @sp: private member of the device structure, which is a pointer to the
 *  s2io_nic structure.
 *  @regs : pointer to the structure with parameters given by ethtool for
 *  dumping the registers.
 *  @reg_space: The input argumnet into which all the registers are dumped.
 *  Description:
 *  Dumps the entire register space of xFrame NIC into the user given
 *  buffer area.
 * Return value :
 * void .
*/

static void s2io_ethtool_gregs(struct net_device *dev,
			       struct ethtool_regs *regs, void *space)
{
	int i;
	u64 reg;
	u8 *reg_space = (u8 *) space;
	struct s2io_nic *sp = dev->priv;

	regs->len = XENA_REG_SPACE;
	regs->version = sp->pdev->subsystem_device;

	for (i = 0; i < regs->len; i += 8) {
		reg = readq(sp->bar0 + i);
		memcpy((reg_space + i), &reg, 8);
	}
}

/**
 *  s2io_phy_id  - timer function that alternates adapter LED.
 *  @data : address of the private member of the device structure, which
 *  is a pointer to the s2io_nic structure, provided as an u32.
 * Description: This is actually the timer function that alternates the
 * adapter LED bit of the adapter control bit to set/reset every time on
 * invocation. The timer is set for 1/2 a second, hence tha NIC blinks
 *  once every second.
*/
static void s2io_phy_id(unsigned long data)
{
	struct s2io_nic *sp = (struct s2io_nic *) data;
	struct XENA_dev_config __iomem *bar0 = sp->bar0;
	u64 val64 = 0;
	u16 subid;

	subid = sp->pdev->subsystem_device;
	if ((sp->device_type == XFRAME_II_DEVICE) ||
		   ((subid & 0xFF) >= 0x07)) {
		val64 = readq(&bar0->gpio_control);
		val64 ^= GPIO_CTRL_GPIO_0;
		writeq(val64, &bar0->gpio_control);
	} else {
		val64 = readq(&bar0->adapter_control);
		val64 ^= ADAPTER_LED_ON;
		writeq(val64, &bar0->adapter_control);
	}

	mod_timer(&sp->id_timer, jiffies + HZ / 2);
}

/**
 * s2io_ethtool_idnic - To physically identify the nic on the system.
 * @sp : private member of the device structure, which is a pointer to the
 * s2io_nic structure.
 * @id : pointer to the structure with identification parameters given by
 * ethtool.
 * Description: Used to physically identify the NIC on the system.
 * The Link LED will blink for a time specified by the user for
 * identification.
 * NOTE: The Link has to be Up to be able to blink the LED. Hence
 * identification is possible only if it's link is up.
 * Return value:
 * int , returns 0 on success
 */

static int s2io_ethtool_idnic(struct net_device *dev, u32 data)
{
	u64 val64 = 0, last_gpio_ctrl_val;
	struct s2io_nic *sp = dev->priv;
	struct XENA_dev_config __iomem *bar0 = sp->bar0;
	u16 subid;

	subid = sp->pdev->subsystem_device;
	last_gpio_ctrl_val = readq(&bar0->gpio_control);
	if ((sp->device_type == XFRAME_I_DEVICE) &&
		((subid & 0xFF) < 0x07)) {
		val64 = readq(&bar0->adapter_control);
		if (!(val64 & ADAPTER_CNTL_EN)) {
			printk(KERN_ERR
			       "Adapter Link down, cannot blink LED\n");
			return -EFAULT;
		}
	}
	if (sp->id_timer.function == NULL) {
		init_timer(&sp->id_timer);
		sp->id_timer.function = s2io_phy_id;
		sp->id_timer.data = (unsigned long) sp;
	}
	mod_timer(&sp->id_timer, jiffies);
	if (data)
		msleep_interruptible(data * HZ);
	else
		msleep_interruptible(MAX_FLICKER_TIME);
	del_timer_sync(&sp->id_timer);

	if (CARDS_WITH_FAULTY_LINK_INDICATORS(sp->device_type, subid)) {
		writeq(last_gpio_ctrl_val, &bar0->gpio_control);
		last_gpio_ctrl_val = readq(&bar0->gpio_control);
	}

	return 0;
}

static void s2io_ethtool_gringparam(struct net_device *dev,
                                    struct ethtool_ringparam *ering)
{
	struct s2io_nic *sp = dev->priv;
	int i,tx_desc_count=0,rx_desc_count=0;

	if (sp->rxd_mode == RXD_MODE_1)
		ering->rx_max_pending = MAX_RX_DESC_1;
	else if (sp->rxd_mode == RXD_MODE_3B)
		ering->rx_max_pending = MAX_RX_DESC_2;
	else if (sp->rxd_mode == RXD_MODE_3A)
		ering->rx_max_pending = MAX_RX_DESC_3;

	ering->tx_max_pending = MAX_TX_DESC;
	for (i = 0 ; i < sp->config.tx_fifo_num ; i++) {
		tx_desc_count += sp->config.tx_cfg[i].fifo_len;
	}
	DBG_PRINT(INFO_DBG,"\nmax txds : %d\n",sp->config.max_txds);
	ering->tx_pending = tx_desc_count;
	rx_desc_count = 0;
	for (i = 0 ; i < sp->config.rx_ring_num ; i++) {
		rx_desc_count += sp->config.rx_cfg[i].num_rxd;
	}
	ering->rx_pending = rx_desc_count;

	ering->rx_mini_max_pending = 0;
	ering->rx_mini_pending = 0;
	if(sp->rxd_mode == RXD_MODE_1)
		ering->rx_jumbo_max_pending = MAX_RX_DESC_1;
	else if (sp->rxd_mode == RXD_MODE_3B)
		ering->rx_jumbo_max_pending = MAX_RX_DESC_2;
	ering->rx_jumbo_pending = rx_desc_count;
}

/**
 * s2io_ethtool_getpause_data -Pause frame frame generation and reception.
 * @sp : private member of the device structure, which is a pointer to the
 *	s2io_nic structure.
 * @ep : pointer to the structure with pause parameters given by ethtool.
 * Description:
 * Returns the Pause frame generation and reception capability of the NIC.
 * Return value:
 *  void
 */
static void s2io_ethtool_getpause_data(struct net_device *dev,
				       struct ethtool_pauseparam *ep)
{
	u64 val64;
	struct s2io_nic *sp = dev->priv;
	struct XENA_dev_config __iomem *bar0 = sp->bar0;

	val64 = readq(&bar0->rmac_pause_cfg);
	if (val64 & RMAC_PAUSE_GEN_ENABLE)
		ep->tx_pause = TRUE;
	if (val64 & RMAC_PAUSE_RX_ENABLE)
		ep->rx_pause = TRUE;
	ep->autoneg = FALSE;
}

/**
 * s2io_ethtool_setpause_data -  set/reset pause frame generation.
 * @sp : private member of the device structure, which is a pointer to the
 *      s2io_nic structure.
 * @ep : pointer to the structure with pause parameters given by ethtool.
 * Description:
 * It can be used to set or reset Pause frame generation or reception
 * support of the NIC.
 * Return value:
 * int, returns 0 on Success
 */

static int s2io_ethtool_setpause_data(struct net_device *dev,
			       struct ethtool_pauseparam *ep)
{
	u64 val64;
	struct s2io_nic *sp = dev->priv;
	struct XENA_dev_config __iomem *bar0 = sp->bar0;

	val64 = readq(&bar0->rmac_pause_cfg);
	if (ep->tx_pause)
		val64 |= RMAC_PAUSE_GEN_ENABLE;
	else
		val64 &= ~RMAC_PAUSE_GEN_ENABLE;
	if (ep->rx_pause)
		val64 |= RMAC_PAUSE_RX_ENABLE;
	else
		val64 &= ~RMAC_PAUSE_RX_ENABLE;
	writeq(val64, &bar0->rmac_pause_cfg);
	return 0;
}

/**
 * read_eeprom - reads 4 bytes of data from user given offset.
 * @sp : private member of the device structure, which is a pointer to the
 *      s2io_nic structure.
 * @off : offset at which the data must be written
 * @data : Its an output parameter where the data read at the given
 *	offset is stored.
 * Description:
 * Will read 4 bytes of data from the user given offset and return the
 * read data.
 * NOTE: Will allow to read only part of the EEPROM visible through the
 *   I2C bus.
 * Return value:
 *  -1 on failure and 0 on success.
 */

#define S2IO_DEV_ID		5
static int read_eeprom(struct s2io_nic * sp, int off, u64 * data)
{
	int ret = -1;
	u32 exit_cnt = 0;
	u64 val64;
	struct XENA_dev_config __iomem *bar0 = sp->bar0;

	if (sp->device_type == XFRAME_I_DEVICE) {
		val64 = I2C_CONTROL_DEV_ID(S2IO_DEV_ID) | I2C_CONTROL_ADDR(off) |
		    I2C_CONTROL_BYTE_CNT(0x3) | I2C_CONTROL_READ |
		    I2C_CONTROL_CNTL_START;
		SPECIAL_REG_WRITE(val64, &bar0->i2c_control, LF);

		while (exit_cnt < 5) {
			val64 = readq(&bar0->i2c_control);
			if (I2C_CONTROL_CNTL_END(val64)) {
				*data = I2C_CONTROL_GET_DATA(val64);
				ret = 0;
				break;
			}
			msleep(50);
			exit_cnt++;
		}
	}

	if (sp->device_type == XFRAME_II_DEVICE) {
		val64 = SPI_CONTROL_KEY(0x9) | SPI_CONTROL_SEL1 |
			SPI_CONTROL_BYTECNT(0x3) |
			SPI_CONTROL_CMD(0x3) | SPI_CONTROL_ADDR(off);
		SPECIAL_REG_WRITE(val64, &bar0->spi_control, LF);
		val64 |= SPI_CONTROL_REQ;
		SPECIAL_REG_WRITE(val64, &bar0->spi_control, LF);
		while (exit_cnt < 5) {
			val64 = readq(&bar0->spi_control);
			if (val64 & SPI_CONTROL_NACK) {
				ret = 1;
				break;
			} else if (val64 & SPI_CONTROL_DONE) {
				*data = readq(&bar0->spi_data);
				*data &= 0xffffff;
				ret = 0;
				break;
			}
			msleep(50);
			exit_cnt++;
		}
	}
	return ret;
}

/**
 *  write_eeprom - actually writes the relevant part of the data value.
 *  @sp : private member of the device structure, which is a pointer to the
 *       s2io_nic structure.
 *  @off : offset at which the data must be written
 *  @data : The data that is to be written
 *  @cnt : Number of bytes of the data that are actually to be written into
 *  the Eeprom. (max of 3)
 * Description:
 *  Actually writes the relevant part of the data value into the Eeprom
 *  through the I2C bus.
 * Return value:
 *  0 on success, -1 on failure.
 */

static int write_eeprom(struct s2io_nic * sp, int off, u64 data, int cnt)
{
	int exit_cnt = 0, ret = -1;
	u64 val64;
	struct XENA_dev_config __iomem *bar0 = sp->bar0;

	if (sp->device_type == XFRAME_I_DEVICE) {
		val64 = I2C_CONTROL_DEV_ID(S2IO_DEV_ID) | I2C_CONTROL_ADDR(off) |
		    I2C_CONTROL_BYTE_CNT(cnt) | I2C_CONTROL_SET_DATA((u32)data) |
		    I2C_CONTROL_CNTL_START;
		SPECIAL_REG_WRITE(val64, &bar0->i2c_control, LF);

		while (exit_cnt < 5) {
			val64 = readq(&bar0->i2c_control);
			if (I2C_CONTROL_CNTL_END(val64)) {
				if (!(val64 & I2C_CONTROL_NACK))
					ret = 0;
				break;
			}
			msleep(50);
			exit_cnt++;
		}
	}

	if (sp->device_type == XFRAME_II_DEVICE) {
		int write_cnt = (cnt == 8) ? 0 : cnt;
		writeq(SPI_DATA_WRITE(data,(cnt<<3)), &bar0->spi_data);

		val64 = SPI_CONTROL_KEY(0x9) | SPI_CONTROL_SEL1 |
			SPI_CONTROL_BYTECNT(write_cnt) |
			SPI_CONTROL_CMD(0x2) | SPI_CONTROL_ADDR(off);
		SPECIAL_REG_WRITE(val64, &bar0->spi_control, LF);
		val64 |= SPI_CONTROL_REQ;
		SPECIAL_REG_WRITE(val64, &bar0->spi_control, LF);
		while (exit_cnt < 5) {
			val64 = readq(&bar0->spi_control);
			if (val64 & SPI_CONTROL_NACK) {
				ret = 1;
				break;
			} else if (val64 & SPI_CONTROL_DONE) {
				ret = 0;
				break;
			}
			msleep(50);
			exit_cnt++;
		}
	}
	return ret;
}
static void s2io_vpd_read(struct s2io_nic *nic)
{
	u8 *vpd_data;
	u8 data;
	int i=0, cnt, fail = 0;
	int vpd_addr = 0x80;

	if (nic->device_type == XFRAME_II_DEVICE) {
		strcpy(nic->product_name, "Xframe II 10GbE network adapter");
		vpd_addr = 0x80;
	}
	else {
		strcpy(nic->product_name, "Xframe I 10GbE network adapter");
		vpd_addr = 0x50;
	}
	strcpy(nic->serial_num, "NOT AVAILABLE");

	vpd_data = kmalloc(256, GFP_KERNEL);
	if (!vpd_data) {
		nic->mac_control.stats_info->sw_stat.mem_alloc_fail_cnt++;
		return;
	}
	nic->mac_control.stats_info->sw_stat.mem_allocated += 256;

	for (i = 0; i < 256; i +=4 ) {
		pci_write_config_byte(nic->pdev, (vpd_addr + 2), i);
		pci_read_config_byte(nic->pdev,  (vpd_addr + 2), &data);
		pci_write_config_byte(nic->pdev, (vpd_addr + 3), 0);
		for (cnt = 0; cnt <5; cnt++) {
			msleep(2);
			pci_read_config_byte(nic->pdev, (vpd_addr + 3), &data);
			if (data == 0x80)
				break;
		}
		if (cnt >= 5) {
			DBG_PRINT(ERR_DBG, "Read of VPD data failed\n");
			fail = 1;
			break;
		}
		pci_read_config_dword(nic->pdev,  (vpd_addr + 4),
				      (u32 *)&vpd_data[i]);
	}

	if(!fail) {
		/* read serial number of adapter */
		for (cnt = 0; cnt < 256; cnt++) {
		if ((vpd_data[cnt] == 'S') &&
			(vpd_data[cnt+1] == 'N') &&
			(vpd_data[cnt+2] < VPD_STRING_LEN)) {
				memset(nic->serial_num, 0, VPD_STRING_LEN);
				memcpy(nic->serial_num, &vpd_data[cnt + 3],
					vpd_data[cnt+2]);
				break;
			}
		}
	}

	if ((!fail) && (vpd_data[1] < VPD_STRING_LEN)) {
		memset(nic->product_name, 0, vpd_data[1]);
		memcpy(nic->product_name, &vpd_data[3], vpd_data[1]);
	}
	kfree(vpd_data);
	nic->mac_control.stats_info->sw_stat.mem_freed += 256;
}

/**
 *  s2io_ethtool_geeprom  - reads the value stored in the Eeprom.
 *  @sp : private member of the device structure, which is a pointer to the *       s2io_nic structure.
 *  @eeprom : pointer to the user level structure provided by ethtool,
 *  containing all relevant information.
 *  @data_buf : user defined value to be written into Eeprom.
 *  Description: Reads the values stored in the Eeprom at given offset
 *  for a given length. Stores these values int the input argument data
 *  buffer 'data_buf' and returns these to the caller (ethtool.)
 *  Return value:
 *  int  0 on success
 */

static int s2io_ethtool_geeprom(struct net_device *dev,
			 struct ethtool_eeprom *eeprom, u8 * data_buf)
{
	u32 i, valid;
	u64 data;
	struct s2io_nic *sp = dev->priv;

	eeprom->magic = sp->pdev->vendor | (sp->pdev->device << 16);

	if ((eeprom->offset + eeprom->len) > (XENA_EEPROM_SPACE))
		eeprom->len = XENA_EEPROM_SPACE - eeprom->offset;

	for (i = 0; i < eeprom->len; i += 4) {
		if (read_eeprom(sp, (eeprom->offset + i), &data)) {
			DBG_PRINT(ERR_DBG, "Read of EEPROM failed\n");
			return -EFAULT;
		}
		valid = INV(data);
		memcpy((data_buf + i), &valid, 4);
	}
	return 0;
}

/**
 *  s2io_ethtool_seeprom - tries to write the user provided value in Eeprom
 *  @sp : private member of the device structure, which is a pointer to the
 *  s2io_nic structure.
 *  @eeprom : pointer to the user level structure provided by ethtool,
 *  containing all relevant information.
 *  @data_buf ; user defined value to be written into Eeprom.
 *  Description:
 *  Tries to write the user provided value in the Eeprom, at the offset
 *  given by the user.
 *  Return value:
 *  0 on success, -EFAULT on failure.
 */

static int s2io_ethtool_seeprom(struct net_device *dev,
				struct ethtool_eeprom *eeprom,
				u8 * data_buf)
{
	int len = eeprom->len, cnt = 0;
	u64 valid = 0, data;
	struct s2io_nic *sp = dev->priv;

	if (eeprom->magic != (sp->pdev->vendor | (sp->pdev->device << 16))) {
		DBG_PRINT(ERR_DBG,
			  "ETHTOOL_WRITE_EEPROM Err: Magic value ");
		DBG_PRINT(ERR_DBG, "is wrong, Its not 0x%x\n",
			  eeprom->magic);
		return -EFAULT;
	}

	while (len) {
		data = (u32) data_buf[cnt] & 0x000000FF;
		if (data) {
			valid = (u32) (data << 24);
		} else
			valid = data;

		if (write_eeprom(sp, (eeprom->offset + cnt), valid, 0)) {
			DBG_PRINT(ERR_DBG,
				  "ETHTOOL_WRITE_EEPROM Err: Cannot ");
			DBG_PRINT(ERR_DBG,
				  "write into the specified offset\n");
			return -EFAULT;
		}
		cnt++;
		len--;
	}

	return 0;
}

/**
 * s2io_register_test - reads and writes into all clock domains.
 * @sp : private member of the device structure, which is a pointer to the
 * s2io_nic structure.
 * @data : variable that returns the result of each of the test conducted b
 * by the driver.
 * Description:
 * Read and write into all clock domains. The NIC has 3 clock domains,
 * see that registers in all the three regions are accessible.
 * Return value:
 * 0 on success.
 */

static int s2io_register_test(struct s2io_nic * sp, uint64_t * data)
{
	struct XENA_dev_config __iomem *bar0 = sp->bar0;
	u64 val64 = 0, exp_val;
	int fail = 0;

	val64 = readq(&bar0->pif_rd_swapper_fb);
	if (val64 != 0x123456789abcdefULL) {
		fail = 1;
		DBG_PRINT(INFO_DBG, "Read Test level 1 fails\n");
	}

	val64 = readq(&bar0->rmac_pause_cfg);
	if (val64 != 0xc000ffff00000000ULL) {
		fail = 1;
		DBG_PRINT(INFO_DBG, "Read Test level 2 fails\n");
	}

	val64 = readq(&bar0->rx_queue_cfg);
	if (sp->device_type == XFRAME_II_DEVICE)
		exp_val = 0x0404040404040404ULL;
	else
		exp_val = 0x0808080808080808ULL;
	if (val64 != exp_val) {
		fail = 1;
		DBG_PRINT(INFO_DBG, "Read Test level 3 fails\n");
	}

	val64 = readq(&bar0->xgxs_efifo_cfg);
	if (val64 != 0x000000001923141EULL) {
		fail = 1;
		DBG_PRINT(INFO_DBG, "Read Test level 4 fails\n");
	}

	val64 = 0x5A5A5A5A5A5A5A5AULL;
	writeq(val64, &bar0->xmsi_data);
	val64 = readq(&bar0->xmsi_data);
	if (val64 != 0x5A5A5A5A5A5A5A5AULL) {
		fail = 1;
		DBG_PRINT(ERR_DBG, "Write Test level 1 fails\n");
	}

	val64 = 0xA5A5A5A5A5A5A5A5ULL;
	writeq(val64, &bar0->xmsi_data);
	val64 = readq(&bar0->xmsi_data);
	if (val64 != 0xA5A5A5A5A5A5A5A5ULL) {
		fail = 1;
		DBG_PRINT(ERR_DBG, "Write Test level 2 fails\n");
	}

	*data = fail;
	return fail;
}

/**
 * s2io_eeprom_test - to verify that EEprom in the xena can be programmed.
 * @sp : private member of the device structure, which is a pointer to the
 * s2io_nic structure.
 * @data:variable that returns the result of each of the test conducted by
 * the driver.
 * Description:
 * Verify that EEPROM in the xena can be programmed using I2C_CONTROL
 * register.
 * Return value:
 * 0 on success.
 */

static int s2io_eeprom_test(struct s2io_nic * sp, uint64_t * data)
{
	int fail = 0;
	u64 ret_data, org_4F0, org_7F0;
	u8 saved_4F0 = 0, saved_7F0 = 0;
	struct net_device *dev = sp->dev;

	/* Test Write Error at offset 0 */
	/* Note that SPI interface allows write access to all areas
	 * of EEPROM. Hence doing all negative testing only for Xframe I.
	 */
	if (sp->device_type == XFRAME_I_DEVICE)
		if (!write_eeprom(sp, 0, 0, 3))
			fail = 1;

	/* Save current values at offsets 0x4F0 and 0x7F0 */
	if (!read_eeprom(sp, 0x4F0, &org_4F0))
		saved_4F0 = 1;
	if (!read_eeprom(sp, 0x7F0, &org_7F0))
		saved_7F0 = 1;

	/* Test Write at offset 4f0 */
	if (write_eeprom(sp, 0x4F0, 0x012345, 3))
		fail = 1;
	if (read_eeprom(sp, 0x4F0, &ret_data))
		fail = 1;

	if (ret_data != 0x012345) {
		DBG_PRINT(ERR_DBG, "%s: eeprom test error at offset 0x4F0. "
			"Data written %llx Data read %llx\n",
			dev->name, (unsigned long long)0x12345,
			(unsigned long long)ret_data);
		fail = 1;
	}

	/* Reset the EEPROM data go FFFF */
	write_eeprom(sp, 0x4F0, 0xFFFFFF, 3);

	/* Test Write Request Error at offset 0x7c */
	if (sp->device_type == XFRAME_I_DEVICE)
		if (!write_eeprom(sp, 0x07C, 0, 3))
			fail = 1;

	/* Test Write Request at offset 0x7f0 */
	if (write_eeprom(sp, 0x7F0, 0x012345, 3))
		fail = 1;
	if (read_eeprom(sp, 0x7F0, &ret_data))
		fail = 1;

	if (ret_data != 0x012345) {
		DBG_PRINT(ERR_DBG, "%s: eeprom test error at offset 0x7F0. "
			"Data written %llx Data read %llx\n",
			dev->name, (unsigned long long)0x12345,
			(unsigned long long)ret_data);
		fail = 1;
	}

	/* Reset the EEPROM data go FFFF */
	write_eeprom(sp, 0x7F0, 0xFFFFFF, 3);

	if (sp->device_type == XFRAME_I_DEVICE) {
		/* Test Write Error at offset 0x80 */
		if (!write_eeprom(sp, 0x080, 0, 3))
			fail = 1;

		/* Test Write Error at offset 0xfc */
		if (!write_eeprom(sp, 0x0FC, 0, 3))
			fail = 1;

		/* Test Write Error at offset 0x100 */
		if (!write_eeprom(sp, 0x100, 0, 3))
			fail = 1;

		/* Test Write Error at offset 4ec */
		if (!write_eeprom(sp, 0x4EC, 0, 3))
			fail = 1;
	}

	/* Restore values at offsets 0x4F0 and 0x7F0 */
	if (saved_4F0)
		write_eeprom(sp, 0x4F0, org_4F0, 3);
	if (saved_7F0)
		write_eeprom(sp, 0x7F0, org_7F0, 3);

	*data = fail;
	return fail;
}

/**
 * s2io_bist_test - invokes the MemBist test of the card .
 * @sp : private member of the device structure, which is a pointer to the
 * s2io_nic structure.
 * @data:variable that returns the result of each of the test conducted by
 * the driver.
 * Description:
 * This invokes the MemBist test of the card. We give around
 * 2 secs time for the Test to complete. If it's still not complete
 * within this peiod, we consider that the test failed.
 * Return value:
 * 0 on success and -1 on failure.
 */

static int s2io_bist_test(struct s2io_nic * sp, uint64_t * data)
{
	u8 bist = 0;
	int cnt = 0, ret = -1;

	pci_read_config_byte(sp->pdev, PCI_BIST, &bist);
	bist |= PCI_BIST_START;
	pci_write_config_word(sp->pdev, PCI_BIST, bist);

	while (cnt < 20) {
		pci_read_config_byte(sp->pdev, PCI_BIST, &bist);
		if (!(bist & PCI_BIST_START)) {
			*data = (bist & PCI_BIST_CODE_MASK);
			ret = 0;
			break;
		}
		msleep(100);
		cnt++;
	}

	return ret;
}

/**
 * s2io-link_test - verifies the link state of the nic
 * @sp ; private member of the device structure, which is a pointer to the
 * s2io_nic structure.
 * @data: variable that returns the result of each of the test conducted by
 * the driver.
 * Description:
 * The function verifies the link state of the NIC and updates the input
 * argument 'data' appropriately.
 * Return value:
 * 0 on success.
 */

static int s2io_link_test(struct s2io_nic * sp, uint64_t * data)
{
	struct XENA_dev_config __iomem *bar0 = sp->bar0;
	u64 val64;

	val64 = readq(&bar0->adapter_status);
	if(!(LINK_IS_UP(val64)))
		*data = 1;
	else
		*data = 0;

	return *data;
}

/**
 * s2io_rldram_test - offline test for access to the RldRam chip on the NIC
 * @sp - private member of the device structure, which is a pointer to the
 * s2io_nic structure.
 * @data - variable that returns the result of each of the test
 * conducted by the driver.
 * Description:
 *  This is one of the offline test that tests the read and write
 *  access to the RldRam chip on the NIC.
 * Return value:
 *  0 on success.
 */

static int s2io_rldram_test(struct s2io_nic * sp, uint64_t * data)
{
	struct XENA_dev_config __iomem *bar0 = sp->bar0;
	u64 val64;
	int cnt, iteration = 0, test_fail = 0;

	val64 = readq(&bar0->adapter_control);
	val64 &= ~ADAPTER_ECC_EN;
	writeq(val64, &bar0->adapter_control);

	val64 = readq(&bar0->mc_rldram_test_ctrl);
	val64 |= MC_RLDRAM_TEST_MODE;
	SPECIAL_REG_WRITE(val64, &bar0->mc_rldram_test_ctrl, LF);

	val64 = readq(&bar0->mc_rldram_mrs);
	val64 |= MC_RLDRAM_QUEUE_SIZE_ENABLE;
	SPECIAL_REG_WRITE(val64, &bar0->mc_rldram_mrs, UF);

	val64 |= MC_RLDRAM_MRS_ENABLE;
	SPECIAL_REG_WRITE(val64, &bar0->mc_rldram_mrs, UF);

	while (iteration < 2) {
		val64 = 0x55555555aaaa0000ULL;
		if (iteration == 1) {
			val64 ^= 0xFFFFFFFFFFFF0000ULL;
		}
		writeq(val64, &bar0->mc_rldram_test_d0);

		val64 = 0xaaaa5a5555550000ULL;
		if (iteration == 1) {
			val64 ^= 0xFFFFFFFFFFFF0000ULL;
		}
		writeq(val64, &bar0->mc_rldram_test_d1);

		val64 = 0x55aaaaaaaa5a0000ULL;
		if (iteration == 1) {
			val64 ^= 0xFFFFFFFFFFFF0000ULL;
		}
		writeq(val64, &bar0->mc_rldram_test_d2);

		val64 = (u64) (0x0000003ffffe0100ULL);
		writeq(val64, &bar0->mc_rldram_test_add);

		val64 = MC_RLDRAM_TEST_MODE | MC_RLDRAM_TEST_WRITE |
		    	MC_RLDRAM_TEST_GO;
		SPECIAL_REG_WRITE(val64, &bar0->mc_rldram_test_ctrl, LF);

		for (cnt = 0; cnt < 5; cnt++) {
			val64 = readq(&bar0->mc_rldram_test_ctrl);
			if (val64 & MC_RLDRAM_TEST_DONE)
				break;
			msleep(200);
		}

		if (cnt == 5)
			break;

		val64 = MC_RLDRAM_TEST_MODE | MC_RLDRAM_TEST_GO;
		SPECIAL_REG_WRITE(val64, &bar0->mc_rldram_test_ctrl, LF);

		for (cnt = 0; cnt < 5; cnt++) {
			val64 = readq(&bar0->mc_rldram_test_ctrl);
			if (val64 & MC_RLDRAM_TEST_DONE)
				break;
			msleep(500);
		}

		if (cnt == 5)
			break;

		val64 = readq(&bar0->mc_rldram_test_ctrl);
		if (!(val64 & MC_RLDRAM_TEST_PASS))
			test_fail = 1;

		iteration++;
	}

	*data = test_fail;

	/* Bring the adapter out of test mode */
	SPECIAL_REG_WRITE(0, &bar0->mc_rldram_test_ctrl, LF);

	return test_fail;
}

/**
 *  s2io_ethtool_test - conducts 6 tsets to determine the health of card.
 *  @sp : private member of the device structure, which is a pointer to the
 *  s2io_nic structure.
 *  @ethtest : pointer to a ethtool command specific structure that will be
 *  returned to the user.
 *  @data : variable that returns the result of each of the test
 * conducted by the driver.
 * Description:
 *  This function conducts 6 tests ( 4 offline and 2 online) to determine
 *  the health of the card.
 * Return value:
 *  void
 */

static void s2io_ethtool_test(struct net_device *dev,
			      struct ethtool_test *ethtest,
			      uint64_t * data)
{
	struct s2io_nic *sp = dev->priv;
	int orig_state = netif_running(sp->dev);

	if (ethtest->flags == ETH_TEST_FL_OFFLINE) {
		/* Offline Tests. */
		if (orig_state)
			s2io_close(sp->dev);

		if (s2io_register_test(sp, &data[0]))
			ethtest->flags |= ETH_TEST_FL_FAILED;

		s2io_reset(sp);

		if (s2io_rldram_test(sp, &data[3]))
			ethtest->flags |= ETH_TEST_FL_FAILED;

		s2io_reset(sp);

		if (s2io_eeprom_test(sp, &data[1]))
			ethtest->flags |= ETH_TEST_FL_FAILED;

		if (s2io_bist_test(sp, &data[4]))
			ethtest->flags |= ETH_TEST_FL_FAILED;

		if (orig_state)
			s2io_open(sp->dev);

		data[2] = 0;
	} else {
		/* Online Tests. */
		if (!orig_state) {
			DBG_PRINT(ERR_DBG,
				  "%s: is not up, cannot run test\n",
				  dev->name);
			data[0] = -1;
			data[1] = -1;
			data[2] = -1;
			data[3] = -1;
			data[4] = -1;
		}

		if (s2io_link_test(sp, &data[2]))
			ethtest->flags |= ETH_TEST_FL_FAILED;

		data[0] = 0;
		data[1] = 0;
		data[3] = 0;
		data[4] = 0;
	}
}

static void s2io_get_ethtool_stats(struct net_device *dev,
				   struct ethtool_stats *estats,
				   u64 * tmp_stats)
{
	int i = 0;
	struct s2io_nic *sp = dev->priv;
	struct stat_block *stat_info = sp->mac_control.stats_info;

	s2io_updt_stats(sp);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stat_info->tmac_frms_oflow) << 32  |
		le32_to_cpu(stat_info->tmac_frms);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stat_info->tmac_data_octets_oflow) << 32 |
		le32_to_cpu(stat_info->tmac_data_octets);
	tmp_stats[i++] = le64_to_cpu(stat_info->tmac_drop_frms);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stat_info->tmac_mcst_frms_oflow) << 32 |
		le32_to_cpu(stat_info->tmac_mcst_frms);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stat_info->tmac_bcst_frms_oflow) << 32 |
		le32_to_cpu(stat_info->tmac_bcst_frms);
	tmp_stats[i++] = le64_to_cpu(stat_info->tmac_pause_ctrl_frms);
        tmp_stats[i++] =
                (u64)le32_to_cpu(stat_info->tmac_ttl_octets_oflow) << 32 |
                le32_to_cpu(stat_info->tmac_ttl_octets);
	tmp_stats[i++] =
                (u64)le32_to_cpu(stat_info->tmac_ucst_frms_oflow) << 32 |
                le32_to_cpu(stat_info->tmac_ucst_frms);
	tmp_stats[i++] =
                (u64)le32_to_cpu(stat_info->tmac_nucst_frms_oflow) << 32 |
                le32_to_cpu(stat_info->tmac_nucst_frms);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stat_info->tmac_any_err_frms_oflow) << 32 |
		le32_to_cpu(stat_info->tmac_any_err_frms);
        tmp_stats[i++] = le64_to_cpu(stat_info->tmac_ttl_less_fb_octets);
	tmp_stats[i++] = le64_to_cpu(stat_info->tmac_vld_ip_octets);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stat_info->tmac_vld_ip_oflow) << 32 |
		le32_to_cpu(stat_info->tmac_vld_ip);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stat_info->tmac_drop_ip_oflow) << 32 |
		le32_to_cpu(stat_info->tmac_drop_ip);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stat_info->tmac_icmp_oflow) << 32 |
		le32_to_cpu(stat_info->tmac_icmp);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stat_info->tmac_rst_tcp_oflow) << 32 |
		le32_to_cpu(stat_info->tmac_rst_tcp);
	tmp_stats[i++] = le64_to_cpu(stat_info->tmac_tcp);
	tmp_stats[i++] = (u64)le32_to_cpu(stat_info->tmac_udp_oflow) << 32 |
		le32_to_cpu(stat_info->tmac_udp);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stat_info->rmac_vld_frms_oflow) << 32 |
		le32_to_cpu(stat_info->rmac_vld_frms);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stat_info->rmac_data_octets_oflow) << 32 |
		le32_to_cpu(stat_info->rmac_data_octets);
	tmp_stats[i++] = le64_to_cpu(stat_info->rmac_fcs_err_frms);
	tmp_stats[i++] = le64_to_cpu(stat_info->rmac_drop_frms);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stat_info->rmac_vld_mcst_frms_oflow) << 32 |
		le32_to_cpu(stat_info->rmac_vld_mcst_frms);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stat_info->rmac_vld_bcst_frms_oflow) << 32 |
		le32_to_cpu(stat_info->rmac_vld_bcst_frms);
	tmp_stats[i++] = le32_to_cpu(stat_info->rmac_in_rng_len_err_frms);
	tmp_stats[i++] = le32_to_cpu(stat_info->rmac_out_rng_len_err_frms);
	tmp_stats[i++] = le64_to_cpu(stat_info->rmac_long_frms);
	tmp_stats[i++] = le64_to_cpu(stat_info->rmac_pause_ctrl_frms);
	tmp_stats[i++] = le64_to_cpu(stat_info->rmac_unsup_ctrl_frms);
        tmp_stats[i++] =
                (u64)le32_to_cpu(stat_info->rmac_ttl_octets_oflow) << 32 |
		le32_to_cpu(stat_info->rmac_ttl_octets);
        tmp_stats[i++] =
                (u64)le32_to_cpu(stat_info->rmac_accepted_ucst_frms_oflow)
		<< 32 | le32_to_cpu(stat_info->rmac_accepted_ucst_frms);
	tmp_stats[i++] =
                (u64)le32_to_cpu(stat_info->rmac_accepted_nucst_frms_oflow)
                 << 32 | le32_to_cpu(stat_info->rmac_accepted_nucst_frms);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stat_info->rmac_discarded_frms_oflow) << 32 |
		le32_to_cpu(stat_info->rmac_discarded_frms);
        tmp_stats[i++] =
                (u64)le32_to_cpu(stat_info->rmac_drop_events_oflow)
                 << 32 | le32_to_cpu(stat_info->rmac_drop_events);
        tmp_stats[i++] = le64_to_cpu(stat_info->rmac_ttl_less_fb_octets);
        tmp_stats[i++] = le64_to_cpu(stat_info->rmac_ttl_frms);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stat_info->rmac_usized_frms_oflow) << 32 |
		le32_to_cpu(stat_info->rmac_usized_frms);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stat_info->rmac_osized_frms_oflow) << 32 |
		le32_to_cpu(stat_info->rmac_osized_frms);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stat_info->rmac_frag_frms_oflow) << 32 |
		le32_to_cpu(stat_info->rmac_frag_frms);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stat_info->rmac_jabber_frms_oflow) << 32 |
		le32_to_cpu(stat_info->rmac_jabber_frms);
	tmp_stats[i++] = le64_to_cpu(stat_info->rmac_ttl_64_frms);
        tmp_stats[i++] = le64_to_cpu(stat_info->rmac_ttl_65_127_frms);
        tmp_stats[i++] = le64_to_cpu(stat_info->rmac_ttl_128_255_frms);
        tmp_stats[i++] = le64_to_cpu(stat_info->rmac_ttl_256_511_frms);
        tmp_stats[i++] = le64_to_cpu(stat_info->rmac_ttl_512_1023_frms);
        tmp_stats[i++] = le64_to_cpu(stat_info->rmac_ttl_1024_1518_frms);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stat_info->rmac_ip_oflow) << 32 |
		le32_to_cpu(stat_info->rmac_ip);
	tmp_stats[i++] = le64_to_cpu(stat_info->rmac_ip_octets);
	tmp_stats[i++] = le32_to_cpu(stat_info->rmac_hdr_err_ip);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stat_info->rmac_drop_ip_oflow) << 32 |
		le32_to_cpu(stat_info->rmac_drop_ip);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stat_info->rmac_icmp_oflow) << 32 |
		le32_to_cpu(stat_info->rmac_icmp);
	tmp_stats[i++] = le64_to_cpu(stat_info->rmac_tcp);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stat_info->rmac_udp_oflow) << 32 |
		le32_to_cpu(stat_info->rmac_udp);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stat_info->rmac_err_drp_udp_oflow) << 32 |
		le32_to_cpu(stat_info->rmac_err_drp_udp);
	tmp_stats[i++] = le64_to_cpu(stat_info->rmac_xgmii_err_sym);
        tmp_stats[i++] = le64_to_cpu(stat_info->rmac_frms_q0);
        tmp_stats[i++] = le64_to_cpu(stat_info->rmac_frms_q1);
        tmp_stats[i++] = le64_to_cpu(stat_info->rmac_frms_q2);
        tmp_stats[i++] = le64_to_cpu(stat_info->rmac_frms_q3);
        tmp_stats[i++] = le64_to_cpu(stat_info->rmac_frms_q4);
        tmp_stats[i++] = le64_to_cpu(stat_info->rmac_frms_q5);
        tmp_stats[i++] = le64_to_cpu(stat_info->rmac_frms_q6);
        tmp_stats[i++] = le64_to_cpu(stat_info->rmac_frms_q7);
        tmp_stats[i++] = le16_to_cpu(stat_info->rmac_full_q0);
        tmp_stats[i++] = le16_to_cpu(stat_info->rmac_full_q1);
        tmp_stats[i++] = le16_to_cpu(stat_info->rmac_full_q2);
        tmp_stats[i++] = le16_to_cpu(stat_info->rmac_full_q3);
        tmp_stats[i++] = le16_to_cpu(stat_info->rmac_full_q4);
        tmp_stats[i++] = le16_to_cpu(stat_info->rmac_full_q5);
        tmp_stats[i++] = le16_to_cpu(stat_info->rmac_full_q6);
        tmp_stats[i++] = le16_to_cpu(stat_info->rmac_full_q7);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stat_info->rmac_pause_cnt_oflow) << 32 |
		le32_to_cpu(stat_info->rmac_pause_cnt);
	tmp_stats[i++] = le64_to_cpu(stat_info->rmac_xgmii_data_err_cnt);
        tmp_stats[i++] = le64_to_cpu(stat_info->rmac_xgmii_ctrl_err_cnt);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stat_info->rmac_accepted_ip_oflow) << 32 |
		le32_to_cpu(stat_info->rmac_accepted_ip);
	tmp_stats[i++] = le32_to_cpu(stat_info->rmac_err_tcp);
	tmp_stats[i++] = le32_to_cpu(stat_info->rd_req_cnt);
	tmp_stats[i++] = le32_to_cpu(stat_info->new_rd_req_cnt);
	tmp_stats[i++] = le32_to_cpu(stat_info->new_rd_req_rtry_cnt);
	tmp_stats[i++] = le32_to_cpu(stat_info->rd_rtry_cnt);
	tmp_stats[i++] = le32_to_cpu(stat_info->wr_rtry_rd_ack_cnt);
	tmp_stats[i++] = le32_to_cpu(stat_info->wr_req_cnt);
	tmp_stats[i++] = le32_to_cpu(stat_info->new_wr_req_cnt);
	tmp_stats[i++] = le32_to_cpu(stat_info->new_wr_req_rtry_cnt);
	tmp_stats[i++] = le32_to_cpu(stat_info->wr_rtry_cnt);
	tmp_stats[i++] = le32_to_cpu(stat_info->wr_disc_cnt);
	tmp_stats[i++] = le32_to_cpu(stat_info->rd_rtry_wr_ack_cnt);
	tmp_stats[i++] = le32_to_cpu(stat_info->txp_wr_cnt);
	tmp_stats[i++] = le32_to_cpu(stat_info->txd_rd_cnt);
	tmp_stats[i++] = le32_to_cpu(stat_info->txd_wr_cnt);
	tmp_stats[i++] = le32_to_cpu(stat_info->rxd_rd_cnt);
	tmp_stats[i++] = le32_to_cpu(stat_info->rxd_wr_cnt);
	tmp_stats[i++] = le32_to_cpu(stat_info->txf_rd_cnt);
	tmp_stats[i++] = le32_to_cpu(stat_info->rxf_wr_cnt);

	/* Enhanced statistics exist only for Hercules */
	if(sp->device_type == XFRAME_II_DEVICE) {
		tmp_stats[i++] =
				le64_to_cpu(stat_info->rmac_ttl_1519_4095_frms);
		tmp_stats[i++] =
				le64_to_cpu(stat_info->rmac_ttl_4096_8191_frms);
		tmp_stats[i++] =
				le64_to_cpu(stat_info->rmac_ttl_8192_max_frms);
		tmp_stats[i++] = le64_to_cpu(stat_info->rmac_ttl_gt_max_frms);
		tmp_stats[i++] = le64_to_cpu(stat_info->rmac_osized_alt_frms);
		tmp_stats[i++] = le64_to_cpu(stat_info->rmac_jabber_alt_frms);
		tmp_stats[i++] = le64_to_cpu(stat_info->rmac_gt_max_alt_frms);
		tmp_stats[i++] = le64_to_cpu(stat_info->rmac_vlan_frms);
		tmp_stats[i++] = le32_to_cpu(stat_info->rmac_len_discard);
		tmp_stats[i++] = le32_to_cpu(stat_info->rmac_fcs_discard);
		tmp_stats[i++] = le32_to_cpu(stat_info->rmac_pf_discard);
		tmp_stats[i++] = le32_to_cpu(stat_info->rmac_da_discard);
		tmp_stats[i++] = le32_to_cpu(stat_info->rmac_red_discard);
		tmp_stats[i++] = le32_to_cpu(stat_info->rmac_rts_discard);
		tmp_stats[i++] = le32_to_cpu(stat_info->rmac_ingm_full_discard);
		tmp_stats[i++] = le32_to_cpu(stat_info->link_fault_cnt);
	}

	tmp_stats[i++] = 0;
	tmp_stats[i++] = stat_info->sw_stat.single_ecc_errs;
	tmp_stats[i++] = stat_info->sw_stat.double_ecc_errs;
	tmp_stats[i++] = stat_info->sw_stat.parity_err_cnt;
	tmp_stats[i++] = stat_info->sw_stat.serious_err_cnt;
	tmp_stats[i++] = stat_info->sw_stat.soft_reset_cnt;
	tmp_stats[i++] = stat_info->sw_stat.fifo_full_cnt;
	tmp_stats[i++] = stat_info->sw_stat.ring_full_cnt;
	tmp_stats[i++] = stat_info->xpak_stat.alarm_transceiver_temp_high;
	tmp_stats[i++] = stat_info->xpak_stat.alarm_transceiver_temp_low;
	tmp_stats[i++] = stat_info->xpak_stat.alarm_laser_bias_current_high;
	tmp_stats[i++] = stat_info->xpak_stat.alarm_laser_bias_current_low;
	tmp_stats[i++] = stat_info->xpak_stat.alarm_laser_output_power_high;
	tmp_stats[i++] = stat_info->xpak_stat.alarm_laser_output_power_low;
	tmp_stats[i++] = stat_info->xpak_stat.warn_transceiver_temp_high;
	tmp_stats[i++] = stat_info->xpak_stat.warn_transceiver_temp_low;
	tmp_stats[i++] = stat_info->xpak_stat.warn_laser_bias_current_high;
	tmp_stats[i++] = stat_info->xpak_stat.warn_laser_bias_current_low;
	tmp_stats[i++] = stat_info->xpak_stat.warn_laser_output_power_high;
	tmp_stats[i++] = stat_info->xpak_stat.warn_laser_output_power_low;
	tmp_stats[i++] = stat_info->sw_stat.clubbed_frms_cnt;
	tmp_stats[i++] = stat_info->sw_stat.sending_both;
	tmp_stats[i++] = stat_info->sw_stat.outof_sequence_pkts;
	tmp_stats[i++] = stat_info->sw_stat.flush_max_pkts;
	if (stat_info->sw_stat.num_aggregations) {
		u64 tmp = stat_info->sw_stat.sum_avg_pkts_aggregated;
		int count = 0;
		/*
		 * Since 64-bit divide does not work on all platforms,
		 * do repeated subtraction.
		 */
		while (tmp >= stat_info->sw_stat.num_aggregations) {
			tmp -= stat_info->sw_stat.num_aggregations;
			count++;
		}
		tmp_stats[i++] = count;
	}
	else
		tmp_stats[i++] = 0;
	tmp_stats[i++] = stat_info->sw_stat.mem_alloc_fail_cnt;
	tmp_stats[i++] = stat_info->sw_stat.watchdog_timer_cnt;
	tmp_stats[i++] = stat_info->sw_stat.mem_allocated;
	tmp_stats[i++] = stat_info->sw_stat.mem_freed;
	tmp_stats[i++] = stat_info->sw_stat.link_up_cnt;
	tmp_stats[i++] = stat_info->sw_stat.link_down_cnt;
	tmp_stats[i++] = stat_info->sw_stat.link_up_time;
	tmp_stats[i++] = stat_info->sw_stat.link_down_time;

	tmp_stats[i++] = stat_info->sw_stat.tx_buf_abort_cnt;
	tmp_stats[i++] = stat_info->sw_stat.tx_desc_abort_cnt;
	tmp_stats[i++] = stat_info->sw_stat.tx_parity_err_cnt;
	tmp_stats[i++] = stat_info->sw_stat.tx_link_loss_cnt;
	tmp_stats[i++] = stat_info->sw_stat.tx_list_proc_err_cnt;

	tmp_stats[i++] = stat_info->sw_stat.rx_parity_err_cnt;
	tmp_stats[i++] = stat_info->sw_stat.rx_abort_cnt;
	tmp_stats[i++] = stat_info->sw_stat.rx_parity_abort_cnt;
	tmp_stats[i++] = stat_info->sw_stat.rx_rda_fail_cnt;
	tmp_stats[i++] = stat_info->sw_stat.rx_unkn_prot_cnt;
	tmp_stats[i++] = stat_info->sw_stat.rx_fcs_err_cnt;
	tmp_stats[i++] = stat_info->sw_stat.rx_buf_size_err_cnt;
	tmp_stats[i++] = stat_info->sw_stat.rx_rxd_corrupt_cnt;
	tmp_stats[i++] = stat_info->sw_stat.rx_unkn_err_cnt;
}

static int s2io_ethtool_get_regs_len(struct net_device *dev)
{
	return (XENA_REG_SPACE);
}


static u32 s2io_ethtool_get_rx_csum(struct net_device * dev)
{
	struct s2io_nic *sp = dev->priv;

	return (sp->rx_csum);
}

static int s2io_ethtool_set_rx_csum(struct net_device *dev, u32 data)
{
	struct s2io_nic *sp = dev->priv;

	if (data)
		sp->rx_csum = 1;
	else
		sp->rx_csum = 0;

	return 0;
}

static int s2io_get_eeprom_len(struct net_device *dev)
{
	return (XENA_EEPROM_SPACE);
}

static int s2io_ethtool_self_test_count(struct net_device *dev)
{
	return (S2IO_TEST_LEN);
}

static void s2io_ethtool_get_strings(struct net_device *dev,
				     u32 stringset, u8 * data)
{
	int stat_size = 0;
	struct s2io_nic *sp = dev->priv;

	switch (stringset) {
	case ETH_SS_TEST:
		memcpy(data, s2io_gstrings, S2IO_STRINGS_LEN);
		break;
	case ETH_SS_STATS:
		stat_size = sizeof(ethtool_xena_stats_keys);
		memcpy(data, &ethtool_xena_stats_keys,stat_size);
		if(sp->device_type == XFRAME_II_DEVICE) {
			memcpy(data + stat_size,
				&ethtool_enhanced_stats_keys,
				sizeof(ethtool_enhanced_stats_keys));
			stat_size += sizeof(ethtool_enhanced_stats_keys);
		}

		memcpy(data + stat_size, &ethtool_driver_stats_keys,
			sizeof(ethtool_driver_stats_keys));
	}
}
static int s2io_ethtool_get_stats_count(struct net_device *dev)
{
	struct s2io_nic *sp = dev->priv;
	int stat_count = 0;
	switch(sp->device_type) {
	case XFRAME_I_DEVICE:
		stat_count = XFRAME_I_STAT_LEN;
	break;

	case XFRAME_II_DEVICE:
		stat_count = XFRAME_II_STAT_LEN;
	break;
	}

	return stat_count;
}

static int s2io_ethtool_op_set_tx_csum(struct net_device *dev, u32 data)
{
	if (data)
		dev->features |= NETIF_F_IP_CSUM;
	else
		dev->features &= ~NETIF_F_IP_CSUM;

	return 0;
}

static u32 s2io_ethtool_op_get_tso(struct net_device *dev)
{
	return (dev->features & NETIF_F_TSO) != 0;
}
static int s2io_ethtool_op_set_tso(struct net_device *dev, u32 data)
{
	if (data)
		dev->features |= (NETIF_F_TSO | NETIF_F_TSO6);
	else
		dev->features &= ~(NETIF_F_TSO | NETIF_F_TSO6);

	return 0;
}

static const struct ethtool_ops netdev_ethtool_ops = {
	.get_settings = s2io_ethtool_gset,
	.set_settings = s2io_ethtool_sset,
	.get_drvinfo = s2io_ethtool_gdrvinfo,
	.get_regs_len = s2io_ethtool_get_regs_len,
	.get_regs = s2io_ethtool_gregs,
	.get_link = ethtool_op_get_link,
	.get_eeprom_len = s2io_get_eeprom_len,
	.get_eeprom = s2io_ethtool_geeprom,
	.set_eeprom = s2io_ethtool_seeprom,
	.get_ringparam = s2io_ethtool_gringparam,
	.get_pauseparam = s2io_ethtool_getpause_data,
	.set_pauseparam = s2io_ethtool_setpause_data,
	.get_rx_csum = s2io_ethtool_get_rx_csum,
	.set_rx_csum = s2io_ethtool_set_rx_csum,
	.get_tx_csum = ethtool_op_get_tx_csum,
	.set_tx_csum = s2io_ethtool_op_set_tx_csum,
	.get_sg = ethtool_op_get_sg,
	.set_sg = ethtool_op_set_sg,
	.get_tso = s2io_ethtool_op_get_tso,
	.set_tso = s2io_ethtool_op_set_tso,
	.get_ufo = ethtool_op_get_ufo,
	.set_ufo = ethtool_op_set_ufo,
	.self_test_count = s2io_ethtool_self_test_count,
	.self_test = s2io_ethtool_test,
	.get_strings = s2io_ethtool_get_strings,
	.phys_id = s2io_ethtool_idnic,
	.get_stats_count = s2io_ethtool_get_stats_count,
	.get_ethtool_stats = s2io_get_ethtool_stats
};

/**
 *  s2io_ioctl - Entry point for the Ioctl
 *  @dev :  Device pointer.
 *  @ifr :  An IOCTL specefic structure, that can contain a pointer to
 *  a proprietary structure used to pass information to the driver.
 *  @cmd :  This is used to distinguish between the different commands that
 *  can be passed to the IOCTL functions.
 *  Description:
 *  Currently there are no special functionality supported in IOCTL, hence
 *  function always return EOPNOTSUPPORTED
 */

static int s2io_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	return -EOPNOTSUPP;
}

/**
 *  s2io_change_mtu - entry point to change MTU size for the device.
 *   @dev : device pointer.
 *   @new_mtu : the new MTU size for the device.
 *   Description: A driver entry point to change MTU size for the device.
 *   Before changing the MTU the device must be stopped.
 *  Return value:
 *   0 on success and an appropriate (-)ve integer as defined in errno.h
 *   file on failure.
 */

static int s2io_change_mtu(struct net_device *dev, int new_mtu)
{
	struct s2io_nic *sp = dev->priv;

	if ((new_mtu < MIN_MTU) || (new_mtu > S2IO_JUMBO_SIZE)) {
		DBG_PRINT(ERR_DBG, "%s: MTU size is invalid.\n",
			  dev->name);
		return -EPERM;
	}

	dev->mtu = new_mtu;
	if (netif_running(dev)) {
		s2io_card_down(sp);
		netif_stop_queue(dev);
		if (s2io_card_up(sp)) {
			DBG_PRINT(ERR_DBG, "%s: Device bring up failed\n",
				  __FUNCTION__);
		}
		if (netif_queue_stopped(dev))
			netif_wake_queue(dev);
	} else { /* Device is down */
		struct XENA_dev_config __iomem *bar0 = sp->bar0;
		u64 val64 = new_mtu;

		writeq(vBIT(val64, 2, 14), &bar0->rmac_max_pyld_len);
	}

	return 0;
}

/**
 *  s2io_tasklet - Bottom half of the ISR.
 *  @dev_adr : address of the device structure in dma_addr_t format.
 *  Description:
 *  This is the tasklet or the bottom half of the ISR. This is
 *  an extension of the ISR which is scheduled by the scheduler to be run
 *  when the load on the CPU is low. All low priority tasks of the ISR can
 *  be pushed into the tasklet. For now the tasklet is used only to
 *  replenish the Rx buffers in the Rx buffer descriptors.
 *  Return value:
 *  void.
 */

static void s2io_tasklet(unsigned long dev_addr)
{
	struct net_device *dev = (struct net_device *) dev_addr;
	struct s2io_nic *sp = dev->priv;
	int i, ret;
	struct mac_info *mac_control;
	struct config_param *config;

	mac_control = &sp->mac_control;
	config = &sp->config;

	if (!TASKLET_IN_USE) {
		for (i = 0; i < config->rx_ring_num; i++) {
			ret = fill_rx_buffers(sp, i);
			if (ret == -ENOMEM) {
				DBG_PRINT(INFO_DBG, "%s: Out of ",
					  dev->name);
				DBG_PRINT(INFO_DBG, "memory in tasklet\n");
				break;
			} else if (ret == -EFILL) {
				DBG_PRINT(INFO_DBG,
					  "%s: Rx Ring %d is full\n",
					  dev->name, i);
				break;
			}
		}
		clear_bit(0, (&sp->tasklet_status));
	}
}

/**
 * s2io_set_link - Set the LInk status
 * @data: long pointer to device private structue
 * Description: Sets the link status for the adapter
 */

static void s2io_set_link(struct work_struct *work)
{
	struct s2io_nic *nic = container_of(work, struct s2io_nic, set_link_task);
	struct net_device *dev = nic->dev;
	struct XENA_dev_config __iomem *bar0 = nic->bar0;
	register u64 val64;
	u16 subid;

	rtnl_lock();

	if (!netif_running(dev))
		goto out_unlock;

	if (test_and_set_bit(0, &(nic->link_state))) {
		/* The card is being reset, no point doing anything */
		goto out_unlock;
	}

	subid = nic->pdev->subsystem_device;
	if (s2io_link_fault_indication(nic) == MAC_RMAC_ERR_TIMER) {
		/*
		 * Allow a small delay for the NICs self initiated
		 * cleanup to complete.
		 */
		msleep(100);
	}

	val64 = readq(&bar0->adapter_status);
	if (LINK_IS_UP(val64)) {
		if (!(readq(&bar0->adapter_control) & ADAPTER_CNTL_EN)) {
			if (verify_xena_quiescence(nic)) {
				val64 = readq(&bar0->adapter_control);
				val64 |= ADAPTER_CNTL_EN;
				writeq(val64, &bar0->adapter_control);
				if (CARDS_WITH_FAULTY_LINK_INDICATORS(
					nic->device_type, subid)) {
					val64 = readq(&bar0->gpio_control);
					val64 |= GPIO_CTRL_GPIO_0;
					writeq(val64, &bar0->gpio_control);
					val64 = readq(&bar0->gpio_control);
				} else {
					val64 |= ADAPTER_LED_ON;
					writeq(val64, &bar0->adapter_control);
				}
				nic->device_enabled_once = TRUE;
			} else {
				DBG_PRINT(ERR_DBG, "%s: Error: ", dev->name);
				DBG_PRINT(ERR_DBG, "device is not Quiescent\n");
				netif_stop_queue(dev);
			}
		}
		val64 = readq(&bar0->adapter_status);
		if (!LINK_IS_UP(val64)) {
			DBG_PRINT(ERR_DBG, "%s:", dev->name);
			DBG_PRINT(ERR_DBG, " Link down after enabling ");
			DBG_PRINT(ERR_DBG, "device \n");
		} else
			s2io_link(nic, LINK_UP);
	} else {
		if (CARDS_WITH_FAULTY_LINK_INDICATORS(nic->device_type,
						      subid)) {
			val64 = readq(&bar0->gpio_control);
			val64 &= ~GPIO_CTRL_GPIO_0;
			writeq(val64, &bar0->gpio_control);
			val64 = readq(&bar0->gpio_control);
		}
		s2io_link(nic, LINK_DOWN);
	}
	clear_bit(0, &(nic->link_state));

out_unlock:
	rtnl_unlock();
}

static int set_rxd_buffer_pointer(struct s2io_nic *sp, struct RxD_t *rxdp,
				struct buffAdd *ba,
				struct sk_buff **skb, u64 *temp0, u64 *temp1,
				u64 *temp2, int size)
{
	struct net_device *dev = sp->dev;
	struct sk_buff *frag_list;

	if ((sp->rxd_mode == RXD_MODE_1) && (rxdp->Host_Control == 0)) {
		/* allocate skb */
		if (*skb) {
			DBG_PRINT(INFO_DBG, "SKB is not NULL\n");
			/*
			 * As Rx frame are not going to be processed,
			 * using same mapped address for the Rxd
			 * buffer pointer
			 */
			((struct RxD1*)rxdp)->Buffer0_ptr = *temp0;
		} else {
			*skb = dev_alloc_skb(size);
			if (!(*skb)) {
				DBG_PRINT(INFO_DBG, "%s: Out of ", dev->name);
				DBG_PRINT(INFO_DBG, "memory to allocate ");
				DBG_PRINT(INFO_DBG, "1 buf mode SKBs\n");
				sp->mac_control.stats_info->sw_stat. \
					mem_alloc_fail_cnt++;
				return -ENOMEM ;
			}
			sp->mac_control.stats_info->sw_stat.mem_allocated 
				+= (*skb)->truesize;
			/* storing the mapped addr in a temp variable
			 * such it will be used for next rxd whose
			 * Host Control is NULL
			 */
			((struct RxD1*)rxdp)->Buffer0_ptr = *temp0 =
				pci_map_single( sp->pdev, (*skb)->data,
					size - NET_IP_ALIGN,
					PCI_DMA_FROMDEVICE);
			rxdp->Host_Control = (unsigned long) (*skb);
		}
	} else if ((sp->rxd_mode == RXD_MODE_3B) && (rxdp->Host_Control == 0)) {
		/* Two buffer Mode */
		if (*skb) {
			((struct RxD3*)rxdp)->Buffer2_ptr = *temp2;
			((struct RxD3*)rxdp)->Buffer0_ptr = *temp0;
			((struct RxD3*)rxdp)->Buffer1_ptr = *temp1;
		} else {
			*skb = dev_alloc_skb(size);
			if (!(*skb)) {
				DBG_PRINT(INFO_DBG, "%s: Out of ", dev->name);
				DBG_PRINT(INFO_DBG, "memory to allocate ");
				DBG_PRINT(INFO_DBG, "2 buf mode SKBs\n");
				sp->mac_control.stats_info->sw_stat. \
					mem_alloc_fail_cnt++;
				return -ENOMEM;
			}
			sp->mac_control.stats_info->sw_stat.mem_allocated 
				+= (*skb)->truesize;
			((struct RxD3*)rxdp)->Buffer2_ptr = *temp2 =
				pci_map_single(sp->pdev, (*skb)->data,
					       dev->mtu + 4,
					       PCI_DMA_FROMDEVICE);
			((struct RxD3*)rxdp)->Buffer0_ptr = *temp0 =
				pci_map_single( sp->pdev, ba->ba_0, BUF0_LEN,
						PCI_DMA_FROMDEVICE);
			rxdp->Host_Control = (unsigned long) (*skb);

			/* Buffer-1 will be dummy buffer not used */
			((struct RxD3*)rxdp)->Buffer1_ptr = *temp1 =
				pci_map_single(sp->pdev, ba->ba_1, BUF1_LEN,
					       PCI_DMA_FROMDEVICE);
		}
	} else if ((rxdp->Host_Control == 0)) {
		/* Three buffer mode */
		if (*skb) {
			((struct RxD3*)rxdp)->Buffer0_ptr = *temp0;
			((struct RxD3*)rxdp)->Buffer1_ptr = *temp1;
			((struct RxD3*)rxdp)->Buffer2_ptr = *temp2;
		} else {
			*skb = dev_alloc_skb(size);
			if (!(*skb)) {
				DBG_PRINT(INFO_DBG, "%s: Out of ", dev->name);
				DBG_PRINT(INFO_DBG, "memory to allocate ");
				DBG_PRINT(INFO_DBG, "3 buf mode SKBs\n");
				sp->mac_control.stats_info->sw_stat. \
					mem_alloc_fail_cnt++;
				return -ENOMEM;
			}
			sp->mac_control.stats_info->sw_stat.mem_allocated 
				+= (*skb)->truesize;
			((struct RxD3*)rxdp)->Buffer0_ptr = *temp0 =
				pci_map_single(sp->pdev, ba->ba_0, BUF0_LEN,
					       PCI_DMA_FROMDEVICE);
			/* Buffer-1 receives L3/L4 headers */
			((struct RxD3*)rxdp)->Buffer1_ptr = *temp1 =
				pci_map_single( sp->pdev, (*skb)->data,
						l3l4hdr_size + 4,
						PCI_DMA_FROMDEVICE);
			/*
			 * skb_shinfo(skb)->frag_list will have L4
			 * data payload
			 */
			skb_shinfo(*skb)->frag_list = dev_alloc_skb(dev->mtu +
								   ALIGN_SIZE);
			if (skb_shinfo(*skb)->frag_list == NULL) {
				DBG_PRINT(ERR_DBG, "%s: dev_alloc_skb \
					  failed\n ", dev->name);
				sp->mac_control.stats_info->sw_stat. \
					mem_alloc_fail_cnt++;
				return -ENOMEM ;
			}
			frag_list = skb_shinfo(*skb)->frag_list;
			frag_list->next = NULL;
			sp->mac_control.stats_info->sw_stat.mem_allocated 
				+= frag_list->truesize;
			/*
			 * Buffer-2 receives L4 data payload
			 */
			((struct RxD3*)rxdp)->Buffer2_ptr = *temp2 =
				pci_map_single( sp->pdev, frag_list->data,
						dev->mtu, PCI_DMA_FROMDEVICE);
		}
	}
	return 0;
}
static void set_rxd_buffer_size(struct s2io_nic *sp, struct RxD_t *rxdp,
				int size)
{
	struct net_device *dev = sp->dev;
	if (sp->rxd_mode == RXD_MODE_1) {
		rxdp->Control_2 = SET_BUFFER0_SIZE_1( size - NET_IP_ALIGN);
	} else if (sp->rxd_mode == RXD_MODE_3B) {
		rxdp->Control_2 = SET_BUFFER0_SIZE_3(BUF0_LEN);
		rxdp->Control_2 |= SET_BUFFER1_SIZE_3(1);
		rxdp->Control_2 |= SET_BUFFER2_SIZE_3( dev->mtu + 4);
	} else {
		rxdp->Control_2 = SET_BUFFER0_SIZE_3(BUF0_LEN);
		rxdp->Control_2 |= SET_BUFFER1_SIZE_3(l3l4hdr_size + 4);
		rxdp->Control_2 |= SET_BUFFER2_SIZE_3(dev->mtu);
	}
}

static  int rxd_owner_bit_reset(struct s2io_nic *sp)
{
	int i, j, k, blk_cnt = 0, size;
	struct mac_info * mac_control = &sp->mac_control;
	struct config_param *config = &sp->config;
	struct net_device *dev = sp->dev;
	struct RxD_t *rxdp = NULL;
	struct sk_buff *skb = NULL;
	struct buffAdd *ba = NULL;
	u64 temp0_64 = 0, temp1_64 = 0, temp2_64 = 0;

	/* Calculate the size based on ring mode */
	size = dev->mtu + HEADER_ETHERNET_II_802_3_SIZE +
		HEADER_802_2_SIZE + HEADER_SNAP_SIZE;
	if (sp->rxd_mode == RXD_MODE_1)
		size += NET_IP_ALIGN;
	else if (sp->rxd_mode == RXD_MODE_3B)
		size = dev->mtu + ALIGN_SIZE + BUF0_LEN + 4;
	else
		size = l3l4hdr_size + ALIGN_SIZE + BUF0_LEN + 4;

	for (i = 0; i < config->rx_ring_num; i++) {
		blk_cnt = config->rx_cfg[i].num_rxd /
			(rxd_count[sp->rxd_mode] +1);

		for (j = 0; j < blk_cnt; j++) {
			for (k = 0; k < rxd_count[sp->rxd_mode]; k++) {
				rxdp = mac_control->rings[i].
					rx_blocks[j].rxds[k].virt_addr;
				if(sp->rxd_mode >= RXD_MODE_3A)
					ba = &mac_control->rings[i].ba[j][k];
				if (set_rxd_buffer_pointer(sp, rxdp, ba,
						       &skb,(u64 *)&temp0_64,
						       (u64 *)&temp1_64,
						       (u64 *)&temp2_64,
							size) == ENOMEM) {
					return 0;
				}

				set_rxd_buffer_size(sp, rxdp, size);
				wmb();
				/* flip the Ownership bit to Hardware */
				rxdp->Control_1 |= RXD_OWN_XENA;
			}
		}
	}
	return 0;

}

static int s2io_add_isr(struct s2io_nic * sp)
{
	int ret = 0;
	struct net_device *dev = sp->dev;
	int err = 0;

	if (sp->intr_type == MSI)
		ret = s2io_enable_msi(sp);
	else if (sp->intr_type == MSI_X)
		ret = s2io_enable_msi_x(sp);
	if (ret) {
		DBG_PRINT(ERR_DBG, "%s: Defaulting to INTA\n", dev->name);
		sp->intr_type = INTA;
	}

	/* Store the values of the MSIX table in the struct s2io_nic structure */
	store_xmsi_data(sp);

	/* After proper initialization of H/W, register ISR */
	if (sp->intr_type == MSI) {
		err = request_irq((int) sp->pdev->irq, s2io_msi_handle,
			IRQF_SHARED, sp->name, dev);
		if (err) {
			pci_disable_msi(sp->pdev);
			DBG_PRINT(ERR_DBG, "%s: MSI registration failed\n",
				  dev->name);
			return -1;
		}
	}
	if (sp->intr_type == MSI_X) {
		int i, msix_tx_cnt=0,msix_rx_cnt=0;

		for (i=1; (sp->s2io_entries[i].in_use == MSIX_FLG); i++) {
			if (sp->s2io_entries[i].type == MSIX_FIFO_TYPE) {
				sprintf(sp->desc[i], "%s:MSI-X-%d-TX",
					dev->name, i);
				err = request_irq(sp->entries[i].vector,
					  s2io_msix_fifo_handle, 0, sp->desc[i],
						  sp->s2io_entries[i].arg);
				/* If either data or addr is zero print it */
				if(!(sp->msix_info[i].addr &&
					sp->msix_info[i].data)) {
					DBG_PRINT(ERR_DBG, "%s @ Addr:0x%llx"
						"Data:0x%lx\n",sp->desc[i],
						(unsigned long long)
						sp->msix_info[i].addr,
						(unsigned long)
						ntohl(sp->msix_info[i].data));
				} else {
					msix_tx_cnt++;
				}
			} else {
				sprintf(sp->desc[i], "%s:MSI-X-%d-RX",
					dev->name, i);
				err = request_irq(sp->entries[i].vector,
					  s2io_msix_ring_handle, 0, sp->desc[i],
						  sp->s2io_entries[i].arg);
				/* If either data or addr is zero print it */
				if(!(sp->msix_info[i].addr &&
					sp->msix_info[i].data)) {
					DBG_PRINT(ERR_DBG, "%s @ Addr:0x%llx"
						"Data:0x%lx\n",sp->desc[i],
						(unsigned long long)
						sp->msix_info[i].addr,
						(unsigned long)
						ntohl(sp->msix_info[i].data));
				} else {
					msix_rx_cnt++;
				}
			}
			if (err) {
				DBG_PRINT(ERR_DBG,"%s:MSI-X-%d registration "
					  "failed\n", dev->name, i);
				DBG_PRINT(ERR_DBG, "Returned: %d\n", err);
				return -1;
			}
			sp->s2io_entries[i].in_use = MSIX_REGISTERED_SUCCESS;
		}
		printk("MSI-X-TX %d entries enabled\n",msix_tx_cnt);
		printk("MSI-X-RX %d entries enabled\n",msix_rx_cnt);
	}
	if (sp->intr_type == INTA) {
		err = request_irq((int) sp->pdev->irq, s2io_isr, IRQF_SHARED,
				sp->name, dev);
		if (err) {
			DBG_PRINT(ERR_DBG, "%s: ISR registration failed\n",
				  dev->name);
			return -1;
		}
	}
	return 0;
}
static void s2io_rem_isr(struct s2io_nic * sp)
{
	int cnt = 0;
	struct net_device *dev = sp->dev;

	if (sp->intr_type == MSI_X) {
		int i;
		u16 msi_control;

		for (i=1; (sp->s2io_entries[i].in_use ==
			MSIX_REGISTERED_SUCCESS); i++) {
			int vector = sp->entries[i].vector;
			void *arg = sp->s2io_entries[i].arg;

			free_irq(vector, arg);
		}
		pci_read_config_word(sp->pdev, 0x42, &msi_control);
		msi_control &= 0xFFFE; /* Disable MSI */
		pci_write_config_word(sp->pdev, 0x42, msi_control);

		pci_disable_msix(sp->pdev);
	} else {
		free_irq(sp->pdev->irq, dev);
		if (sp->intr_type == MSI) {
			u16 val;

			pci_disable_msi(sp->pdev);
			pci_read_config_word(sp->pdev, 0x4c, &val);
			val ^= 0x1;
			pci_write_config_word(sp->pdev, 0x4c, val);
		}
	}
	/* Waiting till all Interrupt handlers are complete */
	cnt = 0;
	do {
		msleep(10);
		if (!atomic_read(&sp->isr_cnt))
			break;
		cnt++;
	} while(cnt < 5);
}

static void s2io_card_down(struct s2io_nic * sp)
{
	int cnt = 0;
	struct XENA_dev_config __iomem *bar0 = sp->bar0;
	unsigned long flags;
	register u64 val64 = 0;

	del_timer_sync(&sp->alarm_timer);
	/* If s2io_set_link task is executing, wait till it completes. */
	while (test_and_set_bit(0, &(sp->link_state))) {
		msleep(50);
	}
	atomic_set(&sp->card_state, CARD_DOWN);

	/* disable Tx and Rx traffic on the NIC */
	stop_nic(sp);

	s2io_rem_isr(sp);

	/* Kill tasklet. */
	tasklet_kill(&sp->task);

	/* Check if the device is Quiescent and then Reset the NIC */
	do {
		/* As per the HW requirement we need to replenish the
		 * receive buffer to avoid the ring bump. Since there is
		 * no intention of processing the Rx frame at this pointwe are
		 * just settting the ownership bit of rxd in Each Rx
		 * ring to HW and set the appropriate buffer size
		 * based on the ring mode
		 */
		rxd_owner_bit_reset(sp);

		val64 = readq(&bar0->adapter_status);
		if (verify_xena_quiescence(sp)) {
			if(verify_pcc_quiescent(sp, sp->device_enabled_once))
			break;
		}

		msleep(50);
		cnt++;
		if (cnt == 10) {
			DBG_PRINT(ERR_DBG,
				  "s2io_close:Device not Quiescent ");
			DBG_PRINT(ERR_DBG, "adaper status reads 0x%llx\n",
				  (unsigned long long) val64);
			break;
		}
	} while (1);
	s2io_reset(sp);

	spin_lock_irqsave(&sp->tx_lock, flags);
	/* Free all Tx buffers */
	free_tx_buffers(sp);
	spin_unlock_irqrestore(&sp->tx_lock, flags);

	/* Free all Rx buffers */
	spin_lock_irqsave(&sp->rx_lock, flags);
	free_rx_buffers(sp);
	spin_unlock_irqrestore(&sp->rx_lock, flags);

	clear_bit(0, &(sp->link_state));
}

static int s2io_card_up(struct s2io_nic * sp)
{
	int i, ret = 0;
	struct mac_info *mac_control;
	struct config_param *config;
	struct net_device *dev = (struct net_device *) sp->dev;
	u16 interruptible;

	/* Initialize the H/W I/O registers */
	if (init_nic(sp) != 0) {
		DBG_PRINT(ERR_DBG, "%s: H/W initialization failed\n",
			  dev->name);
		s2io_reset(sp);
		return -ENODEV;
	}

	/*
	 * Initializing the Rx buffers. For now we are considering only 1
	 * Rx ring and initializing buffers into 30 Rx blocks
	 */
	mac_control = &sp->mac_control;
	config = &sp->config;

	for (i = 0; i < config->rx_ring_num; i++) {
		if ((ret = fill_rx_buffers(sp, i))) {
			DBG_PRINT(ERR_DBG, "%s: Out of memory in Open\n",
				  dev->name);
			s2io_reset(sp);
			free_rx_buffers(sp);
			return -ENOMEM;
		}
		DBG_PRINT(INFO_DBG, "Buf in ring:%d is %d:\n", i,
			  atomic_read(&sp->rx_bufs_left[i]));
	}
	/* Maintain the state prior to the open */
	if (sp->promisc_flg)
		sp->promisc_flg = 0;
	if (sp->m_cast_flg) {
		sp->m_cast_flg = 0;
		sp->all_multi_pos= 0;
	}

	/* Setting its receive mode */
	s2io_set_multicast(dev);

	if (sp->lro) {
		/* Initialize max aggregatable pkts per session based on MTU */
		sp->lro_max_aggr_per_sess = ((1<<16) - 1) / dev->mtu;
		/* Check if we can use(if specified) user provided value */
		if (lro_max_pkts < sp->lro_max_aggr_per_sess)
			sp->lro_max_aggr_per_sess = lro_max_pkts;
	}

	/* Enable Rx Traffic and interrupts on the NIC */
	if (start_nic(sp)) {
		DBG_PRINT(ERR_DBG, "%s: Starting NIC failed\n", dev->name);
		s2io_reset(sp);
		free_rx_buffers(sp);
		return -ENODEV;
	}

	/* Add interrupt service routine */
	if (s2io_add_isr(sp) != 0) {
		if (sp->intr_type == MSI_X)
			s2io_rem_isr(sp);
		s2io_reset(sp);
		free_rx_buffers(sp);
		return -ENODEV;
	}

	S2IO_TIMER_CONF(sp->alarm_timer, s2io_alarm_handle, sp, (HZ/2));

	/* Enable tasklet for the device */
	tasklet_init(&sp->task, s2io_tasklet, (unsigned long) dev);

	/*  Enable select interrupts */
	if (sp->intr_type != INTA)
		en_dis_able_nic_intrs(sp, ENA_ALL_INTRS, DISABLE_INTRS);
	else {
		interruptible = TX_TRAFFIC_INTR | RX_TRAFFIC_INTR;
		interruptible |= TX_PIC_INTR | RX_PIC_INTR;
		interruptible |= TX_MAC_INTR | RX_MAC_INTR;
		en_dis_able_nic_intrs(sp, interruptible, ENABLE_INTRS);
	}


	atomic_set(&sp->card_state, CARD_UP);
	return 0;
}

/**
 * s2io_restart_nic - Resets the NIC.
 * @data : long pointer to the device private structure
 * Description:
 * This function is scheduled to be run by the s2io_tx_watchdog
 * function after 0.5 secs to reset the NIC. The idea is to reduce
 * the run time of the watch dog routine which is run holding a
 * spin lock.
 */

static void s2io_restart_nic(struct work_struct *work)
{
	struct s2io_nic *sp = container_of(work, struct s2io_nic, rst_timer_task);
	struct net_device *dev = sp->dev;

	rtnl_lock();

	if (!netif_running(dev))
		goto out_unlock;

	s2io_card_down(sp);
	if (s2io_card_up(sp)) {
		DBG_PRINT(ERR_DBG, "%s: Device bring up failed\n",
			  dev->name);
	}
	netif_wake_queue(dev);
	DBG_PRINT(ERR_DBG, "%s: was reset by Tx watchdog timer\n",
		  dev->name);
out_unlock:
	rtnl_unlock();
}

/**
 *  s2io_tx_watchdog - Watchdog for transmit side.
 *  @dev : Pointer to net device structure
 *  Description:
 *  This function is triggered if the Tx Queue is stopped
 *  for a pre-defined amount of time when the Interface is still up.
 *  If the Interface is jammed in such a situation, the hardware is
 *  reset (by s2io_close) and restarted again (by s2io_open) to
 *  overcome any problem that might have been caused in the hardware.
 *  Return value:
 *  void
 */

static void s2io_tx_watchdog(struct net_device *dev)
{
	struct s2io_nic *sp = dev->priv;

	if (netif_carrier_ok(dev)) {
		sp->mac_control.stats_info->sw_stat.watchdog_timer_cnt++;
		schedule_work(&sp->rst_timer_task);
		sp->mac_control.stats_info->sw_stat.soft_reset_cnt++;
	}
}

/**
 *   rx_osm_handler - To perform some OS related operations on SKB.
 *   @sp: private member of the device structure,pointer to s2io_nic structure.
 *   @skb : the socket buffer pointer.
 *   @len : length of the packet
 *   @cksum : FCS checksum of the frame.
 *   @ring_no : the ring from which this RxD was extracted.
 *   Description:
 *   This function is called by the Rx interrupt serivce routine to perform
 *   some OS related operations on the SKB before passing it to the upper
 *   layers. It mainly checks if the checksum is OK, if so adds it to the
 *   SKBs cksum variable, increments the Rx packet count and passes the SKB
 *   to the upper layer. If the checksum is wrong, it increments the Rx
 *   packet error count, frees the SKB and returns error.
 *   Return value:
 *   SUCCESS on success and -1 on failure.
 */
static int rx_osm_handler(struct ring_info *ring_data, struct RxD_t * rxdp)
{
	struct s2io_nic *sp = ring_data->nic;
	struct net_device *dev = (struct net_device *) sp->dev;
	struct sk_buff *skb = (struct sk_buff *)
		((unsigned long) rxdp->Host_Control);
	int ring_no = ring_data->ring_no;
	u16 l3_csum, l4_csum;
	unsigned long long err = rxdp->Control_1 & RXD_T_CODE;
	struct lro *lro;

	skb->dev = dev;

	if (err) {
		/* Check for parity error */
		if (err & 0x1) {
			sp->mac_control.stats_info->sw_stat.parity_err_cnt++;
		}
		err >>= 48;
		switch(err) {
			case 1:
				sp->mac_control.stats_info->sw_stat.
				rx_parity_err_cnt++;
			break;

			case 2:
				sp->mac_control.stats_info->sw_stat.
				rx_abort_cnt++;
			break;

			case 3:
				sp->mac_control.stats_info->sw_stat.
				rx_parity_abort_cnt++;
			break;

			case 4:
				sp->mac_control.stats_info->sw_stat.
				rx_rda_fail_cnt++;
			break;

			case 5:
				sp->mac_control.stats_info->sw_stat.
				rx_unkn_prot_cnt++;
			break;

			case 6:
				sp->mac_control.stats_info->sw_stat.
				rx_fcs_err_cnt++;
			break;

			case 7:
				sp->mac_control.stats_info->sw_stat.
				rx_buf_size_err_cnt++;
			break;

			case 8:
				sp->mac_control.stats_info->sw_stat.
				rx_rxd_corrupt_cnt++;
			break;

			case 15:
				sp->mac_control.stats_info->sw_stat.
				rx_unkn_err_cnt++;
			break;
		}
		/*
		* Drop the packet if bad transfer code. Exception being
		* 0x5, which could be due to unsupported IPv6 extension header.
		* In this case, we let stack handle the packet.
		* Note that in this case, since checksum will be incorrect,
		* stack will validate the same.
		*/
		if (err != 0x5) {
			DBG_PRINT(ERR_DBG, "%s: Rx error Value: 0x%llx\n",
				dev->name, err);
			sp->stats.rx_crc_errors++;
			sp->mac_control.stats_info->sw_stat.mem_freed 
				+= skb->truesize;
			dev_kfree_skb(skb);
			atomic_dec(&sp->rx_bufs_left[ring_no]);
			rxdp->Host_Control = 0;
			return 0;
		}
	}

	/* Updating statistics */
	rxdp->Host_Control = 0;
	if (sp->rxd_mode == RXD_MODE_1) {
		int len = RXD_GET_BUFFER0_SIZE_1(rxdp->Control_2);

		sp->stats.rx_bytes += len;
		skb_put(skb, len);

	} else if (sp->rxd_mode >= RXD_MODE_3A) {
		int get_block = ring_data->rx_curr_get_info.block_index;
		int get_off = ring_data->rx_curr_get_info.offset;
		int buf0_len = RXD_GET_BUFFER0_SIZE_3(rxdp->Control_2);
		int buf2_len = RXD_GET_BUFFER2_SIZE_3(rxdp->Control_2);
		unsigned char *buff = skb_push(skb, buf0_len);

		struct buffAdd *ba = &ring_data->ba[get_block][get_off];
		sp->stats.rx_bytes += buf0_len + buf2_len;
		memcpy(buff, ba->ba_0, buf0_len);

		if (sp->rxd_mode == RXD_MODE_3A) {
			int buf1_len = RXD_GET_BUFFER1_SIZE_3(rxdp->Control_2);

			skb_put(skb, buf1_len);
			skb->len += buf2_len;
			skb->data_len += buf2_len;
			skb_put(skb_shinfo(skb)->frag_list, buf2_len);
			sp->stats.rx_bytes += buf1_len;

		} else
			skb_put(skb, buf2_len);
	}

	if ((rxdp->Control_1 & TCP_OR_UDP_FRAME) && ((!sp->lro) ||
	    (sp->lro && (!(rxdp->Control_1 & RXD_FRAME_IP_FRAG)))) &&
	    (sp->rx_csum)) {
		l3_csum = RXD_GET_L3_CKSUM(rxdp->Control_1);
		l4_csum = RXD_GET_L4_CKSUM(rxdp->Control_1);
		if ((l3_csum == L3_CKSUM_OK) && (l4_csum == L4_CKSUM_OK)) {
			/*
			 * NIC verifies if the Checksum of the received
			 * frame is Ok or not and accordingly returns
			 * a flag in the RxD.
			 */
			skb->ip_summed = CHECKSUM_UNNECESSARY;
			if (sp->lro) {
				u32 tcp_len;
				u8 *tcp;
				int ret = 0;

				ret = s2io_club_tcp_session(skb->data, &tcp,
						&tcp_len, &lro, rxdp, sp);
				switch (ret) {
					case 3: /* Begin anew */
						lro->parent = skb;
						goto aggregate;
					case 1: /* Aggregate */
					{
						lro_append_pkt(sp, lro,
							skb, tcp_len);
						goto aggregate;
					}
					case 4: /* Flush session */
					{
						lro_append_pkt(sp, lro,
							skb, tcp_len);
						queue_rx_frame(lro->parent);
						clear_lro_session(lro);
						sp->mac_control.stats_info->
						    sw_stat.flush_max_pkts++;
						goto aggregate;
					}
					case 2: /* Flush both */
						lro->parent->data_len =
							lro->frags_len;
						sp->mac_control.stats_info->
						     sw_stat.sending_both++;
						queue_rx_frame(lro->parent);
						clear_lro_session(lro);
						goto send_up;
					case 0: /* sessions exceeded */
					case -1: /* non-TCP or not
						  * L2 aggregatable
						  */
					case 5: /*
						 * First pkt in session not
						 * L3/L4 aggregatable
						 */
						break;
					default:
						DBG_PRINT(ERR_DBG,
							"%s: Samadhana!!\n",
							 __FUNCTION__);
						BUG();
				}
			}
		} else {
			/*
			 * Packet with erroneous checksum, let the
			 * upper layers deal with it.
			 */
			skb->ip_summed = CHECKSUM_NONE;
		}
	} else {
		skb->ip_summed = CHECKSUM_NONE;
	}
	sp->mac_control.stats_info->sw_stat.mem_freed += skb->truesize;
	if (!sp->lro) {
		skb->protocol = eth_type_trans(skb, dev);
		if ((sp->vlgrp && RXD_GET_VLAN_TAG(rxdp->Control_2) &&
			vlan_strip_flag)) {
			/* Queueing the vlan frame to the upper layer */
			if (napi)
				vlan_hwaccel_receive_skb(skb, sp->vlgrp,
					RXD_GET_VLAN_TAG(rxdp->Control_2));
			else
				vlan_hwaccel_rx(skb, sp->vlgrp,
					RXD_GET_VLAN_TAG(rxdp->Control_2));
		} else {
			if (napi)
				netif_receive_skb(skb);
			else
				netif_rx(skb);
		}
	} else {
send_up:
		queue_rx_frame(skb);
	}
	dev->last_rx = jiffies;
aggregate:
	atomic_dec(&sp->rx_bufs_left[ring_no]);
	return SUCCESS;
}

/**
 *  s2io_link - stops/starts the Tx queue.
 *  @sp : private member of the device structure, which is a pointer to the
 *  s2io_nic structure.
 *  @link : inidicates whether link is UP/DOWN.
 *  Description:
 *  This function stops/starts the Tx queue depending on whether the link
 *  status of the NIC is is down or up. This is called by the Alarm
 *  interrupt handler whenever a link change interrupt comes up.
 *  Return value:
 *  void.
 */

static void s2io_link(struct s2io_nic * sp, int link)
{
	struct net_device *dev = (struct net_device *) sp->dev;

	if (link != sp->last_link_state) {
		if (link == LINK_DOWN) {
			DBG_PRINT(ERR_DBG, "%s: Link down\n", dev->name);
			netif_carrier_off(dev);
			if(sp->mac_control.stats_info->sw_stat.link_up_cnt)
			sp->mac_control.stats_info->sw_stat.link_up_time = 
				jiffies - sp->start_time;
			sp->mac_control.stats_info->sw_stat.link_down_cnt++;
		} else {
			DBG_PRINT(ERR_DBG, "%s: Link Up\n", dev->name);
			if (sp->mac_control.stats_info->sw_stat.link_down_cnt)
			sp->mac_control.stats_info->sw_stat.link_down_time = 
				jiffies - sp->start_time;
			sp->mac_control.stats_info->sw_stat.link_up_cnt++;
			netif_carrier_on(dev);
		}
	}
	sp->last_link_state = link;
	sp->start_time = jiffies;
}

/**
 *  get_xena_rev_id - to identify revision ID of xena.
 *  @pdev : PCI Dev structure
 *  Description:
 *  Function to identify the Revision ID of xena.
 *  Return value:
 *  returns the revision ID of the device.
 */

static int get_xena_rev_id(struct pci_dev *pdev)
{
	u8 id = 0;
	int ret;
	ret = pci_read_config_byte(pdev, PCI_REVISION_ID, (u8 *) & id);
	return id;
}

/**
 *  s2io_init_pci -Initialization of PCI and PCI-X configuration registers .
 *  @sp : private member of the device structure, which is a pointer to the
 *  s2io_nic structure.
 *  Description:
 *  This function initializes a few of the PCI and PCI-X configuration registers
 *  with recommended values.
 *  Return value:
 *  void
 */

static void s2io_init_pci(struct s2io_nic * sp)
{
	u16 pci_cmd = 0, pcix_cmd = 0;

	/* Enable Data Parity Error Recovery in PCI-X command register. */
	pci_read_config_word(sp->pdev, PCIX_COMMAND_REGISTER,
			     &(pcix_cmd));
	pci_write_config_word(sp->pdev, PCIX_COMMAND_REGISTER,
			      (pcix_cmd | 1));
	pci_read_config_word(sp->pdev, PCIX_COMMAND_REGISTER,
			     &(pcix_cmd));

	/* Set the PErr Response bit in PCI command register. */
	pci_read_config_word(sp->pdev, PCI_COMMAND, &pci_cmd);
	pci_write_config_word(sp->pdev, PCI_COMMAND,
			      (pci_cmd | PCI_COMMAND_PARITY));
	pci_read_config_word(sp->pdev, PCI_COMMAND, &pci_cmd);
}

static int s2io_verify_parm(struct pci_dev *pdev, u8 *dev_intr_type)
{
	if ( tx_fifo_num > 8) {
		DBG_PRINT(ERR_DBG, "s2io: Requested number of Tx fifos not "
			 "supported\n");
		DBG_PRINT(ERR_DBG, "s2io: Default to 8 Tx fifos\n");
		tx_fifo_num = 8;
	}
	if ( rx_ring_num > 8) {
		DBG_PRINT(ERR_DBG, "s2io: Requested number of Rx rings not "
			 "supported\n");
		DBG_PRINT(ERR_DBG, "s2io: Default to 8 Rx rings\n");
		rx_ring_num = 8;
	}
	if (*dev_intr_type != INTA)
		napi = 0;

#ifndef CONFIG_PCI_MSI
	if (*dev_intr_type != INTA) {
		DBG_PRINT(ERR_DBG, "s2io: This kernel does not support"
			  "MSI/MSI-X. Defaulting to INTA\n");
		*dev_intr_type = INTA;
	}
#else
	if (*dev_intr_type > MSI_X) {
		DBG_PRINT(ERR_DBG, "s2io: Wrong intr_type requested. "
			  "Defaulting to INTA\n");
		*dev_intr_type = INTA;
	}
#endif
	if ((*dev_intr_type == MSI_X) &&
			((pdev->device != PCI_DEVICE_ID_HERC_WIN) &&
			(pdev->device != PCI_DEVICE_ID_HERC_UNI))) {
		DBG_PRINT(ERR_DBG, "s2io: Xframe I does not support MSI_X. "
					"Defaulting to INTA\n");
		*dev_intr_type = INTA;
	}

	if (rx_ring_mode > 3) {
		DBG_PRINT(ERR_DBG, "s2io: Requested ring mode not supported\n");
		DBG_PRINT(ERR_DBG, "s2io: Defaulting to 3-buffer mode\n");
		rx_ring_mode = 3;
	}
	return SUCCESS;
}

/**
 * rts_ds_steer - Receive traffic steering based on IPv4 or IPv6 TOS
 * or Traffic class respectively.
 * @nic: device peivate variable
 * Description: The function configures the receive steering to
 * desired receive ring.
 * Return Value:  SUCCESS on success and
 * '-1' on failure (endian settings incorrect).
 */
static int rts_ds_steer(struct s2io_nic *nic, u8 ds_codepoint, u8 ring)
{
	struct XENA_dev_config __iomem *bar0 = nic->bar0;
	register u64 val64 = 0;

	if (ds_codepoint > 63)
		return FAILURE;

	val64 = RTS_DS_MEM_DATA(ring);
	writeq(val64, &bar0->rts_ds_mem_data);

	val64 = RTS_DS_MEM_CTRL_WE |
		RTS_DS_MEM_CTRL_STROBE_NEW_CMD |
		RTS_DS_MEM_CTRL_OFFSET(ds_codepoint);

	writeq(val64, &bar0->rts_ds_mem_ctrl);

	return wait_for_cmd_complete(&bar0->rts_ds_mem_ctrl,
				RTS_DS_MEM_CTRL_STROBE_CMD_BEING_EXECUTED,
				S2IO_BIT_RESET);
}

/**
 *  s2io_init_nic - Initialization of the adapter .
 *  @pdev : structure containing the PCI related information of the device.
 *  @pre: List of PCI devices supported by the driver listed in s2io_tbl.
 *  Description:
 *  The function initializes an adapter identified by the pci_dec structure.
 *  All OS related initialization including memory and device structure and
 *  initlaization of the device private variable is done. Also the swapper
 *  control register is initialized to enable read and write into the I/O
 *  registers of the device.
 *  Return value:
 *  returns 0 on success and negative on failure.
 */

static int __devinit
s2io_init_nic(struct pci_dev *pdev, const struct pci_device_id *pre)
{
	struct s2io_nic *sp;
	struct net_device *dev;
	int i, j, ret;
	int dma_flag = FALSE;
	u32 mac_up, mac_down;
	u64 val64 = 0, tmp64 = 0;
	struct XENA_dev_config __iomem *bar0 = NULL;
	u16 subid;
	struct mac_info *mac_control;
	struct config_param *config;
	int mode;
	u8 dev_intr_type = intr_type;

	if ((ret = s2io_verify_parm(pdev, &dev_intr_type)))
		return ret;

	if ((ret = pci_enable_device(pdev))) {
		DBG_PRINT(ERR_DBG,
			  "s2io_init_nic: pci_enable_device failed\n");
		return ret;
	}

	if (!pci_set_dma_mask(pdev, DMA_64BIT_MASK)) {
		DBG_PRINT(INIT_DBG, "s2io_init_nic: Using 64bit DMA\n");
		dma_flag = TRUE;
		if (pci_set_consistent_dma_mask
		    (pdev, DMA_64BIT_MASK)) {
			DBG_PRINT(ERR_DBG,
				  "Unable to obtain 64bit DMA for \
					consistent allocations\n");
			pci_disable_device(pdev);
			return -ENOMEM;
		}
	} else if (!pci_set_dma_mask(pdev, DMA_32BIT_MASK)) {
		DBG_PRINT(INIT_DBG, "s2io_init_nic: Using 32bit DMA\n");
	} else {
		pci_disable_device(pdev);
		return -ENOMEM;
	}
	if (dev_intr_type != MSI_X) {
		if (pci_request_regions(pdev, s2io_driver_name)) {
			DBG_PRINT(ERR_DBG, "Request Regions failed\n");
			pci_disable_device(pdev);
			return -ENODEV;
		}
	}
	else {
		if (!(request_mem_region(pci_resource_start(pdev, 0),
               	         pci_resource_len(pdev, 0), s2io_driver_name))) {
			DBG_PRINT(ERR_DBG, "bar0 Request Regions failed\n");
			pci_disable_device(pdev);
			return -ENODEV;
		}
        	if (!(request_mem_region(pci_resource_start(pdev, 2),
               	         pci_resource_len(pdev, 2), s2io_driver_name))) {
			DBG_PRINT(ERR_DBG, "bar1 Request Regions failed\n");
                	release_mem_region(pci_resource_start(pdev, 0),
                                   pci_resource_len(pdev, 0));
			pci_disable_device(pdev);
			return -ENODEV;
		}
	}

	dev = alloc_etherdev(sizeof(struct s2io_nic));
	if (dev == NULL) {
		DBG_PRINT(ERR_DBG, "Device allocation failed\n");
		pci_disable_device(pdev);
		pci_release_regions(pdev);
		return -ENODEV;
	}

	pci_set_master(pdev);
	pci_set_drvdata(pdev, dev);
	SET_MODULE_OWNER(dev);
	SET_NETDEV_DEV(dev, &pdev->dev);

	/*  Private member variable initialized to s2io NIC structure */
	sp = dev->priv;
	memset(sp, 0, sizeof(struct s2io_nic));
	sp->dev = dev;
	sp->pdev = pdev;
	sp->high_dma_flag = dma_flag;
	sp->device_enabled_once = FALSE;
	if (rx_ring_mode == 1)
		sp->rxd_mode = RXD_MODE_1;
	if (rx_ring_mode == 2)
		sp->rxd_mode = RXD_MODE_3B;
	if (rx_ring_mode == 3)
		sp->rxd_mode = RXD_MODE_3A;

	sp->intr_type = dev_intr_type;

	if ((pdev->device == PCI_DEVICE_ID_HERC_WIN) ||
		(pdev->device == PCI_DEVICE_ID_HERC_UNI))
		sp->device_type = XFRAME_II_DEVICE;
	else
		sp->device_type = XFRAME_I_DEVICE;

	sp->lro = lro;

	/* Initialize some PCI/PCI-X fields of the NIC. */
	s2io_init_pci(sp);

	/*
	 * Setting the device configuration parameters.
	 * Most of these parameters can be specified by the user during
	 * module insertion as they are module loadable parameters. If
	 * these parameters are not not specified during load time, they
	 * are initialized with default values.
	 */
	mac_control = &sp->mac_control;
	config = &sp->config;

	/* Tx side parameters. */
	config->tx_fifo_num = tx_fifo_num;
	for (i = 0; i < MAX_TX_FIFOS; i++) {
		config->tx_cfg[i].fifo_len = tx_fifo_len[i];
		config->tx_cfg[i].fifo_priority = i;
	}

	/* mapping the QoS priority to the configured fifos */
	for (i = 0; i < MAX_TX_FIFOS; i++)
		config->fifo_mapping[i] = fifo_map[config->tx_fifo_num][i];

	config->tx_intr_type = TXD_INT_TYPE_UTILZ;
	for (i = 0; i < config->tx_fifo_num; i++) {
		config->tx_cfg[i].f_no_snoop =
		    (NO_SNOOP_TXD | NO_SNOOP_TXD_BUFFER);
		if (config->tx_cfg[i].fifo_len < 65) {
			config->tx_intr_type = TXD_INT_TYPE_PER_LIST;
			break;
		}
	}
	/* + 2 because one Txd for skb->data and one Txd for UFO */
	config->max_txds = MAX_SKB_FRAGS + 2;

	/* Rx side parameters. */
	config->rx_ring_num = rx_ring_num;
	for (i = 0; i < MAX_RX_RINGS; i++) {
		config->rx_cfg[i].num_rxd = rx_ring_sz[i] *
		    (rxd_count[sp->rxd_mode] + 1);
		config->rx_cfg[i].ring_priority = i;
	}

	for (i = 0; i < rx_ring_num; i++) {
		config->rx_cfg[i].ring_org = RING_ORG_BUFF1;
		config->rx_cfg[i].f_no_snoop =
		    (NO_SNOOP_RXD | NO_SNOOP_RXD_BUFFER);
	}

	/*  Setting Mac Control parameters */
	mac_control->rmac_pause_time = rmac_pause_time;
	mac_control->mc_pause_threshold_q0q3 = mc_pause_threshold_q0q3;
	mac_control->mc_pause_threshold_q4q7 = mc_pause_threshold_q4q7;


	/* Initialize Ring buffer parameters. */
	for (i = 0; i < config->rx_ring_num; i++)
		atomic_set(&sp->rx_bufs_left[i], 0);

	/* Initialize the number of ISRs currently running */
	atomic_set(&sp->isr_cnt, 0);

	/*  initialize the shared memory used by the NIC and the host */
	if (init_shared_mem(sp)) {
		DBG_PRINT(ERR_DBG, "%s: Memory allocation failed\n",
			  dev->name);
		ret = -ENOMEM;
		goto mem_alloc_failed;
	}

	sp->bar0 = ioremap(pci_resource_start(pdev, 0),
				     pci_resource_len(pdev, 0));
	if (!sp->bar0) {
		DBG_PRINT(ERR_DBG, "%s: Neterion: cannot remap io mem1\n",
			  dev->name);
		ret = -ENOMEM;
		goto bar0_remap_failed;
	}

	sp->bar1 = ioremap(pci_resource_start(pdev, 2),
				     pci_resource_len(pdev, 2));
	if (!sp->bar1) {
		DBG_PRINT(ERR_DBG, "%s: Neterion: cannot remap io mem2\n",
			  dev->name);
		ret = -ENOMEM;
		goto bar1_remap_failed;
	}

	dev->irq = pdev->irq;
	dev->base_addr = (unsigned long) sp->bar0;

	/* Initializing the BAR1 address as the start of the FIFO pointer. */
	for (j = 0; j < MAX_TX_FIFOS; j++) {
		mac_control->tx_FIFO_start[j] = (struct TxFIFO_element __iomem *)
		    (sp->bar1 + (j * 0x00020000));
	}

	/*  Driver entry points */
	dev->open = &s2io_open;
	dev->stop = &s2io_close;
	dev->hard_start_xmit = &s2io_xmit;
	dev->get_stats = &s2io_get_stats;
	dev->set_multicast_list = &s2io_set_multicast;
	dev->do_ioctl = &s2io_ioctl;
	dev->change_mtu = &s2io_change_mtu;
	SET_ETHTOOL_OPS(dev, &netdev_ethtool_ops);
	dev->features |= NETIF_F_HW_VLAN_TX | NETIF_F_HW_VLAN_RX;
	dev->vlan_rx_register = s2io_vlan_rx_register;
	dev->vlan_rx_kill_vid = (void *)s2io_vlan_rx_kill_vid;

	/*
	 * will use eth_mac_addr() for  dev->set_mac_address
	 * mac address will be set every time dev->open() is called
	 */
	dev->poll = s2io_poll;
	dev->weight = 32;

#ifdef CONFIG_NET_POLL_CONTROLLER
	dev->poll_controller = s2io_netpoll;
#endif

	dev->features |= NETIF_F_SG | NETIF_F_IP_CSUM;
	if (sp->high_dma_flag == TRUE)
		dev->features |= NETIF_F_HIGHDMA;
	dev->features |= NETIF_F_TSO;
	dev->features |= NETIF_F_TSO6;
	if ((sp->device_type & XFRAME_II_DEVICE) && (ufo))  {
		dev->features |= NETIF_F_UFO;
		dev->features |= NETIF_F_HW_CSUM;
	}

	dev->tx_timeout = &s2io_tx_watchdog;
	dev->watchdog_timeo = WATCH_DOG_TIMEOUT;
	INIT_WORK(&sp->rst_timer_task, s2io_restart_nic);
	INIT_WORK(&sp->set_link_task, s2io_set_link);

	pci_save_state(sp->pdev);

	/* Setting swapper control on the NIC, for proper reset operation */
	if (s2io_set_swapper(sp)) {
		DBG_PRINT(ERR_DBG, "%s:swapper settings are wrong\n",
			  dev->name);
		ret = -EAGAIN;
		goto set_swap_failed;
	}

	/* Verify if the Herc works on the slot its placed into */
	if (sp->device_type & XFRAME_II_DEVICE) {
		mode = s2io_verify_pci_mode(sp);
		if (mode < 0) {
			DBG_PRINT(ERR_DBG, "%s: ", __FUNCTION__);
			DBG_PRINT(ERR_DBG, " Unsupported PCI bus mode\n");
			ret = -EBADSLT;
			goto set_swap_failed;
		}
	}

	/* Not needed for Herc */
	if (sp->device_type & XFRAME_I_DEVICE) {
		/*
		 * Fix for all "FFs" MAC address problems observed on
		 * Alpha platforms
		 */
		fix_mac_address(sp);
		s2io_reset(sp);
	}

	/*
	 * MAC address initialization.
	 * For now only one mac address will be read and used.
	 */
	bar0 = sp->bar0;
	val64 = RMAC_ADDR_CMD_MEM_RD | RMAC_ADDR_CMD_MEM_STROBE_NEW_CMD |
	    RMAC_ADDR_CMD_MEM_OFFSET(0 + MAC_MAC_ADDR_START_OFFSET);
	writeq(val64, &bar0->rmac_addr_cmd_mem);
	wait_for_cmd_complete(&bar0->rmac_addr_cmd_mem,
		      RMAC_ADDR_CMD_MEM_STROBE_CMD_EXECUTING, S2IO_BIT_RESET);
	tmp64 = readq(&bar0->rmac_addr_data0_mem);
	mac_down = (u32) tmp64;
	mac_up = (u32) (tmp64 >> 32);

	sp->def_mac_addr[0].mac_addr[3] = (u8) (mac_up);
	sp->def_mac_addr[0].mac_addr[2] = (u8) (mac_up >> 8);
	sp->def_mac_addr[0].mac_addr[1] = (u8) (mac_up >> 16);
	sp->def_mac_addr[0].mac_addr[0] = (u8) (mac_up >> 24);
	sp->def_mac_addr[0].mac_addr[5] = (u8) (mac_down >> 16);
	sp->def_mac_addr[0].mac_addr[4] = (u8) (mac_down >> 24);

	/*  Set the factory defined MAC address initially   */
	dev->addr_len = ETH_ALEN;
	memcpy(dev->dev_addr, sp->def_mac_addr, ETH_ALEN);

	/* reset Nic and bring it to known state */
	s2io_reset(sp);

	/*
	 * Initialize the tasklet status and link state flags
	 * and the card state parameter
	 */
	atomic_set(&(sp->card_state), 0);
	sp->tasklet_status = 0;
	sp->link_state = 0;

	/* Initialize spinlocks */
	spin_lock_init(&sp->tx_lock);

	if (!napi)
		spin_lock_init(&sp->put_lock);
	spin_lock_init(&sp->rx_lock);

	/*
	 * SXE-002: Configure link and activity LED to init state
	 * on driver load.
	 */
	subid = sp->pdev->subsystem_device;
	if ((subid & 0xFF) >= 0x07) {
		val64 = readq(&bar0->gpio_control);
		val64 |= 0x0000800000000000ULL;
		writeq(val64, &bar0->gpio_control);
		val64 = 0x0411040400000000ULL;
		writeq(val64, (void __iomem *) bar0 + 0x2700);
		val64 = readq(&bar0->gpio_control);
	}

	sp->rx_csum = 1;	/* Rx chksum verify enabled by default */

	if (register_netdev(dev)) {
		DBG_PRINT(ERR_DBG, "Device registration failed\n");
		ret = -ENODEV;
		goto register_failed;
	}
	s2io_vpd_read(sp);
	DBG_PRINT(ERR_DBG, "Copyright(c) 2002-2007 Neterion Inc.\n");
	DBG_PRINT(ERR_DBG, "%s: Neterion %s (rev %d)\n",dev->name,
		  sp->product_name, get_xena_rev_id(sp->pdev));
	DBG_PRINT(ERR_DBG, "%s: Driver version %s\n", dev->name,
		  s2io_driver_version);
	DBG_PRINT(ERR_DBG, "%s: MAC ADDR: "
			  "%02x:%02x:%02x:%02x:%02x:%02x", dev->name,
			  sp->def_mac_addr[0].mac_addr[0],
			  sp->def_mac_addr[0].mac_addr[1],
			  sp->def_mac_addr[0].mac_addr[2],
			  sp->def_mac_addr[0].mac_addr[3],
			  sp->def_mac_addr[0].mac_addr[4],
			  sp->def_mac_addr[0].mac_addr[5]);
	DBG_PRINT(ERR_DBG, "SERIAL NUMBER: %s\n", sp->serial_num);
	if (sp->device_type & XFRAME_II_DEVICE) {
		mode = s2io_print_pci_mode(sp);
		if (mode < 0) {
			DBG_PRINT(ERR_DBG, " Unsupported PCI bus mode\n");
			ret = -EBADSLT;
			unregister_netdev(dev);
			goto set_swap_failed;
		}
	}
	switch(sp->rxd_mode) {
		case RXD_MODE_1:
		    DBG_PRINT(ERR_DBG, "%s: 1-Buffer receive mode enabled\n",
						dev->name);
		    break;
		case RXD_MODE_3B:
		    DBG_PRINT(ERR_DBG, "%s: 2-Buffer receive mode enabled\n",
						dev->name);
		    break;
		case RXD_MODE_3A:
		    DBG_PRINT(ERR_DBG, "%s: 3-Buffer receive mode enabled\n",
						dev->name);
		    break;
	}

	if (napi)
		DBG_PRINT(ERR_DBG, "%s: NAPI enabled\n", dev->name);
	switch(sp->intr_type) {
		case INTA:
		    DBG_PRINT(ERR_DBG, "%s: Interrupt type INTA\n", dev->name);
		    break;
		case MSI:
		    DBG_PRINT(ERR_DBG, "%s: Interrupt type MSI\n", dev->name);
		    break;
		case MSI_X:
		    DBG_PRINT(ERR_DBG, "%s: Interrupt type MSI-X\n", dev->name);
		    break;
	}
	if (sp->lro)
		DBG_PRINT(ERR_DBG, "%s: Large receive offload enabled\n",
			  dev->name);
	if (ufo)
		DBG_PRINT(ERR_DBG, "%s: UDP Fragmentation Offload(UFO)"
					" enabled\n", dev->name);
	/* Initialize device name */
	sprintf(sp->name, "%s Neterion %s", dev->name, sp->product_name);

	/* Initialize bimodal Interrupts */
	sp->config.bimodal = bimodal;
	if (!(sp->device_type & XFRAME_II_DEVICE) && bimodal) {
		sp->config.bimodal = 0;
		DBG_PRINT(ERR_DBG,"%s:Bimodal intr not supported by Xframe I\n",
			dev->name);
	}

	/*
	 * Make Link state as off at this point, when the Link change
	 * interrupt comes the state will be automatically changed to
	 * the right state.
	 */
	netif_carrier_off(dev);

	return 0;

      register_failed:
      set_swap_failed:
	iounmap(sp->bar1);
      bar1_remap_failed:
	iounmap(sp->bar0);
      bar0_remap_failed:
      mem_alloc_failed:
	free_shared_mem(sp);
	pci_disable_device(pdev);
	if (dev_intr_type != MSI_X)
		pci_release_regions(pdev);
	else {
		release_mem_region(pci_resource_start(pdev, 0),
			pci_resource_len(pdev, 0));
		release_mem_region(pci_resource_start(pdev, 2),
			pci_resource_len(pdev, 2));
	}
	pci_set_drvdata(pdev, NULL);
	free_netdev(dev);

	return ret;
}

/**
 * s2io_rem_nic - Free the PCI device
 * @pdev: structure containing the PCI related information of the device.
 * Description: This function is called by the Pci subsystem to release a
 * PCI device and free up all resource held up by the device. This could
 * be in response to a Hot plug event or when the driver is to be removed
 * from memory.
 */

static void __devexit s2io_rem_nic(struct pci_dev *pdev)
{
	struct net_device *dev =
	    (struct net_device *) pci_get_drvdata(pdev);
	struct s2io_nic *sp;

	if (dev == NULL) {
		DBG_PRINT(ERR_DBG, "Driver Data is NULL!!\n");
		return;
	}

	flush_scheduled_work();

	sp = dev->priv;
	unregister_netdev(dev);

	free_shared_mem(sp);
	iounmap(sp->bar0);
	iounmap(sp->bar1);
	if (sp->intr_type != MSI_X)
		pci_release_regions(pdev);
	else {
		release_mem_region(pci_resource_start(pdev, 0),
			pci_resource_len(pdev, 0));
		release_mem_region(pci_resource_start(pdev, 2),
			pci_resource_len(pdev, 2));
	}
	pci_set_drvdata(pdev, NULL);
	free_netdev(dev);
	pci_disable_device(pdev);
}

/**
 * s2io_starter - Entry point for the driver
 * Description: This function is the entry point for the driver. It verifies
 * the module loadable parameters and initializes PCI configuration space.
 */

int __init s2io_starter(void)
{
	return pci_register_driver(&s2io_driver);
}

/**
 * s2io_closer - Cleanup routine for the driver
 * Description: This function is the cleanup routine for the driver. It unregist * ers the driver.
 */

static __exit void s2io_closer(void)
{
	pci_unregister_driver(&s2io_driver);
	DBG_PRINT(INIT_DBG, "cleanup done\n");
}

module_init(s2io_starter);
module_exit(s2io_closer);

static int check_L2_lro_capable(u8 *buffer, struct iphdr **ip,
		struct tcphdr **tcp, struct RxD_t *rxdp)
{
	int ip_off;
	u8 l2_type = (u8)((rxdp->Control_1 >> 37) & 0x7), ip_len;

	if (!(rxdp->Control_1 & RXD_FRAME_PROTO_TCP)) {
		DBG_PRINT(INIT_DBG,"%s: Non-TCP frames not supported for LRO\n",
			  __FUNCTION__);
		return -1;
	}

	/* TODO:
	 * By default the VLAN field in the MAC is stripped by the card, if this
	 * feature is turned off in rx_pa_cfg register, then the ip_off field
	 * has to be shifted by a further 2 bytes
	 */
	switch (l2_type) {
		case 0: /* DIX type */
		case 4: /* DIX type with VLAN */
			ip_off = HEADER_ETHERNET_II_802_3_SIZE;
			break;
		/* LLC, SNAP etc are considered non-mergeable */
		default:
			return -1;
	}

	*ip = (struct iphdr *)((u8 *)buffer + ip_off);
	ip_len = (u8)((*ip)->ihl);
	ip_len <<= 2;
	*tcp = (struct tcphdr *)((unsigned long)*ip + ip_len);

	return 0;
}

static int check_for_socket_match(struct lro *lro, struct iphdr *ip,
				  struct tcphdr *tcp)
{
	DBG_PRINT(INFO_DBG,"%s: Been here...\n", __FUNCTION__);
	if ((lro->iph->saddr != ip->saddr) || (lro->iph->daddr != ip->daddr) ||
	   (lro->tcph->source != tcp->source) || (lro->tcph->dest != tcp->dest))
		return -1;
	return 0;
}

static inline int get_l4_pyld_length(struct iphdr *ip, struct tcphdr *tcp)
{
	return(ntohs(ip->tot_len) - (ip->ihl << 2) - (tcp->doff << 2));
}

static void initiate_new_session(struct lro *lro, u8 *l2h,
		     struct iphdr *ip, struct tcphdr *tcp, u32 tcp_pyld_len)
{
	DBG_PRINT(INFO_DBG,"%s: Been here...\n", __FUNCTION__);
	lro->l2h = l2h;
	lro->iph = ip;
	lro->tcph = tcp;
	lro->tcp_next_seq = tcp_pyld_len + ntohl(tcp->seq);
	lro->tcp_ack = ntohl(tcp->ack_seq);
	lro->sg_num = 1;
	lro->total_len = ntohs(ip->tot_len);
	lro->frags_len = 0;
	/*
	 * check if we saw TCP timestamp. Other consistency checks have
	 * already been done.
 	 */
	if (tcp->doff == 8) {
		u32 *ptr;
		ptr = (u32 *)(tcp+1);
		lro->saw_ts = 1;
		lro->cur_tsval = *(ptr+1);
		lro->cur_tsecr = *(ptr+2);
	}
	lro->in_use = 1;
}

static void update_L3L4_header(struct s2io_nic *sp, struct lro *lro)
{
	struct iphdr *ip = lro->iph;
	struct tcphdr *tcp = lro->tcph;
	__sum16 nchk;
	struct stat_block *statinfo = sp->mac_control.stats_info;
	DBG_PRINT(INFO_DBG,"%s: Been here...\n", __FUNCTION__);

	/* Update L3 header */
	ip->tot_len = htons(lro->total_len);
	ip->check = 0;
	nchk = ip_fast_csum((u8 *)lro->iph, ip->ihl);
	ip->check = nchk;

	/* Update L4 header */
	tcp->ack_seq = lro->tcp_ack;
	tcp->window = lro->window;

	/* Update tsecr field if this session has timestamps enabled */
	if (lro->saw_ts) {
		u32 *ptr = (u32 *)(tcp + 1);
		*(ptr+2) = lro->cur_tsecr;
	}

	/* Update counters required for calculation of
	 * average no. of packets aggregated.
	 */
	statinfo->sw_stat.sum_avg_pkts_aggregated += lro->sg_num;
	statinfo->sw_stat.num_aggregations++;
}

static void aggregate_new_rx(struct lro *lro, struct iphdr *ip,
		struct tcphdr *tcp, u32 l4_pyld)
{
	DBG_PRINT(INFO_DBG,"%s: Been here...\n", __FUNCTION__);
	lro->total_len += l4_pyld;
	lro->frags_len += l4_pyld;
	lro->tcp_next_seq += l4_pyld;
	lro->sg_num++;

	/* Update ack seq no. and window ad(from this pkt) in LRO object */
	lro->tcp_ack = tcp->ack_seq;
	lro->window = tcp->window;

	if (lro->saw_ts) {
		u32 *ptr;
		/* Update tsecr and tsval from this packet */
		ptr = (u32 *) (tcp + 1);
		lro->cur_tsval = *(ptr + 1);
		lro->cur_tsecr = *(ptr + 2);
	}
}

static int verify_l3_l4_lro_capable(struct lro *l_lro, struct iphdr *ip,
				    struct tcphdr *tcp, u32 tcp_pyld_len)
{
	u8 *ptr;

	DBG_PRINT(INFO_DBG,"%s: Been here...\n", __FUNCTION__);

	if (!tcp_pyld_len) {
		/* Runt frame or a pure ack */
		return -1;
	}

	if (ip->ihl != 5) /* IP has options */
		return -1;

	/* If we see CE codepoint in IP header, packet is not mergeable */
	if (INET_ECN_is_ce(ipv4_get_dsfield(ip)))
		return -1;

	/* If we see ECE or CWR flags in TCP header, packet is not mergeable */
	if (tcp->urg || tcp->psh || tcp->rst || tcp->syn || tcp->fin ||
				    tcp->ece || tcp->cwr || !tcp->ack) {
		/*
		 * Currently recognize only the ack control word and
		 * any other control field being set would result in
		 * flushing the LRO session
		 */
		return -1;
	}

	/*
	 * Allow only one TCP timestamp option. Don't aggregate if
	 * any other options are detected.
	 */
	if (tcp->doff != 5 && tcp->doff != 8)
		return -1;

	if (tcp->doff == 8) {
		ptr = (u8 *)(tcp + 1);
		while (*ptr == TCPOPT_NOP)
			ptr++;
		if (*ptr != TCPOPT_TIMESTAMP || *(ptr+1) != TCPOLEN_TIMESTAMP)
			return -1;

		/* Ensure timestamp value increases monotonically */
		if (l_lro)
			if (l_lro->cur_tsval > *((u32 *)(ptr+2)))
				return -1;

		/* timestamp echo reply should be non-zero */
		if (*((u32 *)(ptr+6)) == 0)
			return -1;
	}

	return 0;
}

static int
s2io_club_tcp_session(u8 *buffer, u8 **tcp, u32 *tcp_len, struct lro **lro,
		      struct RxD_t *rxdp, struct s2io_nic *sp)
{
	struct iphdr *ip;
	struct tcphdr *tcph;
	int ret = 0, i;

	if (!(ret = check_L2_lro_capable(buffer, &ip, (struct tcphdr **)tcp,
					 rxdp))) {
		DBG_PRINT(INFO_DBG,"IP Saddr: %x Daddr: %x\n",
			  ip->saddr, ip->daddr);
	} else {
		return ret;
	}

	tcph = (struct tcphdr *)*tcp;
	*tcp_len = get_l4_pyld_length(ip, tcph);
	for (i=0; i<MAX_LRO_SESSIONS; i++) {
		struct lro *l_lro = &sp->lro0_n[i];
		if (l_lro->in_use) {
			if (check_for_socket_match(l_lro, ip, tcph))
				continue;
			/* Sock pair matched */
			*lro = l_lro;

			if ((*lro)->tcp_next_seq != ntohl(tcph->seq)) {
				DBG_PRINT(INFO_DBG, "%s:Out of order. expected "
					  "0x%x, actual 0x%x\n", __FUNCTION__,
					  (*lro)->tcp_next_seq,
					  ntohl(tcph->seq));

				sp->mac_control.stats_info->
				   sw_stat.outof_sequence_pkts++;
				ret = 2;
				break;
			}

			if (!verify_l3_l4_lro_capable(l_lro, ip, tcph,*tcp_len))
				ret = 1; /* Aggregate */
			else
				ret = 2; /* Flush both */
			break;
		}
	}

	if (ret == 0) {
		/* Before searching for available LRO objects,
		 * check if the pkt is L3/L4 aggregatable. If not
		 * don't create new LRO session. Just send this
		 * packet up.
		 */
		if (verify_l3_l4_lro_capable(NULL, ip, tcph, *tcp_len)) {
			return 5;
		}

		for (i=0; i<MAX_LRO_SESSIONS; i++) {
			struct lro *l_lro = &sp->lro0_n[i];
			if (!(l_lro->in_use)) {
				*lro = l_lro;
				ret = 3; /* Begin anew */
				break;
			}
		}
	}

	if (ret == 0) { /* sessions exceeded */
		DBG_PRINT(INFO_DBG,"%s:All LRO sessions already in use\n",
			  __FUNCTION__);
		*lro = NULL;
		return ret;
	}

	switch (ret) {
		case 3:
			initiate_new_session(*lro, buffer, ip, tcph, *tcp_len);
			break;
		case 2:
			update_L3L4_header(sp, *lro);
			break;
		case 1:
			aggregate_new_rx(*lro, ip, tcph, *tcp_len);
			if ((*lro)->sg_num == sp->lro_max_aggr_per_sess) {
				update_L3L4_header(sp, *lro);
				ret = 4; /* Flush the LRO */
			}
			break;
		default:
			DBG_PRINT(ERR_DBG,"%s:Dont know, can't say!!\n",
				__FUNCTION__);
			break;
	}

	return ret;
}

static void clear_lro_session(struct lro *lro)
{
	static u16 lro_struct_size = sizeof(struct lro);

	memset(lro, 0, lro_struct_size);
}

static void queue_rx_frame(struct sk_buff *skb)
{
	struct net_device *dev = skb->dev;

	skb->protocol = eth_type_trans(skb, dev);
	if (napi)
		netif_receive_skb(skb);
	else
		netif_rx(skb);
}

static void lro_append_pkt(struct s2io_nic *sp, struct lro *lro,
			   struct sk_buff *skb,
			   u32 tcp_len)
{
	struct sk_buff *first = lro->parent;

	first->len += tcp_len;
	first->data_len = lro->frags_len;
	skb_pull(skb, (skb->len - tcp_len));
	if (skb_shinfo(first)->frag_list)
		lro->last_frag->next = skb;
	else
		skb_shinfo(first)->frag_list = skb;
	first->truesize += skb->truesize;
	lro->last_frag = skb;
	sp->mac_control.stats_info->sw_stat.clubbed_frms_cnt++;
	return;
}
