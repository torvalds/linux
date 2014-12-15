/**************************************************************
**																	 **
**function define & declare												 **
**																	 **
***************************************************************/

static int ge2d_open(struct inode *inode, struct file *file) ;
static long ge2d_ioctl(struct file *filp, unsigned int cmd, unsigned long args) ;
static int ge2d_release(struct inode *inode, struct file *file);
extern ssize_t work_queue_status_show(struct class *cla,struct class_attribute *attr,char *buf) ;
extern ssize_t free_queue_status_show(struct class *cla,struct class_attribute *attr,char *buf);
/**************************************************************
**																	 **
**	varible define		 												 **
**																	 **
***************************************************************/

static  ge2d_device_t  ge2d_device;
static DEFINE_MUTEX(ge2d_mutex);
static const struct file_operations ge2d_fops = {
	.owner		= THIS_MODULE,
	.open		=ge2d_open,  
	.unlocked_ioctl		= ge2d_ioctl,
	.release		= ge2d_release, 	
};
static struct class_attribute ge2d_class_attrs[] = {
	__ATTR(wq_status,
		S_IRUGO | S_IWUSR,
		work_queue_status_show,
		NULL),
	__ATTR(fq_status,
		S_IRUGO | S_IWUSR,
		free_queue_status_show,
		NULL),
	__ATTR_NULL
};
static struct class ge2d_class = {
	.name = GE2D_CLASS_NAME,
	.class_attrs = ge2d_class_attrs,
};
