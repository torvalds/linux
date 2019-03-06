// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2019 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "habanalabs.h"
#include "include/hw_ip/mmu/mmu_general.h"

#include <linux/pci.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>

#define MMU_ADDR_BUF_SIZE	40
#define MMU_ASID_BUF_SIZE	10
#define MMU_KBUF_SIZE		(MMU_ADDR_BUF_SIZE + MMU_ASID_BUF_SIZE)

static struct dentry *hl_debug_root;

static int hl_debugfs_i2c_read(struct hl_device *hdev, u8 i2c_bus, u8 i2c_addr,
				u8 i2c_reg, u32 *val)
{
	struct armcp_packet pkt;
	int rc;

	if (hl_device_disabled_or_in_reset(hdev))
		return -EBUSY;

	memset(&pkt, 0, sizeof(pkt));

	pkt.ctl = __cpu_to_le32(ARMCP_PACKET_I2C_RD <<
				ARMCP_PKT_CTL_OPCODE_SHIFT);
	pkt.i2c_bus = i2c_bus;
	pkt.i2c_addr = i2c_addr;
	pkt.i2c_reg = i2c_reg;

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt),
					HL_DEVICE_TIMEOUT_USEC, (long *) val);

	if (rc)
		dev_err(hdev->dev, "Failed to read from I2C, error %d\n", rc);

	return rc;
}

static int hl_debugfs_i2c_write(struct hl_device *hdev, u8 i2c_bus, u8 i2c_addr,
				u8 i2c_reg, u32 val)
{
	struct armcp_packet pkt;
	int rc;

	if (hl_device_disabled_or_in_reset(hdev))
		return -EBUSY;

	memset(&pkt, 0, sizeof(pkt));

	pkt.ctl = __cpu_to_le32(ARMCP_PACKET_I2C_WR <<
				ARMCP_PKT_CTL_OPCODE_SHIFT);
	pkt.i2c_bus = i2c_bus;
	pkt.i2c_addr = i2c_addr;
	pkt.i2c_reg = i2c_reg;
	pkt.value = __cpu_to_le64(val);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt),
					HL_DEVICE_TIMEOUT_USEC, NULL);

	if (rc)
		dev_err(hdev->dev, "Failed to write to I2C, error %d\n", rc);

	return rc;
}

static void hl_debugfs_led_set(struct hl_device *hdev, u8 led, u8 state)
{
	struct armcp_packet pkt;
	int rc;

	if (hl_device_disabled_or_in_reset(hdev))
		return;

	memset(&pkt, 0, sizeof(pkt));

	pkt.ctl = __cpu_to_le32(ARMCP_PACKET_LED_SET <<
				ARMCP_PKT_CTL_OPCODE_SHIFT);
	pkt.led_index = __cpu_to_le32(led);
	pkt.value = __cpu_to_le64(state);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt),
						HL_DEVICE_TIMEOUT_USEC, NULL);

	if (rc)
		dev_err(hdev->dev, "Failed to set LED %d, error %d\n", led, rc);
}

static int command_buffers_show(struct seq_file *s, void *data)
{
	struct hl_debugfs_entry *entry = s->private;
	struct hl_dbg_device_entry *dev_entry = entry->dev_entry;
	struct hl_cb *cb;
	bool first = true;

	spin_lock(&dev_entry->cb_spinlock);

	list_for_each_entry(cb, &dev_entry->cb_list, debugfs_list) {
		if (first) {
			first = false;
			seq_puts(s, "\n");
			seq_puts(s, " CB ID   CTX ID   CB size    CB RefCnt    mmap?   CS counter\n");
			seq_puts(s, "---------------------------------------------------------------\n");
		}
		seq_printf(s,
			"   %03d        %d    0x%08x      %d          %d          %d\n",
			cb->id, cb->ctx_id, cb->size,
			kref_read(&cb->refcount),
			cb->mmap, cb->cs_cnt);
	}

	spin_unlock(&dev_entry->cb_spinlock);

	if (!first)
		seq_puts(s, "\n");

	return 0;
}

static int command_submission_show(struct seq_file *s, void *data)
{
	struct hl_debugfs_entry *entry = s->private;
	struct hl_dbg_device_entry *dev_entry = entry->dev_entry;
	struct hl_cs *cs;
	bool first = true;

	spin_lock(&dev_entry->cs_spinlock);

	list_for_each_entry(cs, &dev_entry->cs_list, debugfs_list) {
		if (first) {
			first = false;
			seq_puts(s, "\n");
			seq_puts(s, " CS ID   CTX ASID   CS RefCnt   Submitted    Completed\n");
			seq_puts(s, "------------------------------------------------------\n");
		}
		seq_printf(s,
			"   %llu       %d          %d           %d            %d\n",
			cs->sequence, cs->ctx->asid,
			kref_read(&cs->refcount),
			cs->submitted, cs->completed);
	}

	spin_unlock(&dev_entry->cs_spinlock);

	if (!first)
		seq_puts(s, "\n");

	return 0;
}

static int command_submission_jobs_show(struct seq_file *s, void *data)
{
	struct hl_debugfs_entry *entry = s->private;
	struct hl_dbg_device_entry *dev_entry = entry->dev_entry;
	struct hl_cs_job *job;
	bool first = true;

	spin_lock(&dev_entry->cs_job_spinlock);

	list_for_each_entry(job, &dev_entry->cs_job_list, debugfs_list) {
		if (first) {
			first = false;
			seq_puts(s, "\n");
			seq_puts(s, " JOB ID   CS ID    CTX ASID   H/W Queue\n");
			seq_puts(s, "---------------------------------------\n");
		}
		if (job->cs)
			seq_printf(s,
				"    %02d       %llu         %d         %d\n",
				job->id, job->cs->sequence, job->cs->ctx->asid,
				job->hw_queue_id);
		else
			seq_printf(s,
				"    %02d       0         %d         %d\n",
				job->id, HL_KERNEL_ASID_ID, job->hw_queue_id);
	}

	spin_unlock(&dev_entry->cs_job_spinlock);

	if (!first)
		seq_puts(s, "\n");

	return 0;
}

static int userptr_show(struct seq_file *s, void *data)
{
	struct hl_debugfs_entry *entry = s->private;
	struct hl_dbg_device_entry *dev_entry = entry->dev_entry;
	struct hl_userptr *userptr;
	char dma_dir[4][30] = {"DMA_BIDIRECTIONAL", "DMA_TO_DEVICE",
				"DMA_FROM_DEVICE", "DMA_NONE"};
	bool first = true;

	spin_lock(&dev_entry->userptr_spinlock);

	list_for_each_entry(userptr, &dev_entry->userptr_list, debugfs_list) {
		if (first) {
			first = false;
			seq_puts(s, "\n");
			seq_puts(s, " user virtual address     size             dma dir\n");
			seq_puts(s, "----------------------------------------------------------\n");
		}
		seq_printf(s,
			"    0x%-14llx      %-10u    %-30s\n",
			userptr->addr, userptr->size, dma_dir[userptr->dir]);
	}

	spin_unlock(&dev_entry->userptr_spinlock);

	if (!first)
		seq_puts(s, "\n");

	return 0;
}

static int vm_show(struct seq_file *s, void *data)
{
	struct hl_debugfs_entry *entry = s->private;
	struct hl_dbg_device_entry *dev_entry = entry->dev_entry;
	struct hl_ctx *ctx;
	struct hl_vm *vm;
	struct hl_vm_hash_node *hnode;
	struct hl_userptr *userptr;
	struct hl_vm_phys_pg_pack *phys_pg_pack = NULL;
	enum vm_type_t *vm_type;
	bool once = true;
	int i;

	if (!dev_entry->hdev->mmu_enable)
		return 0;

	spin_lock(&dev_entry->ctx_mem_hash_spinlock);

	list_for_each_entry(ctx, &dev_entry->ctx_mem_hash_list, debugfs_list) {
		once = false;
		seq_puts(s, "\n\n----------------------------------------------------");
		seq_puts(s, "\n----------------------------------------------------\n\n");
		seq_printf(s, "ctx asid: %u\n", ctx->asid);

		seq_puts(s, "\nmappings:\n\n");
		seq_puts(s, "    virtual address        size          handle\n");
		seq_puts(s, "----------------------------------------------------\n");
		mutex_lock(&ctx->mem_hash_lock);
		hash_for_each(ctx->mem_hash, i, hnode, node) {
			vm_type = hnode->ptr;

			if (*vm_type == VM_TYPE_USERPTR) {
				userptr = hnode->ptr;
				seq_printf(s,
					"    0x%-14llx      %-10u\n",
					hnode->vaddr, userptr->size);
			} else {
				phys_pg_pack = hnode->ptr;
				seq_printf(s,
					"    0x%-14llx      %-10u       %-4u\n",
					hnode->vaddr, phys_pg_pack->total_size,
					phys_pg_pack->handle);
			}
		}
		mutex_unlock(&ctx->mem_hash_lock);

		vm = &ctx->hdev->vm;
		spin_lock(&vm->idr_lock);

		if (!idr_is_empty(&vm->phys_pg_pack_handles))
			seq_puts(s, "\n\nallocations:\n");

		idr_for_each_entry(&vm->phys_pg_pack_handles, phys_pg_pack, i) {
			if (phys_pg_pack->asid != ctx->asid)
				continue;

			seq_printf(s, "\nhandle: %u\n", phys_pg_pack->handle);
			seq_printf(s, "page size: %u\n\n",
						phys_pg_pack->page_size);
			seq_puts(s, "   physical address\n");
			seq_puts(s, "---------------------\n");
			for (i = 0 ; i < phys_pg_pack->npages ; i++) {
				seq_printf(s, "    0x%-14llx\n",
						phys_pg_pack->pages[i]);
			}
		}
		spin_unlock(&vm->idr_lock);

	}

	spin_unlock(&dev_entry->ctx_mem_hash_spinlock);

	if (!once)
		seq_puts(s, "\n");

	return 0;
}

/* these inline functions are copied from mmu.c */
static inline u64 get_hop0_addr(struct hl_ctx *ctx)
{
	return ctx->hdev->asic_prop.mmu_pgt_addr +
			(ctx->asid * ctx->hdev->asic_prop.mmu_hop_table_size);
}

static inline u64 get_hop0_pte_addr(struct hl_ctx *ctx, u64 hop_addr,
		u64 virt_addr)
{
	return hop_addr + ctx->hdev->asic_prop.mmu_pte_size *
			((virt_addr & HOP0_MASK) >> HOP0_SHIFT);
}

static inline u64 get_hop1_pte_addr(struct hl_ctx *ctx, u64 hop_addr,
		u64 virt_addr)
{
	return hop_addr + ctx->hdev->asic_prop.mmu_pte_size *
			((virt_addr & HOP1_MASK) >> HOP1_SHIFT);
}

static inline u64 get_hop2_pte_addr(struct hl_ctx *ctx, u64 hop_addr,
		u64 virt_addr)
{
	return hop_addr + ctx->hdev->asic_prop.mmu_pte_size *
			((virt_addr & HOP2_MASK) >> HOP2_SHIFT);
}

static inline u64 get_hop3_pte_addr(struct hl_ctx *ctx, u64 hop_addr,
		u64 virt_addr)
{
	return hop_addr + ctx->hdev->asic_prop.mmu_pte_size *
			((virt_addr & HOP3_MASK) >> HOP3_SHIFT);
}

static inline u64 get_hop4_pte_addr(struct hl_ctx *ctx, u64 hop_addr,
		u64 virt_addr)
{
	return hop_addr + ctx->hdev->asic_prop.mmu_pte_size *
			((virt_addr & HOP4_MASK) >> HOP4_SHIFT);
}

static inline u64 get_next_hop_addr(u64 curr_pte)
{
	if (curr_pte & PAGE_PRESENT_MASK)
		return curr_pte & PHYS_ADDR_MASK;
	else
		return ULLONG_MAX;
}

static int mmu_show(struct seq_file *s, void *data)
{
	struct hl_debugfs_entry *entry = s->private;
	struct hl_dbg_device_entry *dev_entry = entry->dev_entry;
	struct hl_device *hdev = dev_entry->hdev;
	struct hl_ctx *ctx = hdev->user_ctx;

	u64 hop0_addr = 0, hop0_pte_addr = 0, hop0_pte = 0,
		hop1_addr = 0, hop1_pte_addr = 0, hop1_pte = 0,
		hop2_addr = 0, hop2_pte_addr = 0, hop2_pte = 0,
		hop3_addr = 0, hop3_pte_addr = 0, hop3_pte = 0,
		hop4_addr = 0, hop4_pte_addr = 0, hop4_pte = 0,
		virt_addr = dev_entry->mmu_addr;

	if (!hdev->mmu_enable)
		return 0;

	if (!ctx) {
		dev_err(hdev->dev, "no ctx available\n");
		return 0;
	}

	mutex_lock(&ctx->mmu_lock);

	/* the following lookup is copied from unmap() in mmu.c */

	hop0_addr = get_hop0_addr(ctx);
	hop0_pte_addr = get_hop0_pte_addr(ctx, hop0_addr, virt_addr);
	hop0_pte = hdev->asic_funcs->read_pte(hdev, hop0_pte_addr);
	hop1_addr = get_next_hop_addr(hop0_pte);

	if (hop1_addr == ULLONG_MAX)
		goto not_mapped;

	hop1_pte_addr = get_hop1_pte_addr(ctx, hop1_addr, virt_addr);
	hop1_pte = hdev->asic_funcs->read_pte(hdev, hop1_pte_addr);
	hop2_addr = get_next_hop_addr(hop1_pte);

	if (hop2_addr == ULLONG_MAX)
		goto not_mapped;

	hop2_pte_addr = get_hop2_pte_addr(ctx, hop2_addr, virt_addr);
	hop2_pte = hdev->asic_funcs->read_pte(hdev, hop2_pte_addr);
	hop3_addr = get_next_hop_addr(hop2_pte);

	if (hop3_addr == ULLONG_MAX)
		goto not_mapped;

	hop3_pte_addr = get_hop3_pte_addr(ctx, hop3_addr, virt_addr);
	hop3_pte = hdev->asic_funcs->read_pte(hdev, hop3_pte_addr);

	if (!(hop3_pte & LAST_MASK)) {
		hop4_addr = get_next_hop_addr(hop3_pte);

		if (hop4_addr == ULLONG_MAX)
			goto not_mapped;

		hop4_pte_addr = get_hop4_pte_addr(ctx, hop4_addr, virt_addr);
		hop4_pte = hdev->asic_funcs->read_pte(hdev, hop4_pte_addr);
		if (!(hop4_pte & PAGE_PRESENT_MASK))
			goto not_mapped;
	} else {
		if (!(hop3_pte & PAGE_PRESENT_MASK))
			goto not_mapped;
	}

	seq_printf(s, "asid: %u, virt_addr: 0x%llx\n",
			dev_entry->mmu_asid, dev_entry->mmu_addr);

	seq_printf(s, "hop0_addr: 0x%llx\n", hop0_addr);
	seq_printf(s, "hop0_pte_addr: 0x%llx\n", hop0_pte_addr);
	seq_printf(s, "hop0_pte: 0x%llx\n", hop0_pte);

	seq_printf(s, "hop1_addr: 0x%llx\n", hop1_addr);
	seq_printf(s, "hop1_pte_addr: 0x%llx\n", hop1_pte_addr);
	seq_printf(s, "hop1_pte: 0x%llx\n", hop1_pte);

	seq_printf(s, "hop2_addr: 0x%llx\n", hop2_addr);
	seq_printf(s, "hop2_pte_addr: 0x%llx\n", hop2_pte_addr);
	seq_printf(s, "hop2_pte: 0x%llx\n", hop2_pte);

	seq_printf(s, "hop3_addr: 0x%llx\n", hop3_addr);
	seq_printf(s, "hop3_pte_addr: 0x%llx\n", hop3_pte_addr);
	seq_printf(s, "hop3_pte: 0x%llx\n", hop3_pte);

	if (!(hop3_pte & LAST_MASK)) {
		seq_printf(s, "hop4_addr: 0x%llx\n", hop4_addr);
		seq_printf(s, "hop4_pte_addr: 0x%llx\n", hop4_pte_addr);
		seq_printf(s, "hop4_pte: 0x%llx\n", hop4_pte);
	}

	goto out;

not_mapped:
	dev_err(hdev->dev, "virt addr 0x%llx is not mapped to phys addr\n",
			virt_addr);
out:
	mutex_unlock(&ctx->mmu_lock);

	return 0;
}

static ssize_t mmu_write(struct file *file, const char __user *buf,
		size_t count, loff_t *f_pos)
{
	struct seq_file *s = file->private_data;
	struct hl_debugfs_entry *entry = s->private;
	struct hl_dbg_device_entry *dev_entry = entry->dev_entry;
	struct hl_device *hdev = dev_entry->hdev;
	char kbuf[MMU_KBUF_SIZE], asid_kbuf[MMU_ASID_BUF_SIZE],
		addr_kbuf[MMU_ADDR_BUF_SIZE];
	char *c;
	ssize_t rc;

	if (!hdev->mmu_enable)
		return count;

	memset(kbuf, 0, sizeof(kbuf));
	memset(asid_kbuf, 0, sizeof(asid_kbuf));
	memset(addr_kbuf, 0, sizeof(addr_kbuf));

	if (copy_from_user(kbuf, buf, count))
		goto err;

	kbuf[MMU_KBUF_SIZE - 1] = 0;

	c = strchr(kbuf, ' ');
	if (!c)
		goto err;

	memcpy(asid_kbuf, kbuf, c - kbuf);

	rc = kstrtouint(asid_kbuf, 10, &dev_entry->mmu_asid);
	if (rc)
		goto err;

	c = strstr(kbuf, " 0x");
	if (!c)
		goto err;

	c += 3;
	memcpy(addr_kbuf, c, (kbuf + count) - c);

	rc = kstrtoull(addr_kbuf, 16, &dev_entry->mmu_addr);
	if (rc)
		goto err;

	return count;

err:
	dev_err(hdev->dev, "usage: echo <asid> <0xaddr> > mmu\n");

	return -EINVAL;
}

static ssize_t hl_data_read32(struct file *f, char __user *buf,
					size_t count, loff_t *ppos)
{
	struct hl_dbg_device_entry *entry = file_inode(f)->i_private;
	struct hl_device *hdev = entry->hdev;
	char tmp_buf[32];
	u32 val;
	ssize_t rc;

	if (*ppos)
		return 0;

	rc = hdev->asic_funcs->debugfs_read32(hdev, entry->addr, &val);
	if (rc) {
		dev_err(hdev->dev, "Failed to read from 0x%010llx\n",
			entry->addr);
		return rc;
	}

	sprintf(tmp_buf, "0x%08x\n", val);
	rc = simple_read_from_buffer(buf, strlen(tmp_buf) + 1, ppos, tmp_buf,
			strlen(tmp_buf) + 1);

	return rc;
}

static ssize_t hl_data_write32(struct file *f, const char __user *buf,
					size_t count, loff_t *ppos)
{
	struct hl_dbg_device_entry *entry = file_inode(f)->i_private;
	struct hl_device *hdev = entry->hdev;
	u32 value;
	ssize_t rc;

	rc = kstrtouint_from_user(buf, count, 16, &value);
	if (rc)
		return rc;

	rc = hdev->asic_funcs->debugfs_write32(hdev, entry->addr, value);
	if (rc) {
		dev_err(hdev->dev, "Failed to write 0x%08x to 0x%010llx\n",
			value, entry->addr);
		return rc;
	}

	return count;
}

static ssize_t hl_get_power_state(struct file *f, char __user *buf,
		size_t count, loff_t *ppos)
{
	struct hl_dbg_device_entry *entry = file_inode(f)->i_private;
	struct hl_device *hdev = entry->hdev;
	char tmp_buf[200];
	ssize_t rc;
	int i;

	if (*ppos)
		return 0;

	if (hdev->pdev->current_state == PCI_D0)
		i = 1;
	else if (hdev->pdev->current_state == PCI_D3hot)
		i = 2;
	else
		i = 3;

	sprintf(tmp_buf,
		"current power state: %d\n1 - D0\n2 - D3hot\n3 - Unknown\n", i);
	rc = simple_read_from_buffer(buf, strlen(tmp_buf) + 1, ppos, tmp_buf,
			strlen(tmp_buf) + 1);

	return rc;
}

static ssize_t hl_set_power_state(struct file *f, const char __user *buf,
					size_t count, loff_t *ppos)
{
	struct hl_dbg_device_entry *entry = file_inode(f)->i_private;
	struct hl_device *hdev = entry->hdev;
	u32 value;
	ssize_t rc;

	rc = kstrtouint_from_user(buf, count, 10, &value);
	if (rc)
		return rc;

	if (value == 1) {
		pci_set_power_state(hdev->pdev, PCI_D0);
		pci_restore_state(hdev->pdev);
		rc = pci_enable_device(hdev->pdev);
	} else if (value == 2) {
		pci_save_state(hdev->pdev);
		pci_disable_device(hdev->pdev);
		pci_set_power_state(hdev->pdev, PCI_D3hot);
	} else {
		dev_dbg(hdev->dev, "invalid power state value %u\n", value);
		return -EINVAL;
	}

	return count;
}

static ssize_t hl_i2c_data_read(struct file *f, char __user *buf,
					size_t count, loff_t *ppos)
{
	struct hl_dbg_device_entry *entry = file_inode(f)->i_private;
	struct hl_device *hdev = entry->hdev;
	char tmp_buf[32];
	u32 val;
	ssize_t rc;

	if (*ppos)
		return 0;

	rc = hl_debugfs_i2c_read(hdev, entry->i2c_bus, entry->i2c_addr,
			entry->i2c_reg, &val);
	if (rc) {
		dev_err(hdev->dev,
			"Failed to read from I2C bus %d, addr %d, reg %d\n",
			entry->i2c_bus, entry->i2c_addr, entry->i2c_reg);
		return rc;
	}

	sprintf(tmp_buf, "0x%02x\n", val);
	rc = simple_read_from_buffer(buf, strlen(tmp_buf) + 1, ppos, tmp_buf,
			strlen(tmp_buf) + 1);

	return rc;
}

static ssize_t hl_i2c_data_write(struct file *f, const char __user *buf,
					size_t count, loff_t *ppos)
{
	struct hl_dbg_device_entry *entry = file_inode(f)->i_private;
	struct hl_device *hdev = entry->hdev;
	u32 value;
	ssize_t rc;

	rc = kstrtouint_from_user(buf, count, 16, &value);
	if (rc)
		return rc;

	rc = hl_debugfs_i2c_write(hdev, entry->i2c_bus, entry->i2c_addr,
			entry->i2c_reg, value);
	if (rc) {
		dev_err(hdev->dev,
			"Failed to write 0x%02x to I2C bus %d, addr %d, reg %d\n",
			value, entry->i2c_bus, entry->i2c_addr, entry->i2c_reg);
		return rc;
	}

	return count;
}

static ssize_t hl_led0_write(struct file *f, const char __user *buf,
					size_t count, loff_t *ppos)
{
	struct hl_dbg_device_entry *entry = file_inode(f)->i_private;
	struct hl_device *hdev = entry->hdev;
	u32 value;
	ssize_t rc;

	rc = kstrtouint_from_user(buf, count, 10, &value);
	if (rc)
		return rc;

	value = value ? 1 : 0;

	hl_debugfs_led_set(hdev, 0, value);

	return count;
}

static ssize_t hl_led1_write(struct file *f, const char __user *buf,
					size_t count, loff_t *ppos)
{
	struct hl_dbg_device_entry *entry = file_inode(f)->i_private;
	struct hl_device *hdev = entry->hdev;
	u32 value;
	ssize_t rc;

	rc = kstrtouint_from_user(buf, count, 10, &value);
	if (rc)
		return rc;

	value = value ? 1 : 0;

	hl_debugfs_led_set(hdev, 1, value);

	return count;
}

static ssize_t hl_led2_write(struct file *f, const char __user *buf,
					size_t count, loff_t *ppos)
{
	struct hl_dbg_device_entry *entry = file_inode(f)->i_private;
	struct hl_device *hdev = entry->hdev;
	u32 value;
	ssize_t rc;

	rc = kstrtouint_from_user(buf, count, 10, &value);
	if (rc)
		return rc;

	value = value ? 1 : 0;

	hl_debugfs_led_set(hdev, 2, value);

	return count;
}

static ssize_t hl_device_read(struct file *f, char __user *buf,
					size_t count, loff_t *ppos)
{
	char tmp_buf[200];
	ssize_t rc;

	if (*ppos)
		return 0;

	sprintf(tmp_buf,
		"Valid values: disable, enable, suspend, resume, cpu_timeout\n");
	rc = simple_read_from_buffer(buf, strlen(tmp_buf) + 1, ppos, tmp_buf,
			strlen(tmp_buf) + 1);

	return rc;
}

static ssize_t hl_device_write(struct file *f, const char __user *buf,
				     size_t count, loff_t *ppos)
{
	struct hl_dbg_device_entry *entry = file_inode(f)->i_private;
	struct hl_device *hdev = entry->hdev;
	char data[30];

	/* don't allow partial writes */
	if (*ppos != 0)
		return 0;

	simple_write_to_buffer(data, 29, ppos, buf, count);

	if (strncmp("disable", data, strlen("disable")) == 0) {
		hdev->disabled = true;
	} else if (strncmp("enable", data, strlen("enable")) == 0) {
		hdev->disabled = false;
	} else if (strncmp("suspend", data, strlen("suspend")) == 0) {
		hdev->asic_funcs->suspend(hdev);
	} else if (strncmp("resume", data, strlen("resume")) == 0) {
		hdev->asic_funcs->resume(hdev);
	} else if (strncmp("cpu_timeout", data, strlen("cpu_timeout")) == 0) {
		hdev->device_cpu_disabled = true;
	} else {
		dev_err(hdev->dev,
			"Valid values: disable, enable, suspend, resume, cpu_timeout\n");
		count = -EINVAL;
	}

	return count;
}

static const struct file_operations hl_data32b_fops = {
	.owner = THIS_MODULE,
	.read = hl_data_read32,
	.write = hl_data_write32
};

static const struct file_operations hl_i2c_data_fops = {
	.owner = THIS_MODULE,
	.read = hl_i2c_data_read,
	.write = hl_i2c_data_write
};

static const struct file_operations hl_power_fops = {
	.owner = THIS_MODULE,
	.read = hl_get_power_state,
	.write = hl_set_power_state
};

static const struct file_operations hl_led0_fops = {
	.owner = THIS_MODULE,
	.write = hl_led0_write
};

static const struct file_operations hl_led1_fops = {
	.owner = THIS_MODULE,
	.write = hl_led1_write
};

static const struct file_operations hl_led2_fops = {
	.owner = THIS_MODULE,
	.write = hl_led2_write
};

static const struct file_operations hl_device_fops = {
	.owner = THIS_MODULE,
	.read = hl_device_read,
	.write = hl_device_write
};

static const struct hl_info_list hl_debugfs_list[] = {
	{"command_buffers", command_buffers_show, NULL},
	{"command_submission", command_submission_show, NULL},
	{"command_submission_jobs", command_submission_jobs_show, NULL},
	{"userptr", userptr_show, NULL},
	{"vm", vm_show, NULL},
	{"mmu", mmu_show, mmu_write},
};

static int hl_debugfs_open(struct inode *inode, struct file *file)
{
	struct hl_debugfs_entry *node = inode->i_private;

	return single_open(file, node->info_ent->show, node);
}

static ssize_t hl_debugfs_write(struct file *file, const char __user *buf,
		size_t count, loff_t *f_pos)
{
	struct hl_debugfs_entry *node = file->f_inode->i_private;

	if (node->info_ent->write)
		return node->info_ent->write(file, buf, count, f_pos);
	else
		return -EINVAL;

}

static const struct file_operations hl_debugfs_fops = {
	.owner = THIS_MODULE,
	.open = hl_debugfs_open,
	.read = seq_read,
	.write = hl_debugfs_write,
	.llseek = seq_lseek,
	.release = single_release,
};

void hl_debugfs_add_device(struct hl_device *hdev)
{
	struct hl_dbg_device_entry *dev_entry = &hdev->hl_debugfs;
	int count = ARRAY_SIZE(hl_debugfs_list);
	struct hl_debugfs_entry *entry;
	struct dentry *ent;
	int i;

	dev_entry->hdev = hdev;
	dev_entry->entry_arr = kmalloc_array(count,
					sizeof(struct hl_debugfs_entry),
					GFP_KERNEL);
	if (!dev_entry->entry_arr)
		return;

	INIT_LIST_HEAD(&dev_entry->file_list);
	INIT_LIST_HEAD(&dev_entry->cb_list);
	INIT_LIST_HEAD(&dev_entry->cs_list);
	INIT_LIST_HEAD(&dev_entry->cs_job_list);
	INIT_LIST_HEAD(&dev_entry->userptr_list);
	INIT_LIST_HEAD(&dev_entry->ctx_mem_hash_list);
	mutex_init(&dev_entry->file_mutex);
	spin_lock_init(&dev_entry->cb_spinlock);
	spin_lock_init(&dev_entry->cs_spinlock);
	spin_lock_init(&dev_entry->cs_job_spinlock);
	spin_lock_init(&dev_entry->userptr_spinlock);
	spin_lock_init(&dev_entry->ctx_mem_hash_spinlock);

	dev_entry->root = debugfs_create_dir(dev_name(hdev->dev),
						hl_debug_root);

	debugfs_create_x64("addr",
				0644,
				dev_entry->root,
				&dev_entry->addr);

	debugfs_create_file("data32",
				0644,
				dev_entry->root,
				dev_entry,
				&hl_data32b_fops);

	debugfs_create_file("set_power_state",
				0200,
				dev_entry->root,
				dev_entry,
				&hl_power_fops);

	debugfs_create_u8("i2c_bus",
				0644,
				dev_entry->root,
				&dev_entry->i2c_bus);

	debugfs_create_u8("i2c_addr",
				0644,
				dev_entry->root,
				&dev_entry->i2c_addr);

	debugfs_create_u8("i2c_reg",
				0644,
				dev_entry->root,
				&dev_entry->i2c_reg);

	debugfs_create_file("i2c_data",
				0644,
				dev_entry->root,
				dev_entry,
				&hl_i2c_data_fops);

	debugfs_create_file("led0",
				0200,
				dev_entry->root,
				dev_entry,
				&hl_led0_fops);

	debugfs_create_file("led1",
				0200,
				dev_entry->root,
				dev_entry,
				&hl_led1_fops);

	debugfs_create_file("led2",
				0200,
				dev_entry->root,
				dev_entry,
				&hl_led2_fops);

	debugfs_create_file("device",
				0200,
				dev_entry->root,
				dev_entry,
				&hl_device_fops);

	for (i = 0, entry = dev_entry->entry_arr ; i < count ; i++, entry++) {

		ent = debugfs_create_file(hl_debugfs_list[i].name,
					0444,
					dev_entry->root,
					entry,
					&hl_debugfs_fops);
		entry->dent = ent;
		entry->info_ent = &hl_debugfs_list[i];
		entry->dev_entry = dev_entry;
	}
}

void hl_debugfs_remove_device(struct hl_device *hdev)
{
	struct hl_dbg_device_entry *entry = &hdev->hl_debugfs;

	debugfs_remove_recursive(entry->root);

	mutex_destroy(&entry->file_mutex);
	kfree(entry->entry_arr);
}

void hl_debugfs_add_file(struct hl_fpriv *hpriv)
{
	struct hl_dbg_device_entry *dev_entry = &hpriv->hdev->hl_debugfs;

	mutex_lock(&dev_entry->file_mutex);
	list_add(&hpriv->debugfs_list, &dev_entry->file_list);
	mutex_unlock(&dev_entry->file_mutex);
}

void hl_debugfs_remove_file(struct hl_fpriv *hpriv)
{
	struct hl_dbg_device_entry *dev_entry = &hpriv->hdev->hl_debugfs;

	mutex_lock(&dev_entry->file_mutex);
	list_del(&hpriv->debugfs_list);
	mutex_unlock(&dev_entry->file_mutex);
}

void hl_debugfs_add_cb(struct hl_cb *cb)
{
	struct hl_dbg_device_entry *dev_entry = &cb->hdev->hl_debugfs;

	spin_lock(&dev_entry->cb_spinlock);
	list_add(&cb->debugfs_list, &dev_entry->cb_list);
	spin_unlock(&dev_entry->cb_spinlock);
}

void hl_debugfs_remove_cb(struct hl_cb *cb)
{
	struct hl_dbg_device_entry *dev_entry = &cb->hdev->hl_debugfs;

	spin_lock(&dev_entry->cb_spinlock);
	list_del(&cb->debugfs_list);
	spin_unlock(&dev_entry->cb_spinlock);
}

void hl_debugfs_add_cs(struct hl_cs *cs)
{
	struct hl_dbg_device_entry *dev_entry = &cs->ctx->hdev->hl_debugfs;

	spin_lock(&dev_entry->cs_spinlock);
	list_add(&cs->debugfs_list, &dev_entry->cs_list);
	spin_unlock(&dev_entry->cs_spinlock);
}

void hl_debugfs_remove_cs(struct hl_cs *cs)
{
	struct hl_dbg_device_entry *dev_entry = &cs->ctx->hdev->hl_debugfs;

	spin_lock(&dev_entry->cs_spinlock);
	list_del(&cs->debugfs_list);
	spin_unlock(&dev_entry->cs_spinlock);
}

void hl_debugfs_add_job(struct hl_device *hdev, struct hl_cs_job *job)
{
	struct hl_dbg_device_entry *dev_entry = &hdev->hl_debugfs;

	spin_lock(&dev_entry->cs_job_spinlock);
	list_add(&job->debugfs_list, &dev_entry->cs_job_list);
	spin_unlock(&dev_entry->cs_job_spinlock);
}

void hl_debugfs_remove_job(struct hl_device *hdev, struct hl_cs_job *job)
{
	struct hl_dbg_device_entry *dev_entry = &hdev->hl_debugfs;

	spin_lock(&dev_entry->cs_job_spinlock);
	list_del(&job->debugfs_list);
	spin_unlock(&dev_entry->cs_job_spinlock);
}

void hl_debugfs_add_userptr(struct hl_device *hdev, struct hl_userptr *userptr)
{
	struct hl_dbg_device_entry *dev_entry = &hdev->hl_debugfs;

	spin_lock(&dev_entry->userptr_spinlock);
	list_add(&userptr->debugfs_list, &dev_entry->userptr_list);
	spin_unlock(&dev_entry->userptr_spinlock);
}

void hl_debugfs_remove_userptr(struct hl_device *hdev,
				struct hl_userptr *userptr)
{
	struct hl_dbg_device_entry *dev_entry = &hdev->hl_debugfs;

	spin_lock(&dev_entry->userptr_spinlock);
	list_del(&userptr->debugfs_list);
	spin_unlock(&dev_entry->userptr_spinlock);
}

void hl_debugfs_add_ctx_mem_hash(struct hl_device *hdev, struct hl_ctx *ctx)
{
	struct hl_dbg_device_entry *dev_entry = &hdev->hl_debugfs;

	spin_lock(&dev_entry->ctx_mem_hash_spinlock);
	list_add(&ctx->debugfs_list, &dev_entry->ctx_mem_hash_list);
	spin_unlock(&dev_entry->ctx_mem_hash_spinlock);
}

void hl_debugfs_remove_ctx_mem_hash(struct hl_device *hdev, struct hl_ctx *ctx)
{
	struct hl_dbg_device_entry *dev_entry = &hdev->hl_debugfs;

	spin_lock(&dev_entry->ctx_mem_hash_spinlock);
	list_del(&ctx->debugfs_list);
	spin_unlock(&dev_entry->ctx_mem_hash_spinlock);
}

void __init hl_debugfs_init(void)
{
	hl_debug_root = debugfs_create_dir("habanalabs", NULL);
}

void hl_debugfs_fini(void)
{
	debugfs_remove_recursive(hl_debug_root);
}
