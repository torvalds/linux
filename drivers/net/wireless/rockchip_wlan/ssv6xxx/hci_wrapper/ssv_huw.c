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

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/wait.h>
#include <linux/netdevice.h>
#include <hci/ssv_hci.h>
#include <hwif/hwif.h>
#include "ssv_huw.h"
#define SSV6200_ID_NUMBER (128)
#define BLOCKSIZE 0x40
#define RXBUFLENGTH 1024*3
#define RXBUFSIZE 512
#define CHECK_RET(_fun) \
 do { \
  if (0 != _fun) \
   printk("File = %s\nLine = %d\nFunc=%s\nDate=%s\nTime=%s\n", __FILE__, __LINE__, __FUNCTION__, __DATE__, __TIME__); \
 } while (0)
#define SMAC_SRAM_WRITE(_s,_r,_v,_sz) \
    (_s)->hci.hci_ops->hci_write_sram(_r, _v, _sz)
#define SMAC_REG_WRITE(_s,_r,_v) \
    (_s)->hci.hci_ops->hci_write_word(_r, _v)
#define SMAC_REG_READ(_s,_r,_v) \
    (_s)->hci.hci_ops->hci_read_word(_r, _v)
#define HCI_START(_sh) \
    (_sh)->hci.hci_ops->hci_start()
#define HCI_STOP(_sh) \
    (_sh)->hci.hci_ops->hci_stop()
#define HCI_SEND(_sh,_sk,_q) \
    (_sh)->hci.hci_ops->hci_tx(_sk, _q, HCI_FLAGS_NO_FLOWCTRL)
#define HCI_PAUSE(_sh,_mk) \
    (_sh)->hci.hci_ops->hci_tx_pause(_mk)
#define HCI_RESUME(_sh,_mk) \
    (_sh)->hci.hci_ops->hci_tx_resume(_mk)
#define HCI_TXQ_FLUSH(_sh,_mk) \
    (_sh)->hci.hci_ops->hci_txq_flush(_mk)
#define HCI_TXQ_FLUSH_BY_STA(_sh,_aid) \
  (_sh)->hci.hci_ops->hci_txq_flush_by_sta(_aid)
#define HCI_TXQ_EMPTY(_sh,_txqid) \
  (_sh)->hci.hci_ops->hci_txq_empty(_txqid)
#define HCI_WAKEUP_PMU(_sh) \
    (_sh)->hci.hci_ops->hci_pmu_wakeup()
#define HCI_SEND_CMD(_sh,_sk) \
        (_sh)->hci.hci_ops->hci_send_cmd(_sk)
struct ssv_huw_dev {
    struct device *dev;
    struct ssv6xxx_platform_data *priv;
    struct ssv6xxx_hci_info hci;
    char chip_id[24];
    u64 chip_tag;
    u8 funcFocus;
    wait_queue_head_t read_wq;
    spinlock_t rxlock;
    void *bufaddr;
 struct sk_buff_head rx_skb_q;
};
struct ssv_rxbuf
{
    struct list_head list;
    u32 rxsize;
    u8 rxdata[RXBUFLENGTH];
};
struct ssv_huw_dev g_huw_dev;
static unsigned int ssv_sdiobridge_ioctl_major = 0;
static unsigned int num_of_dev = 1;
static struct cdev ssv_sdiobridge_ioctl_cdev;
static struct class *fc;
#if !defined(USE_THREAD_RX) || defined(USE_BATCH_RX)
int ssv_huw_rx(struct sk_buff_head *rx_skb_q, void *args)
#else
int ssv_huw_rx(struct sk_buff *rx_skb, void *args)
#endif
{
    struct ssv_huw_dev *phuw_dev = (struct ssv_huw_dev *)args;
    unsigned long flags;
    spin_lock_irqsave(&phuw_dev->rxlock, flags);
#if !defined(USE_THREAD_RX) || defined(USE_BATCH_RX)
    while (skb_queue_len(rx_skb_q))
        __skb_queue_tail(&phuw_dev->rx_skb_q, __skb_dequeue(rx_skb_q));
#else
    __skb_queue_tail(&phuw_dev->rx_skb_q, rx_skb);
#endif
    spin_unlock_irqrestore(&phuw_dev->rxlock, flags);
    wake_up_interruptible(&phuw_dev->read_wq);
    return 0;
}
void ssv_huw_txbuf_free_skb(struct sk_buff *skb, void *args)
{
    if (!skb)
        return;
    dev_kfree_skb_any(skb);
}
unsigned int skb_queue_len_bhsafe(struct sk_buff_head *head, spinlock_t *plock)
{
    unsigned int len = 0;
    spin_lock_bh(plock);
    len = skb_queue_len(head);
    spin_unlock_bh(plock);
    return len;
}
static long ssv_huw_ioctl_readReg(struct ssv_huw_dev *phuw_dev,unsigned int cmd, struct ssv_huw_cmd *pcmd_data,struct ssv_huw_cmd *pucmd_data,bool isCompat)
{
    long retval =0;
    if ( pcmd_data->in_data_len < 4 || pcmd_data->out_data_len < 4)
    {
        retval = -1;
    }
    else
    {
        u32 tmpdata;
        u32 regval;
        int ret = 0;
#ifdef CONFIG_COMPAT
        if ( isCompat )
        {
            CHECK_RET(copy_from_user(&tmpdata,(int __user *)compat_ptr((unsigned long)pucmd_data->in_data),sizeof(tmpdata)));
        }
        else
#endif
        {
            CHECK_RET(copy_from_user(&tmpdata,(int __user *)pucmd_data->in_data,sizeof(tmpdata)));
        }
        ret = SMAC_REG_READ(phuw_dev, tmpdata, &regval);
        if ( !ret )
        {
#ifdef CONFIG_COMPAT
            if ( isCompat )
            {
                CHECK_RET(copy_to_user((int __user *)compat_ptr((unsigned long)pucmd_data->out_data),&regval,sizeof(regval)));
            }
            else
#endif
            {
                CHECK_RET(copy_to_user((int __user *)pucmd_data->out_data,&regval,sizeof(regval)));
            }
        }
        else
        {
            dev_err(phuw_dev->dev,"%s: error : %d",__FUNCTION__,ret);
            retval = -1;
        }
    }
    return retval;
}
static long ssv_huw_ioctl_writeReg(struct ssv_huw_dev *phuw_dev,unsigned int cmd, struct ssv_huw_cmd *pcmd_data,struct ssv_huw_cmd *pucmd_data,bool isCompat)
{
    long retval =0;
    if ( pcmd_data->in_data_len < 8)
    {
        retval = -1;
    }
    else
    {
        u32 tmpdata[2];
        int ret = 0;
#ifdef CONFIG_COMPAT
        if ( isCompat )
        {
            CHECK_RET(copy_from_user(&tmpdata,(int __user *)compat_ptr((unsigned long)pucmd_data->in_data),sizeof(tmpdata)));
        }
        else
#endif
        {
            CHECK_RET(copy_from_user(&tmpdata,(int __user *)pucmd_data->in_data,sizeof(tmpdata)));
        }
        SMAC_REG_WRITE(phuw_dev, tmpdata[0], tmpdata[1]);
        if ( ret )
        {
            dev_err(phuw_dev->dev,"%s: error : %d",__FUNCTION__,ret);
            retval = -1;
        }
    }
    return retval;
}
static long ssv_huw_ioctl_writeSram(struct ssv_huw_dev *phuw_dev,unsigned int cmd, struct ssv_huw_cmd *pcmd_data,struct ssv_huw_cmd *pucmd_data,bool isCompat)
{
    long retval =0;
    unsigned char *ptr = NULL;
    unsigned int addr;
    if (( pcmd_data->in_data_len != 4) || ( pcmd_data->out_data_len <= 0))
    {
        retval = -1;
    }
    else
    {
        int ret = 0;
        ptr = kzalloc(pcmd_data->out_data_len, GFP_KERNEL);
        if(ptr == NULL)
            return -ENOMEM;
#ifdef CONFIG_COMPAT
        if ( isCompat )
        {
            CHECK_RET(copy_from_user(&addr, (int __user *)compat_ptr((unsigned long)pucmd_data->in_data), sizeof(addr)));
            CHECK_RET(copy_from_user(ptr, (int __user *)compat_ptr((unsigned long)pucmd_data->out_data), pcmd_data->out_data_len));
        }
        else
#endif
        {
            CHECK_RET(copy_from_user(&addr, (int __user *)pucmd_data->in_data, sizeof(addr)));
            CHECK_RET(copy_from_user(ptr, (int __user *)pucmd_data->out_data, pcmd_data->out_data_len));
        }
        SMAC_SRAM_WRITE(phuw_dev, addr, ptr, pcmd_data->out_data_len);
        if ( ret )
        {
            dev_err(phuw_dev->dev,"%s: error : %d",__FUNCTION__,ret);
            retval = -1;
        }
        kfree(ptr);
    }
    return retval;
}
static long ssv_huw_ioctl_process(struct ssv_huw_dev *glue, unsigned int cmd, struct ssv_huw_cmd *pucmd_data, bool isCompat)
{
    struct ssv_huw_cmd cmd_data;
    long retval=0;
    if ( isCompat )
    {
        CHECK_RET(copy_from_user(&cmd_data,(int __user *)pucmd_data,sizeof(*pucmd_data)));
    }
    else
    {
        CHECK_RET(copy_from_user(&cmd_data,(int __user *)pucmd_data,sizeof(*pucmd_data)));
    }
    switch (cmd)
    {
        case IOCTL_SSVSDIO_READ_REG:
            retval = ssv_huw_ioctl_readReg(glue,cmd,&cmd_data,pucmd_data,isCompat);
            break;
        case IOCTL_SSVSDIO_WRITE_REG:
            retval = ssv_huw_ioctl_writeReg(glue,cmd,&cmd_data,pucmd_data,isCompat);
            break;
        case IOCTL_SSVSDIO_WRITE_SRAM:
            retval = ssv_huw_ioctl_writeSram(glue,cmd,&cmd_data,pucmd_data,isCompat);
            break;
        case IOCTL_SSVSDIO_START:
            retval = HCI_START(glue);
            break;
        case IOCTL_SSVSDIO_STOP:
            retval = HCI_STOP(glue);
            break;
        default:
            return -EINVAL;
    }
    return retval;
}
#ifdef CONFIG_COMPAT
static long ssv_huw_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    long retval=0;
    struct ssv_huw_cmd *pucmd_data;
    pucmd_data = (struct ssv_huw_cmd *)arg;
    retval = ssv_huw_ioctl_process(&g_huw_dev, cmd, pucmd_data, true);
    return retval;
}
#endif
static long ssv_huw_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    long retval=0;
    struct ssv_huw_cmd *pucmd_data;
    pucmd_data = (struct ssv_huw_cmd *)arg;
    retval = ssv_huw_ioctl_process( &g_huw_dev,cmd,pucmd_data,false);
    return retval;
}
static int ssv_huw_open(struct inode *inode, struct file *fp)
{
    fp->private_data = &g_huw_dev;
    return 0;
}
static int ssv_huw_release(struct inode *inode, struct file *fp)
{
    struct sk_buff *skb = NULL;
    while((skb = skb_dequeue(&(g_huw_dev.rx_skb_q))) != NULL)
    {
        dev_kfree_skb_any(skb);
        skb = NULL;
    }
    return 0;
}
static ssize_t ssv_huw_read(struct file *fp, char __user * buf, size_t length, loff_t * offset)
{
    int ret = 0, copy_length = 0;
    struct sk_buff *skb = NULL;
    if (skb_queue_len_bhsafe(&(g_huw_dev.rx_skb_q), &(g_huw_dev.rxlock)) == 0)
    {
        ret = wait_event_interruptible((g_huw_dev.read_wq), (skb_queue_len_bhsafe(&(g_huw_dev.rx_skb_q), &(g_huw_dev.rxlock)) != 0));
        if (ret != 0)
            return -1;
    }
    spin_lock_bh(&(g_huw_dev.rxlock));
    if (skb_queue_len(&(g_huw_dev.rx_skb_q)) > 0)
        skb = skb_dequeue(&(g_huw_dev.rx_skb_q));
    spin_unlock_bh(&(g_huw_dev.rxlock));
    if (skb != NULL)
    {
        copy_length = min(skb->len,(u32)length);
        CHECK_RET(copy_to_user((int __user *)buf, skb->data, copy_length));
        dev_kfree_skb_any(skb);
    }
    return copy_length;
}
static ssize_t ssv_huw_write(struct file *fp, const char __user * buf, size_t length, loff_t * offset)
{
    struct sk_buff *skb;
    unsigned int len = (unsigned int)length;
    len = (len & 0x1f)?(((len>>5) + 1)<<5):len;
    skb = __dev_alloc_skb(len, GFP_KERNEL);
    if (skb == NULL)
    {
        dev_err(g_huw_dev.dev,"%s: error : alloc buf error size:%d",__FUNCTION__,(u32)len);
        return -ENOMEM;
    }
    CHECK_RET(copy_from_user(skb->data, (int __user *)buf, length));
    skb_put(skb, length);
    HCI_SEND(&g_huw_dev, skb, 1);
    return length;
}
void ssv_huw_tx_cb(struct sk_buff_head *skb_head, void *args)
{
    struct sk_buff *skb = NULL;
    while ((skb=skb_dequeue(skb_head)))
    {
        dev_kfree_skb_any(skb);
        skb = NULL;
    }
}
int ssv_huw_read_hci_info(struct ssv_huw_dev *phuw_dev)
{
    struct ssv6xxx_hci_info *pinfo = &(phuw_dev->hci);
    pinfo->hci_ops = NULL;
    pinfo->dev = phuw_dev->dev;
    pinfo->hci_rx_cb = ssv_huw_rx;
    pinfo->rx_cb_args = (void *)phuw_dev;
    pinfo->hci_tx_cb= ssv_huw_tx_cb;
    pinfo->tx_cb_args = NULL;
    pinfo->hci_skb_update_cb = NULL;
    pinfo->skb_update_args = NULL;
    pinfo->hci_tx_flow_ctrl_cb = NULL;
    pinfo->tx_fctrl_cb_args = NULL;
    pinfo->hci_tx_q_empty_cb = NULL;
    pinfo->tx_q_empty_args = NULL;
    pinfo->hci_tx_buf_free_cb = ssv_huw_txbuf_free_skb;
    pinfo->tx_buf_free_args = NULL;
    pinfo->if_ops = phuw_dev->priv->ops;
    return 0;
}
struct file_operations s_huw_ops =
{
    .read = ssv_huw_read,
    .write = ssv_huw_write,
    .unlocked_ioctl = ssv_huw_ioctl,
#ifdef CONFIG_COMPAT
    .compat_ioctl = ssv_huw_compat_ioctl,
#endif
    .open = ssv_huw_open,
    .release = ssv_huw_release,
};
static int ssv_huw_init_buf(struct ssv_huw_dev *hdev)
{
    init_waitqueue_head(&hdev->read_wq);
    spin_lock_init(&hdev->rxlock);
    skb_queue_head_init(&(hdev->rx_skb_q));
    return 0;
}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,0)
static char *ssv_huw_devnode(struct device *dev, umode_t *mode)
#else
static char *ssv_huw_devnode(struct device *dev, mode_t *mode)
#endif
{
    if (!mode)
        return NULL;
    *mode = 0666;
    return NULL;
}
int ssv_huw_probe(struct platform_device *pdev)
{
    dev_t dev;
    int alloc_ret = 0;
    int cdev_ret = 0;
    if (!pdev->dev.platform_data) {
        dev_err(&pdev->dev, "no platform data specified!\n");
        return -EINVAL;
    }
    ssv_huw_init_buf(&g_huw_dev);
    g_huw_dev.priv = (pdev->dev.platform_data);
    g_huw_dev.dev = &(pdev->dev);
    ssv_huw_read_hci_info(&g_huw_dev);
    ssv6xxx_hci_register(&(g_huw_dev.hci));
    dev = MKDEV(ssv_sdiobridge_ioctl_major, 0);
    alloc_ret = alloc_chrdev_region(&dev, 0, num_of_dev, FILE_DEVICE_SSVSDIO_NAME);
    if (alloc_ret)
        goto error;
    ssv_sdiobridge_ioctl_major = MAJOR(dev);
    cdev_init(&ssv_sdiobridge_ioctl_cdev, &s_huw_ops);
    cdev_ret = cdev_add(&ssv_sdiobridge_ioctl_cdev, dev, num_of_dev);
    if (cdev_ret)
        goto error;
    fc=class_create(THIS_MODULE, FILE_DEVICE_SSVSDIO_NAME);
    fc->devnode = ssv_huw_devnode;
    device_create(fc,NULL,dev,NULL,"%s",FILE_DEVICE_SSVSDIO_NAME);
    dev_err(&pdev->dev, "%s driver(major: %d) installed.\n", FILE_DEVICE_SSVSDIO_NAME, ssv_sdiobridge_ioctl_major);
    return 0;
error:
    if (cdev_ret == 0)
        cdev_del(&ssv_sdiobridge_ioctl_cdev);
    if (alloc_ret == 0)
        unregister_chrdev_region(dev, num_of_dev);
    return -ENODEV;
}
EXPORT_SYMBOL(ssv_huw_probe);
int ssv_huw_remove(struct platform_device *pdev)
{
    dev_t dev;
    int ret = 0;
    ssv6xxx_hci_deregister();
    memset(&g_huw_dev, 0 , sizeof(g_huw_dev));
    dev = MKDEV(ssv_sdiobridge_ioctl_major, 0);
    device_destroy(fc,dev);
    class_destroy(fc);
    cdev_del(&ssv_sdiobridge_ioctl_cdev);
    unregister_chrdev_region(dev, num_of_dev);
    return ret;
}
EXPORT_SYMBOL(ssv_huw_remove);
static const struct platform_device_id huw_id_table[] = {
    {
        .name = "ssv6200",
        .driver_data = 0x00,
    },
    {},
};
MODULE_DEVICE_TABLE(platform, huw_id_table);
static struct platform_driver ssv_huw_driver =
{
    .probe = ssv_huw_probe,
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,7,0)
    .remove = __devexit_p(ssv_huw_remove),
#else
    .remove = ssv_huw_remove,
#endif
    .id_table = huw_id_table,
    .driver = {
        .name = "SSV WLAN driver",
        .owner = THIS_MODULE,
    }
};
#if (defined(CONFIG_SSV_SUPPORT_ANDROID)||defined(CONFIG_SSV_BUILD_AS_ONE_KO))
int ssv_huw_init(void)
#else
static int __init ssv_huw_init(void)
#endif
{
    int ret;
    memset(&g_huw_dev, 0 , sizeof(g_huw_dev));
    ret = platform_driver_register(&ssv_huw_driver);
    if (ret < 0)
    {
        printk(KERN_ALERT "[HCI user-space wrapper]: Fail to register huw\n");
    }
    return ret;
}
#if (defined(CONFIG_SSV_SUPPORT_ANDROID)||defined(CONFIG_SSV_BUILD_AS_ONE_KO))
void ssv_huw_exit(void)
#else
static void __exit ssv_huw_exit(void)
#endif
{
    platform_driver_unregister(&ssv_huw_driver);
}
#if (defined(CONFIG_SSV_SUPPORT_ANDROID)||defined(CONFIG_SSV_BUILD_AS_ONE_KO))
EXPORT_SYMBOL(ssv_huw_init);
EXPORT_SYMBOL(ssv_huw_exit);
#else
module_init(ssv_huw_init);
module_exit(ssv_huw_exit);
#endif
MODULE_LICENSE("GPL");
