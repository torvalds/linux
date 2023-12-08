/* SPDX-License-Identifier: MIT */
/* List each unit test as selftest(name, function)
 *
 * The name is used as both an enum and expanded as subtest__name to create
 * a module parameter. It must be unique and legal for a C identifier.
 *
 * The function should be of type int function(void). It may be conditionally
 * compiled using #if IS_ENABLED(CONFIG_DRM_I915_SELFTEST).
 *
 * Tests are executed in order by igt/dmabuf_selftest
 */
selftest(sanitycheck, __sanitycheck__) /* keep first (igt selfcheck) */
selftest(dma_fence, dma_fence)
selftest(dma_fence_chain, dma_fence_chain)
selftest(dma_fence_unwrap, dma_fence_unwrap)
selftest(dma_resv, dma_resv)
