// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
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

int qcom_elf_dump(struct list_head *segs, struct device *dev)
{
	struct qcom_dump_segment *segment;
	struct elf32_phdr *phdr;
	struct elf32_hdr *ehdr;
	size_t data_size;
	size_t offset;
	int phnum = 0;
	void *data;
	void __iomem *ptr;


	if (!segs || list_empty(segs))
		return -EINVAL;

	data_size = sizeof(*ehdr);
	list_for_each_entry(segment, segs, node) {
		data_size += sizeof(*phdr) + segment->size;

		phnum++;
	}

	data = vmalloc(data_size);
	if (!data)
		return -ENOMEM;

	pr_debug("Creating elf with size %d\n", data_size);
	ehdr = data;

	memset(ehdr, 0, sizeof(*ehdr));
	memcpy(ehdr->e_ident, ELFMAG, SELFMAG);
	ehdr->e_ident[EI_CLASS] = ELFCLASS32;
	ehdr->e_ident[EI_DATA] = ELFDATA2LSB;
	ehdr->e_ident[EI_VERSION] = EV_CURRENT;
	ehdr->e_ident[EI_OSABI] = ELFOSABI_NONE;
	ehdr->e_type = ET_CORE;
	ehdr->e_machine = EM_NONE;
	ehdr->e_version = EV_CURRENT;
	ehdr->e_entry = 0;
	ehdr->e_phoff = sizeof(*ehdr);
	ehdr->e_ehsize = sizeof(*ehdr);
	ehdr->e_phentsize = sizeof(*phdr);
	ehdr->e_phnum = phnum;

	phdr = data + ehdr->e_phoff;
	offset = ehdr->e_phoff + sizeof(*phdr) * ehdr->e_phnum;
	list_for_each_entry(segment, segs, node) {
		memset(phdr, 0, sizeof(*phdr));
		phdr->p_type = PT_LOAD;
		phdr->p_offset = offset;
		phdr->p_vaddr = segment->da;
		phdr->p_paddr = segment->da;
		phdr->p_filesz = segment->size;
		phdr->p_memsz = segment->size;
		phdr->p_flags = PF_R | PF_W | PF_X;
		phdr->p_align = 0;

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

		offset += phdr->p_filesz;
		phdr++;
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
	qcom_elf_dump(&head, dev);
	return 0;
}
EXPORT_SYMBOL(qcom_fw_elf_dump);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. Ramdump driver");
MODULE_LICENSE("GPL");

