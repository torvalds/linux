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
#include <linux/of_fdt.h>
#include <linux/libfdt.h>
#include <linux/memblock.h>
#include <asm/hvcall.h>
#include <asm/machdep.h>
#include <asm/plpks.h>
#include <asm/firmware.h>

static u8 *ospassword;
static u16 ospasswordlength;

// Retrieved with H_PKS_GET_CONFIG
static u8 version;
static u16 objoverhead;
static u16 maxpwsize;
static u16 maxobjsize;
static s16 maxobjlabelsize;
static u32 totalsize;
static u32 usedspace;
static u32 supportedpolicies;
static u32 maxlargeobjectsize;
static u64 signedupdatealgorithms;

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
	case H_LONG_BUSY_ORDER_1_MSEC:
	case H_LONG_BUSY_ORDER_10_MSEC:
	case H_LONG_BUSY_ORDER_100_MSEC:
	case H_LONG_BUSY_ORDER_1_SEC:
	case H_LONG_BUSY_ORDER_10_SEC:
	case H_LONG_BUSY_ORDER_100_SEC:
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

	pr_debug("Converted hypervisor code %d to Linux %d\n", rc, err);

	return err;
}

static int plpks_gen_password(void)
{
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE] = { 0 };
	u8 *password, consumer = PLPKS_OS_OWNER;
	int rc;

	// If we booted from kexec, we could be reusing an existing password already
	if (ospassword) {
		pr_debug("Password of length %u already in use\n", ospasswordlength);
		return 0;
	}

	// The password must not cross a page boundary, so we align to the next power of 2
	password = kzalloc(roundup_pow_of_two(maxpwsize), GFP_KERNEL);
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
			pr_warn("Password already set - authenticated operations will fail\n");
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

	// The auth structure must not cross a page boundary and must be
	// 16 byte aligned. We align to the next largest power of 2
	auth = kzalloc(roundup_pow_of_two(struct_size(auth, password, maxpwsize)), GFP_KERNEL);
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

/*
 * Label is combination of label attributes + name.
 * Label attributes are used internally by kernel and not exposed to the user.
 */
static struct label *construct_label(char *component, u8 varos, u8 *name,
				     u16 namelen)
{
	struct label *label;
	size_t slen = 0;

	if (!name || namelen > PLPKS_MAX_NAME_SIZE)
		return ERR_PTR(-EINVAL);

	// Support NULL component for signed updates
	if (component) {
		slen = strlen(component);
		if (slen > sizeof(label->attr.prefix))
			return ERR_PTR(-EINVAL);
	}

	// The label structure must not cross a page boundary, so we align to the next power of 2
	label = kzalloc(roundup_pow_of_two(sizeof(*label)), GFP_KERNEL);
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
	struct config {
		u8 version;
		u8 flags;
		__be16 rsvd0;
		__be16 objoverhead;
		__be16 maxpwsize;
		__be16 maxobjlabelsize;
		__be16 maxobjsize;
		__be32 totalsize;
		__be32 usedspace;
		__be32 supportedpolicies;
		__be32 maxlargeobjectsize;
		__be64 signedupdatealgorithms;
		u8 rsvd1[476];
	} __packed * config;
	size_t size;
	int rc = 0;

	size = sizeof(*config);

	// Config struct must not cross a page boundary. So long as the struct
	// size is a power of 2, this should be fine as alignment is guaranteed
	config = kzalloc(size, GFP_KERNEL);
	if (!config) {
		rc = -ENOMEM;
		goto err;
	}

	rc = plpar_hcall(H_PKS_GET_CONFIG, retbuf, virt_to_phys(config), size);

	if (rc != H_SUCCESS) {
		rc = pseries_status_to_err(rc);
		goto err;
	}

	version = config->version;
	objoverhead = be16_to_cpu(config->objoverhead);
	maxpwsize = be16_to_cpu(config->maxpwsize);
	maxobjsize = be16_to_cpu(config->maxobjsize);
	maxobjlabelsize = be16_to_cpu(config->maxobjlabelsize);
	totalsize = be32_to_cpu(config->totalsize);
	usedspace = be32_to_cpu(config->usedspace);
	supportedpolicies = be32_to_cpu(config->supportedpolicies);
	maxlargeobjectsize = be32_to_cpu(config->maxlargeobjectsize);
	signedupdatealgorithms = be64_to_cpu(config->signedupdatealgorithms);

	// Validate that the numbers we get back match the requirements of the spec
	if (maxpwsize < 32) {
		pr_err("Invalid Max Password Size received from hypervisor (%d < 32)\n", maxpwsize);
		rc = -EIO;
		goto err;
	}

	if (maxobjlabelsize < 255) {
		pr_err("Invalid Max Object Label Size received from hypervisor (%d < 255)\n",
		       maxobjlabelsize);
		rc = -EIO;
		goto err;
	}

	if (totalsize < 4096) {
		pr_err("Invalid Total Size received from hypervisor (%d < 4096)\n", totalsize);
		rc = -EIO;
		goto err;
	}

	if (version >= 3 && maxlargeobjectsize >= 65536 && maxobjsize != 0xFFFF) {
		pr_err("Invalid Max Object Size (0x%x != 0xFFFF)\n", maxobjsize);
		rc = -EIO;
		goto err;
	}

err:
	kfree(config);
	return rc;
}

u8 plpks_get_version(void)
{
	return version;
}

u16 plpks_get_objoverhead(void)
{
	return objoverhead;
}

u16 plpks_get_maxpwsize(void)
{
	return maxpwsize;
}

u16 plpks_get_maxobjectsize(void)
{
	return maxobjsize;
}

u16 plpks_get_maxobjectlabelsize(void)
{
	return maxobjlabelsize;
}

u32 plpks_get_totalsize(void)
{
	return totalsize;
}

u32 plpks_get_usedspace(void)
{
	// Unlike other config values, usedspace regularly changes as objects
	// are updated, so we need to refresh.
	int rc = _plpks_get_config();
	if (rc) {
		pr_err("Couldn't get config, rc: %d\n", rc);
		return 0;
	}
	return usedspace;
}

u32 plpks_get_supportedpolicies(void)
{
	return supportedpolicies;
}

u32 plpks_get_maxlargeobjectsize(void)
{
	return maxlargeobjectsize;
}

u64 plpks_get_signedupdatealgorithms(void)
{
	return signedupdatealgorithms;
}

u16 plpks_get_passwordlen(void)
{
	return ospasswordlength;
}

bool plpks_is_available(void)
{
	int rc;

	if (!firmware_has_feature(FW_FEATURE_PLPKS))
		return false;

	rc = _plpks_get_config();
	if (rc)
		return false;

	return true;
}

static int plpks_confirm_object_flushed(struct label *label,
					struct plpks_auth *auth)
{
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE] = { 0 };
	bool timed_out = true;
	u64 timeout = 0;
	u8 status;
	int rc;

	do {
		rc = plpar_hcall(H_PKS_CONFIRM_OBJECT_FLUSHED, retbuf,
				 virt_to_phys(auth), virt_to_phys(label),
				 label->size);

		status = retbuf[0];
		if (rc) {
			timed_out = false;
			if (rc == H_NOT_FOUND && status == 1)
				rc = 0;
			break;
		}

		if (!rc && status == 1) {
			timed_out = false;
			break;
		}

		usleep_range(PLPKS_FLUSH_SLEEP,
			     PLPKS_FLUSH_SLEEP + PLPKS_FLUSH_SLEEP_RANGE);
		timeout = timeout + PLPKS_FLUSH_SLEEP;
	} while (timeout < PLPKS_MAX_TIMEOUT);

	if (timed_out)
		return -ETIMEDOUT;

	return pseries_status_to_err(rc);
}

int plpks_signed_update_var(struct plpks_var *var, u64 flags)
{
	unsigned long retbuf[PLPAR_HCALL9_BUFSIZE] = {0};
	int rc;
	struct label *label;
	struct plpks_auth *auth;
	u64 continuetoken = 0;
	u64 timeout = 0;

	if (!var->data || var->datalen <= 0 || var->namelen > PLPKS_MAX_NAME_SIZE)
		return -EINVAL;

	if (!(var->policy & PLPKS_SIGNEDUPDATE))
		return -EINVAL;

	// Signed updates need the component to be NULL.
	if (var->component)
		return -EINVAL;

	auth = construct_auth(PLPKS_OS_OWNER);
	if (IS_ERR(auth))
		return PTR_ERR(auth);

	label = construct_label(var->component, var->os, var->name, var->namelen);
	if (IS_ERR(label)) {
		rc = PTR_ERR(label);
		goto out;
	}

	do {
		rc = plpar_hcall9(H_PKS_SIGNED_UPDATE, retbuf,
				  virt_to_phys(auth), virt_to_phys(label),
				  label->size, var->policy, flags,
				  virt_to_phys(var->data), var->datalen,
				  continuetoken);

		continuetoken = retbuf[0];
		if (pseries_status_to_err(rc) == -EBUSY) {
			int delay_ms = get_longbusy_msecs(rc);
			mdelay(delay_ms);
			timeout += delay_ms;
		}
		rc = pseries_status_to_err(rc);
	} while (rc == -EBUSY && timeout < PLPKS_MAX_TIMEOUT);

	if (!rc)
		rc = plpks_confirm_object_flushed(label, auth);

	kfree(label);
out:
	kfree(auth);

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

	if (vname.namelen > PLPKS_MAX_NAME_SIZE)
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
		rc = pseries_status_to_err(rc);
		goto out_free_output;
	}

	if (!var->data || var->datalen > retbuf[0])
		var->datalen = retbuf[0];

	var->policy = retbuf[1];

	if (var->data)
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

int plpks_populate_fdt(void *fdt)
{
	int chosen_offset = fdt_path_offset(fdt, "/chosen");

	if (chosen_offset < 0) {
		pr_err("Can't find chosen node: %s\n",
		       fdt_strerror(chosen_offset));
		return chosen_offset;
	}

	return fdt_setprop(fdt, chosen_offset, "ibm,plpks-pw", ospassword, ospasswordlength);
}

// Once a password is registered with the hypervisor it cannot be cleared without
// rebooting the LPAR, so to keep using the PLPKS across kexec boots we need to
// recover the previous password from the FDT.
//
// There are a few challenges here.  We don't want the password to be visible to
// users, so we need to clear it from the FDT.  This has to be done in early boot.
// Clearing it from the FDT would make the FDT's checksum invalid, so we have to
// manually cause the checksum to be recalculated.
void __init plpks_early_init_devtree(void)
{
	void *fdt = initial_boot_params;
	int chosen_node = fdt_path_offset(fdt, "/chosen");
	const u8 *password;
	int len;

	if (chosen_node < 0)
		return;

	password = fdt_getprop(fdt, chosen_node, "ibm,plpks-pw", &len);
	if (len <= 0) {
		pr_debug("Couldn't find ibm,plpks-pw node.\n");
		return;
	}

	ospassword = memblock_alloc_raw(len, SMP_CACHE_BYTES);
	if (!ospassword) {
		pr_err("Error allocating memory for password.\n");
		goto out;
	}

	memcpy(ospassword, password, len);
	ospasswordlength = (u16)len;

out:
	fdt_nop_property(fdt, chosen_node, "ibm,plpks-pw");
	// Since we've cleared the password, we must update the FDT checksum
	early_init_dt_verify(fdt);
}

static __init int pseries_plpks_init(void)
{
	int rc;

	if (!firmware_has_feature(FW_FEATURE_PLPKS))
		return -ENODEV;

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
