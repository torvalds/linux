// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <linux/export.h>
#include <linux/ref_tracker.h>
#include <linux/types.h>

#include <drm/drm_atomic_state_helper.h>

#include <drm/drm_atomic.h>
#include <drm/drm_print.h>
#include <drm/display/drm_dp.h>
#include <drm/display/drm_dp_helper.h>
#include <drm/display/drm_dp_tunnel.h>

#define to_group(__private_obj) \
	container_of(__private_obj, struct drm_dp_tunnel_group, base)

#define to_group_state(__private_state) \
	container_of(__private_state, struct drm_dp_tunnel_group_state, base)

#define is_dp_tunnel_private_obj(__obj) \
	((__obj)->funcs == &tunnel_group_funcs)

#define for_each_new_group_in_state(__state, __new_group_state, __i) \
	for ((__i) = 0; \
	     (__i) < (__state)->num_private_objs; \
	     (__i)++) \
		for_each_if ((__state)->private_objs[__i].ptr && \
			     is_dp_tunnel_private_obj((__state)->private_objs[__i].ptr) && \
			     ((__new_group_state) = \
				to_group_state((__state)->private_objs[__i].new_state), 1))

#define for_each_old_group_in_state(__state, __old_group_state, __i) \
	for ((__i) = 0; \
	     (__i) < (__state)->num_private_objs; \
	     (__i)++) \
		for_each_if ((__state)->private_objs[__i].ptr && \
			     is_dp_tunnel_private_obj((__state)->private_objs[__i].ptr) && \
			     ((__old_group_state) = \
				to_group_state((__state)->private_objs[__i].old_state), 1))

#define for_each_tunnel_in_group(__group, __tunnel) \
	list_for_each_entry(__tunnel, &(__group)->tunnels, node)

#define for_each_tunnel_state(__group_state, __tunnel_state) \
	list_for_each_entry(__tunnel_state, &(__group_state)->tunnel_states, node)

#define for_each_tunnel_state_safe(__group_state, __tunnel_state, __tunnel_state_tmp) \
	list_for_each_entry_safe(__tunnel_state, __tunnel_state_tmp, \
				 &(__group_state)->tunnel_states, node)

#define kbytes_to_mbits(__kbytes) \
	DIV_ROUND_UP((__kbytes) * 8, 1000)

#define DPTUN_BW_ARG(__bw) ((__bw) < 0 ? (__bw) : kbytes_to_mbits(__bw))

#define __tun_prn(__tunnel, __level, __type, __fmt, ...) \
	drm_##__level##__type((__tunnel)->group->mgr->dev, \
			      "[DPTUN %s][%s] " __fmt, \
			      drm_dp_tunnel_name(__tunnel), \
			      (__tunnel)->aux->name, ## \
			      __VA_ARGS__)

#define tun_dbg(__tunnel, __fmt, ...) \
	__tun_prn(__tunnel, dbg, _kms, __fmt, ## __VA_ARGS__)

#define tun_dbg_stat(__tunnel, __err, __fmt, ...) do { \
	if (__err) \
		__tun_prn(__tunnel, dbg, _kms, __fmt " (Failed, err: %pe)\n", \
			  ## __VA_ARGS__, ERR_PTR(__err)); \
	else \
		__tun_prn(__tunnel, dbg, _kms, __fmt " (Ok)\n", \
			  ## __VA_ARGS__); \
} while (0)

#define tun_dbg_atomic(__tunnel, __fmt, ...) \
	__tun_prn(__tunnel, dbg, _atomic, __fmt, ## __VA_ARGS__)

#define tun_grp_dbg(__group, __fmt, ...) \
	drm_dbg_kms((__group)->mgr->dev, \
		    "[DPTUN %s] " __fmt, \
		    drm_dp_tunnel_group_name(__group), ## \
		    __VA_ARGS__)

#define DP_TUNNELING_BASE DP_TUNNELING_OUI

#define __DPTUN_REG_RANGE(__start, __size) \
	GENMASK_ULL((__start) + (__size) - 1, (__start))

#define DPTUN_REG_RANGE(__addr, __size) \
	__DPTUN_REG_RANGE((__addr) - DP_TUNNELING_BASE, (__size))

#define DPTUN_REG(__addr) DPTUN_REG_RANGE(__addr, 1)

#define DPTUN_INFO_REG_MASK ( \
	DPTUN_REG_RANGE(DP_TUNNELING_OUI, DP_TUNNELING_OUI_BYTES) | \
	DPTUN_REG_RANGE(DP_TUNNELING_DEV_ID, DP_TUNNELING_DEV_ID_BYTES) | \
	DPTUN_REG(DP_TUNNELING_HW_REV) | \
	DPTUN_REG(DP_TUNNELING_SW_REV_MAJOR) | \
	DPTUN_REG(DP_TUNNELING_SW_REV_MINOR) | \
	DPTUN_REG(DP_TUNNELING_CAPABILITIES) | \
	DPTUN_REG(DP_IN_ADAPTER_INFO) | \
	DPTUN_REG(DP_USB4_DRIVER_ID) | \
	DPTUN_REG(DP_USB4_DRIVER_BW_CAPABILITY) | \
	DPTUN_REG(DP_IN_ADAPTER_TUNNEL_INFORMATION) | \
	DPTUN_REG(DP_BW_GRANULARITY) | \
	DPTUN_REG(DP_ESTIMATED_BW) | \
	DPTUN_REG(DP_ALLOCATED_BW) | \
	DPTUN_REG(DP_TUNNELING_MAX_LINK_RATE) | \
	DPTUN_REG(DP_TUNNELING_MAX_LANE_COUNT) | \
	DPTUN_REG(DP_DPTX_BW_ALLOCATION_MODE_CONTROL))

static const DECLARE_BITMAP(dptun_info_regs, 64) = {
	DPTUN_INFO_REG_MASK & -1UL,
#if BITS_PER_LONG == 32
	DPTUN_INFO_REG_MASK >> 32,
#endif
};

struct drm_dp_tunnel_regs {
	u8 buf[HWEIGHT64(DPTUN_INFO_REG_MASK)];
};

struct drm_dp_tunnel_group;

struct drm_dp_tunnel {
	struct drm_dp_tunnel_group *group;

	struct list_head node;

	struct kref kref;
	struct ref_tracker *tracker;
	struct drm_dp_aux *aux;
	char name[8];

	int bw_granularity;
	int estimated_bw;
	int allocated_bw;

	int max_dprx_rate;
	u8 max_dprx_lane_count;

	u8 adapter_id;

	bool bw_alloc_supported:1;
	bool bw_alloc_enabled:1;
	bool has_io_error:1;
	bool destroyed:1;
};

struct drm_dp_tunnel_group_state;

struct drm_dp_tunnel_state {
	struct drm_dp_tunnel_group_state *group_state;

	struct drm_dp_tunnel_ref tunnel_ref;

	struct list_head node;

	u32 stream_mask;
	int *stream_bw;
};

struct drm_dp_tunnel_group_state {
	struct drm_private_state base;

	struct list_head tunnel_states;
};

struct drm_dp_tunnel_group {
	struct drm_private_obj base;
	struct drm_dp_tunnel_mgr *mgr;

	struct list_head tunnels;

	/* available BW including the allocated_bw of all tunnels in the group */
	int available_bw;

	u8 drv_group_id;
	char name[8];

	bool active:1;
};

struct drm_dp_tunnel_mgr {
	struct drm_device *dev;

	int group_count;
	struct drm_dp_tunnel_group *groups;
	wait_queue_head_t bw_req_queue;

#ifdef CONFIG_DRM_DISPLAY_DP_TUNNEL_STATE_DEBUG
	struct ref_tracker_dir ref_tracker;
#endif
};

/*
 * The following helpers provide a way to read out the tunneling DPCD
 * registers with a minimal amount of AUX transfers (1 transfer per contiguous
 * range, as permitted by the 16 byte per transfer AUX limit), not accessing
 * other registers to avoid any read side-effects.
 */
static int next_reg_area(int *offset)
{
	*offset = find_next_bit(dptun_info_regs, 64, *offset);

	return find_next_zero_bit(dptun_info_regs, 64, *offset + 1) - *offset;
}

#define tunnel_reg_ptr(__regs, __address) ({ \
	WARN_ON(!test_bit((__address) - DP_TUNNELING_BASE, dptun_info_regs)); \
	&(__regs)->buf[bitmap_weight(dptun_info_regs, (__address) - DP_TUNNELING_BASE)]; \
})

static int read_tunnel_regs(struct drm_dp_aux *aux, struct drm_dp_tunnel_regs *regs)
{
	int offset = 0;
	int len;

	while ((len = next_reg_area(&offset))) {
		int address = DP_TUNNELING_BASE + offset;

		if (drm_dp_dpcd_read_data(aux, address, tunnel_reg_ptr(regs, address), len) < 0)
			return -EIO;

		offset += len;
	}

	return 0;
}

static u8 tunnel_reg(const struct drm_dp_tunnel_regs *regs, int address)
{
	return *tunnel_reg_ptr(regs, address);
}

static u8 tunnel_reg_drv_group_id(const struct drm_dp_tunnel_regs *regs)
{
	u8 drv_id = tunnel_reg(regs, DP_USB4_DRIVER_ID) & DP_USB4_DRIVER_ID_MASK;
	u8 group_id = tunnel_reg(regs, DP_IN_ADAPTER_TUNNEL_INFORMATION) & DP_GROUP_ID_MASK;

	if (!group_id)
		return 0;

	return (drv_id << DP_GROUP_ID_BITS) | group_id;
}

/* Return granularity in kB/s units */
static int tunnel_reg_bw_granularity(const struct drm_dp_tunnel_regs *regs)
{
	int gr = tunnel_reg(regs, DP_BW_GRANULARITY) & DP_BW_GRANULARITY_MASK;

	if (gr > 2)
		return -1;

	return (250000 << gr) / 8;
}

static int tunnel_reg_max_dprx_rate(const struct drm_dp_tunnel_regs *regs)
{
	u8 bw_code = tunnel_reg(regs, DP_TUNNELING_MAX_LINK_RATE);

	return drm_dp_bw_code_to_link_rate(bw_code);
}

static int tunnel_reg_max_dprx_lane_count(const struct drm_dp_tunnel_regs *regs)
{
	return tunnel_reg(regs, DP_TUNNELING_MAX_LANE_COUNT) &
	       DP_TUNNELING_MAX_LANE_COUNT_MASK;
}

static bool tunnel_reg_bw_alloc_supported(const struct drm_dp_tunnel_regs *regs)
{
	u8 cap_mask = DP_TUNNELING_SUPPORT | DP_IN_BW_ALLOCATION_MODE_SUPPORT;

	if ((tunnel_reg(regs, DP_TUNNELING_CAPABILITIES) & cap_mask) != cap_mask)
		return false;

	return tunnel_reg(regs, DP_USB4_DRIVER_BW_CAPABILITY) &
	       DP_USB4_DRIVER_BW_ALLOCATION_MODE_SUPPORT;
}

static bool tunnel_reg_bw_alloc_enabled(const struct drm_dp_tunnel_regs *regs)
{
	return tunnel_reg(regs, DP_DPTX_BW_ALLOCATION_MODE_CONTROL) &
	       DP_DISPLAY_DRIVER_BW_ALLOCATION_MODE_ENABLE;
}

static u8 tunnel_group_drv_id(u8 drv_group_id)
{
	return drv_group_id >> DP_GROUP_ID_BITS;
}

static u8 tunnel_group_id(u8 drv_group_id)
{
	return drv_group_id & DP_GROUP_ID_MASK;
}

const char *drm_dp_tunnel_name(const struct drm_dp_tunnel *tunnel)
{
	return tunnel->name;
}
EXPORT_SYMBOL(drm_dp_tunnel_name);

static const char *drm_dp_tunnel_group_name(const struct drm_dp_tunnel_group *group)
{
	return group->name;
}

static struct drm_dp_tunnel_group *
lookup_or_alloc_group(struct drm_dp_tunnel_mgr *mgr, u8 drv_group_id)
{
	struct drm_dp_tunnel_group *group = NULL;
	int i;

	for (i = 0; i < mgr->group_count; i++) {
		/*
		 * A tunnel group with 0 group ID shouldn't have more than one
		 * tunnels.
		 */
		if (tunnel_group_id(drv_group_id) &&
		    mgr->groups[i].drv_group_id == drv_group_id)
			return &mgr->groups[i];

		if (!group && !mgr->groups[i].active)
			group = &mgr->groups[i];
	}

	if (!group) {
		drm_dbg_kms(mgr->dev,
			    "DPTUN: Can't allocate more tunnel groups\n");
		return NULL;
	}

	group->drv_group_id = drv_group_id;
	group->active = true;

	/*
	 * The group name format here and elsewhere: Driver-ID:Group-ID:*
	 * (* standing for all DP-Adapters/tunnels in the group).
	 */
	snprintf(group->name, sizeof(group->name), "%d:%d:*",
		 tunnel_group_drv_id(drv_group_id) & ((1 << DP_GROUP_ID_BITS) - 1),
		 tunnel_group_id(drv_group_id) & ((1 << DP_USB4_DRIVER_ID_BITS) - 1));

	return group;
}

static void free_group(struct drm_dp_tunnel_group *group)
{
	struct drm_dp_tunnel_mgr *mgr = group->mgr;

	if (drm_WARN_ON(mgr->dev, !list_empty(&group->tunnels)))
		return;

	group->drv_group_id = 0;
	group->available_bw = -1;
	group->active = false;
}

static struct drm_dp_tunnel *
tunnel_get(struct drm_dp_tunnel *tunnel)
{
	kref_get(&tunnel->kref);

	return tunnel;
}

static void free_tunnel(struct kref *kref)
{
	struct drm_dp_tunnel *tunnel = container_of(kref, typeof(*tunnel), kref);
	struct drm_dp_tunnel_group *group = tunnel->group;

	list_del(&tunnel->node);
	if (list_empty(&group->tunnels))
		free_group(group);

	kfree(tunnel);
}

static void tunnel_put(struct drm_dp_tunnel *tunnel)
{
	kref_put(&tunnel->kref, free_tunnel);
}

#ifdef CONFIG_DRM_DISPLAY_DP_TUNNEL_STATE_DEBUG
static void track_tunnel_ref(struct drm_dp_tunnel *tunnel,
			     struct ref_tracker **tracker)
{
	ref_tracker_alloc(&tunnel->group->mgr->ref_tracker,
			  tracker, GFP_KERNEL);
}

static void untrack_tunnel_ref(struct drm_dp_tunnel *tunnel,
			       struct ref_tracker **tracker)
{
	ref_tracker_free(&tunnel->group->mgr->ref_tracker,
			 tracker);
}
#else
static void track_tunnel_ref(struct drm_dp_tunnel *tunnel,
			     struct ref_tracker **tracker)
{
}

static void untrack_tunnel_ref(struct drm_dp_tunnel *tunnel,
			       struct ref_tracker **tracker)
{
}
#endif

/**
 * drm_dp_tunnel_get - Get a reference for a DP tunnel
 * @tunnel: Tunnel object
 * @tracker: Debug tracker for the reference
 *
 * Get a reference for @tunnel, along with a debug tracker to help locating
 * the source of a reference leak/double reference put etc. issue.
 *
 * The reference must be dropped after use calling drm_dp_tunnel_put()
 * passing @tunnel and *@tracker returned from here.
 *
 * Returns @tunnel - as a convenience - along with *@tracker.
 */
struct drm_dp_tunnel *
drm_dp_tunnel_get(struct drm_dp_tunnel *tunnel,
		  struct ref_tracker **tracker)
{
	track_tunnel_ref(tunnel, tracker);

	return tunnel_get(tunnel);
}
EXPORT_SYMBOL(drm_dp_tunnel_get);

/**
 * drm_dp_tunnel_put - Put a reference for a DP tunnel
 * @tunnel: Tunnel object
 * @tracker: Debug tracker for the reference
 *
 * Put a reference for @tunnel along with its debug *@tracker, which
 * was obtained with drm_dp_tunnel_get().
 */
void drm_dp_tunnel_put(struct drm_dp_tunnel *tunnel,
		       struct ref_tracker **tracker)
{
	untrack_tunnel_ref(tunnel, tracker);

	tunnel_put(tunnel);
}
EXPORT_SYMBOL(drm_dp_tunnel_put);

static bool add_tunnel_to_group(struct drm_dp_tunnel_mgr *mgr,
				u8 drv_group_id,
				struct drm_dp_tunnel *tunnel)
{
	struct drm_dp_tunnel_group *group;

	group = lookup_or_alloc_group(mgr, drv_group_id);
	if (!group)
		return false;

	tunnel->group = group;
	list_add(&tunnel->node, &group->tunnels);

	return true;
}

static struct drm_dp_tunnel *
create_tunnel(struct drm_dp_tunnel_mgr *mgr,
	      struct drm_dp_aux *aux,
	      const struct drm_dp_tunnel_regs *regs)
{
	u8 drv_group_id = tunnel_reg_drv_group_id(regs);
	struct drm_dp_tunnel *tunnel;

	tunnel = kzalloc(sizeof(*tunnel), GFP_KERNEL);
	if (!tunnel)
		return NULL;

	INIT_LIST_HEAD(&tunnel->node);

	kref_init(&tunnel->kref);

	tunnel->aux = aux;

	tunnel->adapter_id = tunnel_reg(regs, DP_IN_ADAPTER_INFO) & DP_IN_ADAPTER_NUMBER_MASK;

	snprintf(tunnel->name, sizeof(tunnel->name), "%d:%d:%d",
		 tunnel_group_drv_id(drv_group_id) & ((1 << DP_GROUP_ID_BITS) - 1),
		 tunnel_group_id(drv_group_id) & ((1 << DP_USB4_DRIVER_ID_BITS) - 1),
		 tunnel->adapter_id & ((1 << DP_IN_ADAPTER_NUMBER_BITS) - 1));

	tunnel->bw_granularity = tunnel_reg_bw_granularity(regs);
	tunnel->allocated_bw = tunnel_reg(regs, DP_ALLOCATED_BW) *
			       tunnel->bw_granularity;
	/*
	 * An initial allocated BW of 0 indicates an undefined state: the
	 * actual allocation is determined by the TBT CM, usually following a
	 * legacy allocation policy (based on the max DPRX caps). From the
	 * driver's POV the state becomes defined only after the first
	 * allocation request.
	 */
	if (!tunnel->allocated_bw)
		tunnel->allocated_bw = -1;

	tunnel->bw_alloc_supported = tunnel_reg_bw_alloc_supported(regs);
	tunnel->bw_alloc_enabled = tunnel_reg_bw_alloc_enabled(regs);

	if (!add_tunnel_to_group(mgr, drv_group_id, tunnel)) {
		kfree(tunnel);

		return NULL;
	}

	track_tunnel_ref(tunnel, &tunnel->tracker);

	return tunnel;
}

static void destroy_tunnel(struct drm_dp_tunnel *tunnel)
{
	untrack_tunnel_ref(tunnel, &tunnel->tracker);
	tunnel_put(tunnel);
}

/**
 * drm_dp_tunnel_set_io_error - Set the IO error flag for a DP tunnel
 * @tunnel: Tunnel object
 *
 * Set the IO error flag for @tunnel. Drivers can call this function upon
 * detecting a failure that affects the tunnel functionality, for instance
 * after a DP AUX transfer failure on the port @tunnel is connected to.
 *
 * This disables further management of @tunnel, including any related
 * AUX accesses for tunneling DPCD registers, returning error to the
 * initiators of these. The driver is supposed to drop this tunnel and -
 * optionally - recreate it.
 */
void drm_dp_tunnel_set_io_error(struct drm_dp_tunnel *tunnel)
{
	tunnel->has_io_error = true;
}
EXPORT_SYMBOL(drm_dp_tunnel_set_io_error);

#define SKIP_DPRX_CAPS_CHECK		BIT(0)
#define ALLOW_ALLOCATED_BW_CHANGE	BIT(1)
static bool tunnel_regs_are_valid(struct drm_dp_tunnel_mgr *mgr,
				  const struct drm_dp_tunnel_regs *regs,
				  unsigned int flags)
{
	u8 drv_group_id = tunnel_reg_drv_group_id(regs);
	bool check_dprx = !(flags & SKIP_DPRX_CAPS_CHECK);
	bool ret = true;

	if (!tunnel_reg_bw_alloc_supported(regs)) {
		if (tunnel_group_id(drv_group_id)) {
			drm_dbg_kms(mgr->dev,
				    "DPTUN: A non-zero group ID is only allowed with BWA support\n");
			ret = false;
		}

		if (tunnel_reg(regs, DP_ALLOCATED_BW)) {
			drm_dbg_kms(mgr->dev,
				    "DPTUN: BW is allocated without BWA support\n");
			ret = false;
		}

		return ret;
	}

	if (!tunnel_group_id(drv_group_id)) {
		drm_dbg_kms(mgr->dev,
			    "DPTUN: BWA support requires a non-zero group ID\n");
		ret = false;
	}

	if (check_dprx && hweight8(tunnel_reg_max_dprx_lane_count(regs)) != 1) {
		drm_dbg_kms(mgr->dev,
			    "DPTUN: Invalid DPRX lane count: %d\n",
			    tunnel_reg_max_dprx_lane_count(regs));

		ret = false;
	}

	if (check_dprx && !tunnel_reg_max_dprx_rate(regs)) {
		drm_dbg_kms(mgr->dev,
			    "DPTUN: DPRX rate is 0\n");

		ret = false;
	}

	if (tunnel_reg_bw_granularity(regs) < 0) {
		drm_dbg_kms(mgr->dev,
			    "DPTUN: Invalid BW granularity\n");

		ret = false;
	}

	if (tunnel_reg(regs, DP_ALLOCATED_BW) > tunnel_reg(regs, DP_ESTIMATED_BW)) {
		drm_dbg_kms(mgr->dev,
			    "DPTUN: Allocated BW %d > estimated BW %d Mb/s\n",
			    DPTUN_BW_ARG(tunnel_reg(regs, DP_ALLOCATED_BW) *
					 tunnel_reg_bw_granularity(regs)),
			    DPTUN_BW_ARG(tunnel_reg(regs, DP_ESTIMATED_BW) *
					 tunnel_reg_bw_granularity(regs)));

		ret = false;
	}

	return ret;
}

static int tunnel_allocated_bw(const struct drm_dp_tunnel *tunnel)
{
	return max(tunnel->allocated_bw, 0);
}

static bool tunnel_info_changes_are_valid(struct drm_dp_tunnel *tunnel,
					  const struct drm_dp_tunnel_regs *regs,
					  unsigned int flags)
{
	u8 new_drv_group_id = tunnel_reg_drv_group_id(regs);
	bool ret = true;

	if (tunnel->bw_alloc_supported != tunnel_reg_bw_alloc_supported(regs)) {
		tun_dbg(tunnel,
			"BW alloc support has changed %s -> %s\n",
			str_yes_no(tunnel->bw_alloc_supported),
			str_yes_no(tunnel_reg_bw_alloc_supported(regs)));

		ret = false;
	}

	if (tunnel->group->drv_group_id != new_drv_group_id) {
		tun_dbg(tunnel,
			"Driver/group ID has changed %d:%d:* -> %d:%d:*\n",
			tunnel_group_drv_id(tunnel->group->drv_group_id),
			tunnel_group_id(tunnel->group->drv_group_id),
			tunnel_group_drv_id(new_drv_group_id),
			tunnel_group_id(new_drv_group_id));

		ret = false;
	}

	if (!tunnel->bw_alloc_supported)
		return ret;

	if (tunnel->bw_granularity != tunnel_reg_bw_granularity(regs)) {
		tun_dbg(tunnel,
			"BW granularity has changed: %d -> %d Mb/s\n",
			DPTUN_BW_ARG(tunnel->bw_granularity),
			DPTUN_BW_ARG(tunnel_reg_bw_granularity(regs)));

		ret = false;
	}

	/*
	 * On some devices at least the BW alloc mode enabled status is always
	 * reported as 0, so skip checking that here.
	 */

	if (!(flags & ALLOW_ALLOCATED_BW_CHANGE) &&
	    tunnel_allocated_bw(tunnel) !=
	    tunnel_reg(regs, DP_ALLOCATED_BW) * tunnel->bw_granularity) {
		tun_dbg(tunnel,
			"Allocated BW has changed: %d -> %d Mb/s\n",
			DPTUN_BW_ARG(tunnel->allocated_bw),
			DPTUN_BW_ARG(tunnel_reg(regs, DP_ALLOCATED_BW) * tunnel->bw_granularity));

		ret = false;
	}

	return ret;
}

static int
read_and_verify_tunnel_regs(struct drm_dp_tunnel *tunnel,
			    struct drm_dp_tunnel_regs *regs,
			    unsigned int flags)
{
	int err;

	err = read_tunnel_regs(tunnel->aux, regs);
	if (err < 0) {
		drm_dp_tunnel_set_io_error(tunnel);

		return err;
	}

	if (!tunnel_regs_are_valid(tunnel->group->mgr, regs, flags))
		return -EINVAL;

	if (!tunnel_info_changes_are_valid(tunnel, regs, flags))
		return -EINVAL;

	return 0;
}

static bool update_dprx_caps(struct drm_dp_tunnel *tunnel, const struct drm_dp_tunnel_regs *regs)
{
	bool changed = false;

	if (tunnel_reg_max_dprx_rate(regs) != tunnel->max_dprx_rate) {
		tunnel->max_dprx_rate = tunnel_reg_max_dprx_rate(regs);
		changed = true;
	}

	if (tunnel_reg_max_dprx_lane_count(regs) != tunnel->max_dprx_lane_count) {
		tunnel->max_dprx_lane_count = tunnel_reg_max_dprx_lane_count(regs);
		changed = true;
	}

	return changed;
}

static int dev_id_len(const u8 *dev_id, int max_len)
{
	while (max_len && dev_id[max_len - 1] == '\0')
		max_len--;

	return max_len;
}

static int get_max_dprx_bw(const struct drm_dp_tunnel *tunnel)
{
	int max_dprx_bw = drm_dp_max_dprx_data_rate(tunnel->max_dprx_rate,
						    tunnel->max_dprx_lane_count);

	/*
	 * A BW request of roundup(max_dprx_bw, tunnel->bw_granularity) results in
	 * an allocation of max_dprx_bw. A BW request above this rounded-up
	 * value will fail.
	 */
	return min(roundup(max_dprx_bw, tunnel->bw_granularity),
		   MAX_DP_REQUEST_BW * tunnel->bw_granularity);
}

static int get_max_tunnel_bw(const struct drm_dp_tunnel *tunnel)
{
	return min(get_max_dprx_bw(tunnel), tunnel->group->available_bw);
}

/**
 * drm_dp_tunnel_detect - Detect DP tunnel on the link
 * @mgr: Tunnel manager
 * @aux: DP AUX on which the tunnel will be detected
 *
 * Detect if there is any DP tunnel on the link and add it to the tunnel
 * group's tunnel list.
 *
 * Returns a pointer to a tunnel on success, or an ERR_PTR() error on
 * failure.
 */
struct drm_dp_tunnel *
drm_dp_tunnel_detect(struct drm_dp_tunnel_mgr *mgr,
		     struct drm_dp_aux *aux)
{
	struct drm_dp_tunnel_regs regs;
	struct drm_dp_tunnel *tunnel;
	int err;

	err = read_tunnel_regs(aux, &regs);
	if (err)
		return ERR_PTR(err);

	if (!(tunnel_reg(&regs, DP_TUNNELING_CAPABILITIES) &
	      DP_TUNNELING_SUPPORT))
		return ERR_PTR(-ENODEV);

	/* The DPRX caps are valid only after enabling BW alloc mode. */
	if (!tunnel_regs_are_valid(mgr, &regs, SKIP_DPRX_CAPS_CHECK))
		return ERR_PTR(-EINVAL);

	tunnel = create_tunnel(mgr, aux, &regs);
	if (!tunnel)
		return ERR_PTR(-ENOMEM);

	tun_dbg(tunnel,
		"OUI:%*phD DevID:%*pE Rev-HW:%d.%d SW:%d.%d PR-Sup:%s BWA-Sup:%s BWA-En:%s\n",
		DP_TUNNELING_OUI_BYTES,
			tunnel_reg_ptr(&regs, DP_TUNNELING_OUI),
		dev_id_len(tunnel_reg_ptr(&regs, DP_TUNNELING_DEV_ID), DP_TUNNELING_DEV_ID_BYTES),
			tunnel_reg_ptr(&regs, DP_TUNNELING_DEV_ID),
		(tunnel_reg(&regs, DP_TUNNELING_HW_REV) & DP_TUNNELING_HW_REV_MAJOR_MASK) >>
			DP_TUNNELING_HW_REV_MAJOR_SHIFT,
		(tunnel_reg(&regs, DP_TUNNELING_HW_REV) & DP_TUNNELING_HW_REV_MINOR_MASK) >>
			DP_TUNNELING_HW_REV_MINOR_SHIFT,
		tunnel_reg(&regs, DP_TUNNELING_SW_REV_MAJOR),
		tunnel_reg(&regs, DP_TUNNELING_SW_REV_MINOR),
		str_yes_no(tunnel_reg(&regs, DP_TUNNELING_CAPABILITIES) &
			   DP_PANEL_REPLAY_OPTIMIZATION_SUPPORT),
		str_yes_no(tunnel->bw_alloc_supported),
		str_yes_no(tunnel->bw_alloc_enabled));

	return tunnel;
}
EXPORT_SYMBOL(drm_dp_tunnel_detect);

/**
 * drm_dp_tunnel_destroy - Destroy tunnel object
 * @tunnel: Tunnel object
 *
 * Remove the tunnel from the tunnel topology and destroy it.
 *
 * Returns 0 on success, -ENODEV if the tunnel has been destroyed already.
 */
int drm_dp_tunnel_destroy(struct drm_dp_tunnel *tunnel)
{
	if (!tunnel)
		return 0;

	if (drm_WARN_ON(tunnel->group->mgr->dev, tunnel->destroyed))
		return -ENODEV;

	tun_dbg(tunnel, "destroying\n");

	tunnel->destroyed = true;
	destroy_tunnel(tunnel);

	return 0;
}
EXPORT_SYMBOL(drm_dp_tunnel_destroy);

static int check_tunnel(const struct drm_dp_tunnel *tunnel)
{
	if (tunnel->destroyed)
		return -ENODEV;

	if (tunnel->has_io_error)
		return -EIO;

	return 0;
}

static int group_allocated_bw(struct drm_dp_tunnel_group *group)
{
	struct drm_dp_tunnel *tunnel;
	int group_allocated_bw = 0;

	for_each_tunnel_in_group(group, tunnel) {
		if (check_tunnel(tunnel) == 0 &&
		    tunnel->bw_alloc_enabled)
			group_allocated_bw += tunnel_allocated_bw(tunnel);
	}

	return group_allocated_bw;
}

/*
 * The estimated BW reported by the TBT Connection Manager for each tunnel in
 * a group includes the BW already allocated for the given tunnel and the
 * unallocated BW which is free to be used by any tunnel in the group.
 */
static int group_free_bw(const struct drm_dp_tunnel *tunnel)
{
	return tunnel->estimated_bw - tunnel_allocated_bw(tunnel);
}

static int calc_group_available_bw(const struct drm_dp_tunnel *tunnel)
{
	return group_allocated_bw(tunnel->group) +
	       group_free_bw(tunnel);
}

static int update_group_available_bw(struct drm_dp_tunnel *tunnel,
				     const struct drm_dp_tunnel_regs *regs)
{
	struct drm_dp_tunnel *tunnel_iter;
	int group_available_bw;
	bool changed;

	tunnel->estimated_bw = tunnel_reg(regs, DP_ESTIMATED_BW) * tunnel->bw_granularity;

	if (calc_group_available_bw(tunnel) == tunnel->group->available_bw)
		return 0;

	for_each_tunnel_in_group(tunnel->group, tunnel_iter) {
		int err;

		if (tunnel_iter == tunnel)
			continue;

		if (check_tunnel(tunnel_iter) != 0 ||
		    !tunnel_iter->bw_alloc_enabled)
			continue;

		err = drm_dp_dpcd_probe(tunnel_iter->aux, DP_DPCD_REV);
		if (err) {
			tun_dbg(tunnel_iter,
				"Probe failed, assume disconnected (err %pe)\n",
				ERR_PTR(err));
			drm_dp_tunnel_set_io_error(tunnel_iter);
		}
	}

	group_available_bw = calc_group_available_bw(tunnel);

	tun_dbg(tunnel, "Updated group available BW: %d->%d\n",
		DPTUN_BW_ARG(tunnel->group->available_bw),
		DPTUN_BW_ARG(group_available_bw));

	changed = tunnel->group->available_bw != group_available_bw;

	tunnel->group->available_bw = group_available_bw;

	return changed ? 1 : 0;
}

static int set_bw_alloc_mode(struct drm_dp_tunnel *tunnel, bool enable)
{
	u8 mask = DP_DISPLAY_DRIVER_BW_ALLOCATION_MODE_ENABLE | DP_UNMASK_BW_ALLOCATION_IRQ;
	u8 val;

	if (drm_dp_dpcd_read_byte(tunnel->aux, DP_DPTX_BW_ALLOCATION_MODE_CONTROL, &val) < 0)
		goto out_err;

	if (enable)
		val |= mask;
	else
		val &= ~mask;

	if (drm_dp_dpcd_write_byte(tunnel->aux, DP_DPTX_BW_ALLOCATION_MODE_CONTROL, val) < 0)
		goto out_err;

	tunnel->bw_alloc_enabled = enable;

	return 0;

out_err:
	drm_dp_tunnel_set_io_error(tunnel);

	return -EIO;
}

/**
 * drm_dp_tunnel_enable_bw_alloc - Enable DP tunnel BW allocation mode
 * @tunnel: Tunnel object
 *
 * Enable the DP tunnel BW allocation mode on @tunnel if it supports it.
 *
 * Returns 0 in case of success, negative error code otherwise.
 */
int drm_dp_tunnel_enable_bw_alloc(struct drm_dp_tunnel *tunnel)
{
	struct drm_dp_tunnel_regs regs;
	int err;

	err = check_tunnel(tunnel);
	if (err)
		return err;

	if (!tunnel->bw_alloc_supported)
		return -EOPNOTSUPP;

	if (!tunnel_group_id(tunnel->group->drv_group_id))
		return -EINVAL;

	err = set_bw_alloc_mode(tunnel, true);
	if (err)
		goto out;

	/*
	 * After a BWA disable/re-enable sequence the allocated BW can either
	 * stay at its last requested value or, for instance after system
	 * suspend/resume, TBT CM can reset back the allocation to the amount
	 * allocated in the legacy/non-BWA mode. Accordingly allow for the
	 * allocation to change wrt. the last SW state.
	 */
	err = read_and_verify_tunnel_regs(tunnel, &regs,
					  ALLOW_ALLOCATED_BW_CHANGE);
	if (err) {
		set_bw_alloc_mode(tunnel, false);

		goto out;
	}

	if (!tunnel->max_dprx_rate)
		update_dprx_caps(tunnel, &regs);

	if (tunnel->group->available_bw == -1) {
		err = update_group_available_bw(tunnel, &regs);
		if (err > 0)
			err = 0;
	}
out:
	tun_dbg_stat(tunnel, err,
		     "Enabling BW alloc mode: DPRX:%dx%d Group alloc:%d/%d Mb/s",
		     tunnel->max_dprx_rate / 100, tunnel->max_dprx_lane_count,
		     DPTUN_BW_ARG(group_allocated_bw(tunnel->group)),
		     DPTUN_BW_ARG(tunnel->group->available_bw));

	return err;
}
EXPORT_SYMBOL(drm_dp_tunnel_enable_bw_alloc);

/**
 * drm_dp_tunnel_disable_bw_alloc - Disable DP tunnel BW allocation mode
 * @tunnel: Tunnel object
 *
 * Disable the DP tunnel BW allocation mode on @tunnel.
 *
 * Returns 0 in case of success, negative error code otherwise.
 */
int drm_dp_tunnel_disable_bw_alloc(struct drm_dp_tunnel *tunnel)
{
	int err;

	err = check_tunnel(tunnel);
	if (err)
		return err;

	tunnel->allocated_bw = -1;

	err = set_bw_alloc_mode(tunnel, false);

	tun_dbg_stat(tunnel, err, "Disabling BW alloc mode");

	return err;
}
EXPORT_SYMBOL(drm_dp_tunnel_disable_bw_alloc);

/**
 * drm_dp_tunnel_bw_alloc_is_enabled - Query the BW allocation mode enabled state
 * @tunnel: Tunnel object
 *
 * Query if the BW allocation mode is enabled for @tunnel.
 *
 * Returns %true if the BW allocation mode is enabled for @tunnel.
 */
bool drm_dp_tunnel_bw_alloc_is_enabled(const struct drm_dp_tunnel *tunnel)
{
	return tunnel && tunnel->bw_alloc_enabled;
}
EXPORT_SYMBOL(drm_dp_tunnel_bw_alloc_is_enabled);

static int clear_bw_req_state(struct drm_dp_aux *aux)
{
	u8 bw_req_mask = DP_BW_REQUEST_SUCCEEDED | DP_BW_REQUEST_FAILED;

	if (drm_dp_dpcd_write_byte(aux, DP_TUNNELING_STATUS, bw_req_mask) < 0)
		return -EIO;

	return 0;
}

static int bw_req_complete(struct drm_dp_aux *aux, bool *status_changed)
{
	u8 bw_req_mask = DP_BW_REQUEST_SUCCEEDED | DP_BW_REQUEST_FAILED;
	u8 status_change_mask = DP_BW_ALLOCATION_CAPABILITY_CHANGED | DP_ESTIMATED_BW_CHANGED;
	u8 val;
	int err;

	if (drm_dp_dpcd_read_byte(aux, DP_TUNNELING_STATUS, &val) < 0)
		return -EIO;

	*status_changed = val & status_change_mask;

	val &= bw_req_mask;

	if (!val)
		return -EAGAIN;

	err = clear_bw_req_state(aux);
	if (err < 0)
		return err;

	return val == DP_BW_REQUEST_SUCCEEDED ? 0 : -ENOSPC;
}

static int allocate_tunnel_bw(struct drm_dp_tunnel *tunnel, int bw)
{
	struct drm_dp_tunnel_mgr *mgr = tunnel->group->mgr;
	int request_bw = DIV_ROUND_UP(bw, tunnel->bw_granularity);
	DEFINE_WAIT_FUNC(wait, woken_wake_function);
	long timeout;
	int err;

	if (bw < 0) {
		err = -EINVAL;
		goto out;
	}

	if (request_bw * tunnel->bw_granularity == tunnel->allocated_bw)
		return 0;

	/* Atomic check should prevent the following. */
	if (drm_WARN_ON(mgr->dev, request_bw > MAX_DP_REQUEST_BW)) {
		err = -EINVAL;
		goto out;
	}

	err = clear_bw_req_state(tunnel->aux);
	if (err)
		goto out;

	if (drm_dp_dpcd_write_byte(tunnel->aux, DP_REQUEST_BW, request_bw) < 0) {
		err = -EIO;
		goto out;
	}

	timeout = msecs_to_jiffies(3000);
	add_wait_queue(&mgr->bw_req_queue, &wait);

	for (;;) {
		bool status_changed;

		err = bw_req_complete(tunnel->aux, &status_changed);
		if (err != -EAGAIN)
			break;

		if (status_changed) {
			struct drm_dp_tunnel_regs regs;

			err = read_and_verify_tunnel_regs(tunnel, &regs,
							  ALLOW_ALLOCATED_BW_CHANGE);
			if (err)
				break;
		}

		if (!timeout) {
			err = -ETIMEDOUT;
			break;
		}

		timeout = wait_woken(&wait, TASK_UNINTERRUPTIBLE, timeout);
	};

	remove_wait_queue(&mgr->bw_req_queue, &wait);

	if (err)
		goto out;

	tunnel->allocated_bw = request_bw * tunnel->bw_granularity;

out:
	tun_dbg_stat(tunnel, err, "Allocating %d/%d Mb/s for tunnel: Group alloc:%d/%d Mb/s",
		     DPTUN_BW_ARG(request_bw * tunnel->bw_granularity),
		     DPTUN_BW_ARG(get_max_tunnel_bw(tunnel)),
		     DPTUN_BW_ARG(group_allocated_bw(tunnel->group)),
		     DPTUN_BW_ARG(tunnel->group->available_bw));

	if (err == -EIO)
		drm_dp_tunnel_set_io_error(tunnel);

	return err;
}

/**
 * drm_dp_tunnel_alloc_bw - Allocate BW for a DP tunnel
 * @tunnel: Tunnel object
 * @bw: BW in kB/s units
 *
 * Allocate @bw kB/s for @tunnel. The allocated BW must be freed after use by
 * calling this function for the same tunnel setting @bw to 0.
 *
 * Returns 0 in case of success, a negative error code otherwise.
 */
int drm_dp_tunnel_alloc_bw(struct drm_dp_tunnel *tunnel, int bw)
{
	int err;

	err = check_tunnel(tunnel);
	if (err)
		return err;

	return allocate_tunnel_bw(tunnel, bw);
}
EXPORT_SYMBOL(drm_dp_tunnel_alloc_bw);

/**
 * drm_dp_tunnel_get_allocated_bw - Get the BW allocated for a DP tunnel
 * @tunnel: Tunnel object
 *
 * Get the current BW allocated for @tunnel. After the tunnel is created /
 * resumed and the BW allocation mode is enabled for it, the allocation
 * becomes determined only after the first allocation request by the driver
 * calling drm_dp_tunnel_alloc_bw().
 *
 * Return the BW allocated for the tunnel, or -1 if the allocation is
 * undetermined.
 */
int drm_dp_tunnel_get_allocated_bw(struct drm_dp_tunnel *tunnel)
{
	return tunnel->allocated_bw;
}
EXPORT_SYMBOL(drm_dp_tunnel_get_allocated_bw);

/*
 * Return 0 if the status hasn't changed, 1 if the status has changed, a
 * negative error code in case of an I/O failure.
 */
static int check_and_clear_status_change(struct drm_dp_tunnel *tunnel)
{
	u8 mask = DP_BW_ALLOCATION_CAPABILITY_CHANGED | DP_ESTIMATED_BW_CHANGED;
	u8 val;

	if (drm_dp_dpcd_read_byte(tunnel->aux, DP_TUNNELING_STATUS, &val) < 0)
		goto out_err;

	val &= mask;

	if (val) {
		if (drm_dp_dpcd_write_byte(tunnel->aux, DP_TUNNELING_STATUS, val) < 0)
			goto out_err;

		return 1;
	}

	if (!drm_dp_tunnel_bw_alloc_is_enabled(tunnel))
		return 0;

	/*
	 * Check for estimated BW changes explicitly to account for lost
	 * BW change notifications.
	 */
	if (drm_dp_dpcd_read_byte(tunnel->aux, DP_ESTIMATED_BW, &val) < 0)
		goto out_err;

	if (val * tunnel->bw_granularity != tunnel->estimated_bw)
		return 1;

	return 0;

out_err:
	drm_dp_tunnel_set_io_error(tunnel);

	return -EIO;
}

/**
 * drm_dp_tunnel_update_state - Update DP tunnel SW state with the HW state
 * @tunnel: Tunnel object
 *
 * Update the SW state of @tunnel with the HW state.
 *
 * Returns 0 if the state has not changed, 1 if it has changed and got updated
 * successfully and a negative error code otherwise.
 */
int drm_dp_tunnel_update_state(struct drm_dp_tunnel *tunnel)
{
	struct drm_dp_tunnel_regs regs;
	bool changed = false;
	int ret;

	ret = check_tunnel(tunnel);
	if (ret < 0)
		return ret;

	ret = check_and_clear_status_change(tunnel);
	if (ret < 0)
		goto out;

	if (!ret)
		return 0;

	ret = read_and_verify_tunnel_regs(tunnel, &regs, 0);
	if (ret)
		goto out;

	if (update_dprx_caps(tunnel, &regs))
		changed = true;

	ret = update_group_available_bw(tunnel, &regs);
	if (ret == 1)
		changed = true;

out:
	tun_dbg_stat(tunnel, ret < 0 ? ret : 0,
		     "State update: Changed:%s DPRX:%dx%d Tunnel alloc:%d/%d Group alloc:%d/%d Mb/s",
		     str_yes_no(changed),
		     tunnel->max_dprx_rate / 100, tunnel->max_dprx_lane_count,
		     DPTUN_BW_ARG(tunnel->allocated_bw),
		     DPTUN_BW_ARG(get_max_tunnel_bw(tunnel)),
		     DPTUN_BW_ARG(group_allocated_bw(tunnel->group)),
		     DPTUN_BW_ARG(tunnel->group->available_bw));

	if (ret < 0)
		return ret;

	if (changed)
		return 1;

	return 0;
}
EXPORT_SYMBOL(drm_dp_tunnel_update_state);

/*
 * drm_dp_tunnel_handle_irq - Handle DP tunnel IRQs
 *
 * Handle any pending DP tunnel IRQs, waking up waiters for a completion
 * event.
 *
 * Returns 1 if the state of the tunnel has changed which requires calling
 * drm_dp_tunnel_update_state(), a negative error code in case of a failure,
 * 0 otherwise.
 */
int drm_dp_tunnel_handle_irq(struct drm_dp_tunnel_mgr *mgr, struct drm_dp_aux *aux)
{
	u8 val;

	if (drm_dp_dpcd_read_byte(aux, DP_TUNNELING_STATUS, &val) < 0)
		return -EIO;

	if (val & (DP_BW_REQUEST_SUCCEEDED | DP_BW_REQUEST_FAILED))
		wake_up_all(&mgr->bw_req_queue);

	if (val & (DP_BW_ALLOCATION_CAPABILITY_CHANGED | DP_ESTIMATED_BW_CHANGED))
		return 1;

	return 0;
}
EXPORT_SYMBOL(drm_dp_tunnel_handle_irq);

/**
 * drm_dp_tunnel_max_dprx_rate - Query the maximum rate of the tunnel's DPRX
 * @tunnel: Tunnel object
 *
 * The function is used to query the maximum link rate of the DPRX connected
 * to @tunnel. Note that this rate will not be limited by the BW limit of the
 * tunnel, as opposed to the standard and extended DP_MAX_LINK_RATE DPCD
 * registers.
 *
 * Returns the maximum link rate in 10 kbit/s units.
 */
int drm_dp_tunnel_max_dprx_rate(const struct drm_dp_tunnel *tunnel)
{
	return tunnel->max_dprx_rate;
}
EXPORT_SYMBOL(drm_dp_tunnel_max_dprx_rate);

/**
 * drm_dp_tunnel_max_dprx_lane_count - Query the maximum lane count of the tunnel's DPRX
 * @tunnel: Tunnel object
 *
 * The function is used to query the maximum lane count of the DPRX connected
 * to @tunnel. Note that this lane count will not be limited by the BW limit of
 * the tunnel, as opposed to the standard and extended DP_MAX_LANE_COUNT DPCD
 * registers.
 *
 * Returns the maximum lane count.
 */
int drm_dp_tunnel_max_dprx_lane_count(const struct drm_dp_tunnel *tunnel)
{
	return tunnel->max_dprx_lane_count;
}
EXPORT_SYMBOL(drm_dp_tunnel_max_dprx_lane_count);

/**
 * drm_dp_tunnel_available_bw - Query the estimated total available BW of the tunnel
 * @tunnel: Tunnel object
 *
 * This function is used to query the estimated total available BW of the
 * tunnel. This includes the currently allocated and free BW for all the
 * tunnels in @tunnel's group. The available BW is valid only after the BW
 * allocation mode has been enabled for the tunnel and its state got updated
 * calling drm_dp_tunnel_update_state().
 *
 * Returns the @tunnel group's estimated total available bandwidth in kB/s
 * units, or -1 if the available BW isn't valid (the BW allocation mode is
 * not enabled or the tunnel's state hasn't been updated).
 */
int drm_dp_tunnel_available_bw(const struct drm_dp_tunnel *tunnel)
{
	return tunnel->group->available_bw;
}
EXPORT_SYMBOL(drm_dp_tunnel_available_bw);

static struct drm_dp_tunnel_group_state *
drm_dp_tunnel_atomic_get_group_state(struct drm_atomic_state *state,
				     const struct drm_dp_tunnel *tunnel)
{
	return (struct drm_dp_tunnel_group_state *)
		drm_atomic_get_private_obj_state(state,
						 &tunnel->group->base);
}

static struct drm_dp_tunnel_state *
add_tunnel_state(struct drm_dp_tunnel_group_state *group_state,
		 struct drm_dp_tunnel *tunnel)
{
	struct drm_dp_tunnel_state *tunnel_state;

	tun_dbg_atomic(tunnel,
		       "Adding state for tunnel %p to group state %p\n",
		       tunnel, group_state);

	tunnel_state = kzalloc(sizeof(*tunnel_state), GFP_KERNEL);
	if (!tunnel_state)
		return NULL;

	tunnel_state->group_state = group_state;

	drm_dp_tunnel_ref_get(tunnel, &tunnel_state->tunnel_ref);

	INIT_LIST_HEAD(&tunnel_state->node);
	list_add(&tunnel_state->node, &group_state->tunnel_states);

	return tunnel_state;
}

static void free_tunnel_state(struct drm_dp_tunnel_state *tunnel_state)
{
	tun_dbg_atomic(tunnel_state->tunnel_ref.tunnel,
		       "Freeing state for tunnel %p\n",
		       tunnel_state->tunnel_ref.tunnel);

	list_del(&tunnel_state->node);

	kfree(tunnel_state->stream_bw);
	drm_dp_tunnel_ref_put(&tunnel_state->tunnel_ref);

	kfree(tunnel_state);
}

static void free_group_state(struct drm_dp_tunnel_group_state *group_state)
{
	struct drm_dp_tunnel_state *tunnel_state;
	struct drm_dp_tunnel_state *tunnel_state_tmp;

	for_each_tunnel_state_safe(group_state, tunnel_state, tunnel_state_tmp)
		free_tunnel_state(tunnel_state);

	kfree(group_state);
}

static struct drm_dp_tunnel_state *
get_tunnel_state(struct drm_dp_tunnel_group_state *group_state,
		 const struct drm_dp_tunnel *tunnel)
{
	struct drm_dp_tunnel_state *tunnel_state;

	for_each_tunnel_state(group_state, tunnel_state)
		if (tunnel_state->tunnel_ref.tunnel == tunnel)
			return tunnel_state;

	return NULL;
}

static struct drm_dp_tunnel_state *
get_or_add_tunnel_state(struct drm_dp_tunnel_group_state *group_state,
			struct drm_dp_tunnel *tunnel)
{
	struct drm_dp_tunnel_state *tunnel_state;

	tunnel_state = get_tunnel_state(group_state, tunnel);
	if (tunnel_state)
		return tunnel_state;

	return add_tunnel_state(group_state, tunnel);
}

static struct drm_private_state *
tunnel_group_duplicate_state(struct drm_private_obj *obj)
{
	struct drm_dp_tunnel_group_state *group_state;
	struct drm_dp_tunnel_state *tunnel_state;

	group_state = kzalloc(sizeof(*group_state), GFP_KERNEL);
	if (!group_state)
		return NULL;

	INIT_LIST_HEAD(&group_state->tunnel_states);

	__drm_atomic_helper_private_obj_duplicate_state(obj, &group_state->base);

	for_each_tunnel_state(to_group_state(obj->state), tunnel_state) {
		struct drm_dp_tunnel_state *new_tunnel_state;

		new_tunnel_state = get_or_add_tunnel_state(group_state,
							   tunnel_state->tunnel_ref.tunnel);
		if (!new_tunnel_state)
			goto out_free_state;

		new_tunnel_state->stream_mask = tunnel_state->stream_mask;
		new_tunnel_state->stream_bw = kmemdup(tunnel_state->stream_bw,
						      sizeof(*tunnel_state->stream_bw) *
							hweight32(tunnel_state->stream_mask),
						      GFP_KERNEL);

		if (!new_tunnel_state->stream_bw)
			goto out_free_state;
	}

	return &group_state->base;

out_free_state:
	free_group_state(group_state);

	return NULL;
}

static void tunnel_group_destroy_state(struct drm_private_obj *obj, struct drm_private_state *state)
{
	free_group_state(to_group_state(state));
}

static const struct drm_private_state_funcs tunnel_group_funcs = {
	.atomic_duplicate_state = tunnel_group_duplicate_state,
	.atomic_destroy_state = tunnel_group_destroy_state,
};

/**
 * drm_dp_tunnel_atomic_get_state - get/allocate the new atomic state for a tunnel
 * @state: Atomic state
 * @tunnel: Tunnel to get the state for
 *
 * Get the new atomic state for @tunnel, duplicating it from the old tunnel
 * state if not yet allocated.
 *
 * Return the state or an ERR_PTR() error on failure.
 */
struct drm_dp_tunnel_state *
drm_dp_tunnel_atomic_get_state(struct drm_atomic_state *state,
			       struct drm_dp_tunnel *tunnel)
{
	struct drm_dp_tunnel_group_state *group_state;
	struct drm_dp_tunnel_state *tunnel_state;

	group_state = drm_dp_tunnel_atomic_get_group_state(state, tunnel);
	if (IS_ERR(group_state))
		return ERR_CAST(group_state);

	tunnel_state = get_or_add_tunnel_state(group_state, tunnel);
	if (!tunnel_state)
		return ERR_PTR(-ENOMEM);

	return tunnel_state;
}
EXPORT_SYMBOL(drm_dp_tunnel_atomic_get_state);

/**
 * drm_dp_tunnel_atomic_get_old_state - get the old atomic state for a tunnel
 * @state: Atomic state
 * @tunnel: Tunnel to get the state for
 *
 * Get the old atomic state for @tunnel.
 *
 * Return the old state or NULL if the tunnel's atomic state is not in @state.
 */
struct drm_dp_tunnel_state *
drm_dp_tunnel_atomic_get_old_state(struct drm_atomic_state *state,
				   const struct drm_dp_tunnel *tunnel)
{
	struct drm_dp_tunnel_group_state *old_group_state;
	int i;

	for_each_old_group_in_state(state, old_group_state, i)
		if (to_group(old_group_state->base.obj) == tunnel->group)
			return get_tunnel_state(old_group_state, tunnel);

	return NULL;
}
EXPORT_SYMBOL(drm_dp_tunnel_atomic_get_old_state);

/**
 * drm_dp_tunnel_atomic_get_new_state - get the new atomic state for a tunnel
 * @state: Atomic state
 * @tunnel: Tunnel to get the state for
 *
 * Get the new atomic state for @tunnel.
 *
 * Return the new state or NULL if the tunnel's atomic state is not in @state.
 */
struct drm_dp_tunnel_state *
drm_dp_tunnel_atomic_get_new_state(struct drm_atomic_state *state,
				   const struct drm_dp_tunnel *tunnel)
{
	struct drm_dp_tunnel_group_state *new_group_state;
	int i;

	for_each_new_group_in_state(state, new_group_state, i)
		if (to_group(new_group_state->base.obj) == tunnel->group)
			return get_tunnel_state(new_group_state, tunnel);

	return NULL;
}
EXPORT_SYMBOL(drm_dp_tunnel_atomic_get_new_state);

static bool init_group(struct drm_dp_tunnel_mgr *mgr, struct drm_dp_tunnel_group *group)
{
	struct drm_dp_tunnel_group_state *group_state;

	group_state = kzalloc(sizeof(*group_state), GFP_KERNEL);
	if (!group_state)
		return false;

	INIT_LIST_HEAD(&group_state->tunnel_states);

	group->mgr = mgr;
	group->available_bw = -1;
	INIT_LIST_HEAD(&group->tunnels);

	drm_atomic_private_obj_init(mgr->dev, &group->base, &group_state->base,
				    &tunnel_group_funcs);

	return true;
}

static void cleanup_group(struct drm_dp_tunnel_group *group)
{
	drm_atomic_private_obj_fini(&group->base);
}

#ifdef CONFIG_DRM_DISPLAY_DP_TUNNEL_STATE_DEBUG
static void check_unique_stream_ids(const struct drm_dp_tunnel_group_state *group_state)
{
	const struct drm_dp_tunnel_state *tunnel_state;
	u32 stream_mask = 0;

	for_each_tunnel_state(group_state, tunnel_state) {
		drm_WARN(to_group(group_state->base.obj)->mgr->dev,
			 tunnel_state->stream_mask & stream_mask,
			 "[DPTUN %s]: conflicting stream IDs %x (IDs in other tunnels %x)\n",
			 tunnel_state->tunnel_ref.tunnel->name,
			 tunnel_state->stream_mask,
			 stream_mask);

		stream_mask |= tunnel_state->stream_mask;
	}
}
#else
static void check_unique_stream_ids(const struct drm_dp_tunnel_group_state *group_state)
{
}
#endif

static int stream_id_to_idx(u32 stream_mask, u8 stream_id)
{
	return hweight32(stream_mask & (BIT(stream_id) - 1));
}

static int resize_bw_array(struct drm_dp_tunnel_state *tunnel_state,
			   unsigned long old_mask, unsigned long new_mask)
{
	unsigned long move_mask = old_mask & new_mask;
	int *new_bws = NULL;
	int id;

	WARN_ON(!new_mask);

	if (old_mask == new_mask)
		return 0;

	new_bws = kcalloc(hweight32(new_mask), sizeof(*new_bws), GFP_KERNEL);
	if (!new_bws)
		return -ENOMEM;

	for_each_set_bit(id, &move_mask, BITS_PER_TYPE(move_mask))
		new_bws[stream_id_to_idx(new_mask, id)] =
			tunnel_state->stream_bw[stream_id_to_idx(old_mask, id)];

	kfree(tunnel_state->stream_bw);
	tunnel_state->stream_bw = new_bws;
	tunnel_state->stream_mask = new_mask;

	return 0;
}

static int set_stream_bw(struct drm_dp_tunnel_state *tunnel_state,
			 u8 stream_id, int bw)
{
	int err;

	err = resize_bw_array(tunnel_state,
			      tunnel_state->stream_mask,
			      tunnel_state->stream_mask | BIT(stream_id));
	if (err)
		return err;

	tunnel_state->stream_bw[stream_id_to_idx(tunnel_state->stream_mask, stream_id)] = bw;

	return 0;
}

static int clear_stream_bw(struct drm_dp_tunnel_state *tunnel_state,
			   u8 stream_id)
{
	if (!(tunnel_state->stream_mask & ~BIT(stream_id))) {
		free_tunnel_state(tunnel_state);
		return 0;
	}

	return resize_bw_array(tunnel_state,
			       tunnel_state->stream_mask,
			       tunnel_state->stream_mask & ~BIT(stream_id));
}

/**
 * drm_dp_tunnel_atomic_set_stream_bw - Set the BW for a DP tunnel stream
 * @state: Atomic state
 * @tunnel: DP tunnel containing the stream
 * @stream_id: Stream ID
 * @bw: BW of the stream
 *
 * Set a DP tunnel stream's required BW in the atomic state.
 *
 * Returns 0 in case of success, a negative error code otherwise.
 */
int drm_dp_tunnel_atomic_set_stream_bw(struct drm_atomic_state *state,
				       struct drm_dp_tunnel *tunnel,
				       u8 stream_id, int bw)
{
	struct drm_dp_tunnel_group_state *new_group_state;
	struct drm_dp_tunnel_state *tunnel_state;
	int err;

	if (drm_WARN_ON(tunnel->group->mgr->dev,
			stream_id > BITS_PER_TYPE(tunnel_state->stream_mask)))
		return -EINVAL;

	tun_dbg(tunnel,
		"Setting %d Mb/s for stream %d\n",
		DPTUN_BW_ARG(bw), stream_id);

	new_group_state = drm_dp_tunnel_atomic_get_group_state(state, tunnel);
	if (IS_ERR(new_group_state))
		return PTR_ERR(new_group_state);

	if (bw == 0) {
		tunnel_state = get_tunnel_state(new_group_state, tunnel);
		if (!tunnel_state)
			return 0;

		return clear_stream_bw(tunnel_state, stream_id);
	}

	tunnel_state = get_or_add_tunnel_state(new_group_state, tunnel);
	if (drm_WARN_ON(state->dev, !tunnel_state))
		return -EINVAL;

	err = set_stream_bw(tunnel_state, stream_id, bw);
	if (err)
		return err;

	check_unique_stream_ids(new_group_state);

	return 0;
}
EXPORT_SYMBOL(drm_dp_tunnel_atomic_set_stream_bw);

/**
 * drm_dp_tunnel_atomic_get_required_bw - Get the BW required by a DP tunnel
 * @tunnel_state: Atomic state of the queried tunnel
 *
 * Calculate the BW required by a tunnel adding up the required BW of all
 * the streams in the tunnel.
 *
 * Return the total BW required by the tunnel.
 */
int drm_dp_tunnel_atomic_get_required_bw(const struct drm_dp_tunnel_state *tunnel_state)
{
	int tunnel_bw = 0;
	int i;

	if (!tunnel_state || !tunnel_state->stream_mask)
		return 0;

	for (i = 0; i < hweight32(tunnel_state->stream_mask); i++)
		tunnel_bw += tunnel_state->stream_bw[i];

	return tunnel_bw;
}
EXPORT_SYMBOL(drm_dp_tunnel_atomic_get_required_bw);

/**
 * drm_dp_tunnel_atomic_get_group_streams_in_state - Get mask of stream IDs in a group
 * @state: Atomic state
 * @tunnel: Tunnel object
 * @stream_mask: Mask of streams in @tunnel's group
 *
 * Get the mask of all the stream IDs in the tunnel group of @tunnel.
 *
 * Return 0 in case of success - with the stream IDs in @stream_mask - or a
 * negative error code in case of failure.
 */
int drm_dp_tunnel_atomic_get_group_streams_in_state(struct drm_atomic_state *state,
						    const struct drm_dp_tunnel *tunnel,
						    u32 *stream_mask)
{
	struct drm_dp_tunnel_group_state *group_state;
	struct drm_dp_tunnel_state *tunnel_state;

	group_state = drm_dp_tunnel_atomic_get_group_state(state, tunnel);
	if (IS_ERR(group_state))
		return PTR_ERR(group_state);

	*stream_mask = 0;
	for_each_tunnel_state(group_state, tunnel_state)
		*stream_mask |= tunnel_state->stream_mask;

	return 0;
}
EXPORT_SYMBOL(drm_dp_tunnel_atomic_get_group_streams_in_state);

static int
drm_dp_tunnel_atomic_check_group_bw(struct drm_dp_tunnel_group_state *new_group_state,
				    u32 *failed_stream_mask)
{
	struct drm_dp_tunnel_group *group = to_group(new_group_state->base.obj);
	struct drm_dp_tunnel_state *new_tunnel_state;
	u32 group_stream_mask = 0;
	int group_bw = 0;

	for_each_tunnel_state(new_group_state, new_tunnel_state) {
		struct drm_dp_tunnel *tunnel = new_tunnel_state->tunnel_ref.tunnel;
		int max_dprx_bw = get_max_dprx_bw(tunnel);
		int tunnel_bw = drm_dp_tunnel_atomic_get_required_bw(new_tunnel_state);

		tun_dbg(tunnel,
			"%sRequired %d/%d Mb/s total for tunnel.\n",
			tunnel_bw > max_dprx_bw ? "Not enough BW: " : "",
			DPTUN_BW_ARG(tunnel_bw),
			DPTUN_BW_ARG(max_dprx_bw));

		if (tunnel_bw > max_dprx_bw) {
			*failed_stream_mask = new_tunnel_state->stream_mask;
			return -ENOSPC;
		}

		group_bw += min(roundup(tunnel_bw, tunnel->bw_granularity),
				max_dprx_bw);
		group_stream_mask |= new_tunnel_state->stream_mask;
	}

	tun_grp_dbg(group,
		    "%sRequired %d/%d Mb/s total for tunnel group.\n",
		    group_bw > group->available_bw ? "Not enough BW: " : "",
		    DPTUN_BW_ARG(group_bw),
		    DPTUN_BW_ARG(group->available_bw));

	if (group_bw > group->available_bw) {
		*failed_stream_mask = group_stream_mask;
		return -ENOSPC;
	}

	return 0;
}

/**
 * drm_dp_tunnel_atomic_check_stream_bws - Check BW limit for all streams in state
 * @state: Atomic state
 * @failed_stream_mask: Mask of stream IDs with a BW limit failure
 *
 * Check the required BW of each DP tunnel in @state against both the DPRX BW
 * limit of the tunnel and the BW limit of the tunnel group. Return a mask of
 * stream IDs in @failed_stream_mask once a check fails. The mask will contain
 * either all the streams in a tunnel (in case a DPRX BW limit check failed) or
 * all the streams in a tunnel group (in case a group BW limit check failed).
 *
 * Return 0 if all the BW limit checks passed, -ENOSPC in case a BW limit
 * check failed - with @failed_stream_mask containing the streams failing the
 * check - or a negative error code otherwise.
 */
int drm_dp_tunnel_atomic_check_stream_bws(struct drm_atomic_state *state,
					  u32 *failed_stream_mask)
{
	struct drm_dp_tunnel_group_state *new_group_state;
	int i;

	for_each_new_group_in_state(state, new_group_state, i) {
		int ret;

		ret = drm_dp_tunnel_atomic_check_group_bw(new_group_state,
							  failed_stream_mask);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL(drm_dp_tunnel_atomic_check_stream_bws);

static void destroy_mgr(struct drm_dp_tunnel_mgr *mgr)
{
	int i;

	for (i = 0; i < mgr->group_count; i++) {
		cleanup_group(&mgr->groups[i]);
		drm_WARN_ON(mgr->dev, !list_empty(&mgr->groups[i].tunnels));
	}

#ifdef CONFIG_DRM_DISPLAY_DP_TUNNEL_STATE_DEBUG
	ref_tracker_dir_exit(&mgr->ref_tracker);
#endif

	kfree(mgr->groups);
	kfree(mgr);
}

/**
 * drm_dp_tunnel_mgr_create - Create a DP tunnel manager
 * @dev: DRM device object
 * @max_group_count: Maximum number of tunnel groups
 *
 * Creates a DP tunnel manager for @dev.
 *
 * Returns a pointer to the tunnel manager if created successfully or error
 * pointer in case of failure.
 */
struct drm_dp_tunnel_mgr *
drm_dp_tunnel_mgr_create(struct drm_device *dev, int max_group_count)
{
	struct drm_dp_tunnel_mgr *mgr;
	int i;

	mgr = kzalloc(sizeof(*mgr), GFP_KERNEL);
	if (!mgr)
		return ERR_PTR(-ENOMEM);

	mgr->dev = dev;
	init_waitqueue_head(&mgr->bw_req_queue);

	mgr->groups = kcalloc(max_group_count, sizeof(*mgr->groups), GFP_KERNEL);
	if (!mgr->groups) {
		kfree(mgr);

		return ERR_PTR(-ENOMEM);
	}

#ifdef CONFIG_DRM_DISPLAY_DP_TUNNEL_STATE_DEBUG
	ref_tracker_dir_init(&mgr->ref_tracker, 16, "drm_dptun");
#endif

	for (i = 0; i < max_group_count; i++) {
		if (!init_group(mgr, &mgr->groups[i])) {
			destroy_mgr(mgr);

			return ERR_PTR(-ENOMEM);
		}

		mgr->group_count++;
	}

	return mgr;
}
EXPORT_SYMBOL(drm_dp_tunnel_mgr_create);

/**
 * drm_dp_tunnel_mgr_destroy - Destroy DP tunnel manager
 * @mgr: Tunnel manager object
 *
 * Destroy the tunnel manager.
 */
void drm_dp_tunnel_mgr_destroy(struct drm_dp_tunnel_mgr *mgr)
{
	destroy_mgr(mgr);
}
EXPORT_SYMBOL(drm_dp_tunnel_mgr_destroy);
