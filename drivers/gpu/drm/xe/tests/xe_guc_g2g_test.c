// SPDX-License-Identifier: GPL-2.0 AND MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include <linux/delay.h>

#include <kunit/test.h>
#include <kunit/visibility.h>

#include "tests/xe_kunit_helpers.h"
#include "tests/xe_pci_test.h"
#include "tests/xe_test.h"

#include "xe_bo.h"
#include "xe_device.h"
#include "xe_pm.h"

/*
 * There are different ways to allocate the G2G buffers. The plan for this test
 * is to make sure that all the possible options work. The particular option
 * chosen by the driver may vary from one platform to another, it may also change
 * with time. So to ensure consistency of testing, the relevant driver code is
 * replicated here to guarantee it won't change without the test being updated
 * to keep testing the other options.
 *
 * In order to test the actual code being used by the driver, there is also the
 * 'default' scheme. That will use the official driver routines to test whatever
 * method the driver is using on the current platform at the current time.
 */
enum {
	/* Driver defined allocation scheme */
	G2G_CTB_TYPE_DEFAULT,
	/* Single buffer in host memory */
	G2G_CTB_TYPE_HOST,
	/* Single buffer in a specific tile, loops across all tiles */
	G2G_CTB_TYPE_TILE,
};

/*
 * Payload is opaque to GuC. So KMD can define any structure or size it wants.
 */
struct g2g_test_payload  {
	u32 tx_dev;
	u32 tx_tile;
	u32 rx_dev;
	u32 rx_tile;
	u32 seqno;
};

static void g2g_test_send(struct kunit *test, struct xe_guc *guc,
			  u32 far_tile, u32 far_dev,
			  struct g2g_test_payload *payload)
{
	struct xe_device *xe = guc_to_xe(guc);
	struct xe_gt *gt = guc_to_gt(guc);
	u32 *action, total;
	size_t payload_len;
	int ret;

	static_assert(IS_ALIGNED(sizeof(*payload), sizeof(u32)));
	payload_len = sizeof(*payload) / sizeof(u32);

	total = 4 + payload_len;
	action = kunit_kmalloc_array(test, total, sizeof(*action), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, action);

	action[0] = XE_GUC_ACTION_TEST_G2G_SEND;
	action[1] = far_tile;
	action[2] = far_dev;
	action[3] = payload_len;
	memcpy(action + 4, payload, payload_len * sizeof(u32));

	atomic_inc(&xe->g2g_test_count);

	/*
	 * Should specify the expected response notification here. Problem is that
	 * the response will be coming from a different GuC. By the end, it should
	 * all add up as long as an equal number of messages are sent from each GuC
	 * and to each GuC. However, in the middle negative reservation space errors
	 * and such like can occur. Rather than add intrusive changes to the CT layer
	 * it is simpler to just not bother counting it at all. The system should be
	 * idle when running the selftest, and the selftest's notification total size
	 * is well within the G2H allocation size. So there should be no issues with
	 * needing to block for space, which is all the tracking code is really for.
	 */
	ret = xe_guc_ct_send(&guc->ct, action, total, 0, 0);
	kunit_kfree(test, action);
	KUNIT_ASSERT_EQ_MSG(test, 0, ret, "G2G send failed: %d [%d:%d -> %d:%d]\n", ret,
			    gt_to_tile(gt)->id, G2G_DEV(gt), far_tile, far_dev);
}

/*
 * NB: Can't use KUNIT_ASSERT and friends in here as this is called asynchronously
 * from the G2H notification handler. Need that to actually complete rather than
 * thread-abort in order to keep the rest of the driver alive!
 */
int xe_guc_g2g_test_notification(struct xe_guc *guc, u32 *msg, u32 len)
{
	struct xe_device *xe = guc_to_xe(guc);
	struct xe_gt *rx_gt = guc_to_gt(guc), *test_gt, *tx_gt = NULL;
	u32 tx_tile, tx_dev, rx_tile, rx_dev, idx, got_len;
	struct g2g_test_payload *payload;
	size_t payload_len;
	int ret = 0, i;

	payload_len = sizeof(*payload) / sizeof(u32);

	if (unlikely(len != (G2H_LEN_DW_G2G_NOTIFY_MIN + payload_len))) {
		xe_gt_err(rx_gt, "G2G test notification invalid length %u", len);
		ret = -EPROTO;
		goto done;
	}

	tx_tile = msg[0];
	tx_dev = msg[1];
	got_len = msg[2];
	payload = (struct g2g_test_payload *)(msg + 3);

	rx_tile = gt_to_tile(rx_gt)->id;
	rx_dev = G2G_DEV(rx_gt);

	if (got_len != payload_len) {
		xe_gt_err(rx_gt, "G2G: Invalid payload length: %u vs %zu\n", got_len, payload_len);
		ret = -EPROTO;
		goto done;
	}

	if (payload->tx_dev != tx_dev || payload->tx_tile != tx_tile ||
	    payload->rx_dev != rx_dev || payload->rx_tile != rx_tile) {
		xe_gt_err(rx_gt, "G2G: Invalid payload: %d:%d -> %d:%d vs %d:%d -> %d:%d! [%d]\n",
			  payload->tx_tile, payload->tx_dev, payload->rx_tile, payload->rx_dev,
			  tx_tile, tx_dev, rx_tile, rx_dev, payload->seqno);
		ret = -EPROTO;
		goto done;
	}

	if (!xe->g2g_test_array) {
		xe_gt_err(rx_gt, "G2G: Missing test array!\n");
		ret = -ENOMEM;
		goto done;
	}

	for_each_gt(test_gt, xe, i) {
		if (gt_to_tile(test_gt)->id != tx_tile)
			continue;

		if (G2G_DEV(test_gt) != tx_dev)
			continue;

		if (tx_gt) {
			xe_gt_err(rx_gt, "G2G: Got duplicate TX GTs: %d vs %d for %d:%d!\n",
				  tx_gt->info.id, test_gt->info.id, tx_tile, tx_dev);
			ret = -EINVAL;
			goto done;
		}

		tx_gt = test_gt;
	}
	if (!tx_gt) {
		xe_gt_err(rx_gt, "G2G: Failed to find a TX GT for %d:%d!\n", tx_tile, tx_dev);
		ret = -EINVAL;
		goto done;
	}

	idx = (tx_gt->info.id * xe->info.gt_count) + rx_gt->info.id;

	if (xe->g2g_test_array[idx] != payload->seqno - 1) {
		xe_gt_err(rx_gt, "G2G: Seqno mismatch %d vs %d for %d:%d -> %d:%d!\n",
			  xe->g2g_test_array[idx], payload->seqno - 1,
			  tx_tile, tx_dev, rx_tile, rx_dev);
		ret = -EINVAL;
		goto done;
	}

	xe->g2g_test_array[idx] = payload->seqno;

done:
	atomic_dec(&xe->g2g_test_count);
	return ret;
}

/*
 * Send the given seqno from all GuCs to all other GuCs in tile/GT order
 */
static void g2g_test_in_order(struct kunit *test, struct xe_device *xe, u32 seqno)
{
	struct xe_gt *near_gt, *far_gt;
	int i, j;

	for_each_gt(near_gt, xe, i) {
		u32 near_tile = gt_to_tile(near_gt)->id;
		u32 near_dev = G2G_DEV(near_gt);

		for_each_gt(far_gt, xe, j) {
			u32 far_tile = gt_to_tile(far_gt)->id;
			u32 far_dev = G2G_DEV(far_gt);
			struct g2g_test_payload payload;

			if (far_gt->info.id == near_gt->info.id)
				continue;

			payload.tx_dev = near_dev;
			payload.tx_tile = near_tile;
			payload.rx_dev = far_dev;
			payload.rx_tile = far_tile;
			payload.seqno = seqno;
			g2g_test_send(test, &near_gt->uc.guc, far_tile, far_dev, &payload);
		}
	}
}

#define WAIT_TIME_MS	100
#define WAIT_COUNT	(1000 / WAIT_TIME_MS)

static void g2g_wait_for_complete(void *_xe)
{
	struct xe_device *xe = (struct xe_device *)_xe;
	struct kunit *test = kunit_get_current_test();
	int wait = 0;

	/* Wait for all G2H messages to be received */
	while (atomic_read(&xe->g2g_test_count)) {
		if (++wait > WAIT_COUNT)
			break;

		msleep(WAIT_TIME_MS);
	}

	KUNIT_ASSERT_EQ_MSG(test, 0, atomic_read(&xe->g2g_test_count),
			    "Timed out waiting for notifications\n");
	kunit_info(test, "Got all notifications back\n");
}

#undef WAIT_TIME_MS
#undef WAIT_COUNT

static void g2g_clean_array(void *_xe)
{
	struct xe_device *xe = (struct xe_device *)_xe;

	xe->g2g_test_array = NULL;
}

#define NUM_LOOPS	16

static void g2g_run_test(struct kunit *test, struct xe_device *xe)
{
	u32 seqno, max_array;
	int ret, i, j;

	max_array = xe->info.gt_count * xe->info.gt_count;
	xe->g2g_test_array = kunit_kcalloc(test, max_array, sizeof(u32), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, xe->g2g_test_array);

	ret = kunit_add_action_or_reset(test, g2g_clean_array, xe);
	KUNIT_ASSERT_EQ_MSG(test, 0, ret, "Failed to register clean up action\n");

	/*
	 * Send incrementing seqnos from all GuCs to all other GuCs in tile/GT order.
	 * Tile/GT order doesn't really mean anything to the hardware but it is going
	 * to be a fixed sequence every time.
	 *
	 * Verify that each one comes back having taken the correct route.
	 */
	ret = kunit_add_action(test, g2g_wait_for_complete, xe);
	KUNIT_ASSERT_EQ_MSG(test, 0, ret, "Failed to register clean up action\n");
	for (seqno = 1; seqno < NUM_LOOPS; seqno++)
		g2g_test_in_order(test, xe, seqno);
	seqno--;

	kunit_release_action(test, &g2g_wait_for_complete, xe);

	/* Check for the final seqno in each slot */
	for (i = 0; i < xe->info.gt_count; i++) {
		for (j = 0; j < xe->info.gt_count; j++) {
			u32 idx = (j * xe->info.gt_count) + i;

			if (i == j)
				KUNIT_ASSERT_EQ_MSG(test, 0, xe->g2g_test_array[idx],
						    "identity seqno modified: %d for %dx%d!\n",
						    xe->g2g_test_array[idx], i, j);
			else
				KUNIT_ASSERT_EQ_MSG(test, seqno, xe->g2g_test_array[idx],
						    "invalid seqno: %d vs %d for %dx%d!\n",
						    xe->g2g_test_array[idx], seqno, i, j);
		}
	}

	kunit_kfree(test, xe->g2g_test_array);
	kunit_release_action(test, &g2g_clean_array, xe);

	kunit_info(test, "Test passed\n");
}

#undef NUM_LOOPS

static void g2g_ct_stop(struct xe_guc *guc)
{
	struct xe_gt *remote_gt, *gt = guc_to_gt(guc);
	struct xe_device *xe = gt_to_xe(gt);
	int i, t;

	for_each_gt(remote_gt, xe, i) {
		u32 tile, dev;

		if (remote_gt->info.id == gt->info.id)
			continue;

		tile = gt_to_tile(remote_gt)->id;
		dev = G2G_DEV(remote_gt);

		for (t = 0; t < XE_G2G_TYPE_LIMIT; t++)
			guc_g2g_deregister(guc, tile, dev, t);
	}
}

/* Size of a single allocation that contains all G2G CTBs across all GTs */
static u32 g2g_ctb_size(struct kunit *test, struct xe_device *xe)
{
	unsigned int count = xe->info.gt_count;
	u32 num_channels = (count * (count - 1)) / 2;

	kunit_info(test, "Size: (%d * %d / 2) * %d * 0x%08X + 0x%08X => 0x%08X [%d]\n",
		   count, count - 1, XE_G2G_TYPE_LIMIT, G2G_BUFFER_SIZE, G2G_DESC_AREA_SIZE,
		   num_channels * XE_G2G_TYPE_LIMIT * G2G_BUFFER_SIZE + G2G_DESC_AREA_SIZE,
		   num_channels * XE_G2G_TYPE_LIMIT);

	return num_channels * XE_G2G_TYPE_LIMIT * G2G_BUFFER_SIZE + G2G_DESC_AREA_SIZE;
}

/*
 * Use the driver's regular CTB allocation scheme.
 */
static void g2g_alloc_default(struct kunit *test, struct xe_device *xe)
{
	struct xe_gt *gt;
	int i;

	kunit_info(test, "Default [tiles = %d, GTs = %d]\n",
		   xe->info.tile_count, xe->info.gt_count);

	for_each_gt(gt, xe, i) {
		struct xe_guc *guc = &gt->uc.guc;
		int ret;

		ret = guc_g2g_alloc(guc);
		KUNIT_ASSERT_EQ_MSG(test, 0, ret, "G2G alloc failed: %pe", ERR_PTR(ret));
		continue;
	}
}

static void g2g_distribute(struct kunit *test, struct xe_device *xe, struct xe_bo *bo)
{
	struct xe_gt *root_gt, *gt;
	int i;

	root_gt = xe_device_get_gt(xe, 0);
	root_gt->uc.guc.g2g.bo = bo;
	root_gt->uc.guc.g2g.owned = true;
	kunit_info(test, "[%d.%d] Assigned 0x%p\n", gt_to_tile(root_gt)->id, root_gt->info.id, bo);

	for_each_gt(gt, xe, i) {
		if (gt->info.id != 0) {
			gt->uc.guc.g2g.owned = false;
			gt->uc.guc.g2g.bo = xe_bo_get(bo);
			kunit_info(test, "[%d.%d] Pinned 0x%p\n",
				   gt_to_tile(gt)->id, gt->info.id, gt->uc.guc.g2g.bo);
		}

		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, gt->uc.guc.g2g.bo);
	}
}

/*
 * Allocate a single blob on the host and split between all G2G CTBs.
 */
static void g2g_alloc_host(struct kunit *test, struct xe_device *xe)
{
	struct xe_bo *bo;
	u32 g2g_size;

	kunit_info(test, "Host [tiles = %d, GTs = %d]\n", xe->info.tile_count, xe->info.gt_count);

	g2g_size = g2g_ctb_size(test, xe);
	bo = xe_managed_bo_create_pin_map(xe, xe_device_get_root_tile(xe), g2g_size,
					  XE_BO_FLAG_SYSTEM |
					  XE_BO_FLAG_GGTT |
					  XE_BO_FLAG_GGTT_ALL |
					  XE_BO_FLAG_GGTT_INVALIDATE);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, bo);
	kunit_info(test, "[HST] G2G buffer create: 0x%p\n", bo);

	xe_map_memset(xe, &bo->vmap, 0, 0, g2g_size);

	g2g_distribute(test, xe, bo);
}

/*
 * Allocate a single blob on the given tile and split between all G2G CTBs.
 */
static void g2g_alloc_tile(struct kunit *test, struct xe_device *xe, struct xe_tile *tile)
{
	struct xe_bo *bo;
	u32 g2g_size;

	KUNIT_ASSERT_TRUE(test, IS_DGFX(xe));
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, tile);

	kunit_info(test, "Tile %d [tiles = %d, GTs = %d]\n",
		   tile->id, xe->info.tile_count, xe->info.gt_count);

	g2g_size = g2g_ctb_size(test, xe);
	bo = xe_managed_bo_create_pin_map(xe, tile, g2g_size,
					  XE_BO_FLAG_VRAM_IF_DGFX(tile) |
					  XE_BO_FLAG_GGTT |
					  XE_BO_FLAG_GGTT_ALL |
					  XE_BO_FLAG_GGTT_INVALIDATE);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, bo);
	kunit_info(test, "[%d.*] G2G buffer create: 0x%p\n", tile->id, bo);

	xe_map_memset(xe, &bo->vmap, 0, 0, g2g_size);

	g2g_distribute(test, xe, bo);
}

static void g2g_free(struct kunit *test, struct xe_device *xe)
{
	struct xe_gt *gt;
	struct xe_bo *bo;
	int i;

	for_each_gt(gt, xe, i) {
		bo = gt->uc.guc.g2g.bo;
		if (!bo)
			continue;

		if (gt->uc.guc.g2g.owned) {
			xe_managed_bo_unpin_map_no_vm(bo);
			kunit_info(test, "[%d.%d] Unmapped 0x%p\n",
				   gt_to_tile(gt)->id, gt->info.id, bo);
		} else {
			xe_bo_put(bo);
			kunit_info(test, "[%d.%d] Unpinned 0x%p\n",
				   gt_to_tile(gt)->id, gt->info.id, bo);
		}

		gt->uc.guc.g2g.bo = NULL;
	}
}

static void g2g_stop(struct kunit *test, struct xe_device *xe)
{
	struct xe_gt *gt;
	int i;

	for_each_gt(gt, xe, i) {
		struct xe_guc *guc = &gt->uc.guc;

		if (!guc->g2g.bo)
			continue;

		g2g_ct_stop(guc);
	}

	g2g_free(test, xe);
}

/*
 * Generate a unique id for each bi-directional CTB for each pair of
 * near and far tiles/devices. The id can then be used as an index into
 * a single allocation that is sub-divided into multiple CTBs.
 *
 * For example, with two devices per tile and two tiles, the table should
 * look like:
 *           Far <tile>.<dev>
 *         0.0   0.1   1.0   1.1
 * N 0.0  --/-- 00/01 02/03 04/05
 * e 0.1  01/00 --/-- 06/07 08/09
 * a 1.0  03/02 07/06 --/-- 10/11
 * r 1.1  05/04 09/08 11/10 --/--
 *
 * Where each entry is Rx/Tx channel id.
 *
 * So GuC #3 (tile 1, dev 1) talking to GuC #2 (tile 1, dev 0) would
 * be reading from channel #11 and writing to channel #10. Whereas,
 * GuC #2 talking to GuC #3 would be read on #10 and write to #11.
 */
static int g2g_slot_flat(u32 near_tile, u32 near_dev, u32 far_tile, u32 far_dev,
			 u32 type, u32 max_inst, bool have_dev)
{
	u32 near = near_tile, far = far_tile;
	u32 idx = 0, x, y, direction;
	int i;

	if (have_dev) {
		near = (near << 1) | near_dev;
		far = (far << 1) | far_dev;
	}

	/* No need to send to one's self */
	if (far == near)
		return -1;

	if (far > near) {
		/* Top right table half */
		x = far;
		y = near;

		/* T/R is 'forwards' direction */
		direction = type;
	} else {
		/* Bottom left table half */
		x = near;
		y = far;

		/* B/L is 'backwards' direction */
		direction = (1 - type);
	}

	/* Count the rows prior to the target */
	for (i = y; i > 0; i--)
		idx += max_inst - i;

	/* Count this row up to the target */
	idx += (x - 1 - y);

	/* Slots are in Rx/Tx pairs */
	idx *= 2;

	/* Pick Rx/Tx direction */
	idx += direction;

	return idx;
}

static int g2g_register_flat(struct xe_guc *guc, u32 far_tile, u32 far_dev, u32 type, bool have_dev)
{
	struct xe_gt *gt = guc_to_gt(guc);
	struct xe_device *xe = gt_to_xe(gt);
	u32 near_tile = gt_to_tile(gt)->id;
	u32 near_dev = G2G_DEV(gt);
	u32 max = xe->info.gt_count;
	int idx;
	u32 base, desc, buf;

	if (!guc->g2g.bo)
		return -ENODEV;

	idx = g2g_slot_flat(near_tile, near_dev, far_tile, far_dev, type, max, have_dev);
	xe_assert(xe, idx >= 0);

	base = guc_bo_ggtt_addr(guc, guc->g2g.bo);
	desc = base + idx * G2G_DESC_SIZE;
	buf = base + idx * G2G_BUFFER_SIZE + G2G_DESC_AREA_SIZE;

	xe_assert(xe, (desc - base + G2G_DESC_SIZE) <= G2G_DESC_AREA_SIZE);
	xe_assert(xe, (buf - base + G2G_BUFFER_SIZE) <= xe_bo_size(guc->g2g.bo));

	return guc_action_register_g2g_buffer(guc, type, far_tile, far_dev,
					      desc, buf, G2G_BUFFER_SIZE);
}

static void g2g_start(struct kunit *test, struct xe_guc *guc)
{
	struct xe_gt *remote_gt, *gt = guc_to_gt(guc);
	struct xe_device *xe = gt_to_xe(gt);
	unsigned int i;
	int t, ret;
	bool have_dev;

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, guc->g2g.bo);

	/* GuC interface will need extending if more GT device types are ever created. */
	KUNIT_ASSERT_TRUE(test,
			  (gt->info.type == XE_GT_TYPE_MAIN) ||
			  (gt->info.type == XE_GT_TYPE_MEDIA));

	/* Channel numbering depends on whether there are multiple GTs per tile */
	have_dev = xe->info.gt_count > xe->info.tile_count;

	for_each_gt(remote_gt, xe, i) {
		u32 tile, dev;

		if (remote_gt->info.id == gt->info.id)
			continue;

		tile = gt_to_tile(remote_gt)->id;
		dev = G2G_DEV(remote_gt);

		for (t = 0; t < XE_G2G_TYPE_LIMIT; t++) {
			ret = g2g_register_flat(guc, tile, dev, t, have_dev);
			KUNIT_ASSERT_EQ_MSG(test, 0, ret, "G2G register failed: %pe", ERR_PTR(ret));
		}
	}
}

static void g2g_reinit(struct kunit *test, struct xe_device *xe, int ctb_type, struct xe_tile *tile)
{
	struct xe_gt *gt;
	int i, found = 0;

	g2g_stop(test, xe);

	for_each_gt(gt, xe, i) {
		struct xe_guc *guc = &gt->uc.guc;

		KUNIT_ASSERT_NULL(test, guc->g2g.bo);
	}

	switch (ctb_type) {
	case G2G_CTB_TYPE_DEFAULT:
		g2g_alloc_default(test, xe);
		break;

	case G2G_CTB_TYPE_HOST:
		g2g_alloc_host(test, xe);
		break;

	case G2G_CTB_TYPE_TILE:
		g2g_alloc_tile(test, xe, tile);
		break;

	default:
		KUNIT_ASSERT_TRUE(test, false);
	}

	for_each_gt(gt, xe, i) {
		struct xe_guc *guc = &gt->uc.guc;

		if (!guc->g2g.bo)
			continue;

		if (ctb_type == G2G_CTB_TYPE_DEFAULT)
			guc_g2g_start(guc);
		else
			g2g_start(test, guc);
		found++;
	}

	KUNIT_ASSERT_GT_MSG(test, found, 1, "insufficient G2G channels running: %d", found);

	kunit_info(test, "Testing across %d GTs\n", found);
}

static void g2g_recreate_ctb(void *_xe)
{
	struct xe_device *xe = (struct xe_device *)_xe;
	struct kunit *test = kunit_get_current_test();

	g2g_stop(test, xe);

	if (xe_guc_g2g_wanted(xe))
		g2g_reinit(test, xe, G2G_CTB_TYPE_DEFAULT, NULL);
}

static void g2g_pm_runtime_put(void *_xe)
{
	struct xe_device *xe = (struct xe_device *)_xe;

	xe_pm_runtime_put(xe);
}

static void g2g_pm_runtime_get(struct kunit *test)
{
	struct xe_device *xe = test->priv;
	int ret;

	xe_pm_runtime_get(xe);
	ret = kunit_add_action_or_reset(test, g2g_pm_runtime_put, xe);
	KUNIT_ASSERT_EQ_MSG(test, 0, ret, "Failed to register runtime PM action\n");
}

static void g2g_check_skip(struct kunit *test)
{
	struct xe_device *xe = test->priv;
	struct xe_gt *gt;
	int i;

	if (IS_SRIOV_VF(xe))
		kunit_skip(test, "not supported from a VF");

	if (xe->info.gt_count <= 1)
		kunit_skip(test, "not enough GTs");

	for_each_gt(gt, xe, i) {
		struct xe_guc *guc = &gt->uc.guc;

		if (guc->fw.build_type == CSS_UKERNEL_INFO_BUILDTYPE_PROD)
			kunit_skip(test,
				   "G2G test interface not available in production firmware builds\n");
	}
}

/*
 * Simple test that does not try to recreate the CTBs.
 * Requires that the platform already enables G2G comms
 * but has no risk of leaving the system in a broken state
 * afterwards.
 */
static void xe_live_guc_g2g_kunit_default(struct kunit *test)
{
	struct xe_device *xe = test->priv;

	if (!xe_guc_g2g_wanted(xe))
		kunit_skip(test, "G2G not enabled");

	g2g_check_skip(test);

	g2g_pm_runtime_get(test);

	kunit_info(test, "Testing default CTBs\n");
	g2g_run_test(test, xe);

	kunit_release_action(test, &g2g_pm_runtime_put, xe);
}

/*
 * More complex test that re-creates the CTBs in various location to
 * test access to each location from each GuC. Can be run even on
 * systems that do not enable G2G by default. On the other hand,
 * because it recreates the CTBs, if something goes wrong it could
 * leave the system with broken G2G comms.
 */
static void xe_live_guc_g2g_kunit_allmem(struct kunit *test)
{
	struct xe_device *xe = test->priv;
	int ret;

	g2g_check_skip(test);

	g2g_pm_runtime_get(test);

	/* Make sure to leave the system as we found it */
	ret = kunit_add_action_or_reset(test, g2g_recreate_ctb, xe);
	KUNIT_ASSERT_EQ_MSG(test, 0, ret, "Failed to register CTB re-creation action\n");

	kunit_info(test, "Testing CTB type 'default'...\n");
	g2g_reinit(test, xe, G2G_CTB_TYPE_DEFAULT, NULL);
	g2g_run_test(test, xe);

	kunit_info(test, "Testing CTB type 'host'...\n");
	g2g_reinit(test, xe, G2G_CTB_TYPE_HOST, NULL);
	g2g_run_test(test, xe);

	if (IS_DGFX(xe)) {
		struct xe_tile *tile;
		int id;

		for_each_tile(tile, xe, id) {
			kunit_info(test, "Testing CTB type 'tile: #%d'...\n", id);

			g2g_reinit(test, xe, G2G_CTB_TYPE_TILE, tile);
			g2g_run_test(test, xe);
		}
	} else {
		kunit_info(test, "Skipping local memory on integrated platform\n");
	}

	kunit_release_action(test, g2g_recreate_ctb, xe);
	kunit_release_action(test, g2g_pm_runtime_put, xe);
}

static struct kunit_case xe_guc_g2g_tests[] = {
	KUNIT_CASE_PARAM(xe_live_guc_g2g_kunit_default, xe_pci_live_device_gen_param),
	KUNIT_CASE_PARAM(xe_live_guc_g2g_kunit_allmem, xe_pci_live_device_gen_param),
	{}
};

VISIBLE_IF_KUNIT
struct kunit_suite xe_guc_g2g_test_suite = {
	.name = "xe_guc_g2g",
	.test_cases = xe_guc_g2g_tests,
	.init = xe_kunit_helper_xe_device_live_test_init,
};
EXPORT_SYMBOL_IF_KUNIT(xe_guc_g2g_test_suite);
