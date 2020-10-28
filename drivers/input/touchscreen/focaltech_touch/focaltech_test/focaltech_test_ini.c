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

#include "focaltech_test.h"

/*****************************************************************************
* Private constant and macro definitions using #define
*****************************************************************************/
struct ini_ic_type ic_types[] = {
    {"FT5X46",  0x54000002},
    {"FT5X46i", 0x54010002},
    {"FT5526",  0x54020002},
    {"FT3X17",  0x54030002},
    {"FT5436",  0x54040002},
    {"FT3X27",  0x54050002},
    {"FT5526i", 0x54060002},
    {"FT5416",  0x54070002},
    {"FT5426",  0x54080002},
    {"FT5435",  0x54090002},
    {"FT7681",  0x540A0002},
    {"FT7661",  0x540B0002},
    {"FT7511",  0x540C0002},
    {"FT7421",  0x540D0002},
    {"FT7311",  0x54100002},
    {"FT3327DQQ-001", 0x41000082},
    {"FT5446DQS-W01", 0x40000082},

    {"FT5452",  0x55000081},
    {"FT3518",  0x55010081},
    {"FT3558",  0x55020081},
    {"FT3528",  0x55030081},
    {"FT5536",  0x55040081},

    {"FT5472",  0x8F000083},
    {"FT5446U", 0x8F010083},
    {"FT5456U", 0x8F020083},
    {"FT3417U", 0x8F030083},
    {"FT5426U", 0x8F040083},
    {"FT3428",  0x8F050083},
    {"FT3437U", 0x8F060083},

    {"FT5822",  0x58000001},
    {"FT5626",  0x58010001},
    {"FT5726",  0x58020001},
    {"FT5826B", 0x58030001},
    {"FT3617",  0x58040001},
    {"FT3717",  0x58050001},
    {"FT7811",  0x58060001},
    {"FT5826S", 0x58070001},
    {"FT3517U", 0x58090001},
    {"FT3557",  0x580A0001},

    {"FT6X36",  0x63000003},
    {"FT3X07",  0x63010003},
    {"FT6416",  0x63020003},
    {"FT6336G/U", 0x63030003},
    {"FT7401",  0x63040003},
    {"FT3407U", 0x63050003},
    {"FT6236U", 0x63060003},
    {"FT6436U", 0x63070003},

    {"FT3267",  0x63080004},
    {"FT3367",  0x63090004},


    {"FT8607",  0x81000009},
    {"FT8716",  0x82000005},
    {"FT8716U", 0x44000005},
    {"FT8716F", 0x8A000005},
    {"FT8613",  0x4500000C},

    {"FT8736",  0x83000006},

    {"FT8006M", 0x87000007},
    {"FT8201",  0x87010010},
    {"FT7250",  0x87020007},

    {"FT8006U", 0x8900000B},
    {"FT8006S", 0x8901000B},

    {"FT8719",  0x9000000D},
    {"FT8615",  0x9100000F},

    {"FT8739",  0x8D00000E},

    {"FT8006P", 0x93000011},
};

/*****************************************************************************
* Global variable or extern global variabls/functions
*****************************************************************************/

/*****************************************************************************
* Static function prototypes
*****************************************************************************/
/* Works only for digits and letters, but small and fast */
#define TOLOWER(x) ((x) | 0x20)
static int fts_strncmp(const char *cs, const char *ct, int count)
{
    u8 c1 = 0, c2 = 0;

    while (count) {
        if  ((*cs == '\0') || (*ct == '\0'))
            return -1;
        c1 = TOLOWER(*cs++);
        c2 = TOLOWER(*ct++);
        if (c1 != c2)
            return c1 < c2 ? -1 : 1;
        if (!c1)
            break;
        count--;
    }

    return 0;
}

static int isspace(int x)
{
    if (x == ' ' || x == '\t' || x == '\n' || x == '\f' || x == '\b' || x == '\r')
        return 1;
    else
        return 0;
}

static int isdigit(int x)
{
    if (x <= '9' && x >= '0')
        return 1;
    else
        return 0;
}

static long fts_atol(char *nptr)
{
    int c; /* current char */
    long total; /* current total */
    int sign; /* if ''-'', then negative, otherwise positive */
    /* skip whitespace */
    while ( isspace((int)(unsigned char)*nptr) )
        ++nptr;
    c = (int)(unsigned char) * nptr++;
    sign = c; /* save sign indication */
    if (c == '-' || c == '+')
        c = (int)(unsigned char) * nptr++; /* skip sign */
    total = 0;
    while (isdigit(c)) {
        total = 10 * total + (c - '0'); /* accumulate digit */
        c = (int)(unsigned char) * nptr++; /* get next char */
    }
    if (sign == '-')
        return -total;
    else
        return total; /* return result, negated if necessary */
}

static int fts_atoi(char *nptr)
{
    return (int)fts_atol(nptr);
}

static int fts_test_get_ini_size(char *config_name)
{
    struct file *pfile = NULL;
    struct inode *inode = NULL;
    off_t fsize = 0;
    char filepath[128];

    FTS_TEST_FUNC_ENTER();

    memset(filepath, 0, sizeof(filepath));
    sprintf(filepath, "%s%s", FTS_INI_FILE_PATH, config_name);

    if (NULL == pfile)
        pfile = filp_open(filepath, O_RDONLY, 0);
    if (IS_ERR(pfile)) {
        FTS_TEST_ERROR("error occured while opening file %s.",  filepath);
        return -EIO;
    }

#if 1
    inode = pfile->f_inode;
#else
    /* reserved for linux earlier verion */
    inode = pfile->f_dentry->d_inode;
#endif
    fsize = inode->i_size;
    filp_close(pfile, NULL);

    FTS_TEST_FUNC_ENTER();

    return fsize;
}

static int fts_test_read_ini_data(char *config_name, char *config_buf)
{
    struct file *pfile = NULL;
    struct inode *inode = NULL;
    off_t fsize = 0;
    char filepath[128];
    loff_t pos = 0;
    mm_segment_t old_fs;

    FTS_TEST_FUNC_ENTER();

    memset(filepath, 0, sizeof(filepath));
    sprintf(filepath, "%s%s", FTS_INI_FILE_PATH, config_name);
    if (NULL == pfile) {
        pfile = filp_open(filepath, O_RDONLY, 0);
    }
    if (IS_ERR(pfile)) {
        FTS_TEST_ERROR("error occured while opening file %s.",  filepath);
        return -EIO;
    }

#if 1
    inode = pfile->f_inode;
#else
    /* reserved for linux earlier verion */
    inode = pfile->f_dentry->d_inode;
#endif
    fsize = inode->i_size;
    old_fs = get_fs();
    set_fs(KERNEL_DS);
    pos = 0;
    vfs_read(pfile, config_buf, fsize, &pos);
    filp_close(pfile, NULL);
    set_fs(old_fs);

    FTS_TEST_FUNC_EXIT();
    return 0;
}

static void str_space_remove(char *str)
{
    char *t = str;
    char *s = str;

    while (*t != '\0') {
        if (*t != ' ') {
            *s = *t;
            s++;
        }
        t++;
    }

    *s = '\0';
}

#ifdef INI_PRINT
static void print_ini_data(struct ini_data *ini)
{
    int i = 0;
    int j = 0;
    struct ini_section *section;
    struct ini_keyword *keyword;

    if (!ini || !ini->tmp) {
        FTS_TEST_DBG("ini is null");
        return;
    }

    FTS_TEST_DBG("section num:%d, keyword num total:%d\n",
                 ini->section_num, ini->keyword_num_total);
    for (i = 0; i < ini->section_num; i++) {
        section = &ini->section[i];
        FTS_TEST_DBG("section name:[%s] keyword num:%d\n",
                     section->name, section->keyword_num);
        for (j = 0; j < section->keyword_num; j++) {
            keyword = &section->keyword[j];
            FTS_TEST_DBG("%s=%s\n", keyword->name, keyword->value);
        }
    }
}
#endif

static int ini_get_line(char *filedata, char *line_data, int *line_len)
{
    int i = 0;
    int line_length = 0;
    int type;

    /* get a line data */
    for (i = 0; i < MAX_INI_LINE_LEN; i++) {
        if (('\n' == filedata[i]) || ('\r' == filedata[i])) {
            line_data[line_length++] = '\0';
            if (('\n' == filedata[i + 1]) || ('\r' == filedata[i + 1])) {
                line_length++;
            }
            break;
        } else {
            line_data[line_length++] = filedata[i];
        }
    }

    if (i >= MAX_INI_LINE_LEN) {
        FTS_TEST_ERROR("line length(%d)>max(%d)", line_length, MAX_INI_LINE_LEN);
        return -ENODATA;
    }

    /* remove space */
    str_space_remove(line_data);

    /* confirm line type */
    if (('\0' == line_data[0]) || ('#' == line_data[0])) {
        type = LINE_OTHER;
    } else if ('[' == line_data[0]) {
        type = LINE_SECTION;
    } else {
        type = LINE_KEYWORD; /* key word */
    }

    *line_len = line_length;
    return type;
}

static int ini_parse_keyword(struct ini_data *ini, char *line_buffer)
{
    int i = 0;
    int offset = 0;
    int length = strlen(line_buffer);
    struct ini_section *section = NULL;

    for (i = 0; i < length; i++) {
        if (line_buffer[i] == '=')
            break;
    }

    if ((i == 0) || (i >= length)) {
        FTS_TEST_ERROR("mark(=)in keyword line fail");
        return -ENODATA;
    }

    if ((ini->section_num > 0) && (ini->section_num < MAX_INI_SECTION_NUM)) {
        section = &ini->section[ini->section_num - 1];
    }

    if (NULL == section) {
        FTS_TEST_ERROR("section is null");
        return -ENODATA;
    }

    offset = ini->keyword_num_total;
    if (offset > MAX_KEYWORD_NUM) {
        FTS_TEST_ERROR("keyword num(%d)>max(%d),please check MAX_KEYWORD_NUM",
                       ini->keyword_num_total, MAX_KEYWORD_NUM);
        return -ENODATA;
    }
    memcpy(ini->tmp[offset].name, &line_buffer[0], i);
    ini->tmp[offset].name[i] = '\0';
    memcpy(ini->tmp[offset].value, &line_buffer[i + 1], length - i - 1);
    ini->tmp[offset].value[length - i - 1] = '\0';
    section->keyword_num++;
    ini->keyword_num_total++;

    return 0;
}

static int ini_parse_section(struct ini_data *ini, char *line_buffer)
{
    int length = strlen(line_buffer);
    struct ini_section *section = NULL;

    if ((length <= 2) || (length > MAX_KEYWORD_NAME_LEN)) {
        FTS_TEST_ERROR("section line length fail");
        return -EINVAL;
    }

    if ((ini->section_num < 0) || (ini->section_num > MAX_INI_SECTION_NUM)) {
        FTS_TEST_ERROR("section_num(%d) fail", ini->section_num);
        return -EINVAL;
    }
    section = &ini->section[ini->section_num];
    memcpy(section->name, line_buffer + 1, length - 2);
    section->name[length - 2] = '\0';
    FTS_TEST_INFO("section:%s, keyword offset:%d",
                  section->name, ini->keyword_num_total);
    section->keyword = (struct ini_keyword *)&ini->tmp[ini->keyword_num_total];
    section->keyword_num = 0;
    ini->section_num++;
    if (ini->section_num > MAX_INI_SECTION_NUM) {
        FTS_TEST_ERROR("section num(%d)>max(%d), please check MAX_INI_SECTION_NUM",
                       ini->section_num, MAX_INI_SECTION_NUM);
        return -ENOMEM;
    }

    return 0;
}

static int ini_init_inidata(struct ini_data *ini)
{
    int pos = 0;
    int ret = 0;
    char line_buffer[MAX_INI_LINE_LEN] = { 0 };
    int line_len = 0;

    if (!ini || !ini->data || !ini->tmp) {
        FTS_TEST_DBG("ini/data/tmp is null");
        return -EINVAL;
    }

    while (pos < ini->length) {
        ret = ini_get_line(ini->data + pos, line_buffer, &line_len);
        if (ret < 0) {
            FTS_TEST_ERROR("ini_get_line fail");
            return ret;
        } else if (ret == LINE_KEYWORD) {
            ret = ini_parse_keyword(ini, line_buffer);
            if (ret < 0) {
                FTS_TEST_ERROR("ini_parse_keyword fail");
                return ret;
            }
        } else if (ret == LINE_SECTION) {
            ret = ini_parse_section(ini, line_buffer);
            if (ret < 0) {
                FTS_TEST_ERROR("ini_parse_section fail");
                return ret;
            }
        }

        pos += line_len;
    }

    return 0;
}

static int ini_get_key(char *section_name, char *key_name, char *value)
{
    int i = 0;
    int j = 0;
    struct ini_data *ini = &fts_ftest->ini;
    struct ini_section *section;
    struct ini_keyword *keyword;
    int key_len = 0;

#ifdef INI_PRINT
    FTS_TEST_DBG("section name:%s, key name:%s\n", section_name, key_name);
    FTS_TEST_DBG("section num:%d\n", ini->section_num);
#endif
    for (i = 0; i < ini->section_num; i++) {
        section = &ini->section[i];
        key_len = strlen(section_name);
        if (key_len != strlen(section->name))
            continue;
        if (fts_strncmp(section->name, section_name, key_len) != 0)
            continue;
#ifdef INI_PRINT
        FTS_TEST_DBG("section name:%s keyword num:%d\n",
                     section->name, section->keyword_num);
#endif
        for (j = 0; j < section->keyword_num; j++) {
            keyword = &section->keyword[j];
            key_len = strlen(key_name);
            if (key_len == strlen(keyword->name)) {
                if (0 == fts_strncmp(keyword->name, key_name, key_len)) {
                    key_len = strlen(keyword->value);
                    memcpy(value, keyword->value, key_len);
                    FTS_TEST_DBG("section:%s,%s=%s\n", section_name, key_name, value);
                    return key_len;
                }
            }
        }
    }

    return -ENODATA;
}

/* return keyword's value length if success */
static int ini_get_string_value(char *section_name, char *key_name, char *rval)
{
    if (!section_name || !key_name || !rval) {
        FTS_TEST_ERROR("section_name/key_name/rval is null");
        return -EINVAL;
    }

    return ini_get_key(section_name, key_name, rval);
}

int get_keyword_value(char *section, char *name, int *value)
{
    int ret = 0;
    char str[MAX_KEYWORD_VALUE_LEN] = { 0 };

    ret = ini_get_string_value(section, name, str);
    if (ret > 0) {
        /* search successfully, so change value, otherwise keep default */
        *value = fts_atoi(str);
    }

    return ret;
}

static void fts_init_buffer(int *buffer, int value, int len)
{
    int i = 0;

    if (NULL == buffer) {
        FTS_TEST_ERROR("buffer is null\n");
        return;
    }

    for (i = 0; i < len; i++) {
        buffer[i] = value;
    }
}

static int get_test_item(char name[][MAX_KEYWORD_NAME_LEN], int length, int *val)
{
    int i = 0;
    int ret = 0;
    int tmpval = 0;

    if (length > TEST_ITEM_COUNT_MAX) {
        FTS_TEST_SAVE_ERR("test item count(%d) > max(%d)\n",
                          length, TEST_ITEM_COUNT_MAX);
        return -EINVAL;
    }

    FTS_TEST_INFO("test items in total of driver:%d", length);
    *val = 0;
    for (i = 0; i < length; i++) {
        tmpval = 0;
        ret = get_value_testitem(name[i], &tmpval);
        if (ret < 0) {
            FTS_TEST_DBG("test item:%s not found", name[i]);
        } else {
            FTS_TEST_DBG("test item:%s=%d", name[i], tmpval);
            *val |= (tmpval << i);
        }
    }

    return 0;
}

static int get_basic_threshold(char name[][MAX_KEYWORD_NAME_LEN], int length, int *val)
{
    int i = 0;
    int ret = 0;
    struct fts_test *tdata = fts_ftest;

    FTS_TEST_INFO("basic_thr string length(%d), count(%d)\n", length, tdata->basic_thr_count);
    if (length > fts_ftest->basic_thr_count) {
        FTS_TEST_SAVE_ERR("basic_thr string length > count\n");
        return -EINVAL;
    }

    for (i = 0; i < length; i++) {
        ret = get_value_basic(name[i], &val[i]);
        if (ret < 0) {
            FTS_TEST_DBG("basic thr:%s not found", name[i]);
        } else {
            FTS_TEST_DBG("basic thr:%s=%d", name[i], val[i]);
        }
    }

    return 0;
}

static void get_detail_threshold(char *key_name, bool is_prex, int *thr)
{
    char str[MAX_KEYWORD_VALUE_LEN] = { 0 };
    char str_temp[MAX_KEYWORD_NAME_LEN] = { 0 };
    char str_tmp[MAX_KEYWORD_VALUE_ONE_LEN] = { 0 };
    struct fts_test *tdata = fts_ftest;
    int divider_pos = 0;
    int index = 0;
    int i = 0;
    int j = 0;
    int k = 0;
    int tx_num = 0;
    int rx_num = 0;

    if (!key_name || !thr) {
        FTS_TEST_ERROR("key_name/thr is null");
        return;
    }

    if (is_prex) {
        tx_num = tdata->node.tx_num;
        rx_num = tdata->node.rx_num;
    }
    for (i = 0; i < tx_num + 1; i++) {
        if (is_prex) {
            snprintf(str_temp, MAX_KEYWORD_NAME_LEN, "%s%d", key_name, (i + 1));
        } else {
            snprintf(str_temp, MAX_KEYWORD_NAME_LEN, "%s", key_name);
        }
        divider_pos = ini_get_string_value("SpecialSet", str_temp, str);
        if (divider_pos <= 0)
            continue;
        index = 0;
        k = 0;
        memset(str_tmp, 0, sizeof(str_tmp));
        for (j = 0; j < divider_pos; j++) {
            if (',' == str[j]) {
                thr[i * rx_num + k] = (short)(fts_atoi(str_tmp));
                index = 0;
                memset(str_tmp, 0x00, sizeof(str_tmp));
                k++;
            } else {
                if (' ' == str[j])
                    continue;
                str_tmp[index] = str[j];
                index++;
            }
        }
    }
}

static int init_node_valid(void)
{
    char str[MAX_KEYWORD_NAME_LEN] = {0};
    int i = 0;
    int j = 0;
    int chy = 0;
    int node_num = 0;
    int cnt = 0;
    struct fts_test *tdata = fts_ftest;

    if (!tdata || !tdata->node_valid || !tdata->node_valid_sc) {
        FTS_TEST_ERROR("tdata/node_valid/node_valid_sc is null");
        return -EINVAL;
    }

    chy = tdata->node.rx_num;
    node_num = tdata->node.node_num;
    fts_init_buffer(tdata->node_valid, 1 , node_num);
    if ((tdata->func->hwtype == IC_HW_INCELL) || (tdata->func->hwtype == IC_HW_MC_SC)) {
        for (cnt = 0; cnt < node_num; cnt++) {
            i = cnt / chy + 1;
            j = cnt % chy + 1;
            snprintf(str, MAX_KEYWORD_NAME_LEN, "InvalidNode[%d][%d]", i, j);
            get_keyword_value("INVALID_NODE", str, &tdata->node_valid[cnt]);
        }
    }

    if (tdata->func->hwtype == IC_HW_MC_SC) {
        chy = tdata->sc_node.rx_num;
        node_num = tdata->sc_node.node_num;
        fts_init_buffer(tdata->node_valid_sc, 1, node_num);

        for (cnt = 0; cnt < node_num; cnt++) {
            i = (cnt >= chy) ? 2 : 1;
            j = (cnt >= chy) ? (cnt - chy + 1) : (cnt + 1);
            snprintf(str, MAX_KEYWORD_NAME_LEN, "InvalidNodeS[%d][%d]", i, j);
            get_keyword_value("INVALID_NODES", str, &tdata->node_valid_sc[cnt]);
        }
    }

    print_buffer(tdata->node_valid, tdata->node.node_num, tdata->node.rx_num);
    print_buffer(tdata->node_valid_sc, tdata->sc_node.node_num, tdata->sc_node.rx_num);
    return 0;
}

/* incell */
static int get_test_item_incell(void)
{
    int ret = 0;
    char item_name[][MAX_KEYWORD_NAME_LEN] = TEST_ITEM_INCELL;
    int length = sizeof(item_name) / MAX_KEYWORD_NAME_LEN;
    int item_val = 0;

    ret = get_test_item(item_name, length, &item_val);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("get test item fail\n");
        return ret;
    }

    fts_ftest->ic.incell.u.tmp = item_val;
    return 0;
}

static char bthr_name_incell[][MAX_KEYWORD_NAME_LEN] = BASIC_THRESHOLD_INCELL;
static int get_test_threshold_incell(void)
{
    int ret = 0;
    int length = sizeof(bthr_name_incell) / MAX_KEYWORD_NAME_LEN;
    struct fts_test *tdata = fts_ftest;
    struct incell_threshold *thr = &tdata->ic.incell.thr;
    int node_num = tdata->node.node_num;

    tdata->basic_thr_count = sizeof(struct incell_threshold_b) / sizeof(int);
    /* get standard basic threshold */
    ret = get_basic_threshold(bthr_name_incell, length, (int *)&thr->basic);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("get basic thr fail\n");
        return ret;
    }

    /* basic special set by ic */
    if (tdata->func->param_init) {
        ret = tdata->func->param_init();
        if (ret < 0) {
            FTS_TEST_SAVE_ERR("special basic thr init fail\n");
            return ret;
        }
    }

    /* init buffer */
    fts_init_buffer(thr->rawdata_max, thr->basic.rawdata_max, node_num);
    fts_init_buffer(thr->rawdata_min, thr->basic.rawdata_min, node_num);
    if (tdata->func->rawdata2_support) {
        fts_init_buffer(thr->rawdata2_max, thr->basic.rawdata2_max, node_num);
        fts_init_buffer(thr->rawdata2_min, thr->basic.rawdata2_min, node_num);
    }
    fts_init_buffer(thr->cb_max, thr->basic.cb_max, node_num);
    fts_init_buffer(thr->cb_min, thr->basic.cb_min, node_num);

    /* detail threshold */
    get_detail_threshold("RawData_Max_Tx", true, thr->rawdata_max);
    get_detail_threshold("RawData_Min_Tx", true, thr->rawdata_min);
    get_detail_threshold("CB_Max_Tx", true, thr->cb_max);
    get_detail_threshold("CB_Min_Tx", true, thr->cb_min);

    return 0;
}

static void print_thr_incell(void)
{
    struct fts_test *tdata = fts_ftest;
    struct incell_threshold *thr = &tdata->ic.incell.thr;

    FTS_TEST_DBG("short_res_min:%d", thr->basic.short_res_min);
    FTS_TEST_DBG("short_res_vk_min:%d", thr->basic.short_res_vk_min);
    FTS_TEST_DBG("open_cb_min:%d", thr->basic.open_cb_min);
    FTS_TEST_DBG("open_k1_check:%d", thr->basic.open_k1_check);
    FTS_TEST_DBG("open_k1_value:%d", thr->basic.open_k1_value);
    FTS_TEST_DBG("open_k2_check:%d", thr->basic.open_k2_check);
    FTS_TEST_DBG("open_k2_value:%d", thr->basic.open_k2_value);
    FTS_TEST_DBG("cb_min:%d", thr->basic.cb_min);
    FTS_TEST_DBG("cb_max:%d", thr->basic.cb_max);
    FTS_TEST_DBG("cb_vkey_check:%d", thr->basic.cb_vkey_check);
    FTS_TEST_DBG("cb_min_vk:%d", thr->basic.cb_min_vk);
    FTS_TEST_DBG("cb_max_vk:%d", thr->basic.cb_max_vk);
    FTS_TEST_DBG("rawdata_min:%d", thr->basic.rawdata_min);
    FTS_TEST_DBG("rawdata_max:%d", thr->basic.rawdata_max);
    FTS_TEST_DBG("rawdata_vkey_check:%d", thr->basic.rawdata_vkey_check);
    FTS_TEST_DBG("rawdata_min_vk:%d", thr->basic.rawdata_min_vk);
    FTS_TEST_DBG("rawdata_max_vk:%d", thr->basic.rawdata_max_vk);
    FTS_TEST_DBG("lcdnoise_frame:%d", thr->basic.lcdnoise_frame);
    FTS_TEST_DBG("lcdnoise_coefficient:%d", thr->basic.lcdnoise_coefficient);
    FTS_TEST_DBG("lcdnoise_coefficient_vkey:%d", thr->basic.lcdnoise_coefficient_vkey);

    FTS_TEST_DBG("open_nmos:%d", thr->basic.open_nmos);
    FTS_TEST_DBG("keyshort_k1:%d", thr->basic.keyshort_k1);
    FTS_TEST_DBG("keyshort_cb_max:%d", thr->basic.keyshort_cb_max);
    FTS_TEST_DBG("rawdata2_min:%d", thr->basic.rawdata2_min);
    FTS_TEST_DBG("rawdata2_max:%d", thr->basic.rawdata2_max);


    print_buffer(thr->rawdata_min, tdata->node.node_num, tdata->node.rx_num);
    print_buffer(thr->rawdata_max, tdata->node.node_num, tdata->node.rx_num);
    print_buffer(thr->cb_min, tdata->node.node_num, tdata->node.rx_num);
    print_buffer(thr->cb_max, tdata->node.node_num, tdata->node.rx_num);
    print_buffer(thr->rawdata2_min, tdata->node.node_num, tdata->node.rx_num);
    print_buffer(thr->rawdata2_max, tdata->node.node_num, tdata->node.rx_num);
}

static int ini_init_test_incell(void)
{
    int ret = 0;

    ret = get_test_item_incell();
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("get incell test item fail\n");
        return ret;
    }


    ret = get_test_threshold_incell();
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("get incell threshold fail\n");
        return ret;
    }

    print_thr_incell();
    return 0;
}

/* mc_sc */
static int get_test_item_mc_sc(void)
{
    int ret = 0;
    char item_name[][MAX_KEYWORD_NAME_LEN] = TEST_ITEM_MC_SC;
    int length = sizeof(item_name) / MAX_KEYWORD_NAME_LEN;
    int item_val = 0;

    ret = get_test_item(item_name, length, &item_val);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("get test item fail\n");
        return ret;
    }

    fts_ftest->ic.mc_sc.u.tmp = item_val;
    FTS_TEST_INFO("test item:0x%x in ini", fts_ftest->ic.mc_sc.u.tmp);
    return 0;
}

static char bthr_name_mc_sc[][MAX_KEYWORD_NAME_LEN] = BASIC_THRESHOLD_MC_SC;
static int get_test_threshold_mc_sc(void)
{
    int ret = 0;
    int length = sizeof(bthr_name_mc_sc) / MAX_KEYWORD_NAME_LEN;
    struct fts_test *tdata = fts_ftest;
    struct mc_sc_threshold *thr = &tdata->ic.mc_sc.thr;
    int node_num = tdata->node.node_num;
    int sc_num = tdata->sc_node.node_num;

    tdata->basic_thr_count = sizeof(struct mc_sc_threshold_b) / sizeof(int);
    /* get standard basic threshold */
    ret = get_basic_threshold(bthr_name_mc_sc, length, (int *)&thr->basic);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("get basic thr fail\n");
        return ret;
    }

    /* basic special set by ic */
    if (tdata->func->param_init) {
        ret = tdata->func->param_init();
        if (ret < 0) {
            FTS_TEST_SAVE_ERR("special basic thr init fail\n");
            return ret;
        }
    }

    /* init buffer */
    fts_init_buffer(thr->rawdata_h_min, thr->basic.rawdata_h_min, node_num);
    fts_init_buffer(thr->rawdata_h_max, thr->basic.rawdata_h_max, node_num);
    if (tdata->func->rawdata2_support) {
        fts_init_buffer(thr->rawdata_l_min, thr->basic.rawdata_l_min, node_num);
        fts_init_buffer(thr->rawdata_l_max, thr->basic.rawdata_l_max, node_num);
    }
    fts_init_buffer(thr->tx_linearity_max, thr->basic.uniformity_tx_hole, node_num);
    fts_init_buffer(thr->tx_linearity_min, 0, node_num);
    fts_init_buffer(thr->rx_linearity_max, thr->basic.uniformity_rx_hole, node_num);
    fts_init_buffer(thr->rx_linearity_min, 0, node_num);
    fts_init_buffer(thr->scap_cb_off_min, thr->basic.scap_cb_off_min, sc_num);
    fts_init_buffer(thr->scap_cb_off_max, thr->basic.scap_cb_off_max, sc_num);
    fts_init_buffer(thr->scap_cb_on_min, thr->basic.scap_cb_on_min, sc_num);
    fts_init_buffer(thr->scap_cb_on_max, thr->basic.scap_cb_on_max, sc_num);
    fts_init_buffer(thr->scap_rawdata_off_min, thr->basic.scap_rawdata_off_min, sc_num);
    fts_init_buffer(thr->scap_rawdata_off_max, thr->basic.scap_rawdata_off_max, sc_num);
    fts_init_buffer(thr->scap_rawdata_on_min, thr->basic.scap_rawdata_on_min, sc_num);
    fts_init_buffer(thr->scap_rawdata_on_max, thr->basic.scap_rawdata_on_max, sc_num);
    fts_init_buffer(thr->panel_differ_min, thr->basic.panel_differ_min, node_num);
    fts_init_buffer(thr->panel_differ_max, thr->basic.panel_differ_max, node_num);

    /* detail threshold */
    get_detail_threshold("RawData_Min_High_Tx", true, thr->rawdata_h_min);
    get_detail_threshold("RawData_Max_High_Tx", true, thr->rawdata_h_max);
    if (tdata->func->rawdata2_support) {
        get_detail_threshold("RawData_Min_Low_Tx", true, thr->rawdata_l_min);
        get_detail_threshold("RawData_Max_Low_Tx", true, thr->rawdata_l_max);
    }
    get_detail_threshold("Tx_Linearity_Max_Tx", true, thr->tx_linearity_max);
    get_detail_threshold("Rx_Linearity_Max_Tx", true, thr->rx_linearity_max);
    get_detail_threshold("ScapCB_OFF_Min_", true, thr->scap_cb_off_min);
    get_detail_threshold("ScapCB_OFF_Max_", true, thr->scap_cb_off_max);
    get_detail_threshold("ScapCB_ON_Min_", true, thr->scap_cb_on_min);
    get_detail_threshold("ScapCB_ON_Max_", true, thr->scap_cb_on_max);
    get_detail_threshold("ScapRawData_OFF_Min_", true, thr->scap_rawdata_off_min);
    get_detail_threshold("ScapRawData_OFF_Max_", true, thr->scap_rawdata_off_max);
    get_detail_threshold("ScapRawData_ON_Min_", true, thr->scap_rawdata_on_min);
    get_detail_threshold("ScapRawData_ON_Max_", true, thr->scap_rawdata_on_max);
    get_detail_threshold("Panel_Differ_Min_Tx", true, thr->panel_differ_min);
    get_detail_threshold("Panel_Differ_Max_Tx", true, thr->panel_differ_max);

    return 0;
}

static void print_thr_mc_sc(void)
{
    struct fts_test *tdata = fts_ftest;
    struct mc_sc_threshold *thr = &tdata->ic.mc_sc.thr;

    FTS_TEST_DBG("rawdata_h_min:%d", thr->basic.rawdata_h_min);
    FTS_TEST_DBG("rawdata_h_max:%d", thr->basic.rawdata_h_max);
    FTS_TEST_DBG("rawdata_set_hfreq:%d", thr->basic.rawdata_set_hfreq);
    FTS_TEST_DBG("rawdata_l_min:%d", thr->basic.rawdata_l_min);
    FTS_TEST_DBG("rawdata_l_max:%d", thr->basic.rawdata_l_max);
    FTS_TEST_DBG("rawdata_set_lfreq:%d", thr->basic.rawdata_set_lfreq);
    FTS_TEST_DBG("uniformity_check_tx:%d", thr->basic.uniformity_check_tx);
    FTS_TEST_DBG("uniformity_check_rx:%d", thr->basic.uniformity_check_rx);
    FTS_TEST_DBG("uniformity_check_min_max:%d", thr->basic.uniformity_check_min_max);
    FTS_TEST_DBG("uniformity_tx_hole:%d", thr->basic.uniformity_tx_hole);
    FTS_TEST_DBG("uniformity_rx_hole:%d", thr->basic.uniformity_rx_hole);
    FTS_TEST_DBG("uniformity_min_max_hole:%d", thr->basic.uniformity_min_max_hole);
    FTS_TEST_DBG("scap_cb_off_min:%d", thr->basic.scap_cb_off_min);
    FTS_TEST_DBG("scap_cb_off_max:%d", thr->basic.scap_cb_off_max);
    FTS_TEST_DBG("scap_cb_wp_off_check:%d", thr->basic.scap_cb_wp_off_check);
    FTS_TEST_DBG("scap_cb_on_min:%d", thr->basic.scap_cb_on_min);
    FTS_TEST_DBG("scap_cb_on_max:%d", thr->basic.scap_cb_on_max);
    FTS_TEST_DBG("scap_cb_wp_on_check:%d", thr->basic.scap_cb_wp_on_check);
    FTS_TEST_DBG("scap_rawdata_off_min:%d", thr->basic.scap_rawdata_off_min);
    FTS_TEST_DBG("scap_rawdata_off_max:%d", thr->basic.scap_rawdata_off_max);
    FTS_TEST_DBG("scap_rawdata_wp_off_check:%d", thr->basic.scap_rawdata_wp_off_check);
    FTS_TEST_DBG("scap_rawdata_on_min:%d", thr->basic.scap_rawdata_on_min);
    FTS_TEST_DBG("scap_rawdata_on_max:%d", thr->basic.scap_rawdata_on_max);
    FTS_TEST_DBG("scap_rawdata_wp_on_check:%d", thr->basic.scap_rawdata_wp_on_check);
    FTS_TEST_DBG("short_cg:%d", thr->basic.short_cg);
    FTS_TEST_DBG("short_cc:%d", thr->basic.short_cc);
    FTS_TEST_DBG("panel_differ_min:%d", thr->basic.panel_differ_min);
    FTS_TEST_DBG("panel_differ_max:%d", thr->basic.panel_differ_max);

    print_buffer(thr->rawdata_h_min, tdata->node.node_num, tdata->node.rx_num);
    print_buffer(thr->rawdata_h_max, tdata->node.node_num, tdata->node.rx_num);
    print_buffer(thr->rawdata_l_min, tdata->node.node_num, tdata->node.rx_num);
    print_buffer(thr->rawdata_l_max, tdata->node.node_num, tdata->node.rx_num);
    print_buffer(thr->scap_cb_off_min, tdata->sc_node.node_num, tdata->sc_node.rx_num);
    print_buffer(thr->scap_cb_off_max, tdata->sc_node.node_num, tdata->sc_node.rx_num);
    print_buffer(thr->scap_cb_on_min, tdata->sc_node.node_num, tdata->sc_node.rx_num);
    print_buffer(thr->scap_cb_on_max, tdata->sc_node.node_num, tdata->sc_node.rx_num);
    print_buffer(thr->scap_rawdata_off_min, tdata->sc_node.node_num, tdata->sc_node.rx_num);
    print_buffer(thr->scap_rawdata_off_max, tdata->sc_node.node_num, tdata->sc_node.rx_num);
    print_buffer(thr->scap_rawdata_on_min, tdata->sc_node.node_num, tdata->sc_node.rx_num);
    print_buffer(thr->scap_rawdata_on_max, tdata->sc_node.node_num, tdata->sc_node.rx_num);
    print_buffer(thr->panel_differ_min, tdata->node.node_num, tdata->node.rx_num);
    print_buffer(thr->panel_differ_max, tdata->node.node_num, tdata->node.rx_num);
}

static int ini_init_test_mc_sc(void)
{
    int ret = 0;

    ret = get_test_item_mc_sc();
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("get mc_sc test item fail\n");
        return ret;
    }

    ret = get_test_threshold_mc_sc();
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("get mc_sc threshold fail\n");
        return ret;
    }

    print_thr_mc_sc();
    return 0;
}

/* sc */
static int get_test_item_sc(void)
{
    int ret = 0;
    char item_name[][MAX_KEYWORD_NAME_LEN] = TEST_ITEM_SC;
    int length = sizeof(item_name) / MAX_KEYWORD_NAME_LEN;
    int item_val = 0;

    ret = get_test_item(item_name, length, &item_val);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("get test item fail\n");
        return ret;
    }

    fts_ftest->ic.sc.u.tmp = item_val;
    return 0;
}

static char bthr_name_sc[][MAX_KEYWORD_NAME_LEN] = BASIC_THRESHOLD_SC;
static int get_test_threshold_sc(void)
{
    int ret = 0;
    int length = sizeof(bthr_name_sc) / MAX_KEYWORD_NAME_LEN;
    struct fts_test *tdata = fts_ftest;
    struct sc_threshold *thr = &tdata->ic.sc.thr;
    int node_num = tdata->node.node_num;

    tdata->basic_thr_count = sizeof(struct sc_threshold_b) / sizeof(int);
    /* get standard basic threshold */
    ret = get_basic_threshold(bthr_name_sc, length, (int *)&thr->basic);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("get basic thr fail\n");
        return ret;
    }

    /* basic special set by ic */
    if (tdata->func->param_init) {
        ret = tdata->func->param_init();
        if (ret < 0) {
            FTS_TEST_SAVE_ERR("special basic thr init fail\n");
            return ret;
        }
    }

    /* init buffer */
    fts_init_buffer(thr->rawdata_min, thr->basic.rawdata_min, node_num);
    fts_init_buffer(thr->rawdata_max, thr->basic.rawdata_max, node_num);
    fts_init_buffer(thr->cb_min, thr->basic.cb_min, node_num);
    fts_init_buffer(thr->cb_max, thr->basic.cb_max, node_num);
    fts_init_buffer(thr->dcb_sort, 0, node_num);
    fts_init_buffer(thr->dcb_base, thr->basic.dcb_base, node_num);

    /* detail threshold */
    get_detail_threshold("RawDataTest_Min", false, thr->rawdata_min);
    get_detail_threshold("RawDataTest_Max", false, thr->rawdata_max);
    get_detail_threshold("CbTest_Min", false, thr->cb_min);
    get_detail_threshold("CbTest_Max", false, thr->cb_max);
    get_detail_threshold("DeltaCxTest_Sort", false, thr->dcb_sort);
    get_detail_threshold("DeltaCbTest_Base", false, thr->dcb_base);

    return 0;
}

static void print_thr_sc(void)
{
    struct fts_test *tdata = fts_ftest;
    struct sc_threshold *thr = &tdata->ic.sc.thr;

    FTS_TEST_DBG("rawdata_min:%d", thr->basic.rawdata_min);
    FTS_TEST_DBG("rawdata_max:%d", thr->basic.rawdata_max);
    FTS_TEST_DBG("cb_min:%d", thr->basic.cb_min);
    FTS_TEST_DBG("cb_max:%d", thr->basic.cb_max);
    FTS_TEST_DBG("dcb_differ_max:%d", thr->basic.dcb_differ_max);
    FTS_TEST_DBG("dcb_key_check:%d", thr->basic.dcb_key_check);
    FTS_TEST_DBG("dcb_key_differ_max:%d", thr->basic.dcb_key_differ_max);
    FTS_TEST_DBG("dcb_ds1:%d", thr->basic.dcb_ds1);
    FTS_TEST_DBG("dcb_ds2:%d", thr->basic.dcb_ds2);
    FTS_TEST_DBG("dcb_ds3:%d", thr->basic.dcb_ds3);
    FTS_TEST_DBG("dcb_ds4:%d", thr->basic.dcb_ds4);
    FTS_TEST_DBG("dcb_ds5:%d", thr->basic.dcb_ds5);
    FTS_TEST_DBG("dcb_ds6:%d", thr->basic.dcb_ds6);
    FTS_TEST_DBG("dcb_critical_check:%d", thr->basic.dcb_critical_check);
    FTS_TEST_DBG("dcb_cs1:%d", thr->basic.dcb_cs1);
    FTS_TEST_DBG("dcb_cs2:%d", thr->basic.dcb_cs2);
    FTS_TEST_DBG("dcb_cs3:%d", thr->basic.dcb_cs3);
    FTS_TEST_DBG("dcb_cs4:%d", thr->basic.dcb_cs4);
    FTS_TEST_DBG("dcb_cs5:%d", thr->basic.dcb_cs5);
    FTS_TEST_DBG("dcb_cs6:%d", thr->basic.dcb_cs6);

    print_buffer(thr->rawdata_min, tdata->node.node_num, tdata->node.rx_num);
    print_buffer(thr->rawdata_max, tdata->node.node_num, tdata->node.rx_num);
    print_buffer(thr->cb_min, tdata->node.node_num, tdata->node.rx_num);
    print_buffer(thr->cb_max, tdata->node.node_num, tdata->node.rx_num);
    print_buffer(thr->dcb_sort, tdata->node.node_num, tdata->node.rx_num);
    print_buffer(thr->dcb_base, tdata->node.node_num, tdata->node.rx_num);
}

static int ini_init_test_sc(void)
{
    int ret = 0;

    ret = get_test_item_sc();
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("get sc test item fail\n");
        return ret;
    }

    ret = get_test_threshold_sc();
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("get sc threshold fail\n");
        return ret;
    }

    print_thr_sc();
    return 0;
}

static u32 ini_get_ic_code(char *ic_name)
{
    int i = 0;
    int type_size = 0;
    int ini_icname_len = 0;
    int ic_types_len = 0;

    ini_icname_len = strlen(ic_name);
    type_size = sizeof(ic_types) / sizeof(ic_types[0]);
    for (i = 0; i < type_size; i++) {
        ic_types_len = strlen(ic_name);
        if (ini_icname_len == ic_types_len) {
            if (0 == strncmp(ic_name, ic_types[i].ic_name, ic_types_len))
                return ic_types[i].ic_type;
        }
    }

    FTS_TEST_ERROR("no IC type match");
    return 0;
}


static void ini_init_interface(struct ini_data *ini)
{
    char str[MAX_KEYWORD_VALUE_LEN] = { 0 };
    u32 value = 0;
    struct fts_test *tdata = fts_ftest;

    /* IC type */
    ini_get_string_value("Interface", "IC_Type", str);
    memcpy(ini->ic_name, str, sizeof(str));

    value = ini_get_ic_code(str);
    ini->ic_code = value;
    FTS_TEST_INFO("ic name:%s, ic code:%x", ini->ic_name, ini->ic_code);

    if (IC_HW_MC_SC == tdata->func->hwtype) {
        get_value_interface("Normalize_Type", &value);
        tdata->normalize = (u8)value;
        FTS_TEST_DBG("normalize:%d", tdata->normalize);
    }
}

static int ini_init_test(struct ini_data *ini)
{
    int ret = 0;
    struct fts_test *tdata = fts_ftest;

    /* interface init */
    ini_init_interface(ini);

    /* node valid */
    ret = init_node_valid();
    if (ret < 0) {
        FTS_TEST_ERROR("init node valid fail");
        return ret;
    }

    switch (tdata->func->hwtype) {
    case IC_HW_INCELL:
        ret = ini_init_test_incell();
        break;
    case IC_HW_MC_SC:
        ret = ini_init_test_mc_sc();
        break;
    case IC_HW_SC:
        ret = ini_init_test_sc();
        break;
    default:
        FTS_TEST_SAVE_ERR("test ic type(%d) fail\n", tdata->func->hwtype);
        ret = -EINVAL;
        break;
    }

    return ret;
}

/*
 * fts_test_get_testparam_from_ini - get test parameters from ini
 *
 * read, parse the configuration file, initialize the test variable
 *
 * return 0 if succuss, else errro code
 */
int fts_test_get_testparam_from_ini(char *config_name)
{
    int ret = 0;
    int inisize = 0;
    struct ini_data *ini = &fts_ftest->ini;

    inisize = fts_test_get_ini_size(config_name);
    FTS_TEST_DBG("ini file size:%d", inisize);
    if (inisize <= 0) {
        FTS_TEST_ERROR("get ini file size fail");
        return -ENODATA;
    }

    ini->data = vmalloc(inisize + 1);
    if (NULL == ini->data) {
        FTS_TEST_ERROR("malloc memory for ini data fail");
        return -ENOMEM;
    }
    memset(ini->data, 0, inisize + 1);
    ini->length = inisize + 1;
    ini->keyword_num_total = 0;
    ini->section_num = 0;

    ini->tmp = vmalloc(sizeof(struct ini_keyword) * MAX_KEYWORD_NUM);
    if (NULL == ini->tmp) {
        FTS_TEST_ERROR("malloc memory for ini tmp fail");
        ret = -ENOMEM;
        goto ini_tmp_err;
    }
    memset(ini->tmp, 0, sizeof(struct ini_keyword) * MAX_KEYWORD_NUM);

    ret = fts_test_read_ini_data(config_name, ini->data);
    if (ret) {
        FTS_TEST_ERROR("read ini file fail");
        goto get_inidata_err;
    }
    ini->data[inisize] = '\n';  /* last line is null line */

    /* parse ini data to get keyword name&value */
    ret = ini_init_inidata(ini);
    if (ret < 0) {
        FTS_TEST_ERROR("ini_init_inidata fail");
        goto get_inidata_err;
    }

    /* parse threshold & test item */
    ret = ini_init_test(ini);
    if (ret < 0) {
        FTS_TEST_ERROR("ini init fail");
        goto get_inidata_err;
    }

    ret = 0;
get_inidata_err:
    if (ini->tmp) {
        vfree(ini->tmp);
        ini->tmp = NULL;
    }
ini_tmp_err:
    if (ini->data) {
        vfree(ini->data);
        ini->data = NULL;
    }

    return ret;
}
