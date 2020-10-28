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

/************************************************************************
*
* File Name: focaltech_test.c
*
* Author: Focaltech Driver Team
*
* Created: 2016-08-01
*
* Modify:
*
* Abstract: create char device and proc node for  the comm between APK and TP
*
************************************************************************/

/*****************************************************************************
* Included header files
*****************************************************************************/
#include "focaltech_test.h"

/*****************************************************************************
* Private constant and macro definitions using #define
*****************************************************************************/

/*****************************************************************************
* Global variable or extern global variabls/functions
*****************************************************************************/
struct fts_test *fts_ftest;

struct test_funcs *test_func_list[] = {
    &test_func_ft8201,
};

/*****************************************************************************
* Static function prototypes
*****************************************************************************/

/*****************************************************************************
* functions body
*****************************************************************************/
void sys_delay(int ms)
{
    msleep(ms);
}

int focal_abs(int value)
{
    if (value < 0)
        value = 0 - value;

    return value;
}

void *fts_malloc(size_t size)
{
    return kzalloc(size, GFP_KERNEL);
}

void fts_free_proc(void *p)
{
    return kfree(p);
}

void print_buffer(int *buffer, int length, int line_num)
{
    int i = 0;

    if (NULL == buffer) {
        FTS_TEST_DBG("buffer is null");
        return;
    }

    for (i = 0; i < length; i++) {
        printk("%5d ", buffer[i]);
        if ((0 == (i + 1) % line_num))
            printk("\n");
    }
    printk("\n");
}

/********************************************************************
 * test i2c read/write interface
 *******************************************************************/
static int fts_test_i2c_read(u8 *writebuf, int writelen, u8 *readbuf, int readlen)
{
    int ret = 0;
#if 1
    if (NULL == fts_data) {
        FTS_TEST_ERROR("fts_data is null, no test");
        return -EINVAL;
    }
    ret = fts_i2c_read(fts_data->client, writebuf, writelen, readbuf, readlen);
#else
    ret = fts_i2c_read(writebuf, writelen, readbuf, readlen);
#endif

    if (ret < 0)
        return ret;
    else
        return 0;
}

static int fts_test_i2c_write(u8 *writebuf, int writelen)
{
    int ret = 0;
#if 1
    if (NULL == fts_data) {
        FTS_TEST_ERROR("fts_data is null, no test");
        return -EINVAL;
    }
    ret = fts_i2c_write(fts_data->client, writebuf, writelen);
#else
    ret = fts_i2c_write(writebuf, writelen);
#endif

    if (ret < 0)
        return ret;
    else
        return 0;
}

int fts_test_read_reg(u8 addr, u8 *val)
{
    return fts_test_i2c_read(&addr, 1, val, 1);
}

int fts_test_write_reg(u8 addr, u8 val)
{
    int ret;
    u8 cmd[2] = {0};

    cmd[0] = addr;
    cmd[1] = val;
    ret = fts_test_i2c_write(cmd, 2);

    return ret;
}

int fts_test_read(u8 addr, u8 *readbuf, int readlen)
{
    int ret = 0;
    int i = 0;
    int packet_length = 0;
    int packet_num = 0;
    int packet_remainder = 0;
    int offset = 0;
    int byte_num = readlen;

    packet_num = byte_num / BYTES_PER_TIME;
    packet_remainder = byte_num % BYTES_PER_TIME;
    if (packet_remainder)
        packet_num++;

    if (byte_num < BYTES_PER_TIME) {
        packet_length = byte_num;
    } else {
        packet_length = BYTES_PER_TIME;
    }
    //    FTS_TEST_DBG("packet num:%d, remainder:%d", packet_num, packet_remainder);

    ret = fts_test_i2c_read(&addr, 1, &readbuf[offset], packet_length);
    if (ret < 0) {
        FTS_TEST_ERROR("read buffer fail");
        return ret;
    }
    for (i = 1; i < packet_num; i++) {
        offset += packet_length;
        if ((i == (packet_num - 1)) && packet_remainder) {
            packet_length = packet_remainder;
        }

        ret = fts_test_i2c_read(NULL, 0, &readbuf[offset], packet_length);
        if (ret < 0) {
            FTS_TEST_ERROR("read buffer fail");
            return ret;
        }
    }

    return 0;
}

int fts_test_write(u8 addr, u8 *writebuf, int writelen)
{
    int ret = 0;
    int i = 0;
    u8 data[BYTES_PER_TIME + 1] = { 0 };
    int packet_length = 0;
    int packet_num = 0;
    int packet_remainder = 0;
    int offset = 0;
    int byte_num = writelen;

    packet_num = byte_num / BYTES_PER_TIME;
    packet_remainder = byte_num % BYTES_PER_TIME;
    if (packet_remainder)
        packet_num++;

    if (byte_num < BYTES_PER_TIME) {
        packet_length = byte_num;
    } else {
        packet_length = BYTES_PER_TIME;
    }
    FTS_TEST_DBG("packet num:%d, remainder:%d", packet_num, packet_remainder);

    data[0] = addr;
    for (i = 0; i < packet_num; i++) {
        if (i != 0) {
            data[0] = addr + 1;
        }
        if ((i == (packet_num - 1)) && packet_remainder) {
            packet_length = packet_remainder;
        }
        memcpy(&data[1], &writebuf[offset], packet_length);

        ret = fts_test_i2c_write(data, packet_length + 1);
        if (ret < 0) {
            FTS_TEST_ERROR("write buffer fail");
            return ret;
        }

        offset += packet_length;
    }

    return 0;
}

/********************************************************************
 * test global function enter work/factory mode
 *******************************************************************/
int enter_work_mode(void)
{
    int ret = 0;
    u8 mode = 0;
    int i = 0;
    int j = 0;

    FTS_TEST_FUNC_ENTER();

    ret = fts_test_read_reg(DEVIDE_MODE_ADDR, &mode);
    if ((ret >= 0) && (0x00 == mode))
        return 0;

    for (i = 0; i < ENTER_WORK_FACTORY_RETRIES; i++) {
        ret = fts_test_write_reg(DEVIDE_MODE_ADDR, 0x00);
        if (ret >= 0) {
            sys_delay(FACTORY_TEST_DELAY);
            for (j = 0; j < 20; j++) {
                ret = fts_test_read_reg(DEVIDE_MODE_ADDR, &mode);
                if ((ret >= 0) && (0x00 == mode)) {
                    FTS_TEST_INFO("enter work mode success");
                    return 0;
                } else
                    sys_delay(FACTORY_TEST_DELAY);
            }
        }

        sys_delay(50);
    }

    if (i >= ENTER_WORK_FACTORY_RETRIES) {
        FTS_TEST_ERROR("Enter work mode fail");
        return -EIO;
    }

    FTS_TEST_FUNC_EXIT();
    return 0;
}

int enter_factory_mode(void)
{
    int ret = 0;
    u8 mode = 0;
    int i = 0;
    int j = 0;

    ret = fts_test_read_reg(DEVIDE_MODE_ADDR, &mode);
    if ((ret >= 0) && (0x40 == mode))
        return 0;

    for (i = 0; i < ENTER_WORK_FACTORY_RETRIES; i++) {
        ret = fts_test_write_reg(DEVIDE_MODE_ADDR, 0x40);
        if (ret >= 0) {
            sys_delay(FACTORY_TEST_DELAY);
            for (j = 0; j < 20; j++) {
                ret = fts_test_read_reg(DEVIDE_MODE_ADDR, &mode);
                if ((ret >= 0) && (0x40 == mode)) {
                    FTS_TEST_INFO("enter factory mode success");
                    sys_delay(200);
                    return 0;
                } else
                    sys_delay(FACTORY_TEST_DELAY);
            }
        }

        sys_delay(50);
    }

    if (i >= ENTER_WORK_FACTORY_RETRIES) {
        FTS_TEST_ERROR("Enter factory mode fail");
        return -EIO;
    }

    return 0;
}

/*
 * read_mass_data - read rawdata/short test data
 * addr - register addr which read data from
 * byte_num - read data length, unit:byte
 * buf - save data
 *
 * return 0 if read data succuss, otherwise return error code
 */
int read_mass_data(u8 addr, int byte_num, int *buf)
{
    int ret = 0;
    int i = 0;
    u8 *data = NULL;

    data = (u8 *)fts_malloc(byte_num * sizeof(u8));
    if (NULL == data) {
        FTS_TEST_SAVE_ERR("mass data buffer malloc fail\n");
        return -ENOMEM;
    }

    /* read rawdata buffer */
    FTS_TEST_INFO("mass data len:%d", byte_num);
    ret = fts_test_read(addr, data, byte_num);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("read mass data fail\n");
        goto read_massdata_err;
    }

    for (i = 0; i < byte_num; i = i + 2) {
        buf[i >> 1] = (int)(((int)(data[i]) << 8) + data[i + 1]);
    }

    ret = 0;
read_massdata_err:
    fts_free(data);
    return ret;
}

int short_get_adcdata_incell(u8 retval, u8 ch_num, int byte_num, int *adc_buf)
{
    int ret = 0;
    int times = 0;
    u8 short_state = 0;

    FTS_TEST_FUNC_ENTER();

    /* Start ADC sample */
    ret = fts_test_write_reg(FACTORY_REG_SHORT_TEST_EN, 0x01);
    if (ret) {
        FTS_TEST_SAVE_ERR("start short test fail\n");
        goto adc_err;
    }

    sys_delay(ch_num * FACTORY_TEST_DELAY);
    for (times = 0; times < FACTORY_TEST_RETRY; times++) {
        ret = fts_test_read_reg(FACTORY_REG_SHORT_TEST_STATE, &short_state);
        if ((ret >= 0) && (retval == short_state))
            break;
        else
            FTS_TEST_DBG("reg%x=%x,retry:%d",
                         FACTORY_REG_SHORT_TEST_STATE, short_state, times);

        sys_delay(FACTORY_TEST_RETRY_DELAY);
    }
    if (times >= FACTORY_TEST_RETRY) {
        FTS_TEST_SAVE_ERR("short test timeout, ADC data not OK\n");
        ret = -EIO;
        goto adc_err;
    }

    ret = read_mass_data(FACTORY_REG_SHORT_ADDR, byte_num, adc_buf);
    if (ret) {
        FTS_TEST_SAVE_ERR("get short(adc) data fail\n");
    }

adc_err:
    FTS_TEST_FUNC_EXIT();
    return ret;
}

/*
 * wait_state_update - wait fw status update
 */
int wait_state_update(u8 retval)
{
    int ret = 0;
    int times = 0;
    u8 state = 0xFF;

    while (times++ < FACTORY_TEST_RETRY) {
        sys_delay(FACTORY_TEST_DELAY);
        /* Wait register status update */
        state = 0xFF;
        ret = fts_test_read_reg(FACTORY_REG_PARAM_UPDATE_STATE, &state);
        if ((ret >= 0) && (retval == state))
            break;
        else
            FTS_TEST_DBG("reg%x=%x,retry:%d", \
                         FACTORY_REG_PARAM_UPDATE_STATE, state, times);
    }

    if (times >= FACTORY_TEST_RETRY) {
        FTS_TEST_SAVE_ERR("Wait State Update fail\n");
        return -EIO;
    }

    return 0;
}

/*
 * start_scan - start to scan a frame
 */
int start_scan(void)
{
    int ret = 0;
    u8 addr = 0;
    u8 val = 0;
    u8 finish_val = 0;
    int times = 0;
    struct fts_test *tdata = fts_ftest;

    if ((NULL == tdata) || (NULL == tdata->func) ) {
        FTS_TEST_ERROR("test/func is null\n");
        return -EINVAL;
    }

    if (SCAN_SC == tdata->func->startscan_mode) {
        /* sc ic */
        addr = FACTORY_REG_SCAN_ADDR2;
        val = 0x01;
        finish_val = 0x00;
    } else {
        addr = DEVIDE_MODE_ADDR;
        val = 0xC0;
        finish_val = 0x40;
    }

    /* write register to start scan */
    ret = fts_test_write_reg(addr, val);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("write start scan mode fail\n");
        return ret;
    }

    /* Wait for the scan to complete */
    while (times++ < FACTORY_TEST_RETRY) {
        sys_delay(FACTORY_TEST_DELAY);

        ret = fts_test_read_reg(addr, &val);
        if ((ret >= 0) && (val == finish_val)) {
            break;
        } else
            FTS_TEST_DBG("reg%x=%x,retry:%d", addr, val, times);
    }

    if (times >= FACTORY_TEST_RETRY) {
        FTS_TEST_SAVE_ERR("scan timeout\n");
        return -EIO;
    }

    return 0;
}

static int read_rawdata(
    u8 off_addr,
    u8 off_val,
    u8 rawdata_addr,
    int byte_num,
    int *data)
{
    int ret = 0;

    /* set line addr or rawdata start addr */
    ret = fts_test_write_reg(off_addr, off_val);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("wirte line/start addr fail\n");
        return ret;
    }

    ret = read_mass_data(rawdata_addr, byte_num, data);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("read rawdata fail\n");
        return ret;
    }

    return 0;
}

int get_rawdata(int *data)
{
    int ret = 0;
    u8 val = 0;
    u8 addr = 0;
    u8 rawdata_addr = 0;
    int byte_num = 0;
    struct fts_test *tdata = fts_ftest;

    if ((NULL == tdata) || (NULL == tdata->func) ) {
        FTS_TEST_ERROR("test/func is null\n");
        return -EINVAL;
    }

    /* enter factory mode */
    ret = enter_factory_mode();
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("failed to enter factory mode,ret=%d\n", ret);
        return ret;
    }

    /* start scanning */
    ret = start_scan();
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("scan fail\n");
        return ret;
    }

    /* read rawdata */
    if (IC_HW_INCELL == tdata->func->hwtype) {
        val = 0xAD;
        addr = FACTORY_REG_LINE_ADDR;
        rawdata_addr = FACTORY_REG_RAWDATA_ADDR;
    } else if (IC_HW_MC_SC == tdata->func->hwtype) {
        val = 0xAA;
        addr = FACTORY_REG_LINE_ADDR;
        rawdata_addr = FACTORY_REG_RAWDATA_ADDR_MC_SC;
    } else {
        val = 0x0;
        addr = FACTORY_REG_RAWDATA_SADDR_SC;
        rawdata_addr = FACTORY_REG_RAWDATA_ADDR_SC;
    }

    byte_num = tdata->node.node_num * 2;
    ret = read_rawdata(addr, val, rawdata_addr, byte_num, data);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("read rawdata fail\n");
        return ret;
    }

    return 0;
}

/*
 * chip_clb - auto clb
 */
int chip_clb(void)
{
    int ret = 0;
    u8 val = 0;
    int times = 0;

    /* start clb */
    ret = fts_test_write_reg(FACTORY_REG_CLB, 0x04);
    if (ret) {
        FTS_TEST_SAVE_ERR("write start clb fail\n");
        return ret;
    }

    while (times++ < FACTORY_TEST_RETRY) {
        sys_delay(FACTORY_TEST_RETRY_DELAY);
        ret = fts_test_read_reg(FACTORY_REG_CLB, &val);
        if ((0 == ret) && (0x02 == val)) {
            /* clb ok */
            break;
        } else
            FTS_TEST_DBG("reg%x=%x,retry:%d", FACTORY_REG_CLB, val, times);
    }

    if (times >= FACTORY_TEST_RETRY) {
        FTS_TEST_SAVE_ERR("chip clb timeout\n");
        return -EIO;
    }

    return 0;
}

/*
 * get_cb_incell - get cb data for incell IC
 */
int get_cb_incell(u16 saddr, int byte_num, int *cb_buf)
{
    int ret = 0;
    int i = 0;
    u8 cb_addr = 0;
    u8 addr_h = 0;
    u8 addr_l = 0;
    int read_num = 0;
    int packet_num = 0;
    int packet_remainder = 0;
    int offset = 0;
    int addr = 0;
    u8 *data = NULL;

    data = (u8 *)fts_malloc(byte_num * sizeof(u8));
    if (NULL == data) {
        FTS_TEST_SAVE_ERR("cb buffer malloc fail\n");
        return -ENOMEM;
    }

    packet_num = byte_num / BYTES_PER_TIME;
    packet_remainder = byte_num % BYTES_PER_TIME;
    if (packet_remainder)
        packet_num++;
    read_num = BYTES_PER_TIME;

    FTS_TEST_INFO("cb packet:%d,remainder:%d", packet_num, packet_remainder);
    cb_addr = FACTORY_REG_CB_ADDR;
    for (i = 0; i < packet_num; i++) {
        offset = read_num * i;
        addr = saddr + offset;
        addr_h = (addr >> 8) & 0xFF;
        addr_l = addr & 0xFF;
        if ((i == (packet_num - 1)) && packet_remainder) {
            read_num = packet_remainder;
        }

        ret = fts_test_write_reg(FACTORY_REG_CB_ADDR_H, addr_h);
        if (ret) {
            FTS_TEST_SAVE_ERR("write cb addr high fail\n");
            goto TEST_CB_ERR;
        }
        ret = fts_test_write_reg(FACTORY_REG_CB_ADDR_L, addr_l);
        if (ret) {
            FTS_TEST_SAVE_ERR("write cb addr low fail\n");
            goto TEST_CB_ERR;
        }

        ret = fts_test_read(cb_addr, data + offset, read_num);
        if (ret) {
            FTS_TEST_SAVE_ERR("read cb fail\n");
            goto TEST_CB_ERR;
        }
    }

    for (i = 0; i < byte_num; i++) {
        cb_buf[i] = data[i];
    }

TEST_CB_ERR:
    fts_free(data);
    return ret;
}

int get_cb_sc(int byte_num, int *cb_buf, enum byte_mode mode)
{
    int ret = 0;
    int i = 0;
    int read_num = 0;
    int packet_num = 0;
    int packet_remainder = 0;
    int offset = 0;
    u8 cb_addr = 0;
    u8 off_addr = 0;
    struct fts_test *tdata = fts_ftest;
    u8 *cb = NULL;

    if ((NULL == tdata) || (NULL == tdata->func) ) {
        FTS_TEST_ERROR("test/func is null\n");
        return -EINVAL;
    }

    cb = (u8 *)fts_malloc(byte_num * sizeof(u8));
    if (NULL == cb) {
        FTS_TEST_SAVE_ERR("malloc memory for cb buffer fail\n");
        return -ENOMEM;
    }

    if (IC_HW_MC_SC == tdata->func->hwtype) {
        cb_addr = FACTORY_REG_MC_SC_CB_ADDR;
        off_addr = FACTORY_REG_MC_SC_CB_ADDR_OFF;
    } else if (IC_HW_SC == tdata->func->hwtype) {
        cb_addr = FACTORY_REG_SC_CB_ADDR;
        off_addr = FACTORY_REG_SC_CB_ADDR_OFF;
    }

    packet_num = byte_num / BYTES_PER_TIME;
    packet_remainder = byte_num % BYTES_PER_TIME;
    if (packet_remainder)
        packet_num++;
    read_num = BYTES_PER_TIME;
    offset = 0;

    FTS_TEST_INFO("cb packet:%d,remainder:%d", packet_num, packet_remainder);
    for (i = 0; i < packet_num; i++) {
        if ((i == (packet_num - 1)) && packet_remainder) {
            read_num = packet_remainder;
        }

        ret = fts_test_write_reg(off_addr, offset);
        if (ret < 0) {
            FTS_TEST_SAVE_ERR("write cb addr offset fail\n");
            goto cb_err;
        }

        ret = fts_test_read(cb_addr, cb + offset, read_num);
        if (ret < 0) {
            FTS_TEST_SAVE_ERR("read cb fail\n");
            goto cb_err;
        }

        offset += read_num;
    }

    if (DATA_ONE_BYTE == mode) {
        for (i = 0; i < byte_num; i++) {
            cb_buf[i] = cb[i];
        }
    } else if (DATA_TWO_BYTE == mode) {
        for (i = 0; i < byte_num; i = i + 2) {
            cb_buf[i >> 1] = (int)(((int)(cb[i]) << 8) + cb[i + 1]);
        }
    }

    ret = 0;
cb_err:
    fts_free(cb);
    return ret;
}

bool compare_data(int *data, int min, int max, int min_vk, int max_vk, bool key)
{
    int i = 0;
    bool result = true;
    struct fts_test *tdata = fts_ftest;
    int rx = tdata->node.rx_num;
    int node_va = tdata->node.node_num - tdata->node.key_num;

    if (!data || !tdata->node_valid) {
        FTS_TEST_SAVE_ERR("data/node_valid is null\n");
        return false;
    }

    for (i = 0; i < node_va; i++) {
        if (0 == tdata->node_valid[i])
            continue;

        if ((data[i] < min) || (data[i] > max)) {
            FTS_TEST_SAVE_ERR("test fail,node(%4d,%4d)=%5d,range=(%5d,%5d)\n",
                              i / rx + 1, i % rx + 1, data[i], min, max);
            result = false;
        }
    }

    if (key) {
        for (i = node_va; i < tdata->node.node_num; i++) {
            if (0 == tdata->node_valid[i])
                continue;

            if ((data[i] < min_vk) || (data[i] > max_vk)) {
                FTS_TEST_SAVE_ERR("test fail,node(%4d,%4d)=%5d,range=(%5d,%5d)\n",
                                  i / rx + 1, i % rx + 1,
                                  data[i], min_vk, max_vk);
                result = false;
            }
        }
    }

    return result;
}

bool compare_array(int *data, int *min, int *max, bool key)
{
    int i = 0;
    bool result = true;
    struct fts_test *tdata = fts_ftest;
    int rx = tdata->node.rx_num;
    int node_num = tdata->node.node_num;

    if (!data || !min || !max || !tdata->node_valid) {
        FTS_TEST_SAVE_ERR("data/min/max/node_valid is null\n");
        return false;
    }

    if (!key) {
        node_num -= tdata->node.key_num;
    }
    for (i = 0; i < node_num; i++) {
        if (0 == tdata->node_valid[i])
            continue;

        if ((data[i] < min[i]) || (data[i] > max[i])) {
            FTS_TEST_SAVE_ERR("test fail,node(%4d,%4d)=%5d,range=(%5d,%5d)\n",
                              i / rx + 1, i % rx + 1, data[i], min[i], max[i]);
            result = false;
        }
    }

    return result;
}

/*
 * show_data - show and save test data to testresult.txt
 */
void show_data(int *data, bool key)
{
    int i = 0;
    int j = 0;
    struct fts_test *tdata = fts_ftest;
    int node_num = tdata->node.node_num;
    int tx_num = tdata->node.tx_num;
    int rx_num = tdata->node.rx_num;

    FTS_TEST_FUNC_ENTER();
    for (i = 0; i < tx_num; i++) {
        FTS_TEST_SAVE_INFO("Ch/Tx_%02d:  ", i + 1);
        for (j = 0; j < rx_num; j++) {
            FTS_TEST_SAVE_INFO("%5d, ", data[i * rx_num + j]);
        }
        FTS_TEST_SAVE_INFO("\n");
    }

    if (key) {
        FTS_TEST_SAVE_INFO("Ch/Tx_%02d:  ", tx_num + 1);
        for (i = tx_num * rx_num; i < node_num; i++) {
            FTS_TEST_SAVE_INFO("%5d, ",  data[i]);
        }
        FTS_TEST_SAVE_INFO("\n");
    }
    FTS_TEST_FUNC_EXIT();
}

/*
 * save_testdata_incell - save data to testdata.csv
 */
void save_data_csv(int *data, char *name, u8 code, bool mc_sc, bool key)
{
#if CSV_SUPPORT
    int i = 0;
    int tx = 0;
    int rx = 0;
    int csv_node_num = 0;
    struct fts_test *tdata = fts_ftest;
    struct fts_test_node *node = NULL;
    struct csv_format *csv = &tdata->csv;

    FTS_TEST_FUNC_ENTER();
    if (!csv || !csv->line2_buffer || !csv->data_buffer) {
        FTS_TEST_ERROR("csv buffer is null");
        return;
    }

    if (mc_sc) {
        node = &tdata->sc_node;
        tx = 2;
    } else {
        node = &tdata->node;
        tx = node->tx_num;
    }
    if (key) {
        tx++;
    }
    rx = node->rx_num;
    csv_node_num = tx * rx;

    /* line 2 */
    csv->line2_len += snprintf(csv->line2_buffer + csv->line2_len, \
                               CSV_LINE2_BUFFER_LEN - csv->line2_len,
                               "%s, %d, %d, %d, %d, %d, ", \
                               name, code, tx, rx,
                               csv->start_line, csv->item_count);

    if (csv->line2_len >= CSV_LINE2_BUFFER_LEN - 1) {
        FTS_TEST_ERROR("csv line2 buffer length(%d) fail", csv->line2_len);
    }
    csv->start_line += tx;
    csv->item_count++;

    /* test data */
    for (i = 0; i < csv_node_num; i++) {
        if (((i + 1) % rx) == 0) {
            csv->data_len += snprintf(csv->data_buffer + csv->data_len, \
                                      CSV_DATA_BUFFER_LEN - csv->data_len, \
                                      "%d, \n", data[i]);
        } else {
            csv->data_len += snprintf(csv->data_buffer + csv->data_len, \
                                      CSV_DATA_BUFFER_LEN - csv->data_len, \
                                      "%d, ", data[i]);
        }

        if (csv->data_len >= CSV_DATA_BUFFER_LEN - 1) {
            FTS_TEST_ERROR("csv data buffer length(%d) fail", csv->data_len);
        }
    }


    FTS_TEST_FUNC_EXIT();
#endif
}


/* mc_sc only */
/* Only V3 Pattern has mapping & no-mapping */
int mapping_switch(u8 mapping)
{
    int ret = 0;
    u8 val = 0xFF;
    struct fts_test *tdata = fts_ftest;

    if (tdata->v3_pattern) {
        ret = fts_test_read_reg(FACTORY_REG_NOMAPPING, &val);
        if (ret < 0) {
            FTS_TEST_ERROR("read 0x54 register fail");
            return ret;
        }

        if (val != mapping) {
            ret = fts_test_write_reg(FACTORY_REG_NOMAPPING, mapping);
            if (ret < 0) {
                FTS_TEST_ERROR("write 0x54 register fail");
                return ret;
            }
            sys_delay(FACTORY_TEST_DELAY);
        }
    }

    return 0;
}

bool get_fw_wp(u8 wp_ch_sel, enum wp_type water_proof_type)
{
    bool fw_wp_state = false;

    switch (water_proof_type) {
    case WATER_PROOF_ON:
        /* bit5: 0-check in wp on, 1-not check */
        fw_wp_state = !(wp_ch_sel & 0x20);
        break;
    case WATER_PROOF_ON_TX:
        /* Bit6:  0-check Rx+Tx in wp mode  1-check one channel
           Bit2:  0-check Tx in wp mode;  1-check Rx in wp mode
        */
        fw_wp_state = (!(wp_ch_sel & 0x40) || !(wp_ch_sel & 0x04));
        break;
    case WATER_PROOF_ON_RX:
        fw_wp_state = (!(wp_ch_sel & 0x40) || (wp_ch_sel & 0x04));
        break;
    case WATER_PROOF_OFF:
        /* bit7: 0-check in wp off, 1-not check */
        fw_wp_state = !(wp_ch_sel & 0x80);
        break;
    case WATER_PROOF_OFF_TX:
        /* Bit1-0:  00-check Tx in non-wp mode
                    01-check Rx in non-wp mode
                    10:check Rx+Tx in non-wp mode
        */
        fw_wp_state = ((0x0 == (wp_ch_sel & 0x03)) || (0x02 == (wp_ch_sel & 0x03)));
        break;
    case WATER_PROOF_OFF_RX:
        fw_wp_state = ((0x01 == (wp_ch_sel & 0x03)) || (0x02 == (wp_ch_sel & 0x03)));
        break;
    default:
        break;
    }

    return fw_wp_state;
}

int get_cb_mc_sc(u8 wp, int byte_num, int *cb_buf, enum byte_mode mode)
{
    int ret = 0;

    /* 1:waterproof 0:non-waterproof */
    ret = fts_test_write_reg(FACTORY_REG_MC_SC_MODE, wp);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("get mc_sc mode fail\n");
        return ret;
    }

    /* read cb */
    ret = get_cb_sc(byte_num, cb_buf, mode);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("get sc cb fail\n");
        return ret;
    }

    return 0;
}

int get_rawdata_mc_sc(enum wp_type wp, int *data)
{
    int ret = 0;
    u8 val = 0;
    u8 addr = 0;
    u8 rawdata_addr = 0;
    int byte_num = 0;
    struct fts_test *tdata = fts_ftest;

    if ((NULL == tdata) || (NULL == tdata->func) ) {
        FTS_TEST_ERROR("test/func is null\n");
        return -EINVAL;
    }

    addr = FACTORY_REG_LINE_ADDR;
    rawdata_addr = FACTORY_REG_RAWDATA_ADDR_MC_SC;
    if (WATER_PROOF_ON == wp) {
        val = 0xAC;
    } else {
        val = 0xAB;
    }

    byte_num = tdata->sc_node.node_num * 2;
    ret = read_rawdata(addr, val, rawdata_addr, byte_num, data);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("read rawdata fail\n");
        return ret;
    }

    return 0;
}

int get_rawdata_mc(u8 fre, u8 fir, int *rawdata)
{
    int ret = 0;
    int i = 0;

    if (NULL == rawdata ) {
        FTS_TEST_SAVE_ERR("rawdata buffer is null\n");
        return -EINVAL;
    }

    /* set frequecy high/low */
    ret = fts_test_write_reg(FACTORY_REG_FRE_LIST, fre);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("set frequecy fail,ret=%d\n", ret);
        return ret;
    }

    /* fir enable/disable */
    ret = fts_test_write_reg(FACTORY_REG_FIR, 1);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("set fir fail,ret=%d\n", fir);
        return ret;
    }

    /* get rawdata */
    for (i = 0; i < 3; i++) {
        /* lost 3 frames, in order to obtain stable data */
        ret = get_rawdata(rawdata);
    }
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("get rawdata fail,ret=%d\n", ret);
        return ret;
    }

    return 0;
}

void short_print_mc(int *r, int num)
{
    int i = 0;

    for (i = 0; i < num; i++) {
        printk("%4d ", r[i]);
    }

    printk("\n");
}

int short_get_adc_data_mc(u8 retval, int byte_num, int *adc_buf, u8 mode)
{
    int ret = 0;
    int i = 0;
    u8 short_state = 0;

    FTS_TEST_FUNC_ENTER();
    /* select short test mode & start test */
    ret = fts_test_write_reg(FACTROY_REG_SHORT_TEST_EN, mode);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("write short test mode fail\n");
        goto test_err;
    }

    for (i = 0; i < FACTORY_TEST_RETRY; i++) {
        sys_delay(FACTORY_TEST_RETRY_DELAY);

        ret = fts_test_read_reg(FACTROY_REG_SHORT_TEST_EN, &short_state);
        if ((ret >= 0) && (retval == short_state))
            break;
        else
            FTS_TEST_DBG("reg%x=%x,retry:%d",
                         FACTROY_REG_SHORT_TEST_EN, short_state, i);
    }
    if (i >= FACTORY_TEST_RETRY) {
        FTS_TEST_SAVE_ERR("short test timeout, ADC data not OK\n");
        ret = -EIO;
        goto test_err;
    }

    ret = read_mass_data(FACTORY_REG_SHORT_ADDR_MC, byte_num, adc_buf);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("get short(adc) data fail\n");
    }

    FTS_TEST_DBG("adc data:\n");
    short_print_mc(adc_buf, byte_num / 2);
test_err:
    FTS_TEST_FUNC_EXIT();
    return ret;
}

bool compare_mc_sc(bool tx_check, bool rx_check, int *data, int *min, int *max)
{
    int i = 0;
    bool result = true;
    struct fts_test *tdata = fts_ftest;

    if (rx_check) {
        for (i = 0; i < tdata->sc_node.rx_num; i++) {
            if (0 == tdata->node_valid_sc[i])
                continue;

            if ((data[i] < min[i]) || (data[i] > max[i])) {
                FTS_TEST_SAVE_ERR("test fail,rx%d=%5d,range=(%5d,%5d)\n",
                                  i + 1, data[i], min[i], max[i]);
                result = false;
            }
        }
    }

    if (tx_check) {
        for (i = tdata->sc_node.rx_num; i < tdata->sc_node.node_num; i++) {
            if (0 == tdata->node_valid_sc[i])
                continue;

            if ((data[i] < min[i]) || (data[i] > max[i])) {
                FTS_TEST_SAVE_INFO("test fail,tx%d=%5d,range=(%5d,%5d)\n",
                                   i - tdata->sc_node.rx_num + 1,
                                   data[i], min[i], max[i]);
                result = false;
            }
        }
    }

    return result;
}

void show_data_mc_sc(int *data)
{
    int i = 0;
    struct fts_test *tdata = fts_ftest;

    FTS_TEST_SAVE_INFO("SCap Rx: ");
    for (i = 0; i < tdata->sc_node.rx_num; i++) {
        FTS_TEST_SAVE_INFO( "%5d, ", data[i]);
    }
    FTS_TEST_SAVE_INFO("\n");

    FTS_TEST_SAVE_INFO("SCap Tx: ");
    for (i = tdata->sc_node.rx_num; i < tdata->sc_node.node_num; i++) {
        FTS_TEST_SAVE_INFO( "%5d, ", data[i]);
    }
    FTS_TEST_SAVE_INFO("\n");
}
/* mc_sc end*/

/*
 * fts_test_save_test_data - Save test data to SD card etc.
 */
static int fts_test_save_test_data(char *file_name, char *data_buf, int len)
{
    struct file *pfile = NULL;
    char filepath[128];
    loff_t pos;
    mm_segment_t old_fs;

    FTS_TEST_FUNC_ENTER();
    memset(filepath, 0, sizeof(filepath));
    sprintf(filepath, "%s%s", FTS_INI_FILE_PATH, file_name);
    if (NULL == pfile) {
        pfile = filp_open(filepath, O_TRUNC | O_CREAT | O_RDWR, 0);
    }
    if (IS_ERR(pfile)) {
        FTS_TEST_ERROR("error occured while opening file %s.",  filepath);
        return -EIO;
    }

    old_fs = get_fs();
    set_fs(KERNEL_DS);
    pos = 0;
    vfs_write(pfile, data_buf, len, &pos);
    filp_close(pfile, NULL);
    set_fs(old_fs);

    FTS_TEST_FUNC_EXIT();
    return 0;
}

static int fts_test_malloc_free_data_csv(struct fts_test *tdata, bool allocate)
{
#if CSV_SUPPORT
    struct csv_format *csv = &tdata->csv;

    if (true == allocate) {
        csv->buffer = vmalloc(CSV_BUFFER_LEN);
        if (NULL == csv->buffer) {
            FTS_TEST_ERROR("csv->buffer malloc fail\n");
            return -ENOMEM;
        }
        csv->line2_buffer = vmalloc(CSV_LINE2_BUFFER_LEN);
        if (NULL == csv->line2_buffer) {
            FTS_TEST_ERROR("csv->line2_buffer malloc fail\n");
            return -ENOMEM;
        }
        csv->data_buffer = vmalloc(CSV_DATA_BUFFER_LEN);
        if (NULL == csv->data_buffer) {
            FTS_TEST_ERROR("csv->data_buffer malloc fail\n");
            return -ENOMEM;
        }

        /* initialize variable */
        csv->length = 0;
        csv->line2_len = 0;
        csv->data_len = 0;
        csv->start_line = 11;
        csv->item_count = 1;

    } else {
        if (csv->buffer) {
            vfree(csv->buffer);
            csv->buffer = NULL;
        }
        if (csv->line2_buffer) {
            vfree(csv->line2_buffer);
            csv->line2_buffer = NULL;
        }
        if (csv->data_buffer) {
            vfree(csv->data_buffer);
            csv->data_buffer = NULL;
        }
    }
#endif

    return 0;
}

static int fts_test_malloc_free_data_txt(struct fts_test *tdata, bool allocate)
{
    if (true == allocate) {
        tdata->testresult = vmalloc(TXT_BUFFER_LEN);
        if (NULL == tdata->testresult) {
            FTS_TEST_ERROR("tdata->testresult malloc fail\n");
            return -ENOMEM;
        }

        tdata->testresult_len = 0;
        FTS_TEST_SAVE_INFO("FW version:0x%02x\n", tdata->fw_ver);
        FTS_TEST_SAVE_INFO("tx_num:%d, rx_num:%d\n",
                           tdata->node.tx_num, tdata->node.rx_num);
    } else {
        if (tdata->testresult) {
            vfree(tdata->testresult);
            tdata->testresult = NULL;
        }
    }

    return 0;
}

static void fts_test_save_data_csv(struct fts_test *tdata)
{
#if CSV_SUPPORT
    struct csv_format *csv = &tdata->csv;

    if (!csv || !csv->buffer || !csv->line2_buffer || !csv->data_buffer) {
        FTS_TEST_ERROR("csv buffer is null");
        return;
    }

    /* line 1 */
    csv->length += snprintf(csv->buffer + csv->length, \
                            CSV_BUFFER_LEN - csv->length, \
                            "ECC, 85, 170, IC Name, %s, IC Code, %x\n", \
                            tdata->ini.ic_name, \
                            (tdata->ini.ic_code >> IC_CODE_OFFSET));

    /* line 2 */
    csv->length += snprintf(csv->buffer + csv->length, \
                            CSV_BUFFER_LEN - csv->length, \
                            "TestItem Num, %d, ", \
                            csv->item_count);
    if (csv->line2_len > 0) {
        memcpy(csv->buffer + csv->length, csv->line2_buffer, csv->line2_len);
        csv->length += csv->line2_len;
    }

    /* line 3 ~ 10  "\n" */
    csv->length += snprintf(csv->buffer + csv->length, \
                            CSV_BUFFER_LEN - csv->length, \
                            "\n\n\n\n\n\n\n\n\n");

    /* line 11 ~ data area */
    if (csv->data_len > 0) {
        memcpy(csv->buffer + csv->length, csv->data_buffer, csv->data_len);
        csv->length += csv->data_len;
    }

    FTS_TEST_INFO("csv length:%d", csv->length);
    fts_test_save_test_data(FTS_CSV_FILE_NAME, csv->buffer, csv->length);
#endif
}

static void fts_test_save_result_txt(struct fts_test *tdata)
{
    if (!tdata || !tdata->testresult) {
        FTS_TEST_ERROR("test result is null");
        return;
    }

    FTS_TEST_INFO("test result length in txt:%d", tdata->testresult_len);
    fts_test_save_test_data(FTS_TXT_FILE_NAME, tdata->testresult, tdata->testresult_len);
}

static int fts_test_malloc_free_incell(struct fts_test *tdata, bool allocate)
{
    struct incell_threshold *thr = &tdata->ic.incell.thr;
    int buflen = tdata->node.node_num * sizeof(int);

    if (true == allocate) {
        FTS_TEST_INFO("buflen:%d", buflen);
        fts_malloc_r(thr->rawdata_min, buflen);
        fts_malloc_r(thr->rawdata_max, buflen);
        if (tdata->func->rawdata2_support) {
            fts_malloc_r(thr->rawdata2_min, buflen);
            fts_malloc_r(thr->rawdata2_max, buflen);
        }
        fts_malloc_r(thr->cb_min, buflen);
        fts_malloc_r(thr->cb_max, buflen);
    } else {
        fts_free(thr->rawdata_min);
        fts_free(thr->rawdata_max);
        if (tdata->func->rawdata2_support) {
            fts_free(thr->rawdata2_min);
            fts_free(thr->rawdata2_max);
        }
        fts_free(thr->cb_min);
        fts_free(thr->cb_max);
    }

    return 0;
}

static int fts_test_malloc_free_mc_sc(struct fts_test *tdata, bool allocate)
{
    struct mc_sc_threshold *thr = &tdata->ic.mc_sc.thr;
    int buflen = tdata->node.node_num * sizeof(int);
    int buflen_sc = tdata->sc_node.node_num * sizeof(int);

    if (true == allocate) {
        fts_malloc_r(thr->rawdata_h_min, buflen);
        fts_malloc_r(thr->rawdata_h_max, buflen);
        if (tdata->func->rawdata2_support) {
            fts_malloc_r(thr->rawdata_l_min, buflen);
            fts_malloc_r(thr->rawdata_l_max, buflen);
        }
        fts_malloc_r(thr->tx_linearity_max, buflen);
        fts_malloc_r(thr->tx_linearity_min, buflen);
        fts_malloc_r(thr->rx_linearity_max, buflen);
        fts_malloc_r(thr->rx_linearity_min, buflen);

        fts_malloc_r(thr->scap_cb_off_min, buflen_sc);
        fts_malloc_r(thr->scap_cb_off_max, buflen_sc);
        fts_malloc_r(thr->scap_cb_on_min, buflen_sc);
        fts_malloc_r(thr->scap_cb_on_max, buflen_sc);

        fts_malloc_r(thr->scap_rawdata_off_min, buflen_sc);
        fts_malloc_r(thr->scap_rawdata_off_max, buflen_sc);
        fts_malloc_r(thr->scap_rawdata_on_min, buflen_sc);
        fts_malloc_r(thr->scap_rawdata_on_max, buflen_sc);

        fts_malloc_r(thr->panel_differ_min, buflen);
        fts_malloc_r(thr->panel_differ_max, buflen);
    } else {
        fts_free(thr->rawdata_h_min);
        fts_free(thr->rawdata_h_max);
        if (tdata->func->rawdata2_support) {
            fts_free(thr->rawdata_l_min);
            fts_free(thr->rawdata_l_max);
        }
        fts_free(thr->tx_linearity_max);
        fts_free(thr->tx_linearity_min);
        fts_free(thr->rx_linearity_max);
        fts_free(thr->rx_linearity_min);

        fts_free(thr->scap_cb_off_min);
        fts_free(thr->scap_cb_off_max);
        fts_free(thr->scap_cb_on_min);
        fts_free(thr->scap_cb_on_max);

        fts_free(thr->scap_rawdata_off_min);
        fts_free(thr->scap_rawdata_off_max);
        fts_free(thr->scap_rawdata_on_min);
        fts_free(thr->scap_rawdata_on_max);

        fts_free(thr->panel_differ_min);
        fts_free(thr->panel_differ_max);
    }

    return 0;
}

static int fts_test_malloc_free_sc(struct fts_test *tdata, bool allocate)
{
    struct sc_threshold *thr = &tdata->ic.sc.thr;
    int buflen = tdata->node.node_num * sizeof(int);

    if (true == allocate) {
        fts_malloc_r(thr->rawdata_min, buflen);
        fts_malloc_r(thr->rawdata_max, buflen);
        fts_malloc_r(thr->cb_min, buflen);
        fts_malloc_r(thr->cb_max, buflen);
        fts_malloc_r(thr->dcb_sort, buflen);
        fts_malloc_r(thr->dcb_base, buflen);
    } else {
        fts_free(thr->rawdata_min);
        fts_free(thr->rawdata_max);
        fts_free(thr->cb_min);
        fts_free(thr->cb_max);
        fts_free(thr->dcb_sort);
        fts_free(thr->dcb_base);
    }

    return 0;
}

static int fts_test_malloc_free_thr(struct fts_test *tdata, bool allocate)
{
    int ret = 0;

    if ((NULL == tdata) || (NULL == tdata->func)) {
        FTS_TEST_SAVE_ERR("tdata/func is NULL\n");
        return -EINVAL;
    }

    if (true == allocate) {
        fts_malloc_r(tdata->node_valid, tdata->node.node_num * sizeof(int));
        fts_malloc_r(tdata->node_valid_sc, tdata->sc_node.node_num * sizeof(int));
    } else {
        fts_free(tdata->node_valid);
        fts_free(tdata->node_valid_sc);
    }

    switch (tdata->func->hwtype) {
    case IC_HW_INCELL:
        ret = fts_test_malloc_free_incell(tdata, allocate);
        break;
    case IC_HW_MC_SC:
        ret = fts_test_malloc_free_mc_sc(tdata, allocate);
        break;
    case IC_HW_SC:
        ret = fts_test_malloc_free_sc(tdata, allocate);
        break;
    default:
        FTS_TEST_SAVE_ERR("test ic type(%d) fail\n", tdata->func->hwtype);
        ret = -EINVAL;
        break;
    }

    return ret;
}

/* default enable all test item */
static void fts_test_init_item(struct fts_test *tdata)
{
    switch (tdata->func->hwtype) {
    case IC_HW_INCELL:
        tdata->ic.incell.u.tmp = 0xFFFFFFFF;
        break;
    case IC_HW_MC_SC:
        tdata->ic.mc_sc.u.tmp = 0xFFFFFFFF;
        break;
    case IC_HW_SC:
        tdata->ic.sc.u.tmp = 0xFFFFFFFF;
        break;
    }
}

static int get_tx_rx_num(u8 tx_rx_reg, u8 *ch_num, u8 ch_num_max)
{
    int ret = 0;
    int i = 0;

    for (i = 0; i < 3; i++) {
        ret = fts_test_read_reg(tx_rx_reg, ch_num);
        if ((ret < 0) || (*ch_num > ch_num_max)) {
            sys_delay(50);
        } else
            break;
    }

    if (i >= 3) {
        FTS_TEST_ERROR("get channel num fail");
        return -EIO;
    }

    return 0;
}

static int get_channel_num(struct fts_test *tdata)
{
    int ret = 0;
    u8 tx_num = 0;
    u8 rx_num = 0;
    int key_num = 0;

    /* node structure */
    if (IC_HW_SC == tdata->func->hwtype) {
        ret = get_tx_rx_num(FACTORY_REG_CH_NUM_SC, &tx_num, NUM_MAX_SC);
        if (ret < 0) {
            FTS_TEST_ERROR("get channel number fail");
            return ret;
        }

        ret = get_tx_rx_num(FACTORY_REG_KEY_NUM_SC, &rx_num, KEY_NUM_MAX);
        if (ret < 0) {
            FTS_TEST_ERROR("get key number fail");
            return ret;
        }

        tdata->node.tx_num = 2;
        tdata->node.rx_num = tx_num / 2;
        tdata->node.channel_num = tx_num;
        tdata->node.node_num = tx_num;
        key_num = rx_num;
    } else {
        ret = get_tx_rx_num(FACTORY_REG_CHX_NUM, &tx_num, TX_NUM_MAX);
        if (ret < 0) {
            FTS_TEST_ERROR("get tx_num fail");
            return ret;
        }

        ret = get_tx_rx_num(FACTORY_REG_CHY_NUM, &rx_num, RX_NUM_MAX);
        if (ret < 0) {
            FTS_TEST_ERROR("get rx_num fail");
            return ret;
        }

        tdata->node.tx_num = tx_num;
        tdata->node.rx_num = rx_num;
        if (IC_HW_INCELL == tdata->func->hwtype)
            tdata->node.channel_num = tx_num * rx_num;
        else if (IC_HW_MC_SC == tdata->func->hwtype)
            tdata->node.channel_num = tx_num + rx_num;
        tdata->node.node_num = tx_num * rx_num;
        key_num = tdata->func->key_num_total;
    }

    /* key */
    tdata->node.key_num = key_num;
    tdata->node.node_num += tdata->node.key_num;

    /* sc node structure */
    tdata->sc_node = tdata->node;
    if (IC_HW_MC_SC == tdata->func->hwtype) {
        if (tdata->v3_pattern) {
            ret = get_tx_rx_num(FACTORY_REG_CHX_NUM_NOMAP, &tx_num, TX_NUM_MAX);
            if (ret < 0) {
                FTS_TEST_ERROR("get no-mappint tx_num fail");
                return ret;
            }

            ret = get_tx_rx_num(FACTORY_REG_CHY_NUM_NOMAP, &rx_num, TX_NUM_MAX);
            if (ret < 0) {
                FTS_TEST_ERROR("get no-mapping rx_num fail");
                return ret;
            }

            tdata->sc_node.tx_num = tx_num;
            tdata->sc_node.rx_num = rx_num;
        }
        tdata->sc_node.channel_num = tx_num + rx_num;
        tdata->sc_node.node_num = tx_num + rx_num;
    }

    if (tdata->node.tx_num > TX_NUM_MAX) {
        FTS_TEST_ERROR("tx num(%d) fail", tdata->node.tx_num);
        return -EIO;
    }

    if (tdata->node.rx_num > RX_NUM_MAX) {
        FTS_TEST_ERROR("rx num(%d) fail", tdata->node.rx_num);
        return -EIO;
    }

    FTS_TEST_INFO("node_num:%d, tx:%d, rx:%d", tdata->node.node_num,
                  tdata->node.tx_num, tdata->node.rx_num);
    return 0;
}

static void get_ic_version(struct fts_test *tdata)
{
    u8 val[4] = { 0 };

    fts_test_read_reg(REG_FW_CHIP_IDH, &val[0]);
    fts_test_read_reg(REG_FW_CHIP_IDL, &val[1]);
    fts_test_read_reg(REG_FW_IC_TYPE, &val[2]);
    fts_test_read_reg(REG_FW_IC_VERSION, &val[3]);

    tdata->ic_ver = TEST_IC_VERSION(val[0], val[1], val[2], val[3]);
    FTS_TEST_INFO("test ic version:0x%8x", tdata->ic_ver);
}

static int fts_test_init_basicinfo(struct fts_test *tdata)
{
    int ret = 0;
    u8 val = 0;

    if ((NULL == tdata) || (NULL == tdata->func)) {
        FTS_TEST_SAVE_ERR("tdata/func is NULL\n");
        return -EINVAL;
    }

    get_ic_version(tdata);

    fts_test_read_reg(REG_FW_VERSION, &val);
    tdata->fw_ver = val;

    if (IC_HW_INCELL == tdata->func->hwtype) {
        fts_test_read_reg(REG_VA_TOUCH_THR, &val);
        tdata->va_touch_thr = val;
        fts_test_read_reg(REG_VKEY_TOUCH_THR, &val);
        tdata->vk_touch_thr = val;
    }

    /* enter factory mode */
    ret = enter_factory_mode();
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("enter factory mode fail\n");
        return ret;
    }

    if (IC_HW_MC_SC == tdata->func->hwtype) {
        fts_test_read_reg(FACTORY_REG_PATTERN, &val);
        tdata->v3_pattern = (1 == val) ? true : false;
        fts_test_read_reg(FACTORY_REG_NOMAPPING, &val);
        tdata->mapping = val;
    }

    /* enter into factory mode and read tx/rx num */
    ret = get_channel_num(tdata);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("get channel number fail\n");
        return ret;
    }

    return ret;
}

static int fts_test_main_init(void)
{
    int ret = 0;
    struct fts_test *tdata = fts_ftest;

    FTS_TEST_FUNC_ENTER();
    /* get basic information: tx/rx num ... */
    ret = fts_test_init_basicinfo(tdata);
    if (ret < 0) {
        FTS_TEST_ERROR("test init basicinfo fail");
        return ret;
    }

    /* allocate memory for test threshold */
    ret = fts_test_malloc_free_thr(tdata, true);
    if (ret < 0) {
        FTS_TEST_ERROR("test malloc for threshold fail");
        return ret;
    }

    /* default enable all test item */
    fts_test_init_item(tdata);

    /* allocate memory for test data:csv&txt */
    ret = fts_test_malloc_free_data_csv(tdata, true);
    if (ret < 0) {
        FTS_TEST_ERROR("allocate memory for test data(csv) fail");
        return ret;
    }

    ret = fts_test_malloc_free_data_txt(tdata, true);
    if (ret < 0) {
        FTS_TEST_ERROR("allocate memory for test data(txt) fail");
        return ret;
    }

    /* allocate test data buffer */
    tdata->buffer_length = (tdata->node.tx_num + 1) * tdata->node.rx_num;
    tdata->buffer_length *= sizeof(int);
    FTS_TEST_INFO("test buffer length:%d", tdata->buffer_length);
    tdata->buffer = (int *)fts_malloc(tdata->buffer_length);
    if (NULL == tdata->buffer) {
        FTS_TEST_ERROR("test buffer(%d) malloc fail", tdata->buffer_length);
        return -ENOMEM;
    }
    memset(tdata->buffer, 0, tdata->buffer_length);

    FTS_TEST_FUNC_EXIT();
    return ret;
}

static int fts_test_main_exit(void)
{
    struct fts_test *tdata = fts_ftest;

    FTS_TEST_FUNC_ENTER();
    fts_test_save_data_csv(tdata);
    fts_test_save_result_txt(tdata);

    /* free memory */
    fts_test_malloc_free_data_txt(tdata, false);
    fts_test_malloc_free_data_csv(tdata, false);
    fts_test_malloc_free_thr(tdata, false);
    /*free test data buffer*/
    fts_free(tdata->buffer);

    FTS_TEST_FUNC_EXIT();
    return 0;
}


/*
 * fts_test_get_testparams - get test parameter from ini
 */
static int fts_test_get_testparams(char *config_name)
{
    int ret = 0;

    ret = fts_test_get_testparam_from_ini(config_name);

    return ret;
}

static int fts_test_start(void)
{
    int testresult = 0;
    struct fts_test *tdata = fts_ftest;

    if (tdata && tdata->func && tdata->func->start_test) {
        testresult = tdata->func->start_test();
    } else {
        FTS_TEST_ERROR("test func/start_test func is null");
    }

    return testresult;
}

/*
 * fts_test_entry - test main entry
 *
 * warning - need disable irq & esdcheck before call this function
 *
 */
static int fts_test_entry(char *ini_file_name)
{
    int ret = 0;

    /* test initialize */
    ret = fts_test_main_init();
    if (ret < 0) {
        FTS_TEST_ERROR("fts_test_main_init fail");
        goto test_err;
    }

    /*Read parse configuration file*/
    FTS_TEST_SAVE_INFO("ini_file_name:%s\n", ini_file_name);
    ret = fts_test_get_testparams(ini_file_name);
    if (ret < 0) {
        FTS_TEST_ERROR("get testparam fail");
        goto test_err;
    }

    /* Start testing according to the test configuration */
    /* luoguojin ???? */
    if (true == fts_test_start()) {
        FTS_TEST_SAVE_INFO("\n\n=======Tp test pass.\n");
    } else {
        FTS_TEST_SAVE_INFO("\n\n=======Tp test failure.\n");
    }

    ret = 0;
test_err:
    fts_test_main_exit();
    enter_work_mode();
    return ret;
}

/************************************************************************
* Name: fts_test_show
* Brief:  no
* Input: device, device attribute, char buf
* Output: no
* Return: EPERM
***********************************************************************/
static ssize_t fts_test_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return -EPERM;
}

/************************************************************************
* Name: fts_test_store
* Brief:  upgrade from app.bin
* Input: device, device attribute, char buf, char count
* Output: no
* Return: char count
***********************************************************************/
static ssize_t fts_test_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    char fwname[128] = {0};
    struct fts_ts_data *ts_data = fts_data;
    struct input_dev *input_dev;

    if (ts_data->suspended) {
        FTS_INFO("In suspend, no test, return now");
        return -EINVAL;
    }

    input_dev = ts_data->input_dev;
    memset(fwname, 0, sizeof(fwname));
    sprintf(fwname, "%s", buf);
    fwname[count - 1] = '\0';
    FTS_TEST_DBG("fwname:%s.", fwname);

    mutex_lock(&input_dev->mutex);
    disable_irq(ts_data->irq);

#if defined(FTS_ESDCHECK_EN) && (FTS_ESDCHECK_EN)
    fts_esdcheck_switch(DISABLE);
#endif

    fts_test_entry(fwname);

#if defined(FTS_ESDCHECK_EN) && (FTS_ESDCHECK_EN)
    fts_esdcheck_switch(ENABLE);
#endif

    enable_irq(ts_data->irq);
    mutex_unlock(&input_dev->mutex);

    return count;
}

/*  test from test.ini
*    example:echo "***.ini" > fts_test
*/
static DEVICE_ATTR(fts_test, S_IRUGO | S_IWUSR, fts_test_show, fts_test_store);

/* add your attr in here*/
static struct attribute *fts_test_attributes[] = {
    &dev_attr_fts_test.attr,
    NULL
};

static struct attribute_group fts_test_attribute_group = {
    .attrs = fts_test_attributes
};

static int fts_test_func_init(void)
{
    int i = 0;
    int j = 0;
    int ic_stype = fts_data->ic_info.ids.type;
    struct test_funcs *func = NULL;
    int func_count = sizeof(test_func_list) / sizeof(test_func_list[0]);

    FTS_TEST_INFO("init test function");
    if (0 == func_count) {
        FTS_TEST_SAVE_ERR("test functions list is NULL, fail\n");
        return -ENODATA;
    }

    fts_ftest = (struct fts_test *)kzalloc(sizeof(*fts_ftest), GFP_KERNEL);
    if (NULL == fts_ftest) {
        FTS_TEST_ERROR("malloc memory for test fail");
        return -ENOMEM;
    }

    for (i = 0; i < func_count; i++) {
        func = test_func_list[i];
        for (j = 0; j < FTX_MAX_COMPATIBLE_TYPE; j++) {
            if (0 == func->ctype[j])
                break;
            else if (func->ctype[j] == ic_stype) {
                FTS_TEST_INFO("match test function,type:%x", (int)func->ctype[j]);
                fts_ftest->func = func;
            }
        }
    }
    if (NULL == fts_ftest->func) {
        FTS_TEST_ERROR("no test function match, can't test");
        return -ENODATA;
    }

    return 0;
}

int fts_test_init(struct i2c_client *client)
{
    int ret = 0;

    FTS_TEST_FUNC_ENTER();

    /* get test function, must be the first step */
    ret = fts_test_func_init();
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("test functions init fail\n");
        return ret;
    }

    ret = sysfs_create_group(&client->dev.kobj, &fts_test_attribute_group);
    if (0 != ret) {
        FTS_TEST_ERROR( "[focal] %s() - ERROR: sysfs_create_group() failed.",  __func__);
        sysfs_remove_group(&client->dev.kobj, &fts_test_attribute_group);
    } else {
        FTS_TEST_DBG("[focal] %s() - sysfs_create_group() succeeded.", __func__);
    }

    FTS_TEST_FUNC_EXIT();

    return ret;
}

int fts_test_exit(struct i2c_client *client)
{
    FTS_TEST_FUNC_ENTER();

    sysfs_remove_group(&client->dev.kobj, &fts_test_attribute_group);
    fts_free(fts_ftest);
    FTS_TEST_FUNC_EXIT();
    return 0;
}
