/*
 * AMLOGIC Audio/Video streaming port driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the named License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 *
 * Author:  Tim Yao <timyao@amlogic.com>
 *
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/uio_driver.h>

#include <linux/amlogic/amports/aformat.h>
#include <linux/amlogic/amports/ptsserv.h>

#include <mach/am_regs.h>

#include "streambuf.h"
#include <linux/module.h>

#define INFO_VALID ((astream_dev) && (astream_dev->format))

typedef struct astream_device_s {
    char *name;
    char *format;
    s32   channum;
    s32   samplerate;
    s32   datawidth;

    struct device dev;
} astream_dev_t;

static char *astream_format[] = {
    "amadec_mpeg",
    "amadec_pcm_s16le",
    "amadec_aac",
    "amadec_ac3",
    "amadec_alaw",
    "amadec_mulaw",
    "amadec_dts",
    "amadec_pcm_s16be",
    "amadec_flac",
    "amadec_cook",
    "amadec_pcm_u8",
    "amadec_adpcm",
    "amadec_amr",
    "amadec_raac",
    "amadec_wma",
    "amadec_wmapro",
    "amadec_pcm_bluray",
    "amadec_alac",
    "amadec_vorbis",
    "amadec_aac_latm",
    "amadec_ape",
    "amadec_eac3",
    "amadec_pcm_widi",

};

static const char *na_string = "NA";
static astream_dev_t *astream_dev = NULL;

static ssize_t format_show(struct class *class, struct class_attribute *attr, char *buf)
{
    if (INFO_VALID && astream_dev->format) {
        return sprintf(buf, "%s\n", astream_dev->format);
    } else {
        return sprintf(buf, "%s\n", na_string);
    }
}

static ssize_t channum_show(struct class *class, struct class_attribute *attr, char *buf)
{
    if (INFO_VALID) {
        return sprintf(buf, "%u\n", astream_dev->channum);
    } else {
        return sprintf(buf, "%s\n", na_string);
    }
}

static ssize_t samplerate_show(struct class *class, struct class_attribute *attr, char *buf)
{
    if (INFO_VALID) {
        return sprintf(buf, "%u\n", astream_dev->samplerate);
    } else {
        return sprintf(buf, "%s\n", na_string);
    }
}

static ssize_t datawidth_show(struct class *class, struct class_attribute *attr, char *buf)
{
    if (INFO_VALID) {
        return sprintf(buf, "%u\n", astream_dev->datawidth);
    } else {
        return sprintf(buf, "%s\n", na_string);
    }
}

static ssize_t pts_show(struct class *class, struct class_attribute *attr, char *buf)
{
    u32 pts;
    u32 pts_margin = 0;

    if (astream_dev->samplerate <= 12000) {
        pts_margin = 512;
    }

    if (INFO_VALID &&
        (pts_lookup(PTS_TYPE_AUDIO, &pts, pts_margin) >= 0)) {
        return sprintf(buf, "0x%x\n", pts);
    } else {
        return sprintf(buf, "%s\n", na_string);
    }
}

static struct class_attribute astream_class_attrs[] = {
    __ATTR_RO(format),
    __ATTR_RO(samplerate),
    __ATTR_RO(channum),
    __ATTR_RO(datawidth),
    __ATTR_RO(pts),
    __ATTR_NULL
};

static struct class astream_class = {
        .name = "astream",
        .class_attrs = astream_class_attrs,
    };

#if 1
static struct uio_info astream_uio_info = {
    .name = "astream_uio",
    .version = "0.1",
    .irq = UIO_IRQ_NONE,

    .mem = {
        [0] = {
            .name = "AIFIFO",			
            .memtype = UIO_MEM_PHYS,
            .addr = (IO_CBUS_PHY_BASE + CBUS_REG_OFFSET(AIU_AIFIFO_CTRL)),
            .size = PAGE_SIZE,
        },
        [1] = {
            .memtype = UIO_MEM_PHYS,
            .addr = (IO_CBUS_PHY_BASE + CBUS_REG_OFFSET(VCOP_CTRL_REG)),
            .size = PAGE_SIZE,
        },
        [2] = {
            .name = "SECBUS",
            .memtype = UIO_MEM_PHYS,
            .addr = (IO_SECBUS_PHY_BASE ),
            .size = PAGE_SIZE,
        },  
        [3] = {
            .name = "CBUS",
            .memtype = UIO_MEM_PHYS,
            .addr = (IO_CBUS_PHY_BASE+ CBUS_REG_OFFSET(ASSIST_HW_REV)),
            .size = PAGE_SIZE,
        },  
        [4] = {
            .name = "CBUS-START",
            .memtype = UIO_MEM_PHYS,
            .addr = (IO_CBUS_PHY_BASE+ CBUS_REG_OFFSET(0x1000)),
            .size = PAGE_SIZE*4,
        },                  
    },
};
#endif

static void astream_release(struct device *dev)
{
    kfree(astream_dev);

    astream_dev = NULL;
}

s32 adec_init(stream_port_t *port)
{
    aformat_t af;

    if (!astream_dev) {
        return -ENODEV;
    }

    af = port->aformat;

    astream_dev->channum = port->achanl;
    astream_dev->samplerate = port->asamprate;
    astream_dev->datawidth = port->adatawidth;

    wmb();
    astream_dev->format = astream_format[af];

    return 0;
}

s32 adec_release(aformat_t vf)
{
    printk("adec_release\n");

    if (!astream_dev) {
        return -ENODEV;
    }

    astream_dev->format = NULL;

    return 0;
}

s32 astream_dev_register(void)
{
    s32 r;

    r = class_register(&astream_class);
    if (r) {
        printk("astream class create fail.\n");
        return r;
    }

    astream_dev = kzalloc(sizeof(astream_dev_t), GFP_KERNEL);

    if (!astream_dev) {
        printk("astream device create fail.\n");
        r = -ENOMEM;
        goto err_3;
    }

    astream_dev->dev.class = &astream_class;
    astream_dev->dev.release = astream_release;

    dev_set_name(&astream_dev->dev, "astream-dev");

    dev_set_drvdata(&astream_dev->dev, astream_dev);

    r = device_register(&astream_dev->dev);
    if (r) {
        printk("astream device register fail.\n");
        goto err_2;
    }

#if 1
    if (uio_register_device(&astream_dev->dev, &astream_uio_info)) {
        printk("astream UIO device register fail.\n");
        r = -ENODEV;
        goto err_1;
    }
#endif

    return 0;

err_1:
    device_unregister(&astream_dev->dev);

err_2:
    kfree(astream_dev);
    astream_dev = NULL;

err_3:
    class_unregister(&astream_class);

    return r;
}

void astream_dev_unregister(void)
{
    if (astream_dev) {
#if 1
        uio_unregister_device(&astream_uio_info);
#endif

        device_unregister(&astream_dev->dev);

        class_unregister(&astream_class);
    }
}
