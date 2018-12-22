// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2018 Intel Corporation. All rights reserved. */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/ndctl.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/cred.h>
#include <linux/key.h>
#include <linux/key-type.h>
#include <keys/user-type.h>
#include <keys/encrypted-type.h>
#include "nd-core.h"
#include "nd.h"

#define NVDIMM_BASE_KEY		0
#define NVDIMM_NEW_KEY		1

static bool key_revalidate = true;
module_param(key_revalidate, bool, 0444);
MODULE_PARM_DESC(key_revalidate, "Require key validation at init.");

static void *key_data(struct key *key)
{
	struct encrypted_key_payload *epayload = dereference_key_locked(key);

	lockdep_assert_held_read(&key->sem);

	return epayload->decrypted_data;
}

static void nvdimm_put_key(struct key *key)
{
	if (!key)
		return;

	up_read(&key->sem);
	key_put(key);
}

/*
 * Retrieve kernel key for DIMM and request from user space if
 * necessary. Returns a key held for read and must be put by
 * nvdimm_put_key() before the usage goes out of scope.
 */
static struct key *nvdimm_request_key(struct nvdimm *nvdimm)
{
	struct key *key = NULL;
	static const char NVDIMM_PREFIX[] = "nvdimm:";
	char desc[NVDIMM_KEY_DESC_LEN + sizeof(NVDIMM_PREFIX)];
	struct device *dev = &nvdimm->dev;

	sprintf(desc, "%s%s", NVDIMM_PREFIX, nvdimm->dimm_id);
	key = request_key(&key_type_encrypted, desc, "");
	if (IS_ERR(key)) {
		if (PTR_ERR(key) == -ENOKEY)
			dev_dbg(dev, "request_key() found no key\n");
		else
			dev_dbg(dev, "request_key() upcall failed\n");
		key = NULL;
	} else {
		struct encrypted_key_payload *epayload;

		down_read(&key->sem);
		epayload = dereference_key_locked(key);
		if (epayload->decrypted_datalen != NVDIMM_PASSPHRASE_LEN) {
			up_read(&key->sem);
			key_put(key);
			key = NULL;
		}
	}

	return key;
}

static struct key *nvdimm_lookup_user_key(struct nvdimm *nvdimm,
		key_serial_t id, int subclass)
{
	key_ref_t keyref;
	struct key *key;
	struct encrypted_key_payload *epayload;
	struct device *dev = &nvdimm->dev;

	keyref = lookup_user_key(id, 0, 0);
	if (IS_ERR(keyref))
		return NULL;

	key = key_ref_to_ptr(keyref);
	if (key->type != &key_type_encrypted) {
		key_put(key);
		return NULL;
	}

	dev_dbg(dev, "%s: key found: %#x\n", __func__, key_serial(key));

	down_read_nested(&key->sem, subclass);
	epayload = dereference_key_locked(key);
	if (epayload->decrypted_datalen != NVDIMM_PASSPHRASE_LEN) {
		up_read(&key->sem);
		key_put(key);
		key = NULL;
	}
	return key;
}

static struct key *nvdimm_key_revalidate(struct nvdimm *nvdimm)
{
	struct key *key;
	int rc;

	if (!nvdimm->sec.ops->change_key)
		return NULL;

	key = nvdimm_request_key(nvdimm);
	if (!key)
		return NULL;

	/*
	 * Send the same key to the hardware as new and old key to
	 * verify that the key is good.
	 */
	rc = nvdimm->sec.ops->change_key(nvdimm, key_data(key),
			key_data(key), NVDIMM_USER);
	if (rc < 0) {
		nvdimm_put_key(key);
		key = NULL;
	}
	return key;
}

static int __nvdimm_security_unlock(struct nvdimm *nvdimm)
{
	struct device *dev = &nvdimm->dev;
	struct nvdimm_bus *nvdimm_bus = walk_to_nvdimm_bus(dev);
	struct key *key = NULL;
	int rc;

	/* The bus lock should be held at the top level of the call stack */
	lockdep_assert_held(&nvdimm_bus->reconfig_mutex);

	if (!nvdimm->sec.ops || !nvdimm->sec.ops->unlock
			|| nvdimm->sec.state < 0)
		return -EIO;

	if (test_bit(NDD_SECURITY_OVERWRITE, &nvdimm->flags)) {
		dev_dbg(dev, "Security operation in progress.\n");
		return -EBUSY;
	}

	/*
	 * If the pre-OS has unlocked the DIMM, attempt to send the key
	 * from request_key() to the hardware for verification.  Failure
	 * to revalidate the key against the hardware results in a
	 * freeze of the security configuration. I.e. if the OS does not
	 * have the key, security is being managed pre-OS.
	 */
	if (nvdimm->sec.state == NVDIMM_SECURITY_UNLOCKED) {
		if (!key_revalidate)
			return 0;

		key = nvdimm_key_revalidate(nvdimm);
		if (!key)
			return nvdimm_security_freeze(nvdimm);
	} else
		key = nvdimm_request_key(nvdimm);

	if (!key)
		return -ENOKEY;

	rc = nvdimm->sec.ops->unlock(nvdimm, key_data(key));
	dev_dbg(dev, "key: %d unlock: %s\n", key_serial(key),
			rc == 0 ? "success" : "fail");

	nvdimm_put_key(key);
	nvdimm->sec.state = nvdimm_security_state(nvdimm, NVDIMM_USER);
	return rc;
}

int nvdimm_security_unlock(struct device *dev)
{
	struct nvdimm *nvdimm = to_nvdimm(dev);
	int rc;

	nvdimm_bus_lock(dev);
	rc = __nvdimm_security_unlock(nvdimm);
	nvdimm_bus_unlock(dev);
	return rc;
}

int nvdimm_security_disable(struct nvdimm *nvdimm, unsigned int keyid)
{
	struct device *dev = &nvdimm->dev;
	struct nvdimm_bus *nvdimm_bus = walk_to_nvdimm_bus(dev);
	struct key *key;
	int rc;

	/* The bus lock should be held at the top level of the call stack */
	lockdep_assert_held(&nvdimm_bus->reconfig_mutex);

	if (!nvdimm->sec.ops || !nvdimm->sec.ops->disable
			|| nvdimm->sec.state < 0)
		return -EOPNOTSUPP;

	if (nvdimm->sec.state >= NVDIMM_SECURITY_FROZEN) {
		dev_dbg(dev, "Incorrect security state: %d\n",
				nvdimm->sec.state);
		return -EIO;
	}

	if (test_bit(NDD_SECURITY_OVERWRITE, &nvdimm->flags)) {
		dev_dbg(dev, "Security operation in progress.\n");
		return -EBUSY;
	}

	key = nvdimm_lookup_user_key(nvdimm, keyid, NVDIMM_BASE_KEY);
	if (!key)
		return -ENOKEY;

	rc = nvdimm->sec.ops->disable(nvdimm, key_data(key));
	dev_dbg(dev, "key: %d disable: %s\n", key_serial(key),
			rc == 0 ? "success" : "fail");

	nvdimm_put_key(key);
	nvdimm->sec.state = nvdimm_security_state(nvdimm, NVDIMM_USER);
	return rc;
}

int nvdimm_security_update(struct nvdimm *nvdimm, unsigned int keyid,
		unsigned int new_keyid,
		enum nvdimm_passphrase_type pass_type)
{
	struct device *dev = &nvdimm->dev;
	struct nvdimm_bus *nvdimm_bus = walk_to_nvdimm_bus(dev);
	struct key *key, *newkey;
	int rc;

	/* The bus lock should be held at the top level of the call stack */
	lockdep_assert_held(&nvdimm_bus->reconfig_mutex);

	if (!nvdimm->sec.ops || !nvdimm->sec.ops->change_key
			|| nvdimm->sec.state < 0)
		return -EOPNOTSUPP;

	if (nvdimm->sec.state >= NVDIMM_SECURITY_FROZEN) {
		dev_dbg(dev, "Incorrect security state: %d\n",
				nvdimm->sec.state);
		return -EIO;
	}

	if (keyid == 0)
		key = NULL;
	else {
		key = nvdimm_lookup_user_key(nvdimm, keyid, NVDIMM_BASE_KEY);
		if (!key)
			return -ENOKEY;
	}

	newkey = nvdimm_lookup_user_key(nvdimm, new_keyid, NVDIMM_NEW_KEY);
	if (!newkey) {
		nvdimm_put_key(key);
		return -ENOKEY;
	}

	rc = nvdimm->sec.ops->change_key(nvdimm, key ? key_data(key) : NULL,
			key_data(newkey), pass_type);
	dev_dbg(dev, "key: %d %d update%s: %s\n",
			key_serial(key), key_serial(newkey),
			pass_type == NVDIMM_MASTER ? "(master)" : "(user)",
			rc == 0 ? "success" : "fail");

	nvdimm_put_key(newkey);
	nvdimm_put_key(key);
	if (pass_type == NVDIMM_MASTER)
		nvdimm->sec.ext_state = nvdimm_security_state(nvdimm,
				NVDIMM_MASTER);
	else
		nvdimm->sec.state = nvdimm_security_state(nvdimm,
				NVDIMM_USER);
	return rc;
}

int nvdimm_security_erase(struct nvdimm *nvdimm, unsigned int keyid,
		enum nvdimm_passphrase_type pass_type)
{
	struct device *dev = &nvdimm->dev;
	struct nvdimm_bus *nvdimm_bus = walk_to_nvdimm_bus(dev);
	struct key *key;
	int rc;

	/* The bus lock should be held at the top level of the call stack */
	lockdep_assert_held(&nvdimm_bus->reconfig_mutex);

	if (!nvdimm->sec.ops || !nvdimm->sec.ops->erase
			|| nvdimm->sec.state < 0)
		return -EOPNOTSUPP;

	if (atomic_read(&nvdimm->busy)) {
		dev_dbg(dev, "Unable to secure erase while DIMM active.\n");
		return -EBUSY;
	}

	if (nvdimm->sec.state >= NVDIMM_SECURITY_FROZEN) {
		dev_dbg(dev, "Incorrect security state: %d\n",
				nvdimm->sec.state);
		return -EIO;
	}

	if (test_bit(NDD_SECURITY_OVERWRITE, &nvdimm->flags)) {
		dev_dbg(dev, "Security operation in progress.\n");
		return -EBUSY;
	}

	if (nvdimm->sec.ext_state != NVDIMM_SECURITY_UNLOCKED
			&& pass_type == NVDIMM_MASTER) {
		dev_dbg(dev,
			"Attempt to secure erase in wrong master state.\n");
		return -EOPNOTSUPP;
	}

	key = nvdimm_lookup_user_key(nvdimm, keyid, NVDIMM_BASE_KEY);
	if (!key)
		return -ENOKEY;

	rc = nvdimm->sec.ops->erase(nvdimm, key_data(key), pass_type);
	dev_dbg(dev, "key: %d erase%s: %s\n", key_serial(key),
			pass_type == NVDIMM_MASTER ? "(master)" : "(user)",
			rc == 0 ? "success" : "fail");

	nvdimm_put_key(key);
	nvdimm->sec.state = nvdimm_security_state(nvdimm, NVDIMM_USER);
	return rc;
}

int nvdimm_security_overwrite(struct nvdimm *nvdimm, unsigned int keyid)
{
	struct device *dev = &nvdimm->dev;
	struct nvdimm_bus *nvdimm_bus = walk_to_nvdimm_bus(dev);
	struct key *key;
	int rc;

	/* The bus lock should be held at the top level of the call stack */
	lockdep_assert_held(&nvdimm_bus->reconfig_mutex);

	if (!nvdimm->sec.ops || !nvdimm->sec.ops->overwrite
			|| nvdimm->sec.state < 0)
		return -EOPNOTSUPP;

	if (atomic_read(&nvdimm->busy)) {
		dev_dbg(dev, "Unable to overwrite while DIMM active.\n");
		return -EBUSY;
	}

	if (dev->driver == NULL) {
		dev_dbg(dev, "Unable to overwrite while DIMM active.\n");
		return -EINVAL;
	}

	if (nvdimm->sec.state >= NVDIMM_SECURITY_FROZEN) {
		dev_dbg(dev, "Incorrect security state: %d\n",
				nvdimm->sec.state);
		return -EIO;
	}

	if (test_bit(NDD_SECURITY_OVERWRITE, &nvdimm->flags)) {
		dev_dbg(dev, "Security operation in progress.\n");
		return -EBUSY;
	}

	if (keyid == 0)
		key = NULL;
	else {
		key = nvdimm_lookup_user_key(nvdimm, keyid, NVDIMM_BASE_KEY);
		if (!key)
			return -ENOKEY;
	}

	rc = nvdimm->sec.ops->overwrite(nvdimm, key ? key_data(key) : NULL);
	dev_dbg(dev, "key: %d overwrite submission: %s\n", key_serial(key),
			rc == 0 ? "success" : "fail");

	nvdimm_put_key(key);
	if (rc == 0) {
		set_bit(NDD_SECURITY_OVERWRITE, &nvdimm->flags);
		set_bit(NDD_WORK_PENDING, &nvdimm->flags);
		nvdimm->sec.state = NVDIMM_SECURITY_OVERWRITE;
		/*
		 * Make sure we don't lose device while doing overwrite
		 * query.
		 */
		get_device(dev);
		queue_delayed_work(system_wq, &nvdimm->dwork, 0);
	}

	return rc;
}

void __nvdimm_security_overwrite_query(struct nvdimm *nvdimm)
{
	struct nvdimm_bus *nvdimm_bus = walk_to_nvdimm_bus(&nvdimm->dev);
	int rc;
	unsigned int tmo;

	/* The bus lock should be held at the top level of the call stack */
	lockdep_assert_held(&nvdimm_bus->reconfig_mutex);

	/*
	 * Abort and release device if we no longer have the overwrite
	 * flag set. It means the work has been canceled.
	 */
	if (!test_bit(NDD_WORK_PENDING, &nvdimm->flags))
		return;

	tmo = nvdimm->sec.overwrite_tmo;

	if (!nvdimm->sec.ops || !nvdimm->sec.ops->query_overwrite
			|| nvdimm->sec.state < 0)
		return;

	rc = nvdimm->sec.ops->query_overwrite(nvdimm);
	if (rc == -EBUSY) {

		/* setup delayed work again */
		tmo += 10;
		queue_delayed_work(system_wq, &nvdimm->dwork, tmo * HZ);
		nvdimm->sec.overwrite_tmo = min(15U * 60U, tmo);
		return;
	}

	if (rc < 0)
		dev_dbg(&nvdimm->dev, "overwrite failed\n");
	else
		dev_dbg(&nvdimm->dev, "overwrite completed\n");

	if (nvdimm->sec.overwrite_state)
		sysfs_notify_dirent(nvdimm->sec.overwrite_state);
	nvdimm->sec.overwrite_tmo = 0;
	clear_bit(NDD_SECURITY_OVERWRITE, &nvdimm->flags);
	clear_bit(NDD_WORK_PENDING, &nvdimm->flags);
	put_device(&nvdimm->dev);
	nvdimm->sec.state = nvdimm_security_state(nvdimm, NVDIMM_USER);
	nvdimm->sec.ext_state = nvdimm_security_state(nvdimm, NVDIMM_MASTER);
}

void nvdimm_security_overwrite_query(struct work_struct *work)
{
	struct nvdimm *nvdimm =
		container_of(work, typeof(*nvdimm), dwork.work);

	nvdimm_bus_lock(&nvdimm->dev);
	__nvdimm_security_overwrite_query(nvdimm);
	nvdimm_bus_unlock(&nvdimm->dev);
}
