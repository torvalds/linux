/*
 * UWB reservation management.
 *
 * Copyright (C) 2008 Cambridge Silicon Radio Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/uwb.h>

#include "uwb-internal.h"

static void uwb_rsv_timer(unsigned long arg);

static const char *rsv_states[] = {
	[UWB_RSV_STATE_NONE]          = "none",
	[UWB_RSV_STATE_O_INITIATED]   = "initiated",
	[UWB_RSV_STATE_O_PENDING]     = "pending",
	[UWB_RSV_STATE_O_MODIFIED]    = "modified",
	[UWB_RSV_STATE_O_ESTABLISHED] = "established",
	[UWB_RSV_STATE_T_ACCEPTED]    = "accepted",
	[UWB_RSV_STATE_T_DENIED]      = "denied",
	[UWB_RSV_STATE_T_PENDING]     = "pending",
};

static const char *rsv_types[] = {
	[UWB_DRP_TYPE_ALIEN_BP] = "alien-bp",
	[UWB_DRP_TYPE_HARD]     = "hard",
	[UWB_DRP_TYPE_SOFT]     = "soft",
	[UWB_DRP_TYPE_PRIVATE]  = "private",
	[UWB_DRP_TYPE_PCA]      = "pca",
};

/**
 * uwb_rsv_state_str - return a string for a reservation state
 * @state: the reservation state.
 */
const char *uwb_rsv_state_str(enum uwb_rsv_state state)
{
	if (state < UWB_RSV_STATE_NONE || state >= UWB_RSV_STATE_LAST)
		return "unknown";
	return rsv_states[state];
}
EXPORT_SYMBOL_GPL(uwb_rsv_state_str);

/**
 * uwb_rsv_type_str - return a string for a reservation type
 * @type: the reservation type
 */
const char *uwb_rsv_type_str(enum uwb_drp_type type)
{
	if (type < UWB_DRP_TYPE_ALIEN_BP || type > UWB_DRP_TYPE_PCA)
		return "invalid";
	return rsv_types[type];
}
EXPORT_SYMBOL_GPL(uwb_rsv_type_str);

static void uwb_rsv_dump(struct uwb_rsv *rsv)
{
	struct device *dev = &rsv->rc->uwb_dev.dev;
	struct uwb_dev_addr devaddr;
	char owner[UWB_ADDR_STRSIZE], target[UWB_ADDR_STRSIZE];

	uwb_dev_addr_print(owner, sizeof(owner), &rsv->owner->dev_addr);
	if (rsv->target.type == UWB_RSV_TARGET_DEV)
		devaddr = rsv->target.dev->dev_addr;
	else
		devaddr = rsv->target.devaddr;
	uwb_dev_addr_print(target, sizeof(target), &devaddr);

	dev_dbg(dev, "rsv %s -> %s: %s\n", owner, target, uwb_rsv_state_str(rsv->state));
}

/*
 * Get a free stream index for a reservation.
 *
 * If the target is a DevAddr (e.g., a WUSB cluster reservation) then
 * the stream is allocated from a pool of per-RC stream indexes,
 * otherwise a unique stream index for the target is selected.
 */
static int uwb_rsv_get_stream(struct uwb_rsv *rsv)
{
	struct uwb_rc *rc = rsv->rc;
	unsigned long *streams_bm;
	int stream;

	switch (rsv->target.type) {
	case UWB_RSV_TARGET_DEV:
		streams_bm = rsv->target.dev->streams;
		break;
	case UWB_RSV_TARGET_DEVADDR:
		streams_bm = rc->uwb_dev.streams;
		break;
	default:
		return -EINVAL;
	}

	stream = find_first_zero_bit(streams_bm, UWB_NUM_STREAMS);
	if (stream >= UWB_NUM_STREAMS)
		return -EBUSY;

	rsv->stream = stream;
	set_bit(stream, streams_bm);

	return 0;
}

static void uwb_rsv_put_stream(struct uwb_rsv *rsv)
{
	struct uwb_rc *rc = rsv->rc;
	unsigned long *streams_bm;

	switch (rsv->target.type) {
	case UWB_RSV_TARGET_DEV:
		streams_bm = rsv->target.dev->streams;
		break;
	case UWB_RSV_TARGET_DEVADDR:
		streams_bm = rc->uwb_dev.streams;
		break;
	default:
		return;
	}

	clear_bit(rsv->stream, streams_bm);
}

/*
 * Generate a MAS allocation with a single row component.
 */
static void uwb_rsv_gen_alloc_row(struct uwb_mas_bm *mas,
				  int first_mas, int mas_per_zone,
				  int zs, int ze)
{
	struct uwb_mas_bm col;
	int z;

	bitmap_zero(mas->bm, UWB_NUM_MAS);
	bitmap_zero(col.bm, UWB_NUM_MAS);
	bitmap_fill(col.bm, mas_per_zone);
	bitmap_shift_left(col.bm, col.bm, first_mas + zs * UWB_MAS_PER_ZONE, UWB_NUM_MAS);

	for (z = zs; z <= ze; z++) {
		bitmap_or(mas->bm, mas->bm, col.bm, UWB_NUM_MAS);
		bitmap_shift_left(col.bm, col.bm, UWB_MAS_PER_ZONE, UWB_NUM_MAS);
	}
}

/*
 * Allocate some MAS for this reservation based on current local
 * availability, the reservation parameters (max_mas, min_mas,
 * sparsity), and the WiMedia rules for MAS allocations.
 *
 * Returns -EBUSY is insufficient free MAS are available.
 *
 * FIXME: to simplify this, only safe reservations with a single row
 * component in zones 1 to 15 are tried (zone 0 is skipped to avoid
 * problems with the MAS reserved for the BP).
 *
 * [ECMA-368] section B.2.
 */
static int uwb_rsv_alloc_mas(struct uwb_rsv *rsv)
{
	static const int safe_mas_in_row[UWB_NUM_ZONES] = {
		8, 7, 6, 5, 4, 4, 4, 4, 4, 4, 4, 4, 4, 3, 2, 1,
	};
	int n, r;
	struct uwb_mas_bm mas;
	bool found = false;

	/*
	 * Search all valid safe allocations until either: too few MAS
	 * are available; or the smallest allocation with sufficient
	 * MAS is found.
	 *
	 * The top of the zones are preferred, so space for larger
	 * allocations is available in the bottom of the zone (e.g., a
	 * 15 MAS allocation should start in row 14 leaving space for
	 * a 120 MAS allocation at row 0).
	 */
	for (n = safe_mas_in_row[0]; n >= 1; n--) {
		int num_mas;

		num_mas = n * (UWB_NUM_ZONES - 1);
		if (num_mas < rsv->min_mas)
			break;
		if (found && num_mas < rsv->max_mas)
			break;

		for (r = UWB_MAS_PER_ZONE-1;  r >= 0; r--) {
			if (safe_mas_in_row[r] < n)
				continue;
			uwb_rsv_gen_alloc_row(&mas, r, n, 1, UWB_NUM_ZONES);
			if (uwb_drp_avail_reserve_pending(rsv->rc, &mas) == 0) {
				found = true;
				break;
			}
		}
	}

	if (!found)
		return -EBUSY;

	bitmap_copy(rsv->mas.bm, mas.bm, UWB_NUM_MAS);
	return 0;
}

static void uwb_rsv_stroke_timer(struct uwb_rsv *rsv)
{
	int sframes = UWB_MAX_LOST_BEACONS;

	/*
	 * Multicast reservations can become established within 1
	 * super frame and should not be terminated if no response is
	 * received.
	 */
	if (rsv->is_multicast) {
		if (rsv->state == UWB_RSV_STATE_O_INITIATED)
			sframes = 1;
		if (rsv->state == UWB_RSV_STATE_O_ESTABLISHED)
			sframes = 0;
	}

	rsv->expired = false;
	if (sframes > 0) {
		/*
		 * Add an additional 2 superframes to account for the
		 * time to send the SET DRP IE command.
		 */
		unsigned timeout_us = (sframes + 2) * UWB_SUPERFRAME_LENGTH_US;
		mod_timer(&rsv->timer, jiffies + usecs_to_jiffies(timeout_us));
	} else
		del_timer(&rsv->timer);
}

/*
 * Update a reservations state, and schedule an update of the
 * transmitted DRP IEs.
 */
static void uwb_rsv_state_update(struct uwb_rsv *rsv,
				 enum uwb_rsv_state new_state)
{
	rsv->state = new_state;
	rsv->ie_valid = false;

	uwb_rsv_dump(rsv);

	uwb_rsv_stroke_timer(rsv);
	uwb_rsv_sched_update(rsv->rc);
}

static void uwb_rsv_callback(struct uwb_rsv *rsv)
{
	if (rsv->callback)
		rsv->callback(rsv);
}

void uwb_rsv_set_state(struct uwb_rsv *rsv, enum uwb_rsv_state new_state)
{
	if (rsv->state == new_state) {
		switch (rsv->state) {
		case UWB_RSV_STATE_O_ESTABLISHED:
		case UWB_RSV_STATE_T_ACCEPTED:
		case UWB_RSV_STATE_NONE:
			uwb_rsv_stroke_timer(rsv);
			break;
		default:
			/* Expecting a state transition so leave timer
			   as-is. */
			break;
		}
		return;
	}

	switch (new_state) {
	case UWB_RSV_STATE_NONE:
		uwb_drp_avail_release(rsv->rc, &rsv->mas);
		uwb_rsv_put_stream(rsv);
		uwb_rsv_state_update(rsv, UWB_RSV_STATE_NONE);
		uwb_rsv_callback(rsv);
		break;
	case UWB_RSV_STATE_O_INITIATED:
		uwb_rsv_state_update(rsv, UWB_RSV_STATE_O_INITIATED);
		break;
	case UWB_RSV_STATE_O_PENDING:
		uwb_rsv_state_update(rsv, UWB_RSV_STATE_O_PENDING);
		break;
	case UWB_RSV_STATE_O_ESTABLISHED:
		uwb_drp_avail_reserve(rsv->rc, &rsv->mas);
		uwb_rsv_state_update(rsv, UWB_RSV_STATE_O_ESTABLISHED);
		uwb_rsv_callback(rsv);
		break;
	case UWB_RSV_STATE_T_ACCEPTED:
		uwb_drp_avail_reserve(rsv->rc, &rsv->mas);
		uwb_rsv_state_update(rsv, UWB_RSV_STATE_T_ACCEPTED);
		uwb_rsv_callback(rsv);
		break;
	case UWB_RSV_STATE_T_DENIED:
		uwb_rsv_state_update(rsv, UWB_RSV_STATE_T_DENIED);
		break;
	default:
		dev_err(&rsv->rc->uwb_dev.dev, "unhandled state: %s (%d)\n",
			uwb_rsv_state_str(new_state), new_state);
	}
}

static struct uwb_rsv *uwb_rsv_alloc(struct uwb_rc *rc)
{
	struct uwb_rsv *rsv;

	rsv = kzalloc(sizeof(struct uwb_rsv), GFP_KERNEL);
	if (!rsv)
		return NULL;

	INIT_LIST_HEAD(&rsv->rc_node);
	INIT_LIST_HEAD(&rsv->pal_node);
	init_timer(&rsv->timer);
	rsv->timer.function = uwb_rsv_timer;
	rsv->timer.data     = (unsigned long)rsv;

	rsv->rc = rc;

	return rsv;
}

static void uwb_rsv_free(struct uwb_rsv *rsv)
{
	uwb_dev_put(rsv->owner);
	if (rsv->target.type == UWB_RSV_TARGET_DEV)
		uwb_dev_put(rsv->target.dev);
	kfree(rsv);
}

/**
 * uwb_rsv_create - allocate and initialize a UWB reservation structure
 * @rc: the radio controller
 * @cb: callback to use when the reservation completes or terminates
 * @pal_priv: data private to the PAL to be passed in the callback
 *
 * The callback is called when the state of the reservation changes from:
 *
 *   - pending to accepted
 *   - pending to denined
 *   - accepted to terminated
 *   - pending to terminated
 */
struct uwb_rsv *uwb_rsv_create(struct uwb_rc *rc, uwb_rsv_cb_f cb, void *pal_priv)
{
	struct uwb_rsv *rsv;

	rsv = uwb_rsv_alloc(rc);
	if (!rsv)
		return NULL;

	rsv->callback = cb;
	rsv->pal_priv = pal_priv;

	return rsv;
}
EXPORT_SYMBOL_GPL(uwb_rsv_create);

void uwb_rsv_remove(struct uwb_rsv *rsv)
{
	if (rsv->state != UWB_RSV_STATE_NONE)
		uwb_rsv_set_state(rsv, UWB_RSV_STATE_NONE);
	del_timer_sync(&rsv->timer);
	list_del(&rsv->rc_node);
	uwb_rsv_free(rsv);
}

/**
 * uwb_rsv_destroy - free a UWB reservation structure
 * @rsv: the reservation to free
 *
 * The reservation will be terminated if it is pending or established.
 */
void uwb_rsv_destroy(struct uwb_rsv *rsv)
{
	struct uwb_rc *rc = rsv->rc;

	mutex_lock(&rc->rsvs_mutex);
	uwb_rsv_remove(rsv);
	mutex_unlock(&rc->rsvs_mutex);
}
EXPORT_SYMBOL_GPL(uwb_rsv_destroy);

/**
 * usb_rsv_establish - start a reservation establishment
 * @rsv: the reservation
 *
 * The PAL should fill in @rsv's owner, target, type, max_mas,
 * min_mas, sparsity and is_multicast fields.  If the target is a
 * uwb_dev it must be referenced.
 *
 * The reservation's callback will be called when the reservation is
 * accepted, denied or times out.
 */
int uwb_rsv_establish(struct uwb_rsv *rsv)
{
	struct uwb_rc *rc = rsv->rc;
	int ret;

	mutex_lock(&rc->rsvs_mutex);

	ret = uwb_rsv_get_stream(rsv);
	if (ret)
		goto out;

	ret = uwb_rsv_alloc_mas(rsv);
	if (ret) {
		uwb_rsv_put_stream(rsv);
		goto out;
	}

	list_add_tail(&rsv->rc_node, &rc->reservations);
	rsv->owner = &rc->uwb_dev;
	uwb_dev_get(rsv->owner);
	uwb_rsv_set_state(rsv, UWB_RSV_STATE_O_INITIATED);
out:
	mutex_unlock(&rc->rsvs_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(uwb_rsv_establish);

/**
 * uwb_rsv_modify - modify an already established reservation
 * @rsv: the reservation to modify
 * @max_mas: new maximum MAS to reserve
 * @min_mas: new minimum MAS to reserve
 * @sparsity: new sparsity to use
 *
 * FIXME: implement this once there are PALs that use it.
 */
int uwb_rsv_modify(struct uwb_rsv *rsv, int max_mas, int min_mas, int sparsity)
{
	return -ENOSYS;
}
EXPORT_SYMBOL_GPL(uwb_rsv_modify);

/**
 * uwb_rsv_terminate - terminate an established reservation
 * @rsv: the reservation to terminate
 *
 * A reservation is terminated by removing the DRP IE from the beacon,
 * the other end will consider the reservation to be terminated when
 * it does not see the DRP IE for at least mMaxLostBeacons.
 *
 * If applicable, the reference to the target uwb_dev will be released.
 */
void uwb_rsv_terminate(struct uwb_rsv *rsv)
{
	struct uwb_rc *rc = rsv->rc;

	mutex_lock(&rc->rsvs_mutex);

	uwb_rsv_set_state(rsv, UWB_RSV_STATE_NONE);

	mutex_unlock(&rc->rsvs_mutex);
}
EXPORT_SYMBOL_GPL(uwb_rsv_terminate);

/**
 * uwb_rsv_accept - accept a new reservation from a peer
 * @rsv:      the reservation
 * @cb:       call back for reservation changes
 * @pal_priv: data to be passed in the above call back
 *
 * Reservation requests from peers are denied unless a PAL accepts it
 * by calling this function.
 */
void uwb_rsv_accept(struct uwb_rsv *rsv, uwb_rsv_cb_f cb, void *pal_priv)
{
	rsv->callback = cb;
	rsv->pal_priv = pal_priv;
	rsv->state    = UWB_RSV_STATE_T_ACCEPTED;
}
EXPORT_SYMBOL_GPL(uwb_rsv_accept);

/*
 * Is a received DRP IE for this reservation?
 */
static bool uwb_rsv_match(struct uwb_rsv *rsv, struct uwb_dev *src,
			  struct uwb_ie_drp *drp_ie)
{
	struct uwb_dev_addr *rsv_src;
	int stream;

	stream = uwb_ie_drp_stream_index(drp_ie);

	if (rsv->stream != stream)
		return false;

	switch (rsv->target.type) {
	case UWB_RSV_TARGET_DEVADDR:
		return rsv->stream == stream;
	case UWB_RSV_TARGET_DEV:
		if (uwb_ie_drp_owner(drp_ie))
			rsv_src = &rsv->owner->dev_addr;
		else
			rsv_src = &rsv->target.dev->dev_addr;
		return uwb_dev_addr_cmp(&src->dev_addr, rsv_src) == 0;
	}
	return false;
}

static struct uwb_rsv *uwb_rsv_new_target(struct uwb_rc *rc,
					  struct uwb_dev *src,
					  struct uwb_ie_drp *drp_ie)
{
	struct uwb_rsv *rsv;
	struct uwb_pal *pal;
	enum uwb_rsv_state state;

	rsv = uwb_rsv_alloc(rc);
	if (!rsv)
		return NULL;

	rsv->rc          = rc;
	rsv->owner       = src;
	uwb_dev_get(rsv->owner);
	rsv->target.type = UWB_RSV_TARGET_DEV;
	rsv->target.dev  = &rc->uwb_dev;
	rsv->type        = uwb_ie_drp_type(drp_ie);
	rsv->stream      = uwb_ie_drp_stream_index(drp_ie);
	set_bit(rsv->stream, rsv->owner->streams);
	uwb_drp_ie_to_bm(&rsv->mas, drp_ie);

	/*
	 * See if any PALs are interested in this reservation. If not,
	 * deny the request.
	 */
	rsv->state = UWB_RSV_STATE_T_DENIED;
	spin_lock(&rc->pal_lock);
	list_for_each_entry(pal, &rc->pals, node) {
		if (pal->new_rsv)
			pal->new_rsv(rsv);
		if (rsv->state == UWB_RSV_STATE_T_ACCEPTED)
			break;
	}
	spin_unlock(&rc->pal_lock);

	list_add_tail(&rsv->rc_node, &rc->reservations);
	state = rsv->state;
	rsv->state = UWB_RSV_STATE_NONE;
	uwb_rsv_set_state(rsv, state);

	return rsv;
}

/**
 * uwb_rsv_find - find a reservation for a received DRP IE.
 * @rc: the radio controller
 * @src: source of the DRP IE
 * @drp_ie: the DRP IE
 *
 * If the reservation cannot be found and the DRP IE is from a peer
 * attempting to establish a new reservation, create a new reservation
 * and add it to the list.
 */
struct uwb_rsv *uwb_rsv_find(struct uwb_rc *rc, struct uwb_dev *src,
			     struct uwb_ie_drp *drp_ie)
{
	struct uwb_rsv *rsv;

	list_for_each_entry(rsv, &rc->reservations, rc_node) {
		if (uwb_rsv_match(rsv, src, drp_ie))
			return rsv;
	}

	if (uwb_ie_drp_owner(drp_ie))
		return uwb_rsv_new_target(rc, src, drp_ie);

	return NULL;
}

/*
 * Go through all the reservations and check for timeouts and (if
 * necessary) update their DRP IEs.
 *
 * FIXME: look at building the SET_DRP_IE command here rather than
 * having to rescan the list in uwb_rc_send_all_drp_ie().
 */
static bool uwb_rsv_update_all(struct uwb_rc *rc)
{
	struct uwb_rsv *rsv, *t;
	bool ie_updated = false;

	list_for_each_entry_safe(rsv, t, &rc->reservations, rc_node) {
		if (rsv->expired)
			uwb_drp_handle_timeout(rsv);
		if (!rsv->ie_valid) {
			uwb_drp_ie_update(rsv);
			ie_updated = true;
		}
	}

	return ie_updated;
}

void uwb_rsv_sched_update(struct uwb_rc *rc)
{
	queue_work(rc->rsv_workq, &rc->rsv_update_work);
}

/*
 * Update DRP IEs and, if necessary, the DRP Availability IE and send
 * the updated IEs to the radio controller.
 */
static void uwb_rsv_update_work(struct work_struct *work)
{
	struct uwb_rc *rc = container_of(work, struct uwb_rc, rsv_update_work);
	bool ie_updated;

	mutex_lock(&rc->rsvs_mutex);

	ie_updated = uwb_rsv_update_all(rc);

	if (!rc->drp_avail.ie_valid) {
		uwb_drp_avail_ie_update(rc);
		ie_updated = true;
	}

	if (ie_updated)
		uwb_rc_send_all_drp_ie(rc);

	mutex_unlock(&rc->rsvs_mutex);
}

static void uwb_rsv_timer(unsigned long arg)
{
	struct uwb_rsv *rsv = (struct uwb_rsv *)arg;

	rsv->expired = true;
	uwb_rsv_sched_update(rsv->rc);
}

void uwb_rsv_init(struct uwb_rc *rc)
{
	INIT_LIST_HEAD(&rc->reservations);
	mutex_init(&rc->rsvs_mutex);
	INIT_WORK(&rc->rsv_update_work, uwb_rsv_update_work);

	bitmap_complement(rc->uwb_dev.streams, rc->uwb_dev.streams, UWB_NUM_STREAMS);
}

int uwb_rsv_setup(struct uwb_rc *rc)
{
	char name[16];

	snprintf(name, sizeof(name), "%s_rsvd", dev_name(&rc->uwb_dev.dev));
	rc->rsv_workq = create_singlethread_workqueue(name);
	if (rc->rsv_workq == NULL)
		return -ENOMEM;

	return 0;
}

void uwb_rsv_cleanup(struct uwb_rc *rc)
{
	struct uwb_rsv *rsv, *t;

	mutex_lock(&rc->rsvs_mutex);
	list_for_each_entry_safe(rsv, t, &rc->reservations, rc_node) {
		uwb_rsv_remove(rsv);
	}
	mutex_unlock(&rc->rsvs_mutex);

	cancel_work_sync(&rc->rsv_update_work);
	destroy_workqueue(rc->rsv_workq);
}
