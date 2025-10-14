// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#include <linux/bitfield.h>
#include <net/tcp.h>

#include "fbnic.h"
#include "fbnic_mac.h"
#include "fbnic_netdev.h"

static void fbnic_init_readrq(struct fbnic_dev *fbd, unsigned int offset,
			      unsigned int cls, unsigned int readrq)
{
	u32 val = rd32(fbd, offset);

	/* The TDF_CTL masks are a superset of the RNI_RBP ones. So we can
	 * use them when setting either the TDE_CTF or RNI_RBP registers.
	 */
	val &= FBNIC_QM_TNI_TDF_CTL_MAX_OT | FBNIC_QM_TNI_TDF_CTL_MAX_OB;

	val |= FIELD_PREP(FBNIC_QM_TNI_TDF_CTL_MRRS, readrq) |
	       FIELD_PREP(FBNIC_QM_TNI_TDF_CTL_CLS, cls);

	wr32(fbd, offset, val);
}

static void fbnic_init_mps(struct fbnic_dev *fbd, unsigned int offset,
			   unsigned int cls, unsigned int mps)
{
	u32 val = rd32(fbd, offset);

	/* Currently all MPS masks are identical so just use the first one */
	val &= ~(FBNIC_QM_TNI_TCM_CTL_MPS | FBNIC_QM_TNI_TCM_CTL_CLS);

	val |= FIELD_PREP(FBNIC_QM_TNI_TCM_CTL_MPS, mps) |
	       FIELD_PREP(FBNIC_QM_TNI_TCM_CTL_CLS, cls);

	wr32(fbd, offset, val);
}

static void fbnic_mac_init_axi(struct fbnic_dev *fbd)
{
	bool override_1k = false;
	int readrq, mps, cls;

	/* All of the values are based on being a power of 2 starting
	 * with 64 == 0. Therefore we can either divide by 64 in the
	 * case of constants, or just subtract 6 from the log2 of the value
	 * in order to get the value we will be programming into the
	 * registers.
	 */
	readrq = ilog2(fbd->readrq) - 6;
	if (readrq > 3)
		override_1k = true;
	readrq = clamp(readrq, 0, 3);

	mps = ilog2(fbd->mps) - 6;
	mps = clamp(mps, 0, 3);

	cls = ilog2(L1_CACHE_BYTES) - 6;
	cls = clamp(cls, 0, 3);

	/* Configure Tx/Rx AXI Paths w/ Read Request and Max Payload sizes */
	fbnic_init_readrq(fbd, FBNIC_QM_TNI_TDF_CTL, cls, readrq);
	fbnic_init_mps(fbd, FBNIC_QM_TNI_TCM_CTL, cls, mps);

	/* Configure QM TNI TDE:
	 * - Max outstanding AXI beats to 704(768 - 64) - guaranetees 8% of
	 *   buffer capacity to descriptors.
	 * - Max outstanding transactions to 128
	 */
	wr32(fbd, FBNIC_QM_TNI_TDE_CTL,
	     FIELD_PREP(FBNIC_QM_TNI_TDE_CTL_MRRS_1K, override_1k ? 1 : 0) |
	     FIELD_PREP(FBNIC_QM_TNI_TDE_CTL_MAX_OB, 704) |
	     FIELD_PREP(FBNIC_QM_TNI_TDE_CTL_MAX_OT, 128) |
	     FIELD_PREP(FBNIC_QM_TNI_TDE_CTL_MRRS, readrq) |
	     FIELD_PREP(FBNIC_QM_TNI_TDE_CTL_CLS, cls));

	fbnic_init_readrq(fbd, FBNIC_QM_RNI_RBP_CTL, cls, readrq);
	fbnic_init_mps(fbd, FBNIC_QM_RNI_RDE_CTL, cls, mps);
	fbnic_init_mps(fbd, FBNIC_QM_RNI_RCM_CTL, cls, mps);
}

static void fbnic_mac_init_qm(struct fbnic_dev *fbd)
{
	u64 default_meta = FIELD_PREP(FBNIC_TWD_L2_HLEN_MASK, ETH_HLEN) |
			   FBNIC_TWD_FLAG_REQ_COMPLETION;
	u32 clock_freq;

	/* Configure default TWQ Metadata descriptor */
	wr32(fbd, FBNIC_QM_TWQ_DEFAULT_META_L,
	     lower_32_bits(default_meta));
	wr32(fbd, FBNIC_QM_TWQ_DEFAULT_META_H,
	     upper_32_bits(default_meta));

	/* Configure TSO behavior */
	wr32(fbd, FBNIC_QM_TQS_CTL0,
	     FIELD_PREP(FBNIC_QM_TQS_CTL0_LSO_TS_MASK,
			FBNIC_QM_TQS_CTL0_LSO_TS_LAST) |
	     FIELD_PREP(FBNIC_QM_TQS_CTL0_PREFETCH_THRESH,
			FBNIC_QM_TQS_CTL0_PREFETCH_THRESH_MIN));

	/* Limit EDT to INT_MAX as this is the limit of the EDT Qdisc */
	wr32(fbd, FBNIC_QM_TQS_EDT_TS_RANGE, INT_MAX);

	/* Configure MTU
	 * Due to known HW issue we cannot set the MTU to within 16 octets
	 * of a 64 octet aligned boundary. So we will set the TQS_MTU(s) to
	 * MTU + 1.
	 */
	wr32(fbd, FBNIC_QM_TQS_MTU_CTL0, FBNIC_MAX_JUMBO_FRAME_SIZE + 1);
	wr32(fbd, FBNIC_QM_TQS_MTU_CTL1,
	     FIELD_PREP(FBNIC_QM_TQS_MTU_CTL1_BULK,
			FBNIC_MAX_JUMBO_FRAME_SIZE + 1));

	clock_freq = FBNIC_CLOCK_FREQ;

	/* Be aggressive on the timings. We will have the interrupt
	 * threshold timer tick once every 1 usec and coalesce writes for
	 * up to 80 usecs.
	 */
	wr32(fbd, FBNIC_QM_TCQ_CTL0,
	     FIELD_PREP(FBNIC_QM_TCQ_CTL0_TICK_CYCLES,
			clock_freq / 1000000) |
	     FIELD_PREP(FBNIC_QM_TCQ_CTL0_COAL_WAIT,
			clock_freq / 12500));

	/* We will have the interrupt threshold timer tick once every
	 * 1 usec and coalesce writes for up to 2 usecs.
	 */
	wr32(fbd, FBNIC_QM_RCQ_CTL0,
	     FIELD_PREP(FBNIC_QM_RCQ_CTL0_TICK_CYCLES,
			clock_freq / 1000000) |
	     FIELD_PREP(FBNIC_QM_RCQ_CTL0_COAL_WAIT,
			clock_freq / 500000));

	/* Configure spacer control to 64 beats. */
	wr32(fbd, FBNIC_FAB_AXI4_AR_SPACER_2_CFG,
	     FBNIC_FAB_AXI4_AR_SPACER_MASK |
	     FIELD_PREP(FBNIC_FAB_AXI4_AR_SPACER_THREADSHOLD, 2));
}

#define FBNIC_DROP_EN_MASK	0x7d
#define FBNIC_PAUSE_EN_MASK	0x14
#define FBNIC_ECN_EN_MASK	0x10

struct fbnic_fifo_config {
	unsigned int addr;
	unsigned int size;
};

/* Rx FIFO Configuration
 * The table consists of 8 entries, of which only 4 are currently used
 * The starting addr is in units of 64B and the size is in 2KB units
 * Below is the human readable version of the table defined below:
 * Function		Addr	Size
 * ----------------------------------
 * Network to Host/BMC	384K	64K
 * Unused
 * Unused
 * Network to BMC	448K	32K
 * Network to Host	0	384K
 * Unused
 * BMC to Host		480K	32K
 * Unused
 */
static const struct fbnic_fifo_config fifo_config[] = {
	{ .addr = 0x1800, .size = 0x20 },	/* Network to Host/BMC */
	{ },					/* Unused */
	{ },					/* Unused */
	{ .addr = 0x1c00, .size = 0x10 },	/* Network to BMC */
	{ .addr = 0x0000, .size = 0xc0 },	/* Network to Host */
	{ },					/* Unused */
	{ .addr = 0x1e00, .size = 0x10 },	/* BMC to Host */
	{ }					/* Unused */
};

static void fbnic_mac_init_rxb(struct fbnic_dev *fbd)
{
	bool rx_enable;
	int i;

	rx_enable = !!(rd32(fbd, FBNIC_RPC_RMI_CONFIG) &
		       FBNIC_RPC_RMI_CONFIG_ENABLE);

	for (i = 0; i < 8; i++) {
		unsigned int size = fifo_config[i].size;

		/* If we are coming up on a system that already has the
		 * Rx data path enabled we don't need to reconfigure the
		 * FIFOs. Instead we can check to verify the values are
		 * large enough to meet our needs, and use the values to
		 * populate the flow control, ECN, and drop thresholds.
		 */
		if (rx_enable) {
			size = FIELD_GET(FBNIC_RXB_PBUF_SIZE,
					 rd32(fbd, FBNIC_RXB_PBUF_CFG(i)));
			if (size < fifo_config[i].size)
				dev_warn(fbd->dev,
					 "fifo%d size of %d smaller than expected value of %d\n",
					 i, size << 11,
					 fifo_config[i].size << 11);
		} else {
			/* Program RXB Cuthrough */
			wr32(fbd, FBNIC_RXB_CT_SIZE(i),
			     FIELD_PREP(FBNIC_RXB_CT_SIZE_HEADER, 4) |
			     FIELD_PREP(FBNIC_RXB_CT_SIZE_PAYLOAD, 2));

			/* The granularity for the packet buffer size is 2KB
			 * granularity while the packet buffer base address is
			 * only 64B granularity
			 */
			wr32(fbd, FBNIC_RXB_PBUF_CFG(i),
			     FIELD_PREP(FBNIC_RXB_PBUF_BASE_ADDR,
					fifo_config[i].addr) |
			     FIELD_PREP(FBNIC_RXB_PBUF_SIZE, size));

			/* The granularity for the credits is 64B. This is
			 * based on RXB_PBUF_SIZE * 32 + 4.
			 */
			wr32(fbd, FBNIC_RXB_PBUF_CREDIT(i),
			     FIELD_PREP(FBNIC_RXB_PBUF_CREDIT_MASK,
					size ? size * 32 + 4 : 0));
		}

		if (!size)
			continue;

		/* Pause is size of FIFO with 56KB skid to start/stop */
		wr32(fbd, FBNIC_RXB_PAUSE_THLD(i),
		     !(FBNIC_PAUSE_EN_MASK & (1u << i)) ? 0x1fff :
		     FIELD_PREP(FBNIC_RXB_PAUSE_THLD_ON,
				size * 32 - 0x380) |
		     FIELD_PREP(FBNIC_RXB_PAUSE_THLD_OFF, 0x380));

		/* Enable Drop when only one packet is left in the FIFO */
		wr32(fbd, FBNIC_RXB_DROP_THLD(i),
		     !(FBNIC_DROP_EN_MASK & (1u << i)) ? 0x1fff :
		     FIELD_PREP(FBNIC_RXB_DROP_THLD_ON,
				size * 32 -
				FBNIC_MAX_JUMBO_FRAME_SIZE / 64) |
		     FIELD_PREP(FBNIC_RXB_DROP_THLD_OFF,
				size * 32 -
				FBNIC_MAX_JUMBO_FRAME_SIZE / 64));

		/* Enable ECN bit when 1/4 of RXB is filled with at least
		 * 1 room for one full jumbo frame before setting ECN
		 */
		wr32(fbd, FBNIC_RXB_ECN_THLD(i),
		     !(FBNIC_ECN_EN_MASK & (1u << i)) ? 0x1fff :
		     FIELD_PREP(FBNIC_RXB_ECN_THLD_ON,
				max_t(unsigned int,
				      size * 32 / 4,
				      FBNIC_MAX_JUMBO_FRAME_SIZE / 64)) |
		     FIELD_PREP(FBNIC_RXB_ECN_THLD_OFF,
				max_t(unsigned int,
				      size * 32 / 4,
				      FBNIC_MAX_JUMBO_FRAME_SIZE / 64)));
	}

	/* For now only enable drop and ECN. We need to add driver/kernel
	 * interfaces for configuring pause.
	 */
	wr32(fbd, FBNIC_RXB_PAUSE_DROP_CTRL,
	     FIELD_PREP(FBNIC_RXB_PAUSE_DROP_CTRL_DROP_ENABLE,
			FBNIC_DROP_EN_MASK) |
	     FIELD_PREP(FBNIC_RXB_PAUSE_DROP_CTRL_ECN_ENABLE,
			FBNIC_ECN_EN_MASK));

	/* Program INTF credits */
	wr32(fbd, FBNIC_RXB_INTF_CREDIT,
	     FBNIC_RXB_INTF_CREDIT_MASK0 |
	     FBNIC_RXB_INTF_CREDIT_MASK1 |
	     FBNIC_RXB_INTF_CREDIT_MASK2 |
	     FIELD_PREP(FBNIC_RXB_INTF_CREDIT_MASK3, 8));

	/* Configure calendar slots.
	 * Rx: 0 - 62	RDE 1st, BMC 2nd
	 *     63	BMC 1st, RDE 2nd
	 */
	for (i = 0; i < 16; i++) {
		u32 calendar_val = (i == 15) ? 0x1e1b1b1b : 0x1b1b1b1b;

		wr32(fbd, FBNIC_RXB_CLDR_PRIO_CFG(i), calendar_val);
	}

	/* Split the credits for the DRR up as follows:
	 * Quantum0: 8000	Network to Host
	 * Quantum1: 0		Not used
	 * Quantum2: 80		BMC to Host
	 * Quantum3: 0		Not used
	 * Quantum4: 8000	Multicast to Host and BMC
	 */
	wr32(fbd, FBNIC_RXB_DWRR_RDE_WEIGHT0,
	     FIELD_PREP(FBNIC_RXB_DWRR_RDE_WEIGHT0_QUANTUM0, 0x40) |
	     FIELD_PREP(FBNIC_RXB_DWRR_RDE_WEIGHT0_QUANTUM2, 0x50));
	wr32(fbd, FBNIC_RXB_DWRR_RDE_WEIGHT0_EXT,
	     FIELD_PREP(FBNIC_RXB_DWRR_RDE_WEIGHT0_QUANTUM0, 0x1f));
	wr32(fbd, FBNIC_RXB_DWRR_RDE_WEIGHT1,
	     FIELD_PREP(FBNIC_RXB_DWRR_RDE_WEIGHT1_QUANTUM4, 0x40));
	wr32(fbd, FBNIC_RXB_DWRR_RDE_WEIGHT1_EXT,
	     FIELD_PREP(FBNIC_RXB_DWRR_RDE_WEIGHT1_QUANTUM4, 0x1f));

	/* Program RXB FCS Endian register */
	wr32(fbd, FBNIC_RXB_ENDIAN_FCS, 0x0aaaaaa0);
}

static void fbnic_mac_init_txb(struct fbnic_dev *fbd)
{
	int i;

	wr32(fbd, FBNIC_TCE_TXB_CTRL, 0);

	/* Configure Tx QM Credits */
	wr32(fbd, FBNIC_QM_TQS_CTL1,
	     FIELD_PREP(FBNIC_QM_TQS_CTL1_MC_MAX_CREDITS, 0x40) |
	     FIELD_PREP(FBNIC_QM_TQS_CTL1_BULK_MAX_CREDITS, 0x20));

	/* Initialize internal Tx queues */
	wr32(fbd, FBNIC_TCE_TXB_TEI_Q0_CTRL, 0);
	wr32(fbd, FBNIC_TCE_TXB_TEI_Q1_CTRL, 0);
	wr32(fbd, FBNIC_TCE_TXB_MC_Q_CTRL,
	     FIELD_PREP(FBNIC_TCE_TXB_Q_CTRL_SIZE, 0x400) |
	     FIELD_PREP(FBNIC_TCE_TXB_Q_CTRL_START, 0x000));
	wr32(fbd, FBNIC_TCE_TXB_RX_TEI_Q_CTRL, 0);
	wr32(fbd, FBNIC_TCE_TXB_TX_BMC_Q_CTRL,
	     FIELD_PREP(FBNIC_TCE_TXB_Q_CTRL_SIZE, 0x200) |
	     FIELD_PREP(FBNIC_TCE_TXB_Q_CTRL_START, 0x400));
	wr32(fbd, FBNIC_TCE_TXB_RX_BMC_Q_CTRL,
	     FIELD_PREP(FBNIC_TCE_TXB_Q_CTRL_SIZE, 0x200) |
	     FIELD_PREP(FBNIC_TCE_TXB_Q_CTRL_START, 0x600));

	wr32(fbd, FBNIC_TCE_LSO_CTRL,
	     FBNIC_TCE_LSO_CTRL_IPID_MODE_INC |
	     FIELD_PREP(FBNIC_TCE_LSO_CTRL_TCPF_CLR_1ST, TCPHDR_PSH |
							 TCPHDR_FIN) |
	     FIELD_PREP(FBNIC_TCE_LSO_CTRL_TCPF_CLR_MID, TCPHDR_PSH |
							 TCPHDR_CWR |
							 TCPHDR_FIN) |
	     FIELD_PREP(FBNIC_TCE_LSO_CTRL_TCPF_CLR_END, TCPHDR_CWR));
	wr32(fbd, FBNIC_TCE_CSO_CTRL, 0);

	wr32(fbd, FBNIC_TCE_BMC_MAX_PKTSZ,
	     FIELD_PREP(FBNIC_TCE_BMC_MAX_PKTSZ_TX,
			FBNIC_MAX_JUMBO_FRAME_SIZE) |
	     FIELD_PREP(FBNIC_TCE_BMC_MAX_PKTSZ_RX,
			FBNIC_MAX_JUMBO_FRAME_SIZE));
	wr32(fbd, FBNIC_TCE_MC_MAX_PKTSZ,
	     FIELD_PREP(FBNIC_TCE_MC_MAX_PKTSZ_TMI,
			FBNIC_MAX_JUMBO_FRAME_SIZE));

	/* Configure calendar slots.
	 * Tx: 0 - 62	TMI 1st, BMC 2nd
	 *     63	BMC 1st, TMI 2nd
	 */
	for (i = 0; i < 16; i++) {
		u32 calendar_val = (i == 15) ? 0x1e1b1b1b : 0x1b1b1b1b;

		wr32(fbd, FBNIC_TCE_TXB_CLDR_SLOT_CFG(i), calendar_val);
	}

	/* Configure DWRR */
	wr32(fbd, FBNIC_TCE_TXB_ENQ_WRR_CTRL,
	     FIELD_PREP(FBNIC_TCE_TXB_ENQ_WRR_CTRL_WEIGHT0, 0x64) |
	     FIELD_PREP(FBNIC_TCE_TXB_ENQ_WRR_CTRL_WEIGHT2, 0x04));
	wr32(fbd, FBNIC_TCE_TXB_TEI_DWRR_CTRL, 0);
	wr32(fbd, FBNIC_TCE_TXB_TEI_DWRR_CTRL_EXT, 0);
	wr32(fbd, FBNIC_TCE_TXB_BMC_DWRR_CTRL,
	     FIELD_PREP(FBNIC_TCE_TXB_BMC_DWRR_CTRL_QUANTUM0, 0x50) |
	     FIELD_PREP(FBNIC_TCE_TXB_BMC_DWRR_CTRL_QUANTUM1, 0x82));
	wr32(fbd, FBNIC_TCE_TXB_BMC_DWRR_CTRL_EXT, 0);
	wr32(fbd, FBNIC_TCE_TXB_NTWRK_DWRR_CTRL,
	     FIELD_PREP(FBNIC_TCE_TXB_NTWRK_DWRR_CTRL_QUANTUM1, 0x50) |
	     FIELD_PREP(FBNIC_TCE_TXB_NTWRK_DWRR_CTRL_QUANTUM2, 0x20));
	wr32(fbd, FBNIC_TCE_TXB_NTWRK_DWRR_CTRL_EXT,
	     FIELD_PREP(FBNIC_TCE_TXB_NTWRK_DWRR_CTRL_QUANTUM2, 0x03));

	/* Configure SOP protocol protection */
	wr32(fbd, FBNIC_TCE_SOP_PROT_CTRL,
	     FIELD_PREP(FBNIC_TCE_SOP_PROT_CTRL_TBI, 0x78) |
	     FIELD_PREP(FBNIC_TCE_SOP_PROT_CTRL_TTI_FRM, 0x40) |
	     FIELD_PREP(FBNIC_TCE_SOP_PROT_CTRL_TTI_CM, 0x0c));

	/* Conservative configuration on MAC interface Start of Packet
	 * protection FIFO. This sets the minimum depth of the FIFO before
	 * we start sending packets to the MAC measured in 64B units and
	 * up to 160 entries deep.
	 *
	 * For the ASIC the clock is fast enough that we will likely fill
	 * the SOP FIFO before the MAC can drain it. So just use a minimum
	 * value of 8.
	 */
	wr32(fbd, FBNIC_TMI_SOP_PROT_CTRL, 8);

	wrfl(fbd);
	wr32(fbd, FBNIC_TCE_TXB_CTRL, FBNIC_TCE_TXB_CTRL_TCAM_ENABLE |
				      FBNIC_TCE_TXB_CTRL_LOAD);
}

static void fbnic_mac_init_regs(struct fbnic_dev *fbd)
{
	fbnic_mac_init_axi(fbd);
	fbnic_mac_init_qm(fbd);
	fbnic_mac_init_rxb(fbd);
	fbnic_mac_init_txb(fbd);
}

static void __fbnic_mac_stat_rd64(struct fbnic_dev *fbd, bool reset, u32 reg,
				  struct fbnic_stat_counter *stat)
{
	u64 new_reg_value;

	new_reg_value = fbnic_stat_rd64(fbd, reg, 1);
	if (!reset)
		stat->value += new_reg_value - stat->u.old_reg_value_64;
	stat->u.old_reg_value_64 = new_reg_value;
	stat->reported = true;
}

#define fbnic_mac_stat_rd64(fbd, reset, __stat, __CSR) \
	__fbnic_mac_stat_rd64(fbd, reset, FBNIC_##__CSR##_L, &(__stat))

static void fbnic_mac_tx_pause_config(struct fbnic_dev *fbd, bool tx_pause)
{
	u32 rxb_pause_ctrl;

	/* Enable generation of pause frames if enabled */
	rxb_pause_ctrl = rd32(fbd, FBNIC_RXB_PAUSE_DROP_CTRL);
	rxb_pause_ctrl &= ~FBNIC_RXB_PAUSE_DROP_CTRL_PAUSE_ENABLE;
	if (tx_pause)
		rxb_pause_ctrl |=
			FIELD_PREP(FBNIC_RXB_PAUSE_DROP_CTRL_PAUSE_ENABLE,
				   FBNIC_PAUSE_EN_MASK);
	wr32(fbd, FBNIC_RXB_PAUSE_DROP_CTRL, rxb_pause_ctrl);
}

static int fbnic_pcs_get_link_event_asic(struct fbnic_dev *fbd)
{
	u32 pcs_intr_mask = rd32(fbd, FBNIC_SIG_PCS_INTR_STS);

	if (pcs_intr_mask & FBNIC_SIG_PCS_INTR_LINK_DOWN)
		return FBNIC_LINK_EVENT_DOWN;

	return (pcs_intr_mask & FBNIC_SIG_PCS_INTR_LINK_UP) ?
	       FBNIC_LINK_EVENT_UP : FBNIC_LINK_EVENT_NONE;
}

static u32 __fbnic_mac_cmd_config_asic(struct fbnic_dev *fbd,
				       bool tx_pause, bool rx_pause)
{
	/* Enable MAC Promiscuous mode and Tx padding */
	u32 command_config = FBNIC_MAC_COMMAND_CONFIG_TX_PAD_EN |
			     FBNIC_MAC_COMMAND_CONFIG_PROMISC_EN;
	struct fbnic_net *fbn = netdev_priv(fbd->netdev);

	/* Disable pause frames if not enabled */
	if (!tx_pause)
		command_config |= FBNIC_MAC_COMMAND_CONFIG_TX_PAUSE_DIS;
	if (!rx_pause)
		command_config |= FBNIC_MAC_COMMAND_CONFIG_RX_PAUSE_DIS;

	/* Disable fault handling if no FEC is requested */
	if (fbn->fec == FBNIC_FEC_OFF)
		command_config |= FBNIC_MAC_COMMAND_CONFIG_FLT_HDL_DIS;

	return command_config;
}

static bool fbnic_mac_get_pcs_link_status(struct fbnic_dev *fbd)
{
	struct fbnic_net *fbn = netdev_priv(fbd->netdev);
	u32 pcs_status, lane_mask = ~0;

	pcs_status = rd32(fbd, FBNIC_SIG_PCS_OUT0);
	if (!(pcs_status & FBNIC_SIG_PCS_OUT0_LINK))
		return false;

	/* Define the expected lane mask for the status bits we need to check */
	switch (fbn->aui) {
	case FBNIC_AUI_100GAUI2:
		lane_mask = 0xf;
		break;
	case FBNIC_AUI_50GAUI1:
		lane_mask = 3;
		break;
	case FBNIC_AUI_LAUI2:
		switch (fbn->fec) {
		case FBNIC_FEC_OFF:
			lane_mask = 0x63;
			break;
		case FBNIC_FEC_RS:
			lane_mask = 5;
			break;
		case FBNIC_FEC_BASER:
			lane_mask = 0xf;
			break;
		}
		break;
	case FBNIC_AUI_25GAUI:
		lane_mask = 1;
		break;
	}

	/* Use an XOR to remove the bits we expect to see set */
	switch (fbn->fec) {
	case FBNIC_FEC_OFF:
		lane_mask ^= FIELD_GET(FBNIC_SIG_PCS_OUT0_BLOCK_LOCK,
				       pcs_status);
		break;
	case FBNIC_FEC_RS:
		lane_mask ^= FIELD_GET(FBNIC_SIG_PCS_OUT0_AMPS_LOCK,
				       pcs_status);
		break;
	case FBNIC_FEC_BASER:
		lane_mask ^= FIELD_GET(FBNIC_SIG_PCS_OUT1_FCFEC_LOCK,
				       rd32(fbd, FBNIC_SIG_PCS_OUT1));
		break;
	}

	/* If all lanes cancelled then we have a lock on all lanes */
	return !lane_mask;
}

static bool fbnic_pcs_get_link_asic(struct fbnic_dev *fbd)
{
	bool link;

	/* Flush status bits to clear possible stale data,
	 * bits should reset themselves back to 1 if link is truly up
	 */
	wr32(fbd, FBNIC_SIG_PCS_OUT0, FBNIC_SIG_PCS_OUT0_LINK |
				      FBNIC_SIG_PCS_OUT0_BLOCK_LOCK |
				      FBNIC_SIG_PCS_OUT0_AMPS_LOCK);
	wr32(fbd, FBNIC_SIG_PCS_OUT1, FBNIC_SIG_PCS_OUT1_FCFEC_LOCK);
	wrfl(fbd);

	/* Clear interrupt state due to recent changes. */
	wr32(fbd, FBNIC_SIG_PCS_INTR_STS,
	     FBNIC_SIG_PCS_INTR_LINK_DOWN | FBNIC_SIG_PCS_INTR_LINK_UP);

	link = fbnic_mac_get_pcs_link_status(fbd);

	/* Enable interrupt to only capture changes in link state */
	wr32(fbd, FBNIC_SIG_PCS_INTR_MASK,
	     ~FBNIC_SIG_PCS_INTR_LINK_DOWN & ~FBNIC_SIG_PCS_INTR_LINK_UP);
	wr32(fbd, FBNIC_INTR_MASK_CLEAR(0), 1u << FBNIC_PCS_MSIX_ENTRY);

	return link;
}

void fbnic_mac_get_fw_settings(struct fbnic_dev *fbd, u8 *aui, u8 *fec)
{
	/* Retrieve default speed from FW */
	switch (fbd->fw_cap.link_speed) {
	case FBNIC_FW_LINK_MODE_25CR:
		*aui = FBNIC_AUI_25GAUI;
		break;
	case FBNIC_FW_LINK_MODE_50CR2:
		*aui = FBNIC_AUI_LAUI2;
		break;
	case FBNIC_FW_LINK_MODE_50CR:
		*aui = FBNIC_AUI_50GAUI1;
		*fec = FBNIC_FEC_RS;
		return;
	case FBNIC_FW_LINK_MODE_100CR2:
		*aui = FBNIC_AUI_100GAUI2;
		*fec = FBNIC_FEC_RS;
		return;
	default:
		*aui = FBNIC_AUI_UNKNOWN;
		return;
	}

	/* Update FEC first to reflect FW current mode */
	switch (fbd->fw_cap.link_fec) {
	case FBNIC_FW_LINK_FEC_NONE:
		*fec = FBNIC_FEC_OFF;
		break;
	case FBNIC_FW_LINK_FEC_RS:
	default:
		*fec = FBNIC_FEC_RS;
		break;
	case FBNIC_FW_LINK_FEC_BASER:
		*fec = FBNIC_FEC_BASER;
		break;
	}
}

static int fbnic_pcs_enable_asic(struct fbnic_dev *fbd)
{
	/* Mask and clear the PCS interrupt, will be enabled by link handler */
	wr32(fbd, FBNIC_SIG_PCS_INTR_MASK, ~0);
	wr32(fbd, FBNIC_SIG_PCS_INTR_STS, ~0);

	return 0;
}

static void fbnic_pcs_disable_asic(struct fbnic_dev *fbd)
{
	/* Mask and clear the PCS interrupt */
	wr32(fbd, FBNIC_SIG_PCS_INTR_MASK, ~0);
	wr32(fbd, FBNIC_SIG_PCS_INTR_STS, ~0);
}

static void fbnic_mac_link_down_asic(struct fbnic_dev *fbd)
{
	u32 cmd_cfg, mac_ctrl;

	cmd_cfg = __fbnic_mac_cmd_config_asic(fbd, false, false);
	mac_ctrl = rd32(fbd, FBNIC_SIG_MAC_IN0);

	mac_ctrl |= FBNIC_SIG_MAC_IN0_RESET_FF_TX_CLK |
		    FBNIC_SIG_MAC_IN0_RESET_TX_CLK |
		    FBNIC_SIG_MAC_IN0_RESET_FF_RX_CLK |
		    FBNIC_SIG_MAC_IN0_RESET_RX_CLK;

	wr32(fbd, FBNIC_SIG_MAC_IN0, mac_ctrl);
	wr32(fbd, FBNIC_MAC_COMMAND_CONFIG, cmd_cfg);
}

static void fbnic_mac_link_up_asic(struct fbnic_dev *fbd,
				   bool tx_pause, bool rx_pause)
{
	u32 cmd_cfg, mac_ctrl;

	fbnic_mac_tx_pause_config(fbd, tx_pause);

	cmd_cfg = __fbnic_mac_cmd_config_asic(fbd, tx_pause, rx_pause);
	mac_ctrl = rd32(fbd, FBNIC_SIG_MAC_IN0);

	mac_ctrl &= ~(FBNIC_SIG_MAC_IN0_RESET_FF_TX_CLK |
		      FBNIC_SIG_MAC_IN0_RESET_TX_CLK |
		      FBNIC_SIG_MAC_IN0_RESET_FF_RX_CLK |
		      FBNIC_SIG_MAC_IN0_RESET_RX_CLK);
	cmd_cfg |= FBNIC_MAC_COMMAND_CONFIG_RX_ENA |
		   FBNIC_MAC_COMMAND_CONFIG_TX_ENA;

	wr32(fbd, FBNIC_SIG_MAC_IN0, mac_ctrl);
	wr32(fbd, FBNIC_MAC_COMMAND_CONFIG, cmd_cfg);
}

static void
fbnic_pcs_rsfec_stat_rd32(struct fbnic_dev *fbd, u32 reg, bool reset,
			  struct fbnic_stat_counter *stat)
{
	u32 pcs_rsfec_stat;

	/* The PCS/RFSEC registers are only 16b wide each. So what we will
	 * have after the 64b read is 0x0000xxxx0000xxxx. To make it usable
	 * as a full stat we will shift the upper bits into the lower set of
	 * 0s and then mask off the math at 32b.
	 *
	 * Read ordering must be lower reg followed by upper reg.
	 */
	pcs_rsfec_stat = rd32(fbd, reg) & 0xffff;
	pcs_rsfec_stat |= rd32(fbd, reg + 1) << 16;

	/* RFSEC registers clear themselves upon being read so there is no
	 * need to store the old_reg_value.
	 */
	if (!reset)
		stat->value += pcs_rsfec_stat;
}

static void
fbnic_mac_get_fec_stats(struct fbnic_dev *fbd, bool reset,
			struct fbnic_fec_stats *s)
{
	fbnic_pcs_rsfec_stat_rd32(fbd, FBNIC_RSFEC_CCW_LO(0), reset,
				  &s->corrected_blocks);
	fbnic_pcs_rsfec_stat_rd32(fbd, FBNIC_RSFEC_NCCW_LO(0), reset,
				  &s->uncorrectable_blocks);
}

static void
fbnic_mac_get_pcs_stats(struct fbnic_dev *fbd, bool reset,
			struct fbnic_pcs_stats *s)
{
	int i;

	for (i = 0; i < FBNIC_PCS_MAX_LANES; i++)
		fbnic_pcs_rsfec_stat_rd32(fbd, FBNIC_PCS_SYMBLERR_LO(i), reset,
					  &s->SymbolErrorDuringCarrier.lanes[i]);
}

static void
fbnic_mac_get_eth_mac_stats(struct fbnic_dev *fbd, bool reset,
			    struct fbnic_eth_mac_stats *mac_stats)
{
	fbnic_mac_stat_rd64(fbd, reset, mac_stats->OctetsReceivedOK,
			    MAC_STAT_RX_BYTE_COUNT);
	fbnic_mac_stat_rd64(fbd, reset, mac_stats->AlignmentErrors,
			    MAC_STAT_RX_ALIGN_ERROR);
	fbnic_mac_stat_rd64(fbd, reset, mac_stats->FrameTooLongErrors,
			    MAC_STAT_RX_TOOLONG);
	fbnic_mac_stat_rd64(fbd, reset, mac_stats->FramesReceivedOK,
			    MAC_STAT_RX_RECEIVED_OK);
	fbnic_mac_stat_rd64(fbd, reset, mac_stats->FrameCheckSequenceErrors,
			    MAC_STAT_RX_PACKET_BAD_FCS);
	fbnic_mac_stat_rd64(fbd, reset,
			    mac_stats->FramesLostDueToIntMACRcvError,
			    MAC_STAT_RX_IFINERRORS);
	fbnic_mac_stat_rd64(fbd, reset, mac_stats->MulticastFramesReceivedOK,
			    MAC_STAT_RX_MULTICAST);
	fbnic_mac_stat_rd64(fbd, reset, mac_stats->BroadcastFramesReceivedOK,
			    MAC_STAT_RX_BROADCAST);
	fbnic_mac_stat_rd64(fbd, reset, mac_stats->OctetsTransmittedOK,
			    MAC_STAT_TX_BYTE_COUNT);
	fbnic_mac_stat_rd64(fbd, reset, mac_stats->FramesTransmittedOK,
			    MAC_STAT_TX_TRANSMITTED_OK);
	fbnic_mac_stat_rd64(fbd, reset,
			    mac_stats->FramesLostDueToIntMACXmitError,
			    MAC_STAT_TX_IFOUTERRORS);
	fbnic_mac_stat_rd64(fbd, reset, mac_stats->MulticastFramesXmittedOK,
			    MAC_STAT_TX_MULTICAST);
	fbnic_mac_stat_rd64(fbd, reset, mac_stats->BroadcastFramesXmittedOK,
			    MAC_STAT_TX_BROADCAST);
}

static void
fbnic_mac_get_pause_stats(struct fbnic_dev *fbd, bool reset,
			  struct fbnic_pause_stats *pause_stats)
{
	fbnic_mac_stat_rd64(fbd, reset, pause_stats->tx_pause_frames,
			    MAC_STAT_TX_XOFF_STB);
	fbnic_mac_stat_rd64(fbd, reset, pause_stats->rx_pause_frames,
			    MAC_STAT_RX_XOFF_STB);
}

static void
fbnic_mac_get_eth_ctrl_stats(struct fbnic_dev *fbd, bool reset,
			     struct fbnic_eth_ctrl_stats *ctrl_stats)
{
	fbnic_mac_stat_rd64(fbd, reset, ctrl_stats->MACControlFramesReceived,
			    MAC_STAT_RX_CONTROL_FRAMES);
	fbnic_mac_stat_rd64(fbd, reset, ctrl_stats->MACControlFramesTransmitted,
			    MAC_STAT_TX_CONTROL_FRAMES);
}

static void
fbnic_mac_get_rmon_stats(struct fbnic_dev *fbd, bool reset,
			 struct fbnic_rmon_stats *rmon_stats)
{
	fbnic_mac_stat_rd64(fbd, reset, rmon_stats->undersize_pkts,
			    MAC_STAT_RX_UNDERSIZE);
	fbnic_mac_stat_rd64(fbd, reset, rmon_stats->oversize_pkts,
			    MAC_STAT_RX_OVERSIZE);
	fbnic_mac_stat_rd64(fbd, reset, rmon_stats->fragments,
			    MAC_STAT_RX_FRAGMENT);
	fbnic_mac_stat_rd64(fbd, reset, rmon_stats->jabbers,
			    MAC_STAT_RX_JABBER);

	fbnic_mac_stat_rd64(fbd, reset, rmon_stats->hist[0],
			    MAC_STAT_RX_PACKET_64_BYTES);
	fbnic_mac_stat_rd64(fbd, reset, rmon_stats->hist[1],
			    MAC_STAT_RX_PACKET_65_127_BYTES);
	fbnic_mac_stat_rd64(fbd, reset, rmon_stats->hist[2],
			    MAC_STAT_RX_PACKET_128_255_BYTES);
	fbnic_mac_stat_rd64(fbd, reset, rmon_stats->hist[3],
			    MAC_STAT_RX_PACKET_256_511_BYTES);
	fbnic_mac_stat_rd64(fbd, reset, rmon_stats->hist[4],
			    MAC_STAT_RX_PACKET_512_1023_BYTES);
	fbnic_mac_stat_rd64(fbd, reset, rmon_stats->hist[5],
			    MAC_STAT_RX_PACKET_1024_1518_BYTES);
	fbnic_mac_stat_rd64(fbd, reset, rmon_stats->hist[6],
			    RPC_STAT_RX_PACKET_1519_2047_BYTES);
	fbnic_mac_stat_rd64(fbd, reset, rmon_stats->hist[7],
			    RPC_STAT_RX_PACKET_2048_4095_BYTES);
	fbnic_mac_stat_rd64(fbd, reset, rmon_stats->hist[8],
			    RPC_STAT_RX_PACKET_4096_8191_BYTES);
	fbnic_mac_stat_rd64(fbd, reset, rmon_stats->hist[9],
			    RPC_STAT_RX_PACKET_8192_9216_BYTES);
	fbnic_mac_stat_rd64(fbd, reset, rmon_stats->hist[10],
			    RPC_STAT_RX_PACKET_9217_MAX_BYTES);

	fbnic_mac_stat_rd64(fbd, reset, rmon_stats->hist_tx[0],
			    MAC_STAT_TX_PACKET_64_BYTES);
	fbnic_mac_stat_rd64(fbd, reset, rmon_stats->hist_tx[1],
			    MAC_STAT_TX_PACKET_65_127_BYTES);
	fbnic_mac_stat_rd64(fbd, reset, rmon_stats->hist_tx[2],
			    MAC_STAT_TX_PACKET_128_255_BYTES);
	fbnic_mac_stat_rd64(fbd, reset, rmon_stats->hist_tx[3],
			    MAC_STAT_TX_PACKET_256_511_BYTES);
	fbnic_mac_stat_rd64(fbd, reset, rmon_stats->hist_tx[4],
			    MAC_STAT_TX_PACKET_512_1023_BYTES);
	fbnic_mac_stat_rd64(fbd, reset, rmon_stats->hist_tx[5],
			    MAC_STAT_TX_PACKET_1024_1518_BYTES);
	fbnic_mac_stat_rd64(fbd, reset, rmon_stats->hist_tx[6],
			    TMI_STAT_TX_PACKET_1519_2047_BYTES);
	fbnic_mac_stat_rd64(fbd, reset, rmon_stats->hist_tx[7],
			    TMI_STAT_TX_PACKET_2048_4095_BYTES);
	fbnic_mac_stat_rd64(fbd, reset, rmon_stats->hist_tx[8],
			    TMI_STAT_TX_PACKET_4096_8191_BYTES);
	fbnic_mac_stat_rd64(fbd, reset, rmon_stats->hist_tx[9],
			    TMI_STAT_TX_PACKET_8192_9216_BYTES);
	fbnic_mac_stat_rd64(fbd, reset, rmon_stats->hist_tx[10],
			    TMI_STAT_TX_PACKET_9217_MAX_BYTES);
}

static int fbnic_mac_get_sensor_asic(struct fbnic_dev *fbd, int id,
				     long *val)
{
	struct fbnic_fw_completion *fw_cmpl;
	int err = 0, retries = 5;
	s32 *sensor;

	fw_cmpl = fbnic_fw_alloc_cmpl(FBNIC_TLV_MSG_ID_TSENE_READ_RESP);
	if (!fw_cmpl)
		return -ENOMEM;

	switch (id) {
	case FBNIC_SENSOR_TEMP:
		sensor = &fw_cmpl->u.tsene.millidegrees;
		break;
	case FBNIC_SENSOR_VOLTAGE:
		sensor = &fw_cmpl->u.tsene.millivolts;
		break;
	default:
		err = -EINVAL;
		goto exit_free;
	}

	err = fbnic_fw_xmit_tsene_read_msg(fbd, fw_cmpl);
	if (err) {
		dev_err(fbd->dev,
			"Failed to transmit TSENE read msg, err %d\n",
			err);
		goto exit_free;
	}

	/* Allow 2 seconds for reply, resend and try up to 5 times */
	while (!wait_for_completion_timeout(&fw_cmpl->done, 2 * HZ)) {
		retries--;

		if (retries == 0) {
			dev_err(fbd->dev,
				"Timed out waiting for TSENE read\n");
			err = -ETIMEDOUT;
			goto exit_cleanup;
		}

		err = fbnic_fw_xmit_tsene_read_msg(fbd, NULL);
		if (err) {
			dev_err(fbd->dev,
				"Failed to transmit TSENE read msg, err %d\n",
				err);
			goto exit_cleanup;
		}
	}

	/* Handle error returned by firmware */
	if (fw_cmpl->result) {
		err = fw_cmpl->result;
		dev_err(fbd->dev, "%s: Firmware returned error %d\n",
			__func__, err);
		goto exit_cleanup;
	}

	*val = *sensor;
exit_cleanup:
	fbnic_mbx_clear_cmpl(fbd, fw_cmpl);
exit_free:
	fbnic_fw_put_cmpl(fw_cmpl);

	return err;
}

static const struct fbnic_mac fbnic_mac_asic = {
	.init_regs = fbnic_mac_init_regs,
	.pcs_enable = fbnic_pcs_enable_asic,
	.pcs_disable = fbnic_pcs_disable_asic,
	.pcs_get_link = fbnic_pcs_get_link_asic,
	.pcs_get_link_event = fbnic_pcs_get_link_event_asic,
	.get_fec_stats = fbnic_mac_get_fec_stats,
	.get_pcs_stats = fbnic_mac_get_pcs_stats,
	.get_eth_mac_stats = fbnic_mac_get_eth_mac_stats,
	.get_pause_stats = fbnic_mac_get_pause_stats,
	.get_eth_ctrl_stats = fbnic_mac_get_eth_ctrl_stats,
	.get_rmon_stats = fbnic_mac_get_rmon_stats,
	.link_down = fbnic_mac_link_down_asic,
	.link_up = fbnic_mac_link_up_asic,
	.get_sensor = fbnic_mac_get_sensor_asic,
};

/**
 * fbnic_mac_init - Assign a MAC type and initialize the fbnic device
 * @fbd: Device pointer to device to initialize
 *
 * Return: zero on success, negative on failure
 *
 * Initialize the MAC function pointers and initializes the MAC of
 * the device.
 **/
int fbnic_mac_init(struct fbnic_dev *fbd)
{
	fbd->mac = &fbnic_mac_asic;

	fbd->mac->init_regs(fbd);

	return 0;
}
