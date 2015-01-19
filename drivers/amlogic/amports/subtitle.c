#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/vmalloc.h>
#include <linux/major.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/amlogic/amports/amstream.h>
#include <linux/uaccess.h>

#include <linux/amlogic/amlog.h>
MODULE_AMLOG(AMLOG_DEFAULT_LEVEL, 0, LOG_DEFAULT_LEVEL_DESC, LOG_DEFAULT_MASK_DESC);
#define DEVICE_NAME "amsubtitle"	/* Dev name as it appears in /proc/devices   */
#define DEVICE_CLASS_NAME "subtitle"

static int subdevice_open = 0;

#define MAX_SUBTITLE_PACKET 10
static DEFINE_MUTEX(amsubtitle_mutex);

typedef struct {
    int subtitle_size;
    int subtitle_pts;
    char * data;
} subtitle_data_t;
static subtitle_data_t subtitle_data[MAX_SUBTITLE_PACKET];
static int subtitle_enable = 1;
static int subtitle_total = 0;
static int subtitle_width = 0;
static int subtitle_height = 0;
static int subtitle_type = -1;
static int subtitle_current = 0; // no subtitle
//sub_index node will be modified by libplayer; amlogicplayer will use 
//it to detect wheather libplayer switch sub finished or not
static int subtitle_index = 0; // no subtitle
//static int subtitle_size = 0;
//static int subtitle_read_pos = 0;
static int subtitle_write_pos = 0;
static int subtitle_start_pts = 0;
static int subtitle_fps = 0;
static int subtitle_subtype = 0;
static int subtitle_reset = 0;
//static int *subltitle_address[MAX_SUBTITLE_PACKET];

typedef enum {
    SUB_NULL = -1,
    SUB_ENABLE = 0,
    SUB_TOTAL,
    SUB_WIDTH,
    SUB_HEIGHT,
    SUB_TYPE,
    SUB_CURRENT,
    SUB_INDEX,
    SUB_WRITE_POS,
    SUB_START_PTS,
    SUB_FPS,
    SUB_SUBTYPE,
    SUB_RESET,
    SUB_DATA_T_SIZE,
    SUB_DATA_T_DATA
}subinfo_para_type;

typedef struct {
    subinfo_para_type subinfo_type;
    int subtitle_info;
    char *data;
} subinfo_para_t;

// total
// curr
// bimap
// text
// type
// info
// pts
// duration
// color pallete
// width/height

static ssize_t show_curr(struct class *class,
                         struct class_attribute *attr,
                         char *buf)
{
    return sprintf(buf, "%d: current\n", subtitle_current);
}

static ssize_t store_curr(struct class *class,
                          struct class_attribute *attr,
                          const char *buf,
                          size_t size)
{
    unsigned curr;
    ssize_t r;

    r = sscanf(buf, "%d", &curr);
    //if ((r != 1))
    //return -EINVAL;

    subtitle_current = curr;

    return size;
}

static ssize_t show_index(struct class *class,
                         struct class_attribute *attr,
                         char *buf)
{
    return sprintf(buf, "%d: current\n", subtitle_index);
}

static ssize_t store_index(struct class *class,
                          struct class_attribute *attr,
                          const char *buf,
                          size_t size)
{
    unsigned curr;
    ssize_t r;

    r = sscanf(buf, "%d", &curr);
    //if ((r != 1))
    //return -EINVAL;

    subtitle_index = curr;

    return size;
}


static ssize_t show_reset(struct class *class,
                         struct class_attribute *attr,
                         char *buf)
{
    return sprintf(buf, "%d: current\n", subtitle_reset);
}

static ssize_t store_reset(struct class *class,
                          struct class_attribute *attr,
                          const char *buf,
                          size_t size)
{
    unsigned reset;
    ssize_t r;

    r = sscanf(buf, "%d", &reset);
	
    printk("reset is %d\n", reset);
    //if ((r != 1))
    //return -EINVAL;

    subtitle_reset = reset;

    return size;
}

static ssize_t show_type(struct class *class,
                         struct class_attribute *attr,
                         char *buf)
{
    return sprintf(buf, "%d: type\n", subtitle_type);
}

static ssize_t store_type(struct class *class,
                          struct class_attribute *attr,
                          const char *buf,
                          size_t size)
{
    unsigned type;
    ssize_t r;

    r = sscanf(buf, "%d", &type);
    //if ((r != 1))
    //  return -EINVAL;

    subtitle_type = type;

    return size;
}

static ssize_t show_width(struct class *class,
                          struct class_attribute *attr,
                          char *buf)
{
    return sprintf(buf, "%d: width\n", subtitle_width);
}

static ssize_t store_width(struct class *class,
                           struct class_attribute *attr,
                           const char *buf,
                           size_t size)
{
    unsigned width;
    ssize_t r;

    r = sscanf(buf, "%d", &width);
    //if ((r != 1))
    //return -EINVAL;

    subtitle_width = width;

    return size;
}

static ssize_t show_height(struct class *class,
                           struct class_attribute *attr,
                           char *buf)
{
    return sprintf(buf, "%d: height\n", subtitle_height);
}

static ssize_t store_height(struct class *class,
                            struct class_attribute *attr,
                            const char *buf,
                            size_t size)
{
    unsigned height;
    ssize_t r;

    r = sscanf(buf, "%d", &height);
    //if ((r != 1))
    //return -EINVAL;

    subtitle_height = height;

    return size;
}

static ssize_t show_total(struct class *class,
                          struct class_attribute *attr,
                          char *buf)
{
    return sprintf(buf, "%d: num\n", subtitle_total);
}

static ssize_t store_total(struct class *class,
                           struct class_attribute *attr,
                           const char *buf,
                           size_t size)
{
    unsigned total;
    ssize_t r;

    r = sscanf(buf, "%d", &total);
    if ((r <= 0)) {
        return -EINVAL;
    }
    printk("subtitle num is %d\n", total);
    subtitle_total = total;

    return size;
}

static ssize_t show_enable(struct class *class,
                           struct class_attribute *attr,
                           char *buf)
{
    if (subtitle_enable) {
        return sprintf(buf, "1: enabled\n");
    }

    return sprintf(buf, "0: disabled\n");
}

static ssize_t store_enable(struct class *class,
                            struct class_attribute *attr,
                            const char *buf,
                            size_t size)
{
    unsigned mode;
    ssize_t r;

    r = sscanf(buf, "%d", &mode);
    if ((r != 1)) {
        return -EINVAL;
    }
    printk("subtitle enable is %d\n", mode);
    subtitle_enable = mode ? 1 : 0;

    return size;
}

static ssize_t show_size(struct class *class,
                         struct class_attribute *attr,
                         char *buf)
{
    if (subtitle_enable) {
        return sprintf(buf, "1: size\n");
    }

    return sprintf(buf, "0: size\n");
}

static ssize_t store_size(struct class *class,
                          struct class_attribute *attr,
                          const char *buf,
                          size_t size)
{
    unsigned ssize;
    ssize_t r;

    r = sscanf(buf, "%d", &ssize);
    if ((r <= 0)) {
        return -EINVAL;
    }
    printk("subtitle size is %d\n", ssize);
    subtitle_data[subtitle_write_pos].subtitle_size = ssize;

    return size;
}

static ssize_t show_startpts(struct class *class,
                             struct class_attribute *attr,
                             char *buf)
{
    return sprintf(buf, "%d: pts\n", subtitle_start_pts);
}

static ssize_t store_startpts(struct class *class,
                              struct class_attribute *attr,
                              const char *buf,
                              size_t size)
{
    unsigned spts;
    ssize_t r;

    r = sscanf(buf, "%d", &spts);
    if ((r <= 0)) {
        return -EINVAL;
    }
    printk("subtitle start pts is %x\n", spts);
    subtitle_start_pts = spts;

    return size;
}

static ssize_t show_data(struct class *class,
                         struct class_attribute *attr,
                         char *buf)
{
    if (subtitle_data[subtitle_write_pos].data) {
        return sprintf(buf, "%d\n", (int)(subtitle_data[subtitle_write_pos].data));
    }

    return sprintf(buf, "0: disabled\n");
}

static ssize_t store_data(struct class *class,
                          struct class_attribute *attr,
                          const char *buf,
                          size_t size)
{
    unsigned address;
    ssize_t r;

    r = sscanf(buf, "%d", &address);
    if ((r == 0)) {
        return -EINVAL;
    }
    if (subtitle_data[subtitle_write_pos].subtitle_size > 0) {
        subtitle_data[subtitle_write_pos].data = vmalloc((subtitle_data[subtitle_write_pos].subtitle_size));
        if (subtitle_data[subtitle_write_pos].data)
            memcpy(subtitle_data[subtitle_write_pos].data, (char *)address,
                   subtitle_data[subtitle_write_pos].subtitle_size);
    }
    printk("subtitle data address is %x", (unsigned int)(subtitle_data[subtitle_write_pos].data));
    subtitle_write_pos++;
    if (subtitle_write_pos >= MAX_SUBTITLE_PACKET) {
        subtitle_write_pos = 0;
    }
    return 1;
}

static ssize_t show_fps(struct class *class,
                        struct class_attribute *attr,
                        char *buf)
{
    return sprintf(buf, "%d: fps\n", subtitle_fps);
}

static ssize_t store_fps(struct class *class,
                         struct class_attribute *attr,
                         const char *buf,
                         size_t size)
{
    unsigned ssize;
    ssize_t r;

    r = sscanf(buf, "%d", &ssize);
    if ((r <= 0)) {
        return -EINVAL;
    }
    printk("subtitle fps is %d\n", ssize);
    subtitle_fps = ssize;

    return size;
}

static ssize_t show_subtype(struct class *class,
                            struct class_attribute *attr,
                            char *buf)
{
    return sprintf(buf, "%d: subtype\n", subtitle_subtype);
}

static ssize_t store_subtype(struct class *class,
                             struct class_attribute *attr,
                             const char *buf,
                             size_t size)
{
    unsigned ssize;
    ssize_t r;

    r = sscanf(buf, "%d", &ssize);
    if ((r <= 0)) {
        return -EINVAL;
    }
    printk("subtitle subtype is %d\n", ssize);
    subtitle_subtype = ssize;

    return size;
}

static struct class_attribute subtitle_class_attrs[] = {
    __ATTR(enable,     S_IRUGO | S_IWUSR | S_IWGRP, show_enable,  store_enable),
    __ATTR(total,     S_IRUGO | S_IWUSR | S_IWGRP, show_total,  store_total),
    __ATTR(width,     S_IRUGO | S_IWUSR | S_IWGRP, show_width,  store_width),
    __ATTR(height,     S_IRUGO | S_IWUSR | S_IWGRP, show_height,  store_height),
    __ATTR(type,     S_IRUGO | S_IWUSR | S_IWGRP, show_type,  store_type),
    __ATTR(curr,     S_IRUGO | S_IWUSR | S_IWGRP, show_curr,  store_curr),
    __ATTR(index,     S_IRUGO | S_IWUSR | S_IWGRP, show_index,  store_index),
    __ATTR(size,     S_IRUGO | S_IWUSR | S_IWGRP, show_size,  store_size),
    __ATTR(data,     S_IRUGO | S_IWUSR | S_IWGRP, show_data,  store_data),
    __ATTR(startpts,     S_IRUGO | S_IWUSR | S_IWGRP, show_startpts,  store_startpts),
    __ATTR(fps,     S_IRUGO | S_IWUSR | S_IWGRP, show_fps,  store_fps),
    __ATTR(subtype,     S_IRUGO | S_IWUSR | S_IWGRP, show_subtype,  store_subtype),
	__ATTR(reset, 	S_IRUGO | S_IWUSR, show_reset,  store_reset),
    __ATTR_NULL
};
/*
static struct class subtitle_class = {
        .name = "subtitle",
        .class_attrs = subtitle_class_attrs,
    };
*/
/*********************************************************
 * /dev/amvideo APIs
 *********************************************************/
static int amsubtitle_open(struct inode *inode, struct file *file)
{	
    mutex_lock(&amsubtitle_mutex);
	
    if (subdevice_open) {
        mutex_unlock(&amsubtitle_mutex);
        return -EBUSY;
    }
	
	subdevice_open = 1;

    try_module_get(THIS_MODULE);
	
    mutex_unlock(&amsubtitle_mutex);
	
    return 0;
}

static int amsubtitle_release(struct inode *inode, struct file *file)
{
    mutex_lock(&amsubtitle_mutex);
	
    subdevice_open = 0;
	
    module_put(THIS_MODULE);
	
    mutex_unlock(&amsubtitle_mutex);
	
    return 0;
}

static long amsubtitle_ioctl(struct file *file,
                          unsigned int cmd, ulong arg)
{
    switch (cmd) {
    case AMSTREAM_IOC_GET_SUBTITLE_INFO: {
            subinfo_para_t Vstates;
            subinfo_para_t *states = &Vstates;
            if(copy_from_user((void*)states,(void *)arg,sizeof(Vstates)))
                return -EFAULT;
            switch(states->subinfo_type) {
            case SUB_ENABLE:
                states->subtitle_info = subtitle_enable;
                break;
            case SUB_TOTAL:
                states->subtitle_info = subtitle_total;
                break;
            case SUB_WIDTH:
                states->subtitle_info = subtitle_width;
                break;
            case SUB_HEIGHT:
                states->subtitle_info = subtitle_height;
                break;
            case SUB_TYPE:
                states->subtitle_info = subtitle_type;
                break;
            case SUB_CURRENT:
                states->subtitle_info = subtitle_current;
                break;
            case SUB_INDEX:
                states->subtitle_info = subtitle_index;
                break;
            case SUB_WRITE_POS:
                states->subtitle_info = subtitle_write_pos;
                break;
            case SUB_START_PTS:
                states->subtitle_info = subtitle_start_pts;
                break;
            case SUB_FPS:
                states->subtitle_info = subtitle_fps;
                break;
            case SUB_SUBTYPE:
                states->subtitle_info = subtitle_subtype;
                break;
            case SUB_RESET:
                states->subtitle_info = subtitle_reset;
                break;
            case SUB_DATA_T_SIZE:
	            states->subtitle_info = subtitle_data[subtitle_write_pos].subtitle_size;
                break;
            case SUB_DATA_T_DATA: {
                    if (states->subtitle_info > 0) {
                        states->subtitle_info = (int)subtitle_data[subtitle_write_pos].data;
                    }
				}
                break;
            default:
		        break;
            }
            if(copy_to_user((void*)arg,(void *)states,sizeof(Vstates)))
                return -EFAULT;
        }

		break;
	case AMSTREAM_IOC_SET_SUBTITLE_INFO: {
            subinfo_para_t Vstates;
            subinfo_para_t *states = &Vstates;
            if(copy_from_user((void*)states,(void *)arg,sizeof(Vstates)))
                return -EFAULT;
            switch(states->subinfo_type) {
            case SUB_ENABLE:
                subtitle_enable = states->subtitle_info;
                break;
            case SUB_TOTAL:
                subtitle_total = states->subtitle_info;
                break;
            case SUB_WIDTH:
                subtitle_width = states->subtitle_info;
                break;
            case SUB_HEIGHT:
                subtitle_height = states->subtitle_info;
                break;
            case SUB_TYPE:
                subtitle_type = states->subtitle_info;
                break;
            case SUB_CURRENT:
                subtitle_current = states->subtitle_info;
                break;
            case SUB_INDEX:
                subtitle_index = states->subtitle_info;
                break;
            case SUB_WRITE_POS:
                subtitle_write_pos = states->subtitle_info;
                break;
            case SUB_START_PTS:
                subtitle_start_pts = states->subtitle_info;
                break;
            case SUB_FPS:
                subtitle_fps = states->subtitle_info;
                break;
            case SUB_SUBTYPE:
                subtitle_subtype = states->subtitle_info;
                break;
            case SUB_RESET:
                subtitle_reset = states->subtitle_info;
                break;
            case SUB_DATA_T_SIZE:
	            subtitle_data[subtitle_write_pos].subtitle_size = states->subtitle_info;
                break;
            case SUB_DATA_T_DATA: {
                    if (states->subtitle_info > 0) {
                        subtitle_data[subtitle_write_pos].data = vmalloc((states->subtitle_info));
                        if (subtitle_data[subtitle_write_pos].data)
                            memcpy(subtitle_data[subtitle_write_pos].data, (char *)states->data,
                                states->subtitle_info);
                    }

                    subtitle_write_pos++;
                    if (subtitle_write_pos >= MAX_SUBTITLE_PACKET) {
                        subtitle_write_pos = 0;
                    }
                }
                break;
            default:
		        break;
            }
			
        }

        break;
	default:
		break;
    }

    return 0;
}

const static struct file_operations amsubtitle_fops = {
    .owner    = THIS_MODULE,
    .open     = amsubtitle_open,
    .release  = amsubtitle_release,
    .unlocked_ioctl    = amsubtitle_ioctl,
};

static struct device *amsubtitle_dev;
static dev_t amsub_devno;
static struct class* amsub_clsp;
static struct cdev*  amsub_cdevp;
#define AMSUBTITLE_DEVICE_COUNT 1

static void create_amsub_attrs(struct class* class)
{
    int i=0;
    for(i=0; subtitle_class_attrs[i].attr.name; i++){
        if(class_create_file(class, &subtitle_class_attrs[i]) < 0)
        break;
    }
}

static void remove_amsub_attrs(struct class* class)
{
    int i=0;
    for(i=0; subtitle_class_attrs[i].attr.name; i++){
        class_remove_file(class, &subtitle_class_attrs[i]);
    }
}

static int __init subtitle_init(void)
{
    int ret = 0;

    ret = alloc_chrdev_region(&amsub_devno, 0, AMSUBTITLE_DEVICE_COUNT, DEVICE_NAME);
    if(ret < 0){
        printk("amsub: faild to alloc major number\n");
        ret = - ENODEV;
        return ret;
    }

    amsub_clsp = class_create(THIS_MODULE, DEVICE_CLASS_NAME);
    if(IS_ERR(amsub_clsp)){
        ret = PTR_ERR(amsub_clsp);
        goto err1;
    }
	
    create_amsub_attrs(amsub_clsp);

    amsub_cdevp = kmalloc(sizeof(struct cdev), GFP_KERNEL);
    if(!amsub_cdevp){
        printk("amsub: failed to allocate memory\n");
        ret = -ENOMEM;
        goto err2;
    }

    cdev_init(amsub_cdevp, &amsubtitle_fops);
    amsub_cdevp->owner = THIS_MODULE;
	// connect the major/minor number to cdev
    ret = cdev_add(amsub_cdevp, amsub_devno, AMSUBTITLE_DEVICE_COUNT);
    if(ret){
        printk("amsub:failed to add cdev\n");
        goto err3;
    } 

    amsubtitle_dev = device_create(amsub_clsp, NULL,
                                MKDEV(MAJOR(amsub_devno),0), NULL,
                                DEVICE_NAME);

    if (IS_ERR(amsubtitle_dev)) {
        amlog_level(LOG_LEVEL_ERROR, "## Can't create amsubtitle device\n");
        goto err4;
    }

    return (0);

err4:
    cdev_del(amsub_cdevp);
err3:
    kfree(amsub_cdevp);
err2:
    remove_amsub_attrs(amsub_clsp);
    class_destroy(amsub_clsp);
err1:
    unregister_chrdev_region(amsub_devno, 1);

    return ret;
}

static void __exit subtitle_exit(void)
{
    unregister_chrdev_region(amsub_devno, 1);
    device_destroy(amsub_clsp, MKDEV(MAJOR(amsub_devno),0));
    cdev_del(amsub_cdevp);
    kfree(amsub_cdevp);  
    remove_amsub_attrs(amsub_clsp);
    class_destroy(amsub_clsp);
}

module_init(subtitle_init);
module_exit(subtitle_exit);

MODULE_DESCRIPTION("AMLOGIC Subtitle management driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kevin Wang <kevin.wang@amlogic.com>");
