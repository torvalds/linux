// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/elf.h>
#include <linux/wait.h>
#include <linux/cdev.h>
#include <linux/atomic.h>
#include <soc/qcom/qcom_ramdump.h>
#include <linux/devcoredump.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/devcoredump.h>
#include <linux/soc/qcom/mdt_loader.h>

#define RAMDUMP_TIMEOUT 120000

#define SIZEOF_ELF_STRUCT(__xhdr) \
static inline size_t sizeof_elf_##__xhdr(unsigned char class) \
{ \
	if (class == ELFCLASS32) \
		return sizeof(struct elf32_##__xhdr); \
	else \
		return sizeof(struct elf64_##__xhdr); \
}

SIZEOF_ELF_STRUCT(phdr)
SIZEOF_ELF_STRUCT(hdr)

#define set_xhdr_property(__xhdr, arg, class, member, value) \
do { \
	if (class == ELFCLASS32) \
		((struct elf32_##__xhdr *)arg)->member = value; \
	else \
		((struct elf64_##__xhdr *)arg)->member = value; \
} while (0)

#define set_ehdr_property(arg, class, member, value) \
	set_xhdr_property(hdr, arg, class, member, value)
#define set_phdr_property(arg, class, member, value) \
	set_xhdr_property(phdr, arg, class, member, value)

#define RAMDUMP_NUM_DEVICES	256
#define RAMDUMP_NAME		"ramdump"

static struct class *ramdump_class;
static dev_t ramdump_dev;
static DEFINE_MUTEX(rd_minor_mutex);
static DEFINE_IDA(rd_minor_id);
static bool ramdump_devnode_inited;

struct ramdump_device {
	char name[256];
	struct cdev cdev;
	struct device *dev;
};

struct qcom_ramdump_desc {
	void *data;
	struct completion dump_done;
};

static int enable_dump_collection;
module_param(enable_dump_collection, int, 0644);

bool dump_enabled(void)
{
	return enable_dump_collection;
}
EXPORT_SYMBOL(dump_enabled);

static ssize_t qcom_devcd_readv(char *buffer, loff_t offset, size_t count,
			   void *data, size_t datalen)
{
	struct qcom_ramdump_desc *desc = data;

	return memory_read_from_buffer(buffer, count, &offset, desc->data, datalen);
}

static void qcom_devcd_freev(void *data)
{
	struct qcom_ramdump_desc *desc = data;

	vfree(desc->data);
	complete_all(&desc->dump_done);
}

static int qcom_devcd_dump(struct device *dev, void *data, size_t datalen, gfp_t gfp)
{
	struct qcom_ramdump_desc desc;

	desc.data = data;
	init_completion(&desc.dump_done);

	dev_coredumpm(dev, NULL, &desc, datalen, gfp, qcom_devcd_readv, qcom_devcd_freev);

	wait_for_completion(&desc.dump_done);

	return !completion_done(&desc.dump_done);
}

int qcom_dump(struct list_head *segs, struct device *dev)
{
	struct qcom_dump_segment *segment;
	void *data;
	void __iomem *ptr;
	size_t data_size = 0;
	size_t offset = 0;

	if (!segs || list_empty(segs))
		return -EINVAL;

	list_for_each_entry(segment, segs, node) {
		pr_info("Got segment size %d\n", segment->size);
		data_size += segment->size;
	}

	data = vmalloc(data_size);
	if (!data)
		return -ENOMEM;

	list_for_each_entry(segment, segs, node) {
		if (segment->va)
			memcpy(data + offset, segment->va, segment->size);
		else {
			ptr = devm_ioremap(dev, segment->da, segment->size);
			if (!ptr) {
				dev_err(dev,
					"invalid coredump segment (%pad, %zu)\n",
					&segment->da, segment->size);
				memset(data + offset, 0xff, segment->size);
			} else
				memcpy_fromio(data + offset, ptr,
					      segment->size);
		}
		offset += segment->size;
	}

	return qcom_devcd_dump(dev, data, data_size, GFP_KERNEL);
}
EXPORT_SYMBOL(qcom_dump);

/* Since the elf32 and elf64 identification is identical
 * apart from the class we use elf32 by default.
 */
static void init_elf_identification(struct elf32_hdr *ehdr, unsigned char class)
{
	memcpy(ehdr->e_ident, ELFMAG, SELFMAG);
	ehdr->e_ident[EI_CLASS] = class;
	ehdr->e_ident[EI_DATA] = ELFDATA2LSB;
	ehdr->e_ident[EI_VERSION] = EV_CURRENT;
	ehdr->e_ident[EI_OSABI] = ELFOSABI_NONE;
}

int qcom_elf_dump(struct list_head *segs, struct device *dev, unsigned char class)
{
	struct qcom_dump_segment *segment;
	void *phdr;
	void *ehdr;
	size_t data_size;
	size_t offset;
	int phnum = 0;
	void *data;
	void __iomem *ptr;


	if (!segs || list_empty(segs))
		return -EINVAL;

	data_size = sizeof_elf_hdr(class);
	list_for_each_entry(segment, segs, node) {
		data_size += sizeof_elf_phdr(class) + segment->size;
		phnum++;
	}

	data = vmalloc(data_size);
	if (!data)
		return -ENOMEM;

	pr_debug("Creating elf with size %d\n", data_size);
	ehdr = data;

	memset(ehdr, 0, sizeof_elf_hdr(class));
	init_elf_identification(ehdr, class);
	set_ehdr_property(ehdr, class, e_type, ET_CORE);
	set_ehdr_property(ehdr, class, e_machine, EM_NONE);
	set_ehdr_property(ehdr, class, e_version, EV_CURRENT);
	set_ehdr_property(ehdr, class, e_phoff, sizeof_elf_hdr(class));
	set_ehdr_property(ehdr, class, e_ehsize, sizeof_elf_hdr(class));
	set_ehdr_property(ehdr, class, e_phentsize, sizeof_elf_phdr(class));
	set_ehdr_property(ehdr, class, e_phnum, phnum);

	phdr = data + sizeof_elf_hdr(class);
	offset = sizeof_elf_hdr(class) + sizeof_elf_phdr(class) * phnum;
	list_for_each_entry(segment, segs, node) {
		memset(phdr, 0, sizeof_elf_phdr(class));
		set_phdr_property(phdr, class, p_type, PT_LOAD);
		set_phdr_property(phdr, class, p_offset, offset);
		set_phdr_property(phdr, class, p_vaddr, segment->da);
		set_phdr_property(phdr, class, p_paddr, segment->da);
		set_phdr_property(phdr, class, p_filesz, segment->size);
		set_phdr_property(phdr, class, p_memsz, segment->size);
		set_phdr_property(phdr, class, p_flags, PF_R | PF_W | PF_X);
		set_phdr_property(phdr, class, p_align, 0);

		if (segment->va)
			memcpy(data + offset, segment->va, segment->size);
		else {
			ptr = devm_ioremap(dev, segment->da, segment->size);
			if (!ptr) {
				dev_err(dev,
					"invalid coredump segment (%pad, %zu)\n",
					&segment->da, segment->size);
				memset(data + offset, 0xff, segment->size);
			} else
				memcpy_fromio(data + offset, ptr,
					      segment->size);
		}

		offset += segment->size;
		phdr += sizeof_elf_phdr(class);
	}

	return qcom_devcd_dump(dev, data, data_size, GFP_KERNEL);
}
EXPORT_SYMBOL(qcom_elf_dump);

int qcom_fw_elf_dump(struct firmware *fw, struct device *dev)
{
	const struct elf32_phdr *phdrs, *phdr;
	const struct elf32_hdr *ehdr;
	struct qcom_dump_segment *segment;
	struct list_head head;
	int i;

	ehdr = (struct elf32_hdr *)fw->data;
	phdrs = (struct elf32_phdr *)(ehdr + 1);
	INIT_LIST_HEAD(&head);

	for (i = 0; i < ehdr->e_phnum; i++) {
		phdr = &phdrs[i];

		if (phdr->p_type != PT_LOAD)
			continue;

		if ((phdr->p_flags & QCOM_MDT_TYPE_MASK) == QCOM_MDT_TYPE_HASH)
			continue;

		if (!phdr->p_memsz)
			continue;


		segment = kzalloc(sizeof(*segment), GFP_KERNEL);
		if (!segment)
			return -ENOMEM;

		segment->da = phdr->p_paddr;
		segment->size = phdr->p_memsz;

		list_add_tail(&segment->node, &head);
	}
	qcom_elf_dump(&head, dev, ELFCLASS32);
	return 0;
}
EXPORT_SYMBOL(qcom_fw_elf_dump);

static int ramdump_devnode_init(void)
{
	int ret;

	ramdump_class = class_create(THIS_MODULE, RAMDUMP_NAME);
	ret = alloc_chrdev_region(&ramdump_dev, 0, RAMDUMP_NUM_DEVICES,
				  RAMDUMP_NAME);
	if (ret) {
		pr_err("%s: unable to allocate major\n", __func__);
		return ret;
	}

	ramdump_devnode_inited = true;

	return 0;
}

void *qcom_create_ramdump_device(const char *dev_name, struct device *parent)
{
	int ret, minor;
	struct ramdump_device *rd_dev;

	if (!dev_name) {
		pr_err("%s: Invalid device name.\n", __func__);
		return NULL;
	}

	mutex_lock(&rd_minor_mutex);
	if (!ramdump_devnode_inited) {
		ret = ramdump_devnode_init();
		if (ret) {
			mutex_unlock(&rd_minor_mutex);
			return ERR_PTR(ret);
		}
	}
	mutex_unlock(&rd_minor_mutex);

	rd_dev = kzalloc(sizeof(struct ramdump_device), GFP_KERNEL);

	if (!rd_dev)
		return NULL;

	/* get a minor number */
	minor = ida_simple_get(&rd_minor_id, 0, RAMDUMP_NUM_DEVICES,
			GFP_KERNEL);
	if (minor < 0) {
		pr_err("%s: No more minor numbers left! rc:%d\n", __func__,
			minor);
		ret = -ENODEV;
		goto fail_out_of_minors;
	}

	snprintf(rd_dev->name, ARRAY_SIZE(rd_dev->name), "%s",
		 dev_name);

	rd_dev->dev = device_create(ramdump_class, parent,
				    MKDEV(MAJOR(ramdump_dev), minor),
				   rd_dev, rd_dev->name);
	if (IS_ERR(rd_dev->dev)) {
		ret = PTR_ERR(rd_dev->dev);
		pr_err("%s: device_create failed for %s (%d)\n", __func__,
				dev_name, ret);
		goto fail_return_minor;
	}

	cdev_init(&rd_dev->cdev, NULL);
	ret = cdev_add(&rd_dev->cdev, MKDEV(MAJOR(ramdump_dev), minor), 1);
	if (ret) {
		pr_err("%s: cdev_add failed for %s (%d)\n", __func__,
				dev_name, ret);
		goto fail_cdev_add;
	}

	return (void *)rd_dev->dev;

fail_cdev_add:
	device_unregister(rd_dev->dev);
fail_return_minor:
	ida_simple_remove(&rd_minor_id, minor);
fail_out_of_minors:
	kfree(rd_dev);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL(qcom_create_ramdump_device);

void qcom_destroy_ramdump_device(void *dev)
{
	struct ramdump_device *rd_dev = dev_get_drvdata(dev);
	int minor = MINOR(rd_dev->cdev.dev);

	if (IS_ERR_OR_NULL(rd_dev))
		return;

	cdev_del(&rd_dev->cdev);
	device_unregister(rd_dev->dev);
	ida_simple_remove(&rd_minor_id, minor);
	kfree(rd_dev);
}
EXPORT_SYMBOL(qcom_destroy_ramdump_device);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. Ramdump driver");
MODULE_LICENSE("GPL");

