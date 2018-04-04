/*
 * drivers/firmware/qemu_fw_cfg.c
 *
 * Copyright 2015 Carnegie Mellon University
 *
 * Expose entries from QEMU's firmware configuration (fw_cfg) device in
 * sysfs (read-only, under "/sys/firmware/qemu_fw_cfg/...").
 *
 * The fw_cfg device may be instantiated via either an ACPI node (on x86
 * and select subsets of aarch64), a Device Tree node (on arm), or using
 * a kernel module (or command line) parameter with the following syntax:
 *
 *      [qemu_fw_cfg.]ioport=<size>@<base>[:<ctrl_off>:<data_off>]
 * or
 *      [qemu_fw_cfg.]mmio=<size>@<base>[:<ctrl_off>:<data_off>]
 *
 * where:
 *      <size>     := size of ioport or mmio range
 *      <base>     := physical base address of ioport or mmio range
 *      <ctrl_off> := (optional) offset of control register
 *      <data_off> := (optional) offset of data register
 *
 * e.g.:
 *      qemu_fw_cfg.ioport=2@0x510:0:1		(the default on x86)
 * or
 *      qemu_fw_cfg.mmio=0xA@0x9020000:8:0	(the default on arm)
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/acpi.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/ioport.h>

MODULE_AUTHOR("Gabriel L. Somlo <somlo@cmu.edu>");
MODULE_DESCRIPTION("QEMU fw_cfg sysfs support");
MODULE_LICENSE("GPL");

/* selector key values for "well-known" fw_cfg entries */
#define FW_CFG_SIGNATURE  0x00
#define FW_CFG_ID         0x01
#define FW_CFG_FILE_DIR   0x19

/* size in bytes of fw_cfg signature */
#define FW_CFG_SIG_SIZE 4

/* fw_cfg "file name" is up to 56 characters (including terminating nul) */
#define FW_CFG_MAX_FILE_PATH 56

/* fw_cfg file directory entry type */
struct fw_cfg_file {
	u32 size;
	u16 select;
	u16 reserved;
	char name[FW_CFG_MAX_FILE_PATH];
};

/* fw_cfg device i/o register addresses */
static bool fw_cfg_is_mmio;
static phys_addr_t fw_cfg_p_base;
static resource_size_t fw_cfg_p_size;
static void __iomem *fw_cfg_dev_base;
static void __iomem *fw_cfg_reg_ctrl;
static void __iomem *fw_cfg_reg_data;

/* atomic access to fw_cfg device (potentially slow i/o, so using mutex) */
static DEFINE_MUTEX(fw_cfg_dev_lock);

/* pick appropriate endianness for selector key */
static inline u16 fw_cfg_sel_endianness(u16 key)
{
	return fw_cfg_is_mmio ? cpu_to_be16(key) : cpu_to_le16(key);
}

/* read chunk of given fw_cfg blob (caller responsible for sanity-check) */
static inline void fw_cfg_read_blob(u16 key,
				    void *buf, loff_t pos, size_t count)
{
	u32 glk = -1U;
	acpi_status status;

	/* If we have ACPI, ensure mutual exclusion against any potential
	 * device access by the firmware, e.g. via AML methods:
	 */
	status = acpi_acquire_global_lock(ACPI_WAIT_FOREVER, &glk);
	if (ACPI_FAILURE(status) && status != AE_NOT_CONFIGURED) {
		/* Should never get here */
		WARN(1, "fw_cfg_read_blob: Failed to lock ACPI!\n");
		memset(buf, 0, count);
		return;
	}

	mutex_lock(&fw_cfg_dev_lock);
	iowrite16(fw_cfg_sel_endianness(key), fw_cfg_reg_ctrl);
	while (pos-- > 0)
		ioread8(fw_cfg_reg_data);
	ioread8_rep(fw_cfg_reg_data, buf, count);
	mutex_unlock(&fw_cfg_dev_lock);

	acpi_release_global_lock(glk);
}

/* clean up fw_cfg device i/o */
static void fw_cfg_io_cleanup(void)
{
	if (fw_cfg_is_mmio) {
		iounmap(fw_cfg_dev_base);
		release_mem_region(fw_cfg_p_base, fw_cfg_p_size);
	} else {
		ioport_unmap(fw_cfg_dev_base);
		release_region(fw_cfg_p_base, fw_cfg_p_size);
	}
}

/* arch-specific ctrl & data register offsets are not available in ACPI, DT */
#if !(defined(FW_CFG_CTRL_OFF) && defined(FW_CFG_DATA_OFF))
# if (defined(CONFIG_ARM) || defined(CONFIG_ARM64))
#  define FW_CFG_CTRL_OFF 0x08
#  define FW_CFG_DATA_OFF 0x00
# elif (defined(CONFIG_PPC_PMAC) || defined(CONFIG_SPARC32)) /* ppc/mac,sun4m */
#  define FW_CFG_CTRL_OFF 0x00
#  define FW_CFG_DATA_OFF 0x02
# elif (defined(CONFIG_X86) || defined(CONFIG_SPARC64)) /* x86, sun4u */
#  define FW_CFG_CTRL_OFF 0x00
#  define FW_CFG_DATA_OFF 0x01
# else
#  error "QEMU FW_CFG not available on this architecture!"
# endif
#endif

/* initialize fw_cfg device i/o from platform data */
static int fw_cfg_do_platform_probe(struct platform_device *pdev)
{
	char sig[FW_CFG_SIG_SIZE];
	struct resource *range, *ctrl, *data;

	/* acquire i/o range details */
	fw_cfg_is_mmio = false;
	range = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (!range) {
		fw_cfg_is_mmio = true;
		range = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		if (!range)
			return -EINVAL;
	}
	fw_cfg_p_base = range->start;
	fw_cfg_p_size = resource_size(range);

	if (fw_cfg_is_mmio) {
		if (!request_mem_region(fw_cfg_p_base,
					fw_cfg_p_size, "fw_cfg_mem"))
			return -EBUSY;
		fw_cfg_dev_base = ioremap(fw_cfg_p_base, fw_cfg_p_size);
		if (!fw_cfg_dev_base) {
			release_mem_region(fw_cfg_p_base, fw_cfg_p_size);
			return -EFAULT;
		}
	} else {
		if (!request_region(fw_cfg_p_base,
				    fw_cfg_p_size, "fw_cfg_io"))
			return -EBUSY;
		fw_cfg_dev_base = ioport_map(fw_cfg_p_base, fw_cfg_p_size);
		if (!fw_cfg_dev_base) {
			release_region(fw_cfg_p_base, fw_cfg_p_size);
			return -EFAULT;
		}
	}

	/* were custom register offsets provided (e.g. on the command line)? */
	ctrl = platform_get_resource_byname(pdev, IORESOURCE_REG, "ctrl");
	data = platform_get_resource_byname(pdev, IORESOURCE_REG, "data");
	if (ctrl && data) {
		fw_cfg_reg_ctrl = fw_cfg_dev_base + ctrl->start;
		fw_cfg_reg_data = fw_cfg_dev_base + data->start;
	} else {
		/* use architecture-specific offsets */
		fw_cfg_reg_ctrl = fw_cfg_dev_base + FW_CFG_CTRL_OFF;
		fw_cfg_reg_data = fw_cfg_dev_base + FW_CFG_DATA_OFF;
	}

	/* verify fw_cfg device signature */
	fw_cfg_read_blob(FW_CFG_SIGNATURE, sig, 0, FW_CFG_SIG_SIZE);
	if (memcmp(sig, "QEMU", FW_CFG_SIG_SIZE) != 0) {
		fw_cfg_io_cleanup();
		return -ENODEV;
	}

	return 0;
}

/* fw_cfg revision attribute, in /sys/firmware/qemu_fw_cfg top-level dir. */
static u32 fw_cfg_rev;

static ssize_t fw_cfg_showrev(struct kobject *k, struct attribute *a, char *buf)
{
	return sprintf(buf, "%u\n", fw_cfg_rev);
}

static const struct {
	struct attribute attr;
	ssize_t (*show)(struct kobject *k, struct attribute *a, char *buf);
} fw_cfg_rev_attr = {
	.attr = { .name = "rev", .mode = S_IRUSR },
	.show = fw_cfg_showrev,
};

/* fw_cfg_sysfs_entry type */
struct fw_cfg_sysfs_entry {
	struct kobject kobj;
	struct fw_cfg_file f;
	struct list_head list;
};

/* get fw_cfg_sysfs_entry from kobject member */
static inline struct fw_cfg_sysfs_entry *to_entry(struct kobject *kobj)
{
	return container_of(kobj, struct fw_cfg_sysfs_entry, kobj);
}

/* fw_cfg_sysfs_attribute type */
struct fw_cfg_sysfs_attribute {
	struct attribute attr;
	ssize_t (*show)(struct fw_cfg_sysfs_entry *entry, char *buf);
};

/* get fw_cfg_sysfs_attribute from attribute member */
static inline struct fw_cfg_sysfs_attribute *to_attr(struct attribute *attr)
{
	return container_of(attr, struct fw_cfg_sysfs_attribute, attr);
}

/* global cache of fw_cfg_sysfs_entry objects */
static LIST_HEAD(fw_cfg_entry_cache);

/* kobjects removed lazily by kernel, mutual exclusion needed */
static DEFINE_SPINLOCK(fw_cfg_cache_lock);

static inline void fw_cfg_sysfs_cache_enlist(struct fw_cfg_sysfs_entry *entry)
{
	spin_lock(&fw_cfg_cache_lock);
	list_add_tail(&entry->list, &fw_cfg_entry_cache);
	spin_unlock(&fw_cfg_cache_lock);
}

static inline void fw_cfg_sysfs_cache_delist(struct fw_cfg_sysfs_entry *entry)
{
	spin_lock(&fw_cfg_cache_lock);
	list_del(&entry->list);
	spin_unlock(&fw_cfg_cache_lock);
}

static void fw_cfg_sysfs_cache_cleanup(void)
{
	struct fw_cfg_sysfs_entry *entry, *next;

	list_for_each_entry_safe(entry, next, &fw_cfg_entry_cache, list) {
		/* will end up invoking fw_cfg_sysfs_cache_delist()
		 * via each object's release() method (i.e. destructor)
		 */
		kobject_put(&entry->kobj);
	}
}

/* default_attrs: per-entry attributes and show methods */

#define FW_CFG_SYSFS_ATTR(_attr) \
struct fw_cfg_sysfs_attribute fw_cfg_sysfs_attr_##_attr = { \
	.attr = { .name = __stringify(_attr), .mode = S_IRUSR }, \
	.show = fw_cfg_sysfs_show_##_attr, \
}

static ssize_t fw_cfg_sysfs_show_size(struct fw_cfg_sysfs_entry *e, char *buf)
{
	return sprintf(buf, "%u\n", e->f.size);
}

static ssize_t fw_cfg_sysfs_show_key(struct fw_cfg_sysfs_entry *e, char *buf)
{
	return sprintf(buf, "%u\n", e->f.select);
}

static ssize_t fw_cfg_sysfs_show_name(struct fw_cfg_sysfs_entry *e, char *buf)
{
	return sprintf(buf, "%s\n", e->f.name);
}

static FW_CFG_SYSFS_ATTR(size);
static FW_CFG_SYSFS_ATTR(key);
static FW_CFG_SYSFS_ATTR(name);

static struct attribute *fw_cfg_sysfs_entry_attrs[] = {
	&fw_cfg_sysfs_attr_size.attr,
	&fw_cfg_sysfs_attr_key.attr,
	&fw_cfg_sysfs_attr_name.attr,
	NULL,
};

/* sysfs_ops: find fw_cfg_[entry, attribute] and call appropriate show method */
static ssize_t fw_cfg_sysfs_attr_show(struct kobject *kobj, struct attribute *a,
				      char *buf)
{
	struct fw_cfg_sysfs_entry *entry = to_entry(kobj);
	struct fw_cfg_sysfs_attribute *attr = to_attr(a);

	return attr->show(entry, buf);
}

static const struct sysfs_ops fw_cfg_sysfs_attr_ops = {
	.show = fw_cfg_sysfs_attr_show,
};

/* release: destructor, to be called via kobject_put() */
static void fw_cfg_sysfs_release_entry(struct kobject *kobj)
{
	struct fw_cfg_sysfs_entry *entry = to_entry(kobj);

	fw_cfg_sysfs_cache_delist(entry);
	kfree(entry);
}

/* kobj_type: ties together all properties required to register an entry */
static struct kobj_type fw_cfg_sysfs_entry_ktype = {
	.default_attrs = fw_cfg_sysfs_entry_attrs,
	.sysfs_ops = &fw_cfg_sysfs_attr_ops,
	.release = fw_cfg_sysfs_release_entry,
};

/* raw-read method and attribute */
static ssize_t fw_cfg_sysfs_read_raw(struct file *filp, struct kobject *kobj,
				     struct bin_attribute *bin_attr,
				     char *buf, loff_t pos, size_t count)
{
	struct fw_cfg_sysfs_entry *entry = to_entry(kobj);

	if (pos > entry->f.size)
		return -EINVAL;

	if (count > entry->f.size - pos)
		count = entry->f.size - pos;

	fw_cfg_read_blob(entry->f.select, buf, pos, count);
	return count;
}

static struct bin_attribute fw_cfg_sysfs_attr_raw = {
	.attr = { .name = "raw", .mode = S_IRUSR },
	.read = fw_cfg_sysfs_read_raw,
};

/*
 * Create a kset subdirectory matching each '/' delimited dirname token
 * in 'name', starting with sysfs kset/folder 'dir'; At the end, create
 * a symlink directed at the given 'target'.
 * NOTE: We do this on a best-effort basis, since 'name' is not guaranteed
 * to be a well-behaved path name. Whenever a symlink vs. kset directory
 * name collision occurs, the kernel will issue big scary warnings while
 * refusing to add the offending link or directory. We follow up with our
 * own, slightly less scary error messages explaining the situation :)
 */
static int fw_cfg_build_symlink(struct kset *dir,
				struct kobject *target, const char *name)
{
	int ret;
	struct kset *subdir;
	struct kobject *ko;
	char *name_copy, *p, *tok;

	if (!dir || !target || !name || !*name)
		return -EINVAL;

	/* clone a copy of name for parsing */
	name_copy = p = kstrdup(name, GFP_KERNEL);
	if (!name_copy)
		return -ENOMEM;

	/* create folders for each dirname token, then symlink for basename */
	while ((tok = strsep(&p, "/")) && *tok) {

		/* last (basename) token? If so, add symlink here */
		if (!p || !*p) {
			ret = sysfs_create_link(&dir->kobj, target, tok);
			break;
		}

		/* does the current dir contain an item named after tok ? */
		ko = kset_find_obj(dir, tok);
		if (ko) {
			/* drop reference added by kset_find_obj */
			kobject_put(ko);

			/* ko MUST be a kset - we're about to use it as one ! */
			if (ko->ktype != dir->kobj.ktype) {
				ret = -EINVAL;
				break;
			}

			/* descend into already existing subdirectory */
			dir = to_kset(ko);
		} else {
			/* create new subdirectory kset */
			subdir = kzalloc(sizeof(struct kset), GFP_KERNEL);
			if (!subdir) {
				ret = -ENOMEM;
				break;
			}
			subdir->kobj.kset = dir;
			subdir->kobj.ktype = dir->kobj.ktype;
			ret = kobject_set_name(&subdir->kobj, "%s", tok);
			if (ret) {
				kfree(subdir);
				break;
			}
			ret = kset_register(subdir);
			if (ret) {
				kfree(subdir);
				break;
			}

			/* descend into newly created subdirectory */
			dir = subdir;
		}
	}

	/* we're done with cloned copy of name */
	kfree(name_copy);
	return ret;
}

/* recursively unregister fw_cfg/by_name/ kset directory tree */
static void fw_cfg_kset_unregister_recursive(struct kset *kset)
{
	struct kobject *k, *next;

	list_for_each_entry_safe(k, next, &kset->list, entry)
		/* all set members are ksets too, but check just in case... */
		if (k->ktype == kset->kobj.ktype)
			fw_cfg_kset_unregister_recursive(to_kset(k));

	/* symlinks are cleanly and automatically removed with the directory */
	kset_unregister(kset);
}

/* kobjects & kset representing top-level, by_key, and by_name folders */
static struct kobject *fw_cfg_top_ko;
static struct kobject *fw_cfg_sel_ko;
static struct kset *fw_cfg_fname_kset;

/* register an individual fw_cfg file */
static int fw_cfg_register_file(const struct fw_cfg_file *f)
{
	int err;
	struct fw_cfg_sysfs_entry *entry;

	/* allocate new entry */
	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	/* set file entry information */
	memcpy(&entry->f, f, sizeof(struct fw_cfg_file));

	/* register entry under "/sys/firmware/qemu_fw_cfg/by_key/" */
	err = kobject_init_and_add(&entry->kobj, &fw_cfg_sysfs_entry_ktype,
				   fw_cfg_sel_ko, "%d", entry->f.select);
	if (err)
		goto err_register;

	/* add raw binary content access */
	err = sysfs_create_bin_file(&entry->kobj, &fw_cfg_sysfs_attr_raw);
	if (err)
		goto err_add_raw;

	/* try adding "/sys/firmware/qemu_fw_cfg/by_name/" symlink */
	fw_cfg_build_symlink(fw_cfg_fname_kset, &entry->kobj, entry->f.name);

	/* success, add entry to global cache */
	fw_cfg_sysfs_cache_enlist(entry);
	return 0;

err_add_raw:
	kobject_del(&entry->kobj);
err_register:
	kfree(entry);
	return err;
}

/* iterate over all fw_cfg directory entries, registering each one */
static int fw_cfg_register_dir_entries(void)
{
	int ret = 0;
	u32 count, i;
	struct fw_cfg_file *dir;
	size_t dir_size;

	fw_cfg_read_blob(FW_CFG_FILE_DIR, &count, 0, sizeof(count));
	count = be32_to_cpu(count);
	dir_size = count * sizeof(struct fw_cfg_file);

	dir = kmalloc(dir_size, GFP_KERNEL);
	if (!dir)
		return -ENOMEM;

	fw_cfg_read_blob(FW_CFG_FILE_DIR, dir, sizeof(count), dir_size);

	for (i = 0; i < count; i++) {
		dir[i].size = be32_to_cpu(dir[i].size);
		dir[i].select = be16_to_cpu(dir[i].select);
		ret = fw_cfg_register_file(&dir[i]);
		if (ret)
			break;
	}

	kfree(dir);
	return ret;
}

/* unregister top-level or by_key folder */
static inline void fw_cfg_kobj_cleanup(struct kobject *kobj)
{
	kobject_del(kobj);
	kobject_put(kobj);
}

static int fw_cfg_sysfs_probe(struct platform_device *pdev)
{
	int err;

	/* NOTE: If we supported multiple fw_cfg devices, we'd first create
	 * a subdirectory named after e.g. pdev->id, then hang per-device
	 * by_key (and by_name) subdirectories underneath it. However, only
	 * one fw_cfg device exist system-wide, so if one was already found
	 * earlier, we might as well stop here.
	 */
	if (fw_cfg_sel_ko)
		return -EBUSY;

	/* create by_key and by_name subdirs of /sys/firmware/qemu_fw_cfg/ */
	err = -ENOMEM;
	fw_cfg_sel_ko = kobject_create_and_add("by_key", fw_cfg_top_ko);
	if (!fw_cfg_sel_ko)
		goto err_sel;
	fw_cfg_fname_kset = kset_create_and_add("by_name", NULL, fw_cfg_top_ko);
	if (!fw_cfg_fname_kset)
		goto err_name;

	/* initialize fw_cfg device i/o from platform data */
	err = fw_cfg_do_platform_probe(pdev);
	if (err)
		goto err_probe;

	/* get revision number, add matching top-level attribute */
	fw_cfg_read_blob(FW_CFG_ID, &fw_cfg_rev, 0, sizeof(fw_cfg_rev));
	fw_cfg_rev = le32_to_cpu(fw_cfg_rev);
	err = sysfs_create_file(fw_cfg_top_ko, &fw_cfg_rev_attr.attr);
	if (err)
		goto err_rev;

	/* process fw_cfg file directory entry, registering each file */
	err = fw_cfg_register_dir_entries();
	if (err)
		goto err_dir;

	/* success */
	pr_debug("fw_cfg: loaded.\n");
	return 0;

err_dir:
	fw_cfg_sysfs_cache_cleanup();
	sysfs_remove_file(fw_cfg_top_ko, &fw_cfg_rev_attr.attr);
err_rev:
	fw_cfg_io_cleanup();
err_probe:
	fw_cfg_kset_unregister_recursive(fw_cfg_fname_kset);
err_name:
	fw_cfg_kobj_cleanup(fw_cfg_sel_ko);
err_sel:
	return err;
}

static int fw_cfg_sysfs_remove(struct platform_device *pdev)
{
	pr_debug("fw_cfg: unloading.\n");
	fw_cfg_sysfs_cache_cleanup();
	sysfs_remove_file(fw_cfg_top_ko, &fw_cfg_rev_attr.attr);
	fw_cfg_io_cleanup();
	fw_cfg_kset_unregister_recursive(fw_cfg_fname_kset);
	fw_cfg_kobj_cleanup(fw_cfg_sel_ko);
	return 0;
}

static const struct of_device_id fw_cfg_sysfs_mmio_match[] = {
	{ .compatible = "qemu,fw-cfg-mmio", },
	{},
};
MODULE_DEVICE_TABLE(of, fw_cfg_sysfs_mmio_match);

#ifdef CONFIG_ACPI
static const struct acpi_device_id fw_cfg_sysfs_acpi_match[] = {
	{ "QEMU0002", },
	{},
};
MODULE_DEVICE_TABLE(acpi, fw_cfg_sysfs_acpi_match);
#endif

static struct platform_driver fw_cfg_sysfs_driver = {
	.probe = fw_cfg_sysfs_probe,
	.remove = fw_cfg_sysfs_remove,
	.driver = {
		.name = "fw_cfg",
		.of_match_table = fw_cfg_sysfs_mmio_match,
		.acpi_match_table = ACPI_PTR(fw_cfg_sysfs_acpi_match),
	},
};

#ifdef CONFIG_FW_CFG_SYSFS_CMDLINE

static struct platform_device *fw_cfg_cmdline_dev;

/* this probably belongs in e.g. include/linux/types.h,
 * but right now we are the only ones doing it...
 */
#ifdef CONFIG_PHYS_ADDR_T_64BIT
#define __PHYS_ADDR_PREFIX "ll"
#else
#define __PHYS_ADDR_PREFIX ""
#endif

/* use special scanf/printf modifier for phys_addr_t, resource_size_t */
#define PH_ADDR_SCAN_FMT "@%" __PHYS_ADDR_PREFIX "i%n" \
			 ":%" __PHYS_ADDR_PREFIX "i" \
			 ":%" __PHYS_ADDR_PREFIX "i%n"

#define PH_ADDR_PR_1_FMT "0x%" __PHYS_ADDR_PREFIX "x@" \
			 "0x%" __PHYS_ADDR_PREFIX "x"

#define PH_ADDR_PR_3_FMT PH_ADDR_PR_1_FMT \
			 ":%" __PHYS_ADDR_PREFIX "u" \
			 ":%" __PHYS_ADDR_PREFIX "u"

static int fw_cfg_cmdline_set(const char *arg, const struct kernel_param *kp)
{
	struct resource res[3] = {};
	char *str;
	phys_addr_t base;
	resource_size_t size, ctrl_off, data_off;
	int processed, consumed = 0;

	/* only one fw_cfg device can exist system-wide, so if one
	 * was processed on the command line already, we might as
	 * well stop here.
	 */
	if (fw_cfg_cmdline_dev) {
		/* avoid leaking previously registered device */
		platform_device_unregister(fw_cfg_cmdline_dev);
		return -EINVAL;
	}

	/* consume "<size>" portion of command line argument */
	size = memparse(arg, &str);

	/* get "@<base>[:<ctrl_off>:<data_off>]" chunks */
	processed = sscanf(str, PH_ADDR_SCAN_FMT,
			   &base, &consumed,
			   &ctrl_off, &data_off, &consumed);

	/* sscanf() must process precisely 1 or 3 chunks:
	 * <base> is mandatory, optionally followed by <ctrl_off>
	 * and <data_off>;
	 * there must be no extra characters after the last chunk,
	 * so str[consumed] must be '\0'.
	 */
	if (str[consumed] ||
	    (processed != 1 && processed != 3))
		return -EINVAL;

	res[0].start = base;
	res[0].end = base + size - 1;
	res[0].flags = !strcmp(kp->name, "mmio") ? IORESOURCE_MEM :
						   IORESOURCE_IO;

	/* insert register offsets, if provided */
	if (processed > 1) {
		res[1].name = "ctrl";
		res[1].start = ctrl_off;
		res[1].flags = IORESOURCE_REG;
		res[2].name = "data";
		res[2].start = data_off;
		res[2].flags = IORESOURCE_REG;
	}

	/* "processed" happens to nicely match the number of resources
	 * we need to pass in to this platform device.
	 */
	fw_cfg_cmdline_dev = platform_device_register_simple("fw_cfg",
					PLATFORM_DEVID_NONE, res, processed);

	return PTR_ERR_OR_ZERO(fw_cfg_cmdline_dev);
}

static int fw_cfg_cmdline_get(char *buf, const struct kernel_param *kp)
{
	/* stay silent if device was not configured via the command
	 * line, or if the parameter name (ioport/mmio) doesn't match
	 * the device setting
	 */
	if (!fw_cfg_cmdline_dev ||
	    (!strcmp(kp->name, "mmio") ^
	     (fw_cfg_cmdline_dev->resource[0].flags == IORESOURCE_MEM)))
		return 0;

	switch (fw_cfg_cmdline_dev->num_resources) {
	case 1:
		return snprintf(buf, PAGE_SIZE, PH_ADDR_PR_1_FMT,
				resource_size(&fw_cfg_cmdline_dev->resource[0]),
				fw_cfg_cmdline_dev->resource[0].start);
	case 3:
		return snprintf(buf, PAGE_SIZE, PH_ADDR_PR_3_FMT,
				resource_size(&fw_cfg_cmdline_dev->resource[0]),
				fw_cfg_cmdline_dev->resource[0].start,
				fw_cfg_cmdline_dev->resource[1].start,
				fw_cfg_cmdline_dev->resource[2].start);
	}

	/* Should never get here */
	WARN(1, "Unexpected number of resources: %d\n",
		fw_cfg_cmdline_dev->num_resources);
	return 0;
}

static const struct kernel_param_ops fw_cfg_cmdline_param_ops = {
	.set = fw_cfg_cmdline_set,
	.get = fw_cfg_cmdline_get,
};

device_param_cb(ioport, &fw_cfg_cmdline_param_ops, NULL, S_IRUSR);
device_param_cb(mmio, &fw_cfg_cmdline_param_ops, NULL, S_IRUSR);

#endif /* CONFIG_FW_CFG_SYSFS_CMDLINE */

static int __init fw_cfg_sysfs_init(void)
{
	int ret;

	/* create /sys/firmware/qemu_fw_cfg/ top level directory */
	fw_cfg_top_ko = kobject_create_and_add("qemu_fw_cfg", firmware_kobj);
	if (!fw_cfg_top_ko)
		return -ENOMEM;

	ret = platform_driver_register(&fw_cfg_sysfs_driver);
	if (ret)
		fw_cfg_kobj_cleanup(fw_cfg_top_ko);

	return ret;
}

static void __exit fw_cfg_sysfs_exit(void)
{
	platform_driver_unregister(&fw_cfg_sysfs_driver);

#ifdef CONFIG_FW_CFG_SYSFS_CMDLINE
	platform_device_unregister(fw_cfg_cmdline_dev);
#endif

	/* clean up /sys/firmware/qemu_fw_cfg/ */
	fw_cfg_kobj_cleanup(fw_cfg_top_ko);
}

module_init(fw_cfg_sysfs_init);
module_exit(fw_cfg_sysfs_exit);
