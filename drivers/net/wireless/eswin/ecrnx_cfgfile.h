/**
 ****************************************************************************************
 *
 * @file ecrnx_cfgfile.h
 *
 * Copyright (C) ESWIN 2015-2020
 *
 ****************************************************************************************
 */

#ifndef _ECRNX_CFGFILE_H_
#define _ECRNX_CFGFILE_H_

/*
 * Structure used to retrieve information from the Config file used at Initialization time
 */
struct ecrnx_conf_file {
    u8 mac_addr[ETH_ALEN];
    u8 host_driver_log_level;
    u8 fw_log_level;
    u8 fw_log_type;
};

/*
 * Structure used to retrieve information from the PHY Config file used at Initialization time
 */
struct ecrnx_phy_conf_file {
    struct phy_trd_cfg_tag trd;
    struct phy_karst_cfg_tag karst;
    struct phy_cataxia_cfg_tag cataxia;
};

int ecrnx_parse_configfile(struct ecrnx_hw *ecrnx_hw, const char *filename);
int ecrnx_parse_phy_configfile(struct ecrnx_hw *ecrnx_hw, const char *filename,
                              struct ecrnx_phy_conf_file *config, int path);

#endif /* _ECRNX_CFGFILE_H_ */
