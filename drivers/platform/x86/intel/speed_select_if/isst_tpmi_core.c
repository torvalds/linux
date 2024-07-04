// SPDX-License-Identifier: GPL-2.0-only
/*
 * isst_tpmi.c: SST TPMI interface core
 *
 * Copyright (c) 2023, Intel Corporation.
 * All Rights Reserved.
 *
 * This information will be useful to understand flows:
 * In the current generation of platforms, TPMI is supported via OOB
 * PCI device. This PCI device has one instance per CPU package.
 * There is a unique TPMI ID for SST. Each TPMI ID also has multiple
 * entries, representing per power domain information.
 *
 * There is one dev file for complete SST information and control same as the
 * prior generation of hardware. User spaces don't need to know how the
 * information is presented by the hardware. The TPMI core module implements
 * the hardware mapping.
 */

#define dev_fmt(fmt) "tpmi_sst: " fmt

#include <linux/auxiliary_bus.h>
#include <linux/delay.h>
#include <linux/intel_tpmi.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <uapi/linux/isst_if.h>

#include "isst_tpmi_core.h"
#include "isst_if_common.h"

/* Supported SST hardware version by this driver */
#define ISST_MAJOR_VERSION	0
#define ISST_MINOR_VERSION	1

/*
 * Used to indicate if value read from MMIO needs to get multiplied
 * to get to a standard unit or not.
 */
#define SST_MUL_FACTOR_NONE    1

/* Define 100 as a scaling factor frequency ratio to frequency conversion */
#define SST_MUL_FACTOR_FREQ    100

/* All SST regs are 64 bit size */
#define SST_REG_SIZE   8

/**
 * struct sst_header -	SST main header
 * @interface_version:	Version number for this interface
 * @cap_mask:		Bitmask of the supported sub features. 1=the sub feature is enabled.
 *			0=disabled.
 *			Bit[8]= SST_CP enable (1), disable (0)
 *			bit[9]= SST_PP enable (1), disable (0)
 *			other bits are reserved for future use
 * @cp_offset:		Qword (8 bytes) offset to the SST_CP register bank
 * @pp_offset:		Qword (8 bytes) offset to the SST_PP register bank
 * @reserved:		Reserved for future use
 *
 * This register allows SW to discover SST capability and the offsets to SST-CP
 * and SST-PP register banks.
 */
struct sst_header {
	u8 interface_version;
	u8 cap_mask;
	u8 cp_offset;
	u8 pp_offset;
	u32 reserved;
} __packed;

/**
 * struct cp_header -	SST-CP (core-power) header
 * @feature_id:		0=SST-CP, 1=SST-PP, 2=SST-BF, 3=SST-TF
 * @feature_rev:	Interface Version number for this SST feature
 * @ratio_unit:		Frequency ratio unit. 00: 100MHz. All others are reserved
 * @reserved:		Reserved for future use
 *
 * This structure is used store SST-CP header. This is packed to the same
 * format as defined in the specifications.
 */
struct cp_header {
	u64 feature_id :4;
	u64 feature_rev :8;
	u64 ratio_unit :2;
	u64 reserved :50;
} __packed;

/**
 * struct pp_header -	SST-PP (Perf profile) header
 * @feature_id:		0=SST-CP, 1=SST-PP, 2=SST-BF, 3=SST-TF
 * @feature_rev:	Interface Version number for this SST feature
 * @level_en_mask:	SST-PP level enable/disable fuse mask
 * @allowed_level_mask:	Allowed level mask used for dynamic config level switching
 * @reserved0:		Reserved for future use
 * @ratio_unit:		Frequency ratio unit. 00: 100MHz. All others are reserved
 * @block_size:		Size of PP block in Qword unit (8 bytes)
 * @dynamic_switch:	If set (1), dynamic switching of SST PP is supported
 * @memory_ratio_unit:	Memory Controller frequency ratio unit. 00: 100MHz, others reserved
 * @reserved1:		Reserved for future use
 *
 * This structure is used store SST-PP header. This is packed to the same
 * format as defined in the specifications.
 */
struct pp_header {
	u64 feature_id :4;
	u64 feature_rev :8;
	u64 level_en_mask :8;
	u64 allowed_level_mask :8;
	u64 reserved0 :4;
	u64 ratio_unit :2;
	u64 block_size :8;
	u64 dynamic_switch :1;
	u64 memory_ratio_unit :2;
	u64 reserved1 :19;
} __packed;

/**
 * struct feature_offset -	Offsets to SST-PP features
 * @pp_offset:		Qword offset within PP level for the SST_PP register bank
 * @bf_offset:		Qword offset within PP level for the SST_BF register bank
 * @tf_offset:		Qword offset within PP level for the SST_TF register bank
 * @reserved:		Reserved for future use
 *
 * This structure is used store offsets for SST features in the register bank.
 * This is packed to the same format as defined in the specifications.
 */
struct feature_offset {
	u64 pp_offset :8;
	u64 bf_offset :8;
	u64 tf_offset :8;
	u64 reserved :40;
} __packed;

/**
 * struct levels_offset -	Offsets to each SST PP level
 * @sst_pp_level0_offset:	Qword offset to the register block of PP level 0
 * @sst_pp_level1_offset:	Qword offset to the register block of PP level 1
 * @sst_pp_level2_offset:	Qword offset to the register block of PP level 2
 * @sst_pp_level3_offset:	Qword offset to the register block of PP level 3
 * @sst_pp_level4_offset:	Qword offset to the register block of PP level 4
 * @reserved:			Reserved for future use
 *
 * This structure is used store offsets of SST PP levels in the register bank.
 * This is packed to the same format as defined in the specifications.
 */
struct levels_offset {
	u64 sst_pp_level0_offset :8;
	u64 sst_pp_level1_offset :8;
	u64 sst_pp_level2_offset :8;
	u64 sst_pp_level3_offset :8;
	u64 sst_pp_level4_offset :8;
	u64 reserved :24;
} __packed;

/**
 * struct pp_control_offset -	Offsets for SST PP controls
 * @perf_level:		A SST-PP level that SW intends to switch to
 * @perf_level_lock:	SST-PP level select lock. 0 - unlocked. 1 - locked till next reset
 * @resvd0:		Reserved for future use
 * @current_state:	Bit mask to control the enable(1)/disable(0) state of each feature
 *			of the current PP level, bit 0 = BF, bit 1 = TF, bit 2-7 = reserved
 * @reserved:		Reserved for future use
 *
 * This structure is used store offsets of SST PP controls in the register bank.
 * This is packed to the same format as defined in the specifications.
 */
struct pp_control_offset {
	u64 perf_level :3;
	u64 perf_level_lock :1;
	u64 resvd0 :4;
	u64 current_state :8;
	u64 reserved :48;
} __packed;

/**
 * struct pp_status_offset -	Offsets for SST PP status fields
 * @sst_pp_level:	Returns the current SST-PP level
 * @sst_pp_lock:	Returns the lock bit setting of perf_level_lock in pp_control_offset
 * @error_type:		Returns last error of SST-PP level change request. 0: no error,
 *			1: level change not allowed, others: reserved
 * @feature_state:	Bit mask to indicate the enable(1)/disable(0) state of each feature of the
 *			current PP level. bit 0 = BF, bit 1 = TF, bit 2-7 reserved
 * @reserved0:		Reserved for future use
 * @feature_error_type: Returns last error of the specific feature. Three error_type bits per
 *			feature. i.e. ERROR_TYPE[2:0] for BF, ERROR_TYPE[5:3] for TF, etc.
 *			0x0: no error, 0x1: The specific feature is not supported by the hardware.
 *			0x2-0x6: Reserved. 0x7: feature state change is not allowed.
 * @reserved1:		Reserved for future use
 *
 * This structure is used store offsets of SST PP status in the register bank.
 * This is packed to the same format as defined in the specifications.
 */
struct pp_status_offset {
	u64 sst_pp_level :3;
	u64 sst_pp_lock :1;
	u64 error_type :4;
	u64 feature_state :8;
	u64 reserved0 :16;
	u64 feature_error_type : 24;
	u64 reserved1 :8;
} __packed;

/**
 * struct perf_level -	Used to store perf level and mmio offset
 * @mmio_offset:	mmio offset for a perf level
 * @level:		perf level for this offset
 *
 * This structure is used store final mmio offset of each perf level from the
 * SST base mmio offset.
 */
struct perf_level {
	int mmio_offset;
	int level;
};

/**
 * struct tpmi_per_power_domain_info -	Store per power_domain SST info
 * @package_id:		Package id for this power_domain
 * @power_domain_id:	Power domain id, Each entry from the SST-TPMI instance is a power_domain.
 * @max_level:		Max possible PP level possible for this power_domain
 * @ratio_unit:		Ratio unit for converting to MHz
 * @avx_levels:		Number of AVX levels
 * @pp_block_size:	Block size from PP header
 * @sst_header:		Store SST header for this power_domain
 * @cp_header:		Store SST-CP header for this power_domain
 * @pp_header:		Store SST-PP header for this power_domain
 * @perf_levels:	Pointer to each perf level to map level to mmio offset
 * @feature_offsets:	Store feature offsets for each PP-level
 * @control_offset:	Store the control offset for each PP-level
 * @status_offset:	Store the status offset for each PP-level
 * @sst_base:		Mapped SST base IO memory
 * @auxdev:		Auxiliary device instance enumerated this instance
 * @saved_sst_cp_control: Save SST-CP control configuration to store restore for suspend/resume
 * @saved_clos_configs:	Save SST-CP CLOS configuration to store restore for suspend/resume
 * @saved_clos_assocs:	Save SST-CP CLOS association to store restore for suspend/resume
 * @saved_pp_control:	Save SST-PP control information to store restore for suspend/resume
 * @write_blocked:	Write operation is blocked, so can't change SST state
 *
 * This structure is used store complete SST information for a power_domain. This information
 * is used to read/write request for any SST IOCTL. Each physical CPU package can have multiple
 * power_domains. Each power domain describes its own SST information and has its own controls.
 */
struct tpmi_per_power_domain_info {
	int package_id;
	int power_domain_id;
	int max_level;
	int ratio_unit;
	int avx_levels;
	int pp_block_size;
	struct sst_header sst_header;
	struct cp_header cp_header;
	struct pp_header pp_header;
	struct perf_level *perf_levels;
	struct feature_offset feature_offsets;
	struct pp_control_offset control_offset;
	struct pp_status_offset status_offset;
	void __iomem *sst_base;
	struct auxiliary_device *auxdev;
	u64 saved_sst_cp_control;
	u64 saved_clos_configs[4];
	u64 saved_clos_assocs[4];
	u64 saved_pp_control;
	bool write_blocked;
};

/* Supported maximum partitions */
#define SST_MAX_PARTITIONS	2

/**
 * struct tpmi_sst_struct -	Store sst info for a package
 * @package_id:			Package id for this aux device instance
 * @number_of_power_domains:	Number of power_domains pointed by power_domain_info pointer
 * @power_domain_info:		Pointer to power domains information
 * @cdie_mask:			Mask of compute dies present in a partition from hardware.
 *				This mask is not present in the version 1 information header.
 * @io_dies:			Number of IO dies in a partition. This will be 0 for TPMI
 *				version 1 information header.
 * @partition_mask:		Mask of all partitions.
 * @partition_mask_current:	Current partition mask as some may have been unbound.
 *
 * This structure is used store full SST information for a package.
 * Each package has one or multiple OOB PCI devices. Each package can contain multiple
 * power domains.
 */
struct tpmi_sst_struct {
	int package_id;
	struct tpmi_per_power_domain_info *power_domain_info[SST_MAX_PARTITIONS];
	u16 cdie_mask[SST_MAX_PARTITIONS];
	u8 number_of_power_domains[SST_MAX_PARTITIONS];
	u8 io_dies[SST_MAX_PARTITIONS];
	u8 partition_mask;
	u8 partition_mask_current;
};

/**
 * struct tpmi_sst_common_struct -	Store all SST instances
 * @max_index:		Maximum instances currently present
 * @sst_inst:		Pointer to per package instance
 *
 * Stores every SST Package instance.
 */
struct tpmi_sst_common_struct {
	int max_index;
	struct tpmi_sst_struct **sst_inst;
};

/*
 * Each IOCTL request is processed under this lock. Also used to protect
 * registration functions and common data structures.
 */
static DEFINE_MUTEX(isst_tpmi_dev_lock);

/* Usage count to track, number of TPMI SST instances registered to this core. */
static int isst_core_usage_count;

/* Stores complete SST information for every package and power_domain */
static struct tpmi_sst_common_struct isst_common;

#define SST_MAX_AVX_LEVELS	3

#define SST_PP_OFFSET_0		8
#define SST_PP_OFFSET_1		16
#define SST_PP_OFFSET_SIZE	8

static int sst_add_perf_profiles(struct auxiliary_device *auxdev,
				 struct tpmi_per_power_domain_info *pd_info,
				 int levels)
{
	struct device *dev = &auxdev->dev;
	u64 perf_level_offsets;
	int i;

	pd_info->perf_levels = devm_kcalloc(dev, levels, sizeof(struct perf_level), GFP_KERNEL);
	if (!pd_info->perf_levels)
		return 0;

	pd_info->ratio_unit = pd_info->pp_header.ratio_unit;
	pd_info->avx_levels = SST_MAX_AVX_LEVELS;
	pd_info->pp_block_size = pd_info->pp_header.block_size;

	/* Read PP Offset 0: Get feature offset with PP level */
	*((u64 *)&pd_info->feature_offsets) = readq(pd_info->sst_base +
						    pd_info->sst_header.pp_offset +
						    SST_PP_OFFSET_0);

	perf_level_offsets = readq(pd_info->sst_base + pd_info->sst_header.pp_offset +
				   SST_PP_OFFSET_1);

	for (i = 0; i < levels; ++i) {
		u64 offset;

		offset = perf_level_offsets & (0xffULL << (i * SST_PP_OFFSET_SIZE));
		offset >>= (i * 8);
		offset &= 0xff;
		offset *= 8; /* Convert to byte from QWORD offset */
		pd_info->perf_levels[i].mmio_offset = pd_info->sst_header.pp_offset + offset;
	}

	return 0;
}

static int sst_main(struct auxiliary_device *auxdev, struct tpmi_per_power_domain_info *pd_info)
{
	struct device *dev = &auxdev->dev;
	int i, mask, levels;

	*((u64 *)&pd_info->sst_header) = readq(pd_info->sst_base);
	pd_info->sst_header.cp_offset *= 8;
	pd_info->sst_header.pp_offset *= 8;

	if (pd_info->sst_header.interface_version == TPMI_VERSION_INVALID)
		return -ENODEV;

	if (TPMI_MAJOR_VERSION(pd_info->sst_header.interface_version) != ISST_MAJOR_VERSION) {
		dev_err(dev, "SST: Unsupported major version:%lx\n",
			TPMI_MAJOR_VERSION(pd_info->sst_header.interface_version));
		return -ENODEV;
	}

	if (TPMI_MINOR_VERSION(pd_info->sst_header.interface_version) != ISST_MINOR_VERSION)
		dev_info(dev, "SST: Ignore: Unsupported minor version:%lx\n",
			 TPMI_MINOR_VERSION(pd_info->sst_header.interface_version));

	/* Read SST CP Header */
	*((u64 *)&pd_info->cp_header) = readq(pd_info->sst_base + pd_info->sst_header.cp_offset);

	/* Read PP header */
	*((u64 *)&pd_info->pp_header) = readq(pd_info->sst_base + pd_info->sst_header.pp_offset);

	mask = 0x01;
	levels = 0;
	for (i = 0; i < 8; ++i) {
		if (pd_info->pp_header.level_en_mask & mask)
			levels = i;
		mask <<= 1;
	}
	pd_info->max_level = levels;
	sst_add_perf_profiles(auxdev, pd_info, levels + 1);

	return 0;
}

static u8 isst_instance_count(struct tpmi_sst_struct *sst_inst)
{
	u8 i, max_part, count = 0;

	/* Partition mask starts from bit 0 and contains 1s only */
	max_part = hweight8(sst_inst->partition_mask);
	for (i = 0; i < max_part; i++)
		count += sst_inst->number_of_power_domains[i];

	return count;
}

/**
 * map_cdies() - Map user domain ID to compute domain ID
 * @sst_inst: TPMI Instance
 * @id: User domain ID
 * @partition: Resolved partition
 *
 * Helper function to map_partition_power_domain_id() to resolve compute
 * domain ID and partition. Use hardware provided cdie_mask for a partition
 * as is to resolve a compute domain ID.
 *
 * Return: %-EINVAL on error, otherwise mapped domain ID >= 0.
 */
static int map_cdies(struct tpmi_sst_struct *sst_inst, u8 id, u8 *partition)
{
	u8 i, max_part;

	max_part = hweight8(sst_inst->partition_mask);
	for (i = 0; i < max_part; i++) {
		if (!(sst_inst->cdie_mask[i] & BIT(id)))
			continue;

		*partition = i;
		return id - ffs(sst_inst->cdie_mask[i]) + 1;
	}

	return -EINVAL;
}

/**
 * map_partition_power_domain_id() - Map user domain ID to partition domain ID
 * @sst_inst: TPMI Instance
 * @id: User domain ID
 * @partition: Resolved partition
 *
 * In a partitioned system a CPU package has two separate MMIO ranges (Under
 * two PCI devices). But the CPU package compute die/power domain IDs are
 * unique in a package. User space can get compute die/power domain ID from
 * CPUID and MSR 0x54 for a CPU. So, those IDs need to be preserved even if
 * they are present in two different partitions with its own order.
 *
 * For example for command ISST_IF_COUNT_TPMI_INSTANCES, the valid_mask
 * is 111111b for a 4 compute and 2 IO dies system. This is presented as
 * provided by the hardware in a non-partitioned system with the following
 * order:
 *	I1-I0-C3-C2-C1-C0
 * Here: "C": for compute and "I" for IO die.
 * Compute dies are always present first in TPMI instances, as they have
 * to map to the real power domain/die ID of a system. In a non-partitioned
 * system there is no way to identify compute and IO die boundaries from
 * this driver without reading each CPU's mapping.
 *
 * The same order needs to be preserved, even if those compute dies are
 * distributed among multiple partitions. For example:
 * Partition 1 can contain: I1-C1-C0
 * Partition 2 can contain: I2-C3-C2
 *
 * This will require a conversion of user space IDs to the actual index into
 * array of stored power domains for each partition. For the above example
 * this function will return partition and index as follows:
 *
 * =============	=========	=====	========
 * User space ID	Partition	Index	Die type
 * =============	=========	=====	========
 * 0			0		0	Compute
 * 1			0		1	Compute
 * 2			1		0	Compute
 * 3			1		1	Compute
 * 4			0		2	IO
 * 5			1		2	IO
 * =============	=========	=====	========
 *
 * Return: %-EINVAL on error, otherwise mapped domain ID >= 0.
 */
static int map_partition_power_domain_id(struct tpmi_sst_struct *sst_inst, u8 id, u8 *partition)
{
	u8 i, io_start_id, max_part;

	*partition = 0;

	/* If any PCI device for partition is unbound, treat this as failure */
	if (sst_inst->partition_mask != sst_inst->partition_mask_current)
		return -EINVAL;

	max_part = hweight8(sst_inst->partition_mask);

	/* IO Index begin here */
	io_start_id = fls(sst_inst->cdie_mask[max_part - 1]);

	if (id < io_start_id)
		return map_cdies(sst_inst, id, partition);

	for (i = 0; i < max_part; i++) {
		u8 io_id;

		io_id = id - io_start_id;
		if (io_id < sst_inst->io_dies[i]) {
			u8 cdie_range;

			cdie_range = fls(sst_inst->cdie_mask[i]) - ffs(sst_inst->cdie_mask[i]) + 1;
			*partition = i;
			return cdie_range + io_id;
		}
		io_start_id += sst_inst->io_dies[i];
	}

	return -EINVAL;
}

/*
 * Map a package and power_domain id to SST information structure unique for a power_domain.
 * The caller should call under isst_tpmi_dev_lock.
 */
static struct tpmi_per_power_domain_info *get_instance(int pkg_id, int power_domain_id)
{
	struct tpmi_per_power_domain_info *power_domain_info;
	struct tpmi_sst_struct *sst_inst;
	u8 part;

	if (!in_range(pkg_id, 0, topology_max_packages()) || pkg_id > isst_common.max_index)
		return NULL;

	sst_inst = isst_common.sst_inst[pkg_id];
	if (!sst_inst)
		return NULL;

	power_domain_id = map_partition_power_domain_id(sst_inst, power_domain_id, &part);
	if (power_domain_id < 0)
		return NULL;

	power_domain_info = &sst_inst->power_domain_info[part][power_domain_id];

	if (power_domain_info && !power_domain_info->sst_base)
		return NULL;

	return power_domain_info;
}

static bool disable_dynamic_sst_features(void)
{
	u64 value;

	rdmsrl(MSR_PM_ENABLE, value);
	return !(value & 0x1);
}

#define _read_cp_info(name_str, name, offset, start, width, mult_factor)\
{\
	u64 val, mask;\
	\
	val = readq(power_domain_info->sst_base + power_domain_info->sst_header.cp_offset +\
			(offset));\
	mask = GENMASK_ULL((start + width - 1), start);\
	val &= mask; \
	val >>= start;\
	name = (val * mult_factor);\
}

#define _write_cp_info(name_str, name, offset, start, width, div_factor)\
{\
	u64 val, mask;\
	\
	val = readq(power_domain_info->sst_base +\
		    power_domain_info->sst_header.cp_offset + (offset));\
	mask = GENMASK_ULL((start + width - 1), start);\
	val &= ~mask;\
	val |= (name / div_factor) << start;\
	writeq(val, power_domain_info->sst_base + power_domain_info->sst_header.cp_offset +\
		(offset));\
}

#define	SST_CP_CONTROL_OFFSET	8
#define	SST_CP_STATUS_OFFSET	16

#define SST_CP_ENABLE_START		0
#define SST_CP_ENABLE_WIDTH		1

#define SST_CP_PRIORITY_TYPE_START	1
#define SST_CP_PRIORITY_TYPE_WIDTH	1

static long isst_if_core_power_state(void __user *argp)
{
	struct tpmi_per_power_domain_info *power_domain_info;
	struct isst_core_power core_power;

	if (copy_from_user(&core_power, argp, sizeof(core_power)))
		return -EFAULT;

	if (core_power.get_set && disable_dynamic_sst_features())
		return -EFAULT;

	power_domain_info = get_instance(core_power.socket_id, core_power.power_domain_id);
	if (!power_domain_info)
		return -EINVAL;

	if (core_power.get_set) {
		_write_cp_info("cp_enable", core_power.enable, SST_CP_CONTROL_OFFSET,
			       SST_CP_ENABLE_START, SST_CP_ENABLE_WIDTH, SST_MUL_FACTOR_NONE)
		_write_cp_info("cp_prio_type", core_power.priority_type, SST_CP_CONTROL_OFFSET,
			       SST_CP_PRIORITY_TYPE_START, SST_CP_PRIORITY_TYPE_WIDTH,
			       SST_MUL_FACTOR_NONE)
	} else {
		/* get */
		_read_cp_info("cp_enable", core_power.enable, SST_CP_STATUS_OFFSET,
			      SST_CP_ENABLE_START, SST_CP_ENABLE_WIDTH, SST_MUL_FACTOR_NONE)
		_read_cp_info("cp_prio_type", core_power.priority_type, SST_CP_STATUS_OFFSET,
			      SST_CP_PRIORITY_TYPE_START, SST_CP_PRIORITY_TYPE_WIDTH,
			      SST_MUL_FACTOR_NONE)
		core_power.supported = !!(power_domain_info->sst_header.cap_mask & BIT(0));
		if (copy_to_user(argp, &core_power, sizeof(core_power)))
			return -EFAULT;
	}

	return 0;
}

#define SST_CLOS_CONFIG_0_OFFSET	24

#define SST_CLOS_CONFIG_PRIO_START	4
#define SST_CLOS_CONFIG_PRIO_WIDTH	4

#define SST_CLOS_CONFIG_MIN_START	8
#define SST_CLOS_CONFIG_MIN_WIDTH	8

#define SST_CLOS_CONFIG_MAX_START	16
#define SST_CLOS_CONFIG_MAX_WIDTH	8

static long isst_if_clos_param(void __user *argp)
{
	struct tpmi_per_power_domain_info *power_domain_info;
	struct isst_clos_param clos_param;

	if (copy_from_user(&clos_param, argp, sizeof(clos_param)))
		return -EFAULT;

	power_domain_info = get_instance(clos_param.socket_id, clos_param.power_domain_id);
	if (!power_domain_info)
		return -EINVAL;

	if (clos_param.get_set) {
		if (power_domain_info->write_blocked)
			return -EPERM;

		_write_cp_info("clos.min_freq", clos_param.min_freq_mhz,
			       (SST_CLOS_CONFIG_0_OFFSET + clos_param.clos * SST_REG_SIZE),
			       SST_CLOS_CONFIG_MIN_START, SST_CLOS_CONFIG_MIN_WIDTH,
			       SST_MUL_FACTOR_FREQ);
		_write_cp_info("clos.max_freq", clos_param.max_freq_mhz,
			       (SST_CLOS_CONFIG_0_OFFSET + clos_param.clos * SST_REG_SIZE),
			       SST_CLOS_CONFIG_MAX_START, SST_CLOS_CONFIG_MAX_WIDTH,
			       SST_MUL_FACTOR_FREQ);
		_write_cp_info("clos.prio", clos_param.prop_prio,
			       (SST_CLOS_CONFIG_0_OFFSET + clos_param.clos * SST_REG_SIZE),
			       SST_CLOS_CONFIG_PRIO_START, SST_CLOS_CONFIG_PRIO_WIDTH,
			       SST_MUL_FACTOR_NONE);
	} else {
		/* get */
		_read_cp_info("clos.min_freq", clos_param.min_freq_mhz,
				(SST_CLOS_CONFIG_0_OFFSET + clos_param.clos * SST_REG_SIZE),
				SST_CLOS_CONFIG_MIN_START, SST_CLOS_CONFIG_MIN_WIDTH,
				SST_MUL_FACTOR_FREQ)
		_read_cp_info("clos.max_freq", clos_param.max_freq_mhz,
				(SST_CLOS_CONFIG_0_OFFSET + clos_param.clos * SST_REG_SIZE),
				SST_CLOS_CONFIG_MAX_START, SST_CLOS_CONFIG_MAX_WIDTH,
				SST_MUL_FACTOR_FREQ)
		_read_cp_info("clos.prio", clos_param.prop_prio,
				(SST_CLOS_CONFIG_0_OFFSET + clos_param.clos * SST_REG_SIZE),
				SST_CLOS_CONFIG_PRIO_START, SST_CLOS_CONFIG_PRIO_WIDTH,
				SST_MUL_FACTOR_NONE)

		if (copy_to_user(argp, &clos_param, sizeof(clos_param)))
			return -EFAULT;
	}

	return 0;
}

#define SST_CLOS_ASSOC_0_OFFSET		56
#define SST_CLOS_ASSOC_CPUS_PER_REG	16
#define SST_CLOS_ASSOC_BITS_PER_CPU	4

static long isst_if_clos_assoc(void __user *argp)
{
	struct isst_if_clos_assoc_cmds assoc_cmds;
	unsigned char __user *ptr;
	int i;

	/* Each multi command has u16 command count as the first field */
	if (copy_from_user(&assoc_cmds, argp, sizeof(assoc_cmds)))
		return -EFAULT;

	if (!assoc_cmds.cmd_count || assoc_cmds.cmd_count > ISST_IF_CMD_LIMIT)
		return -EINVAL;

	ptr = argp + offsetof(struct isst_if_clos_assoc_cmds, assoc_info);
	for (i = 0; i < assoc_cmds.cmd_count; ++i) {
		struct tpmi_per_power_domain_info *power_domain_info;
		struct isst_if_clos_assoc clos_assoc;
		int punit_id, punit_cpu_no, pkg_id;
		struct tpmi_sst_struct *sst_inst;
		int offset, shift, cpu;
		u64 val, mask, clos;
		u8 part;

		if (copy_from_user(&clos_assoc, ptr, sizeof(clos_assoc)))
			return -EFAULT;

		if (clos_assoc.socket_id > topology_max_packages())
			return -EINVAL;

		cpu = clos_assoc.logical_cpu;
		clos = clos_assoc.clos;

		if (assoc_cmds.punit_cpu_map)
			punit_cpu_no = cpu;
		else
			return -EOPNOTSUPP;

		if (punit_cpu_no < 0)
			return -EINVAL;

		punit_id = clos_assoc.power_domain_id;
		pkg_id = clos_assoc.socket_id;

		sst_inst = isst_common.sst_inst[pkg_id];

		punit_id = map_partition_power_domain_id(sst_inst, punit_id, &part);
		if (punit_id < 0)
			return -EINVAL;

		power_domain_info = &sst_inst->power_domain_info[part][punit_id];

		if (assoc_cmds.get_set && power_domain_info->write_blocked)
			return -EPERM;

		offset = SST_CLOS_ASSOC_0_OFFSET +
				(punit_cpu_no / SST_CLOS_ASSOC_CPUS_PER_REG) * SST_REG_SIZE;
		shift = punit_cpu_no % SST_CLOS_ASSOC_CPUS_PER_REG;
		shift *= SST_CLOS_ASSOC_BITS_PER_CPU;

		val = readq(power_domain_info->sst_base +
				power_domain_info->sst_header.cp_offset + offset);
		if (assoc_cmds.get_set) {
			mask = GENMASK_ULL((shift + SST_CLOS_ASSOC_BITS_PER_CPU - 1), shift);
			val &= ~mask;
			val |= (clos << shift);
			writeq(val, power_domain_info->sst_base +
					power_domain_info->sst_header.cp_offset + offset);
		} else {
			val >>= shift;
			clos_assoc.clos = val & GENMASK(SST_CLOS_ASSOC_BITS_PER_CPU - 1, 0);
			if (copy_to_user(ptr, &clos_assoc, sizeof(clos_assoc)))
				return -EFAULT;
		}

		ptr += sizeof(clos_assoc);
	}

	return 0;
}

#define _read_pp_info(name_str, name, offset, start, width, mult_factor)\
{\
	u64 val, _mask;\
	\
	val = readq(power_domain_info->sst_base + power_domain_info->sst_header.pp_offset +\
		    (offset));\
	_mask = GENMASK_ULL((start + width - 1), start);\
	val &= _mask;\
	val >>= start;\
	name = (val * mult_factor);\
}

#define _write_pp_info(name_str, name, offset, start, width, div_factor)\
{\
	u64 val, _mask;\
	\
	val = readq(power_domain_info->sst_base + power_domain_info->sst_header.pp_offset +\
		    (offset));\
	_mask = GENMASK((start + width - 1), start);\
	val &= ~_mask;\
	val |= (name / div_factor) << start;\
	writeq(val, power_domain_info->sst_base + power_domain_info->sst_header.pp_offset +\
	      (offset));\
}

#define _read_bf_level_info(name_str, name, level, offset, start, width, mult_factor)\
{\
	u64 val, _mask;\
	\
	val = readq(power_domain_info->sst_base +\
		    power_domain_info->perf_levels[level].mmio_offset +\
		(power_domain_info->feature_offsets.bf_offset * 8) + (offset));\
	_mask = GENMASK_ULL((start + width - 1), start);\
	val &= _mask; \
	val >>= start;\
	name = (val * mult_factor);\
}

#define _read_tf_level_info(name_str, name, level, offset, start, width, mult_factor)\
{\
	u64 val, _mask;\
	\
	val = readq(power_domain_info->sst_base +\
		    power_domain_info->perf_levels[level].mmio_offset +\
		(power_domain_info->feature_offsets.tf_offset * 8) + (offset));\
	_mask = GENMASK_ULL((start + width - 1), start);\
	val &= _mask; \
	val >>= start;\
	name = (val * mult_factor);\
}

#define SST_PP_STATUS_OFFSET	32

#define SST_PP_LEVEL_START	0
#define SST_PP_LEVEL_WIDTH	3

#define SST_PP_LOCK_START	3
#define SST_PP_LOCK_WIDTH	1

#define SST_PP_FEATURE_STATE_START	8
#define SST_PP_FEATURE_STATE_WIDTH	8

#define SST_BF_FEATURE_SUPPORTED_START	12
#define SST_BF_FEATURE_SUPPORTED_WIDTH	1

#define SST_TF_FEATURE_SUPPORTED_START	12
#define SST_TF_FEATURE_SUPPORTED_WIDTH	1

static int isst_if_get_perf_level(void __user *argp)
{
	struct isst_perf_level_info perf_level;
	struct tpmi_per_power_domain_info *power_domain_info;
	unsigned long level_mask;
	u8 level, support;

	if (copy_from_user(&perf_level, argp, sizeof(perf_level)))
		return -EFAULT;

	power_domain_info = get_instance(perf_level.socket_id, perf_level.power_domain_id);
	if (!power_domain_info)
		return -EINVAL;

	perf_level.max_level = power_domain_info->max_level;
	perf_level.level_mask = power_domain_info->pp_header.level_en_mask;
	perf_level.feature_rev = power_domain_info->pp_header.feature_rev;
	_read_pp_info("current_level", perf_level.current_level, SST_PP_STATUS_OFFSET,
		      SST_PP_LEVEL_START, SST_PP_LEVEL_WIDTH, SST_MUL_FACTOR_NONE)
	_read_pp_info("locked", perf_level.locked, SST_PP_STATUS_OFFSET,
		      SST_PP_LOCK_START, SST_PP_LEVEL_WIDTH, SST_MUL_FACTOR_NONE)
	_read_pp_info("feature_state", perf_level.feature_state, SST_PP_STATUS_OFFSET,
		      SST_PP_FEATURE_STATE_START, SST_PP_FEATURE_STATE_WIDTH, SST_MUL_FACTOR_NONE)
	perf_level.enabled = !!(power_domain_info->sst_header.cap_mask & BIT(1));

	level_mask = perf_level.level_mask;
	perf_level.sst_bf_support = 0;
	for_each_set_bit(level, &level_mask, BITS_PER_BYTE) {
		/*
		 * Read BF support for a level. Read output is updated
		 * to "support" variable by the below macro.
		 */
		_read_bf_level_info("bf_support", support, level, 0, SST_BF_FEATURE_SUPPORTED_START,
				    SST_BF_FEATURE_SUPPORTED_WIDTH, SST_MUL_FACTOR_NONE);

		/* If supported set the bit for the level */
		if (support)
			perf_level.sst_bf_support |= BIT(level);
	}

	perf_level.sst_tf_support = 0;
	for_each_set_bit(level, &level_mask, BITS_PER_BYTE) {
		/*
		 * Read TF support for a level. Read output is updated
		 * to "support" variable by the below macro.
		 */
		_read_tf_level_info("tf_support", support, level, 0, SST_TF_FEATURE_SUPPORTED_START,
				    SST_TF_FEATURE_SUPPORTED_WIDTH, SST_MUL_FACTOR_NONE);

		/* If supported set the bit for the level */
		if (support)
			perf_level.sst_tf_support |= BIT(level);
	}

	if (copy_to_user(argp, &perf_level, sizeof(perf_level)))
		return -EFAULT;

	return 0;
}

#define SST_PP_CONTROL_OFFSET		24
#define SST_PP_LEVEL_CHANGE_TIME_MS	5
#define SST_PP_LEVEL_CHANGE_RETRY_COUNT	3

static int isst_if_set_perf_level(void __user *argp)
{
	struct isst_perf_level_control perf_level;
	struct tpmi_per_power_domain_info *power_domain_info;
	int level, retry = 0;

	if (disable_dynamic_sst_features())
		return -EFAULT;

	if (copy_from_user(&perf_level, argp, sizeof(perf_level)))
		return -EFAULT;

	power_domain_info = get_instance(perf_level.socket_id, perf_level.power_domain_id);
	if (!power_domain_info)
		return -EINVAL;

	if (power_domain_info->write_blocked)
		return -EPERM;

	if (!(power_domain_info->pp_header.allowed_level_mask & BIT(perf_level.level)))
		return -EINVAL;

	_read_pp_info("current_level", level, SST_PP_STATUS_OFFSET,
		      SST_PP_LEVEL_START, SST_PP_LEVEL_WIDTH, SST_MUL_FACTOR_NONE)

	/* If the requested new level is same as the current level, reject */
	if (perf_level.level == level)
		return -EINVAL;

	_write_pp_info("perf_level", perf_level.level, SST_PP_CONTROL_OFFSET,
		       SST_PP_LEVEL_START, SST_PP_LEVEL_WIDTH, SST_MUL_FACTOR_NONE)

	/* It is possible that firmware is busy (although unlikely), so retry */
	do {
		/* Give time to FW to process */
		msleep(SST_PP_LEVEL_CHANGE_TIME_MS);

		_read_pp_info("current_level", level, SST_PP_STATUS_OFFSET,
			      SST_PP_LEVEL_START, SST_PP_LEVEL_WIDTH, SST_MUL_FACTOR_NONE)

		/* Check if the new level is active */
		if (perf_level.level == level)
			break;

	} while (retry++ < SST_PP_LEVEL_CHANGE_RETRY_COUNT);

	/* If the level change didn't happen, return fault */
	if (perf_level.level != level)
		return -EFAULT;

	/* Reset the feature state on level change */
	_write_pp_info("perf_feature", 0, SST_PP_CONTROL_OFFSET,
		       SST_PP_FEATURE_STATE_START, SST_PP_FEATURE_STATE_WIDTH,
		       SST_MUL_FACTOR_NONE)

	/* Give time to FW to process */
	msleep(SST_PP_LEVEL_CHANGE_TIME_MS);

	return 0;
}

static int isst_if_set_perf_feature(void __user *argp)
{
	struct isst_perf_feature_control perf_feature;
	struct tpmi_per_power_domain_info *power_domain_info;

	if (disable_dynamic_sst_features())
		return -EFAULT;

	if (copy_from_user(&perf_feature, argp, sizeof(perf_feature)))
		return -EFAULT;

	power_domain_info = get_instance(perf_feature.socket_id, perf_feature.power_domain_id);
	if (!power_domain_info)
		return -EINVAL;

	if (power_domain_info->write_blocked)
		return -EPERM;

	_write_pp_info("perf_feature", perf_feature.feature, SST_PP_CONTROL_OFFSET,
		       SST_PP_FEATURE_STATE_START, SST_PP_FEATURE_STATE_WIDTH,
		       SST_MUL_FACTOR_NONE)

	return 0;
}

#define _read_pp_level_info(name_str, name, level, offset, start, width, mult_factor)\
{\
	u64 val, _mask;\
	\
	val = readq(power_domain_info->sst_base +\
		    power_domain_info->perf_levels[level].mmio_offset +\
		(power_domain_info->feature_offsets.pp_offset * 8) + (offset));\
	_mask = GENMASK_ULL((start + width - 1), start);\
	val &= _mask; \
	val >>= start;\
	name = (val * mult_factor);\
}

#define SST_PP_INFO_0_OFFSET	0
#define SST_PP_INFO_1_OFFSET	8
#define SST_PP_INFO_2_OFFSET	16
#define SST_PP_INFO_3_OFFSET	24

/* SST_PP_INFO_4_OFFSET to SST_PP_INFO_9_OFFSET are trl levels */
#define SST_PP_INFO_4_OFFSET	32

#define SST_PP_INFO_10_OFFSET	80
#define SST_PP_INFO_11_OFFSET	88

#define SST_PP_P1_SSE_START	0
#define SST_PP_P1_SSE_WIDTH	8

#define SST_PP_P1_AVX2_START	8
#define SST_PP_P1_AVX2_WIDTH	8

#define SST_PP_P1_AVX512_START	16
#define SST_PP_P1_AVX512_WIDTH	8

#define SST_PP_P1_AMX_START	24
#define SST_PP_P1_AMX_WIDTH	8

#define SST_PP_TDP_START	32
#define SST_PP_TDP_WIDTH	15

#define SST_PP_T_PROCHOT_START	47
#define SST_PP_T_PROCHOT_WIDTH	8

#define SST_PP_MAX_MEMORY_FREQ_START	55
#define SST_PP_MAX_MEMORY_FREQ_WIDTH	7

#define SST_PP_COOLING_TYPE_START	62
#define SST_PP_COOLING_TYPE_WIDTH	2

#define SST_PP_TRL_0_RATIO_0_START	0
#define SST_PP_TRL_0_RATIO_0_WIDTH	8

#define SST_PP_TRL_CORES_BUCKET_0_START	0
#define SST_PP_TRL_CORES_BUCKET_0_WIDTH	8

#define SST_PP_CORE_RATIO_P0_START	0
#define SST_PP_CORE_RATIO_P0_WIDTH	8

#define SST_PP_CORE_RATIO_P1_START	8
#define SST_PP_CORE_RATIO_P1_WIDTH	8

#define SST_PP_CORE_RATIO_PN_START	16
#define SST_PP_CORE_RATIO_PN_WIDTH	8

#define SST_PP_CORE_RATIO_PM_START	24
#define SST_PP_CORE_RATIO_PM_WIDTH	8

#define SST_PP_CORE_RATIO_P0_FABRIC_START	32
#define SST_PP_CORE_RATIO_P0_FABRIC_WIDTH	8

#define SST_PP_CORE_RATIO_P1_FABRIC_START	40
#define SST_PP_CORE_RATIO_P1_FABRIC_WIDTH	8

#define SST_PP_CORE_RATIO_PM_FABRIC_START	48
#define SST_PP_CORE_RATIO_PM_FABRIC_WIDTH	8

static int isst_if_get_perf_level_info(void __user *argp)
{
	struct isst_perf_level_data_info perf_level;
	struct tpmi_per_power_domain_info *power_domain_info;
	int i, j;

	if (copy_from_user(&perf_level, argp, sizeof(perf_level)))
		return -EFAULT;

	power_domain_info = get_instance(perf_level.socket_id, perf_level.power_domain_id);
	if (!power_domain_info)
		return -EINVAL;

	if (perf_level.level > power_domain_info->max_level)
		return -EINVAL;

	if (!(power_domain_info->pp_header.level_en_mask & BIT(perf_level.level)))
		return -EINVAL;

	_read_pp_level_info("tdp_ratio", perf_level.tdp_ratio, perf_level.level,
			    SST_PP_INFO_0_OFFSET, SST_PP_P1_SSE_START, SST_PP_P1_SSE_WIDTH,
			    SST_MUL_FACTOR_NONE)
	_read_pp_level_info("base_freq_mhz", perf_level.base_freq_mhz, perf_level.level,
			    SST_PP_INFO_0_OFFSET, SST_PP_P1_SSE_START, SST_PP_P1_SSE_WIDTH,
			    SST_MUL_FACTOR_FREQ)
	_read_pp_level_info("base_freq_avx2_mhz", perf_level.base_freq_avx2_mhz, perf_level.level,
			    SST_PP_INFO_0_OFFSET, SST_PP_P1_AVX2_START, SST_PP_P1_AVX2_WIDTH,
			    SST_MUL_FACTOR_FREQ)
	_read_pp_level_info("base_freq_avx512_mhz", perf_level.base_freq_avx512_mhz,
			    perf_level.level, SST_PP_INFO_0_OFFSET, SST_PP_P1_AVX512_START,
			    SST_PP_P1_AVX512_WIDTH, SST_MUL_FACTOR_FREQ)
	_read_pp_level_info("base_freq_amx_mhz", perf_level.base_freq_amx_mhz, perf_level.level,
			    SST_PP_INFO_0_OFFSET, SST_PP_P1_AMX_START, SST_PP_P1_AMX_WIDTH,
			    SST_MUL_FACTOR_FREQ)

	_read_pp_level_info("thermal_design_power_w", perf_level.thermal_design_power_w,
			    perf_level.level, SST_PP_INFO_1_OFFSET, SST_PP_TDP_START,
			    SST_PP_TDP_WIDTH, SST_MUL_FACTOR_NONE)
	perf_level.thermal_design_power_w /= 8; /* units are in 1/8th watt */
	_read_pp_level_info("tjunction_max_c", perf_level.tjunction_max_c, perf_level.level,
			    SST_PP_INFO_1_OFFSET, SST_PP_T_PROCHOT_START, SST_PP_T_PROCHOT_WIDTH,
			    SST_MUL_FACTOR_NONE)
	_read_pp_level_info("max_memory_freq_mhz", perf_level.max_memory_freq_mhz,
			    perf_level.level, SST_PP_INFO_1_OFFSET, SST_PP_MAX_MEMORY_FREQ_START,
			    SST_PP_MAX_MEMORY_FREQ_WIDTH, SST_MUL_FACTOR_FREQ)
	_read_pp_level_info("cooling_type", perf_level.cooling_type, perf_level.level,
			    SST_PP_INFO_1_OFFSET, SST_PP_COOLING_TYPE_START,
			    SST_PP_COOLING_TYPE_WIDTH, SST_MUL_FACTOR_NONE)

	for (i = 0; i < TRL_MAX_LEVELS; ++i) {
		for (j = 0; j < TRL_MAX_BUCKETS; ++j)
			_read_pp_level_info("trl*_bucket*_freq_mhz",
					    perf_level.trl_freq_mhz[i][j], perf_level.level,
					    SST_PP_INFO_4_OFFSET + (i * SST_PP_TRL_0_RATIO_0_WIDTH),
					    j * SST_PP_TRL_0_RATIO_0_WIDTH,
					    SST_PP_TRL_0_RATIO_0_WIDTH,
					    SST_MUL_FACTOR_FREQ);
	}

	for (i = 0; i < TRL_MAX_BUCKETS; ++i)
		_read_pp_level_info("bucket*_core_count", perf_level.bucket_core_counts[i],
				    perf_level.level, SST_PP_INFO_10_OFFSET,
				    SST_PP_TRL_CORES_BUCKET_0_WIDTH * i,
				    SST_PP_TRL_CORES_BUCKET_0_WIDTH, SST_MUL_FACTOR_NONE)

	perf_level.max_buckets = TRL_MAX_BUCKETS;
	perf_level.max_trl_levels = TRL_MAX_LEVELS;

	_read_pp_level_info("p0_freq_mhz", perf_level.p0_freq_mhz, perf_level.level,
			    SST_PP_INFO_11_OFFSET, SST_PP_CORE_RATIO_P0_START,
			    SST_PP_CORE_RATIO_P0_WIDTH, SST_MUL_FACTOR_FREQ)
	_read_pp_level_info("p1_freq_mhz", perf_level.p1_freq_mhz, perf_level.level,
			    SST_PP_INFO_11_OFFSET, SST_PP_CORE_RATIO_P1_START,
			    SST_PP_CORE_RATIO_P1_WIDTH, SST_MUL_FACTOR_FREQ)
	_read_pp_level_info("pn_freq_mhz", perf_level.pn_freq_mhz, perf_level.level,
			    SST_PP_INFO_11_OFFSET, SST_PP_CORE_RATIO_PN_START,
			    SST_PP_CORE_RATIO_PN_WIDTH, SST_MUL_FACTOR_FREQ)
	_read_pp_level_info("pm_freq_mhz", perf_level.pm_freq_mhz, perf_level.level,
			    SST_PP_INFO_11_OFFSET, SST_PP_CORE_RATIO_PM_START,
			    SST_PP_CORE_RATIO_PM_WIDTH, SST_MUL_FACTOR_FREQ)
	_read_pp_level_info("p0_fabric_freq_mhz", perf_level.p0_fabric_freq_mhz,
			    perf_level.level, SST_PP_INFO_11_OFFSET,
			    SST_PP_CORE_RATIO_P0_FABRIC_START,
			    SST_PP_CORE_RATIO_P0_FABRIC_WIDTH, SST_MUL_FACTOR_FREQ)
	_read_pp_level_info("p1_fabric_freq_mhz", perf_level.p1_fabric_freq_mhz,
			    perf_level.level, SST_PP_INFO_11_OFFSET,
			    SST_PP_CORE_RATIO_P1_FABRIC_START,
			    SST_PP_CORE_RATIO_P1_FABRIC_WIDTH, SST_MUL_FACTOR_FREQ)
	_read_pp_level_info("pm_fabric_freq_mhz", perf_level.pm_fabric_freq_mhz,
			    perf_level.level, SST_PP_INFO_11_OFFSET,
			    SST_PP_CORE_RATIO_PM_FABRIC_START,
			    SST_PP_CORE_RATIO_PM_FABRIC_WIDTH, SST_MUL_FACTOR_FREQ)

	if (copy_to_user(argp, &perf_level, sizeof(perf_level)))
		return -EFAULT;

	return 0;
}

#define SST_PP_FUSED_CORE_COUNT_START	0
#define SST_PP_FUSED_CORE_COUNT_WIDTH	8

#define SST_PP_RSLVD_CORE_COUNT_START	8
#define SST_PP_RSLVD_CORE_COUNT_WIDTH	8

#define SST_PP_RSLVD_CORE_MASK_START	0
#define SST_PP_RSLVD_CORE_MASK_WIDTH	64

static int isst_if_get_perf_level_mask(void __user *argp)
{
	static struct isst_perf_level_cpu_mask cpumask;
	struct tpmi_per_power_domain_info *power_domain_info;
	u64 mask;

	if (copy_from_user(&cpumask, argp, sizeof(cpumask)))
		return -EFAULT;

	power_domain_info = get_instance(cpumask.socket_id, cpumask.power_domain_id);
	if (!power_domain_info)
		return -EINVAL;

	_read_pp_level_info("mask", mask, cpumask.level, SST_PP_INFO_2_OFFSET,
			    SST_PP_RSLVD_CORE_MASK_START, SST_PP_RSLVD_CORE_MASK_WIDTH,
			    SST_MUL_FACTOR_NONE)

	cpumask.mask = mask;

	if (!cpumask.punit_cpu_map)
		return -EOPNOTSUPP;

	if (copy_to_user(argp, &cpumask, sizeof(cpumask)))
		return -EFAULT;

	return 0;
}

#define SST_BF_INFO_0_OFFSET	0
#define SST_BF_INFO_1_OFFSET	8

#define SST_BF_P1_HIGH_START	13
#define SST_BF_P1_HIGH_WIDTH	8

#define SST_BF_P1_LOW_START	21
#define SST_BF_P1_LOW_WIDTH	8

#define SST_BF_T_PROHOT_START	38
#define SST_BF_T_PROHOT_WIDTH	8

#define SST_BF_TDP_START	46
#define SST_BF_TDP_WIDTH	15

static int isst_if_get_base_freq_info(void __user *argp)
{
	static struct isst_base_freq_info base_freq;
	struct tpmi_per_power_domain_info *power_domain_info;

	if (copy_from_user(&base_freq, argp, sizeof(base_freq)))
		return -EFAULT;

	power_domain_info = get_instance(base_freq.socket_id, base_freq.power_domain_id);
	if (!power_domain_info)
		return -EINVAL;

	if (base_freq.level > power_domain_info->max_level)
		return -EINVAL;

	_read_bf_level_info("p1_high", base_freq.high_base_freq_mhz, base_freq.level,
			    SST_BF_INFO_0_OFFSET, SST_BF_P1_HIGH_START, SST_BF_P1_HIGH_WIDTH,
			    SST_MUL_FACTOR_FREQ)
	_read_bf_level_info("p1_low", base_freq.low_base_freq_mhz, base_freq.level,
			    SST_BF_INFO_0_OFFSET, SST_BF_P1_LOW_START, SST_BF_P1_LOW_WIDTH,
			    SST_MUL_FACTOR_FREQ)
	_read_bf_level_info("BF-TJ", base_freq.tjunction_max_c, base_freq.level,
			    SST_BF_INFO_0_OFFSET, SST_BF_T_PROHOT_START, SST_BF_T_PROHOT_WIDTH,
			    SST_MUL_FACTOR_NONE)
	_read_bf_level_info("BF-tdp", base_freq.thermal_design_power_w, base_freq.level,
			    SST_BF_INFO_0_OFFSET, SST_BF_TDP_START, SST_BF_TDP_WIDTH,
			    SST_MUL_FACTOR_NONE)
	base_freq.thermal_design_power_w /= 8; /*unit = 1/8th watt*/

	if (copy_to_user(argp, &base_freq, sizeof(base_freq)))
		return -EFAULT;

	return 0;
}

#define P1_HI_CORE_MASK_START	0
#define P1_HI_CORE_MASK_WIDTH	64

static int isst_if_get_base_freq_mask(void __user *argp)
{
	static struct isst_perf_level_cpu_mask cpumask;
	struct tpmi_per_power_domain_info *power_domain_info;
	u64 mask;

	if (copy_from_user(&cpumask, argp, sizeof(cpumask)))
		return -EFAULT;

	power_domain_info = get_instance(cpumask.socket_id, cpumask.power_domain_id);
	if (!power_domain_info)
		return -EINVAL;

	_read_bf_level_info("BF-cpumask", mask, cpumask.level, SST_BF_INFO_1_OFFSET,
			    P1_HI_CORE_MASK_START, P1_HI_CORE_MASK_WIDTH,
			    SST_MUL_FACTOR_NONE)

	cpumask.mask = mask;

	if (!cpumask.punit_cpu_map)
		return -EOPNOTSUPP;

	if (copy_to_user(argp, &cpumask, sizeof(cpumask)))
		return -EFAULT;

	return 0;
}

static int isst_if_get_tpmi_instance_count(void __user *argp)
{
	struct isst_tpmi_instance_count tpmi_inst;
	struct tpmi_sst_struct *sst_inst;
	int i;

	if (copy_from_user(&tpmi_inst, argp, sizeof(tpmi_inst)))
		return -EFAULT;

	if (tpmi_inst.socket_id >= topology_max_packages())
		return -EINVAL;

	sst_inst = isst_common.sst_inst[tpmi_inst.socket_id];

	tpmi_inst.count = isst_instance_count(sst_inst);

	tpmi_inst.valid_mask = 0;
	for (i = 0; i < tpmi_inst.count; i++) {
		struct tpmi_per_power_domain_info *pd_info;
		u8 part;
		int pd;

		pd = map_partition_power_domain_id(sst_inst, i, &part);
		if (pd < 0)
			continue;

		pd_info = &sst_inst->power_domain_info[part][pd];
		if (pd_info->sst_base)
			tpmi_inst.valid_mask |= BIT(i);
	}

	if (!tpmi_inst.valid_mask)
		tpmi_inst.count = 0;

	if (copy_to_user(argp, &tpmi_inst, sizeof(tpmi_inst)))
		return -EFAULT;

	return 0;
}

#define SST_TF_INFO_0_OFFSET	0
#define SST_TF_INFO_1_OFFSET	8
#define SST_TF_INFO_2_OFFSET	16

#define SST_TF_MAX_LP_CLIP_RATIOS	TRL_MAX_LEVELS

#define SST_TF_LP_CLIP_RATIO_0_START	16
#define SST_TF_LP_CLIP_RATIO_0_WIDTH	8

#define SST_TF_RATIO_0_START	0
#define SST_TF_RATIO_0_WIDTH	8

#define SST_TF_NUM_CORE_0_START 0
#define SST_TF_NUM_CORE_0_WIDTH 8

static int isst_if_get_turbo_freq_info(void __user *argp)
{
	static struct isst_turbo_freq_info turbo_freq;
	struct tpmi_per_power_domain_info *power_domain_info;
	int i, j;

	if (copy_from_user(&turbo_freq, argp, sizeof(turbo_freq)))
		return -EFAULT;

	power_domain_info = get_instance(turbo_freq.socket_id, turbo_freq.power_domain_id);
	if (!power_domain_info)
		return -EINVAL;

	if (turbo_freq.level > power_domain_info->max_level)
		return -EINVAL;

	turbo_freq.max_buckets = TRL_MAX_BUCKETS;
	turbo_freq.max_trl_levels = TRL_MAX_LEVELS;
	turbo_freq.max_clip_freqs = SST_TF_MAX_LP_CLIP_RATIOS;

	for (i = 0; i < turbo_freq.max_clip_freqs; ++i)
		_read_tf_level_info("lp_clip*", turbo_freq.lp_clip_freq_mhz[i],
				    turbo_freq.level, SST_TF_INFO_0_OFFSET,
				    SST_TF_LP_CLIP_RATIO_0_START +
				    (i * SST_TF_LP_CLIP_RATIO_0_WIDTH),
				    SST_TF_LP_CLIP_RATIO_0_WIDTH, SST_MUL_FACTOR_FREQ)

	for (i = 0; i < TRL_MAX_LEVELS; ++i) {
		for (j = 0; j < TRL_MAX_BUCKETS; ++j)
			_read_tf_level_info("cydn*_bucket_*_trl",
					    turbo_freq.trl_freq_mhz[i][j], turbo_freq.level,
					    SST_TF_INFO_2_OFFSET + (i * SST_TF_RATIO_0_WIDTH),
					    j * SST_TF_RATIO_0_WIDTH, SST_TF_RATIO_0_WIDTH,
					    SST_MUL_FACTOR_FREQ)
	}

	for (i = 0; i < TRL_MAX_BUCKETS; ++i)
		_read_tf_level_info("bucket_*_core_count", turbo_freq.bucket_core_counts[i],
				    turbo_freq.level, SST_TF_INFO_1_OFFSET,
				    SST_TF_NUM_CORE_0_WIDTH * i, SST_TF_NUM_CORE_0_WIDTH,
				    SST_MUL_FACTOR_NONE)

	if (copy_to_user(argp, &turbo_freq, sizeof(turbo_freq)))
		return -EFAULT;

	return 0;
}

static long isst_if_def_ioctl(struct file *file, unsigned int cmd,
			      unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	long ret = -ENOTTY;

	mutex_lock(&isst_tpmi_dev_lock);
	switch (cmd) {
	case ISST_IF_COUNT_TPMI_INSTANCES:
		ret = isst_if_get_tpmi_instance_count(argp);
		break;
	case ISST_IF_CORE_POWER_STATE:
		ret = isst_if_core_power_state(argp);
		break;
	case ISST_IF_CLOS_PARAM:
		ret = isst_if_clos_param(argp);
		break;
	case ISST_IF_CLOS_ASSOC:
		ret = isst_if_clos_assoc(argp);
		break;
	case ISST_IF_PERF_LEVELS:
		ret = isst_if_get_perf_level(argp);
		break;
	case ISST_IF_PERF_SET_LEVEL:
		ret = isst_if_set_perf_level(argp);
		break;
	case ISST_IF_PERF_SET_FEATURE:
		ret = isst_if_set_perf_feature(argp);
		break;
	case ISST_IF_GET_PERF_LEVEL_INFO:
		ret = isst_if_get_perf_level_info(argp);
		break;
	case ISST_IF_GET_PERF_LEVEL_CPU_MASK:
		ret = isst_if_get_perf_level_mask(argp);
		break;
	case ISST_IF_GET_BASE_FREQ_INFO:
		ret = isst_if_get_base_freq_info(argp);
		break;
	case ISST_IF_GET_BASE_FREQ_CPU_MASK:
		ret = isst_if_get_base_freq_mask(argp);
		break;
	case ISST_IF_GET_TURBO_FREQ_INFO:
		ret = isst_if_get_turbo_freq_info(argp);
		break;
	default:
		break;
	}
	mutex_unlock(&isst_tpmi_dev_lock);

	return ret;
}

#define TPMI_SST_AUTO_SUSPEND_DELAY_MS	2000

int tpmi_sst_dev_add(struct auxiliary_device *auxdev)
{
	struct tpmi_per_power_domain_info *pd_info;
	bool read_blocked = 0, write_blocked = 0;
	struct intel_tpmi_plat_info *plat_info;
	struct device *dev = &auxdev->dev;
	struct tpmi_sst_struct *tpmi_sst;
	u8 i, num_resources, io_die_cnt;
	int ret, pkg = 0, inst = 0;
	bool first_enum = false;
	u16 cdie_mask;
	u8 partition;

	ret = tpmi_get_feature_status(auxdev, TPMI_ID_SST, &read_blocked, &write_blocked);
	if (ret)
		dev_info(dev, "Can't read feature status: ignoring read/write blocked status\n");

	if (read_blocked) {
		dev_info(dev, "Firmware has blocked reads, exiting\n");
		return -ENODEV;
	}

	plat_info = tpmi_get_platform_data(auxdev);
	if (!plat_info) {
		dev_err(dev, "No platform info\n");
		return -EINVAL;
	}

	pkg = plat_info->package_id;
	if (pkg >= topology_max_packages()) {
		dev_err(dev, "Invalid package id :%x\n", pkg);
		return -EINVAL;
	}

	partition = plat_info->partition;
	if (partition >= SST_MAX_PARTITIONS) {
		dev_err(&auxdev->dev, "Invalid partition :%x\n", partition);
		return -EINVAL;
	}

	num_resources = tpmi_get_resource_count(auxdev);

	if (!num_resources)
		return -EINVAL;

	mutex_lock(&isst_tpmi_dev_lock);

	if (isst_common.sst_inst[pkg]) {
		tpmi_sst = isst_common.sst_inst[pkg];
	} else {
		/*
		 * tpmi_sst instance is for a package. So needs to be
		 * allocated only once for both partitions. We can't use
		 * devm_* allocation here as each partition is a
		 * different device, which can be unbound.
		 */
		tpmi_sst = kzalloc(sizeof(*tpmi_sst), GFP_KERNEL);
		if (!tpmi_sst) {
			ret = -ENOMEM;
			goto unlock_exit;
		}
		first_enum = true;
	}

	ret = 0;

	pd_info = devm_kcalloc(dev, num_resources, sizeof(*pd_info), GFP_KERNEL);
	if (!pd_info) {
		ret = -ENOMEM;
		goto unlock_free;
	}

	/* Get the IO die count, if cdie_mask is present */
	if (plat_info->cdie_mask) {
		u8 cdie_range;

		cdie_mask = plat_info->cdie_mask;
		cdie_range = fls(cdie_mask) - ffs(cdie_mask) + 1;
		io_die_cnt = num_resources - cdie_range;
	} else {
		/*
		 * This is a synthetic mask, careful when assuming that
		 * they are compute dies only.
		 */
		cdie_mask = (1 << num_resources) - 1;
		io_die_cnt = 0;
	}

	for (i = 0; i < num_resources; ++i) {
		struct resource *res;

		res = tpmi_get_resource_at_index(auxdev, i);
		if (!res) {
			pd_info[i].sst_base = NULL;
			continue;
		}

		pd_info[i].package_id = pkg;
		pd_info[i].power_domain_id = i;
		pd_info[i].auxdev = auxdev;
		pd_info[i].write_blocked = write_blocked;
		pd_info[i].sst_base = devm_ioremap_resource(dev, res);
		if (IS_ERR(pd_info[i].sst_base)) {
			ret = PTR_ERR(pd_info[i].sst_base);
			goto unlock_free;
		}

		ret = sst_main(auxdev, &pd_info[i]);
		if (ret) {
			/*
			 * This entry is not valid, hardware can partially
			 * populate dies. In this case MMIO will have 0xFFs.
			 * Also possible some pre-production hardware has
			 * invalid data. But don't fail and continue to use
			 * other dies with valid data.
			 */
			devm_iounmap(dev, pd_info[i].sst_base);
			pd_info[i].sst_base = NULL;
			continue;
		}

		++inst;
	}

	if (!inst) {
		ret = -ENODEV;
		goto unlock_free;
	}

	tpmi_sst->package_id = pkg;

	tpmi_sst->power_domain_info[partition] = pd_info;
	tpmi_sst->number_of_power_domains[partition] = num_resources;
	tpmi_sst->cdie_mask[partition] = cdie_mask;
	tpmi_sst->io_dies[partition] = io_die_cnt;
	tpmi_sst->partition_mask |= BIT(partition);
	tpmi_sst->partition_mask_current |= BIT(partition);

	auxiliary_set_drvdata(auxdev, tpmi_sst);

	if (isst_common.max_index < pkg)
		isst_common.max_index = pkg;
	isst_common.sst_inst[pkg] = tpmi_sst;

unlock_free:
	if (ret && first_enum)
		kfree(tpmi_sst);
unlock_exit:
	mutex_unlock(&isst_tpmi_dev_lock);

	return ret;
}
EXPORT_SYMBOL_NS_GPL(tpmi_sst_dev_add, INTEL_TPMI_SST);

void tpmi_sst_dev_remove(struct auxiliary_device *auxdev)
{
	struct tpmi_sst_struct *tpmi_sst = auxiliary_get_drvdata(auxdev);
	struct intel_tpmi_plat_info *plat_info;

	plat_info = tpmi_get_platform_data(auxdev);
	if (!plat_info)
		return;

	mutex_lock(&isst_tpmi_dev_lock);
	tpmi_sst->power_domain_info[plat_info->partition] = NULL;
	tpmi_sst->partition_mask_current &= ~BIT(plat_info->partition);
	/* Free the package instance when the all partitions are removed */
	if (!tpmi_sst->partition_mask_current) {
		kfree(tpmi_sst);
		isst_common.sst_inst[tpmi_sst->package_id] = NULL;
	}
	mutex_unlock(&isst_tpmi_dev_lock);
}
EXPORT_SYMBOL_NS_GPL(tpmi_sst_dev_remove, INTEL_TPMI_SST);

void tpmi_sst_dev_suspend(struct auxiliary_device *auxdev)
{
	struct tpmi_sst_struct *tpmi_sst = auxiliary_get_drvdata(auxdev);
	struct tpmi_per_power_domain_info *power_domain_info;
	struct intel_tpmi_plat_info *plat_info;
	void __iomem *cp_base;

	plat_info = tpmi_get_platform_data(auxdev);
	if (!plat_info)
		return;

	power_domain_info = tpmi_sst->power_domain_info[plat_info->partition];

	cp_base = power_domain_info->sst_base + power_domain_info->sst_header.cp_offset;
	power_domain_info->saved_sst_cp_control = readq(cp_base + SST_CP_CONTROL_OFFSET);

	memcpy_fromio(power_domain_info->saved_clos_configs, cp_base + SST_CLOS_CONFIG_0_OFFSET,
		      sizeof(power_domain_info->saved_clos_configs));

	memcpy_fromio(power_domain_info->saved_clos_assocs, cp_base + SST_CLOS_ASSOC_0_OFFSET,
		      sizeof(power_domain_info->saved_clos_assocs));

	power_domain_info->saved_pp_control = readq(power_domain_info->sst_base +
						    power_domain_info->sst_header.pp_offset +
						    SST_PP_CONTROL_OFFSET);
}
EXPORT_SYMBOL_NS_GPL(tpmi_sst_dev_suspend, INTEL_TPMI_SST);

void tpmi_sst_dev_resume(struct auxiliary_device *auxdev)
{
	struct tpmi_sst_struct *tpmi_sst = auxiliary_get_drvdata(auxdev);
	struct tpmi_per_power_domain_info *power_domain_info;
	struct intel_tpmi_plat_info *plat_info;
	void __iomem *cp_base;

	plat_info = tpmi_get_platform_data(auxdev);
	if (!plat_info)
		return;

	power_domain_info = tpmi_sst->power_domain_info[plat_info->partition];

	cp_base = power_domain_info->sst_base + power_domain_info->sst_header.cp_offset;
	writeq(power_domain_info->saved_sst_cp_control, cp_base + SST_CP_CONTROL_OFFSET);

	memcpy_toio(cp_base + SST_CLOS_CONFIG_0_OFFSET, power_domain_info->saved_clos_configs,
		    sizeof(power_domain_info->saved_clos_configs));

	memcpy_toio(cp_base + SST_CLOS_ASSOC_0_OFFSET, power_domain_info->saved_clos_assocs,
		    sizeof(power_domain_info->saved_clos_assocs));

	writeq(power_domain_info->saved_pp_control, power_domain_info->sst_base +
				power_domain_info->sst_header.pp_offset + SST_PP_CONTROL_OFFSET);
}
EXPORT_SYMBOL_NS_GPL(tpmi_sst_dev_resume, INTEL_TPMI_SST);

#define ISST_TPMI_API_VERSION	0x03

int tpmi_sst_init(void)
{
	struct isst_if_cmd_cb cb;
	int ret = 0;

	mutex_lock(&isst_tpmi_dev_lock);

	if (isst_core_usage_count) {
		++isst_core_usage_count;
		goto init_done;
	}

	isst_common.sst_inst = kcalloc(topology_max_packages(),
				       sizeof(*isst_common.sst_inst),
				       GFP_KERNEL);
	if (!isst_common.sst_inst) {
		ret = -ENOMEM;
		goto init_done;
	}

	memset(&cb, 0, sizeof(cb));
	cb.cmd_size = sizeof(struct isst_if_io_reg);
	cb.offset = offsetof(struct isst_if_io_regs, io_reg);
	cb.cmd_callback = NULL;
	cb.api_version = ISST_TPMI_API_VERSION;
	cb.def_ioctl = isst_if_def_ioctl;
	cb.owner = THIS_MODULE;
	ret = isst_if_cdev_register(ISST_IF_DEV_TPMI, &cb);
	if (ret)
		kfree(isst_common.sst_inst);
	else
		++isst_core_usage_count;
init_done:
	mutex_unlock(&isst_tpmi_dev_lock);
	return ret;
}
EXPORT_SYMBOL_NS_GPL(tpmi_sst_init, INTEL_TPMI_SST);

void tpmi_sst_exit(void)
{
	mutex_lock(&isst_tpmi_dev_lock);
	if (isst_core_usage_count)
		--isst_core_usage_count;

	if (!isst_core_usage_count) {
		isst_if_cdev_unregister(ISST_IF_DEV_TPMI);
		kfree(isst_common.sst_inst);
	}
	mutex_unlock(&isst_tpmi_dev_lock);
}
EXPORT_SYMBOL_NS_GPL(tpmi_sst_exit, INTEL_TPMI_SST);

MODULE_IMPORT_NS(INTEL_TPMI);
MODULE_IMPORT_NS(INTEL_TPMI_POWER_DOMAIN);

MODULE_DESCRIPTION("ISST TPMI interface module");
MODULE_LICENSE("GPL");
