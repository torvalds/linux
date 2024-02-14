// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2021, Linaro Limited
 * Copyright (c) 2016, EPAM Systems
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/crash_dump.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/tee_drv.h>
#include <linux/types.h>
#include "optee_private.h"

int optee_pool_op_alloc_helper(struct tee_shm_pool *pool, struct tee_shm *shm,
			       size_t size, size_t align,
			       int (*shm_register)(struct tee_context *ctx,
						   struct tee_shm *shm,
						   struct page **pages,
						   size_t num_pages,
						   unsigned long start))
{
	size_t nr_pages = roundup(size, PAGE_SIZE) / PAGE_SIZE;
	struct page **pages;
	unsigned int i;
	int rc = 0;

	/*
	 * Ignore alignment since this is already going to be page aligned
	 * and there's no need for any larger alignment.
	 */
	shm->kaddr = alloc_pages_exact(nr_pages * PAGE_SIZE,
				       GFP_KERNEL | __GFP_ZERO);
	if (!shm->kaddr)
		return -ENOMEM;

	shm->paddr = virt_to_phys(shm->kaddr);
	shm->size = nr_pages * PAGE_SIZE;

	pages = kcalloc(nr_pages, sizeof(*pages), GFP_KERNEL);
	if (!pages) {
		rc = -ENOMEM;
		goto err;
	}

	for (i = 0; i < nr_pages; i++)
		pages[i] = virt_to_page((u8 *)shm->kaddr + i * PAGE_SIZE);

	shm->pages = pages;
	shm->num_pages = nr_pages;

	if (shm_register) {
		rc = shm_register(shm->ctx, shm, pages, nr_pages,
				  (unsigned long)shm->kaddr);
		if (rc)
			goto err;
	}

	return 0;
err:
	free_pages_exact(shm->kaddr, shm->size);
	shm->kaddr = NULL;
	return rc;
}

void optee_pool_op_free_helper(struct tee_shm_pool *pool, struct tee_shm *shm,
			       int (*shm_unregister)(struct tee_context *ctx,
						     struct tee_shm *shm))
{
	if (shm_unregister)
		shm_unregister(shm->ctx, shm);
	free_pages_exact(shm->kaddr, shm->size);
	shm->kaddr = NULL;
	kfree(shm->pages);
	shm->pages = NULL;
}

static void optee_bus_scan(struct work_struct *work)
{
	WARN_ON(optee_enumerate_devices(PTA_CMD_GET_DEVICES_SUPP));
}

int optee_open(struct tee_context *ctx, bool cap_memref_null)
{
	struct optee_context_data *ctxdata;
	struct tee_device *teedev = ctx->teedev;
	struct optee *optee = tee_get_drvdata(teedev);

	ctxdata = kzalloc(sizeof(*ctxdata), GFP_KERNEL);
	if (!ctxdata)
		return -ENOMEM;

	if (teedev == optee->supp_teedev) {
		bool busy = true;

		mutex_lock(&optee->supp.mutex);
		if (!optee->supp.ctx) {
			busy = false;
			optee->supp.ctx = ctx;
		}
		mutex_unlock(&optee->supp.mutex);
		if (busy) {
			kfree(ctxdata);
			return -EBUSY;
		}

		if (!optee->scan_bus_done) {
			INIT_WORK(&optee->scan_bus_work, optee_bus_scan);
			schedule_work(&optee->scan_bus_work);
			optee->scan_bus_done = true;
		}
	}
	mutex_init(&ctxdata->mutex);
	INIT_LIST_HEAD(&ctxdata->sess_list);

	ctx->cap_memref_null = cap_memref_null;
	ctx->data = ctxdata;
	return 0;
}

static void optee_release_helper(struct tee_context *ctx,
				 int (*close_session)(struct tee_context *ctx,
						      u32 session,
						      bool system_thread))
{
	struct optee_context_data *ctxdata = ctx->data;
	struct optee_session *sess;
	struct optee_session *sess_tmp;

	if (!ctxdata)
		return;

	list_for_each_entry_safe(sess, sess_tmp, &ctxdata->sess_list,
				 list_node) {
		list_del(&sess->list_node);
		close_session(ctx, sess->session_id, sess->use_sys_thread);
		kfree(sess);
	}
	kfree(ctxdata);
	ctx->data = NULL;
}

void optee_release(struct tee_context *ctx)
{
	optee_release_helper(ctx, optee_close_session_helper);
}

void optee_release_supp(struct tee_context *ctx)
{
	struct optee *optee = tee_get_drvdata(ctx->teedev);

	optee_release_helper(ctx, optee_close_session_helper);

	optee_supp_release(&optee->supp);
}

void optee_remove_common(struct optee *optee)
{
	/* Unregister OP-TEE specific client devices on TEE bus */
	optee_unregister_devices();

	optee_notif_uninit(optee);
	optee_shm_arg_cache_uninit(optee);
	teedev_close_context(optee->ctx);
	/*
	 * The two devices have to be unregistered before we can free the
	 * other resources.
	 */
	tee_device_unregister(optee->supp_teedev);
	tee_device_unregister(optee->teedev);

	tee_shm_pool_free(optee->pool);
	optee_supp_uninit(&optee->supp);
	mutex_destroy(&optee->call_queue.mutex);
}

static int smc_abi_rc;
static int ffa_abi_rc;

static int __init optee_core_init(void)
{
	/*
	 * The kernel may have crashed at the same time that all available
	 * secure world threads were suspended and we cannot reschedule the
	 * suspended threads without access to the crashed kernel's wait_queue.
	 * Therefore, we cannot reliably initialize the OP-TEE driver in the
	 * kdump kernel.
	 */
	if (is_kdump_kernel())
		return -ENODEV;

	smc_abi_rc = optee_smc_abi_register();
	ffa_abi_rc = optee_ffa_abi_register();

	/* If both failed there's no point with this module */
	if (smc_abi_rc && ffa_abi_rc)
		return smc_abi_rc;
	return 0;
}
module_init(optee_core_init);

static void __exit optee_core_exit(void)
{
	if (!smc_abi_rc)
		optee_smc_abi_unregister();
	if (!ffa_abi_rc)
		optee_ffa_abi_unregister();
}
module_exit(optee_core_exit);

MODULE_AUTHOR("Linaro");
MODULE_DESCRIPTION("OP-TEE driver");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:optee");
