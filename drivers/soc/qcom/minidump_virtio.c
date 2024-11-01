// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#define pr_fmt(x) "virtio_minidump: " x

#include <linux/init.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/elf.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <soc/qcom/minidump.h>
#include "debug_symbol.h"
#include "minidump_private.h"
#include "elf.h"
#include <linux/virtio.h>
#include <linux/virtio_types.h>
#include <linux/virtio_config.h>

#define MD_SS_UPDATE_REGION	0
#define MD_SS_ADD_REGION	1
#define MD_SS_REMOVE_REGION	2

#define VIRTIO_ID_MINIDUMP 0xC00D

#define MINIDUMP_MAX_NAME_LENGTH	14
#define MAX_ENTRY_NUM	200

struct virtio_minidump {
	struct virtio_device *vdev;
	struct virtqueue *vq;
	struct completion rsp_avail;
	struct mutex lock;
};

struct virtio_minidump_msg {
	__virtio32 type;
	u8 name[MINIDUMP_MAX_NAME_LENGTH];
	__virtio64 phy_addr;
	__virtio64 size;
	__virtio32 result;
};

struct md_request {
	struct md_region	entry;
	struct work_struct	work;
	enum minidump_entry_cmd	minidump_cmd;
};

/* Protect elfheader from deferred calls contention */
static DEFINE_RWLOCK(mdt_remove_lock);
static struct workqueue_struct *minidump_wq;

/* Set as globle variable */
static struct virtio_minidump *vmd;

static void md_virtio_add_work(struct md_region *entry)
{
	struct virtio_minidump_msg *req, *rsp;
	struct scatterlist sg[1];
	unsigned int len;
	int ret = 0;

	if (!pfn_is_map_memory(virt_to_pfn(entry->virt_addr))) {
		pr_err("%s: Invalid Phy address %llu\n", entry->name, entry->phys_addr);
		return;
	}

	req = kzalloc(sizeof(struct virtio_minidump_msg), GFP_KERNEL);
	if (!req) {
		pr_err("%s: Alloc message memory fail\n", entry->name);
		return;
	}

	strscpy(req->name, entry->name, sizeof(req->name));
	req->phy_addr = cpu_to_virtio64(vmd->vdev, entry->phys_addr);
	req->size = cpu_to_virtio64(vmd->vdev, entry->size);
	req->type = cpu_to_virtio32(vmd->vdev, MD_SS_ADD_REGION);
	sg_init_one(sg, req, sizeof(*req));

	mutex_lock(&vmd->lock);

	ret = virtqueue_add_outbuf(vmd->vq, sg, 1, req, GFP_KERNEL);
	if (ret) {
		pr_err("%s: fail to add output buffer\n", entry->name);
		goto out;
	}

	virtqueue_kick(vmd->vq);

	wait_for_completion(&vmd->rsp_avail);

	rsp = virtqueue_get_buf(vmd->vq, &len);
	if (!rsp) {
		pr_err("%s: fail to get virtqueue buffer\n", entry->name);
		goto out;
	}

	ret = virtio32_to_cpu(vmd->vdev, rsp->result);

out:
	mutex_unlock(&vmd->lock);
	kfree(req);

	pr_debug("%s: %s return %d\n", __func__, entry->name, ret);

}

static void md_virtio_remove_work(struct md_region *entry)
{
	struct virtio_minidump_msg *req, *rsp;
	struct scatterlist sg[1];
	unsigned int len;
	int ret = 0;

	req = kzalloc(sizeof(struct virtio_minidump_msg), GFP_KERNEL);
	if (!req) {
		pr_err("%s: Alloc message memory fail\n", entry->name);
		return;
	}

	strscpy(req->name, entry->name, sizeof(req->name));
	req->type = cpu_to_virtio32(vmd->vdev, MD_SS_REMOVE_REGION);
	sg_init_one(sg, req, sizeof(*req));

	mutex_lock(&vmd->lock);

	ret = virtqueue_add_outbuf(vmd->vq, sg, 1, req, GFP_KERNEL);
	if (ret) {
		pr_err("%s: fail to add output buffer\n", entry->name);
		goto out;
	}

	virtqueue_kick(vmd->vq);

	wait_for_completion(&vmd->rsp_avail);

	rsp = virtqueue_get_buf(vmd->vq, &len);
	if (!rsp) {
		pr_err("%s: fail to get virtqueue buffer\n", entry->name);
		goto out;
	}

	ret = virtio32_to_cpu(vmd->vdev, rsp->result);

out:
	mutex_unlock(&vmd->lock);
	kfree(req);

	pr_debug("%s: %s return %d\n", __func__, entry->name, ret);
}

static void md_virtio_update_work(struct md_region *entry)
{
	struct virtio_minidump_msg *req, *rsp;
	struct scatterlist sg[1];
	unsigned int len;
	int ret = 0;

	req = kzalloc(sizeof(struct virtio_minidump_msg), GFP_KERNEL);
	if (!req) {
		pr_err("%s: Alloc message memory fail\n", entry->name);
		return;
	}

	strscpy(req->name, entry->name, sizeof(req->name));
	req->phy_addr = cpu_to_virtio64(vmd->vdev, entry->phys_addr);
	req->size = cpu_to_virtio64(vmd->vdev, entry->size);
	req->type = cpu_to_virtio32(vmd->vdev, MD_SS_UPDATE_REGION);
	sg_init_one(sg, req, sizeof(*req));

	mutex_lock(&vmd->lock);

	ret = virtqueue_add_outbuf(vmd->vq, sg, 1, req, GFP_KERNEL);
	if (ret) {
		pr_err("%s: fail to add output buffer\n", entry->name);
		goto out;
	}

	virtqueue_kick(vmd->vq);

	wait_for_completion(&vmd->rsp_avail);

	rsp = virtqueue_get_buf(vmd->vq, &len);
	if (!rsp) {
		pr_err("%s: fail to get virtqueue buffer\n", entry->name);
		goto out;
	}

	ret = virtio32_to_cpu(vmd->vdev, rsp->result);

out:
	mutex_unlock(&vmd->lock);
	kfree(req);

	pr_debug("%s: %s return %d\n", __func__, entry->name, ret);
}

static void minidump_work(struct work_struct *work)
{
	struct md_request *vm_work =
		container_of(work, struct md_request, work);
	enum minidump_entry_cmd cmd;

	cmd = vm_work->minidump_cmd;

	switch (cmd) {
	case MINIDUMP_UPDATE:
		md_virtio_update_work(&vm_work->entry);
		break;
	case MINIDUMP_ADD:
		md_virtio_add_work(&vm_work->entry);
		break;
	case MINIDUMP_REMOVE:
		md_virtio_remove_work(&vm_work->entry);
		break;
	default:
		pr_debug("No command for virtio minidump work\n");
		break;
	}
	kfree(vm_work);
}

static int virtio_add_region(const struct md_region *entry)
{
	struct md_request *vm_work;

	/* alloc an entry for workqueue, need free in work */
	vm_work = kzalloc(sizeof(*vm_work), GFP_ATOMIC);
	if (!vm_work)
		return -ENOMEM;
	vm_work->entry = *entry;
	vm_work->minidump_cmd = MINIDUMP_ADD;
	INIT_WORK(&vm_work->work, minidump_work);
	queue_work(minidump_wq, &vm_work->work);

	return 0;
}

static int virtio_remove_region(const struct md_region *entry)
{
	struct md_request *vm_work;

	/* alloc an entry for workqueue, need free in work */
	vm_work = kzalloc(sizeof(*vm_work), GFP_ATOMIC);
	if (!vm_work)
		return -ENOMEM;
	vm_work->entry = *entry;
	vm_work->minidump_cmd = MINIDUMP_REMOVE;
	INIT_WORK(&vm_work->work, minidump_work);
	queue_work(minidump_wq, &vm_work->work);

	return 0;
}

static int virtio_update_region(const struct md_region *entry)
{
	struct md_request *vm_work;

	/* alloc an entry for workqueue, need free in work */
	vm_work = kzalloc(sizeof(*vm_work), GFP_ATOMIC);
	if (!vm_work)
		return -ENOMEM;
	vm_work->entry = *entry;
	vm_work->minidump_cmd = MINIDUMP_UPDATE;
	INIT_WORK(&vm_work->work, minidump_work);
	queue_work(minidump_wq, &vm_work->work);

	return 0;
}

static int virtio_get_available_region(void)
{
	return (MAX_ENTRY_NUM - md_num_regions);
}

/* Check available region count */
static int md_virtio_init_md_table(void)
{
	int res;

	res = virtio_get_available_region();
	if (res < 0) {
		pr_err("Get minidump info failed ret=%d\n", res);
		return res;
	}

	pr_debug("Get available region count: %d\n", res);

	minidump_wq = create_singlethread_workqueue("minidump_wq");
	if (!minidump_wq) {
		pr_err("Unable to initialize workqueue\n");
		return -EINVAL;
	}

	return 0;
}

static int md_virtio_add_pending_entry(struct list_head *pending_list)
{
	struct md_pending_region *pending_region, *tmp;
	unsigned long flags;

	/* Add pending entries to HLOS TOC */
	list_for_each_entry_safe(pending_region, tmp, pending_list, list) {
		virtio_add_region(&pending_region->entry);

		spin_lock_irqsave(&mdt_lock, flags);
		md_add_elf_header(&pending_region->entry);
		list_del(&pending_region->list);
		kfree(pending_region);
		md_num_regions++;
		spin_unlock_irqrestore(&mdt_lock, flags);
	}

	return 0;
}

static void md_virtio_add_header(struct elf_shdr *shdr, struct elfhdr *ehdr, unsigned int elfh_size)
{
	struct md_region *entry;
	char *hdr_name = "KELF_HDR";
	int ret;

	entry = kzalloc(sizeof(struct md_region), GFP_KERNEL);
	if (!entry)
		return;

	strscpy(entry->name, hdr_name, sizeof(entry->name));
	entry->virt_addr = (u64)minidump_elfheader.ehdr;
	entry->phys_addr = virt_to_phys(minidump_elfheader.ehdr);
	entry->size = elfh_size;

	ret = virtio_add_region(entry);
	if (ret)
		pr_err("Failed to register ELF header region\n");

	ehdr->e_shnum = 3;

	kfree(entry);
}

static int md_virtio_remove_region(const struct md_region *entry)
{
	int ret;
	unsigned long flags;

	ret = virtio_remove_region(entry);

	if (ret)
		return -EBUSY;

	spin_lock_irqsave(&mdt_lock, flags);
	write_lock(&mdt_remove_lock);
	msm_minidump_clear_headers(entry);
	md_num_regions--;
	write_unlock(&mdt_remove_lock);
	spin_unlock_irqrestore(&mdt_lock, flags);

	return ret;
}

static int md_virtio_add_region(const struct md_region *entry)
{
	int ret;
	unsigned long flags;

	ret = virtio_add_region(entry);
	if (ret)
		return -EBUSY;

	spin_lock_irqsave(&mdt_lock, flags);
	md_add_elf_header(entry);
	ret = md_num_regions;
	md_num_regions++;
	spin_unlock_irqrestore(&mdt_lock, flags);

	return ret;
}

static void md_virtio_update_elf_header(int entryno, const struct md_region *entry)
{
	struct elfhdr *hdr = minidump_elfheader.ehdr;
	struct elf_shdr *shdr;
	struct elf_phdr *phdr;

	shdr = elf_section(hdr, entryno + 3);
	phdr = elf_program(hdr, entryno + 1);
	shdr->sh_addr = (elf_addr_t)entry->virt_addr;
	phdr->p_vaddr = entry->virt_addr;
	phdr->p_paddr = entry->phys_addr;
}

static int md_virtio_update_region(int regno, const struct md_region *entry)
{
	int ret = 0;
	unsigned long flags;

	ret = virtio_update_region(entry);
	if (ret)
		return -EBUSY;

	read_lock_irqsave(&mdt_remove_lock, flags);
	md_virtio_update_elf_header(regno, entry);
	read_unlock_irqrestore(&mdt_remove_lock, flags);

	return ret;
}

static int md_virtio_get_available_region(void)
{
	int res = -EBUSY;
	unsigned long flags;

	spin_lock_irqsave(&mdt_lock, flags);
	res = virtio_get_available_region();
	spin_unlock_irqrestore(&mdt_lock, flags);

	return res;
}

static bool md_virtio_md_enable(void)
{
	return true;
}

static struct md_region md_virtio_get_region(char *name)
{
	struct md_region tmp = {0};
	int i, j;
	struct elfhdr *hdr = minidump_elfheader.ehdr;
	struct elf_phdr *phdr;
	struct elf_shdr *shdr;
	char *hdr_name;

	for (i = 0; i < hdr->e_shnum; i++) {
		shdr = elf_section(hdr, i);
		hdr_name = elf_lookup_string(hdr, shdr->sh_name);
		if (hdr_name && !strcmp(hdr_name, name)) {
			for (j = 0; j < hdr->e_phnum; j++) {
				phdr = elf_program(hdr, j);
				if (shdr->sh_addr == phdr->p_vaddr) {
					strscpy(tmp.name, hdr_name,
						sizeof(tmp.name));
					tmp.phys_addr = phdr->p_vaddr;
					tmp.virt_addr = phdr->p_paddr;
					tmp.size = phdr->p_filesz;
					goto out;
				}
			}
		}
	}

out:
	return tmp;
}

static const struct md_ops md_virtio_ops = {
	.init_md_table			= md_virtio_init_md_table,
	.add_pending_entry		= md_virtio_add_pending_entry,
	.add_header				= md_virtio_add_header,
	.remove_region			= md_virtio_remove_region,
	.add_region				= md_virtio_add_region,
	.update_region			= md_virtio_update_region,
	.get_available_region	= md_virtio_get_available_region,
	.md_enable				= md_virtio_md_enable,
	.get_region				= md_virtio_get_region,
};

static struct md_init_data md_virtio_init_data = {
	.ops = &md_virtio_ops,
};

/* virtqueue incoming data interrupt IRQ */
static void virtio_minidump_isr(struct virtqueue *vq)
{
	struct virtio_minidump *vmd = vq->vdev->priv;

	complete(&vmd->rsp_avail);
}

static int virtio_md_init_vqs(struct virtio_minidump *vmd)
{
	struct virtqueue *vqs[1];
	vq_callback_t *cbs[] = { virtio_minidump_isr };
	static const char * const names[] = { "virtio_minidump_isr" };
	int err;

	err = virtio_find_vqs(vmd->vdev, 1, vqs, cbs, names, NULL);
	if (err)
		return err;
	vmd->vq = vqs[0];

	return 0;
}

static int minidump_virtio_driver_probe(struct virtio_device *vdev)
{
	int ret = 0;

	if (!virtio_has_feature(vdev, VIRTIO_F_VERSION_1))
		return -ENODEV;

	if (vmd)
		return -EEXIST;

	vmd = devm_kzalloc(&vdev->dev, sizeof(struct virtio_minidump),
			GFP_KERNEL);

	if (!vmd)
		return -ENOMEM;

	vmd->vdev = vdev;
	vdev->priv = vmd;
	mutex_init(&vmd->lock);
	init_completion(&vmd->rsp_avail);

	ret = virtio_md_init_vqs(vmd);
	if (ret) {
		dev_err(&vdev->dev, "fail to initialize virtqueue\n");
		return ret;
	}

	virtio_device_ready(vdev);

	ret = msm_minidump_driver_probe(&md_virtio_init_data);
	if (ret) {
		dev_err(&vdev->dev, "fail to probe driver\n");
		return ret;
	}

	return ret;
}

static void minidump_virtio_driver_remove(struct virtio_device *vdev)
{
	void *buf;

	vdev->config->reset(vdev);
	while ((buf = virtqueue_detach_unused_buf(vmd->vq)) != NULL)
		kfree(buf);
	vdev->config->del_vqs(vdev);
}

static const struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_MINIDUMP, VIRTIO_DEV_ANY_ID},
	{ },
};

static unsigned int features[] = {
	/* none */
};

static struct virtio_driver msm_minidump_virtio_driver = {
	.feature_table = features,
	.feature_table_size = ARRAY_SIZE(features),
	.driver.name = KBUILD_MODNAME,
	.driver.owner = THIS_MODULE,
	.id_table = id_table,
	.probe = minidump_virtio_driver_probe,
	.remove = minidump_virtio_driver_remove,
};

module_virtio_driver(msm_minidump_virtio_driver);

MODULE_LICENSE("GPL");
