/*
 * Copyright (c) 2015-2018 The Linux Foundation. All rights reserved.
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _DPU_CRTC_H_
#define _DPU_CRTC_H_

#include <linux/kthread.h>
#include <drm/drm_crtc.h>
#include "dpu_kms.h"
#include "dpu_core_perf.h"
#include "dpu_hw_blk.h"

#define DPU_CRTC_NAME_SIZE	12

/* define the maximum number of in-flight frame events */
#define DPU_CRTC_FRAME_EVENT_SIZE	4

/**
 * enum dpu_crtc_client_type: crtc client type
 * @RT_CLIENT:	RealTime client like video/cmd mode display
 *              voting through apps rsc
 * @NRT_CLIENT:	Non-RealTime client like WB display
 *              voting through apps rsc
 */
enum dpu_crtc_client_type {
	RT_CLIENT,
	NRT_CLIENT,
};

/**
 * enum dpu_crtc_smmu_state:	smmu state
 * @ATTACHED:	 all the context banks are attached.
 * @DETACHED:	 all the context banks are detached.
 * @ATTACH_ALL_REQ:	 transient state of attaching context banks.
 * @DETACH_ALL_REQ:	 transient state of detaching context banks.
 */
enum dpu_crtc_smmu_state {
	ATTACHED = 0,
	DETACHED,
	ATTACH_ALL_REQ,
	DETACH_ALL_REQ,
};

/**
 * enum dpu_crtc_smmu_state_transition_type: state transition type
 * @NONE: no pending state transitions
 * @PRE_COMMIT: state transitions should be done before processing the commit
 * @POST_COMMIT: state transitions to be done after processing the commit.
 */
enum dpu_crtc_smmu_state_transition_type {
	NONE,
	PRE_COMMIT,
	POST_COMMIT
};

/**
 * struct dpu_crtc_smmu_state_data: stores the smmu state and transition type
 * @state: current state of smmu context banks
 * @transition_type: transition request type
 * @transition_error: whether there is error while transitioning the state
 */
struct dpu_crtc_smmu_state_data {
	uint32_t state;
	uint32_t transition_type;
	uint32_t transition_error;
};

/**
 * struct dpu_crtc_mixer: stores the map for each virtual pipeline in the CRTC
 * @hw_lm:	LM HW Driver context
 * @hw_ctl:	CTL Path HW driver context
 * @encoder:	Encoder attached to this lm & ctl
 * @mixer_op_mode:	mixer blending operation mode
 * @flush_mask:	mixer flush mask for ctl, mixer and pipe
 */
struct dpu_crtc_mixer {
	struct dpu_hw_mixer *hw_lm;
	struct dpu_hw_ctl *hw_ctl;
	struct drm_encoder *encoder;
	u32 mixer_op_mode;
	u32 flush_mask;
};

/**
 * struct dpu_crtc_frame_event: stores crtc frame event for crtc processing
 * @work:	base work structure
 * @crtc:	Pointer to crtc handling this event
 * @list:	event list
 * @ts:		timestamp at queue entry
 * @event:	event identifier
 */
struct dpu_crtc_frame_event {
	struct kthread_work work;
	struct drm_crtc *crtc;
	struct list_head list;
	ktime_t ts;
	u32 event;
};

/**
 * struct dpu_crtc_event - event callback tracking structure
 * @list:     Linked list tracking node
 * @kt_work:  Kthread worker structure
 * @dpu_crtc: Pointer to associated dpu_crtc structure
 * @cb_func:  Pointer to callback function
 * @usr:      Pointer to user data to be provided to the callback
 */
struct dpu_crtc_event {
	struct list_head list;
	struct kthread_work kt_work;
	void *dpu_crtc;

	void (*cb_func)(struct drm_crtc *crtc, void *usr);
	void *usr;
};

/*
 * Maximum number of free event structures to cache
 */
#define DPU_CRTC_MAX_EVENT_COUNT	16

/**
 * struct dpu_crtc - virtualized CRTC data structure
 * @base          : Base drm crtc structure
 * @name          : ASCII description of this crtc
 * @num_ctls      : Number of ctl paths in use
 * @num_mixers    : Number of mixers in use
 * @mixers_swapped: Whether the mixers have been swapped for left/right update
 *                  especially in the case of DSC Merge.
 * @mixers        : List of active mixers
 * @event         : Pointer to last received drm vblank event. If there is a
 *                  pending vblank event, this will be non-null.
 * @vsync_count   : Running count of received vsync events
 * @drm_requested_vblank : Whether vblanks have been enabled in the encoder
 * @property_info : Opaque structure for generic property support
 * @property_defaults : Array of default values for generic property support
 * @stage_cfg     : H/w mixer stage configuration
 * @debugfs_root  : Parent of debugfs node
 * @vblank_cb_count : count of vblank callback since last reset
 * @play_count    : frame count between crtc enable and disable
 * @vblank_cb_time  : ktime at vblank count reset
 * @vblank_requested : whether the user has requested vblank events
 * @suspend         : whether or not a suspend operation is in progress
 * @enabled       : whether the DPU CRTC is currently enabled. updated in the
 *                  commit-thread, not state-swap time which is earlier, so
 *                  safe to make decisions on during VBLANK on/off work
 * @feature_list  : list of color processing features supported on a crtc
 * @active_list   : list of color processing features are active
 * @dirty_list    : list of color processing features are dirty
 * @ad_dirty: list containing ad properties that are dirty
 * @ad_active: list containing ad properties that are active
 * @crtc_lock     : crtc lock around create, destroy and access.
 * @frame_pending : Whether or not an update is pending
 * @frame_events  : static allocation of in-flight frame events
 * @frame_event_list : available frame event list
 * @spin_lock     : spin lock for frame event, transaction status, etc...
 * @frame_done_comp    : for frame_event_done synchronization
 * @event_thread  : Pointer to event handler thread
 * @event_worker  : Event worker queue
 * @event_cache   : Local cache of event worker structures
 * @event_free_list : List of available event structures
 * @event_lock    : Spinlock around event handling code
 * @misr_enable   : boolean entry indicates misr enable/disable status.
 * @misr_frame_count  : misr frame count provided by client
 * @misr_data     : store misr data before turning off the clocks.
 * @phandle: Pointer to power handler
 * @power_event   : registered power event handle
 * @cur_perf      : current performance committed to clock/bandwidth driver
 * @rp_lock       : serialization lock for resource pool
 * @rp_head       : list of active resource pool
 * @scl3_cfg_lut  : qseed3 lut config
 */
struct dpu_crtc {
	struct drm_crtc base;
	char name[DPU_CRTC_NAME_SIZE];

	/* HW Resources reserved for the crtc */
	u32 num_ctls;
	u32 num_mixers;
	bool mixers_swapped;
	struct dpu_crtc_mixer mixers[CRTC_DUAL_MIXERS];
	struct dpu_hw_scaler3_lut_cfg *scl3_lut_cfg;

	struct drm_pending_vblank_event *event;
	u32 vsync_count;

	struct dpu_hw_stage_cfg stage_cfg;
	struct dentry *debugfs_root;

	u32 vblank_cb_count;
	u64 play_count;
	ktime_t vblank_cb_time;
	bool vblank_requested;
	bool suspend;
	bool enabled;

	struct list_head feature_list;
	struct list_head active_list;
	struct list_head dirty_list;
	struct list_head ad_dirty;
	struct list_head ad_active;

	struct mutex crtc_lock;

	atomic_t frame_pending;
	struct dpu_crtc_frame_event frame_events[DPU_CRTC_FRAME_EVENT_SIZE];
	struct list_head frame_event_list;
	spinlock_t spin_lock;
	struct completion frame_done_comp;

	/* for handling internal event thread */
	struct dpu_crtc_event event_cache[DPU_CRTC_MAX_EVENT_COUNT];
	struct list_head event_free_list;
	spinlock_t event_lock;
	bool misr_enable;
	u32 misr_frame_count;
	u32 misr_data[CRTC_DUAL_MIXERS];

	struct dpu_power_handle *phandle;
	struct dpu_power_event *power_event;

	struct dpu_core_perf_params cur_perf;

	struct mutex rp_lock;
	struct list_head rp_head;

	struct dpu_crtc_smmu_state_data smmu_state;
};

#define to_dpu_crtc(x) container_of(x, struct dpu_crtc, base)

/**
 * struct dpu_crtc_res_ops - common operations for crtc resources
 * @get: get given resource
 * @put: put given resource
 */
struct dpu_crtc_res_ops {
	void *(*get)(void *val, u32 type, u64 tag);
	void (*put)(void *val);
};

#define DPU_CRTC_RES_FLAG_FREE		BIT(0)

/**
 * struct dpu_crtc_res - definition of crtc resources
 * @list: list of crtc resource
 * @type: crtc resource type
 * @tag: unique identifier per type
 * @refcount: reference/usage count
 * @ops: callback operations
 * @val: resource handle associated with type/tag
 * @flags: customization flags
 */
struct dpu_crtc_res {
	struct list_head list;
	u32 type;
	u64 tag;
	atomic_t refcount;
	struct dpu_crtc_res_ops ops;
	void *val;
	u32 flags;
};

/**
 * dpu_crtc_respool - crtc resource pool
 * @rp_lock: pointer to serialization lock
 * @rp_head: pointer to head of active resource pools of this crtc
 * @rp_list: list of crtc resource pool
 * @sequence_id: sequence identifier, incremented per state duplication
 * @res_list: list of resource managed by this resource pool
 * @ops: resource operations for parent resource pool
 */
struct dpu_crtc_respool {
	struct mutex *rp_lock;
	struct list_head *rp_head;
	struct list_head rp_list;
	u32 sequence_id;
	struct list_head res_list;
	struct dpu_crtc_res_ops ops;
};

/**
 * struct dpu_crtc_state - dpu container for atomic crtc state
 * @base: Base drm crtc state structure
 * @is_ppsplit    : Whether current topology requires PPSplit special handling
 * @bw_control    : true if bw/clk controlled by core bw/clk properties
 * @bw_split_vote : true if bw controlled by llcc/dram bw properties
 * @lm_bounds     : LM boundaries based on current mode full resolution, no ROI.
 *                  Origin top left of CRTC.
 * @property_state: Local storage for msm_prop properties
 * @property_values: Current crtc property values
 * @input_fence_timeout_ns : Cached input fence timeout, in ns
 * @new_perf: new performance state being requested
 */
struct dpu_crtc_state {
	struct drm_crtc_state base;

	bool bw_control;
	bool bw_split_vote;

	bool is_ppsplit;
	struct drm_rect lm_bounds[CRTC_DUAL_MIXERS];

	uint64_t input_fence_timeout_ns;

	struct dpu_core_perf_params new_perf;
	struct dpu_crtc_respool rp;
};

#define to_dpu_crtc_state(x) \
	container_of(x, struct dpu_crtc_state, base)

/**
 * dpu_crtc_get_mixer_width - get the mixer width
 * Mixer width will be same as panel width(/2 for split)
 */
static inline int dpu_crtc_get_mixer_width(struct dpu_crtc *dpu_crtc,
	struct dpu_crtc_state *cstate, struct drm_display_mode *mode)
{
	u32 mixer_width;

	if (!dpu_crtc || !cstate || !mode)
		return 0;

	mixer_width = (dpu_crtc->num_mixers == CRTC_DUAL_MIXERS ?
			mode->hdisplay / CRTC_DUAL_MIXERS : mode->hdisplay);

	return mixer_width;
}

/**
 * dpu_crtc_get_mixer_height - get the mixer height
 * Mixer height will be same as panel height
 */
static inline int dpu_crtc_get_mixer_height(struct dpu_crtc *dpu_crtc,
		struct dpu_crtc_state *cstate, struct drm_display_mode *mode)
{
	if (!dpu_crtc || !cstate || !mode)
		return 0;

	return mode->vdisplay;
}

/**
 * dpu_crtc_frame_pending - retun the number of pending frames
 * @crtc: Pointer to drm crtc object
 */
static inline int dpu_crtc_frame_pending(struct drm_crtc *crtc)
{
	struct dpu_crtc *dpu_crtc;

	if (!crtc)
		return -EINVAL;

	dpu_crtc = to_dpu_crtc(crtc);
	return atomic_read(&dpu_crtc->frame_pending);
}

/**
 * dpu_crtc_vblank - enable or disable vblanks for this crtc
 * @crtc: Pointer to drm crtc object
 * @en: true to enable vblanks, false to disable
 */
int dpu_crtc_vblank(struct drm_crtc *crtc, bool en);

/**
 * dpu_crtc_commit_kickoff - trigger kickoff of the commit for this crtc
 * @crtc: Pointer to drm crtc object
 */
void dpu_crtc_commit_kickoff(struct drm_crtc *crtc);

/**
 * dpu_crtc_complete_commit - callback signalling completion of current commit
 * @crtc: Pointer to drm crtc object
 * @old_state: Pointer to drm crtc old state object
 */
void dpu_crtc_complete_commit(struct drm_crtc *crtc,
		struct drm_crtc_state *old_state);

/**
 * dpu_crtc_init - create a new crtc object
 * @dev: dpu device
 * @plane: base plane
 * @Return: new crtc object or error
 */
struct drm_crtc *dpu_crtc_init(struct drm_device *dev, struct drm_plane *plane);

/**
 * dpu_crtc_register_custom_event - api for enabling/disabling crtc event
 * @kms: Pointer to dpu_kms
 * @crtc_drm: Pointer to crtc object
 * @event: Event that client is interested
 * @en: Flag to enable/disable the event
 */
int dpu_crtc_register_custom_event(struct dpu_kms *kms,
		struct drm_crtc *crtc_drm, u32 event, bool en);

/**
 * dpu_crtc_get_intf_mode - get interface mode of the given crtc
 * @crtc: Pointert to crtc
 */
enum dpu_intf_mode dpu_crtc_get_intf_mode(struct drm_crtc *crtc);

/**
 * dpu_crtc_get_client_type - check the crtc type- rt, nrt etc.
 * @crtc: Pointer to crtc
 */
static inline enum dpu_crtc_client_type dpu_crtc_get_client_type(
						struct drm_crtc *crtc)
{
	struct dpu_crtc_state *cstate =
			crtc ? to_dpu_crtc_state(crtc->state) : NULL;

	if (!cstate)
		return NRT_CLIENT;

	return RT_CLIENT;
}

/**
 * dpu_crtc_is_enabled - check if dpu crtc is enabled or not
 * @crtc: Pointer to crtc
 */
static inline bool dpu_crtc_is_enabled(struct drm_crtc *crtc)
{
	return crtc ? crtc->enabled : false;
}

/**
 * dpu_crtc_event_queue - request event callback
 * @crtc: Pointer to drm crtc structure
 * @func: Pointer to callback function
 * @usr: Pointer to user data to be passed to callback
 * Returns: Zero on success
 */
int dpu_crtc_event_queue(struct drm_crtc *crtc,
		void (*func)(struct drm_crtc *crtc, void *usr), void *usr);

/**
 * dpu_crtc_res_add - add given resource to resource pool in crtc state
 * @state: Pointer to drm crtc state
 * @type: Resource type
 * @tag: Search tag for given resource
 * @val: Resource handle
 * @ops: Resource callback operations
 * return: 0 if success; error code otherwise
 */
int dpu_crtc_res_add(struct drm_crtc_state *state, u32 type, u64 tag,
		void *val, struct dpu_crtc_res_ops *ops);

/**
 * dpu_crtc_res_get - get given resource from resource pool in crtc state
 * @state: Pointer to drm crtc state
 * @type: Resource type
 * @tag: Search tag for given resource
 * return: Resource handle if success; pointer error or null otherwise
 */
void *dpu_crtc_res_get(struct drm_crtc_state *state, u32 type, u64 tag);

/**
 * dpu_crtc_res_put - return given resource to resource pool in crtc state
 * @state: Pointer to drm crtc state
 * @type: Resource type
 * @tag: Search tag for given resource
 * return: None
 */
void dpu_crtc_res_put(struct drm_crtc_state *state, u32 type, u64 tag);

#endif /* _DPU_CRTC_H_ */
