/*
 * $Id: mtdchar.c,v 1.66 2005/01/05 18:05:11 dwmw2 Exp $
 *
 * Character-device access to raw MTD devices.
 *
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/compatmac.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <asm/uaccess.h>

#ifdef CONFIG_DEVFS_FS
#include <linux/devfs_fs_kernel.h>

static void mtd_notify_add(struct mtd_info* mtd)
{
	if (!mtd)
		return;

	devfs_mk_cdev(MKDEV(MTD_CHAR_MAJOR, mtd->index*2),
		      S_IFCHR | S_IRUGO | S_IWUGO, "mtd/%d", mtd->index);
		
	devfs_mk_cdev(MKDEV(MTD_CHAR_MAJOR, mtd->index*2+1),
		      S_IFCHR | S_IRUGO, "mtd/%dro", mtd->index);
}

static void mtd_notify_remove(struct mtd_info* mtd)
{
	if (!mtd)
		return;
	devfs_remove("mtd/%d", mtd->index);
	devfs_remove("mtd/%dro", mtd->index);
}

static struct mtd_notifier notifier = {
	.add	= mtd_notify_add,
	.remove	= mtd_notify_remove,
};

static inline void mtdchar_devfs_init(void)
{
	devfs_mk_dir("mtd");
	register_mtd_user(&notifier);
}

static inline void mtdchar_devfs_exit(void)
{
	unregister_mtd_user(&notifier);
	devfs_remove("mtd");
}
#else /* !DEVFS */
#define mtdchar_devfs_init() do { } while(0)
#define mtdchar_devfs_exit() do { } while(0)
#endif

static loff_t mtd_lseek (struct file *file, loff_t offset, int orig)
{
	struct mtd_info *mtd = file->private_data;

	switch (orig) {
	case 0:
		/* SEEK_SET */
		file->f_pos = offset;
		break;
	case 1:
		/* SEEK_CUR */
		file->f_pos += offset;
		break;
	case 2:
		/* SEEK_END */
		file->f_pos =mtd->size + offset;
		break;
	default:
		return -EINVAL;
	}

	if (file->f_pos < 0)
		file->f_pos = 0;
	else if (file->f_pos >= mtd->size)
		file->f_pos = mtd->size - 1;

	return file->f_pos;
}



static int mtd_open(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	int devnum = minor >> 1;
	struct mtd_info *mtd;

	DEBUG(MTD_DEBUG_LEVEL0, "MTD_open\n");

	if (devnum >= MAX_MTD_DEVICES)
		return -ENODEV;

	/* You can't open the RO devices RW */
	if ((file->f_mode & 2) && (minor & 1))
		return -EACCES;

	mtd = get_mtd_device(NULL, devnum);
	
	if (!mtd)
		return -ENODEV;
	
	if (MTD_ABSENT == mtd->type) {
		put_mtd_device(mtd);
		return -ENODEV;
	}

	file->private_data = mtd;
		
	/* You can't open it RW if it's not a writeable device */
	if ((file->f_mode & 2) && !(mtd->flags & MTD_WRITEABLE)) {
		put_mtd_device(mtd);
		return -EACCES;
	}
		
	return 0;
} /* mtd_open */

/*====================================================================*/

static int mtd_close(struct inode *inode, struct file *file)
{
	struct mtd_info *mtd;

	DEBUG(MTD_DEBUG_LEVEL0, "MTD_close\n");

	mtd = file->private_data;
	
	if (mtd->sync)
		mtd->sync(mtd);
	
	put_mtd_device(mtd);

	return 0;
} /* mtd_close */

/* FIXME: This _really_ needs to die. In 2.5, we should lock the
   userspace buffer down and use it directly with readv/writev.
*/
#define MAX_KMALLOC_SIZE 0x20000

static ssize_t mtd_read(struct file *file, char __user *buf, size_t count,loff_t *ppos)
{
	struct mtd_info *mtd = file->private_data;
	size_t retlen=0;
	size_t total_retlen=0;
	int ret=0;
	int len;
	char *kbuf;
	
	DEBUG(MTD_DEBUG_LEVEL0,"MTD_read\n");

	if (*ppos + count > mtd->size)
		count = mtd->size - *ppos;

	if (!count)
		return 0;
	
	/* FIXME: Use kiovec in 2.5 to lock down the user's buffers
	   and pass them directly to the MTD functions */
	while (count) {
		if (count > MAX_KMALLOC_SIZE) 
			len = MAX_KMALLOC_SIZE;
		else
			len = count;

		kbuf=kmalloc(len,GFP_KERNEL);
		if (!kbuf)
			return -ENOMEM;
		
		ret = MTD_READ(mtd, *ppos, len, &retlen, kbuf);
		/* Nand returns -EBADMSG on ecc errors, but it returns
		 * the data. For our userspace tools it is important
		 * to dump areas with ecc errors ! 
		 * Userspace software which accesses NAND this way
		 * must be aware of the fact that it deals with NAND
		 */
		if (!ret || (ret == -EBADMSG)) {
			*ppos += retlen;
			if (copy_to_user(buf, kbuf, retlen)) {
			        kfree(kbuf);
				return -EFAULT;
			}
			else
				total_retlen += retlen;

			count -= retlen;
			buf += retlen;
		}
		else {
			kfree(kbuf);
			return ret;
		}
		
		kfree(kbuf);
	}

	return total_retlen;
} /* mtd_read */

static ssize_t mtd_write(struct file *file, const char __user *buf, size_t count,loff_t *ppos)
{
	struct mtd_info *mtd = file->private_data;
	char *kbuf;
	size_t retlen;
	size_t total_retlen=0;
	int ret=0;
	int len;

	DEBUG(MTD_DEBUG_LEVEL0,"MTD_write\n");
	
	if (*ppos == mtd->size)
		return -ENOSPC;
	
	if (*ppos + count > mtd->size)
		count = mtd->size - *ppos;

	if (!count)
		return 0;

	while (count) {
		if (count > MAX_KMALLOC_SIZE) 
			len = MAX_KMALLOC_SIZE;
		else
			len = count;

		kbuf=kmalloc(len,GFP_KERNEL);
		if (!kbuf) {
			printk("kmalloc is null\n");
			return -ENOMEM;
		}

		if (copy_from_user(kbuf, buf, len)) {
			kfree(kbuf);
			return -EFAULT;
		}
		
	        ret = (*(mtd->write))(mtd, *ppos, len, &retlen, kbuf);
		if (!ret) {
			*ppos += retlen;
			total_retlen += retlen;
			count -= retlen;
			buf += retlen;
		}
		else {
			kfree(kbuf);
			return ret;
		}
		
		kfree(kbuf);
	}

	return total_retlen;
} /* mtd_write */

/*======================================================================

    IOCTL calls for getting device parameters.

======================================================================*/
static void mtdchar_erase_callback (struct erase_info *instr)
{
	wake_up((wait_queue_head_t *)instr->priv);
}

static int mtd_ioctl(struct inode *inode, struct file *file,
		     u_int cmd, u_long arg)
{
	struct mtd_info *mtd = file->private_data;
	void __user *argp = (void __user *)arg;
	int ret = 0;
	u_long size;
	
	DEBUG(MTD_DEBUG_LEVEL0, "MTD_ioctl\n");

	size = (cmd & IOCSIZE_MASK) >> IOCSIZE_SHIFT;
	if (cmd & IOC_IN) {
		if (!access_ok(VERIFY_READ, argp, size))
			return -EFAULT;
	}
	if (cmd & IOC_OUT) {
		if (!access_ok(VERIFY_WRITE, argp, size))
			return -EFAULT;
	}
	
	switch (cmd) {
	case MEMGETREGIONCOUNT:
		if (copy_to_user(argp, &(mtd->numeraseregions), sizeof(int)))
			return -EFAULT;
		break;

	case MEMGETREGIONINFO:
	{
		struct region_info_user ur;

		if (copy_from_user(&ur, argp, sizeof(struct region_info_user)))
			return -EFAULT;

		if (ur.regionindex >= mtd->numeraseregions)
			return -EINVAL;
		if (copy_to_user(argp, &(mtd->eraseregions[ur.regionindex]),
				sizeof(struct mtd_erase_region_info)))
			return -EFAULT;
		break;
	}

	case MEMGETINFO:
		if (copy_to_user(argp, mtd, sizeof(struct mtd_info_user)))
			return -EFAULT;
		break;

	case MEMERASE:
	{
		struct erase_info *erase;

		if(!(file->f_mode & 2))
			return -EPERM;

		erase=kmalloc(sizeof(struct erase_info),GFP_KERNEL);
		if (!erase)
			ret = -ENOMEM;
		else {
			wait_queue_head_t waitq;
			DECLARE_WAITQUEUE(wait, current);

			init_waitqueue_head(&waitq);

			memset (erase,0,sizeof(struct erase_info));
			if (copy_from_user(&erase->addr, argp,
				    sizeof(struct erase_info_user))) {
				kfree(erase);
				return -EFAULT;
			}
			erase->mtd = mtd;
			erase->callback = mtdchar_erase_callback;
			erase->priv = (unsigned long)&waitq;
			
			/*
			  FIXME: Allow INTERRUPTIBLE. Which means
			  not having the wait_queue head on the stack.
			  
			  If the wq_head is on the stack, and we
			  leave because we got interrupted, then the
			  wq_head is no longer there when the
			  callback routine tries to wake us up.
			*/
			ret = mtd->erase(mtd, erase);
			if (!ret) {
				set_current_state(TASK_UNINTERRUPTIBLE);
				add_wait_queue(&waitq, &wait);
				if (erase->state != MTD_ERASE_DONE &&
				    erase->state != MTD_ERASE_FAILED)
					schedule();
				remove_wait_queue(&waitq, &wait);
				set_current_state(TASK_RUNNING);

				ret = (erase->state == MTD_ERASE_FAILED)?-EIO:0;
			}
			kfree(erase);
		}
		break;
	}

	case MEMWRITEOOB:
	{
		struct mtd_oob_buf buf;
		void *databuf;
		ssize_t retlen;
		
		if(!(file->f_mode & 2))
			return -EPERM;

		if (copy_from_user(&buf, argp, sizeof(struct mtd_oob_buf)))
			return -EFAULT;
		
		if (buf.length > 0x4096)
			return -EINVAL;

		if (!mtd->write_oob)
			ret = -EOPNOTSUPP;
		else
			ret = access_ok(VERIFY_READ, buf.ptr,
					buf.length) ? 0 : EFAULT;

		if (ret)
			return ret;

		databuf = kmalloc(buf.length, GFP_KERNEL);
		if (!databuf)
			return -ENOMEM;
		
		if (copy_from_user(databuf, buf.ptr, buf.length)) {
			kfree(databuf);
			return -EFAULT;
		}

		ret = (mtd->write_oob)(mtd, buf.start, buf.length, &retlen, databuf);

		if (copy_to_user(argp + sizeof(uint32_t), &retlen, sizeof(uint32_t)))
			ret = -EFAULT;

		kfree(databuf);
		break;

	}

	case MEMREADOOB:
	{
		struct mtd_oob_buf buf;
		void *databuf;
		ssize_t retlen;

		if (copy_from_user(&buf, argp, sizeof(struct mtd_oob_buf)))
			return -EFAULT;
		
		if (buf.length > 0x4096)
			return -EINVAL;

		if (!mtd->read_oob)
			ret = -EOPNOTSUPP;
		else
			ret = access_ok(VERIFY_WRITE, buf.ptr,
					buf.length) ? 0 : -EFAULT;

		if (ret)
			return ret;

		databuf = kmalloc(buf.length, GFP_KERNEL);
		if (!databuf)
			return -ENOMEM;
		
		ret = (mtd->read_oob)(mtd, buf.start, buf.length, &retlen, databuf);

		if (put_user(retlen, (uint32_t __user *)argp))
			ret = -EFAULT;
		else if (retlen && copy_to_user(buf.ptr, databuf, retlen))
			ret = -EFAULT;
		
		kfree(databuf);
		break;
	}

	case MEMLOCK:
	{
		struct erase_info_user info;

		if (copy_from_user(&info, argp, sizeof(info)))
			return -EFAULT;

		if (!mtd->lock)
			ret = -EOPNOTSUPP;
		else
			ret = mtd->lock(mtd, info.start, info.length);
		break;
	}

	case MEMUNLOCK:
	{
		struct erase_info_user info;

		if (copy_from_user(&info, argp, sizeof(info)))
			return -EFAULT;

		if (!mtd->unlock)
			ret = -EOPNOTSUPP;
		else
			ret = mtd->unlock(mtd, info.start, info.length);
		break;
	}

	case MEMSETOOBSEL:
	{
		if (copy_from_user(&mtd->oobinfo, argp, sizeof(struct nand_oobinfo)))
			return -EFAULT;
		break;
	}

	case MEMGETOOBSEL:
	{
		if (copy_to_user(argp, &(mtd->oobinfo), sizeof(struct nand_oobinfo)))
			return -EFAULT;
		break;
	}

	case MEMGETBADBLOCK:
	{
		loff_t offs;
		
		if (copy_from_user(&offs, argp, sizeof(loff_t)))
			return -EFAULT;
		if (!mtd->block_isbad)
			ret = -EOPNOTSUPP;
		else
			return mtd->block_isbad(mtd, offs);
		break;
	}

	case MEMSETBADBLOCK:
	{
		loff_t offs;

		if (copy_from_user(&offs, argp, sizeof(loff_t)))
			return -EFAULT;
		if (!mtd->block_markbad)
			ret = -EOPNOTSUPP;
		else
			return mtd->block_markbad(mtd, offs);
		break;
	}

	default:
		ret = -ENOTTY;
	}

	return ret;
} /* memory_ioctl */

static struct file_operations mtd_fops = {
	.owner		= THIS_MODULE,
	.llseek		= mtd_lseek,
	.read		= mtd_read,
	.write		= mtd_write,
	.ioctl		= mtd_ioctl,
	.open		= mtd_open,
	.release	= mtd_close,
};

static int __init init_mtdchar(void)
{
	if (register_chrdev(MTD_CHAR_MAJOR, "mtd", &mtd_fops)) {
		printk(KERN_NOTICE "Can't allocate major number %d for Memory Technology Devices.\n",
		       MTD_CHAR_MAJOR);
		return -EAGAIN;
	}

	mtdchar_devfs_init();
	return 0;
}

static void __exit cleanup_mtdchar(void)
{
	mtdchar_devfs_exit();
	unregister_chrdev(MTD_CHAR_MAJOR, "mtd");
}

module_init(init_mtdchar);
module_exit(cleanup_mtdchar);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Woodhouse <dwmw2@infradead.org>");
MODULE_DESCRIPTION("Direct character-device access to MTD devices");
