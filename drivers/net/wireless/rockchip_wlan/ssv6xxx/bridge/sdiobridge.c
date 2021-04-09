/*
 * Copyright (c) 2015 South Silicon Valley Microelectronics Inc.
 * Copyright (c) 2015 iComm Corporation
 *
 * This program is free software: you can redistribute it and/or modify 
 * it under the terms of the GNU General Public License as published by 
 * the Free Software Foundation, either version 3 of the License, or 
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  
 * See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License 
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/irq.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/platform_device.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/gpio.h>
#include <linux/pm_runtime.h>
#include <linux/printk.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <hwif/sdio/sdio_def.h>
#include "sdiobridge.h"
#include "debug.h"
#define BLOCKSIZE 0x40
#define RXBUFLENGTH 1024*3
#define RXBUFSIZE 512
enum ssvcabrio_int
{
    SSVCABRIO_INT_RX = 0x00000001,
    SSVCABRIO_INT_TX = 0x00000002,
    SSVCABRIO_INT_GPIO = 0x00000004,
    SSVCABRIO_INT_SYS = 0x00000008,
};
#define CHECK_RET(_fun) \
 do { \
  if (0 != _fun) \
   printk("File = %s\nLine = %d\nFunc=%s\nDate=%s\nTime=%s\n", __FILE__, __LINE__, __FUNCTION__, __DATE__, __TIME__); \
 } while (0)
static unsigned int ssv_sdiobridge_ioctl_major = 0;
static unsigned int num_of_dev = 1;
static struct cdev ssv_sdiobridge_ioctl_cdev;
static struct class *fc;
static struct ssv_sdiobridge_glue *glue;
struct ssv_rxbuf
{
    struct list_head list;
    u32 rxsize;
    u8 rxdata[RXBUFLENGTH];
};
static const struct sdio_device_id ssv_sdiobridge_devices[] =
{
    {SDIO_DEVICE(MANUFACTURER_SSV_CODE, (MANUFACTURER_ID_CABRIO_BASE | 0x0))},
    {}
};
MODULE_DEVICE_TABLE(sdio, ssv_sdiobridge_devices);
static long ssv_sdiobridge_ioctl_getFuncfocus(struct ssv_sdiobridge_glue *glue,unsigned int cmd, struct ssv_sdiobridge_cmd *pcmd_data,struct ssv_sdiobridge_cmd *pucmd_data,bool isCompat)
{
    long retval =0;
    if ( pcmd_data->out_data_len < 1)
    {
        retval = -1;
    }
    else
    {
        u8 out_data = glue->funcFocus;
        if ( isCompat )
        {
            CHECK_RET(copy_to_user((int __user *)compat_ptr((unsigned long)pucmd_data->out_data),&out_data,sizeof(out_data)));
        }
        else
        {
            CHECK_RET(copy_to_user((int __user *)pucmd_data->out_data, &out_data, sizeof(out_data)));
        }
    }
    return retval;
}
static long ssv_sdiobridge_ioctl_setFuncfocus(struct ssv_sdiobridge_glue *glue,unsigned int cmd, struct ssv_sdiobridge_cmd *pcmd_data,struct ssv_sdiobridge_cmd *pucmd_data,bool isCompat)
{
    int retval =0;
    if ( pcmd_data->out_data_len < 0)
    {
        retval = -EFAULT;
        dev_err(glue->dev, "%s : input length must < 0",__FUNCTION__);
    }
    else
    {
        if ( isCompat )
        {
            CHECK_RET(copy_from_user(&glue->funcFocus,(int __user *)compat_ptr((unsigned long)pcmd_data->in_data),sizeof(glue->funcFocus)));
        }
        else
        {
            CHECK_RET(copy_from_user(&glue->funcFocus,(int __user *)pcmd_data->in_data,sizeof(glue->funcFocus)));
        }
    }
    return retval;
}
static long ssv_sdiobridge_ioctl_getBusWidth(struct ssv_sdiobridge_glue *glue,unsigned int cmd, struct ssv_sdiobridge_cmd *pcmd_data,struct ssv_sdiobridge_cmd *pucmd_data,bool isCompat)
{
    struct sdio_func *func = dev_to_sdio_func(glue->dev);
    long retval =0;
    if ( pcmd_data->out_data_len < 1)
    {
        retval = -1;
    }
    else
    {
        u8 out_data = 1;
        if ( func->card->host->ios.bus_width != MMC_BUS_WIDTH_1 )
        {
            out_data = 4;
        }
        if ( isCompat )
        {
            CHECK_RET(copy_to_user((int __user *)compat_ptr((unsigned long)pucmd_data->out_data),&out_data,sizeof(out_data)));
        }
        else
        {
            CHECK_RET(copy_to_user((int __user *)pucmd_data->out_data,&out_data,sizeof(out_data)));
        }
    }
    return retval;
}
#if 0
static long ssv_sdiobridge_ioctl_setBusWidth(struct ssv_sdiobridge_glue *glue,unsigned int cmd, struct ssv_sdiobridge_cmd *pcmd_data,struct ssv_sdiobridge_cmd *pucmd_data,bool isCompat)
{
    struct sdio_func *func = dev_to_sdio_func(glue->dev);
    int retval =0;
    struct ssv_sdiobridge_cmd *pData;
    u8 inData[1];
    copy_from_user(pData,(int __user *)arg,sizeof(*pData));
    if ( isCompat )
    {
        copy_from_user(&cmd_data,(int __user *)compat_ptr((unsigned long)pucmd_data),sizeof(*pucmd_data));
    }
    else
    {
        copy_from_user(&cmd_data,(int __user *)pucmd_data,sizeof(*pucmd_data));
    }
    if ( pData->in_data_len < 0)
    {
        retval = -EFAULT;
        dev_err(glue->dev, "%s : input length must > 1",__FUNCTION__);
    }
    else
    {
        if ( pData->in_data == 1 )
        {
            if ( (func->card->host->caps & MMC_CAP_4_BIT_DATA) && !(func->card->cccr.low_speed && !func->card->cccr.wide_bus) )
            {
                extern void mmc_set_bus_width(struct mmc_host *host, unsigned int width);
                u8 ctrl = sdio_f0_readb(func,SDIO_CCCR_IF,&retval);
                if (retval)
                    return retval;
                if (!(ctrl & SDIO_BUS_WIDTH_4BIT))
                    return 0;
                ctrl &= ~SDIO_BUS_WIDTH_4BIT;
                ctrl |= SDIO_BUS_ASYNC_INT;
                sdio_f0_writeb(func,ctrl,SDIO_CCCR_IF,&retval);
                if (retval)
                    return retval;
                mmc_set_bus_width(func->card->host, MMC_BUS_WIDTH_1);
            }
        }
        else
        {
            if ( func->card->host->ios.bus_width != MMC_BUS_WIDTH_4 )
            {
            }
        }
    }
    return retval;
}
static long ssv_sdiobridge_ioctl_getBlockMode(struct ssv_sdiobridge_glue *glue,unsigned int cmd, struct ssv_sdiobridge_cmd *pcmd_data,struct ssv_sdiobridge_cmd *pucmd_data,bool isCompat)
{
    long retval =0;
    if ( pcmd_data->out_data_len < 1)
    {
        retval = -1;
    }
    else
    {
        if ( isCompat )
        {
            copy_to_user((int __user *)compat_ptr((unsigned long)pucmd_data->out_data),&out_data,sizeof(out_data));
        }
        else
        {
            copy_to_user((int __user *)pucmd_data->out_data,&out_data,sizeof(out_data));
        }
    }
    return retval;
}
static long ssv_sdiobridge_ioctl_setBlockMode(struct ssv_sdiobridge_glue *glue,unsigned int cmd, struct ssv_sdiobridge_cmd *pcmd_data,struct ssv_sdiobridge_cmd *pucmd_data,bool isCompat)
{
    long retval =0;
    if ( pcmd_data->in_data_len < 1)
    {
        retval = -1;
    }
    else
    {
        if ( isCompat )
        {
            copy_from_user(&glue->blockMode,(int __user *)pucmd_data->in_data,sizeof(glue->blockMode));
        }
    }
    return retval;
}
#endif
static long ssv_sdiobridge_ioctl_getBlockSize(struct ssv_sdiobridge_glue *glue,unsigned int cmd, struct ssv_sdiobridge_cmd *pcmd_data,struct ssv_sdiobridge_cmd *pucmd_data,bool isCompat)
{
    long retval =0;
    if ( pcmd_data->out_data_len < 2)
    {
        retval = -1;
    }
    else
    {
        if ( isCompat )
        {
            CHECK_RET(copy_to_user((int __user *)compat_ptr((unsigned long)pucmd_data->out_data),&glue->blockSize,sizeof(glue->blockSize)));
        }
        else
        {
            CHECK_RET(copy_to_user((int __user *)pucmd_data->out_data,&glue->blockSize,sizeof(glue->blockSize)));
        }
    }
    return retval;
}
static long ssv_sdiobridge_ioctl_setBlockSize(struct ssv_sdiobridge_glue *glue,unsigned int cmd, struct ssv_sdiobridge_cmd *pcmd_data,struct ssv_sdiobridge_cmd *pucmd_data,bool isCompat)
{
    struct sdio_func *func = dev_to_sdio_func(glue->dev);
    long retval =0;
    if ( pcmd_data->in_data_len < 2)
    {
        retval = -1;
    }
    else
    {
        if ( isCompat )
        {
            CHECK_RET(copy_from_user(&glue->blockSize,(int __user *)compat_ptr((unsigned long)pucmd_data->in_data),sizeof(glue->blockSize)));
        }
        else
        {
            CHECK_RET(copy_from_user(&glue->blockSize,(int __user *)pucmd_data->in_data,sizeof(glue->blockSize)));
        }
        dev_err(glue->dev,"%s: blockSize [%d]\n",__FUNCTION__,glue->blockSize);
        sdio_claim_host(func);
        sdio_set_block_size(func,glue->blockSize);
        sdio_release_host(func);
    }
    return retval;
}
static long ssv_sdiobridge_ioctl_readByte(struct ssv_sdiobridge_glue *glue,unsigned int cmd, struct ssv_sdiobridge_cmd *pcmd_data,struct ssv_sdiobridge_cmd *pucmd_data,bool isCompat)
{
    struct sdio_func *func = dev_to_sdio_func(glue->dev);
    long retval =0;
    if ( pcmd_data->out_data_len < 4)
    {
        retval = -1;
    }
    else
    {
        u32 address;
        u8 out_data;
        int ret = 0;
        if ( isCompat )
        {
            CHECK_RET(copy_from_user(&address,(int __user *)compat_ptr((unsigned long)pucmd_data->in_data),sizeof(address)));
        }
        else
        {
            CHECK_RET(copy_from_user(&address,(int __user *)pucmd_data->in_data,sizeof(address)));
        }
        sdio_claim_host(func);
        if ( glue->funcFocus == 0 )
        {
            out_data = sdio_f0_readb(func, address, &ret);
        }
        else
        {
            out_data = sdio_readb(func, address, &ret);
        }
        sdio_release_host(func);
        dev_err(glue->dev,"%s: [%X] [%02X] ret:[%d]\n",__FUNCTION__,address,out_data,ret);
        if ( !ret )
        {
            if ( isCompat )
            {
                CHECK_RET(copy_to_user((void *)compat_ptr((unsigned long)pucmd_data->out_data),&out_data,sizeof(out_data)));
            }
            else
            {
                CHECK_RET(copy_to_user((int __user *)pucmd_data->out_data,&out_data,sizeof(out_data)));
            }
        }
        else
        {
            dev_err(glue->dev,"%s: error : %d",__FUNCTION__,ret);
            retval = -1;
        }
    }
    return retval;
}
static long ssv_sdiobridge_ioctl_writeByte(struct ssv_sdiobridge_glue *glue,unsigned int cmd, struct ssv_sdiobridge_cmd *pcmd_data,struct ssv_sdiobridge_cmd *pucmd_data,bool isCompat)
{
    struct sdio_func *func = dev_to_sdio_func(glue->dev);
    long retval =0;
    if ( pcmd_data->in_data_len < 5)
    {
        retval = -1;
    }
    else
    {
        u8 tmp[5];
        u32 address;
        u8 data;
        int ret = 0;
        if ( isCompat )
        {
            CHECK_RET(copy_from_user(tmp,(int __user *)compat_ptr((unsigned long)pucmd_data->in_data),sizeof(tmp)));
        }
        else
        {
            CHECK_RET(copy_from_user(tmp,(int __user *)pucmd_data->in_data,sizeof(tmp)));
        }
        address = *((u32 *)tmp);
        data = tmp[4];
        sdio_claim_host(func);
        if ( glue->funcFocus == 0 )
        {
            sdio_f0_writeb(func,data, address, &ret);
        }
        else
        {
            sdio_writeb(func,data, address, &ret);
        }
        sdio_release_host(func);
        if ( ret )
        {
            dev_err(glue->dev,"%s: error : %d",__FUNCTION__,ret);
            retval = -1;
        }
    }
    return retval;
}
static long ssv_sdiobridge_ioctl_getMultiByteIOPort(struct ssv_sdiobridge_glue *glue,unsigned int cmd, struct ssv_sdiobridge_cmd *pcmd_data,struct ssv_sdiobridge_cmd *pucmd_data,bool isCompat)
{
    long retval =0;
    if ( pcmd_data->out_data_len < 4)
    {
        retval = -1;
    }
    else
    {
        if ( isCompat )
        {
            CHECK_RET(copy_to_user((int __user *)compat_ptr((unsigned long)pucmd_data->out_data),&glue->dataIOPort,sizeof(glue->dataIOPort)));
        }
        else
        {
            CHECK_RET(copy_to_user((int __user *)pucmd_data->out_data,&glue->dataIOPort,sizeof(glue->dataIOPort)));
        }
    }
    return retval;
}
static long ssv_sdiobridge_ioctl_setMultiByteIOPort(struct ssv_sdiobridge_glue *glue,unsigned int cmd, struct ssv_sdiobridge_cmd *pcmd_data,struct ssv_sdiobridge_cmd *pucmd_data,bool isCompat)
{
    long retval =0;
    if ( pcmd_data->in_data_len < 4)
    {
        retval = -1;
    }
    else
    {
        if ( isCompat )
        {
            CHECK_RET(copy_from_user(&glue->dataIOPort,(int __user *)compat_ptr((unsigned long)pucmd_data->in_data),sizeof(glue->dataIOPort)));
        }
        else
        {
            CHECK_RET(copy_from_user(&glue->dataIOPort,(int __user *)pucmd_data->in_data,sizeof(glue->dataIOPort)));
        }
    }
    return retval;
}
static long ssv_sdiobridge_ioctl_readMultiByte(struct ssv_sdiobridge_glue *glue,unsigned int cmd, struct ssv_sdiobridge_cmd *pcmd_data,struct ssv_sdiobridge_cmd *pucmd_data,bool isCompat)
{
    struct sdio_func *func = dev_to_sdio_func(glue->dev);
    long retval =0;
    u8 *tmpdata;
    int ret;
    int readsize;
    tmpdata = kzalloc(pcmd_data->out_data_len, GFP_KERNEL);
    if ( tmpdata == NULL )
    {
        dev_err(glue->dev,"%s: error : alloc buf error size:%d",__FUNCTION__,pcmd_data->out_data_len);
        return -1;
    }
    readsize = sdio_align_size(func,pcmd_data->out_data_len);
    sdio_claim_host(func);
    ret = sdio_memcpy_fromio(func, tmpdata,glue->dataIOPort, readsize );
    sdio_release_host(func);
    if (unlikely(glue->dump))
    {
        printk(KERN_DEBUG "%s: READ data address[%08x] len[%d] readsize[%d]\n",__FUNCTION__,glue->dataIOPort,(int)pcmd_data->out_data_len,readsize);
        print_hex_dump(KERN_DEBUG, "ssv_sdio: READ ",
                       DUMP_PREFIX_OFFSET, 16, 1,
                       tmpdata, pcmd_data->out_data_len, false);
    }
    if ( !ret )
    {
        if ( isCompat )
        {
            CHECK_RET(copy_to_user((int __user *)compat_ptr((unsigned long)pucmd_data->out_data),tmpdata,pcmd_data->out_data_len));
        }
        else
        {
            CHECK_RET(copy_to_user((int __user *)pucmd_data->out_data,tmpdata,pcmd_data->out_data_len));
        }
    }
    else
    {
        dev_err(glue->dev,"%s: error : %d",__FUNCTION__,ret);
        retval = -1;
    }
    kfree(tmpdata);
    dev_err(glue->dev,"%s(): %d\n", __FUNCTION__, ret);
    return retval;
}
static long ssv_sdiobridge_ioctl_writeMultiByte(struct ssv_sdiobridge_glue *glue,unsigned int cmd, struct ssv_sdiobridge_cmd *pcmd_data,struct ssv_sdiobridge_cmd *pucmd_data,bool isCompat)
{
    struct sdio_func *func = dev_to_sdio_func(glue->dev);
    long retval =0;
    u8 *tmpdata;
    int ret;
    int readsize ;
    tmpdata = kzalloc(pcmd_data->in_data_len, GFP_KERNEL);
    if ( tmpdata == NULL )
    {
        dev_err(glue->dev,"%s: error : alloc buf error size:%d",__FUNCTION__,pcmd_data->out_data_len);
        return -1;
    }
    if ( isCompat )
    {
        CHECK_RET(copy_from_user(tmpdata,(int __user *)compat_ptr((unsigned long)pucmd_data->in_data),pcmd_data->in_data_len));
    }
    else
    {
        CHECK_RET(copy_from_user(tmpdata,(int __user *)pucmd_data->in_data,pcmd_data->in_data_len));
    }
    readsize = sdio_align_size(func,pcmd_data->in_data_len);
    if (unlikely(glue->dump))
    {
        printk(KERN_DEBUG "%s: READ data address[%08x] len[%d] readsize[%d]\n",__FUNCTION__,glue->dataIOPort,(int)pcmd_data->in_data_len,readsize);
        print_hex_dump(KERN_DEBUG, "ssv_sdio: WRITE ",
                       DUMP_PREFIX_OFFSET, 16, 1,
                       tmpdata, pcmd_data->in_data_len, false);
    }
    sdio_claim_host(func);
    ret = sdio_memcpy_toio(func, glue->dataIOPort,tmpdata, readsize);
    sdio_release_host(func);
    if ( ret )
    {
        dev_err(glue->dev,"%s: error : %d",__FUNCTION__,ret);
        retval = -1;
    }
    kfree(tmpdata);
    return retval;
}
static long ssv_sdiobridge_ioctl_getMultiByteRegIOPort(struct ssv_sdiobridge_glue *glue,unsigned int cmd, struct ssv_sdiobridge_cmd *pcmd_data,struct ssv_sdiobridge_cmd *pucmd_data,bool isCompat)
{
    long retval =0;
    if ( pcmd_data->out_data_len < 4)
    {
        retval = -1;
    }
    else
    {
        if ( isCompat )
        {
            CHECK_RET(copy_to_user((int __user *)compat_ptr((unsigned long)pucmd_data->out_data),&glue->regIOPort,sizeof(glue->regIOPort)));
        }
        else
        {
            CHECK_RET(copy_to_user((int __user *)pucmd_data->out_data,&glue->regIOPort,sizeof(glue->regIOPort)));
        }
    }
    return retval;
}
static long ssv_sdiobridge_ioctl_setMultiByteRegIOPort(struct ssv_sdiobridge_glue *glue,unsigned int cmd, struct ssv_sdiobridge_cmd *pcmd_data,struct ssv_sdiobridge_cmd *pucmd_data,bool isCompat)
{
    long retval =0;
    if ( pcmd_data->in_data_len < 4)
    {
        retval = -1;
    }
    else
    {
        if ( isCompat )
        {
            CHECK_RET(copy_from_user(&glue->regIOPort,(int __user *)compat_ptr((unsigned long)pucmd_data->in_data),sizeof(glue->regIOPort)));
        }
        else
        {
            CHECK_RET(copy_from_user(&glue->regIOPort,(int __user *)pucmd_data->in_data,sizeof(glue->regIOPort)));
        }
    }
    return retval;
}
static long ssv_sdiobridge_ioctl_readReg(struct ssv_sdiobridge_glue *glue,unsigned int cmd, struct ssv_sdiobridge_cmd *pcmd_data,struct ssv_sdiobridge_cmd *pucmd_data,bool isCompat)
{
    struct sdio_func *func = dev_to_sdio_func(glue->dev);
    long retval =0;
    if ( pcmd_data->in_data_len < 4 || pcmd_data->out_data_len < 4)
    {
        retval = -1;
    }
    else
    {
        u8 tmpdata[4];
        int ret = 0;
        if ( isCompat )
        {
            CHECK_RET(copy_from_user(tmpdata,(int __user *)compat_ptr((unsigned long)pucmd_data->in_data),sizeof(tmpdata)));
        }
        else
        {
            CHECK_RET(copy_from_user(tmpdata,(int __user *)pucmd_data->in_data,sizeof(tmpdata)));
        }
        sdio_claim_host(func);
        dev_err(glue->dev,"%s: read reg 1 [%02X][%02X][%02X][%02X]\n",__FUNCTION__,tmpdata[0],tmpdata[1],tmpdata[2],tmpdata[3]);
        ret = sdio_memcpy_toio(func, glue->regIOPort, tmpdata, 4);
        ret = sdio_memcpy_fromio(func, tmpdata, glue->regIOPort, 4);
        sdio_release_host(func);
        dev_err(glue->dev,"%s: read reg 2 [%02X][%02X][%02X][%02X] ret:%d\n",__FUNCTION__,tmpdata[0],tmpdata[1],tmpdata[2],tmpdata[3],ret);
        if ( !ret )
        {
            if ( isCompat )
            {
                CHECK_RET(copy_to_user((int __user *)compat_ptr((unsigned long)pucmd_data->out_data),tmpdata,sizeof(tmpdata)));
            }
            else
            {
                CHECK_RET(copy_to_user((int __user *)pucmd_data->out_data,tmpdata,sizeof(tmpdata)));
            }
        }
        else
        {
            dev_err(glue->dev,"%s: error : %d",__FUNCTION__,ret);
            retval = -1;
        }
    }
    return retval;
}
static long ssv_sdiobridge_ioctl_writeReg(struct ssv_sdiobridge_glue *glue,unsigned int cmd, struct ssv_sdiobridge_cmd *pcmd_data,struct ssv_sdiobridge_cmd *pucmd_data,bool isCompat)
{
    struct sdio_func *func = dev_to_sdio_func(glue->dev);
    long retval =0;
    if ( pcmd_data->in_data_len < 8)
    {
        retval = -1;
    }
    else
    {
        u8 tmpdata[8];
        int ret = 0;
        if ( isCompat )
        {
            CHECK_RET(copy_from_user(tmpdata,(int __user *)compat_ptr((unsigned long)pucmd_data->in_data),sizeof(tmpdata)));
        }
        else
        {
            CHECK_RET(copy_from_user(tmpdata,(int __user *)pucmd_data->in_data,sizeof(tmpdata)));
        }
        dev_err(glue->dev,"%s: write reg ADR[%02X%02X%02X%02X] [%02X][%02X][%02X][%02X]\n",__FUNCTION__,tmpdata[3],tmpdata[2],tmpdata[1],tmpdata[0],
            tmpdata[7], tmpdata[6], tmpdata[5], tmpdata[4]);
        sdio_claim_host(func);
        ret = sdio_memcpy_toio(func, glue->regIOPort, tmpdata, 8);
        sdio_release_host(func);
        if ( ret )
        {
            dev_err(glue->dev,"%s: error : %d",__FUNCTION__,ret);
            retval = -1;
        }
    }
    return retval;
}
static int ssv_sdiobridge_device_open(struct inode *inode, struct file *filp)
{
    dev_err(glue->dev,"%s():\n", __FUNCTION__);
    filp->private_data = glue;
    return 0;
}
static int ssv_sdiobridge_device_close(struct inode *inode, struct file *filp)
{
    dev_err(glue->dev,"%s():\n", __FUNCTION__);
    return 0;
}
static long ssv_sdiobridge_device_ioctl_process(struct ssv_sdiobridge_glue *glue, unsigned int cmd, struct ssv_sdiobridge_cmd *pucmd_data,bool isCompat)
{
    struct ssv_sdiobridge_cmd cmd_data;
    long retval=0;
    if ( isCompat )
    {
        CHECK_RET(copy_from_user(&cmd_data,(int __user *)pucmd_data,sizeof(*pucmd_data)));
    }
    else
    {
        CHECK_RET(copy_from_user(&cmd_data,(int __user *)pucmd_data,sizeof(*pucmd_data)));
    }
#if 0
#ifdef __x86_64
    dev_err(glue->dev,"%s: isCompat[%d] [%lX] [%lX] [%X] \n",__FUNCTION__,isCompat,IOCTL_SSVSDIO_GET_FUNCTION_FOCUS,IOCTL_SSVSDIO_READ_DATA,cmd);
#else
    dev_err(glue->dev,"%s: isCompat[%d] [%X] [%X] [%X] \n",__FUNCTION__,isCompat,IOCTL_SSVSDIO_GET_FUNCTION_FOCUS,IOCTL_SSVSDIO_READ_DATA,cmd);
#endif
#endif
    switch (cmd)
    {
        case IOCTL_SSVSDIO_GET_FUNCTION_FOCUS:
            retval = ssv_sdiobridge_ioctl_getFuncfocus(glue,cmd,&cmd_data,pucmd_data,isCompat);
            break;
        case IOCTL_SSVSDIO_SET_FUNCTION_FOCUS:
            retval = ssv_sdiobridge_ioctl_setFuncfocus(glue,cmd,&cmd_data,pucmd_data,isCompat);
            break;
        case IOCTL_SSVSDIO_GET_BUS_WIDTH:
            retval = ssv_sdiobridge_ioctl_getBusWidth(glue,cmd,&cmd_data,pucmd_data,isCompat);
            break;
#if 0
        case IOCTL_SSVSDIO_SET_BUS_WIDTH:
            retval = ssv_sdiobridge_ioctl_setBusWidth(glue,cmd,&cmd_data,pucmd_data,isCompat);
            break;
        case IOCTL_SSVSDIO_GET_BUS_CLOCK:
            break;
        case IOCTL_SSVSDIO_SET_BUS_CLOCK:
            break;
        case IOCTL_SSVSDIO_GET_BLOCK_MODE:
            retval = ssv_sdiobridge_ioctl_getBlockMode(glue,cmd,&cmd_data,pucmd_data,isCompat);
            break;
        case IOCTL_SSVSDIO_SET_BLOCK_MODE:
            retval = ssv_sdiobridge_ioctl_setBlockMode(glue,cmd,&cmd_data,pucmd_data,isCompat);
            break;
#endif
        case IOCTL_SSVSDIO_GET_BLOCKLEN:
            retval = ssv_sdiobridge_ioctl_getBlockSize(glue,cmd,&cmd_data,pucmd_data,isCompat);
            break;
        case IOCTL_SSVSDIO_SET_BLOCKLEN:
            retval = ssv_sdiobridge_ioctl_setBlockSize(glue,cmd,&cmd_data,pucmd_data,isCompat);
            break;
        case IOCTL_SSVSDIO_READ_BYTE:
            retval = ssv_sdiobridge_ioctl_readByte(glue,cmd,&cmd_data,pucmd_data,isCompat);
            break;
        case IOCTL_SSVSDIO_WRITE_BYTE:
            retval = ssv_sdiobridge_ioctl_writeByte(glue,cmd,&cmd_data,pucmd_data,isCompat);
            break;
        case IOCTL_SSVSDIO_GET_MULTI_BYTE_IO_PORT:
            retval = ssv_sdiobridge_ioctl_getMultiByteIOPort(glue,cmd,&cmd_data,pucmd_data,isCompat);
            break;
        case IOCTL_SSVSDIO_SET_MULTI_BYTE_IO_PORT:
            retval = ssv_sdiobridge_ioctl_setMultiByteIOPort(glue,cmd,&cmd_data,pucmd_data,isCompat);
            break;
        case IOCTL_SSVSDIO_READ_MULTI_BYTE:
            retval = ssv_sdiobridge_ioctl_readMultiByte(glue,cmd,&cmd_data,pucmd_data,isCompat);
            break;
        case IOCTL_SSVSDIO_WRITE_MULTI_BYTE:
            retval = ssv_sdiobridge_ioctl_writeMultiByte(glue,cmd,&cmd_data,pucmd_data,isCompat);
            break;
        case IOCTL_SSVSDIO_GET_MULTI_BYTE_REG_IO_PORT:
            retval = ssv_sdiobridge_ioctl_getMultiByteRegIOPort(glue,cmd,&cmd_data,pucmd_data,isCompat);
            break;
        case IOCTL_SSVSDIO_SET_MULTI_BYTE_REG_IO_PORT:
            retval = ssv_sdiobridge_ioctl_setMultiByteRegIOPort(glue,cmd,&cmd_data,pucmd_data,isCompat);
            break;
        case IOCTL_SSVSDIO_READ_REG:
            retval = ssv_sdiobridge_ioctl_readReg(glue,cmd,&cmd_data,pucmd_data,isCompat);
            break;
        case IOCTL_SSVSDIO_WRITE_REG:
            retval = ssv_sdiobridge_ioctl_writeReg(glue,cmd,&cmd_data,pucmd_data,isCompat);
            break;
    }
    return retval;
}
static long ssv_sdiobridge_device_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct ssv_sdiobridge_glue *glue =filp->private_data;
    long retval=0;
    struct ssv_sdiobridge_cmd *pucmd_data;
    pucmd_data = (struct ssv_sdiobridge_cmd *)arg;
    retval = ssv_sdiobridge_device_ioctl_process( glue,cmd,pucmd_data,true);
    return retval;
}
static long ssv_sdiobridge_device_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct ssv_sdiobridge_glue *glue =filp->private_data;
    long retval=0;
    struct ssv_sdiobridge_cmd *pucmd_data;
    pucmd_data = (struct ssv_sdiobridge_cmd *)arg;
    retval = ssv_sdiobridge_device_ioctl_process( glue,cmd,pucmd_data,false);
    return retval;
}
static bool ssv_sdiobridge_have_data(struct ssv_sdiobridge_glue *glue)
{
    dev_err(glue->dev,"%s(): !list_empty(&glue->rxreadybuf)[%d]\n", __FUNCTION__,!list_empty(&glue->rxreadybuf));
    return !list_empty(&glue->rxreadybuf);
}
static ssize_t ssv_sdiobridge_device_read(struct file *filp,
        char *buffer,
        size_t length,
        loff_t *offset)
{
    struct ssv_sdiobridge_glue *glue =filp->private_data;
    struct ssv_rxbuf *bf;
    int copylength;
    dev_err(glue->dev,"%s():\n", __FUNCTION__);
    spin_lock_bh(&glue->rxbuflock);
    if (list_empty(&glue->rxreadybuf))
    {
        spin_unlock_bh(&glue->rxbuflock);
        dev_err(glue->dev,"%s():no data for read \n", __FUNCTION__);
#if 1
        if ( wait_event_interruptible(glue->read_wq, ssv_sdiobridge_have_data(glue))!=0)
        {
            dev_err(glue->dev,"%s():not get data ?? \n", __FUNCTION__);
            return -1;
        }
#else
        wait_event(glue->read_wq,ssv_sdiobridge_have_data(glue));
#endif
        spin_lock_bh(&glue->rxbuflock);
        if (list_empty(&glue->rxreadybuf))
        {
            spin_unlock_bh(&glue->rxbuflock);
            dev_err(glue->dev,"%s():stop ?? \n", __FUNCTION__);
            return -1;
        }
    }
    bf = list_first_entry(&glue->rxreadybuf, struct ssv_rxbuf, list);
    list_del(&bf->list);
    spin_unlock_bh(&glue->rxbuflock);
    copylength = min(bf->rxsize,(u32)length);
    CHECK_RET(copy_to_user((int __user *)buffer,bf->rxdata,copylength));
    dev_err(glue->dev,"%s():get rx data : data len:[%d], user read len:[%d],real read len:[%d] \n", __FUNCTION__,bf->rxsize,(u32)length,copylength);
    spin_lock_bh(&glue->rxbuflock);
    list_add_tail(&bf->list, &glue->rxbuf);
    spin_unlock_bh(&glue->rxbuflock);
    return copylength;
}
static ssize_t ssv_sdiobridge_device_write(struct file *filp,
        const char *buff,
        size_t len,
        loff_t *off)
{
    struct ssv_sdiobridge_glue *glue =filp->private_data;
    struct sdio_func *func = dev_to_sdio_func(glue->dev);
    u8 *tmpdata;
    int ret;
    dev_err(glue->dev,"%s():\n", __FUNCTION__);
    tmpdata = kzalloc(len, GFP_KERNEL);
    if ( tmpdata == NULL )
    {
        dev_err(glue->dev,"%s: error : alloc buf error size:%d",__FUNCTION__,(u32)len);
        return -1;
    }
    CHECK_RET(copy_from_user(tmpdata,(int __user *)buff,len));
    if (unlikely(glue->dump))
    {
        printk(KERN_DEBUG "%s: WRITE data address[%08x] len[%d] readsize[%d]\n",__FUNCTION__,glue->dataIOPort,(int)len,sdio_align_size(func,len));
        print_hex_dump(KERN_DEBUG, "ssv_sdio: WRITE ",
                       DUMP_PREFIX_OFFSET, 16, 1,
                       tmpdata, len, false);
    }
    sdio_claim_host(func);
    ret = sdio_memcpy_toio(func, glue->dataIOPort,tmpdata, sdio_align_size(func,len));
    sdio_release_host(func);
    kfree(tmpdata);
    if ( ret )
    {
        dev_err(glue->dev,"%s: error : %d",__FUNCTION__,ret);
        return -1;
    }
    return len;
}
struct file_operations fops =
{
    .owner = THIS_MODULE,
    .read = ssv_sdiobridge_device_read,
    .write = ssv_sdiobridge_device_write,
    .open = ssv_sdiobridge_device_open,
    .release = ssv_sdiobridge_device_close,
    .compat_ioctl = ssv_sdiobridge_device_compat_ioctl,
    .unlocked_ioctl = ssv_sdiobridge_device_ioctl
};
static void ssv_sdiobridge_irq_process(struct sdio_func *func,
                                       struct ssv_sdiobridge_glue *glue)
{
    int err_ret;
    u8 status;
    sdio_claim_host(func);
    status = sdio_readb(func, REG_INT_STATUS, &err_ret);
    if ( status & SSVCABRIO_INT_RX )
    {
        struct ssv_rxbuf *bf;
        int readsize;
        spin_lock_bh(&glue->rxbuflock);
        if (list_empty(&glue->rxbuf))
        {
            spin_unlock_bh(&glue->rxbuflock);
            sdio_release_host(func);
            dev_err(glue->dev, "ssv_sdiobridge_irq_process no avaible rx buf list??\n");
            return;
        }
        else
        {
            bf = list_first_entry(&glue->rxbuf, struct ssv_rxbuf, list);
            list_del(&bf->list);
        }
        spin_unlock_bh(&glue->rxbuflock);
        bf->rxsize = (int)(sdio_readb(func, REG_CARD_PKT_LEN_0, &err_ret)&0xff);
        dev_err(glue->dev, "sdio read rx size[%08x] 0x10[%02x]\n",bf->rxsize, sdio_readb(func, REG_CARD_PKT_LEN_0, &err_ret)&0xff);
        bf->rxsize = bf->rxsize | ((sdio_readb(func, REG_CARD_PKT_LEN_1, &err_ret)&0xff)<<0x8);
        readsize = sdio_align_size(func,bf->rxsize);
        dev_err(glue->dev, "sdio read rx size[%08x] 0x11[%02x] readsize[%d]\n",bf->rxsize, sdio_readb(func, REG_CARD_PKT_LEN_1, &err_ret)&0xff,readsize);
        err_ret = sdio_memcpy_fromio(func, bf->rxdata, glue->dataIOPort, readsize);
        sdio_release_host(func);
        dev_err(glue->dev, "ssv_sdiobridge_irq_process read 53, %d bytes  ret:[%d]\n", readsize,err_ret );
        if (unlikely(glue->dump))
        {
            printk(KERN_DEBUG "ssv_sdiobridge_irq_process: READ data address[%08x] len[%d] readsize[%d]\n",glue->dataIOPort,(int)bf->rxsize,readsize);
            print_hex_dump(KERN_DEBUG, "ssv_sdio: READ ",
                           DUMP_PREFIX_OFFSET, 16, 1,
                           bf->rxdata, bf->rxsize, false);
        }
        if (WARN_ON(err_ret))
        {
            dev_err(glue->dev, "ssv_sdiobridge_irq_process read failed (%d)\n", err_ret);
            spin_lock_bh(&glue->rxbuflock);
            list_add_tail(&bf->list, &glue->rxbuf);
            spin_unlock_bh(&glue->rxbuflock);
        }
        else
        {
            spin_lock_bh(&glue->rxbuflock);
            list_add_tail(&bf->list, &glue->rxreadybuf);
            wake_up(&glue->read_wq);
            spin_unlock_bh(&glue->rxbuflock);
        }
    }
    else
    {
        sdio_release_host(func);
    }
}
static void ssv_sdiobridge_irq_handler(struct sdio_func *func)
{
    struct ssv_sdiobridge_glue *glue = sdio_get_drvdata(func);
    dev_err(&func->dev, "ssv_sdiobridge_irq_handler\n");
    WARN_ON(glue == NULL);
    if ( glue != NULL )
    {
        atomic_set(&glue->irq_handling, 1);
        ssv_sdiobridge_irq_process(func,glue);
        atomic_set(&glue->irq_handling, 0);
        wake_up(&glue->irq_wq);
    }
}
static void ssv_sdiobridge_irq_enable(struct sdio_func *func,
                                      struct ssv_sdiobridge_glue *glue)
{
    int ret;
    func = dev_to_sdio_func(glue->dev);
    sdio_claim_host(func);
    dev_err(glue->dev, "ssv_sdiobridge_irq_enable\n");
    ret = sdio_claim_irq(func, ssv_sdiobridge_irq_handler);
    if (ret)
        dev_err(glue->dev, "Failed to claim sdio irq: %d\n", ret);
    sdio_release_host(func);
}
static bool ssv_sdiobridge_is_on_irq(struct ssv_sdiobridge_glue *glue)
{
    return !atomic_read(&glue->irq_handling);
}
static void ssv_sdiobridge_irq_disable(struct ssv_sdiobridge_glue *glue,bool iswaitirq)
{
    struct sdio_func *func;
    int ret;
    dev_err(glue->dev, "ssv_sdiobridge_irq_disable1\n");
    if ( glue != NULL )
    {
        func = dev_to_sdio_func(glue->dev);
        sdio_claim_host(func);
        dev_err(glue->dev, "ssv_sdiobridge_irq_disable2 [%d]\n",atomic_read(&glue->irq_handling));
        if (atomic_read(&glue->irq_handling)&&iswaitirq)
        {
            dev_err(glue->dev, "ssv_sdiobridge_irq_disable3\n");
            sdio_release_host(func);
            ret = wait_event_interruptible(glue->irq_wq,
                                           ssv_sdiobridge_is_on_irq(glue));
            dev_err(glue->dev, "ssv_sdiobridge_irq_disable4 ret[%d]\n",ret);
            if (ret)
                return;
            sdio_claim_host(func);
        }
        dev_err(glue->dev, "ssv_sdiobridge_irq_disable5\n");
        ret = sdio_release_irq(func);
        if (ret)
            dev_err(glue->dev, "Failed to release sdio irq: %d\n", ret);
        dev_err(glue->dev, "ssv_sdiobridge_irq_disable6\n");
        sdio_release_host(func);
    }
}
#if 0
static void ssv_sdiobridge_irq_sync(struct device *child)
{
    struct ssv_sdiobridge_glue *glue = dev_get_drvdata(child->parent);
    struct sdio_func *func;
    int ret;
    if ( glue != NULL )
    {
        func = dev_to_sdio_func(glue->dev);
        sdio_claim_host(func);
        if (atomic_read(&glue->irq_handling))
        {
            sdio_release_host(func);
            ret = wait_event_interruptible(glue->irq_wq,
                                           ssv_sdiobridge_is_on_irq(glue));
            if (ret)
                return;
            sdio_claim_host(func);
        }
        sdio_release_host(func);
    }
}
#endif
static void ssv_sdiobridge_read_parameter(struct sdio_func *func,
        struct ssv_sdiobridge_glue *glue)
{
    int err_ret;
    sdio_claim_host(func);
    glue->dataIOPort = 0;
    glue->dataIOPort = glue->dataIOPort | (sdio_readb(func, REG_DATA_IO_PORT_0, &err_ret) << ( 8*0 ));
    glue->dataIOPort = glue->dataIOPort | (sdio_readb(func, REG_DATA_IO_PORT_1, &err_ret) << ( 8*1 ));
    glue->dataIOPort = glue->dataIOPort | (sdio_readb(func, REG_DATA_IO_PORT_2, &err_ret) << ( 8*2 ));
    glue->regIOPort = 0;
    glue->regIOPort = glue->regIOPort | (sdio_readb(func, REG_REG_IO_PORT_0, &err_ret) << ( 8*0 ));
    glue->regIOPort = glue->regIOPort | (sdio_readb(func, REG_REG_IO_PORT_1, &err_ret) << ( 8*1 ));
    glue->regIOPort = glue->regIOPort | (sdio_readb(func, REG_REG_IO_PORT_2, &err_ret) << ( 8*2 ));
    dev_err(&func->dev, "dataIOPort 0x%x regIOPort 0x%x [%lx]\n",
            glue->dataIOPort,glue->regIOPort,(long unsigned int)IOCTL_SSVSDIO_GET_BLOCKLEN);
    sdio_set_block_size(func,glue->blockSize);
    sdio_release_host(func);
}
static int ssv_sdiobridge_init_buf(struct ssv_sdiobridge_glue *glue)
{
    u32 bsize,i,error;
    struct ssv_rxbuf *bf;
    init_waitqueue_head(&glue->read_wq);
    spin_lock_init(&glue->rxbuflock);
    INIT_LIST_HEAD(&glue->rxbuf);
    INIT_LIST_HEAD(&glue->rxreadybuf);
    bsize = sizeof(struct ssv_rxbuf) * RXBUFSIZE;
    glue->bufaddr = kzalloc(bsize, GFP_KERNEL);
    if (glue->bufaddr == NULL)
    {
        error = -ENOMEM;
        goto fail;
    }
    bf = glue->bufaddr;
    for (i = 0; i < RXBUFSIZE; i++, bf++)
    {
        list_add_tail(&bf->list, &glue->rxbuf);
    }
    return 0;
fail:
    return error;
}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,0)
static char *ssv_sdiobridge_devnode(struct device *dev, umode_t *mode)
#else
static char *ssv_sdiobridge_devnode(struct device *dev, mode_t *mode)
#endif
{
    if (!mode)
        return NULL;
    *mode = 0666;
    return NULL;
}
extern int ssv_devicetype;
static int __devinit ssv_sdiobridge_probe(struct sdio_func *func,
        const struct sdio_device_id *id)
{
    mmc_pm_flag_t mmcflags;
    int ret = -ENOMEM;
    dev_t dev;
    int alloc_ret = 0;
    int cdev_ret = 0;
    int err_ret;
    if (ssv_devicetype != 1) {
        printk(KERN_INFO "Not using SSV6200 bridge SDIO driver.\n");
        return -ENODEV;
    }
    printk(KERN_INFO "=======================================\n");
    printk(KERN_INFO "==           RUN SDIO BRIDGE         ==\n");
    printk(KERN_INFO "=======================================\n");
    printk(KERN_INFO "ssv_sdiobridge_probe func->num:%d",func->num);
    if (func->num != 0x01)
        return -ENODEV;
    glue = kzalloc(sizeof(*glue), GFP_KERNEL);
    if (!glue)
    {
        dev_err(&func->dev, "can't allocate glue\n");
        goto out;
    }
    glue->blockMode = false;
    glue->blockSize = BLOCKSIZE;
    glue->autoAckInt = true;
    glue->dump = false;
    glue->funcFocus = 1;
    ssv_sdiobridge_init_buf(glue);
    glue->dev = &func->dev;
    func->card->quirks |= MMC_QUIRK_LENIENT_FN0;
    func->card->quirks |= MMC_QUIRK_BLKSZ_FOR_BYTE_MODE;
    mmcflags = sdio_get_host_pm_caps(func);
    dev_err(glue->dev, "sdio PM caps = 0x%x\n", mmcflags);
    sdio_set_drvdata(func, glue);
    pm_runtime_put_noidle(&func->dev);
    ssv_sdiobridge_read_parameter(func,glue);
    dev = MKDEV(ssv_sdiobridge_ioctl_major, 0);
    alloc_ret = alloc_chrdev_region(&dev, 0, num_of_dev, FILE_DEVICE_SSVSDIO_NAME);
    if (alloc_ret)
        goto error;
    ssv_sdiobridge_ioctl_major = MAJOR(dev);
    cdev_init(&ssv_sdiobridge_ioctl_cdev, &fops);
    cdev_ret = cdev_add(&ssv_sdiobridge_ioctl_cdev, dev, num_of_dev);
    if (cdev_ret)
        goto error;
    fc=class_create(THIS_MODULE, FILE_DEVICE_SSVSDIO_NAME);
    fc->devnode = ssv_sdiobridge_devnode;
    device_create(fc, NULL, dev, NULL, "%s", FILE_DEVICE_SSVSDIO_NAME);
    dev_err(glue->dev, "%s driver(major: %d) installed.\n", FILE_DEVICE_SSVSDIO_NAME, ssv_sdiobridge_ioctl_major);
    init_waitqueue_head(&glue->irq_wq);
    ssv_sdiobridge_irq_enable(func,glue);
    sdio_claim_host(func);
#ifdef CONFIG_SSV_SDIO_EXT_INT
    sdio_writeb(func,(~(SSVCABRIO_INT_RX)|SSVCABRIO_INT_GPIO)&0x7, 0x04, &err_ret);
#else
    sdio_writeb(func,(~(SSVCABRIO_INT_RX|SSVCABRIO_INT_GPIO))&0x7, 0x04, &err_ret);
#endif
    sdio_release_host(func);
    return 0;
error:
    if (cdev_ret == 0)
        cdev_del(&ssv_sdiobridge_ioctl_cdev);
    if (alloc_ret == 0)
        unregister_chrdev_region(dev, num_of_dev);
    kfree(glue);
out:
    return ret;
}
static void __devexit ssv_sdiobridge_remove(struct sdio_func *func)
{
    struct ssv_sdiobridge_glue *glue = sdio_get_drvdata(func);
    dev_t dev;
    int err_ret;
    sdio_claim_host(func);
#ifdef CONFIG_SSV_SDIO_EXT_INT
    sdio_writeb(func,0, 0x04, &err_ret);
#else
    sdio_writeb(func,SSVCABRIO_INT_GPIO, 0x04, &err_ret);
#endif
    sdio_release_host(func);
    ssv_sdiobridge_irq_disable(glue,false);
    dev = MKDEV(ssv_sdiobridge_ioctl_major, 0);
    device_destroy(fc,dev);
    class_destroy(fc);
    cdev_del(&ssv_sdiobridge_ioctl_cdev);
    unregister_chrdev_region(dev, num_of_dev);
    pm_runtime_get_noresume(&func->dev);
    if ( glue )
    {
        dev_err(glue->dev, "ssv_sdiobridge_remove");
        if (glue->bufaddr)
        {
            kfree(glue->bufaddr);
        }
        kfree(glue);
        glue = NULL;
    }
    sdio_set_drvdata(func, NULL);
}
#ifdef CONFIG_PM
static int ssv_sdiobridge_suspend(struct device *dev)
{
    int ret = 0;
    return ret;
}
static int ssv_sdiobridge_resume(struct device *dev)
{
    dev_dbg(dev, "ssvcabrio resume\n");
    return 0;
}
static const struct dev_pm_ops ssv_sdiobridge_pm_ops =
{
    .suspend = ssv_sdiobridge_suspend,
    .resume = ssv_sdiobridge_resume,
};
#endif
static struct sdio_driver ssv_sdio_bridge_driver =
{
    .name = "ssv_sdio_bridge",
    .id_table = ssv_sdiobridge_devices,
    .probe = ssv_sdiobridge_probe,
    .remove = __devexit_p(ssv_sdiobridge_remove),
#ifdef CONFIG_PM
    .drv = {
        .pm = &ssv_sdiobridge_pm_ops,
    },
#endif
};
EXPORT_SYMBOL(ssv_sdio_bridge_driver);
#if 1
static int __init ssv_sdiobridge_init(void)
{
    printk(KERN_INFO "ssv_sdiobridge_init\n");
    return sdio_register_driver(&ssv_sdio_bridge_driver);
}
static void __exit ssv_sdiobridge_exit(void)
{
    printk(KERN_INFO "ssv_sdiobridge_exit\n");
    sdio_unregister_driver(&ssv_sdio_bridge_driver);
}
module_init(ssv_sdiobridge_init);
module_exit(ssv_sdiobridge_exit);
#endif
MODULE_LICENSE("GPL");
MODULE_AUTHOR("iComm Semiconductor Co., Ltd");
