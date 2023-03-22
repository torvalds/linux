/**
 ******************************************************************************
 *
 * @file rwnx_mod_params.h
 *
 * @brief Declaration of module parameters
 *
 * Copyright (C) RivieraWaves 2012-2019
 *
 ******************************************************************************
 */

#ifndef _RWNX_MOD_PARAM_H_
#define _RWNX_MOD_PARAM_H_

struct rwnx_mod_params {
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
    bool auto_reply;
    char *ftl;
    bool dpsm;
#ifdef CONFIG_RWNX_FULLMAC
    bool ant_div;
#endif /* CONFIG_RWNX_FULLMAC */
};

extern struct rwnx_mod_params rwnx_mod_params;

struct rwnx_hw;
struct wiphy;
int rwnx_handle_dynparams(struct rwnx_hw *rwnx_hw, struct wiphy *wiphy);
void rwnx_custregd(struct rwnx_hw *rwnx_hw, struct wiphy *wiphy);
void rwnx_enable_wapi(struct rwnx_hw *rwnx_hw);
void rwnx_enable_mfp(struct rwnx_hw *rwnx_hw);

#endif /* _RWNX_MOD_PARAM_H_ */
