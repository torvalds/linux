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
  Linux NICS <linux.nics@intel.com>
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

#include "ixgbe.h"
#include <linux/dcbnl.h>
#include "ixgbe_dcb_82598.h"
#include "ixgbe_dcb_82599.h"

/* Callbacks for DCB netlink in the kernel */
#define BIT_DCB_MODE	0x01
#define BIT_PFC		0x02
#define BIT_PG_RX	0x04
#define BIT_PG_TX	0x08
#define BIT_APP_UPCHG	0x10
#define BIT_LINKSPEED   0x80

/* Responses for the DCB_C_SET_ALL command */
#define DCB_HW_CHG_RST  0  /* DCB configuration changed with reset */
#define DCB_NO_HW_CHG   1  /* DCB configuration did not change */
#define DCB_HW_CHG      2  /* DCB configuration changed, no reset */

int ixgbe_copy_dcb_cfg(struct ixgbe_dcb_config *src_dcb_cfg,
                       struct ixgbe_dcb_config *dst_dcb_cfg, int tc_max)
{
	struct tc_configuration *src_tc_cfg = NULL;
	struct tc_configuration *dst_tc_cfg = NULL;
	int i;

	if (!src_dcb_cfg || !dst_dcb_cfg)
		return -EINVAL;

	for (i = DCB_PG_ATTR_TC_0; i < tc_max + DCB_PG_ATTR_TC_0; i++) {
		src_tc_cfg = &src_dcb_cfg->tc_config[i - DCB_PG_ATTR_TC_0];
		dst_tc_cfg = &dst_dcb_cfg->tc_config[i - DCB_PG_ATTR_TC_0];

		dst_tc_cfg->path[DCB_TX_CONFIG].prio_type =
				src_tc_cfg->path[DCB_TX_CONFIG].prio_type;

		dst_tc_cfg->path[DCB_TX_CONFIG].bwg_id =
				src_tc_cfg->path[DCB_TX_CONFIG].bwg_id;

		dst_tc_cfg->path[DCB_TX_CONFIG].bwg_percent =
				src_tc_cfg->path[DCB_TX_CONFIG].bwg_percent;

		dst_tc_cfg->path[DCB_TX_CONFIG].up_to_tc_bitmap =
				src_tc_cfg->path[DCB_TX_CONFIG].up_to_tc_bitmap;

		dst_tc_cfg->path[DCB_RX_CONFIG].prio_type =
				src_tc_cfg->path[DCB_RX_CONFIG].prio_type;

		dst_tc_cfg->path[DCB_RX_CONFIG].bwg_id =
				src_tc_cfg->path[DCB_RX_CONFIG].bwg_id;

		dst_tc_cfg->path[DCB_RX_CONFIG].bwg_percent =
				src_tc_cfg->path[DCB_RX_CONFIG].bwg_percent;

		dst_tc_cfg->path[DCB_RX_CONFIG].up_to_tc_bitmap =
				src_tc_cfg->path[DCB_RX_CONFIG].up_to_tc_bitmap;
	}

	for (i = DCB_PG_ATTR_BW_ID_0; i < DCB_PG_ATTR_BW_ID_MAX; i++) {
		dst_dcb_cfg->bw_percentage[DCB_TX_CONFIG]
			[i-DCB_PG_ATTR_BW_ID_0] = src_dcb_cfg->bw_percentage
				[DCB_TX_CONFIG][i-DCB_PG_ATTR_BW_ID_0];
		dst_dcb_cfg->bw_percentage[DCB_RX_CONFIG]
			[i-DCB_PG_ATTR_BW_ID_0] = src_dcb_cfg->bw_percentage
				[DCB_RX_CONFIG][i-DCB_PG_ATTR_BW_ID_0];
	}

	for (i = DCB_PFC_UP_ATTR_0; i < DCB_PFC_UP_ATTR_MAX; i++) {
		dst_dcb_cfg->tc_config[i - DCB_PFC_UP_ATTR_0].dcb_pfc =
			src_dcb_cfg->tc_config[i - DCB_PFC_UP_ATTR_0].dcb_pfc;
	}

	dst_dcb_cfg->pfc_mode_enable = src_dcb_cfg->pfc_mode_enable;

	return 0;
}

static u8 ixgbe_dcbnl_get_state(struct net_device *netdev)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	return !!(adapter->flags & IXGBE_FLAG_DCB_ENABLED);
}

static u8 ixgbe_dcbnl_set_state(struct net_device *netdev, u8 state)
{
	u8 err = 0;
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	if (state > 0) {
		/* Turn on DCB */
		if (adapter->flags & IXGBE_FLAG_DCB_ENABLED)
			goto out;

		if (!(adapter->flags & IXGBE_FLAG_MSIX_ENABLED)) {
			e_err(drv, "Enable failed, needs MSI-X\n");
			err = 1;
			goto out;
		}

		if (netif_running(netdev))
			netdev->netdev_ops->ndo_stop(netdev);
		ixgbe_clear_interrupt_scheme(adapter);

		switch (adapter->hw.mac.type) {
		case ixgbe_mac_82598EB:
			adapter->last_lfc_mode = adapter->hw.fc.current_mode;
			adapter->hw.fc.requested_mode = ixgbe_fc_none;
			break;
		case ixgbe_mac_82599EB:
		case ixgbe_mac_X540:
			adapter->flags &= ~IXGBE_FLAG_FDIR_HASH_CAPABLE;
			adapter->flags &= ~IXGBE_FLAG_FDIR_PERFECT_CAPABLE;
			break;
		default:
			break;
		}

		adapter->flags |= IXGBE_FLAG_DCB_ENABLED;
		if (!netdev_get_num_tc(netdev))
			ixgbe_setup_tc(netdev, MAX_TRAFFIC_CLASS);

		ixgbe_init_interrupt_scheme(adapter);
		if (netif_running(netdev))
			netdev->netdev_ops->ndo_open(netdev);
	} else {
		/* Turn off DCB */
		if (adapter->flags & IXGBE_FLAG_DCB_ENABLED) {
			if (netif_running(netdev))
				netdev->netdev_ops->ndo_stop(netdev);
			ixgbe_clear_interrupt_scheme(adapter);

			adapter->hw.fc.requested_mode = adapter->last_lfc_mode;
			adapter->temp_dcb_cfg.pfc_mode_enable = false;
			adapter->dcb_cfg.pfc_mode_enable = false;
			adapter->flags &= ~IXGBE_FLAG_DCB_ENABLED;
			switch (adapter->hw.mac.type) {
			case ixgbe_mac_82599EB:
			case ixgbe_mac_X540:
				adapter->flags |= IXGBE_FLAG_FDIR_HASH_CAPABLE;
				break;
			default:
				break;
			}

			ixgbe_setup_tc(netdev, 0);

			ixgbe_init_interrupt_scheme(adapter);
			if (netif_running(netdev))
				netdev->netdev_ops->ndo_open(netdev);
		}
	}
out:
	return err;
}

static void ixgbe_dcbnl_get_perm_hw_addr(struct net_device *netdev,
					 u8 *perm_addr)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	int i, j;

	memset(perm_addr, 0xff, MAX_ADDR_LEN);

	for (i = 0; i < netdev->addr_len; i++)
		perm_addr[i] = adapter->hw.mac.perm_addr[i];

	switch (adapter->hw.mac.type) {
	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
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
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

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

	if ((adapter->temp_dcb_cfg.tc_config[tc].path[0].prio_type !=
	     adapter->dcb_cfg.tc_config[tc].path[0].prio_type) ||
	    (adapter->temp_dcb_cfg.tc_config[tc].path[0].bwg_id !=
	     adapter->dcb_cfg.tc_config[tc].path[0].bwg_id) ||
	    (adapter->temp_dcb_cfg.tc_config[tc].path[0].bwg_percent !=
	     adapter->dcb_cfg.tc_config[tc].path[0].bwg_percent) ||
	    (adapter->temp_dcb_cfg.tc_config[tc].path[0].up_to_tc_bitmap !=
	     adapter->dcb_cfg.tc_config[tc].path[0].up_to_tc_bitmap))
		adapter->dcb_set_bitmap |= BIT_PG_TX;
}

static void ixgbe_dcbnl_set_pg_bwg_cfg_tx(struct net_device *netdev, int bwg_id,
                                          u8 bw_pct)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	adapter->temp_dcb_cfg.bw_percentage[0][bwg_id] = bw_pct;

	if (adapter->temp_dcb_cfg.bw_percentage[0][bwg_id] !=
	    adapter->dcb_cfg.bw_percentage[0][bwg_id])
		adapter->dcb_set_bitmap |= BIT_PG_TX;
}

static void ixgbe_dcbnl_set_pg_tc_cfg_rx(struct net_device *netdev, int tc,
                                         u8 prio, u8 bwg_id, u8 bw_pct,
                                         u8 up_map)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

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

	if ((adapter->temp_dcb_cfg.tc_config[tc].path[1].prio_type !=
	     adapter->dcb_cfg.tc_config[tc].path[1].prio_type) ||
	    (adapter->temp_dcb_cfg.tc_config[tc].path[1].bwg_id !=
	     adapter->dcb_cfg.tc_config[tc].path[1].bwg_id) ||
	    (adapter->temp_dcb_cfg.tc_config[tc].path[1].bwg_percent !=
	     adapter->dcb_cfg.tc_config[tc].path[1].bwg_percent) ||
	    (adapter->temp_dcb_cfg.tc_config[tc].path[1].up_to_tc_bitmap !=
	     adapter->dcb_cfg.tc_config[tc].path[1].up_to_tc_bitmap))
		adapter->dcb_set_bitmap |= BIT_PG_RX;
}

static void ixgbe_dcbnl_set_pg_bwg_cfg_rx(struct net_device *netdev, int bwg_id,
                                          u8 bw_pct)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	adapter->temp_dcb_cfg.bw_percentage[1][bwg_id] = bw_pct;

	if (adapter->temp_dcb_cfg.bw_percentage[1][bwg_id] !=
	    adapter->dcb_cfg.bw_percentage[1][bwg_id])
		adapter->dcb_set_bitmap |= BIT_PG_RX;
}

static void ixgbe_dcbnl_get_pg_tc_cfg_tx(struct net_device *netdev, int tc,
                                         u8 *prio, u8 *bwg_id, u8 *bw_pct,
                                         u8 *up_map)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	*prio = adapter->dcb_cfg.tc_config[tc].path[0].prio_type;
	*bwg_id = adapter->dcb_cfg.tc_config[tc].path[0].bwg_id;
	*bw_pct = adapter->dcb_cfg.tc_config[tc].path[0].bwg_percent;
	*up_map = adapter->dcb_cfg.tc_config[tc].path[0].up_to_tc_bitmap;
}

static void ixgbe_dcbnl_get_pg_bwg_cfg_tx(struct net_device *netdev, int bwg_id,
                                          u8 *bw_pct)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	*bw_pct = adapter->dcb_cfg.bw_percentage[0][bwg_id];
}

static void ixgbe_dcbnl_get_pg_tc_cfg_rx(struct net_device *netdev, int tc,
                                         u8 *prio, u8 *bwg_id, u8 *bw_pct,
                                         u8 *up_map)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	*prio = adapter->dcb_cfg.tc_config[tc].path[1].prio_type;
	*bwg_id = adapter->dcb_cfg.tc_config[tc].path[1].bwg_id;
	*bw_pct = adapter->dcb_cfg.tc_config[tc].path[1].bwg_percent;
	*up_map = adapter->dcb_cfg.tc_config[tc].path[1].up_to_tc_bitmap;
}

static void ixgbe_dcbnl_get_pg_bwg_cfg_rx(struct net_device *netdev, int bwg_id,
                                          u8 *bw_pct)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	*bw_pct = adapter->dcb_cfg.bw_percentage[1][bwg_id];
}

static void ixgbe_dcbnl_set_pfc_cfg(struct net_device *netdev, int priority,
                                    u8 setting)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	adapter->temp_dcb_cfg.tc_config[priority].dcb_pfc = setting;
	if (adapter->temp_dcb_cfg.tc_config[priority].dcb_pfc !=
	    adapter->dcb_cfg.tc_config[priority].dcb_pfc) {
		adapter->dcb_set_bitmap |= BIT_PFC;
		adapter->temp_dcb_cfg.pfc_mode_enable = true;
	}
}

static void ixgbe_dcbnl_get_pfc_cfg(struct net_device *netdev, int priority,
                                    u8 *setting)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	*setting = adapter->dcb_cfg.tc_config[priority].dcb_pfc;
}

static u8 ixgbe_dcbnl_set_all(struct net_device *netdev)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	struct dcb_app app = {
			      .selector = DCB_APP_IDTYPE_ETHTYPE,
			      .protocol = ETH_P_FCOE,
			     };
	u8 up = dcb_getapp(netdev, &app);
	int ret;

	ret = ixgbe_copy_dcb_cfg(&adapter->temp_dcb_cfg, &adapter->dcb_cfg,
				 MAX_TRAFFIC_CLASS);
	if (ret)
		return DCB_NO_HW_CHG;

	/* In IEEE mode app data must be parsed into DCBX format for
	 * hardware routines.
	 */
	if (adapter->dcbx_cap & DCB_CAP_DCBX_VER_IEEE)
		up = (1 << up);

#ifdef IXGBE_FCOE
	if (up && (up != (1 << adapter->fcoe.up)))
		adapter->dcb_set_bitmap |= BIT_APP_UPCHG;

	/*
	 * Only take down the adapter if an app change occurred. FCoE
	 * may shuffle tx rings in this case and this can not be done
	 * without a reset currently.
	 */
	if (adapter->dcb_set_bitmap & BIT_APP_UPCHG) {
		while (test_and_set_bit(__IXGBE_RESETTING, &adapter->state))
			usleep_range(1000, 2000);

		ixgbe_fcoe_setapp(adapter, up);

		if (netif_running(netdev))
			netdev->netdev_ops->ndo_stop(netdev);
		ixgbe_clear_interrupt_scheme(adapter);
	}
#endif

	if (adapter->dcb_cfg.pfc_mode_enable) {
		switch (adapter->hw.mac.type) {
		case ixgbe_mac_82599EB:
		case ixgbe_mac_X540:
			if (adapter->hw.fc.current_mode != ixgbe_fc_pfc)
				adapter->last_lfc_mode =
				                  adapter->hw.fc.current_mode;
			break;
		default:
			break;
		}
		adapter->hw.fc.requested_mode = ixgbe_fc_pfc;
	} else {
		switch (adapter->hw.mac.type) {
		case ixgbe_mac_82598EB:
			adapter->hw.fc.requested_mode = ixgbe_fc_none;
			break;
		case ixgbe_mac_82599EB:
		case ixgbe_mac_X540:
			adapter->hw.fc.requested_mode = adapter->last_lfc_mode;
			break;
		default:
			break;
		}
	}

#ifdef IXGBE_FCOE
	if (adapter->dcb_set_bitmap & BIT_APP_UPCHG) {
		ixgbe_init_interrupt_scheme(adapter);
		if (netif_running(netdev))
			netdev->netdev_ops->ndo_open(netdev);
		ret = DCB_HW_CHG_RST;
	}
#endif

	if (adapter->dcb_set_bitmap & BIT_PFC) {
		u8 pfc_en;
		ixgbe_dcb_unpack_pfc(&adapter->dcb_cfg, &pfc_en);
		ixgbe_dcb_hw_pfc_config(&adapter->hw, pfc_en);
		ret = DCB_HW_CHG;
	}

	if (adapter->dcb_set_bitmap & (BIT_PG_TX|BIT_PG_RX)) {
		u16 refill[MAX_TRAFFIC_CLASS], max[MAX_TRAFFIC_CLASS];
		u8 bwg_id[MAX_TRAFFIC_CLASS], prio_type[MAX_TRAFFIC_CLASS];
		/* Priority to TC mapping in CEE case default to 1:1 */
		u8 prio_tc[MAX_TRAFFIC_CLASS] = {0, 1, 2, 3, 4, 5, 6, 7};
		int max_frame = adapter->netdev->mtu + ETH_HLEN + ETH_FCS_LEN;

#ifdef CONFIG_FCOE
		if (adapter->netdev->features & NETIF_F_FCOE_MTU)
			max_frame = max(max_frame, IXGBE_FCOE_JUMBO_FRAME_SIZE);
#endif

		ixgbe_dcb_calculate_tc_credits(&adapter->hw, &adapter->dcb_cfg,
					       max_frame, DCB_TX_CONFIG);
		ixgbe_dcb_calculate_tc_credits(&adapter->hw, &adapter->dcb_cfg,
					       max_frame, DCB_RX_CONFIG);

		ixgbe_dcb_unpack_refill(&adapter->dcb_cfg,
					DCB_TX_CONFIG, refill);
		ixgbe_dcb_unpack_max(&adapter->dcb_cfg, max);
		ixgbe_dcb_unpack_bwgid(&adapter->dcb_cfg,
				       DCB_TX_CONFIG, bwg_id);
		ixgbe_dcb_unpack_prio(&adapter->dcb_cfg,
				      DCB_TX_CONFIG, prio_type);

		ixgbe_dcb_hw_ets_config(&adapter->hw, refill, max,
					bwg_id, prio_type, prio_tc);
	}

	if (adapter->dcb_cfg.pfc_mode_enable)
		adapter->hw.fc.current_mode = ixgbe_fc_pfc;

	if (adapter->dcb_set_bitmap & BIT_APP_UPCHG)
		clear_bit(__IXGBE_RESETTING, &adapter->state);
	adapter->dcb_set_bitmap = 0x00;
	return ret;
}

static u8 ixgbe_dcbnl_getcap(struct net_device *netdev, int capid, u8 *cap)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

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

static u8 ixgbe_dcbnl_getnumtcs(struct net_device *netdev, int tcid, u8 *num)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	u8 rval = 0;

	if (adapter->flags & IXGBE_FLAG_DCB_ENABLED) {
		switch (tcid) {
		case DCB_NUMTCS_ATTR_PG:
			*num = MAX_TRAFFIC_CLASS;
			break;
		case DCB_NUMTCS_ATTR_PFC:
			*num = MAX_TRAFFIC_CLASS;
			break;
		default:
			rval = -EINVAL;
			break;
		}
	} else {
		rval = -EINVAL;
	}

	return rval;
}

static u8 ixgbe_dcbnl_setnumtcs(struct net_device *netdev, int tcid, u8 num)
{
	return -EINVAL;
}

static u8 ixgbe_dcbnl_getpfcstate(struct net_device *netdev)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	return adapter->dcb_cfg.pfc_mode_enable;
}

static void ixgbe_dcbnl_setpfcstate(struct net_device *netdev, u8 state)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	adapter->temp_dcb_cfg.pfc_mode_enable = state;
	if (adapter->temp_dcb_cfg.pfc_mode_enable !=
		adapter->dcb_cfg.pfc_mode_enable)
		adapter->dcb_set_bitmap |= BIT_PFC;
}

/**
 * ixgbe_dcbnl_getapp - retrieve the DCBX application user priority
 * @netdev : the corresponding netdev
 * @idtype : identifies the id as ether type or TCP/UDP port number
 * @id: id is either ether type or TCP/UDP port number
 *
 * Returns : on success, returns a non-zero 802.1p user priority bitmap
 * otherwise returns 0 as the invalid user priority bitmap to indicate an
 * error.
 */
static u8 ixgbe_dcbnl_getapp(struct net_device *netdev, u8 idtype, u16 id)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	struct dcb_app app = {
				.selector = idtype,
				.protocol = id,
			     };

	if (!(adapter->dcbx_cap & DCB_CAP_DCBX_VER_CEE))
		return 0;

	return dcb_getapp(netdev, &app);
}

static int ixgbe_dcbnl_ieee_getets(struct net_device *dev,
				   struct ieee_ets *ets)
{
	struct ixgbe_adapter *adapter = netdev_priv(dev);
	struct ieee_ets *my_ets = adapter->ixgbe_ieee_ets;

	/* No IEEE PFC settings available */
	if (!my_ets)
		return -EINVAL;

	ets->ets_cap = MAX_TRAFFIC_CLASS;
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
	struct ixgbe_adapter *adapter = netdev_priv(dev);
	__u16 refill[IEEE_8021QAZ_MAX_TCS], max[IEEE_8021QAZ_MAX_TCS];
	__u8 prio_type[IEEE_8021QAZ_MAX_TCS];
	int max_frame = dev->mtu + ETH_HLEN + ETH_FCS_LEN;
	int i, err;
	__u64 *p = (__u64 *) ets->prio_tc;
	/* naively give each TC a bwg to map onto CEE hardware */
	__u8 bwg_id[IEEE_8021QAZ_MAX_TCS] = {0, 1, 2, 3, 4, 5, 6, 7};

	if (!(adapter->dcbx_cap & DCB_CAP_DCBX_VER_IEEE))
		return -EINVAL;

	if (!adapter->ixgbe_ieee_ets) {
		adapter->ixgbe_ieee_ets = kmalloc(sizeof(struct ieee_ets),
						  GFP_KERNEL);
		if (!adapter->ixgbe_ieee_ets)
			return -ENOMEM;
	}

	memcpy(adapter->ixgbe_ieee_ets, ets, sizeof(*adapter->ixgbe_ieee_ets));

	/* Map TSA onto CEE prio type */
	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++) {
		switch (ets->tc_tsa[i]) {
		case IEEE_8021QAZ_TSA_STRICT:
			prio_type[i] = 2;
			break;
		case IEEE_8021QAZ_TSA_ETS:
			prio_type[i] = 0;
			break;
		default:
			/* Hardware only supports priority strict or
			 * ETS transmission selection algorithms if
			 * we receive some other value from dcbnl
			 * throw an error
			 */
			return -EINVAL;
		}
	}

	if (*p)
		ixgbe_dcbnl_set_state(dev, 1);
	else
		ixgbe_dcbnl_set_state(dev, 0);

	ixgbe_ieee_credits(ets->tc_tx_bw, refill, max, max_frame);
	err = ixgbe_dcb_hw_ets_config(&adapter->hw, refill, max,
				      bwg_id, prio_type, ets->prio_tc);
	return err;
}

static int ixgbe_dcbnl_ieee_getpfc(struct net_device *dev,
				   struct ieee_pfc *pfc)
{
	struct ixgbe_adapter *adapter = netdev_priv(dev);
	struct ieee_pfc *my_pfc = adapter->ixgbe_ieee_pfc;
	int i;

	/* No IEEE PFC settings available */
	if (!my_pfc)
		return -EINVAL;

	pfc->pfc_cap = MAX_TRAFFIC_CLASS;
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
	struct ixgbe_adapter *adapter = netdev_priv(dev);
	int err;

	if (!(adapter->dcbx_cap & DCB_CAP_DCBX_VER_IEEE))
		return -EINVAL;

	if (!adapter->ixgbe_ieee_pfc) {
		adapter->ixgbe_ieee_pfc = kmalloc(sizeof(struct ieee_pfc),
						  GFP_KERNEL);
		if (!adapter->ixgbe_ieee_pfc)
			return -ENOMEM;
	}

	memcpy(adapter->ixgbe_ieee_pfc, pfc, sizeof(*adapter->ixgbe_ieee_pfc));
	err = ixgbe_dcb_hw_pfc_config(&adapter->hw, pfc->pfc_en);
	return err;
}

static int ixgbe_dcbnl_ieee_setapp(struct net_device *dev,
				   struct dcb_app *app)
{
	struct ixgbe_adapter *adapter = netdev_priv(dev);

	if (!(adapter->dcbx_cap & DCB_CAP_DCBX_VER_IEEE))
		return -EINVAL;

	dcb_setapp(dev, app);

#ifdef IXGBE_FCOE
	if (app->selector == 1 && app->protocol == ETH_P_FCOE &&
	    adapter->fcoe.tc == app->priority)
		ixgbe_dcbnl_set_all(dev);
#endif
	return 0;
}

static u8 ixgbe_dcbnl_getdcbx(struct net_device *dev)
{
	struct ixgbe_adapter *adapter = netdev_priv(dev);
	return adapter->dcbx_cap;
}

static u8 ixgbe_dcbnl_setdcbx(struct net_device *dev, u8 mode)
{
	struct ixgbe_adapter *adapter = netdev_priv(dev);
	struct ieee_ets ets = {0};
	struct ieee_pfc pfc = {0};

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
		adapter->dcb_set_bitmap |= (BIT_PFC & BIT_PG_TX & BIT_PG_RX);
		ixgbe_dcbnl_set_all(dev);
	} else {
		/* Drop into single TC mode strict priority as this
		 * indicates CEE and IEEE versions are disabled
		 */
		ixgbe_dcbnl_ieee_setets(dev, &ets);
		ixgbe_dcbnl_ieee_setpfc(dev, &pfc);
		ixgbe_dcbnl_set_state(dev, 0);
	}

	return 0;
}

const struct dcbnl_rtnl_ops dcbnl_ops = {
	.ieee_getets	= ixgbe_dcbnl_ieee_getets,
	.ieee_setets	= ixgbe_dcbnl_ieee_setets,
	.ieee_getpfc	= ixgbe_dcbnl_ieee_getpfc,
	.ieee_setpfc	= ixgbe_dcbnl_ieee_setpfc,
	.ieee_setapp	= ixgbe_dcbnl_ieee_setapp,
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
