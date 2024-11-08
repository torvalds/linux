// SPDX-License-Identifier: GPL-2.0-only
/*
 * POWER LPAR Platform KeyStore(PLPKS)
 * Copyright (C) 2022 IBM Corporation
 * Author: Nayna Jain <nayna@linux.ibm.com>
 *
 * Provides access to variables stored in Power LPAR Platform KeyStore(PLPKS).
 */

#define pr_fmt(fmt) "plpks: " fmt

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <asm/hvcall.h>
#include <asm/machdep.h>

#include "plpks.h"

static u8 *ospassword;
static u16 ospasswordlength;

// Retrieved with H_PKS_GET_CONFIG
static u16 maxpwsize;
static u16 maxobjsize;

struct plpks_auth {
	u8 version;
	u8 consumer;
	__be64 rsvd0;
	__be32 rsvd1;
	__be16 passwordlength;
	u8 password[];
} __packed __aligned(16);

struct label_attr {
	u8 prefix[8];
	u8 version;
	u8 os;
	u8 length;
	u8 reserved[5];
};

struct label {
	struct label_attr attr;
	u8 name[PLPKS_MAX_NAME_SIZE];
	size_t size;
};

static int pseries_status_to_err(int rc)
{
	int err;

	switch (rc) {
	case H_SUCCESS:
		err = 0;
		break;
	case H_FUNCTION:
		err = -ENXIO;
		break;
	case H_PARAMETER:
	case H_P2:
	case H_P3:
	case H_P4:
	case H_P5:
	case H_P6:
		err = -EINVAL;
		break;
	case H_NOT_FOUND:
		err = -ENOENT;
		break;
	case H_BUSY:
		err = -EBUSY;
		break;
	case H_AUTHORITY:
		err = -EPERM;
		break;
	case H_NO_MEM:
		err = -ENOMEM;
		break;
	case H_RESOURCE:
		err = -EEXIST;
		break;
	case H_TOO_BIG:
		err = -EFBIG;
		break;
	case H_STATE:
		err = -EIO;
		break;
	case H_R_STATE:
		err = -EIO;
		break;
	case H_IN_USE:
		err = -EEXIST;
		break;
	case H_ABORTED:
		err = -EIO;
		break;
	default:
		err = -EINVAL;
	}

	return err;
}

static int plpks_gen_password(void)
{
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE] = { 0 };
	u8 *password, consumer = PLPKS_OS_OWNER;
	int rc;

	password = kzalloc(maxpwsize, GFP_KERNEL);
	if (!password)
		return -ENOMEM;

	rc = plpar_hcall(H_PKS_GEN_PASSWORD, retbuf, consumer, 0,
			 virt_to_phys(password), maxpwsize);

	if (!rc) {
		ospasswordlength = maxpwsize;
		ospassword = kzalloc(maxpwsize, GFP_KERNEL);
		if (!ospassword) {
			kfree(password);
			return -ENOMEM;
		}
		memcpy(ospassword, password, ospasswordlength);
	} else {
		if (rc == H_IN_USE) {
			pr_warn("Password is already set for POWER LPAR Platform KeyStore\n");
			rc = 0;
		} else {
			goto out;
		}
	}
out:
	kfree(password);

	return pseries_status_to_err(rc);
}

static struct plpks_auth *construct_auth(u8 consumer)
{
	struct plpks_auth *auth;

	if (consumer > PLPKS_OS_OWNER)
		return ERR_PTR(-EINVAL);

	auth = kzalloc(struct_size(auth, password, maxpwsize), GFP_KERNEL);
	if (!auth)
		return ERR_PTR(-ENOMEM);

	auth->version = 1;
	auth->consumer = consumer;

	if (consumer == PLPKS_FW_OWNER || consumer == PLPKS_BOOTLOADER_OWNER)
		return auth;

	memcpy(auth->password, ospassword, ospasswordlength);

	auth->passwordlength = cpu_to_be16(ospasswordlength);

	return auth;
}

/**
 * Label is combination of label attributes + name.
 * Label attributes are used internally by kernel and not exposed to the user.
 */
static struct label *construct_label(char *component, u8 varos, u8 *name,
				     u16 namelen)
{
	struct label *label;
	size_t slen;

	if (!name || namelen > PLPKS_MAX_NAME_SIZE)
		return ERR_PTR(-EINVAL);

	slen = strlen(component);
	if (component && slen > sizeof(label->attr.prefix))
		return ERR_PTR(-EINVAL);

	label = kzalloc(sizeof(*label), GFP_KERNEL);
	if (!label)
		return ERR_PTR(-ENOMEM);

	if (component)
		memcpy(&label->attr.prefix, component, slen);

	label->attr.version = PLPKS_LABEL_VERSION;
	label->attr.os = varos;
	label->attr.length = PLPKS_MAX_LABEL_ATTR_SIZE;
	memcpy(&label->name, name, namelen);

	label->size = sizeof(struct label_attr) + namelen;

	return label;
}

static int _plpks_get_config(void)
{
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE] = { 0 };
	struct {
		u8 version;
		u8 flags;
		__be32 rsvd0;
		__be16 maxpwsize;
		__be16 maxobjlabelsize;
		__be16 maxobjsize;
		__be32 totalsize;
		__be32 usedspace;
		__be32 supportedpolicies;
		__be64 rsvd1;
	} __packed config;
	size_t size;
	int rc;

	size = sizeof(config);

	rc = plpar_hcall(H_PKS_GET_CONFIG, retbuf, virt_to_phys(&config), size);

	if (rc != H_SUCCESS)
		return pseries_status_to_err(rc);

	maxpwsize = be16_to_cpu(config.maxpwsize);
	maxobjsize = be16_to_cpu(config.maxobjsize);

	return 0;
}

static int plpks_confirm_object_flushed(struct label *label,
					struct plpks_auth *auth)
{
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE] = { 0 };
	u64 timeout = 0;
	u8 status;
	int rc;

	do {
		rc = plpar_hcall(H_PKS_CONFIRM_OBJECT_FLUSHED, retbuf,
				 virt_to_phys(auth), virt_to_phys(label),
				 label->size);

		status = retbuf[0];
		if (rc) {
			if (rc == H_NOT_FOUND && status == 1)
				rc = 0;
			break;
		}

		if (!rc && status == 1)
			break;

		fsleep(PLPKS_FLUSH_SLEEP);
		timeout = timeout + PLPKS_FLUSH_SLEEP;
	} while (timeout < PLPKS_MAX_TIMEOUT);

	rc = pseries_status_to_err(rc);

	return rc;
}

int plpks_write_var(struct plpks_var var)
{
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE] = { 0 };
	struct plpks_auth *auth;
	struct label *label;
	int rc;

	if (!var.component || !var.data || var.datalen <= 0 ||
	    var.namelen > PLPKS_MAX_NAME_SIZE || var.datalen > PLPKS_MAX_DATA_SIZE)
		return -EINVAL;

	if (var.policy & PLPKS_SIGNEDUPDATE)
		return -EINVAL;

	auth = construct_auth(PLPKS_OS_OWNER);
	if (IS_ERR(auth))
		return PTR_ERR(auth);

	label = construct_label(var.component, var.os, var.name, var.namelen);
	if (IS_ERR(label)) {
		rc = PTR_ERR(label);
		goto out;
	}

	rc = plpar_hcall(H_PKS_WRITE_OBJECT, retbuf, virt_to_phys(auth),
			 virt_to_phys(label), label->size, var.policy,
			 virt_to_phys(var.data), var.datalen);

	if (!rc)
		rc = plpks_confirm_object_flushed(label, auth);

	if (rc)
		pr_err("Failed to write variable %s for component %s with error %d\n",
		       var.name, var.component, rc);

	rc = pseries_status_to_err(rc);
	kfree(label);
out:
	kfree(auth);

	return rc;
}

int plpks_remove_var(char *component, u8 varos, struct plpks_var_name vname)
{
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE] = { 0 };
	struct plpks_auth *auth;
	struct label *label;
	int rc;

	if (!component || vname.namelen > PLPKS_MAX_NAME_SIZE)
		return -EINVAL;

	auth = construct_auth(PLPKS_OS_OWNER);
	if (IS_ERR(auth))
		return PTR_ERR(auth);

	label = construct_label(component, varos, vname.name, vname.namelen);
	if (IS_ERR(label)) {
		rc = PTR_ERR(label);
		goto out;
	}

	rc = plpar_hcall(H_PKS_REMOVE_OBJECT, retbuf, virt_to_phys(auth),
			 virt_to_phys(label), label->size);

	if (!rc)
		rc = plpks_confirm_object_flushed(label, auth);

	if (rc)
		pr_err("Failed to remove variable %s for component %s with error %d\n",
		       vname.name, component, rc);

	rc = pseries_status_to_err(rc);
	kfree(label);
out:
	kfree(auth);

	return rc;
}

static int plpks_read_var(u8 consumer, struct plpks_var *var)
{
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE] = { 0 };
	struct plpks_auth *auth;
	struct label *label = NULL;
	u8 *output;
	int rc;

	if (var->namelen > PLPKS_MAX_NAME_SIZE)
		return -EINVAL;

	auth = construct_auth(consumer);
	if (IS_ERR(auth))
		return PTR_ERR(auth);

	if (consumer == PLPKS_OS_OWNER) {
		label = construct_label(var->component, var->os, var->name,
					var->namelen);
		if (IS_ERR(label)) {
			rc = PTR_ERR(label);
			goto out_free_auth;
		}
	}

	output = kzalloc(maxobjsize, GFP_KERNEL);
	if (!output) {
		rc = -ENOMEM;
		goto out_free_label;
	}

	if (consumer == PLPKS_OS_OWNER)
		rc = plpar_hcall(H_PKS_READ_OBJECT, retbuf, virt_to_phys(auth),
				 virt_to_phys(label), label->size, virt_to_phys(output),
				 maxobjsize);
	else
		rc = plpar_hcall(H_PKS_READ_OBJECT, retbuf, virt_to_phys(auth),
				 virt_to_phys(var->name), var->namelen, virt_to_phys(output),
				 maxobjsize);


	if (rc != H_SUCCESS) {
		pr_err("Failed to read variable %s for component %s with error %d\n",
		       var->name, var->component, rc);
		rc = pseries_status_to_err(rc);
		goto out_free_output;
	}

	if (var->datalen == 0 || var->datalen > retbuf[0])
		var->datalen = retbuf[0];

	var->data = kzalloc(var->datalen, GFP_KERNEL);
	if (!var->data) {
		rc = -ENOMEM;
		goto out_free_output;
	}
	var->policy = retbuf[1];

	memcpy(var->data, output, var->datalen);
	rc = 0;

out_free_output:
	kfree(output);
out_free_label:
	kfree(label);
out_free_auth:
	kfree(auth);

	return rc;
}

int plpks_read_os_var(struct plpks_var *var)
{
	return plpks_read_var(PLPKS_OS_OWNER, var);
}

int plpks_read_fw_var(struct plpks_var *var)
{
	return plpks_read_var(PLPKS_FW_OWNER, var);
}

int plpks_read_bootloader_var(struct plpks_var *var)
{
	return plpks_read_var(PLPKS_BOOTLOADER_OWNER, var);
}

static __init int pseries_plpks_init(void)
{
	int rc;

	rc = _plpks_get_config();

	if (rc) {
		pr_err("POWER LPAR Platform KeyStore is not supported or enabled\n");
		return rc;
	}

	rc = plpks_gen_password();
	if (rc)
		pr_err("Failed setting POWER LPAR Platform KeyStore Password\n");
	else
		pr_info("POWER LPAR Platform KeyStore initialized successfully\n");

	return rc;
}
machine_arch_initcall(pseries, pseries_plpks_init);
