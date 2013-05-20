#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/sched.h>
#include <asm/current.h>
#include <asm/uaccess.h>
#include <linux/fcntl.h>
#include <linux/poll.h>
#include <linux/time.h>
#include <linux/delay.h>
#include "stp_exp.h"
#include "wmt_exp.h"

MODULE_LICENSE("Dual BSD/GPL");

#define BT_DRIVER_NAME "mtk_stp_BT_chrdev"
#define BT_DEV_MAJOR 192 // never used number

#define PFX                         "[MTK-BT] "
#define BT_LOG_DBG                  3
#define BT_LOG_INFO                 2
#define BT_LOG_WARN                 1
#define BT_LOG_ERR                  0

#define COMBO_IOC_BT_HWVER           6

#define COMBO_IOC_MAGIC        0xb0
#define COMBO_IOCTL_FW_ASSERT  _IOWR(COMBO_IOC_MAGIC, 0, void*)

unsigned int gDbgLevel = BT_LOG_INFO;

#define BT_DBG_FUNC(fmt, arg...)    if(gDbgLevel >= BT_LOG_DBG){ printk(PFX "%s: "  fmt, __FUNCTION__ ,##arg);}
#define BT_INFO_FUNC(fmt, arg...)   if(gDbgLevel >= BT_LOG_INFO){ printk(PFX "%s: "  fmt, __FUNCTION__ ,##arg);}
#define BT_WARN_FUNC(fmt, arg...)   if(gDbgLevel >= BT_LOG_WARN){ printk(PFX "%s: "  fmt, __FUNCTION__ ,##arg);}
#define BT_ERR_FUNC(fmt, arg...)    if(gDbgLevel >= BT_LOG_ERR){ printk(PFX "%s: "   fmt, __FUNCTION__ ,##arg);}
#define BT_TRC_FUNC(f)              if(gDbgLevel >= BT_LOG_DBG){printk(PFX "<%s> <%d>\n", __FUNCTION__, __LINE__);}

#define VERSION "1.0"
#define BT_NVRAM_CUSTOM_NAME "/data/BT_Addr"

static int BT_devs = 1;        /* device count */
static int BT_major = BT_DEV_MAJOR;       /* dynamic allocation */
module_param(BT_major, uint, 0);
static struct cdev BT_cdev;

static unsigned char i_buf[MTKSTP_BUFFER_SIZE];    // input buffer of read()
static unsigned char o_buf[MTKSTP_BUFFER_SIZE];    // output buffer of write()
static struct semaphore wr_mtx, rd_mtx;
static wait_queue_head_t inq;    /* read queues */
static DECLARE_WAIT_QUEUE_HEAD(BT_wq);
static int flag = 0;
volatile int retflag = 0;

unsigned char g_bt_bd_addr[10]={0x01,0x1a,0xfc,0x06,0x00,0x55,0x66,0x77,0x88,0x00};
unsigned char g_nvram_btdata[8];

static int nvram_read(char *filename, char *buf, ssize_t len, int offset)
{
    struct file *fd;
    //ssize_t ret;
    int retLen = -1;

    mm_segment_t old_fs = get_fs();
    set_fs(KERNEL_DS);

    fd = filp_open(filename, O_WRONLY|O_CREAT, 0644);

    if(IS_ERR(fd)) {
        BT_ERR_FUNC("failed to open!!\n");
        return -1;
    }
    do{
        if ((fd->f_op == NULL) || (fd->f_op->read == NULL))
            {
            BT_ERR_FUNC("file can not be read!!\n");
            break;
            }

        if (fd->f_pos != offset) {
            if (fd->f_op->llseek) {
                    if(fd->f_op->llseek(fd, offset, 0) != offset) {
                        BT_ERR_FUNC("[nvram_read] : failed to seek!!\n");
                        break;
                    }
              } else {
                    fd->f_pos = offset;
              }
        }

            retLen = fd->f_op->read(fd,
                                          buf,
                                          len,
                                          &fd->f_pos);

    }while(false);

    filp_close(fd, NULL);

    set_fs(old_fs);

    return retLen;
}


int platform_load_nvram_data( char * filename, char * buf, int len)
{
    //int ret;
    BT_INFO_FUNC("platform_load_nvram_data ++ BDADDR\n");

    return nvram_read( filename, buf, len, 0);
}

static void bt_cdev_rst_cb(
    ENUM_WMTDRV_TYPE_T src,
    ENUM_WMTDRV_TYPE_T dst,
    ENUM_WMTMSG_TYPE_T type,
    void *buf,
    unsigned int sz){

    /*
        To handle reset procedure please
    */
    ENUM_WMTRSTMSG_TYPE_T rst_msg;

    BT_INFO_FUNC("sizeof(ENUM_WMTRSTMSG_TYPE_T) = %d\n", sizeof(ENUM_WMTRSTMSG_TYPE_T));
    if(sz <= sizeof(ENUM_WMTRSTMSG_TYPE_T)){
        memcpy((char *)&rst_msg, (char *)buf, sz);
        BT_INFO_FUNC("src = %d, dst = %d, type = %d, buf = 0x%x sz = %d, max = %d\n", src, dst, type, rst_msg, sz, WMTRSTMSG_RESET_MAX);
        if((src == WMTDRV_TYPE_WMT) &&
            (dst == WMTDRV_TYPE_BT) &&
                (type == WMTMSG_TYPE_RESET)){
                    if(rst_msg == WMTRSTMSG_RESET_START){
                        BT_INFO_FUNC("BT restart start!\n");
                        retflag = 1;
                        wake_up_interruptible(&inq);
                        /*reset_start message handling*/

                    } else if(rst_msg == WMTRSTMSG_RESET_END){
                        BT_INFO_FUNC("BT restart end!\n");
                        retflag = 2;
                        wake_up_interruptible(&inq);
                        /*reset_end message handling*/
                    }
        }
    } else {
        /*message format invalid*/
    BT_INFO_FUNC("message format invalid!\n");
    }
}

void BT_event_cb(void)
{
    BT_DBG_FUNC("BT_event_cb() \n");

    flag = 1;
    wake_up(&BT_wq);

    /* finally, awake any reader */
    wake_up_interruptible(&inq);  /* blocked in read() and select() */

    return;
}

unsigned int BT_poll(struct file *filp, poll_table *wait)
{
    unsigned int mask = 0;

//    down(&wr_mtx);
    /*
     * The buffer is circular; it is considered full
     * if "wp" is right behind "rp". "left" is 0 if the
     * buffer is empty, and it is "1" if it is completely full.
     */
    if (mtk_wcn_stp_is_rxqueue_empty(BT_TASK_INDX))
    {
        poll_wait(filp, &inq,  wait);

        /* empty let select sleep */
        if((!mtk_wcn_stp_is_rxqueue_empty(BT_TASK_INDX)) || retflag)
        {
            mask |= POLLIN | POLLRDNORM;  /* readable */
        }
    }
    else
    {
        mask |= POLLIN | POLLRDNORM;  /* readable */
    }

    /* do we need condition? */
    mask |= POLLOUT | POLLWRNORM; /* writable */
//    up(&wr_mtx);
    return mask;
}


ssize_t BT_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    int retval = 0;
    int written = 0;
    down(&wr_mtx);

    BT_DBG_FUNC("%s: count %d pos %lld\n", __func__, count, *f_pos);
    if(retflag)
    {
        if (retflag == 1) //reset start
        {
            retval = -88;
            BT_INFO_FUNC("MT662x reset Write: start\n");
        }
        else if (retflag == 2) // reset end
        {
          retval = -99;
            BT_INFO_FUNC("MT662x reset Write: end\n");
        }
    goto OUT;
    }

    if (count > 0)
    {
        int copy_size = (count < MTKSTP_BUFFER_SIZE) ? count : MTKSTP_BUFFER_SIZE;
        if (copy_from_user(&o_buf[0], &buf[0], copy_size))
        {
            retval = -EFAULT;
            goto OUT;
        }
        //printk("%02x ", val);

        written = mtk_wcn_stp_send_data(&o_buf[0], copy_size, BT_TASK_INDX);
        if(0 == written)
        {
            retval = -ENOSPC;
            /*no windowspace in STP is available, native process should not call BT_write with no delay at all*/
            BT_ERR_FUNC("target packet length:%d, write success length:%d, retval = %d.\n", count, written, retval);
        }
        else
        {
            retval = written;
        }

    }else
    {
        retval = -EFAULT;
        BT_ERR_FUNC("target packet length:%d is not allowed, retval = %d.\n", count, retval);
    }

OUT:
    up(&wr_mtx);
    return (retval);
}

ssize_t BT_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    int retval = 0;

    down(&rd_mtx);

    BT_DBG_FUNC("BT_read(): count %d pos %lld\n", count, *f_pos);
    if(retflag)
    {
        if (retflag == 1) //reset start
        {
            retval = -88;
            BT_INFO_FUNC("MT662x reset Read: start\n");
        }
        else if (retflag == 2) // reset end
        {
            retval = -99;
            BT_INFO_FUNC("MT662x reset Read: end\n");
        }
    goto OUT;
    }

    if(count > MTKSTP_BUFFER_SIZE)
    {
        count = MTKSTP_BUFFER_SIZE;
    }
    retval = mtk_wcn_stp_receive_data(i_buf, count, BT_TASK_INDX);

    while(retval == 0) // got nothing, wait for STP's signal
    {
        /*If nonblocking mode, return directly O_NONBLOCK is specified during open() */
        if (filp->f_flags & O_NONBLOCK){
            BT_DBG_FUNC("Non-blocking BT_read() \n");
            retval = -EAGAIN;
            goto OUT;
        }

        BT_DBG_FUNC("BT_read(): wait_event 1\n");
        wait_event(BT_wq, flag != 0);
        BT_DBG_FUNC("BT_read(): wait_event 2\n");
        flag = 0;
        retval = mtk_wcn_stp_receive_data(i_buf, count, BT_TASK_INDX);
        BT_DBG_FUNC("BT_read(): mtk_wcn_stp_receive_data() = %d\n", retval);
    }

    // we got something from STP driver
    if (copy_to_user(buf, i_buf, retval))
    {
        retval = -EFAULT;
        goto OUT;
    }

OUT:
    up(&rd_mtx);
    BT_DBG_FUNC("BT_read(): retval = %d\n", retval);
    return (retval);
}

//int BT_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg)
long BT_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    int retval = 0;
    MTK_WCN_BOOL bRet = MTK_WCN_BOOL_TRUE;

    ENUM_WMTHWVER_TYPE_T hw_ver_sym = WMTHWVER_INVALID;
    BT_DBG_FUNC("BT_ioctl(): cmd (%d)\n", cmd);

    switch(cmd)
    {
#if 0
        case 0: // enable/disable STP
            /* George: STP is controlled by WMT only */
            /* mtk_wcn_stp_enable(arg); */
            break;
#endif
        case 1: // send raw data
            BT_DBG_FUNC("BT_ioctl(): disable raw data from BT dev \n");
            retval = -EINVAL;
            break;
        case COMBO_IOC_BT_HWVER:
            /*get combo hw version*/
            hw_ver_sym = mtk_wcn_wmt_hwver_get();

            BT_INFO_FUNC("BT_ioctl(): get hw version = %d, sizeof(hw_ver_sym) = %d\n", hw_ver_sym, sizeof(hw_ver_sym));
            if(copy_to_user((int __user *)arg, &hw_ver_sym, sizeof(hw_ver_sym))){
               retval = -EFAULT;
            }
            break;

        case COMBO_IOCTL_FW_ASSERT:
            /* BT trigger fw assert for debug*/
            BT_INFO_FUNC("BT Set fw assert......\n");
            bRet = mtk_wcn_wmt_assert();
            if (bRet == MTK_WCN_BOOL_TRUE) {
                BT_INFO_FUNC("BT Set fw assert OK\n");
                retval = 0;
            } else {
                BT_INFO_FUNC("BT Set fw assert Failed\n");
                retval = (-1000);
            }
            break;
            
        default:
            retval = -EFAULT;
            BT_DBG_FUNC("BT_ioctl(): unknown cmd (%d)\n", cmd);
            break;
    }

    return retval;
}

static int BT_open(struct inode *inode, struct file *file)
{
    BT_INFO_FUNC("%s: major %d minor %d (pid %d)\n", __func__,
        imajor(inode),
        iminor(inode),
        current->pid
        );
	if(current->pid ==1)
		return 0;
#if 1 /* GeorgeKuo: turn on function before check stp ready */
     /* turn on BT */
    if (MTK_WCN_BOOL_FALSE == mtk_wcn_wmt_func_on(WMTDRV_TYPE_BT)) {
        BT_WARN_FUNC("WMT turn on BT fail!\n");
        return -ENODEV;
    }else{
        retflag = 0;
        mtk_wcn_wmt_msgcb_reg(WMTDRV_TYPE_BT, bt_cdev_rst_cb);
        BT_INFO_FUNC("WMT register BT rst cb!\n");
    }
#endif

    if (mtk_wcn_stp_is_ready()) {
#if 0 /* GeorgeKuo: turn on function before check stp ready */
         /* turn on BT */
        if (MTK_WCN_BOOL_FALSE == mtk_wcn_wmt_func_on(WMTDRV_TYPE_BT)) {
            BT_WARN_FUNC("WMT turn on BT fail!\n");
            return -ENODEV;
        }
#endif
        mtk_wcn_stp_set_bluez(0);

        BT_INFO_FUNC("Now it's in MTK Bluetooth Mode\n");
        BT_INFO_FUNC("WMT turn on BT OK!\n");
        BT_INFO_FUNC("STP is ready!\n");
        platform_load_nvram_data(BT_NVRAM_CUSTOM_NAME,
            (char *)&g_nvram_btdata, sizeof(g_nvram_btdata));

        BT_INFO_FUNC("Read NVRAM : BD address %02x%02x%02x%02x%02x%02x Cap 0x%02x Codec 0x%02x\n",
            g_nvram_btdata[0], g_nvram_btdata[1], g_nvram_btdata[2],
            g_nvram_btdata[3], g_nvram_btdata[4], g_nvram_btdata[5],
            g_nvram_btdata[6], g_nvram_btdata[7]);

        mtk_wcn_stp_register_event_cb(BT_TASK_INDX, BT_event_cb);
		BT_INFO_FUNC("mtk_wcn_stp_register_event_cb finish\n");
    }
    else {
        BT_ERR_FUNC("STP is not ready\n");

        /*return error code*/
        return -ENODEV;
    }

//    init_MUTEX(&wr_mtx);
    sema_init(&wr_mtx, 1);
//    init_MUTEX(&rd_mtx);
    sema_init(&rd_mtx, 1);
	BT_INFO_FUNC("finish\n");

    return 0;
}

static int BT_close(struct inode *inode, struct file *file)
{
    BT_INFO_FUNC("%s: major %d minor %d (pid %d)\n", __func__,
        imajor(inode),
        iminor(inode),
        current->pid
        );
	if(current->pid ==1)
		return 0;
    retflag = 0;
    mtk_wcn_wmt_msgcb_unreg(WMTDRV_TYPE_BT);
    mtk_wcn_stp_register_event_cb(BT_TASK_INDX, NULL);

    if (MTK_WCN_BOOL_FALSE == mtk_wcn_wmt_func_off(WMTDRV_TYPE_BT)) {
        BT_INFO_FUNC("WMT turn off BT fail!\n");
        return -EIO;    //mostly, native programmer will not check this return value.
    }
    else {
        BT_INFO_FUNC("WMT turn off BT OK!\n");
    }

    return 0;
}

struct file_operations BT_fops = {
    .open = BT_open,
    .release = BT_close,
    .read = BT_read,
    .write = BT_write,
//    .ioctl = BT_ioctl,
    .unlocked_ioctl = BT_unlocked_ioctl,
    .poll = BT_poll
};

static int BT_init(void)
{
    dev_t dev = MKDEV(BT_major, 0);
    int alloc_ret = 0;
    int cdev_err = 0;

    /*static allocate chrdev*/
    alloc_ret = register_chrdev_region(dev, 1, BT_DRIVER_NAME);
    if (alloc_ret) {
        BT_ERR_FUNC("fail to register chrdev\n");
        return alloc_ret;
    }

    cdev_init(&BT_cdev, &BT_fops);
    BT_cdev.owner = THIS_MODULE;

    cdev_err = cdev_add(&BT_cdev, dev, BT_devs);
    if (cdev_err)
        goto error;

    BT_INFO_FUNC("%s driver(major %d) installed.\n", BT_DRIVER_NAME, BT_major);
    retflag = 0;
    mtk_wcn_stp_register_event_cb(BT_TASK_INDX, NULL);

    /* init wait queue */
    init_waitqueue_head(&(inq));

    return 0;

error:
    if (cdev_err == 0)
        cdev_del(&BT_cdev);

    if (alloc_ret == 0)
        unregister_chrdev_region(dev, BT_devs);

    return -1;
}

static void BT_exit(void)
{
    dev_t dev = MKDEV(BT_major, 0);
    retflag = 0;
    mtk_wcn_stp_register_event_cb(BT_TASK_INDX, NULL);  // unregister event callback function

    cdev_del(&BT_cdev);
    unregister_chrdev_region(dev, BT_devs);

    BT_INFO_FUNC("%s driver removed.\n", BT_DRIVER_NAME);
}

module_init(BT_init);
module_exit(BT_exit);


