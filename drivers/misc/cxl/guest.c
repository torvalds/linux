/*
 * Copyright 2015 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/delay.h>

#include "cxl.h"
#include "hcalls.h"
#include "trace.h"

#define CXL_ERROR_DETECTED_EVENT	1
#define CXL_SLOT_RESET_EVENT		2
#define CXL_RESUME_EVENT		3

static void pci_error_handlers(struct cxl_afu *afu,
				int bus_error_event,
				pci_channel_state_t state)
{
	struct pci_dev *afu_dev;

	if (afu->phb == NULL)
		return;

	list_for_each_entry(afu_dev, &afu->phb->bus->devices, bus_list) {
		if (!afu_dev->driver)
			continue;

		switch (bus_error_event) {
		case CXL_ERROR_DETECTED_EVENT:
			afu_dev->error_state = state;

			if (afu_dev->driver->err_handler &&
			    afu_dev->driver->err_handler->error_detected)
				afu_dev->driver->err_handler->error_detected(afu_dev, state);
		break;
		case CXL_SLOT_RESET_EVENT:
			afu_dev->error_state = state;

			if (afu_dev->driver->err_handler &&
			    afu_dev->driver->err_handler->slot_reset)
				afu_dev->driver->err_handler->slot_reset(afu_dev);
		break;
		case CXL_RESUME_EVENT:
			if (afu_dev->driver->err_handler &&
			    afu_dev->driver->err_handler->resume)
				afu_dev->driver->err_handler->resume(afu_dev);
		break;
		}
	}
}

static irqreturn_t guest_handle_psl_slice_error(struct cxl_context *ctx, u64 dsisr,
					u64 errstat)
{
	pr_devel("in %s\n", __func__);
	dev_crit(&ctx->afu->dev, "PSL ERROR STATUS: 0x%.16llx\n", errstat);

	return cxl_ops->ack_irq(ctx, 0, errstat);
}

static ssize_t guest_collect_vpd(struct cxl *adapter, struct cxl_afu *afu,
			void *buf, size_t len)
{
	unsigned int entries, mod;
	unsigned long **vpd_buf = NULL;
	struct sg_list *le;
	int rc = 0, i, tocopy;
	u64 out = 0;

	if (buf == NULL)
		return -EINVAL;

	/* number of entries in the list */
	entries = len / SG_BUFFER_SIZE;
	mod = len % SG_BUFFER_SIZE;
	if (mod)
		entries++;

	if (entries > SG_MAX_ENTRIES) {
		entries = SG_MAX_ENTRIES;
		len = SG_MAX_ENTRIES * SG_BUFFER_SIZE;
		mod = 0;
	}

	vpd_buf = kzalloc(entries * sizeof(unsigned long *), GFP_KERNEL);
	if (!vpd_buf)
		return -ENOMEM;

	le = (struct sg_list *)get_zeroed_page(GFP_KERNEL);
	if (!le) {
		rc = -ENOMEM;
		goto err1;
	}

	for (i = 0; i < entries; i++) {
		vpd_buf[i] = (unsigned long *)get_zeroed_page(GFP_KERNEL);
		if (!vpd_buf[i]) {
			rc = -ENOMEM;
			goto err2;
		}
		le[i].phys_addr = cpu_to_be64(virt_to_phys(vpd_buf[i]));
		le[i].len = cpu_to_be64(SG_BUFFER_SIZE);
		if ((i == (entries - 1)) && mod)
			le[i].len = cpu_to_be64(mod);
	}

	if (adapter)
		rc = cxl_h_collect_vpd_adapter(adapter->guest->handle,
					virt_to_phys(le), entries, &out);
	else
		rc = cxl_h_collect_vpd(afu->guest->handle, 0,
				virt_to_phys(le), entries, &out);
	pr_devel("length of available (entries: %i), vpd: %#llx\n",
		entries, out);

	if (!rc) {
		/*
		 * hcall returns in 'out' the size of available VPDs.
		 * It fills the buffer with as much data as possible.
		 */
		if (out < len)
			len = out;
		rc = len;
		if (out) {
			for (i = 0; i < entries; i++) {
				if (len < SG_BUFFER_SIZE)
					tocopy = len;
				else
					tocopy = SG_BUFFER_SIZE;
				memcpy(buf, vpd_buf[i], tocopy);
				buf += tocopy;
				len -= tocopy;
			}
		}
	}
err2:
	for (i = 0; i < entries; i++) {
		if (vpd_buf[i])
			free_page((unsigned long) vpd_buf[i]);
	}
	free_page((unsigned long) le);
err1:
	kfree(vpd_buf);
	return rc;
}

static int guest_get_irq_info(struct cxl_context *ctx, struct cxl_irq_info *info)
{
	return cxl_h_collect_int_info(ctx->afu->guest->handle, ctx->process_token, info);
}

static irqreturn_t guest_psl_irq(int irq, void *data)
{
	struct cxl_context *ctx = data;
	struct cxl_irq_info irq_info;
	int rc;

	pr_devel("%d: received PSL interrupt %i\n", ctx->pe, irq);
	rc = guest_get_irq_info(ctx, &irq_info);
	if (rc) {
		WARN(1, "Unable to get IRQ info: %i\n", rc);
		return IRQ_HANDLED;
	}

	rc = cxl_irq(irq, ctx, &irq_info);
	return rc;
}

static int afu_read_error_state(struct cxl_afu *afu, int *state_out)
{
	u64 state;
	int rc = 0;

	if (!afu)
		return -EIO;

	rc = cxl_h_read_error_state(afu->guest->handle, &state);
	if (!rc) {
		WARN_ON(state != H_STATE_NORMAL &&
			state != H_STATE_DISABLE &&
			state != H_STATE_TEMP_UNAVAILABLE &&
			state != H_STATE_PERM_UNAVAILABLE);
		*state_out = state & 0xffffffff;
	}
	return rc;
}

static irqreturn_t guest_slice_irq_err(int irq, void *data)
{
	struct cxl_afu *afu = data;
	int rc;
	u64 serr, afu_error, dsisr;

	rc = cxl_h_get_fn_error_interrupt(afu->guest->handle, &serr);
	if (rc) {
		dev_crit(&afu->dev, "Couldn't read PSL_SERR_An: %d\n", rc);
		return IRQ_HANDLED;
	}
	afu_error = cxl_p2n_read(afu, CXL_AFU_ERR_An);
	dsisr = cxl_p2n_read(afu, CXL_PSL_DSISR_An);
	cxl_afu_decode_psl_serr(afu, serr);
	dev_crit(&afu->dev, "AFU_ERR_An: 0x%.16llx\n", afu_error);
	dev_crit(&afu->dev, "PSL_DSISR_An: 0x%.16llx\n", dsisr);

	rc = cxl_h_ack_fn_error_interrupt(afu->guest->handle, serr);
	if (rc)
		dev_crit(&afu->dev, "Couldn't ack slice error interrupt: %d\n",
			rc);

	return IRQ_HANDLED;
}


static int irq_alloc_range(struct cxl *adapter, int len, int *irq)
{
	int i, n;
	struct irq_avail *cur;

	for (i = 0; i < adapter->guest->irq_nranges; i++) {
		cur = &adapter->guest->irq_avail[i];
		n = bitmap_find_next_zero_area(cur->bitmap, cur->range,
					0, len, 0);
		if (n < cur->range) {
			bitmap_set(cur->bitmap, n, len);
			*irq = cur->offset + n;
			pr_devel("guest: allocate IRQs %#x->%#x\n",
				*irq, *irq + len - 1);

			return 0;
		}
	}
	return -ENOSPC;
}

static int irq_free_range(struct cxl *adapter, int irq, int len)
{
	int i, n;
	struct irq_avail *cur;

	if (len == 0)
		return -ENOENT;

	for (i = 0; i < adapter->guest->irq_nranges; i++) {
		cur = &adapter->guest->irq_avail[i];
		if (irq >= cur->offset &&
			(irq + len) <= (cur->offset + cur->range)) {
			n = irq - cur->offset;
			bitmap_clear(cur->bitmap, n, len);
			pr_devel("guest: release IRQs %#x->%#x\n",
				irq, irq + len - 1);
			return 0;
		}
	}
	return -ENOENT;
}

static int guest_reset(struct cxl *adapter)
{
	struct cxl_afu *afu = NULL;
	int i, rc;

	pr_devel("Adapter reset request\n");
	for (i = 0; i < adapter->slices; i++) {
		if ((afu = adapter->afu[i])) {
			pci_error_handlers(afu, CXL_ERROR_DETECTED_EVENT,
					pci_channel_io_frozen);
			cxl_context_detach_all(afu);
		}
	}

	rc = cxl_h_reset_adapter(adapter->guest->handle);
	for (i = 0; i < adapter->slices; i++) {
		if (!rc && (afu = adapter->afu[i])) {
			pci_error_handlers(afu, CXL_SLOT_RESET_EVENT,
					pci_channel_io_normal);
			pci_error_handlers(afu, CXL_RESUME_EVENT, 0);
		}
	}
	return rc;
}

static int guest_alloc_one_irq(struct cxl *adapter)
{
	int irq;

	spin_lock(&adapter->guest->irq_alloc_lock);
	if (irq_alloc_range(adapter, 1, &irq))
		irq = -ENOSPC;
	spin_unlock(&adapter->guest->irq_alloc_lock);
	return irq;
}

static void guest_release_one_irq(struct cxl *adapter, int irq)
{
	spin_lock(&adapter->guest->irq_alloc_lock);
	irq_free_range(adapter, irq, 1);
	spin_unlock(&adapter->guest->irq_alloc_lock);
}

static int guest_alloc_irq_ranges(struct cxl_irq_ranges *irqs,
				struct cxl *adapter, unsigned int num)
{
	int i, try, irq;

	memset(irqs, 0, sizeof(struct cxl_irq_ranges));

	spin_lock(&adapter->guest->irq_alloc_lock);
	for (i = 0; i < CXL_IRQ_RANGES && num; i++) {
		try = num;
		while (try) {
			if (irq_alloc_range(adapter, try, &irq) == 0)
				break;
			try /= 2;
		}
		if (!try)
			goto error;
		irqs->offset[i] = irq;
		irqs->range[i] = try;
		num -= try;
	}
	if (num)
		goto error;
	spin_unlock(&adapter->guest->irq_alloc_lock);
	return 0;

error:
	for (i = 0; i < CXL_IRQ_RANGES; i++)
		irq_free_range(adapter, irqs->offset[i], irqs->range[i]);
	spin_unlock(&adapter->guest->irq_alloc_lock);
	return -ENOSPC;
}

static void guest_release_irq_ranges(struct cxl_irq_ranges *irqs,
				struct cxl *adapter)
{
	int i;

	spin_lock(&adapter->guest->irq_alloc_lock);
	for (i = 0; i < CXL_IRQ_RANGES; i++)
		irq_free_range(adapter, irqs->offset[i], irqs->range[i]);
	spin_unlock(&adapter->guest->irq_alloc_lock);
}

static int guest_register_serr_irq(struct cxl_afu *afu)
{
	afu->err_irq_name = kasprintf(GFP_KERNEL, "cxl-%s-err",
				      dev_name(&afu->dev));
	if (!afu->err_irq_name)
		return -ENOMEM;

	if (!(afu->serr_virq = cxl_map_irq(afu->adapter, afu->serr_hwirq,
				 guest_slice_irq_err, afu, afu->err_irq_name))) {
		kfree(afu->err_irq_name);
		afu->err_irq_name = NULL;
		return -ENOMEM;
	}

	return 0;
}

static void guest_release_serr_irq(struct cxl_afu *afu)
{
	cxl_unmap_irq(afu->serr_virq, afu);
	cxl_ops->release_one_irq(afu->adapter, afu->serr_hwirq);
	kfree(afu->err_irq_name);
}

static int guest_ack_irq(struct cxl_context *ctx, u64 tfc, u64 psl_reset_mask)
{
	return cxl_h_control_faults(ctx->afu->guest->handle, ctx->process_token,
				tfc >> 32, (psl_reset_mask != 0));
}

static void disable_afu_irqs(struct cxl_context *ctx)
{
	irq_hw_number_t hwirq;
	unsigned int virq;
	int r, i;

	pr_devel("Disabling AFU(%d) interrupts\n", ctx->afu->slice);
	for (r = 0; r < CXL_IRQ_RANGES; r++) {
		hwirq = ctx->irqs.offset[r];
		for (i = 0; i < ctx->irqs.range[r]; hwirq++, i++) {
			virq = irq_find_mapping(NULL, hwirq);
			disable_irq(virq);
		}
	}
}

static void enable_afu_irqs(struct cxl_context *ctx)
{
	irq_hw_number_t hwirq;
	unsigned int virq;
	int r, i;

	pr_devel("Enabling AFU(%d) interrupts\n", ctx->afu->slice);
	for (r = 0; r < CXL_IRQ_RANGES; r++) {
		hwirq = ctx->irqs.offset[r];
		for (i = 0; i < ctx->irqs.range[r]; hwirq++, i++) {
			virq = irq_find_mapping(NULL, hwirq);
			enable_irq(virq);
		}
	}
}

static int _guest_afu_cr_readXX(int sz, struct cxl_afu *afu, int cr_idx,
			u64 offset, u64 *val)
{
	unsigned long cr;
	char c;
	int rc = 0;

	if (afu->crs_len < sz)
		return -ENOENT;

	if (unlikely(offset >= afu->crs_len))
		return -ERANGE;

	cr = get_zeroed_page(GFP_KERNEL);
	if (!cr)
		return -ENOMEM;

	rc = cxl_h_get_config(afu->guest->handle, cr_idx, offset,
			virt_to_phys((void *)cr), sz);
	if (rc)
		goto err;

	switch (sz) {
	case 1:
		c = *((char *) cr);
		*val = c;
		break;
	case 2:
		*val = in_le16((u16 *)cr);
		break;
	case 4:
		*val = in_le32((unsigned *)cr);
		break;
	case 8:
		*val = in_le64((u64 *)cr);
		break;
	default:
		WARN_ON(1);
	}
err:
	free_page(cr);
	return rc;
}

static int guest_afu_cr_read32(struct cxl_afu *afu, int cr_idx, u64 offset,
			u32 *out)
{
	int rc;
	u64 val;

	rc = _guest_afu_cr_readXX(4, afu, cr_idx, offset, &val);
	if (!rc)
		*out = (u32) val;
	return rc;
}

static int guest_afu_cr_read16(struct cxl_afu *afu, int cr_idx, u64 offset,
			u16 *out)
{
	int rc;
	u64 val;

	rc = _guest_afu_cr_readXX(2, afu, cr_idx, offset, &val);
	if (!rc)
		*out = (u16) val;
	return rc;
}

static int guest_afu_cr_read8(struct cxl_afu *afu, int cr_idx, u64 offset,
			u8 *out)
{
	int rc;
	u64 val;

	rc = _guest_afu_cr_readXX(1, afu, cr_idx, offset, &val);
	if (!rc)
		*out = (u8) val;
	return rc;
}

static int guest_afu_cr_read64(struct cxl_afu *afu, int cr_idx, u64 offset,
			u64 *out)
{
	return _guest_afu_cr_readXX(8, afu, cr_idx, offset, out);
}

static int guest_afu_cr_write32(struct cxl_afu *afu, int cr, u64 off, u32 in)
{
	/* config record is not writable from guest */
	return -EPERM;
}

static int guest_afu_cr_write16(struct cxl_afu *afu, int cr, u64 off, u16 in)
{
	/* config record is not writable from guest */
	return -EPERM;
}

static int guest_afu_cr_write8(struct cxl_afu *afu, int cr, u64 off, u8 in)
{
	/* config record is not writable from guest */
	return -EPERM;
}

static int attach_afu_directed(struct cxl_context *ctx, u64 wed, u64 amr)
{
	struct cxl_process_element_hcall *elem;
	struct cxl *adapter = ctx->afu->adapter;
	const struct cred *cred;
	u32 pid, idx;
	int rc, r, i;
	u64 mmio_addr, mmio_size;
	__be64 flags = 0;

	/* Must be 8 byte aligned and cannot cross a 4096 byte boundary */
	if (!(elem = (struct cxl_process_element_hcall *)
			get_zeroed_page(GFP_KERNEL)))
		return -ENOMEM;

	elem->version = cpu_to_be64(CXL_PROCESS_ELEMENT_VERSION);
	if (ctx->kernel) {
		pid = 0;
		flags |= CXL_PE_TRANSLATION_ENABLED;
		flags |= CXL_PE_PRIVILEGED_PROCESS;
		if (mfmsr() & MSR_SF)
			flags |= CXL_PE_64_BIT;
	} else {
		pid = current->pid;
		flags |= CXL_PE_PROBLEM_STATE;
		flags |= CXL_PE_TRANSLATION_ENABLED;
		if (!test_tsk_thread_flag(current, TIF_32BIT))
			flags |= CXL_PE_64_BIT;
		cred = get_current_cred();
		if (uid_eq(cred->euid, GLOBAL_ROOT_UID))
			flags |= CXL_PE_PRIVILEGED_PROCESS;
		put_cred(cred);
	}
	elem->flags         = cpu_to_be64(flags);
	elem->common.tid    = cpu_to_be32(0); /* Unused */
	elem->common.pid    = cpu_to_be32(pid);
	elem->common.csrp   = cpu_to_be64(0); /* disable */
	elem->common.aurp0  = cpu_to_be64(0); /* disable */
	elem->common.aurp1  = cpu_to_be64(0); /* disable */

	cxl_prefault(ctx, wed);

	elem->common.sstp0  = cpu_to_be64(ctx->sstp0);
	elem->common.sstp1  = cpu_to_be64(ctx->sstp1);

	/*
	 * Ensure we have at least one interrupt allocated to take faults for
	 * kernel contexts that may not have allocated any AFU IRQs at all:
	 */
	if (ctx->irqs.range[0] == 0) {
		rc = afu_register_irqs(ctx, 0);
		if (rc)
			goto out_free;
	}

	for (r = 0; r < CXL_IRQ_RANGES; r++) {
		for (i = 0; i < ctx->irqs.range[r]; i++) {
			if (r == 0 && i == 0) {
				elem->pslVirtualIsn = cpu_to_be32(ctx->irqs.offset[0]);
			} else {
				idx = ctx->irqs.offset[r] + i - adapter->guest->irq_base_offset;
				elem->applicationVirtualIsnBitmap[idx / 8] |= 0x80 >> (idx % 8);
			}
		}
	}
	elem->common.amr = cpu_to_be64(amr);
	elem->common.wed = cpu_to_be64(wed);

	disable_afu_irqs(ctx);

	rc = cxl_h_attach_process(ctx->afu->guest->handle, elem,
				&ctx->process_token, &mmio_addr, &mmio_size);
	if (rc == H_SUCCESS) {
		if (ctx->master || !ctx->afu->pp_psa) {
			ctx->psn_phys = ctx->afu->psn_phys;
			ctx->psn_size = ctx->afu->adapter->ps_size;
		} else {
			ctx->psn_phys = mmio_addr;
			ctx->psn_size = mmio_size;
		}
		if (ctx->afu->pp_psa && mmio_size &&
			ctx->afu->pp_size == 0) {
			/*
			 * There's no property in the device tree to read the
			 * pp_size. We only find out at the 1st attach.
			 * Compared to bare-metal, it is too late and we
			 * should really lock here. However, on powerVM,
			 * pp_size is really only used to display in /sys.
			 * Being discussed with pHyp for their next release.
			 */
			ctx->afu->pp_size = mmio_size;
		}
		/* from PAPR: process element is bytes 4-7 of process token */
		ctx->external_pe = ctx->process_token & 0xFFFFFFFF;
		pr_devel("CXL pe=%i is known as %i for pHyp, mmio_size=%#llx",
			ctx->pe, ctx->external_pe, ctx->psn_size);
		ctx->pe_inserted = true;
		enable_afu_irqs(ctx);
	}

out_free:
	free_page((u64)elem);
	return rc;
}

static int guest_attach_process(struct cxl_context *ctx, bool kernel, u64 wed, u64 amr)
{
	pr_devel("in %s\n", __func__);

	if (ctx->real_mode)
		return -EPERM;

	ctx->kernel = kernel;
	if (ctx->afu->current_mode == CXL_MODE_DIRECTED)
		return attach_afu_directed(ctx, wed, amr);

	/* dedicated mode not supported on FW840 */

	return -EINVAL;
}

static int detach_afu_directed(struct cxl_context *ctx)
{
	if (!ctx->pe_inserted)
		return 0;
	if (cxl_h_detach_process(ctx->afu->guest->handle, ctx->process_token))
		return -1;
	return 0;
}

static int guest_detach_process(struct cxl_context *ctx)
{
	pr_devel("in %s\n", __func__);
	trace_cxl_detach(ctx);

	if (!cxl_ops->link_ok(ctx->afu->adapter, ctx->afu))
		return -EIO;

	if (ctx->afu->current_mode == CXL_MODE_DIRECTED)
		return detach_afu_directed(ctx);

	return -EINVAL;
}

static void guest_release_afu(struct device *dev)
{
	struct cxl_afu *afu = to_cxl_afu(dev);

	pr_devel("%s\n", __func__);

	idr_destroy(&afu->contexts_idr);

	kfree(afu->guest);
	kfree(afu);
}

ssize_t cxl_guest_read_afu_vpd(struct cxl_afu *afu, void *buf, size_t len)
{
	return guest_collect_vpd(NULL, afu, buf, len);
}

#define ERR_BUFF_MAX_COPY_SIZE PAGE_SIZE
static ssize_t guest_afu_read_err_buffer(struct cxl_afu *afu, char *buf,
					loff_t off, size_t count)
{
	void *tbuf = NULL;
	int rc = 0;

	tbuf = (void *) get_zeroed_page(GFP_KERNEL);
	if (!tbuf)
		return -ENOMEM;

	rc = cxl_h_get_afu_err(afu->guest->handle,
			       off & 0x7,
			       virt_to_phys(tbuf),
			       count);
	if (rc)
		goto err;

	if (count > ERR_BUFF_MAX_COPY_SIZE)
		count = ERR_BUFF_MAX_COPY_SIZE - (off & 0x7);
	memcpy(buf, tbuf, count);
err:
	free_page((u64)tbuf);

	return rc;
}

static int guest_afu_check_and_enable(struct cxl_afu *afu)
{
	return 0;
}

static bool guest_support_attributes(const char *attr_name,
				     enum cxl_attrs type)
{
	switch (type) {
	case CXL_ADAPTER_ATTRS:
		if ((strcmp(attr_name, "base_image") == 0) ||
			(strcmp(attr_name, "load_image_on_perst") == 0) ||
			(strcmp(attr_name, "perst_reloads_same_image") == 0) ||
			(strcmp(attr_name, "image_loaded") == 0))
			return false;
		break;
	case CXL_AFU_MASTER_ATTRS:
		if ((strcmp(attr_name, "pp_mmio_off") == 0))
			return false;
		break;
	case CXL_AFU_ATTRS:
		break;
	default:
		break;
	}

	return true;
}

static int activate_afu_directed(struct cxl_afu *afu)
{
	int rc;

	dev_info(&afu->dev, "Activating AFU(%d) directed mode\n", afu->slice);

	afu->current_mode = CXL_MODE_DIRECTED;

	afu->num_procs = afu->max_procs_virtualised;

	if ((rc = cxl_chardev_m_afu_add(afu)))
		return rc;

	if ((rc = cxl_sysfs_afu_m_add(afu)))
		goto err;

	if ((rc = cxl_chardev_s_afu_add(afu)))
		goto err1;

	return 0;
err1:
	cxl_sysfs_afu_m_remove(afu);
err:
	cxl_chardev_afu_remove(afu);
	return rc;
}

static int guest_afu_activate_mode(struct cxl_afu *afu, int mode)
{
	if (!mode)
		return 0;
	if (!(mode & afu->modes_supported))
		return -EINVAL;

	if (mode == CXL_MODE_DIRECTED)
		return activate_afu_directed(afu);

	if (mode == CXL_MODE_DEDICATED)
		dev_err(&afu->dev, "Dedicated mode not supported\n");

	return -EINVAL;
}

static int deactivate_afu_directed(struct cxl_afu *afu)
{
	dev_info(&afu->dev, "Deactivating AFU(%d) directed mode\n", afu->slice);

	afu->current_mode = 0;
	afu->num_procs = 0;

	cxl_sysfs_afu_m_remove(afu);
	cxl_chardev_afu_remove(afu);

	cxl_ops->afu_reset(afu);

	return 0;
}

static int guest_afu_deactivate_mode(struct cxl_afu *afu, int mode)
{
	if (!mode)
		return 0;
	if (!(mode & afu->modes_supported))
		return -EINVAL;

	if (mode == CXL_MODE_DIRECTED)
		return deactivate_afu_directed(afu);
	return 0;
}

static int guest_afu_reset(struct cxl_afu *afu)
{
	pr_devel("AFU(%d) reset request\n", afu->slice);
	return cxl_h_reset_afu(afu->guest->handle);
}

static int guest_map_slice_regs(struct cxl_afu *afu)
{
	if (!(afu->p2n_mmio = ioremap(afu->guest->p2n_phys, afu->guest->p2n_size))) {
		dev_err(&afu->dev, "Error mapping AFU(%d) MMIO regions\n",
			afu->slice);
		return -ENOMEM;
	}
	return 0;
}

static void guest_unmap_slice_regs(struct cxl_afu *afu)
{
	if (afu->p2n_mmio)
		iounmap(afu->p2n_mmio);
}

static int afu_update_state(struct cxl_afu *afu)
{
	int rc, cur_state;

	rc = afu_read_error_state(afu, &cur_state);
	if (rc)
		return rc;

	if (afu->guest->previous_state == cur_state)
		return 0;

	pr_devel("AFU(%d) update state to %#x\n", afu->slice, cur_state);

	switch (cur_state) {
	case H_STATE_NORMAL:
		afu->guest->previous_state = cur_state;
		break;

	case H_STATE_DISABLE:
		pci_error_handlers(afu, CXL_ERROR_DETECTED_EVENT,
				pci_channel_io_frozen);

		cxl_context_detach_all(afu);
		if ((rc = cxl_ops->afu_reset(afu)))
			pr_devel("reset hcall failed %d\n", rc);

		rc = afu_read_error_state(afu, &cur_state);
		if (!rc && cur_state == H_STATE_NORMAL) {
			pci_error_handlers(afu, CXL_SLOT_RESET_EVENT,
					pci_channel_io_normal);
			pci_error_handlers(afu, CXL_RESUME_EVENT, 0);
		}
		afu->guest->previous_state = 0;
		break;

	case H_STATE_TEMP_UNAVAILABLE:
		afu->guest->previous_state = cur_state;
		break;

	case H_STATE_PERM_UNAVAILABLE:
		dev_err(&afu->dev, "AFU is in permanent error state\n");
		pci_error_handlers(afu, CXL_ERROR_DETECTED_EVENT,
				pci_channel_io_perm_failure);
		afu->guest->previous_state = cur_state;
		break;

	default:
		pr_err("Unexpected AFU(%d) error state: %#x\n",
		       afu->slice, cur_state);
		return -EINVAL;
	}

	return rc;
}

static void afu_handle_errstate(struct work_struct *work)
{
	struct cxl_afu_guest *afu_guest =
		container_of(to_delayed_work(work), struct cxl_afu_guest, work_err);

	if (!afu_update_state(afu_guest->parent) &&
	    afu_guest->previous_state == H_STATE_PERM_UNAVAILABLE)
		return;

	if (afu_guest->handle_err == true)
		schedule_delayed_work(&afu_guest->work_err,
				      msecs_to_jiffies(3000));
}

static bool guest_link_ok(struct cxl *cxl, struct cxl_afu *afu)
{
	int state;

	if (afu && (!afu_read_error_state(afu, &state))) {
		if (state == H_STATE_NORMAL)
			return true;
	}

	return false;
}

static int afu_properties_look_ok(struct cxl_afu *afu)
{
	if (afu->pp_irqs < 0) {
		dev_err(&afu->dev, "Unexpected per-process minimum interrupt value\n");
		return -EINVAL;
	}

	if (afu->max_procs_virtualised < 1) {
		dev_err(&afu->dev, "Unexpected max number of processes virtualised value\n");
		return -EINVAL;
	}

	if (afu->crs_len < 0) {
		dev_err(&afu->dev, "Unexpected configuration record size value\n");
		return -EINVAL;
	}

	return 0;
}

int cxl_guest_init_afu(struct cxl *adapter, int slice, struct device_node *afu_np)
{
	struct cxl_afu *afu;
	bool free = true;
	int rc;

	pr_devel("in %s - AFU(%d)\n", __func__, slice);
	if (!(afu = cxl_alloc_afu(adapter, slice)))
		return -ENOMEM;

	if (!(afu->guest = kzalloc(sizeof(struct cxl_afu_guest), GFP_KERNEL))) {
		kfree(afu);
		return -ENOMEM;
	}

	if ((rc = dev_set_name(&afu->dev, "afu%i.%i",
					  adapter->adapter_num,
					  slice)))
		goto err1;

	adapter->slices++;

	if ((rc = cxl_of_read_afu_handle(afu, afu_np)))
		goto err1;

	if ((rc = cxl_ops->afu_reset(afu)))
		goto err1;

	if ((rc = cxl_of_read_afu_properties(afu, afu_np)))
		goto err1;

	if ((rc = afu_properties_look_ok(afu)))
		goto err1;

	if ((rc = guest_map_slice_regs(afu)))
		goto err1;

	if ((rc = guest_register_serr_irq(afu)))
		goto err2;

	/*
	 * After we call this function we must not free the afu directly, even
	 * if it returns an error!
	 */
	if ((rc = cxl_register_afu(afu)))
		goto err_put1;

	if ((rc = cxl_sysfs_afu_add(afu)))
		goto err_put1;

	/*
	 * pHyp doesn't expose the programming models supported by the
	 * AFU. pHyp currently only supports directed mode. If it adds
	 * dedicated mode later, this version of cxl has no way to
	 * detect it. So we'll initialize the driver, but the first
	 * attach will fail.
	 * Being discussed with pHyp to do better (likely new property)
	 */
	if (afu->max_procs_virtualised == 1)
		afu->modes_supported = CXL_MODE_DEDICATED;
	else
		afu->modes_supported = CXL_MODE_DIRECTED;

	if ((rc = cxl_afu_select_best_mode(afu)))
		goto err_put2;

	adapter->afu[afu->slice] = afu;

	afu->enabled = true;

	/*
	 * wake up the cpu periodically to check the state
	 * of the AFU using "afu" stored in the guest structure.
	 */
	afu->guest->parent = afu;
	afu->guest->handle_err = true;
	INIT_DELAYED_WORK(&afu->guest->work_err, afu_handle_errstate);
	schedule_delayed_work(&afu->guest->work_err, msecs_to_jiffies(1000));

	if ((rc = cxl_pci_vphb_add(afu)))
		dev_info(&afu->dev, "Can't register vPHB\n");

	return 0;

err_put2:
	cxl_sysfs_afu_remove(afu);
err_put1:
	device_unregister(&afu->dev);
	free = false;
	guest_release_serr_irq(afu);
err2:
	guest_unmap_slice_regs(afu);
err1:
	if (free) {
		kfree(afu->guest);
		kfree(afu);
	}
	return rc;
}

void cxl_guest_remove_afu(struct cxl_afu *afu)
{
	pr_devel("in %s - AFU(%d)\n", __func__, afu->slice);

	if (!afu)
		return;

	/* flush and stop pending job */
	afu->guest->handle_err = false;
	flush_delayed_work(&afu->guest->work_err);

	cxl_pci_vphb_remove(afu);
	cxl_sysfs_afu_remove(afu);

	spin_lock(&afu->adapter->afu_list_lock);
	afu->adapter->afu[afu->slice] = NULL;
	spin_unlock(&afu->adapter->afu_list_lock);

	cxl_context_detach_all(afu);
	cxl_ops->afu_deactivate_mode(afu, afu->current_mode);
	guest_release_serr_irq(afu);
	guest_unmap_slice_regs(afu);

	device_unregister(&afu->dev);
}

static void free_adapter(struct cxl *adapter)
{
	struct irq_avail *cur;
	int i;

	if (adapter->guest) {
		if (adapter->guest->irq_avail) {
			for (i = 0; i < adapter->guest->irq_nranges; i++) {
				cur = &adapter->guest->irq_avail[i];
				kfree(cur->bitmap);
			}
			kfree(adapter->guest->irq_avail);
		}
		kfree(adapter->guest->status);
		kfree(adapter->guest);
	}
	cxl_remove_adapter_nr(adapter);
	kfree(adapter);
}

static int properties_look_ok(struct cxl *adapter)
{
	/* The absence of this property means that the operational
	 * status is unknown or okay
	 */
	if (strlen(adapter->guest->status) &&
	    strcmp(adapter->guest->status, "okay")) {
		pr_err("ABORTING:Bad operational status of the device\n");
		return -EINVAL;
	}

	return 0;
}

ssize_t cxl_guest_read_adapter_vpd(struct cxl *adapter, void *buf, size_t len)
{
	return guest_collect_vpd(adapter, NULL, buf, len);
}

void cxl_guest_remove_adapter(struct cxl *adapter)
{
	pr_devel("in %s\n", __func__);

	cxl_sysfs_adapter_remove(adapter);

	cxl_guest_remove_chardev(adapter);
	device_unregister(&adapter->dev);
}

static void release_adapter(struct device *dev)
{
	free_adapter(to_cxl_adapter(dev));
}

struct cxl *cxl_guest_init_adapter(struct device_node *np, struct platform_device *pdev)
{
	struct cxl *adapter;
	bool free = true;
	int rc;

	if (!(adapter = cxl_alloc_adapter()))
		return ERR_PTR(-ENOMEM);

	if (!(adapter->guest = kzalloc(sizeof(struct cxl_guest), GFP_KERNEL))) {
		free_adapter(adapter);
		return ERR_PTR(-ENOMEM);
	}

	adapter->slices = 0;
	adapter->guest->pdev = pdev;
	adapter->dev.parent = &pdev->dev;
	adapter->dev.release = release_adapter;
	dev_set_drvdata(&pdev->dev, adapter);

	/*
	 * Hypervisor controls PSL timebase initialization (p1 register).
	 * On FW840, PSL is initialized.
	 */
	adapter->psl_timebase_synced = true;

	if ((rc = cxl_of_read_adapter_handle(adapter, np)))
		goto err1;

	if ((rc = cxl_of_read_adapter_properties(adapter, np)))
		goto err1;

	if ((rc = properties_look_ok(adapter)))
		goto err1;

	if ((rc = cxl_guest_add_chardev(adapter)))
		goto err1;

	/*
	 * After we call this function we must not free the adapter directly,
	 * even if it returns an error!
	 */
	if ((rc = cxl_register_adapter(adapter)))
		goto err_put1;

	if ((rc = cxl_sysfs_adapter_add(adapter)))
		goto err_put1;

	/* release the context lock as the adapter is configured */
	cxl_adapter_context_unlock(adapter);

	return adapter;

err_put1:
	device_unregister(&adapter->dev);
	free = false;
	cxl_guest_remove_chardev(adapter);
err1:
	if (free)
		free_adapter(adapter);
	return ERR_PTR(rc);
}

void cxl_guest_reload_module(struct cxl *adapter)
{
	struct platform_device *pdev;

	pdev = adapter->guest->pdev;
	cxl_guest_remove_adapter(adapter);

	cxl_of_probe(pdev);
}

const struct cxl_backend_ops cxl_guest_ops = {
	.module = THIS_MODULE,
	.adapter_reset = guest_reset,
	.alloc_one_irq = guest_alloc_one_irq,
	.release_one_irq = guest_release_one_irq,
	.alloc_irq_ranges = guest_alloc_irq_ranges,
	.release_irq_ranges = guest_release_irq_ranges,
	.setup_irq = NULL,
	.handle_psl_slice_error = guest_handle_psl_slice_error,
	.psl_interrupt = guest_psl_irq,
	.ack_irq = guest_ack_irq,
	.attach_process = guest_attach_process,
	.detach_process = guest_detach_process,
	.update_ivtes = NULL,
	.support_attributes = guest_support_attributes,
	.link_ok = guest_link_ok,
	.release_afu = guest_release_afu,
	.afu_read_err_buffer = guest_afu_read_err_buffer,
	.afu_check_and_enable = guest_afu_check_and_enable,
	.afu_activate_mode = guest_afu_activate_mode,
	.afu_deactivate_mode = guest_afu_deactivate_mode,
	.afu_reset = guest_afu_reset,
	.afu_cr_read8 = guest_afu_cr_read8,
	.afu_cr_read16 = guest_afu_cr_read16,
	.afu_cr_read32 = guest_afu_cr_read32,
	.afu_cr_read64 = guest_afu_cr_read64,
	.afu_cr_write8 = guest_afu_cr_write8,
	.afu_cr_write16 = guest_afu_cr_write16,
	.afu_cr_write32 = guest_afu_cr_write32,
	.read_adapter_vpd = cxl_guest_read_adapter_vpd,
};
