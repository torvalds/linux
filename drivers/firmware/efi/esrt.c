/*
 * esrt.c
 *
 * This module exports EFI System Resource Table (ESRT) entries into userspace
 * through the sysfs file system. The ESRT provides a read-only catalog of
 * system components for which the system accepts firmware upgrades via UEFI's
 * "Capsule Update" feature. This module allows userland utilities to evaluate
 * what firmware updates can be applied to this system, and potentially arrange
 * for those updates to occur.
 *
 * Data is currently found below /sys/firmware/efi/esrt/...
 */
#define pr_fmt(fmt) "esrt: " fmt

#include <linux/capability.h>
#include <linux/device.h>
#include <linux/efi.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/list.h>
#include <linux/memblock.h>
#include <linux/slab.h>
#include <linux/types.h>

#include <asm/io.h>
#include <asm/early_ioremap.h>

struct efi_system_resource_entry_v1 {
	efi_guid_t	fw_class;
	u32		fw_type;
	u32		fw_version;
	u32		lowest_supported_fw_version;
	u32		capsule_flags;
	u32		last_attempt_version;
	u32		last_attempt_status;
};

/*
 * _count and _version are what they seem like.  _max is actually just
 * accounting info for the firmware when creating the table; it should never
 * have been exposed to us.  To wit, the spec says:
 * The maximum number of resource array entries that can be within the
 * table without reallocating the table, must not be zero.
 * Since there's no guidance about what that means in terms of memory layout,
 * it means nothing to us.
 */
struct efi_system_resource_table {
	u32	fw_resource_count;
	u32	fw_resource_count_max;
	u64	fw_resource_version;
	u8	entries[];
};

static phys_addr_t esrt_data;
static size_t esrt_data_size;

static struct efi_system_resource_table *esrt;

struct esre_entry {
	union {
		struct efi_system_resource_entry_v1 *esre1;
	} esre;

	struct kobject kobj;
	struct list_head list;
};

/* global list of esre_entry. */
static LIST_HEAD(entry_list);

/* entry attribute */
struct esre_attribute {
	struct attribute attr;
	ssize_t (*show)(struct esre_entry *entry, char *buf);
	ssize_t (*store)(struct esre_entry *entry,
			 const char *buf, size_t count);
};

static struct esre_entry *to_entry(struct kobject *kobj)
{
	return container_of(kobj, struct esre_entry, kobj);
}

static struct esre_attribute *to_attr(struct attribute *attr)
{
	return container_of(attr, struct esre_attribute, attr);
}

static ssize_t esre_attr_show(struct kobject *kobj,
			      struct attribute *_attr, char *buf)
{
	struct esre_entry *entry = to_entry(kobj);
	struct esre_attribute *attr = to_attr(_attr);

	/* Don't tell normal users what firmware versions we've got... */
	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	return attr->show(entry, buf);
}

static const struct sysfs_ops esre_attr_ops = {
	.show = esre_attr_show,
};

/* Generic ESRT Entry ("ESRE") support. */
static ssize_t fw_class_show(struct esre_entry *entry, char *buf)
{
	char *str = buf;

	efi_guid_to_str(&entry->esre.esre1->fw_class, str);
	str += strlen(str);
	str += sprintf(str, "\n");

	return str - buf;
}

static struct esre_attribute esre_fw_class = __ATTR_RO_MODE(fw_class, 0400);

#define esre_attr_decl(name, size, fmt) \
static ssize_t name##_show(struct esre_entry *entry, char *buf) \
{ \
	return sprintf(buf, fmt "\n", \
		       le##size##_to_cpu(entry->esre.esre1->name)); \
} \
\
static struct esre_attribute esre_##name = __ATTR_RO_MODE(name, 0400)

esre_attr_decl(fw_type, 32, "%u");
esre_attr_decl(fw_version, 32, "%u");
esre_attr_decl(lowest_supported_fw_version, 32, "%u");
esre_attr_decl(capsule_flags, 32, "0x%x");
esre_attr_decl(last_attempt_version, 32, "%u");
esre_attr_decl(last_attempt_status, 32, "%u");

static struct attribute *esre1_attrs[] = {
	&esre_fw_class.attr,
	&esre_fw_type.attr,
	&esre_fw_version.attr,
	&esre_lowest_supported_fw_version.attr,
	&esre_capsule_flags.attr,
	&esre_last_attempt_version.attr,
	&esre_last_attempt_status.attr,
	NULL
};
static void esre_release(struct kobject *kobj)
{
	struct esre_entry *entry = to_entry(kobj);

	list_del(&entry->list);
	kfree(entry);
}

static struct kobj_type esre1_ktype = {
	.release = esre_release,
	.sysfs_ops = &esre_attr_ops,
	.default_attrs = esre1_attrs,
};


static struct kobject *esrt_kobj;
static struct kset *esrt_kset;

static int esre_create_sysfs_entry(void *esre, int entry_num)
{
	struct esre_entry *entry;
	char name[20];

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	sprintf(name, "entry%d", entry_num);

	entry->kobj.kset = esrt_kset;

	if (esrt->fw_resource_version == 1) {
		int rc = 0;

		entry->esre.esre1 = esre;
		rc = kobject_init_and_add(&entry->kobj, &esre1_ktype, NULL,
					  "%s", name);
		if (rc) {
			kfree(entry);
			return rc;
		}
	}

	list_add_tail(&entry->list, &entry_list);
	return 0;
}

/* support for displaying ESRT fields at the top level */
#define esrt_attr_decl(name, size, fmt) \
static ssize_t name##_show(struct kobject *kobj, \
				  struct kobj_attribute *attr, char *buf)\
{ \
	return sprintf(buf, fmt "\n", le##size##_to_cpu(esrt->name)); \
} \
\
static struct kobj_attribute esrt_##name = __ATTR_RO_MODE(name, 0400)

esrt_attr_decl(fw_resource_count, 32, "%u");
esrt_attr_decl(fw_resource_count_max, 32, "%u");
esrt_attr_decl(fw_resource_version, 64, "%llu");

static struct attribute *esrt_attrs[] = {
	&esrt_fw_resource_count.attr,
	&esrt_fw_resource_count_max.attr,
	&esrt_fw_resource_version.attr,
	NULL,
};

static inline int esrt_table_exists(void)
{
	if (!efi_enabled(EFI_CONFIG_TABLES))
		return 0;
	if (efi.esrt == EFI_INVALID_TABLE_ADDR)
		return 0;
	return 1;
}

static umode_t esrt_attr_is_visible(struct kobject *kobj,
				    struct attribute *attr, int n)
{
	if (!esrt_table_exists())
		return 0;
	return attr->mode;
}

static struct attribute_group esrt_attr_group = {
	.attrs = esrt_attrs,
	.is_visible = esrt_attr_is_visible,
};

/*
 * remap the table, copy it to kmalloced pages, and unmap it.
 */
void __init efi_esrt_init(void)
{
	void *va;
	struct efi_system_resource_table tmpesrt;
	struct efi_system_resource_entry_v1 *v1_entries;
	size_t size, max, entry_size, entries_size;
	efi_memory_desc_t md;
	int rc;
	phys_addr_t end;

	pr_debug("esrt-init: loading.\n");
	if (!esrt_table_exists())
		return;

	rc = efi_mem_desc_lookup(efi.esrt, &md);
	if (rc < 0) {
		pr_warn("ESRT header is not in the memory map.\n");
		return;
	}

	max = efi_mem_desc_end(&md);
	if (max < efi.esrt) {
		pr_err("EFI memory descriptor is invalid. (esrt: %p max: %p)\n",
		       (void *)efi.esrt, (void *)max);
		return;
	}

	size = sizeof(*esrt);
	max -= efi.esrt;

	if (max < size) {
		pr_err("ESRT header doen't fit on single memory map entry. (size: %zu max: %zu)\n",
		       size, max);
		return;
	}

	va = early_memremap(efi.esrt, size);
	if (!va) {
		pr_err("early_memremap(%p, %zu) failed.\n", (void *)efi.esrt,
		       size);
		return;
	}

	memcpy(&tmpesrt, va, sizeof(tmpesrt));

	if (tmpesrt.fw_resource_version == 1) {
		entry_size = sizeof (*v1_entries);
	} else {
		pr_err("Unsupported ESRT version %lld.\n",
		       tmpesrt.fw_resource_version);
		return;
	}

	if (tmpesrt.fw_resource_count > 0 && max - size < entry_size) {
		pr_err("ESRT memory map entry can only hold the header. (max: %zu size: %zu)\n",
		       max - size, entry_size);
		goto err_memunmap;
	}

	/*
	 * The format doesn't really give us any boundary to test here,
	 * so I'm making up 128 as the max number of individually updatable
	 * components we support.
	 * 128 should be pretty excessive, but there's still some chance
	 * somebody will do that someday and we'll need to raise this.
	 */
	if (tmpesrt.fw_resource_count > 128) {
		pr_err("ESRT says fw_resource_count has very large value %d.\n",
		       tmpesrt.fw_resource_count);
		goto err_memunmap;
	}

	/*
	 * We know it can't be larger than N * sizeof() here, and N is limited
	 * by the previous test to a small number, so there's no overflow.
	 */
	entries_size = tmpesrt.fw_resource_count * entry_size;
	if (max < size + entries_size) {
		pr_err("ESRT does not fit on single memory map entry (size: %zu max: %zu)\n",
		       size, max);
		goto err_memunmap;
	}

	/* remap it with our (plausible) new pages */
	early_memunmap(va, size);
	size += entries_size;
	va = early_memremap(efi.esrt, size);
	if (!va) {
		pr_err("early_memremap(%p, %zu) failed.\n", (void *)efi.esrt,
		       size);
		return;
	}

	esrt_data = (phys_addr_t)efi.esrt;
	esrt_data_size = size;

	end = esrt_data + size;
	pr_info("Reserving ESRT space from %pa to %pa.\n", &esrt_data, &end);
	memblock_reserve(esrt_data, esrt_data_size);

	pr_debug("esrt-init: loaded.\n");
err_memunmap:
	early_memunmap(va, size);
}

static int __init register_entries(void)
{
	struct efi_system_resource_entry_v1 *v1_entries = (void *)esrt->entries;
	int i, rc;

	if (!esrt_table_exists())
		return 0;

	for (i = 0; i < le32_to_cpu(esrt->fw_resource_count); i++) {
		void *esre = NULL;
		if (esrt->fw_resource_version == 1) {
			esre = &v1_entries[i];
		} else {
			pr_err("Unsupported ESRT version %lld.\n",
			       esrt->fw_resource_version);
			return -EINVAL;
		}

		rc = esre_create_sysfs_entry(esre, i);
		if (rc < 0) {
			pr_err("ESRT entry creation failed with error %d.\n",
			       rc);
			return rc;
		}
	}
	return 0;
}

static void cleanup_entry_list(void)
{
	struct esre_entry *entry, *next;

	list_for_each_entry_safe(entry, next, &entry_list, list) {
		kobject_put(&entry->kobj);
	}
}

static int __init esrt_sysfs_init(void)
{
	int error;
	struct efi_system_resource_table __iomem *ioesrt;

	pr_debug("esrt-sysfs: loading.\n");
	if (!esrt_data || !esrt_data_size)
		return -ENOSYS;

	ioesrt = ioremap(esrt_data, esrt_data_size);
	if (!ioesrt) {
		pr_err("ioremap(%pa, %zu) failed.\n", &esrt_data,
		       esrt_data_size);
		return -ENOMEM;
	}

	esrt = kmalloc(esrt_data_size, GFP_KERNEL);
	if (!esrt) {
		pr_err("kmalloc failed. (wanted %zu bytes)\n", esrt_data_size);
		iounmap(ioesrt);
		return -ENOMEM;
	}

	memcpy_fromio(esrt, ioesrt, esrt_data_size);

	esrt_kobj = kobject_create_and_add("esrt", efi_kobj);
	if (!esrt_kobj) {
		pr_err("Firmware table registration failed.\n");
		error = -ENOMEM;
		goto err;
	}

	error = sysfs_create_group(esrt_kobj, &esrt_attr_group);
	if (error) {
		pr_err("Sysfs attribute export failed with error %d.\n",
		       error);
		goto err_remove_esrt;
	}

	esrt_kset = kset_create_and_add("entries", NULL, esrt_kobj);
	if (!esrt_kset) {
		pr_err("kset creation failed.\n");
		error = -ENOMEM;
		goto err_remove_group;
	}

	error = register_entries();
	if (error)
		goto err_cleanup_list;

	memblock_remove(esrt_data, esrt_data_size);

	pr_debug("esrt-sysfs: loaded.\n");

	return 0;
err_cleanup_list:
	cleanup_entry_list();
	kset_unregister(esrt_kset);
err_remove_group:
	sysfs_remove_group(esrt_kobj, &esrt_attr_group);
err_remove_esrt:
	kobject_put(esrt_kobj);
err:
	kfree(esrt);
	esrt = NULL;
	return error;
}
device_initcall(esrt_sysfs_init);

/*
MODULE_AUTHOR("Peter Jones <pjones@redhat.com>");
MODULE_DESCRIPTION("EFI System Resource Table support");
MODULE_LICENSE("GPL");
*/
