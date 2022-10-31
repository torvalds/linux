/**
 ******************************************************************************
 *
 * @file ecrnx_mod_params.h
 *
 * @brief Declaration of module parameters
 *
 * Copyright (C) ESWIN 2015-2020
 *
 ******************************************************************************
 */

#ifndef _ECRNX_MOD_PARAM_H_
#define _ECRNX_MOD_PARAM_H_

struct ecrnx_mod_params {
    bool ht_on;
    bool vht_on;
    bool he_on;
    int mcs_map;
    int he_mcs_map;
    bool he_ul_on;
    bool ldpc_on;
    bool stbc_on;
    bool gf_rx_on;
    int phy_cfg;
    int uapsd_timeout;
    bool ap_uapsd_on;
    bool sgi;
    bool sgi80;
    bool use_2040;
    bool use_80;
    bool custregd;
    bool custchan;
    int nss;
    int amsdu_rx_max;
    bool bfmee;
    bool bfmer;
    bool mesh;
    bool murx;
    bool mutx;
    bool mutx_on;
    unsigned int roc_dur_max;
    int listen_itv;
    bool listen_bcmc;
    int lp_clk_ppm;
    bool ps_on;
    int tx_lft;
    int amsdu_maxnb;
    int uapsd_queues;
    bool tdls;
    bool uf;
    char *ftl;
    bool dpsm;
    int tx_to_bk;
    int tx_to_be;
    int tx_to_vi;
    int tx_to_vo;
#ifdef CONFIG_ECRNX_SOFTMAC
    bool mfp_on;
    bool gf_on;
    bool bwsig_on;
    bool dynbw_on;
    bool agg_tx;
    int  amsdu_force;
    bool rc_probes_on;
    bool cmon;
    bool hwscan;
    bool autobcn;
#else
    bool ant_div;
#endif /* CONFIG_ECRNX_SOFTMAC */
};

extern struct ecrnx_mod_params ecrnx_mod_params;

struct ecrnx_hw;
struct wiphy;
int ecrnx_handle_dynparams(struct ecrnx_hw *ecrnx_hw, struct wiphy *wiphy);
void ecrnx_custregd(struct ecrnx_hw *ecrnx_hw, struct wiphy *wiphy);
void ecrnx_enable_wapi(struct ecrnx_hw *ecrnx_hw);
void ecrnx_enable_mfp(struct ecrnx_hw *ecrnx_hw);
void ecrnx_enable_gcmp(struct ecrnx_hw *ecrnx_hw);
void ecrnx_adjust_amsdu_maxnb(struct ecrnx_hw *ecrnx_hw);

#endif /* _ECRNX_MOD_PARAM_H_ */
