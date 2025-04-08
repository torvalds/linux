// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2012, The Linux Foundation. All rights reserved.
 *
 * Description: CoreSight Trace Memory Controller driver
 */

#include <linux/acpi.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/idr.h>
#include <linux/io.h>
#include <linux/iommu.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/property.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/spinlock.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/coresight.h>
#include <linux/amba/bus.h>
#include <linux/platform_device.h>

#include "coresight-priv.h"
#include "coresight-tmc.h"

DEFINE_CORESIGHT_DEVLIST(etb_devs, "tmc_etb");
DEFINE_CORESIGHT_DEVLIST(etf_devs, "tmc_etf");
DEFINE_CORESIGHT_DEVLIST(etr_devs, "tmc_etr");

int tmc_wait_for_tmcready(struct tmc_drvdata *drvdata)
{
	struct coresight_device *csdev = drvdata->csdev;
	struct csdev_access *csa = &csdev->access;

	/* Ensure formatter, unformatter and hardware fifo are empty */
	if (coresight_timeout(csa, TMC_STS, TMC_STS_TMCREADY_BIT, 1)) {
		dev_err(&csdev->dev,
			"timeout while waiting for TMC to be Ready\n");
		return -EBUSY;
	}
	return 0;
}

void tmc_flush_and_stop(struct tmc_drvdata *drvdata)
{
	struct coresight_device *csdev = drvdata->csdev;
	struct csdev_access *csa = &csdev->access;
	u32 ffcr;

	ffcr = readl_relaxed(drvdata->base + TMC_FFCR);
	ffcr |= TMC_FFCR_STOP_ON_FLUSH;
	writel_relaxed(ffcr, drvdata->base + TMC_FFCR);
	ffcr |= BIT(TMC_FFCR_FLUSHMAN_BIT);
	writel_relaxed(ffcr, drvdata->base + TMC_FFCR);
	/* Ensure flush completes */
	if (coresight_timeout(csa, TMC_FFCR, TMC_FFCR_FLUSHMAN_BIT, 0)) {
		dev_err(&csdev->dev,
		"timeout while waiting for completion of Manual Flush\n");
	}

	tmc_wait_for_tmcready(drvdata);
}

void tmc_enable_hw(struct tmc_drvdata *drvdata)
{
	writel_relaxed(TMC_CTL_CAPT_EN, drvdata->base + TMC_CTL);
}

void tmc_disable_hw(struct tmc_drvdata *drvdata)
{
	writel_relaxed(0x0, drvdata->base + TMC_CTL);
}

u32 tmc_get_memwidth_mask(struct tmc_drvdata *drvdata)
{
	u32 mask = 0;

	/*
	 * When moving RRP or an offset address forward, the new values must
	 * be byte-address aligned to the width of the trace memory databus
	 * _and_ to a frame boundary (16 byte), whichever is the biggest. For
	 * example, for 32-bit, 64-bit and 128-bit wide trace memory, the four
	 * LSBs must be 0s. For 256-bit wide trace memory, the five LSBs must
	 * be 0s.
	 */
	switch (drvdata->memwidth) {
	case TMC_MEM_INTF_WIDTH_32BITS:
	case TMC_MEM_INTF_WIDTH_64BITS:
	case TMC_MEM_INTF_WIDTH_128BITS:
		mask = GENMASK(31, 4);
		break;
	case TMC_MEM_INTF_WIDTH_256BITS:
		mask = GENMASK(31, 5);
		break;
	}

	return mask;
}

static bool is_tmc_crashdata_valid(struct tmc_drvdata *drvdata)
{
	struct tmc_crash_metadata *mdata;

	if (!tmc_has_reserved_buffer(drvdata) ||
	    !tmc_has_crash_mdata_buffer(drvdata))
		return false;

	mdata = drvdata->crash_mdata.vaddr;

	/* Check version match */
	if (mdata->version != CS_CRASHDATA_VERSION)
		return false;

	/* Check for valid metadata */
	if (!mdata->valid) {
		dev_dbg(&drvdata->csdev->dev,
			"Data invalid in tmc crash metadata\n");
		return false;
	}

	/*
	 * Buffer address given by metadata for retrieval of trace data
	 * from previous boot is expected to be same as the reserved
	 * trace buffer memory region provided through DTS
	 */
	if (drvdata->resrv_buf.paddr != mdata->trace_paddr) {
		dev_dbg(&drvdata->csdev->dev,
			"Trace buffer address of previous boot invalid\n");
		return false;
	}

	/* Check data integrity of metadata */
	if (mdata->crc32_mdata != find_crash_metadata_crc(mdata)) {
		dev_err(&drvdata->csdev->dev,
			"CRC mismatch in tmc crash metadata\n");
		return false;
	}
	/* Check data integrity of tracedata */
	if (mdata->crc32_tdata != find_crash_tracedata_crc(drvdata, mdata)) {
		dev_err(&drvdata->csdev->dev,
			"CRC mismatch in tmc crash tracedata\n");
		return false;
	}

	return true;
}

static inline ssize_t tmc_get_resvbuf_trace(struct tmc_drvdata *drvdata,
					  loff_t pos, size_t len, char **bufpp)
{
	s64 offset;
	ssize_t actual = len;
	struct tmc_resrv_buf *rbuf = &drvdata->resrv_buf;

	if (pos + actual > rbuf->len)
		actual = rbuf->len - pos;
	if (actual <= 0)
		return 0;

	/* Compute the offset from which we read the data */
	offset = rbuf->offset + pos;
	if (offset >= rbuf->size)
		offset -= rbuf->size;

	/* Adjust the length to limit this transaction to end of buffer */
	actual = (actual < (rbuf->size - offset)) ?
		actual : rbuf->size - offset;

	*bufpp = (char *)rbuf->vaddr + offset;

	return actual;
}

static int tmc_prepare_crashdata(struct tmc_drvdata *drvdata)
{
	char *bufp;
	ssize_t len;
	u32 status, size;
	u64 rrp, rwp, dba;
	struct tmc_resrv_buf *rbuf;
	struct tmc_crash_metadata *mdata;

	mdata = drvdata->crash_mdata.vaddr;
	rbuf = &drvdata->resrv_buf;

	rrp = mdata->tmc_rrp;
	rwp = mdata->tmc_rwp;
	dba = mdata->tmc_dba;
	status = mdata->tmc_sts;
	size = mdata->tmc_ram_size << 2;

	/* Sync the buffer pointers */
	rbuf->offset = rrp - dba;
	if (status & TMC_STS_FULL)
		rbuf->len = size;
	else
		rbuf->len = rwp - rrp;

	/* Additional sanity checks for validating metadata */
	if ((rbuf->offset > size) ||
	    (rbuf->len > size)) {
		dev_dbg(&drvdata->csdev->dev,
			"Offset and length invalid in tmc crash metadata\n");
		return -EINVAL;
	}

	if (status & TMC_STS_FULL) {
		len = tmc_get_resvbuf_trace(drvdata, 0x0,
					    CORESIGHT_BARRIER_PKT_SIZE, &bufp);
		if (len >= CORESIGHT_BARRIER_PKT_SIZE) {
			coresight_insert_barrier_packet(bufp);
			/* Recalculate crc */
			mdata->crc32_tdata = find_crash_tracedata_crc(drvdata,
								      mdata);
			mdata->crc32_mdata = find_crash_metadata_crc(mdata);
		}
	}

	return 0;
}

static int tmc_read_prepare(struct tmc_drvdata *drvdata)
{
	int ret = 0;

	switch (drvdata->config_type) {
	case TMC_CONFIG_TYPE_ETB:
	case TMC_CONFIG_TYPE_ETF:
		ret = tmc_read_prepare_etb(drvdata);
		break;
	case TMC_CONFIG_TYPE_ETR:
		ret = tmc_read_prepare_etr(drvdata);
		break;
	default:
		ret = -EINVAL;
	}

	if (!ret)
		dev_dbg(&drvdata->csdev->dev, "TMC read start\n");

	return ret;
}

static int tmc_read_unprepare(struct tmc_drvdata *drvdata)
{
	int ret = 0;

	switch (drvdata->config_type) {
	case TMC_CONFIG_TYPE_ETB:
	case TMC_CONFIG_TYPE_ETF:
		ret = tmc_read_unprepare_etb(drvdata);
		break;
	case TMC_CONFIG_TYPE_ETR:
		ret = tmc_read_unprepare_etr(drvdata);
		break;
	default:
		ret = -EINVAL;
	}

	if (!ret)
		dev_dbg(&drvdata->csdev->dev, "TMC read end\n");

	return ret;
}

static int tmc_open(struct inode *inode, struct file *file)
{
	int ret;
	struct tmc_drvdata *drvdata = container_of(file->private_data,
						   struct tmc_drvdata, miscdev);

	ret = tmc_read_prepare(drvdata);
	if (ret)
		return ret;

	nonseekable_open(inode, file);

	dev_dbg(&drvdata->csdev->dev, "%s: successfully opened\n", __func__);
	return 0;
}

static inline ssize_t tmc_get_sysfs_trace(struct tmc_drvdata *drvdata,
					  loff_t pos, size_t len, char **bufpp)
{
	switch (drvdata->config_type) {
	case TMC_CONFIG_TYPE_ETB:
	case TMC_CONFIG_TYPE_ETF:
		return tmc_etb_get_sysfs_trace(drvdata, pos, len, bufpp);
	case TMC_CONFIG_TYPE_ETR:
		return tmc_etr_get_sysfs_trace(drvdata, pos, len, bufpp);
	}

	return -EINVAL;
}

static ssize_t tmc_read(struct file *file, char __user *data, size_t len,
			loff_t *ppos)
{
	char *bufp;
	ssize_t actual;
	struct tmc_drvdata *drvdata = container_of(file->private_data,
						   struct tmc_drvdata, miscdev);
	actual = tmc_get_sysfs_trace(drvdata, *ppos, len, &bufp);
	if (actual <= 0)
		return 0;

	if (copy_to_user(data, bufp, actual)) {
		dev_dbg(&drvdata->csdev->dev,
			"%s: copy_to_user failed\n", __func__);
		return -EFAULT;
	}

	*ppos += actual;
	dev_dbg(&drvdata->csdev->dev, "%zu bytes copied\n", actual);

	return actual;
}

static int tmc_release(struct inode *inode, struct file *file)
{
	int ret;
	struct tmc_drvdata *drvdata = container_of(file->private_data,
						   struct tmc_drvdata, miscdev);

	ret = tmc_read_unprepare(drvdata);
	if (ret)
		return ret;

	dev_dbg(&drvdata->csdev->dev, "%s: released\n", __func__);
	return 0;
}

static const struct file_operations tmc_fops = {
	.owner		= THIS_MODULE,
	.open		= tmc_open,
	.read		= tmc_read,
	.release	= tmc_release,
};

static int tmc_crashdata_open(struct inode *inode, struct file *file)
{
	int err = 0;
	unsigned long flags;
	struct tmc_resrv_buf *rbuf;
	struct tmc_crash_metadata *mdata;
	struct tmc_drvdata *drvdata = container_of(file->private_data,
						   struct tmc_drvdata,
						   crashdev);

	mdata = drvdata->crash_mdata.vaddr;
	rbuf = &drvdata->resrv_buf;

	raw_spin_lock_irqsave(&drvdata->spinlock, flags);
	if (mdata->valid)
		rbuf->reading = true;
	else
		err = -ENOENT;
	raw_spin_unlock_irqrestore(&drvdata->spinlock, flags);
	if (err)
		goto exit;

	nonseekable_open(inode, file);
	dev_dbg(&drvdata->csdev->dev, "%s: successfully opened\n", __func__);
exit:
	return err;
}

static ssize_t tmc_crashdata_read(struct file *file, char __user *data,
				  size_t len, loff_t *ppos)
{
	char *bufp;
	ssize_t actual;
	struct tmc_drvdata *drvdata = container_of(file->private_data,
						   struct tmc_drvdata,
						   crashdev);

	actual = tmc_get_resvbuf_trace(drvdata, *ppos, len, &bufp);
	if (actual <= 0)
		return 0;

	if (copy_to_user(data, bufp, actual)) {
		dev_dbg(&drvdata->csdev->dev,
			"%s: copy_to_user failed\n", __func__);
		return -EFAULT;
	}

	*ppos += actual;
	dev_dbg(&drvdata->csdev->dev, "%zu bytes copied\n", actual);

	return actual;
}

static int tmc_crashdata_release(struct inode *inode, struct file *file)
{
	int ret = 0;
	unsigned long flags;
	struct tmc_resrv_buf *rbuf;
	struct tmc_drvdata *drvdata = container_of(file->private_data,
						   struct tmc_drvdata,
						   crashdev);

	rbuf = &drvdata->resrv_buf;
	raw_spin_lock_irqsave(&drvdata->spinlock, flags);
	rbuf->reading = false;
	raw_spin_unlock_irqrestore(&drvdata->spinlock, flags);

	dev_dbg(&drvdata->csdev->dev, "%s: released\n", __func__);
	return ret;
}

static const struct file_operations tmc_crashdata_fops = {
	.owner		= THIS_MODULE,
	.open		= tmc_crashdata_open,
	.read		= tmc_crashdata_read,
	.release	= tmc_crashdata_release,
};

static enum tmc_mem_intf_width tmc_get_memwidth(u32 devid)
{
	enum tmc_mem_intf_width memwidth;

	/*
	 * Excerpt from the TRM:
	 *
	 * DEVID::MEMWIDTH[10:8]
	 * 0x2 Memory interface databus is 32 bits wide.
	 * 0x3 Memory interface databus is 64 bits wide.
	 * 0x4 Memory interface databus is 128 bits wide.
	 * 0x5 Memory interface databus is 256 bits wide.
	 */
	switch (BMVAL(devid, 8, 10)) {
	case 0x2:
		memwidth = TMC_MEM_INTF_WIDTH_32BITS;
		break;
	case 0x3:
		memwidth = TMC_MEM_INTF_WIDTH_64BITS;
		break;
	case 0x4:
		memwidth = TMC_MEM_INTF_WIDTH_128BITS;
		break;
	case 0x5:
		memwidth = TMC_MEM_INTF_WIDTH_256BITS;
		break;
	default:
		memwidth = 0;
	}

	return memwidth;
}

static struct attribute *coresight_tmc_mgmt_attrs[] = {
	coresight_simple_reg32(rsz, TMC_RSZ),
	coresight_simple_reg32(sts, TMC_STS),
	coresight_simple_reg64(rrp, TMC_RRP, TMC_RRPHI),
	coresight_simple_reg64(rwp, TMC_RWP, TMC_RWPHI),
	coresight_simple_reg32(trg, TMC_TRG),
	coresight_simple_reg32(ctl, TMC_CTL),
	coresight_simple_reg32(ffsr, TMC_FFSR),
	coresight_simple_reg32(ffcr, TMC_FFCR),
	coresight_simple_reg32(mode, TMC_MODE),
	coresight_simple_reg32(pscr, TMC_PSCR),
	coresight_simple_reg32(devid, CORESIGHT_DEVID),
	coresight_simple_reg64(dba, TMC_DBALO, TMC_DBAHI),
	coresight_simple_reg32(axictl, TMC_AXICTL),
	coresight_simple_reg32(authstatus, TMC_AUTHSTATUS),
	NULL,
};

static ssize_t trigger_cntr_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct tmc_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val = drvdata->trigger_cntr;

	return sprintf(buf, "%#lx\n", val);
}

static ssize_t trigger_cntr_store(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct tmc_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	drvdata->trigger_cntr = val;
	return size;
}
static DEVICE_ATTR_RW(trigger_cntr);

static ssize_t buffer_size_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct tmc_drvdata *drvdata = dev_get_drvdata(dev->parent);

	return sprintf(buf, "%#x\n", drvdata->size);
}

static ssize_t buffer_size_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct tmc_drvdata *drvdata = dev_get_drvdata(dev->parent);

	/* Only permitted for TMC-ETRs */
	if (drvdata->config_type != TMC_CONFIG_TYPE_ETR)
		return -EPERM;

	ret = kstrtoul(buf, 0, &val);
	if (ret)
		return ret;
	/* The buffer size should be page aligned */
	if (val & (PAGE_SIZE - 1))
		return -EINVAL;
	drvdata->size = val;
	return size;
}

static DEVICE_ATTR_RW(buffer_size);

static ssize_t stop_on_flush_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct tmc_drvdata *drvdata = dev_get_drvdata(dev->parent);

	return sprintf(buf, "%#x\n", drvdata->stop_on_flush);
}

static ssize_t stop_on_flush_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t size)
{
	int ret;
	u8 val;
	struct tmc_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtou8(buf, 0, &val);
	if (ret)
		return ret;
	if (val)
		drvdata->stop_on_flush = true;
	else
		drvdata->stop_on_flush = false;

	return size;
}

static DEVICE_ATTR_RW(stop_on_flush);


static struct attribute *coresight_tmc_attrs[] = {
	&dev_attr_trigger_cntr.attr,
	&dev_attr_buffer_size.attr,
	&dev_attr_stop_on_flush.attr,
	NULL,
};

static const struct attribute_group coresight_tmc_group = {
	.attrs = coresight_tmc_attrs,
};

static const struct attribute_group coresight_tmc_mgmt_group = {
	.attrs = coresight_tmc_mgmt_attrs,
	.name = "mgmt",
};

static const struct attribute_group *coresight_etf_groups[] = {
	&coresight_tmc_group,
	&coresight_tmc_mgmt_group,
	NULL,
};

static const struct attribute_group *coresight_etr_groups[] = {
	&coresight_etr_group,
	&coresight_tmc_group,
	&coresight_tmc_mgmt_group,
	NULL,
};

static inline bool tmc_etr_can_use_sg(struct device *dev)
{
	int ret;
	u8 val_u8;

	/*
	 * Presence of the property 'arm,scatter-gather' is checked
	 * on the platform for the feature support, rather than its
	 * value.
	 */
	if (is_of_node(dev->fwnode)) {
		return fwnode_property_present(dev->fwnode, "arm,scatter-gather");
	} else if (is_acpi_device_node(dev->fwnode)) {
		/*
		 * TMC_DEVID_NOSCAT test in tmc_etr_setup_caps(), has already ensured
		 * this property is only checked for Coresight SoC 400 TMC configured
		 * as ETR.
		 */
		ret = fwnode_property_read_u8(dev->fwnode, "arm-armhc97c-sg-enable", &val_u8);
		if (!ret)
			return !!val_u8;

		if (fwnode_property_present(dev->fwnode, "arm,scatter-gather")) {
			pr_warn_once("Deprecated ACPI property - arm,scatter-gather\n");
			return true;
		}
	}
	return false;
}

static inline bool tmc_etr_has_non_secure_access(struct tmc_drvdata *drvdata)
{
	u32 auth = readl_relaxed(drvdata->base + TMC_AUTHSTATUS);

	return (auth & TMC_AUTH_NSID_MASK) == 0x3;
}

static const struct amba_id tmc_ids[];

static int of_tmc_get_reserved_resource_by_name(struct device *dev,
						const char *name,
						struct resource *res)
{
	int index, rc = -ENODEV;
	struct device_node *node;

	if (!is_of_node(dev->fwnode))
		return -ENODEV;

	index = of_property_match_string(dev->of_node, "memory-region-names",
					 name);
	if (index < 0)
		return rc;

	node = of_parse_phandle(dev->of_node, "memory-region", index);
	if (!node)
		return rc;

	if (!of_address_to_resource(node, 0, res) &&
	    res->start != 0 && resource_size(res) != 0)
		rc = 0;
	of_node_put(node);

	return rc;
}

static void tmc_get_reserved_region(struct device *parent)
{
	struct tmc_drvdata *drvdata = dev_get_drvdata(parent);
	struct resource res;

	if (of_tmc_get_reserved_resource_by_name(parent, "tracedata", &res))
		return;

	drvdata->resrv_buf.vaddr = memremap(res.start,
						resource_size(&res),
						MEMREMAP_WC);
	if (IS_ERR_OR_NULL(drvdata->resrv_buf.vaddr)) {
		dev_err(parent, "Reserved trace buffer mapping failed\n");
		return;
	}

	drvdata->resrv_buf.paddr = res.start;
	drvdata->resrv_buf.size  = resource_size(&res);

	if (of_tmc_get_reserved_resource_by_name(parent, "metadata", &res))
		return;

	drvdata->crash_mdata.vaddr = memremap(res.start,
					       resource_size(&res),
					       MEMREMAP_WC);
	if (IS_ERR_OR_NULL(drvdata->crash_mdata.vaddr)) {
		dev_err(parent, "Metadata memory mapping failed\n");
		return;
	}

	drvdata->crash_mdata.paddr = res.start;
	drvdata->crash_mdata.size  = resource_size(&res);
}

/* Detect and initialise the capabilities of a TMC ETR */
static int tmc_etr_setup_caps(struct device *parent, u32 devid,
			      struct csdev_access *access)
{
	int rc;
	u32 tmc_pid, dma_mask = 0;
	struct tmc_drvdata *drvdata = dev_get_drvdata(parent);
	void *dev_caps;

	if (!tmc_etr_has_non_secure_access(drvdata))
		return -EACCES;

	tmc_pid = coresight_get_pid(access);
	dev_caps = coresight_get_uci_data_from_amba(tmc_ids, tmc_pid);

	/* Set the unadvertised capabilities */
	tmc_etr_init_caps(drvdata, (u32)(unsigned long)dev_caps);

	if (!(devid & TMC_DEVID_NOSCAT) && tmc_etr_can_use_sg(parent))
		tmc_etr_set_cap(drvdata, TMC_ETR_SG);

	/* Check if the AXI address width is available */
	if (devid & TMC_DEVID_AXIAW_VALID)
		dma_mask = ((devid >> TMC_DEVID_AXIAW_SHIFT) &
				TMC_DEVID_AXIAW_MASK);

	/*
	 * Unless specified in the device configuration, ETR uses a 40-bit
	 * AXI master in place of the embedded SRAM of ETB/ETF.
	 */
	switch (dma_mask) {
	case 32:
	case 40:
	case 44:
	case 48:
	case 52:
		dev_info(parent, "Detected dma mask %dbits\n", dma_mask);
		break;
	default:
		dma_mask = 40;
	}

	rc = dma_set_mask_and_coherent(parent, DMA_BIT_MASK(dma_mask));
	if (rc)
		dev_err(parent, "Failed to setup DMA mask: %d\n", rc);
	return rc;
}

static u32 tmc_etr_get_default_buffer_size(struct device *dev)
{
	u32 size;

	if (fwnode_property_read_u32(dev->fwnode, "arm,buffer-size", &size))
		size = SZ_1M;
	return size;
}

static u32 tmc_etr_get_max_burst_size(struct device *dev)
{
	u32 burst_size;

	if (fwnode_property_read_u32(dev->fwnode, "arm,max-burst-size",
				     &burst_size))
		return TMC_AXICTL_WR_BURST_16;

	/* Only permissible values are 0 to 15 */
	if (burst_size > 0xF)
		burst_size = TMC_AXICTL_WR_BURST_16;

	return burst_size;
}

static void register_crash_dev_interface(struct tmc_drvdata *drvdata,
					 const char *name)
{
	drvdata->crashdev.name =
		devm_kasprintf(&drvdata->csdev->dev, GFP_KERNEL, "%s_%s", "crash", name);
	drvdata->crashdev.minor = MISC_DYNAMIC_MINOR;
	drvdata->crashdev.fops = &tmc_crashdata_fops;
	if (misc_register(&drvdata->crashdev)) {
		dev_dbg(&drvdata->csdev->dev,
			"Failed to setup user interface for crashdata\n");
		drvdata->crashdev.fops = NULL;
	} else
		dev_info(&drvdata->csdev->dev,
			"Valid crash tracedata found\n");
}

static int __tmc_probe(struct device *dev, struct resource *res)
{
	int ret = 0;
	u32 devid;
	void __iomem *base;
	struct coresight_platform_data *pdata = NULL;
	struct tmc_drvdata *drvdata = dev_get_drvdata(dev);
	struct coresight_desc desc = { 0 };
	struct coresight_dev_list *dev_list = NULL;

	ret = -ENOMEM;

	/* Validity for the resource is already checked by the AMBA core */
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base)) {
		ret = PTR_ERR(base);
		goto out;
	}

	drvdata->base = base;
	desc.access = CSDEV_ACCESS_IOMEM(base);

	raw_spin_lock_init(&drvdata->spinlock);

	devid = readl_relaxed(drvdata->base + CORESIGHT_DEVID);
	drvdata->config_type = BMVAL(devid, 6, 7);
	drvdata->memwidth = tmc_get_memwidth(devid);
	/* This device is not associated with a session */
	drvdata->pid = -1;
	drvdata->etr_mode = ETR_MODE_AUTO;

	if (drvdata->config_type == TMC_CONFIG_TYPE_ETR) {
		drvdata->size = tmc_etr_get_default_buffer_size(dev);
		drvdata->max_burst_size = tmc_etr_get_max_burst_size(dev);
	} else {
		drvdata->size = readl_relaxed(drvdata->base + TMC_RSZ) * 4;
	}

	tmc_get_reserved_region(dev);

	desc.dev = dev;

	switch (drvdata->config_type) {
	case TMC_CONFIG_TYPE_ETB:
		desc.groups = coresight_etf_groups;
		desc.type = CORESIGHT_DEV_TYPE_SINK;
		desc.subtype.sink_subtype = CORESIGHT_DEV_SUBTYPE_SINK_BUFFER;
		desc.ops = &tmc_etb_cs_ops;
		dev_list = &etb_devs;
		break;
	case TMC_CONFIG_TYPE_ETR:
		desc.groups = coresight_etr_groups;
		desc.type = CORESIGHT_DEV_TYPE_SINK;
		desc.subtype.sink_subtype = CORESIGHT_DEV_SUBTYPE_SINK_SYSMEM;
		desc.ops = &tmc_etr_cs_ops;
		ret = tmc_etr_setup_caps(dev, devid, &desc.access);
		if (ret)
			goto out;
		idr_init(&drvdata->idr);
		mutex_init(&drvdata->idr_mutex);
		dev_list = &etr_devs;
		break;
	case TMC_CONFIG_TYPE_ETF:
		desc.groups = coresight_etf_groups;
		desc.type = CORESIGHT_DEV_TYPE_LINKSINK;
		desc.subtype.sink_subtype = CORESIGHT_DEV_SUBTYPE_SINK_BUFFER;
		desc.subtype.link_subtype = CORESIGHT_DEV_SUBTYPE_LINK_FIFO;
		desc.ops = &tmc_etf_cs_ops;
		dev_list = &etf_devs;
		break;
	default:
		pr_err("%s: Unsupported TMC config\n", desc.name);
		ret = -EINVAL;
		goto out;
	}

	desc.name = coresight_alloc_device_name(dev_list, dev);
	if (!desc.name) {
		ret = -ENOMEM;
		goto out;
	}

	pdata = coresight_get_platform_data(dev);
	if (IS_ERR(pdata)) {
		ret = PTR_ERR(pdata);
		goto out;
	}
	dev->platform_data = pdata;
	desc.pdata = pdata;

	drvdata->csdev = coresight_register(&desc);
	if (IS_ERR(drvdata->csdev)) {
		ret = PTR_ERR(drvdata->csdev);
		goto out;
	}

	drvdata->miscdev.name = desc.name;
	drvdata->miscdev.minor = MISC_DYNAMIC_MINOR;
	drvdata->miscdev.fops = &tmc_fops;
	ret = misc_register(&drvdata->miscdev);
	if (ret) {
		coresight_unregister(drvdata->csdev);
		goto out;
	}

out:
	if (is_tmc_crashdata_valid(drvdata) &&
	    !tmc_prepare_crashdata(drvdata))
		register_crash_dev_interface(drvdata, desc.name);
	return ret;
}

static int tmc_probe(struct amba_device *adev, const struct amba_id *id)
{
	struct tmc_drvdata *drvdata;
	int ret;

	drvdata = devm_kzalloc(&adev->dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	amba_set_drvdata(adev, drvdata);
	ret = __tmc_probe(&adev->dev, &adev->res);
	if (!ret)
		pm_runtime_put(&adev->dev);

	return ret;
}

static void tmc_shutdown(struct amba_device *adev)
{
	unsigned long flags;
	struct tmc_drvdata *drvdata = amba_get_drvdata(adev);

	raw_spin_lock_irqsave(&drvdata->spinlock, flags);

	if (coresight_get_mode(drvdata->csdev) == CS_MODE_DISABLED)
		goto out;

	if (drvdata->config_type == TMC_CONFIG_TYPE_ETR)
		tmc_etr_disable_hw(drvdata);

	/*
	 * We do not care about coresight unregister here unlike remove
	 * callback which is required for making coresight modular since
	 * the system is going down after this.
	 */
out:
	raw_spin_unlock_irqrestore(&drvdata->spinlock, flags);
}

static void __tmc_remove(struct device *dev)
{
	struct tmc_drvdata *drvdata = dev_get_drvdata(dev);

	/*
	 * Since misc_open() holds a refcount on the f_ops, which is
	 * etb fops in this case, device is there until last file
	 * handler to this device is closed.
	 */
	misc_deregister(&drvdata->miscdev);
	if (drvdata->crashdev.fops)
		misc_deregister(&drvdata->crashdev);
	coresight_unregister(drvdata->csdev);
}

static void tmc_remove(struct amba_device *adev)
{
	__tmc_remove(&adev->dev);
}

static const struct amba_id tmc_ids[] = {
	CS_AMBA_ID(0x000bb961),
	/* Coresight SoC 600 TMC-ETR/ETS */
	CS_AMBA_ID_DATA(0x000bb9e8, (unsigned long)CORESIGHT_SOC_600_ETR_CAPS),
	/* Coresight SoC 600 TMC-ETB */
	CS_AMBA_ID(0x000bb9e9),
	/* Coresight SoC 600 TMC-ETF */
	CS_AMBA_ID(0x000bb9ea),
	{ 0, 0, NULL },
};

MODULE_DEVICE_TABLE(amba, tmc_ids);

static struct amba_driver tmc_driver = {
	.drv = {
		.name   = "coresight-tmc",
		.suppress_bind_attrs = true,
	},
	.probe		= tmc_probe,
	.shutdown	= tmc_shutdown,
	.remove		= tmc_remove,
	.id_table	= tmc_ids,
};

static int tmc_platform_probe(struct platform_device *pdev)
{
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	struct tmc_drvdata *drvdata;
	int ret = 0;

	drvdata = devm_kzalloc(&pdev->dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	drvdata->pclk = coresight_get_enable_apb_pclk(&pdev->dev);
	if (IS_ERR(drvdata->pclk))
		return -ENODEV;

	dev_set_drvdata(&pdev->dev, drvdata);
	pm_runtime_get_noresume(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	ret = __tmc_probe(&pdev->dev, res);
	pm_runtime_put(&pdev->dev);
	if (ret)
		pm_runtime_disable(&pdev->dev);

	return ret;
}

static void tmc_platform_remove(struct platform_device *pdev)
{
	struct tmc_drvdata *drvdata = dev_get_drvdata(&pdev->dev);

	if (WARN_ON(!drvdata))
		return;

	__tmc_remove(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	if (!IS_ERR_OR_NULL(drvdata->pclk))
		clk_put(drvdata->pclk);
}

#ifdef CONFIG_PM
static int tmc_runtime_suspend(struct device *dev)
{
	struct tmc_drvdata *drvdata = dev_get_drvdata(dev);

	if (drvdata && !IS_ERR_OR_NULL(drvdata->pclk))
		clk_disable_unprepare(drvdata->pclk);
	return 0;
}

static int tmc_runtime_resume(struct device *dev)
{
	struct tmc_drvdata *drvdata = dev_get_drvdata(dev);

	if (drvdata && !IS_ERR_OR_NULL(drvdata->pclk))
		clk_prepare_enable(drvdata->pclk);
	return 0;
}
#endif

static const struct dev_pm_ops tmc_dev_pm_ops = {
	SET_RUNTIME_PM_OPS(tmc_runtime_suspend, tmc_runtime_resume, NULL)
};

#ifdef CONFIG_ACPI
static const struct acpi_device_id tmc_acpi_ids[] = {
	{"ARMHC501", 0, 0, 0}, /* ARM CoreSight ETR */
	{"ARMHC97C", 0, 0, 0}, /* ARM CoreSight SoC-400 TMC, SoC-600 ETF/ETB */
	{},
};
MODULE_DEVICE_TABLE(acpi, tmc_acpi_ids);
#endif

static struct platform_driver tmc_platform_driver = {
	.probe	= tmc_platform_probe,
	.remove = tmc_platform_remove,
	.driver	= {
		.name			= "coresight-tmc-platform",
		.acpi_match_table	= ACPI_PTR(tmc_acpi_ids),
		.suppress_bind_attrs	= true,
		.pm			= &tmc_dev_pm_ops,
	},
};

static int __init tmc_init(void)
{
	return coresight_init_driver("tmc", &tmc_driver, &tmc_platform_driver);
}

static void __exit tmc_exit(void)
{
	coresight_remove_driver(&tmc_driver, &tmc_platform_driver);
}
module_init(tmc_init);
module_exit(tmc_exit);

MODULE_AUTHOR("Pratik Patel <pratikp@codeaurora.org>");
MODULE_DESCRIPTION("Arm CoreSight Trace Memory Controller driver");
MODULE_LICENSE("GPL v2");
