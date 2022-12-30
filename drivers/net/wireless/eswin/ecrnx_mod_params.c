/**
******************************************************************************
*
* @file ecrnx_mod_params.c
*
* @brief Set configuration according to modules parameters
*
* Copyright (C) ESWIN 2015-2020
*
******************************************************************************
*/
#include <linux/module.h>
#include <linux/rtnetlink.h>

#include "ecrnx_defs.h"
#include "ecrnx_tx.h"
#include "hal_desc.h"
#include "ecrnx_cfgfile.h"
#include "reg_access.h"
#include "ecrnx_compat.h"

#ifdef CONFIG_ECRNX_SOFTMAC
#define COMMON_PARAM(name, default_softmac, default_fullmac)    \
    .name = default_softmac,
#define SOFTMAC_PARAM(name, default) .name = default,
#define FULLMAC_PARAM(name, default)
#else
#define COMMON_PARAM(name, default_softmac, default_fullmac)    \
    .name = default_fullmac,
#define SOFTMAC_PARAM(name, default)
#define FULLMAC_PARAM(name, default) .name = default,
#endif /* CONFIG_ECRNX_SOFTMAC */

struct ecrnx_mod_params ecrnx_mod_params = {
    /* common parameters */
    COMMON_PARAM(ht_on, true, true)
    COMMON_PARAM(vht_on, false, false)
    COMMON_PARAM(he_on, true, true)
#ifdef CONFIG_6600_HAL
    COMMON_PARAM(he_mcs_map, IEEE80211_HE_MCS_SUPPORT_0_7, IEEE80211_HE_MCS_SUPPORT_0_7)
#else
    COMMON_PARAM(he_mcs_map, IEEE80211_HE_MCS_SUPPORT_0_9, IEEE80211_HE_MCS_SUPPORT_0_9)
#endif
    COMMON_PARAM(he_ul_on, false, false)
    COMMON_PARAM(ldpc_on, false, false)
    COMMON_PARAM(stbc_on, false, false)
    COMMON_PARAM(gf_rx_on, true, true)
    COMMON_PARAM(phy_cfg, 0, 0)
    COMMON_PARAM(uapsd_timeout, 300, 300)
    COMMON_PARAM(ap_uapsd_on, true, true)
    COMMON_PARAM(sgi, true, true)
    COMMON_PARAM(sgi80, true, true)
    COMMON_PARAM(use_2040, 1, 1)
    COMMON_PARAM(nss, 1, 1)
    COMMON_PARAM(amsdu_rx_max, 2, 2)
    COMMON_PARAM(bfmee, true, true)
    COMMON_PARAM(bfmer, true, true)
    COMMON_PARAM(mesh, true, true)
    COMMON_PARAM(murx, true, true)
    COMMON_PARAM(mutx, true, true)
    COMMON_PARAM(mutx_on, true, true)
    COMMON_PARAM(use_80, false, false)
    COMMON_PARAM(custregd, false, false)
    COMMON_PARAM(custchan, false, false)
    COMMON_PARAM(roc_dur_max, 500, 500)
    COMMON_PARAM(listen_itv, 0, 0)
    COMMON_PARAM(listen_bcmc, true, true)
    COMMON_PARAM(lp_clk_ppm, 20, 20)
    COMMON_PARAM(ps_on, true, true)
    COMMON_PARAM(tx_lft, ECRNX_TX_LIFETIME_MS, ECRNX_TX_LIFETIME_MS)
    COMMON_PARAM(amsdu_maxnb, NX_TX_PAYLOAD_MAX, NX_TX_PAYLOAD_MAX)
    // By default, only enable UAPSD for Voice queue (see IEEE80211_DEFAULT_UAPSD_QUEUE comment)
    COMMON_PARAM(uapsd_queues, IEEE80211_WMM_IE_STA_QOSINFO_AC_VO, IEEE80211_WMM_IE_STA_QOSINFO_AC_VO)
    COMMON_PARAM(tdls, true, true)
    COMMON_PARAM(uf, true, true)
    COMMON_PARAM(ftl, "", "")
    COMMON_PARAM(dpsm, true, true)
    COMMON_PARAM(tx_to_bk, 0, 0)
    COMMON_PARAM(tx_to_be, 0, 0)
    COMMON_PARAM(tx_to_vi, 0, 0)
    COMMON_PARAM(tx_to_vo, 0, 0)

    /* SOFTMAC only parameters */
    SOFTMAC_PARAM(mfp_on, false)
    SOFTMAC_PARAM(gf_on, false)
    SOFTMAC_PARAM(bwsig_on, true)
    SOFTMAC_PARAM(dynbw_on, true)
    SOFTMAC_PARAM(agg_tx, true)
    SOFTMAC_PARAM(amsdu_force, 0)
    SOFTMAC_PARAM(rc_probes_on, false)
    SOFTMAC_PARAM(cmon, true)
    SOFTMAC_PARAM(hwscan, true)
    SOFTMAC_PARAM(autobcn, true)

    /* FULLMAC only parameters */
    FULLMAC_PARAM(ant_div, false)
};

#ifdef CONFIG_ECRNX_SOFTMAC
/* SOFTMAC specific parameters */
module_param_named(mfp_on, ecrnx_mod_params.mfp_on, bool, S_IRUGO);
MODULE_PARM_DESC(mfp_on, "Enable MFP (11w) (Default: 0)");

module_param_named(gf_on, ecrnx_mod_params.gf_on, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(gf_on, "Try TXing Green Field if peer supports it (Default: 0)");

module_param_named(bwsig_on, ecrnx_mod_params.bwsig_on, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(bwsig_on, "Enable bandwidth signaling (VHT tx) (Default: 1)");

module_param_named(dynbw_on, ecrnx_mod_params.dynbw_on, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(dynbw_on, "Enable dynamic bandwidth (VHT tx) (Default: 1)");

module_param_named(agg_tx, ecrnx_mod_params.agg_tx, bool, S_IRUGO);
MODULE_PARM_DESC(agg_tx, "Use A-MPDU in TX (Default: 1)");

module_param_named(amsdu_force, ecrnx_mod_params.amsdu_force, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(amsdu_force, "Use A-MSDU in TX: 0-if advertised, 1-yes, 2-no (Default: 0)");

module_param_named(rc_probes_on, ecrnx_mod_params.rc_probes_on, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(rc_probes_on, "IEEE80211_TX_CTL_RATE_CTRL_PROBE is 1st in AMPDU (Default: 0)");

module_param_named(cmon, ecrnx_mod_params.cmon, bool, S_IRUGO);
MODULE_PARM_DESC(cmon, "Connection monitoring work handled by the FW (Default: 1)");

module_param_named(hwscan, ecrnx_mod_params.hwscan, bool, S_IRUGO);
MODULE_PARM_DESC(hwscan, "Scan work handled by the FW (Default: 1)");

module_param_named(autobcn, ecrnx_mod_params.autobcn, bool, S_IRUGO);
MODULE_PARM_DESC(autobcn, "Beacon transmission done autonomously by the FW (Default: 1)");

#else
/* FULLMAC specific parameters*/
module_param_named(ant_div, ecrnx_mod_params.ant_div, bool, S_IRUGO);
MODULE_PARM_DESC(ant_div, "Enable Antenna Diversity (Default: 0)");
#endif /* CONFIG_ECRNX_SOFTMAC */

module_param_named(ht_on, ecrnx_mod_params.ht_on, bool, S_IRUGO);
MODULE_PARM_DESC(ht_on, "Enable HT (Default: 1)");

module_param_named(vht_on, ecrnx_mod_params.vht_on, bool, S_IRUGO);
MODULE_PARM_DESC(vht_on, "Enable VHT (Default: 1)");

module_param_named(he_on, ecrnx_mod_params.he_on, bool, S_IRUGO);
MODULE_PARM_DESC(he_on, "Enable HE (Default: 1)");

module_param_named(mcs_map, ecrnx_mod_params.mcs_map, int, S_IRUGO);
MODULE_PARM_DESC(mcs_map,  "VHT MCS map value  0: MCS0_7, 1: MCS0_8, 2: MCS0_9"
                 " (Default: 2)");

module_param_named(he_mcs_map, ecrnx_mod_params.he_mcs_map, int, S_IRUGO);
MODULE_PARM_DESC(he_mcs_map,  "HE MCS map value  0: MCS0_7, 1: MCS0_9, 2: MCS0_11"
                 " (Default: 2)");

module_param_named(he_ul_on, ecrnx_mod_params.he_ul_on, bool, S_IRUGO);
MODULE_PARM_DESC(he_ul_on, "Enable HE OFDMA UL (Default: 0)");

module_param_named(amsdu_maxnb, ecrnx_mod_params.amsdu_maxnb, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(amsdu_maxnb, "Maximum number of MSDUs inside an A-MSDU in TX: (Default: NX_TX_PAYLOAD_MAX)");

module_param_named(ps_on, ecrnx_mod_params.ps_on, bool, S_IRUGO);
MODULE_PARM_DESC(ps_on, "Enable PowerSaving (Default: 1-Enabled)");

module_param_named(tx_lft, ecrnx_mod_params.tx_lft, int, 0644);
MODULE_PARM_DESC(tx_lft, "Tx lifetime (ms) - setting it to 0 disables retries "
                 "(Default: "__stringify(ECRNX_TX_LIFETIME_MS)")");

module_param_named(ldpc_on, ecrnx_mod_params.ldpc_on, bool, S_IRUGO);
MODULE_PARM_DESC(ldpc_on, "Enable LDPC (Default: 1)");

module_param_named(stbc_on, ecrnx_mod_params.stbc_on, bool, S_IRUGO);
MODULE_PARM_DESC(stbc_on, "Enable STBC in RX (Default: 1)");

module_param_named(gf_rx_on, ecrnx_mod_params.gf_rx_on, bool, S_IRUGO);
MODULE_PARM_DESC(gf_rx_on, "Enable HT greenfield in reception (Default: 1)");

module_param_named(phycfg, ecrnx_mod_params.phy_cfg, int, S_IRUGO);
MODULE_PARM_DESC(phycfg, "Main RF Path (Default: 0)");

module_param_named(uapsd_timeout, ecrnx_mod_params.uapsd_timeout, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(uapsd_timeout,
                 "UAPSD Timer timeout, in ms (Default: 300). If 0, UAPSD is disabled");

module_param_named(uapsd_queues, ecrnx_mod_params.uapsd_queues, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(uapsd_queues, "UAPSD Queues, integer value, must be seen as a bitfield\n"
                 "        Bit 0 = VO\n"
                 "        Bit 1 = VI\n"
                 "        Bit 2 = BK\n"
                 "        Bit 3 = BE\n"
                 "     -> uapsd_queues=7 will enable uapsd for VO, VI and BK queues");

module_param_named(ap_uapsd_on, ecrnx_mod_params.ap_uapsd_on, bool, S_IRUGO);
MODULE_PARM_DESC(ap_uapsd_on, "Enable UAPSD in AP mode (Default: 1)");

module_param_named(sgi, ecrnx_mod_params.sgi, bool, S_IRUGO);
MODULE_PARM_DESC(sgi, "Advertise Short Guard Interval support (Default: 1)");

module_param_named(sgi80, ecrnx_mod_params.sgi80, bool, S_IRUGO);
MODULE_PARM_DESC(sgi80, "Advertise Short Guard Interval support for 80MHz (Default: 1)");

module_param_named(use_2040, ecrnx_mod_params.use_2040, bool, S_IRUGO);
MODULE_PARM_DESC(use_2040, "Enable 40MHz (Default: 1)");

module_param_named(use_80, ecrnx_mod_params.use_80, bool, S_IRUGO);
MODULE_PARM_DESC(use_80, "Enable 80MHz (Default: 1)");

module_param_named(custregd, ecrnx_mod_params.custregd, bool, S_IRUGO);
MODULE_PARM_DESC(custregd,
                 "Use permissive custom regulatory rules (for testing ONLY) (Default: 0)");

module_param_named(custchan, ecrnx_mod_params.custchan, bool, S_IRUGO);
MODULE_PARM_DESC(custchan,
                 "Extend channel set to non-standard channels (for testing ONLY) (Default: 0)");

module_param_named(nss, ecrnx_mod_params.nss, int, S_IRUGO);
MODULE_PARM_DESC(nss, "1 <= nss <= 2 : Supported number of Spatial Streams (Default: 2)");

module_param_named(amsdu_rx_max, ecrnx_mod_params.amsdu_rx_max, int, S_IRUGO);
MODULE_PARM_DESC(amsdu_rx_max, "0 <= amsdu_rx_max <= 2 : Maximum A-MSDU size supported in RX\n"
                 "        0: 3895 bytes\n"
                 "        1: 7991 bytes\n"
                 "        2: 11454 bytes\n"
                 "        This value might be reduced according to the FW capabilities.\n"
                 "        Default: 2");

module_param_named(bfmee, ecrnx_mod_params.bfmee, bool, S_IRUGO);
MODULE_PARM_DESC(bfmee, "Enable Beamformee Capability (Default: 1-Enabled)");

module_param_named(bfmer, ecrnx_mod_params.bfmer, bool, S_IRUGO);
MODULE_PARM_DESC(bfmer, "Enable Beamformer Capability (Default: 1-Enabled)");

module_param_named(mesh, ecrnx_mod_params.mesh, bool, S_IRUGO);
MODULE_PARM_DESC(mesh, "Enable Meshing Capability (Default: 1-Enabled)");

module_param_named(murx, ecrnx_mod_params.murx, bool, S_IRUGO);
MODULE_PARM_DESC(murx, "Enable MU-MIMO RX Capability (Default: 1-Enabled)");

module_param_named(mutx, ecrnx_mod_params.mutx, bool, S_IRUGO);
MODULE_PARM_DESC(mutx, "Enable MU-MIMO TX Capability (Default: 1-Enabled)");

module_param_named(mutx_on, ecrnx_mod_params.mutx_on, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(mutx_on, "Enable MU-MIMO transmissions (Default: 1-Enabled)");

module_param_named(roc_dur_max, ecrnx_mod_params.roc_dur_max, int, S_IRUGO);
MODULE_PARM_DESC(roc_dur_max, "Maximum Remain on Channel duration");

module_param_named(listen_itv, ecrnx_mod_params.listen_itv, int, S_IRUGO);
MODULE_PARM_DESC(listen_itv, "Maximum listen interval");

module_param_named(listen_bcmc, ecrnx_mod_params.listen_bcmc, bool, S_IRUGO);
MODULE_PARM_DESC(listen_bcmc, "Wait for BC/MC traffic following DTIM beacon");

module_param_named(lp_clk_ppm, ecrnx_mod_params.lp_clk_ppm, int, S_IRUGO);
MODULE_PARM_DESC(lp_clk_ppm, "Low Power Clock accuracy of the local device");

module_param_named(tdls, ecrnx_mod_params.tdls, bool, S_IRUGO);
MODULE_PARM_DESC(tdls, "Enable TDLS (Default: 1-Enabled)");

module_param_named(uf, ecrnx_mod_params.uf, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(uf, "Enable Unsupported HT Frame Logging (Default: 1-Enabled)");

module_param_named(ftl, ecrnx_mod_params.ftl, charp, S_IRUGO);
MODULE_PARM_DESC(ftl, "Firmware trace level  (Default: \"\")");

module_param_named(dpsm, ecrnx_mod_params.dpsm, bool, S_IRUGO);
MODULE_PARM_DESC(dpsm, "Enable Dynamic PowerSaving (Default: 1-Enabled)");
module_param_named(tx_to_bk, ecrnx_mod_params.tx_to_bk, int, S_IRUGO);
MODULE_PARM_DESC(tx_to_bk,
     "TX timeout for BK, in ms (Default: 0, Max: 65535). If 0, default value is applied");
module_param_named(tx_to_be, ecrnx_mod_params.tx_to_be, int, S_IRUGO);
MODULE_PARM_DESC(tx_to_be,
     "TX timeout for BE, in ms (Default: 0, Max: 65535). If 0, default value is applied");
module_param_named(tx_to_vi, ecrnx_mod_params.tx_to_vi, int, S_IRUGO);
MODULE_PARM_DESC(tx_to_vi,
     "TX timeout for VI, in ms (Default: 0, Max: 65535). If 0, default value is applied");
module_param_named(tx_to_vo, ecrnx_mod_params.tx_to_vo, int, S_IRUGO);
MODULE_PARM_DESC(tx_to_vo,
     "TX timeout for VO, in ms (Default: 0, Max: 65535). If 0, default value is applied");

/* Regulatory rules */
static struct ieee80211_regdomain ecrnx_regdom = {
    .n_reg_rules = 2,
    .alpha2 = "99",
    .reg_rules = {
        REG_RULE(2390 - 10, 2510 + 10, 40, 0, 1000, 0),
        REG_RULE(5150 - 10, 5970 + 10, 80, 0, 1000, 0),
    }
};

static const int mcs_map_to_rate[4][3] = {
    [PHY_CHNL_BW_20][IEEE80211_VHT_MCS_SUPPORT_0_7] = 65,
    [PHY_CHNL_BW_20][IEEE80211_VHT_MCS_SUPPORT_0_8] = 78,
    [PHY_CHNL_BW_20][IEEE80211_VHT_MCS_SUPPORT_0_9] = 78,
    [PHY_CHNL_BW_40][IEEE80211_VHT_MCS_SUPPORT_0_7] = 135,
    [PHY_CHNL_BW_40][IEEE80211_VHT_MCS_SUPPORT_0_8] = 162,
    [PHY_CHNL_BW_40][IEEE80211_VHT_MCS_SUPPORT_0_9] = 180,
    [PHY_CHNL_BW_80][IEEE80211_VHT_MCS_SUPPORT_0_7] = 292,
    [PHY_CHNL_BW_80][IEEE80211_VHT_MCS_SUPPORT_0_8] = 351,
    [PHY_CHNL_BW_80][IEEE80211_VHT_MCS_SUPPORT_0_9] = 390,
    [PHY_CHNL_BW_160][IEEE80211_VHT_MCS_SUPPORT_0_7] = 585,
    [PHY_CHNL_BW_160][IEEE80211_VHT_MCS_SUPPORT_0_8] = 702,
    [PHY_CHNL_BW_160][IEEE80211_VHT_MCS_SUPPORT_0_9] = 780,
};

#define MAX_VHT_RATE(map, nss, bw) (mcs_map_to_rate[bw][map] * (nss))

#if defined(CONFIG_ECRNX_HE)
struct ieee80211_sta_he_cap ecrnx_he_cap;
#endif

/**
 * Do some sanity check
 *
 */
static int ecrnx_check_fw_hw_feature(struct ecrnx_hw *ecrnx_hw,
                                    struct wiphy *wiphy)
{
    u32_l sys_feat = ecrnx_hw->version_cfm.features;
    u32_l mac_feat = ecrnx_hw->version_cfm.version_machw_1;
    u32_l phy_feat = ecrnx_hw->version_cfm.version_phy_1;
    u32_l phy_vers = ecrnx_hw->version_cfm.version_phy_2;
    u16_l max_sta_nb = ecrnx_hw->version_cfm.max_sta_nb;
    u8_l max_vif_nb = ecrnx_hw->version_cfm.max_vif_nb;
    int bw, res = 0;
    int amsdu_rx;

    if (!ecrnx_hw->mod_params->custregd)
        ecrnx_hw->mod_params->custchan = false;

    if (ecrnx_hw->mod_params->custchan) {
#ifdef CONFIG_ECRNX_SOFTMAC
        ecrnx_hw->mod_params->autobcn = false;
        ecrnx_hw->mod_params->hwscan = false;
        sys_feat &= ~BIT(MM_FEAT_CMON_BIT);
#endif
        ecrnx_hw->mod_params->mesh = false;
        ecrnx_hw->mod_params->tdls = false;
    }

#ifdef CONFIG_ECRNX_SOFTMAC
    if (sys_feat & BIT(MM_FEAT_UMAC_BIT)) {
        wiphy_err(wiphy, "Loading fullmac firmware with softmac driver\n");
        res = -1;
    }

    if (sys_feat & BIT(MM_FEAT_AUTOBCN_BIT) &&
        !ecrnx_hw->mod_params->autobcn) {
        wiphy_err(wiphy,
                  "Auto beacon enabled in firmware but disabled in driver\n");
        res = -1;
    }

    if (!!(sys_feat & BIT(MM_FEAT_HWSCAN_BIT)) !=
        !!ecrnx_hw->mod_params->hwscan) {
        wiphy_err(wiphy,
                  "hwscan %sabled in firmware but %sabled in driver\n",
                  (sys_feat & BIT(MM_FEAT_HWSCAN_BIT)) ? "en" : "dis",
                   ecrnx_hw->mod_params->hwscan ? "en" : "dis");
        res = -1;
    }

    if (sys_feat & BIT(MM_FEAT_CMON_BIT)) {
        ieee80211_hw_set(ecrnx_hw->hw, CONNECTION_MONITOR);
    }

    /* AMPDU (non)support implies different shared structure definition
       so insure that fw and drv have consistent compilation option */
    if (sys_feat & BIT(MM_FEAT_AMPDU_BIT)) {
#ifndef CONFIG_ECRNX_AGG_TX
        wiphy_err(wiphy,
                  "AMPDU enabled in firmware but support not compiled in driver\n");
        res = -1;
#endif /* CONFIG_ECRNX_AGG_TX */
    } else {
#ifdef CONFIG_ECRNX_AGG_TX
        wiphy_err(wiphy,
                  "AMPDU disabled in firmware but support compiled in driver\n");
        res = -1;
#else
        ecrnx_hw->mod_params->agg_tx = false;
#endif /* CONFIG_ECRNX_AGG_TX */
    }

    if (!(sys_feat & BIT(MM_FEAT_DPSM_BIT))) {
        ecrnx_hw->mod_params->dpsm = false;
    }

#else /* check for FULLMAC */

    if (!(sys_feat & BIT(MM_FEAT_UMAC_BIT))) {
        wiphy_err(wiphy,
                  "Loading softmac firmware with fullmac driver\n");
        res = -1;
    }

    if (!(sys_feat & BIT(MM_FEAT_ANT_DIV_BIT))) {
        ecrnx_hw->mod_params->ant_div = false;
    }

#endif /* CONFIG_ECRNX_SOFTMAC */

    if (!(sys_feat & BIT(MM_FEAT_VHT_BIT))) {
        ecrnx_hw->mod_params->vht_on = false;
    }

    // Check if HE is supported
    if (!(sys_feat & BIT(MM_FEAT_HE_BIT))) {
        ecrnx_hw->mod_params->he_on = false;
        ecrnx_hw->mod_params->he_ul_on = false;
    }

    if (!(sys_feat & BIT(MM_FEAT_PS_BIT))) {
        ecrnx_hw->mod_params->ps_on = false;
    }

    /* AMSDU (non)support implies different shared structure definition
       so insure that fw and drv have consistent compilation option */
    if (sys_feat & BIT(MM_FEAT_AMSDU_BIT)) {
#ifndef CONFIG_ECRNX_SPLIT_TX_BUF
        wiphy_err(wiphy,
                  "AMSDU enabled in firmware but support not compiled in driver\n");
        res = -1;
#else
        /* Adjust amsdu_maxnb so that it stays in allowed bounds */
        ecrnx_adjust_amsdu_maxnb(ecrnx_hw);
#endif /* CONFIG_ECRNX_SPLIT_TX_BUF */
    } else {
#ifdef CONFIG_ECRNX_SPLIT_TX_BUF
        wiphy_err(wiphy,
                  "AMSDU disabled in firmware but support compiled in driver\n");
        res = -1;
#endif /* CONFIG_ECRNX_SPLIT_TX_BUF */
    }

    if (!(sys_feat & BIT(MM_FEAT_UAPSD_BIT))) {
        ecrnx_hw->mod_params->uapsd_timeout = 0;
    }

    if (!(sys_feat & BIT(MM_FEAT_BFMEE_BIT))) {
        ecrnx_hw->mod_params->bfmee = false;
    }

    if ((sys_feat & BIT(MM_FEAT_BFMER_BIT))) {
#ifndef CONFIG_ECRNX_BFMER
        wiphy_err(wiphy,
                  "BFMER enabled in firmware but support not compiled in driver\n");
        res = -1;
#endif /* CONFIG_ECRNX_BFMER */
        // Check PHY and MAC HW BFMER support and update parameter accordingly
        if (!(phy_feat & MDM_BFMER_BIT) || !(mac_feat & NXMAC_BFMER_BIT)) {
            ecrnx_hw->mod_params->bfmer = false;
            // Disable the feature in the bitfield so that it won't be displayed
            sys_feat &= ~BIT(MM_FEAT_BFMER_BIT);
        }
    } else {
#ifdef CONFIG_ECRNX_BFMER
        wiphy_err(wiphy,
                  "BFMER disabled in firmware but support compiled in driver\n");
        res = -1;
#else
        ecrnx_hw->mod_params->bfmer = false;
#endif /* CONFIG_ECRNX_BFMER */
    }

    if (!(sys_feat & BIT(MM_FEAT_MESH_BIT))) {
        ecrnx_hw->mod_params->mesh = false;
    }

    if (!(sys_feat & BIT(MM_FEAT_TDLS_BIT))) {
        ecrnx_hw->mod_params->tdls = false;
    }

    if (!(sys_feat & BIT(MM_FEAT_UF_BIT))) {
        ecrnx_hw->mod_params->uf = false;
    }

#ifdef CONFIG_ECRNX_FULLMAC
    if ((sys_feat & BIT(MM_FEAT_MON_DATA_BIT))) {
#ifndef CONFIG_ECRNX_MON_DATA
        wiphy_err(wiphy,
                  "Monitor+Data interface support (MON_DATA) is enabled in firmware but support not compiled in driver\n");
        res = -1;
#endif /* CONFIG_ECRNX_MON_DATA */
    } else {
#ifdef CONFIG_ECRNX_MON_DATA
        wiphy_err(wiphy,
                  "Monitor+Data interface support (MON_DATA) disabled in firmware but support compiled in driver\n");
        res = -1;
#endif /* CONFIG_ECRNX_MON_DATA */
    }
#endif

    // Check supported AMSDU RX size
    amsdu_rx = (sys_feat >> MM_AMSDU_MAX_SIZE_BIT0) & 0x03;
    if (amsdu_rx < ecrnx_hw->mod_params->amsdu_rx_max) {
        ecrnx_hw->mod_params->amsdu_rx_max = amsdu_rx;
    }

    // Check supported BW
    bw = (phy_feat & MDM_CHBW_MASK) >> MDM_CHBW_LSB;
    // Check if 80MHz BW is supported
    if (bw < 2) {
        ecrnx_hw->mod_params->use_80 = false;
    }
    // Check if 40MHz BW is supported
    if (bw < 1)
        ecrnx_hw->mod_params->use_2040 = false;

    // 80MHz BW shall be disabled if 40MHz is not enabled
    if (!ecrnx_hw->mod_params->use_2040)
        ecrnx_hw->mod_params->use_80 = false;

    // Check if HT is supposed to be supported. If not, disable VHT/HE too
    if (!ecrnx_hw->mod_params->ht_on)
    {
        ecrnx_hw->mod_params->vht_on = false;
        ecrnx_hw->mod_params->he_on = false;
        ecrnx_hw->mod_params->he_ul_on = false;
        ecrnx_hw->mod_params->use_80 = false;
        ecrnx_hw->mod_params->use_2040 = false;
    }

    // LDPC is mandatory for HE40 and above, so if LDPC is not supported, then disable
    // HE to use HT/VHT only
    if (ecrnx_hw->mod_params->he_on && !ecrnx_hw->mod_params->ldpc_on)
    {
        ecrnx_hw->mod_params->use_80 = false;
        /* ESWIN turns off 40M automotically when it is HE mode  */
        //ecrnx_hw->mod_params->use_2040 = false;

    }

    // HT greenfield is not supported in modem >= 3.0
    if (__MDM_MAJOR_VERSION(phy_vers) > 0) {
#ifdef CONFIG_ECRNX_SOFTMAC
        ecrnx_hw->mod_params->gf_on = false;
#endif
        ecrnx_hw->mod_params->gf_rx_on = false;
    }

    if (!(sys_feat & BIT(MM_FEAT_MU_MIMO_RX_BIT)) ||
        !ecrnx_hw->mod_params->bfmee) {
        ecrnx_hw->mod_params->murx = false;
    }

    if ((sys_feat & BIT(MM_FEAT_MU_MIMO_TX_BIT))) {
#ifndef CONFIG_ECRNX_MUMIMO_TX
        wiphy_err(wiphy,
                  "MU-MIMO TX enabled in firmware but support not compiled in driver\n");
        res = -1;
#endif /* CONFIG_ECRNX_MUMIMO_TX */
        if (!ecrnx_hw->mod_params->bfmer)
            ecrnx_hw->mod_params->mutx = false;
        // Check PHY and MAC HW MU-MIMO TX support and update parameter accordingly
        else if (!(phy_feat & MDM_MUMIMOTX_BIT) || !(mac_feat & NXMAC_MU_MIMO_TX_BIT)) {
                ecrnx_hw->mod_params->mutx = false;
                // Disable the feature in the bitfield so that it won't be displayed
                sys_feat &= ~BIT(MM_FEAT_MU_MIMO_TX_BIT);
        }
    } else {
#ifdef CONFIG_ECRNX_MUMIMO_TX
        wiphy_err(wiphy,
                  "MU-MIMO TX disabled in firmware but support compiled in driver\n");
        res = -1;
#else
        ecrnx_hw->mod_params->mutx = false;
#endif /* CONFIG_ECRNX_MUMIMO_TX */
    }

    if (sys_feat & BIT(MM_FEAT_WAPI_BIT)) {
        ecrnx_enable_wapi(ecrnx_hw);
    }

#ifdef CONFIG_ECRNX_FULLMAC
    if (sys_feat & BIT(MM_FEAT_MFP_BIT)) {
        ecrnx_enable_mfp(ecrnx_hw);
    }
    if (mac_feat & NXMAC_GCMP_BIT) {
        ecrnx_enable_gcmp(ecrnx_hw);
    }
#endif

#ifdef CONFIG_ECRNX_SOFTMAC
#define QUEUE_NAME "BEACON queue "
#else
#define QUEUE_NAME "Broadcast/Multicast queue "
#endif /* CONFIG_ECRNX_SOFTMAC */

    if (sys_feat & BIT(MM_FEAT_BCN_BIT)) {
#if NX_TXQ_CNT == 4
        wiphy_err(wiphy, QUEUE_NAME
                  "enabled in firmware but support not compiled in driver\n");
        res = -1;
#endif /* NX_TXQ_CNT == 4 */
    } else {
#if NX_TXQ_CNT == 5
        wiphy_err(wiphy, QUEUE_NAME
                  "disabled in firmware but support compiled in driver\n");
        res = -1;
#endif /* NX_TXQ_CNT == 5 */
    }
#undef QUEUE_NAME

#ifdef CONFIG_ECRNX_RADAR
    if (sys_feat & BIT(MM_FEAT_RADAR_BIT)) {
        /* Enable combination with radar detection */
        wiphy->n_iface_combinations++;
    }
#endif /* CONFIG_ECRNX_RADAR */

#ifndef CONFIG_ECRNX_SDM
    switch (__MDM_PHYCFG_FROM_VERS(phy_feat)) {
        case MDM_PHY_CONFIG_TRIDENT:
            ecrnx_hw->mod_params->nss = 1;
            if ((ecrnx_hw->mod_params->phy_cfg < 0) || (ecrnx_hw->mod_params->phy_cfg > 2))
                ecrnx_hw->mod_params->phy_cfg = 2;
            break;
        case MDM_PHY_CONFIG_KARST:
        case MDM_PHY_CONFIG_CATAXIA:
            {
                int nss_supp = (phy_feat & MDM_NSS_MASK) >> MDM_NSS_LSB;
                if (ecrnx_hw->mod_params->nss > nss_supp)
                    ecrnx_hw->mod_params->nss = nss_supp;
                if ((ecrnx_hw->mod_params->phy_cfg < 0) || (ecrnx_hw->mod_params->phy_cfg > 1))
                    ecrnx_hw->mod_params->phy_cfg = 0;
            }
            break;
        default:
            WARN_ON(1);
            break;
    }
#endif /* CONFIG_ECRNX_SDM */

    if ((ecrnx_hw->mod_params->nss < 1) || (ecrnx_hw->mod_params->nss > 2))
        ecrnx_hw->mod_params->nss = 1;


    if ((ecrnx_hw->mod_params->mcs_map < 0) || (ecrnx_hw->mod_params->mcs_map > 2))
        ecrnx_hw->mod_params->mcs_map = 0;

#define PRINT_ECRNX_PHY_FEAT(feat)                                   \
    (phy_feat & MDM_##feat##_BIT ? "["#feat"]" : "")
    wiphy_info(wiphy, "PHY features: [NSS=%d][CHBW=%d]%s%s%s%s%s%s%s\n",
               (phy_feat & MDM_NSS_MASK) >> MDM_NSS_LSB,
               20 * (1 << ((phy_feat & MDM_CHBW_MASK) >> MDM_CHBW_LSB)),
               (phy_feat & (MDM_LDPCDEC_BIT | MDM_LDPCENC_BIT)) ==
                       (MDM_LDPCDEC_BIT | MDM_LDPCENC_BIT) ? "[LDPC]" : "",
               PRINT_ECRNX_PHY_FEAT(VHT),
               PRINT_ECRNX_PHY_FEAT(HE),
               PRINT_ECRNX_PHY_FEAT(BFMER),
               PRINT_ECRNX_PHY_FEAT(BFMEE),
               PRINT_ECRNX_PHY_FEAT(MUMIMOTX),
               PRINT_ECRNX_PHY_FEAT(MUMIMORX)
               );

#define PRINT_ECRNX_FEAT(feat)                                   \
    (sys_feat & BIT(MM_FEAT_##feat##_BIT) ? "["#feat"]" : "")

    wiphy_info(wiphy, "FW features: %s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s\n",
               PRINT_ECRNX_FEAT(BCN),
               PRINT_ECRNX_FEAT(AUTOBCN),
               PRINT_ECRNX_FEAT(HWSCAN),
               PRINT_ECRNX_FEAT(CMON),
               PRINT_ECRNX_FEAT(MROLE),
               PRINT_ECRNX_FEAT(RADAR),
               PRINT_ECRNX_FEAT(PS),
               PRINT_ECRNX_FEAT(UAPSD),
               PRINT_ECRNX_FEAT(DPSM),
               PRINT_ECRNX_FEAT(AMPDU),
               PRINT_ECRNX_FEAT(AMSDU),
               PRINT_ECRNX_FEAT(CHNL_CTXT),
               PRINT_ECRNX_FEAT(REORD),
               PRINT_ECRNX_FEAT(P2P),
               PRINT_ECRNX_FEAT(P2P_GO),
               PRINT_ECRNX_FEAT(UMAC),
               PRINT_ECRNX_FEAT(VHT),
               PRINT_ECRNX_FEAT(HE),
               PRINT_ECRNX_FEAT(BFMEE),
               PRINT_ECRNX_FEAT(BFMER),
               PRINT_ECRNX_FEAT(WAPI),
               PRINT_ECRNX_FEAT(MFP),
               PRINT_ECRNX_FEAT(MU_MIMO_RX),
               PRINT_ECRNX_FEAT(MU_MIMO_TX),
               PRINT_ECRNX_FEAT(MESH),
               PRINT_ECRNX_FEAT(TDLS),
               PRINT_ECRNX_FEAT(ANT_DIV),
               PRINT_ECRNX_FEAT(UF),
               PRINT_ECRNX_FEAT(TWT));
#undef PRINT_ECRNX_FEAT

    if(max_sta_nb != NX_REMOTE_STA_MAX)
    {
        wiphy_err(wiphy, "Different number of supported stations between driver and FW (%d != %d)\n",
                  NX_REMOTE_STA_MAX, max_sta_nb);
        res = -1;
    }

    if(max_vif_nb != NX_VIRT_DEV_MAX)
    {
        wiphy_err(wiphy, "Different number of supported virtual interfaces between driver and FW (%d != %d)\n",
                  NX_VIRT_DEV_MAX, max_vif_nb);
        res = -1;
    }

    return res;
}

static void ecrnx_set_ppe_threshold(struct ecrnx_hw *ecrnx_hw,
                                   struct ieee80211_sta_he_cap *he_cap)
{
    const u8_l PPE_THRES_INFO_OFT = 7;
    const u8_l PPE_THRES_INFO_BIT_LEN = 6;
    struct ppe_thres_info_tag
    {
        u8_l ppet16 : 3;
        u8_l ppet8 : 3;
    }__packed;
    struct ppe_thres_field_tag
    {
        u8_l nsts : 3;
        u8_l ru_idx_bmp : 4;
    };
    int nss = ecrnx_hw->mod_params->nss;
    struct ppe_thres_field_tag* ppe_thres_field = (struct ppe_thres_field_tag*) he_cap->ppe_thres;
    struct ppe_thres_info_tag ppe_thres_info = {.ppet16 = 0, //BSPK
                                                .ppet8 = 7 //None
                                               };
    u8_l* ppe_thres_info_ptr = (u8_l*) &ppe_thres_info;
    u16_l* ppe_thres_ptr = (u16_l*) he_cap->ppe_thres;
    u8_l i, j, cnt, offset;
    if (ecrnx_hw->mod_params->use_80)
    {
        ppe_thres_field->ru_idx_bmp = 7;
        cnt = 3;
    }
    else
    {
        ppe_thres_field->ru_idx_bmp = 1;
        cnt = 1;
    }
    ppe_thres_field->nsts = nss - 1;
    for (i = 0; i < nss ; i++)
    {
        for (j = 0; j < cnt; j++){
            offset = (i * cnt + j) * PPE_THRES_INFO_BIT_LEN + PPE_THRES_INFO_OFT;
            ppe_thres_ptr = (u16_l*)&he_cap->ppe_thres[offset / 8];
            *ppe_thres_ptr |= *ppe_thres_info_ptr << (offset % 8);
        }
    }
}


#ifdef CONFIG_ECRNX_SOFTMAC
static void ecrnx_set_softmac_flags(struct ecrnx_hw *ecrnx_hw)
{
    struct ieee80211_hw *hw = ecrnx_hw->hw;
    int nss;
#ifdef CONFIG_MAC80211_AMSDUS_TX
    ieee80211_hw_set(hw, TX_AMSDU);
    ieee80211_hw_set(hw, TX_FRAG_LIST);
    hw->max_tx_fragments = ecrnx_hw->mod_params->amsdu_maxnb;
#endif

    if (!ecrnx_hw->mod_params->autobcn)
        ieee80211_hw_set(hw, HOST_BROADCAST_PS_BUFFERING);

    if (ecrnx_hw->mod_params->agg_tx)
        ieee80211_hw_set(hw, AMPDU_AGGREGATION);

    if (ecrnx_hw->mod_params->cmon)
        ieee80211_hw_set(hw, CONNECTION_MONITOR);

    if (ecrnx_hw->mod_params->hwscan)
        ieee80211_hw_set(hw, CHANCTX_STA_CSA);

    if (ecrnx_hw->mod_params->ps_on) {
        ieee80211_hw_set(hw, SUPPORTS_PS);
    }
    /* To disable the dynamic PS we say to the stack that we support it in
     * HW. This will force mac80211 rely on us to handle this. */
    ieee80211_hw_set(hw, SUPPORTS_DYNAMIC_PS);

    if (ecrnx_hw->mod_params->mfp_on)
        ieee80211_hw_set(hw, MFP_CAPABLE);

    nss = ecrnx_hw->mod_params->nss;
    ecrnx_hw->phy.ctrlinfo_1.value = 0;
    ecrnx_hw->phy.ctrlinfo_2.value = 0;
    if (nss == 1) {
        ecrnx_hw->phy.ctrlinfo_2.antennaSet = 1;
    } else {
        ecrnx_hw->phy.ctrlinfo_1.fecCoding = 0;
        ecrnx_hw->phy.ctrlinfo_1.nTx = 1;
        ecrnx_hw->phy.ctrlinfo_2.antennaSet = 3;
        ecrnx_hw->phy.ctrlinfo_2.smmIndex = 1;
    }
    ecrnx_hw->phy.stbc_nss = nss >> 1;
}
#endif

#ifdef CONFIG_ECRNX_5G
static void ecrnx_set_vht_capa(struct ecrnx_hw *ecrnx_hw, struct wiphy *wiphy)
{
    struct ieee80211_supported_band *band_5GHz = wiphy->bands[NL80211_BAND_5GHZ];
    int i;
    int nss = ecrnx_hw->mod_params->nss;
    int mcs_map;
    int mcs_map_max;
    int mcs_map_max_2ss_rx = IEEE80211_VHT_MCS_SUPPORT_0_9;
    int mcs_map_max_2ss_tx = IEEE80211_VHT_MCS_SUPPORT_0_9;
    int bw_max;

    if (!ecrnx_hw->mod_params->vht_on)
        return;

    band_5GHz->vht_cap.vht_supported = true;
    if (ecrnx_hw->mod_params->sgi80)
        band_5GHz->vht_cap.cap |= IEEE80211_VHT_CAP_SHORT_GI_80;
    if (ecrnx_hw->mod_params->stbc_on)
        band_5GHz->vht_cap.cap |= IEEE80211_VHT_CAP_RXSTBC_1;
    if (ecrnx_hw->mod_params->ldpc_on)
        band_5GHz->vht_cap.cap |= IEEE80211_VHT_CAP_RXLDPC;
    if (ecrnx_hw->mod_params->bfmee) {
        band_5GHz->vht_cap.cap |= IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE;
        band_5GHz->vht_cap.cap |= 3 << IEEE80211_VHT_CAP_BEAMFORMEE_STS_SHIFT;
    }
    if (nss > 1)
        band_5GHz->vht_cap.cap |= IEEE80211_VHT_CAP_TXSTBC;

    // Update the AMSDU max RX size (not shifted as located at offset 0 of the VHT cap)
    band_5GHz->vht_cap.cap |= ecrnx_hw->mod_params->amsdu_rx_max;

    if (ecrnx_hw->mod_params->bfmer) {
        band_5GHz->vht_cap.cap |= IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE;
        /* Set number of sounding dimensions */
        band_5GHz->vht_cap.cap |= (nss - 1) << IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_SHIFT;
    }
    if (ecrnx_hw->mod_params->murx)
        band_5GHz->vht_cap.cap |= IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE;
    if (ecrnx_hw->mod_params->mutx)
        band_5GHz->vht_cap.cap |= IEEE80211_VHT_CAP_MU_BEAMFORMER_CAPABLE;

    /*
     * MCS map:
     * This capabilities are filled according to the mcs_map module parameter.
     * However currently we have some limitations due to FPGA clock constraints
     * that prevent always using the range of MCS that is defined by the
     * parameter:
     *   - in RX, 2SS, we support up to MCS7
     *   - in TX, 2SS, we support up to MCS8
     */
    // Get max supported BW
    if (ecrnx_hw->mod_params->use_80) {
        bw_max = PHY_CHNL_BW_80;
        mcs_map_max_2ss_rx = IEEE80211_VHT_MCS_SUPPORT_0_7;
        mcs_map_max_2ss_tx = IEEE80211_VHT_MCS_SUPPORT_0_8;
    } else if (ecrnx_hw->mod_params->use_2040)
        bw_max = PHY_CHNL_BW_40;
    else
        bw_max = PHY_CHNL_BW_20;

    // Check if MCS map should be limited to MCS0_8 due to the standard. Indeed in BW20,
    // MCS9 is not supported in 1 and 2 SS
    if (ecrnx_hw->mod_params->use_2040)
        mcs_map_max = IEEE80211_VHT_MCS_SUPPORT_0_9;
    else
        mcs_map_max = IEEE80211_VHT_MCS_SUPPORT_0_8;

    mcs_map = min_t(int, ecrnx_hw->mod_params->mcs_map, mcs_map_max);
    band_5GHz->vht_cap.vht_mcs.rx_mcs_map = cpu_to_le16(0);
    for (i = 0; i < nss; i++) {
        band_5GHz->vht_cap.vht_mcs.rx_mcs_map |= cpu_to_le16(mcs_map << (i*2));
        band_5GHz->vht_cap.vht_mcs.rx_highest = MAX_VHT_RATE(mcs_map, nss, bw_max);
        mcs_map = min_t(int, mcs_map, mcs_map_max_2ss_rx);
    }
    for (; i < 8; i++) {
        band_5GHz->vht_cap.vht_mcs.rx_mcs_map |= cpu_to_le16(
            IEEE80211_VHT_MCS_NOT_SUPPORTED << (i*2));
    }

    mcs_map = min_t(int, ecrnx_hw->mod_params->mcs_map, mcs_map_max);
    band_5GHz->vht_cap.vht_mcs.tx_mcs_map = cpu_to_le16(0);
    for (i = 0; i < nss; i++) {
        band_5GHz->vht_cap.vht_mcs.tx_mcs_map |= cpu_to_le16(mcs_map << (i*2));
        band_5GHz->vht_cap.vht_mcs.tx_highest = MAX_VHT_RATE(mcs_map, nss, bw_max);
        mcs_map = min_t(int, mcs_map, mcs_map_max_2ss_tx);
    }
    for (; i < 8; i++) {
        band_5GHz->vht_cap.vht_mcs.tx_mcs_map |= cpu_to_le16(
            IEEE80211_VHT_MCS_NOT_SUPPORTED << (i*2));
    }

    if (!ecrnx_hw->mod_params->use_80) {
#ifdef CONFIG_VENDOR_ECRNX_VHT_NO80
        band_5GHz->vht_cap.cap |= IEEE80211_VHT_CAP_NOT_SUP_WIDTH_80;
#endif
        band_5GHz->vht_cap.cap &= ~IEEE80211_VHT_CAP_SHORT_GI_80;
    }
}
#else
static void ecrnx_set_vht_capa(struct ecrnx_hw *ecrnx_hw, struct wiphy *wiphy)
{
    struct ieee80211_supported_band *band_2GHz = wiphy->bands[NL80211_BAND_2GHZ];
    int i;
    int nss = ecrnx_hw->mod_params->nss;
    int mcs_map;
    int mcs_map_max;
    int mcs_map_max_2ss_rx = IEEE80211_VHT_MCS_SUPPORT_0_9;
    int mcs_map_max_2ss_tx = IEEE80211_VHT_MCS_SUPPORT_0_9;
    int bw_max;

    if (!ecrnx_hw->mod_params->vht_on)
        return;

    band_2GHz->vht_cap.vht_supported = true;
    if (ecrnx_hw->mod_params->sgi80)
        band_2GHz->vht_cap.cap |= IEEE80211_VHT_CAP_SHORT_GI_80;
    if (ecrnx_hw->mod_params->stbc_on)
        band_2GHz->vht_cap.cap |= IEEE80211_VHT_CAP_RXSTBC_1;
    if (ecrnx_hw->mod_params->ldpc_on)
        band_2GHz->vht_cap.cap |= IEEE80211_VHT_CAP_RXLDPC;
    if (ecrnx_hw->mod_params->bfmee) {
        band_2GHz->vht_cap.cap |= IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
        band_2GHz->vht_cap.cap |= 3 << IEEE80211_VHT_CAP_BEAMFORMEE_STS_SHIFT;
#endif
    }
    if (nss > 1)
        band_2GHz->vht_cap.cap |= IEEE80211_VHT_CAP_TXSTBC;

    // Update the AMSDU max RX size (not shifted as located at offset 0 of the VHT cap)
    band_2GHz->vht_cap.cap |= ecrnx_hw->mod_params->amsdu_rx_max;

    if (ecrnx_hw->mod_params->bfmer) {
        band_2GHz->vht_cap.cap |= IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE;
        /* Set number of sounding dimensions */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
        band_2GHz->vht_cap.cap |= (nss - 1) << IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_SHIFT;
#endif
    }
    if (ecrnx_hw->mod_params->murx)
        band_2GHz->vht_cap.cap |= IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE;
    if (ecrnx_hw->mod_params->mutx)
        band_2GHz->vht_cap.cap |= IEEE80211_VHT_CAP_MU_BEAMFORMER_CAPABLE;

    /*
     * MCS map:
     * This capabilities are filled according to the mcs_map module parameter.
     * However currently we have some limitations due to FPGA clock constraints
     * that prevent always using the range of MCS that is defined by the
     * parameter:
     *   - in RX, 2SS, we support up to MCS7
     *   - in TX, 2SS, we support up to MCS8
     */
    // Get max supported BW
    if (ecrnx_hw->mod_params->use_80) {
        bw_max = PHY_CHNL_BW_80;
        mcs_map_max_2ss_rx = IEEE80211_VHT_MCS_SUPPORT_0_7;
        mcs_map_max_2ss_tx = IEEE80211_VHT_MCS_SUPPORT_0_8;
    } else if (ecrnx_hw->mod_params->use_2040)
        bw_max = PHY_CHNL_BW_40;
    else
        bw_max = PHY_CHNL_BW_20;

    // Check if MCS map should be limited to MCS0_8 due to the standard. Indeed in BW20,
    // MCS9 is not supported in 1 and 2 SS
    if (ecrnx_hw->mod_params->use_2040)
        mcs_map_max = IEEE80211_VHT_MCS_SUPPORT_0_9;
    else
        mcs_map_max = IEEE80211_VHT_MCS_SUPPORT_0_8;

    mcs_map = min_t(int, ecrnx_hw->mod_params->mcs_map, mcs_map_max);
    band_2GHz->vht_cap.vht_mcs.rx_mcs_map = cpu_to_le16(0);
    for (i = 0; i < nss; i++) {
        band_2GHz->vht_cap.vht_mcs.rx_mcs_map |= cpu_to_le16(mcs_map << (i*2));
        band_2GHz->vht_cap.vht_mcs.rx_highest = MAX_VHT_RATE(mcs_map, nss, bw_max);
        mcs_map = min_t(int, mcs_map, mcs_map_max_2ss_rx);
    }
    for (; i < 8; i++) {
        band_2GHz->vht_cap.vht_mcs.rx_mcs_map |= cpu_to_le16(
            IEEE80211_VHT_MCS_NOT_SUPPORTED << (i*2));
    }

    mcs_map = min_t(int, ecrnx_hw->mod_params->mcs_map, mcs_map_max);
    band_2GHz->vht_cap.vht_mcs.tx_mcs_map = cpu_to_le16(0);
    for (i = 0; i < nss; i++) {
        band_2GHz->vht_cap.vht_mcs.tx_mcs_map |= cpu_to_le16(mcs_map << (i*2));
        band_2GHz->vht_cap.vht_mcs.tx_highest = MAX_VHT_RATE(mcs_map, nss, bw_max);
        mcs_map = min_t(int, mcs_map, mcs_map_max_2ss_tx);
    }
    for (; i < 8; i++) {
        band_2GHz->vht_cap.vht_mcs.tx_mcs_map |= cpu_to_le16(
            IEEE80211_VHT_MCS_NOT_SUPPORTED << (i*2));
    }

    if (!ecrnx_hw->mod_params->use_80) {
#ifdef CONFIG_VENDOR_ECRNX_VHT_NO80
        band_2GHz->vht_cap.cap |= IEEE80211_VHT_CAP_NOT_SUP_WIDTH_80;
#endif
        band_2GHz->vht_cap.cap &= ~IEEE80211_VHT_CAP_SHORT_GI_80;
    }
}
#endif


static void ecrnx_set_ht_capa(struct ecrnx_hw *ecrnx_hw, struct wiphy *wiphy)
{
#ifdef CONFIG_ECRNX_5G
    struct ieee80211_supported_band *band_5GHz = wiphy->bands[NL80211_BAND_5GHZ];
#endif
    struct ieee80211_supported_band *band_2GHz = wiphy->bands[NL80211_BAND_2GHZ];
    int i;
    int nss = ecrnx_hw->mod_params->nss;

    if (!ecrnx_hw->mod_params->ht_on) {
        band_2GHz->ht_cap.ht_supported = false;
#ifdef CONFIG_ECRNX_5G
        band_5GHz->ht_cap.ht_supported = false;
#endif
        return;
    }
	//JIRA438 begin by E0000550
    //if (ecrnx_hw->mod_params->stbc_on)
    //JIRA438 end
        band_2GHz->ht_cap.cap |= 1 << IEEE80211_HT_CAP_RX_STBC_SHIFT;
    if (ecrnx_hw->mod_params->ldpc_on)
        band_2GHz->ht_cap.cap |= IEEE80211_HT_CAP_LDPC_CODING;
    if (ecrnx_hw->mod_params->use_2040) {
        band_2GHz->ht_cap.mcs.rx_mask[4] = 0x1; /* MCS32 */
        band_2GHz->ht_cap.cap |= IEEE80211_HT_CAP_SUP_WIDTH_20_40;
        band_2GHz->ht_cap.mcs.rx_highest = cpu_to_le16(135 * nss);
    } else {
        band_2GHz->ht_cap.mcs.rx_highest = cpu_to_le16(65 * nss);
    }
    if (nss > 1)
        band_2GHz->ht_cap.cap |= IEEE80211_HT_CAP_TX_STBC;

    // Update the AMSDU max RX size
    if (ecrnx_hw->mod_params->amsdu_rx_max)
        band_2GHz->ht_cap.cap |= IEEE80211_HT_CAP_MAX_AMSDU;

    if (ecrnx_hw->mod_params->sgi) {
        band_2GHz->ht_cap.cap |= IEEE80211_HT_CAP_SGI_20;
        if (ecrnx_hw->mod_params->use_2040) {
            band_2GHz->ht_cap.cap |= IEEE80211_HT_CAP_SGI_40;
            band_2GHz->ht_cap.mcs.rx_highest = cpu_to_le16(150 * nss);
        } else
            band_2GHz->ht_cap.mcs.rx_highest = cpu_to_le16(72 * nss);
    }
    if (ecrnx_hw->mod_params->gf_rx_on)
        band_2GHz->ht_cap.cap |= IEEE80211_HT_CAP_GRN_FLD;

    for (i = 0; i < nss; i++) {
        band_2GHz->ht_cap.mcs.rx_mask[i] = 0xFF;
    }
	
#ifdef CONFIG_ECRNX_5G
    band_5GHz->ht_cap = band_2GHz->ht_cap;
#endif
}

static void ecrnx_set_he_capa(struct ecrnx_hw *ecrnx_hw, struct wiphy *wiphy)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 20, 0)
#ifdef CONFIG_ECRNX_5G
    struct ieee80211_supported_band *band_5GHz = wiphy->bands[NL80211_BAND_5GHZ];
#endif
    struct ieee80211_supported_band *band_2GHz = wiphy->bands[NL80211_BAND_2GHZ];
#endif

    int i;
    int nss = ecrnx_hw->mod_params->nss;
    struct ieee80211_sta_he_cap *he_cap;
    int mcs_map, mcs_map_max_2ss = IEEE80211_HE_MCS_SUPPORT_0_11;
    u8 dcm_max_ru = IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_242;
    u32_l phy_vers = ecrnx_hw->version_cfm.version_phy_2;

    if (!ecrnx_hw->mod_params->he_on) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 20, 0)
        band_2GHz->iftype_data = NULL;
        band_2GHz->n_iftype_data = 0;
	#ifdef CONFIG_ECRNX_5G
        band_5GHz->iftype_data = NULL;
        band_5GHz->n_iftype_data = 0;
	#endif
#endif
        return;
    }
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 20, 0)
    he_cap = (struct ieee80211_sta_he_cap *) &band_2GHz->iftype_data->he_cap;
#else
    he_cap = &ecrnx_he_cap;
#endif
    he_cap->has_he = true;
    #ifdef CONFIG_ECRNX_FULLMAC
    if (ecrnx_hw->version_cfm.features & BIT(MM_FEAT_TWT_BIT))
    {
        ecrnx_hw->ext_capa[9] = WLAN_EXT_CAPA10_TWT_REQUESTER_SUPPORT;
        he_cap->he_cap_elem.mac_cap_info[0] |= IEEE80211_HE_MAC_CAP0_TWT_REQ;
    }
    #endif
    he_cap->he_cap_elem.mac_cap_info[2] |= IEEE80211_HE_MAC_CAP2_ALL_ACK;
    ecrnx_set_ppe_threshold(ecrnx_hw, he_cap);
#if 0
    /* ESWIN turns off 40M automotically when it is HE mode  */
    if (ecrnx_hw->mod_params->use_2040) {
        he_cap->he_cap_elem.phy_cap_info[0] |=
                        IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G;
        dcm_max_ru = IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_484;
    }
#endif
    if (ecrnx_hw->mod_params->use_80) {
        he_cap->he_cap_elem.phy_cap_info[0] |=
                        IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G;
        mcs_map_max_2ss = IEEE80211_HE_MCS_SUPPORT_0_7;
        dcm_max_ru = IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_996;
    }
    if (ecrnx_hw->mod_params->ldpc_on) {
        he_cap->he_cap_elem.phy_cap_info[1] |= IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD;
    } else {
        // If no LDPC is supported, we have to limit to MCS0_9, as LDPC is mandatory
        // for MCS 10 and 11
        ecrnx_hw->mod_params->he_mcs_map = min_t(int, ecrnx_hw->mod_params->he_mcs_map,
                                                IEEE80211_HE_MCS_SUPPORT_0_9);
    }
#if 0
    /* ESWIN: sync the he capa with 6600 standalone */
    he_cap->he_cap_elem.phy_cap_info[1] |= IEEE80211_HE_PHY_CAP1_HE_LTF_AND_GI_FOR_HE_PPDUS_0_8US |
                                           IEEE80211_HE_PHY_CAP1_MIDAMBLE_RX_TX_MAX_NSTS;
    he_cap->he_cap_elem.phy_cap_info[2] |= IEEE80211_HE_PHY_CAP2_MIDAMBLE_RX_TX_MAX_NSTS |
                                           IEEE80211_HE_PHY_CAP2_NDP_4x_LTF_AND_3_2US |
                                           IEEE80211_HE_PHY_CAP2_DOPPLER_RX;
#else
    he_cap->he_cap_elem.phy_cap_info[1] |= IEEE80211_HE_PHY_CAP1_HE_LTF_AND_GI_FOR_HE_PPDUS_0_8US;
    he_cap->he_cap_elem.phy_cap_info[2] |= IEEE80211_HE_PHY_CAP2_NDP_4x_LTF_AND_3_2US |
                                           IEEE80211_HE_PHY_CAP2_DOPPLER_RX;
#endif
    if (ecrnx_hw->mod_params->stbc_on)
        he_cap->he_cap_elem.phy_cap_info[2] |= IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ;
    he_cap->he_cap_elem.phy_cap_info[3] |= IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_16_QAM |
                                           IEEE80211_HE_PHY_CAP3_RX_HE_MU_PPDU_FROM_NON_AP_STA;

    /* ESWIN: sync the he capa with 6600 standalone */
    if (nss > 1) {
        he_cap->he_cap_elem.phy_cap_info[3] |= IEEE80211_HE_PHY_CAP3_DCM_MAX_RX_NSS_2;
    } else {
        he_cap->he_cap_elem.phy_cap_info[3] |= IEEE80211_HE_PHY_CAP3_DCM_MAX_RX_NSS_1;
    }
    if (ecrnx_hw->mod_params->bfmee) {
        he_cap->he_cap_elem.phy_cap_info[4] |= IEEE80211_HE_PHY_CAP4_SU_BEAMFORMEE;
        he_cap->he_cap_elem.phy_cap_info[4] |=
                     IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_4;
    }
    he_cap->he_cap_elem.phy_cap_info[5] |= IEEE80211_HE_PHY_CAP5_NG16_SU_FEEDBACK |
                                           IEEE80211_HE_PHY_CAP5_NG16_MU_FEEDBACK;
    he_cap->he_cap_elem.phy_cap_info[6] |= IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_42_SU |
                                           IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_75_MU |
#if 0
    /* ESWIN: sync the he capa with 6600 standalone */

                                           IEEE80211_HE_PHY_CAP6_TRIG_SU_BEAMFORMER_FB |
                                           IEEE80211_HE_PHY_CAP6_TRIG_MU_BEAMFORMER_FB |
#endif
                                           IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT |
                                           IEEE80211_HE_PHY_CAP6_PARTIAL_BANDWIDTH_DL_MUMIMO;
    he_cap->he_cap_elem.phy_cap_info[7] |= IEEE80211_HE_PHY_CAP7_HE_SU_MU_PPDU_4XLTF_AND_08_US_GI;
    he_cap->he_cap_elem.phy_cap_info[8] |= IEEE80211_HE_PHY_CAP8_20MHZ_IN_40MHZ_HE_PPDU_IN_2G |
                                           dcm_max_ru;
    he_cap->he_cap_elem.phy_cap_info[9] |= IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_COMP_SIGB |
                                           IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_NON_COMP_SIGB |
                                           IEEE80211_HE_PHY_CAP9_NOMIMAL_PKT_PADDING_16US;
#if 0
    if (__MDM_VERSION(phy_vers) > 30) {
        he_cap->he_cap_elem.phy_cap_info[6] |= IEEE80211_HE_PHY_CAP6_PARTIAL_BW_EXT_RANGE;
        he_cap->he_cap_elem.phy_cap_info[8] |= IEEE80211_HE_PHY_CAP8_HE_ER_SU_1XLTF_AND_08_US_GI |
                                               IEEE80211_HE_PHY_CAP8_HE_ER_SU_PPDU_4XLTF_AND_08_US_GI;
    }
#endif
    mcs_map = ecrnx_hw->mod_params->he_mcs_map;
    memset(&he_cap->he_mcs_nss_supp, 0, sizeof(he_cap->he_mcs_nss_supp));
    for (i = 0; i < nss; i++) {
        __le16 unsup_for_ss = cpu_to_le16(IEEE80211_HE_MCS_NOT_SUPPORTED << (i*2));
        he_cap->he_mcs_nss_supp.rx_mcs_80 |= cpu_to_le16(mcs_map << (i*2));
        he_cap->he_mcs_nss_supp.rx_mcs_160 |= unsup_for_ss;
        he_cap->he_mcs_nss_supp.rx_mcs_80p80 |= unsup_for_ss;
        mcs_map = min_t(int, ecrnx_hw->mod_params->he_mcs_map,
                        mcs_map_max_2ss);
    }
    for (; i < 8; i++) {
        __le16 unsup_for_ss = cpu_to_le16(IEEE80211_HE_MCS_NOT_SUPPORTED << (i*2));
        he_cap->he_mcs_nss_supp.rx_mcs_80 |= unsup_for_ss;
        he_cap->he_mcs_nss_supp.rx_mcs_160 |= unsup_for_ss;
        he_cap->he_mcs_nss_supp.rx_mcs_80p80 |= unsup_for_ss;
    }
    mcs_map = ecrnx_hw->mod_params->he_mcs_map;
    for (i = 0; i < nss; i++) {
        __le16 unsup_for_ss = cpu_to_le16(IEEE80211_HE_MCS_NOT_SUPPORTED << (i*2));
        he_cap->he_mcs_nss_supp.tx_mcs_80 |= cpu_to_le16(mcs_map << (i*2));
        he_cap->he_mcs_nss_supp.tx_mcs_160 |= unsup_for_ss;
        he_cap->he_mcs_nss_supp.tx_mcs_80p80 |= unsup_for_ss;
        mcs_map = min_t(int, ecrnx_hw->mod_params->he_mcs_map,
                        mcs_map_max_2ss);
    }
    for (; i < 8; i++) {
        __le16 unsup_for_ss = cpu_to_le16(IEEE80211_HE_MCS_NOT_SUPPORTED << (i*2));
        he_cap->he_mcs_nss_supp.tx_mcs_80 |= unsup_for_ss;
        he_cap->he_mcs_nss_supp.tx_mcs_160 |= unsup_for_ss;
        he_cap->he_mcs_nss_supp.tx_mcs_80p80 |= unsup_for_ss;
    }
}

static void ecrnx_set_wiphy_params(struct ecrnx_hw *ecrnx_hw, struct wiphy *wiphy)
{
#ifdef CONFIG_ECRNX_SOFTMAC
    struct ieee80211_hw *hw = ecrnx_hw->hw;

    /* SOFTMAC specific parameters */
    if (ecrnx_hw->mod_params->hwscan) {
        hw->wiphy->max_scan_ssids = SCAN_SSID_MAX;
        hw->wiphy->max_scan_ie_len = SCAN_MAX_IE_LEN;
    }
#else
    /* FULLMAC specific parameters */
    wiphy->flags |= WIPHY_FLAG_REPORTS_OBSS;
    wiphy->max_scan_ssids = SCAN_SSID_MAX;
    wiphy->max_scan_ie_len = SCANU_MAX_IE_LEN;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0)
    wiphy->support_mbssid = 1;
#endif
#endif /* CONFIG_ECRNX_FULLMAC */

    if (ecrnx_hw->mod_params->tdls) {
        /* TDLS support */
        wiphy->flags |= WIPHY_FLAG_SUPPORTS_TDLS;
#ifdef CONFIG_ECRNX_FULLMAC
        /* TDLS external setup support */
        wiphy->flags |= WIPHY_FLAG_TDLS_EXTERNAL_SETUP;
#endif
    }

    if (ecrnx_hw->mod_params->ap_uapsd_on)
        wiphy->flags |= WIPHY_FLAG_AP_UAPSD;

#ifdef CONFIG_ECRNX_FULLMAC
    if (ecrnx_hw->mod_params->ps_on)
        wiphy->flags |= WIPHY_FLAG_PS_ON_BY_DEFAULT;
    else
        wiphy->flags &= ~WIPHY_FLAG_PS_ON_BY_DEFAULT;
#endif

    if (ecrnx_hw->mod_params->custregd) {
        // Apply custom regulatory. Note that for recent kernel versions we use instead the
        // REGULATORY_WIPHY_SELF_MANAGED flag, along with the regulatory_set_wiphy_regd()
        // function, that needs to be called after wiphy registration
        // Check if custom channel set shall be enabled. In such case only monitor mode is
        // supported
        if (ecrnx_hw->mod_params->custchan) {
            wiphy->interface_modes = BIT(NL80211_IFTYPE_MONITOR);

            // Enable "extra" channels
            wiphy->bands[NL80211_BAND_2GHZ]->n_channels += 13;
		#ifdef CONFIG_ECRNX_5G
            wiphy->bands[NL80211_BAND_5GHZ]->n_channels += 59;
		#endif
        }
    }
}

static void ecrnx_set_rf_params(struct ecrnx_hw *ecrnx_hw, struct wiphy *wiphy)
{
#ifndef CONFIG_ECRNX_SDM
#ifdef CONFIG_ECRNX_5G
    struct ieee80211_supported_band *band_5GHz = wiphy->bands[NL80211_BAND_5GHZ];
#else
#endif
    u32 mdm_phy_cfg = __MDM_PHYCFG_FROM_VERS(ecrnx_hw->version_cfm.version_phy_1);

    /*
     * Get configuration file depending on the RF
     */
#if 0 /* baoyong: we use the custom rf, do not need this */
    struct ecrnx_phy_conf_file phy_conf;
    if (mdm_phy_cfg == MDM_PHY_CONFIG_TRIDENT) {
        // Retrieve the Trident configuration
        ecrnx_parse_phy_configfile(ecrnx_hw, ECRNX_PHY_CONFIG_TRD_NAME,
                                  &phy_conf, ecrnx_hw->mod_params->phy_cfg);
        memcpy(&ecrnx_hw->phy.cfg, &phy_conf.trd, sizeof(phy_conf.trd));
    } else if (mdm_phy_cfg == MDM_PHY_CONFIG_CATAXIA) {
        memset(&phy_conf.cataxia, 0, sizeof(phy_conf.cataxia));
        phy_conf.cataxia.path_used = ecrnx_hw->mod_params->phy_cfg;
        memcpy(&ecrnx_hw->phy.cfg, &phy_conf.cataxia, sizeof(phy_conf.cataxia));
    } else if (mdm_phy_cfg == MDM_PHY_CONFIG_KARST) {
        // We use the NSS parameter as is
        // Retrieve the Karst configuration
        ecrnx_parse_phy_configfile(ecrnx_hw, ECRNX_PHY_CONFIG_KARST_NAME,
                                  &phy_conf, ecrnx_hw->mod_params->phy_cfg);

        memcpy(&ecrnx_hw->phy.cfg, &phy_conf.karst, sizeof(phy_conf.karst));
    } else {
        WARN_ON(1);
    }
#endif

    /*
     * adjust caps depending on the RF
     */
    switch (mdm_phy_cfg) {
        case MDM_PHY_CONFIG_TRIDENT:
        {
            wiphy_dbg(wiphy, "found Trident PHY .. limit BW to 40MHz\n");
            ecrnx_hw->phy.limit_bw = true;
#ifdef CONFIG_ECRNX_5G
#ifdef CONFIG_VENDOR_ECRNX_VHT_NO80
            band_5GHz->vht_cap.cap |= IEEE80211_VHT_CAP_NOT_SUP_WIDTH_80;
#endif
            band_5GHz->vht_cap.cap &= ~(IEEE80211_VHT_CAP_SHORT_GI_80 |
                                        IEEE80211_VHT_CAP_RXSTBC_MASK);
#endif
            break;
        }
        case MDM_PHY_CONFIG_CATAXIA:
        {
            wiphy_dbg(wiphy, "found CATAXIA PHY\n");
            break;
        }
        case MDM_PHY_CONFIG_KARST:
        {
            wiphy_dbg(wiphy, "found KARST PHY\n");
            break;
        }
        default:
            WARN_ON(1);
            break;
    }
#endif /* CONFIG_ECRNX_SDM */
}

int ecrnx_handle_dynparams(struct ecrnx_hw *ecrnx_hw, struct wiphy *wiphy)
{
    int ret;

    /* Check compatibility between requested parameters and HW/SW features */
    ret = ecrnx_check_fw_hw_feature(ecrnx_hw, wiphy);
    if (ret)
        return ret;
#ifndef CONFIG_ECRNX_ESWIN

    /* Allocate the RX buffers according to the maximum AMSDU RX size */
    ret = ecrnx_ipc_rxbuf_init(ecrnx_hw,
                              (4 * (ecrnx_hw->mod_params->amsdu_rx_max + 1) + 1) * 1024);
    if (ret) {
        wiphy_err(wiphy, "Cannot allocate the RX buffers\n");
        return ret;
    }
#endif
#ifdef CONFIG_ECRNX_SOFTMAC
    /* SOFTMAC specific parameters*/
    ecrnx_set_softmac_flags(ecrnx_hw);
#endif /* CONFIG_ECRNX_SOFTMAC */

    /* Set wiphy parameters */
    ecrnx_set_wiphy_params(ecrnx_hw, wiphy);

    /* Set VHT capabilities */
    ecrnx_set_vht_capa(ecrnx_hw, wiphy);

    /* Set HE capabilities */
    ecrnx_set_he_capa(ecrnx_hw, wiphy);

    /* Set HT capabilities */
    ecrnx_set_ht_capa(ecrnx_hw, wiphy);

    /* Set RF specific parameters (shall be done last as it might change some
       capabilities previously set) */
    ecrnx_set_rf_params(ecrnx_hw, wiphy);

    return 0;
}

void ecrnx_custregd(struct ecrnx_hw *ecrnx_hw, struct wiphy *wiphy)
{
// For older kernel version, the custom regulatory is applied before the wiphy
// registration (in ecrnx_set_wiphy_params()), so nothing has to be done here
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
    if (!ecrnx_hw->mod_params->custregd)
        return;

    wiphy->regulatory_flags |= REGULATORY_IGNORE_STALE_KICKOFF;
    wiphy->regulatory_flags |= REGULATORY_WIPHY_SELF_MANAGED;

    rtnl_lock();
    if (regulatory_set_wiphy_regd_sync_rtnl(wiphy, &ecrnx_regdom))
        wiphy_err(wiphy, "Failed to set custom regdomain\n");
    else
        wiphy_err(wiphy,"\n"
                  "*******************************************************\n"
                  "** CAUTION: USING PERMISSIVE CUSTOM REGULATORY RULES **\n"
                  "*******************************************************\n");
     rtnl_unlock();
#endif
}

void ecrnx_adjust_amsdu_maxnb(struct ecrnx_hw *ecrnx_hw)
{
    if (ecrnx_hw->mod_params->amsdu_maxnb > NX_TX_PAYLOAD_MAX)
        ecrnx_hw->mod_params->amsdu_maxnb = NX_TX_PAYLOAD_MAX;
    else if (ecrnx_hw->mod_params->amsdu_maxnb == 0)
        ecrnx_hw->mod_params->amsdu_maxnb = 1;
}
