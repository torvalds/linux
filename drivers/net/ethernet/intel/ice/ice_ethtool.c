// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018, Intel Corporation. */

/* ethtool support for ice */

#include "ice.h"
#include "ice_lib.h"
#include "ice_dcb_lib.h"

struct ice_stats {
	char stat_string[ETH_GSTRING_LEN];
	int sizeof_stat;
	int stat_offset;
};

#define ICE_STAT(_type, _name, _stat) { \
	.stat_string = _name, \
	.sizeof_stat = FIELD_SIZEOF(_type, _stat), \
	.stat_offset = offsetof(_type, _stat) \
}

#define ICE_VSI_STAT(_name, _stat) \
		ICE_STAT(struct ice_vsi, _name, _stat)
#define ICE_PF_STAT(_name, _stat) \
		ICE_STAT(struct ice_pf, _name, _stat)

static int ice_q_stats_len(struct net_device *netdev)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);

	return ((np->vsi->alloc_txq + np->vsi->alloc_rxq) *
		(sizeof(struct ice_q_stats) / sizeof(u64)));
}

#define ICE_PF_STATS_LEN	ARRAY_SIZE(ice_gstrings_pf_stats)
#define ICE_VSI_STATS_LEN	ARRAY_SIZE(ice_gstrings_vsi_stats)

#define ICE_PFC_STATS_LEN ( \
		(FIELD_SIZEOF(struct ice_pf, stats.priority_xoff_rx) + \
		 FIELD_SIZEOF(struct ice_pf, stats.priority_xon_rx) + \
		 FIELD_SIZEOF(struct ice_pf, stats.priority_xoff_tx) + \
		 FIELD_SIZEOF(struct ice_pf, stats.priority_xon_tx)) \
		 / sizeof(u64))
#define ICE_ALL_STATS_LEN(n)	(ICE_PF_STATS_LEN + ICE_PFC_STATS_LEN + \
				 ICE_VSI_STATS_LEN + ice_q_stats_len(n))

static const struct ice_stats ice_gstrings_vsi_stats[] = {
	ICE_VSI_STAT("rx_unicast", eth_stats.rx_unicast),
	ICE_VSI_STAT("tx_unicast", eth_stats.tx_unicast),
	ICE_VSI_STAT("rx_multicast", eth_stats.rx_multicast),
	ICE_VSI_STAT("tx_multicast", eth_stats.tx_multicast),
	ICE_VSI_STAT("rx_broadcast", eth_stats.rx_broadcast),
	ICE_VSI_STAT("tx_broadcast", eth_stats.tx_broadcast),
	ICE_VSI_STAT("rx_bytes", eth_stats.rx_bytes),
	ICE_VSI_STAT("tx_bytes", eth_stats.tx_bytes),
	ICE_VSI_STAT("rx_dropped", eth_stats.rx_discards),
	ICE_VSI_STAT("rx_unknown_protocol", eth_stats.rx_unknown_protocol),
	ICE_VSI_STAT("rx_alloc_fail", rx_buf_failed),
	ICE_VSI_STAT("rx_pg_alloc_fail", rx_page_failed),
	ICE_VSI_STAT("tx_errors", eth_stats.tx_errors),
	ICE_VSI_STAT("tx_linearize", tx_linearize),
};

enum ice_ethtool_test_id {
	ICE_ETH_TEST_REG = 0,
	ICE_ETH_TEST_EEPROM,
	ICE_ETH_TEST_INTR,
	ICE_ETH_TEST_LOOP,
	ICE_ETH_TEST_LINK,
};

static const char ice_gstrings_test[][ETH_GSTRING_LEN] = {
	"Register test  (offline)",
	"EEPROM test    (offline)",
	"Interrupt test (offline)",
	"Loopback test  (offline)",
	"Link test   (on/offline)",
};

#define ICE_TEST_LEN (sizeof(ice_gstrings_test) / ETH_GSTRING_LEN)

/* These PF_STATs might look like duplicates of some NETDEV_STATs,
 * but they aren't. This device is capable of supporting multiple
 * VSIs/netdevs on a single PF. The NETDEV_STATs are for individual
 * netdevs whereas the PF_STATs are for the physical function that's
 * hosting these netdevs.
 *
 * The PF_STATs are appended to the netdev stats only when ethtool -S
 * is queried on the base PF netdev.
 */
static const struct ice_stats ice_gstrings_pf_stats[] = {
	ICE_PF_STAT("rx_bytes.nic", stats.eth.rx_bytes),
	ICE_PF_STAT("tx_bytes.nic", stats.eth.tx_bytes),
	ICE_PF_STAT("rx_unicast.nic", stats.eth.rx_unicast),
	ICE_PF_STAT("tx_unicast.nic", stats.eth.tx_unicast),
	ICE_PF_STAT("rx_multicast.nic", stats.eth.rx_multicast),
	ICE_PF_STAT("tx_multicast.nic", stats.eth.tx_multicast),
	ICE_PF_STAT("rx_broadcast.nic", stats.eth.rx_broadcast),
	ICE_PF_STAT("tx_broadcast.nic", stats.eth.tx_broadcast),
	ICE_PF_STAT("tx_errors.nic", stats.eth.tx_errors),
	ICE_PF_STAT("rx_size_64.nic", stats.rx_size_64),
	ICE_PF_STAT("tx_size_64.nic", stats.tx_size_64),
	ICE_PF_STAT("rx_size_127.nic", stats.rx_size_127),
	ICE_PF_STAT("tx_size_127.nic", stats.tx_size_127),
	ICE_PF_STAT("rx_size_255.nic", stats.rx_size_255),
	ICE_PF_STAT("tx_size_255.nic", stats.tx_size_255),
	ICE_PF_STAT("rx_size_511.nic", stats.rx_size_511),
	ICE_PF_STAT("tx_size_511.nic", stats.tx_size_511),
	ICE_PF_STAT("rx_size_1023.nic", stats.rx_size_1023),
	ICE_PF_STAT("tx_size_1023.nic", stats.tx_size_1023),
	ICE_PF_STAT("rx_size_1522.nic", stats.rx_size_1522),
	ICE_PF_STAT("tx_size_1522.nic", stats.tx_size_1522),
	ICE_PF_STAT("rx_size_big.nic", stats.rx_size_big),
	ICE_PF_STAT("tx_size_big.nic", stats.tx_size_big),
	ICE_PF_STAT("link_xon_rx.nic", stats.link_xon_rx),
	ICE_PF_STAT("link_xon_tx.nic", stats.link_xon_tx),
	ICE_PF_STAT("link_xoff_rx.nic", stats.link_xoff_rx),
	ICE_PF_STAT("link_xoff_tx.nic", stats.link_xoff_tx),
	ICE_PF_STAT("tx_dropped_link_down.nic", stats.tx_dropped_link_down),
	ICE_PF_STAT("rx_undersize.nic", stats.rx_undersize),
	ICE_PF_STAT("rx_fragments.nic", stats.rx_fragments),
	ICE_PF_STAT("rx_oversize.nic", stats.rx_oversize),
	ICE_PF_STAT("rx_jabber.nic", stats.rx_jabber),
	ICE_PF_STAT("rx_csum_bad.nic", hw_csum_rx_error),
	ICE_PF_STAT("rx_length_errors.nic", stats.rx_len_errors),
	ICE_PF_STAT("rx_dropped.nic", stats.eth.rx_discards),
	ICE_PF_STAT("rx_crc_errors.nic", stats.crc_errors),
	ICE_PF_STAT("illegal_bytes.nic", stats.illegal_bytes),
	ICE_PF_STAT("mac_local_faults.nic", stats.mac_local_faults),
	ICE_PF_STAT("mac_remote_faults.nic", stats.mac_remote_faults),
};

static const u32 ice_regs_dump_list[] = {
	PFGEN_STATE,
	PRTGEN_STATUS,
	QRX_CTRL(0),
	QINT_TQCTL(0),
	QINT_RQCTL(0),
	PFINT_OICR_ENA,
	QRX_ITR(0),
	PF0INT_ITR_0(0),
	PF0INT_ITR_1(0),
	PF0INT_ITR_2(0),
};

struct ice_priv_flag {
	char name[ETH_GSTRING_LEN];
	u32 bitno;			/* bit position in pf->flags */
};

#define ICE_PRIV_FLAG(_name, _bitno) { \
	.name = _name, \
	.bitno = _bitno, \
}

static const struct ice_priv_flag ice_gstrings_priv_flags[] = {
	ICE_PRIV_FLAG("link-down-on-close", ICE_FLAG_LINK_DOWN_ON_CLOSE_ENA),
	ICE_PRIV_FLAG("enable-fw-lldp", ICE_FLAG_ENABLE_FW_LLDP),
};

#define ICE_PRIV_FLAG_ARRAY_SIZE	ARRAY_SIZE(ice_gstrings_priv_flags)

/**
 * ice_nvm_version_str - format the NVM version strings
 * @hw: ptr to the hardware info
 */
static char *ice_nvm_version_str(struct ice_hw *hw)
{
	static char buf[ICE_ETHTOOL_FWVER_LEN];
	u8 ver, patch;
	u32 full_ver;
	u16 build;

	full_ver = hw->nvm.oem_ver;
	ver = (u8)((full_ver & ICE_OEM_VER_MASK) >> ICE_OEM_VER_SHIFT);
	build = (u16)((full_ver & ICE_OEM_VER_BUILD_MASK) >>
		      ICE_OEM_VER_BUILD_SHIFT);
	patch = (u8)(full_ver & ICE_OEM_VER_PATCH_MASK);

	snprintf(buf, sizeof(buf), "%x.%02x 0x%x %d.%d.%d",
		 (hw->nvm.ver & ICE_NVM_VER_HI_MASK) >> ICE_NVM_VER_HI_SHIFT,
		 (hw->nvm.ver & ICE_NVM_VER_LO_MASK) >> ICE_NVM_VER_LO_SHIFT,
		 hw->nvm.eetrack, ver, build, patch);

	return buf;
}

static void
ice_get_drvinfo(struct net_device *netdev, struct ethtool_drvinfo *drvinfo)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_vsi *vsi = np->vsi;
	struct ice_pf *pf = vsi->back;

	strlcpy(drvinfo->driver, KBUILD_MODNAME, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, ice_drv_ver, sizeof(drvinfo->version));
	strlcpy(drvinfo->fw_version, ice_nvm_version_str(&pf->hw),
		sizeof(drvinfo->fw_version));
	strlcpy(drvinfo->bus_info, pci_name(pf->pdev),
		sizeof(drvinfo->bus_info));
	drvinfo->n_priv_flags = ICE_PRIV_FLAG_ARRAY_SIZE;
}

static int ice_get_regs_len(struct net_device __always_unused *netdev)
{
	return sizeof(ice_regs_dump_list);
}

static void
ice_get_regs(struct net_device *netdev, struct ethtool_regs *regs, void *p)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_pf *pf = np->vsi->back;
	struct ice_hw *hw = &pf->hw;
	u32 *regs_buf = (u32 *)p;
	int i;

	regs->version = 1;

	for (i = 0; i < ARRAY_SIZE(ice_regs_dump_list); ++i)
		regs_buf[i] = rd32(hw, ice_regs_dump_list[i]);
}

static u32 ice_get_msglevel(struct net_device *netdev)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_pf *pf = np->vsi->back;

#ifndef CONFIG_DYNAMIC_DEBUG
	if (pf->hw.debug_mask)
		netdev_info(netdev, "hw debug_mask: 0x%llX\n",
			    pf->hw.debug_mask);
#endif /* !CONFIG_DYNAMIC_DEBUG */

	return pf->msg_enable;
}

static void ice_set_msglevel(struct net_device *netdev, u32 data)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_pf *pf = np->vsi->back;

#ifndef CONFIG_DYNAMIC_DEBUG
	if (ICE_DBG_USER & data)
		pf->hw.debug_mask = data;
	else
		pf->msg_enable = data;
#else
	pf->msg_enable = data;
#endif /* !CONFIG_DYNAMIC_DEBUG */
}

static int ice_get_eeprom_len(struct net_device *netdev)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_pf *pf = np->vsi->back;

	return (int)(pf->hw.nvm.sr_words * sizeof(u16));
}

static int
ice_get_eeprom(struct net_device *netdev, struct ethtool_eeprom *eeprom,
	       u8 *bytes)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	u16 first_word, last_word, nwords;
	struct ice_vsi *vsi = np->vsi;
	struct ice_pf *pf = vsi->back;
	struct ice_hw *hw = &pf->hw;
	enum ice_status status;
	struct device *dev;
	int ret = 0;
	u16 *buf;

	dev = &pf->pdev->dev;

	eeprom->magic = hw->vendor_id | (hw->device_id << 16);

	first_word = eeprom->offset >> 1;
	last_word = (eeprom->offset + eeprom->len - 1) >> 1;
	nwords = last_word - first_word + 1;

	buf = devm_kcalloc(dev, nwords, sizeof(u16), GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	status = ice_read_sr_buf(hw, first_word, &nwords, buf);
	if (status) {
		dev_err(dev, "ice_read_sr_buf failed, err %d aq_err %d\n",
			status, hw->adminq.sq_last_status);
		eeprom->len = sizeof(u16) * nwords;
		ret = -EIO;
		goto out;
	}

	memcpy(bytes, (u8 *)buf + (eeprom->offset & 1), eeprom->len);
out:
	devm_kfree(dev, buf);
	return ret;
}

/**
 * ice_active_vfs - check if there are any active VFs
 * @pf: board private structure
 *
 * Returns true if an active VF is found, otherwise returns false
 */
static bool ice_active_vfs(struct ice_pf *pf)
{
	struct ice_vf *vf = pf->vf;
	int i;

	for (i = 0; i < pf->num_alloc_vfs; i++, vf++)
		if (test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states))
			return true;
	return false;
}

/**
 * ice_link_test - perform a link test on a given net_device
 * @netdev: network interface device structure
 *
 * This function performs one of the self-tests required by ethtool.
 * Returns 0 on success, non-zero on failure.
 */
static u64 ice_link_test(struct net_device *netdev)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	enum ice_status status;
	bool link_up = false;

	netdev_info(netdev, "link test\n");
	status = ice_get_link_status(np->vsi->port_info, &link_up);
	if (status) {
		netdev_err(netdev, "link query error, status = %d\n", status);
		return 1;
	}

	if (!link_up)
		return 2;

	return 0;
}

/**
 * ice_eeprom_test - perform an EEPROM test on a given net_device
 * @netdev: network interface device structure
 *
 * This function performs one of the self-tests required by ethtool.
 * Returns 0 on success, non-zero on failure.
 */
static u64 ice_eeprom_test(struct net_device *netdev)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_pf *pf = np->vsi->back;

	netdev_info(netdev, "EEPROM test\n");
	return !!(ice_nvm_validate_checksum(&pf->hw));
}

/**
 * ice_reg_pattern_test
 * @hw: pointer to the HW struct
 * @reg: reg to be tested
 * @mask: bits to be touched
 */
static int ice_reg_pattern_test(struct ice_hw *hw, u32 reg, u32 mask)
{
	struct ice_pf *pf = (struct ice_pf *)hw->back;
	static const u32 patterns[] = {
		0x5A5A5A5A, 0xA5A5A5A5,
		0x00000000, 0xFFFFFFFF
	};
	u32 val, orig_val;
	int i;

	orig_val = rd32(hw, reg);
	for (i = 0; i < ARRAY_SIZE(patterns); ++i) {
		u32 pattern = patterns[i] & mask;

		wr32(hw, reg, pattern);
		val = rd32(hw, reg);
		if (val == pattern)
			continue;
		dev_err(&pf->pdev->dev,
			"%s: reg pattern test failed - reg 0x%08x pat 0x%08x val 0x%08x\n"
			, __func__, reg, pattern, val);
		return 1;
	}

	wr32(hw, reg, orig_val);
	val = rd32(hw, reg);
	if (val != orig_val) {
		dev_err(&pf->pdev->dev,
			"%s: reg restore test failed - reg 0x%08x orig 0x%08x val 0x%08x\n"
			, __func__, reg, orig_val, val);
		return 1;
	}

	return 0;
}

/**
 * ice_reg_test - perform a register test on a given net_device
 * @netdev: network interface device structure
 *
 * This function performs one of the self-tests required by ethtool.
 * Returns 0 on success, non-zero on failure.
 */
static u64 ice_reg_test(struct net_device *netdev)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_hw *hw = np->vsi->port_info->hw;
	u32 int_elements = hw->func_caps.common_cap.num_msix_vectors ?
		hw->func_caps.common_cap.num_msix_vectors - 1 : 1;
	struct ice_diag_reg_test_info {
		u32 address;
		u32 mask;
		u32 elem_num;
		u32 elem_size;
	} ice_reg_list[] = {
		{GLINT_ITR(0, 0), 0x00000fff, int_elements,
			GLINT_ITR(0, 1) - GLINT_ITR(0, 0)},
		{GLINT_ITR(1, 0), 0x00000fff, int_elements,
			GLINT_ITR(1, 1) - GLINT_ITR(1, 0)},
		{GLINT_ITR(0, 0), 0x00000fff, int_elements,
			GLINT_ITR(2, 1) - GLINT_ITR(2, 0)},
		{GLINT_CTL, 0xffff0001, 1, 0}
	};
	int i;

	netdev_dbg(netdev, "Register test\n");
	for (i = 0; i < ARRAY_SIZE(ice_reg_list); ++i) {
		u32 j;

		for (j = 0; j < ice_reg_list[i].elem_num; ++j) {
			u32 mask = ice_reg_list[i].mask;
			u32 reg = ice_reg_list[i].address +
				(j * ice_reg_list[i].elem_size);

			/* bail on failure (non-zero return) */
			if (ice_reg_pattern_test(hw, reg, mask))
				return 1;
		}
	}

	return 0;
}

/**
 * ice_lbtest_prepare_rings - configure Tx/Rx test rings
 * @vsi: pointer to the VSI structure
 *
 * Function configures rings of a VSI for loopback test without
 * enabling interrupts or informing the kernel about new queues.
 *
 * Returns 0 on success, negative on failure.
 */
static int ice_lbtest_prepare_rings(struct ice_vsi *vsi)
{
	int status;

	status = ice_vsi_setup_tx_rings(vsi);
	if (status)
		goto err_setup_tx_ring;

	status = ice_vsi_setup_rx_rings(vsi);
	if (status)
		goto err_setup_rx_ring;

	status = ice_vsi_cfg(vsi);
	if (status)
		goto err_setup_rx_ring;

	status = ice_vsi_start_rx_rings(vsi);
	if (status)
		goto err_start_rx_ring;

	return status;

err_start_rx_ring:
	ice_vsi_free_rx_rings(vsi);
err_setup_rx_ring:
	ice_vsi_stop_lan_tx_rings(vsi, ICE_NO_RESET, 0);
err_setup_tx_ring:
	ice_vsi_free_tx_rings(vsi);

	return status;
}

/**
 * ice_lbtest_disable_rings - disable Tx/Rx test rings after loopback test
 * @vsi: pointer to the VSI structure
 *
 * Function stops and frees VSI rings after a loopback test.
 * Returns 0 on success, negative on failure.
 */
static int ice_lbtest_disable_rings(struct ice_vsi *vsi)
{
	int status;

	status = ice_vsi_stop_lan_tx_rings(vsi, ICE_NO_RESET, 0);
	if (status)
		netdev_err(vsi->netdev, "Failed to stop Tx rings, VSI %d error %d\n",
			   vsi->vsi_num, status);

	status = ice_vsi_stop_rx_rings(vsi);
	if (status)
		netdev_err(vsi->netdev, "Failed to stop Rx rings, VSI %d error %d\n",
			   vsi->vsi_num, status);

	ice_vsi_free_tx_rings(vsi);
	ice_vsi_free_rx_rings(vsi);

	return status;
}

/**
 * ice_lbtest_create_frame - create test packet
 * @pf: pointer to the PF structure
 * @ret_data: allocated frame buffer
 * @size: size of the packet data
 *
 * Function allocates a frame with a test pattern on specific offsets.
 * Returns 0 on success, non-zero on failure.
 */
static int ice_lbtest_create_frame(struct ice_pf *pf, u8 **ret_data, u16 size)
{
	u8 *data;

	if (!pf)
		return -EINVAL;

	data = devm_kzalloc(&pf->pdev->dev, size, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	/* Since the ethernet test frame should always be at least
	 * 64 bytes long, fill some octets in the payload with test data.
	 */
	memset(data, 0xFF, size);
	data[32] = 0xDE;
	data[42] = 0xAD;
	data[44] = 0xBE;
	data[46] = 0xEF;

	*ret_data = data;

	return 0;
}

/**
 * ice_lbtest_check_frame - verify received loopback frame
 * @frame: pointer to the raw packet data
 *
 * Function verifies received test frame with a pattern.
 * Returns true if frame matches the pattern, false otherwise.
 */
static bool ice_lbtest_check_frame(u8 *frame)
{
	/* Validate bytes of a frame under offsets chosen earlier */
	if (frame[32] == 0xDE &&
	    frame[42] == 0xAD &&
	    frame[44] == 0xBE &&
	    frame[46] == 0xEF &&
	    frame[48] == 0xFF)
		return true;

	return false;
}

/**
 * ice_diag_send - send test frames to the test ring
 * @tx_ring: pointer to the transmit ring
 * @data: pointer to the raw packet data
 * @size: size of the packet to send
 *
 * Function sends loopback packets on a test Tx ring.
 */
static int ice_diag_send(struct ice_ring *tx_ring, u8 *data, u16 size)
{
	struct ice_tx_desc *tx_desc;
	struct ice_tx_buf *tx_buf;
	dma_addr_t dma;
	u64 td_cmd;

	tx_desc = ICE_TX_DESC(tx_ring, tx_ring->next_to_use);
	tx_buf = &tx_ring->tx_buf[tx_ring->next_to_use];

	dma = dma_map_single(tx_ring->dev, data, size, DMA_TO_DEVICE);
	if (dma_mapping_error(tx_ring->dev, dma))
		return -EINVAL;

	tx_desc->buf_addr = cpu_to_le64(dma);

	/* These flags are required for a descriptor to be pushed out */
	td_cmd = (u64)(ICE_TX_DESC_CMD_EOP | ICE_TX_DESC_CMD_RS);
	tx_desc->cmd_type_offset_bsz =
		cpu_to_le64(ICE_TX_DESC_DTYPE_DATA |
			    (td_cmd << ICE_TXD_QW1_CMD_S) |
			    ((u64)0 << ICE_TXD_QW1_OFFSET_S) |
			    ((u64)size << ICE_TXD_QW1_TX_BUF_SZ_S) |
			    ((u64)0 << ICE_TXD_QW1_L2TAG1_S));

	tx_buf->next_to_watch = tx_desc;

	/* Force memory write to complete before letting h/w know
	 * there are new descriptors to fetch.
	 */
	wmb();

	tx_ring->next_to_use++;
	if (tx_ring->next_to_use >= tx_ring->count)
		tx_ring->next_to_use = 0;

	writel_relaxed(tx_ring->next_to_use, tx_ring->tail);

	/* Wait until the packets get transmitted to the receive queue. */
	usleep_range(1000, 2000);
	dma_unmap_single(tx_ring->dev, dma, size, DMA_TO_DEVICE);

	return 0;
}

#define ICE_LB_FRAME_SIZE 64
/**
 * ice_lbtest_receive_frames - receive and verify test frames
 * @rx_ring: pointer to the receive ring
 *
 * Function receives loopback packets and verify their correctness.
 * Returns number of received valid frames.
 */
static int ice_lbtest_receive_frames(struct ice_ring *rx_ring)
{
	struct ice_rx_buf *rx_buf;
	int valid_frames, i;
	u8 *received_buf;

	valid_frames = 0;

	for (i = 0; i < rx_ring->count; i++) {
		union ice_32b_rx_flex_desc *rx_desc;

		rx_desc = ICE_RX_DESC(rx_ring, i);

		if (!(rx_desc->wb.status_error0 &
		    cpu_to_le16(ICE_TX_DESC_CMD_EOP | ICE_TX_DESC_CMD_RS)))
			continue;

		rx_buf = &rx_ring->rx_buf[i];
		received_buf = page_address(rx_buf->page);

		if (ice_lbtest_check_frame(received_buf))
			valid_frames++;
	}

	return valid_frames;
}

/**
 * ice_loopback_test - perform a loopback test on a given net_device
 * @netdev: network interface device structure
 *
 * This function performs one of the self-tests required by ethtool.
 * Returns 0 on success, non-zero on failure.
 */
static u64 ice_loopback_test(struct net_device *netdev)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_vsi *orig_vsi = np->vsi, *test_vsi;
	struct ice_pf *pf = orig_vsi->back;
	struct ice_ring *tx_ring, *rx_ring;
	u8 broadcast[ETH_ALEN], ret = 0;
	int num_frames, valid_frames;
	LIST_HEAD(tmp_list);
	u8 *tx_frame;
	int i;

	netdev_info(netdev, "loopback test\n");

	test_vsi = ice_lb_vsi_setup(pf, pf->hw.port_info);
	if (!test_vsi) {
		netdev_err(netdev, "Failed to create a VSI for the loopback test");
		return 1;
	}

	test_vsi->netdev = netdev;
	tx_ring = test_vsi->tx_rings[0];
	rx_ring = test_vsi->rx_rings[0];

	if (ice_lbtest_prepare_rings(test_vsi)) {
		ret = 2;
		goto lbtest_vsi_close;
	}

	if (ice_alloc_rx_bufs(rx_ring, rx_ring->count)) {
		ret = 3;
		goto lbtest_rings_dis;
	}

	/* Enable MAC loopback in firmware */
	if (ice_aq_set_mac_loopback(&pf->hw, true, NULL)) {
		ret = 4;
		goto lbtest_mac_dis;
	}

	/* Test VSI needs to receive broadcast packets */
	eth_broadcast_addr(broadcast);
	if (ice_add_mac_to_list(test_vsi, &tmp_list, broadcast)) {
		ret = 5;
		goto lbtest_mac_dis;
	}

	if (ice_add_mac(&pf->hw, &tmp_list)) {
		ret = 6;
		goto free_mac_list;
	}

	if (ice_lbtest_create_frame(pf, &tx_frame, ICE_LB_FRAME_SIZE)) {
		ret = 7;
		goto remove_mac_filters;
	}

	num_frames = min_t(int, tx_ring->count, 32);
	for (i = 0; i < num_frames; i++) {
		if (ice_diag_send(tx_ring, tx_frame, ICE_LB_FRAME_SIZE)) {
			ret = 8;
			goto lbtest_free_frame;
		}
	}

	valid_frames = ice_lbtest_receive_frames(rx_ring);
	if (!valid_frames)
		ret = 9;
	else if (valid_frames != num_frames)
		ret = 10;

lbtest_free_frame:
	devm_kfree(&pf->pdev->dev, tx_frame);
remove_mac_filters:
	if (ice_remove_mac(&pf->hw, &tmp_list))
		netdev_err(netdev, "Could not remove MAC filter for the test VSI");
free_mac_list:
	ice_free_fltr_list(&pf->pdev->dev, &tmp_list);
lbtest_mac_dis:
	/* Disable MAC loopback after the test is completed. */
	if (ice_aq_set_mac_loopback(&pf->hw, false, NULL))
		netdev_err(netdev, "Could not disable MAC loopback\n");
lbtest_rings_dis:
	if (ice_lbtest_disable_rings(test_vsi))
		netdev_err(netdev, "Could not disable test rings\n");
lbtest_vsi_close:
	test_vsi->netdev = NULL;
	if (ice_vsi_release(test_vsi))
		netdev_err(netdev, "Failed to remove the test VSI");

	return ret;
}

/**
 * ice_intr_test - perform an interrupt test on a given net_device
 * @netdev: network interface device structure
 *
 * This function performs one of the self-tests required by ethtool.
 * Returns 0 on success, non-zero on failure.
 */
static u64 ice_intr_test(struct net_device *netdev)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_pf *pf = np->vsi->back;
	u16 swic_old = pf->sw_int_count;

	netdev_info(netdev, "interrupt test\n");

	wr32(&pf->hw, GLINT_DYN_CTL(pf->oicr_idx),
	     GLINT_DYN_CTL_SW_ITR_INDX_M |
	     GLINT_DYN_CTL_INTENA_MSK_M |
	     GLINT_DYN_CTL_SWINT_TRIG_M);

	usleep_range(1000, 2000);
	return (swic_old == pf->sw_int_count);
}

/**
 * ice_self_test - handler function for performing a self-test by ethtool
 * @netdev: network interface device structure
 * @eth_test: ethtool_test structure
 * @data: required by ethtool.self_test
 *
 * This function is called after invoking 'ethtool -t devname' command where
 * devname is the name of the network device on which ethtool should operate.
 * It performs a set of self-tests to check if a device works properly.
 */
static void
ice_self_test(struct net_device *netdev, struct ethtool_test *eth_test,
	      u64 *data)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	bool if_running = netif_running(netdev);
	struct ice_pf *pf = np->vsi->back;

	if (eth_test->flags == ETH_TEST_FL_OFFLINE) {
		netdev_info(netdev, "offline testing starting\n");

		set_bit(__ICE_TESTING, pf->state);

		if (ice_active_vfs(pf)) {
			dev_warn(&pf->pdev->dev,
				 "Please take active VFs and Netqueues offline and restart the adapter before running NIC diagnostics\n");
			data[ICE_ETH_TEST_REG] = 1;
			data[ICE_ETH_TEST_EEPROM] = 1;
			data[ICE_ETH_TEST_INTR] = 1;
			data[ICE_ETH_TEST_LOOP] = 1;
			data[ICE_ETH_TEST_LINK] = 1;
			eth_test->flags |= ETH_TEST_FL_FAILED;
			clear_bit(__ICE_TESTING, pf->state);
			goto skip_ol_tests;
		}
		/* If the device is online then take it offline */
		if (if_running)
			/* indicate we're in test mode */
			ice_stop(netdev);

		data[ICE_ETH_TEST_LINK] = ice_link_test(netdev);
		data[ICE_ETH_TEST_EEPROM] = ice_eeprom_test(netdev);
		data[ICE_ETH_TEST_INTR] = ice_intr_test(netdev);
		data[ICE_ETH_TEST_LOOP] = ice_loopback_test(netdev);
		data[ICE_ETH_TEST_REG] = ice_reg_test(netdev);

		if (data[ICE_ETH_TEST_LINK] ||
		    data[ICE_ETH_TEST_EEPROM] ||
		    data[ICE_ETH_TEST_LOOP] ||
		    data[ICE_ETH_TEST_INTR] ||
		    data[ICE_ETH_TEST_REG])
			eth_test->flags |= ETH_TEST_FL_FAILED;

		clear_bit(__ICE_TESTING, pf->state);

		if (if_running) {
			int status = ice_open(netdev);

			if (status) {
				dev_err(&pf->pdev->dev,
					"Could not open device %s, err %d",
					pf->int_name, status);
			}
		}
	} else {
		/* Online tests */
		netdev_info(netdev, "online testing starting\n");

		data[ICE_ETH_TEST_LINK] = ice_link_test(netdev);
		if (data[ICE_ETH_TEST_LINK])
			eth_test->flags |= ETH_TEST_FL_FAILED;

		/* Offline only tests, not run in online; pass by default */
		data[ICE_ETH_TEST_REG] = 0;
		data[ICE_ETH_TEST_EEPROM] = 0;
		data[ICE_ETH_TEST_INTR] = 0;
		data[ICE_ETH_TEST_LOOP] = 0;
	}

skip_ol_tests:
	netdev_info(netdev, "testing finished\n");
}

static void ice_get_strings(struct net_device *netdev, u32 stringset, u8 *data)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_vsi *vsi = np->vsi;
	char *p = (char *)data;
	unsigned int i;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < ICE_VSI_STATS_LEN; i++) {
			snprintf(p, ETH_GSTRING_LEN, "%s",
				 ice_gstrings_vsi_stats[i].stat_string);
			p += ETH_GSTRING_LEN;
		}

		ice_for_each_alloc_txq(vsi, i) {
			snprintf(p, ETH_GSTRING_LEN,
				 "tx_queue_%u_packets", i);
			p += ETH_GSTRING_LEN;
			snprintf(p, ETH_GSTRING_LEN, "tx_queue_%u_bytes", i);
			p += ETH_GSTRING_LEN;
		}

		ice_for_each_alloc_rxq(vsi, i) {
			snprintf(p, ETH_GSTRING_LEN,
				 "rx_queue_%u_packets", i);
			p += ETH_GSTRING_LEN;
			snprintf(p, ETH_GSTRING_LEN, "rx_queue_%u_bytes", i);
			p += ETH_GSTRING_LEN;
		}

		if (vsi->type != ICE_VSI_PF)
			return;

		for (i = 0; i < ICE_PF_STATS_LEN; i++) {
			snprintf(p, ETH_GSTRING_LEN, "%s",
				 ice_gstrings_pf_stats[i].stat_string);
			p += ETH_GSTRING_LEN;
		}

		for (i = 0; i < ICE_MAX_USER_PRIORITY; i++) {
			snprintf(p, ETH_GSTRING_LEN,
				 "tx_priority_%u_xon.nic", i);
			p += ETH_GSTRING_LEN;
			snprintf(p, ETH_GSTRING_LEN,
				 "tx_priority_%u_xoff.nic", i);
			p += ETH_GSTRING_LEN;
		}
		for (i = 0; i < ICE_MAX_USER_PRIORITY; i++) {
			snprintf(p, ETH_GSTRING_LEN,
				 "rx_priority_%u_xon.nic", i);
			p += ETH_GSTRING_LEN;
			snprintf(p, ETH_GSTRING_LEN,
				 "rx_priority_%u_xoff.nic", i);
			p += ETH_GSTRING_LEN;
		}
		break;
	case ETH_SS_TEST:
		memcpy(data, ice_gstrings_test, ICE_TEST_LEN * ETH_GSTRING_LEN);
		break;
	case ETH_SS_PRIV_FLAGS:
		for (i = 0; i < ICE_PRIV_FLAG_ARRAY_SIZE; i++) {
			snprintf(p, ETH_GSTRING_LEN, "%s",
				 ice_gstrings_priv_flags[i].name);
			p += ETH_GSTRING_LEN;
		}
		break;
	default:
		break;
	}
}

static int
ice_set_phys_id(struct net_device *netdev, enum ethtool_phys_id_state state)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	bool led_active;

	switch (state) {
	case ETHTOOL_ID_ACTIVE:
		led_active = true;
		break;
	case ETHTOOL_ID_INACTIVE:
		led_active = false;
		break;
	default:
		return -EINVAL;
	}

	if (ice_aq_set_port_id_led(np->vsi->port_info, !led_active, NULL))
		return -EIO;

	return 0;
}

/**
 * ice_set_fec_cfg - Set link FEC options
 * @netdev: network interface device structure
 * @req_fec: FEC mode to configure
 */
static int ice_set_fec_cfg(struct net_device *netdev, enum ice_fec_mode req_fec)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_aqc_set_phy_cfg_data config = { 0 };
	struct ice_aqc_get_phy_caps_data *caps;
	struct ice_vsi *vsi = np->vsi;
	u8 sw_cfg_caps, sw_cfg_fec;
	struct ice_port_info *pi;
	enum ice_status status;
	int err = 0;

	pi = vsi->port_info;
	if (!pi)
		return -EOPNOTSUPP;

	/* Changing the FEC parameters is not supported if not the PF VSI */
	if (vsi->type != ICE_VSI_PF) {
		netdev_info(netdev, "Changing FEC parameters only supported for PF VSI\n");
		return -EOPNOTSUPP;
	}

	/* Get last SW configuration */
	caps = devm_kzalloc(&vsi->back->pdev->dev, sizeof(*caps), GFP_KERNEL);
	if (!caps)
		return -ENOMEM;

	status = ice_aq_get_phy_caps(pi, false, ICE_AQC_REPORT_SW_CFG,
				     caps, NULL);
	if (status) {
		err = -EAGAIN;
		goto done;
	}

	/* Copy SW configuration returned from PHY caps to PHY config */
	ice_copy_phy_caps_to_cfg(caps, &config);
	sw_cfg_caps = caps->caps;
	sw_cfg_fec = caps->link_fec_options;

	/* Get toloplogy caps, then copy PHY FEC topoloy caps to PHY config */
	memset(caps, 0, sizeof(*caps));

	status = ice_aq_get_phy_caps(pi, false, ICE_AQC_REPORT_TOPO_CAP,
				     caps, NULL);
	if (status) {
		err = -EAGAIN;
		goto done;
	}

	config.caps |= (caps->caps & ICE_AQC_PHY_EN_AUTO_FEC);
	config.link_fec_opt = caps->link_fec_options;

	ice_cfg_phy_fec(&config, req_fec);

	/* If FEC mode has changed, then set PHY configuration and enable AN. */
	if ((config.caps & ICE_AQ_PHY_ENA_AUTO_FEC) !=
	    (sw_cfg_caps & ICE_AQC_PHY_EN_AUTO_FEC) ||
	    config.link_fec_opt != sw_cfg_fec) {
		if (caps->caps & ICE_AQC_PHY_AN_MODE)
			config.caps |= ICE_AQ_PHY_ENA_AUTO_LINK_UPDT;

		status = ice_aq_set_phy_cfg(pi->hw, pi->lport, &config, NULL);

		if (status)
			err = -EAGAIN;
	}

done:
	devm_kfree(&vsi->back->pdev->dev, caps);
	return err;
}

/**
 * ice_set_fecparam - Set FEC link options
 * @netdev: network interface device structure
 * @fecparam: Ethtool structure to retrieve FEC parameters
 */
static int
ice_set_fecparam(struct net_device *netdev, struct ethtool_fecparam *fecparam)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_vsi *vsi = np->vsi;
	enum ice_fec_mode fec;

	switch (fecparam->fec) {
	case ETHTOOL_FEC_AUTO:
		fec = ICE_FEC_AUTO;
		break;
	case ETHTOOL_FEC_RS:
		fec = ICE_FEC_RS;
		break;
	case ETHTOOL_FEC_BASER:
		fec = ICE_FEC_BASER;
		break;
	case ETHTOOL_FEC_OFF:
	case ETHTOOL_FEC_NONE:
		fec = ICE_FEC_NONE;
		break;
	default:
		dev_warn(&vsi->back->pdev->dev, "Unsupported FEC mode: %d\n",
			 fecparam->fec);
		return -EINVAL;
	}

	return ice_set_fec_cfg(netdev, fec);
}

/**
 * ice_get_fecparam - Get link FEC options
 * @netdev: network interface device structure
 * @fecparam: Ethtool structure to retrieve FEC parameters
 */
static int
ice_get_fecparam(struct net_device *netdev, struct ethtool_fecparam *fecparam)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_aqc_get_phy_caps_data *caps;
	struct ice_link_status *link_info;
	struct ice_vsi *vsi = np->vsi;
	struct ice_port_info *pi;
	enum ice_status status;
	int err = 0;

	pi = vsi->port_info;

	if (!pi)
		return -EOPNOTSUPP;
	link_info = &pi->phy.link_info;

	/* Set FEC mode based on negotiated link info */
	switch (link_info->fec_info) {
	case ICE_AQ_LINK_25G_KR_FEC_EN:
		fecparam->active_fec = ETHTOOL_FEC_BASER;
		break;
	case ICE_AQ_LINK_25G_RS_528_FEC_EN:
		/* fall through */
	case ICE_AQ_LINK_25G_RS_544_FEC_EN:
		fecparam->active_fec = ETHTOOL_FEC_RS;
		break;
	default:
		fecparam->active_fec = ETHTOOL_FEC_OFF;
		break;
	}

	caps = devm_kzalloc(&vsi->back->pdev->dev, sizeof(*caps), GFP_KERNEL);
	if (!caps)
		return -ENOMEM;

	status = ice_aq_get_phy_caps(pi, false, ICE_AQC_REPORT_TOPO_CAP,
				     caps, NULL);
	if (status) {
		err = -EAGAIN;
		goto done;
	}

	/* Set supported/configured FEC modes based on PHY capability */
	if (caps->caps & ICE_AQC_PHY_EN_AUTO_FEC)
		fecparam->fec |= ETHTOOL_FEC_AUTO;
	if (caps->link_fec_options & ICE_AQC_PHY_FEC_10G_KR_40G_KR4_EN ||
	    caps->link_fec_options & ICE_AQC_PHY_FEC_10G_KR_40G_KR4_REQ ||
	    caps->link_fec_options & ICE_AQC_PHY_FEC_25G_KR_CLAUSE74_EN ||
	    caps->link_fec_options & ICE_AQC_PHY_FEC_25G_KR_REQ)
		fecparam->fec |= ETHTOOL_FEC_BASER;
	if (caps->link_fec_options & ICE_AQC_PHY_FEC_25G_RS_528_REQ ||
	    caps->link_fec_options & ICE_AQC_PHY_FEC_25G_RS_544_REQ ||
	    caps->link_fec_options & ICE_AQC_PHY_FEC_25G_RS_CLAUSE91_EN)
		fecparam->fec |= ETHTOOL_FEC_RS;
	if (caps->link_fec_options == 0)
		fecparam->fec |= ETHTOOL_FEC_OFF;

done:
	devm_kfree(&vsi->back->pdev->dev, caps);
	return err;
}

/**
 * ice_get_priv_flags - report device private flags
 * @netdev: network interface device structure
 *
 * The get string set count and the string set should be matched for each
 * flag returned.  Add new strings for each flag to the ice_gstrings_priv_flags
 * array.
 *
 * Returns a u32 bitmap of flags.
 */
static u32 ice_get_priv_flags(struct net_device *netdev)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_vsi *vsi = np->vsi;
	struct ice_pf *pf = vsi->back;
	u32 i, ret_flags = 0;

	for (i = 0; i < ICE_PRIV_FLAG_ARRAY_SIZE; i++) {
		const struct ice_priv_flag *priv_flag;

		priv_flag = &ice_gstrings_priv_flags[i];

		if (test_bit(priv_flag->bitno, pf->flags))
			ret_flags |= BIT(i);
	}

	return ret_flags;
}

/**
 * ice_set_priv_flags - set private flags
 * @netdev: network interface device structure
 * @flags: bit flags to be set
 */
static int ice_set_priv_flags(struct net_device *netdev, u32 flags)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	DECLARE_BITMAP(change_flags, ICE_PF_FLAGS_NBITS);
	DECLARE_BITMAP(orig_flags, ICE_PF_FLAGS_NBITS);
	struct ice_vsi *vsi = np->vsi;
	struct ice_pf *pf = vsi->back;
	int ret = 0;
	u32 i;

	if (flags > BIT(ICE_PRIV_FLAG_ARRAY_SIZE))
		return -EINVAL;

	set_bit(ICE_FLAG_ETHTOOL_CTXT, pf->flags);

	bitmap_copy(orig_flags, pf->flags, ICE_PF_FLAGS_NBITS);
	for (i = 0; i < ICE_PRIV_FLAG_ARRAY_SIZE; i++) {
		const struct ice_priv_flag *priv_flag;

		priv_flag = &ice_gstrings_priv_flags[i];

		if (flags & BIT(i))
			set_bit(priv_flag->bitno, pf->flags);
		else
			clear_bit(priv_flag->bitno, pf->flags);
	}

	bitmap_xor(change_flags, pf->flags, orig_flags, ICE_PF_FLAGS_NBITS);

	if (test_bit(ICE_FLAG_ENABLE_FW_LLDP, change_flags)) {
		if (!test_bit(ICE_FLAG_ENABLE_FW_LLDP, pf->flags)) {
			enum ice_status status;

			/* Disable FW LLDP engine */
			status = ice_aq_cfg_lldp_mib_change(&pf->hw, false,
							    NULL);
			/* If unregistering for LLDP events fails, this is
			 * not an error state, as there shouldn't be any
			 * events to respond to.
			 */
			if (status)
				dev_info(&pf->pdev->dev,
					 "Failed to unreg for LLDP events\n");

			/* The AQ call to stop the FW LLDP agent will generate
			 * an error if the agent is already stopped.
			 */
			status = ice_aq_stop_lldp(&pf->hw, true, true, NULL);
			if (status)
				dev_warn(&pf->pdev->dev,
					 "Fail to stop LLDP agent\n");
			/* Use case for having the FW LLDP agent stopped
			 * will likely not need DCB, so failure to init is
			 * not a concern of ethtool
			 */
			status = ice_init_pf_dcb(pf, true);
			if (status)
				dev_warn(&pf->pdev->dev, "Fail to init DCB\n");

			/* Forward LLDP packets to default VSI so that they
			 * are passed up the stack
			 */
			ice_cfg_sw_lldp(vsi, false, true);
		} else {
			enum ice_status status;
			bool dcbx_agent_status;

			/* AQ command to start FW LLDP agent will return an
			 * error if the agent is already started
			 */
			status = ice_aq_start_lldp(&pf->hw, true, NULL);
			if (status)
				dev_warn(&pf->pdev->dev,
					 "Fail to start LLDP Agent\n");

			/* AQ command to start FW DCBX agent will fail if
			 * the agent is already started
			 */
			status = ice_aq_start_stop_dcbx(&pf->hw, true,
							&dcbx_agent_status,
							NULL);
			if (status)
				dev_dbg(&pf->pdev->dev,
					"Failed to start FW DCBX\n");

			dev_info(&pf->pdev->dev, "FW DCBX agent is %s\n",
				 dcbx_agent_status ? "ACTIVE" : "DISABLED");

			/* Failure to configure MIB change or init DCB is not
			 * relevant to ethtool.  Print notification that
			 * registration/init failed but do not return error
			 * state to ethtool
			 */
			status = ice_init_pf_dcb(pf, true);
			if (status)
				dev_dbg(&pf->pdev->dev, "Fail to init DCB\n");

			/* Remove rule to direct LLDP packets to default VSI.
			 * The FW LLDP engine will now be consuming them.
			 */
			ice_cfg_sw_lldp(vsi, false, false);
		}
	}
	clear_bit(ICE_FLAG_ETHTOOL_CTXT, pf->flags);
	return ret;
}

static int ice_get_sset_count(struct net_device *netdev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		/* The number (and order) of strings reported *must* remain
		 * constant for a given netdevice. This function must not
		 * report a different number based on run time parameters
		 * (such as the number of queues in use, or the setting of
		 * a private ethtool flag). This is due to the nature of the
		 * ethtool stats API.
		 *
		 * Userspace programs such as ethtool must make 3 separate
		 * ioctl requests, one for size, one for the strings, and
		 * finally one for the stats. Since these cross into
		 * userspace, changes to the number or size could result in
		 * undefined memory access or incorrect string<->value
		 * correlations for statistics.
		 *
		 * Even if it appears to be safe, changes to the size or
		 * order of strings will suffer from race conditions and are
		 * not safe.
		 */
		return ICE_ALL_STATS_LEN(netdev);
	case ETH_SS_TEST:
		return ICE_TEST_LEN;
	case ETH_SS_PRIV_FLAGS:
		return ICE_PRIV_FLAG_ARRAY_SIZE;
	default:
		return -EOPNOTSUPP;
	}
}

static void
ice_get_ethtool_stats(struct net_device *netdev,
		      struct ethtool_stats __always_unused *stats, u64 *data)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_vsi *vsi = np->vsi;
	struct ice_pf *pf = vsi->back;
	struct ice_ring *ring;
	unsigned int j = 0;
	int i = 0;
	char *p;

	for (j = 0; j < ICE_VSI_STATS_LEN; j++) {
		p = (char *)vsi + ice_gstrings_vsi_stats[j].stat_offset;
		data[i++] = (ice_gstrings_vsi_stats[j].sizeof_stat ==
			    sizeof(u64)) ? *(u64 *)p : *(u32 *)p;
	}

	/* populate per queue stats */
	rcu_read_lock();

	ice_for_each_alloc_txq(vsi, j) {
		ring = READ_ONCE(vsi->tx_rings[j]);
		if (ring) {
			data[i++] = ring->stats.pkts;
			data[i++] = ring->stats.bytes;
		} else {
			data[i++] = 0;
			data[i++] = 0;
		}
	}

	ice_for_each_alloc_rxq(vsi, j) {
		ring = READ_ONCE(vsi->rx_rings[j]);
		if (ring) {
			data[i++] = ring->stats.pkts;
			data[i++] = ring->stats.bytes;
		} else {
			data[i++] = 0;
			data[i++] = 0;
		}
	}

	rcu_read_unlock();

	if (vsi->type != ICE_VSI_PF)
		return;

	for (j = 0; j < ICE_PF_STATS_LEN; j++) {
		p = (char *)pf + ice_gstrings_pf_stats[j].stat_offset;
		data[i++] = (ice_gstrings_pf_stats[j].sizeof_stat ==
			     sizeof(u64)) ? *(u64 *)p : *(u32 *)p;
	}

	for (j = 0; j < ICE_MAX_USER_PRIORITY; j++) {
		data[i++] = pf->stats.priority_xon_tx[j];
		data[i++] = pf->stats.priority_xoff_tx[j];
	}

	for (j = 0; j < ICE_MAX_USER_PRIORITY; j++) {
		data[i++] = pf->stats.priority_xon_rx[j];
		data[i++] = pf->stats.priority_xoff_rx[j];
	}
}

/**
 * ice_phy_type_to_ethtool - convert the phy_types to ethtool link modes
 * @netdev: network interface device structure
 * @ks: ethtool link ksettings struct to fill out
 */
static void
ice_phy_type_to_ethtool(struct net_device *netdev,
			struct ethtool_link_ksettings *ks)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_link_status *hw_link_info;
	bool need_add_adv_mode = false;
	struct ice_vsi *vsi = np->vsi;
	u64 phy_types_high;
	u64 phy_types_low;

	hw_link_info = &vsi->port_info->phy.link_info;
	phy_types_low = vsi->port_info->phy.phy_type_low;
	phy_types_high = vsi->port_info->phy.phy_type_high;

	ethtool_link_ksettings_zero_link_mode(ks, supported);
	ethtool_link_ksettings_zero_link_mode(ks, advertising);

	if (phy_types_low & ICE_PHY_TYPE_LOW_100BASE_TX ||
	    phy_types_low & ICE_PHY_TYPE_LOW_100M_SGMII) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     100baseT_Full);
		if (!hw_link_info->req_speeds ||
		    hw_link_info->req_speeds & ICE_AQ_LINK_SPEED_100MB)
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     100baseT_Full);
	}
	if (phy_types_low & ICE_PHY_TYPE_LOW_1000BASE_T ||
	    phy_types_low & ICE_PHY_TYPE_LOW_1G_SGMII) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     1000baseT_Full);
		if (!hw_link_info->req_speeds ||
		    hw_link_info->req_speeds & ICE_AQ_LINK_SPEED_1000MB)
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     1000baseT_Full);
	}
	if (phy_types_low & ICE_PHY_TYPE_LOW_1000BASE_KX) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     1000baseKX_Full);
		if (!hw_link_info->req_speeds ||
		    hw_link_info->req_speeds & ICE_AQ_LINK_SPEED_1000MB)
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     1000baseKX_Full);
	}
	if (phy_types_low & ICE_PHY_TYPE_LOW_1000BASE_SX ||
	    phy_types_low & ICE_PHY_TYPE_LOW_1000BASE_LX) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     1000baseX_Full);
		if (!hw_link_info->req_speeds ||
		    hw_link_info->req_speeds & ICE_AQ_LINK_SPEED_1000MB)
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     1000baseX_Full);
	}
	if (phy_types_low & ICE_PHY_TYPE_LOW_2500BASE_T) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     2500baseT_Full);
		if (!hw_link_info->req_speeds ||
		    hw_link_info->req_speeds & ICE_AQ_LINK_SPEED_2500MB)
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     2500baseT_Full);
	}
	if (phy_types_low & ICE_PHY_TYPE_LOW_2500BASE_X ||
	    phy_types_low & ICE_PHY_TYPE_LOW_2500BASE_KX) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     2500baseX_Full);
		if (!hw_link_info->req_speeds ||
		    hw_link_info->req_speeds & ICE_AQ_LINK_SPEED_2500MB)
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     2500baseX_Full);
	}
	if (phy_types_low & ICE_PHY_TYPE_LOW_5GBASE_T ||
	    phy_types_low & ICE_PHY_TYPE_LOW_5GBASE_KR) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     5000baseT_Full);
		if (!hw_link_info->req_speeds ||
		    hw_link_info->req_speeds & ICE_AQ_LINK_SPEED_5GB)
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     5000baseT_Full);
	}
	if (phy_types_low & ICE_PHY_TYPE_LOW_10GBASE_T ||
	    phy_types_low & ICE_PHY_TYPE_LOW_10G_SFI_DA ||
	    phy_types_low & ICE_PHY_TYPE_LOW_10G_SFI_AOC_ACC ||
	    phy_types_low & ICE_PHY_TYPE_LOW_10G_SFI_C2C) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     10000baseT_Full);
		if (!hw_link_info->req_speeds ||
		    hw_link_info->req_speeds & ICE_AQ_LINK_SPEED_10GB)
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     10000baseT_Full);
	}
	if (phy_types_low & ICE_PHY_TYPE_LOW_10GBASE_KR_CR1) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     10000baseKR_Full);
		if (!hw_link_info->req_speeds ||
		    hw_link_info->req_speeds & ICE_AQ_LINK_SPEED_10GB)
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     10000baseKR_Full);
	}
	if (phy_types_low & ICE_PHY_TYPE_LOW_10GBASE_SR) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     10000baseSR_Full);
		if (!hw_link_info->req_speeds ||
		    hw_link_info->req_speeds & ICE_AQ_LINK_SPEED_10GB)
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     10000baseSR_Full);
	}
	if (phy_types_low & ICE_PHY_TYPE_LOW_10GBASE_LR) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     10000baseLR_Full);
		if (!hw_link_info->req_speeds ||
		    hw_link_info->req_speeds & ICE_AQ_LINK_SPEED_10GB)
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     10000baseLR_Full);
	}
	if (phy_types_low & ICE_PHY_TYPE_LOW_25GBASE_T ||
	    phy_types_low & ICE_PHY_TYPE_LOW_25GBASE_CR ||
	    phy_types_low & ICE_PHY_TYPE_LOW_25GBASE_CR_S ||
	    phy_types_low & ICE_PHY_TYPE_LOW_25GBASE_CR1 ||
	    phy_types_low & ICE_PHY_TYPE_LOW_25G_AUI_AOC_ACC ||
	    phy_types_low & ICE_PHY_TYPE_LOW_25G_AUI_C2C) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     25000baseCR_Full);
		if (!hw_link_info->req_speeds ||
		    hw_link_info->req_speeds & ICE_AQ_LINK_SPEED_25GB)
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     25000baseCR_Full);
	}
	if (phy_types_low & ICE_PHY_TYPE_LOW_25GBASE_SR ||
	    phy_types_low & ICE_PHY_TYPE_LOW_25GBASE_LR) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     25000baseSR_Full);
		if (!hw_link_info->req_speeds ||
		    hw_link_info->req_speeds & ICE_AQ_LINK_SPEED_25GB)
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     25000baseSR_Full);
	}
	if (phy_types_low & ICE_PHY_TYPE_LOW_25GBASE_KR ||
	    phy_types_low & ICE_PHY_TYPE_LOW_25GBASE_KR_S ||
	    phy_types_low & ICE_PHY_TYPE_LOW_25GBASE_KR1) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     25000baseKR_Full);
		if (!hw_link_info->req_speeds ||
		    hw_link_info->req_speeds & ICE_AQ_LINK_SPEED_25GB)
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     25000baseKR_Full);
	}
	if (phy_types_low & ICE_PHY_TYPE_LOW_40GBASE_KR4) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     40000baseKR4_Full);
		if (!hw_link_info->req_speeds ||
		    hw_link_info->req_speeds & ICE_AQ_LINK_SPEED_40GB)
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     40000baseKR4_Full);
	}
	if (phy_types_low & ICE_PHY_TYPE_LOW_40GBASE_CR4 ||
	    phy_types_low & ICE_PHY_TYPE_LOW_40G_XLAUI_AOC_ACC ||
	    phy_types_low & ICE_PHY_TYPE_LOW_40G_XLAUI) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     40000baseCR4_Full);
		if (!hw_link_info->req_speeds ||
		    hw_link_info->req_speeds & ICE_AQ_LINK_SPEED_40GB)
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     40000baseCR4_Full);
	}
	if (phy_types_low & ICE_PHY_TYPE_LOW_40GBASE_SR4) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     40000baseSR4_Full);
		if (!hw_link_info->req_speeds ||
		    hw_link_info->req_speeds & ICE_AQ_LINK_SPEED_40GB)
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     40000baseSR4_Full);
	}
	if (phy_types_low & ICE_PHY_TYPE_LOW_40GBASE_LR4) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     40000baseLR4_Full);
		if (!hw_link_info->req_speeds ||
		    hw_link_info->req_speeds & ICE_AQ_LINK_SPEED_40GB)
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     40000baseLR4_Full);
	}
	if (phy_types_low & ICE_PHY_TYPE_LOW_50GBASE_CR2 ||
	    phy_types_low & ICE_PHY_TYPE_LOW_50G_LAUI2_AOC_ACC ||
	    phy_types_low & ICE_PHY_TYPE_LOW_50G_LAUI2 ||
	    phy_types_low & ICE_PHY_TYPE_LOW_50G_AUI2_AOC_ACC ||
	    phy_types_low & ICE_PHY_TYPE_LOW_50G_AUI2 ||
	    phy_types_low & ICE_PHY_TYPE_LOW_50GBASE_CP ||
	    phy_types_low & ICE_PHY_TYPE_LOW_50GBASE_SR ||
	    phy_types_low & ICE_PHY_TYPE_LOW_50G_AUI1_AOC_ACC ||
	    phy_types_low & ICE_PHY_TYPE_LOW_50G_AUI1) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     50000baseCR2_Full);
		if (!hw_link_info->req_speeds ||
		    hw_link_info->req_speeds & ICE_AQ_LINK_SPEED_50GB)
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     50000baseCR2_Full);
	}
	if (phy_types_low & ICE_PHY_TYPE_LOW_50GBASE_KR2 ||
	    phy_types_low & ICE_PHY_TYPE_LOW_50GBASE_KR_PAM4) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     50000baseKR2_Full);
		if (!hw_link_info->req_speeds ||
		    hw_link_info->req_speeds & ICE_AQ_LINK_SPEED_50GB)
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     50000baseKR2_Full);
	}
	if (phy_types_low & ICE_PHY_TYPE_LOW_50GBASE_SR2 ||
	    phy_types_low & ICE_PHY_TYPE_LOW_50GBASE_LR2 ||
	    phy_types_low & ICE_PHY_TYPE_LOW_50GBASE_FR ||
	    phy_types_low & ICE_PHY_TYPE_LOW_50GBASE_LR) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     50000baseSR2_Full);
		if (!hw_link_info->req_speeds ||
		    hw_link_info->req_speeds & ICE_AQ_LINK_SPEED_50GB)
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     50000baseSR2_Full);
	}
	if (phy_types_low & ICE_PHY_TYPE_LOW_100GBASE_CR4 ||
	    phy_types_low & ICE_PHY_TYPE_LOW_100G_CAUI4_AOC_ACC ||
	    phy_types_low & ICE_PHY_TYPE_LOW_100G_CAUI4 ||
	    phy_types_low & ICE_PHY_TYPE_LOW_100G_AUI4_AOC_ACC ||
	    phy_types_low & ICE_PHY_TYPE_LOW_100G_AUI4 ||
	    phy_types_low & ICE_PHY_TYPE_LOW_100GBASE_CR_PAM4 ||
	    phy_types_low & ICE_PHY_TYPE_LOW_100GBASE_CP2  ||
	    phy_types_high & ICE_PHY_TYPE_HIGH_100G_CAUI2_AOC_ACC ||
	    phy_types_high & ICE_PHY_TYPE_HIGH_100G_CAUI2 ||
	    phy_types_high & ICE_PHY_TYPE_HIGH_100G_AUI2_AOC_ACC ||
	    phy_types_high & ICE_PHY_TYPE_HIGH_100G_AUI2) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     100000baseCR4_Full);
		if (!hw_link_info->req_speeds ||
		    hw_link_info->req_speeds & ICE_AQ_LINK_SPEED_100GB)
			need_add_adv_mode = true;
	}
	if (need_add_adv_mode) {
		need_add_adv_mode = false;
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     100000baseCR4_Full);
	}
	if (phy_types_low & ICE_PHY_TYPE_LOW_100GBASE_SR4 ||
	    phy_types_low & ICE_PHY_TYPE_LOW_100GBASE_SR2) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     100000baseSR4_Full);
		if (!hw_link_info->req_speeds ||
		    hw_link_info->req_speeds & ICE_AQ_LINK_SPEED_100GB)
			need_add_adv_mode = true;
	}
	if (need_add_adv_mode) {
		need_add_adv_mode = false;
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     100000baseSR4_Full);
	}
	if (phy_types_low & ICE_PHY_TYPE_LOW_100GBASE_LR4 ||
	    phy_types_low & ICE_PHY_TYPE_LOW_100GBASE_DR) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     100000baseLR4_ER4_Full);
		if (!hw_link_info->req_speeds ||
		    hw_link_info->req_speeds & ICE_AQ_LINK_SPEED_100GB)
			need_add_adv_mode = true;
	}
	if (need_add_adv_mode) {
		need_add_adv_mode = false;
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     100000baseLR4_ER4_Full);
	}
	if (phy_types_low & ICE_PHY_TYPE_LOW_100GBASE_KR4 ||
	    phy_types_low & ICE_PHY_TYPE_LOW_100GBASE_KR_PAM4 ||
	    phy_types_high & ICE_PHY_TYPE_HIGH_100GBASE_KR2_PAM4) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     100000baseKR4_Full);
		if (!hw_link_info->req_speeds ||
		    hw_link_info->req_speeds & ICE_AQ_LINK_SPEED_100GB)
			need_add_adv_mode = true;
	}
	if (need_add_adv_mode)
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     100000baseKR4_Full);

	/* Autoneg PHY types */
	if (phy_types_low & ICE_PHY_TYPE_LOW_100BASE_TX ||
	    phy_types_low & ICE_PHY_TYPE_LOW_1000BASE_T ||
	    phy_types_low & ICE_PHY_TYPE_LOW_1000BASE_KX ||
	    phy_types_low & ICE_PHY_TYPE_LOW_2500BASE_T ||
	    phy_types_low & ICE_PHY_TYPE_LOW_2500BASE_KX ||
	    phy_types_low & ICE_PHY_TYPE_LOW_5GBASE_T ||
	    phy_types_low & ICE_PHY_TYPE_LOW_5GBASE_KR ||
	    phy_types_low & ICE_PHY_TYPE_LOW_10GBASE_T ||
	    phy_types_low & ICE_PHY_TYPE_LOW_10GBASE_KR_CR1 ||
	    phy_types_low & ICE_PHY_TYPE_LOW_25GBASE_T ||
	    phy_types_low & ICE_PHY_TYPE_LOW_25GBASE_CR ||
	    phy_types_low & ICE_PHY_TYPE_LOW_25GBASE_CR_S ||
	    phy_types_low & ICE_PHY_TYPE_LOW_25GBASE_CR1 ||
	    phy_types_low & ICE_PHY_TYPE_LOW_25GBASE_KR ||
	    phy_types_low & ICE_PHY_TYPE_LOW_25GBASE_KR_S ||
	    phy_types_low & ICE_PHY_TYPE_LOW_25GBASE_KR1 ||
	    phy_types_low & ICE_PHY_TYPE_LOW_40GBASE_CR4 ||
	    phy_types_low & ICE_PHY_TYPE_LOW_40GBASE_KR4) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     Autoneg);
	}
	if (phy_types_low & ICE_PHY_TYPE_LOW_50GBASE_CR2 ||
	    phy_types_low & ICE_PHY_TYPE_LOW_50GBASE_KR2 ||
	    phy_types_low & ICE_PHY_TYPE_LOW_50GBASE_CP ||
	    phy_types_low & ICE_PHY_TYPE_LOW_50GBASE_KR_PAM4) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     Autoneg);
	}
	if (phy_types_low & ICE_PHY_TYPE_LOW_100GBASE_CR4 ||
	    phy_types_low & ICE_PHY_TYPE_LOW_100GBASE_KR4 ||
	    phy_types_low & ICE_PHY_TYPE_LOW_100GBASE_KR_PAM4 ||
	    phy_types_low & ICE_PHY_TYPE_LOW_100GBASE_CP2) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     Autoneg);
	}
}

#define TEST_SET_BITS_TIMEOUT	50
#define TEST_SET_BITS_SLEEP_MAX	2000
#define TEST_SET_BITS_SLEEP_MIN	1000

/**
 * ice_get_settings_link_up - Get Link settings for when link is up
 * @ks: ethtool ksettings to fill in
 * @netdev: network interface device structure
 */
static void
ice_get_settings_link_up(struct ethtool_link_ksettings *ks,
			 struct net_device *netdev)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ethtool_link_ksettings cap_ksettings;
	struct ice_link_status *link_info;
	struct ice_vsi *vsi = np->vsi;
	bool unrecog_phy_high = false;
	bool unrecog_phy_low = false;

	link_info = &vsi->port_info->phy.link_info;

	/* Initialize supported and advertised settings based on PHY settings */
	switch (link_info->phy_type_low) {
	case ICE_PHY_TYPE_LOW_100BASE_TX:
		ethtool_link_ksettings_add_link_mode(ks, supported, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     100baseT_Full);
		ethtool_link_ksettings_add_link_mode(ks, advertising, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     100baseT_Full);
		break;
	case ICE_PHY_TYPE_LOW_100M_SGMII:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     100baseT_Full);
		break;
	case ICE_PHY_TYPE_LOW_1000BASE_T:
		ethtool_link_ksettings_add_link_mode(ks, supported, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     1000baseT_Full);
		ethtool_link_ksettings_add_link_mode(ks, advertising, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     1000baseT_Full);
		break;
	case ICE_PHY_TYPE_LOW_1G_SGMII:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     1000baseT_Full);
		break;
	case ICE_PHY_TYPE_LOW_1000BASE_SX:
	case ICE_PHY_TYPE_LOW_1000BASE_LX:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     1000baseX_Full);
		break;
	case ICE_PHY_TYPE_LOW_1000BASE_KX:
		ethtool_link_ksettings_add_link_mode(ks, supported, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     1000baseKX_Full);
		ethtool_link_ksettings_add_link_mode(ks, advertising, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     1000baseKX_Full);
		break;
	case ICE_PHY_TYPE_LOW_2500BASE_T:
		ethtool_link_ksettings_add_link_mode(ks, supported, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, advertising, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     2500baseT_Full);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     2500baseT_Full);
		break;
	case ICE_PHY_TYPE_LOW_2500BASE_X:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     2500baseX_Full);
		break;
	case ICE_PHY_TYPE_LOW_2500BASE_KX:
		ethtool_link_ksettings_add_link_mode(ks, supported, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     2500baseX_Full);
		ethtool_link_ksettings_add_link_mode(ks, advertising, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     2500baseX_Full);
		break;
	case ICE_PHY_TYPE_LOW_5GBASE_T:
	case ICE_PHY_TYPE_LOW_5GBASE_KR:
		ethtool_link_ksettings_add_link_mode(ks, supported, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     5000baseT_Full);
		ethtool_link_ksettings_add_link_mode(ks, advertising, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     5000baseT_Full);
		break;
	case ICE_PHY_TYPE_LOW_10GBASE_T:
		ethtool_link_ksettings_add_link_mode(ks, supported, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     10000baseT_Full);
		ethtool_link_ksettings_add_link_mode(ks, advertising, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     10000baseT_Full);
		break;
	case ICE_PHY_TYPE_LOW_10G_SFI_DA:
	case ICE_PHY_TYPE_LOW_10G_SFI_AOC_ACC:
	case ICE_PHY_TYPE_LOW_10G_SFI_C2C:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     10000baseT_Full);
		break;
	case ICE_PHY_TYPE_LOW_10GBASE_SR:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     10000baseSR_Full);
		break;
	case ICE_PHY_TYPE_LOW_10GBASE_LR:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     10000baseLR_Full);
		break;
	case ICE_PHY_TYPE_LOW_10GBASE_KR_CR1:
		ethtool_link_ksettings_add_link_mode(ks, supported, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     10000baseKR_Full);
		ethtool_link_ksettings_add_link_mode(ks, advertising, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     10000baseKR_Full);
		break;
	case ICE_PHY_TYPE_LOW_25GBASE_T:
	case ICE_PHY_TYPE_LOW_25GBASE_CR:
	case ICE_PHY_TYPE_LOW_25GBASE_CR_S:
	case ICE_PHY_TYPE_LOW_25GBASE_CR1:
		ethtool_link_ksettings_add_link_mode(ks, supported, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     25000baseCR_Full);
		ethtool_link_ksettings_add_link_mode(ks, advertising, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     25000baseCR_Full);
		break;
	case ICE_PHY_TYPE_LOW_25G_AUI_AOC_ACC:
	case ICE_PHY_TYPE_LOW_25G_AUI_C2C:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     25000baseCR_Full);
		break;
	case ICE_PHY_TYPE_LOW_25GBASE_SR:
	case ICE_PHY_TYPE_LOW_25GBASE_LR:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     25000baseSR_Full);
		break;
	case ICE_PHY_TYPE_LOW_25GBASE_KR:
	case ICE_PHY_TYPE_LOW_25GBASE_KR1:
	case ICE_PHY_TYPE_LOW_25GBASE_KR_S:
		ethtool_link_ksettings_add_link_mode(ks, supported, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     25000baseKR_Full);
		ethtool_link_ksettings_add_link_mode(ks, advertising, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     25000baseKR_Full);
		break;
	case ICE_PHY_TYPE_LOW_40GBASE_CR4:
		ethtool_link_ksettings_add_link_mode(ks, supported, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     40000baseCR4_Full);
		ethtool_link_ksettings_add_link_mode(ks, advertising, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     40000baseCR4_Full);
		break;
	case ICE_PHY_TYPE_LOW_40G_XLAUI_AOC_ACC:
	case ICE_PHY_TYPE_LOW_40G_XLAUI:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     40000baseCR4_Full);
		break;
	case ICE_PHY_TYPE_LOW_40GBASE_SR4:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     40000baseSR4_Full);
		break;
	case ICE_PHY_TYPE_LOW_40GBASE_LR4:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     40000baseLR4_Full);
		break;
	case ICE_PHY_TYPE_LOW_40GBASE_KR4:
		ethtool_link_ksettings_add_link_mode(ks, supported, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     40000baseKR4_Full);
		ethtool_link_ksettings_add_link_mode(ks, advertising, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     40000baseKR4_Full);
		break;
	case ICE_PHY_TYPE_LOW_50GBASE_CR2:
	case ICE_PHY_TYPE_LOW_50GBASE_CP:
		ethtool_link_ksettings_add_link_mode(ks, supported, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     50000baseCR2_Full);
		ethtool_link_ksettings_add_link_mode(ks, advertising, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     50000baseCR2_Full);
		break;
	case ICE_PHY_TYPE_LOW_50G_LAUI2_AOC_ACC:
	case ICE_PHY_TYPE_LOW_50G_LAUI2:
	case ICE_PHY_TYPE_LOW_50G_AUI2_AOC_ACC:
	case ICE_PHY_TYPE_LOW_50G_AUI2:
	case ICE_PHY_TYPE_LOW_50GBASE_SR:
	case ICE_PHY_TYPE_LOW_50G_AUI1_AOC_ACC:
	case ICE_PHY_TYPE_LOW_50G_AUI1:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     50000baseCR2_Full);
		break;
	case ICE_PHY_TYPE_LOW_50GBASE_KR2:
	case ICE_PHY_TYPE_LOW_50GBASE_KR_PAM4:
		ethtool_link_ksettings_add_link_mode(ks, supported, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     50000baseKR2_Full);
		ethtool_link_ksettings_add_link_mode(ks, advertising, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     50000baseKR2_Full);
		break;
	case ICE_PHY_TYPE_LOW_50GBASE_SR2:
	case ICE_PHY_TYPE_LOW_50GBASE_LR2:
	case ICE_PHY_TYPE_LOW_50GBASE_FR:
	case ICE_PHY_TYPE_LOW_50GBASE_LR:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     50000baseSR2_Full);
		break;
	case ICE_PHY_TYPE_LOW_100GBASE_CR4:
		ethtool_link_ksettings_add_link_mode(ks, supported, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     100000baseCR4_Full);
		ethtool_link_ksettings_add_link_mode(ks, advertising, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     100000baseCR4_Full);
		break;
	case ICE_PHY_TYPE_LOW_100G_CAUI4_AOC_ACC:
	case ICE_PHY_TYPE_LOW_100G_CAUI4:
	case ICE_PHY_TYPE_LOW_100G_AUI4_AOC_ACC:
	case ICE_PHY_TYPE_LOW_100G_AUI4:
	case ICE_PHY_TYPE_LOW_100GBASE_CR_PAM4:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     100000baseCR4_Full);
		break;
	case ICE_PHY_TYPE_LOW_100GBASE_CP2:
		ethtool_link_ksettings_add_link_mode(ks, supported, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     100000baseCR4_Full);
		ethtool_link_ksettings_add_link_mode(ks, advertising, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     100000baseCR4_Full);
		break;
	case ICE_PHY_TYPE_LOW_100GBASE_SR4:
	case ICE_PHY_TYPE_LOW_100GBASE_SR2:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     100000baseSR4_Full);
		break;
	case ICE_PHY_TYPE_LOW_100GBASE_LR4:
	case ICE_PHY_TYPE_LOW_100GBASE_DR:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     100000baseLR4_ER4_Full);
		break;
	case ICE_PHY_TYPE_LOW_100GBASE_KR4:
	case ICE_PHY_TYPE_LOW_100GBASE_KR_PAM4:
		ethtool_link_ksettings_add_link_mode(ks, supported, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     100000baseKR4_Full);
		ethtool_link_ksettings_add_link_mode(ks, advertising, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     100000baseKR4_Full);
		break;
	default:
		unrecog_phy_low = true;
	}

	switch (link_info->phy_type_high) {
	case ICE_PHY_TYPE_HIGH_100GBASE_KR2_PAM4:
		ethtool_link_ksettings_add_link_mode(ks, supported, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     100000baseKR4_Full);
		ethtool_link_ksettings_add_link_mode(ks, advertising, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     100000baseKR4_Full);
		break;
	case ICE_PHY_TYPE_HIGH_100G_CAUI2_AOC_ACC:
	case ICE_PHY_TYPE_HIGH_100G_CAUI2:
	case ICE_PHY_TYPE_HIGH_100G_AUI2_AOC_ACC:
	case ICE_PHY_TYPE_HIGH_100G_AUI2:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     100000baseCR4_Full);
		break;
	default:
		unrecog_phy_high = true;
	}

	if (unrecog_phy_low && unrecog_phy_high) {
		/* if we got here and link is up something bad is afoot */
		netdev_info(netdev,
			    "WARNING: Unrecognized PHY_Low (0x%llx).\n",
			    (u64)link_info->phy_type_low);
		netdev_info(netdev,
			    "WARNING: Unrecognized PHY_High (0x%llx).\n",
			    (u64)link_info->phy_type_high);
	}

	/* Now that we've worked out everything that could be supported by the
	 * current PHY type, get what is supported by the NVM and intersect
	 * them to get what is truly supported
	 */
	memset(&cap_ksettings, 0, sizeof(cap_ksettings));
	ice_phy_type_to_ethtool(netdev, &cap_ksettings);
	ethtool_intersect_link_masks(ks, &cap_ksettings);

	switch (link_info->link_speed) {
	case ICE_AQ_LINK_SPEED_100GB:
		ks->base.speed = SPEED_100000;
		break;
	case ICE_AQ_LINK_SPEED_50GB:
		ks->base.speed = SPEED_50000;
		break;
	case ICE_AQ_LINK_SPEED_40GB:
		ks->base.speed = SPEED_40000;
		break;
	case ICE_AQ_LINK_SPEED_25GB:
		ks->base.speed = SPEED_25000;
		break;
	case ICE_AQ_LINK_SPEED_20GB:
		ks->base.speed = SPEED_20000;
		break;
	case ICE_AQ_LINK_SPEED_10GB:
		ks->base.speed = SPEED_10000;
		break;
	case ICE_AQ_LINK_SPEED_5GB:
		ks->base.speed = SPEED_5000;
		break;
	case ICE_AQ_LINK_SPEED_2500MB:
		ks->base.speed = SPEED_2500;
		break;
	case ICE_AQ_LINK_SPEED_1000MB:
		ks->base.speed = SPEED_1000;
		break;
	case ICE_AQ_LINK_SPEED_100MB:
		ks->base.speed = SPEED_100;
		break;
	default:
		netdev_info(netdev,
			    "WARNING: Unrecognized link_speed (0x%x).\n",
			    link_info->link_speed);
		break;
	}
	ks->base.duplex = DUPLEX_FULL;
}

/**
 * ice_get_settings_link_down - Get the Link settings when link is down
 * @ks: ethtool ksettings to fill in
 * @netdev: network interface device structure
 *
 * Reports link settings that can be determined when link is down
 */
static void
ice_get_settings_link_down(struct ethtool_link_ksettings *ks,
			   struct net_device *netdev)
{
	/* link is down and the driver needs to fall back on
	 * supported PHY types to figure out what info to display
	 */
	ice_phy_type_to_ethtool(netdev, ks);

	/* With no link, speed and duplex are unknown */
	ks->base.speed = SPEED_UNKNOWN;
	ks->base.duplex = DUPLEX_UNKNOWN;
}

/**
 * ice_get_link_ksettings - Get Link Speed and Duplex settings
 * @netdev: network interface device structure
 * @ks: ethtool ksettings
 *
 * Reports speed/duplex settings based on media_type
 */
static int
ice_get_link_ksettings(struct net_device *netdev,
		       struct ethtool_link_ksettings *ks)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_aqc_get_phy_caps_data *caps;
	struct ice_link_status *hw_link_info;
	struct ice_vsi *vsi = np->vsi;

	ethtool_link_ksettings_zero_link_mode(ks, supported);
	ethtool_link_ksettings_zero_link_mode(ks, advertising);
	hw_link_info = &vsi->port_info->phy.link_info;

	/* set speed and duplex */
	if (hw_link_info->link_info & ICE_AQ_LINK_UP)
		ice_get_settings_link_up(ks, netdev);
	else
		ice_get_settings_link_down(ks, netdev);

	/* set autoneg settings */
	ks->base.autoneg = (hw_link_info->an_info & ICE_AQ_AN_COMPLETED) ?
		AUTONEG_ENABLE : AUTONEG_DISABLE;

	/* set media type settings */
	switch (vsi->port_info->phy.media_type) {
	case ICE_MEDIA_FIBER:
		ethtool_link_ksettings_add_link_mode(ks, supported, FIBRE);
		ks->base.port = PORT_FIBRE;
		break;
	case ICE_MEDIA_BASET:
		ethtool_link_ksettings_add_link_mode(ks, supported, TP);
		ethtool_link_ksettings_add_link_mode(ks, advertising, TP);
		ks->base.port = PORT_TP;
		break;
	case ICE_MEDIA_BACKPLANE:
		ethtool_link_ksettings_add_link_mode(ks, supported, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, supported, Backplane);
		ethtool_link_ksettings_add_link_mode(ks, advertising, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     Backplane);
		ks->base.port = PORT_NONE;
		break;
	case ICE_MEDIA_DA:
		ethtool_link_ksettings_add_link_mode(ks, supported, FIBRE);
		ethtool_link_ksettings_add_link_mode(ks, advertising, FIBRE);
		ks->base.port = PORT_DA;
		break;
	default:
		ks->base.port = PORT_OTHER;
		break;
	}

	/* flow control is symmetric and always supported */
	ethtool_link_ksettings_add_link_mode(ks, supported, Pause);

	switch (vsi->port_info->fc.req_mode) {
	case ICE_FC_FULL:
		ethtool_link_ksettings_add_link_mode(ks, advertising, Pause);
		break;
	case ICE_FC_TX_PAUSE:
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     Asym_Pause);
		break;
	case ICE_FC_RX_PAUSE:
		ethtool_link_ksettings_add_link_mode(ks, advertising, Pause);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     Asym_Pause);
		break;
	case ICE_FC_PFC:
	default:
		ethtool_link_ksettings_del_link_mode(ks, advertising, Pause);
		ethtool_link_ksettings_del_link_mode(ks, advertising,
						     Asym_Pause);
		break;
	}

	caps = devm_kzalloc(&vsi->back->pdev->dev, sizeof(*caps), GFP_KERNEL);
	if (!caps)
		goto done;

	if (ice_aq_get_phy_caps(vsi->port_info, false, ICE_AQC_REPORT_TOPO_CAP,
				caps, NULL))
		netdev_info(netdev, "Get phy capability failed.\n");

	/* Set supported FEC modes based on PHY capability */
	ethtool_link_ksettings_add_link_mode(ks, supported, FEC_NONE);

	if (caps->link_fec_options & ICE_AQC_PHY_FEC_10G_KR_40G_KR4_EN ||
	    caps->link_fec_options & ICE_AQC_PHY_FEC_25G_KR_CLAUSE74_EN)
		ethtool_link_ksettings_add_link_mode(ks, supported, FEC_BASER);
	if (caps->link_fec_options & ICE_AQC_PHY_FEC_25G_RS_CLAUSE91_EN)
		ethtool_link_ksettings_add_link_mode(ks, supported, FEC_RS);

	if (ice_aq_get_phy_caps(vsi->port_info, false, ICE_AQC_REPORT_SW_CFG,
				caps, NULL))
		netdev_info(netdev, "Get phy capability failed.\n");

	/* Set advertised FEC modes based on PHY capability */
	ethtool_link_ksettings_add_link_mode(ks, advertising, FEC_NONE);

	if (caps->link_fec_options & ICE_AQC_PHY_FEC_10G_KR_40G_KR4_REQ ||
	    caps->link_fec_options & ICE_AQC_PHY_FEC_25G_KR_REQ)
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     FEC_BASER);
	if (caps->link_fec_options & ICE_AQC_PHY_FEC_25G_RS_528_REQ ||
	    caps->link_fec_options & ICE_AQC_PHY_FEC_25G_RS_544_REQ)
		ethtool_link_ksettings_add_link_mode(ks, advertising, FEC_RS);

done:
	devm_kfree(&vsi->back->pdev->dev, caps);
	return 0;
}

/**
 * ice_ksettings_find_adv_link_speed - Find advertising link speed
 * @ks: ethtool ksettings
 */
static u16
ice_ksettings_find_adv_link_speed(const struct ethtool_link_ksettings *ks)
{
	u16 adv_link_speed = 0;

	if (ethtool_link_ksettings_test_link_mode(ks, advertising,
						  100baseT_Full))
		adv_link_speed |= ICE_AQ_LINK_SPEED_100MB;
	if (ethtool_link_ksettings_test_link_mode(ks, advertising,
						  1000baseX_Full))
		adv_link_speed |= ICE_AQ_LINK_SPEED_1000MB;
	if (ethtool_link_ksettings_test_link_mode(ks, advertising,
						  1000baseT_Full) ||
	    ethtool_link_ksettings_test_link_mode(ks, advertising,
						  1000baseKX_Full))
		adv_link_speed |= ICE_AQ_LINK_SPEED_1000MB;
	if (ethtool_link_ksettings_test_link_mode(ks, advertising,
						  2500baseT_Full))
		adv_link_speed |= ICE_AQ_LINK_SPEED_2500MB;
	if (ethtool_link_ksettings_test_link_mode(ks, advertising,
						  2500baseX_Full))
		adv_link_speed |= ICE_AQ_LINK_SPEED_2500MB;
	if (ethtool_link_ksettings_test_link_mode(ks, advertising,
						  5000baseT_Full))
		adv_link_speed |= ICE_AQ_LINK_SPEED_5GB;
	if (ethtool_link_ksettings_test_link_mode(ks, advertising,
						  10000baseT_Full) ||
	    ethtool_link_ksettings_test_link_mode(ks, advertising,
						  10000baseKR_Full))
		adv_link_speed |= ICE_AQ_LINK_SPEED_10GB;
	if (ethtool_link_ksettings_test_link_mode(ks, advertising,
						  10000baseSR_Full) ||
	    ethtool_link_ksettings_test_link_mode(ks, advertising,
						  10000baseLR_Full))
		adv_link_speed |= ICE_AQ_LINK_SPEED_10GB;
	if (ethtool_link_ksettings_test_link_mode(ks, advertising,
						  25000baseCR_Full) ||
	    ethtool_link_ksettings_test_link_mode(ks, advertising,
						  25000baseSR_Full) ||
	    ethtool_link_ksettings_test_link_mode(ks, advertising,
						  25000baseKR_Full))
		adv_link_speed |= ICE_AQ_LINK_SPEED_25GB;
	if (ethtool_link_ksettings_test_link_mode(ks, advertising,
						  40000baseCR4_Full) ||
	    ethtool_link_ksettings_test_link_mode(ks, advertising,
						  40000baseSR4_Full) ||
	    ethtool_link_ksettings_test_link_mode(ks, advertising,
						  40000baseLR4_Full) ||
	    ethtool_link_ksettings_test_link_mode(ks, advertising,
						  40000baseKR4_Full))
		adv_link_speed |= ICE_AQ_LINK_SPEED_40GB;
	if (ethtool_link_ksettings_test_link_mode(ks, advertising,
						  50000baseCR2_Full) ||
	    ethtool_link_ksettings_test_link_mode(ks, advertising,
						  50000baseKR2_Full))
		adv_link_speed |= ICE_AQ_LINK_SPEED_50GB;
	if (ethtool_link_ksettings_test_link_mode(ks, advertising,
						  50000baseSR2_Full))
		adv_link_speed |= ICE_AQ_LINK_SPEED_50GB;
	if (ethtool_link_ksettings_test_link_mode(ks, advertising,
						  100000baseCR4_Full) ||
	    ethtool_link_ksettings_test_link_mode(ks, advertising,
						  100000baseSR4_Full) ||
	    ethtool_link_ksettings_test_link_mode(ks, advertising,
						  100000baseLR4_ER4_Full) ||
	    ethtool_link_ksettings_test_link_mode(ks, advertising,
						  100000baseKR4_Full))
		adv_link_speed |= ICE_AQ_LINK_SPEED_100GB;

	return adv_link_speed;
}

/**
 * ice_setup_autoneg
 * @p: port info
 * @ks: ethtool_link_ksettings
 * @config: configuration that will be sent down to FW
 * @autoneg_enabled: autonegotiation is enabled or not
 * @autoneg_changed: will there a change in autonegotiation
 * @netdev: network interface device structure
 *
 * Setup PHY autonegotiation feature
 */
static int
ice_setup_autoneg(struct ice_port_info *p, struct ethtool_link_ksettings *ks,
		  struct ice_aqc_set_phy_cfg_data *config,
		  u8 autoneg_enabled, u8 *autoneg_changed,
		  struct net_device *netdev)
{
	int err = 0;

	*autoneg_changed = 0;

	/* Check autoneg */
	if (autoneg_enabled == AUTONEG_ENABLE) {
		/* If autoneg was not already enabled */
		if (!(p->phy.link_info.an_info & ICE_AQ_AN_COMPLETED)) {
			/* If autoneg is not supported, return error */
			if (!ethtool_link_ksettings_test_link_mode(ks,
								   supported,
								   Autoneg)) {
				netdev_info(netdev, "Autoneg not supported on this phy.\n");
				err = -EINVAL;
			} else {
				/* Autoneg is allowed to change */
				config->caps |= ICE_AQ_PHY_ENA_AUTO_LINK_UPDT;
				*autoneg_changed = 1;
			}
		}
	} else {
		/* If autoneg is currently enabled */
		if (p->phy.link_info.an_info & ICE_AQ_AN_COMPLETED) {
			/* If autoneg is supported 10GBASE_T is the only PHY
			 * that can disable it, so otherwise return error
			 */
			if (ethtool_link_ksettings_test_link_mode(ks,
								  supported,
								  Autoneg)) {
				netdev_info(netdev, "Autoneg cannot be disabled on this phy\n");
				err = -EINVAL;
			} else {
				/* Autoneg is allowed to change */
				config->caps &= ~ICE_AQ_PHY_ENA_AUTO_LINK_UPDT;
				*autoneg_changed = 1;
			}
		}
	}

	return err;
}

/**
 * ice_set_link_ksettings - Set Speed and Duplex
 * @netdev: network interface device structure
 * @ks: ethtool ksettings
 *
 * Set speed/duplex per media_types advertised/forced
 */
static int
ice_set_link_ksettings(struct net_device *netdev,
		       const struct ethtool_link_ksettings *ks)
{
	u8 autoneg, timeout = TEST_SET_BITS_TIMEOUT, lport = 0;
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ethtool_link_ksettings safe_ks, copy_ks;
	struct ice_aqc_get_phy_caps_data *abilities;
	u16 adv_link_speed, curr_link_speed, idx;
	struct ice_aqc_set_phy_cfg_data config;
	struct ice_pf *pf = np->vsi->back;
	struct ice_port_info *p;
	u8 autoneg_changed = 0;
	enum ice_status status;
	u64 phy_type_high;
	u64 phy_type_low;
	int err = 0;
	bool linkup;

	p = np->vsi->port_info;

	if (!p)
		return -EOPNOTSUPP;

	/* Check if this is LAN VSI */
	ice_for_each_vsi(pf, idx)
		if (pf->vsi[idx]->type == ICE_VSI_PF) {
			if (np->vsi != pf->vsi[idx])
				return -EOPNOTSUPP;
			break;
		}

	if (p->phy.media_type != ICE_MEDIA_BASET &&
	    p->phy.media_type != ICE_MEDIA_FIBER &&
	    p->phy.media_type != ICE_MEDIA_BACKPLANE &&
	    p->phy.media_type != ICE_MEDIA_DA &&
	    p->phy.link_info.link_info & ICE_AQ_LINK_UP)
		return -EOPNOTSUPP;

	/* copy the ksettings to copy_ks to avoid modifying the original */
	memcpy(&copy_ks, ks, sizeof(copy_ks));

	/* save autoneg out of ksettings */
	autoneg = copy_ks.base.autoneg;

	memset(&safe_ks, 0, sizeof(safe_ks));

	/* Get link modes supported by hardware.*/
	ice_phy_type_to_ethtool(netdev, &safe_ks);

	/* and check against modes requested by user.
	 * Return an error if unsupported mode was set.
	 */
	if (!bitmap_subset(copy_ks.link_modes.advertising,
			   safe_ks.link_modes.supported,
			   __ETHTOOL_LINK_MODE_MASK_NBITS))
		return -EINVAL;

	/* get our own copy of the bits to check against */
	memset(&safe_ks, 0, sizeof(safe_ks));
	safe_ks.base.cmd = copy_ks.base.cmd;
	safe_ks.base.link_mode_masks_nwords =
		copy_ks.base.link_mode_masks_nwords;
	ice_get_link_ksettings(netdev, &safe_ks);

	/* set autoneg back to what it currently is */
	copy_ks.base.autoneg = safe_ks.base.autoneg;
	/* we don't compare the speed */
	copy_ks.base.speed = safe_ks.base.speed;

	/* If copy_ks.base and safe_ks.base are not the same now, then they are
	 * trying to set something that we do not support.
	 */
	if (memcmp(&copy_ks.base, &safe_ks.base, sizeof(copy_ks.base)))
		return -EOPNOTSUPP;

	while (test_and_set_bit(__ICE_CFG_BUSY, pf->state)) {
		timeout--;
		if (!timeout)
			return -EBUSY;
		usleep_range(TEST_SET_BITS_SLEEP_MIN, TEST_SET_BITS_SLEEP_MAX);
	}

	abilities = devm_kzalloc(&pf->pdev->dev, sizeof(*abilities),
				 GFP_KERNEL);
	if (!abilities)
		return -ENOMEM;

	/* Get the current PHY config */
	status = ice_aq_get_phy_caps(p, false, ICE_AQC_REPORT_SW_CFG, abilities,
				     NULL);
	if (status) {
		err = -EAGAIN;
		goto done;
	}

	/* Copy abilities to config in case autoneg is not set below */
	memset(&config, 0, sizeof(config));
	config.caps = abilities->caps & ~ICE_AQC_PHY_AN_MODE;
	if (abilities->caps & ICE_AQC_PHY_AN_MODE)
		config.caps |= ICE_AQ_PHY_ENA_AUTO_LINK_UPDT;

	/* Check autoneg */
	err = ice_setup_autoneg(p, &safe_ks, &config, autoneg, &autoneg_changed,
				netdev);

	if (err)
		goto done;

	/* Call to get the current link speed */
	p->phy.get_link_info = true;
	status = ice_get_link_status(p, &linkup);
	if (status) {
		err = -EAGAIN;
		goto done;
	}

	curr_link_speed = p->phy.link_info.link_speed;
	adv_link_speed = ice_ksettings_find_adv_link_speed(ks);

	/* If speed didn't get set, set it to what it currently is.
	 * This is needed because if advertise is 0 (as it is when autoneg
	 * is disabled) then speed won't get set.
	 */
	if (!adv_link_speed)
		adv_link_speed = curr_link_speed;

	/* Convert the advertise link speeds to their corresponded PHY_TYPE */
	ice_update_phy_type(&phy_type_low, &phy_type_high, adv_link_speed);

	if (!autoneg_changed && adv_link_speed == curr_link_speed) {
		netdev_info(netdev, "Nothing changed, exiting without setting anything.\n");
		goto done;
	}

	/* copy over the rest of the abilities */
	config.low_power_ctrl = abilities->low_power_ctrl;
	config.eee_cap = abilities->eee_cap;
	config.eeer_value = abilities->eeer_value;
	config.link_fec_opt = abilities->link_fec_options;

	/* save the requested speeds */
	p->phy.link_info.req_speeds = adv_link_speed;

	/* set link and auto negotiation so changes take effect */
	config.caps |= ICE_AQ_PHY_ENA_LINK;

	if (phy_type_low || phy_type_high) {
		config.phy_type_high = cpu_to_le64(phy_type_high) &
			abilities->phy_type_high;
		config.phy_type_low = cpu_to_le64(phy_type_low) &
			abilities->phy_type_low;
	} else {
		err = -EAGAIN;
		netdev_info(netdev, "Nothing changed. No PHY_TYPE is corresponded to advertised link speed.\n");
		goto done;
	}

	/* If link is up put link down */
	if (p->phy.link_info.link_info & ICE_AQ_LINK_UP) {
		/* Tell the OS link is going down, the link will go
		 * back up when fw says it is ready asynchronously
		 */
		ice_print_link_msg(np->vsi, false);
		netif_carrier_off(netdev);
		netif_tx_stop_all_queues(netdev);
	}

	/* make the aq call */
	status = ice_aq_set_phy_cfg(&pf->hw, lport, &config, NULL);
	if (status) {
		netdev_info(netdev, "Set phy config failed,\n");
		err = -EAGAIN;
	}

done:
	devm_kfree(&pf->pdev->dev, abilities);
	clear_bit(__ICE_CFG_BUSY, pf->state);

	return err;
}

/**
 * ice_get_rxnfc - command to get Rx flow classification rules
 * @netdev: network interface device structure
 * @cmd: ethtool rxnfc command
 * @rule_locs: buffer to rturn Rx flow classification rules
 *
 * Returns Success if the command is supported.
 */
static int
ice_get_rxnfc(struct net_device *netdev, struct ethtool_rxnfc *cmd,
	      u32 __always_unused *rule_locs)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_vsi *vsi = np->vsi;
	int ret = -EOPNOTSUPP;

	switch (cmd->cmd) {
	case ETHTOOL_GRXRINGS:
		cmd->data = vsi->rss_size;
		ret = 0;
		break;
	default:
		break;
	}

	return ret;
}

static void
ice_get_ringparam(struct net_device *netdev, struct ethtool_ringparam *ring)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_vsi *vsi = np->vsi;

	ring->rx_max_pending = ICE_MAX_NUM_DESC;
	ring->tx_max_pending = ICE_MAX_NUM_DESC;
	ring->rx_pending = vsi->rx_rings[0]->count;
	ring->tx_pending = vsi->tx_rings[0]->count;

	/* Rx mini and jumbo rings are not supported */
	ring->rx_mini_max_pending = 0;
	ring->rx_jumbo_max_pending = 0;
	ring->rx_mini_pending = 0;
	ring->rx_jumbo_pending = 0;
}

static int
ice_set_ringparam(struct net_device *netdev, struct ethtool_ringparam *ring)
{
	struct ice_ring *tx_rings = NULL, *rx_rings = NULL;
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_vsi *vsi = np->vsi;
	struct ice_pf *pf = vsi->back;
	int i, timeout = 50, err = 0;
	u32 new_rx_cnt, new_tx_cnt;

	if (ring->tx_pending > ICE_MAX_NUM_DESC ||
	    ring->tx_pending < ICE_MIN_NUM_DESC ||
	    ring->rx_pending > ICE_MAX_NUM_DESC ||
	    ring->rx_pending < ICE_MIN_NUM_DESC) {
		netdev_err(netdev, "Descriptors requested (Tx: %d / Rx: %d) out of range [%d-%d] (increment %d)\n",
			   ring->tx_pending, ring->rx_pending,
			   ICE_MIN_NUM_DESC, ICE_MAX_NUM_DESC,
			   ICE_REQ_DESC_MULTIPLE);
		return -EINVAL;
	}

	new_tx_cnt = ALIGN(ring->tx_pending, ICE_REQ_DESC_MULTIPLE);
	if (new_tx_cnt != ring->tx_pending)
		netdev_info(netdev,
			    "Requested Tx descriptor count rounded up to %d\n",
			    new_tx_cnt);
	new_rx_cnt = ALIGN(ring->rx_pending, ICE_REQ_DESC_MULTIPLE);
	if (new_rx_cnt != ring->rx_pending)
		netdev_info(netdev,
			    "Requested Rx descriptor count rounded up to %d\n",
			    new_rx_cnt);

	/* if nothing to do return success */
	if (new_tx_cnt == vsi->tx_rings[0]->count &&
	    new_rx_cnt == vsi->rx_rings[0]->count) {
		netdev_dbg(netdev, "Nothing to change, descriptor count is same as requested\n");
		return 0;
	}

	while (test_and_set_bit(__ICE_CFG_BUSY, pf->state)) {
		timeout--;
		if (!timeout)
			return -EBUSY;
		usleep_range(1000, 2000);
	}

	/* set for the next time the netdev is started */
	if (!netif_running(vsi->netdev)) {
		for (i = 0; i < vsi->alloc_txq; i++)
			vsi->tx_rings[i]->count = new_tx_cnt;
		for (i = 0; i < vsi->alloc_rxq; i++)
			vsi->rx_rings[i]->count = new_rx_cnt;
		netdev_dbg(netdev, "Link is down, descriptor count change happens when link is brought up\n");
		goto done;
	}

	if (new_tx_cnt == vsi->tx_rings[0]->count)
		goto process_rx;

	/* alloc updated Tx resources */
	netdev_info(netdev, "Changing Tx descriptor count from %d to %d\n",
		    vsi->tx_rings[0]->count, new_tx_cnt);

	tx_rings = devm_kcalloc(&pf->pdev->dev, vsi->alloc_txq,
				sizeof(*tx_rings), GFP_KERNEL);
	if (!tx_rings) {
		err = -ENOMEM;
		goto done;
	}

	for (i = 0; i < vsi->alloc_txq; i++) {
		/* clone ring and setup updated count */
		tx_rings[i] = *vsi->tx_rings[i];
		tx_rings[i].count = new_tx_cnt;
		tx_rings[i].desc = NULL;
		tx_rings[i].tx_buf = NULL;
		err = ice_setup_tx_ring(&tx_rings[i]);
		if (err) {
			while (i) {
				i--;
				ice_clean_tx_ring(&tx_rings[i]);
			}
			devm_kfree(&pf->pdev->dev, tx_rings);
			goto done;
		}
	}

process_rx:
	if (new_rx_cnt == vsi->rx_rings[0]->count)
		goto process_link;

	/* alloc updated Rx resources */
	netdev_info(netdev, "Changing Rx descriptor count from %d to %d\n",
		    vsi->rx_rings[0]->count, new_rx_cnt);

	rx_rings = devm_kcalloc(&pf->pdev->dev, vsi->alloc_rxq,
				sizeof(*rx_rings), GFP_KERNEL);
	if (!rx_rings) {
		err = -ENOMEM;
		goto done;
	}

	for (i = 0; i < vsi->alloc_rxq; i++) {
		/* clone ring and setup updated count */
		rx_rings[i] = *vsi->rx_rings[i];
		rx_rings[i].count = new_rx_cnt;
		rx_rings[i].desc = NULL;
		rx_rings[i].rx_buf = NULL;
		/* this is to allow wr32 to have something to write to
		 * during early allocation of Rx buffers
		 */
		rx_rings[i].tail = vsi->back->hw.hw_addr + PRTGEN_STATUS;

		err = ice_setup_rx_ring(&rx_rings[i]);
		if (err)
			goto rx_unwind;

		/* allocate Rx buffers */
		err = ice_alloc_rx_bufs(&rx_rings[i],
					ICE_DESC_UNUSED(&rx_rings[i]));
rx_unwind:
		if (err) {
			while (i) {
				i--;
				ice_free_rx_ring(&rx_rings[i]);
			}
			devm_kfree(&pf->pdev->dev, rx_rings);
			err = -ENOMEM;
			goto free_tx;
		}
	}

process_link:
	/* Bring interface down, copy in the new ring info, then restore the
	 * interface. if VSI is up, bring it down and then back up
	 */
	if (!test_and_set_bit(__ICE_DOWN, vsi->state)) {
		ice_down(vsi);

		if (tx_rings) {
			for (i = 0; i < vsi->alloc_txq; i++) {
				ice_free_tx_ring(vsi->tx_rings[i]);
				*vsi->tx_rings[i] = tx_rings[i];
			}
			devm_kfree(&pf->pdev->dev, tx_rings);
		}

		if (rx_rings) {
			for (i = 0; i < vsi->alloc_rxq; i++) {
				ice_free_rx_ring(vsi->rx_rings[i]);
				/* copy the real tail offset */
				rx_rings[i].tail = vsi->rx_rings[i]->tail;
				/* this is to fake out the allocation routine
				 * into thinking it has to realloc everything
				 * but the recycling logic will let us re-use
				 * the buffers allocated above
				 */
				rx_rings[i].next_to_use = 0;
				rx_rings[i].next_to_clean = 0;
				rx_rings[i].next_to_alloc = 0;
				*vsi->rx_rings[i] = rx_rings[i];
			}
			devm_kfree(&pf->pdev->dev, rx_rings);
		}

		ice_up(vsi);
	}
	goto done;

free_tx:
	/* error cleanup if the Rx allocations failed after getting Tx */
	if (tx_rings) {
		for (i = 0; i < vsi->alloc_txq; i++)
			ice_free_tx_ring(&tx_rings[i]);
		devm_kfree(&pf->pdev->dev, tx_rings);
	}

done:
	clear_bit(__ICE_CFG_BUSY, pf->state);
	return err;
}

static int ice_nway_reset(struct net_device *netdev)
{
	/* restart autonegotiation */
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_vsi *vsi = np->vsi;
	struct ice_port_info *pi;
	enum ice_status status;

	pi = vsi->port_info;
	/* If VSI state is up, then restart autoneg with link up */
	if (!test_bit(__ICE_DOWN, vsi->back->state))
		status = ice_aq_set_link_restart_an(pi, true, NULL);
	else
		status = ice_aq_set_link_restart_an(pi, false, NULL);

	if (status) {
		netdev_info(netdev, "link restart failed, err %d aq_err %d\n",
			    status, pi->hw->adminq.sq_last_status);
		return -EIO;
	}

	return 0;
}

/**
 * ice_get_pauseparam - Get Flow Control status
 * @netdev: network interface device structure
 * @pause: ethernet pause (flow control) parameters
 */
static void
ice_get_pauseparam(struct net_device *netdev, struct ethtool_pauseparam *pause)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_port_info *pi = np->vsi->port_info;
	struct ice_aqc_get_phy_caps_data *pcaps;
	struct ice_vsi *vsi = np->vsi;
	struct ice_dcbx_cfg *dcbx_cfg;
	enum ice_status status;

	/* Initialize pause params */
	pause->rx_pause = 0;
	pause->tx_pause = 0;

	dcbx_cfg = &pi->local_dcbx_cfg;

	pcaps = devm_kzalloc(&vsi->back->pdev->dev, sizeof(*pcaps),
			     GFP_KERNEL);
	if (!pcaps)
		return;

	/* Get current PHY config */
	status = ice_aq_get_phy_caps(pi, false, ICE_AQC_REPORT_SW_CFG, pcaps,
				     NULL);
	if (status)
		goto out;

	pause->autoneg = ((pcaps->caps & ICE_AQC_PHY_AN_MODE) ?
			AUTONEG_ENABLE : AUTONEG_DISABLE);

	if (dcbx_cfg->pfc.pfcena)
		/* PFC enabled so report LFC as off */
		goto out;

	if (pcaps->caps & ICE_AQC_PHY_EN_TX_LINK_PAUSE)
		pause->tx_pause = 1;
	if (pcaps->caps & ICE_AQC_PHY_EN_RX_LINK_PAUSE)
		pause->rx_pause = 1;

out:
	devm_kfree(&vsi->back->pdev->dev, pcaps);
}

/**
 * ice_set_pauseparam - Set Flow Control parameter
 * @netdev: network interface device structure
 * @pause: return Tx/Rx flow control status
 */
static int
ice_set_pauseparam(struct net_device *netdev, struct ethtool_pauseparam *pause)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_link_status *hw_link_info;
	struct ice_pf *pf = np->vsi->back;
	struct ice_dcbx_cfg *dcbx_cfg;
	struct ice_vsi *vsi = np->vsi;
	struct ice_hw *hw = &pf->hw;
	struct ice_port_info *pi;
	enum ice_status status;
	u8 aq_failures;
	bool link_up;
	int err = 0;

	pi = vsi->port_info;
	hw_link_info = &pi->phy.link_info;
	dcbx_cfg = &pi->local_dcbx_cfg;
	link_up = hw_link_info->link_info & ICE_AQ_LINK_UP;

	/* Changing the port's flow control is not supported if this isn't the
	 * PF VSI
	 */
	if (vsi->type != ICE_VSI_PF) {
		netdev_info(netdev, "Changing flow control parameters only supported for PF VSI\n");
		return -EOPNOTSUPP;
	}

	if (pause->autoneg != (hw_link_info->an_info & ICE_AQ_AN_COMPLETED)) {
		netdev_info(netdev, "To change autoneg please use: ethtool -s <dev> autoneg <on|off>\n");
		return -EOPNOTSUPP;
	}

	/* If we have link and don't have autoneg */
	if (!test_bit(__ICE_DOWN, pf->state) &&
	    !(hw_link_info->an_info & ICE_AQ_AN_COMPLETED)) {
		/* Send message that it might not necessarily work*/
		netdev_info(netdev, "Autoneg did not complete so changing settings may not result in an actual change.\n");
	}

	if (dcbx_cfg->pfc.pfcena) {
		netdev_info(netdev, "Priority flow control enabled. Cannot set link flow control.\n");
		return -EOPNOTSUPP;
	}
	if (pause->rx_pause && pause->tx_pause)
		pi->fc.req_mode = ICE_FC_FULL;
	else if (pause->rx_pause && !pause->tx_pause)
		pi->fc.req_mode = ICE_FC_RX_PAUSE;
	else if (!pause->rx_pause && pause->tx_pause)
		pi->fc.req_mode = ICE_FC_TX_PAUSE;
	else if (!pause->rx_pause && !pause->tx_pause)
		pi->fc.req_mode = ICE_FC_NONE;
	else
		return -EINVAL;

	/* Tell the OS link is going down, the link will go back up when fw
	 * says it is ready asynchronously
	 */
	ice_print_link_msg(vsi, false);
	netif_carrier_off(netdev);
	netif_tx_stop_all_queues(netdev);

	/* Set the FC mode and only restart AN if link is up */
	status = ice_set_fc(pi, &aq_failures, link_up);

	if (aq_failures & ICE_SET_FC_AQ_FAIL_GET) {
		netdev_info(netdev, "Set fc failed on the get_phy_capabilities call with err %d aq_err %d\n",
			    status, hw->adminq.sq_last_status);
		err = -EAGAIN;
	} else if (aq_failures & ICE_SET_FC_AQ_FAIL_SET) {
		netdev_info(netdev, "Set fc failed on the set_phy_config call with err %d aq_err %d\n",
			    status, hw->adminq.sq_last_status);
		err = -EAGAIN;
	} else if (aq_failures & ICE_SET_FC_AQ_FAIL_UPDATE) {
		netdev_info(netdev, "Set fc failed on the get_link_info call with err %d aq_err %d\n",
			    status, hw->adminq.sq_last_status);
		err = -EAGAIN;
	}

	if (!test_bit(__ICE_DOWN, pf->state)) {
		/* Give it a little more time to try to come back. If still
		 * down, restart autoneg link or reinitialize the interface.
		 */
		msleep(75);
		if (!test_bit(__ICE_DOWN, pf->state))
			return ice_nway_reset(netdev);

		ice_down(vsi);
		ice_up(vsi);
	}

	return err;
}

/**
 * ice_get_rxfh_key_size - get the RSS hash key size
 * @netdev: network interface device structure
 *
 * Returns the table size.
 */
static u32 ice_get_rxfh_key_size(struct net_device __always_unused *netdev)
{
	return ICE_VSIQF_HKEY_ARRAY_SIZE;
}

/**
 * ice_get_rxfh_indir_size - get the Rx flow hash indirection table size
 * @netdev: network interface device structure
 *
 * Returns the table size.
 */
static u32 ice_get_rxfh_indir_size(struct net_device *netdev)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);

	return np->vsi->rss_table_size;
}

/**
 * ice_get_rxfh - get the Rx flow hash indirection table
 * @netdev: network interface device structure
 * @indir: indirection table
 * @key: hash key
 * @hfunc: hash function
 *
 * Reads the indirection table directly from the hardware.
 */
static int
ice_get_rxfh(struct net_device *netdev, u32 *indir, u8 *key, u8 *hfunc)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_vsi *vsi = np->vsi;
	struct ice_pf *pf = vsi->back;
	int ret = 0, i;
	u8 *lut;

	if (hfunc)
		*hfunc = ETH_RSS_HASH_TOP;

	if (!indir)
		return 0;

	if (!test_bit(ICE_FLAG_RSS_ENA, pf->flags)) {
		/* RSS not supported return error here */
		netdev_warn(netdev, "RSS is not configured on this VSI!\n");
		return -EIO;
	}

	lut = devm_kzalloc(&pf->pdev->dev, vsi->rss_table_size, GFP_KERNEL);
	if (!lut)
		return -ENOMEM;

	if (ice_get_rss(vsi, key, lut, vsi->rss_table_size)) {
		ret = -EIO;
		goto out;
	}

	for (i = 0; i < vsi->rss_table_size; i++)
		indir[i] = (u32)(lut[i]);

out:
	devm_kfree(&pf->pdev->dev, lut);
	return ret;
}

/**
 * ice_set_rxfh - set the Rx flow hash indirection table
 * @netdev: network interface device structure
 * @indir: indirection table
 * @key: hash key
 * @hfunc: hash function
 *
 * Returns -EINVAL if the table specifies an invalid queue ID, otherwise
 * returns 0 after programming the table.
 */
static int
ice_set_rxfh(struct net_device *netdev, const u32 *indir, const u8 *key,
	     const u8 hfunc)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_vsi *vsi = np->vsi;
	struct ice_pf *pf = vsi->back;
	u8 *seed = NULL;

	if (hfunc != ETH_RSS_HASH_NO_CHANGE && hfunc != ETH_RSS_HASH_TOP)
		return -EOPNOTSUPP;

	if (!test_bit(ICE_FLAG_RSS_ENA, pf->flags)) {
		/* RSS not supported return error here */
		netdev_warn(netdev, "RSS is not configured on this VSI!\n");
		return -EIO;
	}

	if (key) {
		if (!vsi->rss_hkey_user) {
			vsi->rss_hkey_user =
				devm_kzalloc(&pf->pdev->dev,
					     ICE_VSIQF_HKEY_ARRAY_SIZE,
					     GFP_KERNEL);
			if (!vsi->rss_hkey_user)
				return -ENOMEM;
		}
		memcpy(vsi->rss_hkey_user, key, ICE_VSIQF_HKEY_ARRAY_SIZE);
		seed = vsi->rss_hkey_user;
	}

	if (!vsi->rss_lut_user) {
		vsi->rss_lut_user = devm_kzalloc(&pf->pdev->dev,
						 vsi->rss_table_size,
						 GFP_KERNEL);
		if (!vsi->rss_lut_user)
			return -ENOMEM;
	}

	/* Each 32 bits pointed by 'indir' is stored with a lut entry */
	if (indir) {
		int i;

		for (i = 0; i < vsi->rss_table_size; i++)
			vsi->rss_lut_user[i] = (u8)(indir[i]);
	} else {
		ice_fill_rss_lut(vsi->rss_lut_user, vsi->rss_table_size,
				 vsi->rss_size);
	}

	if (ice_set_rss(vsi, seed, vsi->rss_lut_user, vsi->rss_table_size))
		return -EIO;

	return 0;
}

enum ice_container_type {
	ICE_RX_CONTAINER,
	ICE_TX_CONTAINER,
};

/**
 * ice_get_rc_coalesce - get ITR values for specific ring container
 * @ec: ethtool structure to fill with driver's coalesce settings
 * @c_type: container type, Rx or Tx
 * @rc: ring container that the ITR values will come from
 *
 * Query the device for ice_ring_container specific ITR values. This is
 * done per ice_ring_container because each q_vector can have 1 or more rings
 * and all of said ring(s) will have the same ITR values.
 *
 * Returns 0 on success, negative otherwise.
 */
static int
ice_get_rc_coalesce(struct ethtool_coalesce *ec, enum ice_container_type c_type,
		    struct ice_ring_container *rc)
{
	struct ice_pf *pf;

	if (!rc->ring)
		return -EINVAL;

	pf = rc->ring->vsi->back;

	switch (c_type) {
	case ICE_RX_CONTAINER:
		ec->use_adaptive_rx_coalesce = ITR_IS_DYNAMIC(rc->itr_setting);
		ec->rx_coalesce_usecs = rc->itr_setting & ~ICE_ITR_DYNAMIC;
		ec->rx_coalesce_usecs_high = rc->ring->q_vector->intrl;
		break;
	case ICE_TX_CONTAINER:
		ec->use_adaptive_tx_coalesce = ITR_IS_DYNAMIC(rc->itr_setting);
		ec->tx_coalesce_usecs = rc->itr_setting & ~ICE_ITR_DYNAMIC;
		break;
	default:
		dev_dbg(&pf->pdev->dev, "Invalid c_type %d\n", c_type);
		return -EINVAL;
	}

	return 0;
}

/**
 * ice_get_q_coalesce - get a queue's ITR/INTRL (coalesce) settings
 * @vsi: VSI associated to the queue for getting ITR/INTRL (coalesce) settings
 * @ec: coalesce settings to program the device with
 * @q_num: update ITR/INTRL (coalesce) settings for this queue number/index
 *
 * Return 0 on success, and negative under the following conditions:
 * 1. Getting Tx or Rx ITR/INTRL (coalesce) settings failed.
 * 2. The q_num passed in is not a valid number/index for Tx and Rx rings.
 */
static int
ice_get_q_coalesce(struct ice_vsi *vsi, struct ethtool_coalesce *ec, int q_num)
{
	if (q_num < vsi->num_rxq && q_num < vsi->num_txq) {
		if (ice_get_rc_coalesce(ec, ICE_RX_CONTAINER,
					&vsi->rx_rings[q_num]->q_vector->rx))
			return -EINVAL;
		if (ice_get_rc_coalesce(ec, ICE_TX_CONTAINER,
					&vsi->tx_rings[q_num]->q_vector->tx))
			return -EINVAL;
	} else if (q_num < vsi->num_rxq) {
		if (ice_get_rc_coalesce(ec, ICE_RX_CONTAINER,
					&vsi->rx_rings[q_num]->q_vector->rx))
			return -EINVAL;
	} else if (q_num < vsi->num_txq) {
		if (ice_get_rc_coalesce(ec, ICE_TX_CONTAINER,
					&vsi->tx_rings[q_num]->q_vector->tx))
			return -EINVAL;
	} else {
		return -EINVAL;
	}

	return 0;
}

/**
 * __ice_get_coalesce - get ITR/INTRL values for the device
 * @netdev: pointer to the netdev associated with this query
 * @ec: ethtool structure to fill with driver's coalesce settings
 * @q_num: queue number to get the coalesce settings for
 *
 * If the caller passes in a negative q_num then we return coalesce settings
 * based on queue number 0, else use the actual q_num passed in.
 */
static int
__ice_get_coalesce(struct net_device *netdev, struct ethtool_coalesce *ec,
		   int q_num)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_vsi *vsi = np->vsi;

	if (q_num < 0)
		q_num = 0;

	if (ice_get_q_coalesce(vsi, ec, q_num))
		return -EINVAL;

	if (q_num < vsi->num_txq)
		ec->tx_max_coalesced_frames_irq = vsi->work_lmt;

	if (q_num < vsi->num_rxq)
		ec->rx_max_coalesced_frames_irq = vsi->work_lmt;

	return 0;
}

static int
ice_get_coalesce(struct net_device *netdev, struct ethtool_coalesce *ec)
{
	return __ice_get_coalesce(netdev, ec, -1);
}

static int
ice_get_per_q_coalesce(struct net_device *netdev, u32 q_num,
		       struct ethtool_coalesce *ec)
{
	return __ice_get_coalesce(netdev, ec, q_num);
}

/**
 * ice_set_rc_coalesce - set ITR values for specific ring container
 * @c_type: container type, Rx or Tx
 * @ec: ethtool structure from user to update ITR settings
 * @rc: ring container that the ITR values will come from
 * @vsi: VSI associated to the ring container
 *
 * Set specific ITR values. This is done per ice_ring_container because each
 * q_vector can have 1 or more rings and all of said ring(s) will have the same
 * ITR values.
 *
 * Returns 0 on success, negative otherwise.
 */
static int
ice_set_rc_coalesce(enum ice_container_type c_type, struct ethtool_coalesce *ec,
		    struct ice_ring_container *rc, struct ice_vsi *vsi)
{
	struct ice_pf *pf = vsi->back;
	u16 itr_setting;

	if (!rc->ring)
		return -EINVAL;

	itr_setting = rc->itr_setting & ~ICE_ITR_DYNAMIC;

	switch (c_type) {
	case ICE_RX_CONTAINER:
		if (ec->rx_coalesce_usecs_high > ICE_MAX_INTRL ||
		    (ec->rx_coalesce_usecs_high &&
		     ec->rx_coalesce_usecs_high < pf->hw.intrl_gran)) {
			netdev_info(vsi->netdev,
				    "Invalid value, rx-usecs-high valid values are 0 (disabled), %d-%d\n",
				    pf->hw.intrl_gran, ICE_MAX_INTRL);
			return -EINVAL;
		}

		if (ec->rx_coalesce_usecs_high != rc->ring->q_vector->intrl) {
			rc->ring->q_vector->intrl = ec->rx_coalesce_usecs_high;
			wr32(&pf->hw, GLINT_RATE(rc->ring->q_vector->reg_idx),
			     ice_intrl_usec_to_reg(ec->rx_coalesce_usecs_high,
						   pf->hw.intrl_gran));
		}

		if (ec->rx_coalesce_usecs != itr_setting &&
		    ec->use_adaptive_rx_coalesce) {
			netdev_info(vsi->netdev,
				    "Rx interrupt throttling cannot be changed if adaptive-rx is enabled\n");
			return -EINVAL;
		}

		if (ec->rx_coalesce_usecs > ICE_ITR_MAX) {
			netdev_info(vsi->netdev,
				    "Invalid value, rx-usecs range is 0-%d\n",
				   ICE_ITR_MAX);
			return -EINVAL;
		}

		if (ec->use_adaptive_rx_coalesce) {
			rc->itr_setting |= ICE_ITR_DYNAMIC;
		} else {
			rc->itr_setting = ITR_REG_ALIGN(ec->rx_coalesce_usecs);
			rc->target_itr = ITR_TO_REG(rc->itr_setting);
		}
		break;
	case ICE_TX_CONTAINER:
		if (ec->tx_coalesce_usecs_high) {
			netdev_info(vsi->netdev,
				    "setting tx-usecs-high is not supported\n");
			return -EINVAL;
		}

		if (ec->tx_coalesce_usecs != itr_setting &&
		    ec->use_adaptive_tx_coalesce) {
			netdev_info(vsi->netdev,
				    "Tx interrupt throttling cannot be changed if adaptive-tx is enabled\n");
			return -EINVAL;
		}

		if (ec->tx_coalesce_usecs > ICE_ITR_MAX) {
			netdev_info(vsi->netdev,
				    "Invalid value, tx-usecs range is 0-%d\n",
				   ICE_ITR_MAX);
			return -EINVAL;
		}

		if (ec->use_adaptive_tx_coalesce) {
			rc->itr_setting |= ICE_ITR_DYNAMIC;
		} else {
			rc->itr_setting = ITR_REG_ALIGN(ec->tx_coalesce_usecs);
			rc->target_itr = ITR_TO_REG(rc->itr_setting);
		}
		break;
	default:
		dev_dbg(&pf->pdev->dev, "Invalid container type %d\n", c_type);
		return -EINVAL;
	}

	return 0;
}

/**
 * ice_set_q_coalesce - set a queue's ITR/INTRL (coalesce) settings
 * @vsi: VSI associated to the queue that need updating
 * @ec: coalesce settings to program the device with
 * @q_num: update ITR/INTRL (coalesce) settings for this queue number/index
 *
 * Return 0 on success, and negative under the following conditions:
 * 1. Setting Tx or Rx ITR/INTRL (coalesce) settings failed.
 * 2. The q_num passed in is not a valid number/index for Tx and Rx rings.
 */
static int
ice_set_q_coalesce(struct ice_vsi *vsi, struct ethtool_coalesce *ec, int q_num)
{
	if (q_num < vsi->num_rxq && q_num < vsi->num_txq) {
		if (ice_set_rc_coalesce(ICE_RX_CONTAINER, ec,
					&vsi->rx_rings[q_num]->q_vector->rx,
					vsi))
			return -EINVAL;

		if (ice_set_rc_coalesce(ICE_TX_CONTAINER, ec,
					&vsi->tx_rings[q_num]->q_vector->tx,
					vsi))
			return -EINVAL;
	} else if (q_num < vsi->num_rxq) {
		if (ice_set_rc_coalesce(ICE_RX_CONTAINER, ec,
					&vsi->rx_rings[q_num]->q_vector->rx,
					vsi))
			return -EINVAL;
	} else if (q_num < vsi->num_txq) {
		if (ice_set_rc_coalesce(ICE_TX_CONTAINER, ec,
					&vsi->tx_rings[q_num]->q_vector->tx,
					vsi))
			return -EINVAL;
	} else {
		return -EINVAL;
	}

	return 0;
}

/**
 * __ice_set_coalesce - set ITR/INTRL values for the device
 * @netdev: pointer to the netdev associated with this query
 * @ec: ethtool structure to fill with driver's coalesce settings
 * @q_num: queue number to get the coalesce settings for
 *
 * If the caller passes in a negative q_num then we set the coalesce settings
 * for all Tx/Rx queues, else use the actual q_num passed in.
 */
static int
__ice_set_coalesce(struct net_device *netdev, struct ethtool_coalesce *ec,
		   int q_num)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_vsi *vsi = np->vsi;

	if (q_num < 0) {
		int i;

		ice_for_each_q_vector(vsi, i) {
			if (ice_set_q_coalesce(vsi, ec, i))
				return -EINVAL;
		}
		goto set_work_lmt;
	}

	if (ice_set_q_coalesce(vsi, ec, q_num))
		return -EINVAL;

set_work_lmt:

	if (ec->tx_max_coalesced_frames_irq || ec->rx_max_coalesced_frames_irq)
		vsi->work_lmt = max(ec->tx_max_coalesced_frames_irq,
				    ec->rx_max_coalesced_frames_irq);

	return 0;
}

static int
ice_set_coalesce(struct net_device *netdev, struct ethtool_coalesce *ec)
{
	return __ice_set_coalesce(netdev, ec, -1);
}

static int
ice_set_per_q_coalesce(struct net_device *netdev, u32 q_num,
		       struct ethtool_coalesce *ec)
{
	return __ice_set_coalesce(netdev, ec, q_num);
}

static const struct ethtool_ops ice_ethtool_ops = {
	.get_link_ksettings	= ice_get_link_ksettings,
	.set_link_ksettings	= ice_set_link_ksettings,
	.get_drvinfo            = ice_get_drvinfo,
	.get_regs_len           = ice_get_regs_len,
	.get_regs               = ice_get_regs,
	.get_msglevel           = ice_get_msglevel,
	.set_msglevel           = ice_set_msglevel,
	.self_test		= ice_self_test,
	.get_link		= ethtool_op_get_link,
	.get_eeprom_len		= ice_get_eeprom_len,
	.get_eeprom		= ice_get_eeprom,
	.get_coalesce		= ice_get_coalesce,
	.set_coalesce		= ice_set_coalesce,
	.get_strings		= ice_get_strings,
	.set_phys_id		= ice_set_phys_id,
	.get_ethtool_stats      = ice_get_ethtool_stats,
	.get_priv_flags		= ice_get_priv_flags,
	.set_priv_flags		= ice_set_priv_flags,
	.get_sset_count		= ice_get_sset_count,
	.get_rxnfc		= ice_get_rxnfc,
	.get_ringparam		= ice_get_ringparam,
	.set_ringparam		= ice_set_ringparam,
	.nway_reset		= ice_nway_reset,
	.get_pauseparam		= ice_get_pauseparam,
	.set_pauseparam		= ice_set_pauseparam,
	.get_rxfh_key_size	= ice_get_rxfh_key_size,
	.get_rxfh_indir_size	= ice_get_rxfh_indir_size,
	.get_rxfh		= ice_get_rxfh,
	.set_rxfh		= ice_set_rxfh,
	.get_ts_info		= ethtool_op_get_ts_info,
	.get_per_queue_coalesce = ice_get_per_q_coalesce,
	.set_per_queue_coalesce = ice_set_per_q_coalesce,
	.get_fecparam		= ice_get_fecparam,
	.set_fecparam		= ice_set_fecparam,
};

/**
 * ice_set_ethtool_ops - setup netdev ethtool ops
 * @netdev: network interface device structure
 *
 * setup netdev ethtool ops with ice specific ops
 */
void ice_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &ice_ethtool_ops;
}
