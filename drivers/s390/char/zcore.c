// SPDX-License-Identifier: GPL-1.0+
/*
 * zcore module to export memory content and register sets for creating system
 * dumps on SCSI/NVMe disks (zfcp/nvme dump).
 *
 * For more information please refer to Documentation/s390/zfcpdump.rst
 *
 * Copyright IBM Corp. 2003, 2008
 * Author(s): Michael Holzheu
 */

#define KMSG_COMPONENT "zdump"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/debugfs.h>

#include <asm/asm-offsets.h>
#include <asm/ipl.h>
#include <asm/sclp.h>
#include <asm/setup.h>
#include <linux/uaccess.h>
#include <asm/debug.h>
#include <asm/processor.h>
#include <asm/irqflags.h>
#include <asm/checksum.h>
#include <asm/os_info.h>
#include <asm/switch_to.h>
#include "sclp.h"

#define TRACE(x...) debug_sprintf_event(zcore_dbf, 1, x)

enum arch_id {
	ARCH_S390	= 0,
	ARCH_S390X	= 1,
};

struct ipib_info {
	unsigned long	ipib;
	u32		checksum;
}  __attribute__((packed));

static struct debug_info *zcore_dbf;
static int hsa_available;
static struct dentry *zcore_dir;
static struct dentry *zcore_reipl_file;
static struct dentry *zcore_hsa_file;
static struct ipl_parameter_block *zcore_ipl_block;

static DEFINE_MUTEX(hsa_buf_mutex);
static char hsa_buf[PAGE_SIZE] __aligned(PAGE_SIZE);

/*
 * Copy memory from HSA to user memory (not reentrant):
 *
 * @dest:  User buffer where memory should be copied to
 * @src:   Start address within HSA where data should be copied
 * @count: Size of buffer, which should be copied
 */
int memcpy_hsa_user(void __user *dest, unsigned long src, size_t count)
{
	unsigned long offset, bytes;

	if (!hsa_available)
		return -ENODATA;

	mutex_lock(&hsa_buf_mutex);
	while (count) {
		if (sclp_sdias_copy(hsa_buf, src / PAGE_SIZE + 2, 1)) {
			TRACE("sclp_sdias_copy() failed\n");
			mutex_unlock(&hsa_buf_mutex);
			return -EIO;
		}
		offset = src % PAGE_SIZE;
		bytes = min(PAGE_SIZE - offset, count);
		if (copy_to_user(dest, hsa_buf + offset, bytes)) {
			mutex_unlock(&hsa_buf_mutex);
			return -EFAULT;
		}
		src += bytes;
		dest += bytes;
		count -= bytes;
	}
	mutex_unlock(&hsa_buf_mutex);
	return 0;
}

/*
 * Copy memory from HSA to kernel memory (not reentrant):
 *
 * @dest:  Kernel or user buffer where memory should be copied to
 * @src:   Start address within HSA where data should be copied
 * @count: Size of buffer, which should be copied
 */
int memcpy_hsa_kernel(void *dest, unsigned long src, size_t count)
{
	unsigned long offset, bytes;

	if (!hsa_available)
		return -ENODATA;

	mutex_lock(&hsa_buf_mutex);
	while (count) {
		if (sclp_sdias_copy(hsa_buf, src / PAGE_SIZE + 2, 1)) {
			TRACE("sclp_sdias_copy() failed\n");
			mutex_unlock(&hsa_buf_mutex);
			return -EIO;
		}
		offset = src % PAGE_SIZE;
		bytes = min(PAGE_SIZE - offset, count);
		memcpy(dest, hsa_buf + offset, bytes);
		src += bytes;
		dest += bytes;
		count -= bytes;
	}
	mutex_unlock(&hsa_buf_mutex);
	return 0;
}

static int __init init_cpu_info(void)
{
	struct save_area *sa;

	/* get info for boot cpu from lowcore, stored in the HSA */
	sa = save_area_boot_cpu();
	if (!sa)
		return -ENOMEM;
	if (memcpy_hsa_kernel(hsa_buf, __LC_FPREGS_SAVE_AREA, 512) < 0) {
		TRACE("could not copy from HSA\n");
		return -EIO;
	}
	save_area_add_regs(sa, hsa_buf); /* vx registers are saved in smp.c */
	return 0;
}

/*
 * Release the HSA
 */
static void release_hsa(void)
{
	diag308(DIAG308_REL_HSA, NULL);
	hsa_available = 0;
}

static ssize_t zcore_reipl_write(struct file *filp, const char __user *buf,
				 size_t count, loff_t *ppos)
{
	if (zcore_ipl_block) {
		diag308(DIAG308_SET, zcore_ipl_block);
		diag308(DIAG308_LOAD_CLEAR, NULL);
	}
	return count;
}

static int zcore_reipl_open(struct inode *inode, struct file *filp)
{
	return stream_open(inode, filp);
}

static int zcore_reipl_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static const struct file_operations zcore_reipl_fops = {
	.owner		= THIS_MODULE,
	.write		= zcore_reipl_write,
	.open		= zcore_reipl_open,
	.release	= zcore_reipl_release,
	.llseek		= no_llseek,
};

static ssize_t zcore_hsa_read(struct file *filp, char __user *buf,
			      size_t count, loff_t *ppos)
{
	static char str[18];

	if (hsa_available)
		snprintf(str, sizeof(str), "%lx\n", sclp.hsa_size);
	else
		snprintf(str, sizeof(str), "0\n");
	return simple_read_from_buffer(buf, count, ppos, str, strlen(str));
}

static ssize_t zcore_hsa_write(struct file *filp, const char __user *buf,
			       size_t count, loff_t *ppos)
{
	char value;

	if (*ppos != 0)
		return -EPIPE;
	if (copy_from_user(&value, buf, 1))
		return -EFAULT;
	if (value != '0')
		return -EINVAL;
	release_hsa();
	return count;
}

static const struct file_operations zcore_hsa_fops = {
	.owner		= THIS_MODULE,
	.write		= zcore_hsa_write,
	.read		= zcore_hsa_read,
	.open		= nonseekable_open,
	.llseek		= no_llseek,
};

static int __init check_sdias(void)
{
	if (!sclp.hsa_size) {
		TRACE("Could not determine HSA size\n");
		return -ENODEV;
	}
	return 0;
}

/*
 * Provide IPL parameter information block from either HSA or memory
 * for future reipl
 */
static int __init zcore_reipl_init(void)
{
	struct ipib_info ipib_info;
	int rc;

	rc = memcpy_hsa_kernel(&ipib_info, __LC_DUMP_REIPL, sizeof(ipib_info));
	if (rc)
		return rc;
	if (ipib_info.ipib == 0)
		return 0;
	zcore_ipl_block = (void *) __get_free_page(GFP_KERNEL);
	if (!zcore_ipl_block)
		return -ENOMEM;
	if (ipib_info.ipib < sclp.hsa_size)
		rc = memcpy_hsa_kernel(zcore_ipl_block, ipib_info.ipib,
				       PAGE_SIZE);
	else
		rc = memcpy_real(zcore_ipl_block, (void *) ipib_info.ipib,
				 PAGE_SIZE);
	if (rc || (__force u32)csum_partial(zcore_ipl_block, zcore_ipl_block->hdr.len, 0) !=
	    ipib_info.checksum) {
		TRACE("Checksum does not match\n");
		free_page((unsigned long) zcore_ipl_block);
		zcore_ipl_block = NULL;
	}
	return 0;
}

static int __init zcore_init(void)
{
	unsigned char arch;
	int rc;

	if (!is_ipl_type_dump())
		return -ENODATA;
	if (OLDMEM_BASE)
		return -ENODATA;

	zcore_dbf = debug_register("zcore", 4, 1, 4 * sizeof(long));
	debug_register_view(zcore_dbf, &debug_sprintf_view);
	debug_set_level(zcore_dbf, 6);

	if (ipl_info.type == IPL_TYPE_FCP_DUMP) {
		TRACE("type:   fcp\n");
		TRACE("devno:  %x\n", ipl_info.data.fcp.dev_id.devno);
		TRACE("wwpn:   %llx\n", (unsigned long long) ipl_info.data.fcp.wwpn);
		TRACE("lun:    %llx\n", (unsigned long long) ipl_info.data.fcp.lun);
	} else if (ipl_info.type == IPL_TYPE_NVME_DUMP) {
		TRACE("type:   nvme\n");
		TRACE("fid:    %x\n", ipl_info.data.nvme.fid);
		TRACE("nsid:   %x\n", ipl_info.data.nvme.nsid);
	}

	rc = sclp_sdias_init();
	if (rc)
		goto fail;

	rc = check_sdias();
	if (rc)
		goto fail;
	hsa_available = 1;

	rc = memcpy_hsa_kernel(&arch, __LC_AR_MODE_ID, 1);
	if (rc)
		goto fail;

	if (arch == ARCH_S390) {
		pr_alert("The 64-bit dump tool cannot be used for a "
			 "32-bit system\n");
		rc = -EINVAL;
		goto fail;
	}

	pr_alert("The dump process started for a 64-bit operating system\n");
	rc = init_cpu_info();
	if (rc)
		goto fail;

	rc = zcore_reipl_init();
	if (rc)
		goto fail;

	zcore_dir = debugfs_create_dir("zcore" , NULL);
	if (!zcore_dir) {
		rc = -ENOMEM;
		goto fail;
	}
	zcore_reipl_file = debugfs_create_file("reipl", S_IRUSR, zcore_dir,
						NULL, &zcore_reipl_fops);
	if (!zcore_reipl_file) {
		rc = -ENOMEM;
		goto fail_dir;
	}
	zcore_hsa_file = debugfs_create_file("hsa", S_IRUSR|S_IWUSR, zcore_dir,
					     NULL, &zcore_hsa_fops);
	if (!zcore_hsa_file) {
		rc = -ENOMEM;
		goto fail_reipl_file;
	}
	return 0;

fail_reipl_file:
	debugfs_remove(zcore_reipl_file);
fail_dir:
	debugfs_remove(zcore_dir);
fail:
	diag308(DIAG308_REL_HSA, NULL);
	return rc;
}
subsys_initcall(zcore_init);
