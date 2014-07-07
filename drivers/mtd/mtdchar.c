/*
 * Copyright © 1999-2010 David Woodhouse <dwmw2@infradead.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <linux/device.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/mutex.h>
#include <linux/backing-dev.h>
#include <linux/compat.h>
#include <linux/mount.h>
#include <linux/blkpg.h>
#include <linux/magic.h>
#include <linux/major.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/map.h>

#include <asm/uaccess.h>

#include "mtdcore.h"

static DEFINE_MUTEX(mtd_mutex);

/*
 * Data structure to hold the pointer to the mtd device as well
 * as mode information of various use cases.
 */
struct mtd_file_info {
	struct mtd_info *mtd;
	struct inode *ino;
	enum mtd_file_modes mode;
};

static loff_t mtdchar_lseek(struct file *file, loff_t offset, int orig)
{
	struct mtd_file_info *mfi = file->private_data;
	return fixed_size_llseek(file, offset, orig, mfi->mtd->size);
}

static int count;
static struct vfsmount *mnt;
static struct file_system_type mtd_inodefs_type;

static int mtdchar_open(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	int devnum = minor >> 1;
	int ret = 0;
	struct mtd_info *mtd;
	struct mtd_file_info *mfi;
	struct inode *mtd_ino;

	pr_debug("MTD_open\n");

	/* You can't open the RO devices RW */
	if ((file->f_mode & FMODE_WRITE) && (minor & 1))
		return -EACCES;

	ret = simple_pin_fs(&mtd_inodefs_type, &mnt, &count);
	if (ret)
		return ret;

	mutex_lock(&mtd_mutex);
	mtd = get_mtd_device(NULL, devnum);

	if (IS_ERR(mtd)) {
		ret = PTR_ERR(mtd);
		goto out;
	}

	if (mtd->type == MTD_ABSENT) {
		ret = -ENODEV;
		goto out1;
	}

	mtd_ino = iget_locked(mnt->mnt_sb, devnum);
	if (!mtd_ino) {
		ret = -ENOMEM;
		goto out1;
	}
	if (mtd_ino->i_state & I_NEW) {
		mtd_ino->i_private = mtd;
		mtd_ino->i_mode = S_IFCHR;
		mtd_ino->i_data.backing_dev_info = mtd->backing_dev_info;
		unlock_new_inode(mtd_ino);
	}
	file->f_mapping = mtd_ino->i_mapping;

	/* You can't open it RW if it's not a writeable device */
	if ((file->f_mode & FMODE_WRITE) && !(mtd->flags & MTD_WRITEABLE)) {
		ret = -EACCES;
		goto out2;
	}

	mfi = kzalloc(sizeof(*mfi), GFP_KERNEL);
	if (!mfi) {
		ret = -ENOMEM;
		goto out2;
	}
	mfi->ino = mtd_ino;
	mfi->mtd = mtd;
	file->private_data = mfi;
	mutex_unlock(&mtd_mutex);
	return 0;

out2:
	iput(mtd_ino);
out1:
	put_mtd_device(mtd);
out:
	mutex_unlock(&mtd_mutex);
	simple_release_fs(&mnt, &count);
	return ret;
} /* mtdchar_open */

/*====================================================================*/

static int mtdchar_close(struct inode *inode, struct file *file)
{
	struct mtd_file_info *mfi = file->private_data;
	struct mtd_info *mtd = mfi->mtd;

	pr_debug("MTD_close\n");

	/* Only sync if opened RW */
	if ((file->f_mode & FMODE_WRITE))
		mtd_sync(mtd);

	iput(mfi->ino);

	put_mtd_device(mtd);
	file->private_data = NULL;
	kfree(mfi);
	simple_release_fs(&mnt, &count);

	return 0;
} /* mtdchar_close */

/* Back in June 2001, dwmw2 wrote:
 *
 *   FIXME: This _really_ needs to die. In 2.5, we should lock the
 *   userspace buffer down and use it directly with readv/writev.
 *
 * The implementation below, using mtd_kmalloc_up_to, mitigates
 * allocation failures when the system is under low-memory situations
 * or if memory is highly fragmented at the cost of reducing the
 * performance of the requested transfer due to a smaller buffer size.
 *
 * A more complex but more memory-efficient implementation based on
 * get_user_pages and iovecs to cover extents of those pages is a
 * longer-term goal, as intimated by dwmw2 above. However, for the
 * write case, this requires yet more complex head and tail transfer
 * handling when those head and tail offsets and sizes are such that
 * alignment requirements are not met in the NAND subdriver.
 */

static ssize_t mtdchar_read(struct file *file, char __user *buf, size_t count,
			loff_t *ppos)
{
	struct mtd_file_info *mfi = file->private_data;
	struct mtd_info *mtd = mfi->mtd;
	size_t retlen;
	size_t total_retlen=0;
	int ret=0;
	int len;
	size_t size = count;
	char *kbuf;

	pr_debug("MTD_read\n");

	if (*ppos + count > mtd->size)
		count = mtd->size - *ppos;

	if (!count)
		return 0;

	kbuf = mtd_kmalloc_up_to(mtd, &size);
	if (!kbuf)
		return -ENOMEM;

	while (count) {
		len = min_t(size_t, count, size);

		switch (mfi->mode) {
		case MTD_FILE_MODE_OTP_FACTORY:
			ret = mtd_read_fact_prot_reg(mtd, *ppos, len,
						     &retlen, kbuf);
			break;
		case MTD_FILE_MODE_OTP_USER:
			ret = mtd_read_user_prot_reg(mtd, *ppos, len,
						     &retlen, kbuf);
			break;
		case MTD_FILE_MODE_RAW:
		{
			struct mtd_oob_ops ops;

			ops.mode = MTD_OPS_RAW;
			ops.datbuf = kbuf;
			ops.oobbuf = NULL;
			ops.len = len;

			ret = mtd_read_oob(mtd, *ppos, &ops);
			retlen = ops.retlen;
			break;
		}
		default:
			ret = mtd_read(mtd, *ppos, len, &retlen, kbuf);
		}
		/* Nand returns -EBADMSG on ECC errors, but it returns
		 * the data. For our userspace tools it is important
		 * to dump areas with ECC errors!
		 * For kernel internal usage it also might return -EUCLEAN
		 * to signal the caller that a bitflip has occurred and has
		 * been corrected by the ECC algorithm.
		 * Userspace software which accesses NAND this way
		 * must be aware of the fact that it deals with NAND
		 */
		if (!ret || mtd_is_bitflip_or_eccerr(ret)) {
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
} /* mtdchar_read */

static ssize_t mtdchar_write(struct file *file, const char __user *buf, size_t count,
			loff_t *ppos)
{
	struct mtd_file_info *mfi = file->private_data;
	struct mtd_info *mtd = mfi->mtd;
	size_t size = count;
	char *kbuf;
	size_t retlen;
	size_t total_retlen=0;
	int ret=0;
	int len;

	pr_debug("MTD_write\n");

	if (*ppos == mtd->size)
		return -ENOSPC;

	if (*ppos + count > mtd->size)
		count = mtd->size - *ppos;

	if (!count)
		return 0;

	kbuf = mtd_kmalloc_up_to(mtd, &size);
	if (!kbuf)
		return -ENOMEM;

	while (count) {
		len = min_t(size_t, count, size);

		if (copy_from_user(kbuf, buf, len)) {
			kfree(kbuf);
			return -EFAULT;
		}

		switch (mfi->mode) {
		case MTD_FILE_MODE_OTP_FACTORY:
			ret = -EROFS;
			break;
		case MTD_FILE_MODE_OTP_USER:
			ret = mtd_write_user_prot_reg(mtd, *ppos, len,
						      &retlen, kbuf);
			break;

		case MTD_FILE_MODE_RAW:
		{
			struct mtd_oob_ops ops;

			ops.mode = MTD_OPS_RAW;
			ops.datbuf = kbuf;
			ops.oobbuf = NULL;
			ops.ooboffs = 0;
			ops.len = len;

			ret = mtd_write_oob(mtd, *ppos, &ops);
			retlen = ops.retlen;
			break;
		}

		default:
			ret = mtd_write(mtd, *ppos, len, &retlen, kbuf);
		}

		/*
		 * Return -ENOSPC only if no data could be written at all.
		 * Otherwise just return the number of bytes that actually
		 * have been written.
		 */
		if ((ret == -ENOSPC) && (total_retlen))
			break;

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
} /* mtdchar_write */

/*======================================================================

    IOCTL calls for getting device parameters.

======================================================================*/
static void mtdchar_erase_callback (struct erase_info *instr)
{
	wake_up((wait_queue_head_t *)instr->priv);
}

static int otp_select_filemode(struct mtd_file_info *mfi, int mode)
{
	struct mtd_info *mtd = mfi->mtd;
	size_t retlen;

	switch (mode) {
	case MTD_OTP_FACTORY:
		if (mtd_read_fact_prot_reg(mtd, -1, 0, &retlen, NULL) ==
				-EOPNOTSUPP)
			return -EOPNOTSUPP;

		mfi->mode = MTD_FILE_MODE_OTP_FACTORY;
		break;
	case MTD_OTP_USER:
		if (mtd_read_user_prot_reg(mtd, -1, 0, &retlen, NULL) ==
				-EOPNOTSUPP)
			return -EOPNOTSUPP;

		mfi->mode = MTD_FILE_MODE_OTP_USER;
		break;
	case MTD_OTP_OFF:
		mfi->mode = MTD_FILE_MODE_NORMAL;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int mtdchar_writeoob(struct file *file, struct mtd_info *mtd,
	uint64_t start, uint32_t length, void __user *ptr,
	uint32_t __user *retp)
{
	struct mtd_file_info *mfi = file->private_data;
	struct mtd_oob_ops ops;
	uint32_t retlen;
	int ret = 0;

	if (!(file->f_mode & FMODE_WRITE))
		return -EPERM;

	if (length > 4096)
		return -EINVAL;

	if (!mtd->_write_oob)
		ret = -EOPNOTSUPP;
	else
		ret = access_ok(VERIFY_READ, ptr, length) ? 0 : -EFAULT;

	if (ret)
		return ret;

	ops.ooblen = length;
	ops.ooboffs = start & (mtd->writesize - 1);
	ops.datbuf = NULL;
	ops.mode = (mfi->mode == MTD_FILE_MODE_RAW) ? MTD_OPS_RAW :
		MTD_OPS_PLACE_OOB;

	if (ops.ooboffs && ops.ooblen > (mtd->oobsize - ops.ooboffs))
		return -EINVAL;

	ops.oobbuf = memdup_user(ptr, length);
	if (IS_ERR(ops.oobbuf))
		return PTR_ERR(ops.oobbuf);

	start &= ~((uint64_t)mtd->writesize - 1);
	ret = mtd_write_oob(mtd, start, &ops);

	if (ops.oobretlen > 0xFFFFFFFFU)
		ret = -EOVERFLOW;
	retlen = ops.oobretlen;
	if (copy_to_user(retp, &retlen, sizeof(length)))
		ret = -EFAULT;

	kfree(ops.oobbuf);
	return ret;
}

static int mtdchar_readoob(struct file *file, struct mtd_info *mtd,
	uint64_t start, uint32_t length, void __user *ptr,
	uint32_t __user *retp)
{
	struct mtd_file_info *mfi = file->private_data;
	struct mtd_oob_ops ops;
	int ret = 0;

	if (length > 4096)
		return -EINVAL;

	if (!access_ok(VERIFY_WRITE, ptr, length))
		return -EFAULT;

	ops.ooblen = length;
	ops.ooboffs = start & (mtd->writesize - 1);
	ops.datbuf = NULL;
	ops.mode = (mfi->mode == MTD_FILE_MODE_RAW) ? MTD_OPS_RAW :
		MTD_OPS_PLACE_OOB;

	if (ops.ooboffs && ops.ooblen > (mtd->oobsize - ops.ooboffs))
		return -EINVAL;

	ops.oobbuf = kmalloc(length, GFP_KERNEL);
	if (!ops.oobbuf)
		return -ENOMEM;

	start &= ~((uint64_t)mtd->writesize - 1);
	ret = mtd_read_oob(mtd, start, &ops);

	if (put_user(ops.oobretlen, retp))
		ret = -EFAULT;
	else if (ops.oobretlen && copy_to_user(ptr, ops.oobbuf,
					    ops.oobretlen))
		ret = -EFAULT;

	kfree(ops.oobbuf);

	/*
	 * NAND returns -EBADMSG on ECC errors, but it returns the OOB
	 * data. For our userspace tools it is important to dump areas
	 * with ECC errors!
	 * For kernel internal usage it also might return -EUCLEAN
	 * to signal the caller that a bitflip has occured and has
	 * been corrected by the ECC algorithm.
	 *
	 * Note: currently the standard NAND function, nand_read_oob_std,
	 * does not calculate ECC for the OOB area, so do not rely on
	 * this behavior unless you have replaced it with your own.
	 */
	if (mtd_is_bitflip_or_eccerr(ret))
		return 0;

	return ret;
}

/*
 * Copies (and truncates, if necessary) data from the larger struct,
 * nand_ecclayout, to the smaller, deprecated layout struct,
 * nand_ecclayout_user. This is necessary only to support the deprecated
 * API ioctl ECCGETLAYOUT while allowing all new functionality to use
 * nand_ecclayout flexibly (i.e. the struct may change size in new
 * releases without requiring major rewrites).
 */
static int shrink_ecclayout(const struct nand_ecclayout *from,
		struct nand_ecclayout_user *to)
{
	int i;

	if (!from || !to)
		return -EINVAL;

	memset(to, 0, sizeof(*to));

	to->eccbytes = min((int)from->eccbytes, MTD_MAX_ECCPOS_ENTRIES);
	for (i = 0; i < to->eccbytes; i++)
		to->eccpos[i] = from->eccpos[i];

	for (i = 0; i < MTD_MAX_OOBFREE_ENTRIES; i++) {
		if (from->oobfree[i].length == 0 &&
				from->oobfree[i].offset == 0)
			break;
		to->oobavail += from->oobfree[i].length;
		to->oobfree[i] = from->oobfree[i];
	}

	return 0;
}

static int mtdchar_blkpg_ioctl(struct mtd_info *mtd,
			   struct blkpg_ioctl_arg __user *arg)
{
	struct blkpg_ioctl_arg a;
	struct blkpg_partition p;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (copy_from_user(&a, arg, sizeof(struct blkpg_ioctl_arg)))
		return -EFAULT;

	if (copy_from_user(&p, a.data, sizeof(struct blkpg_partition)))
		return -EFAULT;

	switch (a.op) {
	case BLKPG_ADD_PARTITION:

		/* Only master mtd device must be used to add partitions */
		if (mtd_is_partition(mtd))
			return -EINVAL;

		return mtd_add_partition(mtd, p.devname, p.start, p.length);

	case BLKPG_DEL_PARTITION:

		if (p.pno < 0)
			return -EINVAL;

		return mtd_del_partition(mtd, p.pno);

	default:
		return -EINVAL;
	}
}

static int mtdchar_write_ioctl(struct mtd_info *mtd,
		struct mtd_write_req __user *argp)
{
	struct mtd_write_req req;
	struct mtd_oob_ops ops;
	const void __user *usr_data, *usr_oob;
	int ret;

	if (copy_from_user(&req, argp, sizeof(req)))
		return -EFAULT;

	usr_data = (const void __user *)(uintptr_t)req.usr_data;
	usr_oob = (const void __user *)(uintptr_t)req.usr_oob;
	if (!access_ok(VERIFY_READ, usr_data, req.len) ||
	    !access_ok(VERIFY_READ, usr_oob, req.ooblen))
		return -EFAULT;

	if (!mtd->_write_oob)
		return -EOPNOTSUPP;

	ops.mode = req.mode;
	ops.len = (size_t)req.len;
	ops.ooblen = (size_t)req.ooblen;
	ops.ooboffs = 0;

	if (usr_data) {
		ops.datbuf = memdup_user(usr_data, ops.len);
		if (IS_ERR(ops.datbuf))
			return PTR_ERR(ops.datbuf);
	} else {
		ops.datbuf = NULL;
	}

	if (usr_oob) {
		ops.oobbuf = memdup_user(usr_oob, ops.ooblen);
		if (IS_ERR(ops.oobbuf)) {
			kfree(ops.datbuf);
			return PTR_ERR(ops.oobbuf);
		}
	} else {
		ops.oobbuf = NULL;
	}

	ret = mtd_write_oob(mtd, (loff_t)req.start, &ops);

	kfree(ops.datbuf);
	kfree(ops.oobbuf);

	return ret;
}

static int mtdchar_ioctl(struct file *file, u_int cmd, u_long arg)
{
	struct mtd_file_info *mfi = file->private_data;
	struct mtd_info *mtd = mfi->mtd;
	void __user *argp = (void __user *)arg;
	int ret = 0;
	u_long size;
	struct mtd_info_user info;

	pr_debug("MTD_ioctl\n");

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
		uint32_t ur_idx;
		struct mtd_erase_region_info *kr;
		struct region_info_user __user *ur = argp;

		if (get_user(ur_idx, &(ur->regionindex)))
			return -EFAULT;

		if (ur_idx >= mtd->numeraseregions)
			return -EINVAL;

		kr = &(mtd->eraseregions[ur_idx]);

		if (put_user(kr->offset, &(ur->offset))
		    || put_user(kr->erasesize, &(ur->erasesize))
		    || put_user(kr->numblocks, &(ur->numblocks)))
			return -EFAULT;

		break;
	}

	case MEMGETINFO:
		memset(&info, 0, sizeof(info));
		info.type	= mtd->type;
		info.flags	= mtd->flags;
		info.size	= mtd->size;
		info.erasesize	= mtd->erasesize;
		info.writesize	= mtd->writesize;
		info.oobsize	= mtd->oobsize;
		/* The below field is obsolete */
		info.padding	= 0;
		if (copy_to_user(argp, &info, sizeof(struct mtd_info_user)))
			return -EFAULT;
		break;

	case MEMERASE:
	case MEMERASE64:
	{
		struct erase_info *erase;

		if(!(file->f_mode & FMODE_WRITE))
			return -EPERM;

		erase=kzalloc(sizeof(struct erase_info),GFP_KERNEL);
		if (!erase)
			ret = -ENOMEM;
		else {
			wait_queue_head_t waitq;
			DECLARE_WAITQUEUE(wait, current);

			init_waitqueue_head(&waitq);

			if (cmd == MEMERASE64) {
				struct erase_info_user64 einfo64;

				if (copy_from_user(&einfo64, argp,
					    sizeof(struct erase_info_user64))) {
					kfree(erase);
					return -EFAULT;
				}
				erase->addr = einfo64.start;
				erase->len = einfo64.length;
			} else {
				struct erase_info_user einfo32;

				if (copy_from_user(&einfo32, argp,
					    sizeof(struct erase_info_user))) {
					kfree(erase);
					return -EFAULT;
				}
				erase->addr = einfo32.start;
				erase->len = einfo32.length;
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
			ret = mtd_erase(mtd, erase);
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
		struct mtd_oob_buf __user *buf_user = argp;

		/* NOTE: writes return length to buf_user->length */
		if (copy_from_user(&buf, argp, sizeof(buf)))
			ret = -EFAULT;
		else
			ret = mtdchar_writeoob(file, mtd, buf.start, buf.length,
				buf.ptr, &buf_user->length);
		break;
	}

	case MEMREADOOB:
	{
		struct mtd_oob_buf buf;
		struct mtd_oob_buf __user *buf_user = argp;

		/* NOTE: writes return length to buf_user->start */
		if (copy_from_user(&buf, argp, sizeof(buf)))
			ret = -EFAULT;
		else
			ret = mtdchar_readoob(file, mtd, buf.start, buf.length,
				buf.ptr, &buf_user->start);
		break;
	}

	case MEMWRITEOOB64:
	{
		struct mtd_oob_buf64 buf;
		struct mtd_oob_buf64 __user *buf_user = argp;

		if (copy_from_user(&buf, argp, sizeof(buf)))
			ret = -EFAULT;
		else
			ret = mtdchar_writeoob(file, mtd, buf.start, buf.length,
				(void __user *)(uintptr_t)buf.usr_ptr,
				&buf_user->length);
		break;
	}

	case MEMREADOOB64:
	{
		struct mtd_oob_buf64 buf;
		struct mtd_oob_buf64 __user *buf_user = argp;

		if (copy_from_user(&buf, argp, sizeof(buf)))
			ret = -EFAULT;
		else
			ret = mtdchar_readoob(file, mtd, buf.start, buf.length,
				(void __user *)(uintptr_t)buf.usr_ptr,
				&buf_user->length);
		break;
	}

	case MEMWRITE:
	{
		ret = mtdchar_write_ioctl(mtd,
		      (struct mtd_write_req __user *)arg);
		break;
	}

	case MEMLOCK:
	{
		struct erase_info_user einfo;

		if (copy_from_user(&einfo, argp, sizeof(einfo)))
			return -EFAULT;

		ret = mtd_lock(mtd, einfo.start, einfo.length);
		break;
	}

	case MEMUNLOCK:
	{
		struct erase_info_user einfo;

		if (copy_from_user(&einfo, argp, sizeof(einfo)))
			return -EFAULT;

		ret = mtd_unlock(mtd, einfo.start, einfo.length);
		break;
	}

	case MEMISLOCKED:
	{
		struct erase_info_user einfo;

		if (copy_from_user(&einfo, argp, sizeof(einfo)))
			return -EFAULT;

		ret = mtd_is_locked(mtd, einfo.start, einfo.length);
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
		oi.eccbytes = mtd->ecclayout->eccbytes;

		if (copy_to_user(argp, &oi, sizeof(struct nand_oobinfo)))
			return -EFAULT;
		break;
	}

	case MEMGETBADBLOCK:
	{
		loff_t offs;

		if (copy_from_user(&offs, argp, sizeof(loff_t)))
			return -EFAULT;
		return mtd_block_isbad(mtd, offs);
		break;
	}

	case MEMSETBADBLOCK:
	{
		loff_t offs;

		if (copy_from_user(&offs, argp, sizeof(loff_t)))
			return -EFAULT;
		return mtd_block_markbad(mtd, offs);
		break;
	}

	case OTPSELECT:
	{
		int mode;
		if (copy_from_user(&mode, argp, sizeof(int)))
			return -EFAULT;

		mfi->mode = MTD_FILE_MODE_NORMAL;

		ret = otp_select_filemode(mfi, mode);

		file->f_pos = 0;
		break;
	}

	case OTPGETREGIONCOUNT:
	case OTPGETREGIONINFO:
	{
		struct otp_info *buf = kmalloc(4096, GFP_KERNEL);
		size_t retlen;
		if (!buf)
			return -ENOMEM;
		switch (mfi->mode) {
		case MTD_FILE_MODE_OTP_FACTORY:
			ret = mtd_get_fact_prot_info(mtd, 4096, &retlen, buf);
			break;
		case MTD_FILE_MODE_OTP_USER:
			ret = mtd_get_user_prot_info(mtd, 4096, &retlen, buf);
			break;
		default:
			ret = -EINVAL;
			break;
		}
		if (!ret) {
			if (cmd == OTPGETREGIONCOUNT) {
				int nbr = retlen / sizeof(struct otp_info);
				ret = copy_to_user(argp, &nbr, sizeof(int));
			} else
				ret = copy_to_user(argp, buf, retlen);
			if (ret)
				ret = -EFAULT;
		}
		kfree(buf);
		break;
	}

	case OTPLOCK:
	{
		struct otp_info oinfo;

		if (mfi->mode != MTD_FILE_MODE_OTP_USER)
			return -EINVAL;
		if (copy_from_user(&oinfo, argp, sizeof(oinfo)))
			return -EFAULT;
		ret = mtd_lock_user_prot_reg(mtd, oinfo.start, oinfo.length);
		break;
	}

	/* This ioctl is being deprecated - it truncates the ECC layout */
	case ECCGETLAYOUT:
	{
		struct nand_ecclayout_user *usrlay;

		if (!mtd->ecclayout)
			return -EOPNOTSUPP;

		usrlay = kmalloc(sizeof(*usrlay), GFP_KERNEL);
		if (!usrlay)
			return -ENOMEM;

		shrink_ecclayout(mtd->ecclayout, usrlay);

		if (copy_to_user(argp, usrlay, sizeof(*usrlay)))
			ret = -EFAULT;
		kfree(usrlay);
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
		case MTD_FILE_MODE_OTP_FACTORY:
		case MTD_FILE_MODE_OTP_USER:
			ret = otp_select_filemode(mfi, arg);
			break;

		case MTD_FILE_MODE_RAW:
			if (!mtd_has_oob(mtd))
				return -EOPNOTSUPP;
			mfi->mode = arg;

		case MTD_FILE_MODE_NORMAL:
			break;
		default:
			ret = -EINVAL;
		}
		file->f_pos = 0;
		break;
	}

	case BLKPG:
	{
		ret = mtdchar_blkpg_ioctl(mtd,
		      (struct blkpg_ioctl_arg __user *)arg);
		break;
	}

	case BLKRRPART:
	{
		/* No reread partition feature. Just return ok */
		ret = 0;
		break;
	}

	default:
		ret = -ENOTTY;
	}

	return ret;
} /* memory_ioctl */

static long mtdchar_unlocked_ioctl(struct file *file, u_int cmd, u_long arg)
{
	int ret;

	mutex_lock(&mtd_mutex);
	ret = mtdchar_ioctl(file, cmd, arg);
	mutex_unlock(&mtd_mutex);

	return ret;
}

#ifdef CONFIG_COMPAT

struct mtd_oob_buf32 {
	u_int32_t start;
	u_int32_t length;
	compat_caddr_t ptr;	/* unsigned char* */
};

#define MEMWRITEOOB32		_IOWR('M', 3, struct mtd_oob_buf32)
#define MEMREADOOB32		_IOWR('M', 4, struct mtd_oob_buf32)

static long mtdchar_compat_ioctl(struct file *file, unsigned int cmd,
	unsigned long arg)
{
	struct mtd_file_info *mfi = file->private_data;
	struct mtd_info *mtd = mfi->mtd;
	void __user *argp = compat_ptr(arg);
	int ret = 0;

	mutex_lock(&mtd_mutex);

	switch (cmd) {
	case MEMWRITEOOB32:
	{
		struct mtd_oob_buf32 buf;
		struct mtd_oob_buf32 __user *buf_user = argp;

		if (copy_from_user(&buf, argp, sizeof(buf)))
			ret = -EFAULT;
		else
			ret = mtdchar_writeoob(file, mtd, buf.start,
				buf.length, compat_ptr(buf.ptr),
				&buf_user->length);
		break;
	}

	case MEMREADOOB32:
	{
		struct mtd_oob_buf32 buf;
		struct mtd_oob_buf32 __user *buf_user = argp;

		/* NOTE: writes return length to buf->start */
		if (copy_from_user(&buf, argp, sizeof(buf)))
			ret = -EFAULT;
		else
			ret = mtdchar_readoob(file, mtd, buf.start,
				buf.length, compat_ptr(buf.ptr),
				&buf_user->start);
		break;
	}
	default:
		ret = mtdchar_ioctl(file, cmd, (unsigned long)argp);
	}

	mutex_unlock(&mtd_mutex);

	return ret;
}

#endif /* CONFIG_COMPAT */

/*
 * try to determine where a shared mapping can be made
 * - only supported for NOMMU at the moment (MMU can't doesn't copy private
 *   mappings)
 */
#ifndef CONFIG_MMU
static unsigned long mtdchar_get_unmapped_area(struct file *file,
					   unsigned long addr,
					   unsigned long len,
					   unsigned long pgoff,
					   unsigned long flags)
{
	struct mtd_file_info *mfi = file->private_data;
	struct mtd_info *mtd = mfi->mtd;
	unsigned long offset;
	int ret;

	if (addr != 0)
		return (unsigned long) -EINVAL;

	if (len > mtd->size || pgoff >= (mtd->size >> PAGE_SHIFT))
		return (unsigned long) -EINVAL;

	offset = pgoff << PAGE_SHIFT;
	if (offset > mtd->size - len)
		return (unsigned long) -EINVAL;

	ret = mtd_get_unmapped_area(mtd, len, offset, flags);
	return ret == -EOPNOTSUPP ? -ENODEV : ret;
}
#endif

/*
 * set up a mapping for shared memory segments
 */
static int mtdchar_mmap(struct file *file, struct vm_area_struct *vma)
{
#ifdef CONFIG_MMU
	struct mtd_file_info *mfi = file->private_data;
	struct mtd_info *mtd = mfi->mtd;
	struct map_info *map = mtd->priv;

        /* This is broken because it assumes the MTD device is map-based
	   and that mtd->priv is a valid struct map_info.  It should be
	   replaced with something that uses the mtd_get_unmapped_area()
	   operation properly. */
	if (0 /*mtd->type == MTD_RAM || mtd->type == MTD_ROM*/) {
#ifdef pgprot_noncached
		if (file->f_flags & O_DSYNC || map->phys >= __pa(high_memory))
			vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
#endif
		return vm_iomap_memory(vma, map->phys, map->size);
	}
	return -ENODEV;
#else
	return vma->vm_flags & VM_SHARED ? 0 : -EACCES;
#endif
}

static const struct file_operations mtd_fops = {
	.owner		= THIS_MODULE,
	.llseek		= mtdchar_lseek,
	.read		= mtdchar_read,
	.write		= mtdchar_write,
	.unlocked_ioctl	= mtdchar_unlocked_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= mtdchar_compat_ioctl,
#endif
	.open		= mtdchar_open,
	.release	= mtdchar_close,
	.mmap		= mtdchar_mmap,
#ifndef CONFIG_MMU
	.get_unmapped_area = mtdchar_get_unmapped_area,
#endif
};

static const struct super_operations mtd_ops = {
	.drop_inode = generic_delete_inode,
	.statfs = simple_statfs,
};

static struct dentry *mtd_inodefs_mount(struct file_system_type *fs_type,
				int flags, const char *dev_name, void *data)
{
	return mount_pseudo(fs_type, "mtd_inode:", &mtd_ops, NULL, MTD_INODE_FS_MAGIC);
}

static struct file_system_type mtd_inodefs_type = {
       .name = "mtd_inodefs",
       .mount = mtd_inodefs_mount,
       .kill_sb = kill_anon_super,
};
MODULE_ALIAS_FS("mtd_inodefs");

int __init init_mtdchar(void)
{
	int ret;

	ret = __register_chrdev(MTD_CHAR_MAJOR, 0, 1 << MINORBITS,
				   "mtd", &mtd_fops);
	if (ret < 0) {
		pr_err("Can't allocate major number %d for MTD\n",
		       MTD_CHAR_MAJOR);
		return ret;
	}

	ret = register_filesystem(&mtd_inodefs_type);
	if (ret) {
		pr_err("Can't register mtd_inodefs filesystem, error %d\n",
		       ret);
		goto err_unregister_chdev;
	}

	return ret;

err_unregister_chdev:
	__unregister_chrdev(MTD_CHAR_MAJOR, 0, 1 << MINORBITS, "mtd");
	return ret;
}

void __exit cleanup_mtdchar(void)
{
	unregister_filesystem(&mtd_inodefs_type);
	__unregister_chrdev(MTD_CHAR_MAJOR, 0, 1 << MINORBITS, "mtd");
}

MODULE_ALIAS_CHARDEV_MAJOR(MTD_CHAR_MAJOR);
