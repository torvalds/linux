/************************************************************************
 * s2io.c: A Linux PCI-X Ethernet driver for Neterion 10GbE Server NIC
 * Copyright(c) 2002-2010 Exar Corp.
 *
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
 * explanation of all the variables.
 *
 * rx_ring_num : This can be used to program the number of receive rings used
 * in the driver.
 * rx_ring_sz: This defines the number of receive blocks each ring can have.
 *     This is also an array of size 8.
 * rx_ring_mode: This defines the operation mode of all 8 rings. The valid
 *		values are 1, 2.
 * tx_fifo_num: This defines the number of Tx FIFOs thats used int the driver.
 * tx_fifo_len: This too is an array of 8. Each element defines the number of
 * Tx descriptors that can be associated with each corresponding FIFO.
 * intr_type: This defines the type of interrupt. The values can be 0(INTA),
 *     2(MSI_X). Default value is '2(MSI_X)'
 * lro_max_pkts: This parameter defines maximum number of packets can be
 *     aggregated as a single large packet
 * napi: This parameter used to enable/disable NAPI (polling Rx)
 *     Possible values '1' for enable and '0' for disable. Default is '1'
 * vlan_tag_strip: This can be used to enable or disable vlan stripping.
 *                 Possible values '1' for enable , '0' for disable.
 *                 Default is '2' - which means disable in promisc mode
 *                 and enable in non-promiscuous mode.
 * multiq: This parameter used to enable/disable MULTIQUEUE support.
 *      Possible values '1' for enable and '0' for disable. Default is '0'
 ************************************************************************/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/mdio.h>
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
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/prefetch.h>
#include <net/tcp.h>
#include <net/checksum.h>

#include <asm/div64.h>
#include <asm/irq.h>

/* local include */
#include "s2io.h"
#include "s2io-regs.h"

#define DRV_VERSION "2.0.26.28"

/* S2io Driver name & version. */
static const char s2io_driver_name[] = "Neterion";
static const char s2io_driver_version[] = DRV_VERSION;

static const int rxd_size[2] = {32, 48};
static const int rxd_count[2] = {127, 85};

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
#define CARDS_WITH_FAULTY_LINK_INDICATORS(dev_type, subid)		\
	(dev_type == XFRAME_I_DEVICE) ?					\
	((((subid >= 0x600B) && (subid <= 0x600D)) ||			\
	  ((subid >= 0x640B) && (subid <= 0x640D))) ? 1 : 0) : 0

#define LINK_IS_UP(val64) (!(val64 & (ADAPTER_STATUS_RMAC_REMOTE_FAULT | \
				      ADAPTER_STATUS_RMAC_LOCAL_FAULT)))

static inline int is_s2io_card_up(const struct s2io_nic *sp)
{
	return test_bit(__S2IO_STATE_CARD_UP, &sp->state);
}

/* Ethtool related variables and Macros. */
static const char s2io_gstrings[][ETH_GSTRING_LEN] = {
	"Register test\t(offline)",
	"Eeprom test\t(offline)",
	"Link test\t(online)",
	"RLDRAM test\t(offline)",
	"BIST Test\t(offline)"
};

static const char ethtool_xena_stats_keys[][ETH_GSTRING_LEN] = {
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

static const char ethtool_enhanced_stats_keys[][ETH_GSTRING_LEN] = {
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

static const char ethtool_driver_stats_keys[][ETH_GSTRING_LEN] = {
	{"\n DRIVER STATISTICS"},
	{"single_bit_ecc_errs"},
	{"double_bit_ecc_errs"},
	{"parity_err_cnt"},
	{"serious_err_cnt"},
	{"soft_reset_cnt"},
	{"fifo_full_cnt"},
	{"ring_0_full_cnt"},
	{"ring_1_full_cnt"},
	{"ring_2_full_cnt"},
	{"ring_3_full_cnt"},
	{"ring_4_full_cnt"},
	{"ring_5_full_cnt"},
	{"ring_6_full_cnt"},
	{"ring_7_full_cnt"},
	{"alarm_transceiver_temp_high"},
	{"alarm_transceiver_temp_low"},
	{"alarm_laser_bias_current_high"},
	{"alarm_laser_bias_current_low"},
	{"alarm_laser_output_power_high"},
	{"alarm_laser_output_power_low"},
	{"warn_transceiver_temp_high"},
	{"warn_transceiver_temp_low"},
	{"warn_laser_bias_current_high"},
	{"warn_laser_bias_current_low"},
	{"warn_laser_output_power_high"},
	{"warn_laser_output_power_low"},
	{"lro_aggregated_pkts"},
	{"lro_flush_both_count"},
	{"lro_out_of_sequence_pkts"},
	{"lro_flush_due_to_max_pkts"},
	{"lro_avg_aggr_pkts"},
	{"mem_alloc_fail_cnt"},
	{"pci_map_fail_cnt"},
	{"watchdog_timer_cnt"},
	{"mem_allocated"},
	{"mem_freed"},
	{"link_up_cnt"},
	{"link_down_cnt"},
	{"link_up_time"},
	{"link_down_time"},
	{"tx_tcode_buf_abort_cnt"},
	{"tx_tcode_desc_abort_cnt"},
	{"tx_tcode_parity_err_cnt"},
	{"tx_tcode_link_loss_cnt"},
	{"tx_tcode_list_proc_err_cnt"},
	{"rx_tcode_parity_err_cnt"},
	{"rx_tcode_abort_cnt"},
	{"rx_tcode_parity_abort_cnt"},
	{"rx_tcode_rda_fail_cnt"},
	{"rx_tcode_unkn_prot_cnt"},
	{"rx_tcode_fcs_err_cnt"},
	{"rx_tcode_buf_size_err_cnt"},
	{"rx_tcode_rxd_corrupt_cnt"},
	{"rx_tcode_unkn_err_cnt"},
	{"tda_err_cnt"},
	{"pfc_err_cnt"},
	{"pcc_err_cnt"},
	{"tti_err_cnt"},
	{"tpa_err_cnt"},
	{"sm_err_cnt"},
	{"lso_err_cnt"},
	{"mac_tmac_err_cnt"},
	{"mac_rmac_err_cnt"},
	{"xgxs_txgxs_err_cnt"},
	{"xgxs_rxgxs_err_cnt"},
	{"rc_err_cnt"},
	{"prc_pcix_err_cnt"},
	{"rpa_err_cnt"},
	{"rda_err_cnt"},
	{"rti_err_cnt"},
	{"mc_err_cnt"}
};

#define S2IO_XENA_STAT_LEN	ARRAY_SIZE(ethtool_xena_stats_keys)
#define S2IO_ENHANCED_STAT_LEN	ARRAY_SIZE(ethtool_enhanced_stats_keys)
#define S2IO_DRIVER_STAT_LEN	ARRAY_SIZE(ethtool_driver_stats_keys)

#define XFRAME_I_STAT_LEN (S2IO_XENA_STAT_LEN + S2IO_DRIVER_STAT_LEN)
#define XFRAME_II_STAT_LEN (XFRAME_I_STAT_LEN + S2IO_ENHANCED_STAT_LEN)

#define XFRAME_I_STAT_STRINGS_LEN (XFRAME_I_STAT_LEN * ETH_GSTRING_LEN)
#define XFRAME_II_STAT_STRINGS_LEN (XFRAME_II_STAT_LEN * ETH_GSTRING_LEN)

#define S2IO_TEST_LEN	ARRAY_SIZE(s2io_gstrings)
#define S2IO_STRINGS_LEN	(S2IO_TEST_LEN * ETH_GSTRING_LEN)

#define S2IO_TIMER_CONF(timer, handle, arg, exp)	\
	init_timer(&timer);				\
	timer.function = handle;			\
	timer.data = (unsigned long)arg;		\
	mod_timer(&timer, (jiffies + exp))		\

/* copy mac addr to def_mac_addr array */
static void do_s2io_copy_mac_addr(struct s2io_nic *sp, int offset, u64 mac_addr)
{
	sp->def_mac_addr[offset].mac_addr[5] = (u8) (mac_addr);
	sp->def_mac_addr[offset].mac_addr[4] = (u8) (mac_addr >> 8);
	sp->def_mac_addr[offset].mac_addr[3] = (u8) (mac_addr >> 16);
	sp->def_mac_addr[offset].mac_addr[2] = (u8) (mac_addr >> 24);
	sp->def_mac_addr[offset].mac_addr[1] = (u8) (mac_addr >> 32);
	sp->def_mac_addr[offset].mac_addr[0] = (u8) (mac_addr >> 40);
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
S2IO_PARM_INT(tx_fifo_num, FIFO_DEFAULT_NUM);
S2IO_PARM_INT(rx_ring_num, 1);
S2IO_PARM_INT(multiq, 0);
S2IO_PARM_INT(rx_ring_mode, 1);
S2IO_PARM_INT(use_continuous_tx_intrs, 1);
S2IO_PARM_INT(rmac_pause_time, 0x100);
S2IO_PARM_INT(mc_pause_threshold_q0q3, 187);
S2IO_PARM_INT(mc_pause_threshold_q4q7, 187);
S2IO_PARM_INT(shared_splits, 0);
S2IO_PARM_INT(tmac_util_period, 5);
S2IO_PARM_INT(rmac_util_period, 5);
S2IO_PARM_INT(l3l4hdr_size, 128);
/* 0 is no steering, 1 is Priority steering, 2 is Default steering */
S2IO_PARM_INT(tx_steering_type, TX_DEFAULT_STEERING);
/* Frequency of Rx desc syncs expressed as power of 2 */
S2IO_PARM_INT(rxsync_frequency, 3);
/* Interrupt type. Values can be 0(INTA), 2(MSI_X) */
S2IO_PARM_INT(intr_type, 2);
/* Large receive offload feature */

/* Max pkts to be aggregated by LRO at one time. If not specified,
 * aggregation happens until we hit max IP pkt size(64K)
 */
S2IO_PARM_INT(lro_max_pkts, 0xFFFF);
S2IO_PARM_INT(indicate_max_pkts, 0);

S2IO_PARM_INT(napi, 1);
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
static const struct pci_device_id s2io_tbl[] = {
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

static const struct pci_error_handlers s2io_err_handler = {
	.error_detected = s2io_io_error_detected,
	.slot_reset = s2io_io_slot_reset,
	.resume = s2io_io_resume,
};

static struct pci_driver s2io_driver = {
	.name = "S2IO",
	.id_table = s2io_tbl,
	.probe = s2io_init_nic,
	.remove = s2io_rem_nic,
	.err_handler = &s2io_err_handler,
};

/* A simplifier macro used both by init and free shared_mem Fns(). */
#define TXD_MEM_PAGE_CNT(len, per_each) ((len+per_each - 1) / per_each)

/* netqueue manipulation helper functions */
static inline void s2io_stop_all_tx_queue(struct s2io_nic *sp)
{
	if (!sp->config.multiq) {
		int i;

		for (i = 0; i < sp->config.tx_fifo_num; i++)
			sp->mac_control.fifos[i].queue_state = FIFO_QUEUE_STOP;
	}
	netif_tx_stop_all_queues(sp->dev);
}

static inline void s2io_stop_tx_queue(struct s2io_nic *sp, int fifo_no)
{
	if (!sp->config.multiq)
		sp->mac_control.fifos[fifo_no].queue_state =
			FIFO_QUEUE_STOP;

	netif_tx_stop_all_queues(sp->dev);
}

static inline void s2io_start_all_tx_queue(struct s2io_nic *sp)
{
	if (!sp->config.multiq) {
		int i;

		for (i = 0; i < sp->config.tx_fifo_num; i++)
			sp->mac_control.fifos[i].queue_state = FIFO_QUEUE_START;
	}
	netif_tx_start_all_queues(sp->dev);
}

static inline void s2io_wake_all_tx_queue(struct s2io_nic *sp)
{
	if (!sp->config.multiq) {
		int i;

		for (i = 0; i < sp->config.tx_fifo_num; i++)
			sp->mac_control.fifos[i].queue_state = FIFO_QUEUE_START;
	}
	netif_tx_wake_all_queues(sp->dev);
}

static inline void s2io_wake_tx_queue(
	struct fifo_info *fifo, int cnt, u8 multiq)
{

	if (multiq) {
		if (cnt && __netif_subqueue_stopped(fifo->dev, fifo->fifo_no))
			netif_wake_subqueue(fifo->dev, fifo->fifo_no);
	} else if (cnt && (fifo->queue_state == FIFO_QUEUE_STOP)) {
		if (netif_queue_stopped(fifo->dev)) {
			fifo->queue_state = FIFO_QUEUE_START;
			netif_wake_queue(fifo->dev);
		}
	}
}

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
	struct config_param *config = &nic->config;
	struct mac_info *mac_control = &nic->mac_control;
	unsigned long long mem_allocated = 0;

	/* Allocation and initialization of TXDLs in FIFOs */
	size = 0;
	for (i = 0; i < config->tx_fifo_num; i++) {
		struct tx_fifo_config *tx_cfg = &config->tx_cfg[i];

		size += tx_cfg->fifo_len;
	}
	if (size > MAX_AVAILABLE_TXDS) {
		DBG_PRINT(ERR_DBG,
			  "Too many TxDs requested: %d, max supported: %d\n",
			  size, MAX_AVAILABLE_TXDS);
		return -EINVAL;
	}

	size = 0;
	for (i = 0; i < config->tx_fifo_num; i++) {
		struct tx_fifo_config *tx_cfg = &config->tx_cfg[i];

		size = tx_cfg->fifo_len;
		/*
		 * Legal values are from 2 to 8192
		 */
		if (size < 2) {
			DBG_PRINT(ERR_DBG, "Fifo %d: Invalid length (%d) - "
				  "Valid lengths are 2 through 8192\n",
				  i, size);
			return -EINVAL;
		}
	}

	lst_size = (sizeof(struct TxD) * config->max_txds);
	lst_per_page = PAGE_SIZE / lst_size;

	for (i = 0; i < config->tx_fifo_num; i++) {
		struct fifo_info *fifo = &mac_control->fifos[i];
		struct tx_fifo_config *tx_cfg = &config->tx_cfg[i];
		int fifo_len = tx_cfg->fifo_len;
		int list_holder_size = fifo_len * sizeof(struct list_info_hold);

		fifo->list_info = kzalloc(list_holder_size, GFP_KERNEL);
		if (!fifo->list_info) {
			DBG_PRINT(INFO_DBG, "Malloc failed for list_info\n");
			return -ENOMEM;
		}
		mem_allocated += list_holder_size;
	}
	for (i = 0; i < config->tx_fifo_num; i++) {
		int page_num = TXD_MEM_PAGE_CNT(config->tx_cfg[i].fifo_len,
						lst_per_page);
		struct fifo_info *fifo = &mac_control->fifos[i];
		struct tx_fifo_config *tx_cfg = &config->tx_cfg[i];

		fifo->tx_curr_put_info.offset = 0;
		fifo->tx_curr_put_info.fifo_len = tx_cfg->fifo_len - 1;
		fifo->tx_curr_get_info.offset = 0;
		fifo->tx_curr_get_info.fifo_len = tx_cfg->fifo_len - 1;
		fifo->fifo_no = i;
		fifo->nic = nic;
		fifo->max_txds = MAX_SKB_FRAGS + 2;
		fifo->dev = dev;

		for (j = 0; j < page_num; j++) {
			int k = 0;
			dma_addr_t tmp_p;
			void *tmp_v;
			tmp_v = pci_alloc_consistent(nic->pdev,
						     PAGE_SIZE, &tmp_p);
			if (!tmp_v) {
				DBG_PRINT(INFO_DBG,
					  "pci_alloc_consistent failed for TxDL\n");
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
					  "%s: Zero DMA address for TxDL. "
					  "Virtual address %p\n",
					  dev->name, tmp_v);
				tmp_v = pci_alloc_consistent(nic->pdev,
							     PAGE_SIZE, &tmp_p);
				if (!tmp_v) {
					DBG_PRINT(INFO_DBG,
						  "pci_alloc_consistent failed for TxDL\n");
					return -ENOMEM;
				}
				mem_allocated += PAGE_SIZE;
			}
			while (k < lst_per_page) {
				int l = (j * lst_per_page) + k;
				if (l == tx_cfg->fifo_len)
					break;
				fifo->list_info[l].list_virt_addr =
					tmp_v + (k * lst_size);
				fifo->list_info[l].list_phy_addr =
					tmp_p + (k * lst_size);
				k++;
			}
		}
	}

	for (i = 0; i < config->tx_fifo_num; i++) {
		struct fifo_info *fifo = &mac_control->fifos[i];
		struct tx_fifo_config *tx_cfg = &config->tx_cfg[i];

		size = tx_cfg->fifo_len;
		fifo->ufo_in_band_v = kcalloc(size, sizeof(u64), GFP_KERNEL);
		if (!fifo->ufo_in_band_v)
			return -ENOMEM;
		mem_allocated += (size * sizeof(u64));
	}

	/* Allocation and initialization of RXDs in Rings */
	size = 0;
	for (i = 0; i < config->rx_ring_num; i++) {
		struct rx_ring_config *rx_cfg = &config->rx_cfg[i];
		struct ring_info *ring = &mac_control->rings[i];

		if (rx_cfg->num_rxd % (rxd_count[nic->rxd_mode] + 1)) {
			DBG_PRINT(ERR_DBG, "%s: Ring%d RxD count is not a "
				  "multiple of RxDs per Block\n",
				  dev->name, i);
			return FAILURE;
		}
		size += rx_cfg->num_rxd;
		ring->block_count = rx_cfg->num_rxd /
			(rxd_count[nic->rxd_mode] + 1);
		ring->pkt_cnt = rx_cfg->num_rxd - ring->block_count;
	}
	if (nic->rxd_mode == RXD_MODE_1)
		size = (size * (sizeof(struct RxD1)));
	else
		size = (size * (sizeof(struct RxD3)));

	for (i = 0; i < config->rx_ring_num; i++) {
		struct rx_ring_config *rx_cfg = &config->rx_cfg[i];
		struct ring_info *ring = &mac_control->rings[i];

		ring->rx_curr_get_info.block_index = 0;
		ring->rx_curr_get_info.offset = 0;
		ring->rx_curr_get_info.ring_len = rx_cfg->num_rxd - 1;
		ring->rx_curr_put_info.block_index = 0;
		ring->rx_curr_put_info.offset = 0;
		ring->rx_curr_put_info.ring_len = rx_cfg->num_rxd - 1;
		ring->nic = nic;
		ring->ring_no = i;

		blk_cnt = rx_cfg->num_rxd / (rxd_count[nic->rxd_mode] + 1);
		/*  Allocating all the Rx blocks */
		for (j = 0; j < blk_cnt; j++) {
			struct rx_block_info *rx_blocks;
			int l;

			rx_blocks = &ring->rx_blocks[j];
			size = SIZE_OF_BLOCK;	/* size is always page size */
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

			size = sizeof(struct rxd_info) *
				rxd_count[nic->rxd_mode];
			rx_blocks->block_virt_addr = tmp_v_addr;
			rx_blocks->block_dma_addr = tmp_p_addr;
			rx_blocks->rxds = kmalloc(size,  GFP_KERNEL);
			if (!rx_blocks->rxds)
				return -ENOMEM;
			mem_allocated += size;
			for (l = 0; l < rxd_count[nic->rxd_mode]; l++) {
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
			int next = (j + 1) % blk_cnt;
			tmp_v_addr = ring->rx_blocks[j].block_virt_addr;
			tmp_v_addr_next = ring->rx_blocks[next].block_virt_addr;
			tmp_p_addr = ring->rx_blocks[j].block_dma_addr;
			tmp_p_addr_next = ring->rx_blocks[next].block_dma_addr;

			pre_rxd_blk = tmp_v_addr;
			pre_rxd_blk->reserved_2_pNext_RxD_block =
				(unsigned long)tmp_v_addr_next;
			pre_rxd_blk->pNext_RxD_Blk_physical =
				(u64)tmp_p_addr_next;
		}
	}
	if (nic->rxd_mode == RXD_MODE_3B) {
		/*
		 * Allocation of Storages for buffer addresses in 2BUFF mode
		 * and the buffers as well.
		 */
		for (i = 0; i < config->rx_ring_num; i++) {
			struct rx_ring_config *rx_cfg = &config->rx_cfg[i];
			struct ring_info *ring = &mac_control->rings[i];

			blk_cnt = rx_cfg->num_rxd /
				(rxd_count[nic->rxd_mode] + 1);
			size = sizeof(struct buffAdd *) * blk_cnt;
			ring->ba = kmalloc(size, GFP_KERNEL);
			if (!ring->ba)
				return -ENOMEM;
			mem_allocated += size;
			for (j = 0; j < blk_cnt; j++) {
				int k = 0;

				size = sizeof(struct buffAdd) *
					(rxd_count[nic->rxd_mode] + 1);
				ring->ba[j] = kmalloc(size, GFP_KERNEL);
				if (!ring->ba[j])
					return -ENOMEM;
				mem_allocated += size;
				while (k != rxd_count[nic->rxd_mode]) {
					ba = &ring->ba[j][k];
					size = BUF0_LEN + ALIGN_SIZE;
					ba->ba_0_org = kmalloc(size, GFP_KERNEL);
					if (!ba->ba_0_org)
						return -ENOMEM;
					mem_allocated += size;
					tmp = (unsigned long)ba->ba_0_org;
					tmp += ALIGN_SIZE;
					tmp &= ~((unsigned long)ALIGN_SIZE);
					ba->ba_0 = (void *)tmp;

					size = BUF1_LEN + ALIGN_SIZE;
					ba->ba_1_org = kmalloc(size, GFP_KERNEL);
					if (!ba->ba_1_org)
						return -ENOMEM;
					mem_allocated += size;
					tmp = (unsigned long)ba->ba_1_org;
					tmp += ALIGN_SIZE;
					tmp &= ~((unsigned long)ALIGN_SIZE);
					ba->ba_1 = (void *)tmp;
					k++;
				}
			}
		}
	}

	/* Allocation and initialization of Statistics block */
	size = sizeof(struct stat_block);
	mac_control->stats_mem =
		pci_alloc_consistent(nic->pdev, size,
				     &mac_control->stats_mem_phy);

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
	mac_control->stats_info = tmp_v_addr;
	memset(tmp_v_addr, 0, size);
	DBG_PRINT(INIT_DBG, "%s: Ring Mem PHY: 0x%llx\n",
		dev_name(&nic->pdev->dev), (unsigned long long)tmp_p_addr);
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
	void *tmp_v_addr;
	dma_addr_t tmp_p_addr;
	int lst_size, lst_per_page;
	struct net_device *dev;
	int page_num = 0;
	struct config_param *config;
	struct mac_info *mac_control;
	struct stat_block *stats;
	struct swStat *swstats;

	if (!nic)
		return;

	dev = nic->dev;

	config = &nic->config;
	mac_control = &nic->mac_control;
	stats = mac_control->stats_info;
	swstats = &stats->sw_stat;

	lst_size = sizeof(struct TxD) * config->max_txds;
	lst_per_page = PAGE_SIZE / lst_size;

	for (i = 0; i < config->tx_fifo_num; i++) {
		struct fifo_info *fifo = &mac_control->fifos[i];
		struct tx_fifo_config *tx_cfg = &config->tx_cfg[i];

		page_num = TXD_MEM_PAGE_CNT(tx_cfg->fifo_len, lst_per_page);
		for (j = 0; j < page_num; j++) {
			int mem_blks = (j * lst_per_page);
			struct list_info_hold *fli;

			if (!fifo->list_info)
				return;

			fli = &fifo->list_info[mem_blks];
			if (!fli->list_virt_addr)
				break;
			pci_free_consistent(nic->pdev, PAGE_SIZE,
					    fli->list_virt_addr,
					    fli->list_phy_addr);
			swstats->mem_freed += PAGE_SIZE;
		}
		/* If we got a zero DMA address during allocation,
		 * free the page now
		 */
		if (mac_control->zerodma_virt_addr) {
			pci_free_consistent(nic->pdev, PAGE_SIZE,
					    mac_control->zerodma_virt_addr,
					    (dma_addr_t)0);
			DBG_PRINT(INIT_DBG,
				  "%s: Freeing TxDL with zero DMA address. "
				  "Virtual address %p\n",
				  dev->name, mac_control->zerodma_virt_addr);
			swstats->mem_freed += PAGE_SIZE;
		}
		kfree(fifo->list_info);
		swstats->mem_freed += tx_cfg->fifo_len *
			sizeof(struct list_info_hold);
	}

	size = SIZE_OF_BLOCK;
	for (i = 0; i < config->rx_ring_num; i++) {
		struct ring_info *ring = &mac_control->rings[i];

		blk_cnt = ring->block_count;
		for (j = 0; j < blk_cnt; j++) {
			tmp_v_addr = ring->rx_blocks[j].block_virt_addr;
			tmp_p_addr = ring->rx_blocks[j].block_dma_addr;
			if (tmp_v_addr == NULL)
				break;
			pci_free_consistent(nic->pdev, size,
					    tmp_v_addr, tmp_p_addr);
			swstats->mem_freed += size;
			kfree(ring->rx_blocks[j].rxds);
			swstats->mem_freed += sizeof(struct rxd_info) *
				rxd_count[nic->rxd_mode];
		}
	}

	if (nic->rxd_mode == RXD_MODE_3B) {
		/* Freeing buffer storage addresses in 2BUFF mode. */
		for (i = 0; i < config->rx_ring_num; i++) {
			struct rx_ring_config *rx_cfg = &config->rx_cfg[i];
			struct ring_info *ring = &mac_control->rings[i];

			blk_cnt = rx_cfg->num_rxd /
				(rxd_count[nic->rxd_mode] + 1);
			for (j = 0; j < blk_cnt; j++) {
				int k = 0;
				if (!ring->ba[j])
					continue;
				while (k != rxd_count[nic->rxd_mode]) {
					struct buffAdd *ba = &ring->ba[j][k];
					kfree(ba->ba_0_org);
					swstats->mem_freed +=
						BUF0_LEN + ALIGN_SIZE;
					kfree(ba->ba_1_org);
					swstats->mem_freed +=
						BUF1_LEN + ALIGN_SIZE;
					k++;
				}
				kfree(ring->ba[j]);
				swstats->mem_freed += sizeof(struct buffAdd) *
					(rxd_count[nic->rxd_mode] + 1);
			}
			kfree(ring->ba);
			swstats->mem_freed += sizeof(struct buffAdd *) *
				blk_cnt;
		}
	}

	for (i = 0; i < nic->config.tx_fifo_num; i++) {
		struct fifo_info *fifo = &mac_control->fifos[i];
		struct tx_fifo_config *tx_cfg = &config->tx_cfg[i];

		if (fifo->ufo_in_band_v) {
			swstats->mem_freed += tx_cfg->fifo_len *
				sizeof(u64);
			kfree(fifo->ufo_in_band_v);
		}
	}

	if (mac_control->stats_mem) {
		swstats->mem_freed += mac_control->stats_mem_sz;
		pci_free_consistent(nic->pdev,
				    mac_control->stats_mem_sz,
				    mac_control->stats_mem,
				    mac_control->stats_mem_phy);
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

	if (val64 & PCI_MODE_UNKNOWN_MODE)
		return -1;      /* Unknown PCI mode */
	return mode;
}

#define NEC_VENID   0x1033
#define NEC_DEVID   0x0125
static int s2io_on_nec_bridge(struct pci_dev *s2io_pdev)
{
	struct pci_dev *tdev = NULL;
	for_each_pci_dev(tdev) {
		if (tdev->vendor == NEC_VENID && tdev->device == NEC_DEVID) {
			if (tdev->bus == s2io_pdev->bus->parent) {
				pci_dev_put(tdev);
				return 1;
			}
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
	const char *pcimode;

	val64 = readq(&bar0->pci_mode);
	mode = (u8)GET_PCI_MODE(val64);

	if (val64 & PCI_MODE_UNKNOWN_MODE)
		return -1;	/* Unknown PCI mode */

	config->bus_speed = bus_speed[mode];

	if (s2io_on_nec_bridge(nic->pdev)) {
		DBG_PRINT(ERR_DBG, "%s: Device is on PCI-E bus\n",
			  nic->dev->name);
		return mode;
	}

	switch (mode) {
	case PCI_MODE_PCI_33:
		pcimode = "33MHz PCI bus";
		break;
	case PCI_MODE_PCI_66:
		pcimode = "66MHz PCI bus";
		break;
	case PCI_MODE_PCIX_M1_66:
		pcimode = "66MHz PCIX(M1) bus";
		break;
	case PCI_MODE_PCIX_M1_100:
		pcimode = "100MHz PCIX(M1) bus";
		break;
	case PCI_MODE_PCIX_M1_133:
		pcimode = "133MHz PCIX(M1) bus";
		break;
	case PCI_MODE_PCIX_M2_66:
		pcimode = "133MHz PCIX(M2) bus";
		break;
	case PCI_MODE_PCIX_M2_100:
		pcimode = "200MHz PCIX(M2) bus";
		break;
	case PCI_MODE_PCIX_M2_133:
		pcimode = "266MHz PCIX(M2) bus";
		break;
	default:
		pcimode = "unsupported bus!";
		mode = -1;
	}

	DBG_PRINT(ERR_DBG, "%s: Device is on %d bit %s\n",
		  nic->dev->name, val64 & PCI_MODE_32_BITS ? 32 : 64, pcimode);

	return mode;
}

/**
 *  init_tti - Initialization transmit traffic interrupt scheme
 *  @nic: device private variable
 *  @link: link status (UP/DOWN) used to enable/disable continuous
 *  transmit interrupts
 *  Description: The function configures transmit traffic interrupts
 *  Return Value:  SUCCESS on success and
 *  '-1' on failure
 */

static int init_tti(struct s2io_nic *nic, int link)
{
	struct XENA_dev_config __iomem *bar0 = nic->bar0;
	register u64 val64 = 0;
	int i;
	struct config_param *config = &nic->config;

	for (i = 0; i < config->tx_fifo_num; i++) {
		/*
		 * TTI Initialization. Default Tx timer gets us about
		 * 250 interrupts per sec. Continuous interrupts are enabled
		 * by default.
		 */
		if (nic->device_type == XFRAME_II_DEVICE) {
			int count = (nic->config.bus_speed * 125)/2;
			val64 = TTI_DATA1_MEM_TX_TIMER_VAL(count);
		} else
			val64 = TTI_DATA1_MEM_TX_TIMER_VAL(0x2078);

		val64 |= TTI_DATA1_MEM_TX_URNG_A(0xA) |
			TTI_DATA1_MEM_TX_URNG_B(0x10) |
			TTI_DATA1_MEM_TX_URNG_C(0x30) |
			TTI_DATA1_MEM_TX_TIMER_AC_EN;
		if (i == 0)
			if (use_continuous_tx_intrs && (link == LINK_UP))
				val64 |= TTI_DATA1_MEM_TX_TIMER_CI_EN;
		writeq(val64, &bar0->tti_data1_mem);

		if (nic->config.intr_type == MSI_X) {
			val64 = TTI_DATA2_MEM_TX_UFC_A(0x10) |
				TTI_DATA2_MEM_TX_UFC_B(0x100) |
				TTI_DATA2_MEM_TX_UFC_C(0x200) |
				TTI_DATA2_MEM_TX_UFC_D(0x300);
		} else {
			if ((nic->config.tx_steering_type ==
			     TX_DEFAULT_STEERING) &&
			    (config->tx_fifo_num > 1) &&
			    (i >= nic->udp_fifo_idx) &&
			    (i < (nic->udp_fifo_idx +
				  nic->total_udp_fifos)))
				val64 = TTI_DATA2_MEM_TX_UFC_A(0x50) |
					TTI_DATA2_MEM_TX_UFC_B(0x80) |
					TTI_DATA2_MEM_TX_UFC_C(0x100) |
					TTI_DATA2_MEM_TX_UFC_D(0x120);
			else
				val64 = TTI_DATA2_MEM_TX_UFC_A(0x10) |
					TTI_DATA2_MEM_TX_UFC_B(0x20) |
					TTI_DATA2_MEM_TX_UFC_C(0x40) |
					TTI_DATA2_MEM_TX_UFC_D(0x80);
		}

		writeq(val64, &bar0->tti_data2_mem);

		val64 = TTI_CMD_MEM_WE |
			TTI_CMD_MEM_STROBE_NEW_CMD |
			TTI_CMD_MEM_OFFSET(i);
		writeq(val64, &bar0->tti_command_mem);

		if (wait_for_cmd_complete(&bar0->tti_command_mem,
					  TTI_CMD_MEM_STROBE_NEW_CMD,
					  S2IO_BIT_RESET) != SUCCESS)
			return FAILURE;
	}

	return SUCCESS;
}

/**
 *  init_nic - Initialization of hardware
 *  @nic: device private variable
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
	int dtx_cnt = 0;
	unsigned long long mem_share;
	int mem_size;
	struct config_param *config = &nic->config;
	struct mac_info *mac_control = &nic->mac_control;

	/* to set the swapper controle on the card */
	if (s2io_set_swapper(nic)) {
		DBG_PRINT(ERR_DBG, "ERROR: Setting Swapper failed\n");
		return -EIO;
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

	/* Ensure that it's safe to access registers by checking
	 * RIC_RUNNING bit is reset. Check is valid only for XframeII.
	 */
	if (nic->device_type == XFRAME_II_DEVICE) {
		for (i = 0; i < 50; i++) {
			val64 = readq(&bar0->adapter_status);
			if (!(val64 & ADAPTER_STATUS_RIC_RUNNING))
				break;
			msleep(10);
		}
		if (i == 50)
			return -ENODEV;
	}

	/*  Enable Receiving broadcasts */
	add = &bar0->mac_cfg;
	val64 = readq(&bar0->mac_cfg);
	val64 |= MAC_RMAC_BCAST_ENABLE;
	writeq(RMAC_CFG_KEY(0x4C0D), &bar0->rmac_cfg_key);
	writel((u32)val64, add);
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
		struct tx_fifo_config *tx_cfg = &config->tx_cfg[i];

		val64 |= vBIT(tx_cfg->fifo_len - 1, ((j * 32) + 19), 13) |
			vBIT(tx_cfg->fifo_priority, ((j * 32) + 5), 3);

		if (i == (config->tx_fifo_num - 1)) {
			if (i % 2 == 0)
				i++;
		}

		switch (i) {
		case 1:
			writeq(val64, &bar0->tx_fifo_partition_0);
			val64 = 0;
			j = 0;
			break;
		case 3:
			writeq(val64, &bar0->tx_fifo_partition_1);
			val64 = 0;
			j = 0;
			break;
		case 5:
			writeq(val64, &bar0->tx_fifo_partition_2);
			val64 = 0;
			j = 0;
			break;
		case 7:
			writeq(val64, &bar0->tx_fifo_partition_3);
			val64 = 0;
			j = 0;
			break;
		default:
			j++;
			break;
		}
	}

	/*
	 * Disable 4 PCCs for Xena1, 2 and 3 as per H/W bug
	 * SXE-008 TRANSMIT DMA ARBITRATION ISSUE.
	 */
	if ((nic->device_type == XFRAME_I_DEVICE) && (nic->pdev->revision < 4))
		writeq(PCC_ENABLE_FOUR, &bar0->pcc_enable);

	val64 = readq(&bar0->tx_fifo_partition_0);
	DBG_PRINT(INIT_DBG, "Fifo partition at: 0x%p is: 0x%llx\n",
		  &bar0->tx_fifo_partition_0, (unsigned long long)val64);

	/*
	 * Initialization of Tx_PA_CONFIG register to ignore packet
	 * integrity checking.
	 */
	val64 = readq(&bar0->tx_pa_cfg);
	val64 |= TX_PA_CFG_IGNORE_FRM_ERR |
		TX_PA_CFG_IGNORE_SNAP_OUI |
		TX_PA_CFG_IGNORE_LLC_CTRL |
		TX_PA_CFG_IGNORE_L2_ERR;
	writeq(val64, &bar0->tx_pa_cfg);

	/* Rx DMA initialization. */
	val64 = 0;
	for (i = 0; i < config->rx_ring_num; i++) {
		struct rx_ring_config *rx_cfg = &config->rx_cfg[i];

		val64 |= vBIT(rx_cfg->ring_priority, (5 + (i * 8)), 3);
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
	 * as per the number of FIFOs for equal scheduling priority
	 */
	switch (config->tx_fifo_num) {
	case 1:
		val64 = 0x0;
		writeq(val64, &bar0->tx_w_round_robin_0);
		writeq(val64, &bar0->tx_w_round_robin_1);
		writeq(val64, &bar0->tx_w_round_robin_2);
		writeq(val64, &bar0->tx_w_round_robin_3);
		writeq(val64, &bar0->tx_w_round_robin_4);
		break;
	case 2:
		val64 = 0x0001000100010001ULL;
		writeq(val64, &bar0->tx_w_round_robin_0);
		writeq(val64, &bar0->tx_w_round_robin_1);
		writeq(val64, &bar0->tx_w_round_robin_2);
		writeq(val64, &bar0->tx_w_round_robin_3);
		val64 = 0x0001000100000000ULL;
		writeq(val64, &bar0->tx_w_round_robin_4);
		break;
	case 3:
		val64 = 0x0001020001020001ULL;
		writeq(val64, &bar0->tx_w_round_robin_0);
		val64 = 0x0200010200010200ULL;
		writeq(val64, &bar0->tx_w_round_robin_1);
		val64 = 0x0102000102000102ULL;
		writeq(val64, &bar0->tx_w_round_robin_2);
		val64 = 0x0001020001020001ULL;
		writeq(val64, &bar0->tx_w_round_robin_3);
		val64 = 0x0200010200000000ULL;
		writeq(val64, &bar0->tx_w_round_robin_4);
		break;
	case 4:
		val64 = 0x0001020300010203ULL;
		writeq(val64, &bar0->tx_w_round_robin_0);
		writeq(val64, &bar0->tx_w_round_robin_1);
		writeq(val64, &bar0->tx_w_round_robin_2);
		writeq(val64, &bar0->tx_w_round_robin_3);
		val64 = 0x0001020300000000ULL;
		writeq(val64, &bar0->tx_w_round_robin_4);
		break;
	case 5:
		val64 = 0x0001020304000102ULL;
		writeq(val64, &bar0->tx_w_round_robin_0);
		val64 = 0x0304000102030400ULL;
		writeq(val64, &bar0->tx_w_round_robin_1);
		val64 = 0x0102030400010203ULL;
		writeq(val64, &bar0->tx_w_round_robin_2);
		val64 = 0x0400010203040001ULL;
		writeq(val64, &bar0->tx_w_round_robin_3);
		val64 = 0x0203040000000000ULL;
		writeq(val64, &bar0->tx_w_round_robin_4);
		break;
	case 6:
		val64 = 0x0001020304050001ULL;
		writeq(val64, &bar0->tx_w_round_robin_0);
		val64 = 0x0203040500010203ULL;
		writeq(val64, &bar0->tx_w_round_robin_1);
		val64 = 0x0405000102030405ULL;
		writeq(val64, &bar0->tx_w_round_robin_2);
		val64 = 0x0001020304050001ULL;
		writeq(val64, &bar0->tx_w_round_robin_3);
		val64 = 0x0203040500000000ULL;
		writeq(val64, &bar0->tx_w_round_robin_4);
		break;
	case 7:
		val64 = 0x0001020304050600ULL;
		writeq(val64, &bar0->tx_w_round_robin_0);
		val64 = 0x0102030405060001ULL;
		writeq(val64, &bar0->tx_w_round_robin_1);
		val64 = 0x0203040506000102ULL;
		writeq(val64, &bar0->tx_w_round_robin_2);
		val64 = 0x0304050600010203ULL;
		writeq(val64, &bar0->tx_w_round_robin_3);
		val64 = 0x0405060000000000ULL;
		writeq(val64, &bar0->tx_w_round_robin_4);
		break;
	case 8:
		val64 = 0x0001020304050607ULL;
		writeq(val64, &bar0->tx_w_round_robin_0);
		writeq(val64, &bar0->tx_w_round_robin_1);
		writeq(val64, &bar0->tx_w_round_robin_2);
		writeq(val64, &bar0->tx_w_round_robin_3);
		val64 = 0x0001020300000000ULL;
		writeq(val64, &bar0->tx_w_round_robin_4);
		break;
	}

	/* Enable all configured Tx FIFO partitions */
	val64 = readq(&bar0->tx_fifo_partition_0);
	val64 |= (TX_FIFO_PARTITION_EN);
	writeq(val64, &bar0->tx_fifo_partition_0);

	/* Filling the Rx round robin registers as per the
	 * number of Rings and steering based on QoS with
	 * equal priority.
	 */
	switch (config->rx_ring_num) {
	case 1:
		val64 = 0x0;
		writeq(val64, &bar0->rx_w_round_robin_0);
		writeq(val64, &bar0->rx_w_round_robin_1);
		writeq(val64, &bar0->rx_w_round_robin_2);
		writeq(val64, &bar0->rx_w_round_robin_3);
		writeq(val64, &bar0->rx_w_round_robin_4);

		val64 = 0x8080808080808080ULL;
		writeq(val64, &bar0->rts_qos_steering);
		break;
	case 2:
		val64 = 0x0001000100010001ULL;
		writeq(val64, &bar0->rx_w_round_robin_0);
		writeq(val64, &bar0->rx_w_round_robin_1);
		writeq(val64, &bar0->rx_w_round_robin_2);
		writeq(val64, &bar0->rx_w_round_robin_3);
		val64 = 0x0001000100000000ULL;
		writeq(val64, &bar0->rx_w_round_robin_4);

		val64 = 0x8080808040404040ULL;
		writeq(val64, &bar0->rts_qos_steering);
		break;
	case 3:
		val64 = 0x0001020001020001ULL;
		writeq(val64, &bar0->rx_w_round_robin_0);
		val64 = 0x0200010200010200ULL;
		writeq(val64, &bar0->rx_w_round_robin_1);
		val64 = 0x0102000102000102ULL;
		writeq(val64, &bar0->rx_w_round_robin_2);
		val64 = 0x0001020001020001ULL;
		writeq(val64, &bar0->rx_w_round_robin_3);
		val64 = 0x0200010200000000ULL;
		writeq(val64, &bar0->rx_w_round_robin_4);

		val64 = 0x8080804040402020ULL;
		writeq(val64, &bar0->rts_qos_steering);
		break;
	case 4:
		val64 = 0x0001020300010203ULL;
		writeq(val64, &bar0->rx_w_round_robin_0);
		writeq(val64, &bar0->rx_w_round_robin_1);
		writeq(val64, &bar0->rx_w_round_robin_2);
		writeq(val64, &bar0->rx_w_round_robin_3);
		val64 = 0x0001020300000000ULL;
		writeq(val64, &bar0->rx_w_round_robin_4);

		val64 = 0x8080404020201010ULL;
		writeq(val64, &bar0->rts_qos_steering);
		break;
	case 5:
		val64 = 0x0001020304000102ULL;
		writeq(val64, &bar0->rx_w_round_robin_0);
		val64 = 0x0304000102030400ULL;
		writeq(val64, &bar0->rx_w_round_robin_1);
		val64 = 0x0102030400010203ULL;
		writeq(val64, &bar0->rx_w_round_robin_2);
		val64 = 0x0400010203040001ULL;
		writeq(val64, &bar0->rx_w_round_robin_3);
		val64 = 0x0203040000000000ULL;
		writeq(val64, &bar0->rx_w_round_robin_4);

		val64 = 0x8080404020201008ULL;
		writeq(val64, &bar0->rts_qos_steering);
		break;
	case 6:
		val64 = 0x0001020304050001ULL;
		writeq(val64, &bar0->rx_w_round_robin_0);
		val64 = 0x0203040500010203ULL;
		writeq(val64, &bar0->rx_w_round_robin_1);
		val64 = 0x0405000102030405ULL;
		writeq(val64, &bar0->rx_w_round_robin_2);
		val64 = 0x0001020304050001ULL;
		writeq(val64, &bar0->rx_w_round_robin_3);
		val64 = 0x0203040500000000ULL;
		writeq(val64, &bar0->rx_w_round_robin_4);

		val64 = 0x8080404020100804ULL;
		writeq(val64, &bar0->rts_qos_steering);
		break;
	case 7:
		val64 = 0x0001020304050600ULL;
		writeq(val64, &bar0->rx_w_round_robin_0);
		val64 = 0x0102030405060001ULL;
		writeq(val64, &bar0->rx_w_round_robin_1);
		val64 = 0x0203040506000102ULL;
		writeq(val64, &bar0->rx_w_round_robin_2);
		val64 = 0x0304050600010203ULL;
		writeq(val64, &bar0->rx_w_round_robin_3);
		val64 = 0x0405060000000000ULL;
		writeq(val64, &bar0->rx_w_round_robin_4);

		val64 = 0x8080402010080402ULL;
		writeq(val64, &bar0->rts_qos_steering);
		break;
	case 8:
		val64 = 0x0001020304050607ULL;
		writeq(val64, &bar0->rx_w_round_robin_0);
		writeq(val64, &bar0->rx_w_round_robin_1);
		writeq(val64, &bar0->rx_w_round_robin_2);
		writeq(val64, &bar0->rx_w_round_robin_3);
		val64 = 0x0001020300000000ULL;
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
			DBG_PRINT(ERR_DBG,
				  "%s: rts_ds_steer failed on codepoint %d\n",
				  dev->name, i);
			return -ENODEV;
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

	/* Initialize TTI */
	if (SUCCESS != init_tti(nic, nic->last_link_state))
		return -ENODEV;

	/* RTI Initialization */
	if (nic->device_type == XFRAME_II_DEVICE) {
		/*
		 * Programmed to generate Apprx 500 Intrs per
		 * second
		 */
		int count = (nic->config.bus_speed * 125)/4;
		val64 = RTI_DATA1_MEM_RX_TIMER_VAL(count);
	} else
		val64 = RTI_DATA1_MEM_RX_TIMER_VAL(0xFFF);
	val64 |= RTI_DATA1_MEM_RX_URNG_A(0xA) |
		RTI_DATA1_MEM_RX_URNG_B(0x10) |
		RTI_DATA1_MEM_RX_URNG_C(0x30) |
		RTI_DATA1_MEM_RX_TIMER_AC_EN;

	writeq(val64, &bar0->rti_data1_mem);

	val64 = RTI_DATA2_MEM_RX_UFC_A(0x1) |
		RTI_DATA2_MEM_RX_UFC_B(0x2) ;
	if (nic->config.intr_type == MSI_X)
		val64 |= (RTI_DATA2_MEM_RX_UFC_C(0x20) |
			  RTI_DATA2_MEM_RX_UFC_D(0x40));
	else
		val64 |= (RTI_DATA2_MEM_RX_UFC_C(0x40) |
			  RTI_DATA2_MEM_RX_UFC_D(0x80));
	writeq(val64, &bar0->rti_data2_mem);

	for (i = 0; i < config->rx_ring_num; i++) {
		val64 = RTI_CMD_MEM_WE |
			RTI_CMD_MEM_STROBE_NEW_CMD |
			RTI_CMD_MEM_OFFSET(i);
		writeq(val64, &bar0->rti_command_mem);

		/*
		 * Once the operation completes, the Strobe bit of the
		 * command register will be reset. We poll for this
		 * particular condition. We wait for a maximum of 500ms
		 * for the operation to complete, if it's not complete
		 * by then we return error.
		 */
		time = 0;
		while (true) {
			val64 = readq(&bar0->rti_command_mem);
			if (!(val64 & RTI_CMD_MEM_STROBE_NEW_CMD))
				break;

			if (time > 10) {
				DBG_PRINT(ERR_DBG, "%s: RTI init failed\n",
					  dev->name);
				return -ENODEV;
			}
			time++;
			msleep(50);
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
		val64 |= (((u64)0xFF00 |
			   nic->mac_control.mc_pause_threshold_q0q3)
			  << (i * 2 * 8));
	}
	writeq(val64, &bar0->mc_pause_thresh_q0q3);

	val64 = 0;
	for (i = 0; i < 4; i++) {
		val64 |= (((u64)0xFF00 |
			   nic->mac_control.mc_pause_threshold_q4q7)
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
		val64 &= ~(s2BIT(13)|s2BIT(14)|s2BIT(15));
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
	if (nic->device_type == XFRAME_II_DEVICE)
		return LINK_UP_DOWN_INTERRUPT;
	else
		return MAC_RMAC_ERR_TIMER;
}

/**
 *  do_s2io_write_bits -  update alarm bits in alarm register
 *  @value: alarm bits
 *  @flag: interrupt status
 *  @addr: address value
 *  Description: update alarm bits in alarm register
 *  Return Value:
 *  NONE.
 */
static void do_s2io_write_bits(u64 value, int flag, void __iomem *addr)
{
	u64 temp64;

	temp64 = readq(addr);

	if (flag == ENABLE_INTRS)
		temp64 &= ~((u64)value);
	else
		temp64 |= ((u64)value);
	writeq(temp64, addr);
}

static void en_dis_err_alarms(struct s2io_nic *nic, u16 mask, int flag)
{
	struct XENA_dev_config __iomem *bar0 = nic->bar0;
	register u64 gen_int_mask = 0;
	u64 interruptible;

	writeq(DISABLE_ALL_INTRS, &bar0->general_int_mask);
	if (mask & TX_DMA_INTR) {
		gen_int_mask |= TXDMA_INT_M;

		do_s2io_write_bits(TXDMA_TDA_INT | TXDMA_PFC_INT |
				   TXDMA_PCC_INT | TXDMA_TTI_INT |
				   TXDMA_LSO_INT | TXDMA_TPA_INT |
				   TXDMA_SM_INT, flag, &bar0->txdma_int_mask);

		do_s2io_write_bits(PFC_ECC_DB_ERR | PFC_SM_ERR_ALARM |
				   PFC_MISC_0_ERR | PFC_MISC_1_ERR |
				   PFC_PCIX_ERR | PFC_ECC_SG_ERR, flag,
				   &bar0->pfc_err_mask);

		do_s2io_write_bits(TDA_Fn_ECC_DB_ERR | TDA_SM0_ERR_ALARM |
				   TDA_SM1_ERR_ALARM | TDA_Fn_ECC_SG_ERR |
				   TDA_PCIX_ERR, flag, &bar0->tda_err_mask);

		do_s2io_write_bits(PCC_FB_ECC_DB_ERR | PCC_TXB_ECC_DB_ERR |
				   PCC_SM_ERR_ALARM | PCC_WR_ERR_ALARM |
				   PCC_N_SERR | PCC_6_COF_OV_ERR |
				   PCC_7_COF_OV_ERR | PCC_6_LSO_OV_ERR |
				   PCC_7_LSO_OV_ERR | PCC_FB_ECC_SG_ERR |
				   PCC_TXB_ECC_SG_ERR,
				   flag, &bar0->pcc_err_mask);

		do_s2io_write_bits(TTI_SM_ERR_ALARM | TTI_ECC_SG_ERR |
				   TTI_ECC_DB_ERR, flag, &bar0->tti_err_mask);

		do_s2io_write_bits(LSO6_ABORT | LSO7_ABORT |
				   LSO6_SM_ERR_ALARM | LSO7_SM_ERR_ALARM |
				   LSO6_SEND_OFLOW | LSO7_SEND_OFLOW,
				   flag, &bar0->lso_err_mask);

		do_s2io_write_bits(TPA_SM_ERR_ALARM | TPA_TX_FRM_DROP,
				   flag, &bar0->tpa_err_mask);

		do_s2io_write_bits(SM_SM_ERR_ALARM, flag, &bar0->sm_err_mask);
	}

	if (mask & TX_MAC_INTR) {
		gen_int_mask |= TXMAC_INT_M;
		do_s2io_write_bits(MAC_INT_STATUS_TMAC_INT, flag,
				   &bar0->mac_int_mask);
		do_s2io_write_bits(TMAC_TX_BUF_OVRN | TMAC_TX_SM_ERR |
				   TMAC_ECC_SG_ERR | TMAC_ECC_DB_ERR |
				   TMAC_DESC_ECC_SG_ERR | TMAC_DESC_ECC_DB_ERR,
				   flag, &bar0->mac_tmac_err_mask);
	}

	if (mask & TX_XGXS_INTR) {
		gen_int_mask |= TXXGXS_INT_M;
		do_s2io_write_bits(XGXS_INT_STATUS_TXGXS, flag,
				   &bar0->xgxs_int_mask);
		do_s2io_write_bits(TXGXS_ESTORE_UFLOW | TXGXS_TX_SM_ERR |
				   TXGXS_ECC_SG_ERR | TXGXS_ECC_DB_ERR,
				   flag, &bar0->xgxs_txgxs_err_mask);
	}

	if (mask & RX_DMA_INTR) {
		gen_int_mask |= RXDMA_INT_M;
		do_s2io_write_bits(RXDMA_INT_RC_INT_M | RXDMA_INT_RPA_INT_M |
				   RXDMA_INT_RDA_INT_M | RXDMA_INT_RTI_INT_M,
				   flag, &bar0->rxdma_int_mask);
		do_s2io_write_bits(RC_PRCn_ECC_DB_ERR | RC_FTC_ECC_DB_ERR |
				   RC_PRCn_SM_ERR_ALARM | RC_FTC_SM_ERR_ALARM |
				   RC_PRCn_ECC_SG_ERR | RC_FTC_ECC_SG_ERR |
				   RC_RDA_FAIL_WR_Rn, flag, &bar0->rc_err_mask);
		do_s2io_write_bits(PRC_PCI_AB_RD_Rn | PRC_PCI_AB_WR_Rn |
				   PRC_PCI_AB_F_WR_Rn | PRC_PCI_DP_RD_Rn |
				   PRC_PCI_DP_WR_Rn | PRC_PCI_DP_F_WR_Rn, flag,
				   &bar0->prc_pcix_err_mask);
		do_s2io_write_bits(RPA_SM_ERR_ALARM | RPA_CREDIT_ERR |
				   RPA_ECC_SG_ERR | RPA_ECC_DB_ERR, flag,
				   &bar0->rpa_err_mask);
		do_s2io_write_bits(RDA_RXDn_ECC_DB_ERR | RDA_FRM_ECC_DB_N_AERR |
				   RDA_SM1_ERR_ALARM | RDA_SM0_ERR_ALARM |
				   RDA_RXD_ECC_DB_SERR | RDA_RXDn_ECC_SG_ERR |
				   RDA_FRM_ECC_SG_ERR |
				   RDA_MISC_ERR|RDA_PCIX_ERR,
				   flag, &bar0->rda_err_mask);
		do_s2io_write_bits(RTI_SM_ERR_ALARM |
				   RTI_ECC_SG_ERR | RTI_ECC_DB_ERR,
				   flag, &bar0->rti_err_mask);
	}

	if (mask & RX_MAC_INTR) {
		gen_int_mask |= RXMAC_INT_M;
		do_s2io_write_bits(MAC_INT_STATUS_RMAC_INT, flag,
				   &bar0->mac_int_mask);
		interruptible = (RMAC_RX_BUFF_OVRN | RMAC_RX_SM_ERR |
				 RMAC_UNUSED_INT | RMAC_SINGLE_ECC_ERR |
				 RMAC_DOUBLE_ECC_ERR);
		if (s2io_link_fault_indication(nic) == MAC_RMAC_ERR_TIMER)
			interruptible |= RMAC_LINK_STATE_CHANGE_INT;
		do_s2io_write_bits(interruptible,
				   flag, &bar0->mac_rmac_err_mask);
	}

	if (mask & RX_XGXS_INTR) {
		gen_int_mask |= RXXGXS_INT_M;
		do_s2io_write_bits(XGXS_INT_STATUS_RXGXS, flag,
				   &bar0->xgxs_int_mask);
		do_s2io_write_bits(RXGXS_ESTORE_OFLOW | RXGXS_RX_SM_ERR, flag,
				   &bar0->xgxs_rxgxs_err_mask);
	}

	if (mask & MC_INTR) {
		gen_int_mask |= MC_INT_M;
		do_s2io_write_bits(MC_INT_MASK_MC_INT,
				   flag, &bar0->mc_int_mask);
		do_s2io_write_bits(MC_ERR_REG_SM_ERR | MC_ERR_REG_ECC_ALL_SNG |
				   MC_ERR_REG_ECC_ALL_DBL | PLL_LOCK_N, flag,
				   &bar0->mc_err_mask);
	}
	nic->general_int_mask = gen_int_mask;

	/* Remove this line when alarm interrupts are enabled */
	nic->general_int_mask = 0;
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
	register u64 temp64 = 0, intr_mask = 0;

	intr_mask = nic->general_int_mask;

	/*  Top level interrupt classification */
	/*  PIC Interrupts */
	if (mask & TX_PIC_INTR) {
		/*  Enable PIC Intrs in the general intr mask register */
		intr_mask |= TXPIC_INT_M;
		if (flag == ENABLE_INTRS) {
			/*
			 * If Hercules adapter enable GPIO otherwise
			 * disable all PCIX, Flash, MDIO, IIC and GPIO
			 * interrupts for now.
			 * TODO
			 */
			if (s2io_link_fault_indication(nic) ==
			    LINK_UP_DOWN_INTERRUPT) {
				do_s2io_write_bits(PIC_INT_GPIO, flag,
						   &bar0->pic_int_mask);
				do_s2io_write_bits(GPIO_INT_MASK_LINK_UP, flag,
						   &bar0->gpio_int_mask);
			} else
				writeq(DISABLE_ALL_INTRS, &bar0->pic_int_mask);
		} else if (flag == DISABLE_INTRS) {
			/*
			 * Disable PIC Intrs in the general
			 * intr mask register
			 */
			writeq(DISABLE_ALL_INTRS, &bar0->pic_int_mask);
		}
	}

	/*  Tx traffic interrupts */
	if (mask & TX_TRAFFIC_INTR) {
		intr_mask |= TXTRAFFIC_INT_M;
		if (flag == ENABLE_INTRS) {
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
		}
	}

	/*  Rx traffic interrupts */
	if (mask & RX_TRAFFIC_INTR) {
		intr_mask |= RXTRAFFIC_INT_M;
		if (flag == ENABLE_INTRS) {
			/* writing 0 Enables all 8 RX interrupt levels */
			writeq(0x0, &bar0->rx_traffic_mask);
		} else if (flag == DISABLE_INTRS) {
			/*
			 * Disable Rx Traffic Intrs in the general intr mask
			 * register.
			 */
			writeq(DISABLE_ALL_INTRS, &bar0->rx_traffic_mask);
		}
	}

	temp64 = readq(&bar0->general_int_mask);
	if (flag == ENABLE_INTRS)
		temp64 &= ~((u64)intr_mask);
	else
		temp64 = DISABLE_ALL_INTRS;
	writeq(temp64, &bar0->general_int_mask);

	nic->general_int_mask = readq(&bar0->general_int_mask);
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

	if (flag == false) {
		if ((!herc && (sp->pdev->revision >= 4)) || herc) {
			if (!(val64 & ADAPTER_STATUS_RMAC_PCC_IDLE))
				ret = 1;
		} else {
			if (!(val64 & ADAPTER_STATUS_RMAC_PCC_FOUR_IDLE))
				ret = 1;
		}
	} else {
		if ((!herc && (sp->pdev->revision >= 4)) || herc) {
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
		DBG_PRINT(ERR_DBG, "TDMA is not ready!\n");
		return 0;
	}
	if (!(val64 & ADAPTER_STATUS_RDMA_READY)) {
		DBG_PRINT(ERR_DBG, "RDMA is not ready!\n");
		return 0;
	}
	if (!(val64 & ADAPTER_STATUS_PFC_READY)) {
		DBG_PRINT(ERR_DBG, "PFC is not ready!\n");
		return 0;
	}
	if (!(val64 & ADAPTER_STATUS_TMAC_BUF_EMPTY)) {
		DBG_PRINT(ERR_DBG, "TMAC BUF is not empty!\n");
		return 0;
	}
	if (!(val64 & ADAPTER_STATUS_PIC_QUIESCENT)) {
		DBG_PRINT(ERR_DBG, "PIC is not QUIESCENT!\n");
		return 0;
	}
	if (!(val64 & ADAPTER_STATUS_MC_DRAM_READY)) {
		DBG_PRINT(ERR_DBG, "MC_DRAM is not ready!\n");
		return 0;
	}
	if (!(val64 & ADAPTER_STATUS_MC_QUEUES_READY)) {
		DBG_PRINT(ERR_DBG, "MC_QUEUES is not ready!\n");
		return 0;
	}
	if (!(val64 & ADAPTER_STATUS_M_PLL_LOCK)) {
		DBG_PRINT(ERR_DBG, "M_PLL is not locked!\n");
		return 0;
	}

	/*
	 * In PCI 33 mode, the P_PLL is not used, and therefore,
	 * the the P_PLL_LOCK bit in the adapter_status register will
	 * not be asserted.
	 */
	if (!(val64 & ADAPTER_STATUS_P_PLL_LOCK) &&
	    sp->device_type == XFRAME_II_DEVICE &&
	    mode != PCI_MODE_PCI_33) {
		DBG_PRINT(ERR_DBG, "P_PLL is not locked!\n");
		return 0;
	}
	if (!((val64 & ADAPTER_STATUS_RC_PRC_QUIESCENT) ==
	      ADAPTER_STATUS_RC_PRC_QUIESCENT)) {
		DBG_PRINT(ERR_DBG, "RC_PRC is not QUIESCENT!\n");
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

static void fix_mac_address(struct s2io_nic *sp)
{
	struct XENA_dev_config __iomem *bar0 = sp->bar0;
	int i = 0;

	while (fix_mac[i] != END_SIGN) {
		writeq(fix_mac[i++], &bar0->gpio_control);
		udelay(10);
		(void) readq(&bar0->gpio_control);
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
	struct config_param *config = &nic->config;
	struct mac_info *mac_control = &nic->mac_control;

	/*  PRC Initialization and configuration */
	for (i = 0; i < config->rx_ring_num; i++) {
		struct ring_info *ring = &mac_control->rings[i];

		writeq((u64)ring->rx_blocks[0].block_dma_addr,
		       &bar0->prc_rxd0_n[i]);

		val64 = readq(&bar0->prc_ctrl_n[i]);
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
		nic->vlan_strip_flag = 0;
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
	 * Verify if the device is ready to be enabled, if so enable
	 * it.
	 */
	val64 = readq(&bar0->adapter_status);
	if (!verify_xena_quiescence(nic)) {
		DBG_PRINT(ERR_DBG, "%s: device is not ready, "
			  "Adapter status reads: 0x%llx\n",
			  dev->name, (unsigned long long)val64);
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
		 * Dont see link state interrupts initially on some switches,
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
static struct sk_buff *s2io_txdl_getskb(struct fifo_info *fifo_data,
					struct TxD *txdlp, int get_off)
{
	struct s2io_nic *nic = fifo_data->nic;
	struct sk_buff *skb;
	struct TxD *txds;
	u16 j, frg_cnt;

	txds = txdlp;
	if (txds->Host_Control == (u64)(long)fifo_data->ufo_in_band_v) {
		pci_unmap_single(nic->pdev, (dma_addr_t)txds->Buffer_Pointer,
				 sizeof(u64), PCI_DMA_TODEVICE);
		txds++;
	}

	skb = (struct sk_buff *)((unsigned long)txds->Host_Control);
	if (!skb) {
		memset(txdlp, 0, (sizeof(struct TxD) * fifo_data->max_txds));
		return NULL;
	}
	pci_unmap_single(nic->pdev, (dma_addr_t)txds->Buffer_Pointer,
			 skb_headlen(skb), PCI_DMA_TODEVICE);
	frg_cnt = skb_shinfo(skb)->nr_frags;
	if (frg_cnt) {
		txds++;
		for (j = 0; j < frg_cnt; j++, txds++) {
			const skb_frag_t *frag = &skb_shinfo(skb)->frags[j];
			if (!txds->Buffer_Pointer)
				break;
			pci_unmap_page(nic->pdev,
				       (dma_addr_t)txds->Buffer_Pointer,
				       skb_frag_size(frag), PCI_DMA_TODEVICE);
		}
	}
	memset(txdlp, 0, (sizeof(struct TxD) * fifo_data->max_txds));
	return skb;
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
	int cnt = 0;
	struct config_param *config = &nic->config;
	struct mac_info *mac_control = &nic->mac_control;
	struct stat_block *stats = mac_control->stats_info;
	struct swStat *swstats = &stats->sw_stat;

	for (i = 0; i < config->tx_fifo_num; i++) {
		struct tx_fifo_config *tx_cfg = &config->tx_cfg[i];
		struct fifo_info *fifo = &mac_control->fifos[i];
		unsigned long flags;

		spin_lock_irqsave(&fifo->tx_lock, flags);
		for (j = 0; j < tx_cfg->fifo_len; j++) {
			txdp = fifo->list_info[j].list_virt_addr;
			skb = s2io_txdl_getskb(&mac_control->fifos[i], txdp, j);
			if (skb) {
				swstats->mem_freed += skb->truesize;
				dev_kfree_skb(skb);
				cnt++;
			}
		}
		DBG_PRINT(INTR_DBG,
			  "%s: forcibly freeing %d skbs on FIFO%d\n",
			  dev->name, cnt, i);
		fifo->tx_curr_get_info.offset = 0;
		fifo->tx_curr_put_info.offset = 0;
		spin_unlock_irqrestore(&fifo->tx_lock, flags);
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

	/*  Disable all interrupts */
	en_dis_err_alarms(nic, ENA_ALL_INTRS, DISABLE_INTRS);
	interruptible = TX_TRAFFIC_INTR | RX_TRAFFIC_INTR;
	interruptible |= TX_PIC_INTR;
	en_dis_able_nic_intrs(nic, interruptible, DISABLE_INTRS);

	/* Clearing Adapter_En bit of ADAPTER_CONTROL Register */
	val64 = readq(&bar0->adapter_control);
	val64 &= ~(ADAPTER_CNTL_EN);
	writeq(val64, &bar0->adapter_control);
}

/**
 *  fill_rx_buffers - Allocates the Rx side skbs
 *  @ring_info: per ring structure
 *  @from_card_up: If this is true, we will map the buffer to get
 *     the dma address for buf0 and buf1 to give it to the card.
 *     Else we will sync the already mapped buffer to give it to the card.
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
static int fill_rx_buffers(struct s2io_nic *nic, struct ring_info *ring,
			   int from_card_up)
{
	struct sk_buff *skb;
	struct RxD_t *rxdp;
	int off, size, block_no, block_no1;
	u32 alloc_tab = 0;
	u32 alloc_cnt;
	u64 tmp;
	struct buffAdd *ba;
	struct RxD_t *first_rxdp = NULL;
	u64 Buffer0_ptr = 0, Buffer1_ptr = 0;
	struct RxD1 *rxdp1;
	struct RxD3 *rxdp3;
	struct swStat *swstats = &ring->nic->mac_control.stats_info->sw_stat;

	alloc_cnt = ring->pkt_cnt - ring->rx_bufs_left;

	block_no1 = ring->rx_curr_get_info.block_index;
	while (alloc_tab < alloc_cnt) {
		block_no = ring->rx_curr_put_info.block_index;

		off = ring->rx_curr_put_info.offset;

		rxdp = ring->rx_blocks[block_no].rxds[off].virt_addr;

		if ((block_no == block_no1) &&
		    (off == ring->rx_curr_get_info.offset) &&
		    (rxdp->Host_Control)) {
			DBG_PRINT(INTR_DBG, "%s: Get and Put info equated\n",
				  ring->dev->name);
			goto end;
		}
		if (off && (off == ring->rxd_count)) {
			ring->rx_curr_put_info.block_index++;
			if (ring->rx_curr_put_info.block_index ==
			    ring->block_count)
				ring->rx_curr_put_info.block_index = 0;
			block_no = ring->rx_curr_put_info.block_index;
			off = 0;
			ring->rx_curr_put_info.offset = off;
			rxdp = ring->rx_blocks[block_no].block_virt_addr;
			DBG_PRINT(INTR_DBG, "%s: Next block at: %p\n",
				  ring->dev->name, rxdp);

		}

		if ((rxdp->Control_1 & RXD_OWN_XENA) &&
		    ((ring->rxd_mode == RXD_MODE_3B) &&
		     (rxdp->Control_2 & s2BIT(0)))) {
			ring->rx_curr_put_info.offset = off;
			goto end;
		}
		/* calculate size of skb based on ring mode */
		size = ring->mtu +
			HEADER_ETHERNET_II_802_3_SIZE +
			HEADER_802_2_SIZE + HEADER_SNAP_SIZE;
		if (ring->rxd_mode == RXD_MODE_1)
			size += NET_IP_ALIGN;
		else
			size = ring->mtu + ALIGN_SIZE + BUF0_LEN + 4;

		/* allocate skb */
		skb = netdev_alloc_skb(nic->dev, size);
		if (!skb) {
			DBG_PRINT(INFO_DBG, "%s: Could not allocate skb\n",
				  ring->dev->name);
			if (first_rxdp) {
				dma_wmb();
				first_rxdp->Control_1 |= RXD_OWN_XENA;
			}
			swstats->mem_alloc_fail_cnt++;

			return -ENOMEM ;
		}
		swstats->mem_allocated += skb->truesize;

		if (ring->rxd_mode == RXD_MODE_1) {
			/* 1 buffer mode - normal operation mode */
			rxdp1 = (struct RxD1 *)rxdp;
			memset(rxdp, 0, sizeof(struct RxD1));
			skb_reserve(skb, NET_IP_ALIGN);
			rxdp1->Buffer0_ptr =
				pci_map_single(ring->pdev, skb->data,
					       size - NET_IP_ALIGN,
					       PCI_DMA_FROMDEVICE);
			if (pci_dma_mapping_error(nic->pdev,
						  rxdp1->Buffer0_ptr))
				goto pci_map_failed;

			rxdp->Control_2 =
				SET_BUFFER0_SIZE_1(size - NET_IP_ALIGN);
			rxdp->Host_Control = (unsigned long)skb;
		} else if (ring->rxd_mode == RXD_MODE_3B) {
			/*
			 * 2 buffer mode -
			 * 2 buffer mode provides 128
			 * byte aligned receive buffers.
			 */

			rxdp3 = (struct RxD3 *)rxdp;
			/* save buffer pointers to avoid frequent dma mapping */
			Buffer0_ptr = rxdp3->Buffer0_ptr;
			Buffer1_ptr = rxdp3->Buffer1_ptr;
			memset(rxdp, 0, sizeof(struct RxD3));
			/* restore the buffer pointers for dma sync*/
			rxdp3->Buffer0_ptr = Buffer0_ptr;
			rxdp3->Buffer1_ptr = Buffer1_ptr;

			ba = &ring->ba[block_no][off];
			skb_reserve(skb, BUF0_LEN);
			tmp = (u64)(unsigned long)skb->data;
			tmp += ALIGN_SIZE;
			tmp &= ~ALIGN_SIZE;
			skb->data = (void *) (unsigned long)tmp;
			skb_reset_tail_pointer(skb);

			if (from_card_up) {
				rxdp3->Buffer0_ptr =
					pci_map_single(ring->pdev, ba->ba_0,
						       BUF0_LEN,
						       PCI_DMA_FROMDEVICE);
				if (pci_dma_mapping_error(nic->pdev,
							  rxdp3->Buffer0_ptr))
					goto pci_map_failed;
			} else
				pci_dma_sync_single_for_device(ring->pdev,
							       (dma_addr_t)rxdp3->Buffer0_ptr,
							       BUF0_LEN,
							       PCI_DMA_FROMDEVICE);

			rxdp->Control_2 = SET_BUFFER0_SIZE_3(BUF0_LEN);
			if (ring->rxd_mode == RXD_MODE_3B) {
				/* Two buffer mode */

				/*
				 * Buffer2 will have L3/L4 header plus
				 * L4 payload
				 */
				rxdp3->Buffer2_ptr = pci_map_single(ring->pdev,
								    skb->data,
								    ring->mtu + 4,
								    PCI_DMA_FROMDEVICE);

				if (pci_dma_mapping_error(nic->pdev,
							  rxdp3->Buffer2_ptr))
					goto pci_map_failed;

				if (from_card_up) {
					rxdp3->Buffer1_ptr =
						pci_map_single(ring->pdev,
							       ba->ba_1,
							       BUF1_LEN,
							       PCI_DMA_FROMDEVICE);

					if (pci_dma_mapping_error(nic->pdev,
								  rxdp3->Buffer1_ptr)) {
						pci_unmap_single(ring->pdev,
								 (dma_addr_t)(unsigned long)
								 skb->data,
								 ring->mtu + 4,
								 PCI_DMA_FROMDEVICE);
						goto pci_map_failed;
					}
				}
				rxdp->Control_2 |= SET_BUFFER1_SIZE_3(1);
				rxdp->Control_2 |= SET_BUFFER2_SIZE_3
					(ring->mtu + 4);
			}
			rxdp->Control_2 |= s2BIT(0);
			rxdp->Host_Control = (unsigned long) (skb);
		}
		if (alloc_tab & ((1 << rxsync_frequency) - 1))
			rxdp->Control_1 |= RXD_OWN_XENA;
		off++;
		if (off == (ring->rxd_count + 1))
			off = 0;
		ring->rx_curr_put_info.offset = off;

		rxdp->Control_2 |= SET_RXD_MARKER;
		if (!(alloc_tab & ((1 << rxsync_frequency) - 1))) {
			if (first_rxdp) {
				dma_wmb();
				first_rxdp->Control_1 |= RXD_OWN_XENA;
			}
			first_rxdp = rxdp;
		}
		ring->rx_bufs_left += 1;
		alloc_tab++;
	}

end:
	/* Transfer ownership of first descriptor to adapter just before
	 * exiting. Before that, use memory barrier so that ownership
	 * and other fields are seen by adapter correctly.
	 */
	if (first_rxdp) {
		dma_wmb();
		first_rxdp->Control_1 |= RXD_OWN_XENA;
	}

	return SUCCESS;

pci_map_failed:
	swstats->pci_map_fail_cnt++;
	swstats->mem_freed += skb->truesize;
	dev_kfree_skb_irq(skb);
	return -ENOMEM;
}

static void free_rxd_blk(struct s2io_nic *sp, int ring_no, int blk)
{
	struct net_device *dev = sp->dev;
	int j;
	struct sk_buff *skb;
	struct RxD_t *rxdp;
	struct RxD1 *rxdp1;
	struct RxD3 *rxdp3;
	struct mac_info *mac_control = &sp->mac_control;
	struct stat_block *stats = mac_control->stats_info;
	struct swStat *swstats = &stats->sw_stat;

	for (j = 0 ; j < rxd_count[sp->rxd_mode]; j++) {
		rxdp = mac_control->rings[ring_no].
			rx_blocks[blk].rxds[j].virt_addr;
		skb = (struct sk_buff *)((unsigned long)rxdp->Host_Control);
		if (!skb)
			continue;
		if (sp->rxd_mode == RXD_MODE_1) {
			rxdp1 = (struct RxD1 *)rxdp;
			pci_unmap_single(sp->pdev,
					 (dma_addr_t)rxdp1->Buffer0_ptr,
					 dev->mtu +
					 HEADER_ETHERNET_II_802_3_SIZE +
					 HEADER_802_2_SIZE + HEADER_SNAP_SIZE,
					 PCI_DMA_FROMDEVICE);
			memset(rxdp, 0, sizeof(struct RxD1));
		} else if (sp->rxd_mode == RXD_MODE_3B) {
			rxdp3 = (struct RxD3 *)rxdp;
			pci_unmap_single(sp->pdev,
					 (dma_addr_t)rxdp3->Buffer0_ptr,
					 BUF0_LEN,
					 PCI_DMA_FROMDEVICE);
			pci_unmap_single(sp->pdev,
					 (dma_addr_t)rxdp3->Buffer1_ptr,
					 BUF1_LEN,
					 PCI_DMA_FROMDEVICE);
			pci_unmap_single(sp->pdev,
					 (dma_addr_t)rxdp3->Buffer2_ptr,
					 dev->mtu + 4,
					 PCI_DMA_FROMDEVICE);
			memset(rxdp, 0, sizeof(struct RxD3));
		}
		swstats->mem_freed += skb->truesize;
		dev_kfree_skb(skb);
		mac_control->rings[ring_no].rx_bufs_left -= 1;
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
	struct config_param *config = &sp->config;
	struct mac_info *mac_control = &sp->mac_control;

	for (i = 0; i < config->rx_ring_num; i++) {
		struct ring_info *ring = &mac_control->rings[i];

		for (blk = 0; blk < rx_ring_sz[i]; blk++)
			free_rxd_blk(sp, i, blk);

		ring->rx_curr_put_info.block_index = 0;
		ring->rx_curr_get_info.block_index = 0;
		ring->rx_curr_put_info.offset = 0;
		ring->rx_curr_get_info.offset = 0;
		ring->rx_bufs_left = 0;
		DBG_PRINT(INIT_DBG, "%s: Freed 0x%x Rx Buffers on ring%d\n",
			  dev->name, buf_cnt, i);
	}
}

static int s2io_chk_rx_buffers(struct s2io_nic *nic, struct ring_info *ring)
{
	if (fill_rx_buffers(nic, ring, 0) == -ENOMEM) {
		DBG_PRINT(INFO_DBG, "%s: Out of memory in Rx Intr!!\n",
			  ring->dev->name);
	}
	return 0;
}

/**
 * s2io_poll - Rx interrupt handler for NAPI support
 * @napi : pointer to the napi structure.
 * @budget : The number of packets that were budgeted to be processed
 * during  one pass through the 'Poll" function.
 * Description:
 * Comes into picture only if NAPI support has been incorporated. It does
 * the same thing that rx_intr_handler does, but not in a interrupt context
 * also It will process only a given number of packets.
 * Return value:
 * 0 on success and 1 if there are No Rx packets to be processed.
 */

static int s2io_poll_msix(struct napi_struct *napi, int budget)
{
	struct ring_info *ring = container_of(napi, struct ring_info, napi);
	struct net_device *dev = ring->dev;
	int pkts_processed = 0;
	u8 __iomem *addr = NULL;
	u8 val8 = 0;
	struct s2io_nic *nic = netdev_priv(dev);
	struct XENA_dev_config __iomem *bar0 = nic->bar0;
	int budget_org = budget;

	if (unlikely(!is_s2io_card_up(nic)))
		return 0;

	pkts_processed = rx_intr_handler(ring, budget);
	s2io_chk_rx_buffers(nic, ring);

	if (pkts_processed < budget_org) {
		napi_complete_done(napi, pkts_processed);
		/*Re Enable MSI-Rx Vector*/
		addr = (u8 __iomem *)&bar0->xmsi_mask_reg;
		addr += 7 - ring->ring_no;
		val8 = (ring->ring_no == 0) ? 0x3f : 0xbf;
		writeb(val8, addr);
		val8 = readb(addr);
	}
	return pkts_processed;
}

static int s2io_poll_inta(struct napi_struct *napi, int budget)
{
	struct s2io_nic *nic = container_of(napi, struct s2io_nic, napi);
	int pkts_processed = 0;
	int ring_pkts_processed, i;
	struct XENA_dev_config __iomem *bar0 = nic->bar0;
	int budget_org = budget;
	struct config_param *config = &nic->config;
	struct mac_info *mac_control = &nic->mac_control;

	if (unlikely(!is_s2io_card_up(nic)))
		return 0;

	for (i = 0; i < config->rx_ring_num; i++) {
		struct ring_info *ring = &mac_control->rings[i];
		ring_pkts_processed = rx_intr_handler(ring, budget);
		s2io_chk_rx_buffers(nic, ring);
		pkts_processed += ring_pkts_processed;
		budget -= ring_pkts_processed;
		if (budget <= 0)
			break;
	}
	if (pkts_processed < budget_org) {
		napi_complete_done(napi, pkts_processed);
		/* Re enable the Rx interrupts for the ring */
		writeq(0, &bar0->rx_traffic_mask);
		readl(&bar0->rx_traffic_mask);
	}
	return pkts_processed;
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
	struct s2io_nic *nic = netdev_priv(dev);
	const int irq = nic->pdev->irq;
	struct XENA_dev_config __iomem *bar0 = nic->bar0;
	u64 val64 = 0xFFFFFFFFFFFFFFFFULL;
	int i;
	struct config_param *config = &nic->config;
	struct mac_info *mac_control = &nic->mac_control;

	if (pci_channel_offline(nic->pdev))
		return;

	disable_irq(irq);

	writeq(val64, &bar0->rx_traffic_int);
	writeq(val64, &bar0->tx_traffic_int);

	/* we need to free up the transmitted skbufs or else netpoll will
	 * run out of skbs and will fail and eventually netpoll application such
	 * as netdump will fail.
	 */
	for (i = 0; i < config->tx_fifo_num; i++)
		tx_intr_handler(&mac_control->fifos[i]);

	/* check for received packet and indicate up to network */
	for (i = 0; i < config->rx_ring_num; i++) {
		struct ring_info *ring = &mac_control->rings[i];

		rx_intr_handler(ring, 0);
	}

	for (i = 0; i < config->rx_ring_num; i++) {
		struct ring_info *ring = &mac_control->rings[i];

		if (fill_rx_buffers(nic, ring, 0) == -ENOMEM) {
			DBG_PRINT(INFO_DBG,
				  "%s: Out of memory in Rx Netpoll!!\n",
				  dev->name);
			break;
		}
	}
	enable_irq(irq);
}
#endif

/**
 *  rx_intr_handler - Rx interrupt handler
 *  @ring_info: per ring structure.
 *  @budget: budget for napi processing.
 *  Description:
 *  If the interrupt is because of a received frame or if the
 *  receive ring contains fresh as yet un-processed frames,this function is
 *  called. It picks out the RxD at which place the last Rx processing had
 *  stopped and sends the skb to the OSM's Rx handler and then increments
 *  the offset.
 *  Return Value:
 *  No. of napi packets processed.
 */
static int rx_intr_handler(struct ring_info *ring_data, int budget)
{
	int get_block, put_block;
	struct rx_curr_get_info get_info, put_info;
	struct RxD_t *rxdp;
	struct sk_buff *skb;
	int pkt_cnt = 0, napi_pkts = 0;
	int i;
	struct RxD1 *rxdp1;
	struct RxD3 *rxdp3;

	if (budget <= 0)
		return napi_pkts;

	get_info = ring_data->rx_curr_get_info;
	get_block = get_info.block_index;
	memcpy(&put_info, &ring_data->rx_curr_put_info, sizeof(put_info));
	put_block = put_info.block_index;
	rxdp = ring_data->rx_blocks[get_block].rxds[get_info.offset].virt_addr;

	while (RXD_IS_UP2DT(rxdp)) {
		/*
		 * If your are next to put index then it's
		 * FIFO full condition
		 */
		if ((get_block == put_block) &&
		    (get_info.offset + 1) == put_info.offset) {
			DBG_PRINT(INTR_DBG, "%s: Ring Full\n",
				  ring_data->dev->name);
			break;
		}
		skb = (struct sk_buff *)((unsigned long)rxdp->Host_Control);
		if (skb == NULL) {
			DBG_PRINT(ERR_DBG, "%s: NULL skb in Rx Intr\n",
				  ring_data->dev->name);
			return 0;
		}
		if (ring_data->rxd_mode == RXD_MODE_1) {
			rxdp1 = (struct RxD1 *)rxdp;
			pci_unmap_single(ring_data->pdev, (dma_addr_t)
					 rxdp1->Buffer0_ptr,
					 ring_data->mtu +
					 HEADER_ETHERNET_II_802_3_SIZE +
					 HEADER_802_2_SIZE +
					 HEADER_SNAP_SIZE,
					 PCI_DMA_FROMDEVICE);
		} else if (ring_data->rxd_mode == RXD_MODE_3B) {
			rxdp3 = (struct RxD3 *)rxdp;
			pci_dma_sync_single_for_cpu(ring_data->pdev,
						    (dma_addr_t)rxdp3->Buffer0_ptr,
						    BUF0_LEN,
						    PCI_DMA_FROMDEVICE);
			pci_unmap_single(ring_data->pdev,
					 (dma_addr_t)rxdp3->Buffer2_ptr,
					 ring_data->mtu + 4,
					 PCI_DMA_FROMDEVICE);
		}
		prefetch(skb->data);
		rx_osm_handler(ring_data, rxdp);
		get_info.offset++;
		ring_data->rx_curr_get_info.offset = get_info.offset;
		rxdp = ring_data->rx_blocks[get_block].
			rxds[get_info.offset].virt_addr;
		if (get_info.offset == rxd_count[ring_data->rxd_mode]) {
			get_info.offset = 0;
			ring_data->rx_curr_get_info.offset = get_info.offset;
			get_block++;
			if (get_block == ring_data->block_count)
				get_block = 0;
			ring_data->rx_curr_get_info.block_index = get_block;
			rxdp = ring_data->rx_blocks[get_block].block_virt_addr;
		}

		if (ring_data->nic->config.napi) {
			budget--;
			napi_pkts++;
			if (!budget)
				break;
		}
		pkt_cnt++;
		if ((indicate_max_pkts) && (pkt_cnt > indicate_max_pkts))
			break;
	}
	if (ring_data->lro) {
		/* Clear all LRO sessions before exiting */
		for (i = 0; i < MAX_LRO_SESSIONS; i++) {
			struct lro *lro = &ring_data->lro0_n[i];
			if (lro->in_use) {
				update_L3L4_header(ring_data->nic, lro);
				queue_rx_frame(lro->parent, lro->vlan_tag);
				clear_lro_session(lro);
			}
		}
	}
	return napi_pkts;
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
	struct tx_curr_get_info get_info, put_info;
	struct sk_buff *skb = NULL;
	struct TxD *txdlp;
	int pkt_cnt = 0;
	unsigned long flags = 0;
	u8 err_mask;
	struct stat_block *stats = nic->mac_control.stats_info;
	struct swStat *swstats = &stats->sw_stat;

	if (!spin_trylock_irqsave(&fifo_data->tx_lock, flags))
		return;

	get_info = fifo_data->tx_curr_get_info;
	memcpy(&put_info, &fifo_data->tx_curr_put_info, sizeof(put_info));
	txdlp = fifo_data->list_info[get_info.offset].list_virt_addr;
	while ((!(txdlp->Control_1 & TXD_LIST_OWN_XENA)) &&
	       (get_info.offset != put_info.offset) &&
	       (txdlp->Host_Control)) {
		/* Check for TxD errors */
		if (txdlp->Control_1 & TXD_T_CODE) {
			unsigned long long err;
			err = txdlp->Control_1 & TXD_T_CODE;
			if (err & 0x1) {
				swstats->parity_err_cnt++;
			}

			/* update t_code statistics */
			err_mask = err >> 48;
			switch (err_mask) {
			case 2:
				swstats->tx_buf_abort_cnt++;
				break;

			case 3:
				swstats->tx_desc_abort_cnt++;
				break;

			case 7:
				swstats->tx_parity_err_cnt++;
				break;

			case 10:
				swstats->tx_link_loss_cnt++;
				break;

			case 15:
				swstats->tx_list_proc_err_cnt++;
				break;
			}
		}

		skb = s2io_txdl_getskb(fifo_data, txdlp, get_info.offset);
		if (skb == NULL) {
			spin_unlock_irqrestore(&fifo_data->tx_lock, flags);
			DBG_PRINT(ERR_DBG, "%s: NULL skb in Tx Free Intr\n",
				  __func__);
			return;
		}
		pkt_cnt++;

		/* Updating the statistics block */
		swstats->mem_freed += skb->truesize;
		dev_kfree_skb_irq(skb);

		get_info.offset++;
		if (get_info.offset == get_info.fifo_len + 1)
			get_info.offset = 0;
		txdlp = fifo_data->list_info[get_info.offset].list_virt_addr;
		fifo_data->tx_curr_get_info.offset = get_info.offset;
	}

	s2io_wake_tx_queue(fifo_data, pkt_cnt, nic->config.multiq);

	spin_unlock_irqrestore(&fifo_data->tx_lock, flags);
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
static void s2io_mdio_write(u32 mmd_type, u64 addr, u16 value,
			    struct net_device *dev)
{
	u64 val64;
	struct s2io_nic *sp = netdev_priv(dev);
	struct XENA_dev_config __iomem *bar0 = sp->bar0;

	/* address transaction */
	val64 = MDIO_MMD_INDX_ADDR(addr) |
		MDIO_MMD_DEV_ADDR(mmd_type) |
		MDIO_MMS_PRT_ADDR(0x0);
	writeq(val64, &bar0->mdio_control);
	val64 = val64 | MDIO_CTRL_START_TRANS(0xE);
	writeq(val64, &bar0->mdio_control);
	udelay(100);

	/* Data transaction */
	val64 = MDIO_MMD_INDX_ADDR(addr) |
		MDIO_MMD_DEV_ADDR(mmd_type) |
		MDIO_MMS_PRT_ADDR(0x0) |
		MDIO_MDIO_DATA(value) |
		MDIO_OP(MDIO_OP_WRITE_TRANS);
	writeq(val64, &bar0->mdio_control);
	val64 = val64 | MDIO_CTRL_START_TRANS(0xE);
	writeq(val64, &bar0->mdio_control);
	udelay(100);

	val64 = MDIO_MMD_INDX_ADDR(addr) |
		MDIO_MMD_DEV_ADDR(mmd_type) |
		MDIO_MMS_PRT_ADDR(0x0) |
		MDIO_OP(MDIO_OP_READ_TRANS);
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
	struct s2io_nic *sp = netdev_priv(dev);
	struct XENA_dev_config __iomem *bar0 = sp->bar0;

	/* address transaction */
	val64 = val64 | (MDIO_MMD_INDX_ADDR(addr)
			 | MDIO_MMD_DEV_ADDR(mmd_type)
			 | MDIO_MMS_PRT_ADDR(0x0));
	writeq(val64, &bar0->mdio_control);
	val64 = val64 | MDIO_CTRL_START_TRANS(0xE);
	writeq(val64, &bar0->mdio_control);
	udelay(100);

	/* Data transaction */
	val64 = MDIO_MMD_INDX_ADDR(addr) |
		MDIO_MMD_DEV_ADDR(mmd_type) |
		MDIO_MMS_PRT_ADDR(0x0) |
		MDIO_OP(MDIO_OP_READ_TRANS);
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
 *  @counter      : counter value to be updated
 *  @flag         : flag to indicate the status
 *  @type         : counter type
 *  Description:
 *  This function is to check the status of the xpak counters value
 *  NONE
 */

static void s2io_chk_xpak_counter(u64 *counter, u64 * regs_stat, u32 index,
				  u16 flag, u16 type)
{
	u64 mask = 0x3;
	u64 val64;
	int i;
	for (i = 0; i < index; i++)
		mask = mask << 0x2;

	if (flag > 0) {
		*counter = *counter + 1;
		val64 = *regs_stat & mask;
		val64 = val64 >> (index * 0x2);
		val64 = val64 + 1;
		if (val64 == 3) {
			switch (type) {
			case 1:
				DBG_PRINT(ERR_DBG,
					  "Take Xframe NIC out of service.\n");
				DBG_PRINT(ERR_DBG,
"Excessive temperatures may result in premature transceiver failure.\n");
				break;
			case 2:
				DBG_PRINT(ERR_DBG,
					  "Take Xframe NIC out of service.\n");
				DBG_PRINT(ERR_DBG,
"Excessive bias currents may indicate imminent laser diode failure.\n");
				break;
			case 3:
				DBG_PRINT(ERR_DBG,
					  "Take Xframe NIC out of service.\n");
				DBG_PRINT(ERR_DBG,
"Excessive laser output power may saturate far-end receiver.\n");
				break;
			default:
				DBG_PRINT(ERR_DBG,
					  "Incorrect XPAK Alarm type\n");
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

	struct s2io_nic *sp = netdev_priv(dev);
	struct stat_block *stats = sp->mac_control.stats_info;
	struct xpakStat *xstats = &stats->xpak_stat;

	/* Check the communication with the MDIO slave */
	addr = MDIO_CTRL1;
	val64 = 0x0;
	val64 = s2io_mdio_read(MDIO_MMD_PMAPMD, addr, dev);
	if ((val64 == 0xFFFF) || (val64 == 0x0000)) {
		DBG_PRINT(ERR_DBG,
			  "ERR: MDIO slave access failed - Returned %llx\n",
			  (unsigned long long)val64);
		return;
	}

	/* Check for the expected value of control reg 1 */
	if (val64 != MDIO_CTRL1_SPEED10G) {
		DBG_PRINT(ERR_DBG, "Incorrect value at PMA address 0x0000 - "
			  "Returned: %llx- Expected: 0x%x\n",
			  (unsigned long long)val64, MDIO_CTRL1_SPEED10G);
		return;
	}

	/* Loading the DOM register to MDIO register */
	addr = 0xA100;
	s2io_mdio_write(MDIO_MMD_PMAPMD, addr, val16, dev);
	val64 = s2io_mdio_read(MDIO_MMD_PMAPMD, addr, dev);

	/* Reading the Alarm flags */
	addr = 0xA070;
	val64 = 0x0;
	val64 = s2io_mdio_read(MDIO_MMD_PMAPMD, addr, dev);

	flag = CHECKBIT(val64, 0x7);
	type = 1;
	s2io_chk_xpak_counter(&xstats->alarm_transceiver_temp_high,
			      &xstats->xpak_regs_stat,
			      0x0, flag, type);

	if (CHECKBIT(val64, 0x6))
		xstats->alarm_transceiver_temp_low++;

	flag = CHECKBIT(val64, 0x3);
	type = 2;
	s2io_chk_xpak_counter(&xstats->alarm_laser_bias_current_high,
			      &xstats->xpak_regs_stat,
			      0x2, flag, type);

	if (CHECKBIT(val64, 0x2))
		xstats->alarm_laser_bias_current_low++;

	flag = CHECKBIT(val64, 0x1);
	type = 3;
	s2io_chk_xpak_counter(&xstats->alarm_laser_output_power_high,
			      &xstats->xpak_regs_stat,
			      0x4, flag, type);

	if (CHECKBIT(val64, 0x0))
		xstats->alarm_laser_output_power_low++;

	/* Reading the Warning flags */
	addr = 0xA074;
	val64 = 0x0;
	val64 = s2io_mdio_read(MDIO_MMD_PMAPMD, addr, dev);

	if (CHECKBIT(val64, 0x7))
		xstats->warn_transceiver_temp_high++;

	if (CHECKBIT(val64, 0x6))
		xstats->warn_transceiver_temp_low++;

	if (CHECKBIT(val64, 0x3))
		xstats->warn_laser_bias_current_high++;

	if (CHECKBIT(val64, 0x2))
		xstats->warn_laser_bias_current_low++;

	if (CHECKBIT(val64, 0x1))
		xstats->warn_laser_output_power_high++;

	if (CHECKBIT(val64, 0x0))
		xstats->warn_laser_output_power_low++;
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
			if (val64 & busy_bit) {
				ret = SUCCESS;
				break;
			}
		}

		if (in_interrupt())
			mdelay(delay);
		else
			msleep(delay);

		if (++cnt >= 10)
			delay = 50;
	} while (cnt < 20);
	return ret;
}
/**
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

static void s2io_reset(struct s2io_nic *sp)
{
	struct XENA_dev_config __iomem *bar0 = sp->bar0;
	u64 val64;
	u16 subid, pci_cmd;
	int i;
	u16 val16;
	unsigned long long up_cnt, down_cnt, up_time, down_time, reset_cnt;
	unsigned long long mem_alloc_cnt, mem_free_cnt, watchdog_cnt;
	struct stat_block *stats;
	struct swStat *swstats;

	DBG_PRINT(INIT_DBG, "%s: Resetting XFrame card %s\n",
		  __func__, pci_name(sp->pdev));

	/* Back up  the PCI-X CMD reg, dont want to lose MMRBC, OST settings */
	pci_read_config_word(sp->pdev, PCIX_COMMAND_REGISTER, &(pci_cmd));

	val64 = SW_RESET_ALL;
	writeq(val64, &bar0->sw_reset);
	if (strstr(sp->product_name, "CX4"))
		msleep(750);
	msleep(250);
	for (i = 0; i < S2IO_MAX_PCI_CONFIG_SPACE_REINIT; i++) {

		/* Restore the PCI state saved during initialization. */
		pci_restore_state(sp->pdev);
		pci_save_state(sp->pdev);
		pci_read_config_word(sp->pdev, 0x2, &val16);
		if (check_pci_device_id(val16) != (u16)PCI_ANY_ID)
			break;
		msleep(200);
	}

	if (check_pci_device_id(val16) == (u16)PCI_ANY_ID)
		DBG_PRINT(ERR_DBG, "%s SW_Reset failed!\n", __func__);

	pci_write_config_word(sp->pdev, PCIX_COMMAND_REGISTER, pci_cmd);

	s2io_init_pci(sp);

	/* Set swapper to enable I/O register access */
	s2io_set_swapper(sp);

	/* restore mac_addr entries */
	do_s2io_restore_unicast_mc(sp);

	/* Restore the MSIX table entries from local variables */
	restore_xmsi_data(sp);

	/* Clear certain PCI/PCI-X fields after reset */
	if (sp->device_type == XFRAME_II_DEVICE) {
		/* Clear "detected parity error" bit */
		pci_write_config_word(sp->pdev, PCI_STATUS, 0x8000);

		/* Clearing PCIX Ecc status register */
		pci_write_config_dword(sp->pdev, 0x68, 0x7C);

		/* Clearing PCI_STATUS error reflected here */
		writeq(s2BIT(62), &bar0->txpic_int_reg);
	}

	/* Reset device statistics maintained by OS */
	memset(&sp->stats, 0, sizeof(struct net_device_stats));

	stats = sp->mac_control.stats_info;
	swstats = &stats->sw_stat;

	/* save link up/down time/cnt, reset/memory/watchdog cnt */
	up_cnt = swstats->link_up_cnt;
	down_cnt = swstats->link_down_cnt;
	up_time = swstats->link_up_time;
	down_time = swstats->link_down_time;
	reset_cnt = swstats->soft_reset_cnt;
	mem_alloc_cnt = swstats->mem_allocated;
	mem_free_cnt = swstats->mem_freed;
	watchdog_cnt = swstats->watchdog_timer_cnt;

	memset(stats, 0, sizeof(struct stat_block));

	/* restore link up/down time/cnt, reset/memory/watchdog cnt */
	swstats->link_up_cnt = up_cnt;
	swstats->link_down_cnt = down_cnt;
	swstats->link_up_time = up_time;
	swstats->link_down_time = down_time;
	swstats->soft_reset_cnt = reset_cnt;
	swstats->mem_allocated = mem_alloc_cnt;
	swstats->mem_freed = mem_free_cnt;
	swstats->watchdog_timer_cnt = watchdog_cnt;

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
	 * Clear spurious ECC interrupts that would have occurred on
	 * XFRAME II cards after reset.
	 */
	if (sp->device_type == XFRAME_II_DEVICE) {
		val64 = readq(&bar0->pcc_err_reg);
		writeq(val64, &bar0->pcc_err_reg);
	}

	sp->device_enabled_once = false;
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

static int s2io_set_swapper(struct s2io_nic *sp)
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
		static const u64 value[] = {
			0xC30000C3C30000C3ULL,	/* FE=1, SE=1 */
			0x8100008181000081ULL,	/* FE=1, SE=0 */
			0x4200004242000042ULL,	/* FE=0, SE=1 */
			0			/* FE=0, SE=0 */
		};

		while (i < 4) {
			writeq(value[i], &bar0->swapper_ctrl);
			val64 = readq(&bar0->pif_rd_swapper_fb);
			if (val64 == 0x0123456789ABCDEFULL)
				break;
			i++;
		}
		if (i == 4) {
			DBG_PRINT(ERR_DBG, "%s: Endian settings are wrong, "
				  "feedback read %llx\n",
				  dev->name, (unsigned long long)val64);
			return FAILURE;
		}
		valr = value[i];
	} else {
		valr = readq(&bar0->swapper_ctrl);
	}

	valt = 0x0123456789ABCDEFULL;
	writeq(valt, &bar0->xmsi_address);
	val64 = readq(&bar0->xmsi_address);

	if (val64 != valt) {
		int i = 0;
		static const u64 value[] = {
			0x00C3C30000C3C300ULL,	/* FE=1, SE=1 */
			0x0081810000818100ULL,	/* FE=1, SE=0 */
			0x0042420000424200ULL,	/* FE=0, SE=1 */
			0			/* FE=0, SE=0 */
		};

		while (i < 4) {
			writeq((value[i] | valr), &bar0->swapper_ctrl);
			writeq(valt, &bar0->xmsi_address);
			val64 = readq(&bar0->xmsi_address);
			if (val64 == valt)
				break;
			i++;
		}
		if (i == 4) {
			unsigned long long x = val64;
			DBG_PRINT(ERR_DBG,
				  "Write failed, Xmsi_addr reads:0x%llx\n", x);
			return FAILURE;
		}
	}
	val64 = readq(&bar0->swapper_ctrl);
	val64 &= 0xFFFF000000000000ULL;

#ifdef __BIG_ENDIAN
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
		  SWAPPER_CTRL_STATS_FE |
		  SWAPPER_CTRL_STATS_SE);
	if (sp->config.intr_type == INTA)
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
		  SWAPPER_CTRL_STATS_FE |
		  SWAPPER_CTRL_STATS_SE);
	if (sp->config.intr_type == INTA)
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
		DBG_PRINT(ERR_DBG,
			  "%s: Endian settings are wrong, feedback read %llx\n",
			  dev->name, (unsigned long long)val64);
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
		if (!(val64 & s2BIT(15)))
			break;
		mdelay(1);
		cnt++;
	} while (cnt < 5);
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
	int i, msix_index;

	if (nic->device_type == XFRAME_I_DEVICE)
		return;

	for (i = 0; i < MAX_REQUESTED_MSI_X; i++) {
		msix_index = (i) ? ((i-1) * 8 + 1) : 0;
		writeq(nic->msix_info[i].addr, &bar0->xmsi_address);
		writeq(nic->msix_info[i].data, &bar0->xmsi_data);
		val64 = (s2BIT(7) | s2BIT(15) | vBIT(msix_index, 26, 6));
		writeq(val64, &bar0->xmsi_access);
		if (wait_for_msix_trans(nic, msix_index)) {
			DBG_PRINT(ERR_DBG, "%s: index: %d failed\n",
				  __func__, msix_index);
			continue;
		}
	}
}

static void store_xmsi_data(struct s2io_nic *nic)
{
	struct XENA_dev_config __iomem *bar0 = nic->bar0;
	u64 val64, addr, data;
	int i, msix_index;

	if (nic->device_type == XFRAME_I_DEVICE)
		return;

	/* Store and display */
	for (i = 0; i < MAX_REQUESTED_MSI_X; i++) {
		msix_index = (i) ? ((i-1) * 8 + 1) : 0;
		val64 = (s2BIT(15) | vBIT(msix_index, 26, 6));
		writeq(val64, &bar0->xmsi_access);
		if (wait_for_msix_trans(nic, msix_index)) {
			DBG_PRINT(ERR_DBG, "%s: index: %d failed\n",
				  __func__, msix_index);
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

static int s2io_enable_msi_x(struct s2io_nic *nic)
{
	struct XENA_dev_config __iomem *bar0 = nic->bar0;
	u64 rx_mat;
	u16 msi_control; /* Temp variable */
	int ret, i, j, msix_indx = 1;
	int size;
	struct stat_block *stats = nic->mac_control.stats_info;
	struct swStat *swstats = &stats->sw_stat;

	size = nic->num_entries * sizeof(struct msix_entry);
	nic->entries = kzalloc(size, GFP_KERNEL);
	if (!nic->entries) {
		DBG_PRINT(INFO_DBG, "%s: Memory allocation failed\n",
			  __func__);
		swstats->mem_alloc_fail_cnt++;
		return -ENOMEM;
	}
	swstats->mem_allocated += size;

	size = nic->num_entries * sizeof(struct s2io_msix_entry);
	nic->s2io_entries = kzalloc(size, GFP_KERNEL);
	if (!nic->s2io_entries) {
		DBG_PRINT(INFO_DBG, "%s: Memory allocation failed\n",
			  __func__);
		swstats->mem_alloc_fail_cnt++;
		kfree(nic->entries);
		swstats->mem_freed
			+= (nic->num_entries * sizeof(struct msix_entry));
		return -ENOMEM;
	}
	swstats->mem_allocated += size;

	nic->entries[0].entry = 0;
	nic->s2io_entries[0].entry = 0;
	nic->s2io_entries[0].in_use = MSIX_FLG;
	nic->s2io_entries[0].type = MSIX_ALARM_TYPE;
	nic->s2io_entries[0].arg = &nic->mac_control.fifos;

	for (i = 1; i < nic->num_entries; i++) {
		nic->entries[i].entry = ((i - 1) * 8) + 1;
		nic->s2io_entries[i].entry = ((i - 1) * 8) + 1;
		nic->s2io_entries[i].arg = NULL;
		nic->s2io_entries[i].in_use = 0;
	}

	rx_mat = readq(&bar0->rx_mat);
	for (j = 0; j < nic->config.rx_ring_num; j++) {
		rx_mat |= RX_MAT_SET(j, msix_indx);
		nic->s2io_entries[j+1].arg = &nic->mac_control.rings[j];
		nic->s2io_entries[j+1].type = MSIX_RING_TYPE;
		nic->s2io_entries[j+1].in_use = MSIX_FLG;
		msix_indx += 8;
	}
	writeq(rx_mat, &bar0->rx_mat);
	readq(&bar0->rx_mat);

	ret = pci_enable_msix_range(nic->pdev, nic->entries,
				    nic->num_entries, nic->num_entries);
	/* We fail init if error or we get less vectors than min required */
	if (ret < 0) {
		DBG_PRINT(ERR_DBG, "Enabling MSI-X failed\n");
		kfree(nic->entries);
		swstats->mem_freed += nic->num_entries *
			sizeof(struct msix_entry);
		kfree(nic->s2io_entries);
		swstats->mem_freed += nic->num_entries *
			sizeof(struct s2io_msix_entry);
		nic->entries = NULL;
		nic->s2io_entries = NULL;
		return -ENOMEM;
	}

	/*
	 * To enable MSI-X, MSI also needs to be enabled, due to a bug
	 * in the herc NIC. (Temp change, needs to be removed later)
	 */
	pci_read_config_word(nic->pdev, 0x42, &msi_control);
	msi_control |= 0x1; /* Enable MSI */
	pci_write_config_word(nic->pdev, 0x42, msi_control);

	return 0;
}

/* Handle software interrupt used during MSI(X) test */
static irqreturn_t s2io_test_intr(int irq, void *dev_id)
{
	struct s2io_nic *sp = dev_id;

	sp->msi_detected = 1;
	wake_up(&sp->msi_wait);

	return IRQ_HANDLED;
}

/* Test interrupt path by forcing a a software IRQ */
static int s2io_test_msi(struct s2io_nic *sp)
{
	struct pci_dev *pdev = sp->pdev;
	struct XENA_dev_config __iomem *bar0 = sp->bar0;
	int err;
	u64 val64, saved64;

	err = request_irq(sp->entries[1].vector, s2io_test_intr, 0,
			  sp->name, sp);
	if (err) {
		DBG_PRINT(ERR_DBG, "%s: PCI %s: cannot assign irq %d\n",
			  sp->dev->name, pci_name(pdev), pdev->irq);
		return err;
	}

	init_waitqueue_head(&sp->msi_wait);
	sp->msi_detected = 0;

	saved64 = val64 = readq(&bar0->scheduled_int_ctrl);
	val64 |= SCHED_INT_CTRL_ONE_SHOT;
	val64 |= SCHED_INT_CTRL_TIMER_EN;
	val64 |= SCHED_INT_CTRL_INT2MSI(1);
	writeq(val64, &bar0->scheduled_int_ctrl);

	wait_event_timeout(sp->msi_wait, sp->msi_detected, HZ/10);

	if (!sp->msi_detected) {
		/* MSI(X) test failed, go back to INTx mode */
		DBG_PRINT(ERR_DBG, "%s: PCI %s: No interrupt was generated "
			  "using MSI(X) during test\n",
			  sp->dev->name, pci_name(pdev));

		err = -EOPNOTSUPP;
	}

	free_irq(sp->entries[1].vector, sp);

	writeq(saved64, &bar0->scheduled_int_ctrl);

	return err;
}

static void remove_msix_isr(struct s2io_nic *sp)
{
	int i;
	u16 msi_control;

	for (i = 0; i < sp->num_entries; i++) {
		if (sp->s2io_entries[i].in_use == MSIX_REGISTERED_SUCCESS) {
			int vector = sp->entries[i].vector;
			void *arg = sp->s2io_entries[i].arg;
			free_irq(vector, arg);
		}
	}

	kfree(sp->entries);
	kfree(sp->s2io_entries);
	sp->entries = NULL;
	sp->s2io_entries = NULL;

	pci_read_config_word(sp->pdev, 0x42, &msi_control);
	msi_control &= 0xFFFE; /* Disable MSI */
	pci_write_config_word(sp->pdev, 0x42, msi_control);

	pci_disable_msix(sp->pdev);
}

static void remove_inta_isr(struct s2io_nic *sp)
{
	free_irq(sp->pdev->irq, sp->dev);
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
	struct s2io_nic *sp = netdev_priv(dev);
	struct swStat *swstats = &sp->mac_control.stats_info->sw_stat;
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

	if (do_s2io_prog_unicast(dev, dev->dev_addr) == FAILURE) {
		DBG_PRINT(ERR_DBG, "Set Mac Address Failed\n");
		s2io_card_down(sp);
		err = -ENODEV;
		goto hw_init_failed;
	}
	s2io_start_all_tx_queue(sp);
	return 0;

hw_init_failed:
	if (sp->config.intr_type == MSI_X) {
		if (sp->entries) {
			kfree(sp->entries);
			swstats->mem_freed += sp->num_entries *
				sizeof(struct msix_entry);
		}
		if (sp->s2io_entries) {
			kfree(sp->s2io_entries);
			swstats->mem_freed += sp->num_entries *
				sizeof(struct s2io_msix_entry);
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
	struct s2io_nic *sp = netdev_priv(dev);
	struct config_param *config = &sp->config;
	u64 tmp64;
	int offset;

	/* Return if the device is already closed               *
	 *  Can happen when s2io_card_up failed in change_mtu    *
	 */
	if (!is_s2io_card_up(sp))
		return 0;

	s2io_stop_all_tx_queue(sp);
	/* delete all populated mac entries */
	for (offset = 1; offset < config->max_mc_addr; offset++) {
		tmp64 = do_s2io_read_unicast_mc(sp, offset);
		if (tmp64 != S2IO_DISABLE_MAC_ENTRY)
			do_s2io_delete_unicast_mc(sp, tmp64);
	}

	s2io_card_down(sp);

	return 0;
}

/**
 *  s2io_xmit - Tx entry point of te driver
 *  @skb : the socket buffer containing the Tx data.
 *  @dev : device pointer.
 *  Description :
 *  This function is the Tx entry point of the driver. S2IO NIC supports
 *  certain protocol assist features on Tx side, namely  CSO, S/G, LSO.
 *  NOTE: when device can't queue the pkt,just the trans_start variable will
 *  not be upadted.
 *  Return value:
 *  0 on success & 1 on failure.
 */

static netdev_tx_t s2io_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct s2io_nic *sp = netdev_priv(dev);
	u16 frg_cnt, frg_len, i, queue, queue_len, put_off, get_off;
	register u64 val64;
	struct TxD *txdp;
	struct TxFIFO_element __iomem *tx_fifo;
	unsigned long flags = 0;
	u16 vlan_tag = 0;
	struct fifo_info *fifo = NULL;
	int offload_type;
	int enable_per_list_interrupt = 0;
	struct config_param *config = &sp->config;
	struct mac_info *mac_control = &sp->mac_control;
	struct stat_block *stats = mac_control->stats_info;
	struct swStat *swstats = &stats->sw_stat;

	DBG_PRINT(TX_DBG, "%s: In Neterion Tx routine\n", dev->name);

	if (unlikely(skb->len <= 0)) {
		DBG_PRINT(TX_DBG, "%s: Buffer has no data..\n", dev->name);
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	if (!is_s2io_card_up(sp)) {
		DBG_PRINT(TX_DBG, "%s: Card going down for reset\n",
			  dev->name);
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	queue = 0;
	if (skb_vlan_tag_present(skb))
		vlan_tag = skb_vlan_tag_get(skb);
	if (sp->config.tx_steering_type == TX_DEFAULT_STEERING) {
		if (skb->protocol == htons(ETH_P_IP)) {
			struct iphdr *ip;
			struct tcphdr *th;
			ip = ip_hdr(skb);

			if (!ip_is_fragment(ip)) {
				th = (struct tcphdr *)(((unsigned char *)ip) +
						       ip->ihl*4);

				if (ip->protocol == IPPROTO_TCP) {
					queue_len = sp->total_tcp_fifos;
					queue = (ntohs(th->source) +
						 ntohs(th->dest)) &
						sp->fifo_selector[queue_len - 1];
					if (queue >= queue_len)
						queue = queue_len - 1;
				} else if (ip->protocol == IPPROTO_UDP) {
					queue_len = sp->total_udp_fifos;
					queue = (ntohs(th->source) +
						 ntohs(th->dest)) &
						sp->fifo_selector[queue_len - 1];
					if (queue >= queue_len)
						queue = queue_len - 1;
					queue += sp->udp_fifo_idx;
					if (skb->len > 1024)
						enable_per_list_interrupt = 1;
				}
			}
		}
	} else if (sp->config.tx_steering_type == TX_PRIORITY_STEERING)
		/* get fifo number based on skb->priority value */
		queue = config->fifo_mapping
			[skb->priority & (MAX_TX_FIFOS - 1)];
	fifo = &mac_control->fifos[queue];

	spin_lock_irqsave(&fifo->tx_lock, flags);

	if (sp->config.multiq) {
		if (__netif_subqueue_stopped(dev, fifo->fifo_no)) {
			spin_unlock_irqrestore(&fifo->tx_lock, flags);
			return NETDEV_TX_BUSY;
		}
	} else if (unlikely(fifo->queue_state == FIFO_QUEUE_STOP)) {
		if (netif_queue_stopped(dev)) {
			spin_unlock_irqrestore(&fifo->tx_lock, flags);
			return NETDEV_TX_BUSY;
		}
	}

	put_off = (u16)fifo->tx_curr_put_info.offset;
	get_off = (u16)fifo->tx_curr_get_info.offset;
	txdp = fifo->list_info[put_off].list_virt_addr;

	queue_len = fifo->tx_curr_put_info.fifo_len + 1;
	/* Avoid "put" pointer going beyond "get" pointer */
	if (txdp->Host_Control ||
	    ((put_off+1) == queue_len ? 0 : (put_off+1)) == get_off) {
		DBG_PRINT(TX_DBG, "Error in xmit, No free TXDs.\n");
		s2io_stop_tx_queue(sp, fifo->fifo_no);
		dev_kfree_skb_any(skb);
		spin_unlock_irqrestore(&fifo->tx_lock, flags);
		return NETDEV_TX_OK;
	}

	offload_type = s2io_offload_type(skb);
	if (offload_type & (SKB_GSO_TCPV4 | SKB_GSO_TCPV6)) {
		txdp->Control_1 |= TXD_TCP_LSO_EN;
		txdp->Control_1 |= TXD_TCP_LSO_MSS(s2io_tcp_mss(skb));
	}
	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		txdp->Control_2 |= (TXD_TX_CKO_IPV4_EN |
				    TXD_TX_CKO_TCP_EN |
				    TXD_TX_CKO_UDP_EN);
	}
	txdp->Control_1 |= TXD_GATHER_CODE_FIRST;
	txdp->Control_1 |= TXD_LIST_OWN_XENA;
	txdp->Control_2 |= TXD_INT_NUMBER(fifo->fifo_no);
	if (enable_per_list_interrupt)
		if (put_off & (queue_len >> 5))
			txdp->Control_2 |= TXD_INT_TYPE_PER_LIST;
	if (vlan_tag) {
		txdp->Control_2 |= TXD_VLAN_ENABLE;
		txdp->Control_2 |= TXD_VLAN_TAG(vlan_tag);
	}

	frg_len = skb_headlen(skb);
	txdp->Buffer_Pointer = pci_map_single(sp->pdev, skb->data,
					      frg_len, PCI_DMA_TODEVICE);
	if (pci_dma_mapping_error(sp->pdev, txdp->Buffer_Pointer))
		goto pci_map_failed;

	txdp->Host_Control = (unsigned long)skb;
	txdp->Control_1 |= TXD_BUFFER0_SIZE(frg_len);

	frg_cnt = skb_shinfo(skb)->nr_frags;
	/* For fragmented SKB. */
	for (i = 0; i < frg_cnt; i++) {
		const skb_frag_t *frag = &skb_shinfo(skb)->frags[i];
		/* A '0' length fragment will be ignored */
		if (!skb_frag_size(frag))
			continue;
		txdp++;
		txdp->Buffer_Pointer = (u64)skb_frag_dma_map(&sp->pdev->dev,
							     frag, 0,
							     skb_frag_size(frag),
							     DMA_TO_DEVICE);
		txdp->Control_1 = TXD_BUFFER0_SIZE(skb_frag_size(frag));
	}
	txdp->Control_1 |= TXD_GATHER_CODE_LAST;

	tx_fifo = mac_control->tx_FIFO_start[queue];
	val64 = fifo->list_info[put_off].list_phy_addr;
	writeq(val64, &tx_fifo->TxDL_Pointer);

	val64 = (TX_FIFO_LAST_TXD_NUM(frg_cnt) | TX_FIFO_FIRST_LIST |
		 TX_FIFO_LAST_LIST);
	if (offload_type)
		val64 |= TX_FIFO_SPECIAL_FUNC;

	writeq(val64, &tx_fifo->List_Control);

	mmiowb();

	put_off++;
	if (put_off == fifo->tx_curr_put_info.fifo_len + 1)
		put_off = 0;
	fifo->tx_curr_put_info.offset = put_off;

	/* Avoid "put" pointer going beyond "get" pointer */
	if (((put_off+1) == queue_len ? 0 : (put_off+1)) == get_off) {
		swstats->fifo_full_cnt++;
		DBG_PRINT(TX_DBG,
			  "No free TxDs for xmit, Put: 0x%x Get:0x%x\n",
			  put_off, get_off);
		s2io_stop_tx_queue(sp, fifo->fifo_no);
	}
	swstats->mem_allocated += skb->truesize;
	spin_unlock_irqrestore(&fifo->tx_lock, flags);

	if (sp->config.intr_type == MSI_X)
		tx_intr_handler(fifo);

	return NETDEV_TX_OK;

pci_map_failed:
	swstats->pci_map_fail_cnt++;
	s2io_stop_tx_queue(sp, fifo->fifo_no);
	swstats->mem_freed += skb->truesize;
	dev_kfree_skb_any(skb);
	spin_unlock_irqrestore(&fifo->tx_lock, flags);
	return NETDEV_TX_OK;
}

static void
s2io_alarm_handle(unsigned long data)
{
	struct s2io_nic *sp = (struct s2io_nic *)data;
	struct net_device *dev = sp->dev;

	s2io_handle_errors(dev);
	mod_timer(&sp->alarm_timer, jiffies + HZ / 2);
}

static irqreturn_t s2io_msix_ring_handle(int irq, void *dev_id)
{
	struct ring_info *ring = (struct ring_info *)dev_id;
	struct s2io_nic *sp = ring->nic;
	struct XENA_dev_config __iomem *bar0 = sp->bar0;

	if (unlikely(!is_s2io_card_up(sp)))
		return IRQ_HANDLED;

	if (sp->config.napi) {
		u8 __iomem *addr = NULL;
		u8 val8 = 0;

		addr = (u8 __iomem *)&bar0->xmsi_mask_reg;
		addr += (7 - ring->ring_no);
		val8 = (ring->ring_no == 0) ? 0x7f : 0xff;
		writeb(val8, addr);
		val8 = readb(addr);
		napi_schedule(&ring->napi);
	} else {
		rx_intr_handler(ring, 0);
		s2io_chk_rx_buffers(sp, ring);
	}

	return IRQ_HANDLED;
}

static irqreturn_t s2io_msix_fifo_handle(int irq, void *dev_id)
{
	int i;
	struct fifo_info *fifos = (struct fifo_info *)dev_id;
	struct s2io_nic *sp = fifos->nic;
	struct XENA_dev_config __iomem *bar0 = sp->bar0;
	struct config_param *config  = &sp->config;
	u64 reason;

	if (unlikely(!is_s2io_card_up(sp)))
		return IRQ_NONE;

	reason = readq(&bar0->general_int_status);
	if (unlikely(reason == S2IO_MINUS_ONE))
		/* Nothing much can be done. Get out */
		return IRQ_HANDLED;

	if (reason & (GEN_INTR_TXPIC | GEN_INTR_TXTRAFFIC)) {
		writeq(S2IO_MINUS_ONE, &bar0->general_int_mask);

		if (reason & GEN_INTR_TXPIC)
			s2io_txpic_intr_handle(sp);

		if (reason & GEN_INTR_TXTRAFFIC)
			writeq(S2IO_MINUS_ONE, &bar0->tx_traffic_int);

		for (i = 0; i < config->tx_fifo_num; i++)
			tx_intr_handler(&fifos[i]);

		writeq(sp->general_int_mask, &bar0->general_int_mask);
		readl(&bar0->general_int_status);
		return IRQ_HANDLED;
	}
	/* The interrupt was not raised by us */
	return IRQ_NONE;
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
			val64 |= GPIO_INT_REG_LINK_DOWN;
			val64 |= GPIO_INT_REG_LINK_UP;
			writeq(val64, &bar0->gpio_int_reg);
			val64 = readq(&bar0->gpio_int_mask);
			val64 &= ~(GPIO_INT_MASK_LINK_UP |
				   GPIO_INT_MASK_LINK_DOWN);
			writeq(val64, &bar0->gpio_int_mask);
		} else if (val64 & GPIO_INT_REG_LINK_UP) {
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

		} else if (val64 & GPIO_INT_REG_LINK_DOWN) {
			val64 = readq(&bar0->adapter_status);
			s2io_link(sp, LINK_DOWN);
			/* Link is down so unmaks link up interrupt */
			val64 = readq(&bar0->gpio_int_mask);
			val64 &= ~GPIO_INT_MASK_LINK_UP;
			val64 |= GPIO_INT_MASK_LINK_DOWN;
			writeq(val64, &bar0->gpio_int_mask);

			/* turn off LED */
			val64 = readq(&bar0->adapter_control);
			val64 = val64 & (~ADAPTER_LED_ON);
			writeq(val64, &bar0->adapter_control);
		}
	}
	val64 = readq(&bar0->gpio_int_mask);
}

/**
 *  do_s2io_chk_alarm_bit - Check for alarm and incrment the counter
 *  @value: alarm bits
 *  @addr: address value
 *  @cnt: counter variable
 *  Description: Check for alarm and increment the counter
 *  Return Value:
 *  1 - if alarm bit set
 *  0 - if alarm bit is not set
 */
static int do_s2io_chk_alarm_bit(u64 value, void __iomem *addr,
				 unsigned long long *cnt)
{
	u64 val64;
	val64 = readq(addr);
	if (val64 & value) {
		writeq(val64, addr);
		(*cnt)++;
		return 1;
	}
	return 0;

}

/**
 *  s2io_handle_errors - Xframe error indication handler
 *  @nic: device private variable
 *  Description: Handle alarms such as loss of link, single or
 *  double ECC errors, critical and serious errors.
 *  Return Value:
 *  NONE
 */
static void s2io_handle_errors(void *dev_id)
{
	struct net_device *dev = (struct net_device *)dev_id;
	struct s2io_nic *sp = netdev_priv(dev);
	struct XENA_dev_config __iomem *bar0 = sp->bar0;
	u64 temp64 = 0, val64 = 0;
	int i = 0;

	struct swStat *sw_stat = &sp->mac_control.stats_info->sw_stat;
	struct xpakStat *stats = &sp->mac_control.stats_info->xpak_stat;

	if (!is_s2io_card_up(sp))
		return;

	if (pci_channel_offline(sp->pdev))
		return;

	memset(&sw_stat->ring_full_cnt, 0,
	       sizeof(sw_stat->ring_full_cnt));

	/* Handling the XPAK counters update */
	if (stats->xpak_timer_count < 72000) {
		/* waiting for an hour */
		stats->xpak_timer_count++;
	} else {
		s2io_updt_xpak_counter(dev);
		/* reset the count to zero */
		stats->xpak_timer_count = 0;
	}

	/* Handling link status change error Intr */
	if (s2io_link_fault_indication(sp) == MAC_RMAC_ERR_TIMER) {
		val64 = readq(&bar0->mac_rmac_err_reg);
		writeq(val64, &bar0->mac_rmac_err_reg);
		if (val64 & RMAC_LINK_STATE_CHANGE_INT)
			schedule_work(&sp->set_link_task);
	}

	/* In case of a serious error, the device will be Reset. */
	if (do_s2io_chk_alarm_bit(SERR_SOURCE_ANY, &bar0->serr_source,
				  &sw_stat->serious_err_cnt))
		goto reset;

	/* Check for data parity error */
	if (do_s2io_chk_alarm_bit(GPIO_INT_REG_DP_ERR_INT, &bar0->gpio_int_reg,
				  &sw_stat->parity_err_cnt))
		goto reset;

	/* Check for ring full counter */
	if (sp->device_type == XFRAME_II_DEVICE) {
		val64 = readq(&bar0->ring_bump_counter1);
		for (i = 0; i < 4; i++) {
			temp64 = (val64 & vBIT(0xFFFF, (i*16), 16));
			temp64 >>= 64 - ((i+1)*16);
			sw_stat->ring_full_cnt[i] += temp64;
		}

		val64 = readq(&bar0->ring_bump_counter2);
		for (i = 0; i < 4; i++) {
			temp64 = (val64 & vBIT(0xFFFF, (i*16), 16));
			temp64 >>= 64 - ((i+1)*16);
			sw_stat->ring_full_cnt[i+4] += temp64;
		}
	}

	val64 = readq(&bar0->txdma_int_status);
	/*check for pfc_err*/
	if (val64 & TXDMA_PFC_INT) {
		if (do_s2io_chk_alarm_bit(PFC_ECC_DB_ERR | PFC_SM_ERR_ALARM |
					  PFC_MISC_0_ERR | PFC_MISC_1_ERR |
					  PFC_PCIX_ERR,
					  &bar0->pfc_err_reg,
					  &sw_stat->pfc_err_cnt))
			goto reset;
		do_s2io_chk_alarm_bit(PFC_ECC_SG_ERR,
				      &bar0->pfc_err_reg,
				      &sw_stat->pfc_err_cnt);
	}

	/*check for tda_err*/
	if (val64 & TXDMA_TDA_INT) {
		if (do_s2io_chk_alarm_bit(TDA_Fn_ECC_DB_ERR |
					  TDA_SM0_ERR_ALARM |
					  TDA_SM1_ERR_ALARM,
					  &bar0->tda_err_reg,
					  &sw_stat->tda_err_cnt))
			goto reset;
		do_s2io_chk_alarm_bit(TDA_Fn_ECC_SG_ERR | TDA_PCIX_ERR,
				      &bar0->tda_err_reg,
				      &sw_stat->tda_err_cnt);
	}
	/*check for pcc_err*/
	if (val64 & TXDMA_PCC_INT) {
		if (do_s2io_chk_alarm_bit(PCC_SM_ERR_ALARM | PCC_WR_ERR_ALARM |
					  PCC_N_SERR | PCC_6_COF_OV_ERR |
					  PCC_7_COF_OV_ERR | PCC_6_LSO_OV_ERR |
					  PCC_7_LSO_OV_ERR | PCC_FB_ECC_DB_ERR |
					  PCC_TXB_ECC_DB_ERR,
					  &bar0->pcc_err_reg,
					  &sw_stat->pcc_err_cnt))
			goto reset;
		do_s2io_chk_alarm_bit(PCC_FB_ECC_SG_ERR | PCC_TXB_ECC_SG_ERR,
				      &bar0->pcc_err_reg,
				      &sw_stat->pcc_err_cnt);
	}

	/*check for tti_err*/
	if (val64 & TXDMA_TTI_INT) {
		if (do_s2io_chk_alarm_bit(TTI_SM_ERR_ALARM,
					  &bar0->tti_err_reg,
					  &sw_stat->tti_err_cnt))
			goto reset;
		do_s2io_chk_alarm_bit(TTI_ECC_SG_ERR | TTI_ECC_DB_ERR,
				      &bar0->tti_err_reg,
				      &sw_stat->tti_err_cnt);
	}

	/*check for lso_err*/
	if (val64 & TXDMA_LSO_INT) {
		if (do_s2io_chk_alarm_bit(LSO6_ABORT | LSO7_ABORT |
					  LSO6_SM_ERR_ALARM | LSO7_SM_ERR_ALARM,
					  &bar0->lso_err_reg,
					  &sw_stat->lso_err_cnt))
			goto reset;
		do_s2io_chk_alarm_bit(LSO6_SEND_OFLOW | LSO7_SEND_OFLOW,
				      &bar0->lso_err_reg,
				      &sw_stat->lso_err_cnt);
	}

	/*check for tpa_err*/
	if (val64 & TXDMA_TPA_INT) {
		if (do_s2io_chk_alarm_bit(TPA_SM_ERR_ALARM,
					  &bar0->tpa_err_reg,
					  &sw_stat->tpa_err_cnt))
			goto reset;
		do_s2io_chk_alarm_bit(TPA_TX_FRM_DROP,
				      &bar0->tpa_err_reg,
				      &sw_stat->tpa_err_cnt);
	}

	/*check for sm_err*/
	if (val64 & TXDMA_SM_INT) {
		if (do_s2io_chk_alarm_bit(SM_SM_ERR_ALARM,
					  &bar0->sm_err_reg,
					  &sw_stat->sm_err_cnt))
			goto reset;
	}

	val64 = readq(&bar0->mac_int_status);
	if (val64 & MAC_INT_STATUS_TMAC_INT) {
		if (do_s2io_chk_alarm_bit(TMAC_TX_BUF_OVRN | TMAC_TX_SM_ERR,
					  &bar0->mac_tmac_err_reg,
					  &sw_stat->mac_tmac_err_cnt))
			goto reset;
		do_s2io_chk_alarm_bit(TMAC_ECC_SG_ERR | TMAC_ECC_DB_ERR |
				      TMAC_DESC_ECC_SG_ERR |
				      TMAC_DESC_ECC_DB_ERR,
				      &bar0->mac_tmac_err_reg,
				      &sw_stat->mac_tmac_err_cnt);
	}

	val64 = readq(&bar0->xgxs_int_status);
	if (val64 & XGXS_INT_STATUS_TXGXS) {
		if (do_s2io_chk_alarm_bit(TXGXS_ESTORE_UFLOW | TXGXS_TX_SM_ERR,
					  &bar0->xgxs_txgxs_err_reg,
					  &sw_stat->xgxs_txgxs_err_cnt))
			goto reset;
		do_s2io_chk_alarm_bit(TXGXS_ECC_SG_ERR | TXGXS_ECC_DB_ERR,
				      &bar0->xgxs_txgxs_err_reg,
				      &sw_stat->xgxs_txgxs_err_cnt);
	}

	val64 = readq(&bar0->rxdma_int_status);
	if (val64 & RXDMA_INT_RC_INT_M) {
		if (do_s2io_chk_alarm_bit(RC_PRCn_ECC_DB_ERR |
					  RC_FTC_ECC_DB_ERR |
					  RC_PRCn_SM_ERR_ALARM |
					  RC_FTC_SM_ERR_ALARM,
					  &bar0->rc_err_reg,
					  &sw_stat->rc_err_cnt))
			goto reset;
		do_s2io_chk_alarm_bit(RC_PRCn_ECC_SG_ERR |
				      RC_FTC_ECC_SG_ERR |
				      RC_RDA_FAIL_WR_Rn, &bar0->rc_err_reg,
				      &sw_stat->rc_err_cnt);
		if (do_s2io_chk_alarm_bit(PRC_PCI_AB_RD_Rn |
					  PRC_PCI_AB_WR_Rn |
					  PRC_PCI_AB_F_WR_Rn,
					  &bar0->prc_pcix_err_reg,
					  &sw_stat->prc_pcix_err_cnt))
			goto reset;
		do_s2io_chk_alarm_bit(PRC_PCI_DP_RD_Rn |
				      PRC_PCI_DP_WR_Rn |
				      PRC_PCI_DP_F_WR_Rn,
				      &bar0->prc_pcix_err_reg,
				      &sw_stat->prc_pcix_err_cnt);
	}

	if (val64 & RXDMA_INT_RPA_INT_M) {
		if (do_s2io_chk_alarm_bit(RPA_SM_ERR_ALARM | RPA_CREDIT_ERR,
					  &bar0->rpa_err_reg,
					  &sw_stat->rpa_err_cnt))
			goto reset;
		do_s2io_chk_alarm_bit(RPA_ECC_SG_ERR | RPA_ECC_DB_ERR,
				      &bar0->rpa_err_reg,
				      &sw_stat->rpa_err_cnt);
	}

	if (val64 & RXDMA_INT_RDA_INT_M) {
		if (do_s2io_chk_alarm_bit(RDA_RXDn_ECC_DB_ERR |
					  RDA_FRM_ECC_DB_N_AERR |
					  RDA_SM1_ERR_ALARM |
					  RDA_SM0_ERR_ALARM |
					  RDA_RXD_ECC_DB_SERR,
					  &bar0->rda_err_reg,
					  &sw_stat->rda_err_cnt))
			goto reset;
		do_s2io_chk_alarm_bit(RDA_RXDn_ECC_SG_ERR |
				      RDA_FRM_ECC_SG_ERR |
				      RDA_MISC_ERR |
				      RDA_PCIX_ERR,
				      &bar0->rda_err_reg,
				      &sw_stat->rda_err_cnt);
	}

	if (val64 & RXDMA_INT_RTI_INT_M) {
		if (do_s2io_chk_alarm_bit(RTI_SM_ERR_ALARM,
					  &bar0->rti_err_reg,
					  &sw_stat->rti_err_cnt))
			goto reset;
		do_s2io_chk_alarm_bit(RTI_ECC_SG_ERR | RTI_ECC_DB_ERR,
				      &bar0->rti_err_reg,
				      &sw_stat->rti_err_cnt);
	}

	val64 = readq(&bar0->mac_int_status);
	if (val64 & MAC_INT_STATUS_RMAC_INT) {
		if (do_s2io_chk_alarm_bit(RMAC_RX_BUFF_OVRN | RMAC_RX_SM_ERR,
					  &bar0->mac_rmac_err_reg,
					  &sw_stat->mac_rmac_err_cnt))
			goto reset;
		do_s2io_chk_alarm_bit(RMAC_UNUSED_INT |
				      RMAC_SINGLE_ECC_ERR |
				      RMAC_DOUBLE_ECC_ERR,
				      &bar0->mac_rmac_err_reg,
				      &sw_stat->mac_rmac_err_cnt);
	}

	val64 = readq(&bar0->xgxs_int_status);
	if (val64 & XGXS_INT_STATUS_RXGXS) {
		if (do_s2io_chk_alarm_bit(RXGXS_ESTORE_OFLOW | RXGXS_RX_SM_ERR,
					  &bar0->xgxs_rxgxs_err_reg,
					  &sw_stat->xgxs_rxgxs_err_cnt))
			goto reset;
	}

	val64 = readq(&bar0->mc_int_status);
	if (val64 & MC_INT_STATUS_MC_INT) {
		if (do_s2io_chk_alarm_bit(MC_ERR_REG_SM_ERR,
					  &bar0->mc_err_reg,
					  &sw_stat->mc_err_cnt))
			goto reset;

		/* Handling Ecc errors */
		if (val64 & (MC_ERR_REG_ECC_ALL_SNG | MC_ERR_REG_ECC_ALL_DBL)) {
			writeq(val64, &bar0->mc_err_reg);
			if (val64 & MC_ERR_REG_ECC_ALL_DBL) {
				sw_stat->double_ecc_errs++;
				if (sp->device_type != XFRAME_II_DEVICE) {
					/*
					 * Reset XframeI only if critical error
					 */
					if (val64 &
					    (MC_ERR_REG_MIRI_ECC_DB_ERR_0 |
					     MC_ERR_REG_MIRI_ECC_DB_ERR_1))
						goto reset;
				}
			} else
				sw_stat->single_ecc_errs++;
		}
	}
	return;

reset:
	s2io_stop_all_tx_queue(sp);
	schedule_work(&sp->rst_timer_task);
	sw_stat->soft_reset_cnt++;
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
	struct net_device *dev = (struct net_device *)dev_id;
	struct s2io_nic *sp = netdev_priv(dev);
	struct XENA_dev_config __iomem *bar0 = sp->bar0;
	int i;
	u64 reason = 0;
	struct mac_info *mac_control;
	struct config_param *config;

	/* Pretend we handled any irq's from a disconnected card */
	if (pci_channel_offline(sp->pdev))
		return IRQ_NONE;

	if (!is_s2io_card_up(sp))
		return IRQ_NONE;

	config = &sp->config;
	mac_control = &sp->mac_control;

	/*
	 * Identify the cause for interrupt and call the appropriate
	 * interrupt handler. Causes for the interrupt could be;
	 * 1. Rx of packet.
	 * 2. Tx complete.
	 * 3. Link down.
	 */
	reason = readq(&bar0->general_int_status);

	if (unlikely(reason == S2IO_MINUS_ONE))
		return IRQ_HANDLED;	/* Nothing much can be done. Get out */

	if (reason &
	    (GEN_INTR_RXTRAFFIC | GEN_INTR_TXTRAFFIC | GEN_INTR_TXPIC)) {
		writeq(S2IO_MINUS_ONE, &bar0->general_int_mask);

		if (config->napi) {
			if (reason & GEN_INTR_RXTRAFFIC) {
				napi_schedule(&sp->napi);
				writeq(S2IO_MINUS_ONE, &bar0->rx_traffic_mask);
				writeq(S2IO_MINUS_ONE, &bar0->rx_traffic_int);
				readl(&bar0->rx_traffic_int);
			}
		} else {
			/*
			 * rx_traffic_int reg is an R1 register, writing all 1's
			 * will ensure that the actual interrupt causing bit
			 * get's cleared and hence a read can be avoided.
			 */
			if (reason & GEN_INTR_RXTRAFFIC)
				writeq(S2IO_MINUS_ONE, &bar0->rx_traffic_int);

			for (i = 0; i < config->rx_ring_num; i++) {
				struct ring_info *ring = &mac_control->rings[i];

				rx_intr_handler(ring, 0);
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
		 * Reallocate the buffers from the interrupt handler itself.
		 */
		if (!config->napi) {
			for (i = 0; i < config->rx_ring_num; i++) {
				struct ring_info *ring = &mac_control->rings[i];

				s2io_chk_rx_buffers(sp, ring);
			}
		}
		writeq(sp->general_int_mask, &bar0->general_int_mask);
		readl(&bar0->general_int_status);

		return IRQ_HANDLED;

	} else if (!reason) {
		/* The interrupt was not raised by us */
		return IRQ_NONE;
	}

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

	if (is_s2io_card_up(sp)) {
		/* Apprx 30us on a 133 MHz bus */
		val64 = SET_UPDT_CLICKS(10) |
			STAT_CFG_ONE_SHOT_EN | STAT_CFG_STAT_EN;
		writeq(val64, &bar0->stat_cfg);
		do {
			udelay(100);
			val64 = readq(&bar0->stat_cfg);
			if (!(val64 & s2BIT(0)))
				break;
			cnt++;
			if (cnt == 5)
				break; /* Updt failed */
		} while (1);
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
	struct s2io_nic *sp = netdev_priv(dev);
	struct mac_info *mac_control = &sp->mac_control;
	struct stat_block *stats = mac_control->stats_info;
	u64 delta;

	/* Configure Stats for immediate updt */
	s2io_updt_stats(sp);

	/* A device reset will cause the on-adapter statistics to be zero'ed.
	 * This can be done while running by changing the MTU.  To prevent the
	 * system from having the stats zero'ed, the driver keeps a copy of the
	 * last update to the system (which is also zero'ed on reset).  This
	 * enables the driver to accurately know the delta between the last
	 * update and the current update.
	 */
	delta = ((u64) le32_to_cpu(stats->rmac_vld_frms_oflow) << 32 |
		le32_to_cpu(stats->rmac_vld_frms)) - sp->stats.rx_packets;
	sp->stats.rx_packets += delta;
	dev->stats.rx_packets += delta;

	delta = ((u64) le32_to_cpu(stats->tmac_frms_oflow) << 32 |
		le32_to_cpu(stats->tmac_frms)) - sp->stats.tx_packets;
	sp->stats.tx_packets += delta;
	dev->stats.tx_packets += delta;

	delta = ((u64) le32_to_cpu(stats->rmac_data_octets_oflow) << 32 |
		le32_to_cpu(stats->rmac_data_octets)) - sp->stats.rx_bytes;
	sp->stats.rx_bytes += delta;
	dev->stats.rx_bytes += delta;

	delta = ((u64) le32_to_cpu(stats->tmac_data_octets_oflow) << 32 |
		le32_to_cpu(stats->tmac_data_octets)) - sp->stats.tx_bytes;
	sp->stats.tx_bytes += delta;
	dev->stats.tx_bytes += delta;

	delta = le64_to_cpu(stats->rmac_drop_frms) - sp->stats.rx_errors;
	sp->stats.rx_errors += delta;
	dev->stats.rx_errors += delta;

	delta = ((u64) le32_to_cpu(stats->tmac_any_err_frms_oflow) << 32 |
		le32_to_cpu(stats->tmac_any_err_frms)) - sp->stats.tx_errors;
	sp->stats.tx_errors += delta;
	dev->stats.tx_errors += delta;

	delta = le64_to_cpu(stats->rmac_drop_frms) - sp->stats.rx_dropped;
	sp->stats.rx_dropped += delta;
	dev->stats.rx_dropped += delta;

	delta = le64_to_cpu(stats->tmac_drop_frms) - sp->stats.tx_dropped;
	sp->stats.tx_dropped += delta;
	dev->stats.tx_dropped += delta;

	/* The adapter MAC interprets pause frames as multicast packets, but
	 * does not pass them up.  This erroneously increases the multicast
	 * packet count and needs to be deducted when the multicast frame count
	 * is queried.
	 */
	delta = (u64) le32_to_cpu(stats->rmac_vld_mcst_frms_oflow) << 32 |
		le32_to_cpu(stats->rmac_vld_mcst_frms);
	delta -= le64_to_cpu(stats->rmac_pause_ctrl_frms);
	delta -= sp->stats.multicast;
	sp->stats.multicast += delta;
	dev->stats.multicast += delta;

	delta = ((u64) le32_to_cpu(stats->rmac_usized_frms_oflow) << 32 |
		le32_to_cpu(stats->rmac_usized_frms)) +
		le64_to_cpu(stats->rmac_long_frms) - sp->stats.rx_length_errors;
	sp->stats.rx_length_errors += delta;
	dev->stats.rx_length_errors += delta;

	delta = le64_to_cpu(stats->rmac_fcs_err_frms) - sp->stats.rx_crc_errors;
	sp->stats.rx_crc_errors += delta;
	dev->stats.rx_crc_errors += delta;

	return &dev->stats;
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
	struct netdev_hw_addr *ha;
	struct s2io_nic *sp = netdev_priv(dev);
	struct XENA_dev_config __iomem *bar0 = sp->bar0;
	u64 val64 = 0, multi_mac = 0x010203040506ULL, mask =
		0xfeffffffffffULL;
	u64 dis_addr = S2IO_DISABLE_MAC_ENTRY, mac_addr = 0;
	void __iomem *add;
	struct config_param *config = &sp->config;

	if ((dev->flags & IFF_ALLMULTI) && (!sp->m_cast_flg)) {
		/*  Enable all Multicast addresses */
		writeq(RMAC_ADDR_DATA0_MEM_ADDR(multi_mac),
		       &bar0->rmac_addr_data0_mem);
		writeq(RMAC_ADDR_DATA1_MEM_MASK(mask),
		       &bar0->rmac_addr_data1_mem);
		val64 = RMAC_ADDR_CMD_MEM_WE |
			RMAC_ADDR_CMD_MEM_STROBE_NEW_CMD |
			RMAC_ADDR_CMD_MEM_OFFSET(config->max_mc_addr - 1);
		writeq(val64, &bar0->rmac_addr_cmd_mem);
		/* Wait till command completes */
		wait_for_cmd_complete(&bar0->rmac_addr_cmd_mem,
				      RMAC_ADDR_CMD_MEM_STROBE_CMD_EXECUTING,
				      S2IO_BIT_RESET);

		sp->m_cast_flg = 1;
		sp->all_multi_pos = config->max_mc_addr - 1;
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
		writel((u32)val64, add);
		writeq(RMAC_CFG_KEY(0x4C0D), &bar0->rmac_cfg_key);
		writel((u32) (val64 >> 32), (add + 4));

		if (vlan_tag_strip != 1) {
			val64 = readq(&bar0->rx_pa_cfg);
			val64 &= ~RX_PA_CFG_STRIP_VLAN_TAG;
			writeq(val64, &bar0->rx_pa_cfg);
			sp->vlan_strip_flag = 0;
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
		writel((u32)val64, add);
		writeq(RMAC_CFG_KEY(0x4C0D), &bar0->rmac_cfg_key);
		writel((u32) (val64 >> 32), (add + 4));

		if (vlan_tag_strip != 0) {
			val64 = readq(&bar0->rx_pa_cfg);
			val64 |= RX_PA_CFG_STRIP_VLAN_TAG;
			writeq(val64, &bar0->rx_pa_cfg);
			sp->vlan_strip_flag = 1;
		}

		val64 = readq(&bar0->mac_cfg);
		sp->promisc_flg = 0;
		DBG_PRINT(INFO_DBG, "%s: left promiscuous mode\n", dev->name);
	}

	/*  Update individual M_CAST address list */
	if ((!sp->m_cast_flg) && netdev_mc_count(dev)) {
		if (netdev_mc_count(dev) >
		    (config->max_mc_addr - config->max_mac_addr)) {
			DBG_PRINT(ERR_DBG,
				  "%s: No more Rx filters can be added - "
				  "please enable ALL_MULTI instead\n",
				  dev->name);
			return;
		}

		prev_cnt = sp->mc_addr_count;
		sp->mc_addr_count = netdev_mc_count(dev);

		/* Clear out the previous list of Mc in the H/W. */
		for (i = 0; i < prev_cnt; i++) {
			writeq(RMAC_ADDR_DATA0_MEM_ADDR(dis_addr),
			       &bar0->rmac_addr_data0_mem);
			writeq(RMAC_ADDR_DATA1_MEM_MASK(0ULL),
			       &bar0->rmac_addr_data1_mem);
			val64 = RMAC_ADDR_CMD_MEM_WE |
				RMAC_ADDR_CMD_MEM_STROBE_NEW_CMD |
				RMAC_ADDR_CMD_MEM_OFFSET
				(config->mc_start_offset + i);
			writeq(val64, &bar0->rmac_addr_cmd_mem);

			/* Wait for command completes */
			if (wait_for_cmd_complete(&bar0->rmac_addr_cmd_mem,
						  RMAC_ADDR_CMD_MEM_STROBE_CMD_EXECUTING,
						  S2IO_BIT_RESET)) {
				DBG_PRINT(ERR_DBG,
					  "%s: Adding Multicasts failed\n",
					  dev->name);
				return;
			}
		}

		/* Create the new Rx filter list and update the same in H/W. */
		i = 0;
		netdev_for_each_mc_addr(ha, dev) {
			mac_addr = 0;
			for (j = 0; j < ETH_ALEN; j++) {
				mac_addr |= ha->addr[j];
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
				(i + config->mc_start_offset);
			writeq(val64, &bar0->rmac_addr_cmd_mem);

			/* Wait for command completes */
			if (wait_for_cmd_complete(&bar0->rmac_addr_cmd_mem,
						  RMAC_ADDR_CMD_MEM_STROBE_CMD_EXECUTING,
						  S2IO_BIT_RESET)) {
				DBG_PRINT(ERR_DBG,
					  "%s: Adding Multicasts failed\n",
					  dev->name);
				return;
			}
			i++;
		}
	}
}

/* read from CAM unicast & multicast addresses and store it in
 * def_mac_addr structure
 */
static void do_s2io_store_unicast_mc(struct s2io_nic *sp)
{
	int offset;
	u64 mac_addr = 0x0;
	struct config_param *config = &sp->config;

	/* store unicast & multicast mac addresses */
	for (offset = 0; offset < config->max_mc_addr; offset++) {
		mac_addr = do_s2io_read_unicast_mc(sp, offset);
		/* if read fails disable the entry */
		if (mac_addr == FAILURE)
			mac_addr = S2IO_DISABLE_MAC_ENTRY;
		do_s2io_copy_mac_addr(sp, offset, mac_addr);
	}
}

/* restore unicast & multicast MAC to CAM from def_mac_addr structure */
static void do_s2io_restore_unicast_mc(struct s2io_nic *sp)
{
	int offset;
	struct config_param *config = &sp->config;
	/* restore unicast mac address */
	for (offset = 0; offset < config->max_mac_addr; offset++)
		do_s2io_prog_unicast(sp->dev,
				     sp->def_mac_addr[offset].mac_addr);

	/* restore multicast mac address */
	for (offset = config->mc_start_offset;
	     offset < config->max_mc_addr; offset++)
		do_s2io_add_mc(sp, sp->def_mac_addr[offset].mac_addr);
}

/* add a multicast MAC address to CAM */
static int do_s2io_add_mc(struct s2io_nic *sp, u8 *addr)
{
	int i;
	u64 mac_addr = 0;
	struct config_param *config = &sp->config;

	for (i = 0; i < ETH_ALEN; i++) {
		mac_addr <<= 8;
		mac_addr |= addr[i];
	}
	if ((0ULL == mac_addr) || (mac_addr == S2IO_DISABLE_MAC_ENTRY))
		return SUCCESS;

	/* check if the multicast mac already preset in CAM */
	for (i = config->mc_start_offset; i < config->max_mc_addr; i++) {
		u64 tmp64;
		tmp64 = do_s2io_read_unicast_mc(sp, i);
		if (tmp64 == S2IO_DISABLE_MAC_ENTRY) /* CAM entry is empty */
			break;

		if (tmp64 == mac_addr)
			return SUCCESS;
	}
	if (i == config->max_mc_addr) {
		DBG_PRINT(ERR_DBG,
			  "CAM full no space left for multicast MAC\n");
		return FAILURE;
	}
	/* Update the internal structure with this new mac address */
	do_s2io_copy_mac_addr(sp, i, mac_addr);

	return do_s2io_add_mac(sp, mac_addr, i);
}

/* add MAC address to CAM */
static int do_s2io_add_mac(struct s2io_nic *sp, u64 addr, int off)
{
	u64 val64;
	struct XENA_dev_config __iomem *bar0 = sp->bar0;

	writeq(RMAC_ADDR_DATA0_MEM_ADDR(addr),
	       &bar0->rmac_addr_data0_mem);

	val64 =	RMAC_ADDR_CMD_MEM_WE | RMAC_ADDR_CMD_MEM_STROBE_NEW_CMD |
		RMAC_ADDR_CMD_MEM_OFFSET(off);
	writeq(val64, &bar0->rmac_addr_cmd_mem);

	/* Wait till command completes */
	if (wait_for_cmd_complete(&bar0->rmac_addr_cmd_mem,
				  RMAC_ADDR_CMD_MEM_STROBE_CMD_EXECUTING,
				  S2IO_BIT_RESET)) {
		DBG_PRINT(INFO_DBG, "do_s2io_add_mac failed\n");
		return FAILURE;
	}
	return SUCCESS;
}
/* deletes a specified unicast/multicast mac entry from CAM */
static int do_s2io_delete_unicast_mc(struct s2io_nic *sp, u64 addr)
{
	int offset;
	u64 dis_addr = S2IO_DISABLE_MAC_ENTRY, tmp64;
	struct config_param *config = &sp->config;

	for (offset = 1;
	     offset < config->max_mc_addr; offset++) {
		tmp64 = do_s2io_read_unicast_mc(sp, offset);
		if (tmp64 == addr) {
			/* disable the entry by writing  0xffffffffffffULL */
			if (do_s2io_add_mac(sp, dis_addr, offset) ==  FAILURE)
				return FAILURE;
			/* store the new mac list from CAM */
			do_s2io_store_unicast_mc(sp);
			return SUCCESS;
		}
	}
	DBG_PRINT(ERR_DBG, "MAC address 0x%llx not found in CAM\n",
		  (unsigned long long)addr);
	return FAILURE;
}

/* read mac entries from CAM */
static u64 do_s2io_read_unicast_mc(struct s2io_nic *sp, int offset)
{
	u64 tmp64 = 0xffffffffffff0000ULL, val64;
	struct XENA_dev_config __iomem *bar0 = sp->bar0;

	/* read mac addr */
	val64 =	RMAC_ADDR_CMD_MEM_RD | RMAC_ADDR_CMD_MEM_STROBE_NEW_CMD |
		RMAC_ADDR_CMD_MEM_OFFSET(offset);
	writeq(val64, &bar0->rmac_addr_cmd_mem);

	/* Wait till command completes */
	if (wait_for_cmd_complete(&bar0->rmac_addr_cmd_mem,
				  RMAC_ADDR_CMD_MEM_STROBE_CMD_EXECUTING,
				  S2IO_BIT_RESET)) {
		DBG_PRINT(INFO_DBG, "do_s2io_read_unicast_mc failed\n");
		return FAILURE;
	}
	tmp64 = readq(&bar0->rmac_addr_data0_mem);

	return tmp64 >> 16;
}

/**
 * s2io_set_mac_addr - driver entry point
 */

static int s2io_set_mac_addr(struct net_device *dev, void *p)
{
	struct sockaddr *addr = p;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);

	/* store the MAC address in CAM */
	return do_s2io_prog_unicast(dev, dev->dev_addr);
}
/**
 *  do_s2io_prog_unicast - Programs the Xframe mac address
 *  @dev : pointer to the device structure.
 *  @addr: a uchar pointer to the new mac address which is to be set.
 *  Description : This procedure will program the Xframe to receive
 *  frames with new Mac Address
 *  Return value: SUCCESS on success and an appropriate (-)ve integer
 *  as defined in errno.h file on failure.
 */

static int do_s2io_prog_unicast(struct net_device *dev, u8 *addr)
{
	struct s2io_nic *sp = netdev_priv(dev);
	register u64 mac_addr = 0, perm_addr = 0;
	int i;
	u64 tmp64;
	struct config_param *config = &sp->config;

	/*
	 * Set the new MAC address as the new unicast filter and reflect this
	 * change on the device address registered with the OS. It will be
	 * at offset 0.
	 */
	for (i = 0; i < ETH_ALEN; i++) {
		mac_addr <<= 8;
		mac_addr |= addr[i];
		perm_addr <<= 8;
		perm_addr |= sp->def_mac_addr[0].mac_addr[i];
	}

	/* check if the dev_addr is different than perm_addr */
	if (mac_addr == perm_addr)
		return SUCCESS;

	/* check if the mac already preset in CAM */
	for (i = 1; i < config->max_mac_addr; i++) {
		tmp64 = do_s2io_read_unicast_mc(sp, i);
		if (tmp64 == S2IO_DISABLE_MAC_ENTRY) /* CAM entry is empty */
			break;

		if (tmp64 == mac_addr) {
			DBG_PRINT(INFO_DBG,
				  "MAC addr:0x%llx already present in CAM\n",
				  (unsigned long long)mac_addr);
			return SUCCESS;
		}
	}
	if (i == config->max_mac_addr) {
		DBG_PRINT(ERR_DBG, "CAM full no space left for Unicast MAC\n");
		return FAILURE;
	}
	/* Update the internal structure with this new mac address */
	do_s2io_copy_mac_addr(sp, i, mac_addr);

	return do_s2io_add_mac(sp, mac_addr, i);
}

/**
 * s2io_ethtool_set_link_ksettings - Sets different link parameters.
 * @sp : private member of the device structure, which is a pointer to the
 * s2io_nic structure.
 * @cmd: pointer to the structure with parameters given by ethtool to set
 * link information.
 * Description:
 * The function sets different link parameters provided by the user onto
 * the NIC.
 * Return value:
 * 0 on success.
 */

static int
s2io_ethtool_set_link_ksettings(struct net_device *dev,
				const struct ethtool_link_ksettings *cmd)
{
	struct s2io_nic *sp = netdev_priv(dev);
	if ((cmd->base.autoneg == AUTONEG_ENABLE) ||
	    (cmd->base.speed != SPEED_10000) ||
	    (cmd->base.duplex != DUPLEX_FULL))
		return -EINVAL;
	else {
		s2io_close(sp->dev);
		s2io_open(sp->dev);
	}

	return 0;
}

/**
 * s2io_ethtol_get_link_ksettings - Return link specific information.
 * @sp : private member of the device structure, pointer to the
 *      s2io_nic structure.
 * @cmd : pointer to the structure with parameters given by ethtool
 * to return link information.
 * Description:
 * Returns link specific information like speed, duplex etc.. to ethtool.
 * Return value :
 * return 0 on success.
 */

static int
s2io_ethtool_get_link_ksettings(struct net_device *dev,
				struct ethtool_link_ksettings *cmd)
{
	struct s2io_nic *sp = netdev_priv(dev);

	ethtool_link_ksettings_zero_link_mode(cmd, supported);
	ethtool_link_ksettings_add_link_mode(cmd, supported, 10000baseT_Full);
	ethtool_link_ksettings_add_link_mode(cmd, supported, FIBRE);

	ethtool_link_ksettings_zero_link_mode(cmd, advertising);
	ethtool_link_ksettings_add_link_mode(cmd, advertising, 10000baseT_Full);
	ethtool_link_ksettings_add_link_mode(cmd, advertising, FIBRE);

	cmd->base.port = PORT_FIBRE;

	if (netif_carrier_ok(sp->dev)) {
		cmd->base.speed = SPEED_10000;
		cmd->base.duplex = DUPLEX_FULL;
	} else {
		cmd->base.speed = SPEED_UNKNOWN;
		cmd->base.duplex = DUPLEX_UNKNOWN;
	}

	cmd->base.autoneg = AUTONEG_DISABLE;
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
	struct s2io_nic *sp = netdev_priv(dev);

	strlcpy(info->driver, s2io_driver_name, sizeof(info->driver));
	strlcpy(info->version, s2io_driver_version, sizeof(info->version));
	strlcpy(info->bus_info, pci_name(sp->pdev), sizeof(info->bus_info));
}

/**
 *  s2io_ethtool_gregs - dumps the entire space of Xfame into the buffer.
 *  @sp: private member of the device structure, which is a pointer to the
 *  s2io_nic structure.
 *  @regs : pointer to the structure with parameters given by ethtool for
 *  dumping the registers.
 *  @reg_space: The input argument into which all the registers are dumped.
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
	u8 *reg_space = (u8 *)space;
	struct s2io_nic *sp = netdev_priv(dev);

	regs->len = XENA_REG_SPACE;
	regs->version = sp->pdev->subsystem_device;

	for (i = 0; i < regs->len; i += 8) {
		reg = readq(sp->bar0 + i);
		memcpy((reg_space + i), &reg, 8);
	}
}

/*
 *  s2io_set_led - control NIC led
 */
static void s2io_set_led(struct s2io_nic *sp, bool on)
{
	struct XENA_dev_config __iomem *bar0 = sp->bar0;
	u16 subid = sp->pdev->subsystem_device;
	u64 val64;

	if ((sp->device_type == XFRAME_II_DEVICE) ||
	    ((subid & 0xFF) >= 0x07)) {
		val64 = readq(&bar0->gpio_control);
		if (on)
			val64 |= GPIO_CTRL_GPIO_0;
		else
			val64 &= ~GPIO_CTRL_GPIO_0;

		writeq(val64, &bar0->gpio_control);
	} else {
		val64 = readq(&bar0->adapter_control);
		if (on)
			val64 |= ADAPTER_LED_ON;
		else
			val64 &= ~ADAPTER_LED_ON;

		writeq(val64, &bar0->adapter_control);
	}

}

/**
 * s2io_ethtool_set_led - To physically identify the nic on the system.
 * @dev : network device
 * @state: led setting
 *
 * Description: Used to physically identify the NIC on the system.
 * The Link LED will blink for a time specified by the user for
 * identification.
 * NOTE: The Link has to be Up to be able to blink the LED. Hence
 * identification is possible only if it's link is up.
 */

static int s2io_ethtool_set_led(struct net_device *dev,
				enum ethtool_phys_id_state state)
{
	struct s2io_nic *sp = netdev_priv(dev);
	struct XENA_dev_config __iomem *bar0 = sp->bar0;
	u16 subid = sp->pdev->subsystem_device;

	if ((sp->device_type == XFRAME_I_DEVICE) && ((subid & 0xFF) < 0x07)) {
		u64 val64 = readq(&bar0->adapter_control);
		if (!(val64 & ADAPTER_CNTL_EN)) {
			pr_err("Adapter Link down, cannot blink LED\n");
			return -EAGAIN;
		}
	}

	switch (state) {
	case ETHTOOL_ID_ACTIVE:
		sp->adapt_ctrl_org = readq(&bar0->gpio_control);
		return 1;	/* cycle on/off once per second */

	case ETHTOOL_ID_ON:
		s2io_set_led(sp, true);
		break;

	case ETHTOOL_ID_OFF:
		s2io_set_led(sp, false);
		break;

	case ETHTOOL_ID_INACTIVE:
		if (CARDS_WITH_FAULTY_LINK_INDICATORS(sp->device_type, subid))
			writeq(sp->adapt_ctrl_org, &bar0->gpio_control);
	}

	return 0;
}

static void s2io_ethtool_gringparam(struct net_device *dev,
				    struct ethtool_ringparam *ering)
{
	struct s2io_nic *sp = netdev_priv(dev);
	int i, tx_desc_count = 0, rx_desc_count = 0;

	if (sp->rxd_mode == RXD_MODE_1) {
		ering->rx_max_pending = MAX_RX_DESC_1;
		ering->rx_jumbo_max_pending = MAX_RX_DESC_1;
	} else {
		ering->rx_max_pending = MAX_RX_DESC_2;
		ering->rx_jumbo_max_pending = MAX_RX_DESC_2;
	}

	ering->tx_max_pending = MAX_TX_DESC;

	for (i = 0; i < sp->config.rx_ring_num; i++)
		rx_desc_count += sp->config.rx_cfg[i].num_rxd;
	ering->rx_pending = rx_desc_count;
	ering->rx_jumbo_pending = rx_desc_count;

	for (i = 0; i < sp->config.tx_fifo_num; i++)
		tx_desc_count += sp->config.tx_cfg[i].fifo_len;
	ering->tx_pending = tx_desc_count;
	DBG_PRINT(INFO_DBG, "max txds: %d\n", sp->config.max_txds);
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
	struct s2io_nic *sp = netdev_priv(dev);
	struct XENA_dev_config __iomem *bar0 = sp->bar0;

	val64 = readq(&bar0->rmac_pause_cfg);
	if (val64 & RMAC_PAUSE_GEN_ENABLE)
		ep->tx_pause = true;
	if (val64 & RMAC_PAUSE_RX_ENABLE)
		ep->rx_pause = true;
	ep->autoneg = false;
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
	struct s2io_nic *sp = netdev_priv(dev);
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
static int read_eeprom(struct s2io_nic *sp, int off, u64 *data)
{
	int ret = -1;
	u32 exit_cnt = 0;
	u64 val64;
	struct XENA_dev_config __iomem *bar0 = sp->bar0;

	if (sp->device_type == XFRAME_I_DEVICE) {
		val64 = I2C_CONTROL_DEV_ID(S2IO_DEV_ID) |
			I2C_CONTROL_ADDR(off) |
			I2C_CONTROL_BYTE_CNT(0x3) |
			I2C_CONTROL_READ |
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

static int write_eeprom(struct s2io_nic *sp, int off, u64 data, int cnt)
{
	int exit_cnt = 0, ret = -1;
	u64 val64;
	struct XENA_dev_config __iomem *bar0 = sp->bar0;

	if (sp->device_type == XFRAME_I_DEVICE) {
		val64 = I2C_CONTROL_DEV_ID(S2IO_DEV_ID) |
			I2C_CONTROL_ADDR(off) |
			I2C_CONTROL_BYTE_CNT(cnt) |
			I2C_CONTROL_SET_DATA((u32)data) |
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
		writeq(SPI_DATA_WRITE(data, (cnt << 3)), &bar0->spi_data);

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
	int i = 0, cnt, len, fail = 0;
	int vpd_addr = 0x80;
	struct swStat *swstats = &nic->mac_control.stats_info->sw_stat;

	if (nic->device_type == XFRAME_II_DEVICE) {
		strcpy(nic->product_name, "Xframe II 10GbE network adapter");
		vpd_addr = 0x80;
	} else {
		strcpy(nic->product_name, "Xframe I 10GbE network adapter");
		vpd_addr = 0x50;
	}
	strcpy(nic->serial_num, "NOT AVAILABLE");

	vpd_data = kmalloc(256, GFP_KERNEL);
	if (!vpd_data) {
		swstats->mem_alloc_fail_cnt++;
		return;
	}
	swstats->mem_allocated += 256;

	for (i = 0; i < 256; i += 4) {
		pci_write_config_byte(nic->pdev, (vpd_addr + 2), i);
		pci_read_config_byte(nic->pdev,  (vpd_addr + 2), &data);
		pci_write_config_byte(nic->pdev, (vpd_addr + 3), 0);
		for (cnt = 0; cnt < 5; cnt++) {
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

	if (!fail) {
		/* read serial number of adapter */
		for (cnt = 0; cnt < 252; cnt++) {
			if ((vpd_data[cnt] == 'S') &&
			    (vpd_data[cnt+1] == 'N')) {
				len = vpd_data[cnt+2];
				if (len < min(VPD_STRING_LEN, 256-cnt-2)) {
					memcpy(nic->serial_num,
					       &vpd_data[cnt + 3],
					       len);
					memset(nic->serial_num+len,
					       0,
					       VPD_STRING_LEN-len);
					break;
				}
			}
		}
	}

	if ((!fail) && (vpd_data[1] < VPD_STRING_LEN)) {
		len = vpd_data[1];
		memcpy(nic->product_name, &vpd_data[3], len);
		nic->product_name[len] = 0;
	}
	kfree(vpd_data);
	swstats->mem_freed += 256;
}

/**
 *  s2io_ethtool_geeprom  - reads the value stored in the Eeprom.
 *  @sp : private member of the device structure, which is a pointer to the
 *  s2io_nic structure.
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
	struct s2io_nic *sp = netdev_priv(dev);

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
				u8 *data_buf)
{
	int len = eeprom->len, cnt = 0;
	u64 valid = 0, data;
	struct s2io_nic *sp = netdev_priv(dev);

	if (eeprom->magic != (sp->pdev->vendor | (sp->pdev->device << 16))) {
		DBG_PRINT(ERR_DBG,
			  "ETHTOOL_WRITE_EEPROM Err: "
			  "Magic value is wrong, it is 0x%x should be 0x%x\n",
			  (sp->pdev->vendor | (sp->pdev->device << 16)),
			  eeprom->magic);
		return -EFAULT;
	}

	while (len) {
		data = (u32)data_buf[cnt] & 0x000000FF;
		if (data)
			valid = (u32)(data << 24);
		else
			valid = data;

		if (write_eeprom(sp, (eeprom->offset + cnt), valid, 0)) {
			DBG_PRINT(ERR_DBG,
				  "ETHTOOL_WRITE_EEPROM Err: "
				  "Cannot write into the specified offset\n");
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

static int s2io_register_test(struct s2io_nic *sp, uint64_t *data)
{
	struct XENA_dev_config __iomem *bar0 = sp->bar0;
	u64 val64 = 0, exp_val;
	int fail = 0;

	val64 = readq(&bar0->pif_rd_swapper_fb);
	if (val64 != 0x123456789abcdefULL) {
		fail = 1;
		DBG_PRINT(INFO_DBG, "Read Test level %d fails\n", 1);
	}

	val64 = readq(&bar0->rmac_pause_cfg);
	if (val64 != 0xc000ffff00000000ULL) {
		fail = 1;
		DBG_PRINT(INFO_DBG, "Read Test level %d fails\n", 2);
	}

	val64 = readq(&bar0->rx_queue_cfg);
	if (sp->device_type == XFRAME_II_DEVICE)
		exp_val = 0x0404040404040404ULL;
	else
		exp_val = 0x0808080808080808ULL;
	if (val64 != exp_val) {
		fail = 1;
		DBG_PRINT(INFO_DBG, "Read Test level %d fails\n", 3);
	}

	val64 = readq(&bar0->xgxs_efifo_cfg);
	if (val64 != 0x000000001923141EULL) {
		fail = 1;
		DBG_PRINT(INFO_DBG, "Read Test level %d fails\n", 4);
	}

	val64 = 0x5A5A5A5A5A5A5A5AULL;
	writeq(val64, &bar0->xmsi_data);
	val64 = readq(&bar0->xmsi_data);
	if (val64 != 0x5A5A5A5A5A5A5A5AULL) {
		fail = 1;
		DBG_PRINT(ERR_DBG, "Write Test level %d fails\n", 1);
	}

	val64 = 0xA5A5A5A5A5A5A5A5ULL;
	writeq(val64, &bar0->xmsi_data);
	val64 = readq(&bar0->xmsi_data);
	if (val64 != 0xA5A5A5A5A5A5A5A5ULL) {
		fail = 1;
		DBG_PRINT(ERR_DBG, "Write Test level %d fails\n", 2);
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

static int s2io_eeprom_test(struct s2io_nic *sp, uint64_t *data)
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

static int s2io_bist_test(struct s2io_nic *sp, uint64_t *data)
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
 * s2io_link_test - verifies the link state of the nic
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

static int s2io_link_test(struct s2io_nic *sp, uint64_t *data)
{
	struct XENA_dev_config __iomem *bar0 = sp->bar0;
	u64 val64;

	val64 = readq(&bar0->adapter_status);
	if (!(LINK_IS_UP(val64)))
		*data = 1;
	else
		*data = 0;

	return *data;
}

/**
 * s2io_rldram_test - offline test for access to the RldRam chip on the NIC
 * @sp: private member of the device structure, which is a pointer to the
 * s2io_nic structure.
 * @data: variable that returns the result of each of the test
 * conducted by the driver.
 * Description:
 *  This is one of the offline test that tests the read and write
 *  access to the RldRam chip on the NIC.
 * Return value:
 *  0 on success.
 */

static int s2io_rldram_test(struct s2io_nic *sp, uint64_t *data)
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
		if (iteration == 1)
			val64 ^= 0xFFFFFFFFFFFF0000ULL;
		writeq(val64, &bar0->mc_rldram_test_d0);

		val64 = 0xaaaa5a5555550000ULL;
		if (iteration == 1)
			val64 ^= 0xFFFFFFFFFFFF0000ULL;
		writeq(val64, &bar0->mc_rldram_test_d1);

		val64 = 0x55aaaaaaaa5a0000ULL;
		if (iteration == 1)
			val64 ^= 0xFFFFFFFFFFFF0000ULL;
		writeq(val64, &bar0->mc_rldram_test_d2);

		val64 = (u64) (0x0000003ffffe0100ULL);
		writeq(val64, &bar0->mc_rldram_test_add);

		val64 = MC_RLDRAM_TEST_MODE |
			MC_RLDRAM_TEST_WRITE |
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
			      uint64_t *data)
{
	struct s2io_nic *sp = netdev_priv(dev);
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
			DBG_PRINT(ERR_DBG, "%s: is not up, cannot run test\n",
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
				   u64 *tmp_stats)
{
	int i = 0, k;
	struct s2io_nic *sp = netdev_priv(dev);
	struct stat_block *stats = sp->mac_control.stats_info;
	struct swStat *swstats = &stats->sw_stat;
	struct xpakStat *xstats = &stats->xpak_stat;

	s2io_updt_stats(sp);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stats->tmac_frms_oflow) << 32  |
		le32_to_cpu(stats->tmac_frms);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stats->tmac_data_octets_oflow) << 32 |
		le32_to_cpu(stats->tmac_data_octets);
	tmp_stats[i++] = le64_to_cpu(stats->tmac_drop_frms);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stats->tmac_mcst_frms_oflow) << 32 |
		le32_to_cpu(stats->tmac_mcst_frms);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stats->tmac_bcst_frms_oflow) << 32 |
		le32_to_cpu(stats->tmac_bcst_frms);
	tmp_stats[i++] = le64_to_cpu(stats->tmac_pause_ctrl_frms);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stats->tmac_ttl_octets_oflow) << 32 |
		le32_to_cpu(stats->tmac_ttl_octets);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stats->tmac_ucst_frms_oflow) << 32 |
		le32_to_cpu(stats->tmac_ucst_frms);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stats->tmac_nucst_frms_oflow) << 32 |
		le32_to_cpu(stats->tmac_nucst_frms);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stats->tmac_any_err_frms_oflow) << 32 |
		le32_to_cpu(stats->tmac_any_err_frms);
	tmp_stats[i++] = le64_to_cpu(stats->tmac_ttl_less_fb_octets);
	tmp_stats[i++] = le64_to_cpu(stats->tmac_vld_ip_octets);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stats->tmac_vld_ip_oflow) << 32 |
		le32_to_cpu(stats->tmac_vld_ip);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stats->tmac_drop_ip_oflow) << 32 |
		le32_to_cpu(stats->tmac_drop_ip);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stats->tmac_icmp_oflow) << 32 |
		le32_to_cpu(stats->tmac_icmp);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stats->tmac_rst_tcp_oflow) << 32 |
		le32_to_cpu(stats->tmac_rst_tcp);
	tmp_stats[i++] = le64_to_cpu(stats->tmac_tcp);
	tmp_stats[i++] = (u64)le32_to_cpu(stats->tmac_udp_oflow) << 32 |
		le32_to_cpu(stats->tmac_udp);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stats->rmac_vld_frms_oflow) << 32 |
		le32_to_cpu(stats->rmac_vld_frms);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stats->rmac_data_octets_oflow) << 32 |
		le32_to_cpu(stats->rmac_data_octets);
	tmp_stats[i++] = le64_to_cpu(stats->rmac_fcs_err_frms);
	tmp_stats[i++] = le64_to_cpu(stats->rmac_drop_frms);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stats->rmac_vld_mcst_frms_oflow) << 32 |
		le32_to_cpu(stats->rmac_vld_mcst_frms);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stats->rmac_vld_bcst_frms_oflow) << 32 |
		le32_to_cpu(stats->rmac_vld_bcst_frms);
	tmp_stats[i++] = le32_to_cpu(stats->rmac_in_rng_len_err_frms);
	tmp_stats[i++] = le32_to_cpu(stats->rmac_out_rng_len_err_frms);
	tmp_stats[i++] = le64_to_cpu(stats->rmac_long_frms);
	tmp_stats[i++] = le64_to_cpu(stats->rmac_pause_ctrl_frms);
	tmp_stats[i++] = le64_to_cpu(stats->rmac_unsup_ctrl_frms);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stats->rmac_ttl_octets_oflow) << 32 |
		le32_to_cpu(stats->rmac_ttl_octets);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stats->rmac_accepted_ucst_frms_oflow) << 32
		| le32_to_cpu(stats->rmac_accepted_ucst_frms);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stats->rmac_accepted_nucst_frms_oflow)
		<< 32 | le32_to_cpu(stats->rmac_accepted_nucst_frms);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stats->rmac_discarded_frms_oflow) << 32 |
		le32_to_cpu(stats->rmac_discarded_frms);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stats->rmac_drop_events_oflow)
		<< 32 | le32_to_cpu(stats->rmac_drop_events);
	tmp_stats[i++] = le64_to_cpu(stats->rmac_ttl_less_fb_octets);
	tmp_stats[i++] = le64_to_cpu(stats->rmac_ttl_frms);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stats->rmac_usized_frms_oflow) << 32 |
		le32_to_cpu(stats->rmac_usized_frms);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stats->rmac_osized_frms_oflow) << 32 |
		le32_to_cpu(stats->rmac_osized_frms);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stats->rmac_frag_frms_oflow) << 32 |
		le32_to_cpu(stats->rmac_frag_frms);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stats->rmac_jabber_frms_oflow) << 32 |
		le32_to_cpu(stats->rmac_jabber_frms);
	tmp_stats[i++] = le64_to_cpu(stats->rmac_ttl_64_frms);
	tmp_stats[i++] = le64_to_cpu(stats->rmac_ttl_65_127_frms);
	tmp_stats[i++] = le64_to_cpu(stats->rmac_ttl_128_255_frms);
	tmp_stats[i++] = le64_to_cpu(stats->rmac_ttl_256_511_frms);
	tmp_stats[i++] = le64_to_cpu(stats->rmac_ttl_512_1023_frms);
	tmp_stats[i++] = le64_to_cpu(stats->rmac_ttl_1024_1518_frms);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stats->rmac_ip_oflow) << 32 |
		le32_to_cpu(stats->rmac_ip);
	tmp_stats[i++] = le64_to_cpu(stats->rmac_ip_octets);
	tmp_stats[i++] = le32_to_cpu(stats->rmac_hdr_err_ip);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stats->rmac_drop_ip_oflow) << 32 |
		le32_to_cpu(stats->rmac_drop_ip);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stats->rmac_icmp_oflow) << 32 |
		le32_to_cpu(stats->rmac_icmp);
	tmp_stats[i++] = le64_to_cpu(stats->rmac_tcp);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stats->rmac_udp_oflow) << 32 |
		le32_to_cpu(stats->rmac_udp);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stats->rmac_err_drp_udp_oflow) << 32 |
		le32_to_cpu(stats->rmac_err_drp_udp);
	tmp_stats[i++] = le64_to_cpu(stats->rmac_xgmii_err_sym);
	tmp_stats[i++] = le64_to_cpu(stats->rmac_frms_q0);
	tmp_stats[i++] = le64_to_cpu(stats->rmac_frms_q1);
	tmp_stats[i++] = le64_to_cpu(stats->rmac_frms_q2);
	tmp_stats[i++] = le64_to_cpu(stats->rmac_frms_q3);
	tmp_stats[i++] = le64_to_cpu(stats->rmac_frms_q4);
	tmp_stats[i++] = le64_to_cpu(stats->rmac_frms_q5);
	tmp_stats[i++] = le64_to_cpu(stats->rmac_frms_q6);
	tmp_stats[i++] = le64_to_cpu(stats->rmac_frms_q7);
	tmp_stats[i++] = le16_to_cpu(stats->rmac_full_q0);
	tmp_stats[i++] = le16_to_cpu(stats->rmac_full_q1);
	tmp_stats[i++] = le16_to_cpu(stats->rmac_full_q2);
	tmp_stats[i++] = le16_to_cpu(stats->rmac_full_q3);
	tmp_stats[i++] = le16_to_cpu(stats->rmac_full_q4);
	tmp_stats[i++] = le16_to_cpu(stats->rmac_full_q5);
	tmp_stats[i++] = le16_to_cpu(stats->rmac_full_q6);
	tmp_stats[i++] = le16_to_cpu(stats->rmac_full_q7);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stats->rmac_pause_cnt_oflow) << 32 |
		le32_to_cpu(stats->rmac_pause_cnt);
	tmp_stats[i++] = le64_to_cpu(stats->rmac_xgmii_data_err_cnt);
	tmp_stats[i++] = le64_to_cpu(stats->rmac_xgmii_ctrl_err_cnt);
	tmp_stats[i++] =
		(u64)le32_to_cpu(stats->rmac_accepted_ip_oflow) << 32 |
		le32_to_cpu(stats->rmac_accepted_ip);
	tmp_stats[i++] = le32_to_cpu(stats->rmac_err_tcp);
	tmp_stats[i++] = le32_to_cpu(stats->rd_req_cnt);
	tmp_stats[i++] = le32_to_cpu(stats->new_rd_req_cnt);
	tmp_stats[i++] = le32_to_cpu(stats->new_rd_req_rtry_cnt);
	tmp_stats[i++] = le32_to_cpu(stats->rd_rtry_cnt);
	tmp_stats[i++] = le32_to_cpu(stats->wr_rtry_rd_ack_cnt);
	tmp_stats[i++] = le32_to_cpu(stats->wr_req_cnt);
	tmp_stats[i++] = le32_to_cpu(stats->new_wr_req_cnt);
	tmp_stats[i++] = le32_to_cpu(stats->new_wr_req_rtry_cnt);
	tmp_stats[i++] = le32_to_cpu(stats->wr_rtry_cnt);
	tmp_stats[i++] = le32_to_cpu(stats->wr_disc_cnt);
	tmp_stats[i++] = le32_to_cpu(stats->rd_rtry_wr_ack_cnt);
	tmp_stats[i++] = le32_to_cpu(stats->txp_wr_cnt);
	tmp_stats[i++] = le32_to_cpu(stats->txd_rd_cnt);
	tmp_stats[i++] = le32_to_cpu(stats->txd_wr_cnt);
	tmp_stats[i++] = le32_to_cpu(stats->rxd_rd_cnt);
	tmp_stats[i++] = le32_to_cpu(stats->rxd_wr_cnt);
	tmp_stats[i++] = le32_to_cpu(stats->txf_rd_cnt);
	tmp_stats[i++] = le32_to_cpu(stats->rxf_wr_cnt);

	/* Enhanced statistics exist only for Hercules */
	if (sp->device_type == XFRAME_II_DEVICE) {
		tmp_stats[i++] =
			le64_to_cpu(stats->rmac_ttl_1519_4095_frms);
		tmp_stats[i++] =
			le64_to_cpu(stats->rmac_ttl_4096_8191_frms);
		tmp_stats[i++] =
			le64_to_cpu(stats->rmac_ttl_8192_max_frms);
		tmp_stats[i++] = le64_to_cpu(stats->rmac_ttl_gt_max_frms);
		tmp_stats[i++] = le64_to_cpu(stats->rmac_osized_alt_frms);
		tmp_stats[i++] = le64_to_cpu(stats->rmac_jabber_alt_frms);
		tmp_stats[i++] = le64_to_cpu(stats->rmac_gt_max_alt_frms);
		tmp_stats[i++] = le64_to_cpu(stats->rmac_vlan_frms);
		tmp_stats[i++] = le32_to_cpu(stats->rmac_len_discard);
		tmp_stats[i++] = le32_to_cpu(stats->rmac_fcs_discard);
		tmp_stats[i++] = le32_to_cpu(stats->rmac_pf_discard);
		tmp_stats[i++] = le32_to_cpu(stats->rmac_da_discard);
		tmp_stats[i++] = le32_to_cpu(stats->rmac_red_discard);
		tmp_stats[i++] = le32_to_cpu(stats->rmac_rts_discard);
		tmp_stats[i++] = le32_to_cpu(stats->rmac_ingm_full_discard);
		tmp_stats[i++] = le32_to_cpu(stats->link_fault_cnt);
	}

	tmp_stats[i++] = 0;
	tmp_stats[i++] = swstats->single_ecc_errs;
	tmp_stats[i++] = swstats->double_ecc_errs;
	tmp_stats[i++] = swstats->parity_err_cnt;
	tmp_stats[i++] = swstats->serious_err_cnt;
	tmp_stats[i++] = swstats->soft_reset_cnt;
	tmp_stats[i++] = swstats->fifo_full_cnt;
	for (k = 0; k < MAX_RX_RINGS; k++)
		tmp_stats[i++] = swstats->ring_full_cnt[k];
	tmp_stats[i++] = xstats->alarm_transceiver_temp_high;
	tmp_stats[i++] = xstats->alarm_transceiver_temp_low;
	tmp_stats[i++] = xstats->alarm_laser_bias_current_high;
	tmp_stats[i++] = xstats->alarm_laser_bias_current_low;
	tmp_stats[i++] = xstats->alarm_laser_output_power_high;
	tmp_stats[i++] = xstats->alarm_laser_output_power_low;
	tmp_stats[i++] = xstats->warn_transceiver_temp_high;
	tmp_stats[i++] = xstats->warn_transceiver_temp_low;
	tmp_stats[i++] = xstats->warn_laser_bias_current_high;
	tmp_stats[i++] = xstats->warn_laser_bias_current_low;
	tmp_stats[i++] = xstats->warn_laser_output_power_high;
	tmp_stats[i++] = xstats->warn_laser_output_power_low;
	tmp_stats[i++] = swstats->clubbed_frms_cnt;
	tmp_stats[i++] = swstats->sending_both;
	tmp_stats[i++] = swstats->outof_sequence_pkts;
	tmp_stats[i++] = swstats->flush_max_pkts;
	if (swstats->num_aggregations) {
		u64 tmp = swstats->sum_avg_pkts_aggregated;
		int count = 0;
		/*
		 * Since 64-bit divide does not work on all platforms,
		 * do repeated subtraction.
		 */
		while (tmp >= swstats->num_aggregations) {
			tmp -= swstats->num_aggregations;
			count++;
		}
		tmp_stats[i++] = count;
	} else
		tmp_stats[i++] = 0;
	tmp_stats[i++] = swstats->mem_alloc_fail_cnt;
	tmp_stats[i++] = swstats->pci_map_fail_cnt;
	tmp_stats[i++] = swstats->watchdog_timer_cnt;
	tmp_stats[i++] = swstats->mem_allocated;
	tmp_stats[i++] = swstats->mem_freed;
	tmp_stats[i++] = swstats->link_up_cnt;
	tmp_stats[i++] = swstats->link_down_cnt;
	tmp_stats[i++] = swstats->link_up_time;
	tmp_stats[i++] = swstats->link_down_time;

	tmp_stats[i++] = swstats->tx_buf_abort_cnt;
	tmp_stats[i++] = swstats->tx_desc_abort_cnt;
	tmp_stats[i++] = swstats->tx_parity_err_cnt;
	tmp_stats[i++] = swstats->tx_link_loss_cnt;
	tmp_stats[i++] = swstats->tx_list_proc_err_cnt;

	tmp_stats[i++] = swstats->rx_parity_err_cnt;
	tmp_stats[i++] = swstats->rx_abort_cnt;
	tmp_stats[i++] = swstats->rx_parity_abort_cnt;
	tmp_stats[i++] = swstats->rx_rda_fail_cnt;
	tmp_stats[i++] = swstats->rx_unkn_prot_cnt;
	tmp_stats[i++] = swstats->rx_fcs_err_cnt;
	tmp_stats[i++] = swstats->rx_buf_size_err_cnt;
	tmp_stats[i++] = swstats->rx_rxd_corrupt_cnt;
	tmp_stats[i++] = swstats->rx_unkn_err_cnt;
	tmp_stats[i++] = swstats->tda_err_cnt;
	tmp_stats[i++] = swstats->pfc_err_cnt;
	tmp_stats[i++] = swstats->pcc_err_cnt;
	tmp_stats[i++] = swstats->tti_err_cnt;
	tmp_stats[i++] = swstats->tpa_err_cnt;
	tmp_stats[i++] = swstats->sm_err_cnt;
	tmp_stats[i++] = swstats->lso_err_cnt;
	tmp_stats[i++] = swstats->mac_tmac_err_cnt;
	tmp_stats[i++] = swstats->mac_rmac_err_cnt;
	tmp_stats[i++] = swstats->xgxs_txgxs_err_cnt;
	tmp_stats[i++] = swstats->xgxs_rxgxs_err_cnt;
	tmp_stats[i++] = swstats->rc_err_cnt;
	tmp_stats[i++] = swstats->prc_pcix_err_cnt;
	tmp_stats[i++] = swstats->rpa_err_cnt;
	tmp_stats[i++] = swstats->rda_err_cnt;
	tmp_stats[i++] = swstats->rti_err_cnt;
	tmp_stats[i++] = swstats->mc_err_cnt;
}

static int s2io_ethtool_get_regs_len(struct net_device *dev)
{
	return XENA_REG_SPACE;
}


static int s2io_get_eeprom_len(struct net_device *dev)
{
	return XENA_EEPROM_SPACE;
}

static int s2io_get_sset_count(struct net_device *dev, int sset)
{
	struct s2io_nic *sp = netdev_priv(dev);

	switch (sset) {
	case ETH_SS_TEST:
		return S2IO_TEST_LEN;
	case ETH_SS_STATS:
		switch (sp->device_type) {
		case XFRAME_I_DEVICE:
			return XFRAME_I_STAT_LEN;
		case XFRAME_II_DEVICE:
			return XFRAME_II_STAT_LEN;
		default:
			return 0;
		}
	default:
		return -EOPNOTSUPP;
	}
}

static void s2io_ethtool_get_strings(struct net_device *dev,
				     u32 stringset, u8 *data)
{
	int stat_size = 0;
	struct s2io_nic *sp = netdev_priv(dev);

	switch (stringset) {
	case ETH_SS_TEST:
		memcpy(data, s2io_gstrings, S2IO_STRINGS_LEN);
		break;
	case ETH_SS_STATS:
		stat_size = sizeof(ethtool_xena_stats_keys);
		memcpy(data, &ethtool_xena_stats_keys, stat_size);
		if (sp->device_type == XFRAME_II_DEVICE) {
			memcpy(data + stat_size,
			       &ethtool_enhanced_stats_keys,
			       sizeof(ethtool_enhanced_stats_keys));
			stat_size += sizeof(ethtool_enhanced_stats_keys);
		}

		memcpy(data + stat_size, &ethtool_driver_stats_keys,
		       sizeof(ethtool_driver_stats_keys));
	}
}

static int s2io_set_features(struct net_device *dev, netdev_features_t features)
{
	struct s2io_nic *sp = netdev_priv(dev);
	netdev_features_t changed = (features ^ dev->features) & NETIF_F_LRO;

	if (changed && netif_running(dev)) {
		int rc;

		s2io_stop_all_tx_queue(sp);
		s2io_card_down(sp);
		dev->features = features;
		rc = s2io_card_up(sp);
		if (rc)
			s2io_reset(sp);
		else
			s2io_start_all_tx_queue(sp);

		return rc ? rc : 1;
	}

	return 0;
}

static const struct ethtool_ops netdev_ethtool_ops = {
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
	.self_test = s2io_ethtool_test,
	.get_strings = s2io_ethtool_get_strings,
	.set_phys_id = s2io_ethtool_set_led,
	.get_ethtool_stats = s2io_get_ethtool_stats,
	.get_sset_count = s2io_get_sset_count,
	.get_link_ksettings = s2io_ethtool_get_link_ksettings,
	.set_link_ksettings = s2io_ethtool_set_link_ksettings,
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
	struct s2io_nic *sp = netdev_priv(dev);
	int ret = 0;

	dev->mtu = new_mtu;
	if (netif_running(dev)) {
		s2io_stop_all_tx_queue(sp);
		s2io_card_down(sp);
		ret = s2io_card_up(sp);
		if (ret) {
			DBG_PRINT(ERR_DBG, "%s: Device bring up failed\n",
				  __func__);
			return ret;
		}
		s2io_wake_all_tx_queue(sp);
	} else { /* Device is down */
		struct XENA_dev_config __iomem *bar0 = sp->bar0;
		u64 val64 = new_mtu;

		writeq(vBIT(val64, 2, 14), &bar0->rmac_max_pyld_len);
	}

	return ret;
}

/**
 * s2io_set_link - Set the LInk status
 * @data: long pointer to device private structue
 * Description: Sets the link status for the adapter
 */

static void s2io_set_link(struct work_struct *work)
{
	struct s2io_nic *nic = container_of(work, struct s2io_nic,
					    set_link_task);
	struct net_device *dev = nic->dev;
	struct XENA_dev_config __iomem *bar0 = nic->bar0;
	register u64 val64;
	u16 subid;

	rtnl_lock();

	if (!netif_running(dev))
		goto out_unlock;

	if (test_and_set_bit(__S2IO_STATE_LINK_TASK, &(nic->state))) {
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
				nic->device_enabled_once = true;
			} else {
				DBG_PRINT(ERR_DBG,
					  "%s: Error: device is not Quiescent\n",
					  dev->name);
				s2io_stop_all_tx_queue(nic);
			}
		}
		val64 = readq(&bar0->adapter_control);
		val64 |= ADAPTER_LED_ON;
		writeq(val64, &bar0->adapter_control);
		s2io_link(nic, LINK_UP);
	} else {
		if (CARDS_WITH_FAULTY_LINK_INDICATORS(nic->device_type,
						      subid)) {
			val64 = readq(&bar0->gpio_control);
			val64 &= ~GPIO_CTRL_GPIO_0;
			writeq(val64, &bar0->gpio_control);
			val64 = readq(&bar0->gpio_control);
		}
		/* turn off LED */
		val64 = readq(&bar0->adapter_control);
		val64 = val64 & (~ADAPTER_LED_ON);
		writeq(val64, &bar0->adapter_control);
		s2io_link(nic, LINK_DOWN);
	}
	clear_bit(__S2IO_STATE_LINK_TASK, &(nic->state));

out_unlock:
	rtnl_unlock();
}

static int set_rxd_buffer_pointer(struct s2io_nic *sp, struct RxD_t *rxdp,
				  struct buffAdd *ba,
				  struct sk_buff **skb, u64 *temp0, u64 *temp1,
				  u64 *temp2, int size)
{
	struct net_device *dev = sp->dev;
	struct swStat *stats = &sp->mac_control.stats_info->sw_stat;

	if ((sp->rxd_mode == RXD_MODE_1) && (rxdp->Host_Control == 0)) {
		struct RxD1 *rxdp1 = (struct RxD1 *)rxdp;
		/* allocate skb */
		if (*skb) {
			DBG_PRINT(INFO_DBG, "SKB is not NULL\n");
			/*
			 * As Rx frame are not going to be processed,
			 * using same mapped address for the Rxd
			 * buffer pointer
			 */
			rxdp1->Buffer0_ptr = *temp0;
		} else {
			*skb = netdev_alloc_skb(dev, size);
			if (!(*skb)) {
				DBG_PRINT(INFO_DBG,
					  "%s: Out of memory to allocate %s\n",
					  dev->name, "1 buf mode SKBs");
				stats->mem_alloc_fail_cnt++;
				return -ENOMEM ;
			}
			stats->mem_allocated += (*skb)->truesize;
			/* storing the mapped addr in a temp variable
			 * such it will be used for next rxd whose
			 * Host Control is NULL
			 */
			rxdp1->Buffer0_ptr = *temp0 =
				pci_map_single(sp->pdev, (*skb)->data,
					       size - NET_IP_ALIGN,
					       PCI_DMA_FROMDEVICE);
			if (pci_dma_mapping_error(sp->pdev, rxdp1->Buffer0_ptr))
				goto memalloc_failed;
			rxdp->Host_Control = (unsigned long) (*skb);
		}
	} else if ((sp->rxd_mode == RXD_MODE_3B) && (rxdp->Host_Control == 0)) {
		struct RxD3 *rxdp3 = (struct RxD3 *)rxdp;
		/* Two buffer Mode */
		if (*skb) {
			rxdp3->Buffer2_ptr = *temp2;
			rxdp3->Buffer0_ptr = *temp0;
			rxdp3->Buffer1_ptr = *temp1;
		} else {
			*skb = netdev_alloc_skb(dev, size);
			if (!(*skb)) {
				DBG_PRINT(INFO_DBG,
					  "%s: Out of memory to allocate %s\n",
					  dev->name,
					  "2 buf mode SKBs");
				stats->mem_alloc_fail_cnt++;
				return -ENOMEM;
			}
			stats->mem_allocated += (*skb)->truesize;
			rxdp3->Buffer2_ptr = *temp2 =
				pci_map_single(sp->pdev, (*skb)->data,
					       dev->mtu + 4,
					       PCI_DMA_FROMDEVICE);
			if (pci_dma_mapping_error(sp->pdev, rxdp3->Buffer2_ptr))
				goto memalloc_failed;
			rxdp3->Buffer0_ptr = *temp0 =
				pci_map_single(sp->pdev, ba->ba_0, BUF0_LEN,
					       PCI_DMA_FROMDEVICE);
			if (pci_dma_mapping_error(sp->pdev,
						  rxdp3->Buffer0_ptr)) {
				pci_unmap_single(sp->pdev,
						 (dma_addr_t)rxdp3->Buffer2_ptr,
						 dev->mtu + 4,
						 PCI_DMA_FROMDEVICE);
				goto memalloc_failed;
			}
			rxdp->Host_Control = (unsigned long) (*skb);

			/* Buffer-1 will be dummy buffer not used */
			rxdp3->Buffer1_ptr = *temp1 =
				pci_map_single(sp->pdev, ba->ba_1, BUF1_LEN,
					       PCI_DMA_FROMDEVICE);
			if (pci_dma_mapping_error(sp->pdev,
						  rxdp3->Buffer1_ptr)) {
				pci_unmap_single(sp->pdev,
						 (dma_addr_t)rxdp3->Buffer0_ptr,
						 BUF0_LEN, PCI_DMA_FROMDEVICE);
				pci_unmap_single(sp->pdev,
						 (dma_addr_t)rxdp3->Buffer2_ptr,
						 dev->mtu + 4,
						 PCI_DMA_FROMDEVICE);
				goto memalloc_failed;
			}
		}
	}
	return 0;

memalloc_failed:
	stats->pci_map_fail_cnt++;
	stats->mem_freed += (*skb)->truesize;
	dev_kfree_skb(*skb);
	return -ENOMEM;
}

static void set_rxd_buffer_size(struct s2io_nic *sp, struct RxD_t *rxdp,
				int size)
{
	struct net_device *dev = sp->dev;
	if (sp->rxd_mode == RXD_MODE_1) {
		rxdp->Control_2 = SET_BUFFER0_SIZE_1(size - NET_IP_ALIGN);
	} else if (sp->rxd_mode == RXD_MODE_3B) {
		rxdp->Control_2 = SET_BUFFER0_SIZE_3(BUF0_LEN);
		rxdp->Control_2 |= SET_BUFFER1_SIZE_3(1);
		rxdp->Control_2 |= SET_BUFFER2_SIZE_3(dev->mtu + 4);
	}
}

static  int rxd_owner_bit_reset(struct s2io_nic *sp)
{
	int i, j, k, blk_cnt = 0, size;
	struct config_param *config = &sp->config;
	struct mac_info *mac_control = &sp->mac_control;
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

	for (i = 0; i < config->rx_ring_num; i++) {
		struct rx_ring_config *rx_cfg = &config->rx_cfg[i];
		struct ring_info *ring = &mac_control->rings[i];

		blk_cnt = rx_cfg->num_rxd / (rxd_count[sp->rxd_mode] + 1);

		for (j = 0; j < blk_cnt; j++) {
			for (k = 0; k < rxd_count[sp->rxd_mode]; k++) {
				rxdp = ring->rx_blocks[j].rxds[k].virt_addr;
				if (sp->rxd_mode == RXD_MODE_3B)
					ba = &ring->ba[j][k];
				if (set_rxd_buffer_pointer(sp, rxdp, ba, &skb,
							   &temp0_64,
							   &temp1_64,
							   &temp2_64,
							   size) == -ENOMEM) {
					return 0;
				}

				set_rxd_buffer_size(sp, rxdp, size);
				dma_wmb();
				/* flip the Ownership bit to Hardware */
				rxdp->Control_1 |= RXD_OWN_XENA;
			}
		}
	}
	return 0;

}

static int s2io_add_isr(struct s2io_nic *sp)
{
	int ret = 0;
	struct net_device *dev = sp->dev;
	int err = 0;

	if (sp->config.intr_type == MSI_X)
		ret = s2io_enable_msi_x(sp);
	if (ret) {
		DBG_PRINT(ERR_DBG, "%s: Defaulting to INTA\n", dev->name);
		sp->config.intr_type = INTA;
	}

	/*
	 * Store the values of the MSIX table in
	 * the struct s2io_nic structure
	 */
	store_xmsi_data(sp);

	/* After proper initialization of H/W, register ISR */
	if (sp->config.intr_type == MSI_X) {
		int i, msix_rx_cnt = 0;

		for (i = 0; i < sp->num_entries; i++) {
			if (sp->s2io_entries[i].in_use == MSIX_FLG) {
				if (sp->s2io_entries[i].type ==
				    MSIX_RING_TYPE) {
					snprintf(sp->desc[i],
						sizeof(sp->desc[i]),
						"%s:MSI-X-%d-RX",
						dev->name, i);
					err = request_irq(sp->entries[i].vector,
							  s2io_msix_ring_handle,
							  0,
							  sp->desc[i],
							  sp->s2io_entries[i].arg);
				} else if (sp->s2io_entries[i].type ==
					   MSIX_ALARM_TYPE) {
					snprintf(sp->desc[i],
						sizeof(sp->desc[i]),
						"%s:MSI-X-%d-TX",
						dev->name, i);
					err = request_irq(sp->entries[i].vector,
							  s2io_msix_fifo_handle,
							  0,
							  sp->desc[i],
							  sp->s2io_entries[i].arg);

				}
				/* if either data or addr is zero print it. */
				if (!(sp->msix_info[i].addr &&
				      sp->msix_info[i].data)) {
					DBG_PRINT(ERR_DBG,
						  "%s @Addr:0x%llx Data:0x%llx\n",
						  sp->desc[i],
						  (unsigned long long)
						  sp->msix_info[i].addr,
						  (unsigned long long)
						  ntohl(sp->msix_info[i].data));
				} else
					msix_rx_cnt++;
				if (err) {
					remove_msix_isr(sp);

					DBG_PRINT(ERR_DBG,
						  "%s:MSI-X-%d registration "
						  "failed\n", dev->name, i);

					DBG_PRINT(ERR_DBG,
						  "%s: Defaulting to INTA\n",
						  dev->name);
					sp->config.intr_type = INTA;
					break;
				}
				sp->s2io_entries[i].in_use =
					MSIX_REGISTERED_SUCCESS;
			}
		}
		if (!err) {
			pr_info("MSI-X-RX %d entries enabled\n", --msix_rx_cnt);
			DBG_PRINT(INFO_DBG,
				  "MSI-X-TX entries enabled through alarm vector\n");
		}
	}
	if (sp->config.intr_type == INTA) {
		err = request_irq(sp->pdev->irq, s2io_isr, IRQF_SHARED,
				  sp->name, dev);
		if (err) {
			DBG_PRINT(ERR_DBG, "%s: ISR registration failed\n",
				  dev->name);
			return -1;
		}
	}
	return 0;
}

static void s2io_rem_isr(struct s2io_nic *sp)
{
	if (sp->config.intr_type == MSI_X)
		remove_msix_isr(sp);
	else
		remove_inta_isr(sp);
}

static void do_s2io_card_down(struct s2io_nic *sp, int do_io)
{
	int cnt = 0;
	struct XENA_dev_config __iomem *bar0 = sp->bar0;
	register u64 val64 = 0;
	struct config_param *config;
	config = &sp->config;

	if (!is_s2io_card_up(sp))
		return;

	del_timer_sync(&sp->alarm_timer);
	/* If s2io_set_link task is executing, wait till it completes. */
	while (test_and_set_bit(__S2IO_STATE_LINK_TASK, &(sp->state)))
		msleep(50);
	clear_bit(__S2IO_STATE_CARD_UP, &sp->state);

	/* Disable napi */
	if (sp->config.napi) {
		int off = 0;
		if (config->intr_type ==  MSI_X) {
			for (; off < sp->config.rx_ring_num; off++)
				napi_disable(&sp->mac_control.rings[off].napi);
		}
		else
			napi_disable(&sp->napi);
	}

	/* disable Tx and Rx traffic on the NIC */
	if (do_io)
		stop_nic(sp);

	s2io_rem_isr(sp);

	/* stop the tx queue, indicate link down */
	s2io_link(sp, LINK_DOWN);

	/* Check if the device is Quiescent and then Reset the NIC */
	while (do_io) {
		/* As per the HW requirement we need to replenish the
		 * receive buffer to avoid the ring bump. Since there is
		 * no intention of processing the Rx frame at this pointwe are
		 * just setting the ownership bit of rxd in Each Rx
		 * ring to HW and set the appropriate buffer size
		 * based on the ring mode
		 */
		rxd_owner_bit_reset(sp);

		val64 = readq(&bar0->adapter_status);
		if (verify_xena_quiescence(sp)) {
			if (verify_pcc_quiescent(sp, sp->device_enabled_once))
				break;
		}

		msleep(50);
		cnt++;
		if (cnt == 10) {
			DBG_PRINT(ERR_DBG, "Device not Quiescent - "
				  "adapter status reads 0x%llx\n",
				  (unsigned long long)val64);
			break;
		}
	}
	if (do_io)
		s2io_reset(sp);

	/* Free all Tx buffers */
	free_tx_buffers(sp);

	/* Free all Rx buffers */
	free_rx_buffers(sp);

	clear_bit(__S2IO_STATE_LINK_TASK, &(sp->state));
}

static void s2io_card_down(struct s2io_nic *sp)
{
	do_s2io_card_down(sp, 1);
}

static int s2io_card_up(struct s2io_nic *sp)
{
	int i, ret = 0;
	struct config_param *config;
	struct mac_info *mac_control;
	struct net_device *dev = sp->dev;
	u16 interruptible;

	/* Initialize the H/W I/O registers */
	ret = init_nic(sp);
	if (ret != 0) {
		DBG_PRINT(ERR_DBG, "%s: H/W initialization failed\n",
			  dev->name);
		if (ret != -EIO)
			s2io_reset(sp);
		return ret;
	}

	/*
	 * Initializing the Rx buffers. For now we are considering only 1
	 * Rx ring and initializing buffers into 30 Rx blocks
	 */
	config = &sp->config;
	mac_control = &sp->mac_control;

	for (i = 0; i < config->rx_ring_num; i++) {
		struct ring_info *ring = &mac_control->rings[i];

		ring->mtu = dev->mtu;
		ring->lro = !!(dev->features & NETIF_F_LRO);
		ret = fill_rx_buffers(sp, ring, 1);
		if (ret) {
			DBG_PRINT(ERR_DBG, "%s: Out of memory in Open\n",
				  dev->name);
			s2io_reset(sp);
			free_rx_buffers(sp);
			return -ENOMEM;
		}
		DBG_PRINT(INFO_DBG, "Buf in ring:%d is %d:\n", i,
			  ring->rx_bufs_left);
	}

	/* Initialise napi */
	if (config->napi) {
		if (config->intr_type ==  MSI_X) {
			for (i = 0; i < sp->config.rx_ring_num; i++)
				napi_enable(&sp->mac_control.rings[i].napi);
		} else {
			napi_enable(&sp->napi);
		}
	}

	/* Maintain the state prior to the open */
	if (sp->promisc_flg)
		sp->promisc_flg = 0;
	if (sp->m_cast_flg) {
		sp->m_cast_flg = 0;
		sp->all_multi_pos = 0;
	}

	/* Setting its receive mode */
	s2io_set_multicast(dev);

	if (dev->features & NETIF_F_LRO) {
		/* Initialize max aggregatable pkts per session based on MTU */
		sp->lro_max_aggr_per_sess = ((1<<16) - 1) / dev->mtu;
		/* Check if we can use (if specified) user provided value */
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
		if (sp->config.intr_type == MSI_X)
			s2io_rem_isr(sp);
		s2io_reset(sp);
		free_rx_buffers(sp);
		return -ENODEV;
	}

	S2IO_TIMER_CONF(sp->alarm_timer, s2io_alarm_handle, sp, (HZ/2));

	set_bit(__S2IO_STATE_CARD_UP, &sp->state);

	/*  Enable select interrupts */
	en_dis_err_alarms(sp, ENA_ALL_INTRS, ENABLE_INTRS);
	if (sp->config.intr_type != INTA) {
		interruptible = TX_TRAFFIC_INTR | TX_PIC_INTR;
		en_dis_able_nic_intrs(sp, interruptible, ENABLE_INTRS);
	} else {
		interruptible = TX_TRAFFIC_INTR | RX_TRAFFIC_INTR;
		interruptible |= TX_PIC_INTR;
		en_dis_able_nic_intrs(sp, interruptible, ENABLE_INTRS);
	}

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
		DBG_PRINT(ERR_DBG, "%s: Device bring up failed\n", dev->name);
	}
	s2io_wake_all_tx_queue(sp);
	DBG_PRINT(ERR_DBG, "%s: was reset by Tx watchdog timer\n", dev->name);
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
	struct s2io_nic *sp = netdev_priv(dev);
	struct swStat *swstats = &sp->mac_control.stats_info->sw_stat;

	if (netif_carrier_ok(dev)) {
		swstats->watchdog_timer_cnt++;
		schedule_work(&sp->rst_timer_task);
		swstats->soft_reset_cnt++;
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
	struct net_device *dev = ring_data->dev;
	struct sk_buff *skb = (struct sk_buff *)
		((unsigned long)rxdp->Host_Control);
	int ring_no = ring_data->ring_no;
	u16 l3_csum, l4_csum;
	unsigned long long err = rxdp->Control_1 & RXD_T_CODE;
	struct lro *uninitialized_var(lro);
	u8 err_mask;
	struct swStat *swstats = &sp->mac_control.stats_info->sw_stat;

	skb->dev = dev;

	if (err) {
		/* Check for parity error */
		if (err & 0x1)
			swstats->parity_err_cnt++;

		err_mask = err >> 48;
		switch (err_mask) {
		case 1:
			swstats->rx_parity_err_cnt++;
			break;

		case 2:
			swstats->rx_abort_cnt++;
			break;

		case 3:
			swstats->rx_parity_abort_cnt++;
			break;

		case 4:
			swstats->rx_rda_fail_cnt++;
			break;

		case 5:
			swstats->rx_unkn_prot_cnt++;
			break;

		case 6:
			swstats->rx_fcs_err_cnt++;
			break;

		case 7:
			swstats->rx_buf_size_err_cnt++;
			break;

		case 8:
			swstats->rx_rxd_corrupt_cnt++;
			break;

		case 15:
			swstats->rx_unkn_err_cnt++;
			break;
		}
		/*
		 * Drop the packet if bad transfer code. Exception being
		 * 0x5, which could be due to unsupported IPv6 extension header.
		 * In this case, we let stack handle the packet.
		 * Note that in this case, since checksum will be incorrect,
		 * stack will validate the same.
		 */
		if (err_mask != 0x5) {
			DBG_PRINT(ERR_DBG, "%s: Rx error Value: 0x%x\n",
				  dev->name, err_mask);
			dev->stats.rx_crc_errors++;
			swstats->mem_freed
				+= skb->truesize;
			dev_kfree_skb(skb);
			ring_data->rx_bufs_left -= 1;
			rxdp->Host_Control = 0;
			return 0;
		}
	}

	rxdp->Host_Control = 0;
	if (sp->rxd_mode == RXD_MODE_1) {
		int len = RXD_GET_BUFFER0_SIZE_1(rxdp->Control_2);

		skb_put(skb, len);
	} else if (sp->rxd_mode == RXD_MODE_3B) {
		int get_block = ring_data->rx_curr_get_info.block_index;
		int get_off = ring_data->rx_curr_get_info.offset;
		int buf0_len = RXD_GET_BUFFER0_SIZE_3(rxdp->Control_2);
		int buf2_len = RXD_GET_BUFFER2_SIZE_3(rxdp->Control_2);
		unsigned char *buff = skb_push(skb, buf0_len);

		struct buffAdd *ba = &ring_data->ba[get_block][get_off];
		memcpy(buff, ba->ba_0, buf0_len);
		skb_put(skb, buf2_len);
	}

	if ((rxdp->Control_1 & TCP_OR_UDP_FRAME) &&
	    ((!ring_data->lro) ||
	     (!(rxdp->Control_1 & RXD_FRAME_IP_FRAG))) &&
	    (dev->features & NETIF_F_RXCSUM)) {
		l3_csum = RXD_GET_L3_CKSUM(rxdp->Control_1);
		l4_csum = RXD_GET_L4_CKSUM(rxdp->Control_1);
		if ((l3_csum == L3_CKSUM_OK) && (l4_csum == L4_CKSUM_OK)) {
			/*
			 * NIC verifies if the Checksum of the received
			 * frame is Ok or not and accordingly returns
			 * a flag in the RxD.
			 */
			skb->ip_summed = CHECKSUM_UNNECESSARY;
			if (ring_data->lro) {
				u32 tcp_len = 0;
				u8 *tcp;
				int ret = 0;

				ret = s2io_club_tcp_session(ring_data,
							    skb->data, &tcp,
							    &tcp_len, &lro,
							    rxdp, sp);
				switch (ret) {
				case 3: /* Begin anew */
					lro->parent = skb;
					goto aggregate;
				case 1: /* Aggregate */
					lro_append_pkt(sp, lro, skb, tcp_len);
					goto aggregate;
				case 4: /* Flush session */
					lro_append_pkt(sp, lro, skb, tcp_len);
					queue_rx_frame(lro->parent,
						       lro->vlan_tag);
					clear_lro_session(lro);
					swstats->flush_max_pkts++;
					goto aggregate;
				case 2: /* Flush both */
					lro->parent->data_len = lro->frags_len;
					swstats->sending_both++;
					queue_rx_frame(lro->parent,
						       lro->vlan_tag);
					clear_lro_session(lro);
					goto send_up;
				case 0: /* sessions exceeded */
				case -1: /* non-TCP or not L2 aggregatable */
				case 5: /*
					 * First pkt in session not
					 * L3/L4 aggregatable
					 */
					break;
				default:
					DBG_PRINT(ERR_DBG,
						  "%s: Samadhana!!\n",
						  __func__);
					BUG();
				}
			}
		} else {
			/*
			 * Packet with erroneous checksum, let the
			 * upper layers deal with it.
			 */
			skb_checksum_none_assert(skb);
		}
	} else
		skb_checksum_none_assert(skb);

	swstats->mem_freed += skb->truesize;
send_up:
	skb_record_rx_queue(skb, ring_no);
	queue_rx_frame(skb, RXD_GET_VLAN_TAG(rxdp->Control_2));
aggregate:
	sp->mac_control.rings[ring_no].rx_bufs_left -= 1;
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

static void s2io_link(struct s2io_nic *sp, int link)
{
	struct net_device *dev = sp->dev;
	struct swStat *swstats = &sp->mac_control.stats_info->sw_stat;

	if (link != sp->last_link_state) {
		init_tti(sp, link);
		if (link == LINK_DOWN) {
			DBG_PRINT(ERR_DBG, "%s: Link down\n", dev->name);
			s2io_stop_all_tx_queue(sp);
			netif_carrier_off(dev);
			if (swstats->link_up_cnt)
				swstats->link_up_time =
					jiffies - sp->start_time;
			swstats->link_down_cnt++;
		} else {
			DBG_PRINT(ERR_DBG, "%s: Link Up\n", dev->name);
			if (swstats->link_down_cnt)
				swstats->link_down_time =
					jiffies - sp->start_time;
			swstats->link_up_cnt++;
			netif_carrier_on(dev);
			s2io_wake_all_tx_queue(sp);
		}
	}
	sp->last_link_state = link;
	sp->start_time = jiffies;
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

static void s2io_init_pci(struct s2io_nic *sp)
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

static int s2io_verify_parm(struct pci_dev *pdev, u8 *dev_intr_type,
			    u8 *dev_multiq)
{
	int i;

	if ((tx_fifo_num > MAX_TX_FIFOS) || (tx_fifo_num < 1)) {
		DBG_PRINT(ERR_DBG, "Requested number of tx fifos "
			  "(%d) not supported\n", tx_fifo_num);

		if (tx_fifo_num < 1)
			tx_fifo_num = 1;
		else
			tx_fifo_num = MAX_TX_FIFOS;

		DBG_PRINT(ERR_DBG, "Default to %d tx fifos\n", tx_fifo_num);
	}

	if (multiq)
		*dev_multiq = multiq;

	if (tx_steering_type && (1 == tx_fifo_num)) {
		if (tx_steering_type != TX_DEFAULT_STEERING)
			DBG_PRINT(ERR_DBG,
				  "Tx steering is not supported with "
				  "one fifo. Disabling Tx steering.\n");
		tx_steering_type = NO_STEERING;
	}

	if ((tx_steering_type < NO_STEERING) ||
	    (tx_steering_type > TX_DEFAULT_STEERING)) {
		DBG_PRINT(ERR_DBG,
			  "Requested transmit steering not supported\n");
		DBG_PRINT(ERR_DBG, "Disabling transmit steering\n");
		tx_steering_type = NO_STEERING;
	}

	if (rx_ring_num > MAX_RX_RINGS) {
		DBG_PRINT(ERR_DBG,
			  "Requested number of rx rings not supported\n");
		DBG_PRINT(ERR_DBG, "Default to %d rx rings\n",
			  MAX_RX_RINGS);
		rx_ring_num = MAX_RX_RINGS;
	}

	if ((*dev_intr_type != INTA) && (*dev_intr_type != MSI_X)) {
		DBG_PRINT(ERR_DBG, "Wrong intr_type requested. "
			  "Defaulting to INTA\n");
		*dev_intr_type = INTA;
	}

	if ((*dev_intr_type == MSI_X) &&
	    ((pdev->device != PCI_DEVICE_ID_HERC_WIN) &&
	     (pdev->device != PCI_DEVICE_ID_HERC_UNI))) {
		DBG_PRINT(ERR_DBG, "Xframe I does not support MSI_X. "
			  "Defaulting to INTA\n");
		*dev_intr_type = INTA;
	}

	if ((rx_ring_mode != 1) && (rx_ring_mode != 2)) {
		DBG_PRINT(ERR_DBG, "Requested ring mode not supported\n");
		DBG_PRINT(ERR_DBG, "Defaulting to 1-buffer mode\n");
		rx_ring_mode = 1;
	}

	for (i = 0; i < MAX_RX_RINGS; i++)
		if (rx_ring_sz[i] > MAX_RX_BLOCKS_PER_RING) {
			DBG_PRINT(ERR_DBG, "Requested rx ring size not "
				  "supported\nDefaulting to %d\n",
				  MAX_RX_BLOCKS_PER_RING);
			rx_ring_sz[i] = MAX_RX_BLOCKS_PER_RING;
		}

	return SUCCESS;
}

/**
 * rts_ds_steer - Receive traffic steering based on IPv4 or IPv6 TOS
 * or Traffic class respectively.
 * @nic: device private variable
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

static const struct net_device_ops s2io_netdev_ops = {
	.ndo_open	        = s2io_open,
	.ndo_stop	        = s2io_close,
	.ndo_get_stats	        = s2io_get_stats,
	.ndo_start_xmit    	= s2io_xmit,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_rx_mode	= s2io_set_multicast,
	.ndo_do_ioctl	   	= s2io_ioctl,
	.ndo_set_mac_address    = s2io_set_mac_addr,
	.ndo_change_mtu	   	= s2io_change_mtu,
	.ndo_set_features	= s2io_set_features,
	.ndo_tx_timeout	   	= s2io_tx_watchdog,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller    = s2io_netpoll,
#endif
};

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

static int
s2io_init_nic(struct pci_dev *pdev, const struct pci_device_id *pre)
{
	struct s2io_nic *sp;
	struct net_device *dev;
	int i, j, ret;
	int dma_flag = false;
	u32 mac_up, mac_down;
	u64 val64 = 0, tmp64 = 0;
	struct XENA_dev_config __iomem *bar0 = NULL;
	u16 subid;
	struct config_param *config;
	struct mac_info *mac_control;
	int mode;
	u8 dev_intr_type = intr_type;
	u8 dev_multiq = 0;

	ret = s2io_verify_parm(pdev, &dev_intr_type, &dev_multiq);
	if (ret)
		return ret;

	ret = pci_enable_device(pdev);
	if (ret) {
		DBG_PRINT(ERR_DBG,
			  "%s: pci_enable_device failed\n", __func__);
		return ret;
	}

	if (!pci_set_dma_mask(pdev, DMA_BIT_MASK(64))) {
		DBG_PRINT(INIT_DBG, "%s: Using 64bit DMA\n", __func__);
		dma_flag = true;
		if (pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64))) {
			DBG_PRINT(ERR_DBG,
				  "Unable to obtain 64bit DMA "
				  "for consistent allocations\n");
			pci_disable_device(pdev);
			return -ENOMEM;
		}
	} else if (!pci_set_dma_mask(pdev, DMA_BIT_MASK(32))) {
		DBG_PRINT(INIT_DBG, "%s: Using 32bit DMA\n", __func__);
	} else {
		pci_disable_device(pdev);
		return -ENOMEM;
	}
	ret = pci_request_regions(pdev, s2io_driver_name);
	if (ret) {
		DBG_PRINT(ERR_DBG, "%s: Request Regions failed - %x\n",
			  __func__, ret);
		pci_disable_device(pdev);
		return -ENODEV;
	}
	if (dev_multiq)
		dev = alloc_etherdev_mq(sizeof(struct s2io_nic), tx_fifo_num);
	else
		dev = alloc_etherdev(sizeof(struct s2io_nic));
	if (dev == NULL) {
		pci_disable_device(pdev);
		pci_release_regions(pdev);
		return -ENODEV;
	}

	pci_set_master(pdev);
	pci_set_drvdata(pdev, dev);
	SET_NETDEV_DEV(dev, &pdev->dev);

	/*  Private member variable initialized to s2io NIC structure */
	sp = netdev_priv(dev);
	sp->dev = dev;
	sp->pdev = pdev;
	sp->high_dma_flag = dma_flag;
	sp->device_enabled_once = false;
	if (rx_ring_mode == 1)
		sp->rxd_mode = RXD_MODE_1;
	if (rx_ring_mode == 2)
		sp->rxd_mode = RXD_MODE_3B;

	sp->config.intr_type = dev_intr_type;

	if ((pdev->device == PCI_DEVICE_ID_HERC_WIN) ||
	    (pdev->device == PCI_DEVICE_ID_HERC_UNI))
		sp->device_type = XFRAME_II_DEVICE;
	else
		sp->device_type = XFRAME_I_DEVICE;


	/* Initialize some PCI/PCI-X fields of the NIC. */
	s2io_init_pci(sp);

	/*
	 * Setting the device configuration parameters.
	 * Most of these parameters can be specified by the user during
	 * module insertion as they are module loadable parameters. If
	 * these parameters are not not specified during load time, they
	 * are initialized with default values.
	 */
	config = &sp->config;
	mac_control = &sp->mac_control;

	config->napi = napi;
	config->tx_steering_type = tx_steering_type;

	/* Tx side parameters. */
	if (config->tx_steering_type == TX_PRIORITY_STEERING)
		config->tx_fifo_num = MAX_TX_FIFOS;
	else
		config->tx_fifo_num = tx_fifo_num;

	/* Initialize the fifos used for tx steering */
	if (config->tx_fifo_num < 5) {
		if (config->tx_fifo_num  == 1)
			sp->total_tcp_fifos = 1;
		else
			sp->total_tcp_fifos = config->tx_fifo_num - 1;
		sp->udp_fifo_idx = config->tx_fifo_num - 1;
		sp->total_udp_fifos = 1;
		sp->other_fifo_idx = sp->total_tcp_fifos - 1;
	} else {
		sp->total_tcp_fifos = (tx_fifo_num - FIFO_UDP_MAX_NUM -
				       FIFO_OTHER_MAX_NUM);
		sp->udp_fifo_idx = sp->total_tcp_fifos;
		sp->total_udp_fifos = FIFO_UDP_MAX_NUM;
		sp->other_fifo_idx = sp->udp_fifo_idx + FIFO_UDP_MAX_NUM;
	}

	config->multiq = dev_multiq;
	for (i = 0; i < config->tx_fifo_num; i++) {
		struct tx_fifo_config *tx_cfg = &config->tx_cfg[i];

		tx_cfg->fifo_len = tx_fifo_len[i];
		tx_cfg->fifo_priority = i;
	}

	/* mapping the QoS priority to the configured fifos */
	for (i = 0; i < MAX_TX_FIFOS; i++)
		config->fifo_mapping[i] = fifo_map[config->tx_fifo_num - 1][i];

	/* map the hashing selector table to the configured fifos */
	for (i = 0; i < config->tx_fifo_num; i++)
		sp->fifo_selector[i] = fifo_selector[i];


	config->tx_intr_type = TXD_INT_TYPE_UTILZ;
	for (i = 0; i < config->tx_fifo_num; i++) {
		struct tx_fifo_config *tx_cfg = &config->tx_cfg[i];

		tx_cfg->f_no_snoop = (NO_SNOOP_TXD | NO_SNOOP_TXD_BUFFER);
		if (tx_cfg->fifo_len < 65) {
			config->tx_intr_type = TXD_INT_TYPE_PER_LIST;
			break;
		}
	}
	/* + 2 because one Txd for skb->data and one Txd for UFO */
	config->max_txds = MAX_SKB_FRAGS + 2;

	/* Rx side parameters. */
	config->rx_ring_num = rx_ring_num;
	for (i = 0; i < config->rx_ring_num; i++) {
		struct rx_ring_config *rx_cfg = &config->rx_cfg[i];
		struct ring_info *ring = &mac_control->rings[i];

		rx_cfg->num_rxd = rx_ring_sz[i] * (rxd_count[sp->rxd_mode] + 1);
		rx_cfg->ring_priority = i;
		ring->rx_bufs_left = 0;
		ring->rxd_mode = sp->rxd_mode;
		ring->rxd_count = rxd_count[sp->rxd_mode];
		ring->pdev = sp->pdev;
		ring->dev = sp->dev;
	}

	for (i = 0; i < rx_ring_num; i++) {
		struct rx_ring_config *rx_cfg = &config->rx_cfg[i];

		rx_cfg->ring_org = RING_ORG_BUFF1;
		rx_cfg->f_no_snoop = (NO_SNOOP_RXD | NO_SNOOP_RXD_BUFFER);
	}

	/*  Setting Mac Control parameters */
	mac_control->rmac_pause_time = rmac_pause_time;
	mac_control->mc_pause_threshold_q0q3 = mc_pause_threshold_q0q3;
	mac_control->mc_pause_threshold_q4q7 = mc_pause_threshold_q4q7;


	/*  initialize the shared memory used by the NIC and the host */
	if (init_shared_mem(sp)) {
		DBG_PRINT(ERR_DBG, "%s: Memory allocation failed\n", dev->name);
		ret = -ENOMEM;
		goto mem_alloc_failed;
	}

	sp->bar0 = pci_ioremap_bar(pdev, 0);
	if (!sp->bar0) {
		DBG_PRINT(ERR_DBG, "%s: Neterion: cannot remap io mem1\n",
			  dev->name);
		ret = -ENOMEM;
		goto bar0_remap_failed;
	}

	sp->bar1 = pci_ioremap_bar(pdev, 2);
	if (!sp->bar1) {
		DBG_PRINT(ERR_DBG, "%s: Neterion: cannot remap io mem2\n",
			  dev->name);
		ret = -ENOMEM;
		goto bar1_remap_failed;
	}

	/* Initializing the BAR1 address as the start of the FIFO pointer. */
	for (j = 0; j < MAX_TX_FIFOS; j++) {
		mac_control->tx_FIFO_start[j] = sp->bar1 + (j * 0x00020000);
	}

	/*  Driver entry points */
	dev->netdev_ops = &s2io_netdev_ops;
	dev->ethtool_ops = &netdev_ethtool_ops;
	dev->hw_features = NETIF_F_SG | NETIF_F_IP_CSUM |
		NETIF_F_TSO | NETIF_F_TSO6 |
		NETIF_F_RXCSUM | NETIF_F_LRO;
	dev->features |= dev->hw_features |
		NETIF_F_HW_VLAN_CTAG_TX | NETIF_F_HW_VLAN_CTAG_RX;
	if (sp->high_dma_flag == true)
		dev->features |= NETIF_F_HIGHDMA;
	dev->watchdog_timeo = WATCH_DOG_TIMEOUT;
	INIT_WORK(&sp->rst_timer_task, s2io_restart_nic);
	INIT_WORK(&sp->set_link_task, s2io_set_link);

	pci_save_state(sp->pdev);

	/* Setting swapper control on the NIC, for proper reset operation */
	if (s2io_set_swapper(sp)) {
		DBG_PRINT(ERR_DBG, "%s: swapper settings are wrong\n",
			  dev->name);
		ret = -EAGAIN;
		goto set_swap_failed;
	}

	/* Verify if the Herc works on the slot its placed into */
	if (sp->device_type & XFRAME_II_DEVICE) {
		mode = s2io_verify_pci_mode(sp);
		if (mode < 0) {
			DBG_PRINT(ERR_DBG, "%s: Unsupported PCI bus mode\n",
				  __func__);
			ret = -EBADSLT;
			goto set_swap_failed;
		}
	}

	if (sp->config.intr_type == MSI_X) {
		sp->num_entries = config->rx_ring_num + 1;
		ret = s2io_enable_msi_x(sp);

		if (!ret) {
			ret = s2io_test_msi(sp);
			/* rollback MSI-X, will re-enable during add_isr() */
			remove_msix_isr(sp);
		}
		if (ret) {

			DBG_PRINT(ERR_DBG,
				  "MSI-X requested but failed to enable\n");
			sp->config.intr_type = INTA;
		}
	}

	if (config->intr_type ==  MSI_X) {
		for (i = 0; i < config->rx_ring_num ; i++) {
			struct ring_info *ring = &mac_control->rings[i];

			netif_napi_add(dev, &ring->napi, s2io_poll_msix, 64);
		}
	} else {
		netif_napi_add(dev, &sp->napi, s2io_poll_inta, 64);
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
		RMAC_ADDR_CMD_MEM_OFFSET(0 + S2IO_MAC_ADDR_START_OFFSET);
	writeq(val64, &bar0->rmac_addr_cmd_mem);
	wait_for_cmd_complete(&bar0->rmac_addr_cmd_mem,
			      RMAC_ADDR_CMD_MEM_STROBE_CMD_EXECUTING,
			      S2IO_BIT_RESET);
	tmp64 = readq(&bar0->rmac_addr_data0_mem);
	mac_down = (u32)tmp64;
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

	/* initialize number of multicast & unicast MAC entries variables */
	if (sp->device_type == XFRAME_I_DEVICE) {
		config->max_mc_addr = S2IO_XENA_MAX_MC_ADDRESSES;
		config->max_mac_addr = S2IO_XENA_MAX_MAC_ADDRESSES;
		config->mc_start_offset = S2IO_XENA_MC_ADDR_START_OFFSET;
	} else if (sp->device_type == XFRAME_II_DEVICE) {
		config->max_mc_addr = S2IO_HERC_MAX_MC_ADDRESSES;
		config->max_mac_addr = S2IO_HERC_MAX_MAC_ADDRESSES;
		config->mc_start_offset = S2IO_HERC_MC_ADDR_START_OFFSET;
	}

	/* MTU range: 46 - 9600 */
	dev->min_mtu = MIN_MTU;
	dev->max_mtu = S2IO_JUMBO_SIZE;

	/* store mac addresses from CAM to s2io_nic structure */
	do_s2io_store_unicast_mc(sp);

	/* Configure MSIX vector for number of rings configured plus one */
	if ((sp->device_type == XFRAME_II_DEVICE) &&
	    (config->intr_type == MSI_X))
		sp->num_entries = config->rx_ring_num + 1;

	/* Store the values of the MSIX table in the s2io_nic structure */
	store_xmsi_data(sp);
	/* reset Nic and bring it to known state */
	s2io_reset(sp);

	/*
	 * Initialize link state flags
	 * and the card state parameter
	 */
	sp->state = 0;

	/* Initialize spinlocks */
	for (i = 0; i < sp->config.tx_fifo_num; i++) {
		struct fifo_info *fifo = &mac_control->fifos[i];

		spin_lock_init(&fifo->tx_lock);
	}

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
		writeq(val64, (void __iomem *)bar0 + 0x2700);
		val64 = readq(&bar0->gpio_control);
	}

	sp->rx_csum = 1;	/* Rx chksum verify enabled by default */

	if (register_netdev(dev)) {
		DBG_PRINT(ERR_DBG, "Device registration failed\n");
		ret = -ENODEV;
		goto register_failed;
	}
	s2io_vpd_read(sp);
	DBG_PRINT(ERR_DBG, "Copyright(c) 2002-2010 Exar Corp.\n");
	DBG_PRINT(ERR_DBG, "%s: Neterion %s (rev %d)\n", dev->name,
		  sp->product_name, pdev->revision);
	DBG_PRINT(ERR_DBG, "%s: Driver version %s\n", dev->name,
		  s2io_driver_version);
	DBG_PRINT(ERR_DBG, "%s: MAC Address: %pM\n", dev->name, dev->dev_addr);
	DBG_PRINT(ERR_DBG, "Serial number: %s\n", sp->serial_num);
	if (sp->device_type & XFRAME_II_DEVICE) {
		mode = s2io_print_pci_mode(sp);
		if (mode < 0) {
			ret = -EBADSLT;
			unregister_netdev(dev);
			goto set_swap_failed;
		}
	}
	switch (sp->rxd_mode) {
	case RXD_MODE_1:
		DBG_PRINT(ERR_DBG, "%s: 1-Buffer receive mode enabled\n",
			  dev->name);
		break;
	case RXD_MODE_3B:
		DBG_PRINT(ERR_DBG, "%s: 2-Buffer receive mode enabled\n",
			  dev->name);
		break;
	}

	switch (sp->config.napi) {
	case 0:
		DBG_PRINT(ERR_DBG, "%s: NAPI disabled\n", dev->name);
		break;
	case 1:
		DBG_PRINT(ERR_DBG, "%s: NAPI enabled\n", dev->name);
		break;
	}

	DBG_PRINT(ERR_DBG, "%s: Using %d Tx fifo(s)\n", dev->name,
		  sp->config.tx_fifo_num);

	DBG_PRINT(ERR_DBG, "%s: Using %d Rx ring(s)\n", dev->name,
		  sp->config.rx_ring_num);

	switch (sp->config.intr_type) {
	case INTA:
		DBG_PRINT(ERR_DBG, "%s: Interrupt type INTA\n", dev->name);
		break;
	case MSI_X:
		DBG_PRINT(ERR_DBG, "%s: Interrupt type MSI-X\n", dev->name);
		break;
	}
	if (sp->config.multiq) {
		for (i = 0; i < sp->config.tx_fifo_num; i++) {
			struct fifo_info *fifo = &mac_control->fifos[i];

			fifo->multiq = config->multiq;
		}
		DBG_PRINT(ERR_DBG, "%s: Multiqueue support enabled\n",
			  dev->name);
	} else
		DBG_PRINT(ERR_DBG, "%s: Multiqueue support disabled\n",
			  dev->name);

	switch (sp->config.tx_steering_type) {
	case NO_STEERING:
		DBG_PRINT(ERR_DBG, "%s: No steering enabled for transmit\n",
			  dev->name);
		break;
	case TX_PRIORITY_STEERING:
		DBG_PRINT(ERR_DBG,
			  "%s: Priority steering enabled for transmit\n",
			  dev->name);
		break;
	case TX_DEFAULT_STEERING:
		DBG_PRINT(ERR_DBG,
			  "%s: Default steering enabled for transmit\n",
			  dev->name);
	}

	DBG_PRINT(ERR_DBG, "%s: Large receive offload enabled\n",
		  dev->name);
	/* Initialize device name */
	snprintf(sp->name, sizeof(sp->name), "%s Neterion %s", dev->name,
		 sp->product_name);

	if (vlan_tag_strip)
		sp->vlan_strip_flag = 1;
	else
		sp->vlan_strip_flag = 0;

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
	pci_release_regions(pdev);
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

static void s2io_rem_nic(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct s2io_nic *sp;

	if (dev == NULL) {
		DBG_PRINT(ERR_DBG, "Driver Data is NULL!!\n");
		return;
	}

	sp = netdev_priv(dev);

	cancel_work_sync(&sp->rst_timer_task);
	cancel_work_sync(&sp->set_link_task);

	unregister_netdev(dev);

	free_shared_mem(sp);
	iounmap(sp->bar0);
	iounmap(sp->bar1);
	pci_release_regions(pdev);
	free_netdev(dev);
	pci_disable_device(pdev);
}

module_pci_driver(s2io_driver);

static int check_L2_lro_capable(u8 *buffer, struct iphdr **ip,
				struct tcphdr **tcp, struct RxD_t *rxdp,
				struct s2io_nic *sp)
{
	int ip_off;
	u8 l2_type = (u8)((rxdp->Control_1 >> 37) & 0x7), ip_len;

	if (!(rxdp->Control_1 & RXD_FRAME_PROTO_TCP)) {
		DBG_PRINT(INIT_DBG,
			  "%s: Non-TCP frames not supported for LRO\n",
			  __func__);
		return -1;
	}

	/* Checking for DIX type or DIX type with VLAN */
	if ((l2_type == 0) || (l2_type == 4)) {
		ip_off = HEADER_ETHERNET_II_802_3_SIZE;
		/*
		 * If vlan stripping is disabled and the frame is VLAN tagged,
		 * shift the offset by the VLAN header size bytes.
		 */
		if ((!sp->vlan_strip_flag) &&
		    (rxdp->Control_1 & RXD_FRAME_VLAN_TAG))
			ip_off += HEADER_VLAN_SIZE;
	} else {
		/* LLC, SNAP etc are considered non-mergeable */
		return -1;
	}

	*ip = (struct iphdr *)(buffer + ip_off);
	ip_len = (u8)((*ip)->ihl);
	ip_len <<= 2;
	*tcp = (struct tcphdr *)((unsigned long)*ip + ip_len);

	return 0;
}

static int check_for_socket_match(struct lro *lro, struct iphdr *ip,
				  struct tcphdr *tcp)
{
	DBG_PRINT(INFO_DBG, "%s: Been here...\n", __func__);
	if ((lro->iph->saddr != ip->saddr) ||
	    (lro->iph->daddr != ip->daddr) ||
	    (lro->tcph->source != tcp->source) ||
	    (lro->tcph->dest != tcp->dest))
		return -1;
	return 0;
}

static inline int get_l4_pyld_length(struct iphdr *ip, struct tcphdr *tcp)
{
	return ntohs(ip->tot_len) - (ip->ihl << 2) - (tcp->doff << 2);
}

static void initiate_new_session(struct lro *lro, u8 *l2h,
				 struct iphdr *ip, struct tcphdr *tcp,
				 u32 tcp_pyld_len, u16 vlan_tag)
{
	DBG_PRINT(INFO_DBG, "%s: Been here...\n", __func__);
	lro->l2h = l2h;
	lro->iph = ip;
	lro->tcph = tcp;
	lro->tcp_next_seq = tcp_pyld_len + ntohl(tcp->seq);
	lro->tcp_ack = tcp->ack_seq;
	lro->sg_num = 1;
	lro->total_len = ntohs(ip->tot_len);
	lro->frags_len = 0;
	lro->vlan_tag = vlan_tag;
	/*
	 * Check if we saw TCP timestamp.
	 * Other consistency checks have already been done.
	 */
	if (tcp->doff == 8) {
		__be32 *ptr;
		ptr = (__be32 *)(tcp+1);
		lro->saw_ts = 1;
		lro->cur_tsval = ntohl(*(ptr+1));
		lro->cur_tsecr = *(ptr+2);
	}
	lro->in_use = 1;
}

static void update_L3L4_header(struct s2io_nic *sp, struct lro *lro)
{
	struct iphdr *ip = lro->iph;
	struct tcphdr *tcp = lro->tcph;
	struct swStat *swstats = &sp->mac_control.stats_info->sw_stat;

	DBG_PRINT(INFO_DBG, "%s: Been here...\n", __func__);

	/* Update L3 header */
	csum_replace2(&ip->check, ip->tot_len, htons(lro->total_len));
	ip->tot_len = htons(lro->total_len);

	/* Update L4 header */
	tcp->ack_seq = lro->tcp_ack;
	tcp->window = lro->window;

	/* Update tsecr field if this session has timestamps enabled */
	if (lro->saw_ts) {
		__be32 *ptr = (__be32 *)(tcp + 1);
		*(ptr+2) = lro->cur_tsecr;
	}

	/* Update counters required for calculation of
	 * average no. of packets aggregated.
	 */
	swstats->sum_avg_pkts_aggregated += lro->sg_num;
	swstats->num_aggregations++;
}

static void aggregate_new_rx(struct lro *lro, struct iphdr *ip,
			     struct tcphdr *tcp, u32 l4_pyld)
{
	DBG_PRINT(INFO_DBG, "%s: Been here...\n", __func__);
	lro->total_len += l4_pyld;
	lro->frags_len += l4_pyld;
	lro->tcp_next_seq += l4_pyld;
	lro->sg_num++;

	/* Update ack seq no. and window ad(from this pkt) in LRO object */
	lro->tcp_ack = tcp->ack_seq;
	lro->window = tcp->window;

	if (lro->saw_ts) {
		__be32 *ptr;
		/* Update tsecr and tsval from this packet */
		ptr = (__be32 *)(tcp+1);
		lro->cur_tsval = ntohl(*(ptr+1));
		lro->cur_tsecr = *(ptr + 2);
	}
}

static int verify_l3_l4_lro_capable(struct lro *l_lro, struct iphdr *ip,
				    struct tcphdr *tcp, u32 tcp_pyld_len)
{
	u8 *ptr;

	DBG_PRINT(INFO_DBG, "%s: Been here...\n", __func__);

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
	if (tcp->urg || tcp->psh || tcp->rst ||
	    tcp->syn || tcp->fin ||
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
			if (l_lro->cur_tsval > ntohl(*((__be32 *)(ptr+2))))
				return -1;

		/* timestamp echo reply should be non-zero */
		if (*((__be32 *)(ptr+6)) == 0)
			return -1;
	}

	return 0;
}

static int s2io_club_tcp_session(struct ring_info *ring_data, u8 *buffer,
				 u8 **tcp, u32 *tcp_len, struct lro **lro,
				 struct RxD_t *rxdp, struct s2io_nic *sp)
{
	struct iphdr *ip;
	struct tcphdr *tcph;
	int ret = 0, i;
	u16 vlan_tag = 0;
	struct swStat *swstats = &sp->mac_control.stats_info->sw_stat;

	ret = check_L2_lro_capable(buffer, &ip, (struct tcphdr **)tcp,
				   rxdp, sp);
	if (ret)
		return ret;

	DBG_PRINT(INFO_DBG, "IP Saddr: %x Daddr: %x\n", ip->saddr, ip->daddr);

	vlan_tag = RXD_GET_VLAN_TAG(rxdp->Control_2);
	tcph = (struct tcphdr *)*tcp;
	*tcp_len = get_l4_pyld_length(ip, tcph);
	for (i = 0; i < MAX_LRO_SESSIONS; i++) {
		struct lro *l_lro = &ring_data->lro0_n[i];
		if (l_lro->in_use) {
			if (check_for_socket_match(l_lro, ip, tcph))
				continue;
			/* Sock pair matched */
			*lro = l_lro;

			if ((*lro)->tcp_next_seq != ntohl(tcph->seq)) {
				DBG_PRINT(INFO_DBG, "%s: Out of sequence. "
					  "expected 0x%x, actual 0x%x\n",
					  __func__,
					  (*lro)->tcp_next_seq,
					  ntohl(tcph->seq));

				swstats->outof_sequence_pkts++;
				ret = 2;
				break;
			}

			if (!verify_l3_l4_lro_capable(l_lro, ip, tcph,
						      *tcp_len))
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
		if (verify_l3_l4_lro_capable(NULL, ip, tcph, *tcp_len))
			return 5;

		for (i = 0; i < MAX_LRO_SESSIONS; i++) {
			struct lro *l_lro = &ring_data->lro0_n[i];
			if (!(l_lro->in_use)) {
				*lro = l_lro;
				ret = 3; /* Begin anew */
				break;
			}
		}
	}

	if (ret == 0) { /* sessions exceeded */
		DBG_PRINT(INFO_DBG, "%s: All LRO sessions already in use\n",
			  __func__);
		*lro = NULL;
		return ret;
	}

	switch (ret) {
	case 3:
		initiate_new_session(*lro, buffer, ip, tcph, *tcp_len,
				     vlan_tag);
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
		DBG_PRINT(ERR_DBG, "%s: Don't know, can't say!!\n", __func__);
		break;
	}

	return ret;
}

static void clear_lro_session(struct lro *lro)
{
	static u16 lro_struct_size = sizeof(struct lro);

	memset(lro, 0, lro_struct_size);
}

static void queue_rx_frame(struct sk_buff *skb, u16 vlan_tag)
{
	struct net_device *dev = skb->dev;
	struct s2io_nic *sp = netdev_priv(dev);

	skb->protocol = eth_type_trans(skb, dev);
	if (vlan_tag && sp->vlan_strip_flag)
		__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q), vlan_tag);
	if (sp->config.napi)
		netif_receive_skb(skb);
	else
		netif_rx(skb);
}

static void lro_append_pkt(struct s2io_nic *sp, struct lro *lro,
			   struct sk_buff *skb, u32 tcp_len)
{
	struct sk_buff *first = lro->parent;
	struct swStat *swstats = &sp->mac_control.stats_info->sw_stat;

	first->len += tcp_len;
	first->data_len = lro->frags_len;
	skb_pull(skb, (skb->len - tcp_len));
	if (skb_shinfo(first)->frag_list)
		lro->last_frag->next = skb;
	else
		skb_shinfo(first)->frag_list = skb;
	first->truesize += skb->truesize;
	lro->last_frag = skb;
	swstats->clubbed_frms_cnt++;
}

/**
 * s2io_io_error_detected - called when PCI error is detected
 * @pdev: Pointer to PCI device
 * @state: The current pci connection state
 *
 * This function is called after a PCI bus error affecting
 * this device has been detected.
 */
static pci_ers_result_t s2io_io_error_detected(struct pci_dev *pdev,
					       pci_channel_state_t state)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct s2io_nic *sp = netdev_priv(netdev);

	netif_device_detach(netdev);

	if (state == pci_channel_io_perm_failure)
		return PCI_ERS_RESULT_DISCONNECT;

	if (netif_running(netdev)) {
		/* Bring down the card, while avoiding PCI I/O */
		do_s2io_card_down(sp, 0);
	}
	pci_disable_device(pdev);

	return PCI_ERS_RESULT_NEED_RESET;
}

/**
 * s2io_io_slot_reset - called after the pci bus has been reset.
 * @pdev: Pointer to PCI device
 *
 * Restart the card from scratch, as if from a cold-boot.
 * At this point, the card has exprienced a hard reset,
 * followed by fixups by BIOS, and has its config space
 * set up identically to what it was at cold boot.
 */
static pci_ers_result_t s2io_io_slot_reset(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct s2io_nic *sp = netdev_priv(netdev);

	if (pci_enable_device(pdev)) {
		pr_err("Cannot re-enable PCI device after reset.\n");
		return PCI_ERS_RESULT_DISCONNECT;
	}

	pci_set_master(pdev);
	s2io_reset(sp);

	return PCI_ERS_RESULT_RECOVERED;
}

/**
 * s2io_io_resume - called when traffic can start flowing again.
 * @pdev: Pointer to PCI device
 *
 * This callback is called when the error recovery driver tells
 * us that its OK to resume normal operation.
 */
static void s2io_io_resume(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct s2io_nic *sp = netdev_priv(netdev);

	if (netif_running(netdev)) {
		if (s2io_card_up(sp)) {
			pr_err("Can't bring device back up after reset.\n");
			return;
		}

		if (s2io_set_mac_addr(netdev, netdev->dev_addr) == FAILURE) {
			s2io_card_down(sp);
			pr_err("Can't restore mac addr after reset.\n");
			return;
		}
	}

	netif_device_attach(netdev);
	netif_tx_wake_all_queues(netdev);
}
