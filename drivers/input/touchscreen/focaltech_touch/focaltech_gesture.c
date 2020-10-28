/*
 *
 * FocalTech TouchScreen driver.
 *
 * Copyright (c) 2012-2018, Focaltech Ltd. All rights reserved.
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

/*****************************************************************************
*
* File Name: focaltech_gestrue.c
*
* Author: Focaltech Driver Team
*
* Created: 2016-08-08
*
* Abstract:
*
* Reference:
*
*****************************************************************************/

/*****************************************************************************
* 1.Included header files
*****************************************************************************/
#include "focaltech_core.h"
#if FTS_GESTURE_EN
/******************************************************************************
* Private constant and macro definitions using #define
*****************************************************************************/
#define KEY_GESTURE_U                           KEY_U
#define KEY_GESTURE_UP                          KEY_UP
#define KEY_GESTURE_DOWN                        KEY_DOWN
#define KEY_GESTURE_LEFT                        KEY_LEFT
#define KEY_GESTURE_RIGHT                       KEY_RIGHT
#define KEY_GESTURE_O                           KEY_O
#define KEY_GESTURE_E                           KEY_E
#define KEY_GESTURE_M                           KEY_M
#define KEY_GESTURE_L                           KEY_L
#define KEY_GESTURE_W                           KEY_W
#define KEY_GESTURE_S                           KEY_S
#define KEY_GESTURE_V                           KEY_V
#define KEY_GESTURE_C                           KEY_C
#define KEY_GESTURE_Z                           KEY_Z

#define GESTURE_LEFT                            0x20
#define GESTURE_RIGHT                           0x21
#define GESTURE_UP                              0x22
#define GESTURE_DOWN                            0x23
#define GESTURE_DOUBLECLICK                     0x24
#define GESTURE_O                               0x30
#define GESTURE_W                               0x31
#define GESTURE_M                               0x32
#define GESTURE_E                               0x33
#define GESTURE_L                               0x44
#define GESTURE_S                               0x46
#define GESTURE_V                               0x54
#define GESTURE_Z                               0x41
#define GESTURE_C                               0x34
#define FTS_GESTRUE_POINTS                      6
#define FTS_GESTRUE_POINTS_HEADER               2

/*****************************************************************************
* Private enumerations, structures and unions using typedef
*****************************************************************************/
/*
* header        -   byte0:gesture id
*                   byte1:pointnum
*                   byte2~7:reserved
* coordinate_x  -   All gesture point x coordinate
* coordinate_y  -   All gesture point y coordinate
* mode          -   1:enable gesture function(default)
*               -   0:disable
* active        -   1:enter into gesture(suspend)
*                   0:gesture disable or resume
*/
struct fts_gesture_st {
    u8 header[FTS_GESTRUE_POINTS_HEADER];
    u16 coordinate_x[FTS_GESTRUE_POINTS];
    u16 coordinate_y[FTS_GESTRUE_POINTS];
    u8 mode;   /*host driver enable gesture flag*/
    u8 active;  /*gesture actutally work*/
};

/*****************************************************************************
* Static variables
*****************************************************************************/
static struct fts_gesture_st fts_gesture_data;

/*****************************************************************************
* Global variable or extern global variabls/functions
*****************************************************************************/

/*****************************************************************************
* Static function prototypes
*****************************************************************************/
static ssize_t fts_gesture_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t fts_gesture_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);
static ssize_t fts_gesture_buf_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t fts_gesture_buf_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);

/* sysfs gesture node
 *   read example: cat  fts_gesture_mode        ---read gesture mode
 *   write example:echo 01 > fts_gesture_mode   ---write gesture mode to 01
 *
 */
static DEVICE_ATTR (fts_gesture_mode, S_IRUGO | S_IWUSR, fts_gesture_show, fts_gesture_store);
/*
 *   read example: cat fts_gesture_buf        ---read gesture buf
 */
static DEVICE_ATTR (fts_gesture_buf, S_IRUGO | S_IWUSR, fts_gesture_buf_show, fts_gesture_buf_store);
static struct attribute *fts_gesture_mode_attrs[] = {
    &dev_attr_fts_gesture_mode.attr,
    &dev_attr_fts_gesture_buf.attr,
    NULL,
};

static struct attribute_group fts_gesture_group = {
    .attrs = fts_gesture_mode_attrs,
};

/************************************************************************
* Name: fts_gesture_show
*  Brief:
*  Input: device, device attribute, char buf
* Output:
* Return:
***********************************************************************/
static ssize_t fts_gesture_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    int count;
    u8 val;
    struct input_dev *input_dev = fts_data->input_dev;
    struct i2c_client *client = container_of(dev, struct i2c_client, dev);

    mutex_lock(&input_dev->mutex);
    fts_i2c_read_reg(client, FTS_REG_GESTURE_EN, &val);
    count = snprintf(buf, PAGE_SIZE, "Gesture Mode: %s\n", fts_gesture_data.mode ? "On" : "Off");
    count += snprintf(buf + count, PAGE_SIZE, "Reg(0xD0) = %d\n", val);
    mutex_unlock(&input_dev->mutex);

    return count;
}

/************************************************************************
* Name: fts_gesture_store
*  Brief:
*  Input: device, device attribute, char buf, char count
* Output:
* Return:
***********************************************************************/
static ssize_t fts_gesture_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct input_dev *input_dev = fts_data->input_dev;
    mutex_lock(&input_dev->mutex);
    if (FTS_SYSFS_ECHO_ON(buf)) {
        FTS_INFO("[GESTURE]enable gesture");
        fts_gesture_data.mode = ENABLE;
    } else if (FTS_SYSFS_ECHO_OFF(buf)) {
        FTS_INFO("[GESTURE]disable gesture");
        fts_gesture_data.mode = DISABLE;
    }
    mutex_unlock(&input_dev->mutex);

    return count;
}
/************************************************************************
* Name: fts_gesture_buf_show
*  Brief:
*  Input: device, device attribute, char buf
* Output:
* Return:
***********************************************************************/
static ssize_t fts_gesture_buf_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    int count;
    int i = 0;
    struct input_dev *input_dev = fts_data->input_dev;

    mutex_lock(&input_dev->mutex);
    count = snprintf(buf, PAGE_SIZE, "Gesture ID: 0x%x\n", fts_gesture_data.header[0]);
    count += snprintf(buf + count, PAGE_SIZE, "Gesture PointNum: %d\n", fts_gesture_data.header[1]);
    count += snprintf(buf + count, PAGE_SIZE, "Gesture Point Buf:\n");
    for (i = 0; i < fts_gesture_data.header[1]; i++) {
        count += snprintf(buf + count, PAGE_SIZE, "%3d(%4d,%4d) ", i, fts_gesture_data.coordinate_x[i], fts_gesture_data.coordinate_y[i]);
        if ((i + 1) % 4 == 0)
            count += snprintf(buf + count, PAGE_SIZE, "\n");
    }
    count += snprintf(buf + count, PAGE_SIZE, "\n");
    mutex_unlock(&input_dev->mutex);

    return count;
}

/************************************************************************
* Name: fts_gesture_buf_store
*  Brief:
*  Input: device, device attribute, char buf, char count
* Output:
* Return:
***********************************************************************/
static ssize_t fts_gesture_buf_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    /* place holder for future use */
    return -EPERM;
}

/*****************************************************************************
*   Name: fts_create_gesture_sysfs
*  Brief:
*  Input:
* Output:
* Return: 0-success or others-error
*****************************************************************************/
int fts_create_gesture_sysfs(struct i2c_client *client)
{
    int ret = 0;

    ret = sysfs_create_group(&client->dev.kobj, &fts_gesture_group);
    if ( ret != 0) {
        FTS_ERROR( "[GESTURE]fts_gesture_mode_group(sysfs) create failed!");
        sysfs_remove_group(&client->dev.kobj, &fts_gesture_group);
        return ret;
    }
    return 0;
}

/*****************************************************************************
*   Name: fts_gesture_report
*  Brief:
*  Input:
* Output:
* Return:
*****************************************************************************/
static void fts_gesture_report(struct input_dev *input_dev, int gesture_id)
{
    int gesture;

    FTS_FUNC_ENTER();
    FTS_INFO("fts gesture_id==0x%x ", gesture_id);
    switch (gesture_id) {
    case GESTURE_LEFT:
        gesture = KEY_GESTURE_LEFT;
        break;
    case GESTURE_RIGHT:
        gesture = KEY_GESTURE_RIGHT;
        break;
    case GESTURE_UP:
        gesture = KEY_GESTURE_UP;
        break;
    case GESTURE_DOWN:
        gesture = KEY_GESTURE_DOWN;
        break;
    case GESTURE_DOUBLECLICK:
        gesture = KEY_GESTURE_U;
        break;
    case GESTURE_O:
        gesture = KEY_GESTURE_O;
        break;
    case GESTURE_W:
        gesture = KEY_GESTURE_W;
        break;
    case GESTURE_M:
        gesture = KEY_GESTURE_M;
        break;
    case GESTURE_E:
        gesture = KEY_GESTURE_E;
        break;
    case GESTURE_L:
        gesture = KEY_GESTURE_L;
        break;
    case GESTURE_S:
        gesture = KEY_GESTURE_S;
        break;
    case GESTURE_V:
        gesture = KEY_GESTURE_V;
        break;
    case GESTURE_Z:
        gesture = KEY_GESTURE_Z;
        break;
    case  GESTURE_C:
        gesture = KEY_GESTURE_C;
        break;
    default:
        gesture = -1;
        break;
    }
    /* report event key */
    if (gesture != -1) {
        FTS_DEBUG("Gesture Code=%d", gesture);
        input_report_key(input_dev, gesture, 1);
        input_sync(input_dev);
        input_report_key(input_dev, gesture, 0);
        input_sync(input_dev);
    }

    FTS_FUNC_EXIT();
}

/************************************************************************
*   Name: fts_gesture_read_buffer
*  Brief: read data from TP register
*  Input:
* Output:
* Return: fail <0
***********************************************************************/
static int fts_gesture_read_buffer(struct i2c_client *client, u8 *buf, int read_bytes)
{
    int remain_bytes;
    int ret;
    int i;

    if (read_bytes <= I2C_BUFFER_LENGTH_MAXINUM) {
        ret = fts_i2c_read(client, buf, 1, buf, read_bytes);
    } else {
        ret = fts_i2c_read(client, buf, 1, buf, I2C_BUFFER_LENGTH_MAXINUM);
        remain_bytes = read_bytes - I2C_BUFFER_LENGTH_MAXINUM;
        for (i = 1; remain_bytes > 0; i++) {
            if (remain_bytes <= I2C_BUFFER_LENGTH_MAXINUM)
                ret = fts_i2c_read(client, buf, 0, buf + I2C_BUFFER_LENGTH_MAXINUM * i, remain_bytes);
            else
                ret = fts_i2c_read(client, buf, 0, buf + I2C_BUFFER_LENGTH_MAXINUM * i, I2C_BUFFER_LENGTH_MAXINUM);
            remain_bytes -= I2C_BUFFER_LENGTH_MAXINUM;
        }
    }

    return ret;
}

/************************************************************************
*   Name: fts_gesture_readdata
*  Brief: read data from TP register
*  Input:
* Output:
* Return: return 0 if succuss, otherwise reture error code
***********************************************************************/
int fts_gesture_readdata(struct fts_ts_data *ts_data)
{
    u8 buf[FTS_GESTRUE_POINTS * 4] = { 0 };
    int ret = 0;
    int i = 0;
    int gestrue_id = 0;
    int read_bytes = 0;
    u8 pointnum;
    u8 state;
    struct i2c_client *client = ts_data->client;
    struct input_dev *input_dev = ts_data->input_dev;

    if (!ts_data->suspended) {
        return -EINVAL;
    }

    ret = fts_i2c_read_reg(client, FTS_REG_GESTURE_EN, &state);
    if ((ret < 0) || (state != ENABLE)) {
        FTS_DEBUG("gesture not enable, don't process gesture");
        return -EIO;
    }

    /* init variable before read gesture point */
    memset(fts_gesture_data.header, 0, FTS_GESTRUE_POINTS_HEADER);
    memset(fts_gesture_data.coordinate_x, 0, FTS_GESTRUE_POINTS * sizeof(u16));
    memset(fts_gesture_data.coordinate_y, 0, FTS_GESTRUE_POINTS * sizeof(u16));

    buf[0] = FTS_REG_GESTURE_OUTPUT_ADDRESS;
    ret = fts_i2c_read(client, buf, 1, buf, FTS_GESTRUE_POINTS_HEADER);
    if (ret < 0) {
        FTS_ERROR("[GESTURE]Read gesture header data failed!!");
        FTS_FUNC_EXIT();
        return ret;
    }

    memcpy(fts_gesture_data.header, buf, FTS_GESTRUE_POINTS_HEADER);
    gestrue_id = buf[0];
    pointnum = buf[1];
    read_bytes = ((int)pointnum) * 4 + FTS_GESTRUE_POINTS_HEADER;
    buf[0] = FTS_REG_GESTURE_OUTPUT_ADDRESS;
    FTS_DEBUG("[GESTURE]PointNum=%d", pointnum);
    if (pointnum > FTS_GESTRUE_POINTS) {
        FTS_ERROR("[GESTURE]gesture point number(%d) fail");
        return -EIO;
    }

    ret = fts_gesture_read_buffer(client, buf, read_bytes);
    if (ret < 0) {
        FTS_ERROR("[GESTURE]Read gesture touch data failed!!");
        FTS_FUNC_EXIT();
        return ret;
    }

    fts_gesture_report(input_dev, gestrue_id);
    for (i = 0; i < pointnum; i++) {
        fts_gesture_data.coordinate_x[i] = (((s16) buf[0 + (4 * i + 2)]) & 0x0F) << 8
                                           | (((s16) buf[1 + (4 * i + 2)]) & 0xFF);
        fts_gesture_data.coordinate_y[i] = (((s16) buf[2 + (4 * i + 2)]) & 0x0F) << 8
                                           | (((s16) buf[3 + (4 * i + 2)]) & 0xFF);
    }

    return 0;
}

/*****************************************************************************
*   Name: fts_gesture_recovery
*  Brief: recovery gesture state when reset or power on
*  Input:
* Output:
* Return:
*****************************************************************************/
void fts_gesture_recovery(struct i2c_client *client)
{
    if ((ENABLE == fts_gesture_data.mode) && (ENABLE == fts_gesture_data.active)) {
        FTS_INFO("enter fts_gesture_recovery");
        fts_i2c_write_reg(client, 0xD1, 0xff);
        fts_i2c_write_reg(client, 0xD2, 0xff);
        fts_i2c_write_reg(client, 0xD5, 0xff);
        fts_i2c_write_reg(client, 0xD6, 0xff);
        fts_i2c_write_reg(client, 0xD7, 0xff);
        fts_i2c_write_reg(client, 0xD8, 0xff);
        fts_i2c_write_reg(client, FTS_REG_GESTURE_EN, ENABLE);
    }
}

/*****************************************************************************
*   Name: fts_gesture_suspend
*  Brief:
*  Input:
* Output:
* Return: return 0 if succuss, otherwise return error code
*****************************************************************************/
int fts_gesture_suspend(struct i2c_client *client)
{
    int ret;
    int i;
    u8 state;

    FTS_INFO("gesture suspend...");
    /* gesture not enable, return immediately */
    if (fts_gesture_data.mode == DISABLE) {
        FTS_INFO("gesture is disabled");
        return -EINVAL;
    }

    for (i = 0; i < 5; i++) {
        fts_i2c_write_reg(client, 0xd1, 0xff);
        fts_i2c_write_reg(client, 0xd2, 0xff);
        fts_i2c_write_reg(client, 0xd5, 0xff);
        fts_i2c_write_reg(client, 0xd6, 0xff);
        fts_i2c_write_reg(client, 0xd7, 0xff);
        fts_i2c_write_reg(client, 0xd8, 0xff);
        fts_i2c_write_reg(client, FTS_REG_GESTURE_EN, ENABLE);
        msleep(1);
        fts_i2c_read_reg(client, FTS_REG_GESTURE_EN, &state);
        if (state == ENABLE)
            break;
    }

    if (i >= 5) {
        FTS_ERROR("[GESTURE]Enter into gesture(suspend) failed!\n");
        fts_gesture_data.active = DISABLE;
        return -EIO;
    }

    ret = enable_irq_wake(fts_data->irq);
    if (ret) {
        FTS_INFO("enable_irq_wake(irq:%d) failed", fts_data->irq);
    }

    fts_gesture_data.active = ENABLE;
    FTS_INFO("[GESTURE]Enter into gesture(suspend) successfully!");
    FTS_FUNC_EXIT();
    return 0;
}

/*****************************************************************************
*   Name: fts_gesture_resume
*  Brief:
*  Input:
* Output:
* Return: return 0 if succuss, otherwise return error code
*****************************************************************************/
int fts_gesture_resume(struct i2c_client *client)
{
    int ret;
    int i;
    u8 state;

    FTS_INFO("gesture resume...");
    /* gesture not enable, return immediately */
    if (fts_gesture_data.mode == DISABLE) {
        FTS_DEBUG("gesture is disabled");
        return -EINVAL;
    }

    if (fts_gesture_data.active == DISABLE) {
        FTS_DEBUG("gesture in suspend is failed, no running fts_gesture_resume");
        return -EINVAL;
    }

    fts_gesture_data.active = DISABLE;
    for (i = 0; i < 5; i++) {
        fts_i2c_write_reg(client, FTS_REG_GESTURE_EN, DISABLE);
        msleep(1);
        fts_i2c_read_reg(client, FTS_REG_GESTURE_EN, &state);
        if (state == DISABLE)
            break;
    }

    if (i >= 5) {
        FTS_ERROR("[GESTURE]Clear gesture(resume) failed!\n");
        return -EIO;
    }

    ret = disable_irq_wake(fts_data->irq);
    if (ret) {
        FTS_INFO("disable_irq_wake(irq:%d) failed", fts_data->irq);
    }

    FTS_INFO("[GESTURE]resume from gesture successfully!");
    FTS_FUNC_EXIT();
    return 0;
}

/*****************************************************************************
*   Name: fts_gesture_init
*  Brief:
*  Input:
* Output:
* Return:
*****************************************************************************/
int fts_gesture_init(struct fts_ts_data *ts_data)
{
    struct i2c_client *client = ts_data->client;
    struct input_dev *input_dev = ts_data->input_dev;

    FTS_FUNC_ENTER();
    input_set_capability(input_dev, EV_KEY, KEY_POWER);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_U);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_UP);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_DOWN);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_LEFT);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_RIGHT);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_O);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_E);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_M);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_L);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_W);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_S);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_V);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_Z);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_C);

    __set_bit(KEY_GESTURE_RIGHT, input_dev->keybit);
    __set_bit(KEY_GESTURE_LEFT, input_dev->keybit);
    __set_bit(KEY_GESTURE_UP, input_dev->keybit);
    __set_bit(KEY_GESTURE_DOWN, input_dev->keybit);
    __set_bit(KEY_GESTURE_U, input_dev->keybit);
    __set_bit(KEY_GESTURE_O, input_dev->keybit);
    __set_bit(KEY_GESTURE_E, input_dev->keybit);
    __set_bit(KEY_GESTURE_M, input_dev->keybit);
    __set_bit(KEY_GESTURE_W, input_dev->keybit);
    __set_bit(KEY_GESTURE_L, input_dev->keybit);
    __set_bit(KEY_GESTURE_S, input_dev->keybit);
    __set_bit(KEY_GESTURE_V, input_dev->keybit);
    __set_bit(KEY_GESTURE_C, input_dev->keybit);
    __set_bit(KEY_GESTURE_Z, input_dev->keybit);

    fts_create_gesture_sysfs(client);
    fts_gesture_data.mode = ENABLE;
    fts_gesture_data.active = DISABLE;

    FTS_FUNC_EXIT();
    return 0;
}

/************************************************************************
*   Name: fts_gesture_exit
*  Brief: call when driver removed
*  Input:
* Output:
* Return:
***********************************************************************/
int fts_gesture_exit(struct i2c_client *client)
{
    FTS_FUNC_ENTER();
    sysfs_remove_group(&client->dev.kobj, &fts_gesture_group);
    FTS_FUNC_EXIT();
    return 0;
}
#endif
