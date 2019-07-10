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

#include "i915_selftest.h"
#include "gem/i915_gem_pm.h"

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

static int ring_doorbell_nop(struct intel_guc_client *client)
{
	struct guc_process_desc *desc = __get_process_desc(client);
	int err;

	client->use_nop_wqi = true;

	spin_lock_irq(&client->wq_lock);

	guc_wq_item_append(client, 0, 0, 0, 0);
	guc_ring_doorbell(client);

	spin_unlock_irq(&client->wq_lock);

	client->use_nop_wqi = false;

	/* if there are no issues GuC will update the WQ head and keep the
	 * WQ in active status
	 */
	err = wait_for(READ_ONCE(desc->head) == READ_ONCE(desc->tail), 10);
	if (err) {
		pr_err("doorbell %u ring failed!\n", client->doorbell_id);
		return -EIO;
	}

	if (desc->wq_status != WQ_STATUS_ACTIVE) {
		pr_err("doorbell %u ring put WQ in bad state (%u)!\n",
		       client->doorbell_id, desc->wq_status);
		return -EIO;
	}

	return 0;
}

/*
 * Basic client sanity check, handy to validate create_clients.
 */
static int validate_client(struct intel_guc_client *client, int client_priority)
{
	if (client->priority != client_priority ||
	    client->doorbell_id == GUC_DOORBELL_INVALID)
		return -EINVAL;
	else
		return 0;
}

static bool client_doorbell_in_sync(struct intel_guc_client *client)
{
	return !client || doorbell_ok(client->guc, client->doorbell_id);
}

/*
 * Check that we're able to synchronize guc_clients with their doorbells
 *
 * We're creating clients and reserving doorbells once, at module load. During
 * module lifetime, GuC, doorbell HW, and i915 state may go out of sync due to
 * GuC being reset. In other words - GuC clients are still around, but the
 * status of their doorbells may be incorrect. This is the reason behind
 * validating that the doorbells status expected by the driver matches what the
 * GuC/HW have.
 */
static int igt_guc_clients(void *args)
{
	struct drm_i915_private *dev_priv = args;
	intel_wakeref_t wakeref;
	struct intel_guc *guc;
	int err = 0;

	GEM_BUG_ON(!HAS_GUC(dev_priv));
	mutex_lock(&dev_priv->drm.struct_mutex);
	wakeref = intel_runtime_pm_get(&dev_priv->runtime_pm);

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
	guc_clients_disable(guc);
	guc_clients_destroy(guc);
	if (guc->execbuf_client) {
		pr_err("guc_clients_destroy lied!\n");
		err = -EINVAL;
		goto unlock;
	}

	err = guc_clients_create(guc);
	if (err) {
		pr_err("Failed to create clients\n");
		goto unlock;
	}
	GEM_BUG_ON(!guc->execbuf_client);

	err = validate_client(guc->execbuf_client,
			      GUC_CLIENT_PRIORITY_KMD_NORMAL);
	if (err) {
		pr_err("execbug client validation failed\n");
		goto out;
	}

	/* the client should now have reserved a doorbell */
	if (!has_doorbell(guc->execbuf_client)) {
		pr_err("guc_clients_create didn't reserve doorbells\n");
		err = -EINVAL;
		goto out;
	}

	/* Now enable the clients */
	guc_clients_enable(guc);

	/* each client should now have received a doorbell */
	if (!client_doorbell_in_sync(guc->execbuf_client)) {
		pr_err("failed to initialize the doorbells\n");
		err = -EINVAL;
		goto out;
	}

	/*
	 * Basic test - an attempt to reallocate a valid doorbell to the
	 * client it is currently assigned should not cause a failure.
	 */
	err = create_doorbell(guc->execbuf_client);

out:
	/*
	 * Leave clean state for other test, plus the driver always destroy the
	 * clients during unload.
	 */
	guc_clients_disable(guc);
	guc_clients_destroy(guc);
	guc_clients_create(guc);
	guc_clients_enable(guc);
unlock:
	intel_runtime_pm_put(&dev_priv->runtime_pm, wakeref);
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
	intel_wakeref_t wakeref;
	struct intel_guc *guc;
	int i, err = 0;
	u16 db_id;

	GEM_BUG_ON(!HAS_GUC(dev_priv));
	mutex_lock(&dev_priv->drm.struct_mutex);
	wakeref = intel_runtime_pm_get(&dev_priv->runtime_pm);

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
		clients[i] = guc_client_alloc(guc, i % GUC_CLIENT_PRIORITY_NUM);

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

		err = validate_client(clients[i], i % GUC_CLIENT_PRIORITY_NUM);
		if (err) {
			pr_err("[%d] client_alloc sanity check failed!\n", i);
			err = -EINVAL;
			goto out;
		}

		db_id = clients[i]->doorbell_id;

		err = __guc_client_enable(clients[i]);
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

		err = ring_doorbell_nop(clients[i]);
		if (err)
			goto out;
	}

out:
	for (i = 0; i < ATTEMPTS; i++)
		if (!IS_ERR_OR_NULL(clients[i])) {
			__guc_client_disable(clients[i]);
			guc_client_free(clients[i]);
		}
unlock:
	intel_runtime_pm_put(&dev_priv->runtime_pm, wakeref);
	mutex_unlock(&dev_priv->drm.struct_mutex);
	return err;
}

int intel_guc_live_selftest(struct drm_i915_private *dev_priv)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_guc_clients),
		SUBTEST(igt_guc_doorbells),
	};

	if (!USES_GUC_SUBMISSION(dev_priv))
		return 0;

	return i915_subtests(tests, dev_priv);
}
