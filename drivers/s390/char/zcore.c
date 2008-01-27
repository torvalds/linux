/*
 * zcore module to export memory content and register sets for creating system
 * dumps on SCSI disks (zfcpdump). The "zcore/mem" debugfs file shows the same
 * dump format as s390 standalone dumps.
 *
 * For more information please refer to Documentation/s390/zfcpdump.txt
 *
 * Copyright IBM Corp. 2003,2007
 * Author(s): Michael Holzheu
 */

#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/utsname.h>
#include <linux/debugfs.h>
#include <asm/ipl.h>
#include <asm/sclp.h>
#include <asm/setup.h>
#include <asm/sigp.h>
#include <asm/uaccess.h>
#include <asm/debug.h>
#include <asm/processor.h>
#include <asm/irqflags.h>
#include "sclp.h"

#define TRACE(x...) debug_sprintf_event(zcore_dbf, 1, x)
#define MSG(x...) printk( KERN_ALERT x )
#define ERROR_MSG(x...) printk ( KERN_ALERT "DUMP: " x )

#define TO_USER		0
#define TO_KERNEL	1

enum arch_id {
	ARCH_S390	= 0,
	ARCH_S390X	= 1,
};

/* dump system info */

struct sys_info {
	enum arch_id	arch;
	unsigned long	sa_base;
	u32		sa_size;
	int		cpu_map[NR_CPUS];
	unsigned long	mem_size;
	union save_area	lc_mask;
};

static struct sys_info sys_info;
static struct debug_info *zcore_dbf;
static int hsa_available;
static struct dentry *zcore_dir;
static struct dentry *zcore_file;

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

static int memcpy_real(void *dest, unsigned long src, size_t count)
{
	unsigned long flags;
	int rc = -EFAULT;
	register unsigned long _dest asm("2") = (unsigned long) dest;
	register unsigned long _len1 asm("3") = (unsigned long) count;
	register unsigned long _src  asm("4") = src;
	register unsigned long _len2 asm("5") = (unsigned long) count;

	if (count == 0)
		return 0;
	flags = __raw_local_irq_stnsm(0xf8UL); /* switch to real mode */
	asm volatile (
		"0:	mvcle	%1,%2,0x0\n"
		"1:	jo	0b\n"
		"	lhi	%0,0x0\n"
		"2:\n"
		EX_TABLE(1b,2b)
		: "+d" (rc), "+d" (_dest), "+d" (_src), "+d" (_len1),
		  "+d" (_len2), "=m" (*((long*)dest))
		: "m" (*((long*)src))
		: "cc", "memory");
	__raw_local_irq_ssm(flags);

	return rc;
}

static int memcpy_real_user(void __user *dest, unsigned long src, size_t count)
{
	static char buf[4096];
	int offs = 0, size;

	while (offs < count) {
		size = min(sizeof(buf), count - offs);
		if (memcpy_real(buf, src + offs, size))
			return -EFAULT;
		if (copy_to_user(dest + offs, buf, size))
			return -EFAULT;
		offs += size;
	}
	return 0;
}

#ifdef __s390x__
/*
 * Convert s390x (64 bit) cpu info to s390 (32 bit) cpu info
 */
static void __init s390x_to_s390_regs(union save_area *out, union save_area *in,
				      int cpu)
{
	int i;

	for (i = 0; i < 16; i++) {
		out->s390.gp_regs[i] = in->s390x.gp_regs[i] & 0x00000000ffffffff;
		out->s390.acc_regs[i] = in->s390x.acc_regs[i];
		out->s390.ctrl_regs[i] =
			in->s390x.ctrl_regs[i] & 0x00000000ffffffff;
	}
	/* locore for 31 bit has only space for fpregs 0,2,4,6 */
	out->s390.fp_regs[0] = in->s390x.fp_regs[0];
	out->s390.fp_regs[1] = in->s390x.fp_regs[2];
	out->s390.fp_regs[2] = in->s390x.fp_regs[4];
	out->s390.fp_regs[3] = in->s390x.fp_regs[6];
	memcpy(&(out->s390.psw[0]), &(in->s390x.psw[0]), 4);
	out->s390.psw[1] |= 0x8; /* set bit 12 */
	memcpy(&(out->s390.psw[4]),&(in->s390x.psw[12]), 4);
	out->s390.psw[4] |= 0x80; /* set (31bit) addressing bit */
	out->s390.pref_reg = in->s390x.pref_reg;
	out->s390.timer = in->s390x.timer;
	out->s390.clk_cmp = in->s390x.clk_cmp;
}

static void __init s390x_to_s390_save_areas(void)
{
	int i = 1;
	static union save_area tmp;

	while (zfcpdump_save_areas[i]) {
		s390x_to_s390_regs(&tmp, zfcpdump_save_areas[i], i);
		memcpy(zfcpdump_save_areas[i], &tmp, sizeof(tmp));
		i++;
	}
}

#endif /* __s390x__ */

static int __init init_cpu_info(enum arch_id arch)
{
	union save_area *sa;

	/* get info for boot cpu from lowcore, stored in the HSA */

	sa = kmalloc(sizeof(*sa), GFP_KERNEL);
	if (!sa) {
		ERROR_MSG("kmalloc failed: %s: %i\n",__FUNCTION__, __LINE__);
		return -ENOMEM;
	}
	if (memcpy_hsa_kernel(sa, sys_info.sa_base, sys_info.sa_size) < 0) {
		ERROR_MSG("could not copy from HSA\n");
		kfree(sa);
		return -EIO;
	}
	zfcpdump_save_areas[0] = sa;

#ifdef __s390x__
	/* convert s390x regs to s390, if we are dumping an s390 Linux */

	if (arch == ARCH_S390)
		s390x_to_s390_save_areas();
#endif

	return 0;
}

static DEFINE_MUTEX(zcore_mutex);

#define DUMP_VERSION	0x3
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
	cpuid_t cpu_id;
	u32 arch_id;
	u32 volnr;
	u32 build_arch;
	u64 rmem_size;
	char pad2[4016];
} __attribute__((packed,__aligned__(16)));

static struct zcore_header zcore_header = {
	.magic		= DUMP_MAGIC,
	.version	= DUMP_VERSION,
	.header_size	= 4096,
	.dump_level	= 0,
	.page_size	= PAGE_SIZE,
	.mem_start	= 0,
#ifdef __s390x__
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

		if (sys_info.arch == ARCH_S390)
			prefix = zfcpdump_save_areas[i]->s390.pref_reg;
		else
			prefix = zfcpdump_save_areas[i]->s390x.pref_reg;

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


static void __init set_s390_lc_mask(union save_area *map)
{
	memset(&map->s390.ext_save, 0xff, sizeof(map->s390.ext_save));
	memset(&map->s390.timer, 0xff, sizeof(map->s390.timer));
	memset(&map->s390.clk_cmp, 0xff, sizeof(map->s390.clk_cmp));
	memset(&map->s390.psw, 0xff, sizeof(map->s390.psw));
	memset(&map->s390.pref_reg, 0xff, sizeof(map->s390.pref_reg));
	memset(&map->s390.acc_regs, 0xff, sizeof(map->s390.acc_regs));
	memset(&map->s390.fp_regs, 0xff, sizeof(map->s390.fp_regs));
	memset(&map->s390.gp_regs, 0xff, sizeof(map->s390.gp_regs));
	memset(&map->s390.ctrl_regs, 0xff, sizeof(map->s390.ctrl_regs));
}

static void __init set_s390x_lc_mask(union save_area *map)
{
	memset(&map->s390x.fp_regs, 0xff, sizeof(map->s390x.fp_regs));
	memset(&map->s390x.gp_regs, 0xff, sizeof(map->s390x.gp_regs));
	memset(&map->s390x.psw, 0xff, sizeof(map->s390x.psw));
	memset(&map->s390x.pref_reg, 0xff, sizeof(map->s390x.pref_reg));
	memset(&map->s390x.fp_ctrl_reg, 0xff, sizeof(map->s390x.fp_ctrl_reg));
	memset(&map->s390x.tod_reg, 0xff, sizeof(map->s390x.tod_reg));
	memset(&map->s390x.timer, 0xff, sizeof(map->s390x.timer));
	memset(&map->s390x.clk_cmp, 0xff, sizeof(map->s390x.clk_cmp));
	memset(&map->s390x.acc_regs, 0xff, sizeof(map->s390x.acc_regs));
	memset(&map->s390x.ctrl_regs, 0xff, sizeof(map->s390x.ctrl_regs));
}

/*
 * Initialize dump globals for a given architecture
 */
static int __init sys_info_init(enum arch_id arch)
{
	switch (arch) {
	case ARCH_S390X:
		MSG("DETECTED 'S390X (64 bit) OS'\n");
		sys_info.sa_base = SAVE_AREA_BASE_S390X;
		sys_info.sa_size = sizeof(struct save_area_s390x);
		set_s390x_lc_mask(&sys_info.lc_mask);
		break;
	case ARCH_S390:
		MSG("DETECTED 'S390 (32 bit) OS'\n");
		sys_info.sa_base = SAVE_AREA_BASE_S390;
		sys_info.sa_size = sizeof(struct save_area_s390);
		set_s390_lc_mask(&sys_info.lc_mask);
		break;
	default:
		ERROR_MSG("unknown architecture 0x%x.\n",arch);
		return -EINVAL;
	}
	sys_info.arch = arch;
	if (init_cpu_info(arch)) {
		ERROR_MSG("get cpu info failed\n");
		return -ENOMEM;
	}
	sys_info.mem_size = real_memory_size;

	return 0;
}

static int __init check_sdias(void)
{
	int rc, act_hsa_size;

	rc = sclp_sdias_blk_count();
	if (rc < 0) {
		ERROR_MSG("Could not determine HSA size\n");
		return rc;
	}
	act_hsa_size = (rc - 1) * PAGE_SIZE;
	if (act_hsa_size < ZFCPDUMP_HSA_SIZE) {
		ERROR_MSG("HSA size too small: %i\n", act_hsa_size);
		return -EINVAL;
	}
	return 0;
}

static void __init zcore_header_init(int arch, struct zcore_header *hdr)
{
	if (arch == ARCH_S390X)
		hdr->arch_id = DUMP_ARCH_S390X;
	else
		hdr->arch_id = DUMP_ARCH_S390;
	hdr->mem_size = sys_info.mem_size;
	hdr->rmem_size = sys_info.mem_size;
	hdr->mem_end = sys_info.mem_size;
	hdr->num_pages = sys_info.mem_size / PAGE_SIZE;
	hdr->tod = get_clock();
	get_cpu_id(&hdr->cpu_id);
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
	if (rc) {
		ERROR_MSG("Dump initialization failed\n");
		goto fail;
	}

	rc = memcpy_hsa_kernel(&arch, __LC_AR_MODE_ID, 1);
	if (rc) {
		ERROR_MSG("sdial memcpy for arch id failed\n");
		goto fail;
	}

#ifndef __s390x__
	if (arch == ARCH_S390X) {
		ERROR_MSG("32 bit dumper can't dump 64 bit system!\n");
		rc = -EINVAL;
		goto fail;
	}
#endif

	rc = sys_info_init(arch);
	if (rc) {
		ERROR_MSG("arch init failed\n");
		goto fail;
	}

	zcore_header_init(arch, &zcore_header);

	zcore_dir = debugfs_create_dir("zcore" , NULL);
	if (!zcore_dir) {
		rc = -ENOMEM;
		goto fail;
	}
	zcore_file = debugfs_create_file("mem", S_IRUSR, zcore_dir, NULL,
					 &zcore_fops);
	if (!zcore_file) {
		debugfs_remove(zcore_dir);
		rc = -ENOMEM;
		goto fail;
	}
	hsa_available = 1;
	return 0;

fail:
	diag308(DIAG308_REL_HSA, NULL);
	return rc;
}

static void __exit zcore_exit(void)
{
	debug_unregister(zcore_dbf);
	sclp_sdias_exit();
	diag308(DIAG308_REL_HSA, NULL);
}

MODULE_AUTHOR("Copyright IBM Corp. 2003,2007");
MODULE_DESCRIPTION("zcore module for zfcpdump support");
MODULE_LICENSE("GPL");

subsys_initcall(zcore_init);
module_exit(zcore_exit);
