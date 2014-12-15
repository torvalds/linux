#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/semaphore.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/kernel.h>
#include <linux/semaphore.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>

//#define  NULL 	0x0
#define TRUE 	0
#define  FALSE 	(-1)
#define  FT_RW_IIC_DRV  "ft_rw_iic_drv"
#define FT_RW_IIC_DRV_MAJOR 210    /*预设的ft_rw_iic_drv的主设备号*/

#define FT_I2C_RDWR_MAX_QUEUE 36
#define FT_I2C_SLAVEADDR   11
#define FT_I2C_RW          12
static int ft_rw_iic_drv_major = FT_RW_IIC_DRV_MAJOR;

static struct i2c_client *this_client;
static struct class *my_class;
static u16 g_slaveaddr = 0x38;
struct ft_rw_i2c_dev {
    struct cdev cdev;
    struct semaphore ft_rw_i2c_sem;   
};
struct ft_rw_i2c_dev *ft_rw_i2c_dev_tt;

typedef struct ft_rw_i2c
{
  u8 *buf;  //buffer
  __u16 addr; //slave addr
  u8 	flag;//0-write 1-read
  __u16 length; //the length of data 
}*pft_rw_i2c;

typedef struct ft_rw_i2c_queue
{
	struct ft_rw_i2c __user *i2c_queue;
	int queuenum;	
}*pft_rw_i2c_queue;

static int ft_rw_iic_drv_myread(u8* buf, int length)
{
   
#if 0
	struct i2c_adapter *adap=this_client->adapter;
	struct i2c_msg msg;
	int ret;

	msg.addr = g_slaveaddr;
	msg.flags = this_client->flags & I2C_M_TEN;
	msg.flags |= I2C_M_RD;
	msg.len = length;
	msg.buf = buf;

	ret = i2c_transfer(adap, &msg, 1);

	/* If everything went ok (i.e. 1 msg transmitted), return #bytes
	   transmitted, else error code. */
	return (ret == 1) ? length : ret;
#else
int ret;

    	//printk("want to read length=%d\n", length);
    ret = i2c_master_recv(this_client, buf, length);

    if(ret<=0)
    {
        printk("[TSP]ft_rw_iic_drv_read error\n");
    }
#endif
    return ret;
}

static int ft_rw_iic_drv_mywrite(u8* buf, int length)
{
#if 0
	//printk("want to write length=%d\n", length);
	struct i2c_adapter *adap=this_client->adapter;
		struct i2c_msg msg;
	int ret;
		msg.addr = g_slaveaddr;
		msg.flags = this_client->flags & I2C_M_TEN;
		msg.len = length;
		msg.buf = (char *)buf;
	
		ret = i2c_transfer(adap, &msg, 1);
	
		/* If everything went ok (i.e. 1 msg transmitted), return #bytes
		   transmitted, else error code. */
		return (ret == 1) ? length : ret;
#else
	int ret;
    ret=i2c_master_send(this_client, buf, length);
    if(ret<=0)
    {
        printk("[TSP]ft_rw_iic_drv_write error line = %d, ret = %d\n", __LINE__, ret);
    }

    return ret;
#endif
}

static int ft_rw_iic_drv_RDWR(unsigned long arg)
{

	struct ft_rw_i2c_queue i2c_rw_queue;
	u8 __user **data_ptrs;
	struct ft_rw_i2c * i2c_rw_msg;int ret = 0;
	int i;

	  if (!access_ok(VERIFY_READ, (struct ft_rw_i2c_queue *)arg, sizeof(struct ft_rw_i2c_queue)))
			return -EFAULT;

	if(copy_from_user(&i2c_rw_queue,
		(struct ft_rw_i2c_queue *)arg, 
		sizeof(struct ft_rw_i2c_queue)))
		return -EFAULT;

	if(i2c_rw_queue.queuenum > FT_I2C_RDWR_MAX_QUEUE)
		return -EINVAL;


	i2c_rw_msg = (struct ft_rw_i2c*)
		kmalloc(i2c_rw_queue.queuenum *sizeof(struct ft_rw_i2c),
		GFP_KERNEL);
	if(!i2c_rw_msg)
		return -ENOMEM;
	//printk("%s****%d\n", __FUNCTION__, __LINE__);

	if(copy_from_user(i2c_rw_msg, i2c_rw_queue.i2c_queue,
			i2c_rw_queue.queuenum*sizeof(struct ft_rw_i2c)))
		{
		kfree(i2c_rw_msg);
		return -EFAULT;
		}

	data_ptrs = kmalloc(i2c_rw_queue.queuenum * sizeof(u8 __user *), GFP_KERNEL);
	if (data_ptrs == NULL) {
		kfree(i2c_rw_msg);
		return -ENOMEM;
	}
	
	ret = 0;
	for(i=0; i< i2c_rw_queue.queuenum; i++)
	{
		if((i2c_rw_msg[i].length > 8192)||
			(i2c_rw_msg[i].flag & I2C_M_RECV_LEN)){
			ret = -EINVAL;
			break;
		}
		data_ptrs[i] = (u8 __user *)i2c_rw_msg[i].buf;
		i2c_rw_msg[i].buf = kmalloc(i2c_rw_msg[i].length, GFP_KERNEL);
		if(i2c_rw_msg[i].buf == NULL){
			ret = -ENOMEM;
			break;
		}

		if(copy_from_user(i2c_rw_msg[i].buf, data_ptrs[i], i2c_rw_msg[i].length))
		{
			++i;
			ret = -EFAULT;
			break;
		}
	}

	if(ret < 0)
	{
		int j;
		for(j=0; j<i; ++j)
			kfree(i2c_rw_msg[j].buf);
		kfree(data_ptrs);
		kfree(i2c_rw_msg);
		return ret;
	}

	for(i=0; i< i2c_rw_queue.queuenum; i++)
	{
		if(i2c_rw_msg[i].flag)
		{
   	   		ret = ft_rw_iic_drv_myread(i2c_rw_msg[i].buf, i2c_rw_msg[i].length);
			if(ret>=0){
				//printk("copy data to user\n");
   	   			ret = copy_to_user(data_ptrs[i], i2c_rw_msg[i].buf, ret);
				}
   	   	}
		else
		{
			ret = ft_rw_iic_drv_mywrite(i2c_rw_msg[i].buf, i2c_rw_msg[i].length);
		}
	}

	return ret;
}

/*  
       [function]:
                       char device open function interface 
*/
static int ft_rw_iic_drv_open(struct inode *inode, struct file *filp)
{
 
 filp->private_data=ft_rw_i2c_dev_tt;
 return 0;
}


/*  
       [function]:
                       char device close function interface 
*/
static int ft_rw_iic_drv_release(struct inode *inode, struct file *filp)
{

  return 0;
}


/*  
       [function]:
                       char device ioctrl function interface

       [parameter]:
                       filp:          file entrance
                       cmd:         the command from the application
                       mess:        the structure which is including some message
       [return]:

                      FALSE: write failure
                      TRUE:  write success

*/
//static int ft_rw_iic_drv_ioctl(struct inode *inode, struct file *filp, unsigned
static int ft_rw_iic_drv_ioctl(struct file *filp, unsigned
  int cmd, unsigned long arg)
{
  //struct globalmem_dev *dev = filp->private_data;/*获得设备结构体指针*/
  int ret = 0;
  down(&ft_rw_i2c_dev_tt->ft_rw_i2c_sem);
  //printk("call ioctl\n");
   switch (cmd)
  {
#if 0  
  case FT_I2C_SLAVEADDR:
  {
  	printk("set slave addr\n");

  	if ((arg > 0x3ff) ||
		    (((this_client->flags & I2C_M_TEN) == 0) && arg > 0x7f)){
			ret = -EINVAL;
			break;
  		}
	this_client->addr = arg;
	g_slaveaddr = arg;
	}
  	break;
#endif

  case FT_I2C_RW:
  	//printk("%s****%d\n", __FUNCTION__, __LINE__);
  	ret = ft_rw_iic_drv_RDWR(arg);	
	      break; 
  default:
  	printk("no command, command=%d\n", cmd);
  	ret =  -ENOTTY;
  	    break;
  }
   up(&ft_rw_i2c_dev_tt->ft_rw_i2c_sem);
  return ret;	
}


/*    
    [function]:char device file operation which will be put to register the char device

*/
static const struct file_operations ft_rw_iic_drv_fops =
{
  .owner              = THIS_MODULE,
  .open               = ft_rw_iic_drv_open,
  .release            = ft_rw_iic_drv_release,
  .unlocked_ioctl     = ft_rw_iic_drv_ioctl,
};

/*   */
static void ft_rw_iic_drv_setup_cdev(struct ft_rw_i2c_dev*dev, int index)
{
  int err, devno = MKDEV(ft_rw_iic_drv_major, index);

  cdev_init(&dev->cdev, &ft_rw_iic_drv_fops);
  dev->cdev.owner = THIS_MODULE;
  dev->cdev.ops = &ft_rw_iic_drv_fops;
  err = cdev_add(&dev->cdev, devno, 1);
  if (err)
    printk(KERN_NOTICE "Error %d adding LED%d", err, index);
}

static int __devexit ft_rw_iic_drv_remove(struct i2c_client *client)
{
  i2c_set_clientdata(client, NULL);
 //	 struct i2c_client *client1 = i2c_get_clientdata(client);;
//	kfree(client1);
  	return 0;
}

static int ft_rw_iic_drv_myinitdev()
{
	int err = 0;
    dev_t devno = MKDEV(ft_rw_iic_drv_major, 0);

	if(ft_rw_iic_drv_major)
		err = register_chrdev_region(devno, 1, "ft_rw_iic_drv");
	else{
		err = alloc_chrdev_region(&devno, 0, 1, "ft_rw_iic_drv");
		ft_rw_iic_drv_major = MAJOR(devno);
		}
	if(err < 0)
		return err;

	ft_rw_i2c_dev_tt = kmalloc(sizeof(struct ft_rw_i2c_dev), GFP_KERNEL);
	if(!ft_rw_i2c_dev_tt){
		err = -ENOMEM;
		unregister_chrdev_region(devno, 1);
		return err;
		}
	init_MUTEX(&ft_rw_i2c_dev_tt->ft_rw_i2c_sem);
	ft_rw_iic_drv_setup_cdev(ft_rw_i2c_dev_tt, 0); 

	#if 1
       my_class = class_create(THIS_MODULE, "my_class");
       if(IS_ERR(my_class)) 
       {
          printk("Err: failed in creating class.\n");
          return -1; 
        } 
        //step6 create device node
	 device_create( my_class, NULL, MKDEV(ft_rw_iic_drv_major, 0),NULL, "ft_rw_iic_drv");
	#endif

	return 0;
}

static int ft_rw_iic_drv_probe(struct i2c_client *client, const struct i2c_device_id *id)
{	
	int err = 0;
 printk("search i2c device \n");
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
        err = -ENODEV;
		printk("i2c_check_functionality err\n");
        return err;
      }
printk("i2c_set_clientdata\n");
	 this_client = client;
     i2c_set_clientdata(client, ft_rw_i2c_dev_tt);

    return 0;

}

static const struct i2c_device_id ft_rw_iic_drv_id[] = {
    { FT_RW_IIC_DRV, 0 },{ }
};

MODULE_DEVICE_TABLE(i2c, ft_rw_iic_drv_id);

static struct i2c_driver ft_rw_iic_drv_driver = {
    .probe        = ft_rw_iic_drv_probe,
    .remove        = __devexit_p(ft_rw_iic_drv_remove),
    .id_table    = ft_rw_iic_drv_id,
    .driver    = {
        .name = FT_RW_IIC_DRV,
    },
};


static int __init ft_rw_iic_drv_init(void)
{

    printk("\n----init ---\n");
	ft_rw_iic_drv_myinitdev();
    return i2c_add_driver(&ft_rw_iic_drv_driver);
}

static void __exit ft_rw_iic_drv_exit(void)
{
#if 1
   //delete device node under /dev
   device_destroy(my_class, MKDEV(ft_rw_iic_drv_major, 0)); 
    //delete class created by us
   class_destroy(my_class);  
#endif
   //delet the cdev
   cdev_del(&ft_rw_i2c_dev_tt->cdev);
   kfree(ft_rw_i2c_dev_tt);
   unregister_chrdev_region(MKDEV(ft_rw_iic_drv_major, 0), 1); 

   i2c_del_driver(&ft_rw_iic_drv_driver);  
}

#ifdef CONFIG_DEFERRED_MODULE_INIT
deferred_module_init(ft_rw_iic_drv_init);
#else
module_init(ft_rw_iic_drv_init);
#endif
module_exit(ft_rw_iic_drv_exit);

MODULE_AUTHOR("<luowj@Focaltech-systems.com>");
MODULE_DESCRIPTION("FocalTech rw i2c driver");
MODULE_LICENSE("GPL");
/*Define the module version*/
MODULE_VERSION("v1.0");