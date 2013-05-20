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
#if (!defined(MT6628_FM)&&!defined(MT6620_FM))
#include "fm_cust_cfg.h"
#endif
static fm_cust_cfg fm_config;
static fm_s32 fm_index = 0;

#if 0
static fm_s32 to_upper(fm_s8 *str)
{
    fm_s32 i = 0;

    for (i = 0; i < (int)strlen(str); i++) {
        if (('a' <= str[i]) && (str[i] <= 'z')) {
            str[i] = str[i] - ('a' - 'A');
        }
    }

    return 0;
}
#endif

fm_s32 to_upper_n(fm_s8 *str, fm_s32 len)
{
    fm_s32 i = 0;

    for (i = 0; i < len; i++) {
        if (('a' <= str[i]) && (str[i] <= 'z')) {
            str[i] = str[i] - ('a' - 'A');
        }
    }

    return 0;
}

fm_s32 check_hex_str(fm_s8 *str, fm_s32 len)
{
    fm_s32 i = 0;

    for (i = 0; i < len; i++) {
        if ((('a' <= str[i]) && (str[i] <= 'z')) || (('A' <= str[i]) && (str[i] <= 'Z')) || (('0' <= str[i]) && (str[i] <= '9'))) {
            ;
        } else {
            return -1;
        }
    }

    return 0;
}

fm_s32 check_dec_str(fm_s8 *str, fm_s32 len)
{
    fm_s32 i = 0;

    for (i = 0; i < len; i++) {
        if (('0' <= str[i]) && (str[i] <= '9')) {
            ;
        } else {
            return -1;
        }
    }

    return 0;
}

fm_s32 ascii_to_hex(fm_s8 *in_ascii, fm_u16 *out_hex)
{
    fm_s32 len = (fm_s32)strlen(in_ascii);
    int    i = 0;
    fm_u16 tmp;

    len = (len > 4) ? 4 : len;

    if (check_hex_str(in_ascii, len)) {
        return -1;
    }

    to_upper_n(in_ascii, len);
    *out_hex = 0;

    for (i = 0; i < len; i++) {
        if (in_ascii[len-i-1] < 'A') {
            tmp = in_ascii[len-i-1];
            *out_hex |= ((tmp - '0') << (4 * i));
        } else {
            tmp = in_ascii[len-i-1];
            *out_hex |= ((tmp - 'A' + 10) << (4 * i));
        }
    }

    return 0;
}

fm_s32 ascii_to_dec(fm_s8 *in_ascii, fm_s32 *out_dec)
{
    fm_s32 len = (fm_s32)strlen(in_ascii);
    int i = 0;
    int flag;
    int multi = 1;

    len = (len > 10) ? 10 : len;

    if (in_ascii[0] == '-') {
        flag = -1;
        in_ascii += 1;
        len -= 1;
    } else {
        flag = 1;
    }

    if (check_dec_str(in_ascii, len)) {
        return -1;
    }

    *out_dec = 0;
    multi = 1;

    for (i = 0; i < len; i++) {
        *out_dec += ((in_ascii[len-i-1] - '0') * multi);
        multi *= 10;
    }

    *out_dec *= flag;
    return 0;
}

fm_s32 trim_string(fm_s8 **start)
{
    fm_s8 *end = *start;

    /* Advance to non-space character */
    while (*(*start) == ' ') {
        (*start)++;
    }

    /* Move to end of string */
    while (*end != '\0') {
        (end)++;
    }

    /* Backup to non-space character */
    do {
        end--;
    } while ((end >= *start) && (*end == ' '));

    /* Terminate string after last non-space character */
    *(++end) = '\0';
    return (end - *start);
}

fm_s32 trim_path(fm_s8 **start)
{
    fm_s8 *end = *start;

    while (*(*start) == ' ') {
        (*start)++;
    }

    while (*end != '\0') {
        (end)++;
    }

    do {
        end--;
    } while ((end >= *start) && ((*end == ' ') || (*end == '\n') || (*end == '\r')));

    *(++end) = '\0';
    return (end - *start);
}

fm_s32 cfg_parser(fm_s8 *buffer, CFG_HANDLER handler, fm_cust_cfg *cfg)
{
    fm_s32 ret = 0;
    fm_s8 *p = buffer;
    fm_s8 *group_start = NULL;
    fm_s8 *key_start = NULL;
    fm_s8 *value_start = NULL;

    enum fm_cfg_parser_state state = FM_CFG_STAT_NONE;

    FMR_ASSERT(p);

    for (p = buffer; *p != '\0'; p++) {
        switch (state) {
        case FM_CFG_STAT_NONE: {
            if (*p == '[') {
                //if we get char '[' in none state, it means a new group name start
                state = FM_CFG_STAT_GROUP;
                group_start = p + 1;
            } else if (*p == COMMENT_CHAR) {
                //if we get char '#' in none state, it means a new comment start
                state = FM_CFG_STAT_COMMENT;
            } else if (!isspace(*p) && (*p != '\n') && (*p != '\r')) {
                //if we get an nonspace char in none state, it means a new key start
                state = FM_CFG_STAT_KEY;
                key_start = p;
            }

            break;
        }
        case FM_CFG_STAT_GROUP: {
            if (*p == ']') {
                //if we get char ']' in group state, it means a group name complete
                *p = '\0';
                //FIX_ME
                //record group name
                state = FM_CFG_STAT_NONE;
                trim_string(&group_start);
                //WCN_DBG(FM_NTC|MAIN, "g=%s\n", group_start);
            }

            break;
        }
        case FM_CFG_STAT_COMMENT: {
            if (*p == '\n') {
                //if we get char '\n' in comment state, it means new line start
                state = FM_CFG_STAT_NONE;
                group_start = p + 1;
            }

            break;
        }
        case FM_CFG_STAT_KEY: {
            if (*p == DELIMIT_CHAR) {
                //if we get char '=' in key state, it means a key name complete
                *p = '\0';
                //FIX_ME
                //record key name
                state = FM_CFG_STAT_VALUE;
                value_start = p + 1;
                trim_string(&key_start);
                //WCN_DBG(FM_NTC|MAIN, "k=%s\n", key_start);
            }

            break;
        }
        case FM_CFG_STAT_VALUE: {
            if (*p == '\n' || *p == '\r') {
                //if we get char '\n' or '\r' in value state, it means a value complete
                *p = '\0';
                //record value
                trim_string(&value_start);
                //WCN_DBG(FM_NTC|MAIN, "v=%s\n", value_start);

                if (handler) {
                    ret = handler(group_start, key_start, value_start, cfg);
                }

                state = FM_CFG_STAT_NONE;
            }

            break;
        }
        default:
            break;
        }
    }

    return ret;
}

fm_s32 cfg_item_match(fm_s8 *src_key, fm_s8 *src_val, fm_s8 *dst_key, fm_s32 *dst_val)
{
    fm_s32 ret = 0;
    fm_u16 tmp_hex;
    fm_s32 tmp_dec;

	//WCN_DBG(FM_NTC|MAIN,"src_key=%s,src_val=%s\n", src_key,src_val);
	//WCN_DBG(FM_NTC|MAIN,"dst_key=%s\n", dst_key);
    if (strcmp(src_key, dst_key) == 0) {
        if (strncmp(src_val, "0x", strlen("0x")) == 0) {
            src_val += strlen("0x");
            //WCN_DBG(FM_NTC|MAIN,"%s\n", src_val);
            ret = ascii_to_hex(src_val, &tmp_hex);

            if (!ret) {
                *dst_val = tmp_hex;
                //WCN_DBG(FM_NTC|MAIN, "%s 0x%04x\n", dst_key, tmp_hex);
                return 0;
            } else {
                //WCN_DBG(FM_ERR | MAIN, "%s format error\n", dst_key);
                return 1;
            }
        } else {
            ret = ascii_to_dec(src_val, &tmp_dec);

            if (!ret /*&& ((0 <= tmp_dec) && (tmp_dec <= 0xFFFF))*/) {
                *dst_val = tmp_dec;
                //WCN_DBG(FM_NTC|MAIN, "%s %d\n", dst_key, tmp_dec);
                return 0;
            } else {
                //WCN_DBG(FM_ERR | MAIN, "%s format error\n", dst_key);
                return 1;
            }
        }
    }
    //else
    //{
	//	WCN_DBG(FM_ERR | MAIN, "src_key!=dst_key\n");
    //}

    return -1;
}

static fm_s32 cfg_item_handler(fm_s8 *grp, fm_s8 *key, fm_s8 *val, fm_cust_cfg *cfg)
{
    fm_s32 ret = 0;
    struct fm_rx_cust_cfg *rx_cfg = &cfg->rx_cfg;
    struct fm_tx_cust_cfg *tx_cfg = &cfg->tx_cfg;

    if (0 <= (ret = cfg_item_match(key, val, "FMR_RSSI_TH_L", &rx_cfg->long_ana_rssi_th))) {//FMR_RSSI_TH_L = 0x0301
        return ret;
    } else if (0 <= (ret = cfg_item_match(key, val, "FMR_RSSI_TH_S", &rx_cfg->short_ana_rssi_th))) {
        return ret;
    } else if (0 <= (ret = cfg_item_match(key, val, "FMR_CQI_TH", &rx_cfg->cqi_th))) {
        return ret;
    } else if (0 <= (ret = cfg_item_match(key, val, "FMR_MR_TH", &rx_cfg->mr_th))) {
        return ret;
    } else if (0 <= (ret = cfg_item_match(key, val, "FMR_SMG_TH", &rx_cfg->smg_th))) {
        return ret;
    } else if (0 <= (ret = cfg_item_match(key, val, "FMR_SCAN_CH_SIZE", &rx_cfg->scan_ch_size))) {
        return ret;
    } else if (0 <= (ret = cfg_item_match(key, val, "FMR_SCAN_SORT", &rx_cfg->scan_sort))) {
        return ret;
    } else if (0 <= (ret = cfg_item_match(key, val, "FMR_SEEK_SPACE", &rx_cfg->seek_space))) {
        return ret;
    } else if (0 <= (ret = cfg_item_match(key, val, "FMR_BAND", &rx_cfg->band))) {
        return ret;
    } else if (0 <= (ret = cfg_item_match(key, val, "FMR_BAND_FREQ_L", &rx_cfg->band_freq_l))) {
        return ret;
    } else if (0 <= (ret = cfg_item_match(key, val, "FMR_BAND_FREQ_H", &rx_cfg->band_freq_h))) {
        return ret;
    } else if (0 <= (ret = cfg_item_match(key, val, "FMR_FAKE_CH", &rx_cfg->fake_ch[fm_index]))) {
        fm_index += 1;
        rx_cfg->fake_ch_num = (rx_cfg->fake_ch_num < fm_index) ? fm_index : rx_cfg->fake_ch_num;
        return ret;
    } else if (0 <= (ret = cfg_item_match(key, val, "FMR_FAKE_CH_RSSI", &rx_cfg->fake_ch_rssi_th))) {
        return ret;
    } else if (0 <= (ret = cfg_item_match(key, val, "FMR_DEEMPHASIS", &rx_cfg->deemphasis))) {
        return ret;
    } else if (0 <= (ret = cfg_item_match(key, val, "FMR_OSC_FREQ", &rx_cfg->osc_freq))) {
        return ret;
    } else if (0 <= (ret = cfg_item_match(key, val, "FMT_SCAN_HOLE_L", &tx_cfg->scan_hole_low))) {
        return ret;
    } else if (0 <= (ret = cfg_item_match(key, val, "FMT_SCAN_HOLE_H", &tx_cfg->scan_hole_high))) {
        return ret;
    } else if (0 <= (ret = cfg_item_match(key, val, "FMT_PWR_LVL_MAX", &tx_cfg->power_level))) {
        return ret;
    } else {
        WCN_DBG(FM_WAR | MAIN, "invalid key\n");
        return -1;
    }
}

static fm_s32 fm_cust_config_default(fm_cust_cfg *cfg)
{
    FMR_ASSERT(cfg);
#if (!defined(MT6628_FM)&&!defined(MT6620_FM))

    cfg->rx_cfg.long_ana_rssi_th = FM_RX_RSSI_TH_LONG;
    cfg->rx_cfg.short_ana_rssi_th = FM_RX_RSSI_TH_SHORT;
    cfg->rx_cfg.cqi_th = FM_RX_CQI_TH;
    cfg->rx_cfg.mr_th = FM_RX_MR_TH;
    cfg->rx_cfg.smg_th = FM_RX_SMG_TH;
    cfg->rx_cfg.scan_ch_size = FM_RX_SCAN_CH_SIZE;
    cfg->rx_cfg.seek_space = FM_RX_SEEK_SPACE;
    cfg->rx_cfg.band = FM_RX_BAND;
    cfg->rx_cfg.band_freq_l = FM_RX_BAND_FREQ_L;
    cfg->rx_cfg.band_freq_h = FM_RX_BAND_FREQ_H;
    cfg->rx_cfg.scan_sort = FM_RX_SCAN_SORT_SELECT;
    cfg->rx_cfg.fake_ch_num = FM_RX_FAKE_CH_NUM;
    cfg->rx_cfg.fake_ch_rssi_th = FM_RX_FAKE_CH_RSSI;
    cfg->rx_cfg.fake_ch[0] = FM_RX_FAKE_CH_1;
    cfg->rx_cfg.fake_ch[1] = FM_RX_FAKE_CH_2;
    cfg->rx_cfg.fake_ch[2] = FM_RX_FAKE_CH_3;
    cfg->rx_cfg.fake_ch[3] = FM_RX_FAKE_CH_4;
    cfg->rx_cfg.fake_ch[4] = FM_RX_FAKE_CH_5;
    cfg->rx_cfg.deemphasis = FM_RX_DEEMPHASIS;

    cfg->tx_cfg.scan_hole_low = FM_TX_SCAN_HOLE_LOW;
    cfg->tx_cfg.scan_hole_high = FM_TX_SCAN_HOLE_HIGH;
    cfg->tx_cfg.power_level = FM_TX_PWR_LEVEL_MAX;
#endif
    return 0;
}

static fm_s32 fm_cust_config_file(const fm_s8 *filename, fm_cust_cfg *cfg)
{
    fm_s32 ret = 0;
    fm_s8 *buf = NULL;
    fm_s32 file_len = 0;

    if (!(buf = fm_zalloc(4096))) {
        WCN_DBG(FM_ALT | MAIN, "-ENOMEM\n");
        return -ENOMEM;
    }

    fm_index = 0;

    file_len = fm_file_read(filename, buf, 4096, 0);

    if (file_len <= 0) {
        ret = -1;
        goto out;
    }

    ret = cfg_parser(buf, cfg_item_handler, cfg);

out:

    if (buf) {
        fm_free(buf);
    }

    return ret;
}

static fm_s32 fm_cust_config_print(fm_cust_cfg *cfg)
{
    fm_s32 i;

    WCN_DBG(FM_NTC | MAIN, "rssi_l:\t0x%04x\n", cfg->rx_cfg.long_ana_rssi_th);
    WCN_DBG(FM_NTC | MAIN, "rssi_s:\t0x%04x\n", cfg->rx_cfg.short_ana_rssi_th);
    WCN_DBG(FM_NTC | MAIN, "mr_th:\t0x%04x\n", cfg->rx_cfg.mr_th);
    WCN_DBG(FM_NTC | MAIN, "cqi_th:\t0x%04x\n", cfg->rx_cfg.cqi_th);
    WCN_DBG(FM_NTC | MAIN, "smg_th:\t0x%04x\n", cfg->rx_cfg.smg_th);
    WCN_DBG(FM_NTC | MAIN, "scan_ch_size:\t%d\n", cfg->rx_cfg.scan_ch_size);
    WCN_DBG(FM_NTC | MAIN, "seek_space:\t%d\n", cfg->rx_cfg.seek_space);
    WCN_DBG(FM_NTC | MAIN, "band:\t%d\n", cfg->rx_cfg.band);
    WCN_DBG(FM_NTC | MAIN, "band_freq_l:\t%d\n", cfg->rx_cfg.band_freq_l);
    WCN_DBG(FM_NTC | MAIN, "band_freq_h:\t%d\n", cfg->rx_cfg.band_freq_h);
    WCN_DBG(FM_NTC | MAIN, "scan_sort:\t%d\n", cfg->rx_cfg.scan_sort);
    WCN_DBG(FM_NTC | MAIN, "fake_ch_num:\t%d\n", cfg->rx_cfg.fake_ch_num);
    WCN_DBG(FM_NTC | MAIN, "fake_ch_rssi_th:\t%d\n", cfg->rx_cfg.fake_ch_rssi_th);

    for (i = 0; i < cfg->rx_cfg.fake_ch_num; i++) {
        WCN_DBG(FM_NTC | MAIN, "fake_ch:\t%d\n", cfg->rx_cfg.fake_ch[i]);
    }

    WCN_DBG(FM_NTC | MAIN, "de_emphasis:\t%d\n", cfg->rx_cfg.deemphasis);
    WCN_DBG(FM_NTC | MAIN, "osc_freq:\t%d\n", cfg->rx_cfg.osc_freq);
    WCN_DBG(FM_NTC | MAIN, "scan_hole_low:\t%d\n", cfg->tx_cfg.scan_hole_low);
    WCN_DBG(FM_NTC | MAIN, "scan_hole_high:\t%d\n", cfg->tx_cfg.scan_hole_high);
    WCN_DBG(FM_NTC | MAIN, "power_level:\t%d\n", cfg->tx_cfg.power_level);

    return 0;
}
fm_s32 fm_cust_config(const fm_s8 *filepath)
{
    fm_s32 ret = 0;
    fm_s8 *filep = NULL;
    fm_s8 file_path[51] = {0};

    fm_cust_config_default(&fm_config);
    WCN_DBG(FM_NTC | MAIN, "FM default config\n");
    fm_cust_config_print(&fm_config);

    if (!filepath) {
        filep = FM_CUST_CFG_PATH;
    } else {
        memcpy(file_path, filepath, (strlen(filepath) > 50) ? 50 : strlen(filepath));
        filep = file_path;
        trim_path(&filep);
    }

    ret = fm_cust_config_file(filep, &fm_config);
    WCN_DBG(FM_NTC | MAIN, "FM cust config\n");
    fm_cust_config_print(&fm_config);
    return ret;
}

fm_u16 fm_cust_config_fetch(enum fm_cust_cfg_op op_code)
{
    fm_u16 tmp = 0;
    fm_s32 i;
    static fm_s32 fake_ch_idx = 0;

    switch (op_code) {
        //For FM RX
    case FM_CFG_RX_RSSI_TH_LONG: {
        tmp = fm_config.rx_cfg.long_ana_rssi_th;
        break;
    }
    case FM_CFG_RX_RSSI_TH_SHORT: {
        tmp = fm_config.rx_cfg.short_ana_rssi_th;
        break;
    }
    case FM_CFG_RX_CQI_TH: {
        tmp = fm_config.rx_cfg.cqi_th;
        break;
    }
    case FM_CFG_RX_MR_TH: {
        tmp = fm_config.rx_cfg.mr_th;
        break;
    }
    case FM_CFG_RX_SMG_TH: {
        tmp = fm_config.rx_cfg.smg_th;
        break;
    }
    case FM_CFG_RX_SCAN_CH_SIZE: {
        tmp = fm_config.rx_cfg.scan_ch_size;
        break;
    }
    case FM_CFG_RX_SEEK_SPACE: {
        tmp = fm_config.rx_cfg.seek_space;
        break;
    }
    case FM_CFG_RX_BAND: {
        tmp = fm_config.rx_cfg.band;
        break;
    }
    case FM_CFG_RX_BAND_FREQ_L: {
        tmp = fm_config.rx_cfg.band_freq_l;
        break;
    }
    case FM_CFG_RX_BAND_FREQ_H: {
        tmp = fm_config.rx_cfg.band_freq_h;
        break;
    }
    case FM_CFG_RX_SCAN_SORT: {
        tmp = fm_config.rx_cfg.scan_sort;
        break;
    }
    case FM_CFG_RX_FAKE_CH_NUM: {
        tmp = fm_config.rx_cfg.fake_ch_num;
        break;
    }
    case FM_CFG_RX_FAKE_CH: {
        tmp = fm_config.rx_cfg.fake_ch[fake_ch_idx];
        i = (fm_config.rx_cfg.fake_ch_num > 0) ? fm_config.rx_cfg.fake_ch_num : FAKE_CH_MAX;
        fake_ch_idx++;
        fake_ch_idx = fake_ch_idx % i;
        break;
    }
    case FM_CFG_RX_FAKE_CH_RSSI: {
        tmp = fm_config.rx_cfg.fake_ch_rssi_th;
        break;
    }
    case FM_CFG_RX_DEEMPHASIS: {
        tmp = fm_config.rx_cfg.deemphasis;
        break;
    }
    case FM_CFG_RX_OSC_FREQ: {
        tmp = fm_config.rx_cfg.osc_freq;
        break;
    }
    //For FM TX
    case FM_CFG_TX_SCAN_HOLE_LOW: {
        tmp = fm_config.tx_cfg.scan_hole_low;
        break;
    }
    case FM_CFG_TX_SCAN_HOLE_HIGH: {
        tmp = fm_config.tx_cfg.scan_hole_high;
        break;
    }
    case FM_CFG_TX_PWR_LEVEL: {
        tmp = fm_config.tx_cfg.power_level;
        break;
    }
    default:
        break;
    }

    WCN_DBG(FM_DBG | MAIN, "cust cfg %d: 0x%04x\n", op_code, tmp);
    return tmp;
}
