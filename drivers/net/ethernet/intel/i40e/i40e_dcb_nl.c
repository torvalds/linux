/*******************************************************************************
 *
 * Intel Ethernet Controller XL710 Family Linux Driver
 * Copyright(c) 2013 - 2014 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 ******************************************************************************/

#ifdef CONFIG_I40E_DCB
#include "i40e.h"
#include <net/dcbnl.h>

/**
 * i40e_get_pfc_delay - retrieve PFC Link Delay
 * @hw: pointer to hardware struct
 * @delay: holds the PFC Link delay value
 *
 * Returns PFC Link Delay from the PRTDCB_GENC.PFCLDA
 **/
static void i40e_get_pfc_delay(struct i40e_hw *hw, u16 *delay)
{
	u32 val;

	val = rd32(hw, I40E_PRTDCB_GENC);
	*delay = (u16)((val & I40E_PRTDCB_GENC_PFCLDA_MASK) >>
		       I40E_PRTDCB_GENC_PFCLDA_SHIFT);
}

/**
 * i40e_dcbnl_ieee_getets - retrieve local IEEE ETS configuration
 * @netdev: the corresponding netdev
 * @ets: structure to hold the ETS information
 *
 * Returns local IEEE ETS configuration
 **/
static int i40e_dcbnl_ieee_getets(struct net_device *dev,
				  struct ieee_ets *ets)
{
	struct i40e_pf *pf = i40e_netdev_to_pf(dev);
	struct i40e_dcbx_config *dcbxcfg;
	struct i40e_hw *hw = &pf->hw;

	if (!(pf->dcbx_cap & DCB_CAP_DCBX_VER_IEEE))
		return -EINVAL;

	dcbxcfg = &hw->local_dcbx_config;
	ets->willing = dcbxcfg->etscfg.willing;
	ets->ets_cap = dcbxcfg->etscfg.maxtcs;
	ets->cbs = dcbxcfg->etscfg.cbs;
	memcpy(ets->tc_tx_bw, dcbxcfg->etscfg.tcbwtable,
		sizeof(ets->tc_tx_bw));
	memcpy(ets->tc_rx_bw, dcbxcfg->etscfg.tcbwtable,
		sizeof(ets->tc_rx_bw));
	memcpy(ets->tc_tsa, dcbxcfg->etscfg.tsatable,
		sizeof(ets->tc_tsa));
	memcpy(ets->prio_tc, dcbxcfg->etscfg.prioritytable,
		sizeof(ets->prio_tc));
	memcpy(ets->tc_reco_bw, dcbxcfg->etsrec.tcbwtable,
		sizeof(ets->tc_reco_bw));
	memcpy(ets->tc_reco_tsa, dcbxcfg->etsrec.tsatable,
		sizeof(ets->tc_reco_tsa));
	memcpy(ets->reco_prio_tc, dcbxcfg->etscfg.prioritytable,
		sizeof(ets->reco_prio_tc));

	return 0;
}

/**
 * i40e_dcbnl_ieee_getpfc - retrieve local IEEE PFC configuration
 * @netdev: the corresponding netdev
 * @ets: structure to hold the PFC information
 *
 * Returns local IEEE PFC configuration
 **/
static int i40e_dcbnl_ieee_getpfc(struct net_device *dev,
				  struct ieee_pfc *pfc)
{
	struct i40e_pf *pf = i40e_netdev_to_pf(dev);
	struct i40e_dcbx_config *dcbxcfg;
	struct i40e_hw *hw = &pf->hw;
	int i;

	if (!(pf->dcbx_cap & DCB_CAP_DCBX_VER_IEEE))
		return -EINVAL;

	dcbxcfg = &hw->local_dcbx_config;
	pfc->pfc_cap = dcbxcfg->pfc.pfccap;
	pfc->pfc_en = dcbxcfg->pfc.pfcenable;
	pfc->mbc = dcbxcfg->pfc.mbc;
	i40e_get_pfc_delay(hw, &pfc->delay);

	/* Get Requests/Indicatiosn */
	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++) {
		pfc->requests[i] = pf->stats.priority_xoff_tx[i];
		pfc->indications[i] = pf->stats.priority_xoff_rx[i];
	}

	return 0;
}

/**
 * i40e_dcbnl_getdcbx - retrieve current DCBx capability
 * @netdev: the corresponding netdev
 *
 * Returns DCBx capability features
 **/
static u8 i40e_dcbnl_getdcbx(struct net_device *dev)
{
	struct i40e_pf *pf = i40e_netdev_to_pf(dev);

	return pf->dcbx_cap;
}

/**
 * i40e_dcbnl_get_perm_hw_addr - MAC address used by DCBx
 * @netdev: the corresponding netdev
 *
 * Returns the SAN MAC address used for LLDP exchange
 **/
static void i40e_dcbnl_get_perm_hw_addr(struct net_device *dev,
					u8 *perm_addr)
{
	struct i40e_pf *pf = i40e_netdev_to_pf(dev);
	int i, j;

	memset(perm_addr, 0xff, MAX_ADDR_LEN);

	for (i = 0; i < dev->addr_len; i++)
		perm_addr[i] = pf->hw.mac.perm_addr[i];

	for (j = 0; j < dev->addr_len; j++, i++)
		perm_addr[i] = pf->hw.mac.san_addr[j];
}

static const struct dcbnl_rtnl_ops dcbnl_ops = {
	.ieee_getets	= i40e_dcbnl_ieee_getets,
	.ieee_getpfc	= i40e_dcbnl_ieee_getpfc,
	.getdcbx	= i40e_dcbnl_getdcbx,
	.getpermhwaddr  = i40e_dcbnl_get_perm_hw_addr,
};

/**
 * i40e_dcbnl_set_all - set all the apps and ieee data from DCBx config
 * @vsi: the corresponding vsi
 *
 * Set up all the IEEE APPs in the DCBNL App Table and generate event for
 * other settings
 **/
void i40e_dcbnl_set_all(struct i40e_vsi *vsi)
{
	struct net_device *dev = vsi->netdev;
	struct i40e_pf *pf = i40e_netdev_to_pf(dev);
	struct i40e_dcbx_config *dcbxcfg;
	struct i40e_hw *hw = &pf->hw;
	struct dcb_app sapp;
	u8 prio, tc_map;
	int i;

	/* DCB not enabled */
	if (!(pf->flags & I40E_FLAG_DCB_ENABLED))
		return;

	/* MFP mode but not an iSCSI PF so return */
	if ((pf->flags & I40E_FLAG_MFP_ENABLED) && !(pf->hw.func_caps.iscsi))
		return;

	dcbxcfg = &hw->local_dcbx_config;

	/* Set up all the App TLVs if DCBx is negotiated */
	for (i = 0; i < dcbxcfg->numapps; i++) {
		prio = dcbxcfg->app[i].priority;
		tc_map = (1 << dcbxcfg->etscfg.prioritytable[prio]);

		/* Add APP only if the TC is enabled for this VSI */
		if (tc_map & vsi->tc_config.enabled_tc) {
			sapp.selector = dcbxcfg->app[i].selector;
			sapp.protocol = dcbxcfg->app[i].protocolid;
			sapp.priority = prio;
			dcb_ieee_setapp(dev, &sapp);
		}
	}

	/* Notify user-space of the changes */
	dcbnl_ieee_notify(dev, RTM_SETDCB, DCB_CMD_IEEE_SET, 0, 0);
}

/**
 * i40e_dcbnl_vsi_del_app - Delete APP for given VSI
 * @vsi: the corresponding vsi
 * @app: APP to delete
 *
 * Delete given APP from the DCBNL APP table for given
 * VSI
 **/
static int i40e_dcbnl_vsi_del_app(struct i40e_vsi *vsi,
				  struct i40e_dcb_app_priority_table *app)
{
	struct net_device *dev = vsi->netdev;
	struct dcb_app sapp;

	if (!dev)
		return -EINVAL;

	sapp.selector = app->selector;
	sapp.protocol = app->protocolid;
	sapp.priority = app->priority;
	return dcb_ieee_delapp(dev, &sapp);
}

/**
 * i40e_dcbnl_del_app - Delete APP on all VSIs
 * @pf: the corresponding PF
 * @app: APP to delete
 *
 * Delete given APP from all the VSIs for given PF
 **/
static void i40e_dcbnl_del_app(struct i40e_pf *pf,
			       struct i40e_dcb_app_priority_table *app)
{
	int v, err;
	for (v = 0; v < pf->num_alloc_vsi; v++) {
		if (pf->vsi[v] && pf->vsi[v]->netdev) {
			err = i40e_dcbnl_vsi_del_app(pf->vsi[v], app);
			if (err)
				dev_info(&pf->pdev->dev, "%s: Failed deleting app for VSI seid=%d err=%d sel=%d proto=0x%x prio=%d\n",
					 __func__, pf->vsi[v]->seid,
					 err, app->selector,
					 app->protocolid, app->priority);
		}
	}
}

/**
 * i40e_dcbnl_find_app - Search APP in given DCB config
 * @cfg: DCBX configuration data
 * @app: APP to search for
 *
 * Find given APP in the DCB configuration
 **/
static bool i40e_dcbnl_find_app(struct i40e_dcbx_config *cfg,
				struct i40e_dcb_app_priority_table *app)
{
	int i;

	for (i = 0; i < cfg->numapps; i++) {
		if (app->selector == cfg->app[i].selector &&
		    app->protocolid == cfg->app[i].protocolid &&
		    app->priority == cfg->app[i].priority)
			return true;
	}

	return false;
}

/**
 * i40e_dcbnl_flush_apps - Delete all removed APPs
 * @pf: the corresponding PF
 * @old_cfg: old DCBX configuration data
 * @new_cfg: new DCBX configuration data
 *
 * Find and delete all APPs that are not present in the passed
 * DCB configuration
 **/
void i40e_dcbnl_flush_apps(struct i40e_pf *pf,
			   struct i40e_dcbx_config *old_cfg,
			   struct i40e_dcbx_config *new_cfg)
{
	struct i40e_dcb_app_priority_table app;
	int i;

	/* MFP mode but not an iSCSI PF so return */
	if ((pf->flags & I40E_FLAG_MFP_ENABLED) && !(pf->hw.func_caps.iscsi))
		return;

	for (i = 0; i < old_cfg->numapps; i++) {
		app = old_cfg->app[i];
		/* The APP is not available anymore delete it */
		if (!i40e_dcbnl_find_app(new_cfg, &app))
			i40e_dcbnl_del_app(pf, &app);
	}
}

/**
 * i40e_dcbnl_setup - DCBNL setup
 * @vsi: the corresponding vsi
 *
 * Set up DCBNL ops and initial APP TLVs
 **/
void i40e_dcbnl_setup(struct i40e_vsi *vsi)
{
	struct net_device *dev = vsi->netdev;
	struct i40e_pf *pf = i40e_netdev_to_pf(dev);

	/* Not DCB capable */
	if (!(pf->flags & I40E_FLAG_DCB_CAPABLE))
		return;

	dev->dcbnl_ops = &dcbnl_ops;

	/* Set initial IEEE DCB settings */
	i40e_dcbnl_set_all(vsi);
}
#endif /* CONFIG_I40E_DCB */
