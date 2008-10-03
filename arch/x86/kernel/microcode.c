/*
 *	Intel CPU Microcode Update Driver for Linux
 *
 *	Copyright (C) 2000-2006 Tigran Aivazian <tigran@aivazian.fsnet.co.uk>
 *		      2006	Shaohua Li <shaohua.li@intel.com>
 *
 *	This driver allows to upgrade microcode on Intel processors
 *	belonging to IA-32 family - PentiumPro, Pentium II,
 *	Pentium III, Xeon, Pentium 4, etc.
 *
 *	Reference: Section 8.11 of Volume 3a, IA-32 Intel? Architecture
 *	Software Developer's Manual
 *	Order Number 253668 or free download from:
 *
 *	http://developer.intel.com/design/pentium4/manuals/253668.htm
 *
 *	For more information, go to http://www.urbanmyth.org/microcode
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	1.0	16 Feb 2000, Tigran Aivazian <tigran@sco.com>
 *		Initial release.
 *	1.01	18 Feb 2000, Tigran Aivazian <tigran@sco.com>
 *		Added read() support + cleanups.
 *	1.02	21 Feb 2000, Tigran Aivazian <tigran@sco.com>
 *		Added 'device trimming' support. open(O_WRONLY) zeroes
 *		and frees the saved copy of applied microcode.
 *	1.03	29 Feb 2000, Tigran Aivazian <tigran@sco.com>
 *		Made to use devfs (/dev/cpu/microcode) + cleanups.
 *	1.04	06 Jun 2000, Simon Trimmer <simon@veritas.com>
 *		Added misc device support (now uses both devfs and misc).
 *		Added MICROCODE_IOCFREE ioctl to clear memory.
 *	1.05	09 Jun 2000, Simon Trimmer <simon@veritas.com>
 *		Messages for error cases (non Intel & no suitable microcode).
 *	1.06	03 Aug 2000, Tigran Aivazian <tigran@veritas.com>
 *		Removed ->release(). Removed exclusive open and status bitmap.
 *		Added microcode_rwsem to serialize read()/write()/ioctl().
 *		Removed global kernel lock usage.
 *	1.07	07 Sep 2000, Tigran Aivazian <tigran@veritas.com>
 *		Write 0 to 0x8B msr and then cpuid before reading revision,
 *		so that it works even if there were no update done by the
 *		BIOS. Otherwise, reading from 0x8B gives junk (which happened
 *		to be 0 on my machine which is why it worked even when I
 *		disabled update by the BIOS)
 *		Thanks to Eric W. Biederman <ebiederman@lnxi.com> for the fix.
 *	1.08	11 Dec 2000, Richard Schaal <richard.schaal@intel.com> and
 *			     Tigran Aivazian <tigran@veritas.com>
 *		Intel Pentium 4 processor support and bugfixes.
 *	1.09	30 Oct 2001, Tigran Aivazian <tigran@veritas.com>
 *		Bugfix for HT (Hyper-Threading) enabled processors
 *		whereby processor resources are shared by all logical processors
 *		in a single CPU package.
 *	1.10	28 Feb 2002 Asit K Mallick <asit.k.mallick@intel.com> and
 *		Tigran Aivazian <tigran@veritas.com>,
 *		Serialize updates as required on HT processors due to speculative
 *		nature of implementation.
 *	1.11	22 Mar 2002 Tigran Aivazian <tigran@veritas.com>
 *		Fix the panic when writing zero-length microcode chunk.
 *	1.12	29 Sep 2003 Nitin Kamble <nitin.a.kamble@intel.com>,
 *		Jun Nakajima <jun.nakajima@intel.com>
 *		Support for the microcode updates in the new format.
 *	1.13	10 Oct 2003 Tigran Aivazian <tigran@veritas.com>
 *		Removed ->read() method and obsoleted MICROCODE_IOCFREE ioctl
 *		because we no longer hold a copy of applied microcode
 *		in kernel memory.
 *	1.14	25 Jun 2004 Tigran Aivazian <tigran@veritas.com>
 *		Fix sigmatch() macro to handle old CPUs with pf == 0.
 *		Thanks to Stuart Swales for pointing out this bug.
 */

//#define DEBUG /* pr_debug */
#include <linux/capability.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/cpumask.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/miscdevice.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/cpu.h>
#include <linux/firmware.h>
#include <linux/platform_device.h>

#include <asm/msr.h>
#include <asm/uaccess.h>
#include <asm/processor.h>

MODULE_DESCRIPTION("Intel CPU (IA-32) Microcode Update Driver");
MODULE_AUTHOR("Tigran Aivazian <tigran@aivazian.fsnet.co.uk>");
MODULE_LICENSE("GPL");

#define MICROCODE_VERSION 	"1.14a"

#define DEFAULT_UCODE_DATASIZE 	(2000) 	  /* 2000 bytes */
#define MC_HEADER_SIZE		(sizeof (microcode_header_t))  	  /* 48 bytes */
#define DEFAULT_UCODE_TOTALSIZE (DEFAULT_UCODE_DATASIZE + MC_HEADER_SIZE) /* 2048 bytes */
#define EXT_HEADER_SIZE		(sizeof (struct extended_sigtable)) /* 20 bytes */
#define EXT_SIGNATURE_SIZE	(sizeof (struct extended_signature)) /* 12 bytes */
#define DWSIZE			(sizeof (u32))
#define get_totalsize(mc) \
	(((microcode_t *)mc)->hdr.totalsize ? \
	 ((microcode_t *)mc)->hdr.totalsize : DEFAULT_UCODE_TOTALSIZE)
#define get_datasize(mc) \
	(((microcode_t *)mc)->hdr.datasize ? \
	 ((microcode_t *)mc)->hdr.datasize : DEFAULT_UCODE_DATASIZE)

#define sigmatch(s1, s2, p1, p2) \
	(((s1) == (s2)) && (((p1) & (p2)) || (((p1) == 0) && ((p2) == 0))))

#define exttable_size(et) ((et)->count * EXT_SIGNATURE_SIZE + EXT_HEADER_SIZE)

/* serialize access to the physical write to MSR 0x79 */
static DEFINE_SPINLOCK(microcode_update_lock);

/* no concurrent ->write()s are allowed on /dev/cpu/microcode */
static DEFINE_MUTEX(microcode_mutex);

static struct ucode_cpu_info {
	int valid;
	unsigned int sig;
	unsigned int pf;
	unsigned int rev;
	microcode_t *mc;
} ucode_cpu_info[NR_CPUS];

static void collect_cpu_info(int cpu_num)
{
	struct cpuinfo_x86 *c = &cpu_data(cpu_num);
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu_num;
	unsigned int val[2];

	/* We should bind the task to the CPU */
	BUG_ON(raw_smp_processor_id() != cpu_num);
	uci->pf = uci->rev = 0;
	uci->mc = NULL;
	uci->valid = 1;

	if (c->x86_vendor != X86_VENDOR_INTEL || c->x86 < 6 ||
	    	cpu_has(c, X86_FEATURE_IA64)) {
		printk(KERN_ERR "microcode: CPU%d not a capable Intel "
			"processor\n", cpu_num);
		uci->valid = 0;
		return;
	}

	uci->sig = cpuid_eax(0x00000001);

	if ((c->x86_model >= 5) || (c->x86 > 6)) {
		/* get processor flags from MSR 0x17 */
		rdmsr(MSR_IA32_PLATFORM_ID, val[0], val[1]);
		uci->pf = 1 << ((val[1] >> 18) & 7);
	}

	wrmsr(MSR_IA32_UCODE_REV, 0, 0);
	/* see notes above for revision 1.07.  Apparent chip bug */
	sync_core();
	/* get the current revision from MSR 0x8B */
	rdmsr(MSR_IA32_UCODE_REV, val[0], uci->rev);
	pr_debug("microcode: collect_cpu_info : sig=0x%x, pf=0x%x, rev=0x%x\n",
			uci->sig, uci->pf, uci->rev);
}

static inline int microcode_update_match(int cpu_num,
	microcode_header_t *mc_header, int sig, int pf)
{
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu_num;

	if (!sigmatch(sig, uci->sig, pf, uci->pf)
		|| mc_header->rev <= uci->rev)
		return 0;
	return 1;
}

static int microcode_sanity_check(void *mc)
{
	microcode_header_t *mc_header = mc;
	struct extended_sigtable *ext_header = NULL;
	struct extended_signature *ext_sig;
	unsigned long total_size, data_size, ext_table_size;
	int sum, orig_sum, ext_sigcount = 0, i;

	total_size = get_totalsize(mc_header);
	data_size = get_datasize(mc_header);
	if (data_size + MC_HEADER_SIZE > total_size) {
		printk(KERN_ERR "microcode: error! "
			"Bad data size in microcode data file\n");
		return -EINVAL;
	}

	if (mc_header->ldrver != 1 || mc_header->hdrver != 1) {
		printk(KERN_ERR "microcode: error! "
			"Unknown microcode update format\n");
		return -EINVAL;
	}
	ext_table_size = total_size - (MC_HEADER_SIZE + data_size);
	if (ext_table_size) {
		if ((ext_table_size < EXT_HEADER_SIZE)
		 || ((ext_table_size - EXT_HEADER_SIZE) % EXT_SIGNATURE_SIZE)) {
			printk(KERN_ERR "microcode: error! "
				"Small exttable size in microcode data file\n");
			return -EINVAL;
		}
		ext_header = mc + MC_HEADER_SIZE + data_size;
		if (ext_table_size != exttable_size(ext_header)) {
			printk(KERN_ERR "microcode: error! "
				"Bad exttable size in microcode data file\n");
			return -EFAULT;
		}
		ext_sigcount = ext_header->count;
	}

	/* check extended table checksum */
	if (ext_table_size) {
		int ext_table_sum = 0;
		int *ext_tablep = (int *)ext_header;

		i = ext_table_size / DWSIZE;
		while (i--)
			ext_table_sum += ext_tablep[i];
		if (ext_table_sum) {
			printk(KERN_WARNING "microcode: aborting, "
				"bad extended signature table checksum\n");
			return -EINVAL;
		}
	}

	/* calculate the checksum */
	orig_sum = 0;
	i = (MC_HEADER_SIZE + data_size) / DWSIZE;
	while (i--)
		orig_sum += ((int *)mc)[i];
	if (orig_sum) {
		printk(KERN_ERR "microcode: aborting, bad checksum\n");
		return -EINVAL;
	}
	if (!ext_table_size)
		return 0;
	/* check extended signature checksum */
	for (i = 0; i < ext_sigcount; i++) {
		ext_sig = (void *)ext_header + EXT_HEADER_SIZE +
			  EXT_SIGNATURE_SIZE * i;
		sum = orig_sum
			- (mc_header->sig + mc_header->pf + mc_header->cksum)
			+ (ext_sig->sig + ext_sig->pf + ext_sig->cksum);
		if (sum) {
			printk(KERN_ERR "microcode: aborting, bad checksum\n");
			return -EINVAL;
		}
	}
	return 0;
}

/*
 * return 0 - no update found
 * return 1 - found update
 * return < 0 - error
 */
static int get_maching_microcode(void *mc, int cpu)
{
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu;
	microcode_header_t *mc_header = mc;
	struct extended_sigtable *ext_header;
	unsigned long total_size = get_totalsize(mc_header);
	int ext_sigcount, i;
	struct extended_signature *ext_sig;
	void *new_mc;

	if (microcode_update_match(cpu, mc_header,
			mc_header->sig, mc_header->pf))
		goto find;

	if (total_size <= get_datasize(mc_header) + MC_HEADER_SIZE)
		return 0;

	ext_header = mc + get_datasize(mc_header) + MC_HEADER_SIZE;
	ext_sigcount = ext_header->count;
	ext_sig = (void *)ext_header + EXT_HEADER_SIZE;
	for (i = 0; i < ext_sigcount; i++) {
		if (microcode_update_match(cpu, mc_header,
				ext_sig->sig, ext_sig->pf))
			goto find;
		ext_sig++;
	}
	return 0;
find:
	pr_debug("microcode: CPU%d found a matching microcode update with"
		" version 0x%x (current=0x%x)\n", cpu, mc_header->rev,uci->rev);
	new_mc = vmalloc(total_size);
	if (!new_mc) {
		printk(KERN_ERR "microcode: error! Can not allocate memory\n");
		return -ENOMEM;
	}

	/* free previous update file */
	vfree(uci->mc);

	memcpy(new_mc, mc, total_size);
	uci->mc = new_mc;
	return 1;
}

static void apply_microcode(int cpu)
{
	unsigned long flags;
	unsigned int val[2];
	int cpu_num = raw_smp_processor_id();
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu_num;

	/* We should bind the task to the CPU */
	BUG_ON(cpu_num != cpu);

	if (uci->mc == NULL)
		return;

	/* serialize access to the physical write to MSR 0x79 */
	spin_lock_irqsave(&microcode_update_lock, flags);

	/* write microcode via MSR 0x79 */
	wrmsr(MSR_IA32_UCODE_WRITE,
		(unsigned long) uci->mc->bits,
		(unsigned long) uci->mc->bits >> 16 >> 16);
	wrmsr(MSR_IA32_UCODE_REV, 0, 0);

	/* see notes above for revision 1.07.  Apparent chip bug */
	sync_core();

	/* get the current revision from MSR 0x8B */
	rdmsr(MSR_IA32_UCODE_REV, val[0], val[1]);

	spin_unlock_irqrestore(&microcode_update_lock, flags);
	if (val[1] != uci->mc->hdr.rev) {
		printk(KERN_ERR "microcode: CPU%d update from revision "
			"0x%x to 0x%x failed\n", cpu_num, uci->rev, val[1]);
		return;
	}
	printk(KERN_INFO "microcode: CPU%d updated from revision "
	       "0x%x to 0x%x, date = %08x \n",
	       cpu_num, uci->rev, val[1], uci->mc->hdr.date);
	uci->rev = val[1];
}

#ifdef CONFIG_MICROCODE_OLD_INTERFACE
static void __user *user_buffer;	/* user area microcode data buffer */
static unsigned int user_buffer_size;	/* it's size */

static long get_next_ucode(void **mc, long offset)
{
	microcode_header_t mc_header;
	unsigned long total_size;

	/* No more data */
	if (offset >= user_buffer_size)
		return 0;
	if (copy_from_user(&mc_header, user_buffer + offset, MC_HEADER_SIZE)) {
		printk(KERN_ERR "microcode: error! Can not read user data\n");
		return -EFAULT;
	}
	total_size = get_totalsize(&mc_header);
	if (offset + total_size > user_buffer_size) {
		printk(KERN_ERR "microcode: error! Bad total size in microcode "
				"data file\n");
		return -EINVAL;
	}
	*mc = vmalloc(total_size);
	if (!*mc)
		return -ENOMEM;
	if (copy_from_user(*mc, user_buffer + offset, total_size)) {
		printk(KERN_ERR "microcode: error! Can not read user data\n");
		vfree(*mc);
		return -EFAULT;
	}
	return offset + total_size;
}

static int do_microcode_update (void)
{
	long cursor = 0;
	int error = 0;
	void *new_mc = NULL;
	int cpu;
	cpumask_t old;

	old = current->cpus_allowed;

	while ((cursor = get_next_ucode(&new_mc, cursor)) > 0) {
		error = microcode_sanity_check(new_mc);
		if (error)
			goto out;
		/*
		 * It's possible the data file has multiple matching ucode,
		 * lets keep searching till the latest version
		 */
		for_each_online_cpu(cpu) {
			struct ucode_cpu_info *uci = ucode_cpu_info + cpu;

			if (!uci->valid)
				continue;
			set_cpus_allowed_ptr(current, &cpumask_of_cpu(cpu));
			error = get_maching_microcode(new_mc, cpu);
			if (error < 0)
				goto out;
			if (error == 1)
				apply_microcode(cpu);
		}
		vfree(new_mc);
	}
out:
	if (cursor > 0)
		vfree(new_mc);
	if (cursor < 0)
		error = cursor;
	set_cpus_allowed_ptr(current, &old);
	return error;
}

static int microcode_open (struct inode *unused1, struct file *unused2)
{
	cycle_kernel_lock();
	return capable(CAP_SYS_RAWIO) ? 0 : -EPERM;
}

static ssize_t microcode_write (struct file *file, const char __user *buf, size_t len, loff_t *ppos)
{
	ssize_t ret;

	if ((len >> PAGE_SHIFT) > num_physpages) {
		printk(KERN_ERR "microcode: too much data (max %ld pages)\n", num_physpages);
		return -EINVAL;
	}

	get_online_cpus();
	mutex_lock(&microcode_mutex);

	user_buffer = (void __user *) buf;
	user_buffer_size = (int) len;

	ret = do_microcode_update();
	if (!ret)
		ret = (ssize_t)len;

	mutex_unlock(&microcode_mutex);
	put_online_cpus();

	return ret;
}

static const struct file_operations microcode_fops = {
	.owner		= THIS_MODULE,
	.write		= microcode_write,
	.open		= microcode_open,
};

static struct miscdevice microcode_dev = {
	.minor		= MICROCODE_MINOR,
	.name		= "microcode",
	.fops		= &microcode_fops,
};

static int __init microcode_dev_init (void)
{
	int error;

	error = misc_register(&microcode_dev);
	if (error) {
		printk(KERN_ERR
			"microcode: can't misc_register on minor=%d\n",
			MICROCODE_MINOR);
		return error;
	}

	return 0;
}

static void microcode_dev_exit (void)
{
	misc_deregister(&microcode_dev);
}

MODULE_ALIAS_MISCDEV(MICROCODE_MINOR);
#else
#define microcode_dev_init() 0
#define microcode_dev_exit() do { } while(0)
#endif

static long get_next_ucode_from_buffer(void **mc, const u8 *buf,
	unsigned long size, long offset)
{
	microcode_header_t *mc_header;
	unsigned long total_size;

	/* No more data */
	if (offset >= size)
		return 0;
	mc_header = (microcode_header_t *)(buf + offset);
	total_size = get_totalsize(mc_header);

	if (offset + total_size > size) {
		printk(KERN_ERR "microcode: error! Bad data in microcode data file\n");
		return -EINVAL;
	}

	*mc = vmalloc(total_size);
	if (!*mc) {
		printk(KERN_ERR "microcode: error! Can not allocate memory\n");
		return -ENOMEM;
	}
	memcpy(*mc, buf + offset, total_size);
	return offset + total_size;
}

/* fake device for request_firmware */
static struct platform_device *microcode_pdev;

static int cpu_request_microcode(int cpu)
{
	char name[30];
	struct cpuinfo_x86 *c = &cpu_data(cpu);
	const struct firmware *firmware;
	const u8 *buf;
	unsigned long size;
	long offset = 0;
	int error;
	void *mc;

	/* We should bind the task to the CPU */
	BUG_ON(cpu != raw_smp_processor_id());
	sprintf(name,"intel-ucode/%02x-%02x-%02x",
		c->x86, c->x86_model, c->x86_mask);
	error = request_firmware(&firmware, name, &microcode_pdev->dev);
	if (error) {
		pr_debug("microcode: data file %s load failed\n", name);
		return error;
	}
	buf = firmware->data;
	size = firmware->size;
	while ((offset = get_next_ucode_from_buffer(&mc, buf, size, offset))
			> 0) {
		error = microcode_sanity_check(mc);
		if (error)
			break;
		error = get_maching_microcode(mc, cpu);
		if (error < 0)
			break;
		/*
		 * It's possible the data file has multiple matching ucode,
		 * lets keep searching till the latest version
		 */
		if (error == 1) {
			apply_microcode(cpu);
			error = 0;
		}
		vfree(mc);
	}
	if (offset > 0)
		vfree(mc);
	if (offset < 0)
		error = offset;
	release_firmware(firmware);

	return error;
}

static int apply_microcode_check_cpu(int cpu)
{
	struct cpuinfo_x86 *c = &cpu_data(cpu);
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu;
	cpumask_t old;
	unsigned int val[2];
	int err = 0;

	/* Check if the microcode is available */
	if (!uci->mc)
		return 0;

	old = current->cpus_allowed;
	set_cpus_allowed_ptr(current, &cpumask_of_cpu(cpu));

	/* Check if the microcode we have in memory matches the CPU */
	if (c->x86_vendor != X86_VENDOR_INTEL || c->x86 < 6 ||
	    cpu_has(c, X86_FEATURE_IA64) || uci->sig != cpuid_eax(0x00000001))
		err = -EINVAL;

	if (!err && ((c->x86_model >= 5) || (c->x86 > 6))) {
		/* get processor flags from MSR 0x17 */
		rdmsr(MSR_IA32_PLATFORM_ID, val[0], val[1]);
		if (uci->pf != (1 << ((val[1] >> 18) & 7)))
			err = -EINVAL;
	}

	if (!err) {
		wrmsr(MSR_IA32_UCODE_REV, 0, 0);
		/* see notes above for revision 1.07.  Apparent chip bug */
		sync_core();
		/* get the current revision from MSR 0x8B */
		rdmsr(MSR_IA32_UCODE_REV, val[0], val[1]);
		if (uci->rev != val[1])
			err = -EINVAL;
	}

	if (!err)
		apply_microcode(cpu);
	else
		printk(KERN_ERR "microcode: Could not apply microcode to CPU%d:"
			" sig=0x%x, pf=0x%x, rev=0x%x\n",
			cpu, uci->sig, uci->pf, uci->rev);

	set_cpus_allowed_ptr(current, &old);
	return err;
}

static void microcode_init_cpu(int cpu, int resume)
{
	cpumask_t old;
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu;

	old = current->cpus_allowed;

	set_cpus_allowed_ptr(current, &cpumask_of_cpu(cpu));
	mutex_lock(&microcode_mutex);
	collect_cpu_info(cpu);
	if (uci->valid && system_state == SYSTEM_RUNNING && !resume)
		cpu_request_microcode(cpu);
	mutex_unlock(&microcode_mutex);
	set_cpus_allowed_ptr(current, &old);
}

static void microcode_fini_cpu(int cpu)
{
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu;

	mutex_lock(&microcode_mutex);
	uci->valid = 0;
	vfree(uci->mc);
	uci->mc = NULL;
	mutex_unlock(&microcode_mutex);
}

static ssize_t reload_store(struct sys_device *dev,
			    struct sysdev_attribute *attr,
			    const char *buf, size_t sz)
{
	struct ucode_cpu_info *uci = ucode_cpu_info + dev->id;
	char *end;
	unsigned long val = simple_strtoul(buf, &end, 0);
	int err = 0;
	int cpu = dev->id;

	if (end == buf)
		return -EINVAL;
	if (val == 1) {
		cpumask_t old = current->cpus_allowed;

		get_online_cpus();
		set_cpus_allowed_ptr(current, &cpumask_of_cpu(cpu));

		mutex_lock(&microcode_mutex);
		if (uci->valid)
			err = cpu_request_microcode(cpu);
		mutex_unlock(&microcode_mutex);
		put_online_cpus();
		set_cpus_allowed_ptr(current, &old);
	}
	if (err)
		return err;
	return sz;
}

static ssize_t version_show(struct sys_device *dev,
			struct sysdev_attribute *attr, char *buf)
{
	struct ucode_cpu_info *uci = ucode_cpu_info + dev->id;

	return sprintf(buf, "0x%x\n", uci->rev);
}

static ssize_t pf_show(struct sys_device *dev,
			struct sysdev_attribute *attr, char *buf)
{
	struct ucode_cpu_info *uci = ucode_cpu_info + dev->id;

	return sprintf(buf, "0x%x\n", uci->pf);
}

static SYSDEV_ATTR(reload, 0200, NULL, reload_store);
static SYSDEV_ATTR(version, 0400, version_show, NULL);
static SYSDEV_ATTR(processor_flags, 0400, pf_show, NULL);

static struct attribute *mc_default_attrs[] = {
	&attr_reload.attr,
	&attr_version.attr,
	&attr_processor_flags.attr,
	NULL
};

static struct attribute_group mc_attr_group = {
	.attrs = mc_default_attrs,
	.name = "microcode",
};

static int __mc_sysdev_add(struct sys_device *sys_dev, int resume)
{
	int err, cpu = sys_dev->id;
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu;

	if (!cpu_online(cpu))
		return 0;

	pr_debug("microcode: CPU%d added\n", cpu);
	memset(uci, 0, sizeof(*uci));

	err = sysfs_create_group(&sys_dev->kobj, &mc_attr_group);
	if (err)
		return err;

	microcode_init_cpu(cpu, resume);

	return 0;
}

static int mc_sysdev_add(struct sys_device *sys_dev)
{
	return __mc_sysdev_add(sys_dev, 0);
}

static int mc_sysdev_remove(struct sys_device *sys_dev)
{
	int cpu = sys_dev->id;

	if (!cpu_online(cpu))
		return 0;

	pr_debug("microcode: CPU%d removed\n", cpu);
	microcode_fini_cpu(cpu);
	sysfs_remove_group(&sys_dev->kobj, &mc_attr_group);
	return 0;
}

static int mc_sysdev_resume(struct sys_device *dev)
{
	int cpu = dev->id;

	if (!cpu_online(cpu))
		return 0;
	pr_debug("microcode: CPU%d resumed\n", cpu);
	/* only CPU 0 will apply ucode here */
	apply_microcode(0);
	return 0;
}

static struct sysdev_driver mc_sysdev_driver = {
	.add = mc_sysdev_add,
	.remove = mc_sysdev_remove,
	.resume = mc_sysdev_resume,
};

static __cpuinit int
mc_cpu_callback(struct notifier_block *nb, unsigned long action, void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;
	struct sys_device *sys_dev;

	sys_dev = get_cpu_sysdev(cpu);
	switch (action) {
	case CPU_UP_CANCELED_FROZEN:
		/* The CPU refused to come up during a system resume */
		microcode_fini_cpu(cpu);
		break;
	case CPU_ONLINE:
	case CPU_DOWN_FAILED:
		mc_sysdev_add(sys_dev);
		break;
	case CPU_ONLINE_FROZEN:
		/* System-wide resume is in progress, try to apply microcode */
		if (apply_microcode_check_cpu(cpu)) {
			/* The application of microcode failed */
			microcode_fini_cpu(cpu);
			__mc_sysdev_add(sys_dev, 1);
			break;
		}
	case CPU_DOWN_FAILED_FROZEN:
		if (sysfs_create_group(&sys_dev->kobj, &mc_attr_group))
			printk(KERN_ERR "microcode: Failed to create the sysfs "
				"group for CPU%d\n", cpu);
		break;
	case CPU_DOWN_PREPARE:
		mc_sysdev_remove(sys_dev);
		break;
	case CPU_DOWN_PREPARE_FROZEN:
		/* Suspend is in progress, only remove the interface */
		sysfs_remove_group(&sys_dev->kobj, &mc_attr_group);
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block __refdata mc_cpu_notifier = {
	.notifier_call = mc_cpu_callback,
};

static int __init microcode_init (void)
{
	int error;

	printk(KERN_INFO
		"IA-32 Microcode Update Driver: v" MICROCODE_VERSION " <tigran@aivazian.fsnet.co.uk>\n");

	error = microcode_dev_init();
	if (error)
		return error;
	microcode_pdev = platform_device_register_simple("microcode", -1,
							 NULL, 0);
	if (IS_ERR(microcode_pdev)) {
		microcode_dev_exit();
		return PTR_ERR(microcode_pdev);
	}

	get_online_cpus();
	error = sysdev_driver_register(&cpu_sysdev_class, &mc_sysdev_driver);
	put_online_cpus();
	if (error) {
		microcode_dev_exit();
		platform_device_unregister(microcode_pdev);
		return error;
	}

	register_hotcpu_notifier(&mc_cpu_notifier);
	return 0;
}

static void __exit microcode_exit (void)
{
	microcode_dev_exit();

	unregister_hotcpu_notifier(&mc_cpu_notifier);

	get_online_cpus();
	sysdev_driver_unregister(&cpu_sysdev_class, &mc_sysdev_driver);
	put_online_cpus();

	platform_device_unregister(microcode_pdev);
}

module_init(microcode_init)
module_exit(microcode_exit)
