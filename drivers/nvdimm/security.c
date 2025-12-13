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

static const char zero_key[NVDIMM_PASSPHRASE_LEN];

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

static const void *nvdimm_get_key_payload(struct nvdimm *nvdimm,
		struct key **key)
{
	*key = nvdimm_request_key(nvdimm);
	if (!*key)
		return zero_key;

	return key_data(*key);
}

static struct key *nvdimm_lookup_user_key(struct nvdimm *nvdimm,
		key_serial_t id, int subclass)
{
	key_ref_t keyref;
	struct key *key;
	struct encrypted_key_payload *epayload;
	struct device *dev = &nvdimm->dev;

	keyref = lookup_user_key(id, 0, KEY_NEED_SEARCH);
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

static const void *nvdimm_get_user_key_payload(struct nvdimm *nvdimm,
		key_serial_t id, int subclass, struct key **key)
{
	*key = NULL;
	if (id == 0) {
		if (subclass == NVDIMM_BASE_KEY)
			return zero_key;
		else
			return NULL;
	}

	*key = nvdimm_lookup_user_key(nvdimm, id, subclass);
	if (!*key)
		return NULL;

	return key_data(*key);
}


static int nvdimm_key_revalidate(struct nvdimm *nvdimm)
{
	struct key *key;
	int rc;
	const void *data;

	if (!nvdimm->sec.ops->change_key)
		return -EOPNOTSUPP;

	data = nvdimm_get_key_payload(nvdimm, &key);

	/*
	 * Send the same key to the hardware as new and old key to
	 * verify that the key is good.
	 */
	rc = nvdimm->sec.ops->change_key(nvdimm, data, data, NVDIMM_USER);
	if (rc < 0) {
		nvdimm_put_key(key);
		return rc;
	}

	nvdimm_put_key(key);
	nvdimm->sec.flags = nvdimm_security_flags(nvdimm, NVDIMM_USER);
	return 0;
}

static int __nvdimm_security_unlock(struct nvdimm *nvdimm)
{
	struct device *dev = &nvdimm->dev;
	struct nvdimm_bus *nvdimm_bus = walk_to_nvdimm_bus(dev);
	struct key *key;
	const void *data;
	int rc;

	/* The bus lock should be held at the top level of the call stack */
	lockdep_assert_held(&nvdimm_bus->reconfig_mutex);

	if (!nvdimm->sec.ops || !nvdimm->sec.ops->unlock
			|| !nvdimm->sec.flags)
		return -EIO;

	/* cxl_test needs this to pre-populate the security state */
	if (IS_ENABLED(CONFIG_NVDIMM_SECURITY_TEST))
		nvdimm->sec.flags = nvdimm_security_flags(nvdimm, NVDIMM_USER);

	/* No need to go further if security is disabled */
	if (test_bit(NVDIMM_SECURITY_DISABLED, &nvdimm->sec.flags))
		return 0;

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
	if (test_bit(NVDIMM_SECURITY_UNLOCKED, &nvdimm->sec.flags)) {
		if (!key_revalidate)
			return 0;

		return nvdimm_key_revalidate(nvdimm);
	} else
		data = nvdimm_get_key_payload(nvdimm, &key);

	rc = nvdimm->sec.ops->unlock(nvdimm, data);
	dev_dbg(dev, "key: %d unlock: %s\n", key_serial(key),
			rc == 0 ? "success" : "fail");
	if (rc == 0)
		set_bit(NDD_INCOHERENT, &nvdimm->flags);

	nvdimm_put_key(key);
	nvdimm->sec.flags = nvdimm_security_flags(nvdimm, NVDIMM_USER);
	return rc;
}

int nvdimm_security_unlock(struct device *dev)
{
	struct nvdimm *nvdimm = to_nvdimm(dev);

	guard(nvdimm_bus)(dev);
	return __nvdimm_security_unlock(nvdimm);
}

static int check_security_state(struct nvdimm *nvdimm)
{
	struct device *dev = &nvdimm->dev;

	if (test_bit(NVDIMM_SECURITY_FROZEN, &nvdimm->sec.flags)) {
		dev_dbg(dev, "Incorrect security state: %#lx\n",
				nvdimm->sec.flags);
		return -EIO;
	}

	if (test_bit(NDD_SECURITY_OVERWRITE, &nvdimm->flags)) {
		dev_dbg(dev, "Security operation in progress.\n");
		return -EBUSY;
	}

	return 0;
}

static int security_disable(struct nvdimm *nvdimm, unsigned int keyid,
			    enum nvdimm_passphrase_type pass_type)
{
	struct device *dev = &nvdimm->dev;
	struct nvdimm_bus *nvdimm_bus = walk_to_nvdimm_bus(dev);
	struct key *key;
	int rc;
	const void *data;

	/* The bus lock should be held at the top level of the call stack */
	lockdep_assert_held(&nvdimm_bus->reconfig_mutex);

	if (!nvdimm->sec.ops || !nvdimm->sec.flags)
		return -EOPNOTSUPP;

	if (pass_type == NVDIMM_USER && !nvdimm->sec.ops->disable)
		return -EOPNOTSUPP;

	if (pass_type == NVDIMM_MASTER && !nvdimm->sec.ops->disable_master)
		return -EOPNOTSUPP;

	rc = check_security_state(nvdimm);
	if (rc)
		return rc;

	data = nvdimm_get_user_key_payload(nvdimm, keyid,
			NVDIMM_BASE_KEY, &key);
	if (!data)
		return -ENOKEY;

	if (pass_type == NVDIMM_MASTER) {
		rc = nvdimm->sec.ops->disable_master(nvdimm, data);
		dev_dbg(dev, "key: %d disable_master: %s\n", key_serial(key),
			rc == 0 ? "success" : "fail");
	} else {
		rc = nvdimm->sec.ops->disable(nvdimm, data);
		dev_dbg(dev, "key: %d disable: %s\n", key_serial(key),
			rc == 0 ? "success" : "fail");
	}

	nvdimm_put_key(key);
	if (pass_type == NVDIMM_MASTER)
		nvdimm->sec.ext_flags = nvdimm_security_flags(nvdimm, NVDIMM_MASTER);
	else
		nvdimm->sec.flags = nvdimm_security_flags(nvdimm, NVDIMM_USER);
	return rc;
}

static int security_update(struct nvdimm *nvdimm, unsigned int keyid,
		unsigned int new_keyid,
		enum nvdimm_passphrase_type pass_type)
{
	struct device *dev = &nvdimm->dev;
	struct nvdimm_bus *nvdimm_bus = walk_to_nvdimm_bus(dev);
	struct key *key, *newkey;
	int rc;
	const void *data, *newdata;

	/* The bus lock should be held at the top level of the call stack */
	lockdep_assert_held(&nvdimm_bus->reconfig_mutex);

	if (!nvdimm->sec.ops || !nvdimm->sec.ops->change_key
			|| !nvdimm->sec.flags)
		return -EOPNOTSUPP;

	rc = check_security_state(nvdimm);
	if (rc)
		return rc;

	data = nvdimm_get_user_key_payload(nvdimm, keyid,
			NVDIMM_BASE_KEY, &key);
	if (!data)
		return -ENOKEY;

	newdata = nvdimm_get_user_key_payload(nvdimm, new_keyid,
			NVDIMM_NEW_KEY, &newkey);
	if (!newdata) {
		nvdimm_put_key(key);
		return -ENOKEY;
	}

	rc = nvdimm->sec.ops->change_key(nvdimm, data, newdata, pass_type);
	dev_dbg(dev, "key: %d %d update%s: %s\n",
			key_serial(key), key_serial(newkey),
			pass_type == NVDIMM_MASTER ? "(master)" : "(user)",
			rc == 0 ? "success" : "fail");

	nvdimm_put_key(newkey);
	nvdimm_put_key(key);
	if (pass_type == NVDIMM_MASTER)
		nvdimm->sec.ext_flags = nvdimm_security_flags(nvdimm,
				NVDIMM_MASTER);
	else
		nvdimm->sec.flags = nvdimm_security_flags(nvdimm,
				NVDIMM_USER);
	return rc;
}

static int security_erase(struct nvdimm *nvdimm, unsigned int keyid,
		enum nvdimm_passphrase_type pass_type)
{
	struct device *dev = &nvdimm->dev;
	struct nvdimm_bus *nvdimm_bus = walk_to_nvdimm_bus(dev);
	struct key *key = NULL;
	int rc;
	const void *data;

	/* The bus lock should be held at the top level of the call stack */
	lockdep_assert_held(&nvdimm_bus->reconfig_mutex);

	if (!nvdimm->sec.ops || !nvdimm->sec.ops->erase
			|| !nvdimm->sec.flags)
		return -EOPNOTSUPP;

	rc = check_security_state(nvdimm);
	if (rc)
		return rc;

	if (!test_bit(NVDIMM_SECURITY_UNLOCKED, &nvdimm->sec.ext_flags)
			&& pass_type == NVDIMM_MASTER) {
		dev_dbg(dev,
			"Attempt to secure erase in wrong master state.\n");
		return -EOPNOTSUPP;
	}

	data = nvdimm_get_user_key_payload(nvdimm, keyid,
			NVDIMM_BASE_KEY, &key);
	if (!data)
		return -ENOKEY;

	rc = nvdimm->sec.ops->erase(nvdimm, data, pass_type);
	if (rc == 0)
		set_bit(NDD_INCOHERENT, &nvdimm->flags);
	dev_dbg(dev, "key: %d erase%s: %s\n", key_serial(key),
			pass_type == NVDIMM_MASTER ? "(master)" : "(user)",
			rc == 0 ? "success" : "fail");

	nvdimm_put_key(key);
	nvdimm->sec.flags = nvdimm_security_flags(nvdimm, NVDIMM_USER);
	return rc;
}

static int security_overwrite(struct nvdimm *nvdimm, unsigned int keyid)
{
	struct device *dev = &nvdimm->dev;
	struct nvdimm_bus *nvdimm_bus = walk_to_nvdimm_bus(dev);
	struct key *key = NULL;
	int rc;
	const void *data;

	/* The bus lock should be held at the top level of the call stack */
	lockdep_assert_held(&nvdimm_bus->reconfig_mutex);

	if (!nvdimm->sec.ops || !nvdimm->sec.ops->overwrite
			|| !nvdimm->sec.flags)
		return -EOPNOTSUPP;

	rc = check_security_state(nvdimm);
	if (rc)
		return rc;

	data = nvdimm_get_user_key_payload(nvdimm, keyid,
			NVDIMM_BASE_KEY, &key);
	if (!data)
		return -ENOKEY;

	rc = nvdimm->sec.ops->overwrite(nvdimm, data);
	if (rc == 0)
		set_bit(NDD_INCOHERENT, &nvdimm->flags);
	dev_dbg(dev, "key: %d overwrite submission: %s\n", key_serial(key),
			rc == 0 ? "success" : "fail");

	nvdimm_put_key(key);
	if (rc == 0) {
		set_bit(NDD_SECURITY_OVERWRITE, &nvdimm->flags);
		set_bit(NDD_WORK_PENDING, &nvdimm->flags);
		set_bit(NVDIMM_SECURITY_OVERWRITE, &nvdimm->sec.flags);
		/*
		 * Make sure we don't lose device while doing overwrite
		 * query.
		 */
		get_device(dev);
		queue_delayed_work(system_wq, &nvdimm->dwork, 0);
	}

	return rc;
}

static void __nvdimm_security_overwrite_query(struct nvdimm *nvdimm)
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
			|| !nvdimm->sec.flags)
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

	/*
	 * Mark the overwrite work done and update dimm security flags,
	 * then send a sysfs event notification to wake up userspace
	 * poll threads to picked up the changed state.
	 */
	nvdimm->sec.overwrite_tmo = 0;
	clear_bit(NDD_SECURITY_OVERWRITE, &nvdimm->flags);
	clear_bit(NDD_WORK_PENDING, &nvdimm->flags);
	nvdimm->sec.flags = nvdimm_security_flags(nvdimm, NVDIMM_USER);
	nvdimm->sec.ext_flags = nvdimm_security_flags(nvdimm, NVDIMM_MASTER);
	if (nvdimm->sec.overwrite_state)
		sysfs_notify_dirent(nvdimm->sec.overwrite_state);
	put_device(&nvdimm->dev);
}

void nvdimm_security_overwrite_query(struct work_struct *work)
{
	struct nvdimm *nvdimm =
		container_of(work, typeof(*nvdimm), dwork.work);

	guard(nvdimm_bus)(&nvdimm->dev);
	__nvdimm_security_overwrite_query(nvdimm);
}

#define OPS							\
	C( OP_FREEZE,		"freeze",		1),	\
	C( OP_DISABLE,		"disable",		2),	\
	C( OP_DISABLE_MASTER,	"disable_master",	2),	\
	C( OP_UPDATE,		"update",		3),	\
	C( OP_ERASE,		"erase",		2),	\
	C( OP_OVERWRITE,	"overwrite",		2),	\
	C( OP_MASTER_UPDATE,	"master_update",	3),	\
	C( OP_MASTER_ERASE,	"master_erase",		2)
#undef C
#define C(a, b, c) a
enum nvdimmsec_op_ids { OPS };
#undef C
#define C(a, b, c) { b, c }
static struct {
	const char *name;
	int args;
} ops[] = { OPS };
#undef C

#define SEC_CMD_SIZE 32
#define KEY_ID_SIZE 10

ssize_t nvdimm_security_store(struct device *dev, const char *buf, size_t len)
{
	struct nvdimm *nvdimm = to_nvdimm(dev);
	ssize_t rc;
	char cmd[SEC_CMD_SIZE+1], keystr[KEY_ID_SIZE+1],
		nkeystr[KEY_ID_SIZE+1];
	unsigned int key, newkey;
	int i;

	rc = sscanf(buf, "%"__stringify(SEC_CMD_SIZE)"s"
			" %"__stringify(KEY_ID_SIZE)"s"
			" %"__stringify(KEY_ID_SIZE)"s",
			cmd, keystr, nkeystr);
	if (rc < 1)
		return -EINVAL;
	for (i = 0; i < ARRAY_SIZE(ops); i++)
		if (sysfs_streq(cmd, ops[i].name))
			break;
	if (i >= ARRAY_SIZE(ops))
		return -EINVAL;
	if (ops[i].args > 1)
		rc = kstrtouint(keystr, 0, &key);
	if (rc >= 0 && ops[i].args > 2)
		rc = kstrtouint(nkeystr, 0, &newkey);
	if (rc < 0)
		return rc;

	if (i == OP_FREEZE) {
		dev_dbg(dev, "freeze\n");
		rc = nvdimm_security_freeze(nvdimm);
	} else if (i == OP_DISABLE) {
		dev_dbg(dev, "disable %u\n", key);
		rc = security_disable(nvdimm, key, NVDIMM_USER);
	} else if (i == OP_DISABLE_MASTER) {
		dev_dbg(dev, "disable_master %u\n", key);
		rc = security_disable(nvdimm, key, NVDIMM_MASTER);
	} else if (i == OP_UPDATE || i == OP_MASTER_UPDATE) {
		dev_dbg(dev, "%s %u %u\n", ops[i].name, key, newkey);
		rc = security_update(nvdimm, key, newkey, i == OP_UPDATE
				? NVDIMM_USER : NVDIMM_MASTER);
	} else if (i == OP_ERASE || i == OP_MASTER_ERASE) {
		dev_dbg(dev, "%s %u\n", ops[i].name, key);
		if (atomic_read(&nvdimm->busy)) {
			dev_dbg(dev, "Unable to secure erase while DIMM active.\n");
			return -EBUSY;
		}
		rc = security_erase(nvdimm, key, i == OP_ERASE
				? NVDIMM_USER : NVDIMM_MASTER);
	} else if (i == OP_OVERWRITE) {
		dev_dbg(dev, "overwrite %u\n", key);
		if (atomic_read(&nvdimm->busy)) {
			dev_dbg(dev, "Unable to overwrite while DIMM active.\n");
			return -EBUSY;
		}
		rc = security_overwrite(nvdimm, key);
	} else
		return -EINVAL;

	if (rc == 0)
		rc = len;
	return rc;
}
