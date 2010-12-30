/*
 * zcore module to export memory content and register sets for creating system
 * dumps on SCSI disks (zfcpdump). The "zcore/mem" debugfs file shows the same
 * dump format as s390 standalone dumps.
 *
 * For more information please refer to Documentation/s390/zfcpdump.txt
 *
 * Copyright IBM Corp. 2003,2008
 * Author(s): Michael Holzheu
 */

#define KMSG_COMPONENT "zdump"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>
#include <linux/debugfs.h>
#include <asm/asm-offsets.h>
#include <asm/ipl.h>
#include <asm/sclp.h>
#include <asm/setup.h>
#include <asm/sigp.h>
#include <asm/uaccess.h>
#include <asm/debug.h>
#include <asm/processor.h>
#include <asm/irqflags.h>
#include <asm/checksum.h>
#include "sclp.h"

#define TRACE(x...) debug_sprintf_event(zcore_dbf, 1, x)

#define TO_USER		0
#define TO_KERNEL	1
#define CHUNK_INFO_SIZE	34 /* 2 16-byte char, each followed by blank */

enum arch_id {
	ARCH_S390	= 0,
	ARCH_S390X	= 1,
};

/* dump system info */

struct sys_info {
	enum arch_id	 arch;
	unsigned long	 sa_base;
	u32		 sa_size;
	int		 cpu_map[NR_CPUS];
	unsigned long	 mem_size;
	struct save_area lc_mask;
};

struct ipib_info {
	unsigned long	ipib;
	u32		checksum;
}  __attribute__((packed));

static struct sys_info sys_info;
static struct debug_info *zcore_dbf;
static int hsa_available;
static struct dentry *zcore_dir;
static struct dentry *zcore_file;
static struct dentry *zcore_memmap_file;
static struct dentry *zcore_reipl_file;
static struct ipl_parameter_block *ipl_block;

/*
 * Copy memory from HSA to kernel or user memory (not reentrant):
 *
 * @dest:  Kernel or user buffer where memory should be copied to
 * @src:   Start address within HSA where data should be copied
 * @count: Size of buffer, which should be copied
 * @mode:  Either TO_KERNEL or TO_USER
 */
static int memcpy_hsa(void *dest, unsigned long src, size_t count, int mode)
{
	int offs, blk_num;
	static char buf[PAGE_SIZE] __attribute__((__aligned__(PAGE_SIZE)));

	if (count == 0)
		return 0;

	/* copy first block */
	offs = 0;
	if ((src % PAGE_SIZE) != 0) {
		blk_num = src / PAGE_SIZE + 2;
		if (sclp_sdias_copy(buf, blk_num, 1)) {
			TRACE("sclp_sdias_copy() failed\n");
			return -EIO;
		}
		offs = min((PAGE_SIZE - (src % PAGE_SIZE)), count);
		if (mode == TO_USER) {
			if (copy_to_user((__force __user void*) dest,
					 buf + (src % PAGE_SIZE), offs))
				return -EFAULT;
		} else
			memcpy(dest, buf + (src % PAGE_SIZE), offs);
	}
	if (offs == count)
		goto out;

	/* copy middle */
	for (; (offs + PAGE_SIZE) <= count; offs += PAGE_SIZE) {
		blk_num = (src + offs) / PAGE_SIZE + 2;
		if (sclp_sdias_copy(buf, blk_num, 1)) {
			TRACE("sclp_sdias_copy() failed\n");
			return -EIO;
		}
		if (mode == TO_USER) {
			if (copy_to_user((__force __user void*) dest + offs,
					 buf, PAGE_SIZE))
				return -EFAULT;
		} else
			memcpy(dest + offs, buf, PAGE_SIZE);
	}
	if (offs == count)
		goto out;

	/* copy last block */
	blk_num = (src + offs) / PAGE_SIZE + 2;
	if (sclp_sdias_copy(buf, blk_num, 1)) {
		TRACE("sclp_sdias_copy() failed\n");
		return -EIO;
	}
	if (mode == TO_USER) {
		if (copy_to_user((__force __user void*) dest + offs, buf,
				 PAGE_SIZE))
			return -EFAULT;
	} else
		memcpy(dest + offs, buf, count - offs);
out:
	return 0;
}

static int memcpy_hsa_user(void __user *dest, unsigned long src, size_t count)
{
	return memcpy_hsa((void __force *) dest, src, count, TO_USER);
}

static int memcpy_hsa_kernel(void *dest, unsigned long src, size_t count)
{
	return memcpy_hsa(dest, src, count, TO_KERNEL);
}

static int memcpy_real_user(void __user *dest, unsigned long src, size_t count)
{
	static char buf[4096];
	int offs = 0, size;

	while (offs < count) {
		size = min(sizeof(buf), count - offs);
		if (memcpy_real(buf, (void *) src + offs, size))
			return -EFAULT;
		if (copy_to_user(dest + offs, buf, size))
			return -EFAULT;
		offs += size;
	}
	return 0;
}

static int __init init_cpu_info(enum arch_id arch)
{
	struct save_area *sa;

	/* get info for boot cpu from lowcore, stored in the HSA */

	sa = kmalloc(sizeof(*sa), GFP_KERNEL);
	if (!sa)
		return -ENOMEM;
	if (memcpy_hsa_kernel(sa, sys_info.sa_base, sys_info.sa_size) < 0) {
		TRACE("could not copy from HSA\n");
		kfree(sa);
		return -EIO;
	}
	zfcpdump_save_areas[0] = sa;
	return 0;
}

static DEFINE_MUTEX(zcore_mutex);

#define DUMP_VERSION	0x5
#define DUMP_MAGIC	0xa8190173618f23fdULL
#define DUMP_ARCH_S390X	2
#define DUMP_ARCH_S390	1
#define HEADER_SIZE	4096

/* dump header dumped according to s390 crash dump format */

struct zcore_header {
	u64 magic;
	u32 version;
	u32 header_size;
	u32 dump_level;
	u32 page_size;
	u64 mem_size;
	u64 mem_start;
	u64 mem_end;
	u32 num_pages;
	u32 pad1;
	u64 tod;
	struct cpuid cpu_id;
	u32 arch_id;
	u32 volnr;
	u32 build_arch;
	u64 rmem_size;
	u8 mvdump;
	u16 cpu_cnt;
	u16 real_cpu_cnt;
	u8 end_pad1[0x200-0x061];
	u64 mvdump_sign;
	u64 mvdump_zipl_time;
	u8 end_pad2[0x800-0x210];
	u32 lc_vec[512];
} __attribute__((packed,__aligned__(16)));

static struct zcore_header zcore_header = {
	.magic		= DUMP_MAGIC,
	.version	= DUMP_VERSION,
	.header_size	= 4096,
	.dump_level	= 0,
	.page_size	= PAGE_SIZE,
	.mem_start	= 0,
#ifdef CONFIG_64BIT
	.build_arch	= DUMP_ARCH_S390X,
#else
	.build_arch	= DUMP_ARCH_S390,
#endif
};

/*
 * Copy lowcore info to buffer. Use map in order to copy only register parts.
 *
 * @buf:    User buffer
 * @sa:     Pointer to save area
 * @sa_off: Offset in save area to copy
 * @len:    Number of bytes to copy
 */
static int copy_lc(void __user *buf, void *sa, int sa_off, int len)
{
	int i;
	char *lc_mask = (char*)&sys_info.lc_mask;

	for (i = 0; i < len; i++) {
		if (!lc_mask[i + sa_off])
			continue;
		if (copy_to_user(buf + i, sa + sa_off + i, 1))
			return -EFAULT;
	}
	return 0;
}

/*
 * Copy lowcores info to memory, if necessary
 *
 * @buf:   User buffer
 * @addr:  Start address of buffer in dump memory
 * @count: Size of buffer
 */
static int zcore_add_lc(char __user *buf, unsigned long start, size_t count)
{
	unsigned long end;
	int i = 0;

	if (count == 0)
		return 0;

	end = start + count;
	while (zfcpdump_save_areas[i]) {
		unsigned long cp_start, cp_end; /* copy range */
		unsigned long sa_start, sa_end; /* save area range */
		unsigned long prefix;
		unsigned long sa_off, len, buf_off;

		prefix = zfcpdump_save_areas[i]->pref_reg;
		sa_start = prefix + sys_info.sa_base;
		sa_end = prefix + sys_info.sa_base + sys_info.sa_size;

		if ((end < sa_start) || (start > sa_end))
			goto next;
		cp_start = max(start, sa_start);
		cp_end = min(end, sa_end);

		buf_off = cp_start - start;
		sa_off = cp_start - sa_start;
		len = cp_end - cp_start;

		TRACE("copy_lc for: %lx\n", start);
		if (copy_lc(buf + buf_off, zfcpdump_save_areas[i], sa_off, len))
			return -EFAULT;
next:
		i++;
	}
	return 0;
}

/*
 * Read routine for zcore character device
 * First 4K are dump header
 * Next 32MB are HSA Memory
 * Rest is read from absolute Memory
 */
static ssize_t zcore_read(struct file *file, char __user *buf, size_t count,
			  loff_t *ppos)
{
	unsigned long mem_start; /* Start address in memory */
	size_t mem_offs;	 /* Offset in dump memory */
	size_t hdr_count;	 /* Size of header part of output buffer */
	size_t size;
	int rc;

	mutex_lock(&zcore_mutex);

	if (*ppos > (sys_info.mem_size + HEADER_SIZE)) {
		rc = -EINVAL;
		goto fail;
	}

	count = min(count, (size_t) (sys_info.mem_size + HEADER_SIZE - *ppos));

	/* Copy dump header */
	if (*ppos < HEADER_SIZE) {
		size = min(count, (size_t) (HEADER_SIZE - *ppos));
		if (copy_to_user(buf, &zcore_header + *ppos, size)) {
			rc = -EFAULT;
			goto fail;
		}
		hdr_count = size;
		mem_start = 0;
	} else {
		hdr_count = 0;
		mem_start = *ppos - HEADER_SIZE;
	}

	mem_offs = 0;

	/* Copy from HSA data */
	if (*ppos < (ZFCPDUMP_HSA_SIZE + HEADER_SIZE)) {
		size = min((count - hdr_count), (size_t) (ZFCPDUMP_HSA_SIZE
			   - mem_start));
		rc = memcpy_hsa_user(buf + hdr_count, mem_start, size);
		if (rc)
			goto fail;

		mem_offs += size;
	}

	/* Copy from real mem */
	size = count - mem_offs - hdr_count;
	rc = memcpy_real_user(buf + hdr_count + mem_offs, mem_start + mem_offs,
			      size);
	if (rc)
		goto fail;

	/*
	 * Since s390 dump analysis tools like lcrash or crash
	 * expect register sets in the prefix pages of the cpus,
	 * we copy them into the read buffer, if necessary.
	 * buf + hdr_count: Start of memory part of output buffer
	 * mem_start: Start memory address to copy from
	 * count - hdr_count: Size of memory area to copy
	 */
	if (zcore_add_lc(buf + hdr_count, mem_start, count - hdr_count)) {
		rc = -EFAULT;
		goto fail;
	}
	*ppos += count;
fail:
	mutex_unlock(&zcore_mutex);
	return (rc < 0) ? rc : count;
}

static int zcore_open(struct inode *inode, struct file *filp)
{
	if (!hsa_available)
		return -ENODATA;
	else
		return capable(CAP_SYS_RAWIO) ? 0 : -EPERM;
}

static int zcore_release(struct inode *inode, struct file *filep)
{
	diag308(DIAG308_REL_HSA, NULL);
	hsa_available = 0;
	return 0;
}

static loff_t zcore_lseek(struct file *file, loff_t offset, int orig)
{
	loff_t rc;

	mutex_lock(&zcore_mutex);
	switch (orig) {
	case 0:
		file->f_pos = offset;
		rc = file->f_pos;
		break;
	case 1:
		file->f_pos += offset;
		rc = file->f_pos;
		break;
	default:
		rc = -EINVAL;
	}
	mutex_unlock(&zcore_mutex);
	return rc;
}

static const struct file_operations zcore_fops = {
	.owner		= THIS_MODULE,
	.llseek		= zcore_lseek,
	.read		= zcore_read,
	.open		= zcore_open,
	.release	= zcore_release,
};

static ssize_t zcore_memmap_read(struct file *filp, char __user *buf,
				 size_t count, loff_t *ppos)
{
	return simple_read_from_buffer(buf, count, ppos, filp->private_data,
				       MEMORY_CHUNKS * CHUNK_INFO_SIZE);
}

static int zcore_memmap_open(struct inode *inode, struct file *filp)
{
	int i;
	char *buf;
	struct mem_chunk *chunk_array;

	chunk_array = kzalloc(MEMORY_CHUNKS * sizeof(struct mem_chunk),
			      GFP_KERNEL);
	if (!chunk_array)
		return -ENOMEM;
	detect_memory_layout(chunk_array);
	buf = kzalloc(MEMORY_CHUNKS * CHUNK_INFO_SIZE, GFP_KERNEL);
	if (!buf) {
		kfree(chunk_array);
		return -ENOMEM;
	}
	for (i = 0; i < MEMORY_CHUNKS; i++) {
		sprintf(buf + (i * CHUNK_INFO_SIZE), "%016llx %016llx ",
			(unsigned long long) chunk_array[i].addr,
			(unsigned long long) chunk_array[i].size);
		if (chunk_array[i].size == 0)
			break;
	}
	kfree(chunk_array);
	filp->private_data = buf;
	return nonseekable_open(inode, filp);
}

static int zcore_memmap_release(struct inode *inode, struct file *filp)
{
	kfree(filp->private_data);
	return 0;
}

static const struct file_operations zcore_memmap_fops = {
	.owner		= THIS_MODULE,
	.read		= zcore_memmap_read,
	.open		= zcore_memmap_open,
	.release	= zcore_memmap_release,
	.llseek		= no_llseek,
};

static ssize_t zcore_reipl_write(struct file *filp, const char __user *buf,
				 size_t count, loff_t *ppos)
{
	if (ipl_block) {
		diag308(DIAG308_SET, ipl_block);
		diag308(DIAG308_IPL, NULL);
	}
	return count;
}

static int zcore_reipl_open(struct inode *inode, struct file *filp)
{
	return nonseekable_open(inode, filp);
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

#ifdef CONFIG_32BIT

static void __init set_lc_mask(struct save_area *map)
{
	memset(&map->ext_save, 0xff, sizeof(map->ext_save));
	memset(&map->timer, 0xff, sizeof(map->timer));
	memset(&map->clk_cmp, 0xff, sizeof(map->clk_cmp));
	memset(&map->psw, 0xff, sizeof(map->psw));
	memset(&map->pref_reg, 0xff, sizeof(map->pref_reg));
	memset(&map->acc_regs, 0xff, sizeof(map->acc_regs));
	memset(&map->fp_regs, 0xff, sizeof(map->fp_regs));
	memset(&map->gp_regs, 0xff, sizeof(map->gp_regs));
	memset(&map->ctrl_regs, 0xff, sizeof(map->ctrl_regs));
}

#else /* CONFIG_32BIT */

static void __init set_lc_mask(struct save_area *map)
{
	memset(&map->fp_regs, 0xff, sizeof(map->fp_regs));
	memset(&map->gp_regs, 0xff, sizeof(map->gp_regs));
	memset(&map->psw, 0xff, sizeof(map->psw));
	memset(&map->pref_reg, 0xff, sizeof(map->pref_reg));
	memset(&map->fp_ctrl_reg, 0xff, sizeof(map->fp_ctrl_reg));
	memset(&map->tod_reg, 0xff, sizeof(map->tod_reg));
	memset(&map->timer, 0xff, sizeof(map->timer));
	memset(&map->clk_cmp, 0xff, sizeof(map->clk_cmp));
	memset(&map->acc_regs, 0xff, sizeof(map->acc_regs));
	memset(&map->ctrl_regs, 0xff, sizeof(map->ctrl_regs));
}

#endif /* CONFIG_32BIT */

/*
 * Initialize dump globals for a given architecture
 */
static int __init sys_info_init(enum arch_id arch)
{
	int rc;

	switch (arch) {
	case ARCH_S390X:
		pr_alert("DETECTED 'S390X (64 bit) OS'\n");
		break;
	case ARCH_S390:
		pr_alert("DETECTED 'S390 (32 bit) OS'\n");
		break;
	default:
		pr_alert("0x%x is an unknown architecture.\n",arch);
		return -EINVAL;
	}
	sys_info.sa_base = SAVE_AREA_BASE;
	sys_info.sa_size = sizeof(struct save_area);
	sys_info.arch = arch;
	set_lc_mask(&sys_info.lc_mask);
	rc = init_cpu_info(arch);
	if (rc)
		return rc;
	sys_info.mem_size = real_memory_size;

	return 0;
}

static int __init check_sdias(void)
{
	int rc, act_hsa_size;

	rc = sclp_sdias_blk_count();
	if (rc < 0) {
		TRACE("Could not determine HSA size\n");
		return rc;
	}
	act_hsa_size = (rc - 1) * PAGE_SIZE;
	if (act_hsa_size < ZFCPDUMP_HSA_SIZE) {
		TRACE("HSA size too small: %i\n", act_hsa_size);
		return -EINVAL;
	}
	return 0;
}

static int __init get_mem_size(unsigned long *mem)
{
	int i;
	struct mem_chunk *chunk_array;

	chunk_array = kzalloc(MEMORY_CHUNKS * sizeof(struct mem_chunk),
			      GFP_KERNEL);
	if (!chunk_array)
		return -ENOMEM;
	detect_memory_layout(chunk_array);
	for (i = 0; i < MEMORY_CHUNKS; i++) {
		if (chunk_array[i].size == 0)
			break;
		*mem += chunk_array[i].size;
	}
	kfree(chunk_array);
	return 0;
}

static int __init zcore_header_init(int arch, struct zcore_header *hdr)
{
	int rc, i;
	unsigned long memory = 0;
	u32 prefix;

	if (arch == ARCH_S390X)
		hdr->arch_id = DUMP_ARCH_S390X;
	else
		hdr->arch_id = DUMP_ARCH_S390;
	rc = get_mem_size(&memory);
	if (rc)
		return rc;
	hdr->mem_size = memory;
	hdr->rmem_size = memory;
	hdr->mem_end = sys_info.mem_size;
	hdr->num_pages = memory / PAGE_SIZE;
	hdr->tod = get_clock();
	get_cpu_id(&hdr->cpu_id);
	for (i = 0; zfcpdump_save_areas[i]; i++) {
		prefix = zfcpdump_save_areas[i]->pref_reg;
		hdr->real_cpu_cnt++;
		if (!prefix)
			continue;
		hdr->lc_vec[hdr->cpu_cnt] = prefix;
		hdr->cpu_cnt++;
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
	ipl_block = (void *) __get_free_page(GFP_KERNEL);
	if (!ipl_block)
		return -ENOMEM;
	if (ipib_info.ipib < ZFCPDUMP_HSA_SIZE)
		rc = memcpy_hsa_kernel(ipl_block, ipib_info.ipib, PAGE_SIZE);
	else
		rc = memcpy_real(ipl_block, (void *) ipib_info.ipib, PAGE_SIZE);
	if (rc || csum_partial(ipl_block, ipl_block->hdr.len, 0) !=
	    ipib_info.checksum) {
		TRACE("Checksum does not match\n");
		free_page((unsigned long) ipl_block);
		ipl_block = NULL;
	}
	return 0;
}

static int __init zcore_init(void)
{
	unsigned char arch;
	int rc;

	if (ipl_info.type != IPL_TYPE_FCP_DUMP)
		return -ENODATA;

	zcore_dbf = debug_register("zcore", 4, 1, 4 * sizeof(long));
	debug_register_view(zcore_dbf, &debug_sprintf_view);
	debug_set_level(zcore_dbf, 6);

	TRACE("devno:  %x\n", ipl_info.data.fcp.dev_id.devno);
	TRACE("wwpn:   %llx\n", (unsigned long long) ipl_info.data.fcp.wwpn);
	TRACE("lun:    %llx\n", (unsigned long long) ipl_info.data.fcp.lun);

	rc = sclp_sdias_init();
	if (rc)
		goto fail;

	rc = check_sdias();
	if (rc)
		goto fail;

	rc = memcpy_hsa_kernel(&arch, __LC_AR_MODE_ID, 1);
	if (rc)
		goto fail;

#ifdef CONFIG_64BIT
	if (arch == ARCH_S390) {
		pr_alert("The 64-bit dump tool cannot be used for a "
			 "32-bit system\n");
		rc = -EINVAL;
		goto fail;
	}
#else /* CONFIG_64BIT */
	if (arch == ARCH_S390X) {
		pr_alert("The 32-bit dump tool cannot be used for a "
			 "64-bit system\n");
		rc = -EINVAL;
		goto fail;
	}
#endif /* CONFIG_64BIT */

	rc = sys_info_init(arch);
	if (rc)
		goto fail;

	rc = zcore_header_init(arch, &zcore_header);
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
	zcore_file = debugfs_create_file("mem", S_IRUSR, zcore_dir, NULL,
					 &zcore_fops);
	if (!zcore_file) {
		rc = -ENOMEM;
		goto fail_dir;
	}
	zcore_memmap_file = debugfs_create_file("memmap", S_IRUSR, zcore_dir,
						NULL, &zcore_memmap_fops);
	if (!zcore_memmap_file) {
		rc = -ENOMEM;
		goto fail_file;
	}
	zcore_reipl_file = debugfs_create_file("reipl", S_IRUSR, zcore_dir,
						NULL, &zcore_reipl_fops);
	if (!zcore_reipl_file) {
		rc = -ENOMEM;
		goto fail_memmap_file;
	}
	hsa_available = 1;
	return 0;

fail_memmap_file:
	debugfs_remove(zcore_memmap_file);
fail_file:
	debugfs_remove(zcore_file);
fail_dir:
	debugfs_remove(zcore_dir);
fail:
	diag308(DIAG308_REL_HSA, NULL);
	return rc;
}

static void __exit zcore_exit(void)
{
	debug_unregister(zcore_dbf);
	sclp_sdias_exit();
	free_page((unsigned long) ipl_block);
	debugfs_remove(zcore_reipl_file);
	debugfs_remove(zcore_memmap_file);
	debugfs_remove(zcore_file);
	debugfs_remove(zcore_dir);
	diag308(DIAG308_REL_HSA, NULL);
}

MODULE_AUTHOR("Copyright IBM Corp. 2003,2008");
MODULE_DESCRIPTION("zcore module for zfcpdump support");
MODULE_LICENSE("GPL");

subsys_initcall(zcore_init);
module_exit(zcore_exit);
