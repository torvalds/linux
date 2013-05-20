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
#include "mt6628_fm_cust_cfg.h"
fm_cust_cfg mt6628_fm_config;
//static fm_s32 fm_index = 0;

static fm_s32 MT6628fm_cust_config_print(fm_cust_cfg *cfg)
{
    WCN_DBG(FM_NTC | MAIN, "MT6628 rssi_l:\t%d\n", cfg->rx_cfg.long_ana_rssi_th);
    WCN_DBG(FM_NTC | MAIN, "MT6628 rssi_s:\t%d\n", cfg->rx_cfg.short_ana_rssi_th);
    WCN_DBG(FM_NTC | MAIN, "MT6628 pamd_th:\t%d\n", cfg->rx_cfg.pamd_th);
    WCN_DBG(FM_NTC | MAIN, "MT6628 mr_th:\t%d\n", cfg->rx_cfg.mr_th);
    WCN_DBG(FM_NTC | MAIN, "MT6628 atdc_th:\t%d\n", cfg->rx_cfg.atdc_th);
    WCN_DBG(FM_NTC | MAIN, "MT6628 prx_th:\t%d\n", cfg->rx_cfg.prx_th);
    WCN_DBG(FM_NTC | MAIN, "MT6628 atdev_th:\t%d\n", cfg->rx_cfg.atdev_th);
    WCN_DBG(FM_NTC | MAIN, "MT6628 smg_th:\t%d\n", cfg->rx_cfg.smg_th);
    WCN_DBG(FM_NTC | MAIN, "de_emphasis:\t%d\n", cfg->rx_cfg.deemphasis);
    WCN_DBG(FM_NTC | MAIN, "osc_freq:\t%d\n", cfg->rx_cfg.osc_freq);

    return 0;
}

static fm_s32 MT6628cfg_item_handler(fm_s8 *grp, fm_s8 *key, fm_s8 *val, fm_cust_cfg *cfg)
{
    fm_s32 ret = 0;
    struct fm_rx_cust_cfg *rx_cfg = &cfg->rx_cfg;

    if (0 <= (ret = cfg_item_match(key, val, "FM_RX_RSSI_TH_LONG_MT6628", &rx_cfg->long_ana_rssi_th))) 
    {//FMR_RSSI_TH_L = 0x0301
        return ret;
    } 
    else if (0 <= (ret = cfg_item_match(key, val, "FM_RX_RSSI_TH_SHORT_MT6628", &rx_cfg->short_ana_rssi_th))) 
    {
        return ret;
    } 
    else if (0 <= (ret = cfg_item_match(key, val, "FM_RX_DESENSE_RSSI_MT6628", &rx_cfg->desene_rssi_th))) 
    {
        return ret;
    } 
    else if (0 <= (ret = cfg_item_match(key, val, "FM_RX_PAMD_TH_MT6628", &rx_cfg->pamd_th))) 
    {
        return ret;
    } 
    else if (0 <= (ret = cfg_item_match(key, val, "FM_RX_MR_TH_MT6628", &rx_cfg->mr_th))) 
    {
        return ret;
    } 
    else if (0 <= (ret = cfg_item_match(key, val, "FM_RX_ATDC_TH_MT6628", &rx_cfg->atdc_th))) 
    {
        return ret;
    } 
    else if (0 <= (ret = cfg_item_match(key, val, "FM_RX_PRX_TH_MT6628", &rx_cfg->prx_th))) 
    {
        return ret;
    } 
    /*else if (0 <= (ret = cfg_item_match(key, val, "FM_RX_ATDEV_TH_MT6628", &rx_cfg->atdev_th))) 
    {
        return ret;
    } */
    else if (0 <= (ret = cfg_item_match(key, val, "FM_RX_SMG_TH_MT6628", &rx_cfg->smg_th))) 
    {
        return ret;
    } 
    else if (0 <= (ret = cfg_item_match(key, val, "FM_RX_DEEMPHASIS_MT6628", &rx_cfg->deemphasis))) 
    {
        return ret;
    } 
    else if (0 <= (ret = cfg_item_match(key, val, "FM_RX_OSC_FREQ_MT6628", &rx_cfg->osc_freq))) 
    {
        return ret;
    } 
    else 
    {
        WCN_DBG(FM_WAR | MAIN, "MT6628 invalid key\n");
        return -1;
    }
}

static fm_s32 MT6628fm_cust_config_default(fm_cust_cfg *cfg)
{
    FMR_ASSERT(cfg);

    cfg->rx_cfg.long_ana_rssi_th = FM_RX_RSSI_TH_LONG_MT6628;
    cfg->rx_cfg.short_ana_rssi_th = FM_RX_RSSI_TH_SHORT_MT6628;
    cfg->rx_cfg.desene_rssi_th = FM_RX_DESENSE_RSSI_MT6628;
    cfg->rx_cfg.pamd_th = FM_RX_PAMD_TH_MT6628;
    cfg->rx_cfg.mr_th = FM_RX_MR_TH_MT6628;
    cfg->rx_cfg.atdc_th = FM_RX_ATDC_TH_MT6628;
    cfg->rx_cfg.prx_th = FM_RX_PRX_TH_MT6628;
    cfg->rx_cfg.smg_th = FM_RX_SMG_TH_MT6628;
    cfg->rx_cfg.deemphasis = FM_RX_DEEMPHASIS_MT6628;
	cfg->rx_cfg.osc_freq = FM_RX_OSC_FREQ_MT6628;

    return 0;
}

static fm_s32 MT6628fm_cust_config_file(const fm_s8 *filename, fm_cust_cfg *cfg)
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

    ret = cfg_parser(buf, MT6628cfg_item_handler, cfg);

out:

    if (buf) {
        fm_free(buf);
    }

    return ret;
}
#define MT6628_FM_CUST_CFG_PATH "etc/fmr/fm_cust.cfg"
fm_s32 MT6628fm_cust_config_setup(const fm_s8 *filepath)
{
    fm_s32 ret = 0;
    fm_s8 *filep = NULL;
    fm_s8 file_path[51] = {0};

    MT6628fm_cust_config_default(&mt6628_fm_config);
    WCN_DBG(FM_NTC | MAIN, "MT6628 FM default config\n");
    MT6628fm_cust_config_print(&mt6628_fm_config);
	
    if (!filepath) {
        filep = MT6628_FM_CUST_CFG_PATH;
    } else {
        memcpy(file_path, filepath, (strlen(filepath) > 50) ? 50 : strlen(filepath));
        filep = file_path;
        trim_path(&filep);
    }

    ret = MT6628fm_cust_config_file(filep, &mt6628_fm_config);
    WCN_DBG(FM_NTC | MAIN, "MT6628 FM cust config\n");
    MT6628fm_cust_config_print(&mt6628_fm_config);
	
    return ret;
}
fm_u16 MT6628fm_cust_config_fetch(enum fm_cust_cfg_op op_code)
{
#if 0
    fm_u16 tmp = 0;
    fm_s32 i;
    static fm_s32 fake_ch_idx = 0;

    switch (op_code) {
        //For FM RX
    case FM_CFG_RX_RSSI_TH_LONG: {
        tmp = mt6628_fm_config.rx_cfg.long_ana_rssi_th;
        break;
    }
    case FM_CFG_RX_RSSI_TH_SHORT: {
        tmp = mt6628_fm_config.rx_cfg.short_ana_rssi_th;
        break;
    }
    case FM_CFG_RX_CQI_TH: {
        tmp = mt6628_fm_config.rx_cfg.cqi_th;
        break;
    }
    case FM_CFG_RX_MR_TH: {
        tmp = mt6628_fm_config.rx_cfg.mr_th;
        break;
    }
    case FM_CFG_RX_SMG_TH: {
        tmp = mt6628_fm_config.rx_cfg.smg_th;
        break;
    }
    case FM_CFG_RX_SCAN_CH_SIZE: {
        tmp = mt6628_fm_config.rx_cfg.scan_ch_size;
        break;
    }
    case FM_CFG_RX_SEEK_SPACE: {
        tmp = mt6628_fm_config.rx_cfg.seek_space;
        break;
    }
    case FM_CFG_RX_BAND: {
        tmp = mt6628_fm_config.rx_cfg.band;
        break;
    }
    case FM_CFG_RX_BAND_FREQ_L: {
        tmp = mt6628_fm_config.rx_cfg.band_freq_l;
        break;
    }
    case FM_CFG_RX_BAND_FREQ_H: {
        tmp = mt6628_fm_config.rx_cfg.band_freq_h;
        break;
    }
    case FM_CFG_RX_SCAN_SORT: {
        tmp = mt6628_fm_config.rx_cfg.scan_sort;
        break;
    }
    case FM_CFG_RX_FAKE_CH_NUM: {
        tmp = mt6628_fm_config.rx_cfg.fake_ch_num;
        break;
    }
    case FM_CFG_RX_FAKE_CH: {
        tmp = mt6628_fm_config.rx_cfg.fake_ch[fake_ch_idx];
        i = (mt6628_fm_config.rx_cfg.fake_ch_num > 0) ? mt6628_fm_config.rx_cfg.fake_ch_num : FAKE_CH_MAX;
        fake_ch_idx++;
        fake_ch_idx = fake_ch_idx % i;
        break;
    }
    case FM_CFG_RX_FAKE_CH_RSSI: {
        tmp = mt6628_fm_config.rx_cfg.fake_ch_rssi_th;
        break;
    }
    case FM_CFG_RX_DEEMPHASIS: {
        tmp = mt6628_fm_config.rx_cfg.deemphasis;
        break;
    }
    case FM_CFG_RX_OSC_FREQ: {
        tmp = mt6628_fm_config.rx_cfg.osc_freq;
        break;
    }
    //For FM TX
    case FM_CFG_TX_SCAN_HOLE_LOW: {
        tmp = mt6628_fm_config.tx_cfg.scan_hole_low;
        break;
    }
    case FM_CFG_TX_SCAN_HOLE_HIGH: {
        tmp = mt6628_fm_config.tx_cfg.scan_hole_high;
        break;
    }
    case FM_CFG_TX_PWR_LEVEL: {
        tmp = mt6628_fm_config.tx_cfg.power_level;
        break;
    }
    default:
        break;
    }

    WCN_DBG(FM_DBG | MAIN, "mt6628_cust cfg %d: 0x%04x\n", op_code, tmp);
#endif
    return 0;
}
