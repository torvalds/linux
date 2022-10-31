/**
 ****************************************************************************************
 *
 * @file ecrnx_configparse.c
 *
 * Copyright (C) ESWIN 2015-2020
 *
 ****************************************************************************************
 */
#include <linux/firmware.h>
#include <linux/if_ether.h>

#include "ecrnx_defs.h"
#include "ecrnx_cfgfile.h"
#include "ecrnx_debug.h"
#include "ecrnx_debugfs_func.h"

/**
 *
 */
static const char *ecrnx_find_tag(const u8 *file_data, unsigned int file_size,
                                 const char *tag_name, unsigned int tag_len)
{
    unsigned int curr, line_start = 0, line_size;

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    /* Walk through all the lines of the configuration file */
    while (line_start < file_size) {
        /* Search the end of the current line (or the end of the file) */
        for (curr = line_start; curr < file_size; curr++)
            if (file_data[curr] == '\n')
                break;

        /* Compute the line size */
        line_size = curr - line_start;

        /* Check if this line contains the expected tag */
        if ((line_size == (strlen(tag_name) + tag_len)) &&
            (!strncmp(&file_data[line_start], tag_name, strlen(tag_name))))
            return (&file_data[line_start + strlen(tag_name)]);

        /* Move to next line */
        line_start = curr + 1;
    }

    /* Tag not found */
    return NULL;
}

/**
 * Parse the Config file used at init time
 */
int ecrnx_parse_configfile(struct ecrnx_hw *ecrnx_hw, const char *filename)
{
    const struct firmware *config_fw;
    u8 dflt_mac[ETH_ALEN] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x5f };
    int ret;
    const u8 *tag_ptr;
    bool mac_flag = false, dbg_level_flag = false, fw_log_lv_flag = false, fw_log_type_flag = false;

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 18, 0)
    ret = firmware_request_nowarn(&config_fw, filename, ecrnx_hw->dev); //avoid the files not exit error
#else
    ret = request_firmware(&config_fw, filename, ecrnx_hw->dev);
#endif

    if (ret == 0) {
        /* Get MAC Address */
        tag_ptr = ecrnx_find_tag(config_fw->data, config_fw->size, "MAC_ADDR=", strlen("00:00:00:00:00:00"));
        if (tag_ptr != NULL) {
            u8 *addr = ecrnx_hw->conf_param.mac_addr;
            if (sscanf(tag_ptr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                        addr + 0, addr + 1, addr + 2,
                        addr + 3, addr + 4, addr + 5) == ETH_ALEN){
                mac_flag = true;
            }
        }

        tag_ptr = ecrnx_find_tag(config_fw->data, config_fw->size, "DRIVER_LOG_LEVEL=", strlen("0"));
        if (tag_ptr != NULL){
            if(sscanf(tag_ptr, "%hhx", &ecrnx_hw->conf_param.host_driver_log_level) == 1){
                ecrnx_dbg_level = ecrnx_hw->conf_param.host_driver_log_level;
                dbg_level_flag = true;
            }
        }

        tag_ptr = ecrnx_find_tag(config_fw->data, config_fw->size, "FW_LOG_LEVEL=", strlen("0"));
        if (tag_ptr != NULL){
            if(sscanf(tag_ptr, "%hhx", &ecrnx_hw->conf_param.fw_log_level) == 1){
                fw_log_lv_flag = true;
            }
        }

        tag_ptr = ecrnx_find_tag(config_fw->data, config_fw->size, "FW_LOG_TYPE=", strlen("0"));
        if (tag_ptr != NULL){
            if(sscanf(tag_ptr, "%hhx", &ecrnx_hw->conf_param.fw_log_type) == 1){
                fw_log_type_flag = true;
            }
        }

        /* Release the configuration file */
        release_firmware(config_fw);
    }

    if(!mac_flag){
        memcpy(ecrnx_hw->conf_param.mac_addr, dflt_mac, ETH_ALEN);
    }

    if(!dbg_level_flag){
        ecrnx_hw->conf_param.host_driver_log_level = ecrnx_dbg_level;
    }

    if(!fw_log_lv_flag){
        ecrnx_hw->conf_param.fw_log_level = log_ctl.level;
    }

    if(!fw_log_type_flag){
        ecrnx_hw->conf_param.fw_log_type = log_ctl.dir;
    }

    ECRNX_PRINT("MAC Address is:%pM\n", ecrnx_hw->conf_param.mac_addr);
    ECRNX_PRINT("host driver log level is:%d \n", ecrnx_hw->conf_param.host_driver_log_level);
    ECRNX_PRINT("firmware log level is:%d \n", ecrnx_hw->conf_param.fw_log_level);

    if(0 == ecrnx_hw->conf_param.fw_log_type){
        ECRNX_PRINT("firmware log level type:%d (print to chip's uart) \n", ecrnx_hw->conf_param.fw_log_type);
    }else if(1 == ecrnx_hw->conf_param.fw_log_type){
        ECRNX_PRINT("firmware log level type:%d (print to host debugfs) \n", ecrnx_hw->conf_param.fw_log_type);
    }else if(2 == ecrnx_hw->conf_param.fw_log_type){
        ECRNX_PRINT("firmware log level type:%d (print to host kernel) \n", ecrnx_hw->conf_param.fw_log_type);
    }else{
        ECRNX_ERR("firmware log level type error;\n");
    }
    return 0;
}

/**
 * Parse the Config file used at init time
 */
int ecrnx_parse_phy_configfile(struct ecrnx_hw *ecrnx_hw, const char *filename,
                              struct ecrnx_phy_conf_file *config, int path)
{
    const struct firmware *config_fw;
    int ret;
    const u8 *tag_ptr;

    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

    if ((ret = request_firmware(&config_fw, filename, ecrnx_hw->dev))) {
        ECRNX_ERR(KERN_CRIT "%s: Failed to get %s (%d)\n", __func__, filename, ret);
        return ret;
    }

    /* Get Trident path mapping */
    tag_ptr = ecrnx_find_tag(config_fw->data, config_fw->size,
                            "TRD_PATH_MAPPING=", strlen("00"));
    if (tag_ptr != NULL) {
        u8 val;
        if (sscanf(tag_ptr, "%hhx", &val) == 1)
            config->trd.path_mapping = val;
        else
            config->trd.path_mapping = path;
    } else
        config->trd.path_mapping = path;

    ECRNX_DBG("Trident path mapping is: %d\n", config->trd.path_mapping);

    /* Get DC offset compensation */
    tag_ptr = ecrnx_find_tag(config_fw->data, config_fw->size,
                            "TX_DC_OFF_COMP=", strlen("00000000"));
    if (tag_ptr != NULL) {
        if (sscanf(tag_ptr, "%08x", &config->trd.tx_dc_off_comp) != 1)
            config->trd.tx_dc_off_comp = 0;
    } else
        config->trd.tx_dc_off_comp = 0;

    ECRNX_DBG("TX DC offset compensation is: %08X\n", config->trd.tx_dc_off_comp);

    /* Get Karst TX IQ compensation value for path0 on 2.4GHz */
    tag_ptr = ecrnx_find_tag(config_fw->data, config_fw->size,
                            "KARST_TX_IQ_COMP_2_4G_PATH_0=", strlen("00000000"));
    if (tag_ptr != NULL) {
        if (sscanf(tag_ptr, "%08x", &config->karst.tx_iq_comp_2_4G[0]) != 1)
            config->karst.tx_iq_comp_2_4G[0] = 0x01000000;
    } else
        config->karst.tx_iq_comp_2_4G[0] = 0x01000000;

    ECRNX_DBG("Karst TX IQ compensation for path 0 on 2.4GHz is: %08X\n", config->karst.tx_iq_comp_2_4G[0]);

    /* Get Karst TX IQ compensation value for path1 on 2.4GHz */
    tag_ptr = ecrnx_find_tag(config_fw->data, config_fw->size,
                            "KARST_TX_IQ_COMP_2_4G_PATH_1=", strlen("00000000"));
    if (tag_ptr != NULL) {
        if (sscanf(tag_ptr, "%08x", &config->karst.tx_iq_comp_2_4G[1]) != 1)
            config->karst.tx_iq_comp_2_4G[1] = 0x01000000;
    } else
        config->karst.tx_iq_comp_2_4G[1] = 0x01000000;

    ECRNX_DBG("Karst TX IQ compensation for path 1 on 2.4GHz is: %08X\n", config->karst.tx_iq_comp_2_4G[1]);

    /* Get Karst RX IQ compensation value for path0 on 2.4GHz */
    tag_ptr = ecrnx_find_tag(config_fw->data, config_fw->size,
                            "KARST_RX_IQ_COMP_2_4G_PATH_0=", strlen("00000000"));
    if (tag_ptr != NULL) {
        if (sscanf(tag_ptr, "%08x", &config->karst.rx_iq_comp_2_4G[0]) != 1)
            config->karst.rx_iq_comp_2_4G[0] = 0x01000000;
    } else
        config->karst.rx_iq_comp_2_4G[0] = 0x01000000;

    ECRNX_DBG("Karst RX IQ compensation for path 0 on 2.4GHz is: %08X\n", config->karst.rx_iq_comp_2_4G[0]);

    /* Get Karst RX IQ compensation value for path1 on 2.4GHz */
    tag_ptr = ecrnx_find_tag(config_fw->data, config_fw->size,
                            "KARST_RX_IQ_COMP_2_4G_PATH_1=", strlen("00000000"));
    if (tag_ptr != NULL) {
        if (sscanf(tag_ptr, "%08x", &config->karst.rx_iq_comp_2_4G[1]) != 1)
            config->karst.rx_iq_comp_2_4G[1] = 0x01000000;
    } else
        config->karst.rx_iq_comp_2_4G[1] = 0x01000000;

    ECRNX_DBG("Karst RX IQ compensation for path 1 on 2.4GHz is: %08X\n", config->karst.rx_iq_comp_2_4G[1]);

    /* Get Karst TX IQ compensation value for path0 on 5GHz */
    tag_ptr = ecrnx_find_tag(config_fw->data, config_fw->size,
                            "KARST_TX_IQ_COMP_5G_PATH_0=", strlen("00000000"));
    if (tag_ptr != NULL) {
        if (sscanf(tag_ptr, "%08x", &config->karst.tx_iq_comp_5G[0]) != 1)
            config->karst.tx_iq_comp_5G[0] = 0x01000000;
    } else
        config->karst.tx_iq_comp_5G[0] = 0x01000000;

    ECRNX_DBG("Karst TX IQ compensation for path 0 on 5GHz is: %08X\n", config->karst.tx_iq_comp_5G[0]);

    /* Get Karst TX IQ compensation value for path1 on 5GHz */
    tag_ptr = ecrnx_find_tag(config_fw->data, config_fw->size,
                            "KARST_TX_IQ_COMP_5G_PATH_1=", strlen("00000000"));
    if (tag_ptr != NULL) {
        if (sscanf(tag_ptr, "%08x", &config->karst.tx_iq_comp_5G[1]) != 1)
            config->karst.tx_iq_comp_5G[1] = 0x01000000;
    } else
        config->karst.tx_iq_comp_5G[1] = 0x01000000;

    ECRNX_DBG("Karst TX IQ compensation for path 1 on 5GHz is: %08X\n", config->karst.tx_iq_comp_5G[1]);

    /* Get Karst RX IQ compensation value for path0 on 5GHz */
    tag_ptr = ecrnx_find_tag(config_fw->data, config_fw->size,
                            "KARST_RX_IQ_COMP_5G_PATH_0=", strlen("00000000"));
    if (tag_ptr != NULL) {
        if (sscanf(tag_ptr, "%08x", &config->karst.rx_iq_comp_5G[0]) != 1)
            config->karst.rx_iq_comp_5G[0] = 0x01000000;
    } else
        config->karst.rx_iq_comp_5G[0] = 0x01000000;

    ECRNX_DBG("Karst RX IQ compensation for path 0 on 5GHz is: %08X\n", config->karst.rx_iq_comp_5G[0]);

    /* Get Karst RX IQ compensation value for path1 on 5GHz */
    tag_ptr = ecrnx_find_tag(config_fw->data, config_fw->size,
                            "KARST_RX_IQ_COMP_5G_PATH_1=", strlen("00000000"));
    if (tag_ptr != NULL) {
        if (sscanf(tag_ptr, "%08x", &config->karst.rx_iq_comp_5G[1]) != 1)
            config->karst.rx_iq_comp_5G[1] = 0x01000000;
    } else
        config->karst.rx_iq_comp_5G[1] = 0x01000000;

    ECRNX_DBG("Karst RX IQ compensation for path 1 on 5GHz is: %08X\n", config->karst.rx_iq_comp_5G[1]);

    /* Get Karst default path */
    tag_ptr = ecrnx_find_tag(config_fw->data, config_fw->size,
                            "KARST_DEFAULT_PATH=", strlen("00"));
    if (tag_ptr != NULL) {
        u8 val;
        if (sscanf(tag_ptr, "%hhx", &val) == 1)
            config->karst.path_used = val;
        else
            config->karst.path_used = path;
    } else
        config->karst.path_used = path;

    ECRNX_DBG("Karst default path is: %d\n", config->karst.path_used);

    /* Release the configuration file */
    release_firmware(config_fw);

    return 0;
}

