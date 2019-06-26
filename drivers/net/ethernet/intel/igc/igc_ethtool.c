// SPDX-License-Identifier: GPL-2.0
/* Copyright (c)  2018 Intel Corporation */

/* ethtool support for igc */
#include <linux/pm_runtime.h>

#include "igc.h"

static const char igc_priv_flags_strings[][ETH_GSTRING_LEN] = {
#define IGC_PRIV_FLAGS_LEGACY_RX	BIT(0)
	"legacy-rx",
};

#define IGC_PRIV_FLAGS_STR_LEN ARRAY_SIZE(igc_priv_flags_strings)

static void igc_get_drvinfo(struct net_device *netdev,
			    struct ethtool_drvinfo *drvinfo)
{
	struct igc_adapter *adapter = netdev_priv(netdev);

	strlcpy(drvinfo->driver,  igc_driver_name, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, igc_driver_version, sizeof(drvinfo->version));

	/* add fw_version here */
	strlcpy(drvinfo->bus_info, pci_name(adapter->pdev),
		sizeof(drvinfo->bus_info));

	drvinfo->n_priv_flags = IGC_PRIV_FLAGS_STR_LEN;
}

static int igc_get_regs_len(struct net_device *netdev)
{
	return IGC_REGS_LEN * sizeof(u32);
}

static void igc_get_regs(struct net_device *netdev,
			 struct ethtool_regs *regs, void *p)
{
	struct igc_adapter *adapter = netdev_priv(netdev);
	struct igc_hw *hw = &adapter->hw;
	u32 *regs_buff = p;
	u8 i;

	memset(p, 0, IGC_REGS_LEN * sizeof(u32));

	regs->version = (1u << 24) | (hw->revision_id << 16) | hw->device_id;

	/* General Registers */
	regs_buff[0] = rd32(IGC_CTRL);
	regs_buff[1] = rd32(IGC_STATUS);
	regs_buff[2] = rd32(IGC_CTRL_EXT);
	regs_buff[3] = rd32(IGC_MDIC);
	regs_buff[4] = rd32(IGC_CONNSW);

	/* NVM Register */
	regs_buff[5] = rd32(IGC_EECD);

	/* Interrupt */
	/* Reading EICS for EICR because they read the
	 * same but EICS does not clear on read
	 */
	regs_buff[6] = rd32(IGC_EICS);
	regs_buff[7] = rd32(IGC_EICS);
	regs_buff[8] = rd32(IGC_EIMS);
	regs_buff[9] = rd32(IGC_EIMC);
	regs_buff[10] = rd32(IGC_EIAC);
	regs_buff[11] = rd32(IGC_EIAM);
	/* Reading ICS for ICR because they read the
	 * same but ICS does not clear on read
	 */
	regs_buff[12] = rd32(IGC_ICS);
	regs_buff[13] = rd32(IGC_ICS);
	regs_buff[14] = rd32(IGC_IMS);
	regs_buff[15] = rd32(IGC_IMC);
	regs_buff[16] = rd32(IGC_IAC);
	regs_buff[17] = rd32(IGC_IAM);

	/* Flow Control */
	regs_buff[18] = rd32(IGC_FCAL);
	regs_buff[19] = rd32(IGC_FCAH);
	regs_buff[20] = rd32(IGC_FCTTV);
	regs_buff[21] = rd32(IGC_FCRTL);
	regs_buff[22] = rd32(IGC_FCRTH);
	regs_buff[23] = rd32(IGC_FCRTV);

	/* Receive */
	regs_buff[24] = rd32(IGC_RCTL);
	regs_buff[25] = rd32(IGC_RXCSUM);
	regs_buff[26] = rd32(IGC_RLPML);
	regs_buff[27] = rd32(IGC_RFCTL);

	/* Transmit */
	regs_buff[28] = rd32(IGC_TCTL);
	regs_buff[29] = rd32(IGC_TIPG);

	/* Wake Up */

	/* MAC */

	/* Statistics */
	regs_buff[30] = adapter->stats.crcerrs;
	regs_buff[31] = adapter->stats.algnerrc;
	regs_buff[32] = adapter->stats.symerrs;
	regs_buff[33] = adapter->stats.rxerrc;
	regs_buff[34] = adapter->stats.mpc;
	regs_buff[35] = adapter->stats.scc;
	regs_buff[36] = adapter->stats.ecol;
	regs_buff[37] = adapter->stats.mcc;
	regs_buff[38] = adapter->stats.latecol;
	regs_buff[39] = adapter->stats.colc;
	regs_buff[40] = adapter->stats.dc;
	regs_buff[41] = adapter->stats.tncrs;
	regs_buff[42] = adapter->stats.sec;
	regs_buff[43] = adapter->stats.htdpmc;
	regs_buff[44] = adapter->stats.rlec;
	regs_buff[45] = adapter->stats.xonrxc;
	regs_buff[46] = adapter->stats.xontxc;
	regs_buff[47] = adapter->stats.xoffrxc;
	regs_buff[48] = adapter->stats.xofftxc;
	regs_buff[49] = adapter->stats.fcruc;
	regs_buff[50] = adapter->stats.prc64;
	regs_buff[51] = adapter->stats.prc127;
	regs_buff[52] = adapter->stats.prc255;
	regs_buff[53] = adapter->stats.prc511;
	regs_buff[54] = adapter->stats.prc1023;
	regs_buff[55] = adapter->stats.prc1522;
	regs_buff[56] = adapter->stats.gprc;
	regs_buff[57] = adapter->stats.bprc;
	regs_buff[58] = adapter->stats.mprc;
	regs_buff[59] = adapter->stats.gptc;
	regs_buff[60] = adapter->stats.gorc;
	regs_buff[61] = adapter->stats.gotc;
	regs_buff[62] = adapter->stats.rnbc;
	regs_buff[63] = adapter->stats.ruc;
	regs_buff[64] = adapter->stats.rfc;
	regs_buff[65] = adapter->stats.roc;
	regs_buff[66] = adapter->stats.rjc;
	regs_buff[67] = adapter->stats.mgprc;
	regs_buff[68] = adapter->stats.mgpdc;
	regs_buff[69] = adapter->stats.mgptc;
	regs_buff[70] = adapter->stats.tor;
	regs_buff[71] = adapter->stats.tot;
	regs_buff[72] = adapter->stats.tpr;
	regs_buff[73] = adapter->stats.tpt;
	regs_buff[74] = adapter->stats.ptc64;
	regs_buff[75] = adapter->stats.ptc127;
	regs_buff[76] = adapter->stats.ptc255;
	regs_buff[77] = adapter->stats.ptc511;
	regs_buff[78] = adapter->stats.ptc1023;
	regs_buff[79] = adapter->stats.ptc1522;
	regs_buff[80] = adapter->stats.mptc;
	regs_buff[81] = adapter->stats.bptc;
	regs_buff[82] = adapter->stats.tsctc;
	regs_buff[83] = adapter->stats.iac;
	regs_buff[84] = adapter->stats.rpthc;
	regs_buff[85] = adapter->stats.hgptc;
	regs_buff[86] = adapter->stats.hgorc;
	regs_buff[87] = adapter->stats.hgotc;
	regs_buff[88] = adapter->stats.lenerrs;
	regs_buff[89] = adapter->stats.scvpc;
	regs_buff[90] = adapter->stats.hrmpc;

	for (i = 0; i < 4; i++)
		regs_buff[91 + i] = rd32(IGC_SRRCTL(i));
	for (i = 0; i < 4; i++)
		regs_buff[95 + i] = rd32(IGC_PSRTYPE(i));
	for (i = 0; i < 4; i++)
		regs_buff[99 + i] = rd32(IGC_RDBAL(i));
	for (i = 0; i < 4; i++)
		regs_buff[103 + i] = rd32(IGC_RDBAH(i));
	for (i = 0; i < 4; i++)
		regs_buff[107 + i] = rd32(IGC_RDLEN(i));
	for (i = 0; i < 4; i++)
		regs_buff[111 + i] = rd32(IGC_RDH(i));
	for (i = 0; i < 4; i++)
		regs_buff[115 + i] = rd32(IGC_RDT(i));
	for (i = 0; i < 4; i++)
		regs_buff[119 + i] = rd32(IGC_RXDCTL(i));

	for (i = 0; i < 10; i++)
		regs_buff[123 + i] = rd32(IGC_EITR(i));
	for (i = 0; i < 16; i++)
		regs_buff[139 + i] = rd32(IGC_RAL(i));
	for (i = 0; i < 16; i++)
		regs_buff[145 + i] = rd32(IGC_RAH(i));

	for (i = 0; i < 4; i++)
		regs_buff[149 + i] = rd32(IGC_TDBAL(i));
	for (i = 0; i < 4; i++)
		regs_buff[152 + i] = rd32(IGC_TDBAH(i));
	for (i = 0; i < 4; i++)
		regs_buff[156 + i] = rd32(IGC_TDLEN(i));
	for (i = 0; i < 4; i++)
		regs_buff[160 + i] = rd32(IGC_TDH(i));
	for (i = 0; i < 4; i++)
		regs_buff[164 + i] = rd32(IGC_TDT(i));
	for (i = 0; i < 4; i++)
		regs_buff[168 + i] = rd32(IGC_TXDCTL(i));
}

static u32 igc_get_msglevel(struct net_device *netdev)
{
	struct igc_adapter *adapter = netdev_priv(netdev);

	return adapter->msg_enable;
}

static void igc_set_msglevel(struct net_device *netdev, u32 data)
{
	struct igc_adapter *adapter = netdev_priv(netdev);

	adapter->msg_enable = data;
}

static int igc_nway_reset(struct net_device *netdev)
{
	struct igc_adapter *adapter = netdev_priv(netdev);

	if (netif_running(netdev))
		igc_reinit_locked(adapter);
	return 0;
}

static u32 igc_get_link(struct net_device *netdev)
{
	struct igc_adapter *adapter = netdev_priv(netdev);
	struct igc_mac_info *mac = &adapter->hw.mac;

	/* If the link is not reported up to netdev, interrupts are disabled,
	 * and so the physical link state may have changed since we last
	 * looked. Set get_link_status to make sure that the true link
	 * state is interrogated, rather than pulling a cached and possibly
	 * stale link state from the driver.
	 */
	if (!netif_carrier_ok(netdev))
		mac->get_link_status = 1;

	return igc_has_link(adapter);
}

static int igc_get_eeprom_len(struct net_device *netdev)
{
	struct igc_adapter *adapter = netdev_priv(netdev);

	return adapter->hw.nvm.word_size * 2;
}

static int igc_get_eeprom(struct net_device *netdev,
			  struct ethtool_eeprom *eeprom, u8 *bytes)
{
	struct igc_adapter *adapter = netdev_priv(netdev);
	struct igc_hw *hw = &adapter->hw;
	int first_word, last_word;
	u16 *eeprom_buff;
	int ret_val = 0;
	u16 i;

	if (eeprom->len == 0)
		return -EINVAL;

	eeprom->magic = hw->vendor_id | (hw->device_id << 16);

	first_word = eeprom->offset >> 1;
	last_word = (eeprom->offset + eeprom->len - 1) >> 1;

	eeprom_buff = kmalloc_array(last_word - first_word + 1, sizeof(u16),
				    GFP_KERNEL);
	if (!eeprom_buff)
		return -ENOMEM;

	if (hw->nvm.type == igc_nvm_eeprom_spi) {
		ret_val = hw->nvm.ops.read(hw, first_word,
					   last_word - first_word + 1,
					   eeprom_buff);
	} else {
		for (i = 0; i < last_word - first_word + 1; i++) {
			ret_val = hw->nvm.ops.read(hw, first_word + i, 1,
						   &eeprom_buff[i]);
			if (ret_val)
				break;
		}
	}

	/* Device's eeprom is always little-endian, word addressable */
	for (i = 0; i < last_word - first_word + 1; i++)
		le16_to_cpus(&eeprom_buff[i]);

	memcpy(bytes, (u8 *)eeprom_buff + (eeprom->offset & 1),
	       eeprom->len);
	kfree(eeprom_buff);

	return ret_val;
}

static int igc_set_eeprom(struct net_device *netdev,
			  struct ethtool_eeprom *eeprom, u8 *bytes)
{
	struct igc_adapter *adapter = netdev_priv(netdev);
	struct igc_hw *hw = &adapter->hw;
	int max_len, first_word, last_word, ret_val = 0;
	u16 *eeprom_buff;
	void *ptr;
	u16 i;

	if (eeprom->len == 0)
		return -EOPNOTSUPP;

	if (hw->mac.type >= igc_i225 &&
	    !igc_get_flash_presence_i225(hw)) {
		return -EOPNOTSUPP;
	}

	if (eeprom->magic != (hw->vendor_id | (hw->device_id << 16)))
		return -EFAULT;

	max_len = hw->nvm.word_size * 2;

	first_word = eeprom->offset >> 1;
	last_word = (eeprom->offset + eeprom->len - 1) >> 1;
	eeprom_buff = kmalloc(max_len, GFP_KERNEL);
	if (!eeprom_buff)
		return -ENOMEM;

	ptr = (void *)eeprom_buff;

	if (eeprom->offset & 1) {
		/* need read/modify/write of first changed EEPROM word
		 * only the second byte of the word is being modified
		 */
		ret_val = hw->nvm.ops.read(hw, first_word, 1,
					    &eeprom_buff[0]);
		ptr++;
	}
	if (((eeprom->offset + eeprom->len) & 1) && ret_val == 0) {
		/* need read/modify/write of last changed EEPROM word
		 * only the first byte of the word is being modified
		 */
		ret_val = hw->nvm.ops.read(hw, last_word, 1,
				   &eeprom_buff[last_word - first_word]);
	}

	/* Device's eeprom is always little-endian, word addressable */
	for (i = 0; i < last_word - first_word + 1; i++)
		le16_to_cpus(&eeprom_buff[i]);

	memcpy(ptr, bytes, eeprom->len);

	for (i = 0; i < last_word - first_word + 1; i++)
		eeprom_buff[i] = cpu_to_le16(eeprom_buff[i]);

	ret_val = hw->nvm.ops.write(hw, first_word,
				    last_word - first_word + 1, eeprom_buff);

	/* Update the checksum if nvm write succeeded */
	if (ret_val == 0)
		hw->nvm.ops.update(hw);

	/* check if need: igc_set_fw_version(adapter); */
	kfree(eeprom_buff);
	return ret_val;
}

static void igc_get_ringparam(struct net_device *netdev,
			      struct ethtool_ringparam *ring)
{
	struct igc_adapter *adapter = netdev_priv(netdev);

	ring->rx_max_pending = IGC_MAX_RXD;
	ring->tx_max_pending = IGC_MAX_TXD;
	ring->rx_pending = adapter->rx_ring_count;
	ring->tx_pending = adapter->tx_ring_count;
}

static int igc_set_ringparam(struct net_device *netdev,
			     struct ethtool_ringparam *ring)
{
	struct igc_adapter *adapter = netdev_priv(netdev);
	struct igc_ring *temp_ring;
	u16 new_rx_count, new_tx_count;
	int i, err = 0;

	if (ring->rx_mini_pending || ring->rx_jumbo_pending)
		return -EINVAL;

	new_rx_count = min_t(u32, ring->rx_pending, IGC_MAX_RXD);
	new_rx_count = max_t(u16, new_rx_count, IGC_MIN_RXD);
	new_rx_count = ALIGN(new_rx_count, REQ_RX_DESCRIPTOR_MULTIPLE);

	new_tx_count = min_t(u32, ring->tx_pending, IGC_MAX_TXD);
	new_tx_count = max_t(u16, new_tx_count, IGC_MIN_TXD);
	new_tx_count = ALIGN(new_tx_count, REQ_TX_DESCRIPTOR_MULTIPLE);

	if (new_tx_count == adapter->tx_ring_count &&
	    new_rx_count == adapter->rx_ring_count) {
		/* nothing to do */
		return 0;
	}

	while (test_and_set_bit(__IGC_RESETTING, &adapter->state))
		usleep_range(1000, 2000);

	if (!netif_running(adapter->netdev)) {
		for (i = 0; i < adapter->num_tx_queues; i++)
			adapter->tx_ring[i]->count = new_tx_count;
		for (i = 0; i < adapter->num_rx_queues; i++)
			adapter->rx_ring[i]->count = new_rx_count;
		adapter->tx_ring_count = new_tx_count;
		adapter->rx_ring_count = new_rx_count;
		goto clear_reset;
	}

	if (adapter->num_tx_queues > adapter->num_rx_queues)
		temp_ring = vmalloc(array_size(sizeof(struct igc_ring),
					       adapter->num_tx_queues));
	else
		temp_ring = vmalloc(array_size(sizeof(struct igc_ring),
					       adapter->num_rx_queues));

	if (!temp_ring) {
		err = -ENOMEM;
		goto clear_reset;
	}

	igc_down(adapter);

	/* We can't just free everything and then setup again,
	 * because the ISRs in MSI-X mode get passed pointers
	 * to the Tx and Rx ring structs.
	 */
	if (new_tx_count != adapter->tx_ring_count) {
		for (i = 0; i < adapter->num_tx_queues; i++) {
			memcpy(&temp_ring[i], adapter->tx_ring[i],
			       sizeof(struct igc_ring));

			temp_ring[i].count = new_tx_count;
			err = igc_setup_tx_resources(&temp_ring[i]);
			if (err) {
				while (i) {
					i--;
					igc_free_tx_resources(&temp_ring[i]);
				}
				goto err_setup;
			}
		}

		for (i = 0; i < adapter->num_tx_queues; i++) {
			igc_free_tx_resources(adapter->tx_ring[i]);

			memcpy(adapter->tx_ring[i], &temp_ring[i],
			       sizeof(struct igc_ring));
		}

		adapter->tx_ring_count = new_tx_count;
	}

	if (new_rx_count != adapter->rx_ring_count) {
		for (i = 0; i < adapter->num_rx_queues; i++) {
			memcpy(&temp_ring[i], adapter->rx_ring[i],
			       sizeof(struct igc_ring));

			temp_ring[i].count = new_rx_count;
			err = igc_setup_rx_resources(&temp_ring[i]);
			if (err) {
				while (i) {
					i--;
					igc_free_rx_resources(&temp_ring[i]);
				}
				goto err_setup;
			}
		}

		for (i = 0; i < adapter->num_rx_queues; i++) {
			igc_free_rx_resources(adapter->rx_ring[i]);

			memcpy(adapter->rx_ring[i], &temp_ring[i],
			       sizeof(struct igc_ring));
		}

		adapter->rx_ring_count = new_rx_count;
	}
err_setup:
	igc_up(adapter);
	vfree(temp_ring);
clear_reset:
	clear_bit(__IGC_RESETTING, &adapter->state);
	return err;
}

static void igc_get_pauseparam(struct net_device *netdev,
			       struct ethtool_pauseparam *pause)
{
	struct igc_adapter *adapter = netdev_priv(netdev);
	struct igc_hw *hw = &adapter->hw;

	pause->autoneg =
		(adapter->fc_autoneg ? AUTONEG_ENABLE : AUTONEG_DISABLE);

	if (hw->fc.current_mode == igc_fc_rx_pause) {
		pause->rx_pause = 1;
	} else if (hw->fc.current_mode == igc_fc_tx_pause) {
		pause->tx_pause = 1;
	} else if (hw->fc.current_mode == igc_fc_full) {
		pause->rx_pause = 1;
		pause->tx_pause = 1;
	}
}

static int igc_set_pauseparam(struct net_device *netdev,
			      struct ethtool_pauseparam *pause)
{
	struct igc_adapter *adapter = netdev_priv(netdev);
	struct igc_hw *hw = &adapter->hw;
	int retval = 0;

	adapter->fc_autoneg = pause->autoneg;

	while (test_and_set_bit(__IGC_RESETTING, &adapter->state))
		usleep_range(1000, 2000);

	if (adapter->fc_autoneg == AUTONEG_ENABLE) {
		hw->fc.requested_mode = igc_fc_default;
		if (netif_running(adapter->netdev)) {
			igc_down(adapter);
			igc_up(adapter);
		} else {
			igc_reset(adapter);
		}
	} else {
		if (pause->rx_pause && pause->tx_pause)
			hw->fc.requested_mode = igc_fc_full;
		else if (pause->rx_pause && !pause->tx_pause)
			hw->fc.requested_mode = igc_fc_rx_pause;
		else if (!pause->rx_pause && pause->tx_pause)
			hw->fc.requested_mode = igc_fc_tx_pause;
		else if (!pause->rx_pause && !pause->tx_pause)
			hw->fc.requested_mode = igc_fc_none;

		hw->fc.current_mode = hw->fc.requested_mode;

		retval = ((hw->phy.media_type == igc_media_type_copper) ?
			  igc_force_mac_fc(hw) : igc_setup_link(hw));
	}

	clear_bit(__IGC_RESETTING, &adapter->state);
	return retval;
}

static int igc_get_coalesce(struct net_device *netdev,
			    struct ethtool_coalesce *ec)
{
	struct igc_adapter *adapter = netdev_priv(netdev);

	if (adapter->rx_itr_setting <= 3)
		ec->rx_coalesce_usecs = adapter->rx_itr_setting;
	else
		ec->rx_coalesce_usecs = adapter->rx_itr_setting >> 2;

	if (!(adapter->flags & IGC_FLAG_QUEUE_PAIRS)) {
		if (adapter->tx_itr_setting <= 3)
			ec->tx_coalesce_usecs = adapter->tx_itr_setting;
		else
			ec->tx_coalesce_usecs = adapter->tx_itr_setting >> 2;
	}

	return 0;
}

static int igc_set_coalesce(struct net_device *netdev,
			    struct ethtool_coalesce *ec)
{
	struct igc_adapter *adapter = netdev_priv(netdev);
	int i;

	if (ec->rx_max_coalesced_frames ||
	    ec->rx_coalesce_usecs_irq ||
	    ec->rx_max_coalesced_frames_irq ||
	    ec->tx_max_coalesced_frames ||
	    ec->tx_coalesce_usecs_irq ||
	    ec->stats_block_coalesce_usecs ||
	    ec->use_adaptive_rx_coalesce ||
	    ec->use_adaptive_tx_coalesce ||
	    ec->pkt_rate_low ||
	    ec->rx_coalesce_usecs_low ||
	    ec->rx_max_coalesced_frames_low ||
	    ec->tx_coalesce_usecs_low ||
	    ec->tx_max_coalesced_frames_low ||
	    ec->pkt_rate_high ||
	    ec->rx_coalesce_usecs_high ||
	    ec->rx_max_coalesced_frames_high ||
	    ec->tx_coalesce_usecs_high ||
	    ec->tx_max_coalesced_frames_high ||
	    ec->rate_sample_interval)
		return -ENOTSUPP;

	if (ec->rx_coalesce_usecs > IGC_MAX_ITR_USECS ||
	    (ec->rx_coalesce_usecs > 3 &&
	     ec->rx_coalesce_usecs < IGC_MIN_ITR_USECS) ||
	    ec->rx_coalesce_usecs == 2)
		return -EINVAL;

	if (ec->tx_coalesce_usecs > IGC_MAX_ITR_USECS ||
	    (ec->tx_coalesce_usecs > 3 &&
	     ec->tx_coalesce_usecs < IGC_MIN_ITR_USECS) ||
	    ec->tx_coalesce_usecs == 2)
		return -EINVAL;

	if ((adapter->flags & IGC_FLAG_QUEUE_PAIRS) && ec->tx_coalesce_usecs)
		return -EINVAL;

	/* If ITR is disabled, disable DMAC */
	if (ec->rx_coalesce_usecs == 0) {
		if (adapter->flags & IGC_FLAG_DMAC)
			adapter->flags &= ~IGC_FLAG_DMAC;
	}

	/* convert to rate of irq's per second */
	if (ec->rx_coalesce_usecs && ec->rx_coalesce_usecs <= 3)
		adapter->rx_itr_setting = ec->rx_coalesce_usecs;
	else
		adapter->rx_itr_setting = ec->rx_coalesce_usecs << 2;

	/* convert to rate of irq's per second */
	if (adapter->flags & IGC_FLAG_QUEUE_PAIRS)
		adapter->tx_itr_setting = adapter->rx_itr_setting;
	else if (ec->tx_coalesce_usecs && ec->tx_coalesce_usecs <= 3)
		adapter->tx_itr_setting = ec->tx_coalesce_usecs;
	else
		adapter->tx_itr_setting = ec->tx_coalesce_usecs << 2;

	for (i = 0; i < adapter->num_q_vectors; i++) {
		struct igc_q_vector *q_vector = adapter->q_vector[i];

		q_vector->tx.work_limit = adapter->tx_work_limit;
		if (q_vector->rx.ring)
			q_vector->itr_val = adapter->rx_itr_setting;
		else
			q_vector->itr_val = adapter->tx_itr_setting;
		if (q_vector->itr_val && q_vector->itr_val <= 3)
			q_vector->itr_val = IGC_START_ITR;
		q_vector->set_itr = 1;
	}

	return 0;
}

void igc_write_rss_indir_tbl(struct igc_adapter *adapter)
{
	struct igc_hw *hw = &adapter->hw;
	u32 reg = IGC_RETA(0);
	u32 shift = 0;
	int i = 0;

	while (i < IGC_RETA_SIZE) {
		u32 val = 0;
		int j;

		for (j = 3; j >= 0; j--) {
			val <<= 8;
			val |= adapter->rss_indir_tbl[i + j];
		}

		wr32(reg, val << shift);
		reg += 4;
		i += 4;
	}
}

static u32 igc_get_rxfh_indir_size(struct net_device *netdev)
{
	return IGC_RETA_SIZE;
}

static int igc_get_rxfh(struct net_device *netdev, u32 *indir, u8 *key,
			u8 *hfunc)
{
	struct igc_adapter *adapter = netdev_priv(netdev);
	int i;

	if (hfunc)
		*hfunc = ETH_RSS_HASH_TOP;
	if (!indir)
		return 0;
	for (i = 0; i < IGC_RETA_SIZE; i++)
		indir[i] = adapter->rss_indir_tbl[i];

	return 0;
}

static int igc_set_rxfh(struct net_device *netdev, const u32 *indir,
			const u8 *key, const u8 hfunc)
{
	struct igc_adapter *adapter = netdev_priv(netdev);
	u32 num_queues;
	int i;

	/* We do not allow change in unsupported parameters */
	if (key ||
	    (hfunc != ETH_RSS_HASH_NO_CHANGE && hfunc != ETH_RSS_HASH_TOP))
		return -EOPNOTSUPP;
	if (!indir)
		return 0;

	num_queues = adapter->rss_queues;

	/* Verify user input. */
	for (i = 0; i < IGC_RETA_SIZE; i++)
		if (indir[i] >= num_queues)
			return -EINVAL;

	for (i = 0; i < IGC_RETA_SIZE; i++)
		adapter->rss_indir_tbl[i] = indir[i];

	igc_write_rss_indir_tbl(adapter);

	return 0;
}

static unsigned int igc_max_channels(struct igc_adapter *adapter)
{
	return igc_get_max_rss_queues(adapter);
}

static void igc_get_channels(struct net_device *netdev,
			     struct ethtool_channels *ch)
{
	struct igc_adapter *adapter = netdev_priv(netdev);

	/* Report maximum channels */
	ch->max_combined = igc_max_channels(adapter);

	/* Report info for other vector */
	if (adapter->flags & IGC_FLAG_HAS_MSIX) {
		ch->max_other = NON_Q_VECTORS;
		ch->other_count = NON_Q_VECTORS;
	}

	ch->combined_count = adapter->rss_queues;
}

static int igc_set_channels(struct net_device *netdev,
			    struct ethtool_channels *ch)
{
	struct igc_adapter *adapter = netdev_priv(netdev);
	unsigned int count = ch->combined_count;
	unsigned int max_combined = 0;

	/* Verify they are not requesting separate vectors */
	if (!count || ch->rx_count || ch->tx_count)
		return -EINVAL;

	/* Verify other_count is valid and has not been changed */
	if (ch->other_count != NON_Q_VECTORS)
		return -EINVAL;

	/* Verify the number of channels doesn't exceed hw limits */
	max_combined = igc_max_channels(adapter);
	if (count > max_combined)
		return -EINVAL;

	if (count != adapter->rss_queues) {
		adapter->rss_queues = count;
		igc_set_flag_queue_pairs(adapter, max_combined);

		/* Hardware has to reinitialize queues and interrupts to
		 * match the new configuration.
		 */
		return igc_reinit_queues(adapter);
	}

	return 0;
}

static u32 igc_get_priv_flags(struct net_device *netdev)
{
	struct igc_adapter *adapter = netdev_priv(netdev);
	u32 priv_flags = 0;

	if (adapter->flags & IGC_FLAG_RX_LEGACY)
		priv_flags |= IGC_PRIV_FLAGS_LEGACY_RX;

	return priv_flags;
}

static int igc_set_priv_flags(struct net_device *netdev, u32 priv_flags)
{
	struct igc_adapter *adapter = netdev_priv(netdev);
	unsigned int flags = adapter->flags;

	flags &= ~IGC_FLAG_RX_LEGACY;
	if (priv_flags & IGC_PRIV_FLAGS_LEGACY_RX)
		flags |= IGC_FLAG_RX_LEGACY;

	if (flags != adapter->flags) {
		adapter->flags = flags;

		/* reset interface to repopulate queues */
		if (netif_running(netdev))
			igc_reinit_locked(adapter);
	}

	return 0;
}

static int igc_ethtool_begin(struct net_device *netdev)
{
	struct igc_adapter *adapter = netdev_priv(netdev);

	pm_runtime_get_sync(&adapter->pdev->dev);
	return 0;
}

static void igc_ethtool_complete(struct net_device *netdev)
{
	struct igc_adapter *adapter = netdev_priv(netdev);

	pm_runtime_put(&adapter->pdev->dev);
}

static int igc_get_link_ksettings(struct net_device *netdev,
				  struct ethtool_link_ksettings *cmd)
{
	struct igc_adapter *adapter = netdev_priv(netdev);
	struct igc_hw *hw = &adapter->hw;
	u32 status;
	u32 speed;

	ethtool_link_ksettings_zero_link_mode(cmd, supported);
	ethtool_link_ksettings_zero_link_mode(cmd, advertising);

	/* supported link modes */
	ethtool_link_ksettings_add_link_mode(cmd, supported, 10baseT_Half);
	ethtool_link_ksettings_add_link_mode(cmd, supported, 10baseT_Full);
	ethtool_link_ksettings_add_link_mode(cmd, supported, 100baseT_Half);
	ethtool_link_ksettings_add_link_mode(cmd, supported, 100baseT_Full);
	ethtool_link_ksettings_add_link_mode(cmd, supported, 1000baseT_Full);
	ethtool_link_ksettings_add_link_mode(cmd, supported, 2500baseT_Full);

	/* twisted pair */
	cmd->base.port = PORT_TP;
	cmd->base.phy_address = hw->phy.addr;

	/* advertising link modes */
	ethtool_link_ksettings_add_link_mode(cmd, advertising, 10baseT_Half);
	ethtool_link_ksettings_add_link_mode(cmd, advertising, 10baseT_Full);
	ethtool_link_ksettings_add_link_mode(cmd, advertising, 100baseT_Half);
	ethtool_link_ksettings_add_link_mode(cmd, advertising, 100baseT_Full);
	ethtool_link_ksettings_add_link_mode(cmd, advertising, 1000baseT_Full);
	ethtool_link_ksettings_add_link_mode(cmd, advertising, 2500baseT_Full);

	/* set autoneg settings */
	if (hw->mac.autoneg == 1) {
		ethtool_link_ksettings_add_link_mode(cmd, supported, Autoneg);
		ethtool_link_ksettings_add_link_mode(cmd, advertising,
						     Autoneg);
	}

	switch (hw->fc.requested_mode) {
	case igc_fc_full:
		ethtool_link_ksettings_add_link_mode(cmd, advertising, Pause);
		break;
	case igc_fc_rx_pause:
		ethtool_link_ksettings_add_link_mode(cmd, advertising, Pause);
		ethtool_link_ksettings_add_link_mode(cmd, advertising,
						     Asym_Pause);
		break;
	case igc_fc_tx_pause:
		ethtool_link_ksettings_add_link_mode(cmd, advertising,
						     Asym_Pause);
		break;
	default:
		ethtool_link_ksettings_add_link_mode(cmd, advertising, Pause);
		ethtool_link_ksettings_add_link_mode(cmd, advertising,
						     Asym_Pause);
	}

	status = rd32(IGC_STATUS);

	if (status & IGC_STATUS_LU) {
		if (status & IGC_STATUS_SPEED_1000) {
			/* For I225, STATUS will indicate 1G speed in both
			 * 1 Gbps and 2.5 Gbps link modes.
			 * An additional bit is used
			 * to differentiate between 1 Gbps and 2.5 Gbps.
			 */
			if (hw->mac.type == igc_i225 &&
			    (status & IGC_STATUS_SPEED_2500)) {
				speed = SPEED_2500;
				hw_dbg("2500 Mbs, ");
			} else {
				speed = SPEED_1000;
				hw_dbg("1000 Mbs, ");
			}
		} else if (status & IGC_STATUS_SPEED_100) {
			speed = SPEED_100;
			hw_dbg("100 Mbs, ");
		} else {
			speed = SPEED_10;
			hw_dbg("10 Mbs, ");
		}
		if ((status & IGC_STATUS_FD) ||
		    hw->phy.media_type != igc_media_type_copper)
			cmd->base.duplex = DUPLEX_FULL;
		else
			cmd->base.duplex = DUPLEX_HALF;
	} else {
		speed = SPEED_UNKNOWN;
		cmd->base.duplex = DUPLEX_UNKNOWN;
	}
	cmd->base.speed = speed;
	if (hw->mac.autoneg)
		cmd->base.autoneg = AUTONEG_ENABLE;
	else
		cmd->base.autoneg = AUTONEG_DISABLE;

	/* MDI-X => 2; MDI =>1; Invalid =>0 */
	if (hw->phy.media_type == igc_media_type_copper)
		cmd->base.eth_tp_mdix = hw->phy.is_mdix ? ETH_TP_MDI_X :
						      ETH_TP_MDI;
	else
		cmd->base.eth_tp_mdix = ETH_TP_MDI_INVALID;

	if (hw->phy.mdix == AUTO_ALL_MODES)
		cmd->base.eth_tp_mdix_ctrl = ETH_TP_MDI_AUTO;
	else
		cmd->base.eth_tp_mdix_ctrl = hw->phy.mdix;

	return 0;
}

static int igc_set_link_ksettings(struct net_device *netdev,
				  const struct ethtool_link_ksettings *cmd)
{
	struct igc_adapter *adapter = netdev_priv(netdev);
	struct igc_hw *hw = &adapter->hw;
	u32 advertising;

	/* When adapter in resetting mode, autoneg/speed/duplex
	 * cannot be changed
	 */
	if (igc_check_reset_block(hw)) {
		dev_err(&adapter->pdev->dev,
			"Cannot change link characteristics when reset is active.\n");
		return -EINVAL;
	}

	/* MDI setting is only allowed when autoneg enabled because
	 * some hardware doesn't allow MDI setting when speed or
	 * duplex is forced.
	 */
	if (cmd->base.eth_tp_mdix_ctrl) {
		if (cmd->base.eth_tp_mdix_ctrl != ETH_TP_MDI_AUTO &&
		    cmd->base.autoneg != AUTONEG_ENABLE) {
			dev_err(&adapter->pdev->dev, "forcing MDI/MDI-X state is not supported when link speed and/or duplex are forced\n");
			return -EINVAL;
		}
	}

	while (test_and_set_bit(__IGC_RESETTING, &adapter->state))
		usleep_range(1000, 2000);

	ethtool_convert_link_mode_to_legacy_u32(&advertising,
						cmd->link_modes.advertising);

	if (cmd->base.autoneg == AUTONEG_ENABLE) {
		hw->mac.autoneg = 1;
		hw->phy.autoneg_advertised = advertising;
		if (adapter->fc_autoneg)
			hw->fc.requested_mode = igc_fc_default;
	} else {
		/* calling this overrides forced MDI setting */
		dev_info(&adapter->pdev->dev,
			 "Force mode currently not supported\n");
	}

	/* MDI-X => 2; MDI => 1; Auto => 3 */
	if (cmd->base.eth_tp_mdix_ctrl) {
		/* fix up the value for auto (3 => 0) as zero is mapped
		 * internally to auto
		 */
		if (cmd->base.eth_tp_mdix_ctrl == ETH_TP_MDI_AUTO)
			hw->phy.mdix = AUTO_ALL_MODES;
		else
			hw->phy.mdix = cmd->base.eth_tp_mdix_ctrl;
	}

	/* reset the link */
	if (netif_running(adapter->netdev)) {
		igc_down(adapter);
		igc_up(adapter);
	} else {
		igc_reset(adapter);
	}

	clear_bit(__IGC_RESETTING, &adapter->state);

	return 0;
}

static const struct ethtool_ops igc_ethtool_ops = {
	.get_drvinfo		= igc_get_drvinfo,
	.get_regs_len		= igc_get_regs_len,
	.get_regs		= igc_get_regs,
	.get_msglevel		= igc_get_msglevel,
	.set_msglevel		= igc_set_msglevel,
	.nway_reset		= igc_nway_reset,
	.get_link		= igc_get_link,
	.get_eeprom_len		= igc_get_eeprom_len,
	.get_eeprom		= igc_get_eeprom,
	.set_eeprom		= igc_set_eeprom,
	.get_ringparam		= igc_get_ringparam,
	.set_ringparam		= igc_set_ringparam,
	.get_pauseparam		= igc_get_pauseparam,
	.set_pauseparam		= igc_set_pauseparam,
	.get_coalesce		= igc_get_coalesce,
	.set_coalesce		= igc_set_coalesce,
	.get_rxfh_indir_size	= igc_get_rxfh_indir_size,
	.get_rxfh		= igc_get_rxfh,
	.set_rxfh		= igc_set_rxfh,
	.get_channels		= igc_get_channels,
	.set_channels		= igc_set_channels,
	.get_priv_flags		= igc_get_priv_flags,
	.set_priv_flags		= igc_set_priv_flags,
	.begin			= igc_ethtool_begin,
	.complete		= igc_ethtool_complete,
	.get_link_ksettings	= igc_get_link_ksettings,
	.set_link_ksettings	= igc_set_link_ksettings,
};

void igc_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &igc_ethtool_ops;
}
