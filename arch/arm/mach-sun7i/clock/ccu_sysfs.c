/*
 * arch/arm/mach-sun6i/clock/ccu_sysfs.c
 * (c) Copyright 2010-2015
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * James Deng <csjamesdeng@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/kdev_t.h>
#include <linux/clk.h>

#include "ccm_i.h"
#include "ccu_sysfs.h"

/* __ccu_export/export_store/ccu_attr_group/clk_get */

static DEFINE_MUTEX(sysfs_lock);

/* clock handle for sunxi clock */
struct ccu_sysfs_handle {
    bool                exported;
    char                name[256];
    __aw_ccu_clk_id_e   id;
    struct clk          *clk;
};

static struct ccu_sysfs_handle g_clk_handle[AW_CCU_CLK_CNT];

ssize_t ccu_export_store(struct class *class,
                         struct class_attribute *attr,
                         const char *buf, size_t len);
ssize_t ccu_unexport_store(struct class *class,
                           struct class_attribute *attr,
                           const char *buf, size_t len);

static struct class_attribute ccu_class_attrs[] = {
    __ATTR(export, 0200, NULL, ccu_export_store),
    __ATTR(unexport, 0200, NULL, ccu_unexport_store),
    __ATTR_NULL,
};

static struct class ccu_class = {
    .name =         "ccu",
    .owner =        THIS_MODULE,
    .class_attrs =  ccu_class_attrs,
};

static __aw_ccu_clk_id_e __get_clock_id(char *clock_name)
{
    u32 i = 0;

    for (i = 0; i < (u32)AW_CCU_CLK_CNT; i++)
        if (true == sysfs_streq(aw_ccu_clk_tbl[i].name, (const char *)clock_name))
            return aw_ccu_clk_tbl[i].id;

    CCU_ERR("%s: could not find id for clock %s\n", __func__, clock_name);
    return AW_SYS_CLK_NONE;
}

static int __match_export(struct device *dev, void *data)
{
    return dev_get_drvdata(dev) == data;
}

static ssize_t ccu_rate_show(struct device *dev,
                             struct device_attribute *attr, char *buf)
{
    unsigned long rate = 0;
    struct ccu_sysfs_handle *pclk_handle = dev_get_drvdata(dev);

    if (NULL == pclk_handle->clk) {
        CCU_ERR("%s: clk is NULL\n", __func__);
        return -EINVAL;
    }

    rate = clk_get_rate(pclk_handle->clk);
    CCU_DBG("%s: get rate %d\n", __func__, (int)rate);

    return sprintf(buf, "%d\n", (int)rate);
}

static ssize_t ccu_rate_store(struct device *dev,
                              struct device_attribute *attr, const char *buf, size_t size)
{
    u32 usign = 0;
    long itemp = 0;
    struct ccu_sysfs_handle *pclk_handle = dev_get_drvdata(dev);

    CCU_ASSERT_GOTO(NULL != pclk_handle->clk, usign, end);

    CCU_ASSERT_GOTO(strict_strtol(buf, 0, &itemp) >= 0, usign, end);
    CCU_DBG("%s: rate to set is %d\n", __func__, (int)itemp);

    CCU_ASSERT_GOTO(0 == clk_set_rate(pclk_handle->clk, itemp), usign, end);
    CCU_DBG("%s: clk_set_rate success\n", __func__);

end:
    if (0 != usign) {
        CCU_ERR("%s: line %d\n", __func__, usign);
        return -EINVAL;
    }
    return size;
}
static DEVICE_ATTR(rate, 0666, ccu_rate_show, ccu_rate_store);

static ssize_t ccu_reset_store(struct device *dev,
                               struct device_attribute *attr, const char *buf, size_t size)
{
    u32 usign = 0;
    long itemp = 0;
    struct ccu_sysfs_handle *pclk_handle = dev_get_drvdata(dev);

    CCU_ASSERT_GOTO(NULL != pclk_handle->clk, usign, end);

    CCU_ASSERT_GOTO(strict_strtol(buf, 0, &itemp) >= 0, usign, end);
    CCU_DBG("%s: para itemp is %d\n", __func__, (int)itemp);

    switch (itemp) {
        case 1: /* reset */
            CCU_ASSERT_GOTO(0 == clk_reset(pclk_handle->clk, AW_CCU_CLK_RESET), usign, end);
            CCU_DBG("%s: clk_reset - RESET success\n", __func__);
            break;
        case 0: /* nreset */
            CCU_ASSERT_GOTO(0 == clk_reset(pclk_handle->clk, AW_CCU_CLK_NRESET), usign, end);
            CCU_DBG("%s: clk_reset - NRESET success\n", __func__);
            break;
        default:
            usign = __LINE__;
            break;
    }

end:
    if (0 != usign) {
        CCU_ERR("%s: line %d\n", __func__, (int)usign);
        return -EINVAL;
    }

    return size;
}
static DEVICE_ATTR(reset, 0222, NULL, ccu_reset_store);

static ssize_t ccu_enable_store(struct device *dev,
                                struct device_attribute *attr, const char *buf, size_t size)
{
    u32 usign = 0;
    long itemp = 0;
    struct ccu_sysfs_handle *pclk_handle = dev_get_drvdata(dev);

    CCU_ASSERT_GOTO(NULL != pclk_handle->clk, usign, end);

    CCU_ASSERT_GOTO(strict_strtol(buf, 0, &itemp) >= 0, usign, end);
    CCU_DBG("%s: para itemp is %d\n", __func__, (int)itemp);

    switch (itemp) {
        case 1: /* enable clock */
            CCU_ASSERT_GOTO(0 == clk_enable(pclk_handle->clk), usign, end);
            CCU_DBG("%s: clk_enable success\n", __func__);
            break;
        case 0: /* disable clock */
            clk_disable(pclk_handle->clk);
            break;
        default:
            usign = __LINE__;
            break;
    }

end:
    if (0 != usign) {
        CCU_ERR("%s: line %d\n", __func__, (int)usign);
        return -EINVAL;
    }
    return size;
}
static DEVICE_ATTR(enable, 0222, NULL, ccu_enable_store);

static ssize_t ccu_parent_show(struct device *dev,
                               struct device_attribute *attr, char *buf)
{
    u32 usign = 0;
    struct clk *clk_parent = NULL;
    struct ccu_sysfs_handle *pclk_handle = dev_get_drvdata(dev);

    if (NULL == pclk_handle->clk) {
        usign = __LINE__;
        goto err;
    }

    clk_parent = clk_get_parent(pclk_handle->clk);
    if (NULL == clk_parent) {
        usign = __LINE__;
        goto err;
    }
    CCU_DBG("%s: get parent name %s\n", __func__, clk_parent->aw_clk->name);

    return sprintf(buf, "%s\n", clk_parent->aw_clk->name);

err:
    CCU_ERR("%s: line %d\n", __func__, (int)usign);
    return -EINVAL;
}

static ssize_t ccu_parent_store(struct device *dev,
                                struct device_attribute *attr, const char *buf, size_t size)
{
    u32 usign = 0;
    struct clk  *parent = NULL;
    __aw_ccu_clk_id_e   clk_id = AW_SYS_CLK_NONE;
    struct ccu_sysfs_handle *pclk_handle = dev_get_drvdata(dev);

    if (NULL == pclk_handle->clk) {
        usign = __LINE__;
        goto end;
    }

    clk_id = __get_clock_id((char *)buf);
    if (AW_SYS_CLK_NONE == clk_id) {
        usign = __LINE__;
        goto end;
    }
    CCU_DBG("%s: parent name %s, id %d\n", __func__, buf, (int)clk_id);

    parent = &aw_clock[clk_id];
    if (0 != clk_set_parent(pclk_handle->clk, parent)) {
        usign = __LINE__;
        goto end;
    }
    CCU_DBG("%s: clk_set_parent success\n", __func__);

end:
    if (0 != usign) {
        CCU_ERR("%s: line %d\n", __func__, (int)usign);
        return -EINVAL;
    }
    return size;
}
static DEVICE_ATTR(parent, 0666, ccu_parent_show, ccu_parent_store);

static ssize_t ccu_get_store(struct device *dev,
                             struct device_attribute *attr, const char *buf, size_t size)
{
    int i = -1;
    struct ccu_sysfs_handle *pclk_handle = dev_get_drvdata(dev);

    if (strict_strtol(buf, 0, (long *)&i) < 0) {
        CCU_ERR("%s: strict_strtol %s failed\n", __func__, buf);
        return -EINVAL;
    }

    if (0 == i) { /* to clk_put */
        if (NULL == pclk_handle->clk) {
            CCU_ERR("%s: clock %s not got yet\n", __func__, pclk_handle->name);
            return -EINVAL;
        } else {
            clk_put(pclk_handle->clk);
            pclk_handle->clk = NULL;
        }
    } else if (1 == i) { /* to clk_get */
        if (NULL != pclk_handle->clk) {
            CCU_ERR("%s: clock %s already got, handle 0x%08x\n", __func__,
                    pclk_handle->name, (u32)pclk_handle->clk);
            return -EINVAL;
        } else {
            pclk_handle->clk = clk_get(NULL, pclk_handle->name);
            if (IS_ERR(pclk_handle->clk)) {
                CCU_ERR("%s: get clock %s failed\n", __func__, pclk_handle->name);
                return -EINVAL;
            }
        }
    } else {
        CCU_ERR("%s: para %s invalid\n", __func__, buf);
        return -EINVAL;
    }

    return size;
}
static DEVICE_ATTR(get, 0222, NULL, ccu_get_store);

static const struct attribute *ccu_attrs[] = {
    &dev_attr_get.attr,
    &dev_attr_parent.attr,
    &dev_attr_rate.attr,
    &dev_attr_enable.attr,
    &dev_attr_reset.attr,
    NULL,
};

static const struct attribute_group ccu_attr_group = {
    .attrs = (struct attribute **)ccu_attrs,
};

bool __ccu_export(char *clock_name, __aw_ccu_clk_id_e clock_id)
{
    int status = -EINVAL;
    struct device   *dev = NULL;

    /* get clock id and name for g_clk_handle[i] */
    g_clk_handle[clock_id].id = clock_id;
    strcpy(g_clk_handle[clock_id].name, clock_name);
    CCU_DBG("%s: g_clk_handle[i] - id %d, name %s\n", __func__,
            (int)clock_id, g_clk_handle[clock_id].name);

    /* create device dir */
    dev = device_create(&ccu_class, NULL, MKDEV(0, 0), (void *)&g_clk_handle[clock_id], clock_name);
    if (!IS_ERR(dev)) {
        status = sysfs_create_group(&dev->kobj, &ccu_attr_group);
        if (status != 0) {
            device_unregister(dev);
            CCU_ERR("%s: sysfs_create_group failed, status %d\n", __func__, status);
        }
    } else
        status = PTR_ERR(dev);

    if (status) {
        CCU_ERR("%s: %s, status %d\n", __func__, clock_name, status);
        return false;
    }

    CCU_DBG("%s: %s success\n", __func__, clock_name);
    return true;
}

bool __ccu_unexport(char *clk_name, __aw_ccu_clk_id_e clk_id)
{
    u32     usign = 0;
    struct device   *dev = NULL;

    /* must release clock handle first, echo 0 > get */
    CCU_ASSERT_GOTO(NULL == g_clk_handle[clk_id].clk, usign, end);

    dev = class_find_device(&ccu_class, NULL, &g_clk_handle[clk_id], __match_export);
    if (dev) {
        CCU_DBG("%s: class_find_device success\n", __func__);
        put_device(dev);
        device_unregister(dev);
    } else {
        usign = __LINE__;
        goto end;
    }

end:
    if (0 != usign) {
        CCU_ERR("%s: line %d\n", __func__, (int)usign);
        return false;
    }
    return true;
}

/**
 * sysfs_to_str - convert sysfs buf string to standard string
 * @old_str:    old sysf string, end up with '\n'
 * @new_str:    store the new string converted.
 * @new_buf_size: length of new_str buffer.
 *
 * return true if success, false if failed.
 */
bool inline sysfs_to_str(const char *old_str, char *new_str, int new_buf_size)
{
    int i = 0;

    while ('\0' != *old_str && '\n' != *old_str) {
        *new_str++ = *old_str++;
        if (++i >= new_buf_size)
            return false;
    }
    *new_str = '\0';
    return true;
}

ssize_t ccu_export_store(struct class *class,
                         struct class_attribute *attr,
                         const char *buf, size_t len)
{
    char buf_temp[256] = {0};
    __aw_ccu_clk_id_e clk_id = AW_SYS_CLK_NONE;

    /* get clock id and name(convert from sysfs str) */
    if (false == sysfs_to_str(buf, buf_temp, sizeof(buf_temp))) {
        CCU_ERR("%s: sysfs_to_str failed, buf %s\n", __func__, buf);
        return -EINVAL;
    }
    clk_id = __get_clock_id(buf_temp);
    if (AW_SYS_CLK_NONE == clk_id) {
        CCU_ERR("%s: invalid clock name %s\n", __func__, buf_temp);
        return -EINVAL;
    }
    CCU_DBG("%s: clock name %s, clock id %d\n", __func__, buf_temp, (int)clk_id);

    mutex_lock(&sysfs_lock);

    /* check if exported */
    if (true == g_clk_handle[clk_id].exported) {
        mutex_unlock(&sysfs_lock);
        CCU_ERR("%s: clock %s already exported\n", __func__, buf_temp);
        return -EINVAL;
    }

    /* export clock */
    if (false == __ccu_export(buf_temp, clk_id)) {
        mutex_unlock(&sysfs_lock);
        CCU_ERR("%s: __ccu_export clock %s err\n", __func__, buf_temp);
        return -EINVAL;
    }

    /* change exported flag to true */
    g_clk_handle[clk_id].exported = true;

    mutex_unlock(&sysfs_lock);
    CCU_DBG("%s %s success\n", __func__, buf_temp);
    return len;
}

ssize_t ccu_unexport_store(struct class *class,
                           struct class_attribute *attr,
                           const char *buf, size_t len)
{
    char buf_temp[256] = {0};
    __aw_ccu_clk_id_e clk_id = AW_SYS_CLK_NONE;

    /* get clock id and name(convert from sysfs str) */
    if (false == sysfs_to_str(buf, buf_temp, sizeof(buf_temp))) {
        CCU_ERR("%s: sysfs_to_str failed, buf %s\n", __func__, buf);
        return -EINVAL;
    }
    clk_id = __get_clock_id(buf_temp);
    if (AW_SYS_CLK_NONE == clk_id) {
        CCU_ERR("%s: invalid clock name %s\n", __func__, buf_temp);
        return -EINVAL;
    }
    CCU_DBG("%s: clock name %s, clock id %d\n", __func__, buf_temp, (int)clk_id);

    mutex_lock(&sysfs_lock);

    /* check if unexported */
    if (false == g_clk_handle[clk_id].exported) {
        mutex_unlock(&sysfs_lock);
        CCU_ERR("%s: clock %s already un-exported\n", __func__, buf_temp);
        return -EINVAL;
    }

    /* unexport clock */
    if (false == __ccu_unexport(buf_temp, clk_id)) {
        mutex_unlock(&sysfs_lock);
        CCU_ERR("%s: __ccu_unexport clock %s err\n", __func__, buf_temp);
        return -EINVAL;
    }

    /* change exported flag to false */
    g_clk_handle[clk_id].exported = false;

    mutex_unlock(&sysfs_lock);
    CCU_DBG("%s: clock %s success\n", __func__, buf_temp);
    return len;
}

static int __init ccu_sysfs_init(void)
{
    int status;

    /* create /sys/class/ccu/ */
    status = class_register(&ccu_class);
    if (status < 0)
        CCU_ERR("%s: status %d\n", __func__, status);
    else
        CCU_DBG("%s successfully\n", __func__);

    return status;
}
postcore_initcall(ccu_sysfs_init);
