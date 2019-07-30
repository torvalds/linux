// SPDX-License-Identifier: GPL-2.0+
#if 0
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>	/* printk() */
#include <linux/slab.h>		/* kmalloc() */
#include <linux/fs.h>		/* everything... */
#include <linux/errno.h>	/* error codes */
#include <linux/types.h>	/* size_t */
#include <linux/cdev.h>
#include <asm/uaccess.h>	/* copy_*_user */

#include "i2c_driver.h"

int i2c_cdev_open(struct inode *inode, struct file *filp)
{
  struct i2c_device *lddev;
  
  if(NULL == inode) {
    //printk(KERN_WARNING "<pl_i2c> i2c_cdev_open: inode is a NULL pointer\n");
    DBG_PRINT(KERN_WARNING, "i2c_cdev_open: inode is a NULL pointer\n");
    return -EINVAL;
  }
  if(NULL == filp) {
    //printk(KERN_WARNING "<pl_i2c> i2c_cdev_open: filp is a NULL pointer\n");
    DBG_PRINT(KERN_WARNING, "i2c_cdev_open: filp is a NULL pointer\n");
    return -EINVAL;
  }
  
  lddev = container_of(inode->i_cdev, struct i2c_device, cdev);
  //printk(KERN_DEBUG "<pl_i2c> i2c_cdev_open(filp = [%p], lddev = [%p])\n", filp, lddev);
  DBG_PRINT(KERN_DEBUG, "i2c_cdev_open(filp = [%p], lddev = [%p])\n", filp, lddev);
  
  filp->private_data = lddev; /* so other methods can access it */
  
  return 0;	/* success */
}

int i2c_cdev_close(struct inode *inode, struct file *filp)
{
  struct i2c_device *lddev;
  
  if(NULL == inode) {
    //printk(KERN_WARNING "<pl_i2c> i2c_cdev_close: inode is a NULL pointer\n");
    DBG_PRINT(KERN_WARNING, "i2c_cdev_close: inode is a NULL pointer\n");
    return -EINVAL;
  }
  if(NULL == filp) {
    //printk(KERN_WARNING "<pl_i2c> i2c_cdev_close: filp is a NULL pointer\n");
    DBG_PRINT(KERN_WARNING, "i2c_cdev_close: filp is a NULL pointer\n");
    return -EINVAL;
  }
  
  lddev = filp->private_data;
  //printk(KERN_DEBUG "<pl_i2c> i2c_cdev_close(filp = [%p], lddev = [%p])\n", filp, lddev);
  DBG_PRINT(KERN_DEBUG, "i2c_cdev_close(filp = [%p], lddev = [%p])\n", filp, lddev);
  
  return 0;
}

ssize_t i2c_cdev_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
  size_t copy;
  ssize_t ret = 0;
  int err = 0;
  u64 read_val;
  char tmp_buf[48] = { 0 };
  struct i2c_device *lddev = filp->private_data;

  if(NULL == filp) {
    //printk(KERN_WARNING "<pl_i2c> i2c_cdev_read: filp is a NULL pointer\n");
    DBG_PRINT(KERN_WARNING, "i2c_cdev_read: filp is a NULL pointer\n");
    return -EINVAL;
  }
  if(NULL == buf) {
    //printk(KERN_WARNING "<pl_i2c> i2c_cdev_read: buf is a NULL pointer\n");
    DBG_PRINT(KERN_WARNING, "i2c_cdev_read: buf is a NULL pointer\n");
    return -EINVAL;
  }
  if(NULL == f_pos) {
    //printk(KERN_WARNING "<pl_i2c> i2c_cdev_read: f_pos is a NULL pointer\n");
    DBG_PRINT(KERN_WARNING, "i2c_cdev_read: f_pos is a NULL pointer\n");
    return -EINVAL;
  }

  if(count < sizeof(tmp_buf)) {
    //printk(KERN_INFO "<pl_i2c> i2c_cdev_read: buffer is too small (count = %d, should be at least %d bytes)\n", (int)count, (int)sizeof(tmp_buf));
    DBG_PRINT(KERN_INFO, "i2c_cdev_read: buffer is too small (count = %d, should be at least %d bytes)\n", (int)count, (int)sizeof(tmp_buf));
    return -EINVAL;
  }
  if(((*f_pos * 8) + lddev->pldev->resource[0].start) > lddev->pldev->resource[0].end) {
    //printk(KERN_INFO "<pl_i2c> i2c_cdev_read: bad read addr %016llx\n", (*f_pos * 8) + lddev->pldev->resource[0].start);
    DBG_PRINT(KERN_INFO, "i2c_cdev_read: bad read addr %016llx\n", (*f_pos * 8) + lddev->pldev->resource[0].start);
    //printk(KERN_INFO "<pl_i2c> i2c_cdev_read: addr end %016llx\n", lddev->pldev->resource[0].end);
    DBG_PRINT(KERN_INFO, "i2c_cdev_read: addr end %016llx\n", lddev->pldev->resource[0].end);
    //printk(KERN_INFO "<pl_i2c> i2c_cdev_read: EOF reached\n");
    DBG_PRINT(KERN_INFO, "i2c_cdev_read: EOF reached\n");
    return 0;
  }

  down_read(&lddev->rw_sem);
  
  read_val = *(lddev->regs + *f_pos);
  copy = clamp_t(size_t, count, 1, sizeof(tmp_buf));
  copy = scnprintf(tmp_buf, copy, "reg: 0x%x val: 0x%llx\n", (unsigned int)*f_pos, read_val);
  err = copy_to_user(buf, tmp_buf, copy);
  if(err) {
    //printk(KERN_INFO "<pl_i2c> i2c_cdev_read: could not copy to user (err = %d)\n", err);
    DBG_PRINT(KERN_INFO, "i2c_cdev_read: could not copy to user (err = %d)\n", err);
    return -EINVAL;
  }

  ret = (ssize_t)copy;
  (*f_pos)++;
  
  up_read(&lddev->rw_sem);
  
  return ret;
}

ssize_t i2c_cdev_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
  u8 reg;
  u8 val;
  char tmp[8] = { 0 };
  struct i2c_device *lddev = filp->private_data;

  if(NULL == filp) {
    //printk(KERN_WARNING "<pl_i2c> i2c_cdev_write: filp is a NULL pointer\n");
    DBG_PRINT(KERN_WARNING, "i2c_cdev_write: filp is a NULL pointer\n");
    return -EINVAL;
  }
  if(NULL == buf) {
    //printk(KERN_WARNING "<pl_i2c> i2c_cdev_write: buf is a NULL pointer\n");
    DBG_PRINT(KERN_WARNING, "i2c_cdev_write: buf is a NULL pointer\n");
    return -EINVAL;
  }
  if(NULL == f_pos) {
    //printk(KERN_WARNING "<pl_i2c> i2c_cdev_write: f_pos is a NULL pointer\n");
    DBG_PRINT(KERN_WARNING, "i2c_cdev_write: f_pos is a NULL pointer\n");
    return -EINVAL;
  }

  //printk(KERN_DEBUG "<pl_i2c> i2c_cdev_write(filp = [%p], lddev = [%p])\n", filp, lddev);
  DBG_PRINT(KERN_DEBUG, "i2c_cdev_write(filp = [%p], lddev = [%p])\n", filp, lddev);

  down_write(&lddev->rw_sem);

  if(count >= 2) {
    if(copy_from_user(tmp, buf, 2)) {
      return -EFAULT;
    }
    
    reg = tmp[0] - '0';
    val = tmp[1] - '0';

    //printk(KERN_DEBUG "  reg = %d  val = %d\n", reg, val);
    DBG_PRINT(KERN_DEBUG, "  reg = %d  val = %d\n", reg, val);

    if(reg >= 0 && reg < 16) {
      //printk(KERN_DEBUG "  Writing 0x%x to %p\n", val, lddev->regs + reg);
      DBG_PRINT(KERN_DEBUG, "  Writing 0x%x to %p\n", val, lddev->regs + reg);
      *(lddev->regs + reg) = val;
    }
  }

  (*f_pos)++;

  up_write(&lddev->rw_sem);

  return count;
}

struct file_operations i2c_fops = {
  .owner		= THIS_MODULE,
  .open		= i2c_cdev_open,
  .release	= i2c_cdev_close,
  .read		= i2c_cdev_read,
  .write		= i2c_cdev_write,
};
#endif
