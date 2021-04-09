/*
 *
 * FocalTech TouchScreen driver.
 *
 * Copyright (c) 2012-2018, FocalTech Systems, Ltd., all rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _TEST_LIB_H
#define _TEST_LIB_H

/*****************************************************************************
* Included header files
*****************************************************************************/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/i2c.h>//iic
#include <linux/delay.h>//msleep
#include <linux/string.h>
#include <asm/unistd.h>
#include <linux/vmalloc.h>

#include "../focaltech_core.h"
#include "focaltech_test_ini.h"

/*****************************************************************************
* Macro definitions using #define
*****************************************************************************/
#define FTS_INI_FILE_PATH                       "/mnt/sdcard/"
#define FTS_CSV_FILE_NAME                       "testdata.csv"
#define FTS_TXT_FILE_NAME                       "testresult.txt"
#define false 0
#define true  1
#define TEST_ICSERIES_LEN                       (8)
#define TEST_ICSERIES(x)                        ((x) >> TEST_ICSERIES_LEN)

#define TEST_OPEN_MAX_VALUE                     (255)
#define BYTES_PER_TIME                          (32)  /* max:128 */
/* CSV & TXT */
#define CSV_LINE1_BUFFER_LEN                    (1024*1)
#define CSV_LINE2_BUFFER_LEN                    (1024*2)
#define CSV_DATA_BUFFER_LEN                     (1024*80)
#define CSV_BUFFER_LEN  (CSV_LINE1_BUFFER_LEN + CSV_LINE2_BUFFER_LEN + CSV_DATA_BUFFER_LEN)
#define TXT_BUFFER_LEN                          (1024*80*5)

/*-----------------------------------------------------------
Test Status
-----------------------------------------------------------*/
#define RESULT_NULL                             0
#define RESULT_PASS                             1
#define RESULT_NG                               2

#define TX_NUM_MAX                              60
#define RX_NUM_MAX                              60
#define NUM_MAX                     ((TX_NUM_MAX)*(RX_NUM_MAX))
#define NUM_MAX_SC                              (144)
#define KEY_NUM_MAX                             6
#define TEST_ITEM_COUNT_MAX                     32
#define TEST_SHORT_RES_MAX                      0xFFFF

/*
 * factory test registers
 */
#define ENTER_WORK_FACTORY_RETRIES              5

#define START_SCAN_RETRIES_INCELL               20
#define START_SCAN_RETRIES_DELAY_INCELL         16
#define FACTORY_TEST_RETRY                      50
#define FACTORY_TEST_DELAY                      18
#define FACTORY_TEST_RETRY_DELAY                100

#define DEVIDE_MODE_ADDR                        0x00
#define REG_FW_CHIP_IDL                         0x9F
#define REG_FW_IC_TYPE                          0xA0
#define REG_FW_IC_VERSION                       0xB1
#define REG_FW_CHIP_IDH                         0xA3
#define REG_FW_VERSION                          0xA6
#define REG_VA_TOUCH_THR                        0x80
#define REG_VKEY_TOUCH_THR                      0x82

#define FACTORY_REG_LINE_ADDR                   0x01
#define FACTORY_REG_CHX_NUM                     0x02
#define FACTORY_REG_CHY_NUM                     0x03
#define FACTORY_REG_CLB                         0x04
#define FACTORY_REG_DATA_SELECT                 0x06
#define FACTORY_REG_RAWBUF_SELECT               0x09
#define FACTORY_REG_KEY_CBWIDTH                 0x0B
#define FACTORY_REG_PARAM_UPDATE_STATE          0x0E
#define FACTORY_REG_SHORT_TEST_EN               0x0F
#define FACTORY_REG_SHORT_TEST_STATE            0x10
#define FACTORY_REG_LCD_NOISE_START             0x11
#define FACTORY_REG_LCD_NOISE_FRAME             0x12
#define FACTORY_REG_LCD_NOISE_NUMBER            0x13
#define FACTORY_REG_LCD_NOISE_TTHR              0x14
#define FACTORY_REG_OPEN_START                  0x15
#define FACTORY_REG_OPEN_STATE                  0x16
#define FACTORY_REG_CB_ADDR_H                   0x18
#define FACTORY_REG_CB_ADDR_L                   0x19
#define FACTORY_REG_LCD_NOISE_STATE             0x1E
#define FACTORY_REG_KEYSHORT_EN                 0x2E
#define FACTORY_REG_KEYSHORT_STATE              0x2F

#define FACTORY_REG_LEFT_KEY                    0x1E
#define FACTORY_REG_RIGHT_KEY                   0x1F
#define FACTORY_REG_OPEN_REG20                  0x20
#define FACTORY_REG_OPEN_REG21                  0x21
#define FACTORY_REG_OPEN_REG22                  0x22
#define FACTORY_REG_OPEN_REG23                  0x23
#define FACTORY_REG_OPEN_REG86                  0x86
#define FACTORY_REG_K1                          0x31
#define FACTORY_REG_K2                          0x32
#define FACTORY_REG_RAWDATA_ADDR                0x6A
#define FACTORY_REG_CB_ADDR                     0x6E
#define FACTORY_REG_SHORT_ADDR                  0x89
#define FACTORY_REG_RAWDATA_TEST_EN             0x9E
#define FACTORY_REG_CB_TEST_EN                  0x9F
#define FACTORY_REG_OPEN_TEST_EN                0xA0

/* mc_sc */
#define FACTORY_REG_FRE_LIST                    0x0A
#define FACTORY_REG_NORMALIZE                   0x16
#define FACTORY_REG_RAWDATA_ADDR_MC_SC          0x36
#define FACTORY_REG_PATTERN                     0x53
#define FACTORY_REG_NOMAPPING                   0x54
#define FACTORY_REG_CHX_NUM_NOMAP               0x55
#define FACTORY_REG_CHY_NUM_NOMAP               0x56
#define FACTORY_REG_WC_SEL                      0x09
#define FACTORY_REG_MC_SC_MODE                  0x44
#define FACTORY_REG_MC_SC_CB_ADDR_OFF           0x45
#define FACTORY_REG_MC_SC_CB_ADDR               0x4E
#define FACTROY_REG_SHORT_TEST_EN               0x07
#define FACTROY_REG_SHORT_CA                    0x01
#define FACTROY_REG_SHORT_CC                    0x02
#define FACTROY_REG_SHORT_CG                    0x03
#define FACTROY_REG_SHORT_OFFSET                0x04
#define FACTROY_REG_SHORT_AB_CH                 0x58
#define FACTROY_REG_SHORT_DELAY                 0x5A
#define FACTORY_REG_SHORT_ADDR_MC               0xF4
#define FACTORY_REG_FIR                         0xFB

/* sc */
#define FACTORY_REG_SCAN_ADDR2                  0x08
#define FACTORY_REG_CH_NUM_SC                   0x0A
#define FACTORY_REG_KEY_NUM_SC                  0x0B
#define FACTORY_REG_SC_CB_ADDR_OFF              0x33
#define FACTORY_REG_SC_CB_ADDR                  0x39
#define FACTORY_REG_RAWDATA_SADDR_SC            0x34
#define FACTORY_REG_RAWDATA_ADDR_SC             0x35
#define FACTORY_REG_CB_SEL                      0x41
#define FACTORY_REG_FMODE                       0xAE

#define TEST_RETVAL_00                          0x00
#define TEST_RETVAL_AA                          0xAA

/*****************************************************************************
* enumerations, structures and unions
*****************************************************************************/
struct csv_format {
    u8 *buffer;
    int length;
    u8 *line2_buffer;
    int line2_len;
    int start_line;
    int item_count;
    u8 *data_buffer;
    int data_len;
};

/* incell */
struct incell_testitem {
    u32 short_test                  : 1;
    u32 open_test                   : 1;
    u32 cb_test                     : 1;
    u32 rawdata_test                : 1;
    u32 lcdnoise_test               : 1;
    u32 keyshort_test               : 1;
};

struct incell_threshold_b {
    int short_res_min;
    int short_res_vk_min;
    int open_cb_min;
    int open_k1_check;
    int open_k1_value;
    int open_k2_check;
    int open_k2_value;
    int cb_min;
    int cb_max;
    int cb_vkey_check;
    int cb_min_vk;
    int cb_max_vk;
    int rawdata_min;
    int rawdata_max;
    int rawdata_vkey_check;
    int rawdata_min_vk;
    int rawdata_max_vk;
    int lcdnoise_frame;
    int lcdnoise_coefficient;
    int lcdnoise_coefficient_vkey;
    int open_nmos;
    int keyshort_k1;
    int keyshort_cb_max;
    int rawdata2_min;
    int rawdata2_max;
};

struct incell_threshold {
    struct incell_threshold_b basic;
    int *rawdata_min;
    int *rawdata_max;
    int *rawdata2_min;
    int *rawdata2_max;
    int *cb_min;
    int *cb_max;
};

struct incell_test {
    struct incell_threshold thr;
    union {
        int tmp;
        struct incell_testitem item;
    } u;
};

/* mc_sc */
enum mapping_type {
    MAPPING = 0,
    NO_MAPPING = 1,
};

struct mc_sc_testitem {
    u32 rawdata_test                : 1;
    u32 rawdata_uniformity_test     : 1;
    u32 scap_cb_test                : 1;
    u32 scap_rawdata_test           : 1;
    u32 short_test                  : 1;
    u32 panel_differ_test           : 1;
};

struct mc_sc_threshold_b {
    int rawdata_h_min;
    int rawdata_h_max;
    int rawdata_set_hfreq;
    int rawdata_l_min;
    int rawdata_l_max;
    int rawdata_set_lfreq;
    int uniformity_check_tx;
    int uniformity_check_rx;
    int uniformity_check_min_max;
    int uniformity_tx_hole;
    int uniformity_rx_hole;
    int uniformity_min_max_hole;
    int scap_cb_off_min;
    int scap_cb_off_max;
    int scap_cb_wp_off_check;
    int scap_cb_on_min;
    int scap_cb_on_max;
    int scap_cb_wp_on_check;
    int scap_rawdata_off_min;
    int scap_rawdata_off_max;
    int scap_rawdata_wp_off_check;
    int scap_rawdata_on_min;
    int scap_rawdata_on_max;
    int scap_rawdata_wp_on_check;
    int short_cg;
    int short_cc;
    int panel_differ_min;
    int panel_differ_max;

};

struct mc_sc_threshold {
    struct mc_sc_threshold_b basic;
    int *rawdata_h_min;
    int *rawdata_h_max;
    int *rawdata_l_min;
    int *rawdata_l_max;
    int *tx_linearity_max;
    int *tx_linearity_min;
    int *rx_linearity_max;
    int *rx_linearity_min;
    int *scap_cb_off_min;
    int *scap_cb_off_max;
    int *scap_cb_on_min;
    int *scap_cb_on_max;
    int *scap_rawdata_off_min;
    int *scap_rawdata_off_max;
    int *scap_rawdata_on_min;
    int *scap_rawdata_on_max;
    int *panel_differ_min;
    int *panel_differ_max;
};

struct mc_sc_test {
    struct mc_sc_threshold thr;
    union {
        u32 tmp;
        struct mc_sc_testitem item;
    } u;
};

/* sc */
struct sc_testitem {
    u32 rawdata_test                : 1;
    u32 cb_test                     : 1;
    u32 delta_cb_test               : 1;
};

struct sc_threshold_b {
    int rawdata_min;
    int rawdata_max;
    int cb_min;
    int cb_max;
    int dcb_base;
    int dcb_differ_max;
    int dcb_key_check;
    int dcb_key_differ_max;
    int dcb_ds1;
    int dcb_ds2;
    int dcb_ds3;
    int dcb_ds4;
    int dcb_ds5;
    int dcb_ds6;
    int dcb_critical_check;
    int dcb_cs1;
    int dcb_cs2;
    int dcb_cs3;
    int dcb_cs4;
    int dcb_cs5;
    int dcb_cs6;
};

struct sc_threshold {
    struct sc_threshold_b basic;
    int *rawdata_min;
    int *rawdata_max;
    int *cb_min;
    int *cb_max;
    int *dcb_sort;
    int *dcb_base;
};

struct sc_test {
    struct sc_threshold thr;
    union {
        u32 tmp;
        struct sc_testitem item;
    } u;
};

enum test_hw_type {
    IC_HW_INCELL = 1,
    IC_HW_MC_SC,
    IC_HW_SC,
};

enum test_scan_mode {
    SCAN_NORMAL = 0,
    SCAN_SC,
};

struct fts_test_node {
    int channel_num;
    int tx_num;
    int rx_num;
    int node_num;
    int key_num;
};

struct fts_test {
    struct fts_ts_data *ts_data;
    struct fts_test_node node;
    struct fts_test_node sc_node;
    u32 ic_ver;
    u8 fw_ver;
    u8 va_touch_thr;
    u8 vk_touch_thr;
    bool key_support;
    bool v3_pattern;
    u8 mapping;
    u8 normalize;
    int test_num;
    int *buffer;
    int buffer_length;
    int *node_valid;
    int *node_valid_sc;
    int basic_thr_count;
    int code1;
    int code2;
    int offset;
    union {
        struct incell_test incell;
        struct mc_sc_test mc_sc;
        struct sc_test sc;
    } ic;

    struct test_funcs *func;
    struct csv_format csv;
    char *testresult;
    int testresult_len;
    struct ini_data ini;
};

struct test_funcs {
    u64 ctype[FTX_MAX_COMPATIBLE_TYPE];
    enum test_hw_type hwtype;
    int startscan_mode;
    int key_num_total;
    bool rawdata2_support;
    bool force_touch;
    int (*param_init)(void);
    int (*init)(void);
    int (*start_test)(void);
};

enum byte_mode {
    DATA_ONE_BYTE,
    DATA_TWO_BYTE,
};
/* mc_sc */
enum normalize_type {
    NORMALIZE_OVERALL,
    NORMALIZE_AUTO,
};

enum wp_type {
    WATER_PROOF_OFF = 0,
    WATER_PROOF_ON = 1,
    WATER_PROOF_ON_TX,
    WATER_PROOF_ON_RX,
    WATER_PROOF_OFF_TX,
    WATER_PROOF_OFF_RX,
};
/* mc end */

/* sc */
enum factory_mode {
    FACTORY_NORMAL,
    FACTORY_TESTMODE_1,
    FACTORY_TESTMODE_2,
};

enum dcb_sort_num {
    DCB_SORT_MIN = 1,
    DCB_SORT_MAX = 6,
};

struct dcb_sort_d {
    int ch_num;
    int deviation;
    int critical;
    int min;
    int max;
};
/* sc end */

enum csv_itemcode_incell {
    CODE_ENTER_FACTORY_MODE = 0,
    CODE_RAWDATA_TEST = 7,
    CODE_CB_TEST = 12,
    CODE_SHORT_TEST = 14,
    CODE_OPEN_TEST = 15,
    CODE_LCD_NOISE_TEST = 19,
};

enum csv_itemcode_mc_sc {
    CODE_M_RAWDATA_TEST = 7,
    CODE_M_SCAP_CB_TEST = 9,
    CODE_M_SCAP_RAWDATA_TEST = 10,
    CODE_M_WEAK_SHORT_CIRCUIT_TEST = 15,
    CODE_M_PANELDIFFER_TEST = 20,
};

enum csv_itemcode_sc {
    CODE_S_RAWDATA_TEST = 7,
    CODE_S_CB_TEST = 13,
    CODE_S_DCB_TEST = 14,
};

/*****************************************************************************
* Global variable or extern global variabls/functions
*****************************************************************************/
extern struct test_funcs test_func_ft8201;

extern struct fts_test *fts_ftest;

void sys_delay(int ms);
int focal_abs(int value);
void print_buffer(int *buffer, int length, int line_num);
int fts_test_read_reg(u8 addr, u8 *val);
int fts_test_write_reg(u8 addr, u8 val);
int fts_test_read(u8 addr, u8 *readbuf, int readlen);
int fts_test_write(u8 addr, u8 *writebuf, int writelen);
int enter_work_mode(void);
int enter_factory_mode(void);
int read_mass_data(u8 addr, int byte_num, int *buf);
int chip_clb(void);
int wait_state_update(u8 retval);
int get_cb_incell(u16 saddr, int byte_num, int *cb_buf);
int short_get_adcdata_incell(u8 retval, u8 ch_num, int byte_num, int *adc_buf);
int start_scan(void);
int get_rawdata(int *data);
int get_cb_sc(int byte_num, int *cb_buf, enum byte_mode mode);
bool compare_data(int *data, int min, int max, int min_vk, int max_vk, bool key);
bool compare_array(int *data, int *min, int *max, bool key);
void show_data(int *data, bool key);
void save_data_csv(int *data, char *name, u8 code, bool mc_sc, bool key);
/* mc_sc */
int mapping_switch(u8 mapping);
bool get_fw_wp(u8 wp_channel_select, enum wp_type water_proof_type);
int get_cb_mc_sc(u8 wp, int byte_num, int *cb_buf, enum byte_mode mode);
int get_rawdata_mc_sc(enum wp_type wp, int *data);
int get_rawdata_mc(u8 fre, u8 fir, int *rawdata);
void short_print_mc(int *r, int num);
int short_get_adc_data_mc(u8 retval, int byte_num, int *adc_buf, u8 mode);
bool compare_mc_sc(bool, bool, int *, int *, int *);
void show_data_mc_sc(int *data);
void *fts_malloc(size_t size);
void fts_free_proc(void *p);
int fts_test_init(struct i2c_client *client);
int fts_test_exit(struct i2c_client *client);

#define fts_malloc_r(p, size) do {\
    if (NULL == p) {\
        p = fts_malloc(size);\
        if (NULL == p) {\
            return -ENOMEM;\
        }\
    }\
} while(0)

#define fts_free(p) do {\
    if (p) {\
        fts_free_proc(p);\
        p = NULL;\
    }\
} while(0)

#define TEST_IC_VERSION(idh, idl, ver1, ver0) \
    (((idh) << 24) | ((idl) << 16) | ((ver1) << 8) | (ver0))

#define CSV_SUPPORT             1

#define FOCAL_TEST_DEBUG_EN     1
#if (FOCAL_TEST_DEBUG_EN)
#define FTS_TEST_DBG(fmt, args...) do {printk("[FTS][TEST]%s. line: %d.  "fmt"\n",  __FUNCTION__, __LINE__, ##args);} while (0)
#define FTS_TEST_FUNC_ENTER() printk("[FTS][TEST]%s: Enter(%d)\n", __func__, __LINE__)
#define FTS_TEST_FUNC_EXIT()  printk("[FTS][TEST]%s: Exit(%d)\n", __func__, __LINE__)
#else
#define FTS_TEST_DBG(fmt, args...) do{}while(0)
#define FTS_TEST_FUNC_ENTER()
#define FTS_TEST_FUNC_EXIT()
#endif

#define FTS_TEST_INFO(fmt, args...) do { printk(KERN_ERR "[FTS][TEST][Info]%s. line: %d.  "fmt"\n",  __FUNCTION__, __LINE__, ##args);} while (0)
#define FTS_TEST_ERROR(fmt, args...) do { printk(KERN_ERR "[FTS][TEST][Error]%s. line: %d.  "fmt"\n",  __FUNCTION__, __LINE__, ##args);} while (0)

#define FTS_TEST_SAVE_INFO(fmt, args...)  do { \
    if (fts_ftest->testresult) { \
        fts_ftest->testresult_len += snprintf( \
        fts_ftest->testresult + fts_ftest->testresult_len, \
        TXT_BUFFER_LEN, \
        fmt, ##args);\
    } \
} while (0)

#define FTS_TEST_SAVE_ERR(fmt, args...)  do { \
    if (fts_ftest->testresult) { \
        fts_ftest->testresult_len += snprintf( \
        fts_ftest->testresult + fts_ftest->testresult_len, \
        TXT_BUFFER_LEN, \
        fmt, ##args);\
    } \
    printk(KERN_ERR "[FTS][TEST][Error]%s. line: %d.  "fmt"",  __FUNCTION__, __LINE__, ##args);\
} while (0)
#endif
