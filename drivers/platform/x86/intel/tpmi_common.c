// SPDX-License-Identifier: GPL-2.0-only
/*
 * Enumeration independent part of the Intel TPMI driver
 *
 * Copyright (c) 2026, Intel Corporation.
 * All Rights Reserved.
 *
 * The TPMI (Topology Aware Register and PM Capsule Interface) provides a
 * flexible, extendable and PCIe enumerable MMIO interface for PM features.
 *
 * For example Intel RAPL (Running Average Power Limit) provides a MMIO
 * interface using TPMI. This has advantage over traditional MSR
 * (Model Specific Register) interface, where a thread needs to be scheduled
 * on the target CPU to read or write. Also the RAPL features vary between
 * CPU models, and hence lot of model specific code. Here TPMI provides an
 * architectural interface by providing hierarchical tables and fields,
 * which will not need any model specific implementation.
 *
 * The TPMI specification defines a PFS (PM Feature Structure) table.
 * This table is present in the TPMI MMIO region. Each TPMI PM feature
 * has one entry in the PFS with a unique TPMI ID and its access details.
 *
 * The names of the devices created for the PM features start with the
 * "intel_vsec.tpmi-" prefix which is followed by a specific name of the
 * given PM feature (for example, "intel_vsec.tpmi-rapl.0").
 *
 * The device nodes are create by using interface "intel_vsec_add_aux()"
 * provided by the Intel VSEC driver.
 */

#include <linux/align.h>
#include <linux/array_size.h>
#include <linux/auxiliary_bus.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/compiler_attributes.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/idr.h>
#include <linux/intel_tpmi.h>
#include <linux/intel_vsec.h>
#include <linux/iopoll.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/pci.h>
#include <linux/security.h>
#include <linux/seq_file.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/string_helpers.h>
#include <linux/time64.h>
#include <linux/types.h>

#include "tpmi_common.h"

/**
 * struct intel_tpmi_pfs_entry - TPMI PM Feature Structure (PFS) entry
 * @tpmi_id:	TPMI feature identifier (what the feature is and its data format).
 * @num_entries: Number of feature interface instances present in the PFS.
 *		 This represents the maximum number of Power domains in the SoC.
 * @entry_size:	Interface instance entry size in 32-bit words.
 * @cap_offset:	Offset from the PM_Features base address to the base of the PM VSEC
 *		register bank in KB.
 * @attribute:	Feature attribute: 0=BIOS. 1=OS. 2-3=Reserved.
 * @reserved:	Bits for use in the future.
 *
 * Represents one TPMI feature entry data in the PFS retrieved as is
 * from the hardware.
 */
struct intel_tpmi_pfs_entry {
	u64 tpmi_id:8;
	u64 num_entries:8;
	u64 entry_size:16;
	u64 cap_offset:16;
	u64 attribute:2;
	u64 reserved:14;
} __packed;

/**
 * struct intel_tpmi_pm_feature - TPMI PM Feature information for a TPMI ID
 * @pfs_header:	PFS header retireved from the hardware.
 * @vsec_offset: Starting MMIO address for this feature in bytes. Essentially
 *		 this offset = "Address" from VSEC header + PFS Capability
 *		 offset for this feature entry.
 *
 * Represents TPMI instance information for one TPMI ID.
 */
struct intel_tpmi_pm_feature {
	struct intel_tpmi_pfs_entry pfs_header;
	u64 vsec_offset;
};

/**
 * struct tpmi_info_header - CPU package ID to PCI device mapping information
 * @fn:		PCI function number
 * @dev:	PCI device number
 * @bus:	PCI bus number
 * @pkg:	CPU Package id
 * @segment:	PCI segment id
 * @partition:	Package Partition id
 * @cdie_mask:	Bitmap of compute dies in the current partition
 * @reserved:	Reserved for future use
 * @lock:	When set to 1 the register is locked and becomes read-only
 *		until next reset. Not for use by the OS driver.
 *
 * The structure to read hardware provided mapping information.
 */
struct tpmi_info_header {
	u64 fn:3;
	u64 dev:5;
	u64 bus:8;
	u64 pkg:8;
	u64 segment:8;
	u64 partition:2;
	u64 cdie_mask:16;
	u64 reserved:13;
	u64 lock:1;
} __packed;

/**
 * struct tpmi_feature_state - Structure to read hardware state of a feature
 * @enabled:	Enable state of a feature, 1: enabled, 0: disabled
 * @reserved_1:	Reserved for future use
 * @write_blocked: Writes are blocked means all write operations are ignored
 * @read_blocked: Reads are blocked means will read 0xFFs
 * @pcs_select:	Interface used by out of band software, not used in OS
 * @reserved_2:	Reserved for future use
 * @id:		TPMI ID of the feature
 * @reserved_3:	Reserved for future use
 * @locked:	When set to 1, OS can't change this register.
 *
 * The structure is used to read hardware state of a TPMI feature. This
 * information is used for debug and restricting operations for this feature.
 */
struct tpmi_feature_state {
	u32 enabled:1;
	u32 reserved_1:3;
	u32 write_blocked:1;
	u32 read_blocked:1;
	u32 pcs_select:1;
	u32 reserved_2:1;
	u32 id:8;
	u32 reserved_3:15;
	u32 locked:1;
} __packed;

/*
 * Return the size of a single feature interface instance in bytes. The
 * hardware reports it in u32 units. The value comes from trusted hardware,
 * but is still worth verifying on pre silicon platforms, so return 0 when
 * it is out of range.
 */
static u32 tpmi_get_single_entry_size(const struct intel_tpmi_pm_feature *pfs)
{
	if (pfs->pfs_header.entry_size > SZ_1K)
		return 0;

	return pfs->pfs_header.entry_size * sizeof(u32);
}

/* Used during auxbus device creation */
static DEFINE_IDA(intel_vsec_tpmi_ida);

static BLOCKING_NOTIFIER_HEAD(tpmi_notify_list);

int tpmi_register_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&tpmi_notify_list, nb);
}
EXPORT_SYMBOL_NS_GPL(tpmi_register_notifier, "INTEL_TPMI");

int tpmi_unregister_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&tpmi_notify_list, nb);
}
EXPORT_SYMBOL_NS_GPL(tpmi_unregister_notifier, "INTEL_TPMI");

struct oobmsm_plat_info *tpmi_get_platform_data(struct auxiliary_device *auxdev)
{
	struct intel_vsec_device *vsec_dev = auxdev_to_ivdev(auxdev);

	return vsec_dev->priv_data;
}
EXPORT_SYMBOL_NS_GPL(tpmi_get_platform_data, "INTEL_TPMI");

int tpmi_get_resource_count(struct auxiliary_device *auxdev)
{
	struct intel_vsec_device *vsec_dev = auxdev_to_ivdev(auxdev);

	if (vsec_dev)
		return vsec_dev->num_resources;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(tpmi_get_resource_count, "INTEL_TPMI");

struct resource *tpmi_get_resource_at_index(struct auxiliary_device *auxdev, int index)
{
	struct intel_vsec_device *vsec_dev = auxdev_to_ivdev(auxdev);

	if (vsec_dev && index < vsec_dev->num_resources)
		return &vsec_dev->resource[index];

	return NULL;
}
EXPORT_SYMBOL_NS_GPL(tpmi_get_resource_at_index, "INTEL_TPMI");

/* TPMI Control Interface */

#define TPMI_CONTROL_STATUS_OFFSET	0x00
#define TPMI_COMMAND_OFFSET		0x08
#define TMPI_CONTROL_DATA_VAL_OFFSET	0x0c

/*
 * Spec is calling for max 1 seconds to get ownership at the worst
 * case. Read at 10 ms timeouts and repeat up to 1 second.
 */
#define TPMI_CONTROL_TIMEOUT_US		(10 * USEC_PER_MSEC)
#define TPMI_CONTROL_TIMEOUT_MAX_US	(1 * USEC_PER_SEC)

#define TPMI_RB_TIMEOUT_US		(10 * USEC_PER_MSEC)
#define TPMI_RB_TIMEOUT_MAX_US		USEC_PER_SEC

/* TPMI Control status register defines */

#define TPMI_CONTROL_STATUS_RB		BIT_ULL(0)

#define TPMI_CONTROL_STATUS_OWNER	GENMASK_ULL(5, 4)
#define TPMI_OWNER_NONE			0
#define TPMI_OWNER_IN_BAND		1

#define TPMI_CONTROL_STATUS_CPL		BIT_ULL(6)
#define TPMI_CONTROL_STATUS_RESULT	GENMASK_ULL(15, 8)
#define TPMI_CONTROL_STATUS_LEN		GENMASK_ULL(31, 16)

#define TPMI_CMD_PKT_LEN		2
#define TPMI_CMD_STATUS_SUCCESS		0x40

/* TPMI command data registers */
#define TMPI_CONTROL_DATA_CMD		GENMASK_ULL(7, 0)
#define TPMI_CONTROL_DATA_VAL_FEATURE	GENMASK_ULL(48, 40)

/* Command to send via control interface */
#define TPMI_CONTROL_GET_STATE_CMD	0x10

#define TPMI_CONTROL_CMD_MASK		GENMASK_ULL(48, 40)

#define TPMI_CMD_LEN_MASK		GENMASK_ULL(18, 16)

/* Mutex to complete get feature status without interruption */
static DEFINE_MUTEX(tpmi_dev_lock);

static int tpmi_wait_for_owner(struct intel_tpmi_info *tpmi_info, u8 owner)
{
	u64 control;

	return readq_poll_timeout(tpmi_info->tpmi_control_mem + TPMI_CONTROL_STATUS_OFFSET,
				  control, owner == FIELD_GET(TPMI_CONTROL_STATUS_OWNER, control),
				  TPMI_CONTROL_TIMEOUT_US, TPMI_CONTROL_TIMEOUT_MAX_US);
}

static int tpmi_read_feature_status(struct intel_tpmi_info *tpmi_info, int feature_id,
				    struct tpmi_feature_state *feature_state)
{
	u64 control, data;
	int ret;

	if (!tpmi_info->tpmi_control_mem)
		return -EFAULT;

	guard(mutex)(&tpmi_dev_lock);

	/* Wait for owner bit set to 0 (none) */
	ret = tpmi_wait_for_owner(tpmi_info, TPMI_OWNER_NONE);
	if (ret)
		return ret;

	/*
	 * Set command id to 0x10 for TPMI_GET_STATE, with the feature id 32
	 * bits into the DATA offset, +8 for the feature_id field.
	 */
	data = FIELD_PREP(TMPI_CONTROL_DATA_CMD, TPMI_CONTROL_GET_STATE_CMD) |
	       FIELD_PREP(TPMI_CONTROL_DATA_VAL_FEATURE, feature_id);

	/* Write at command offset for qword access */
	writeq(data, tpmi_info->tpmi_control_mem + TPMI_COMMAND_OFFSET);

	/* Wait for owner bit set to in-band */
	ret = tpmi_wait_for_owner(tpmi_info, TPMI_OWNER_IN_BAND);
	if (ret)
		return ret;

	/* Set Run Busy and packet length of 2 dwords */
	control = TPMI_CONTROL_STATUS_RB |
		  FIELD_PREP(TPMI_CONTROL_STATUS_LEN, TPMI_CMD_PKT_LEN);

	/* Write at status offset for qword access */
	writeq(control, tpmi_info->tpmi_control_mem + TPMI_CONTROL_STATUS_OFFSET);

	/* Wait for Run Busy clear */
	ret = readq_poll_timeout(tpmi_info->tpmi_control_mem + TPMI_CONTROL_STATUS_OFFSET,
				 control, !(control & TPMI_CONTROL_STATUS_RB),
				 TPMI_RB_TIMEOUT_US, TPMI_RB_TIMEOUT_MAX_US);
	if (ret)
		goto done_proc;

	control = FIELD_GET(TPMI_CONTROL_STATUS_RESULT, control);
	if (control != TPMI_CMD_STATUS_SUCCESS) {
		ret = -EBUSY;
		goto done_proc;
	}

	/* Response is ready */
	memcpy_fromio(feature_state, tpmi_info->tpmi_control_mem + TMPI_CONTROL_DATA_VAL_OFFSET,
		      sizeof(*feature_state));

	ret = 0;

done_proc:
	/* Set CPL "completion" bit */
	writeq(TPMI_CONTROL_STATUS_CPL, tpmi_info->tpmi_control_mem + TPMI_CONTROL_STATUS_OFFSET);

	return ret;
}

int tpmi_get_feature_status(struct auxiliary_device *auxdev,
			    int feature_id, bool *read_blocked, bool *write_blocked)
{
	struct intel_tpmi_info *tpmi_info = dev_get_drvdata(auxdev->dev.parent);
	struct tpmi_feature_state feature_state;
	int ret;

	ret = tpmi_read_feature_status(tpmi_info, feature_id, &feature_state);
	if (ret)
		return ret;

	*read_blocked = feature_state.read_blocked;
	*write_blocked = feature_state.write_blocked;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(tpmi_get_feature_status, "INTEL_TPMI");

struct dentry *tpmi_get_debugfs_dir(struct auxiliary_device *auxdev)
{
	struct intel_tpmi_info *tpmi_info = dev_get_drvdata(auxdev->dev.parent);

	return tpmi_info->dbgfs_dir;
}
EXPORT_SYMBOL_NS_GPL(tpmi_get_debugfs_dir, "INTEL_TPMI");

static int tpmi_pfs_dbg_show(struct seq_file *s, void *unused)
{
	struct intel_tpmi_info *tpmi_info = s->private;
	int locked, disabled, read_blocked, write_blocked;
	struct tpmi_feature_state feature_state;
	struct intel_tpmi_pm_feature *pfs;
	int ret;

	seq_printf(s, "tpmi PFS start offset 0x:%llx\n", tpmi_info->pfs_start);
	seq_puts(s, "tpmi_id\t\tentries\t\tsize\t\tcap_offset\tattribute\tvsec_offset\tlocked\tdisabled\tread_blocked\twrite_blocked\n");
	for (int i = 0; i < tpmi_info->feature_count; i++) {
		pfs = &tpmi_info->tpmi_features[i];
		ret = tpmi_read_feature_status(tpmi_info, pfs->pfs_header.tpmi_id, &feature_state);
		if (ret) {
			locked = 'U';
			disabled = 'U';
			read_blocked = 'U';
			write_blocked = 'U';
		} else {
			disabled = feature_state.enabled ? 'N' : 'Y';
			locked = feature_state.locked ? 'Y' : 'N';
			read_blocked = feature_state.read_blocked ? 'Y' : 'N';
			write_blocked = feature_state.write_blocked ? 'Y' : 'N';
		}
		seq_printf(s, "0x%02x\t\t0x%02x\t\t0x%04x\t\t0x%04x\t\t0x%02x\t\t0x%016llx\t%c\t%c\t\t%c\t\t%c\n",
			   pfs->pfs_header.tpmi_id, pfs->pfs_header.num_entries,
			   pfs->pfs_header.entry_size, pfs->pfs_header.cap_offset,
			   pfs->pfs_header.attribute, pfs->vsec_offset, locked, disabled,
			   read_blocked, write_blocked);
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(tpmi_pfs_dbg);

#define MEM_DUMP_COLUMN_COUNT	8

static int tpmi_mem_dump_show(struct seq_file *s, void *unused)
{
	size_t row_size = MEM_DUMP_COLUMN_COUNT * sizeof(u32);
	struct intel_tpmi_pm_feature *pfs = s->private;
	void __iomem *mem;
	u32 size;
	u64 off;

	size = tpmi_get_single_entry_size(pfs);
	if (!size)
		return -EIO;

	u8 *buffer __free(kfree) = kmalloc(size, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	off = pfs->vsec_offset;

	guard(mutex)(&tpmi_dev_lock);

	for (int count = 0; count < pfs->pfs_header.num_entries; count++) {
		seq_printf(s, "TPMI Instance:%d offset:0x%llx\n", count, off);

		mem = ioremap(off, size);
		if (!mem)
			return -ENOMEM;

		memcpy_fromio(buffer, mem, size);

		seq_hex_dump(s, " ", DUMP_PREFIX_OFFSET, row_size, sizeof(u32), buffer, size,
			     false);

		iounmap(mem);

		off += size;
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(tpmi_mem_dump);

static ssize_t mem_write_write(struct file *file, const char __user *userbuf, size_t len,
			       loff_t *ppos)
{
	struct seq_file *m = file->private_data;
	struct intel_tpmi_pm_feature *pfs = m->private;
	u32 addr, value, punit, size;
	u32 num_elems;
	void __iomem *mem;
	int ret;

	size = tpmi_get_single_entry_size(pfs);
	if (!size)
		return -EIO;

	u32 *array __free(kfree) = NULL;
	ret = parse_int_array_user(userbuf, len, (int **)&array);
	if (ret < 0)
		return ret;

	num_elems = *array;
	if (num_elems != 3)
		return -EINVAL;

	punit = array[1];
	addr = array[2];
	value = array[3];

	if (!IS_ALIGNED(addr, sizeof(u32)))
		return -EINVAL;

	if (punit >= pfs->pfs_header.num_entries)
		return -EINVAL;

	if (addr >= size)
		return -EINVAL;

	guard(mutex)(&tpmi_dev_lock);

	mem = ioremap(pfs->vsec_offset + punit * size, size);
	if (!mem)
		return -ENOMEM;

	writel(value, mem + addr);

	iounmap(mem);

	return len;
}

static int mem_write_show(struct seq_file *s, void *unused)
{
	return 0;
}
DEFINE_SHOW_STORE_ATTRIBUTE(mem_write);

static void tpmi_dbgfs_register(struct intel_tpmi_info *tpmi_info)
{
	char name[64];

	scnprintf(name, sizeof(name), "tpmi-%s", dev_name(tpmi_info->parent));
	tpmi_info->dbgfs_dir = debugfs_create_dir(name, NULL);

	debugfs_create_file("pfs_dump", 0444, tpmi_info->dbgfs_dir, tpmi_info, &tpmi_pfs_dbg_fops);

	for (int i = 0; i < tpmi_info->feature_count; i++) {
		struct intel_tpmi_pm_feature *pfs;
		struct dentry *dir;

		pfs = &tpmi_info->tpmi_features[i];
		scnprintf(name, sizeof(name), "tpmi-id-%02x", pfs->pfs_header.tpmi_id);
		dir = debugfs_create_dir(name, tpmi_info->dbgfs_dir);

		debugfs_create_file("mem_dump", 0444, dir, pfs, &tpmi_mem_dump_fops);
		debugfs_create_file("mem_write", 0644, dir, pfs, &mem_write_fops);
	}
}

static void tpmi_set_control_base(struct intel_tpmi_info *tpmi_info,
				  struct intel_tpmi_pm_feature *pfs)
{
	void __iomem *mem;
	u32 size;

	size = tpmi_get_single_entry_size(pfs);
	if (!size)
		return;

	mem = devm_ioremap(tpmi_info->tpmi_dev, pfs->vsec_offset, size);
	if (!mem)
		return;

	/* mem is pointing to TPMI CONTROL base */
	tpmi_info->tpmi_control_mem = mem;
}

/*
 * The TPMI IDs are sparse, so the unused entries are left NULL and are
 * rejected by the caller like any other unsupported feature.
 */
static const char * const intel_tpmi_names[] = {
	[TPMI_ID_RAPL]		= "tpmi-rapl",
	[TPMI_ID_PEM]		= "tpmi-pem",
	[TPMI_ID_UNCORE]	= "tpmi-uncore",
	[TPMI_ID_SST]		= "tpmi-sst",
	[TPMI_ID_PLR]		= "tpmi-plr",
};

static const char *intel_tpmi_name(enum intel_tpmi_id id)
{
	if (id >= ARRAY_SIZE(intel_tpmi_names))
		return NULL;

	return intel_tpmi_names[id];
}

static int tpmi_create_device(struct intel_tpmi_info *tpmi_info,
			      struct intel_tpmi_pm_feature *pfs,
			      u64 pfs_start)
{
	struct intel_vsec_device *feature_vsec_dev;
	struct tpmi_feature_state feature_state;
	struct resource *res;
	const char *name;
	int ret;

	ret = tpmi_read_feature_status(tpmi_info, pfs->pfs_header.tpmi_id, &feature_state);
	if (ret)
		return ret;

	/*
	 * If not enabled, continue to look at other features in the PFS, so return -EOPNOTSUPP.
	 * This will not cause failure of loading of this driver.
	 */
	if (!feature_state.enabled)
		return -EOPNOTSUPP;

	name = intel_tpmi_name(pfs->pfs_header.tpmi_id);
	if (!name)
		return -EOPNOTSUPP;

	feature_vsec_dev = kzalloc_flex(*feature_vsec_dev, resource, pfs->pfs_header.num_entries);
	if (!feature_vsec_dev)
		return -ENOMEM;

	feature_vsec_dev->num_resources = pfs->pfs_header.num_entries;
	res = feature_vsec_dev->resource;

	for (int i = 0; i < pfs->pfs_header.num_entries; i++) {
		u64 entry_size_bytes = pfs->pfs_header.entry_size * sizeof(u32);

		res[i] = DEFINE_RES_MEM(pfs->vsec_offset + entry_size_bytes * i, entry_size_bytes);
	}

	feature_vsec_dev->dev = tpmi_info->parent;
	feature_vsec_dev->priv_data = &tpmi_info->plat_info;
	feature_vsec_dev->priv_data_size = sizeof(tpmi_info->plat_info);
	feature_vsec_dev->ida = &intel_vsec_tpmi_ida;

	/*
	 * intel_vsec_add_aux() is resource managed, no explicit
	 * delete is required on error or on module unload.
	 * feature_vsec_dev and res memory are also freed as part of
	 * device deletion.
	 *
	 * "name" must outlive the auxiliary device, as the auxiliary bus
	 * stores the pointer rather than a copy of the string.
	 */
	return intel_vsec_add_aux(tpmi_info->tpmi_dev, feature_vsec_dev, name);
}

/*
 * Create a device for every feature in the PFS that has one.
 *
 * A feature that is not supported is reported as -EOPNOTSUPP and skipped.
 * Any other error fails the driver load. intel_vsec_add_aux() is resource
 * managed, so the devices created before such a failure need no clean up
 * here.
 */
static int tpmi_create_devices(struct intel_tpmi_info *tpmi_info)
{
	int ret;

	for (int i = 0; i < tpmi_info->feature_count; i++) {
		ret = tpmi_create_device(tpmi_info, &tpmi_info->tpmi_features[i],
					 tpmi_info->pfs_start);
		if (ret == -EOPNOTSUPP)
			continue;
		if (ret)
			return ret;
	}

	return 0;
}

#define TPMI_INFO_BUS_INFO_OFFSET	0x08
#define TPMI_INFO_MAJOR_VERSION		0x00
#define TPMI_INFO_MINOR_VERSION		0x02

static int tpmi_process_info(struct intel_tpmi_info *tpmi_info,
			     struct intel_tpmi_pm_feature *pfs)
{
	struct tpmi_info_header header;
	void __iomem *info_mem;
	u64 feature_header;
	int ret = 0;

	info_mem = ioremap(pfs->vsec_offset, pfs->pfs_header.entry_size * sizeof(u32));
	if (!info_mem)
		return -ENOMEM;

	feature_header = readq(info_mem);
	if (TPMI_MAJOR_VERSION(feature_header) != TPMI_INFO_MAJOR_VERSION) {
		ret = -ENODEV;
		goto error_info_header;
	}

	memcpy_fromio(&header, info_mem + TPMI_INFO_BUS_INFO_OFFSET, sizeof(header));

	tpmi_info->plat_info.package_id = header.pkg;
	tpmi_info->plat_info.bus_number = header.bus;
	tpmi_info->plat_info.device_number = header.dev;
	tpmi_info->plat_info.function_number = header.fn;

	if (TPMI_MINOR_VERSION(feature_header) >= TPMI_INFO_MINOR_VERSION) {
		tpmi_info->plat_info.cdie_mask = header.cdie_mask;
		tpmi_info->plat_info.partition = header.partition;
		tpmi_info->plat_info.segment = header.segment;
	}

error_info_header:
	iounmap(info_mem);

	return ret;
}

static int tpmi_fetch_pfs_header(struct intel_tpmi_pm_feature *pfs, u64 start, int size)
{
	void __iomem *pfs_mem;

	pfs_mem = ioremap(start, size);
	if (!pfs_mem)
		return -ENOMEM;

	memcpy_fromio(&pfs->pfs_header, pfs_mem, sizeof(pfs->pfs_header));

	iounmap(pfs_mem);

	return 0;
}

#define TPMI_CAP_OFFSET_UNIT	1024

/**
 * intel_tpmi_init() - Bring up a TPMI instance
 * @tpmi_info:	Caller allocated and zeroed state of the TPMI instance
 * @tpmi_dev:	Device representing the TPMI instance. Owns the devm
 *		allocations and parents the TPMI feature devices
 * @parent:	Device the TPMI instance was enumerated from
 * @resource:	Array of @feature_count MMIO resources, one per PFS entry
 * @feature_count: Number of entries in @resource
 *
 * Walk the PFS of the TPMI instance described by @resource, register the
 * debugfs interface and create a device for each supported PM feature.
 *
 * Return: 0 on success, negative errno otherwise.
 */
int intel_tpmi_init(struct intel_tpmi_info *tpmi_info, struct device *tpmi_dev,
		    struct device *parent, struct resource *resource, int feature_count)
{
	struct pci_dev *pci_dev = dev_is_pci(parent) ? to_pci_dev(parent) : NULL;
	u64 pfs_start = 0;
	int ret;

	tpmi_info->tpmi_dev = tpmi_dev;
	tpmi_info->parent = parent;
	tpmi_info->feature_count = feature_count;
	tpmi_info->resource = resource;

	/*
	 * Seed the bus number from the enumerating device. It is only a
	 * fallback, tpmi_process_info() replaces it with the value the
	 * TPMI_INFO feature reports.
	 */
	if (pci_dev)
		tpmi_info->plat_info.bus_number = pci_dev->bus->number;

	tpmi_info->tpmi_features = devm_kcalloc(tpmi_info->tpmi_dev, tpmi_info->feature_count,
						sizeof(*tpmi_info->tpmi_features),
						GFP_KERNEL);
	if (!tpmi_info->tpmi_features)
		return -ENOMEM;

	for (int i = 0; i < tpmi_info->feature_count; i++) {
		struct intel_tpmi_pm_feature *pfs;
		struct resource *res;
		u64 res_start;
		int size, ret;

		pfs = &tpmi_info->tpmi_features[i];

		res = &tpmi_info->resource[i];
		if (!res)
			continue;

		res_start = res->start;
		size = resource_size(res);
		if (size < 0)
			continue;

		ret = tpmi_fetch_pfs_header(pfs, res_start, size);
		if (ret)
			continue;

		if (!pfs_start)
			pfs_start = res_start;

		pfs->vsec_offset = pfs_start + pfs->pfs_header.cap_offset * TPMI_CAP_OFFSET_UNIT;

		/*
		 * Process TPMI_INFO to get PCI device to CPU package ID.
		 * Device nodes for TPMI features are not created in this
		 * for loop. So, the mapping information will be available
		 * when actual device nodes created outside this
		 * loop via tpmi_create_devices().
		 */
		if (pfs->pfs_header.tpmi_id == TPMI_INFO_ID) {
			ret = tpmi_process_info(tpmi_info, pfs);
			if (ret)
				return ret;

			/*
			 * The mapping is looked up by pci_dev, so it is only
			 * meaningful for PCI enumerated TPMI instances.
			 */
			if (pci_dev) {
				ret = intel_vsec_set_mapping(&tpmi_info->plat_info,
							     &pci_dev->dev);
				if (ret)
					return ret;
			}
		}

		if (pfs->pfs_header.tpmi_id == TPMI_CONTROL_ID)
			tpmi_set_control_base(tpmi_info, pfs);
	}

	tpmi_info->pfs_start = pfs_start;

	dev_set_drvdata(tpmi_info->tpmi_dev, tpmi_info);

	/*
	 * Allow debugfs when security policy allows. Everything this debugfs
	 * interface provides, can also be done via /dev/mem access. If
	 * /dev/mem interface is locked, don't allow debugfs to present any
	 * information. Also check for CAP_SYS_RAWIO as /dev/mem interface.
	 */
	if (!security_locked_down(LOCKDOWN_DEV_MEM) && capable(CAP_SYS_RAWIO))
		tpmi_dbgfs_register(tpmi_info);

	ret = tpmi_create_devices(tpmi_info);
	if (ret) {
		debugfs_remove_recursive(tpmi_info->dbgfs_dir);
		return ret;
	}

	blocking_notifier_call_chain(&tpmi_notify_list, TPMI_CORE_INIT, tpmi_info->tpmi_dev);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(intel_tpmi_init, "INTEL_TPMI");

/**
 * intel_tpmi_deinit() - Tear a TPMI instance down
 * @tpmi_info:	State of the TPMI instance, as passed to intel_tpmi_init()
 *
 * The TPMI feature devices and the devm allocations are released by the
 * device core when the TPMI device goes away.
 */
void intel_tpmi_deinit(struct intel_tpmi_info *tpmi_info)
{
	blocking_notifier_call_chain(&tpmi_notify_list, TPMI_CORE_EXIT, tpmi_info->tpmi_dev);

	debugfs_remove_recursive(tpmi_info->dbgfs_dir);
}
EXPORT_SYMBOL_NS_GPL(intel_tpmi_deinit, "INTEL_TPMI");

MODULE_IMPORT_NS("INTEL_VSEC");
MODULE_DESCRIPTION("Intel TPMI core support");
MODULE_LICENSE("GPL");
