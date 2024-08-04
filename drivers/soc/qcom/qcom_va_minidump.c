// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022,2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "va-minidump: %s: " fmt, __func__

#include <linux/init.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/of_reserved_mem.h>
#include <linux/dma-mapping.h>
#include <linux/dma-direct.h>
#include <linux/elf.h>
#include <linux/slab.h>
#include <linux/panic_notifier.h>
#include <soc/qcom/minidump.h>
#include "elf.h"

struct va_md_tree_node {
	struct va_md_entry entry;
	int lindex;
	int rindex;
};

struct va_md_elf_info {
	unsigned long ehdr;
	unsigned long shdr_cnt;
	unsigned long phdr_cnt;
	unsigned long pload_size;
	unsigned long str_tbl_size;
};

#define VA_MD_VADDR_MARKER	-1
#define VA_MD_CB_MARKER		-2
#define MAX_ELF_SECTION		0xFFFFU

struct va_minidump_data {
	phys_addr_t mem_phys_addr;
	unsigned int total_mem_size;
	unsigned long elf_mem;
	unsigned int num_sections;
	unsigned long str_tbl_idx;
	struct va_md_elf_info elf;
	struct md_region md_entry;
	bool in_oops_handler;
	bool va_md_minidump_reg;
	bool va_md_init;
	struct list_head va_md_list;
	struct kset *va_md_kset;
};

/*
 * Incase, client uses stack memory to allocate
 * notifier block, we have to make a copy.
 */
struct notifier_block_list {
	struct notifier_block nb;
	struct list_head nb_list;
};

struct va_md_s_data {
	struct kobject s_kobj;
	struct atomic_notifier_head va_md_s_notif_list;
	struct list_head va_md_s_list;
	struct list_head va_md_s_nb_list;
	bool enable;
};

struct va_minidump_data va_md_data;
static DEFINE_MUTEX(va_md_lock);

#define to_va_md_attr(_attr) container_of(_attr, struct va_md_attribute, attr)
#define to_va_md_s_data(obj) container_of(obj, struct va_md_s_data, s_kobj)

struct va_md_attribute {
	struct attribute attr;
	ssize_t (*show)(struct kobject *kobj, struct attribute *attr,
					char *buf);
	ssize_t (*store)(struct kobject *kobj, struct attribute *attr,
					const char *buf, size_t count);
};

static ssize_t attr_show(struct kobject *kobj, struct attribute *attr,
							char *buf)
{
	struct va_md_attribute *va_md_attr = to_va_md_attr(attr);
	ssize_t ret = -EIO;

	if (va_md_attr->show)
		ret = va_md_attr->show(kobj, attr, buf);

	return ret;
}

static ssize_t attr_store(struct kobject *kobj, struct attribute *attr,
				const char *buf, size_t count)
{
	struct va_md_attribute *va_md_attr = to_va_md_attr(attr);
	ssize_t ret = -EIO;

	if (va_md_attr->store)
		ret = va_md_attr->store(kobj, attr, buf, count);

	return ret;
}

static const struct sysfs_ops va_md_sysfs_ops = {
	.show   = attr_show,
	.store  = attr_store,
};

static struct kobj_type va_md_kobj_type = {
	.sysfs_ops      = &va_md_sysfs_ops,
};

static ssize_t enable_show(struct kobject *kobj, struct attribute *this, char *buf)
{
	struct va_md_s_data *vamd_sdata = to_va_md_s_data(kobj);

	return scnprintf(buf, PAGE_SIZE, "enable: %u\n", vamd_sdata->enable);
}

static ssize_t enable_store(struct kobject *kobj, struct attribute *this,
				const char *buf, size_t count)
{
	struct va_md_s_data *vamd_sdata = to_va_md_s_data(kobj);
	bool val;
	int ret;

	ret = kstrtobool(buf, &val);
	if (ret) {
		pr_err("Invalid value passed\n");
		return ret;
	}

	vamd_sdata->enable = val;
	return count;
}

static struct va_md_attribute va_md_s_attr = __ATTR_RW(enable);
static struct attribute *va_md_s_attrs[] = {
	&va_md_s_attr.attr,
	NULL
};

static struct attribute_group va_md_s_attr_group = {
	.attrs = va_md_s_attrs,
};

bool qcom_va_md_enabled(void)
{
	/*
	 * Ensure that minidump is enabled and va-minidump is initialized
	 * before we start registration.
	 */
	return msm_minidump_enabled() && smp_load_acquire(&va_md_data.va_md_init);
}
EXPORT_SYMBOL(qcom_va_md_enabled);

int qcom_va_md_register(const char *name, struct notifier_block *nb)
{
	int ret = 0;
	struct va_md_s_data *va_md_s_data;
	struct notifier_block_list *nbl, *temp_nbl;
	struct kobject *kobj;

	if (!qcom_va_md_enabled()) {
		pr_err("qcom va minidump driver is not initialized\n");
		return -ENODEV;
	}

	nbl = kzalloc(sizeof(struct notifier_block_list), GFP_KERNEL);
	if (!nbl)
		return -ENOMEM;

	nbl->nb = *nb;
	mutex_lock(&va_md_lock);
	kobj = kset_find_obj(va_md_data.va_md_kset, name);
	if (kobj) {
		pr_warn("subsystem: %s is already registered\n", name);
		kobject_put(kobj);
		va_md_s_data = to_va_md_s_data(kobj);
		goto register_notifier;
	}

	va_md_s_data = kzalloc(sizeof(*va_md_s_data), GFP_KERNEL);
	if (!va_md_s_data) {
		ret = -ENOMEM;
		kfree(nbl);
		goto out;
	}

	va_md_s_data->s_kobj.kset = va_md_data.va_md_kset;
	ret = kobject_init_and_add(&va_md_s_data->s_kobj, &va_md_kobj_type,
				&va_md_data.va_md_kset->kobj, name);
	if (ret) {
		pr_err("%s: Error in kobject creation\n", __func__);
		kobject_put(&va_md_s_data->s_kobj);
		kfree(nbl);
		goto out;
	}

	kobject_uevent(&va_md_s_data->s_kobj, KOBJ_ADD);
	ret = sysfs_create_group(&va_md_s_data->s_kobj, &va_md_s_attr_group);
	if (ret) {
		pr_err("%s: Error in creation sysfs_create_group\n", __func__);
		kobject_put(&va_md_s_data->s_kobj);
		kfree(nbl);
		goto out;
	}

	ATOMIC_INIT_NOTIFIER_HEAD(&va_md_s_data->va_md_s_notif_list);
	INIT_LIST_HEAD(&va_md_s_data->va_md_s_nb_list);
	va_md_s_data->enable = false;
	list_add_tail(&va_md_s_data->va_md_s_list, &va_md_data.va_md_list);

register_notifier:
	list_for_each_entry(temp_nbl, &va_md_s_data->va_md_s_nb_list, nb_list) {
		if (temp_nbl->nb.notifier_call == nbl->nb.notifier_call) {
			pr_warn("subsystem:%s callback is already registered\n", name);
			kfree(nbl);
			ret = -EEXIST;
			goto out;
		}
	}

	atomic_notifier_chain_register(&va_md_s_data->va_md_s_notif_list, &nbl->nb);
	list_add_tail(&nbl->nb_list, &va_md_s_data->va_md_s_nb_list);
out:
	mutex_unlock(&va_md_lock);
	return ret;
}
EXPORT_SYMBOL(qcom_va_md_register);

int qcom_va_md_unregister(const char *name, struct notifier_block *nb)
{
	struct va_md_s_data *va_md_s_data;
	struct notifier_block_list *nbl, *tmpnbl;
	struct kobject *kobj;
	int ret = 0;
	bool found = false;

	if (!qcom_va_md_enabled()) {
		pr_err("qcom va minidump driver is not initialized\n");
		return -ENODEV;
	}

	mutex_lock(&va_md_lock);
	kobj = kset_find_obj(va_md_data.va_md_kset, name);
	if (!kobj) {
		pr_warn("subsystem: %s is not registered\n", name);
		mutex_unlock(&va_md_lock);
		return -EINVAL;
	}
	va_md_s_data = to_va_md_s_data(kobj);
	kobject_put(kobj);

	list_for_each_entry_safe(nbl, tmpnbl, &va_md_s_data->va_md_s_nb_list, nb_list) {
		if (nbl->nb.notifier_call == nb->notifier_call) {
			atomic_notifier_chain_unregister(&va_md_s_data->va_md_s_notif_list,
							&nbl->nb);
			list_del(&nbl->nb_list);
			kfree(nbl);
			found = true;
			break;
		}
	}

	if (!found) {
		pr_warn("subsystem:%s callback is not registered\n", name);
		ret = -EINVAL;
	} else if (list_empty(&va_md_s_data->va_md_s_nb_list)) {
		list_del(&va_md_s_data->va_md_s_nb_list);
		sysfs_remove_group(&va_md_s_data->s_kobj, &va_md_s_attr_group);
		kobject_put(&va_md_s_data->s_kobj);
		list_del(&va_md_s_data->va_md_s_list);
		kfree(va_md_s_data);
	}

	mutex_unlock(&va_md_lock);
	return ret;
}
EXPORT_SYMBOL(qcom_va_md_unregister);

static void va_md_add_entry(struct va_md_entry *entry)
{
	struct va_md_tree_node *dst = ((struct va_md_tree_node *)va_md_data.elf_mem) +
					va_md_data.num_sections;
	unsigned int len = strlen(entry->owner);

	dst->entry = *entry;
	WARN_ONCE(len > MAX_OWNER_STRING - 1,
		"Client entry name %s (len = %u) is greater than expected %u\n",
		entry->owner, len, MAX_OWNER_STRING - 1);
	dst->entry.owner[MAX_OWNER_STRING - 1] = '\0';

	if (entry->vaddr) {
		dst->lindex = VA_MD_VADDR_MARKER;
		dst->rindex = VA_MD_VADDR_MARKER;
	} else {
		dst->lindex = VA_MD_CB_MARKER;
		dst->rindex = VA_MD_CB_MARKER;
	}

	va_md_data.num_sections++;
}

static bool va_md_check_overlap(struct va_md_entry *entry, unsigned int index)
{
	unsigned long ent_start, ent_end;
	unsigned long node_start, node_end;
	struct va_md_tree_node *node = (struct va_md_tree_node *)va_md_data.elf_mem;

	node_start = node[index].entry.vaddr;
	node_end = node[index].entry.vaddr + node[index].entry.size - 1;
	ent_start = entry->vaddr;
	ent_end = entry->vaddr + entry->size - 1;

	if (((node_start <= ent_start) && (ent_start <= node_end)) ||
		((node_start <= ent_end) && (ent_end <= node_end)) ||
		((ent_start <= node_start) && (node_end <= ent_end)))
		return true;

	return false;
}

static bool va_md_move_left(struct va_md_entry *entry, unsigned int index)
{
	unsigned long ent_start, ent_end;
	unsigned long node_start, node_end;
	struct va_md_tree_node *node = (struct va_md_tree_node *)va_md_data.elf_mem;

	node_start = node[index].entry.vaddr;
	node_end = node[index].entry.vaddr + node[index].entry.size - 1;
	ent_start = entry->vaddr;
	ent_end = entry->vaddr + entry->size - 1;

	if ((ent_start < node_start) && (ent_end < node_start))
		return true;

	return false;
}

static bool va_md_move_right(struct va_md_entry *entry, unsigned int index)
{
	unsigned long ent_start, ent_end;
	unsigned long node_start, node_end;
	struct va_md_tree_node *node = (struct va_md_tree_node *)va_md_data.elf_mem;

	node_start = node[index].entry.vaddr;
	node_end = node[index].entry.vaddr + node[index].entry.size - 1;
	ent_start = entry->vaddr;
	ent_end = entry->vaddr + entry->size - 1;

	if ((ent_start > node_end) && (ent_end > node_end))
		return true;

	return false;
}

static int va_md_tree_insert(struct va_md_entry *entry)
{
	unsigned int baseindex = 0;
	int ret = 0;
	static int num_nodes;
	struct va_md_tree_node *tree = (struct va_md_tree_node *)va_md_data.elf_mem;

	if (!entry->vaddr || !va_md_data.num_sections) {
		va_md_add_entry(entry);
		goto out;
	}

	while (baseindex < va_md_data.num_sections) {
		if ((tree[baseindex].lindex == VA_MD_CB_MARKER) &&
			(tree[baseindex].rindex == VA_MD_CB_MARKER)) {
			baseindex++;
			continue;
		}

		if (va_md_check_overlap(entry, baseindex)) {
			entry->owner[MAX_OWNER_STRING - 1] = '\0';
			pr_err("Overlapping region owner:%s\n", entry->owner);
			ret = -EINVAL;
			goto out;
		}

		if (va_md_move_left(entry, baseindex)) {
			if (tree[baseindex].lindex == VA_MD_VADDR_MARKER) {
				tree[baseindex].lindex = va_md_data.num_sections;
				va_md_add_entry(entry);
				num_nodes++;
				goto exit_loop;
			} else {
				baseindex = tree[baseindex].lindex;
				continue;
			}

		} else if (va_md_move_right(entry, baseindex)) {
			if (tree[baseindex].rindex == VA_MD_VADDR_MARKER) {
				tree[baseindex].rindex = va_md_data.num_sections;
				va_md_add_entry(entry);
				num_nodes++;
				goto exit_loop;
			} else {
				baseindex = tree[baseindex].rindex;
				continue;
			}
		} else {
			pr_err("Warning: Corrupted Binary Search Tree\n");
		}
	}

exit_loop:
	if (!num_nodes) {
		va_md_add_entry(entry);
		num_nodes++;
	}

out:
	return ret;
}

static bool va_md_overflow_check(void)
{
	unsigned long end_addr;
	unsigned long start_addr = va_md_data.elf_mem;

	start_addr += sizeof(struct va_md_tree_node) * va_md_data.num_sections;
	end_addr = start_addr + sizeof(struct va_md_tree_node) - 1;

	if (end_addr > va_md_data.elf_mem + va_md_data.total_mem_size - 1)
		return true;
	else
		return false;
}

int qcom_va_md_add_region(struct va_md_entry *entry)
{
	if (!va_md_data.in_oops_handler)
		return -EINVAL;

	if ((!entry->vaddr == !entry->cb) || (entry->size <= 0)) {
		entry->owner[MAX_OWNER_STRING - 1] = '\0';
		pr_err("Invalid entry from owner:%s\n", entry->owner);
		return -EINVAL;
	}

	if (va_md_data.num_sections > MAX_ELF_SECTION) {
		pr_err("MAX_ELF_SECTION reached\n");
		return -ENOSPC;
	}

	if (va_md_overflow_check()) {
		pr_err("Total CMA consumed for Qcom VA minidump\n");
		return -ENOMEM;
	}

	return va_md_tree_insert(entry);
}
EXPORT_SYMBOL(qcom_va_md_add_region);

static void qcom_va_md_minidump_registration(void)
{
	strscpy(va_md_data.md_entry.name, "KVA_DUMP", sizeof(va_md_data.md_entry.name));

	va_md_data.md_entry.virt_addr = va_md_data.elf.ehdr;
	va_md_data.md_entry.phys_addr =	va_md_data.mem_phys_addr +
		(sizeof(struct va_md_tree_node) * va_md_data.num_sections);
	va_md_data.md_entry.size = sizeof(struct elfhdr) +
		(sizeof(struct elf_shdr) * va_md_data.elf.shdr_cnt) +
		(sizeof(struct elf_phdr) * va_md_data.elf.phdr_cnt) +
		va_md_data.elf.pload_size + va_md_data.elf.str_tbl_size;
	va_md_data.md_entry.size = ALIGN(va_md_data.md_entry.size, 4);

	if (msm_minidump_add_region(&va_md_data.md_entry) < 0) {
		pr_err("Failed to register VA driver CMA region with minidump\n");
		va_md_data.va_md_minidump_reg = false;
		return;
	}

	va_md_data.va_md_minidump_reg = true;
}

static inline unsigned long set_sec_name(struct elfhdr *ehdr, const char *name)
{
	char *strtab = elf_str_table(ehdr);
	unsigned long idx = va_md_data.str_tbl_idx;
	unsigned long ret = 0;

	if ((strtab == NULL) || (name == NULL))
		return 0;

	ret = idx;
	idx += strscpy((strtab + idx), name, MAX_OWNER_STRING);
	va_md_data.str_tbl_idx = idx + 1;
	return ret;
}

static void qcom_va_add_elf_hdr(void)
{
	struct elfhdr *ehdr = (struct elfhdr *)va_md_data.elf.ehdr;
	unsigned long phdr_off;

	phdr_off = sizeof(*ehdr) + (sizeof(struct elf_shdr) * va_md_data.elf.shdr_cnt);

	memcpy(ehdr->e_ident, ELFMAG, SELFMAG);
	ehdr->e_ident[EI_CLASS] = ELF_CLASS;
	ehdr->e_ident[EI_DATA] = ELF_DATA;
	ehdr->e_ident[EI_VERSION] = EV_CURRENT;
	ehdr->e_ident[EI_OSABI] = ELF_OSABI;
	ehdr->e_type = ET_CORE;
	ehdr->e_machine  = ELF_ARCH;
	ehdr->e_version = EV_CURRENT;
	ehdr->e_ehsize = sizeof(*ehdr);
	ehdr->e_shoff = sizeof(*ehdr);
	ehdr->e_shentsize = sizeof(struct elf_shdr);
	ehdr->e_shstrndx = 1;
	ehdr->e_phentsize = sizeof(struct elf_phdr);
	ehdr->e_phoff = phdr_off;
}

static void qcom_va_add_hdrs(void)
{
	struct elf_shdr *shdr;
	struct elf_phdr *phdr;
	unsigned long strtbl_off, offset, i;
	struct elfhdr *ehdr = (struct elfhdr *)va_md_data.elf.ehdr;
	struct va_md_tree_node *arr = (struct va_md_tree_node *)va_md_data.elf_mem;

	strtbl_off = ehdr->e_phoff + (sizeof(*phdr) * va_md_data.elf.phdr_cnt);

	/* First section header is NULL */
	shdr = elf_section(ehdr, ehdr->e_shnum);
	ehdr->e_shnum++;

	/* String table section */
	va_md_data.str_tbl_idx = 1;
	shdr = elf_section(ehdr, ehdr->e_shnum);
	ehdr->e_shnum++;

	shdr->sh_type = SHT_STRTAB;
	shdr->sh_offset = strtbl_off;
	shdr->sh_name = set_sec_name(ehdr, "STR_TBL");
	shdr->sh_size = va_md_data.elf.str_tbl_size;

	offset = strtbl_off + va_md_data.elf.str_tbl_size;
	for (i = 0; i < (va_md_data.elf.shdr_cnt - 2); i++) {
		/* section header */
		shdr = elf_section(ehdr, ehdr->e_shnum);
		shdr->sh_type = SHT_PROGBITS;
		shdr->sh_name = set_sec_name(ehdr, arr[i].entry.owner);
		shdr->sh_size = arr[i].entry.size;
		shdr->sh_flags = SHF_WRITE;
		shdr->sh_offset = offset;

		/* program header */
		phdr = elf_program(ehdr, ehdr->e_phnum);
		phdr->p_type = PT_LOAD;
		phdr->p_offset = offset;
		phdr->p_filesz = phdr->p_memsz = arr[i].entry.size;
		phdr->p_flags = PF_R | PF_W;

		if (arr[i].entry.vaddr) {
			shdr->sh_addr =  phdr->p_vaddr = arr[i].entry.vaddr;
			memcpy((void *)(va_md_data.elf.ehdr + offset),
				(void *)shdr->sh_addr, shdr->sh_size);
		} else {
			shdr->sh_addr =  phdr->p_vaddr = va_md_data.elf.ehdr + offset;
			arr[i].entry.cb((void *)(va_md_data.elf.ehdr + offset),
				shdr->sh_size);
		}

		offset += shdr->sh_size;
		ehdr->e_shnum++;
		ehdr->e_phnum++;
	}
}

static int qcom_va_md_calc_size(unsigned int shdr_cnt)
{
	unsigned int len, size = 0;
	static unsigned long tot_size;
	struct va_md_tree_node *arr = (struct va_md_tree_node *)va_md_data.elf_mem;

	if (!shdr_cnt) {
		tot_size = sizeof(struct va_md_tree_node) * va_md_data.num_sections;
		size = (sizeof(struct elfhdr) + (2 * sizeof(struct elf_shdr)) +
				strlen("STR_TBL") + 2);
	}

	len = strlen(arr[shdr_cnt].entry.owner);
	size += (sizeof(struct elf_shdr) + sizeof(struct elf_phdr) +
		arr[shdr_cnt].entry.size + len + 1);
	tot_size += size;
	if (tot_size > va_md_data.total_mem_size) {
		pr_err("Total CMA consumed, no space left\n");
		return -ENOSPC;
	}

	if (!shdr_cnt) {
		va_md_data.elf.ehdr = va_md_data.elf_mem + (sizeof(struct va_md_tree_node)
					* va_md_data.num_sections);
		va_md_data.elf.shdr_cnt = 2;
		va_md_data.elf.phdr_cnt = 0;
		va_md_data.elf.pload_size = 0;
		va_md_data.elf.str_tbl_size = strlen("STR_TBL") + 2;
	}

	va_md_data.elf.shdr_cnt++;
	va_md_data.elf.phdr_cnt++;
	va_md_data.elf.pload_size += arr[shdr_cnt].entry.size;
	va_md_data.elf.str_tbl_size += (len + 1);

	return 0;
}

static int qcom_va_md_calc_elf_size(void)
{
	unsigned int i;
	int ret = 0;

	if (va_md_overflow_check()) {
		pr_err("Total CMA consumed, no space to create ELF\n");
		return -ENOSPC;
	}

	pr_debug("Num sections:%u\n", va_md_data.num_sections);
	for (i = 0; i < va_md_data.num_sections; i++) {
		ret = qcom_va_md_calc_size(i);
		if (ret < 0)
			break;
	}

	return ret;
}

static int qcom_va_md_panic_handler(struct notifier_block *this,
				    unsigned long event, void *ptr)
{
	unsigned long size;
	struct va_md_s_data *va_md_s_data;

	if (va_md_data.in_oops_handler)
		return NOTIFY_DONE;

	va_md_data.in_oops_handler = true;

	list_for_each_entry(va_md_s_data, &va_md_data.va_md_list, va_md_s_list) {
		if (va_md_s_data->enable)
			atomic_notifier_call_chain(&va_md_s_data->va_md_s_notif_list,
							0, NULL);
	}

	if (!va_md_data.num_sections)
		goto out;

	if (qcom_va_md_calc_elf_size() < 0)
		goto out;

	size = sizeof(struct elfhdr) +
		(sizeof(struct elf_shdr) * va_md_data.elf.shdr_cnt) +
		(sizeof(struct elf_phdr) * va_md_data.elf.phdr_cnt) +
		va_md_data.elf.pload_size + va_md_data.elf.str_tbl_size;
	size = ALIGN(size, 4);
	memset((void *)va_md_data.elf.ehdr, 0, size);

	qcom_va_md_minidump_registration();
out:
	return NOTIFY_DONE;
}

static int qcom_va_md_elf_panic_handler(struct notifier_block *this,
				    unsigned long event, void *ptr)
{
	if (!va_md_data.num_sections || !va_md_data.va_md_minidump_reg)
		goto out;

	qcom_va_add_elf_hdr();
	qcom_va_add_hdrs();

out:
	va_md_data.in_oops_handler = false;
	return NOTIFY_DONE;
}

static struct notifier_block qcom_va_md_panic_blk = {
	.notifier_call = qcom_va_md_panic_handler,
	.priority = INT_MAX - 4,
};

static struct notifier_block qcom_va_md_elf_panic_blk = {
	.notifier_call = qcom_va_md_elf_panic_handler,
	.priority = INT_MAX - 5,
};

static int qcom_va_md_reserve_mem(struct device *dev)
{
	struct device_node *node;
	unsigned int size[2];
	int ret = 0;

	node = of_parse_phandle(dev->of_node, "memory-region", 0);
	if (node) {
		ret = of_reserved_mem_device_init_by_idx(dev, dev->of_node, 0);
		of_node_put(dev->of_node);
		if (ret) {
			pr_err("Failed to initialize CMA mem, ret %d\n",
				ret);
			goto out;
		}
	}

	ret = of_property_read_u32_array(node, "size", size, 2);
	if (ret) {
		pr_err("Failed to get size of CMA, ret %d\n", ret);
		goto out;
	}

	va_md_data.total_mem_size = size[1];

out:
	return ret;
}

static int qcom_va_md_driver_remove(struct platform_device *pdev)
{
	struct va_md_s_data *va_md_s_data, *tmp;
	struct notifier_block_list *nbl, *tmpnbl;

	mutex_lock(&va_md_lock);
	list_for_each_entry_safe(va_md_s_data, tmp, &va_md_data.va_md_list, va_md_s_list) {
		list_for_each_entry_safe(nbl, tmpnbl, &va_md_s_data->va_md_s_nb_list, nb_list) {
			atomic_notifier_chain_unregister(&va_md_s_data->va_md_s_notif_list,
								&nbl->nb);
			list_del(&nbl->nb_list);
			kfree(nbl);
		}

		list_del(&va_md_s_data->va_md_s_nb_list);
		sysfs_remove_group(&va_md_s_data->s_kobj, &va_md_s_attr_group);
		kobject_put(&va_md_s_data->s_kobj);
		list_del(&va_md_s_data->va_md_s_list);
		kfree(va_md_s_data);
	}

	mutex_unlock(&va_md_lock);
	kset_unregister(va_md_data.va_md_kset);
	atomic_notifier_chain_unregister(&panic_notifier_list, &qcom_va_md_elf_panic_blk);
	atomic_notifier_chain_unregister(&panic_notifier_list, &qcom_va_md_panic_blk);
	vunmap((void *)va_md_data.elf_mem);
	return 0;
}

static int qcom_va_md_driver_probe(struct platform_device *pdev)
{
	int ret = 0;
	int i;
	void *vaddr;
	int count;
	struct page **pages, *page;
	dma_addr_t dma_handle;

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (ret)
		return ret;

	ret = qcom_va_md_reserve_mem(&pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "CMA for VA based minidump is not present\n");
		goto out;
	}

	vaddr = dma_alloc_coherent(&pdev->dev, va_md_data.total_mem_size, &dma_handle,
				   GFP_KERNEL);
	if (!vaddr) {
		ret = -ENOMEM;
		goto out;
	}

	dma_free_coherent(&pdev->dev, va_md_data.total_mem_size, vaddr, dma_handle);
	page = phys_to_page(dma_to_phys(&pdev->dev, dma_handle));
	count = PAGE_ALIGN(va_md_data.total_mem_size) >> PAGE_SHIFT;
	pages = kmalloc_array(count, sizeof(struct page *), GFP_KERNEL);
	if (!pages)
		return -ENOMEM;

	for (i = 0; i < count; i++)
		pages[i] = nth_page(page, i);

	vaddr = vmap(pages, count, VM_DMA_COHERENT, pgprot_dmacoherent(PAGE_KERNEL));
	kfree(pages);

	va_md_data.mem_phys_addr = dma_to_phys(&pdev->dev, dma_handle);
	va_md_data.elf_mem = (unsigned long)vaddr;

	atomic_notifier_chain_register(&panic_notifier_list, &qcom_va_md_panic_blk);
	atomic_notifier_chain_register(&panic_notifier_list, &qcom_va_md_elf_panic_blk);

	INIT_LIST_HEAD(&va_md_data.va_md_list);
	va_md_data.va_md_kset = kset_create_and_add("va-minidump", NULL, kernel_kobj);
	if (!va_md_data.va_md_kset) {
		dev_err(&pdev->dev, "Failed to create kset for va-minidump\n");
		vunmap((void *)va_md_data.elf_mem);
		ret = -ENOMEM;
		goto out;
	}

	/* All updates above should be visible, before init completes */
	smp_store_release(&va_md_data.va_md_init, true);
out:
	return ret;
}

static const struct of_device_id qcom_va_md_of_match[] = {
	{.compatible = "qcom,va-minidump"},
	{}
};

MODULE_DEVICE_TABLE(of, qcom_va_md_of_match);

static struct platform_driver qcom_va_md_driver = {
	.driver = {
		   .name = "qcom-va-minidump",
		   .of_match_table = qcom_va_md_of_match,
		   },
	.probe = qcom_va_md_driver_probe,
	.remove = qcom_va_md_driver_remove,
};

module_platform_driver(qcom_va_md_driver);

MODULE_DESCRIPTION("Qcom VA Minidump Driver");
MODULE_LICENSE("GPL v2");
