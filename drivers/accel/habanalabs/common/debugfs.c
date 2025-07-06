// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2021 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "habanalabs.h"
#include "../include/hw_ip/mmu/mmu_general.h"

#include <linux/pci.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/iommu.h>

#define MMU_ADDR_BUF_SIZE	40
#define MMU_ASID_BUF_SIZE	10
#define MMU_KBUF_SIZE		(MMU_ADDR_BUF_SIZE + MMU_ASID_BUF_SIZE)
#define I2C_MAX_TRANSACTION_LEN	8

static int hl_debugfs_i2c_read(struct hl_device *hdev, u8 i2c_bus, u8 i2c_addr,
				u8 i2c_reg, u8 i2c_len, u64 *val)
{
	struct cpucp_packet pkt;
	int rc;

	if (!hl_device_operational(hdev, NULL))
		return -EBUSY;

	if (i2c_len > I2C_MAX_TRANSACTION_LEN) {
		dev_err(hdev->dev, "I2C transaction length %u, exceeds maximum of %u\n",
				i2c_len, I2C_MAX_TRANSACTION_LEN);
		return -EINVAL;
	}

	memset(&pkt, 0, sizeof(pkt));

	pkt.ctl = cpu_to_le32(CPUCP_PACKET_I2C_RD <<
				CPUCP_PKT_CTL_OPCODE_SHIFT);
	pkt.i2c_bus = i2c_bus;
	pkt.i2c_addr = i2c_addr;
	pkt.i2c_reg = i2c_reg;
	pkt.i2c_len = i2c_len;

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt), 0, val);
	if (rc && rc != -EAGAIN)
		dev_err(hdev->dev, "Failed to read from I2C, error %d\n", rc);

	return rc;
}

static int hl_debugfs_i2c_write(struct hl_device *hdev, u8 i2c_bus, u8 i2c_addr,
				u8 i2c_reg, u8 i2c_len, u64 val)
{
	struct cpucp_packet pkt;
	int rc;

	if (!hl_device_operational(hdev, NULL))
		return -EBUSY;

	if (i2c_len > I2C_MAX_TRANSACTION_LEN) {
		dev_err(hdev->dev, "I2C transaction length %u, exceeds maximum of %u\n",
				i2c_len, I2C_MAX_TRANSACTION_LEN);
		return -EINVAL;
	}

	memset(&pkt, 0, sizeof(pkt));

	pkt.ctl = cpu_to_le32(CPUCP_PACKET_I2C_WR <<
				CPUCP_PKT_CTL_OPCODE_SHIFT);
	pkt.i2c_bus = i2c_bus;
	pkt.i2c_addr = i2c_addr;
	pkt.i2c_reg = i2c_reg;
	pkt.i2c_len = i2c_len;
	pkt.value = cpu_to_le64(val);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt), 0, NULL);
	if (rc && rc != -EAGAIN)
		dev_err(hdev->dev, "Failed to write to I2C, error %d\n", rc);

	return rc;
}

static void hl_debugfs_led_set(struct hl_device *hdev, u8 led, u8 state)
{
	struct cpucp_packet pkt;
	int rc;

	if (!hl_device_operational(hdev, NULL))
		return;

	memset(&pkt, 0, sizeof(pkt));

	pkt.ctl = cpu_to_le32(CPUCP_PACKET_LED_SET <<
				CPUCP_PKT_CTL_OPCODE_SHIFT);
	pkt.led_index = cpu_to_le32(led);
	pkt.value = cpu_to_le64(state);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt), 0, NULL);
	if (rc && rc != -EAGAIN)
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
			"   %03llu        %d    0x%08x      %d          %d          %d\n",
			cb->buf->handle, cb->ctx->asid, cb->size,
			kref_read(&cb->buf->refcount),
			atomic_read(&cb->buf->mmap), atomic_read(&cb->cs_cnt));
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
			seq_puts(s, " CS ID   CS TYPE   CTX ASID   CS RefCnt   Submitted    Completed\n");
			seq_puts(s, "----------------------------------------------------------------\n");
		}
		seq_printf(s,
			"   %llu        %d          %d          %d           %d            %d\n",
			cs->sequence, cs->type, cs->ctx->asid,
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
			seq_puts(s, " JOB ID   CS ID    CS TYPE    CTX ASID   JOB RefCnt   H/W Queue\n");
			seq_puts(s, "---------------------------------------------------------------\n");
		}
		if (job->cs)
			seq_printf(s,
				"   %02d      %llu        %d        %d          %d           %d\n",
				job->id, job->cs->sequence, job->cs->type,
				job->cs->ctx->asid, kref_read(&job->refcount),
				job->hw_queue_id);
		else
			seq_printf(s,
				"   %02d      0        0        %d          %d           %d\n",
				job->id, HL_KERNEL_ASID_ID,
				kref_read(&job->refcount), job->hw_queue_id);
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
			seq_puts(s, " pid      user virtual address     size             dma dir\n");
			seq_puts(s, "----------------------------------------------------------\n");
		}
		seq_printf(s, " %-7d  0x%-14llx      %-10llu    %-30s\n",
				userptr->pid, userptr->addr, userptr->size,
				dma_dir[userptr->dir]);
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
	struct hl_vm_hw_block_list_node *lnode;
	struct hl_ctx *ctx;
	struct hl_vm *vm;
	struct hl_vm_hash_node *hnode;
	struct hl_userptr *userptr;
	struct hl_vm_phys_pg_pack *phys_pg_pack = NULL;
	struct hl_va_range *va_range;
	struct hl_vm_va_block *va_block;
	enum vm_type *vm_type;
	bool once = true;
	u64 j;
	int i;

	mutex_lock(&dev_entry->ctx_mem_hash_mutex);

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
					"    0x%-14llx      %-10llu\n",
					hnode->vaddr, userptr->size);
			} else {
				phys_pg_pack = hnode->ptr;
				seq_printf(s,
					"    0x%-14llx      %-10llu       %-4u\n",
					hnode->vaddr, phys_pg_pack->total_size,
					phys_pg_pack->handle);
			}
		}
		mutex_unlock(&ctx->mem_hash_lock);

		if (ctx->asid != HL_KERNEL_ASID_ID &&
		    !list_empty(&ctx->hw_block_mem_list)) {
			seq_puts(s, "\nhw_block mappings:\n\n");
			seq_puts(s,
				"    virtual address    block size    mapped size    HW block id\n");
			seq_puts(s,
				"---------------------------------------------------------------\n");
			mutex_lock(&ctx->hw_block_list_lock);
			list_for_each_entry(lnode, &ctx->hw_block_mem_list, node) {
				seq_printf(s,
					"    0x%-14lx   %-6u        %-6u             %-9u\n",
					lnode->vaddr, lnode->block_size, lnode->mapped_size,
					lnode->id);
			}
			mutex_unlock(&ctx->hw_block_list_lock);
		}

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
			for (j = 0 ; j < phys_pg_pack->npages ; j++) {
				seq_printf(s, "    0x%-14llx\n",
						phys_pg_pack->pages[j]);
			}
		}
		spin_unlock(&vm->idr_lock);

	}

	mutex_unlock(&dev_entry->ctx_mem_hash_mutex);

	ctx = hl_get_compute_ctx(dev_entry->hdev);
	if (ctx) {
		seq_puts(s, "\nVA ranges:\n\n");
		for (i = HL_VA_RANGE_TYPE_HOST ; i < HL_VA_RANGE_TYPE_MAX ; ++i) {
			va_range = ctx->va_range[i];
			seq_printf(s, "   va_range %d\n", i);
			seq_puts(s, "---------------------\n");
			mutex_lock(&va_range->lock);
			list_for_each_entry(va_block, &va_range->list, node) {
				seq_printf(s, "%#16llx - %#16llx (%#llx)\n",
					   va_block->start, va_block->end,
					   va_block->size);
			}
			mutex_unlock(&va_range->lock);
			seq_puts(s, "\n");
		}
		hl_ctx_put(ctx);
	}

	if (!once)
		seq_puts(s, "\n");

	return 0;
}

static int userptr_lookup_show(struct seq_file *s, void *data)
{
	struct hl_debugfs_entry *entry = s->private;
	struct hl_dbg_device_entry *dev_entry = entry->dev_entry;
	struct scatterlist *sg;
	struct hl_userptr *userptr;
	bool first = true;
	u64 total_npages, npages, sg_start, sg_end;
	dma_addr_t dma_addr;
	int i;

	spin_lock(&dev_entry->userptr_spinlock);

	list_for_each_entry(userptr, &dev_entry->userptr_list, debugfs_list) {
		if (dev_entry->userptr_lookup >= userptr->addr &&
		dev_entry->userptr_lookup < userptr->addr + userptr->size) {
			total_npages = 0;
			for_each_sgtable_dma_sg(userptr->sgt, sg, i) {
				npages = hl_get_sg_info(sg, &dma_addr);
				sg_start = userptr->addr +
					total_npages * PAGE_SIZE;
				sg_end = userptr->addr +
					(total_npages + npages) * PAGE_SIZE;

				if (dev_entry->userptr_lookup >= sg_start &&
				    dev_entry->userptr_lookup < sg_end) {
					dma_addr += (dev_entry->userptr_lookup -
							sg_start);
					if (first) {
						first = false;
						seq_puts(s, "\n");
						seq_puts(s, " user virtual address         dma address       pid        region start     region size\n");
						seq_puts(s, "---------------------------------------------------------------------------------------\n");
					}
					seq_printf(s, " 0x%-18llx  0x%-16llx  %-8u  0x%-16llx %-12llu\n",
						dev_entry->userptr_lookup,
						(u64)dma_addr, userptr->pid,
						userptr->addr, userptr->size);
				}
				total_npages += npages;
			}
		}
	}

	spin_unlock(&dev_entry->userptr_spinlock);

	if (!first)
		seq_puts(s, "\n");

	return 0;
}

static ssize_t userptr_lookup_write(struct file *file, const char __user *buf,
		size_t count, loff_t *f_pos)
{
	struct seq_file *s = file->private_data;
	struct hl_debugfs_entry *entry = s->private;
	struct hl_dbg_device_entry *dev_entry = entry->dev_entry;
	ssize_t rc;
	u64 value;

	rc = kstrtoull_from_user(buf, count, 16, &value);
	if (rc)
		return rc;

	dev_entry->userptr_lookup = value;

	return count;
}

static int mmu_show(struct seq_file *s, void *data)
{
	struct hl_debugfs_entry *entry = s->private;
	struct hl_dbg_device_entry *dev_entry = entry->dev_entry;
	struct hl_device *hdev = dev_entry->hdev;
	struct hl_ctx *ctx;
	struct hl_mmu_hop_info hops_info = {0};
	u64 virt_addr = dev_entry->mmu_addr, phys_addr;
	int i;

	if (dev_entry->mmu_asid == HL_KERNEL_ASID_ID)
		ctx = hdev->kernel_ctx;
	else
		ctx = hl_get_compute_ctx(hdev);

	if (!ctx) {
		dev_err(hdev->dev, "no ctx available\n");
		return 0;
	}

	if (hl_mmu_get_tlb_info(ctx, virt_addr, &hops_info)) {
		dev_err(hdev->dev, "virt addr 0x%llx is not mapped to phys addr\n",
				virt_addr);
		goto put_ctx;
	}

	hl_mmu_va_to_pa(ctx, virt_addr, &phys_addr);

	if (hops_info.scrambled_vaddr &&
		(dev_entry->mmu_addr != hops_info.scrambled_vaddr))
		seq_printf(s,
			"asid: %u, virt_addr: 0x%llx, scrambled virt_addr: 0x%llx,\nphys_addr: 0x%llx, scrambled_phys_addr: 0x%llx\n",
			dev_entry->mmu_asid, dev_entry->mmu_addr,
			hops_info.scrambled_vaddr,
			hops_info.unscrambled_paddr, phys_addr);
	else
		seq_printf(s,
			"asid: %u, virt_addr: 0x%llx, phys_addr: 0x%llx\n",
			dev_entry->mmu_asid, dev_entry->mmu_addr, phys_addr);

	for (i = 0 ; i < hops_info.used_hops ; i++) {
		seq_printf(s, "hop%d_addr: 0x%llx\n",
				i, hops_info.hop_info[i].hop_addr);
		seq_printf(s, "hop%d_pte_addr: 0x%llx\n",
				i, hops_info.hop_info[i].hop_pte_addr);
		seq_printf(s, "hop%d_pte: 0x%llx\n",
				i, hops_info.hop_info[i].hop_pte_val);
	}

put_ctx:
	if (dev_entry->mmu_asid != HL_KERNEL_ASID_ID)
		hl_ctx_put(ctx);

	return 0;
}

static ssize_t mmu_asid_va_write(struct file *file, const char __user *buf,
		size_t count, loff_t *f_pos)
{
	struct seq_file *s = file->private_data;
	struct hl_debugfs_entry *entry = s->private;
	struct hl_dbg_device_entry *dev_entry = entry->dev_entry;
	struct hl_device *hdev = dev_entry->hdev;
	char kbuf[MMU_KBUF_SIZE] = {0};
	char *c;
	ssize_t rc;

	if (count > sizeof(kbuf) - 1)
		goto err;
	if (copy_from_user(kbuf, buf, count))
		goto err;
	kbuf[count] = 0;

	c = strchr(kbuf, ' ');
	if (!c)
		goto err;
	*c = '\0';

	rc = kstrtouint(kbuf, 10, &dev_entry->mmu_asid);
	if (rc)
		goto err;

	if (strncmp(c+1, "0x", 2))
		goto err;
	rc = kstrtoull(c+3, 16, &dev_entry->mmu_addr);
	if (rc)
		goto err;

	return count;

err:
	dev_err(hdev->dev, "usage: echo <asid> <0xaddr> > mmu\n");

	return -EINVAL;
}

static int mmu_ack_error(struct seq_file *s, void *data)
{
	struct hl_debugfs_entry *entry = s->private;
	struct hl_dbg_device_entry *dev_entry = entry->dev_entry;
	struct hl_device *hdev = dev_entry->hdev;
	int rc;

	if (!dev_entry->mmu_cap_mask) {
		dev_err(hdev->dev, "mmu_cap_mask is not set\n");
		goto err;
	}

	rc = hdev->asic_funcs->ack_mmu_errors(hdev, dev_entry->mmu_cap_mask);
	if (rc)
		goto err;

	return 0;
err:
	return -EINVAL;
}

static ssize_t mmu_ack_error_value_write(struct file *file,
		const char __user *buf,
		size_t count, loff_t *f_pos)
{
	struct seq_file *s = file->private_data;
	struct hl_debugfs_entry *entry = s->private;
	struct hl_dbg_device_entry *dev_entry = entry->dev_entry;
	struct hl_device *hdev = dev_entry->hdev;
	char kbuf[MMU_KBUF_SIZE] = {0};
	ssize_t rc;

	if (count > sizeof(kbuf) - 1)
		goto err;

	if (copy_from_user(kbuf, buf, count))
		goto err;

	kbuf[count] = 0;

	if (strncmp(kbuf, "0x", 2))
		goto err;

	rc = kstrtoull(kbuf, 16, &dev_entry->mmu_cap_mask);
	if (rc)
		goto err;

	return count;
err:
	dev_err(hdev->dev, "usage: echo <0xmmu_cap_mask > > mmu_error\n");

	return -EINVAL;
}

static int engines_show(struct seq_file *s, void *data)
{
	struct hl_debugfs_entry *entry = s->private;
	struct hl_dbg_device_entry *dev_entry = entry->dev_entry;
	struct hl_device *hdev = dev_entry->hdev;
	struct engines_data eng_data;

	if (hdev->reset_info.in_reset) {
		dev_warn_ratelimited(hdev->dev,
				"Can't check device idle during reset\n");
		return 0;
	}

	eng_data.actual_size = 0;
	eng_data.allocated_buf_size = HL_ENGINES_DATA_MAX_SIZE;
	eng_data.buf = vmalloc(eng_data.allocated_buf_size);
	if (!eng_data.buf)
		return -ENOMEM;

	hdev->asic_funcs->is_device_idle(hdev, NULL, 0, &eng_data);

	if (eng_data.actual_size > eng_data.allocated_buf_size) {
		dev_err(hdev->dev,
				"Engines data size (%d Bytes) is bigger than allocated size (%u Bytes)\n",
				eng_data.actual_size, eng_data.allocated_buf_size);
		vfree(eng_data.buf);
		return -ENOMEM;
	}

	seq_write(s, eng_data.buf, eng_data.actual_size);

	vfree(eng_data.buf);

	return 0;
}

static ssize_t hl_memory_scrub(struct file *f, const char __user *buf,
					size_t count, loff_t *ppos)
{
	struct hl_dbg_device_entry *entry = file_inode(f)->i_private;
	struct hl_device *hdev = entry->hdev;
	u64 val = hdev->memory_scrub_val;
	int rc;

	if (!hl_device_operational(hdev, NULL)) {
		dev_warn_ratelimited(hdev->dev, "Can't scrub memory, device is not operational\n");
		return -EIO;
	}

	mutex_lock(&hdev->fpriv_list_lock);
	if (hdev->is_compute_ctx_active) {
		mutex_unlock(&hdev->fpriv_list_lock);
		dev_err(hdev->dev, "can't scrub dram, context exist\n");
		return -EBUSY;
	}
	hdev->is_in_dram_scrub = true;
	mutex_unlock(&hdev->fpriv_list_lock);

	rc = hdev->asic_funcs->scrub_device_dram(hdev, val);

	mutex_lock(&hdev->fpriv_list_lock);
	hdev->is_in_dram_scrub = false;
	mutex_unlock(&hdev->fpriv_list_lock);

	if (rc)
		return rc;
	return count;
}

static bool hl_is_device_va(struct hl_device *hdev, u64 addr)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;

	if (prop->dram_supports_virtual_memory &&
		(addr >= prop->dmmu.start_addr && addr < prop->dmmu.end_addr))
		return true;

	if (addr >= prop->pmmu.start_addr &&
		addr < prop->pmmu.end_addr)
		return true;

	if (addr >= prop->pmmu_huge.start_addr &&
		addr < prop->pmmu_huge.end_addr)
		return true;

	return false;
}

static bool hl_is_device_internal_memory_va(struct hl_device *hdev, u64 addr,
						u32 size)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u64 dram_start_addr, dram_end_addr;

	if (prop->dram_supports_virtual_memory) {
		dram_start_addr = prop->dmmu.start_addr;
		dram_end_addr = prop->dmmu.end_addr;
	} else {
		dram_start_addr = prop->dram_base_address;
		dram_end_addr = prop->dram_end_address;
	}

	if (hl_mem_area_inside_range(addr, size, dram_start_addr,
					dram_end_addr))
		return true;

	if (hl_mem_area_inside_range(addr, size, prop->sram_base_address,
					prop->sram_end_address))
		return true;

	return false;
}

static int device_va_to_pa(struct hl_device *hdev, u64 virt_addr, u32 size,
			u64 *phys_addr)
{
	struct hl_vm_phys_pg_pack *phys_pg_pack;
	struct hl_ctx *ctx;
	struct hl_vm_hash_node *hnode;
	u64 end_address, range_size;
	struct hl_userptr *userptr;
	enum vm_type *vm_type;
	bool valid = false;
	int i, rc = 0;

	ctx = hl_get_compute_ctx(hdev);

	if (!ctx) {
		dev_err(hdev->dev, "no ctx available\n");
		return -EINVAL;
	}

	/* Verify address is mapped */
	mutex_lock(&ctx->mem_hash_lock);
	hash_for_each(ctx->mem_hash, i, hnode, node) {
		vm_type = hnode->ptr;

		if (*vm_type == VM_TYPE_USERPTR) {
			userptr = hnode->ptr;
			range_size = userptr->size;
		} else {
			phys_pg_pack = hnode->ptr;
			range_size = phys_pg_pack->total_size;
		}

		end_address = virt_addr + size;
		if ((virt_addr >= hnode->vaddr) &&
				(end_address <= hnode->vaddr + range_size)) {
			valid = true;
			break;
		}
	}
	mutex_unlock(&ctx->mem_hash_lock);

	if (!valid) {
		dev_err(hdev->dev,
			"virt addr 0x%llx is not mapped\n",
			virt_addr);
		rc = -EINVAL;
		goto put_ctx;
	}

	rc = hl_mmu_va_to_pa(ctx, virt_addr, phys_addr);
	if (rc) {
		dev_err(hdev->dev,
			"virt addr 0x%llx is not mapped to phys addr\n",
			virt_addr);
		rc = -EINVAL;
	}

put_ctx:
	hl_ctx_put(ctx);

	return rc;
}

static int hl_access_dev_mem_by_region(struct hl_device *hdev, u64 addr,
		u64 *val, enum debugfs_access_type acc_type, bool *found)
{
	size_t acc_size = (acc_type == DEBUGFS_READ64 || acc_type == DEBUGFS_WRITE64) ?
		sizeof(u64) : sizeof(u32);
	struct pci_mem_region *mem_reg;
	int i;

	for (i = 0; i < PCI_REGION_NUMBER; i++) {
		mem_reg = &hdev->pci_mem_region[i];
		if (!mem_reg->used)
			continue;
		if (addr >= mem_reg->region_base &&
			addr <= mem_reg->region_base + mem_reg->region_size - acc_size) {
			*found = true;
			return hdev->asic_funcs->access_dev_mem(hdev, i, addr, val, acc_type);
		}
	}
	return 0;
}

static void hl_access_host_mem(struct hl_device *hdev, u64 addr, u64 *val,
		enum debugfs_access_type acc_type)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u64 offset = prop->device_dma_offset_for_host_access;

	switch (acc_type) {
	case DEBUGFS_READ32:
		*val = *(u32 *) phys_to_virt(addr - offset);
		break;
	case DEBUGFS_WRITE32:
		*(u32 *) phys_to_virt(addr - offset) = *val;
		break;
	case DEBUGFS_READ64:
		*val = *(u64 *) phys_to_virt(addr - offset);
		break;
	case DEBUGFS_WRITE64:
		*(u64 *) phys_to_virt(addr - offset) = *val;
		break;
	default:
		dev_err(hdev->dev, "hostmem access-type %d id not supported\n", acc_type);
		break;
	}
}

static int hl_access_mem(struct hl_device *hdev, u64 addr, u64 *val,
				enum debugfs_access_type acc_type)
{
	size_t acc_size = (acc_type == DEBUGFS_READ64 || acc_type == DEBUGFS_WRITE64) ?
		sizeof(u64) : sizeof(u32);
	u64 host_start = hdev->asic_prop.host_base_address;
	u64 host_end = hdev->asic_prop.host_end_address;
	bool user_address, found = false;
	int rc;

	user_address = hl_is_device_va(hdev, addr);
	if (user_address) {
		rc = device_va_to_pa(hdev, addr, acc_size, &addr);
		if (rc)
			return rc;
	}

	rc = hl_access_dev_mem_by_region(hdev, addr, val, acc_type, &found);
	if (rc) {
		dev_err(hdev->dev,
			"Failed reading addr %#llx from dev mem (%d)\n",
			addr, rc);
		return rc;
	}

	if (found)
		return 0;

	if (!user_address || device_iommu_mapped(&hdev->pdev->dev)) {
		rc = -EINVAL;
		goto err;
	}

	if (addr >= host_start && addr <= host_end - acc_size) {
		hl_access_host_mem(hdev, addr, val, acc_type);
	} else {
		rc = -EINVAL;
		goto err;
	}

	return 0;
err:
	dev_err(hdev->dev, "invalid addr %#llx\n", addr);
	return rc;
}

static ssize_t hl_data_read32(struct file *f, char __user *buf,
					size_t count, loff_t *ppos)
{
	struct hl_dbg_device_entry *entry = file_inode(f)->i_private;
	struct hl_device *hdev = entry->hdev;
	u64 value64, addr = entry->addr;
	char tmp_buf[32];
	ssize_t rc;
	u32 val;

	if (hdev->reset_info.in_reset) {
		dev_warn_ratelimited(hdev->dev, "Can't read during reset\n");
		return 0;
	}

	if (*ppos)
		return 0;

	rc = hl_access_mem(hdev, addr, &value64, DEBUGFS_READ32);
	if (rc)
		return rc;

	val = value64; /* downcast back to 32 */

	sprintf(tmp_buf, "0x%08x\n", val);
	return simple_read_from_buffer(buf, count, ppos, tmp_buf,
			strlen(tmp_buf));
}

static ssize_t hl_data_write32(struct file *f, const char __user *buf,
					size_t count, loff_t *ppos)
{
	struct hl_dbg_device_entry *entry = file_inode(f)->i_private;
	struct hl_device *hdev = entry->hdev;
	u64 value64, addr = entry->addr;
	u32 value;
	ssize_t rc;

	if (hdev->reset_info.in_reset) {
		dev_warn_ratelimited(hdev->dev, "Can't write during reset\n");
		return 0;
	}

	rc = kstrtouint_from_user(buf, count, 16, &value);
	if (rc)
		return rc;

	value64 = value;
	rc = hl_access_mem(hdev, addr, &value64, DEBUGFS_WRITE32);
	if (rc)
		return rc;

	return count;
}

static ssize_t hl_data_read64(struct file *f, char __user *buf,
					size_t count, loff_t *ppos)
{
	struct hl_dbg_device_entry *entry = file_inode(f)->i_private;
	struct hl_device *hdev = entry->hdev;
	u64 addr = entry->addr;
	char tmp_buf[32];
	ssize_t rc;
	u64 val;

	if (hdev->reset_info.in_reset) {
		dev_warn_ratelimited(hdev->dev, "Can't read during reset\n");
		return 0;
	}

	if (*ppos)
		return 0;

	rc = hl_access_mem(hdev, addr, &val, DEBUGFS_READ64);
	if (rc)
		return rc;

	sprintf(tmp_buf, "0x%016llx\n", val);
	return simple_read_from_buffer(buf, count, ppos, tmp_buf,
			strlen(tmp_buf));
}

static ssize_t hl_data_write64(struct file *f, const char __user *buf,
					size_t count, loff_t *ppos)
{
	struct hl_dbg_device_entry *entry = file_inode(f)->i_private;
	struct hl_device *hdev = entry->hdev;
	u64 addr = entry->addr;
	u64 value;
	ssize_t rc;

	if (hdev->reset_info.in_reset) {
		dev_warn_ratelimited(hdev->dev, "Can't write during reset\n");
		return 0;
	}

	rc = kstrtoull_from_user(buf, count, 16, &value);
	if (rc)
		return rc;

	rc = hl_access_mem(hdev, addr, &value, DEBUGFS_WRITE64);
	if (rc)
		return rc;

	return count;
}

static ssize_t hl_dma_size_write(struct file *f, const char __user *buf,
					size_t count, loff_t *ppos)
{
	struct hl_dbg_device_entry *entry = file_inode(f)->i_private;
	struct hl_device *hdev = entry->hdev;
	u64 addr = entry->addr;
	ssize_t rc;
	u32 size;

	if (hdev->reset_info.in_reset) {
		dev_warn_ratelimited(hdev->dev, "Can't DMA during reset\n");
		return 0;
	}
	rc = kstrtouint_from_user(buf, count, 16, &size);
	if (rc)
		return rc;

	if (!size) {
		dev_err(hdev->dev, "DMA read failed. size can't be 0\n");
		return -EINVAL;
	}

	if (size > SZ_128M) {
		dev_err(hdev->dev,
			"DMA read failed. size can't be larger than 128MB\n");
		return -EINVAL;
	}

	if (!hl_is_device_internal_memory_va(hdev, addr, size)) {
		dev_err(hdev->dev,
			"DMA read failed. Invalid 0x%010llx + 0x%08x\n",
			addr, size);
		return -EINVAL;
	}

	/* Free the previous allocation, if there was any */
	entry->data_dma_blob_desc.size = 0;
	vfree(entry->data_dma_blob_desc.data);

	entry->data_dma_blob_desc.data = vmalloc(size);
	if (!entry->data_dma_blob_desc.data)
		return -ENOMEM;

	rc = hdev->asic_funcs->debugfs_read_dma(hdev, addr, size,
						entry->data_dma_blob_desc.data);
	if (rc) {
		dev_err(hdev->dev, "Failed to DMA from 0x%010llx\n", addr);
		vfree(entry->data_dma_blob_desc.data);
		entry->data_dma_blob_desc.data = NULL;
		return -EIO;
	}

	entry->data_dma_blob_desc.size = size;

	return count;
}

static ssize_t hl_monitor_dump_trigger(struct file *f, const char __user *buf,
					size_t count, loff_t *ppos)
{
	struct hl_dbg_device_entry *entry = file_inode(f)->i_private;
	struct hl_device *hdev = entry->hdev;
	u32 size, trig;
	ssize_t rc;

	if (hdev->reset_info.in_reset) {
		dev_warn_ratelimited(hdev->dev, "Can't dump monitors during reset\n");
		return 0;
	}
	rc = kstrtouint_from_user(buf, count, 10, &trig);
	if (rc)
		return rc;

	if (trig != 1) {
		dev_err(hdev->dev, "Must write 1 to trigger monitor dump\n");
		return -EINVAL;
	}

	size = sizeof(struct cpucp_monitor_dump);

	/* Free the previous allocation, if there was any */
	entry->mon_dump_blob_desc.size = 0;
	vfree(entry->mon_dump_blob_desc.data);

	entry->mon_dump_blob_desc.data = vmalloc(size);
	if (!entry->mon_dump_blob_desc.data)
		return -ENOMEM;

	rc = hdev->asic_funcs->get_monitor_dump(hdev, entry->mon_dump_blob_desc.data);
	if (rc) {
		dev_err(hdev->dev, "Failed to dump monitors\n");
		vfree(entry->mon_dump_blob_desc.data);
		entry->mon_dump_blob_desc.data = NULL;
		return -EIO;
	}

	entry->mon_dump_blob_desc.size = size;

	return count;
}

static ssize_t hl_get_power_state(struct file *f, char __user *buf,
		size_t count, loff_t *ppos)
{
	struct hl_dbg_device_entry *entry = file_inode(f)->i_private;
	struct hl_device *hdev = entry->hdev;
	char tmp_buf[200];
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
	return simple_read_from_buffer(buf, count, ppos, tmp_buf,
			strlen(tmp_buf));
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
		if (rc < 0)
			return rc;
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
	u64 val;
	ssize_t rc;

	if (*ppos)
		return 0;

	rc = hl_debugfs_i2c_read(hdev, entry->i2c_bus, entry->i2c_addr,
			entry->i2c_reg, entry->i2c_len, &val);
	if (rc) {
		dev_err(hdev->dev,
			"Failed to read from I2C bus %d, addr %d, reg %d, len %d\n",
			entry->i2c_bus, entry->i2c_addr, entry->i2c_reg, entry->i2c_len);
		return rc;
	}

	sprintf(tmp_buf, "%#02llx\n", val);
	rc = simple_read_from_buffer(buf, count, ppos, tmp_buf,
			strlen(tmp_buf));

	return rc;
}

static ssize_t hl_i2c_data_write(struct file *f, const char __user *buf,
					size_t count, loff_t *ppos)
{
	struct hl_dbg_device_entry *entry = file_inode(f)->i_private;
	struct hl_device *hdev = entry->hdev;
	u64 value;
	ssize_t rc;

	rc = kstrtou64_from_user(buf, count, 16, &value);
	if (rc)
		return rc;

	rc = hl_debugfs_i2c_write(hdev, entry->i2c_bus, entry->i2c_addr,
			entry->i2c_reg, entry->i2c_len, value);
	if (rc) {
		dev_err(hdev->dev,
			"Failed to write %#02llx to I2C bus %d, addr %d, reg %d, len %d\n",
			value, entry->i2c_bus, entry->i2c_addr, entry->i2c_reg, entry->i2c_len);
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
	static const char *help =
		"Valid values: disable, enable, suspend, resume, cpu_timeout\n";
	return simple_read_from_buffer(buf, count, ppos, help, strlen(help));
}

static ssize_t hl_device_write(struct file *f, const char __user *buf,
				     size_t count, loff_t *ppos)
{
	struct hl_dbg_device_entry *entry = file_inode(f)->i_private;
	struct hl_device *hdev = entry->hdev;
	char data[30] = {0};

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

static ssize_t hl_clk_gate_read(struct file *f, char __user *buf,
					size_t count, loff_t *ppos)
{
	return 0;
}

static ssize_t hl_clk_gate_write(struct file *f, const char __user *buf,
				     size_t count, loff_t *ppos)
{
	return count;
}

static ssize_t hl_stop_on_err_read(struct file *f, char __user *buf,
					size_t count, loff_t *ppos)
{
	struct hl_dbg_device_entry *entry = file_inode(f)->i_private;
	struct hl_device *hdev = entry->hdev;
	char tmp_buf[200];
	ssize_t rc;

	if (!hdev->asic_prop.configurable_stop_on_err)
		return -EOPNOTSUPP;

	if (*ppos)
		return 0;

	sprintf(tmp_buf, "%d\n", hdev->stop_on_err);
	rc = simple_read_from_buffer(buf, strlen(tmp_buf) + 1, ppos, tmp_buf,
			strlen(tmp_buf) + 1);

	return rc;
}

static ssize_t hl_stop_on_err_write(struct file *f, const char __user *buf,
				     size_t count, loff_t *ppos)
{
	struct hl_dbg_device_entry *entry = file_inode(f)->i_private;
	struct hl_device *hdev = entry->hdev;
	u32 value;
	ssize_t rc;

	if (!hdev->asic_prop.configurable_stop_on_err)
		return -EOPNOTSUPP;

	if (hdev->reset_info.in_reset) {
		dev_warn_ratelimited(hdev->dev,
				"Can't change stop on error during reset\n");
		return 0;
	}

	rc = kstrtouint_from_user(buf, count, 10, &value);
	if (rc)
		return rc;

	hdev->stop_on_err = value ? 1 : 0;

	hl_device_reset(hdev, 0);

	return count;
}

static ssize_t hl_security_violations_read(struct file *f, char __user *buf,
					size_t count, loff_t *ppos)
{
	struct hl_dbg_device_entry *entry = file_inode(f)->i_private;
	struct hl_device *hdev = entry->hdev;

	hdev->asic_funcs->ack_protection_bits_errors(hdev);

	return 0;
}

static ssize_t hl_state_dump_read(struct file *f, char __user *buf,
					size_t count, loff_t *ppos)
{
	struct hl_dbg_device_entry *entry = file_inode(f)->i_private;
	ssize_t rc;

	down_read(&entry->state_dump_sem);
	if (!entry->state_dump[entry->state_dump_head])
		rc = 0;
	else
		rc = simple_read_from_buffer(
			buf, count, ppos,
			entry->state_dump[entry->state_dump_head],
			strlen(entry->state_dump[entry->state_dump_head]));
	up_read(&entry->state_dump_sem);

	return rc;
}

static ssize_t hl_state_dump_write(struct file *f, const char __user *buf,
					size_t count, loff_t *ppos)
{
	struct hl_dbg_device_entry *entry = file_inode(f)->i_private;
	struct hl_device *hdev = entry->hdev;
	ssize_t rc;
	u32 size;
	int i;

	rc = kstrtouint_from_user(buf, count, 10, &size);
	if (rc)
		return rc;

	if (size <= 0 || size >= ARRAY_SIZE(entry->state_dump)) {
		dev_err(hdev->dev, "Invalid number of dumps to skip\n");
		return -EINVAL;
	}

	if (entry->state_dump[entry->state_dump_head]) {
		down_write(&entry->state_dump_sem);
		for (i = 0; i < size; ++i) {
			vfree(entry->state_dump[entry->state_dump_head]);
			entry->state_dump[entry->state_dump_head] = NULL;
			if (entry->state_dump_head > 0)
				entry->state_dump_head--;
			else
				entry->state_dump_head =
					ARRAY_SIZE(entry->state_dump) - 1;
		}
		up_write(&entry->state_dump_sem);
	}

	return count;
}

static ssize_t hl_timeout_locked_read(struct file *f, char __user *buf,
					size_t count, loff_t *ppos)
{
	struct hl_dbg_device_entry *entry = file_inode(f)->i_private;
	struct hl_device *hdev = entry->hdev;
	char tmp_buf[200];
	ssize_t rc;

	if (*ppos)
		return 0;

	sprintf(tmp_buf, "%d\n",
		jiffies_to_msecs(hdev->timeout_jiffies) / 1000);
	rc = simple_read_from_buffer(buf, strlen(tmp_buf) + 1, ppos, tmp_buf,
			strlen(tmp_buf) + 1);

	return rc;
}

static ssize_t hl_timeout_locked_write(struct file *f, const char __user *buf,
				     size_t count, loff_t *ppos)
{
	struct hl_dbg_device_entry *entry = file_inode(f)->i_private;
	struct hl_device *hdev = entry->hdev;
	u32 value;
	ssize_t rc;

	rc = kstrtouint_from_user(buf, count, 10, &value);
	if (rc)
		return rc;

	if (value)
		hdev->timeout_jiffies = secs_to_jiffies(value);
	else
		hdev->timeout_jiffies = MAX_SCHEDULE_TIMEOUT;

	return count;
}

static ssize_t hl_check_razwi_happened(struct file *f, char __user *buf,
					size_t count, loff_t *ppos)
{
	struct hl_dbg_device_entry *entry = file_inode(f)->i_private;
	struct hl_device *hdev = entry->hdev;

	hdev->asic_funcs->check_if_razwi_happened(hdev);

	return 0;
}

static const struct file_operations hl_mem_scrub_fops = {
	.owner = THIS_MODULE,
	.write = hl_memory_scrub,
};

static const struct file_operations hl_data32b_fops = {
	.owner = THIS_MODULE,
	.read = hl_data_read32,
	.write = hl_data_write32
};

static const struct file_operations hl_data64b_fops = {
	.owner = THIS_MODULE,
	.read = hl_data_read64,
	.write = hl_data_write64
};

static const struct file_operations hl_dma_size_fops = {
	.owner = THIS_MODULE,
	.write = hl_dma_size_write
};

static const struct file_operations hl_monitor_dump_fops = {
	.owner = THIS_MODULE,
	.write = hl_monitor_dump_trigger
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

static const struct file_operations hl_clk_gate_fops = {
	.owner = THIS_MODULE,
	.read = hl_clk_gate_read,
	.write = hl_clk_gate_write
};

static const struct file_operations hl_stop_on_err_fops = {
	.owner = THIS_MODULE,
	.read = hl_stop_on_err_read,
	.write = hl_stop_on_err_write
};

static const struct file_operations hl_security_violations_fops = {
	.owner = THIS_MODULE,
	.read = hl_security_violations_read
};

static const struct file_operations hl_state_dump_fops = {
	.owner = THIS_MODULE,
	.read = hl_state_dump_read,
	.write = hl_state_dump_write
};

static const struct file_operations hl_timeout_locked_fops = {
	.owner = THIS_MODULE,
	.read = hl_timeout_locked_read,
	.write = hl_timeout_locked_write
};

static const struct file_operations hl_razwi_check_fops = {
	.owner = THIS_MODULE,
	.read = hl_check_razwi_happened
};

static const struct hl_info_list hl_debugfs_list[] = {
	{"command_buffers", command_buffers_show, NULL},
	{"command_submission", command_submission_show, NULL},
	{"command_submission_jobs", command_submission_jobs_show, NULL},
	{"userptr", userptr_show, NULL},
	{"vm", vm_show, NULL},
	{"userptr_lookup", userptr_lookup_show, userptr_lookup_write},
	{"mmu", mmu_show, mmu_asid_va_write},
	{"mmu_error", mmu_ack_error, mmu_ack_error_value_write},
	{"engines", engines_show, NULL},
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

static void add_secured_nodes(struct hl_dbg_device_entry *dev_entry, struct dentry *root)
{
	debugfs_create_u8("i2c_bus",
				0644,
				root,
				&dev_entry->i2c_bus);

	debugfs_create_u8("i2c_addr",
				0644,
				root,
				&dev_entry->i2c_addr);

	debugfs_create_u8("i2c_reg",
				0644,
				root,
				&dev_entry->i2c_reg);

	debugfs_create_u8("i2c_len",
				0644,
				root,
				&dev_entry->i2c_len);

	debugfs_create_file("i2c_data",
				0644,
				root,
				dev_entry,
				&hl_i2c_data_fops);

	debugfs_create_file("led0",
				0200,
				root,
				dev_entry,
				&hl_led0_fops);

	debugfs_create_file("led1",
				0200,
				root,
				dev_entry,
				&hl_led1_fops);

	debugfs_create_file("led2",
				0200,
				root,
				dev_entry,
				&hl_led2_fops);
}

static void add_files_to_device(struct hl_device *hdev, struct hl_dbg_device_entry *dev_entry,
				struct dentry *root)
{
	int count = ARRAY_SIZE(hl_debugfs_list);
	struct hl_debugfs_entry *entry;
	int i;

	debugfs_create_x64("memory_scrub_val",
				0644,
				root,
				&hdev->memory_scrub_val);

	debugfs_create_file("memory_scrub",
				0200,
				root,
				dev_entry,
				&hl_mem_scrub_fops);

	debugfs_create_x64("addr",
				0644,
				root,
				&dev_entry->addr);

	debugfs_create_file("data32",
				0644,
				root,
				dev_entry,
				&hl_data32b_fops);

	debugfs_create_file("data64",
				0644,
				root,
				dev_entry,
				&hl_data64b_fops);

	debugfs_create_file("set_power_state",
				0644,
				root,
				dev_entry,
				&hl_power_fops);

	debugfs_create_file("device",
				0644,
				root,
				dev_entry,
				&hl_device_fops);

	debugfs_create_file("clk_gate",
				0644,
				root,
				dev_entry,
				&hl_clk_gate_fops);

	debugfs_create_file("stop_on_err",
				0644,
				root,
				dev_entry,
				&hl_stop_on_err_fops);

	debugfs_create_file("dump_security_violations",
				0400,
				root,
				dev_entry,
				&hl_security_violations_fops);

	debugfs_create_file("dump_razwi_events",
				0400,
				root,
				dev_entry,
				&hl_razwi_check_fops);

	debugfs_create_file("dma_size",
				0200,
				root,
				dev_entry,
				&hl_dma_size_fops);

	debugfs_create_blob("data_dma",
				0400,
				root,
				&dev_entry->data_dma_blob_desc);

	debugfs_create_file("monitor_dump_trig",
				0200,
				root,
				dev_entry,
				&hl_monitor_dump_fops);

	debugfs_create_blob("monitor_dump",
				0400,
				root,
				&dev_entry->mon_dump_blob_desc);

	debugfs_create_x8("skip_reset_on_timeout",
				0644,
				root,
				&hdev->reset_info.skip_reset_on_timeout);

	debugfs_create_file("state_dump",
				0644,
				root,
				dev_entry,
				&hl_state_dump_fops);

	debugfs_create_file("timeout_locked",
				0644,
				root,
				dev_entry,
				&hl_timeout_locked_fops);

	debugfs_create_u32("device_release_watchdog_timeout",
				0644,
				root,
				&hdev->device_release_watchdog_timeout_sec);

	debugfs_create_u16("server_type",
				0444,
				root,
				&hdev->asic_prop.server_type);

	for (i = 0, entry = dev_entry->entry_arr ; i < count ; i++, entry++) {
		debugfs_create_file(hl_debugfs_list[i].name,
					0644,
					root,
					entry,
					&hl_debugfs_fops);
		entry->info_ent = &hl_debugfs_list[i];
		entry->dev_entry = dev_entry;
	}
}

int hl_debugfs_device_init(struct hl_device *hdev)
{
	struct hl_dbg_device_entry *dev_entry = &hdev->hl_debugfs;
	int count = ARRAY_SIZE(hl_debugfs_list);

	dev_entry->hdev = hdev;
	dev_entry->entry_arr = kmalloc_array(count, sizeof(struct hl_debugfs_entry), GFP_KERNEL);
	if (!dev_entry->entry_arr)
		return -ENOMEM;

	dev_entry->data_dma_blob_desc.size = 0;
	dev_entry->data_dma_blob_desc.data = NULL;
	dev_entry->mon_dump_blob_desc.size = 0;
	dev_entry->mon_dump_blob_desc.data = NULL;

	INIT_LIST_HEAD(&dev_entry->file_list);
	INIT_LIST_HEAD(&dev_entry->cb_list);
	INIT_LIST_HEAD(&dev_entry->cs_list);
	INIT_LIST_HEAD(&dev_entry->cs_job_list);
	INIT_LIST_HEAD(&dev_entry->userptr_list);
	INIT_LIST_HEAD(&dev_entry->ctx_mem_hash_list);
	mutex_init(&dev_entry->file_mutex);
	init_rwsem(&dev_entry->state_dump_sem);
	spin_lock_init(&dev_entry->cb_spinlock);
	spin_lock_init(&dev_entry->cs_spinlock);
	spin_lock_init(&dev_entry->cs_job_spinlock);
	spin_lock_init(&dev_entry->userptr_spinlock);
	mutex_init(&dev_entry->ctx_mem_hash_mutex);

	return 0;
}

void hl_debugfs_device_fini(struct hl_device *hdev)
{
	struct hl_dbg_device_entry *entry = &hdev->hl_debugfs;
	int i;

	mutex_destroy(&entry->ctx_mem_hash_mutex);
	mutex_destroy(&entry->file_mutex);

	vfree(entry->data_dma_blob_desc.data);
	vfree(entry->mon_dump_blob_desc.data);

	for (i = 0; i < ARRAY_SIZE(entry->state_dump); ++i)
		vfree(entry->state_dump[i]);

	kfree(entry->entry_arr);
}

void hl_debugfs_add_device(struct hl_device *hdev)
{
	struct hl_dbg_device_entry *dev_entry = &hdev->hl_debugfs;

	dev_entry->root = hdev->drm.accel->debugfs_root;

	add_files_to_device(hdev, dev_entry, dev_entry->root);

	if (!hdev->asic_prop.fw_security_enabled)
		add_secured_nodes(dev_entry, dev_entry->root);
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

	mutex_lock(&dev_entry->ctx_mem_hash_mutex);
	list_add(&ctx->debugfs_list, &dev_entry->ctx_mem_hash_list);
	mutex_unlock(&dev_entry->ctx_mem_hash_mutex);
}

void hl_debugfs_remove_ctx_mem_hash(struct hl_device *hdev, struct hl_ctx *ctx)
{
	struct hl_dbg_device_entry *dev_entry = &hdev->hl_debugfs;

	mutex_lock(&dev_entry->ctx_mem_hash_mutex);
	list_del(&ctx->debugfs_list);
	mutex_unlock(&dev_entry->ctx_mem_hash_mutex);
}

/**
 * hl_debugfs_set_state_dump - register state dump making it accessible via
 *                             debugfs
 * @hdev: pointer to the device structure
 * @data: the actual dump data
 * @length: the length of the data
 */
void hl_debugfs_set_state_dump(struct hl_device *hdev, char *data,
					unsigned long length)
{
	struct hl_dbg_device_entry *dev_entry = &hdev->hl_debugfs;

	down_write(&dev_entry->state_dump_sem);

	dev_entry->state_dump_head = (dev_entry->state_dump_head + 1) %
					ARRAY_SIZE(dev_entry->state_dump);
	vfree(dev_entry->state_dump[dev_entry->state_dump_head]);
	dev_entry->state_dump[dev_entry->state_dump_head] = data;

	up_write(&dev_entry->state_dump_sem);
}
