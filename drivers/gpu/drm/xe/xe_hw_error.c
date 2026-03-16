// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include <linux/bitmap.h>
#include <linux/fault-inject.h>

#include "regs/xe_gsc_regs.h"
#include "regs/xe_hw_error_regs.h"
#include "regs/xe_irq_regs.h"

#include "xe_device.h"
#include "xe_drm_ras.h"
#include "xe_hw_error.h"
#include "xe_mmio.h"
#include "xe_survivability_mode.h"

#define GT_HW_ERROR_MAX_ERR_BITS		16
#define HEC_UNCORR_FW_ERR_BITS			4
#define XE_RAS_REG_SIZE				32
#define XE_SOC_NUM_IEH				2

#define PVC_ERROR_MASK_SET(hw_err, err_bit)	((hw_err == HARDWARE_ERROR_CORRECTABLE) ? \
						 (PVC_COR_ERR_MASK & REG_BIT(err_bit)) : \
						 (PVC_FAT_ERR_MASK & REG_BIT(err_bit)))

extern struct fault_attr inject_csc_hw_error;

static const char * const error_severity[] = DRM_XE_RAS_ERROR_SEVERITY_NAMES;

static const char * const hec_uncorrected_fw_errors[] = {
	"Fatal",
	"CSE Disabled",
	"FD Corruption",
	"Data Corruption"
};

static const unsigned long xe_hw_error_map[] = {
	[XE_GT_ERROR]	= DRM_XE_RAS_ERR_COMP_CORE_COMPUTE,
	[XE_SOC_ERROR]	= DRM_XE_RAS_ERR_COMP_SOC_INTERNAL,
};

enum gt_vector_regs {
	ERR_STAT_GT_VECTOR0 = 0,
	ERR_STAT_GT_VECTOR1,
	ERR_STAT_GT_VECTOR2,
	ERR_STAT_GT_VECTOR3,
	ERR_STAT_GT_VECTOR4,
	ERR_STAT_GT_VECTOR5,
	ERR_STAT_GT_VECTOR6,
	ERR_STAT_GT_VECTOR7,
	ERR_STAT_GT_VECTOR_MAX
};

#define PVC_GT_VECTOR_LEN(hw_err)	((hw_err == HARDWARE_ERROR_CORRECTABLE) ? \
					 ERR_STAT_GT_VECTOR4 : ERR_STAT_GT_VECTOR_MAX)

static enum drm_xe_ras_error_severity hw_err_to_severity(const enum hardware_error hw_err)
{
	if (hw_err == HARDWARE_ERROR_CORRECTABLE)
		return DRM_XE_RAS_ERR_SEV_CORRECTABLE;

	/* Uncorrectable errors comprise of both fatal and non-fatal errors */
	return DRM_XE_RAS_ERR_SEV_UNCORRECTABLE;
}

static const char * const pvc_master_global_err_reg[] = {
	[0 ... 1]	= "Undefined",
	[2]		= "HBM SS0: Channel0",
	[3]		= "HBM SS0: Channel1",
	[4]		= "HBM SS0: Channel2",
	[5]		= "HBM SS0: Channel3",
	[6]		= "HBM SS0: Channel4",
	[7]		= "HBM SS0: Channel5",
	[8]		= "HBM SS0: Channel6",
	[9]		= "HBM SS0: Channel7",
	[10]		= "HBM SS1: Channel0",
	[11]		= "HBM SS1: Channel1",
	[12]		= "HBM SS1: Channel2",
	[13]		= "HBM SS1: Channel3",
	[14]		= "HBM SS1: Channel4",
	[15]		= "HBM SS1: Channel5",
	[16]		= "HBM SS1: Channel6",
	[17]		= "HBM SS1: Channel7",
	[18 ... 31]	= "Undefined",
};
static_assert(ARRAY_SIZE(pvc_master_global_err_reg) == XE_RAS_REG_SIZE);

static const char * const pvc_slave_global_err_reg[] = {
	[0]		= "Undefined",
	[1]		= "HBM SS2: Channel0",
	[2]		= "HBM SS2: Channel1",
	[3]		= "HBM SS2: Channel2",
	[4]		= "HBM SS2: Channel3",
	[5]		= "HBM SS2: Channel4",
	[6]		= "HBM SS2: Channel5",
	[7]		= "HBM SS2: Channel6",
	[8]		= "HBM SS2: Channel7",
	[9]		= "HBM SS3: Channel0",
	[10]		= "HBM SS3: Channel1",
	[11]		= "HBM SS3: Channel2",
	[12]		= "HBM SS3: Channel3",
	[13]		= "HBM SS3: Channel4",
	[14]		= "HBM SS3: Channel5",
	[15]		= "HBM SS3: Channel6",
	[16]		= "HBM SS3: Channel7",
	[17]		= "Undefined",
	[18]		= "ANR MDFI",
	[19 ... 31]	= "Undefined",
};
static_assert(ARRAY_SIZE(pvc_slave_global_err_reg) == XE_RAS_REG_SIZE);

static const char * const pvc_slave_local_fatal_err_reg[] = {
	[0]		= "Local IEH: Malformed PCIe AER",
	[1]		= "Local IEH: Malformed PCIe ERR",
	[2]		= "Local IEH: UR conditions in IEH",
	[3]		= "Local IEH: From SERR Sources",
	[4 ... 19]	= "Undefined",
	[20]		= "Malformed MCA error packet (HBM/Punit)",
	[21 ... 31]	= "Undefined",
};
static_assert(ARRAY_SIZE(pvc_slave_local_fatal_err_reg) == XE_RAS_REG_SIZE);

static const char * const pvc_master_local_fatal_err_reg[] = {
	[0]		= "Local IEH: Malformed IOSF PCIe AER",
	[1]		= "Local IEH: Malformed IOSF PCIe ERR",
	[2]		= "Local IEH: UR RESPONSE",
	[3]		= "Local IEH: From SERR SPI controller",
	[4]		= "Base Die MDFI T2T",
	[5]		= "Undefined",
	[6]		= "Base Die MDFI T2C",
	[7]		= "Undefined",
	[8]		= "Invalid CSC PSF Command Parity",
	[9]		= "Invalid CSC PSF Unexpected Completion",
	[10]		= "Invalid CSC PSF Unsupported Request",
	[11]		= "Invalid PCIe PSF Command Parity",
	[12]		= "PCIe PSF Unexpected Completion",
	[13]		= "PCIe PSF Unsupported Request",
	[14 ... 19]	= "Undefined",
	[20]		= "Malformed MCA error packet (HBM/Punit)",
	[21 ... 31]	= "Undefined",
};
static_assert(ARRAY_SIZE(pvc_master_local_fatal_err_reg) == XE_RAS_REG_SIZE);

static const char * const pvc_master_local_nonfatal_err_reg[] = {
	[0 ... 3]	= "Undefined",
	[4]		= "Base Die MDFI T2T",
	[5]		= "Undefined",
	[6]		= "Base Die MDFI T2C",
	[7]		= "Undefined",
	[8]		= "Invalid CSC PSF Command Parity",
	[9]		= "Invalid CSC PSF Unexpected Completion",
	[10]		= "Invalid PCIe PSF Command Parity",
	[11 ... 31]	= "Undefined",
};
static_assert(ARRAY_SIZE(pvc_master_local_nonfatal_err_reg) == XE_RAS_REG_SIZE);

#define PVC_MASTER_LOCAL_REG_INFO(hw_err)	((hw_err == HARDWARE_ERROR_FATAL) ? \
						 pvc_master_local_fatal_err_reg : \
						 pvc_master_local_nonfatal_err_reg)

static bool fault_inject_csc_hw_error(void)
{
	return IS_ENABLED(CONFIG_DEBUG_FS) && should_fail(&inject_csc_hw_error, 1);
}

static void csc_hw_error_work(struct work_struct *work)
{
	struct xe_tile *tile = container_of(work, typeof(*tile), csc_hw_error_work);
	struct xe_device *xe = tile_to_xe(tile);
	int ret;

	ret = xe_survivability_mode_runtime_enable(xe);
	if (ret)
		drm_err(&xe->drm, "Failed to enable runtime survivability mode\n");
}

static void csc_hw_error_handler(struct xe_tile *tile, const enum hardware_error hw_err)
{
	const enum drm_xe_ras_error_severity severity = hw_err_to_severity(hw_err);
	const char *severity_str = error_severity[severity];
	struct xe_device *xe = tile_to_xe(tile);
	struct xe_mmio *mmio = &tile->mmio;
	u32 base, err_bit, err_src;
	unsigned long fw_err;

	if (xe->info.platform != XE_BATTLEMAGE)
		return;

	base = BMG_GSC_HECI1_BASE;
	lockdep_assert_held(&xe->irq.lock);
	err_src = xe_mmio_read32(mmio, HEC_UNCORR_ERR_STATUS(base));
	if (!err_src) {
		drm_err_ratelimited(&xe->drm, HW_ERR "Tile%d reported %s HEC_ERR_STATUS register blank\n",
				    tile->id, severity_str);
		return;
	}

	if (err_src & UNCORR_FW_REPORTED_ERR) {
		fw_err = xe_mmio_read32(mmio, HEC_UNCORR_FW_ERR_DW0(base));
		for_each_set_bit(err_bit, &fw_err, HEC_UNCORR_FW_ERR_BITS) {
			drm_err_ratelimited(&xe->drm, HW_ERR
					    "HEC FW %s %s reported, bit[%d] is set\n",
					     hec_uncorrected_fw_errors[err_bit], severity_str,
					     err_bit);

			schedule_work(&tile->csc_hw_error_work);
		}
	}

	xe_mmio_write32(mmio, HEC_UNCORR_ERR_STATUS(base), err_src);
}

static void log_hw_error(struct xe_tile *tile, const char *name,
			 const enum drm_xe_ras_error_severity severity)
{
	const char *severity_str = error_severity[severity];
	struct xe_device *xe = tile_to_xe(tile);

	if (severity == DRM_XE_RAS_ERR_SEV_CORRECTABLE)
		drm_warn(&xe->drm, "%s %s detected\n", name, severity_str);
	else
		drm_err_ratelimited(&xe->drm, "%s %s detected\n", name, severity_str);
}

static void log_gt_err(struct xe_tile *tile, const char *name, int i, u32 err,
		       const enum drm_xe_ras_error_severity severity)
{
	const char *severity_str = error_severity[severity];
	struct xe_device *xe = tile_to_xe(tile);

	if (severity == DRM_XE_RAS_ERR_SEV_CORRECTABLE)
		drm_warn(&xe->drm, "%s %s detected, ERROR_STAT_GT_VECTOR%d:0x%08x\n",
			 name, severity_str, i, err);
	else
		drm_err_ratelimited(&xe->drm, "%s %s detected, ERROR_STAT_GT_VECTOR%d:0x%08x\n",
				    name, severity_str, i, err);
}

static void log_soc_error(struct xe_tile *tile, const char * const *reg_info,
			  const enum drm_xe_ras_error_severity severity, u32 err_bit, u32 index)
{
	const char *severity_str = error_severity[severity];
	struct xe_device *xe = tile_to_xe(tile);
	struct xe_drm_ras *ras = &xe->ras;
	struct xe_drm_ras_counter *info = ras->info[severity];
	const char *name;

	name = reg_info[err_bit];

	if (strcmp(name, "Undefined")) {
		if (severity == DRM_XE_RAS_ERR_SEV_CORRECTABLE)
			drm_warn(&xe->drm, "%s SOC %s detected", name, severity_str);
		else
			drm_err_ratelimited(&xe->drm, "%s SOC %s detected", name, severity_str);
		atomic_inc(&info[index].counter);
	}
}

static void gt_hw_error_handler(struct xe_tile *tile, const enum hardware_error hw_err,
				u32 error_id)
{
	const enum drm_xe_ras_error_severity severity = hw_err_to_severity(hw_err);
	struct xe_device *xe = tile_to_xe(tile);
	struct xe_drm_ras *ras = &xe->ras;
	struct xe_drm_ras_counter *info = ras->info[severity];
	struct xe_mmio *mmio = &tile->mmio;
	unsigned long err_stat = 0;
	int i;

	if (xe->info.platform != XE_PVC)
		return;

	if (hw_err == HARDWARE_ERROR_NONFATAL) {
		atomic_inc(&info[error_id].counter);
		log_hw_error(tile, info[error_id].name, severity);
		return;
	}

	for (i = 0; i < PVC_GT_VECTOR_LEN(hw_err); i++) {
		u32 vector, val;

		vector = xe_mmio_read32(mmio, ERR_STAT_GT_VECTOR_REG(hw_err, i));
		if (!vector)
			continue;

		switch (i) {
		case ERR_STAT_GT_VECTOR0:
		case ERR_STAT_GT_VECTOR1: {
			u32 errbit;

			val = hweight32(vector);
			atomic_add(val, &info[error_id].counter);
			log_gt_err(tile, "Subslice", i, vector, severity);

			/*
			 * Error status register is only populated once per error.
			 * Read the register and clear once.
			 */
			if (err_stat)
				break;

			err_stat = xe_mmio_read32(mmio, ERR_STAT_GT_REG(hw_err));
			for_each_set_bit(errbit, &err_stat, GT_HW_ERROR_MAX_ERR_BITS) {
				if (PVC_ERROR_MASK_SET(hw_err, errbit))
					atomic_inc(&info[error_id].counter);
			}
			if (err_stat)
				xe_mmio_write32(mmio, ERR_STAT_GT_REG(hw_err), err_stat);
			break;
		}
		case ERR_STAT_GT_VECTOR2:
		case ERR_STAT_GT_VECTOR3:
			val = hweight32(vector);
			atomic_add(val, &info[error_id].counter);
			log_gt_err(tile, "L3 BANK", i, vector, severity);
			break;
		case ERR_STAT_GT_VECTOR6:
			val = hweight32(vector);
			atomic_add(val, &info[error_id].counter);
			log_gt_err(tile, "TLB", i, vector, severity);
			break;
		case ERR_STAT_GT_VECTOR7:
			val = hweight32(vector);
			atomic_add(val, &info[error_id].counter);
			log_gt_err(tile, "L3 Fabric", i, vector, severity);
			break;
		default:
			log_gt_err(tile, "Undefined", i, vector, severity);
		}

		xe_mmio_write32(mmio, ERR_STAT_GT_VECTOR_REG(hw_err, i), vector);
	}
}

static void soc_slave_ieh_handler(struct xe_tile *tile, const enum hardware_error hw_err, u32 error_id)
{
	const enum drm_xe_ras_error_severity severity = hw_err_to_severity(hw_err);
	unsigned long slave_global_errstat, slave_local_errstat;
	struct xe_mmio *mmio = &tile->mmio;
	u32 regbit, slave;

	slave = SOC_PVC_SLAVE_BASE;
	slave_global_errstat = xe_mmio_read32(mmio, SOC_GLOBAL_ERR_STAT_REG(slave, hw_err));

	if (slave_global_errstat & SOC_IEH1_LOCAL_ERR_STATUS) {
		slave_local_errstat = xe_mmio_read32(mmio, SOC_LOCAL_ERR_STAT_REG(slave, hw_err));

		if (hw_err == HARDWARE_ERROR_FATAL) {
			for_each_set_bit(regbit, &slave_local_errstat, XE_RAS_REG_SIZE)
				log_soc_error(tile, pvc_slave_local_fatal_err_reg, severity,
					      regbit, error_id);
		}

		xe_mmio_write32(mmio, SOC_LOCAL_ERR_STAT_REG(slave, hw_err),
				slave_local_errstat);
	}

	for_each_set_bit(regbit, &slave_global_errstat, XE_RAS_REG_SIZE)
		log_soc_error(tile, pvc_slave_global_err_reg, severity, regbit, error_id);

	xe_mmio_write32(mmio, SOC_GLOBAL_ERR_STAT_REG(slave, hw_err), slave_global_errstat);
}

static void soc_hw_error_handler(struct xe_tile *tile, const enum hardware_error hw_err,
				 u32 error_id)
{
	const enum drm_xe_ras_error_severity severity = hw_err_to_severity(hw_err);
	struct xe_device *xe = tile_to_xe(tile);
	struct xe_mmio *mmio = &tile->mmio;
	unsigned long master_global_errstat, master_local_errstat;
	u32 master, slave, regbit;
	int i;

	if (xe->info.platform != XE_PVC)
		return;

	master = SOC_PVC_MASTER_BASE;
	slave = SOC_PVC_SLAVE_BASE;

	/* Mask error type in GSYSEVTCTL so that no new errors of the type will be reported */
	for (i = 0; i < XE_SOC_NUM_IEH; i++)
		xe_mmio_write32(mmio, SOC_GSYSEVTCTL_REG(master, slave, i), ~REG_BIT(hw_err));

	if (hw_err == HARDWARE_ERROR_CORRECTABLE) {
		xe_mmio_write32(mmio, SOC_GLOBAL_ERR_STAT_REG(master, hw_err), REG_GENMASK(31, 0));
		xe_mmio_write32(mmio, SOC_LOCAL_ERR_STAT_REG(master, hw_err), REG_GENMASK(31, 0));
		xe_mmio_write32(mmio, SOC_GLOBAL_ERR_STAT_REG(slave, hw_err), REG_GENMASK(31, 0));
		xe_mmio_write32(mmio, SOC_LOCAL_ERR_STAT_REG(slave, hw_err), REG_GENMASK(31, 0));
		goto unmask_gsysevtctl;
	}

	/*
	 * Read the master global IEH error register, if BIT(1) is set then process
	 * the slave IEH first. If BIT(0) in global error register is set then process
	 * the corresponding local error registers.
	 */
	master_global_errstat = xe_mmio_read32(mmio, SOC_GLOBAL_ERR_STAT_REG(master, hw_err));
	if (master_global_errstat & SOC_SLAVE_IEH)
		soc_slave_ieh_handler(tile, hw_err, error_id);

	if (master_global_errstat & SOC_IEH0_LOCAL_ERR_STATUS) {
		master_local_errstat = xe_mmio_read32(mmio, SOC_LOCAL_ERR_STAT_REG(master, hw_err));

		for_each_set_bit(regbit, &master_local_errstat, XE_RAS_REG_SIZE)
			log_soc_error(tile, PVC_MASTER_LOCAL_REG_INFO(hw_err), severity, regbit, error_id);

		xe_mmio_write32(mmio, SOC_LOCAL_ERR_STAT_REG(master, hw_err), master_local_errstat);
	}

	for_each_set_bit(regbit, &master_global_errstat, XE_RAS_REG_SIZE)
		log_soc_error(tile, pvc_master_global_err_reg, severity, regbit, error_id);

	xe_mmio_write32(mmio, SOC_GLOBAL_ERR_STAT_REG(master, hw_err), master_global_errstat);

unmask_gsysevtctl:
	for (i = 0; i < XE_SOC_NUM_IEH; i++)
		xe_mmio_write32(mmio, SOC_GSYSEVTCTL_REG(master, slave, i),
				(HARDWARE_ERROR_MAX << 1) + 1);
}

static void hw_error_source_handler(struct xe_tile *tile, const enum hardware_error hw_err)
{
	const enum drm_xe_ras_error_severity severity = hw_err_to_severity(hw_err);
	const char *severity_str = error_severity[severity];
	struct xe_device *xe = tile_to_xe(tile);
	struct xe_drm_ras *ras = &xe->ras;
	struct xe_drm_ras_counter *info = ras->info[severity];
	unsigned long flags, err_src;
	u32 err_bit;

	if (!IS_DGFX(xe))
		return;

	spin_lock_irqsave(&xe->irq.lock, flags);
	err_src = xe_mmio_read32(&tile->mmio, DEV_ERR_STAT_REG(hw_err));
	if (!err_src) {
		drm_err_ratelimited(&xe->drm, HW_ERR "Tile%d reported %s DEV_ERR_STAT register blank!\n",
				    tile->id, severity_str);
		goto unlock;
	}

	/*
	 * On encountering CSC firmware errors, the graphics device becomes unrecoverable
	 * so return immediately on error. The only way to recover from these errors is
	 * firmware flash. The device will enter Runtime Survivability mode when such
	 * errors are detected.
	 */
	if (err_src & REG_BIT(XE_CSC_ERROR)) {
		csc_hw_error_handler(tile, hw_err);
		goto clear_reg;
	}

	if (!info)
		goto clear_reg;

	for_each_set_bit(err_bit, &err_src, XE_RAS_REG_SIZE) {
		const char *name;
		u32 error_id;

		/* Check error bit is within bounds */
		if (err_bit >= ARRAY_SIZE(xe_hw_error_map))
			break;

		error_id = xe_hw_error_map[err_bit];

		/* Check error component is within max */
		if (!error_id || error_id >= DRM_XE_RAS_ERR_COMP_MAX)
			continue;

		name = info[error_id].name;
		if (!name)
			continue;

		if (severity == DRM_XE_RAS_ERR_SEV_CORRECTABLE) {
			drm_warn(&xe->drm, HW_ERR
				 "TILE%d reported %s %s, bit[%d] is set\n",
				 tile->id, name, severity_str, err_bit);
		} else {
			drm_err_ratelimited(&xe->drm, HW_ERR
					    "TILE%d reported %s %s, bit[%d] is set\n",
					    tile->id, name, severity_str, err_bit);
		}

		if (err_bit == XE_GT_ERROR)
			gt_hw_error_handler(tile, hw_err, error_id);
		if (err_bit == XE_SOC_ERROR)
			soc_hw_error_handler(tile, hw_err, error_id);
	}

clear_reg:
	xe_mmio_write32(&tile->mmio, DEV_ERR_STAT_REG(hw_err), err_src);
unlock:
	spin_unlock_irqrestore(&xe->irq.lock, flags);
}

/**
 * xe_hw_error_irq_handler - irq handling for hw errors
 * @tile: tile instance
 * @master_ctl: value read from master interrupt register
 *
 * Xe platforms add three error bits to the master interrupt register to support error handling.
 * These three bits are used to convey the class of error FATAL, NONFATAL, or CORRECTABLE.
 * To process the interrupt, determine the source of error by reading the Device Error Source
 * Register that corresponds to the class of error being serviced.
 */
void xe_hw_error_irq_handler(struct xe_tile *tile, const u32 master_ctl)
{
	enum hardware_error hw_err;

	if (fault_inject_csc_hw_error())
		schedule_work(&tile->csc_hw_error_work);

	for (hw_err = 0; hw_err < HARDWARE_ERROR_MAX; hw_err++) {
		if (master_ctl & ERROR_IRQ(hw_err))
			hw_error_source_handler(tile, hw_err);
	}
}

static int hw_error_info_init(struct xe_device *xe)
{
	if (xe->info.platform != XE_PVC)
		return 0;

	return xe_drm_ras_init(xe);
}

/*
 * Process hardware errors during boot
 */
static void process_hw_errors(struct xe_device *xe)
{
	struct xe_tile *tile;
	u32 master_ctl;
	u8 id;

	for_each_tile(tile, xe, id) {
		master_ctl = xe_mmio_read32(&tile->mmio, GFX_MSTR_IRQ);
		xe_hw_error_irq_handler(tile, master_ctl);
		xe_mmio_write32(&tile->mmio, GFX_MSTR_IRQ, master_ctl);
	}
}

/**
 * xe_hw_error_init - Initialize hw errors
 * @xe: xe device instance
 *
 * Initialize and check for errors that occurred during boot
 * prior to driver load
 */
void xe_hw_error_init(struct xe_device *xe)
{
	struct xe_tile *tile = xe_device_get_root_tile(xe);
	int ret;

	if (!IS_DGFX(xe) || IS_SRIOV_VF(xe))
		return;

	INIT_WORK(&tile->csc_hw_error_work, csc_hw_error_work);

	ret = hw_error_info_init(xe);
	if (ret)
		drm_err(&xe->drm, "Failed to initialize XE DRM RAS (%pe)\n", ERR_PTR(ret));

	process_hw_errors(xe);
}
