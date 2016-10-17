/*
 *
 * (C) COPYRIGHT 2010-2016 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */





/*
 * Base kernel Power Management hardware control
 */

#include <mali_kbase.h>
#include <mali_kbase_config_defaults.h>
#include <mali_midg_regmap.h>
#if defined(CONFIG_MALI_GATOR_SUPPORT)
#include <mali_kbase_gator.h>
#endif
#include <mali_kbase_tlstream.h>
#include <mali_kbase_pm.h>
#include <mali_kbase_config_defaults.h>
#include <mali_kbase_smc.h>
#include <mali_kbase_hwaccess_jm.h>
#include <backend/gpu/mali_kbase_cache_policy_backend.h>
#include <backend/gpu/mali_kbase_device_internal.h>
#include <backend/gpu/mali_kbase_irq_internal.h>
#include <backend/gpu/mali_kbase_pm_internal.h>

#include <linux/of.h>

#if MALI_MOCK_TEST
#define MOCKABLE(function) function##_original
#else
#define MOCKABLE(function) function
#endif				/* MALI_MOCK_TEST */

/* Special value to indicate that the JM_CONFIG reg isn't currently used. */
#define KBASE_JM_CONFIG_UNUSED (1<<31)

/**
 * enum kbasep_pm_action - Actions that can be performed on a core.
 *
 * This enumeration is private to the file. Its values are set to allow
 * core_type_to_reg() function, which decodes this enumeration, to be simpler
 * and more efficient.
 *
 * @ACTION_PRESENT: The cores that are present
 * @ACTION_READY: The cores that are ready
 * @ACTION_PWRON: Power on the cores specified
 * @ACTION_PWROFF: Power off the cores specified
 * @ACTION_PWRTRANS: The cores that are transitioning
 * @ACTION_PWRACTIVE: The cores that are active
 */
enum kbasep_pm_action {
	ACTION_PRESENT = 0,
	ACTION_READY = (SHADER_READY_LO - SHADER_PRESENT_LO),
	ACTION_PWRON = (SHADER_PWRON_LO - SHADER_PRESENT_LO),
	ACTION_PWROFF = (SHADER_PWROFF_LO - SHADER_PRESENT_LO),
	ACTION_PWRTRANS = (SHADER_PWRTRANS_LO - SHADER_PRESENT_LO),
	ACTION_PWRACTIVE = (SHADER_PWRACTIVE_LO - SHADER_PRESENT_LO)
};

static u64 kbase_pm_get_state(
		struct kbase_device *kbdev,
		enum kbase_pm_core_type core_type,
		enum kbasep_pm_action action);

/**
 * core_type_to_reg - Decode a core type and action to a register.
 *
 * Given a core type (defined by kbase_pm_core_type) and an action (defined
 * by kbasep_pm_action) this function will return the register offset that
 * will perform the action on the core type. The register returned is the _LO
 * register and an offset must be applied to use the _HI register.
 *
 * @core_type: The type of core
 * @action:    The type of action
 *
 * Return: The register offset of the _LO register that performs an action of
 * type @action on a core of type @core_type.
 */
static u32 core_type_to_reg(enum kbase_pm_core_type core_type,
						enum kbasep_pm_action action)
{
	return (u32)core_type + (u32)action;
}

#ifdef CONFIG_ARM64
static void mali_cci_flush_l2(struct kbase_device *kbdev)
{
	const u32 mask = CLEAN_CACHES_COMPLETED | RESET_COMPLETED;
	u32 loops = KBASE_CLEAN_CACHE_MAX_LOOPS;
	u32 raw;

	/*
	 * Note that we don't take the cache flush mutex here since
	 * we expect to be the last user of the L2, all other L2 users
	 * would have dropped their references, to initiate L2 power
	 * down, L2 power down being the only valid place for this
	 * to be called from.
	 */

	kbase_reg_write(kbdev,
			GPU_CONTROL_REG(GPU_COMMAND),
			GPU_COMMAND_CLEAN_INV_CACHES,
			NULL);

	raw = kbase_reg_read(kbdev,
		GPU_CONTROL_REG(GPU_IRQ_RAWSTAT),
		NULL);

	/* Wait for cache flush to complete before continuing, exit on
	 * gpu resets or loop expiry. */
	while (((raw & mask) == 0) && --loops) {
		raw = kbase_reg_read(kbdev,
					GPU_CONTROL_REG(GPU_IRQ_RAWSTAT),
					NULL);
	}
}
#endif

/**
 * kbase_pm_invoke - Invokes an action on a core set
 *
 * This function performs the action given by @action on a set of cores of a
 * type given by @core_type. It is a static function used by
 * kbase_pm_transition_core_type()
 *
 * @kbdev:     The kbase device structure of the device
 * @core_type: The type of core that the action should be performed on
 * @cores:     A bit mask of cores to perform the action on (low 32 bits)
 * @action:    The action to perform on the cores
 */
static void kbase_pm_invoke(struct kbase_device *kbdev,
					enum kbase_pm_core_type core_type,
					u64 cores,
					enum kbasep_pm_action action)
{
	u32 reg;
	u32 lo = cores & 0xFFFFFFFF;
	u32 hi = (cores >> 32) & 0xFFFFFFFF;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	reg = core_type_to_reg(core_type, action);

	KBASE_DEBUG_ASSERT(reg);
#if defined(CONFIG_MALI_GATOR_SUPPORT)
	if (cores) {
		if (action == ACTION_PWRON)
			kbase_trace_mali_pm_power_on(core_type, cores);
		else if (action == ACTION_PWROFF)
			kbase_trace_mali_pm_power_off(core_type, cores);
	}
#endif

	if (cores) {
		u64 state = kbase_pm_get_state(kbdev, core_type, ACTION_READY);

		if (action == ACTION_PWRON)
			state |= cores;
		else if (action == ACTION_PWROFF)
			state &= ~cores;
		kbase_tlstream_aux_pm_state(core_type, state);
	}

	/* Tracing */
	if (cores) {
		if (action == ACTION_PWRON)
			switch (core_type) {
			case KBASE_PM_CORE_SHADER:
				KBASE_TRACE_ADD(kbdev, PM_PWRON, NULL, NULL, 0u,
									lo);
				break;
			case KBASE_PM_CORE_TILER:
				KBASE_TRACE_ADD(kbdev, PM_PWRON_TILER, NULL,
								NULL, 0u, lo);
				break;
			case KBASE_PM_CORE_L2:
				KBASE_TRACE_ADD(kbdev, PM_PWRON_L2, NULL, NULL,
									0u, lo);
				break;
			default:
				break;
			}
		else if (action == ACTION_PWROFF)
			switch (core_type) {
			case KBASE_PM_CORE_SHADER:
				KBASE_TRACE_ADD(kbdev, PM_PWROFF, NULL, NULL,
									0u, lo);
				break;
			case KBASE_PM_CORE_TILER:
				KBASE_TRACE_ADD(kbdev, PM_PWROFF_TILER, NULL,
								NULL, 0u, lo);
				break;
			case KBASE_PM_CORE_L2:
				KBASE_TRACE_ADD(kbdev, PM_PWROFF_L2, NULL, NULL,
									0u, lo);
				/* disable snoops before L2 is turned off */
				kbase_pm_cache_snoop_disable(kbdev);
				break;
			default:
				break;
			}
	}

	if (lo != 0)
		kbase_reg_write(kbdev, GPU_CONTROL_REG(reg), lo, NULL);

	if (hi != 0)
		kbase_reg_write(kbdev, GPU_CONTROL_REG(reg + 4), hi, NULL);
}

/**
 * kbase_pm_get_state - Get information about a core set
 *
 * This function gets information (chosen by @action) about a set of cores of
 * a type given by @core_type. It is a static function used by
 * kbase_pm_get_present_cores(), kbase_pm_get_active_cores(),
 * kbase_pm_get_trans_cores() and kbase_pm_get_ready_cores().
 *
 * @kbdev:     The kbase device structure of the device
 * @core_type: The type of core that the should be queried
 * @action:    The property of the cores to query
 *
 * Return: A bit mask specifying the state of the cores
 */
static u64 kbase_pm_get_state(struct kbase_device *kbdev,
					enum kbase_pm_core_type core_type,
					enum kbasep_pm_action action)
{
	u32 reg;
	u32 lo, hi;

	reg = core_type_to_reg(core_type, action);

	KBASE_DEBUG_ASSERT(reg);

	lo = kbase_reg_read(kbdev, GPU_CONTROL_REG(reg), NULL);
	hi = kbase_reg_read(kbdev, GPU_CONTROL_REG(reg + 4), NULL);

	return (((u64) hi) << 32) | ((u64) lo);
}

void kbasep_pm_read_present_cores(struct kbase_device *kbdev)
{
	kbdev->shader_inuse_bitmap = 0;
	kbdev->shader_needed_bitmap = 0;
	kbdev->shader_available_bitmap = 0;
	kbdev->tiler_available_bitmap = 0;
	kbdev->l2_users_count = 0;
	kbdev->l2_available_bitmap = 0;
	kbdev->tiler_needed_cnt = 0;
	kbdev->tiler_inuse_cnt = 0;

	memset(kbdev->shader_needed_cnt, 0, sizeof(kbdev->shader_needed_cnt));
}

KBASE_EXPORT_TEST_API(kbasep_pm_read_present_cores);

/**
 * kbase_pm_get_present_cores - Get the cores that are present
 *
 * @kbdev: Kbase device
 * @type: The type of cores to query
 *
 * Return: Bitmask of the cores that are present
 */
u64 kbase_pm_get_present_cores(struct kbase_device *kbdev,
						enum kbase_pm_core_type type)
{
	KBASE_DEBUG_ASSERT(kbdev != NULL);

	switch (type) {
	case KBASE_PM_CORE_L2:
		return kbdev->gpu_props.props.raw_props.l2_present;
	case KBASE_PM_CORE_SHADER:
		return kbdev->gpu_props.props.raw_props.shader_present;
	case KBASE_PM_CORE_TILER:
		return kbdev->gpu_props.props.raw_props.tiler_present;
	}
	KBASE_DEBUG_ASSERT(0);
	return 0;
}

KBASE_EXPORT_TEST_API(kbase_pm_get_present_cores);

/**
 * kbase_pm_get_active_cores - Get the cores that are "active"
 *                             (busy processing work)
 *
 * @kbdev: Kbase device
 * @type: The type of cores to query
 *
 * Return: Bitmask of cores that are active
 */
u64 kbase_pm_get_active_cores(struct kbase_device *kbdev,
						enum kbase_pm_core_type type)
{
	return kbase_pm_get_state(kbdev, type, ACTION_PWRACTIVE);
}

KBASE_EXPORT_TEST_API(kbase_pm_get_active_cores);

/**
 * kbase_pm_get_trans_cores - Get the cores that are transitioning between
 *                            power states
 *
 * @kbdev: Kbase device
 * @type: The type of cores to query
 *
 * Return: Bitmask of cores that are transitioning
 */
u64 kbase_pm_get_trans_cores(struct kbase_device *kbdev,
						enum kbase_pm_core_type type)
{
	return kbase_pm_get_state(kbdev, type, ACTION_PWRTRANS);
}

KBASE_EXPORT_TEST_API(kbase_pm_get_trans_cores);

/**
 * kbase_pm_get_ready_cores - Get the cores that are powered on
 *
 * @kbdev: Kbase device
 * @type: The type of cores to query
 *
 * Return: Bitmask of cores that are ready (powered on)
 */
u64 kbase_pm_get_ready_cores(struct kbase_device *kbdev,
						enum kbase_pm_core_type type)
{
	u64 result;

	result = kbase_pm_get_state(kbdev, type, ACTION_READY);

	switch (type) {
	case KBASE_PM_CORE_SHADER:
		KBASE_TRACE_ADD(kbdev, PM_CORES_POWERED, NULL, NULL, 0u,
								(u32) result);
		break;
	case KBASE_PM_CORE_TILER:
		KBASE_TRACE_ADD(kbdev, PM_CORES_POWERED_TILER, NULL, NULL, 0u,
								(u32) result);
		break;
	case KBASE_PM_CORE_L2:
		KBASE_TRACE_ADD(kbdev, PM_CORES_POWERED_L2, NULL, NULL, 0u,
								(u32) result);
		break;
	default:
		break;
	}

	return result;
}

KBASE_EXPORT_TEST_API(kbase_pm_get_ready_cores);

/**
 * kbase_pm_transition_core_type - Perform power transitions for a particular
 *                                 core type.
 *
 * This function will perform any available power transitions to make the actual
 * hardware state closer to the desired state. If a core is currently
 * transitioning then changes to the power state of that call cannot be made
 * until the transition has finished. Cores which are not present in the
 * hardware are ignored if they are specified in the desired_state bitmask,
 * however the return value will always be 0 in this case.
 *
 * @kbdev:             The kbase device
 * @type:              The core type to perform transitions for
 * @desired_state:     A bit mask of the desired state of the cores
 * @in_use:            A bit mask of the cores that are currently running
 *                     jobs. These cores have to be kept powered up because
 *                     there are jobs running (or about to run) on them.
 * @available:         Receives a bit mask of the cores that the job
 *                     scheduler can use to submit jobs to. May be NULL if
 *                     this is not needed.
 * @powering_on:       Bit mask to update with cores that are
 *                    transitioning to a power-on state.
 *
 * Return: true if the desired state has been reached, false otherwise
 */
static bool kbase_pm_transition_core_type(struct kbase_device *kbdev,
						enum kbase_pm_core_type type,
						u64 desired_state,
						u64 in_use,
						u64 * const available,
						u64 *powering_on)
{
	u64 present;
	u64 ready;
	u64 trans;
	u64 powerup;
	u64 powerdown;
	u64 powering_on_trans;
	u64 desired_state_in_use;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	/* Get current state */
	present = kbase_pm_get_present_cores(kbdev, type);
	trans = kbase_pm_get_trans_cores(kbdev, type);
	ready = kbase_pm_get_ready_cores(kbdev, type);
	/* mask off ready from trans in case transitions finished between the
	 * register reads */
	trans &= ~ready;

	powering_on_trans = trans & *powering_on;
	*powering_on = powering_on_trans;

	if (available != NULL)
		*available = (ready | powering_on_trans) & desired_state;

	/* Update desired state to include the in-use cores. These have to be
	 * kept powered up because there are jobs running or about to run on
	 * these cores
	 */
	desired_state_in_use = desired_state | in_use;

	/* Update state of whether l2 caches are powered */
	if (type == KBASE_PM_CORE_L2) {
		if ((ready == present) && (desired_state_in_use == ready) &&
								(trans == 0)) {
			/* All are ready, none will be turned off, and none are
			 * transitioning */
			kbdev->pm.backend.l2_powered = 1;
			/*
			 * Ensure snoops are enabled after L2 is powered up,
			 * note that kbase keeps track of the snoop state, so
			 * safe to repeatedly call.
			 */
			kbase_pm_cache_snoop_enable(kbdev);
			if (kbdev->l2_users_count > 0) {
				/* Notify any registered l2 cache users
				 * (optimized out when no users waiting) */
				wake_up(&kbdev->pm.backend.l2_powered_wait);
			}
		} else
			kbdev->pm.backend.l2_powered = 0;
	}

	if (desired_state == ready && (trans == 0))
		return true;

	/* Restrict the cores to those that are actually present */
	powerup = desired_state_in_use & present;
	powerdown = (~desired_state_in_use) & present;

	/* Restrict to cores that are not already in the desired state */
	powerup &= ~ready;
	powerdown &= ready;

	/* Don't transition any cores that are already transitioning, except for
	 * Mali cores that support the following case:
	 *
	 * If the SHADER_PWRON or TILER_PWRON registers are written to turn on
	 * a core that is currently transitioning to power off, then this is
	 * remembered and the shader core is automatically powered up again once
	 * the original transition completes. Once the automatic power on is
	 * complete any job scheduled on the shader core should start.
	 */
	powerdown &= ~trans;

	if (kbase_hw_has_feature(kbdev,
				BASE_HW_FEATURE_PWRON_DURING_PWROFF_TRANS))
		if (KBASE_PM_CORE_SHADER == type || KBASE_PM_CORE_TILER == type)
			trans = powering_on_trans; /* for exception cases, only
						    * mask off cores in power on
						    * transitions */

	powerup &= ~trans;

	/* Perform transitions if any */
	kbase_pm_invoke(kbdev, type, powerup, ACTION_PWRON);
	kbase_pm_invoke(kbdev, type, powerdown, ACTION_PWROFF);

	/* Recalculate cores transitioning on, and re-evaluate our state */
	powering_on_trans |= powerup;
	*powering_on = powering_on_trans;
	if (available != NULL)
		*available = (ready | powering_on_trans) & desired_state;

	return false;
}

KBASE_EXPORT_TEST_API(kbase_pm_transition_core_type);

/**
 * get_desired_cache_status - Determine which caches should be on for a
 *                            particular core state
 *
 * This function takes a bit mask of the present caches and the cores (or
 * caches) that are attached to the caches that will be powered. It then
 * computes which caches should be turned on to allow the cores requested to be
 * powered up.
 *
 * @present:       The bit mask of present caches
 * @cores_powered: A bit mask of cores (or L2 caches) that are desired to
 *                 be powered
 * @tilers_powered: The bit mask of tilers that are desired to be powered
 *
 * Return: A bit mask of the caches that should be turned on
 */
static u64 get_desired_cache_status(u64 present, u64 cores_powered,
		u64 tilers_powered)
{
	u64 desired = 0;

	while (present) {
		/* Find out which is the highest set bit */
		u64 bit = fls64(present) - 1;
		u64 bit_mask = 1ull << bit;
		/* Create a mask which has all bits from 'bit' upwards set */

		u64 mask = ~(bit_mask - 1);

		/* If there are any cores powered at this bit or above (that
		 * haven't previously been processed) then we need this core on
		 */
		if (cores_powered & mask)
			desired |= bit_mask;

		/* Remove bits from cores_powered and present */
		cores_powered &= ~mask;
		present &= ~bit_mask;
	}

	/* Power up the required L2(s) for the tiler */
	if (tilers_powered)
		desired |= 1;

	return desired;
}

KBASE_EXPORT_TEST_API(get_desired_cache_status);

bool
MOCKABLE(kbase_pm_check_transitions_nolock) (struct kbase_device *kbdev)
{
	bool cores_are_available = false;
	bool in_desired_state = true;
	u64 desired_l2_state;
	u64 cores_powered;
	u64 tilers_powered;
	u64 tiler_available_bitmap;
	u64 shader_available_bitmap;
	u64 shader_ready_bitmap;
	u64 shader_transitioning_bitmap;
	u64 l2_available_bitmap;
	u64 prev_l2_available_bitmap;

	KBASE_DEBUG_ASSERT(NULL != kbdev);
	lockdep_assert_held(&kbdev->hwaccess_lock);

	spin_lock(&kbdev->pm.backend.gpu_powered_lock);
	if (kbdev->pm.backend.gpu_powered == false) {
		spin_unlock(&kbdev->pm.backend.gpu_powered_lock);
		if (kbdev->pm.backend.desired_shader_state == 0 &&
				kbdev->pm.backend.desired_tiler_state == 0)
			return true;
		return false;
	}

	/* Trace that a change-state is being requested, and that it took
	 * (effectively) no time to start it. This is useful for counting how
	 * many state changes occurred, in a way that's backwards-compatible
	 * with processing the trace data */
	kbase_timeline_pm_send_event(kbdev,
				KBASE_TIMELINE_PM_EVENT_CHANGE_GPU_STATE);
	kbase_timeline_pm_handle_event(kbdev,
				KBASE_TIMELINE_PM_EVENT_CHANGE_GPU_STATE);

	/* If any cores are already powered then, we must keep the caches on */
	cores_powered = kbase_pm_get_ready_cores(kbdev, KBASE_PM_CORE_SHADER);

	cores_powered |= kbdev->pm.backend.desired_shader_state;

	/* Work out which tilers want to be powered */
	tilers_powered = kbase_pm_get_ready_cores(kbdev, KBASE_PM_CORE_TILER);
	tilers_powered |= kbdev->pm.backend.desired_tiler_state;

	/* If there are l2 cache users registered, keep all l2s powered even if
	 * all other cores are off. */
	if (kbdev->l2_users_count > 0)
		cores_powered |= kbdev->gpu_props.props.raw_props.l2_present;

	desired_l2_state = get_desired_cache_status(
			kbdev->gpu_props.props.raw_props.l2_present,
			cores_powered, tilers_powered);

	/* If any l2 cache is on, then enable l2 #0, for use by job manager */
	if (0 != desired_l2_state)
		desired_l2_state |= 1;

	prev_l2_available_bitmap = kbdev->l2_available_bitmap;
	in_desired_state &= kbase_pm_transition_core_type(kbdev,
				KBASE_PM_CORE_L2, desired_l2_state, 0,
				&l2_available_bitmap,
				&kbdev->pm.backend.powering_on_l2_state);

	if (kbdev->l2_available_bitmap != l2_available_bitmap)
		KBASE_TIMELINE_POWER_L2(kbdev, l2_available_bitmap);

	kbdev->l2_available_bitmap = l2_available_bitmap;

	if (in_desired_state) {
		in_desired_state &= kbase_pm_transition_core_type(kbdev,
				KBASE_PM_CORE_TILER,
				kbdev->pm.backend.desired_tiler_state,
				0, &tiler_available_bitmap,
				&kbdev->pm.backend.powering_on_tiler_state);
		in_desired_state &= kbase_pm_transition_core_type(kbdev,
				KBASE_PM_CORE_SHADER,
				kbdev->pm.backend.desired_shader_state,
				kbdev->shader_inuse_bitmap,
				&shader_available_bitmap,
				&kbdev->pm.backend.powering_on_shader_state);

		if (kbdev->shader_available_bitmap != shader_available_bitmap) {
			KBASE_TRACE_ADD(kbdev, PM_CORES_CHANGE_AVAILABLE, NULL,
						NULL, 0u,
						(u32) shader_available_bitmap);
			KBASE_TIMELINE_POWER_SHADER(kbdev,
						shader_available_bitmap);
		}

		kbdev->shader_available_bitmap = shader_available_bitmap;

		if (kbdev->tiler_available_bitmap != tiler_available_bitmap) {
			KBASE_TRACE_ADD(kbdev, PM_CORES_CHANGE_AVAILABLE_TILER,
						NULL, NULL, 0u,
						(u32) tiler_available_bitmap);
			KBASE_TIMELINE_POWER_TILER(kbdev,
							tiler_available_bitmap);
		}

		kbdev->tiler_available_bitmap = tiler_available_bitmap;

	} else if ((l2_available_bitmap &
			kbdev->gpu_props.props.raw_props.tiler_present) !=
			kbdev->gpu_props.props.raw_props.tiler_present) {
		tiler_available_bitmap = 0;

		if (kbdev->tiler_available_bitmap != tiler_available_bitmap)
			KBASE_TIMELINE_POWER_TILER(kbdev,
							tiler_available_bitmap);

		kbdev->tiler_available_bitmap = tiler_available_bitmap;
	}

	/* State updated for slow-path waiters */
	kbdev->pm.backend.gpu_in_desired_state = in_desired_state;

	shader_ready_bitmap = kbase_pm_get_ready_cores(kbdev,
							KBASE_PM_CORE_SHADER);
	shader_transitioning_bitmap = kbase_pm_get_trans_cores(kbdev,
							KBASE_PM_CORE_SHADER);

	/* Determine whether the cores are now available (even if the set of
	 * available cores is empty). Note that they can be available even if
	 * we've not finished transitioning to the desired state */
	if ((kbdev->shader_available_bitmap &
					kbdev->pm.backend.desired_shader_state)
				== kbdev->pm.backend.desired_shader_state &&
		(kbdev->tiler_available_bitmap &
					kbdev->pm.backend.desired_tiler_state)
				== kbdev->pm.backend.desired_tiler_state) {
		cores_are_available = true;

		KBASE_TRACE_ADD(kbdev, PM_CORES_AVAILABLE, NULL, NULL, 0u,
				(u32)(kbdev->shader_available_bitmap &
				kbdev->pm.backend.desired_shader_state));
		KBASE_TRACE_ADD(kbdev, PM_CORES_AVAILABLE_TILER, NULL, NULL, 0u,
				(u32)(kbdev->tiler_available_bitmap &
				kbdev->pm.backend.desired_tiler_state));

		/* Log timelining information about handling events that power
		 * up cores, to match up either with immediate submission either
		 * because cores already available, or from PM IRQ */
		if (!in_desired_state)
			kbase_timeline_pm_send_event(kbdev,
				KBASE_TIMELINE_PM_EVENT_GPU_STATE_CHANGED);
	}

	if (in_desired_state) {
		KBASE_DEBUG_ASSERT(cores_are_available);

#if defined(CONFIG_MALI_GATOR_SUPPORT)
		kbase_trace_mali_pm_status(KBASE_PM_CORE_L2,
						kbase_pm_get_ready_cores(kbdev,
							KBASE_PM_CORE_L2));
		kbase_trace_mali_pm_status(KBASE_PM_CORE_SHADER,
						kbase_pm_get_ready_cores(kbdev,
							KBASE_PM_CORE_SHADER));
		kbase_trace_mali_pm_status(KBASE_PM_CORE_TILER,
						kbase_pm_get_ready_cores(kbdev,
							KBASE_PM_CORE_TILER));
#endif

		kbase_tlstream_aux_pm_state(
				KBASE_PM_CORE_L2,
				kbase_pm_get_ready_cores(
					kbdev, KBASE_PM_CORE_L2));
		kbase_tlstream_aux_pm_state(
				KBASE_PM_CORE_SHADER,
				kbase_pm_get_ready_cores(
					kbdev, KBASE_PM_CORE_SHADER));
		kbase_tlstream_aux_pm_state(
				KBASE_PM_CORE_TILER,
				kbase_pm_get_ready_cores(
					kbdev,
					KBASE_PM_CORE_TILER));

		KBASE_TRACE_ADD(kbdev, PM_DESIRED_REACHED, NULL, NULL,
				kbdev->pm.backend.gpu_in_desired_state,
				(u32)kbdev->pm.backend.desired_shader_state);
		KBASE_TRACE_ADD(kbdev, PM_DESIRED_REACHED_TILER, NULL, NULL, 0u,
				(u32)kbdev->pm.backend.desired_tiler_state);

		/* Log timelining information for synchronous waiters */
		kbase_timeline_pm_send_event(kbdev,
				KBASE_TIMELINE_PM_EVENT_GPU_STATE_CHANGED);
		/* Wake slow-path waiters. Job scheduler does not use this. */
		KBASE_TRACE_ADD(kbdev, PM_WAKE_WAITERS, NULL, NULL, 0u, 0);

		wake_up(&kbdev->pm.backend.gpu_in_desired_state_wait);
	}

	spin_unlock(&kbdev->pm.backend.gpu_powered_lock);

	/* kbase_pm_ca_update_core_status can cause one-level recursion into
	 * this function, so it must only be called once all changes to kbdev
	 * have been committed, and after the gpu_powered_lock has been
	 * dropped. */
	if (kbdev->shader_ready_bitmap != shader_ready_bitmap ||
	    kbdev->shader_transitioning_bitmap != shader_transitioning_bitmap) {
		kbdev->shader_ready_bitmap = shader_ready_bitmap;
		kbdev->shader_transitioning_bitmap =
						shader_transitioning_bitmap;

		kbase_pm_ca_update_core_status(kbdev, shader_ready_bitmap,
						shader_transitioning_bitmap);
	}

	/* The core availability policy is not allowed to keep core group 0
	 * turned off (unless it was changing the l2 power state) */
	if (!((shader_ready_bitmap | shader_transitioning_bitmap) &
		kbdev->gpu_props.props.coherency_info.group[0].core_mask) &&
		(prev_l2_available_bitmap == desired_l2_state) &&
		!(kbase_pm_ca_get_core_mask(kbdev) &
		kbdev->gpu_props.props.coherency_info.group[0].core_mask))
		BUG();

	/* The core availability policy is allowed to keep core group 1 off,
	 * but all jobs specifically targeting CG1 must fail */
	if (!((shader_ready_bitmap | shader_transitioning_bitmap) &
		kbdev->gpu_props.props.coherency_info.group[1].core_mask) &&
		!(kbase_pm_ca_get_core_mask(kbdev) &
		kbdev->gpu_props.props.coherency_info.group[1].core_mask))
		kbdev->pm.backend.cg1_disabled = true;
	else
		kbdev->pm.backend.cg1_disabled = false;

	return cores_are_available;
}
KBASE_EXPORT_TEST_API(kbase_pm_check_transitions_nolock);

/* Timeout for kbase_pm_check_transitions_sync when wait_event_killable has
 * aborted due to a fatal signal. If the time spent waiting has exceeded this
 * threshold then there is most likely a hardware issue. */
#define PM_TIMEOUT (5*HZ) /* 5s */

void kbase_pm_check_transitions_sync(struct kbase_device *kbdev)
{
	unsigned long flags;
	unsigned long timeout;
	bool cores_are_available;
	int ret;

	/* Force the transition to be checked and reported - the cores may be
	 * 'available' (for job submission) but not fully powered up. */
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	cores_are_available = kbase_pm_check_transitions_nolock(kbdev);

	/* Don't need 'cores_are_available', because we don't return anything */
	CSTD_UNUSED(cores_are_available);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	timeout = jiffies + PM_TIMEOUT;

	/* Wait for cores */
	ret = wait_event_killable(kbdev->pm.backend.gpu_in_desired_state_wait,
			kbdev->pm.backend.gpu_in_desired_state);

	if (ret < 0 && time_after(jiffies, timeout)) {
		dev_err(kbdev->dev, "Power transition timed out unexpectedly\n");
		dev_err(kbdev->dev, "Desired state :\n");
		dev_err(kbdev->dev, "\tShader=%016llx\n",
				kbdev->pm.backend.desired_shader_state);
		dev_err(kbdev->dev, "\tTiler =%016llx\n",
				kbdev->pm.backend.desired_tiler_state);
		dev_err(kbdev->dev, "Current state :\n");
		dev_err(kbdev->dev, "\tShader=%08x%08x\n",
				kbase_reg_read(kbdev,
					GPU_CONTROL_REG(SHADER_READY_HI), NULL),
				kbase_reg_read(kbdev,
					GPU_CONTROL_REG(SHADER_READY_LO),
					NULL));
		dev_err(kbdev->dev, "\tTiler =%08x%08x\n",
				kbase_reg_read(kbdev,
					GPU_CONTROL_REG(TILER_READY_HI), NULL),
				kbase_reg_read(kbdev,
					GPU_CONTROL_REG(TILER_READY_LO), NULL));
		dev_err(kbdev->dev, "\tL2    =%08x%08x\n",
				kbase_reg_read(kbdev,
					GPU_CONTROL_REG(L2_READY_HI), NULL),
				kbase_reg_read(kbdev,
					GPU_CONTROL_REG(L2_READY_LO), NULL));
		dev_err(kbdev->dev, "Cores transitioning :\n");
		dev_err(kbdev->dev, "\tShader=%08x%08x\n",
				kbase_reg_read(kbdev, GPU_CONTROL_REG(
						SHADER_PWRTRANS_HI), NULL),
				kbase_reg_read(kbdev, GPU_CONTROL_REG(
						SHADER_PWRTRANS_LO), NULL));
		dev_err(kbdev->dev, "\tTiler =%08x%08x\n",
				kbase_reg_read(kbdev, GPU_CONTROL_REG(
						TILER_PWRTRANS_HI), NULL),
				kbase_reg_read(kbdev, GPU_CONTROL_REG(
						TILER_PWRTRANS_LO), NULL));
		dev_err(kbdev->dev, "\tL2    =%08x%08x\n",
				kbase_reg_read(kbdev, GPU_CONTROL_REG(
						L2_PWRTRANS_HI), NULL),
				kbase_reg_read(kbdev, GPU_CONTROL_REG(
						L2_PWRTRANS_LO), NULL));
#if KBASE_GPU_RESET_EN
		dev_err(kbdev->dev, "Sending reset to GPU - all running jobs will be lost\n");
		if (kbase_prepare_to_reset_gpu(kbdev))
			kbase_reset_gpu(kbdev);
#endif /* KBASE_GPU_RESET_EN */
	} else {
		/* Log timelining information that a change in state has
		 * completed */
		kbase_timeline_pm_handle_event(kbdev,
				KBASE_TIMELINE_PM_EVENT_GPU_STATE_CHANGED);
	}
}
KBASE_EXPORT_TEST_API(kbase_pm_check_transitions_sync);

void kbase_pm_enable_interrupts(struct kbase_device *kbdev)
{
	unsigned long flags;

	KBASE_DEBUG_ASSERT(NULL != kbdev);
	/*
	 * Clear all interrupts,
	 * and unmask them all.
	 */
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_IRQ_CLEAR), GPU_IRQ_REG_ALL,
									NULL);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_IRQ_MASK), GPU_IRQ_REG_ALL,
									NULL);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	kbase_reg_write(kbdev, JOB_CONTROL_REG(JOB_IRQ_CLEAR), 0xFFFFFFFF,
									NULL);
	kbase_reg_write(kbdev, JOB_CONTROL_REG(JOB_IRQ_MASK), 0xFFFFFFFF, NULL);

	kbase_reg_write(kbdev, MMU_REG(MMU_IRQ_CLEAR), 0xFFFFFFFF, NULL);
	kbase_reg_write(kbdev, MMU_REG(MMU_IRQ_MASK), 0xFFFFFFFF, NULL);
}

KBASE_EXPORT_TEST_API(kbase_pm_enable_interrupts);

void kbase_pm_disable_interrupts_nolock(struct kbase_device *kbdev)
{
	KBASE_DEBUG_ASSERT(NULL != kbdev);
	/*
	 * Mask all interrupts,
	 * and clear them all.
	 */
	lockdep_assert_held(&kbdev->hwaccess_lock);

	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_IRQ_MASK), 0, NULL);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_IRQ_CLEAR), GPU_IRQ_REG_ALL,
									NULL);
	kbase_reg_write(kbdev, JOB_CONTROL_REG(JOB_IRQ_MASK), 0, NULL);
	kbase_reg_write(kbdev, JOB_CONTROL_REG(JOB_IRQ_CLEAR), 0xFFFFFFFF,
									NULL);

	kbase_reg_write(kbdev, MMU_REG(MMU_IRQ_MASK), 0, NULL);
	kbase_reg_write(kbdev, MMU_REG(MMU_IRQ_CLEAR), 0xFFFFFFFF, NULL);
}

void kbase_pm_disable_interrupts(struct kbase_device *kbdev)
{
	unsigned long flags;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kbase_pm_disable_interrupts_nolock(kbdev);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
}

KBASE_EXPORT_TEST_API(kbase_pm_disable_interrupts);


/*
 * pmu layout:
 * 0x0000: PMU TAG (RO) (0xCAFECAFE)
 * 0x0004: PMU VERSION ID (RO) (0x00000000)
 * 0x0008: CLOCK ENABLE (RW) (31:1 SBZ, 0 CLOCK STATE)
 */
void kbase_pm_clock_on(struct kbase_device *kbdev, bool is_resume)
{
	bool reset_required = is_resume;
	struct kbasep_js_device_data *js_devdata = &kbdev->js_data;
	unsigned long flags;
	int i;

	KBASE_DEBUG_ASSERT(NULL != kbdev);
	lockdep_assert_held(&js_devdata->runpool_mutex);
	lockdep_assert_held(&kbdev->pm.lock);

	if (kbdev->pm.backend.gpu_powered) {
		/* Already turned on */
		if (kbdev->poweroff_pending)
			kbase_pm_enable_interrupts(kbdev);
		kbdev->poweroff_pending = false;
		KBASE_DEBUG_ASSERT(!is_resume);
		return;
	}

	kbdev->poweroff_pending = false;

	KBASE_TRACE_ADD(kbdev, PM_GPU_ON, NULL, NULL, 0u, 0u);

	if (is_resume && kbdev->pm.backend.callback_power_resume) {
		kbdev->pm.backend.callback_power_resume(kbdev);
		return;
	} else if (kbdev->pm.backend.callback_power_on) {
		kbdev->pm.backend.callback_power_on(kbdev);
		/* If your platform properly keeps the GPU state you may use the
		 * return value of the callback_power_on function to
		 * conditionally reset the GPU on power up. Currently we are
		 * conservative and always reset the GPU. */
		reset_required = true;
	}

	spin_lock_irqsave(&kbdev->pm.backend.gpu_powered_lock, flags);
	kbdev->pm.backend.gpu_powered = true;
	spin_unlock_irqrestore(&kbdev->pm.backend.gpu_powered_lock, flags);

	if (reset_required) {
		/* GPU state was lost, reset GPU to ensure it is in a
		 * consistent state */
		kbase_pm_init_hw(kbdev, PM_ENABLE_IRQS);
	}

	mutex_lock(&kbdev->mmu_hw_mutex);
	/* Reprogram the GPU's MMU */
	for (i = 0; i < kbdev->nr_hw_address_spaces; i++) {
		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

		if (js_devdata->runpool_irq.per_as_data[i].kctx)
			kbase_mmu_update(
				js_devdata->runpool_irq.per_as_data[i].kctx);
		else
			kbase_mmu_disable_as(kbdev, i);

		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	}
	mutex_unlock(&kbdev->mmu_hw_mutex);

	/* Lastly, enable the interrupts */
	kbase_pm_enable_interrupts(kbdev);
}

KBASE_EXPORT_TEST_API(kbase_pm_clock_on);

bool kbase_pm_clock_off(struct kbase_device *kbdev, bool is_suspend)
{
	unsigned long flags;

	KBASE_DEBUG_ASSERT(NULL != kbdev);
	lockdep_assert_held(&kbdev->pm.lock);

	/* ASSERT that the cores should now be unavailable. No lock needed. */
	KBASE_DEBUG_ASSERT(kbdev->shader_available_bitmap == 0u);

	kbdev->poweroff_pending = true;

	if (!kbdev->pm.backend.gpu_powered) {
		/* Already turned off */
		if (is_suspend && kbdev->pm.backend.callback_power_suspend)
			kbdev->pm.backend.callback_power_suspend(kbdev);
		return true;
	}

	KBASE_TRACE_ADD(kbdev, PM_GPU_OFF, NULL, NULL, 0u, 0u);

	/* Disable interrupts. This also clears any outstanding interrupts */
	kbase_pm_disable_interrupts(kbdev);
	/* Ensure that any IRQ handlers have finished */
	kbase_synchronize_irqs(kbdev);

	spin_lock_irqsave(&kbdev->pm.backend.gpu_powered_lock, flags);

	if (atomic_read(&kbdev->faults_pending)) {
		/* Page/bus faults are still being processed. The GPU can not
		 * be powered off until they have completed */
		spin_unlock_irqrestore(&kbdev->pm.backend.gpu_powered_lock,
									flags);
		return false;
	}

	kbase_pm_cache_snoop_disable(kbdev);

	/* The GPU power may be turned off from this point */
	kbdev->pm.backend.gpu_powered = false;
	spin_unlock_irqrestore(&kbdev->pm.backend.gpu_powered_lock, flags);

	if (is_suspend && kbdev->pm.backend.callback_power_suspend)
		kbdev->pm.backend.callback_power_suspend(kbdev);
	else if (kbdev->pm.backend.callback_power_off)
		kbdev->pm.backend.callback_power_off(kbdev);
	return true;
}

KBASE_EXPORT_TEST_API(kbase_pm_clock_off);

struct kbasep_reset_timeout_data {
	struct hrtimer timer;
	bool timed_out;
	struct kbase_device *kbdev;
};

void kbase_pm_reset_done(struct kbase_device *kbdev)
{
	KBASE_DEBUG_ASSERT(kbdev != NULL);
	kbdev->pm.backend.reset_done = true;
	wake_up(&kbdev->pm.backend.reset_done_wait);
}

/**
 * kbase_pm_wait_for_reset - Wait for a reset to happen
 *
 * Wait for the %RESET_COMPLETED IRQ to occur, then reset the waiting state.
 *
 * @kbdev: Kbase device
 */
static void kbase_pm_wait_for_reset(struct kbase_device *kbdev)
{
	lockdep_assert_held(&kbdev->pm.lock);

	wait_event(kbdev->pm.backend.reset_done_wait,
						(kbdev->pm.backend.reset_done));
	kbdev->pm.backend.reset_done = false;
}

KBASE_EXPORT_TEST_API(kbase_pm_reset_done);

static enum hrtimer_restart kbasep_reset_timeout(struct hrtimer *timer)
{
	struct kbasep_reset_timeout_data *rtdata =
		container_of(timer, struct kbasep_reset_timeout_data, timer);

	rtdata->timed_out = 1;

	/* Set the wait queue to wake up kbase_pm_init_hw even though the reset
	 * hasn't completed */
	kbase_pm_reset_done(rtdata->kbdev);

	return HRTIMER_NORESTART;
}

static void kbase_pm_hw_issues_detect(struct kbase_device *kbdev)
{
	struct device_node *np = kbdev->dev->of_node;
	u32 jm_values[4];
	const u32 gpu_id = kbdev->gpu_props.props.raw_props.gpu_id;
	const u32 prod_id = (gpu_id & GPU_ID_VERSION_PRODUCT_ID) >>
		GPU_ID_VERSION_PRODUCT_ID_SHIFT;
	const u32 major = (gpu_id & GPU_ID_VERSION_MAJOR) >>
		GPU_ID_VERSION_MAJOR_SHIFT;

	kbdev->hw_quirks_sc = 0;

	/* Needed due to MIDBASE-1494: LS_PAUSEBUFFER_DISABLE. See PRLAM-8443.
	 * and needed due to MIDGLES-3539. See PRLAM-11035 */
	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8443) ||
			kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_11035))
		kbdev->hw_quirks_sc |= SC_LS_PAUSEBUFFER_DISABLE;

	/* Needed due to MIDBASE-2054: SDC_DISABLE_OQ_DISCARD. See PRLAM-10327.
	 */
	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_10327))
		kbdev->hw_quirks_sc |= SC_SDC_DISABLE_OQ_DISCARD;

#ifdef CONFIG_MALI_PRFCNT_SET_SECONDARY
	/* Enable alternative hardware counter selection if configured. */
	if (!GPU_ID_IS_NEW_FORMAT(prod_id))
		kbdev->hw_quirks_sc |= SC_ALT_COUNTERS;
#endif

	/* Needed due to MIDBASE-2795. ENABLE_TEXGRD_FLAGS. See PRLAM-10797. */
	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_10797))
		kbdev->hw_quirks_sc |= SC_ENABLE_TEXGRD_FLAGS;

	if (!kbase_hw_has_issue(kbdev, GPUCORE_1619)) {
		if (prod_id < 0x750 || prod_id == 0x6956) /* T60x, T62x, T72x */
			kbdev->hw_quirks_sc |= SC_LS_ATTR_CHECK_DISABLE;
		else if (prod_id >= 0x750 && prod_id <= 0x880) /* T76x, T8xx */
			kbdev->hw_quirks_sc |= SC_LS_ALLOW_ATTR_TYPES;
	}

	kbdev->hw_quirks_tiler = kbase_reg_read(kbdev,
			GPU_CONTROL_REG(TILER_CONFIG), NULL);

	/* Set tiler clock gate override if required */
	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_T76X_3953))
		kbdev->hw_quirks_tiler |= TC_CLOCK_GATE_OVERRIDE;

	/* Limit the GPU bus bandwidth if the platform needs this. */
	kbdev->hw_quirks_mmu = kbase_reg_read(kbdev,
			GPU_CONTROL_REG(L2_MMU_CONFIG), NULL);

	/* Limit read ID width for AXI */
	kbdev->hw_quirks_mmu &= ~(L2_MMU_CONFIG_LIMIT_EXTERNAL_READS);
	kbdev->hw_quirks_mmu |= (DEFAULT_ARID_LIMIT & 0x3) <<
				L2_MMU_CONFIG_LIMIT_EXTERNAL_READS_SHIFT;

	/* Limit write ID width for AXI */
	kbdev->hw_quirks_mmu &= ~(L2_MMU_CONFIG_LIMIT_EXTERNAL_WRITES);
	kbdev->hw_quirks_mmu |= (DEFAULT_AWID_LIMIT & 0x3) <<
				L2_MMU_CONFIG_LIMIT_EXTERNAL_WRITES_SHIFT;

	if (kbdev->system_coherency == COHERENCY_ACE) {
		/* Allow memory configuration disparity to be ignored, we
		 * optimize the use of shared memory and thus we expect
		 * some disparity in the memory configuration */
		kbdev->hw_quirks_mmu |= L2_MMU_CONFIG_ALLOW_SNOOP_DISPARITY;
	}

	/* Only for T86x/T88x-based products after r2p0 */
	if (prod_id >= 0x860 && prod_id <= 0x880 && major >= 2) {
		/* The JM_CONFIG register is specified as follows in the
		 T86x/T88x Engineering Specification Supplement:
		 The values are read from device tree in order.
		*/
#define TIMESTAMP_OVERRIDE  1
#define CLOCK_GATE_OVERRIDE (1<<1)
#define JOB_THROTTLE_ENABLE (1<<2)
#define JOB_THROTTLE_LIMIT_SHIFT 3

		/* 6 bits in the register */
		const u32 jm_max_limit = 0x3F;

		if (of_property_read_u32_array(np,
					"jm_config",
					&jm_values[0],
					ARRAY_SIZE(jm_values))) {
			/* Entry not in device tree, use defaults  */
			jm_values[0] = 0;
			jm_values[1] = 0;
			jm_values[2] = 0;
			jm_values[3] = jm_max_limit; /* Max value */
		}

		/* Limit throttle limit to 6 bits*/
		if (jm_values[3] > jm_max_limit) {
			dev_dbg(kbdev->dev, "JOB_THROTTLE_LIMIT supplied in device tree is too large. Limiting to MAX (63).");
			jm_values[3] = jm_max_limit;
		}

		/* Aggregate to one integer. */
		kbdev->hw_quirks_jm = (jm_values[0] ? TIMESTAMP_OVERRIDE : 0);
		kbdev->hw_quirks_jm |= (jm_values[1] ? CLOCK_GATE_OVERRIDE : 0);
		kbdev->hw_quirks_jm |= (jm_values[2] ? JOB_THROTTLE_ENABLE : 0);
		kbdev->hw_quirks_jm |= (jm_values[3] <<
				JOB_THROTTLE_LIMIT_SHIFT);
	} else {
		kbdev->hw_quirks_jm = KBASE_JM_CONFIG_UNUSED;
	}


}

static void kbase_pm_hw_issues_apply(struct kbase_device *kbdev)
{
	if (kbdev->hw_quirks_sc)
		kbase_reg_write(kbdev, GPU_CONTROL_REG(SHADER_CONFIG),
				kbdev->hw_quirks_sc, NULL);

	kbase_reg_write(kbdev, GPU_CONTROL_REG(TILER_CONFIG),
			kbdev->hw_quirks_tiler, NULL);

	kbase_reg_write(kbdev, GPU_CONTROL_REG(L2_MMU_CONFIG),
			kbdev->hw_quirks_mmu, NULL);


	if (kbdev->hw_quirks_jm != KBASE_JM_CONFIG_UNUSED)
		kbase_reg_write(kbdev, GPU_CONTROL_REG(JM_CONFIG),
				kbdev->hw_quirks_jm, NULL);

}

void kbase_pm_cache_snoop_enable(struct kbase_device *kbdev)
{
	if ((kbdev->current_gpu_coherency_mode == COHERENCY_ACE) &&
		!kbdev->cci_snoop_enabled) {
#ifdef CONFIG_ARM64
		if (kbdev->snoop_enable_smc != 0)
			kbase_invoke_smc_fid(kbdev->snoop_enable_smc, 0, 0, 0);
#endif /* CONFIG_ARM64 */
		dev_dbg(kbdev->dev, "MALI - CCI Snoops - Enabled\n");
		kbdev->cci_snoop_enabled = true;
	}
}

void kbase_pm_cache_snoop_disable(struct kbase_device *kbdev)
{
	if (kbdev->cci_snoop_enabled) {
#ifdef CONFIG_ARM64
		if (kbdev->snoop_disable_smc != 0) {
			mali_cci_flush_l2(kbdev);
			kbase_invoke_smc_fid(kbdev->snoop_disable_smc, 0, 0, 0);
		}
#endif /* CONFIG_ARM64 */
		dev_dbg(kbdev->dev, "MALI - CCI Snoops Disabled\n");
		kbdev->cci_snoop_enabled = false;
	}
}

static int kbase_pm_reset_do_normal(struct kbase_device *kbdev)
{
	struct kbasep_reset_timeout_data rtdata;

	KBASE_TRACE_ADD(kbdev, CORE_GPU_SOFT_RESET, NULL, NULL, 0u, 0);

	kbase_tlstream_jd_gpu_soft_reset(kbdev);

	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_COMMAND),
						GPU_COMMAND_SOFT_RESET, NULL);

	/* Unmask the reset complete interrupt only */
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_IRQ_MASK), RESET_COMPLETED,
									NULL);

	/* Initialize a structure for tracking the status of the reset */
	rtdata.kbdev = kbdev;
	rtdata.timed_out = 0;

	/* Create a timer to use as a timeout on the reset */
	hrtimer_init_on_stack(&rtdata.timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	rtdata.timer.function = kbasep_reset_timeout;

	hrtimer_start(&rtdata.timer, HR_TIMER_DELAY_MSEC(RESET_TIMEOUT),
							HRTIMER_MODE_REL);

	/* Wait for the RESET_COMPLETED interrupt to be raised */
	kbase_pm_wait_for_reset(kbdev);

	if (rtdata.timed_out == 0) {
		/* GPU has been reset */
		hrtimer_cancel(&rtdata.timer);
		destroy_hrtimer_on_stack(&rtdata.timer);
		return 0;
	}

	/* No interrupt has been received - check if the RAWSTAT register says
	 * the reset has completed */
	if (kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_IRQ_RAWSTAT), NULL) &
							RESET_COMPLETED) {
		/* The interrupt is set in the RAWSTAT; this suggests that the
		 * interrupts are not getting to the CPU */
		dev_err(kbdev->dev, "Reset interrupt didn't reach CPU. Check interrupt assignments.\n");
		/* If interrupts aren't working we can't continue. */
		destroy_hrtimer_on_stack(&rtdata.timer);
		return -EINVAL;
	}

	/* The GPU doesn't seem to be responding to the reset so try a hard
	 * reset */
	dev_err(kbdev->dev, "Failed to soft-reset GPU (timed out after %d ms), now attempting a hard reset\n",
								RESET_TIMEOUT);
	KBASE_TRACE_ADD(kbdev, CORE_GPU_HARD_RESET, NULL, NULL, 0u, 0);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_COMMAND),
						GPU_COMMAND_HARD_RESET, NULL);

	/* Restart the timer to wait for the hard reset to complete */
	rtdata.timed_out = 0;

	hrtimer_start(&rtdata.timer, HR_TIMER_DELAY_MSEC(RESET_TIMEOUT),
							HRTIMER_MODE_REL);

	/* Wait for the RESET_COMPLETED interrupt to be raised */
	kbase_pm_wait_for_reset(kbdev);

	if (rtdata.timed_out == 0) {
		/* GPU has been reset */
		hrtimer_cancel(&rtdata.timer);
		destroy_hrtimer_on_stack(&rtdata.timer);
		return 0;
	}

	destroy_hrtimer_on_stack(&rtdata.timer);

	dev_err(kbdev->dev, "Failed to hard-reset the GPU (timed out after %d ms)\n",
								RESET_TIMEOUT);

	return -EINVAL;
}

static int kbase_pm_reset_do_protected(struct kbase_device *kbdev)
{
	KBASE_TRACE_ADD(kbdev, CORE_GPU_SOFT_RESET, NULL, NULL, 0u, 0);
	kbase_tlstream_jd_gpu_soft_reset(kbdev);

	return kbdev->protected_ops->protected_mode_reset(kbdev);
}

int kbase_pm_init_hw(struct kbase_device *kbdev, unsigned int flags)
{
	unsigned long irq_flags;
	int err;
	bool resume_vinstr = false;

	KBASE_DEBUG_ASSERT(NULL != kbdev);
	lockdep_assert_held(&kbdev->pm.lock);

	/* Ensure the clock is on before attempting to access the hardware */
	if (!kbdev->pm.backend.gpu_powered) {
		if (kbdev->pm.backend.callback_power_on)
			kbdev->pm.backend.callback_power_on(kbdev);

		spin_lock_irqsave(&kbdev->pm.backend.gpu_powered_lock,
								irq_flags);
		kbdev->pm.backend.gpu_powered = true;
		spin_unlock_irqrestore(&kbdev->pm.backend.gpu_powered_lock,
								irq_flags);
	}

	/* Ensure interrupts are off to begin with, this also clears any
	 * outstanding interrupts */
	kbase_pm_disable_interrupts(kbdev);
	/* Ensure cache snoops are disabled before reset. */
	kbase_pm_cache_snoop_disable(kbdev);
	/* Prepare for the soft-reset */
	kbdev->pm.backend.reset_done = false;

	/* The cores should be made unavailable due to the reset */
	spin_lock_irqsave(&kbdev->hwaccess_lock, irq_flags);
	if (kbdev->shader_available_bitmap != 0u)
			KBASE_TRACE_ADD(kbdev, PM_CORES_CHANGE_AVAILABLE, NULL,
						NULL, 0u, (u32)0u);
	if (kbdev->tiler_available_bitmap != 0u)
			KBASE_TRACE_ADD(kbdev, PM_CORES_CHANGE_AVAILABLE_TILER,
						NULL, NULL, 0u, (u32)0u);
	kbdev->shader_available_bitmap = 0u;
	kbdev->tiler_available_bitmap = 0u;
	kbdev->l2_available_bitmap = 0u;
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, irq_flags);

	/* Soft reset the GPU */
	if (kbdev->protected_mode_support &&
			kbdev->protected_ops->protected_mode_reset)
		err = kbase_pm_reset_do_protected(kbdev);
	else
		err = kbase_pm_reset_do_normal(kbdev);

	spin_lock_irqsave(&kbdev->hwaccess_lock, irq_flags);
	if (kbdev->protected_mode)
		resume_vinstr = true;
	kbdev->protected_mode = false;
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, irq_flags);

	if (err)
		goto exit;

	if (flags & PM_HW_ISSUES_DETECT)
		kbase_pm_hw_issues_detect(kbdev);

	kbase_pm_hw_issues_apply(kbdev);
	kbase_cache_set_coherency_mode(kbdev, kbdev->system_coherency);

	/* Sanity check protected mode was left after reset */
	if (kbase_hw_has_feature(kbdev, BASE_HW_FEATURE_PROTECTED_MODE)) {
		u32 gpu_status = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(GPU_STATUS), NULL);

		WARN_ON(gpu_status & GPU_STATUS_PROTECTED_MODE_ACTIVE);
	}

	/* If cycle counter was in use re-enable it, enable_irqs will only be
	 * false when called from kbase_pm_powerup */
	if (kbdev->pm.backend.gpu_cycle_counter_requests &&
						(flags & PM_ENABLE_IRQS)) {
		/* enable interrupts as the L2 may have to be powered on */
		kbase_pm_enable_interrupts(kbdev);
		kbase_pm_request_l2_caches(kbdev);

		/* Re-enable the counters if we need to */
		spin_lock_irqsave(
			&kbdev->pm.backend.gpu_cycle_counter_requests_lock,
								irq_flags);
		if (kbdev->pm.backend.gpu_cycle_counter_requests)
			kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_COMMAND),
					GPU_COMMAND_CYCLE_COUNT_START, NULL);
		spin_unlock_irqrestore(
			&kbdev->pm.backend.gpu_cycle_counter_requests_lock,
								irq_flags);

		spin_lock_irqsave(&kbdev->hwaccess_lock, irq_flags);
		kbase_pm_release_l2_caches(kbdev);
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, irq_flags);

		kbase_pm_disable_interrupts(kbdev);
	}

	if (flags & PM_ENABLE_IRQS)
		kbase_pm_enable_interrupts(kbdev);

exit:
	/* If GPU is leaving protected mode resume vinstr operation. */
	if (kbdev->vinstr_ctx && resume_vinstr)
		kbase_vinstr_resume(kbdev->vinstr_ctx);

	return err;
}

/**
 * kbase_pm_request_gpu_cycle_counter_do_request - Request cycle counters
 *
 * Increase the count of cycle counter users and turn the cycle counters on if
 * they were previously off
 *
 * This function is designed to be called by
 * kbase_pm_request_gpu_cycle_counter() or
 * kbase_pm_request_gpu_cycle_counter_l2_is_on() only
 *
 * When this function is called the l2 cache must be on and the l2 cache users
 * count must have been incremented by a call to (
 * kbase_pm_request_l2_caches() or kbase_pm_request_l2_caches_l2_on() )
 *
 * @kbdev:     The kbase device structure of the device
 */
static void
kbase_pm_request_gpu_cycle_counter_do_request(struct kbase_device *kbdev)
{
	unsigned long flags;

	spin_lock_irqsave(&kbdev->pm.backend.gpu_cycle_counter_requests_lock,
									flags);

	++kbdev->pm.backend.gpu_cycle_counter_requests;

	if (1 == kbdev->pm.backend.gpu_cycle_counter_requests)
		kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_COMMAND),
					GPU_COMMAND_CYCLE_COUNT_START, NULL);

	spin_unlock_irqrestore(
			&kbdev->pm.backend.gpu_cycle_counter_requests_lock,
									flags);
}

void kbase_pm_request_gpu_cycle_counter(struct kbase_device *kbdev)
{
	KBASE_DEBUG_ASSERT(kbdev != NULL);

	KBASE_DEBUG_ASSERT(kbdev->pm.backend.gpu_powered);

	KBASE_DEBUG_ASSERT(kbdev->pm.backend.gpu_cycle_counter_requests <
								INT_MAX);

	kbase_pm_request_l2_caches(kbdev);

	kbase_pm_request_gpu_cycle_counter_do_request(kbdev);
}

KBASE_EXPORT_TEST_API(kbase_pm_request_gpu_cycle_counter);

void kbase_pm_request_gpu_cycle_counter_l2_is_on(struct kbase_device *kbdev)
{
	KBASE_DEBUG_ASSERT(kbdev != NULL);

	KBASE_DEBUG_ASSERT(kbdev->pm.backend.gpu_powered);

	KBASE_DEBUG_ASSERT(kbdev->pm.backend.gpu_cycle_counter_requests <
								INT_MAX);

	kbase_pm_request_l2_caches_l2_is_on(kbdev);

	kbase_pm_request_gpu_cycle_counter_do_request(kbdev);
}

KBASE_EXPORT_TEST_API(kbase_pm_request_gpu_cycle_counter_l2_is_on);

void kbase_pm_release_gpu_cycle_counter_nolock(struct kbase_device *kbdev)
{
	unsigned long flags;

	KBASE_DEBUG_ASSERT(kbdev != NULL);

	lockdep_assert_held(&kbdev->hwaccess_lock);

	spin_lock_irqsave(&kbdev->pm.backend.gpu_cycle_counter_requests_lock,
									flags);

	KBASE_DEBUG_ASSERT(kbdev->pm.backend.gpu_cycle_counter_requests > 0);

	--kbdev->pm.backend.gpu_cycle_counter_requests;

	if (0 == kbdev->pm.backend.gpu_cycle_counter_requests)
		kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_COMMAND),
					GPU_COMMAND_CYCLE_COUNT_STOP, NULL);

	spin_unlock_irqrestore(
			&kbdev->pm.backend.gpu_cycle_counter_requests_lock,
									flags);

	kbase_pm_release_l2_caches(kbdev);
}

void kbase_pm_release_gpu_cycle_counter(struct kbase_device *kbdev)
{
	unsigned long flags;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	kbase_pm_release_gpu_cycle_counter_nolock(kbdev);

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
}

KBASE_EXPORT_TEST_API(kbase_pm_release_gpu_cycle_counter);
