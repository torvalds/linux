/*
 * Copyright © 2017 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#include "../i915_selftest.h"

/* max doorbell number + negative test for each client type */
#define ATTEMPTS (GUC_NUM_DOORBELLS + GUC_CLIENT_PRIORITY_NUM)

static struct intel_guc_client *clients[ATTEMPTS];

static bool available_dbs(struct intel_guc *guc, u32 priority)
{
	unsigned long offset;
	unsigned long end;
	u16 id;

	/* first half is used for normal priority, second half for high */
	offset = 0;
	end = GUC_NUM_DOORBELLS / 2;
	if (priority <= GUC_CLIENT_PRIORITY_HIGH) {
		offset = end;
		end += offset;
	}

	id = find_next_zero_bit(guc->doorbell_bitmap, end, offset);
	if (id < end)
		return true;

	return false;
}

static int check_all_doorbells(struct intel_guc *guc)
{
	u16 db_id;

	pr_info_once("Max number of doorbells: %d", GUC_NUM_DOORBELLS);
	for (db_id = 0; db_id < GUC_NUM_DOORBELLS; ++db_id) {
		if (!doorbell_ok(guc, db_id)) {
			pr_err("doorbell %d, not ok\n", db_id);
			return -EIO;
		}
	}

	return 0;
}

/*
 * Basic client sanity check, handy to validate create_clients.
 */
static int validate_client(struct intel_guc_client *client,
			   int client_priority,
			   bool is_preempt_client)
{
	struct drm_i915_private *dev_priv = guc_to_i915(client->guc);
	struct i915_gem_context *ctx_owner = is_preempt_client ?
			dev_priv->preempt_context : dev_priv->kernel_context;

	if (client->owner != ctx_owner ||
	    client->engines != INTEL_INFO(dev_priv)->ring_mask ||
	    client->priority != client_priority ||
	    client->doorbell_id == GUC_DOORBELL_INVALID)
		return -EINVAL;
	else
		return 0;
}

/*
 * Check that guc_init_doorbell_hw is doing what it should.
 *
 * During GuC submission enable, we create GuC clients and their doorbells,
 * but after resetting the microcontroller (resume & gpu reset), these
 * GuC clients are still around, but the status of their doorbells may be
 * incorrect. This is the reason behind validating that the doorbells status
 * expected by the driver matches what the GuC/HW have.
 */
static int igt_guc_init_doorbell_hw(void *args)
{
	struct drm_i915_private *dev_priv = args;
	struct intel_guc *guc;
	DECLARE_BITMAP(db_bitmap_bk, GUC_NUM_DOORBELLS);
	int i, err = 0;

	GEM_BUG_ON(!HAS_GUC(dev_priv));
	mutex_lock(&dev_priv->drm.struct_mutex);

	guc = &dev_priv->guc;
	if (!guc) {
		pr_err("No guc object!\n");
		err = -EINVAL;
		goto unlock;
	}

	err = check_all_doorbells(guc);
	if (err)
		goto unlock;

	/*
	 * Get rid of clients created during driver load because the test will
	 * recreate them.
	 */
	guc_clients_destroy(guc);
	if (guc->execbuf_client || guc->preempt_client) {
		pr_err("guc_clients_destroy lied!\n");
		err = -EINVAL;
		goto unlock;
	}

	err = guc_clients_create(guc);
	if (err) {
		pr_err("Failed to create clients\n");
		goto unlock;
	}

	err = validate_client(guc->execbuf_client,
			      GUC_CLIENT_PRIORITY_KMD_NORMAL, false);
	if (err) {
		pr_err("execbug client validation failed\n");
		goto out;
	}

	err = validate_client(guc->preempt_client,
			      GUC_CLIENT_PRIORITY_KMD_HIGH, true);
	if (err) {
		pr_err("preempt client validation failed\n");
		goto out;
	}

	/* each client should have received a doorbell during alloc */
	if (!has_doorbell(guc->execbuf_client) ||
	    !has_doorbell(guc->preempt_client)) {
		pr_err("guc_clients_create didn't create doorbells\n");
		err = -EINVAL;
		goto out;
	}

	/*
	 * Basic test - an attempt to reallocate a valid doorbell to the
	 * client it is currently assigned should not cause a failure.
	 */
	err = guc_init_doorbell_hw(guc);
	if (err)
		goto out;

	/*
	 * Negative test - a client with no doorbell (invalid db id).
	 * Each client gets a doorbell when it is created, after destroying
	 * the doorbell, the db id is changed to GUC_DOORBELL_INVALID and the
	 * firmware will reject any attempt to allocate a doorbell with an
	 * invalid id (db has to be reserved before allocation).
	 */
	destroy_doorbell(guc->execbuf_client);
	if (has_doorbell(guc->execbuf_client)) {
		pr_err("destroy db did not work\n");
		err = -EINVAL;
		goto out;
	}

	err = guc_init_doorbell_hw(guc);
	if (err != -EIO) {
		pr_err("unexpected (err = %d)", err);
		goto out;
	}

	if (!available_dbs(guc, guc->execbuf_client->priority)) {
		pr_err("doorbell not available when it should\n");
		err = -EIO;
		goto out;
	}

	/* clean after test */
	err = create_doorbell(guc->execbuf_client);
	if (err) {
		pr_err("recreate doorbell failed\n");
		goto out;
	}

	/*
	 * Negative test - doorbell_bitmap out of sync, will trigger a few of
	 * WARN_ON(!doorbell_ok(guc, db_id)) but that's ok as long as the
	 * doorbells from our clients don't fail.
	 */
	bitmap_copy(db_bitmap_bk, guc->doorbell_bitmap, GUC_NUM_DOORBELLS);
	for (i = 0; i < GUC_NUM_DOORBELLS; i++)
		if (i % 2)
			test_and_change_bit(i, guc->doorbell_bitmap);

	err = guc_init_doorbell_hw(guc);
	if (err) {
		pr_err("out of sync doorbell caused an error\n");
		goto out;
	}

	/* restore 'correct' db bitmap */
	bitmap_copy(guc->doorbell_bitmap, db_bitmap_bk, GUC_NUM_DOORBELLS);
	err = guc_init_doorbell_hw(guc);
	if (err) {
		pr_err("restored doorbell caused an error\n");
		goto out;
	}

out:
	/*
	 * Leave clean state for other test, plus the driver always destroy the
	 * clients during unload.
	 */
	guc_clients_destroy(guc);
	guc_clients_create(guc);
unlock:
	mutex_unlock(&dev_priv->drm.struct_mutex);
	return err;
}

/*
 * Create as many clients as number of doorbells. Note that there's already
 * client(s)/doorbell(s) created during driver load, but this test creates
 * its own and do not interact with the existing ones.
 */
static int igt_guc_doorbells(void *arg)
{
	struct drm_i915_private *dev_priv = arg;
	struct intel_guc *guc;
	int i, err = 0;
	u16 db_id;

	GEM_BUG_ON(!HAS_GUC(dev_priv));
	mutex_lock(&dev_priv->drm.struct_mutex);

	guc = &dev_priv->guc;
	if (!guc) {
		pr_err("No guc object!\n");
		err = -EINVAL;
		goto unlock;
	}

	err = check_all_doorbells(guc);
	if (err)
		goto unlock;

	for (i = 0; i < ATTEMPTS; i++) {
		clients[i] = guc_client_alloc(dev_priv,
					      INTEL_INFO(dev_priv)->ring_mask,
					      i % GUC_CLIENT_PRIORITY_NUM,
					      dev_priv->kernel_context);

		if (!clients[i]) {
			pr_err("[%d] No guc client\n", i);
			err = -EINVAL;
			goto out;
		}

		if (IS_ERR(clients[i])) {
			if (PTR_ERR(clients[i]) != -ENOSPC) {
				pr_err("[%d] unexpected error\n", i);
				err = PTR_ERR(clients[i]);
				goto out;
			}

			if (available_dbs(guc, i % GUC_CLIENT_PRIORITY_NUM)) {
				pr_err("[%d] non-db related alloc fail\n", i);
				err = -EINVAL;
				goto out;
			}

			/* expected, ran out of dbs for this client type */
			continue;
		}

		/*
		 * The check below is only valid because we keep a doorbell
		 * assigned during the whole life of the client.
		 */
		if (clients[i]->stage_id >= GUC_NUM_DOORBELLS) {
			pr_err("[%d] more clients than doorbells (%d >= %d)\n",
			       i, clients[i]->stage_id, GUC_NUM_DOORBELLS);
			err = -EINVAL;
			goto out;
		}

		err = validate_client(clients[i],
				      i % GUC_CLIENT_PRIORITY_NUM, false);
		if (err) {
			pr_err("[%d] client_alloc sanity check failed!\n", i);
			err = -EINVAL;
			goto out;
		}

		db_id = clients[i]->doorbell_id;

		/*
		 * Client alloc gives us a doorbell, but we want to exercise
		 * this ourselves (this resembles guc_init_doorbell_hw)
		 */
		destroy_doorbell(clients[i]);
		if (clients[i]->doorbell_id != GUC_DOORBELL_INVALID) {
			pr_err("[%d] destroy db did not work!\n", i);
			err = -EINVAL;
			goto out;
		}

		err = __reserve_doorbell(clients[i]);
		if (err) {
			pr_err("[%d] Failed to reserve a doorbell\n", i);
			goto out;
		}

		__update_doorbell_desc(clients[i], clients[i]->doorbell_id);
		err = __create_doorbell(clients[i]);
		if (err) {
			pr_err("[%d] Failed to create a doorbell\n", i);
			goto out;
		}

		/* doorbell id shouldn't change, we are holding the mutex */
		if (db_id != clients[i]->doorbell_id) {
			pr_err("[%d] doorbell id changed (%d != %d)\n",
			       i, db_id, clients[i]->doorbell_id);
			err = -EINVAL;
			goto out;
		}

		err = check_all_doorbells(guc);
		if (err)
			goto out;
	}

out:
	for (i = 0; i < ATTEMPTS; i++)
		if (!IS_ERR_OR_NULL(clients[i]))
			guc_client_free(clients[i]);
unlock:
	mutex_unlock(&dev_priv->drm.struct_mutex);
	return err;
}

int intel_guc_live_selftest(struct drm_i915_private *dev_priv)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_guc_init_doorbell_hw),
		SUBTEST(igt_guc_doorbells),
	};

	if (!i915_modparams.enable_guc_submission)
		return 0;

	return i915_subtests(tests, dev_priv);
}
