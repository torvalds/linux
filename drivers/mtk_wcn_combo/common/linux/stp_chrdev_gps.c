/** $Log: stp_chrdev_gps.c $
 *
 * 12 13 2010 Sean.Wang
 * (1) Add GPS_DEBUG_TRACE_GPIO to disable GPIO debugging trace
 * (2) Add GPS_DEBUG_DUMP to support GPS data dump
 * (3) Add mtk_wcn_stp_is_ready() check in GPS_open()
*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/sched.h>
#include <asm/current.h>
#include <asm/uaccess.h>
#include <linux/skbuff.h>

#include "stp_exp.h"
#include "wmt_exp.h"

MODULE_LICENSE("GPL");

#define GPS_DRIVER_NAME "mtk_stp_GPS_chrdev"
#define GPS_DEV_MAJOR 191 // never used number
#define GPS_DEBUG_TRACE_GPIO         0
#define GPS_DEBUG_DUMP               0

#define PFX                         "[GPS] "
#define GPS_LOG_DBG                  3
#define GPS_LOG_INFO                 2
#define GPS_LOG_WARN                 1
#define GPS_LOG_ERR                  0

#define COMBO_IOC_GPS_HWVER           6

unsigned int gDbgLevel = GPS_LOG_DBG;

#define GPS_DBG_FUNC(fmt, arg...)    if(gDbgLevel >= GPS_LOG_DBG){ printk(PFX "%s: "  fmt, __FUNCTION__ ,##arg);}
#define GPS_INFO_FUNC(fmt, arg...)   if(gDbgLevel >= GPS_LOG_INFO){ printk(PFX "%s: "  fmt, __FUNCTION__ ,##arg);}
#define GPS_WARN_FUNC(fmt, arg...)   if(gDbgLevel >= GPS_LOG_WARN){ printk(PFX "%s: "  fmt, __FUNCTION__ ,##arg);}
#define GPS_ERR_FUNC(fmt, arg...)    if(gDbgLevel >= GPS_LOG_ERR){ printk(PFX "%s: "  fmt, __FUNCTION__ ,##arg);}
#define GPS_TRC_FUNC(f)              if(gDbgLevel >= GPS_LOG_DBG){ printk(PFX "<%s> <%d>\n", __FUNCTION__, __LINE__);}


static int GPS_devs = 1;        /* device count */
static int GPS_major = GPS_DEV_MAJOR;       /* dynamic allocation */
module_param(GPS_major, uint, 0);
static struct cdev GPS_cdev;

static unsigned char i_buf[MTKSTP_BUFFER_SIZE];    // input buffer of read()
static unsigned char o_buf[MTKSTP_BUFFER_SIZE];    // output buffer of write()
static struct semaphore wr_mtx, rd_mtx;
static DECLARE_WAIT_QUEUE_HEAD(GPS_wq);
static int flag = 0;

static void GPS_event_cb(void);

ssize_t GPS_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    int retval = 0;
    int written = 0;
    down(&wr_mtx);

    //GPS_TRC_FUNC();

    /*printk("%s: count %d pos %lld\n", __func__, count, *f_pos);*/
    if (count > 0)
    {
        int copy_size = (count < MTKSTP_BUFFER_SIZE) ? count : MTKSTP_BUFFER_SIZE;
        if (copy_from_user(&o_buf[0], &buf[0], copy_size))
        {
            retval = -EFAULT;
            goto out;
        }
        //printk("%02x ", val);
#if GPS_DEBUG_TRACE_GPIO        
        mtk_wcn_stp_debug_gpio_assert(IDX_GPS_TX, DBG_TIE_LOW);
#endif
        written = mtk_wcn_stp_send_data(&o_buf[0], copy_size, GPS_TASK_INDX);
#if GPS_DEBUG_TRACE_GPIO
        mtk_wcn_stp_debug_gpio_assert(IDX_GPS_TX, DBG_TIE_HIGH);
#endif

#if GPS_DEBUG_DUMP
{            
    unsigned char *buf_ptr = &o_buf[0];            
    int k=0;            
    printk("--[GPS-WRITE]--");            
    for(k=0; k < 10 ; k++){  
    if(k%16 == 0)  printk("\n");                
        printk("0x%02x ", o_buf[k]);            
    }            
    printk("\n");        
}
#endif
        /*
            If cannot send successfully, enqueue again
        
        if (written != copy_size) {
            // George: FIXME! Move GPS retry handling from app to driver 
        }
        */
        if(0 == written)
        {
            retval = -ENOSPC;
            /*no windowspace in STP is available, native process should not call GPS_write with no delay at all*/
            GPS_ERR_FUNC("target packet length:%d, write success length:%d, retval = %d.\n", count, written, retval);
        }
        else
        {
            retval = written;
        }
    }
    else
    {
        retval = -EFAULT;
        GPS_ERR_FUNC("target packet length:%d is not allowed, retval = %d.\n", count, retval);
    }
out:
    up(&wr_mtx);
    return (retval);
}

ssize_t GPS_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    long val = 0;
    int retval;

    down(&rd_mtx);

/*    printk("GPS_read(): count %d pos %lld\n", count, *f_pos);*/

    if(count > MTKSTP_BUFFER_SIZE)
    {
        count = MTKSTP_BUFFER_SIZE;
    }
    
#if GPS_DEBUG_TRACE_GPIO    
    mtk_wcn_stp_debug_gpio_assert(IDX_GPS_RX, DBG_TIE_LOW);
#endif
    retval = mtk_wcn_stp_receive_data(i_buf, count, GPS_TASK_INDX);
#if GPS_DEBUG_TRACE_GPIO
    mtk_wcn_stp_debug_gpio_assert(IDX_GPS_RX, DBG_TIE_HIGH);
#endif

    while(retval == 0) // got nothing, wait for STP's signal
    {
        /*wait_event(GPS_wq, flag != 0);*/ /* George: let signal wake up */
        val = wait_event_interruptible(GPS_wq, flag != 0);
        flag = 0;
            
#if GPS_DEBUG_TRACE_GPIO
        mtk_wcn_stp_debug_gpio_assert(IDX_GPS_RX, DBG_TIE_LOW);
#endif

        retval = mtk_wcn_stp_receive_data(i_buf, count, GPS_TASK_INDX);

#if GPS_DEBUG_TRACE_GPIO
        mtk_wcn_stp_debug_gpio_assert(IDX_GPS_RX, DBG_TIE_HIGH);
#endif
        /* if we are signaled */
        if (val) {
            if (-ERESTARTSYS == val) {
                GPS_INFO_FUNC("signaled by -ERESTARTSYS(%ld) \n ", val);
            }
            else {
                GPS_INFO_FUNC("signaled by %ld \n ", val);
            }
            break;
        }
    }

#if GPS_DEBUG_DUMP    
{   
    unsigned char *buf_ptr = &i_buf[0];        
    int k=0;        
    printk("--[GPS-READ]--");       
    for(k=0; k < 10 ; k++){       
    if(k%16 == 0)  printk("\n");            
    printk("0x%02x ", i_buf[k]);        
    }        
    printk("--\n");    
}
#endif

    if (retval) {
    // we got something from STP driver
        if (copy_to_user(buf, i_buf, retval)) {
        retval = -EFAULT;
        goto OUT;
        }
        else {
            /* success */
        }
    }
    else {
        // we got nothing from STP driver, being signaled
        retval = val;
    }

OUT:
    up(&rd_mtx);
/*    printk("GPS_read(): retval = %d\n", retval);*/
    return (retval);
}

//int GPS_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg)
long GPS_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    int retval = 0;
    ENUM_WMTHWVER_TYPE_T hw_ver_sym = WMTHWVER_INVALID;

    printk("GPS_ioctl(): cmd (%d)\n", cmd);

    switch(cmd)
    {
        case 0: // enable/disable STP
            GPS_INFO_FUNC(KERN_INFO "GPS_ioctl(): disable STP control from GPS dev\n");
            retval = -EINVAL;
#if 1
#else
             /* George: STP is controlled by WMT only */
            mtk_wcn_stp_enable(arg);
#endif
            break;

        case 1: // send raw data
            GPS_INFO_FUNC(KERN_INFO "GPS_ioctl(): disable raw data from GPS dev \n");
            retval = -EINVAL;
            break;

        case COMBO_IOC_GPS_HWVER:             
            /*get combo hw version*/            
            hw_ver_sym = mtk_wcn_wmt_hwver_get();

            GPS_INFO_FUNC(KERN_INFO "GPS_ioctl(): get hw version = %d, sizeof(hw_ver_sym) = %d\n", hw_ver_sym, sizeof(hw_ver_sym));
            if(copy_to_user((int __user *)arg, &hw_ver_sym, sizeof(hw_ver_sym))){
               retval = -EFAULT;
            }
            break;
            
        default:
            retval = -EFAULT;
            GPS_INFO_FUNC(KERN_INFO "GPS_ioctl(): unknown cmd (%d)\n", cmd);
            break;
    }

/*OUT:*/
    return retval;
}

static void gps_cdev_rst_cb(
    ENUM_WMTDRV_TYPE_T src, 
    ENUM_WMTDRV_TYPE_T dst, 
    ENUM_WMTMSG_TYPE_T type, 
    void *buf, 
    unsigned int sz){

    /*
        To handle reset procedure please
    */
    ENUM_WMTRSTMSG_TYPE_T rst_msg;

    GPS_INFO_FUNC("sizeof(ENUM_WMTRSTMSG_TYPE_T) = %d\n", sizeof(ENUM_WMTRSTMSG_TYPE_T));
    if(sz <= sizeof(ENUM_WMTRSTMSG_TYPE_T)){ 
        memcpy((char *)&rst_msg, (char *)buf, sz);
        GPS_INFO_FUNC("src = %d, dst = %d, type = %d, buf = 0x%x sz = %d, max = %d\n", src, dst, type, rst_msg, sz, WMTRSTMSG_RESET_MAX);
        if((src==WMTDRV_TYPE_WMT) && 
            (dst == WMTDRV_TYPE_GPS) &&
                (type == WMTMSG_TYPE_RESET)){                
                    if(rst_msg == WMTRSTMSG_RESET_START){
                        GPS_INFO_FUNC("gps restart start!\n");

                        /*reset_start message handling*/
                        
                    } else if(rst_msg == WMTRSTMSG_RESET_END){
                        GPS_INFO_FUNC("gps restart end!\n");

                        /*reset_end message handling*/
                    }
        }
    } else {
        /*message format invalid*/
    }
}

static int GPS_open(struct inode *inode, struct file *file)
{
    printk("%s: major %d minor %d (pid %d)\n", __func__,
        imajor(inode),
        iminor(inode),
        current->pid
        );
	if(current->pid ==1)
			return 0;

#if 1 /* GeorgeKuo: turn on function before check stp ready */
     /* turn on BT */

    if (MTK_WCN_BOOL_FALSE == mtk_wcn_wmt_func_on(WMTDRV_TYPE_GPS)) {
        GPS_WARN_FUNC("WMT turn on GPS fail!\n");
        return -ENODEV;
    } else {
        mtk_wcn_wmt_msgcb_reg(WMTDRV_TYPE_GPS, gps_cdev_rst_cb);
        GPS_INFO_FUNC("WMT turn on GPS OK!\n");
    }
#endif

    if (mtk_wcn_stp_is_ready()) {
#if 0
        if (MTK_WCN_BOOL_FALSE == mtk_wcn_wmt_func_on(WMTDRV_TYPE_GPS)) {
            GPS_WARN_FUNC("WMT turn on GPS fail!\n");
            return -ENODEV;
        }
        GPS_INFO_FUNC("WMT turn on GPS OK!\n");
#endif
        mtk_wcn_stp_register_event_cb(GPS_TASK_INDX, GPS_event_cb);
    }  else {        
        GPS_ERR_FUNC("STP is not ready, Cannot open GPS Devices\n\r");
        
        /*return error code*/
        return -ENODEV;
    }

    //init_MUTEX(&wr_mtx);
    sema_init(&wr_mtx, 1);
    //init_MUTEX(&rd_mtx);
    sema_init(&rd_mtx, 1);
    
    return 0;
}

static int GPS_close(struct inode *inode, struct file *file)
{
    printk("%s: major %d minor %d (pid %d)\n", __func__,
        imajor(inode),
        iminor(inode),
        current->pid
        );
	if(current->pid ==1)
			return 0;

    /*Flush Rx Queue*/
    mtk_wcn_stp_register_event_cb(GPS_TASK_INDX, 0x0);  // unregister event callback function
    mtk_wcn_wmt_msgcb_unreg(WMTDRV_TYPE_GPS);
    
    if (MTK_WCN_BOOL_FALSE == mtk_wcn_wmt_func_off(WMTDRV_TYPE_GPS)) {
        GPS_WARN_FUNC("WMT turn off GPS fail!\n");
        return -EIO;    //mostly, native programer does not care this return vlaue, but we still return error code.
    }
    else {       
        GPS_INFO_FUNC("WMT turn off GPS OK!\n");
    }

    return 0;
}

struct file_operations GPS_fops = {
    .open = GPS_open,
    .release = GPS_close,
    .read = GPS_read,
    .write = GPS_write,
//    .ioctl = GPS_ioctl
    .unlocked_ioctl = GPS_unlocked_ioctl,
};

void GPS_event_cb(void)
{
/*    printk("GPS_event_cb() \n");*/

    flag = 1;
    wake_up(&GPS_wq);

    return;
}

static int GPS_init(void)
{
    dev_t dev = MKDEV(GPS_major, 0);
    int alloc_ret = 0;
    int cdev_err = 0;

    /*static allocate chrdev*/
    alloc_ret = register_chrdev_region(dev, 1, GPS_DRIVER_NAME);
    if (alloc_ret) {
        printk("fail to register chrdev\n");
        return alloc_ret;
    }

    cdev_init(&GPS_cdev, &GPS_fops);
    GPS_cdev.owner = THIS_MODULE;

    cdev_err = cdev_add(&GPS_cdev, dev, GPS_devs);
    if (cdev_err)
        goto error;

    printk(KERN_ALERT "%s driver(major %d) installed.\n", GPS_DRIVER_NAME, GPS_major);
    
    return 0;

error:
    if (cdev_err == 0)
        cdev_del(&GPS_cdev);

    if (alloc_ret == 0)
        unregister_chrdev_region(dev, GPS_devs);

    return -1;
}

static void GPS_exit(void)
{
    dev_t dev = MKDEV(GPS_major, 0);

    cdev_del(&GPS_cdev);
    unregister_chrdev_region(dev, GPS_devs);

    printk(KERN_ALERT "%s driver removed.\n", GPS_DRIVER_NAME);
}

module_init(GPS_init);
module_exit(GPS_exit);

EXPORT_SYMBOL(GPS_event_cb);

