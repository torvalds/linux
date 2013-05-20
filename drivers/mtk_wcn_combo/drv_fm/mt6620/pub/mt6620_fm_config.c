/* fm_config.c
 *
 * (C) Copyright 2011
 * MediaTek <www.MediaTek.com>
 * hongcheng <hongcheng.xia@MediaTek.com>
 *
 * FM Radio Driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/string.h>
#include <linux/slab.h>

#include "fm_typedef.h"
#include "fm_rds.h"
#include "fm_dbg.h"
#include "fm_err.h"
#include "fm_stdlib.h"
#include "fm_patch.h"
#include "fm_config.h"
//#include "fm_cust_cfg.h"
#include "mt6620_fm_cust_cfg.h"

fm_cust_cfg mt6620_fm_config;
//static fm_s32 fm_index = 0;

static fm_s32 MT6620fm_cust_config_print(fm_cust_cfg *cfg)
{
 //   fm_s32 i;

    WCN_DBG(FM_NTC | MAIN, "MT6620 rssi_l:\t%d\n", cfg->rx_cfg.long_ana_rssi_th);
    WCN_DBG(FM_NTC | MAIN, "MT6620 rssi_s:\t%d\n", cfg->rx_cfg.short_ana_rssi_th);
    WCN_DBG(FM_NTC | MAIN, "MT6620 pamd_th:\t%d\n", cfg->rx_cfg.pamd_th);
    WCN_DBG(FM_NTC | MAIN, "MT6620 mr_th:\t%d\n", cfg->rx_cfg.mr_th);
    WCN_DBG(FM_NTC | MAIN, "MT6620 atdc_th:\t%d\n", cfg->rx_cfg.atdc_th);
    WCN_DBG(FM_NTC | MAIN, "MT6620 prx_th:\t%d\n", cfg->rx_cfg.prx_th);
    WCN_DBG(FM_NTC | MAIN, "MT6620 atdev_th:\t%d\n", cfg->rx_cfg.atdev_th);
    WCN_DBG(FM_NTC | MAIN, "MT6620 smg_th:\t%d\n", cfg->rx_cfg.smg_th);
    WCN_DBG(FM_NTC | MAIN, "MT6620 de_emphasis:\t%d\n", cfg->rx_cfg.deemphasis);
    WCN_DBG(FM_NTC | MAIN, "MT6620 osc_freq:\t%d\n", cfg->rx_cfg.osc_freq);
    
    WCN_DBG(FM_NTC | MAIN, "MT6620 scan_hole_low:\t%d\n", cfg->tx_cfg.scan_hole_low);
    WCN_DBG(FM_NTC | MAIN, "MT6620 scan_hole_high:\t%d\n", cfg->tx_cfg.scan_hole_high);
    WCN_DBG(FM_NTC | MAIN, "MT6620 power_level:\t%d\n", cfg->tx_cfg.power_level);
#if 0
    WCN_DBG(FM_NTC | MAIN, "MT6620 rssi_l:\t0x%04x\n", cfg->rx_cfg.long_ana_rssi_th);
    WCN_DBG(FM_NTC | MAIN, "MT6620 rssi_s:\t0x%04x\n", cfg->rx_cfg.short_ana_rssi_th);
    WCN_DBG(FM_NTC | MAIN, "MT6620 mr_th:\t0x%04x\n", cfg->rx_cfg.mr_th);
    WCN_DBG(FM_NTC | MAIN, "MT6620 cqi_th:\t0x%04x\n", cfg->rx_cfg.cqi_th);
    WCN_DBG(FM_NTC | MAIN, "MT6620 smg_th:\t0x%04x\n", cfg->rx_cfg.smg_th);
    WCN_DBG(FM_NTC | MAIN, "MT6620 scan_ch_size:\t%d\n", cfg->rx_cfg.scan_ch_size);
    WCN_DBG(FM_NTC | MAIN, "MT6620 seek_space:\t%d\n", cfg->rx_cfg.seek_space);
    WCN_DBG(FM_NTC | MAIN, "MT6620 band:\t%d\n", cfg->rx_cfg.band);
    WCN_DBG(FM_NTC | MAIN, "MT6620 band_freq_l:\t%d\n", cfg->rx_cfg.band_freq_l);
    WCN_DBG(FM_NTC | MAIN, "MT6620 band_freq_h:\t%d\n", cfg->rx_cfg.band_freq_h);
    WCN_DBG(FM_NTC | MAIN, "MT6620 scan_sort:\t%d\n", cfg->rx_cfg.scan_sort);
    WCN_DBG(FM_NTC | MAIN, "MT6620 fake_ch_num:\t%d\n", cfg->rx_cfg.fake_ch_num);
    WCN_DBG(FM_NTC | MAIN, "MT6620 fake_ch_rssi_th:\t%d\n", cfg->rx_cfg.fake_ch_rssi_th);

    for (i = 0; i < cfg->rx_cfg.fake_ch_num; i++) {
        WCN_DBG(FM_NTC | MAIN, "fake_ch:\t%d\n", cfg->rx_cfg.fake_ch[i]);
    }

    WCN_DBG(FM_NTC | MAIN, "de_emphasis:\t%d\n", cfg->rx_cfg.deemphasis);
    WCN_DBG(FM_NTC | MAIN, "osc_freq:\t%d\n", cfg->rx_cfg.osc_freq);
    WCN_DBG(FM_NTC | MAIN, "scan_hole_low:\t%d\n", cfg->tx_cfg.scan_hole_low);
    WCN_DBG(FM_NTC | MAIN, "scan_hole_high:\t%d\n", cfg->tx_cfg.scan_hole_high);
    WCN_DBG(FM_NTC | MAIN, "power_level:\t%d\n", cfg->tx_cfg.power_level);
#endif
    return 0;
}

static fm_s32 MT6620cfg_item_handler(fm_s8 *grp, fm_s8 *key, fm_s8 *val, fm_cust_cfg *cfg)
{
    fm_s32 ret = 0;
    struct fm_rx_cust_cfg *rx_cfg = &cfg->rx_cfg;
    struct fm_tx_cust_cfg *tx_cfg = &cfg->tx_cfg;

    if (0 <= (ret = cfg_item_match(key, val, "FM_RX_RSSI_TH_LONG_MT6620", &rx_cfg->long_ana_rssi_th))) 
    {//FMR_RSSI_TH_L = 0x0301
        return ret;
    } 
    else if (0 <= (ret = cfg_item_match(key, val, "FM_RX_RSSI_TH_SHORT_MT6620", &rx_cfg->short_ana_rssi_th))) 
    {
        return ret;
    } 
    else if (0 <= (ret = cfg_item_match(key, val, "FM_RX_DESENSE_RSSI_MT6620", &rx_cfg->desene_rssi_th))) 
    {
        return ret;
    } 
    else if (0 <= (ret = cfg_item_match(key, val, "FM_RX_PAMD_TH_MT6620", &rx_cfg->pamd_th))) 
    {
        return ret;
    } 
    else if (0 <= (ret = cfg_item_match(key, val, "FM_RX_MR_TH_MT6620", &rx_cfg->mr_th))) 
    {
        return ret;
    } 
    else if (0 <= (ret = cfg_item_match(key, val, "FM_RX_ATDC_TH_MT6620", &rx_cfg->atdc_th))) 
    {
        return ret;
    } 
    else if (0 <= (ret = cfg_item_match(key, val, "FM_RX_PRX_TH_MT6620", &rx_cfg->prx_th))) 
    {
        return ret;
    } 
    else if (0 <= (ret = cfg_item_match(key, val, "FM_RX_SMG_TH_MT6620", &rx_cfg->smg_th))) 
    {
        return ret;
    } 
    else if (0 <= (ret = cfg_item_match(key, val, "FM_RX_DEEMPHASIS_MT6620", &rx_cfg->deemphasis))) 
    {
        return ret;
    } 
    else if (0 <= (ret = cfg_item_match(key, val, "FM_RX_OSC_FREQ_MT6620", &rx_cfg->osc_freq))) 
    {
        return ret;
    } 
    else if (0 <= (ret = cfg_item_match(key, val, "FMT_SCAN_HOLE_L_MT6620", &tx_cfg->scan_hole_low))) 
    {
        return ret;
    } 
    else if (0 <= (ret = cfg_item_match(key, val, "FMT_SCAN_HOLE_H_MT6620", &tx_cfg->scan_hole_high))) 
    {
        return ret;
    } 
    else if (0 <= (ret = cfg_item_match(key, val, "FMT_PWR_LVL_MAX_MT6620", &tx_cfg->power_level))) 
    {
        return ret;
    } 
    else 
    {
        WCN_DBG(FM_WAR | MAIN, "invalid key\n");
        return -1;
    }
/*    if (0 <= (ret = cfg_item_match(key, val, "FMR_RSSI_TH_L_MT6620", &rx_cfg->long_ana_rssi_th))) {//FMR_RSSI_TH_L = 0x0301
        return ret;
    } else if (0 <= (ret = cfg_item_match(key, val, "FMR_RSSI_TH_S_MT6620", &rx_cfg->short_ana_rssi_th))) {
        return ret;
    } else if (0 <= (ret = cfg_item_match(key, val, "FMR_CQI_TH_MT6620", &rx_cfg->cqi_th))) {
        return ret;
    } else if (0 <= (ret = cfg_item_match(key, val, "FMR_MR_TH_MT6620", &rx_cfg->mr_th))) {
        return ret;
    } else if (0 <= (ret = cfg_item_match(key, val, "FMR_SMG_TH_MT6620", &rx_cfg->smg_th))) {
        return ret;
    } else if (0 <= (ret = cfg_item_match(key, val, "FMR_SCAN_CH_SIZE_MT6620", &rx_cfg->scan_ch_size))) {
        return ret;
    } else if (0 <= (ret = cfg_item_match(key, val, "FMR_SCAN_SORT_MT6620", &rx_cfg->scan_sort))) {
        return ret;
    } else if (0 <= (ret = cfg_item_match(key, val, "FMR_SEEK_SPACE_MT6620", &rx_cfg->seek_space))) {
        return ret;
    } else if (0 <= (ret = cfg_item_match(key, val, "FMR_BAND_MT6620", &rx_cfg->band))) {
        return ret;
    } else if (0 <= (ret = cfg_item_match(key, val, "FMR_BAND_FREQ_L_MT6620", &rx_cfg->band_freq_l))) {
        return ret;
    } else if (0 <= (ret = cfg_item_match(key, val, "FMR_BAND_FREQ_H_MT6620", &rx_cfg->band_freq_h))) {
        return ret;
    } else if (0 <= (ret = cfg_item_match(key, val, "FMR_FAKE_CH_MT6620", &rx_cfg->fake_ch[fm_index]))) {
        fm_index += 1;
        rx_cfg->fake_ch_num = (rx_cfg->fake_ch_num < fm_index) ? fm_index : rx_cfg->fake_ch_num;
        return ret;
    } else if (0 <= (ret = cfg_item_match(key, val, "FMR_FAKE_CH_RSSI_MT6620", &rx_cfg->fake_ch_rssi_th))) {
        return ret;
    } else if (0 <= (ret = cfg_item_match(key, val, "FMR_DEEMPHASIS_MT6620", &rx_cfg->deemphasis))) {
        return ret;
    } else if (0 <= (ret = cfg_item_match(key, val, "FMR_OSC_FREQ_MT6620", &rx_cfg->osc_freq))) {
        return ret;
    } */
}
static fm_s32 MT6620fm_cust_config_default(fm_cust_cfg *cfg)
{
    FMR_ASSERT(cfg);

    cfg->rx_cfg.long_ana_rssi_th = FM_RX_RSSI_TH_LONG_MT6620;
    cfg->rx_cfg.short_ana_rssi_th = FM_RX_RSSI_TH_SHORT_MT6620;
    cfg->rx_cfg.desene_rssi_th = FM_RX_DESENSE_RSSI_MT6620;
    cfg->rx_cfg.pamd_th = FM_RX_PAMD_TH_MT6620;
    cfg->rx_cfg.mr_th = FM_RX_MR_TH_MT6620;
    cfg->rx_cfg.atdc_th = FM_RX_ATDC_TH_MT6620;
    cfg->rx_cfg.prx_th = FM_RX_PRX_TH_MT6620;
    cfg->rx_cfg.smg_th = FM_RX_SMG_TH_MT6620;
    cfg->rx_cfg.deemphasis = FM_RX_DEEMPHASIS_MT6620;
	cfg->rx_cfg.osc_freq = FM_RX_OSC_FREQ_MT6620;

    cfg->tx_cfg.scan_hole_low = FM_TX_SCAN_HOLE_LOW_MT6620;
    cfg->tx_cfg.scan_hole_high = FM_TX_SCAN_HOLE_HIGH_MT6620;
    cfg->tx_cfg.power_level = FM_TX_PWR_LEVEL_MAX_MT6620;

    return 0;
}

static fm_s32 MT6620fm_cust_config_file(const fm_s8 *filename, fm_cust_cfg *cfg)
{
    fm_s32 ret = 0;
    fm_s8 *buf = NULL;
    fm_s32 file_len = 0;

    if (!(buf = fm_zalloc(4096))) {
        WCN_DBG(FM_ALT | MAIN, "-ENOMEM\n");
        return -ENOMEM;
    }

//    fm_index = 0;

    file_len = fm_file_read(filename, buf, 4096, 0);

    if (file_len <= 0) {
        ret = -1;
        goto out;
    }

    ret = cfg_parser(buf, MT6620cfg_item_handler, cfg);

out:

    if (buf) {
        fm_free(buf);
    }

    return ret;
}
#define MT6620_FM_CUST_CFG_PATH "etc/fmr/mt6620_fm_cust.cfg"
fm_s32 MT6620fm_cust_config_setup(const fm_s8 *filepath)
{
    fm_s32 ret = 0;
    fm_s8 *filep = NULL;
    fm_s8 file_path[51] = {0};

    MT6620fm_cust_config_default(&mt6620_fm_config);
    WCN_DBG(FM_NTC | MAIN, "MT6620 FM default config\n");
    MT6620fm_cust_config_print(&mt6620_fm_config);

    if (!filepath) {
        filep = MT6620_FM_CUST_CFG_PATH;
    } else {
        memcpy(file_path, filepath, (strlen(filepath) > 50) ? 50 : strlen(filepath));
        filep = file_path;
        trim_path(&filep);
    }

    ret = MT6620fm_cust_config_file(filep, &mt6620_fm_config);
    WCN_DBG(FM_NTC | MAIN, "MT6620 FM cust config\n");
    MT6620fm_cust_config_print(&mt6620_fm_config);
	
    return ret;
}

fm_u16 MT6620fm_cust_config_fetch(enum fm_cust_cfg_op op_code)
{
#if 0
    fm_u16 tmp = 0;
    fm_s32 i;
    static fm_s32 fake_ch_idx = 0;

    switch (op_code) {
        //For FM RX
    case FM_CFG_RX_RSSI_TH_LONG: {
        tmp = mt6620_fm_config.rx_cfg.long_ana_rssi_th;
        break;
    }
    case FM_CFG_RX_RSSI_TH_SHORT: {
        tmp = mt6620_fm_config.rx_cfg.short_ana_rssi_th;
        break;
    }
    case FM_CFG_RX_CQI_TH: {
        tmp = mt6620_fm_config.rx_cfg.cqi_th;
        break;
    }
    case FM_CFG_RX_MR_TH: {
        tmp = mt6620_fm_config.rx_cfg.mr_th;
        break;
    }
    case FM_CFG_RX_SMG_TH: {
        tmp = mt6620_fm_config.rx_cfg.smg_th;
        break;
    }
    case FM_CFG_RX_SCAN_CH_SIZE: {
        tmp = mt6620_fm_config.rx_cfg.scan_ch_size;
        break;
    }
    case FM_CFG_RX_SEEK_SPACE: {
        tmp = mt6620_fm_config.rx_cfg.seek_space;
        break;
    }
    case FM_CFG_RX_BAND: {
        tmp = mt6620_fm_config.rx_cfg.band;
        break;
    }
    case FM_CFG_RX_BAND_FREQ_L: {
        tmp = mt6620_fm_config.rx_cfg.band_freq_l;
        break;
    }
    case FM_CFG_RX_BAND_FREQ_H: {
        tmp = mt6620_fm_config.rx_cfg.band_freq_h;
        break;
    }
    case FM_CFG_RX_SCAN_SORT: {
        tmp = mt6620_fm_config.rx_cfg.scan_sort;
        break;
    }
    case FM_CFG_RX_FAKE_CH_NUM: {
        tmp = mt6620_fm_config.rx_cfg.fake_ch_num;
        break;
    }
    case FM_CFG_RX_FAKE_CH: {
        tmp = mt6620_fm_config.rx_cfg.fake_ch[fake_ch_idx];
        i = (mt6620_fm_config.rx_cfg.fake_ch_num > 0) ? mt6620_fm_config.rx_cfg.fake_ch_num : FAKE_CH_MAX;
        fake_ch_idx++;
        fake_ch_idx = fake_ch_idx % i;
        break;
    }
    case FM_CFG_RX_FAKE_CH_RSSI: {
        tmp = mt6620_fm_config.rx_cfg.fake_ch_rssi_th;
        break;
    }
    case FM_CFG_RX_DEEMPHASIS: {
        tmp = mt6620_fm_config.rx_cfg.deemphasis;
        break;
    }
    case FM_CFG_RX_OSC_FREQ: {
        tmp = mt6620_fm_config.rx_cfg.osc_freq;
        break;
    }
    //For FM TX
    case FM_CFG_TX_SCAN_HOLE_LOW: {
        tmp = mt6620_fm_config.tx_cfg.scan_hole_low;
        break;
    }
    case FM_CFG_TX_SCAN_HOLE_HIGH: {
        tmp = mt6620_fm_config.tx_cfg.scan_hole_high;
        break;
    }
    case FM_CFG_TX_PWR_LEVEL: {
        tmp = mt6620_fm_config.tx_cfg.power_level;
        break;
    }
    default:
        break;
    }

    WCN_DBG(FM_DBG | MAIN, "mt6620_cust cfg %d: 0x%04x\n", op_code, tmp);
    return tmp;
#endif
	return 0;
}
