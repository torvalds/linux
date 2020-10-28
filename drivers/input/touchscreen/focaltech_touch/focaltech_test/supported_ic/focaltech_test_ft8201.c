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

/*******************************************************************************
* Included header files
*******************************************************************************/
#include "../focaltech_test.h"

/*******************************************************************************
* Private constant and macro definitions using #define
*******************************************************************************/
#define REG_MS_SELECT           0x26
#define REG_CH_X_MASTER         0x50
#define REG_CH_Y_MASTER         0x51
#define REG_CH_X_SLAVE          0x52
#define REG_CH_Y_SLAVE          0x53
#define REG_FW_INFO_CNT         0x17
#define I2C_ADDR_M              0
#define I2C_ADDR_S              12
#define REG_FW_INFO_ADDR        0x81
#define REG_FW_INFO_LEN         32
#define MAX_ADC_VALUE                   4015
#define FACTORY_NOISE_MODE_REG          0x5E

/*******************************************************************************
* Private enumerations, structures and unions using typedef
*******************************************************************************/
enum M_S_TYPE {
    CHIP_AS_SLAVE = 0,
    CHIP_AS_MASTER = 1,
    SINGLE_CHIP = 3,
};

enum CASCADE_DIRECTION {
    CASCADE_LEFT_RIGHT = 0,
    CASCADE_UP_DOWN    = 1,
};

/*
 * m_s_sel    - master/slave information
 * m_i2c_addr - master ic I2C address
 * s_i2c_addr - slave ic I2C address
 * m_tx       - master IC tx number
 * m_rx       - master IC rx number
 * s_tx       - slave IC tx number
 * s_rx       - slave IC rx number
 */
struct ft8201_info {
    union m_s_sel {
        struct bits {
            u8 type         : 6;
            u8 direction    : 1;
            u8 s0_as_slave  : 1;
        } bits;
        u8 byte_val;
    } m_s_sel;
    u8 m_i2c_addr;
    u8 s_i2c_addr;
    u8 m_tx;
    u8 m_rx;
    u8 s_tx;
    u8 s_rx;
    u8  current_slave_addr;
};

/*******************************************************************************
* Static function prototypes
*******************************************************************************/
static void fts_array_copy(int *dest, const int *src, int len)
{
    int i = 0;

    for (i = 0; i < len; i++) {
        dest[i] = src[i];
    }
}

static void work_as_master(struct ft8201_info *info)
{
    if (fts_data->client->addr != info->m_i2c_addr) {
        FTS_TEST_DBG("change i2c addr to master(0x%x)\n", info->m_i2c_addr);
        fts_data->client->addr = info->m_i2c_addr;
    }
}

static void work_as_slave(struct ft8201_info *info)
{
    if (fts_data->client->addr != info->s_i2c_addr) {
        FTS_TEST_DBG("change i2c addr to slave(0x%x)\n", info->s_i2c_addr);
        fts_data->client->addr = info->s_i2c_addr;
    }
}

static int ft8201_write_reg(struct ft8201_info *info, u8 reg_addr, u8 reg_val)
{
    int ret = 0;

    /* write master reg */
    work_as_master(info);
    ret = fts_test_write_reg(reg_addr, reg_val);
    if (ret) {
        FTS_TEST_SAVE_ERR("write master reg fail\n");
        return ret;
    }

    /* write slave reg */
    work_as_slave(info);
    ret = fts_test_write_reg(reg_addr, reg_val);
    if (ret) {
        FTS_TEST_SAVE_ERR("write slave reg fail\n");
        work_as_master(info);
        return ret;
    }
    work_as_master(info);

    return 0;
}

static void integrate_data(struct ft8201_info *info, int *m_buf, int *s_buf, int *data)
{
    int i = 0;
    int *s0_buf;
    int *s1_buf;
    int s0_ch = 0;
    int s0_tx = 0;
    int s0_rx = 0;
    int s1_ch = 0;
    int s1_rx = 0;
    int row = 0;
    int s0_row = 0;
    int s1_row = 0;

    FTS_TEST_FUNC_ENTER();

    if (false == info->m_s_sel.bits.s0_as_slave) {
        s0_buf = m_buf;
        s0_tx = info->m_tx;
        s0_rx = info->m_rx;
        s0_ch = info->m_tx * info->m_rx;
        s1_buf = s_buf;
        s1_rx = info->s_rx;
        s1_ch = info->s_tx * info->s_rx;
    } else {
        s0_buf = s_buf;
        s0_tx = info->s_tx;
        s0_rx = info->s_rx;
        s0_ch = info->s_tx * info->s_rx;
        s1_buf = m_buf;
        s1_rx = info->m_rx;
        s1_ch = info->m_tx * info->m_rx;
    }

    FTS_TEST_DBG("%d %d %d %d %d", s0_tx, s0_rx, s0_ch, s1_rx, s1_ch);
    if (CASCADE_LEFT_RIGHT == info->m_s_sel.bits.direction) {
        /* cascade direction : left to right */
        for (i = 0; i < s0_tx; i++) {
            row = i * (s0_rx + s1_rx);
            s0_row = i * s0_rx;
            s1_row = i * s1_rx;

            fts_array_copy(data + row, s0_buf + s0_row, s0_rx);
            fts_array_copy(data + row + s0_rx, s1_buf + s1_row, s1_rx);
        }

    } else {
        /* cascade direction : up to down */
        fts_array_copy(data, s0_buf, s0_ch);
        fts_array_copy(data + s0_ch, s1_buf, s1_ch);
    }

    /* key */
    fts_array_copy(data + s0_ch + s1_ch, s0_buf + s0_ch, 6);
    fts_array_copy(data + s0_ch + s1_ch + 6, s1_buf + s1_ch, 6);

    FTS_TEST_FUNC_EXIT();
}

static int check_ic_info_validity(struct ft8201_info *info)
{
    /* IC type */
    if ((info->m_s_sel.bits.type != CHIP_AS_SLAVE)
        && (info->m_s_sel.bits.type != CHIP_AS_MASTER)) {
        FTS_TEST_SAVE_ERR("IC cascade type(%d) fail\n", info->m_s_sel.bits.type);
        return -EINVAL;
    }

    /* I2C addr */
    if ((0 == info->m_i2c_addr) || (0 == info->s_i2c_addr)) {
        FTS_TEST_SAVE_ERR("i2c addr of master(0x%x)/slave(0x%x) fail\n",
                          info->m_i2c_addr, info->s_i2c_addr);
        return -EINVAL;
    }

    /* tx/rx */
    if ((0 == info->m_tx) || (info->m_tx > TX_NUM_MAX)) {
        FTS_TEST_SAVE_ERR("master tx(%d) fail\n", info->m_tx);
        return -EINVAL;
    }

    if ((0 == info->m_rx) || (info->m_rx > TX_NUM_MAX)) {
        FTS_TEST_SAVE_ERR("master rx(%d) fail\n", info->m_rx);
        return -EINVAL;
    }

    if ((0 == info->s_tx) || (info->s_tx > TX_NUM_MAX)) {
        FTS_TEST_SAVE_ERR("slave tx(%d) fail\n", info->s_tx);
        return -EINVAL;
    }

    if ((0 == info->s_rx) || (info->s_rx > TX_NUM_MAX)) {
        FTS_TEST_SAVE_ERR("slave rx(%d) fail\n", info->s_rx);
        return -EINVAL;
    }

    return 0;
}

static int get_chip_information(struct ft8201_info *info)
{
    int ret = 0;
    u8 value[REG_FW_INFO_LEN] = { 0 };
    u8 cmd = 0;

    ret = fts_test_read_reg(REG_MS_SELECT, &value[0]);
    if (ret) {
        FTS_TEST_SAVE_ERR("read m/s select info fail\n");
        return ret;
    }
    info->m_s_sel.byte_val = value[0];

    ret = fts_test_read_reg(REG_CH_X_MASTER, &value[0]);
    if (ret) {
        FTS_TEST_SAVE_ERR("read ch_x_m fail\n");
        return ret;
    }
    info->m_tx = value[0];

    ret = fts_test_read_reg(REG_CH_Y_MASTER, &value[0]);
    if (ret) {
        FTS_TEST_SAVE_ERR("read ch_y_m fail\n");
        return ret;
    }
    info->m_rx = value[0];

    ret = fts_test_read_reg(REG_CH_X_SLAVE, &value[0]);
    if (ret) {
        FTS_TEST_SAVE_ERR("read ch_x_s fail\n");
        return ret;
    }
    info->s_tx = value[0];

    ret = fts_test_read_reg(REG_CH_Y_SLAVE, &value[0]);
    if (ret) {
        FTS_TEST_SAVE_ERR("read ch_y_s fail\n");
        return ret;
    }
    info->s_rx = value[0];

    ret = fts_test_write_reg(REG_FW_INFO_CNT, 0);
    if (ret) {
        FTS_TEST_SAVE_ERR("write fw into cnt fail\n");
        return ret;
    }
    cmd = REG_FW_INFO_ADDR;
    ret = fts_test_read(cmd, &value[0], REG_FW_INFO_LEN);
    if (ret) {
        FTS_TEST_SAVE_ERR("read fw info fail\n");
        return ret;
    }

    if ((value[I2C_ADDR_M] + value[I2C_ADDR_M + 1]) == 0xFF) {
        info->m_i2c_addr = value[I2C_ADDR_M] >> 1;
    }

    if ((value[I2C_ADDR_S] + value[I2C_ADDR_S + 1]) == 0xFF) {
        info->s_i2c_addr = value[I2C_ADDR_S] >> 1;
    }

    FTS_TEST_DBG("%s=%d,%s=%d,%s=%d,%s=0x%x,%s=0x%x,%s=%d,%s=%d,%s=%d,%s=%d\n",
                 "type", info->m_s_sel.bits.type,
                 "direction", info->m_s_sel.bits.direction,
                 "s0_as_slave", info->m_s_sel.bits.s0_as_slave,
                 "m_i2c_addr", info->m_i2c_addr,
                 "s_i2c_addr", info->s_i2c_addr,
                 "m_tx", info->m_tx,
                 "m_rx", info->m_rx,
                 "s_tx", info->s_tx,
                 "s_rx", info->s_rx
                );

    ret = check_ic_info_validity(info);
    if (ret) {
        FTS_TEST_SAVE_ERR("ic information invalid\n");
        return ret;
    }

    return 0;
}

static int ft8201_test_init(struct ft8201_info *info)
{
    int ret = 0;

    /* initialize info */
    memset(info, 0, sizeof(struct ft8201_info));

    /* enter factory mode */
    ret = enter_factory_mode();
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("enter factory mode fail, can't get tx/rx num\n");
        return ret;
    }

    /* get chip info */
    ret = get_chip_information(info);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("get chip information fail\n");
        return ret;
    }

    return 0;
}

static u8 ft8201_chip_clb(struct ft8201_info *info)
{
    int ret = 0;

    FTS_TEST_FUNC_ENTER();
    /* master clb */
    work_as_master(info);
    ret = chip_clb();
    if (ret) {
        FTS_TEST_SAVE_ERR("master clb fail\n");
        return ret;
    }

    /* slave clb */
    work_as_slave(info);
    ret = chip_clb();
    if (ret) {
        FTS_TEST_SAVE_ERR("master clb fail\n");
        work_as_master(info);
        return ret;
    }
    work_as_master(info);

    FTS_TEST_FUNC_EXIT();
    return 0;
}

static int ft8201_get_tx_rx_cb(struct ft8201_info *info, u8 start_node, int read_num, int *read_buffer)
{
    int ret = 0;
    int *buffer_master = NULL;
    int *buffer_slave = NULL;
    int master_tx = info->m_tx;
    int master_rx = info->m_rx;
    int slave_tx = info->s_tx;
    int slave_rx = info->s_rx;

    FTS_TEST_FUNC_ENTER();

    buffer_master = fts_malloc((master_tx + 1) * master_rx * sizeof(int));
    if (NULL == buffer_master) {
        FTS_TEST_SAVE_ERR("%s:master buf malloc fail\n", __func__);
        ret = -ENOMEM;
        goto GET_CB_ERR;
    }

    buffer_slave = fts_malloc((slave_tx + 1) * slave_rx * sizeof(int));
    if (NULL == buffer_slave) {
        FTS_TEST_SAVE_ERR("%s:slave buf malloc fail\n", __func__);
        ret = -ENOMEM;
        goto GET_CB_ERR;
    }

    /* master cb */
    work_as_master(info);
    ret = get_cb_incell(0, master_tx * master_rx  + 6, buffer_master);
    if (ret ) {
        FTS_TEST_SAVE_ERR("master clb fail\n");
        goto GET_CB_ERR;
    }

    /* slave cb */
    work_as_slave(info);
    ret = get_cb_incell(0, slave_tx * slave_rx + 6, buffer_slave);
    if (ret ) {
        FTS_TEST_SAVE_ERR("slave clb fail\n");
        work_as_master(info);
        goto GET_CB_ERR;
    }
    work_as_master(info);

    integrate_data(info, buffer_master, buffer_slave, read_buffer);

GET_CB_ERR:
    fts_free(buffer_master);
    fts_free(buffer_slave);

    FTS_TEST_FUNC_EXIT();
    return ret;
}

static int read_adc_data(u8 retval, int byte_num, int *adc_buf)
{
    int ret = 0;
    int times = 0;
    u8 short_state = 0;

    FTS_TEST_FUNC_ENTER();

    for (times = 0; times < FACTORY_TEST_RETRY; times++) {
        ret = fts_test_read_reg(FACTORY_REG_SHORT_TEST_STATE, &short_state);
        if ((0 == ret) && (retval == short_state))
            break;
        else
            FTS_TEST_DBG("reg%x=%x,retry:%d",
                         FACTORY_REG_SHORT_TEST_STATE, short_state, times);

        sys_delay(FACTORY_TEST_RETRY_DELAY);
    }
    if (times >= FACTORY_TEST_RETRY) {
        FTS_TEST_SAVE_ERR("short test timeout, ADC data not OK\n");
        ret = -EIO;
        goto ADC_ERROR;
    }

    ret = read_mass_data(FACTORY_REG_SHORT_ADDR, byte_num, adc_buf);
    if (ret) {
        FTS_TEST_SAVE_ERR("get short(adc) data fail\n");
    }

ADC_ERROR:
    FTS_TEST_FUNC_EXIT();
    return ret;
}

static u8 ft8201_weakshort_get_adcdata(struct ft8201_info *info, int *rbuf)
{
    int ret = 0;
    int master_adc_num = 0;
    int slave_adc_num = 0;
    int *buffer_master = NULL;
    int *buffer_slave = NULL;
    int master_tx = info->m_tx;
    int master_rx = info->m_rx;
    int slave_tx = info->s_tx;
    int slave_rx = info->s_rx;
    int ch_num = 0;

    FTS_TEST_FUNC_ENTER();

    buffer_master = fts_malloc((master_tx + 1) * master_rx * sizeof(int));
    if (NULL == buffer_master) {
        FTS_TEST_SAVE_ERR("%s:master buf malloc fail\n", __func__);
        ret = -ENOMEM;
        goto ADC_ERROR;
    }

    buffer_slave = fts_malloc((slave_tx + 1) * slave_rx * sizeof(int));
    if (NULL == buffer_slave) {
        FTS_TEST_SAVE_ERR("%s:slave buf malloc fail\n", __func__);
        ret = -ENOMEM;
        goto ADC_ERROR;
    }

    /* Start ADC sample */
    ch_num = master_tx + master_rx;
    ret = fts_test_write_reg(FACTORY_REG_SHORT_TEST_EN, 0x01);
    if (ret) {
        FTS_TEST_SAVE_ERR("start short test fail\n");
        goto ADC_ERROR;
    }
    sys_delay(ch_num * FACTORY_TEST_DELAY);

    /* read master adc data */
    master_adc_num = (master_tx * master_rx + 6) * 2;
    work_as_master(info);
    ret = read_adc_data(TEST_RETVAL_00, master_adc_num, buffer_master);
    if (ret) {
        FTS_TEST_SAVE_ERR("read master adc data fail\n");
        goto ADC_ERROR;
    }

    /* read slave adc data */
    slave_adc_num = (slave_tx * slave_rx + 6) * 2;
    work_as_slave(info);
    ret = read_adc_data(TEST_RETVAL_00, slave_adc_num, buffer_slave);
    if (ret) {
        FTS_TEST_SAVE_ERR("read master adc data fail\n");
        work_as_master(info);
        goto ADC_ERROR;
    }
    work_as_master(info);

    /* data integration */
    integrate_data(info, buffer_master, buffer_slave, rbuf);

ADC_ERROR:
    fts_free(buffer_master);
    fts_free(buffer_slave);

    FTS_TEST_FUNC_EXIT();
    return ret;
}

static int ft8201_short_test(struct ft8201_info *info, struct fts_test *tdata, bool *test_result)
{
    int ret = 0;
    bool tmp_result = true;
    int *adcdata = NULL;
    int tmp_adc = 0;
    int i = 0;
    struct incell_threshold *thr = &tdata->ic.incell.thr;

    FTS_TEST_FUNC_ENTER();
    FTS_TEST_SAVE_INFO("\n============ Test Item: short test\n");
    memset(tdata->buffer, 0, tdata->buffer_length);
    adcdata = tdata->buffer;

    ret = enter_factory_mode();
    if (ret) {
        FTS_TEST_SAVE_ERR("//Failed to Enter factory mode.ret=%d\n", ret);
        goto test_err;
    }

    ret = ft8201_weakshort_get_adcdata(info, adcdata);
    if (ret) {
        FTS_TEST_SAVE_ERR("//Failed to get AdcData. ret=%d\n", ret);
        goto test_err;
    }

    /* change adc to resistance */
    for (i = 0; i < tdata->node.node_num; ++i) {
        tmp_adc = adcdata[i];
        /* avoid calculating the value of the resistance is too large, limiting the size of the ADC value */
        if (tmp_adc > MAX_ADC_VALUE)
            tmp_adc = MAX_ADC_VALUE;
        adcdata[i] = (tmp_adc * 100) / (4095 - tmp_adc);
    }

    /* save */
    show_data(adcdata, true);
    save_data_csv(adcdata, "Short Circuit Test", \
                  CODE_SHORT_TEST, false, true);

    /* compare */
    tmp_result = compare_data(adcdata, thr->basic.short_res_min, TEST_SHORT_RES_MAX, thr->basic.short_res_vk_min, TEST_SHORT_RES_MAX, true);

    ret = 0;
test_err:
    if (tmp_result) {
        *test_result = true;
        FTS_TEST_SAVE_INFO("\n------ Short Circuit Test PASS\n");
    } else {
        *test_result = false;
        FTS_TEST_SAVE_INFO("\n------ Short Circuit Test NG\n");
    }
    FTS_TEST_FUNC_EXIT();
    return ret;
}

static int ft8201_open_test(struct ft8201_info *info, struct fts_test *tdata, bool *test_result)
{
    int ret = 0;
    bool tmp_result = false;
    u8 reg20_val = 0;
    u8 reg86_val = 0;
    u8 tmp_val = 0;
    int min = 0;
    int max = 0;
    int *opendata = NULL;
    int byte_num = 0;
    struct incell_threshold *thr = &tdata->ic.incell.thr;


    FTS_TEST_FUNC_ENTER();
    FTS_TEST_SAVE_INFO("\n============ Test Item: Open Test\n");
    memset(tdata->buffer, 0, tdata->buffer_length);
    opendata = tdata->buffer;

    ret = enter_factory_mode();
    if (ret) {
        FTS_TEST_SAVE_ERR("Enter Factory Failed\n");
        goto test_err;
    }

    ret = fts_test_read_reg(FACTORY_REG_OPEN_REG86, &reg86_val);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("read 0x86 fail\n");
        goto test_err;
    }

    ret = fts_test_read_reg(FACTORY_REG_OPEN_REG20, &reg20_val);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("read 0x20 fail\n");
        goto test_err;
    }


    /* set open mode */
    ret = ft8201_write_reg(info, FACTORY_REG_OPEN_REG86, 0x01);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("write 0x86 fail\n");
        goto restore_reg;
    }

    /* set Bit4~Bit5 of reg0x20 is set to 2b'10 (Source to GND) */
    tmp_val = reg20_val | (1 << 5);
    tmp_val &= ~(1 << 4);
    ret = ft8201_write_reg(info, FACTORY_REG_OPEN_REG20, tmp_val);
    if (ret) {
        FTS_TEST_SAVE_ERR("Failed to Read or Write Reg\n");
        goto restore_reg;
    }

    /* wait fw state update before clb */
    ret = wait_state_update(TEST_RETVAL_00);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("wait state update fail\n");
        goto restore_reg;
    }

    ret = ft8201_chip_clb(info);
    if (ret) {
        FTS_TEST_SAVE_ERR("auto clb fail\n");
        goto restore_reg;
    }

    /* get cb data */
    byte_num = tdata->node.tx_num * tdata->node.rx_num;
    ret = ft8201_get_tx_rx_cb(info, 0, byte_num, opendata);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("get cb fail\n");
        goto restore_reg;
    }

    /* show open data */
    show_data(opendata, false);
    /* save data */
    save_data_csv(opendata, "Open Test", \
                  CODE_OPEN_TEST, false, false);

    /* compare */
    min = thr->basic.open_cb_min;
    max = 256;
    FTS_TEST_DBG("open %d %d\n", min, opendata[0]);
    tmp_result = compare_data(opendata, min, max, 0, 0, false);

    //ret = 0;

restore_reg:
    /* restore */
    ret = ft8201_write_reg(info, FACTORY_REG_OPEN_REG86, reg86_val);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("restore reg86 fail\n");
    }

    ret = ft8201_write_reg(info, FACTORY_REG_OPEN_REG20, reg20_val);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("restore reg20 fail\n");
    }

    ret = wait_state_update(TEST_RETVAL_00);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("wait state update fail\n");
    }

    ret = ft8201_chip_clb(info);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("auto clb fail\n");
    }

test_err:
    if (tmp_result) {
        *test_result = true;
        FTS_TEST_SAVE_INFO("\n------ Open Test PASS\n");
    } else {
        *test_result = false;
        FTS_TEST_SAVE_INFO("\n------ Open Test NG\n");
    }
    FTS_TEST_FUNC_EXIT();
    return ret;
}


static int ft8201_cb_test(struct ft8201_info *info, struct fts_test *tdata, bool *test_result)
{
    bool tmp_result = false;
    int ret = 0;
    bool key_check = false;
    int byte_num = 0;
    int *cbdata = NULL;
    struct incell_threshold *thr = &tdata->ic.incell.thr;

    FTS_TEST_FUNC_ENTER();
    FTS_TEST_SAVE_INFO("\n============ Test Item: CB Test\n");
    memset(tdata->buffer, 0, tdata->buffer_length);
    cbdata = tdata->buffer;

    if (!thr->cb_min || !thr->cb_max) {
        FTS_TEST_SAVE_ERR("cb_min/max is null\n");
        ret = -EINVAL;
        goto test_err;
    }

    ret = enter_factory_mode();
    if (ret) {
        FTS_TEST_SAVE_ERR("// Failed to Enter factory mode.ret:%d\n", ret);
        goto test_err;
    }

    ret = ft8201_chip_clb(info);
    if (ret) {
        FTS_TEST_SAVE_ERR("//========= auto clb Failed\n");
        goto test_err;
    }

    byte_num = tdata->node.node_num;
    ret = ft8201_get_tx_rx_cb(info, 0, byte_num, cbdata);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("get cb fail\n");
        goto test_err;
    }

    key_check = thr->basic.cb_vkey_check;

    show_data(cbdata, key_check);
    save_data_csv(cbdata, "CB Test", \
                  CODE_CB_TEST, false, key_check);
    /* compare */
    tmp_result = compare_array(cbdata, thr->cb_min, thr->cb_max, key_check);

    ret = 0;

test_err:
    if (tmp_result) {
        *test_result = true;
        FTS_TEST_SAVE_INFO("\n------ CB Test PASS\n");
    } else {
        *test_result = false;
        FTS_TEST_SAVE_INFO("\n------ CB Test NG\n");
    }
    FTS_TEST_FUNC_EXIT();
    return ret;

}

static int ft8201_rawdata_test(struct ft8201_info *info, struct fts_test *tdata, bool *test_result)
{
    int ret = 0;
    bool tmp_result = false;
    int i = 0;
    bool key_check = true;
    int *rawdata = NULL;
    struct incell_threshold *thr = &tdata->ic.incell.thr;

    FTS_TEST_FUNC_ENTER();
    FTS_TEST_SAVE_INFO("\n============ Test Item: RawData Test\n");
    memset(tdata->buffer, 0, tdata->buffer_length);
    rawdata = tdata->buffer;

    if (!thr->rawdata_min || !thr->rawdata_max) {
        FTS_TEST_SAVE_ERR("rawdata_min/max is null\n");
        ret = -EINVAL;
        goto test_err;
    }

    ret = enter_factory_mode();
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("enter factory mode fail,ret=%d\n", ret);
        goto test_err;
    }

    /* read rawdata */
    for (i = 0 ; i < 3; i++) {
        ret = get_rawdata(rawdata);
    }
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("get RawData fail,ret=%d\n", ret);
        goto test_err;
    }

    /* save */
    show_data(rawdata, key_check);
    save_data_csv(rawdata, "RawData Test", \
                  CODE_RAWDATA_TEST, false, key_check);
    /* compare */
    tmp_result = compare_array(rawdata,
                               thr->rawdata_min,
                               thr->rawdata_max,
                               key_check);


test_err:
    if (tmp_result) {
        *test_result = true;
        FTS_TEST_SAVE_INFO("\n------ RawData Test PASS\n");
    } else {
        *test_result = false;
        FTS_TEST_SAVE_INFO("\n------ RawData Test NG\n");
    }
    FTS_TEST_FUNC_EXIT();
    return ret;

}

static int ft8201_lcdnoise_test(struct ft8201_info *info, struct fts_test *tdata, bool *test_result)
{
    int ret = 0;
    bool tmp_result = false;
    int frame_num = 0;
    int i = 0;
    int max = 0;
    int max_vk = 0;
    int byte_num  = 0;
    u8 old_mode = 0;
    u8 reg_value = 0;
    u8 status = 0;
    int *lcdnoise = NULL;
    struct incell_threshold *thr = &tdata->ic.incell.thr;

    FTS_TEST_FUNC_ENTER();
    FTS_TEST_SAVE_INFO("\n============ Test Item: LCD Noise Test\n");
    memset(tdata->buffer, 0, tdata->buffer_length);
    lcdnoise = tdata->buffer;

    ret = enter_factory_mode();
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("enter factory mode fail,ret=%d\n", ret);
        goto test_err;
    }


    ret = fts_test_read_reg(FACTORY_REG_DATA_SELECT, &old_mode);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("read reg06 fail\n");
        goto test_err;
    }

    ret = fts_test_read_reg(FACTORY_NOISE_MODE_REG, &reg_value);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("read reg5e fail\n");
        goto test_err;
    }

    ret = ft8201_write_reg(info, FACTORY_REG_DATA_SELECT, 0x64);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("write 0x64 to reg5e fail\n");
        goto restore_reg;
    }

    ret = ft8201_write_reg(info, FACTORY_REG_DATA_SELECT, 0x01);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("write 1 to reg06 fail\n");
        goto restore_reg;
    }

    frame_num = thr->basic.lcdnoise_frame;
    ret = ft8201_write_reg(info, FACTORY_REG_LCD_NOISE_FRAME, frame_num & 0xff);
    if (ret < 0) {
        FTS_TEST_SAVE_INFO("write frame num fail\n");
        goto restore_reg;
    }
    ret = ft8201_write_reg(info, FACTORY_REG_LCD_NOISE_FRAME + 1, (frame_num >> 8) & 0xff);
    if (ret < 0) {
        FTS_TEST_SAVE_INFO("write frame num fail\n");
        goto restore_reg;
    }

    /* read noise data */
    ret = fts_test_write_reg(FACTORY_REG_LINE_ADDR, 0xAD);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("write 0xAD to reg01 fail\n");
        goto restore_reg;
    }

    /* start test */
    ret = ft8201_write_reg(info, FACTORY_REG_LCD_NOISE_START, 0x01);
    if (ret < 0) {
        FTS_TEST_SAVE_INFO("start lcdnoise test fail\n");
        goto restore_reg;
    }
    sys_delay(frame_num * FACTORY_TEST_DELAY / 2);
    for (i = 0; i < FACTORY_TEST_RETRY; i++) {
        status = 0xFF;
        ret = fts_test_read_reg(FACTORY_REG_LCD_NOISE_START, &status );
        if ((ret >= 0) && (0x00 == status)) {
            break;
        } else {
            FTS_TEST_DBG("reg%x=%x,retry:%d\n", FACTORY_REG_LCD_NOISE_START, status, i);
        }
        sys_delay(FACTORY_TEST_RETRY_DELAY);
    }
    if (i >= FACTORY_TEST_RETRY) {
        FTS_TEST_SAVE_ERR("lcdnoise test timeout\n");
        //ret = -ENODATA;
        goto restore_reg;
    }

    byte_num = tdata->node.node_num * 2;
    ret = read_mass_data(FACTORY_REG_RAWDATA_ADDR, byte_num, lcdnoise);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("read rawdata fail\n");
        goto restore_reg;
    }

    /* save */
    show_data(lcdnoise, true);
    save_data_csv(lcdnoise, "LCD Noise Test", \
                  CODE_LCD_NOISE_TEST, false, true);

    /* compare */
    max = thr->basic.lcdnoise_coefficient * tdata->va_touch_thr * 32 / 100;
    max_vk = thr->basic.lcdnoise_coefficient_vkey * tdata->vk_touch_thr * 32 / 100;
    tmp_result = compare_data(lcdnoise, 0, max, 0, max_vk, true);

    //ret = 0;

restore_reg:
    ret = fts_test_write_reg(FACTORY_REG_DATA_SELECT, old_mode);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("restore reg06 fail\n");
    }
    ret = fts_test_write_reg(FACTORY_NOISE_MODE_REG, reg_value);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("restore reg5e fail\n");
    }
    ret = fts_test_write_reg(FACTORY_REG_LCD_NOISE_START, 0x00);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("restore reg11 fail\n");
    }

test_err:
    if (tmp_result) {
        *test_result = true;
        FTS_TEST_SAVE_INFO("\n------ LCD Noise Test PASS\n");
    } else {
        *test_result = false;
        FTS_TEST_SAVE_INFO("\n------ LCD Noise Test NG\n");
    }
    FTS_TEST_FUNC_EXIT();
    return ret;

}

static int start_test_ft8201(void)
{
    int ret = 0;
    struct fts_test *tdata = fts_ftest;
    struct incell_testitem *test_item = &tdata->ic.incell.u.item;
    bool temp_result = false;
    bool test_result = true;
    struct ft8201_info info;

    FTS_TEST_FUNC_ENTER();
    FTS_TEST_INFO("test item:0x%x", fts_ftest->ic.incell.u.tmp);

    if (!tdata || !tdata->testresult || !tdata->buffer) {
        FTS_TEST_ERROR("tdata is null");
        return -EINVAL;
    }

    ret = ft8201_test_init(&info);
    if (ret) {
        FTS_TEST_SAVE_ERR("test init fail\n");
        return ret;
    }

    /* short test */
    if (true == test_item->short_test) {
        ret = ft8201_short_test(&info, tdata, &temp_result);
        if ((ret < 0) || (false == temp_result)) {
            test_result = false;
        }
    }

    /* open test */
    if (true == test_item->open_test) {
        ret = ft8201_open_test(&info, tdata, &temp_result);
        if ((ret < 0) || (false == temp_result)) {
            test_result = false;
        }
    }

    /* cb test */
    if (true == test_item->cb_test) {
        ret = ft8201_cb_test(&info, tdata, &temp_result);
        if ((ret < 0) || (false == temp_result)) {
            test_result = false;
        }
    }

    /* rawdata test */
    if (true == test_item->rawdata_test) {
        ret = ft8201_rawdata_test(&info, tdata, &temp_result);
        if ((ret < 0) || (false == temp_result)) {
            test_result = false;
        }
    }

    /* lcd noise test */
    if (true == test_item->lcdnoise_test) {
        ret = ft8201_lcdnoise_test(&info, tdata, &temp_result);
        if ((ret < 0) || (false == temp_result)) {
            test_result = false;
        }
    }

    return test_result;
}

struct test_funcs test_func_ft8201 = {
    .ctype = {0x10},
    .hwtype = IC_HW_INCELL,
    .key_num_total = 12,
    .start_test = start_test_ft8201,
};

