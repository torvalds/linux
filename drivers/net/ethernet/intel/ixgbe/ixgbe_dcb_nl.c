// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 1999 - 2024 Intel Corporation. */

#include "ixgbe.h"
#include <linux/dcbnl.h>
#include "ixgbe_dcb_82598.h"
#include "ixgbe_dcb_82599.h"
#include "ixgbe_sriov.h"

/* Callbacks for DCB netlink in the kernel */
#define BIT_PFC		0x02
#define BIT_PG_RX	0x04
#define BIT_PG_TX	0x08
#define BIT_APP_UPCHG	0x10

/* Responses for the DCB_C_SET_ALL command */
#define DCB_HW_CHG_RST  0  /* DCB configuration changed with reset */
#define DCB_NO_HW_CHG   1  /* DCB configuration did not change */
#define DCB_HW_CHG      2  /* DCB configuration changed, no reset */

static int ixgbe_copy_dcb_cfg(struct ixgbe_adapter *adapter, int tc_max)
{
	struct ixgbe_dcb_config *scfg = &adapter->temp_dcb_cfg;
	struct ixgbe_dcb_config *dcfg = &adapter->dcb_cfg;
	struct tc_configuration *src = NULL;
	struct tc_configuration *dst = NULL;
	int i, j;
	int tx = DCB_TX_CONFIG;
	int rx = DCB_RX_CONFIG;
	int changes = 0;
#ifdef IXGBE_FCOE
	struct dcb_app app = {
			      .selector = DCB_APP_IDTYPE_ETHTYPE,
			      .protocol = ETH_P_FCOE,
			     };
	u8 up = dcb_getapp(adapter->netdev, &app);

	if (up && !(up & BIT(adapter->fcoe.up)))
		changes |= BIT_APP_UPCHG;
#endif

	for (i = DCB_PG_ATTR_TC_0; i < tc_max + DCB_PG_ATTR_TC_0; i++) {
		src = &scfg->tc_config[i - DCB_PG_ATTR_TC_0];
		dst = &dcfg->tc_config[i - DCB_PG_ATTR_TC_0];

		if (dst->path[tx].prio_type != src->path[tx].prio_type) {
			dst->path[tx].prio_type = src->path[tx].prio_type;
			changes |= BIT_PG_TX;
		}

		if (dst->path[tx].bwg_id != src->path[tx].bwg_id) {
			dst->path[tx].bwg_id = src->path[tx].bwg_id;
			changes |= BIT_PG_TX;
		}

		if (dst->path[tx].bwg_percent != src->path[tx].bwg_percent) {
			dst->path[tx].bwg_percent = src->path[tx].bwg_percent;
			changes |= BIT_PG_TX;
		}

		if (dst->path[tx].up_to_tc_bitmap !=
				src->path[tx].up_to_tc_bitmap) {
			dst->path[tx].up_to_tc_bitmap =
				src->path[tx].up_to_tc_bitmap;
			changes |= (BIT_PG_TX | BIT_PFC | BIT_APP_UPCHG);
		}

		if (dst->path[rx].prio_type != src->path[rx].prio_type) {
			dst->path[rx].prio_type = src->path[rx].prio_type;
			changes |= BIT_PG_RX;
		}

		if (dst->path[rx].bwg_id != src->path[rx].bwg_id) {
			dst->path[rx].bwg_id = src->path[rx].bwg_id;
			changes |= BIT_PG_RX;
		}

		if (dst->path[rx].bwg_percent != src->path[rx].bwg_percent) {
			dst->path[rx].bwg_percent = src->path[rx].bwg_percent;
			changes |= BIT_PG_RX;
		}

		if (dst->path[rx].up_to_tc_bitmap !=
				src->path[rx].up_to_tc_bitmap) {
			dst->path[rx].up_to_tc_bitmap =
				src->path[rx].up_to_tc_bitmap;
			changes |= (BIT_PG_RX | BIT_PFC | BIT_APP_UPCHG);
		}
	}

	for (i = DCB_PG_ATTR_BW_ID_0; i < DCB_PG_ATTR_BW_ID_MAX; i++) {
		j = i - DCB_PG_ATTR_BW_ID_0;
		if (dcfg->bw_percentage[tx][j] != scfg->bw_percentage[tx][j]) {
			dcfg->bw_percentage[tx][j] = scfg->bw_percentage[tx][j];
			changes |= BIT_PG_TX;
		}
		if (dcfg->bw_percentage[rx][j] != scfg->bw_percentage[rx][j]) {
			dcfg->bw_percentage[rx][j] = scfg->bw_percentage[rx][j];
			changes |= BIT_PG_RX;
		}
	}

	for (i = DCB_PFC_UP_ATTR_0; i < DCB_PFC_UP_ATTR_MAX; i++) {
		j = i - DCB_PFC_UP_ATTR_0;
		if (dcfg->tc_config[j].dcb_pfc != scfg->tc_config[j].dcb_pfc) {
			dcfg->tc_config[j].dcb_pfc = scfg->tc_config[j].dcb_pfc;
			changes |= BIT_PFC;
		}
	}

	if (dcfg->pfc_mode_enable != scfg->pfc_mode_enable) {
		dcfg->pfc_mode_enable = scfg->pfc_mode_enable;
		changes |= BIT_PFC;
	}

	return changes;
}

static u8 ixgbe_dcbnl_get_state(struct net_device *netdev)
{
	struct ixgbe_adapter *adapter = ixgbe_from_netdev(netdev);

	return !!(adapter->flags & IXGBE_FLAG_DCB_ENABLED);
}

static u8 ixgbe_dcbnl_set_state(struct net_device *netdev, u8 state)
{
	struct ixgbe_adapter *adapter = ixgbe_from_netdev(netdev);

	/* Fail command if not in CEE mode */
	if (!(adapter->dcbx_cap & DCB_CAP_DCBX_VER_CEE))
		return 1;

	/* verify there is something to do, if not then exit */
	if (!state == !(adapter->flags & IXGBE_FLAG_DCB_ENABLED))
		return 0;

	return !!ixgbe_setup_tc(netdev,
				state ? adapter->dcb_cfg.num_tcs.pg_tcs : 0);
}

static void ixgbe_dcbnl_get_perm_hw_addr(struct net_device *netdev,
					 u8 *perm_addr)
{
	struct ixgbe_adapter *adapter = ixgbe_from_netdev(netdev);
	int i, j;

	memset(perm_addr, 0xff, MAX_ADDR_LEN);

	for (i = 0; i < netdev->addr_len; i++)
		perm_addr[i] = adapter->hw.mac.perm_addr[i];

	switch (adapter->hw.mac.type) {
	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
	case ixgbe_mac_X550:
	case ixgbe_mac_e610:
		for (j = 0; j < netdev->addr_len; j++, i++)
			perm_addr[i] = adapter->hw.mac.san_addr[j];
		break;
	default:
		break;
	}
}

static void ixgbe_dcbnl_set_pg_tc_cfg_tx(struct net_device *netdev, int tc,
					 u8 prio, u8 bwg_id, u8 bw_pct,
					 u8 up_map)
{
	struct ixgbe_adapter *adapter = ixgbe_from_netdev(netdev);

	if (prio != DCB_ATTR_VALUE_UNDEFINED)
		adapter->temp_dcb_cfg.tc_config[tc].path[0].prio_type = prio;
	if (bwg_id != DCB_ATTR_VALUE_UNDEFINED)
		adapter->temp_dcb_cfg.tc_config[tc].path[0].bwg_id = bwg_id;
	if (bw_pct != DCB_ATTR_VALUE_UNDEFINED)
		adapter->temp_dcb_cfg.tc_config[tc].path[0].bwg_percent =
			bw_pct;
	if (up_map != DCB_ATTR_VALUE_UNDEFINED)
		adapter->temp_dcb_cfg.tc_config[tc].path[0].up_to_tc_bitmap =
			up_map;
}

static void ixgbe_dcbnl_set_pg_bwg_cfg_tx(struct net_device *netdev, int bwg_id,
					  u8 bw_pct)
{
	struct ixgbe_adapter *adapter = ixgbe_from_netdev(netdev);

	adapter->temp_dcb_cfg.bw_percentage[0][bwg_id] = bw_pct;
}

static void ixgbe_dcbnl_set_pg_tc_cfg_rx(struct net_device *netdev, int tc,
					 u8 prio, u8 bwg_id, u8 bw_pct,
					 u8 up_map)
{
	struct ixgbe_adapter *adapter = ixgbe_from_netdev(netdev);

	if (prio != DCB_ATTR_VALUE_UNDEFINED)
		adapter->temp_dcb_cfg.tc_config[tc].path[1].prio_type = prio;
	if (bwg_id != DCB_ATTR_VALUE_UNDEFINED)
		adapter->temp_dcb_cfg.tc_config[tc].path[1].bwg_id = bwg_id;
	if (bw_pct != DCB_ATTR_VALUE_UNDEFINED)
		adapter->temp_dcb_cfg.tc_config[tc].path[1].bwg_percent =
			bw_pct;
	if (up_map != DCB_ATTR_VALUE_UNDEFINED)
		adapter->temp_dcb_cfg.tc_config[tc].path[1].up_to_tc_bitmap =
			up_map;
}

static void ixgbe_dcbnl_set_pg_bwg_cfg_rx(struct net_device *netdev, int bwg_id,
					  u8 bw_pct)
{
	struct ixgbe_adapter *adapter = ixgbe_from_netdev(netdev);

	adapter->temp_dcb_cfg.bw_percentage[1][bwg_id] = bw_pct;
}

static void ixgbe_dcbnl_get_pg_tc_cfg_tx(struct net_device *netdev, int tc,
					 u8 *prio, u8 *bwg_id, u8 *bw_pct,
					 u8 *up_map)
{
	struct ixgbe_adapter *adapter = ixgbe_from_netdev(netdev);

	*prio = adapter->dcb_cfg.tc_config[tc].path[0].prio_type;
	*bwg_id = adapter->dcb_cfg.tc_config[tc].path[0].bwg_id;
	*bw_pct = adapter->dcb_cfg.tc_config[tc].path[0].bwg_percent;
	*up_map = adapter->dcb_cfg.tc_config[tc].path[0].up_to_tc_bitmap;
}

static void ixgbe_dcbnl_get_pg_bwg_cfg_tx(struct net_device *netdev, int bwg_id,
					  u8 *bw_pct)
{
	struct ixgbe_adapter *adapter = ixgbe_from_netdev(netdev);

	*bw_pct = adapter->dcb_cfg.bw_percentage[0][bwg_id];
}

static void ixgbe_dcbnl_get_pg_tc_cfg_rx(struct net_device *netdev, int tc,
					 u8 *prio, u8 *bwg_id, u8 *bw_pct,
					 u8 *up_map)
{
	struct ixgbe_adapter *adapter = ixgbe_from_netdev(netdev);

	*prio = adapter->dcb_cfg.tc_config[tc].path[1].prio_type;
	*bwg_id = adapter->dcb_cfg.tc_config[tc].path[1].bwg_id;
	*bw_pct = adapter->dcb_cfg.tc_config[tc].path[1].bwg_percent;
	*up_map = adapter->dcb_cfg.tc_config[tc].path[1].up_to_tc_bitmap;
}

static void ixgbe_dcbnl_get_pg_bwg_cfg_rx(struct net_device *netdev, int bwg_id,
					  u8 *bw_pct)
{
	struct ixgbe_adapter *adapter = ixgbe_from_netdev(netdev);

	*bw_pct = adapter->dcb_cfg.bw_percentage[1][bwg_id];
}

static void ixgbe_dcbnl_set_pfc_cfg(struct net_device *netdev, int priority,
				    u8 setting)
{
	struct ixgbe_adapter *adapter = ixgbe_from_netdev(netdev);

	adapter->temp_dcb_cfg.tc_config[priority].dcb_pfc = setting;
	if (adapter->temp_dcb_cfg.tc_config[priority].dcb_pfc !=
	    adapter->dcb_cfg.tc_config[priority].dcb_pfc)
		adapter->temp_dcb_cfg.pfc_mode_enable = true;
}

static void ixgbe_dcbnl_get_pfc_cfg(struct net_device *netdev, int priority,
				    u8 *setting)
{
	struct ixgbe_adapter *adapter = ixgbe_from_netdev(netdev);

	*setting = adapter->dcb_cfg.tc_config[priority].dcb_pfc;
}

static void ixgbe_dcbnl_devreset(struct net_device *dev)
{
	struct ixgbe_adapter *adapter = ixgbe_from_netdev(dev);

	while (test_and_set_bit(__IXGBE_RESETTING, &adapter->state))
		usleep_range(1000, 2000);

	if (netif_running(dev))
		dev->netdev_ops->ndo_stop(dev);

	ixgbe_clear_interrupt_scheme(adapter);
	ixgbe_init_interrupt_scheme(adapter);

	if (netif_running(dev))
		dev->netdev_ops->ndo_open(dev);

	clear_bit(__IXGBE_RESETTING, &adapter->state);
}

static u8 ixgbe_dcbnl_set_all(struct net_device *netdev)
{
	struct ixgbe_adapter *adapter = ixgbe_from_netdev(netdev);
	struct ixgbe_dcb_config *dcb_cfg = &adapter->dcb_cfg;
	struct ixgbe_hw *hw = &adapter->hw;
	int ret = DCB_NO_HW_CHG;
	int i;

	/* Fail command if not in CEE mode */
	if (!(adapter->dcbx_cap & DCB_CAP_DCBX_VER_CEE))
		return DCB_NO_HW_CHG;

	adapter->dcb_set_bitmap |= ixgbe_copy_dcb_cfg(adapter,
						      MAX_TRAFFIC_CLASS);
	if (!adapter->dcb_set_bitmap)
		return DCB_NO_HW_CHG;

	if (adapter->dcb_set_bitmap & (BIT_PG_TX|BIT_PG_RX)) {
		u16 refill[MAX_TRAFFIC_CLASS], max[MAX_TRAFFIC_CLASS];
		u8 bwg_id[MAX_TRAFFIC_CLASS], prio_type[MAX_TRAFFIC_CLASS];
		/* Priority to TC mapping in CEE case default to 1:1 */
		u8 prio_tc[MAX_USER_PRIORITY];
		int max_frame = adapter->netdev->mtu + ETH_HLEN + ETH_FCS_LEN;

#ifdef IXGBE_FCOE
		if (adapter->netdev->fcoe_mtu)
			max_frame = max(max_frame, IXGBE_FCOE_JUMBO_FRAME_SIZE);
#endif

		ixgbe_dcb_calculate_tc_credits(hw, dcb_cfg, max_frame,
					       DCB_TX_CONFIG);
		ixgbe_dcb_calculate_tc_credits(hw, dcb_cfg, max_frame,
					       DCB_RX_CONFIG);

		ixgbe_dcb_unpack_refill(dcb_cfg, DCB_TX_CONFIG, refill);
		ixgbe_dcb_unpack_max(dcb_cfg, max);
		ixgbe_dcb_unpack_bwgid(dcb_cfg, DCB_TX_CONFIG, bwg_id);
		ixgbe_dcb_unpack_prio(dcb_cfg, DCB_TX_CONFIG, prio_type);
		ixgbe_dcb_unpack_map(dcb_cfg, DCB_TX_CONFIG, prio_tc);

		ixgbe_dcb_hw_ets_config(hw, refill, max, bwg_id,
					prio_type, prio_tc);

		for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++)
			netdev_set_prio_tc_map(netdev, i, prio_tc[i]);

		ret = DCB_HW_CHG_RST;
	}

	if (adapter->dcb_set_bitmap & BIT_PFC) {
		if (dcb_cfg->pfc_mode_enable) {
			u8 pfc_en;
			u8 prio_tc[MAX_USER_PRIORITY];

			ixgbe_dcb_unpack_map(dcb_cfg, DCB_TX_CONFIG, prio_tc);
			ixgbe_dcb_unpack_pfc(dcb_cfg, &pfc_en);
			ixgbe_dcb_hw_pfc_config(hw, pfc_en, prio_tc);
		} else {
			hw->mac.ops.fc_enable(hw);
		}

		ixgbe_set_rx_drop_en(adapter);

		ret = DCB_HW_CHG;
	}

#ifdef IXGBE_FCOE
	/* Reprogram FCoE hardware offloads when the traffic class
	 * FCoE is using changes. This happens if the APP info
	 * changes or the up2tc mapping is updated.
	 */
	if (adapter->dcb_set_bitmap & BIT_APP_UPCHG) {
		struct dcb_app app = {
				      .selector = DCB_APP_IDTYPE_ETHTYPE,
				      .protocol = ETH_P_FCOE,
				     };
		u8 up = dcb_getapp(netdev, &app);

		adapter->fcoe.up = ffs(up) - 1;
		ixgbe_dcbnl_devreset(netdev);
		ret = DCB_HW_CHG_RST;
	}
#endif

	adapter->dcb_set_bitmap = 0x00;
	return ret;
}

static u8 ixgbe_dcbnl_getcap(struct net_device *netdev, int capid, u8 *cap)
{
	struct ixgbe_adapter *adapter = ixgbe_from_netdev(netdev);

	switch (capid) {
	case DCB_CAP_ATTR_PG:
		*cap = true;
		break;
	case DCB_CAP_ATTR_PFC:
		*cap = true;
		break;
	case DCB_CAP_ATTR_UP2TC:
		*cap = false;
		break;
	case DCB_CAP_ATTR_PG_TCS:
		*cap = 0x80;
		break;
	case DCB_CAP_ATTR_PFC_TCS:
		*cap = 0x80;
		break;
	case DCB_CAP_ATTR_GSP:
		*cap = true;
		break;
	case DCB_CAP_ATTR_BCN:
		*cap = false;
		break;
	case DCB_CAP_ATTR_DCBX:
		*cap = adapter->dcbx_cap;
		break;
	default:
		*cap = false;
		break;
	}

	return 0;
}

static int ixgbe_dcbnl_getnumtcs(struct net_device *netdev, int tcid, u8 *num)
{
	struct ixgbe_adapter *adapter = ixgbe_from_netdev(netdev);

	if (adapter->flags & IXGBE_FLAG_DCB_ENABLED) {
		switch (tcid) {
		case DCB_NUMTCS_ATTR_PG:
			*num = adapter->dcb_cfg.num_tcs.pg_tcs;
			break;
		case DCB_NUMTCS_ATTR_PFC:
			*num = adapter->dcb_cfg.num_tcs.pfc_tcs;
			break;
		default:
			return -EINVAL;
		}
	} else {
		return -EINVAL;
	}

	return 0;
}

static int ixgbe_dcbnl_setnumtcs(struct net_device *netdev, int tcid, u8 num)
{
	return -EINVAL;
}

static u8 ixgbe_dcbnl_getpfcstate(struct net_device *netdev)
{
	struct ixgbe_adapter *adapter = ixgbe_from_netdev(netdev);

	return adapter->dcb_cfg.pfc_mode_enable;
}

static void ixgbe_dcbnl_setpfcstate(struct net_device *netdev, u8 state)
{
	struct ixgbe_adapter *adapter = ixgbe_from_netdev(netdev);

	adapter->temp_dcb_cfg.pfc_mode_enable = state;
}

/**
 * ixgbe_dcbnl_getapp - retrieve the DCBX application user priority
 * @netdev : the corresponding netdev
 * @idtype : identifies the id as ether type or TCP/UDP port number
 * @id: id is either ether type or TCP/UDP port number
 *
 * Returns : on success, returns a non-zero 802.1p user priority bitmap
 * otherwise returns -EINVAL as the invalid user priority bitmap to indicate an
 * error.
 */
static int ixgbe_dcbnl_getapp(struct net_device *netdev, u8 idtype, u16 id)
{
	struct ixgbe_adapter *adapter = ixgbe_from_netdev(netdev);
	struct dcb_app app = {
				.selector = idtype,
				.protocol = id,
			     };

	if (!(adapter->dcbx_cap & DCB_CAP_DCBX_VER_CEE))
		return -EINVAL;

	return dcb_getapp(netdev, &app);
}

static int ixgbe_dcbnl_ieee_getets(struct net_device *dev,
				   struct ieee_ets *ets)
{
	struct ixgbe_adapter *adapter = ixgbe_from_netdev(dev);
	struct ieee_ets *my_ets = adapter->ixgbe_ieee_ets;

	ets->ets_cap = adapter->dcb_cfg.num_tcs.pg_tcs;

	/* No IEEE PFC settings available */
	if (!my_ets)
		return 0;

	ets->cbs = my_ets->cbs;
	memcpy(ets->tc_tx_bw, my_ets->tc_tx_bw, sizeof(ets->tc_tx_bw));
	memcpy(ets->tc_rx_bw, my_ets->tc_rx_bw, sizeof(ets->tc_rx_bw));
	memcpy(ets->tc_tsa, my_ets->tc_tsa, sizeof(ets->tc_tsa));
	memcpy(ets->prio_tc, my_ets->prio_tc, sizeof(ets->prio_tc));
	return 0;
}

static int ixgbe_dcbnl_ieee_setets(struct net_device *dev,
				   struct ieee_ets *ets)
{
	struct ixgbe_adapter *adapter = ixgbe_from_netdev(dev);
	int max_frame = dev->mtu + ETH_HLEN + ETH_FCS_LEN;
	int i, err;
	__u8 max_tc = 0;
	__u8 map_chg = 0;

	if (!(adapter->dcbx_cap & DCB_CAP_DCBX_VER_IEEE))
		return -EINVAL;

	if (!adapter->ixgbe_ieee_ets) {
		adapter->ixgbe_ieee_ets = kmalloc(sizeof(struct ieee_ets),
						  GFP_KERNEL);
		if (!adapter->ixgbe_ieee_ets)
			return -ENOMEM;

		/* initialize UP2TC mappings to invalid value */
		for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++)
			adapter->ixgbe_ieee_ets->prio_tc[i] =
				IEEE_8021QAZ_MAX_TCS;
		/* if possible update UP2TC mappings from HW */
		ixgbe_dcb_read_rtrup2tc(&adapter->hw,
					adapter->ixgbe_ieee_ets->prio_tc);
	}

	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++) {
		if (ets->prio_tc[i] > max_tc)
			max_tc = ets->prio_tc[i];
		if (ets->prio_tc[i] != adapter->ixgbe_ieee_ets->prio_tc[i])
			map_chg = 1;
	}

	memcpy(adapter->ixgbe_ieee_ets, ets, sizeof(*adapter->ixgbe_ieee_ets));

	if (max_tc)
		max_tc++;

	if (max_tc > adapter->dcb_cfg.num_tcs.pg_tcs)
		return -EINVAL;

	if (max_tc != adapter->hw_tcs) {
		err = ixgbe_setup_tc(dev, max_tc);
		if (err)
			return err;
	} else if (map_chg) {
		ixgbe_dcbnl_devreset(dev);
	}

	return ixgbe_dcb_hw_ets(&adapter->hw, ets, max_frame);
}

static int ixgbe_dcbnl_ieee_getpfc(struct net_device *dev,
				   struct ieee_pfc *pfc)
{
	struct ixgbe_adapter *adapter = ixgbe_from_netdev(dev);
	struct ieee_pfc *my_pfc = adapter->ixgbe_ieee_pfc;
	int i;

	pfc->pfc_cap = adapter->dcb_cfg.num_tcs.pfc_tcs;

	/* No IEEE PFC settings available */
	if (!my_pfc)
		return 0;

	pfc->pfc_en = my_pfc->pfc_en;
	pfc->mbc = my_pfc->mbc;
	pfc->delay = my_pfc->delay;

	for (i = 0; i < MAX_TRAFFIC_CLASS; i++) {
		pfc->requests[i] = adapter->stats.pxoffrxc[i];
		pfc->indications[i] = adapter->stats.pxofftxc[i];
	}

	return 0;
}

static int ixgbe_dcbnl_ieee_setpfc(struct net_device *dev,
				   struct ieee_pfc *pfc)
{
	struct ixgbe_adapter *adapter = ixgbe_from_netdev(dev);
	struct ixgbe_hw *hw = &adapter->hw;
	u8 *prio_tc;
	int err;

	if (!(adapter->dcbx_cap & DCB_CAP_DCBX_VER_IEEE))
		return -EINVAL;

	if (!adapter->ixgbe_ieee_pfc) {
		adapter->ixgbe_ieee_pfc = kmalloc(sizeof(struct ieee_pfc),
						  GFP_KERNEL);
		if (!adapter->ixgbe_ieee_pfc)
			return -ENOMEM;
	}

	prio_tc = adapter->ixgbe_ieee_ets->prio_tc;
	memcpy(adapter->ixgbe_ieee_pfc, pfc, sizeof(*adapter->ixgbe_ieee_pfc));

	/* Enable link flow control parameters if PFC is disabled */
	if (pfc->pfc_en)
		err = ixgbe_dcb_hw_pfc_config(hw, pfc->pfc_en, prio_tc);
	else
		err = hw->mac.ops.fc_enable(hw);

	ixgbe_set_rx_drop_en(adapter);

	return err;
}

static int ixgbe_dcbnl_ieee_setapp(struct net_device *dev,
				   struct dcb_app *app)
{
	struct ixgbe_adapter *adapter = ixgbe_from_netdev(dev);
	int err;

	if (!(adapter->dcbx_cap & DCB_CAP_DCBX_VER_IEEE))
		return -EINVAL;

	err = dcb_ieee_setapp(dev, app);
	if (err)
		return err;

#ifdef IXGBE_FCOE
	if (app->selector == IEEE_8021QAZ_APP_SEL_ETHERTYPE &&
	    app->protocol == ETH_P_FCOE) {
		u8 app_mask = dcb_ieee_getapp_mask(dev, app);

		if (app_mask & BIT(adapter->fcoe.up))
			return 0;

		adapter->fcoe.up = app->priority;
		ixgbe_dcbnl_devreset(dev);
	}
#endif

	/* VF devices should use default UP when available */
	if (app->selector == IEEE_8021QAZ_APP_SEL_ETHERTYPE &&
	    app->protocol == 0) {
		int vf;

		adapter->default_up = app->priority;

		for (vf = 0; vf < adapter->num_vfs; vf++) {
			struct vf_data_storage *vfinfo = &adapter->vfinfo[vf];

			if (!vfinfo->pf_qos)
				ixgbe_set_vmvir(adapter, vfinfo->pf_vlan,
						app->priority, vf);
		}
	}

	return 0;
}

static int ixgbe_dcbnl_ieee_delapp(struct net_device *dev,
				   struct dcb_app *app)
{
	struct ixgbe_adapter *adapter = ixgbe_from_netdev(dev);
	int err;

	if (!(adapter->dcbx_cap & DCB_CAP_DCBX_VER_IEEE))
		return -EINVAL;

	err = dcb_ieee_delapp(dev, app);

#ifdef IXGBE_FCOE
	if (!err && app->selector == IEEE_8021QAZ_APP_SEL_ETHERTYPE &&
	    app->protocol == ETH_P_FCOE) {
		u8 app_mask = dcb_ieee_getapp_mask(dev, app);

		if (app_mask & BIT(adapter->fcoe.up))
			return 0;

		adapter->fcoe.up = app_mask ?
				   ffs(app_mask) - 1 : IXGBE_FCOE_DEFTC;
		ixgbe_dcbnl_devreset(dev);
	}
#endif
	/* IF default priority is being removed clear VF default UP */
	if (app->selector == IEEE_8021QAZ_APP_SEL_ETHERTYPE &&
	    app->protocol == 0 && adapter->default_up == app->priority) {
		int vf;
		long unsigned int app_mask = dcb_ieee_getapp_mask(dev, app);
		int qos = app_mask ? find_first_bit(&app_mask, 8) : 0;

		adapter->default_up = qos;

		for (vf = 0; vf < adapter->num_vfs; vf++) {
			struct vf_data_storage *vfinfo = &adapter->vfinfo[vf];

			if (!vfinfo->pf_qos)
				ixgbe_set_vmvir(adapter, vfinfo->pf_vlan,
						qos, vf);
		}
	}

	return err;
}

static u8 ixgbe_dcbnl_getdcbx(struct net_device *dev)
{
	struct ixgbe_adapter *adapter = ixgbe_from_netdev(dev);
	return adapter->dcbx_cap;
}

static u8 ixgbe_dcbnl_setdcbx(struct net_device *dev, u8 mode)
{
	struct ixgbe_adapter *adapter = ixgbe_from_netdev(dev);
	struct ieee_ets ets = {0};
	struct ieee_pfc pfc = {0};
	int err = 0;

	/* no support for LLD_MANAGED modes or CEE+IEEE */
	if ((mode & DCB_CAP_DCBX_LLD_MANAGED) ||
	    ((mode & DCB_CAP_DCBX_VER_IEEE) && (mode & DCB_CAP_DCBX_VER_CEE)) ||
	    !(mode & DCB_CAP_DCBX_HOST))
		return 1;

	if (mode == adapter->dcbx_cap)
		return 0;

	adapter->dcbx_cap = mode;

	/* ETS and PFC defaults */
	ets.ets_cap = 8;
	pfc.pfc_cap = 8;

	if (mode & DCB_CAP_DCBX_VER_IEEE) {
		ixgbe_dcbnl_ieee_setets(dev, &ets);
		ixgbe_dcbnl_ieee_setpfc(dev, &pfc);
	} else if (mode & DCB_CAP_DCBX_VER_CEE) {
		u8 mask = BIT_PFC | BIT_PG_TX | BIT_PG_RX | BIT_APP_UPCHG;

		adapter->dcb_set_bitmap |= mask;
		ixgbe_dcbnl_set_all(dev);
	} else {
		/* Drop into single TC mode strict priority as this
		 * indicates CEE and IEEE versions are disabled
		 */
		ixgbe_dcbnl_ieee_setets(dev, &ets);
		ixgbe_dcbnl_ieee_setpfc(dev, &pfc);
		err = ixgbe_setup_tc(dev, 0);
	}

	return err ? 1 : 0;
}

const struct dcbnl_rtnl_ops ixgbe_dcbnl_ops = {
	.ieee_getets	= ixgbe_dcbnl_ieee_getets,
	.ieee_setets	= ixgbe_dcbnl_ieee_setets,
	.ieee_getpfc	= ixgbe_dcbnl_ieee_getpfc,
	.ieee_setpfc	= ixgbe_dcbnl_ieee_setpfc,
	.ieee_setapp	= ixgbe_dcbnl_ieee_setapp,
	.ieee_delapp	= ixgbe_dcbnl_ieee_delapp,
	.getstate	= ixgbe_dcbnl_get_state,
	.setstate	= ixgbe_dcbnl_set_state,
	.getpermhwaddr	= ixgbe_dcbnl_get_perm_hw_addr,
	.setpgtccfgtx	= ixgbe_dcbnl_set_pg_tc_cfg_tx,
	.setpgbwgcfgtx	= ixgbe_dcbnl_set_pg_bwg_cfg_tx,
	.setpgtccfgrx	= ixgbe_dcbnl_set_pg_tc_cfg_rx,
	.setpgbwgcfgrx	= ixgbe_dcbnl_set_pg_bwg_cfg_rx,
	.getpgtccfgtx	= ixgbe_dcbnl_get_pg_tc_cfg_tx,
	.getpgbwgcfgtx	= ixgbe_dcbnl_get_pg_bwg_cfg_tx,
	.getpgtccfgrx	= ixgbe_dcbnl_get_pg_tc_cfg_rx,
	.getpgbwgcfgrx	= ixgbe_dcbnl_get_pg_bwg_cfg_rx,
	.setpfccfg	= ixgbe_dcbnl_set_pfc_cfg,
	.getpfccfg	= ixgbe_dcbnl_get_pfc_cfg,
	.setall		= ixgbe_dcbnl_set_all,
	.getcap		= ixgbe_dcbnl_getcap,
	.getnumtcs	= ixgbe_dcbnl_getnumtcs,
	.setnumtcs	= ixgbe_dcbnl_setnumtcs,
	.getpfcstate	= ixgbe_dcbnl_getpfcstate,
	.setpfcstate	= ixgbe_dcbnl_setpfcstate,
	.getapp		= ixgbe_dcbnl_getapp,
	.getdcbx	= ixgbe_dcbnl_getdcbx,
	.setdcbx	= ixgbe_dcbnl_setdcbx,
};
