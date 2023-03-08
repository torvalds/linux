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

#include <linux/auxiliary_bus.h>
#include <linux/intel_tpmi.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <uapi/linux/isst_if.h>

#include "isst_tpmi_core.h"
#include "isst_if_common.h"

/* Supported SST hardware version by this driver */
#define ISST_HEADER_VERSION		1

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
};

/**
 * struct tpmi_sst_struct -	Store sst info for a package
 * @package_id:			Package id for this aux device instance
 * @number_of_power_domains:	Number of power_domains pointed by power_domain_info pointer
 * @power_domain_info:		Pointer to power domains information
 *
 * This structure is used store full SST information for a package.
 * Each package has a unique OOB PCI device, which enumerates TPMI.
 * Each Package will have multiple power_domains.
 */
struct tpmi_sst_struct {
	int package_id;
	int number_of_power_domains;
	struct tpmi_per_power_domain_info *power_domain_info;
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
	u64 perf_level_offsets;
	int i;

	pd_info->perf_levels = devm_kcalloc(&auxdev->dev, levels,
					    sizeof(struct perf_level),
					    GFP_KERNEL);
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

		offset = perf_level_offsets & (0xff << (i * SST_PP_OFFSET_SIZE));
		offset >>= (i * 8);
		offset &= 0xff;
		offset *= 8; /* Convert to byte from QWORD offset */
		pd_info->perf_levels[i].mmio_offset = pd_info->sst_header.pp_offset + offset;
	}

	return 0;
}

static int sst_main(struct auxiliary_device *auxdev, struct tpmi_per_power_domain_info *pd_info)
{
	int i, mask, levels;

	*((u64 *)&pd_info->sst_header) = readq(pd_info->sst_base);
	pd_info->sst_header.cp_offset *= 8;
	pd_info->sst_header.pp_offset *= 8;

	if (pd_info->sst_header.interface_version != ISST_HEADER_VERSION) {
		dev_err(&auxdev->dev, "SST: Unsupported version:%x\n",
			pd_info->sst_header.interface_version);
		return -ENODEV;
	}

	/* Read SST CP Header */
	*((u64 *)&pd_info->cp_header) = readq(pd_info->sst_base + pd_info->sst_header.cp_offset);

	/* Read PP header */
	*((u64 *)&pd_info->pp_header) = readq(pd_info->sst_base + pd_info->sst_header.pp_offset);

	/* Force level_en_mask level 0 */
	pd_info->pp_header.level_en_mask |= 0x01;

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

/*
 * Map a package and power_domain id to SST information structure unique for a power_domain.
 * The caller should call under isst_tpmi_dev_lock.
 */
static struct tpmi_per_power_domain_info *get_instance(int pkg_id, int power_domain_id)
{
	struct tpmi_per_power_domain_info *power_domain_info;
	struct tpmi_sst_struct *sst_inst;

	if (pkg_id < 0 || pkg_id > isst_common.max_index ||
	    pkg_id >= topology_max_packages())
		return NULL;

	sst_inst = isst_common.sst_inst[pkg_id];
	if (!sst_inst)
		return NULL;

	if (power_domain_id < 0 || power_domain_id >= sst_inst->number_of_power_domains)
		return NULL;

	power_domain_info = &sst_inst->power_domain_info[power_domain_id];

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

	if (disable_dynamic_sst_features())
		return -EFAULT;

	if (copy_from_user(&core_power, argp, sizeof(core_power)))
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

		if (clos_assoc.power_domain_id > sst_inst->number_of_power_domains)
			return -EINVAL;

		power_domain_info = &sst_inst->power_domain_info[punit_id];

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

static int isst_if_get_tpmi_instance_count(void __user *argp)
{
	struct isst_tpmi_instance_count tpmi_inst;
	struct tpmi_sst_struct *sst_inst;
	int i;

	if (copy_from_user(&tpmi_inst, argp, sizeof(tpmi_inst)))
		return -EFAULT;

	if (tpmi_inst.socket_id >= topology_max_packages())
		return -EINVAL;

	tpmi_inst.count = isst_common.sst_inst[tpmi_inst.socket_id]->number_of_power_domains;

	sst_inst = isst_common.sst_inst[tpmi_inst.socket_id];
	tpmi_inst.valid_mask = 0;
	for (i = 0; i < sst_inst->number_of_power_domains; ++i) {
		struct tpmi_per_power_domain_info *pd_info;

		pd_info = &sst_inst->power_domain_info[i];
		if (pd_info->sst_base)
			tpmi_inst.valid_mask |= BIT(i);
	}

	if (copy_to_user(argp, &tpmi_inst, sizeof(tpmi_inst)))
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
	default:
		break;
	}
	mutex_unlock(&isst_tpmi_dev_lock);

	return ret;
}

#define TPMI_SST_AUTO_SUSPEND_DELAY_MS	2000

int tpmi_sst_dev_add(struct auxiliary_device *auxdev)
{
	struct intel_tpmi_plat_info *plat_info;
	struct tpmi_sst_struct *tpmi_sst;
	int i, ret, pkg = 0, inst = 0;
	int num_resources;

	plat_info = tpmi_get_platform_data(auxdev);
	if (!plat_info) {
		dev_err(&auxdev->dev, "No platform info\n");
		return -EINVAL;
	}

	pkg = plat_info->package_id;
	if (pkg >= topology_max_packages()) {
		dev_err(&auxdev->dev, "Invalid package id :%x\n", pkg);
		return -EINVAL;
	}

	if (isst_common.sst_inst[pkg])
		return -EEXIST;

	num_resources = tpmi_get_resource_count(auxdev);

	if (!num_resources)
		return -EINVAL;

	tpmi_sst = devm_kzalloc(&auxdev->dev, sizeof(*tpmi_sst), GFP_KERNEL);
	if (!tpmi_sst)
		return -ENOMEM;

	tpmi_sst->power_domain_info = devm_kcalloc(&auxdev->dev, num_resources,
						   sizeof(*tpmi_sst->power_domain_info),
						   GFP_KERNEL);
	if (!tpmi_sst->power_domain_info)
		return -ENOMEM;

	tpmi_sst->number_of_power_domains = num_resources;

	for (i = 0; i < num_resources; ++i) {
		struct resource *res;

		res = tpmi_get_resource_at_index(auxdev, i);
		if (!res) {
			tpmi_sst->power_domain_info[i].sst_base = NULL;
			continue;
		}

		tpmi_sst->power_domain_info[i].package_id = pkg;
		tpmi_sst->power_domain_info[i].power_domain_id = i;
		tpmi_sst->power_domain_info[i].auxdev = auxdev;
		tpmi_sst->power_domain_info[i].sst_base = devm_ioremap_resource(&auxdev->dev, res);
		if (IS_ERR(tpmi_sst->power_domain_info[i].sst_base))
			return PTR_ERR(tpmi_sst->power_domain_info[i].sst_base);

		ret = sst_main(auxdev, &tpmi_sst->power_domain_info[i]);
		if (ret) {
			devm_iounmap(&auxdev->dev, tpmi_sst->power_domain_info[i].sst_base);
			tpmi_sst->power_domain_info[i].sst_base =  NULL;
			continue;
		}

		++inst;
	}

	if (!inst)
		return -ENODEV;

	tpmi_sst->package_id = pkg;
	auxiliary_set_drvdata(auxdev, tpmi_sst);

	mutex_lock(&isst_tpmi_dev_lock);
	if (isst_common.max_index < pkg)
		isst_common.max_index = pkg;
	isst_common.sst_inst[pkg] = tpmi_sst;
	mutex_unlock(&isst_tpmi_dev_lock);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(tpmi_sst_dev_add, INTEL_TPMI_SST);

void tpmi_sst_dev_remove(struct auxiliary_device *auxdev)
{
	struct tpmi_sst_struct *tpmi_sst = auxiliary_get_drvdata(auxdev);

	mutex_lock(&isst_tpmi_dev_lock);
	isst_common.sst_inst[tpmi_sst->package_id] = NULL;
	mutex_unlock(&isst_tpmi_dev_lock);
}
EXPORT_SYMBOL_NS_GPL(tpmi_sst_dev_remove, INTEL_TPMI_SST);

#define ISST_TPMI_API_VERSION	0x02

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
	if (!isst_common.sst_inst)
		return -ENOMEM;

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

MODULE_LICENSE("GPL");
