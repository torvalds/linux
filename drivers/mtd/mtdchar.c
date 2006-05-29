/*
 * $Id: mtdchar.c,v 1.76 2005/11/07 11:14:20 gleixner Exp $
 *
 * Character-device access to raw MTD devices.
 *
 */

#include <linux/config.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sched.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/compatmac.h>

#include <asm/uaccess.h>

static struct class *mtd_class;

static void mtd_notify_add(struct mtd_info* mtd)
{
	if (!mtd)
		return;

	class_device_create(mtd_class, NULL, MKDEV(MTD_CHAR_MAJOR, mtd->index*2),
			    NULL, "mtd%d", mtd->index);

	class_device_create(mtd_class, NULL,
			    MKDEV(MTD_CHAR_MAJOR, mtd->index*2+1),
			    NULL, "mtd%dro", mtd->index);
}

static void mtd_notify_remove(struct mtd_info* mtd)
{
	if (!mtd)
		return;

	class_device_destroy(mtd_class, MKDEV(MTD_CHAR_MAJOR, mtd->index*2));
	class_device_destroy(mtd_class, MKDEV(MTD_CHAR_MAJOR, mtd->index*2+1));
}

static struct mtd_notifier notifier = {
	.add	= mtd_notify_add,
	.remove	= mtd_notify_remove,
};

/*
 * Data structure to hold the pointer to the mtd device as well
 * as mode information ofr various use cases.
 */
struct mtd_file_info {
	struct mtd_info *mtd;
	enum mtd_file_modes mode;
};

static loff_t mtd_lseek (struct file *file, loff_t offset, int orig)
{
	struct mtd_file_info *mfi = file->private_data;
	struct mtd_info *mtd = mfi->mtd;

	switch (orig) {
	case 0:
		/* SEEK_SET */
		break;
	case 1:
		/* SEEK_CUR */
		offset += file->f_pos;
		break;
	case 2:
		/* SEEK_END */
		offset += mtd->size;
		break;
	default:
		return -EINVAL;
	}

	if (offset >= 0 && offset < mtd->size)
		return file->f_pos = offset;

	return -EINVAL;
}



static int mtd_open(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	int devnum = minor >> 1;
	struct mtd_info *mtd;
	struct mtd_file_info *mfi;

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

	/* You can't open it RW if it's not a writeable device */
	if ((file->f_mode & 2) && !(mtd->flags & MTD_WRITEABLE)) {
		put_mtd_device(mtd);
		return -EACCES;
	}

	mfi = kzalloc(sizeof(*mfi), GFP_KERNEL);
	if (!mfi) {
		put_mtd_device(mtd);
		return -ENOMEM;
	}
	mfi->mtd = mtd;
	file->private_data = mfi;

	return 0;
} /* mtd_open */

/*====================================================================*/

static int mtd_close(struct inode *inode, struct file *file)
{
	struct mtd_file_info *mfi = file->private_data;
	struct mtd_info *mtd = mfi->mtd;

	DEBUG(MTD_DEBUG_LEVEL0, "MTD_close\n");

	if (mtd->sync)
		mtd->sync(mtd);

	put_mtd_device(mtd);
	file->private_data = NULL;
	kfree(mfi);

	return 0;
} /* mtd_close */

/* FIXME: This _really_ needs to die. In 2.5, we should lock the
   userspace buffer down and use it directly with readv/writev.
*/
#define MAX_KMALLOC_SIZE 0x20000

static ssize_t mtd_read(struct file *file, char __user *buf, size_t count,loff_t *ppos)
{
	struct mtd_file_info *mfi = file->private_data;
	struct mtd_info *mtd = mfi->mtd;
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

	if (count > MAX_KMALLOC_SIZE)
		kbuf=kmalloc(MAX_KMALLOC_SIZE, GFP_KERNEL);
	else
		kbuf=kmalloc(count, GFP_KERNEL);

	if (!kbuf)
		return -ENOMEM;

	while (count) {

		if (count > MAX_KMALLOC_SIZE)
			len = MAX_KMALLOC_SIZE;
		else
			len = count;

		switch (mfi->mode) {
		case MTD_MODE_OTP_FACTORY:
			ret = mtd->read_fact_prot_reg(mtd, *ppos, len, &retlen, kbuf);
			break;
		case MTD_MODE_OTP_USER:
			ret = mtd->read_user_prot_reg(mtd, *ppos, len, &retlen, kbuf);
			break;
		case MTD_MODE_RAW:
		{
			struct mtd_oob_ops ops;

			ops.mode = MTD_OOB_RAW;
			ops.datbuf = kbuf;
			ops.oobbuf = NULL;
			ops.len = len;

			ret = mtd->read_oob(mtd, *ppos, &ops);
			retlen = ops.retlen;
			break;
		}
		default:
			ret = mtd->read(mtd, *ppos, len, &retlen, kbuf);
		}
		/* Nand returns -EBADMSG on ecc errors, but it returns
		 * the data. For our userspace tools it is important
		 * to dump areas with ecc errors !
		 * For kernel internal usage it also might return -EUCLEAN
		 * to signal the caller that a bitflip has occured and has
		 * been corrected by the ECC algorithm.
		 * Userspace software which accesses NAND this way
		 * must be aware of the fact that it deals with NAND
		 */
		if (!ret || (ret == -EUCLEAN) || (ret == -EBADMSG)) {
			*ppos += retlen;
			if (copy_to_user(buf, kbuf, retlen)) {
				kfree(kbuf);
				return -EFAULT;
			}
			else
				total_retlen += retlen;

			count -= retlen;
			buf += retlen;
			if (retlen == 0)
				count = 0;
		}
		else {
			kfree(kbuf);
			return ret;
		}

	}

	kfree(kbuf);
	return total_retlen;
} /* mtd_read */

static ssize_t mtd_write(struct file *file, const char __user *buf, size_t count,loff_t *ppos)
{
	struct mtd_file_info *mfi = file->private_data;
	struct mtd_info *mtd = mfi->mtd;
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

	if (count > MAX_KMALLOC_SIZE)
		kbuf=kmalloc(MAX_KMALLOC_SIZE, GFP_KERNEL);
	else
		kbuf=kmalloc(count, GFP_KERNEL);

	if (!kbuf)
		return -ENOMEM;

	while (count) {

		if (count > MAX_KMALLOC_SIZE)
			len = MAX_KMALLOC_SIZE;
		else
			len = count;

		if (copy_from_user(kbuf, buf, len)) {
			kfree(kbuf);
			return -EFAULT;
		}

		switch (mfi->mode) {
		case MTD_MODE_OTP_FACTORY:
			ret = -EROFS;
			break;
		case MTD_MODE_OTP_USER:
			if (!mtd->write_user_prot_reg) {
				ret = -EOPNOTSUPP;
				break;
			}
			ret = mtd->write_user_prot_reg(mtd, *ppos, len, &retlen, kbuf);
			break;

		case MTD_MODE_RAW:
		{
			struct mtd_oob_ops ops;

			ops.mode = MTD_OOB_RAW;
			ops.datbuf = kbuf;
			ops.oobbuf = NULL;
			ops.len = len;

			ret = mtd->write_oob(mtd, *ppos, &ops);
			retlen = ops.retlen;
			break;
		}

		default:
			ret = (*(mtd->write))(mtd, *ppos, len, &retlen, kbuf);
		}
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
	}

	kfree(kbuf);
	return total_retlen;
} /* mtd_write */

/*======================================================================

    IOCTL calls for getting device parameters.

======================================================================*/
static void mtdchar_erase_callback (struct erase_info *instr)
{
	wake_up((wait_queue_head_t *)instr->priv);
}

#if defined(CONFIG_MTD_OTP) || defined(CONFIG_MTD_ONENAND_OTP)
static int otp_select_filemode(struct mtd_file_info *mfi, int mode)
{
	struct mtd_info *mtd = mfi->mtd;
	int ret = 0;

	switch (mode) {
	case MTD_OTP_FACTORY:
		if (!mtd->read_fact_prot_reg)
			ret = -EOPNOTSUPP;
		else
			mfi->mode = MTD_MODE_OTP_FACTORY;
		break;
	case MTD_OTP_USER:
		if (!mtd->read_fact_prot_reg)
			ret = -EOPNOTSUPP;
		else
			mfi->mode = MTD_MODE_OTP_USER;
		break;
	default:
		ret = -EINVAL;
	case MTD_OTP_OFF:
		break;
	}
	return ret;
}
#else
# define otp_select_filemode(f,m)	-EOPNOTSUPP
#endif

static int mtd_ioctl(struct inode *inode, struct file *file,
		     u_int cmd, u_long arg)
{
	struct mtd_file_info *mfi = file->private_data;
	struct mtd_info *mtd = mfi->mtd;
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
		struct mtd_oob_ops ops;

		if(!(file->f_mode & 2))
			return -EPERM;

		if (copy_from_user(&buf, argp, sizeof(struct mtd_oob_buf)))
			return -EFAULT;

		if (buf.length > 4096)
			return -EINVAL;

		if (!mtd->write_oob)
			ret = -EOPNOTSUPP;
		else
			ret = access_ok(VERIFY_READ, buf.ptr,
					buf.length) ? 0 : EFAULT;

		if (ret)
			return ret;

		ops.len = buf.length;
		ops.ooblen = mtd->oobsize;
		ops.ooboffs = buf.start & (mtd->oobsize - 1);
		ops.datbuf = NULL;
		ops.mode = MTD_OOB_PLACE;

		if (ops.ooboffs && ops.len > (ops.ooblen - ops.ooboffs))
			return -EINVAL;

		ops.oobbuf = kmalloc(buf.length, GFP_KERNEL);
		if (!ops.oobbuf)
			return -ENOMEM;

		if (copy_from_user(ops.oobbuf, buf.ptr, buf.length)) {
			kfree(ops.oobbuf);
			return -EFAULT;
		}

		buf.start &= ~(mtd->oobsize - 1);
		ret = mtd->write_oob(mtd, buf.start, &ops);

		if (copy_to_user(argp + sizeof(uint32_t), &ops.retlen,
				 sizeof(uint32_t)))
			ret = -EFAULT;

		kfree(ops.oobbuf);
		break;

	}

	case MEMREADOOB:
	{
		struct mtd_oob_buf buf;
		struct mtd_oob_ops ops;

		if (copy_from_user(&buf, argp, sizeof(struct mtd_oob_buf)))
			return -EFAULT;

		if (buf.length > 4096)
			return -EINVAL;

		if (!mtd->read_oob)
			ret = -EOPNOTSUPP;
		else
			ret = access_ok(VERIFY_WRITE, buf.ptr,
					buf.length) ? 0 : -EFAULT;
		if (ret)
			return ret;

		ops.len = buf.length;
		ops.ooblen = mtd->oobsize;
		ops.ooboffs = buf.start & (mtd->oobsize - 1);
		ops.datbuf = NULL;
		ops.mode = MTD_OOB_PLACE;

		if (ops.ooboffs && ops.len > (ops.ooblen - ops.ooboffs))
			return -EINVAL;

		ops.oobbuf = kmalloc(buf.length, GFP_KERNEL);
		if (!ops.oobbuf)
			return -ENOMEM;

		buf.start &= ~(mtd->oobsize - 1);
		ret = mtd->read_oob(mtd, buf.start, &ops);

		if (put_user(ops.retlen, (uint32_t __user *)argp))
			ret = -EFAULT;
		else if (ops.retlen && copy_to_user(buf.ptr, ops.oobbuf,
						    ops.retlen))
			ret = -EFAULT;

		kfree(ops.oobbuf);
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

	/* Legacy interface */
	case MEMGETOOBSEL:
	{
		struct nand_oobinfo oi;

		if (!mtd->ecclayout)
			return -EOPNOTSUPP;
		if (mtd->ecclayout->eccbytes > ARRAY_SIZE(oi.eccpos))
			return -EINVAL;

		oi.useecc = MTD_NANDECC_AUTOPLACE;
		memcpy(&oi.eccpos, mtd->ecclayout->eccpos, sizeof(oi.eccpos));
		memcpy(&oi.oobfree, mtd->ecclayout->oobfree,
		       sizeof(oi.oobfree));

		if (copy_to_user(argp, &oi, sizeof(struct nand_oobinfo)))
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

#if defined(CONFIG_MTD_OTP) || defined(CONFIG_MTD_ONENAND_OTP)
	case OTPSELECT:
	{
		int mode;
		if (copy_from_user(&mode, argp, sizeof(int)))
			return -EFAULT;

		mfi->mode = MTD_MODE_NORMAL;

		ret = otp_select_filemode(mfi, mode);

		file->f_pos = 0;
		break;
	}

	case OTPGETREGIONCOUNT:
	case OTPGETREGIONINFO:
	{
		struct otp_info *buf = kmalloc(4096, GFP_KERNEL);
		if (!buf)
			return -ENOMEM;
		ret = -EOPNOTSUPP;
		switch (mfi->mode) {
		case MTD_MODE_OTP_FACTORY:
			if (mtd->get_fact_prot_info)
				ret = mtd->get_fact_prot_info(mtd, buf, 4096);
			break;
		case MTD_MODE_OTP_USER:
			if (mtd->get_user_prot_info)
				ret = mtd->get_user_prot_info(mtd, buf, 4096);
			break;
		default:
			break;
		}
		if (ret >= 0) {
			if (cmd == OTPGETREGIONCOUNT) {
				int nbr = ret / sizeof(struct otp_info);
				ret = copy_to_user(argp, &nbr, sizeof(int));
			} else
				ret = copy_to_user(argp, buf, ret);
			if (ret)
				ret = -EFAULT;
		}
		kfree(buf);
		break;
	}

	case OTPLOCK:
	{
		struct otp_info info;

		if (mfi->mode != MTD_MODE_OTP_USER)
			return -EINVAL;
		if (copy_from_user(&info, argp, sizeof(info)))
			return -EFAULT;
		if (!mtd->lock_user_prot_reg)
			return -EOPNOTSUPP;
		ret = mtd->lock_user_prot_reg(mtd, info.start, info.length);
		break;
	}
#endif

	case ECCGETLAYOUT:
	{
		if (!mtd->ecclayout)
			return -EOPNOTSUPP;

		if (copy_to_user(argp, &mtd->ecclayout,
				 sizeof(struct nand_ecclayout)))
			return -EFAULT;
		break;
	}

	case ECCGETSTATS:
	{
		if (copy_to_user(argp, &mtd->ecc_stats,
				 sizeof(struct mtd_ecc_stats)))
			return -EFAULT;
		break;
	}

	case MTDFILEMODE:
	{
		mfi->mode = 0;

		switch(arg) {
		case MTD_MODE_OTP_FACTORY:
		case MTD_MODE_OTP_USER:
			ret = otp_select_filemode(mfi, arg);
			break;

		case MTD_MODE_RAW:
			if (!mtd->read_oob || !mtd->write_oob)
				return -EOPNOTSUPP;
			mfi->mode = arg;

		case MTD_MODE_NORMAL:
			break;
		default:
			ret = -EINVAL;
		}
		file->f_pos = 0;
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

	mtd_class = class_create(THIS_MODULE, "mtd");

	if (IS_ERR(mtd_class)) {
		printk(KERN_ERR "Error creating mtd class.\n");
		unregister_chrdev(MTD_CHAR_MAJOR, "mtd");
		return PTR_ERR(mtd_class);
	}

	register_mtd_user(&notifier);
	return 0;
}

static void __exit cleanup_mtdchar(void)
{
	unregister_mtd_user(&notifier);
	class_destroy(mtd_class);
	unregister_chrdev(MTD_CHAR_MAJOR, "mtd");
}

module_init(init_mtdchar);
module_exit(cleanup_mtdchar);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Woodhouse <dwmw2@infradead.org>");
MODULE_DESCRIPTION("Direct character-device access to MTD devices");
