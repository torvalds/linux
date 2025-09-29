// SPDX-License-Identifier: GPL-2.0 AND MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include <uapi/drm/xe_drm.h>

#include <kunit/test.h>
#include <kunit/visibility.h>

#include "tests/xe_kunit_helpers.h"
#include "tests/xe_pci_test.h"

#include "xe_pci.h"
#include "xe_pm.h"

static bool p2p_enabled(struct dma_buf_test_params *params)
{
	return IS_ENABLED(CONFIG_PCI_P2PDMA) && params->attach_ops &&
		params->attach_ops->allow_peer2peer;
}

static bool is_dynamic(struct dma_buf_test_params *params)
{
	return IS_ENABLED(CONFIG_DMABUF_MOVE_NOTIFY) && params->attach_ops &&
		params->attach_ops->move_notify;
}

static void check_residency(struct kunit *test, struct xe_bo *exported,
			    struct xe_bo *imported, struct dma_buf *dmabuf,
			    struct drm_exec *exec)
{
	struct dma_buf_test_params *params = to_dma_buf_test_params(test->priv);
	u32 mem_type;
	int ret;

	xe_bo_assert_held(exported);
	xe_bo_assert_held(imported);

	mem_type = XE_PL_VRAM0;
	if (!(params->mem_mask & XE_BO_FLAG_VRAM0))
		/* No VRAM allowed */
		mem_type = XE_PL_TT;
	else if (params->force_different_devices && !p2p_enabled(params))
		/* No P2P */
		mem_type = XE_PL_TT;
	else if (params->force_different_devices && !is_dynamic(params) &&
		 (params->mem_mask & XE_BO_FLAG_SYSTEM))
		/* Pin migrated to TT */
		mem_type = XE_PL_TT;

	if (!xe_bo_is_mem_type(exported, mem_type)) {
		KUNIT_FAIL(test, "Exported bo was not in expected memory type.\n");
		return;
	}

	if (xe_bo_is_pinned(exported))
		return;

	/*
	 * Evict exporter. Evicting the exported bo will
	 * evict also the imported bo through the move_notify() functionality if
	 * importer is on a different device. If they're on the same device,
	 * the exporter and the importer should be the same bo.
	 */
	ret = xe_bo_evict(exported, exec);
	if (ret) {
		if (ret != -EINTR && ret != -ERESTARTSYS)
			KUNIT_FAIL(test, "Evicting exporter failed with err=%d.\n",
				   ret);
		return;
	}

	/* Verify that also importer has been evicted to SYSTEM */
	if (exported != imported && !xe_bo_is_mem_type(imported, XE_PL_SYSTEM)) {
		KUNIT_FAIL(test, "Importer wasn't properly evicted.\n");
		return;
	}

	/* Re-validate the importer. This should move also exporter in. */
	ret = xe_bo_validate(imported, NULL, false, exec);
	if (ret) {
		if (ret != -EINTR && ret != -ERESTARTSYS)
			KUNIT_FAIL(test, "Validating importer failed with err=%d.\n",
				   ret);
		return;
	}

	KUNIT_EXPECT_TRUE(test, xe_bo_is_mem_type(exported, mem_type));

	if (params->force_different_devices)
		KUNIT_EXPECT_TRUE(test, xe_bo_is_mem_type(imported, XE_PL_TT));
	else
		KUNIT_EXPECT_TRUE(test, exported == imported);
}

static void xe_test_dmabuf_import_same_driver(struct xe_device *xe)
{
	struct kunit *test = kunit_get_current_test();
	struct dma_buf_test_params *params = to_dma_buf_test_params(test->priv);
	struct drm_gem_object *import;
	struct dma_buf *dmabuf;
	struct xe_bo *bo;
	size_t size;

	/* No VRAM on this device? */
	if (!ttm_manager_type(&xe->ttm, XE_PL_VRAM0) &&
	    (params->mem_mask & XE_BO_FLAG_VRAM0))
		return;

	size = PAGE_SIZE;
	if ((params->mem_mask & XE_BO_FLAG_VRAM0) &&
	    xe->info.vram_flags & XE_VRAM_FLAGS_NEED64K)
		size = SZ_64K;

	kunit_info(test, "running %s\n", __func__);
	bo = xe_bo_create_user(xe, NULL, size, DRM_XE_GEM_CPU_CACHING_WC,
			       params->mem_mask, NULL);
	if (IS_ERR(bo)) {
		KUNIT_FAIL(test, "xe_bo_create() failed with err=%ld\n",
			   PTR_ERR(bo));
		return;
	}

	dmabuf = xe_gem_prime_export(&bo->ttm.base, 0);
	if (IS_ERR(dmabuf)) {
		KUNIT_FAIL(test, "xe_gem_prime_export() failed with err=%ld\n",
			   PTR_ERR(dmabuf));
		goto out;
	}
	bo->ttm.base.dma_buf = dmabuf;

	import = xe_gem_prime_import(&xe->drm, dmabuf);
	if (!IS_ERR(import)) {
		struct xe_bo *import_bo = gem_to_xe_bo(import);

		/*
		 * Did import succeed when it shouldn't due to lack of p2p support?
		 */
		if (params->force_different_devices &&
		    !p2p_enabled(params) &&
		    !(params->mem_mask & XE_BO_FLAG_SYSTEM)) {
			KUNIT_FAIL(test,
				   "xe_gem_prime_import() succeeded when it shouldn't have\n");
		} else {
			struct drm_exec *exec = XE_VALIDATION_OPT_OUT;
			int err;

			/* Is everything where we expect it to be? */
			xe_bo_lock(import_bo, false);
			err = xe_bo_validate(import_bo, NULL, false, exec);

			/* Pinning in VRAM is not allowed. */
			if (!is_dynamic(params) &&
			    params->force_different_devices &&
			    !(params->mem_mask & XE_BO_FLAG_SYSTEM))
				KUNIT_EXPECT_EQ(test, err, -EINVAL);
			/* Otherwise only expect interrupts or success. */
			else if (err && err != -EINTR && err != -ERESTARTSYS)
				KUNIT_EXPECT_TRUE(test, !err || err == -EINTR ||
						  err == -ERESTARTSYS);

			if (!err)
				check_residency(test, bo, import_bo, dmabuf, exec);
			xe_bo_unlock(import_bo);
		}
		drm_gem_object_put(import);
	} else if (PTR_ERR(import) != -EOPNOTSUPP) {
		/* Unexpected error code. */
		KUNIT_FAIL(test,
			   "xe_gem_prime_import failed with the wrong err=%ld\n",
			   PTR_ERR(import));
	} else if (!params->force_different_devices ||
		   p2p_enabled(params) ||
		   (params->mem_mask & XE_BO_FLAG_SYSTEM)) {
		/* Shouldn't fail if we can reuse same bo, use p2p or use system */
		KUNIT_FAIL(test, "dynamic p2p attachment failed with err=%ld\n",
			   PTR_ERR(import));
	}
	bo->ttm.base.dma_buf = NULL;
	dma_buf_put(dmabuf);
out:
	drm_gem_object_put(&bo->ttm.base);
}

static const struct dma_buf_attach_ops nop2p_attach_ops = {
	.allow_peer2peer = false,
	.move_notify = xe_dma_buf_move_notify
};

/*
 * We test the implementation with bos of different residency and with
 * importers with different capabilities; some lacking p2p support and some
 * lacking dynamic capabilities (attach_ops == NULL). We also fake
 * different devices avoiding the import shortcut that just reuses the same
 * gem object.
 */
static const struct dma_buf_test_params test_params[] = {
	{.mem_mask = XE_BO_FLAG_VRAM0,
	 .attach_ops = &xe_dma_buf_attach_ops},
	{.mem_mask = XE_BO_FLAG_VRAM0 | XE_BO_FLAG_NEEDS_CPU_ACCESS,
	 .attach_ops = &xe_dma_buf_attach_ops,
	 .force_different_devices = true},

	{.mem_mask = XE_BO_FLAG_VRAM0,
	 .attach_ops = &nop2p_attach_ops},
	{.mem_mask = XE_BO_FLAG_VRAM0,
	 .attach_ops = &nop2p_attach_ops,
	 .force_different_devices = true},

	{.mem_mask = XE_BO_FLAG_VRAM0},
	{.mem_mask = XE_BO_FLAG_VRAM0,
	 .force_different_devices = true},

	{.mem_mask = XE_BO_FLAG_SYSTEM,
	 .attach_ops = &xe_dma_buf_attach_ops},
	{.mem_mask = XE_BO_FLAG_SYSTEM,
	 .attach_ops = &xe_dma_buf_attach_ops,
	 .force_different_devices = true},

	{.mem_mask = XE_BO_FLAG_SYSTEM,
	 .attach_ops = &nop2p_attach_ops},
	{.mem_mask = XE_BO_FLAG_SYSTEM,
	 .attach_ops = &nop2p_attach_ops,
	 .force_different_devices = true},

	{.mem_mask = XE_BO_FLAG_SYSTEM},
	{.mem_mask = XE_BO_FLAG_SYSTEM,
	 .force_different_devices = true},

	{.mem_mask = XE_BO_FLAG_SYSTEM | XE_BO_FLAG_VRAM0,
	 .attach_ops = &xe_dma_buf_attach_ops},
	{.mem_mask = XE_BO_FLAG_SYSTEM | XE_BO_FLAG_VRAM0 |
		     XE_BO_FLAG_NEEDS_CPU_ACCESS,
	 .attach_ops = &xe_dma_buf_attach_ops,
	 .force_different_devices = true},

	{.mem_mask = XE_BO_FLAG_SYSTEM | XE_BO_FLAG_VRAM0,
	 .attach_ops = &nop2p_attach_ops},
	{.mem_mask = XE_BO_FLAG_SYSTEM | XE_BO_FLAG_VRAM0,
	 .attach_ops = &nop2p_attach_ops,
	 .force_different_devices = true},

	{.mem_mask = XE_BO_FLAG_SYSTEM | XE_BO_FLAG_VRAM0},
	{.mem_mask = XE_BO_FLAG_SYSTEM | XE_BO_FLAG_VRAM0,
	 .force_different_devices = true},

	{}
};

static int dma_buf_run_device(struct xe_device *xe)
{
	const struct dma_buf_test_params *params;
	struct kunit *test = kunit_get_current_test();

	xe_pm_runtime_get(xe);
	for (params = test_params; params->mem_mask; ++params) {
		struct dma_buf_test_params p = *params;

		p.base.id = XE_TEST_LIVE_DMA_BUF;
		test->priv = &p;
		xe_test_dmabuf_import_same_driver(xe);
	}
	xe_pm_runtime_put(xe);

	/* A non-zero return would halt iteration over driver devices */
	return 0;
}

static void xe_dma_buf_kunit(struct kunit *test)
{
	struct xe_device *xe = test->priv;

	dma_buf_run_device(xe);
}

static struct kunit_case xe_dma_buf_tests[] = {
	KUNIT_CASE_PARAM(xe_dma_buf_kunit, xe_pci_live_device_gen_param),
	{}
};

VISIBLE_IF_KUNIT
struct kunit_suite xe_dma_buf_test_suite = {
	.name = "xe_dma_buf",
	.test_cases = xe_dma_buf_tests,
	.init = xe_kunit_helper_xe_device_live_test_init,
};
EXPORT_SYMBOL_IF_KUNIT(xe_dma_buf_test_suite);
