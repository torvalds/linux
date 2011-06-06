/*******************************************************************************

  Intel 10 Gigabit PCI Express Linux driver
  Copyright(c) 1999 - 2011 Intel Corporation.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Contact Information:
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

#include "ixgbe.h"
#include "ixgbe_type.h"
#include "ixgbe_dcb.h"
#include "ixgbe_dcb_82599.h"

/**
 * ixgbe_dcb_config_packet_buffers_82599 - Configure DCB packet buffers
 * @hw: pointer to hardware structure
 * @rx_pba: method to distribute packet buffer
 *
 * Configure packet buffers for DCB mode.
 */
static s32 ixgbe_dcb_config_packet_buffers_82599(struct ixgbe_hw *hw, u8 rx_pba)
{
	int num_tcs = IXGBE_MAX_PACKET_BUFFERS;
	u32 rx_pb_size = hw->mac.rx_pb_size << IXGBE_RXPBSIZE_SHIFT;
	u32 rxpktsize;
	u32 txpktsize;
	u32 txpbthresh;
	u8  i = 0;

	/*
	 * This really means configure the first half of the TCs
	 * (Traffic Classes) to use 5/8 of the Rx packet buffer
	 * space.  To determine the size of the buffer for each TC,
	 * we are multiplying the average size by 5/4 and applying
	 * it to half of the traffic classes.
	 */
	if (rx_pba == pba_80_48) {
		rxpktsize = (rx_pb_size * 5) / (num_tcs * 4);
		rx_pb_size -= rxpktsize * (num_tcs / 2);
		for (; i < (num_tcs / 2); i++)
			IXGBE_WRITE_REG(hw, IXGBE_RXPBSIZE(i), rxpktsize);
	}

	/* Divide the remaining Rx packet buffer evenly among the TCs */
	rxpktsize = rx_pb_size / (num_tcs - i);
	for (; i < num_tcs; i++)
		IXGBE_WRITE_REG(hw, IXGBE_RXPBSIZE(i), rxpktsize);

	/*
	 * Setup Tx packet buffer and threshold equally for all TCs
	 * TXPBTHRESH register is set in K so divide by 1024 and subtract
	 * 10 since the largest packet we support is just over 9K.
	 */
	txpktsize = IXGBE_TXPBSIZE_MAX / num_tcs;
	txpbthresh = (txpktsize / 1024) - IXGBE_TXPKT_SIZE_MAX;
	for (i = 0; i < num_tcs; i++) {
		IXGBE_WRITE_REG(hw, IXGBE_TXPBSIZE(i), txpktsize);
		IXGBE_WRITE_REG(hw, IXGBE_TXPBTHRESH(i), txpbthresh);
	}

	/* Clear unused TCs, if any, to zero buffer size*/
	for (; i < MAX_TRAFFIC_CLASS; i++) {
		IXGBE_WRITE_REG(hw, IXGBE_RXPBSIZE(i), 0);
		IXGBE_WRITE_REG(hw, IXGBE_TXPBSIZE(i), 0);
		IXGBE_WRITE_REG(hw, IXGBE_TXPBTHRESH(i), 0);
	}

	return 0;
}

/**
 * ixgbe_dcb_config_rx_arbiter_82599 - Config Rx Data arbiter
 * @hw: pointer to hardware structure
 * @refill: refill credits index by traffic class
 * @max: max credits index by traffic class
 * @bwg_id: bandwidth grouping indexed by traffic class
 * @prio_type: priority type indexed by traffic class
 *
 * Configure Rx Packet Arbiter and credits for each traffic class.
 */
s32 ixgbe_dcb_config_rx_arbiter_82599(struct ixgbe_hw *hw,
				      u16 *refill,
				      u16 *max,
				      u8 *bwg_id,
				      u8 *prio_type,
				      u8 *prio_tc)
{
	u32    reg           = 0;
	u32    credit_refill = 0;
	u32    credit_max    = 0;
	u8     i             = 0;

	/*
	 * Disable the arbiter before changing parameters
	 * (always enable recycle mode; WSP)
	 */
	reg = IXGBE_RTRPCS_RRM | IXGBE_RTRPCS_RAC | IXGBE_RTRPCS_ARBDIS;
	IXGBE_WRITE_REG(hw, IXGBE_RTRPCS, reg);

	/* Map all traffic classes to their UP, 1 to 1 */
	reg = 0;
	for (i = 0; i < MAX_TRAFFIC_CLASS; i++)
		reg |= (prio_tc[i] << (i * IXGBE_RTRUP2TC_UP_SHIFT));
	IXGBE_WRITE_REG(hw, IXGBE_RTRUP2TC, reg);

	/* Configure traffic class credits and priority */
	for (i = 0; i < MAX_TRAFFIC_CLASS; i++) {
		credit_refill = refill[i];
		credit_max    = max[i];
		reg = credit_refill | (credit_max << IXGBE_RTRPT4C_MCL_SHIFT);

		reg |= (u32)(bwg_id[i]) << IXGBE_RTRPT4C_BWG_SHIFT;

		if (prio_type[i] == prio_link)
			reg |= IXGBE_RTRPT4C_LSP;

		IXGBE_WRITE_REG(hw, IXGBE_RTRPT4C(i), reg);
	}

	/*
	 * Configure Rx packet plane (recycle mode; WSP) and
	 * enable arbiter
	 */
	reg = IXGBE_RTRPCS_RRM | IXGBE_RTRPCS_RAC;
	IXGBE_WRITE_REG(hw, IXGBE_RTRPCS, reg);

	return 0;
}

/**
 * ixgbe_dcb_config_tx_desc_arbiter_82599 - Config Tx Desc. arbiter
 * @hw: pointer to hardware structure
 * @refill: refill credits index by traffic class
 * @max: max credits index by traffic class
 * @bwg_id: bandwidth grouping indexed by traffic class
 * @prio_type: priority type indexed by traffic class
 *
 * Configure Tx Descriptor Arbiter and credits for each traffic class.
 */
s32 ixgbe_dcb_config_tx_desc_arbiter_82599(struct ixgbe_hw *hw,
					   u16 *refill,
					   u16 *max,
					   u8 *bwg_id,
					   u8 *prio_type)
{
	u32    reg, max_credits;
	u8     i;

	/* Clear the per-Tx queue credits; we use per-TC instead */
	for (i = 0; i < 128; i++) {
		IXGBE_WRITE_REG(hw, IXGBE_RTTDQSEL, i);
		IXGBE_WRITE_REG(hw, IXGBE_RTTDT1C, 0);
	}

	/* Configure traffic class credits and priority */
	for (i = 0; i < MAX_TRAFFIC_CLASS; i++) {
		max_credits = max[i];
		reg = max_credits << IXGBE_RTTDT2C_MCL_SHIFT;
		reg |= refill[i];
		reg |= (u32)(bwg_id[i]) << IXGBE_RTTDT2C_BWG_SHIFT;

		if (prio_type[i] == prio_group)
			reg |= IXGBE_RTTDT2C_GSP;

		if (prio_type[i] == prio_link)
			reg |= IXGBE_RTTDT2C_LSP;

		IXGBE_WRITE_REG(hw, IXGBE_RTTDT2C(i), reg);
	}

	/*
	 * Configure Tx descriptor plane (recycle mode; WSP) and
	 * enable arbiter
	 */
	reg = IXGBE_RTTDCS_TDPAC | IXGBE_RTTDCS_TDRM;
	IXGBE_WRITE_REG(hw, IXGBE_RTTDCS, reg);

	return 0;
}

/**
 * ixgbe_dcb_config_tx_data_arbiter_82599 - Config Tx Data arbiter
 * @hw: pointer to hardware structure
 * @refill: refill credits index by traffic class
 * @max: max credits index by traffic class
 * @bwg_id: bandwidth grouping indexed by traffic class
 * @prio_type: priority type indexed by traffic class
 *
 * Configure Tx Packet Arbiter and credits for each traffic class.
 */
s32 ixgbe_dcb_config_tx_data_arbiter_82599(struct ixgbe_hw *hw,
					   u16 *refill,
					   u16 *max,
					   u8 *bwg_id,
					   u8 *prio_type,
					   u8 *prio_tc)
{
	u32 reg;
	u8 i;

	/*
	 * Disable the arbiter before changing parameters
	 * (always enable recycle mode; SP; arb delay)
	 */
	reg = IXGBE_RTTPCS_TPPAC | IXGBE_RTTPCS_TPRM |
	      (IXGBE_RTTPCS_ARBD_DCB << IXGBE_RTTPCS_ARBD_SHIFT) |
	      IXGBE_RTTPCS_ARBDIS;
	IXGBE_WRITE_REG(hw, IXGBE_RTTPCS, reg);

	/* Map all traffic classes to their UP, 1 to 1 */
	reg = 0;
	for (i = 0; i < MAX_TRAFFIC_CLASS; i++)
		reg |= (prio_tc[i] << (i * IXGBE_RTTUP2TC_UP_SHIFT));
	IXGBE_WRITE_REG(hw, IXGBE_RTTUP2TC, reg);

	/* Configure traffic class credits and priority */
	for (i = 0; i < MAX_TRAFFIC_CLASS; i++) {
		reg = refill[i];
		reg |= (u32)(max[i]) << IXGBE_RTTPT2C_MCL_SHIFT;
		reg |= (u32)(bwg_id[i]) << IXGBE_RTTPT2C_BWG_SHIFT;

		if (prio_type[i] == prio_group)
			reg |= IXGBE_RTTPT2C_GSP;

		if (prio_type[i] == prio_link)
			reg |= IXGBE_RTTPT2C_LSP;

		IXGBE_WRITE_REG(hw, IXGBE_RTTPT2C(i), reg);
	}

	/*
	 * Configure Tx packet plane (recycle mode; SP; arb delay) and
	 * enable arbiter
	 */
	reg = IXGBE_RTTPCS_TPPAC | IXGBE_RTTPCS_TPRM |
	      (IXGBE_RTTPCS_ARBD_DCB << IXGBE_RTTPCS_ARBD_SHIFT);
	IXGBE_WRITE_REG(hw, IXGBE_RTTPCS, reg);

	return 0;
}

/**
 * ixgbe_dcb_config_pfc_82599 - Configure priority flow control
 * @hw: pointer to hardware structure
 * @pfc_en: enabled pfc bitmask
 *
 * Configure Priority Flow Control (PFC) for each traffic class.
 */
s32 ixgbe_dcb_config_pfc_82599(struct ixgbe_hw *hw, u8 pfc_en)
{
	u32 i, reg, rx_pba_size;

	/* Configure PFC Tx thresholds per TC */
	for (i = 0; i < MAX_TRAFFIC_CLASS; i++) {
		int enabled = pfc_en & (1 << i);
		rx_pba_size = IXGBE_READ_REG(hw, IXGBE_RXPBSIZE(i));
		rx_pba_size >>= IXGBE_RXPBSIZE_SHIFT;

		reg = (rx_pba_size - hw->fc.low_water) << 10;

		if (enabled)
			reg |= IXGBE_FCRTL_XONE;
		IXGBE_WRITE_REG(hw, IXGBE_FCRTL_82599(i), reg);

		reg = (rx_pba_size - hw->fc.high_water) << 10;
		if (enabled)
			reg |= IXGBE_FCRTH_FCEN;
		IXGBE_WRITE_REG(hw, IXGBE_FCRTH_82599(i), reg);
	}

	if (pfc_en) {
		/* Configure pause time (2 TCs per register) */
		reg = hw->fc.pause_time | (hw->fc.pause_time << 16);
		for (i = 0; i < (MAX_TRAFFIC_CLASS / 2); i++)
			IXGBE_WRITE_REG(hw, IXGBE_FCTTV(i), reg);

		/* Configure flow control refresh threshold value */
		IXGBE_WRITE_REG(hw, IXGBE_FCRTV, hw->fc.pause_time / 2);


		reg = IXGBE_FCCFG_TFCE_PRIORITY;
		IXGBE_WRITE_REG(hw, IXGBE_FCCFG, reg);
		/*
		 * Enable Receive PFC
		 * 82599 will always honor XOFF frames we receive when
		 * we are in PFC mode however X540 only honors enabled
		 * traffic classes.
		 */
		reg = IXGBE_READ_REG(hw, IXGBE_MFLCN);
		reg &= ~IXGBE_MFLCN_RFCE;
		reg |= IXGBE_MFLCN_RPFCE | IXGBE_MFLCN_DPF;

		if (hw->mac.type == ixgbe_mac_X540)
			reg |= pfc_en << IXGBE_MFLCN_RPFCE_SHIFT;

		IXGBE_WRITE_REG(hw, IXGBE_MFLCN, reg);

	} else {
		for (i = 0; i < MAX_TRAFFIC_CLASS; i++)
			hw->mac.ops.fc_enable(hw, i);
	}

	return 0;
}

/**
 * ixgbe_dcb_config_tc_stats_82599 - Config traffic class statistics
 * @hw: pointer to hardware structure
 *
 * Configure queue statistics registers, all queues belonging to same traffic
 * class uses a single set of queue statistics counters.
 */
static s32 ixgbe_dcb_config_tc_stats_82599(struct ixgbe_hw *hw)
{
	u32 reg = 0;
	u8  i   = 0;

	/*
	 * Receive Queues stats setting
	 * 32 RQSMR registers, each configuring 4 queues.
	 * Set all 16 queues of each TC to the same stat
	 * with TC 'n' going to stat 'n'.
	 */
	for (i = 0; i < 32; i++) {
		reg = 0x01010101 * (i / 4);
		IXGBE_WRITE_REG(hw, IXGBE_RQSMR(i), reg);
	}
	/*
	 * Transmit Queues stats setting
	 * 32 TQSM registers, each controlling 4 queues.
	 * Set all queues of each TC to the same stat
	 * with TC 'n' going to stat 'n'.
	 * Tx queues are allocated non-uniformly to TCs:
	 * 32, 32, 16, 16, 8, 8, 8, 8.
	 */
	for (i = 0; i < 32; i++) {
		if (i < 8)
			reg = 0x00000000;
		else if (i < 16)
			reg = 0x01010101;
		else if (i < 20)
			reg = 0x02020202;
		else if (i < 24)
			reg = 0x03030303;
		else if (i < 26)
			reg = 0x04040404;
		else if (i < 28)
			reg = 0x05050505;
		else if (i < 30)
			reg = 0x06060606;
		else
			reg = 0x07070707;
		IXGBE_WRITE_REG(hw, IXGBE_TQSM(i), reg);
	}

	return 0;
}

/**
 * ixgbe_dcb_config_82599 - Configure general DCB parameters
 * @hw: pointer to hardware structure
 *
 * Configure general DCB parameters.
 */
static s32 ixgbe_dcb_config_82599(struct ixgbe_hw *hw)
{
	u32 reg;
	u32 q;

	/* Disable the Tx desc arbiter so that MTQC can be changed */
	reg = IXGBE_READ_REG(hw, IXGBE_RTTDCS);
	reg |= IXGBE_RTTDCS_ARBDIS;
	IXGBE_WRITE_REG(hw, IXGBE_RTTDCS, reg);

	/* Enable DCB for Rx with 8 TCs */
	reg = IXGBE_READ_REG(hw, IXGBE_MRQC);
	switch (reg & IXGBE_MRQC_MRQE_MASK) {
	case 0:
	case IXGBE_MRQC_RT4TCEN:
		/* RSS disabled cases */
		reg = (reg & ~IXGBE_MRQC_MRQE_MASK) | IXGBE_MRQC_RT8TCEN;
		break;
	case IXGBE_MRQC_RSSEN:
	case IXGBE_MRQC_RTRSS4TCEN:
		/* RSS enabled cases */
		reg = (reg & ~IXGBE_MRQC_MRQE_MASK) | IXGBE_MRQC_RTRSS8TCEN;
		break;
	default:
		/* Unsupported value, assume stale data, overwrite no RSS */
		reg = (reg & ~IXGBE_MRQC_MRQE_MASK) | IXGBE_MRQC_RT8TCEN;
	}
	IXGBE_WRITE_REG(hw, IXGBE_MRQC, reg);

	/* Enable DCB for Tx with 8 TCs */
	reg = IXGBE_MTQC_RT_ENA | IXGBE_MTQC_8TC_8TQ;
	IXGBE_WRITE_REG(hw, IXGBE_MTQC, reg);

	/* Disable drop for all queues */
	for (q = 0; q < 128; q++)
		IXGBE_WRITE_REG(hw, IXGBE_QDE, q << IXGBE_QDE_IDX_SHIFT);

	/* Enable the Tx desc arbiter */
	reg = IXGBE_READ_REG(hw, IXGBE_RTTDCS);
	reg &= ~IXGBE_RTTDCS_ARBDIS;
	IXGBE_WRITE_REG(hw, IXGBE_RTTDCS, reg);

	/* Enable Security TX Buffer IFG for DCB */
	reg = IXGBE_READ_REG(hw, IXGBE_SECTXMINIFG);
	reg |= IXGBE_SECTX_DCB;
	IXGBE_WRITE_REG(hw, IXGBE_SECTXMINIFG, reg);

	return 0;
}

/**
 * ixgbe_dcb_hw_config_82599 - Configure and enable DCB
 * @hw: pointer to hardware structure
 * @rx_pba: method to distribute packet buffer
 * @refill: refill credits index by traffic class
 * @max: max credits index by traffic class
 * @bwg_id: bandwidth grouping indexed by traffic class
 * @prio_type: priority type indexed by traffic class
 * @pfc_en: enabled pfc bitmask
 *
 * Configure dcb settings and enable dcb mode.
 */
s32 ixgbe_dcb_hw_config_82599(struct ixgbe_hw *hw,
			      u8 rx_pba, u8 pfc_en, u16 *refill,
			      u16 *max, u8 *bwg_id, u8 *prio_type, u8 *prio_tc)
{
	ixgbe_dcb_config_packet_buffers_82599(hw, rx_pba);
	ixgbe_dcb_config_82599(hw);
	ixgbe_dcb_config_rx_arbiter_82599(hw, refill, max, bwg_id,
					  prio_type, prio_tc);
	ixgbe_dcb_config_tx_desc_arbiter_82599(hw, refill, max,
					       bwg_id, prio_type);
	ixgbe_dcb_config_tx_data_arbiter_82599(hw, refill, max,
					       bwg_id, prio_type, prio_tc);
	ixgbe_dcb_config_pfc_82599(hw, pfc_en);
	ixgbe_dcb_config_tc_stats_82599(hw);

	return 0;
}

