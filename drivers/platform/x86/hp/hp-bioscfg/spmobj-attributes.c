// SPDX-License-Identifier: GPL-2.0
/*
 * Functions corresponding to secure platform management object type
 * attributes under BIOS PASSWORD for use with hp-bioscfg driver
 *
 * Copyright (c) 2022 HP Development Company, L.P.
 */

#include "bioscfg.h"

static const char * const spm_state_types[] = {
	"not provisioned",
	"provisioned",
	"provisioning in progress",
};

static const char * const spm_mechanism_types[] = {
	"not provisioned",
	"signing-key",
	"endorsement-key",
};

struct secureplatform_provisioning_data {
	u8 state;
	u8 version[2];
	u8 reserved1;
	u32 features;
	u32 nonce;
	u8 reserved2[28];
	u8 sk_mod[MAX_KEY_MOD_SIZE];
	u8 kek_mod[MAX_KEY_MOD_SIZE];
};

/**
 * hp_calculate_security_buffer() - determines size of security buffer
 * for authentication scheme
 *
 * @authentication: the authentication content
 *
 * Currently only supported type is Admin password
 */
size_t hp_calculate_security_buffer(const char *authentication)
{
	size_t size, authlen;

	if (!authentication)
		return sizeof(u16) * 2;

	authlen = strlen(authentication);
	if (!authlen)
		return sizeof(u16) * 2;

	size = sizeof(u16) + authlen * sizeof(u16);
	if (!strstarts(authentication, BEAM_PREFIX))
		size += strlen(UTF_PREFIX) * sizeof(u16);

	return size;
}

/**
 * hp_populate_security_buffer() - builds a security buffer for
 * authentication scheme
 *
 * @authbuf: the security buffer
 * @authentication: the authentication content
 *
 * Currently only supported type is PLAIN TEXT
 */
int hp_populate_security_buffer(u16 *authbuf, const char *authentication)
{
	u16 *auth = authbuf;
	char *strprefix = NULL;
	int ret = 0;

	if (strstarts(authentication, BEAM_PREFIX)) {
		/*
		 * BEAM_PREFIX is append to authbuf when a signature
		 * is provided and Sure Admin is enabled in BIOS
		 */
		/* BEAM_PREFIX found, convert part to unicode */
		auth = hp_ascii_to_utf16_unicode(auth, authentication);
		if (!auth)
			return -EINVAL;

	} else {
		/*
		 * UTF-16 prefix is append to the * authbuf when a BIOS
		 * admin password is configured in BIOS
		 */

		/* append UTF_PREFIX to part and then convert it to unicode */
		strprefix = kasprintf(GFP_KERNEL, "%s%s", UTF_PREFIX,
				      authentication);
		if (!strprefix)
			return -ENOMEM;

		auth = hp_ascii_to_utf16_unicode(auth, strprefix);
		kfree(strprefix);

		if (!auth) {
			ret = -EINVAL;
			goto out_buffer;
		}
	}

out_buffer:
	return ret;
}

static ssize_t update_spm_state(void)
{
	struct secureplatform_provisioning_data data;
	int ret;

	ret = hp_wmi_perform_query(HPWMI_SECUREPLATFORM_GET_STATE,
				   HPWMI_SECUREPLATFORM, &data, 0,
				   sizeof(data));
	if (ret < 0)
		return ret;

	bioscfg_drv.spm_data.mechanism = data.state;
	if (bioscfg_drv.spm_data.mechanism)
		bioscfg_drv.spm_data.is_enabled = 1;

	return 0;
}

static ssize_t statusbin(struct kobject *kobj,
			 struct kobj_attribute *attr,
			 struct secureplatform_provisioning_data *buf)
{
	int ret = hp_wmi_perform_query(HPWMI_SECUREPLATFORM_GET_STATE,
				       HPWMI_SECUREPLATFORM, buf, 0,
				       sizeof(*buf));

	if (ret < 0)
		return ret;

	return sizeof(struct secureplatform_provisioning_data);
}

/*
 * status_show - Reads SPM status
 */
static ssize_t status_show(struct kobject *kobj, struct kobj_attribute
			   *attr, char *buf)
{
	int ret, i;
	int len = 0;
	struct secureplatform_provisioning_data data;

	ret = statusbin(kobj, attr, &data);
	if (ret < 0)
		return ret;

	/*
	 * 'status' is a read-only file that returns ASCII text in
	 * JSON format reporting the status information.
	 *
	 * "State": "not provisioned | provisioned | provisioning in progress ",
	 * "Version": " Major. Minor ",
	 * "Nonce": <16-bit unsigned number display in base 10>,
	 * "FeaturesInUse": <16-bit unsigned number display in base 10>,
	 * "EndorsementKeyMod": "<256 bytes in base64>",
	 * "SigningKeyMod": "<256 bytes in base64>"
	 */

	len += sysfs_emit_at(buf, len, "{\n");
	len += sysfs_emit_at(buf, len, "\t\"State\": \"%s\",\n",
			     spm_state_types[data.state]);
	len += sysfs_emit_at(buf, len, "\t\"Version\": \"%d.%d\"",
			     data.version[0], data.version[1]);

	/*
	 * state == 0 means secure platform management
	 * feature is not configured in BIOS.
	 */
	if (data.state == 0) {
		len += sysfs_emit_at(buf, len, "\n");
		goto status_exit;
	} else {
		len += sysfs_emit_at(buf, len, ",\n");
	}

	len += sysfs_emit_at(buf, len, "\t\"Nonce\": %d,\n", data.nonce);
	len += sysfs_emit_at(buf, len, "\t\"FeaturesInUse\": %d,\n", data.features);
	len += sysfs_emit_at(buf, len, "\t\"EndorsementKeyMod\": \"");

	for (i = 255; i >= 0; i--)
		len += sysfs_emit_at(buf, len, " %u", data.kek_mod[i]);

	len += sysfs_emit_at(buf, len, " \",\n");
	len += sysfs_emit_at(buf, len, "\t\"SigningKeyMod\": \"");

	for (i = 255; i >= 0; i--)
		len += sysfs_emit_at(buf, len, " %u", data.sk_mod[i]);

	/* Return buf contents */
	len += sysfs_emit_at(buf, len, " \"\n");

status_exit:
	len += sysfs_emit_at(buf, len, "}\n");

	return len;
}

static struct kobj_attribute password_spm_status = __ATTR_RO(status);

ATTRIBUTE_SPM_N_PROPERTY_SHOW(is_enabled, spm);
static struct kobj_attribute password_spm_is_key_enabled = __ATTR_RO(is_enabled);

static ssize_t key_mechanism_show(struct kobject *kobj, struct kobj_attribute *attr,
				  char *buf)
{
	return sysfs_emit(buf, "%s\n",
			  spm_mechanism_types[bioscfg_drv.spm_data.mechanism]);
}

static struct kobj_attribute password_spm_key_mechanism = __ATTR_RO(key_mechanism);

static ssize_t sk_store(struct kobject *kobj,
			struct kobj_attribute *attr,
			const char *buf, size_t count)
{
	int ret;
	int length;

	length = count;
	if (buf[length - 1] == '\n')
		length--;

	/* allocate space and copy current signing key */
	bioscfg_drv.spm_data.signing_key = kmalloc(length, GFP_KERNEL);
	if (!bioscfg_drv.spm_data.signing_key)
		return -ENOMEM;

	memcpy(bioscfg_drv.spm_data.signing_key, buf, length);

	/* submit signing key payload */
	ret = hp_wmi_perform_query(HPWMI_SECUREPLATFORM_SET_SK,
				   HPWMI_SECUREPLATFORM,
				   (void *)bioscfg_drv.spm_data.signing_key,
				   count, 0);

	if (!ret) {
		bioscfg_drv.spm_data.mechanism = SIGNING_KEY;
		hp_set_reboot_and_signal_event();
	}

	kfree(bioscfg_drv.spm_data.signing_key);
	bioscfg_drv.spm_data.signing_key = NULL;

	return ret ? ret : count;
}

static struct kobj_attribute password_spm_signing_key = __ATTR_WO(sk);

static ssize_t kek_store(struct kobject *kobj,
			 struct kobj_attribute *attr,
			 const char *buf, size_t count)
{
	int ret;
	int length;

	length = count;
	if (buf[length - 1] == '\n')
		length--;

	/* allocate space and copy current signing key */
	bioscfg_drv.spm_data.endorsement_key = kmalloc(length, GFP_KERNEL);
	if (!bioscfg_drv.spm_data.endorsement_key) {
		ret = -ENOMEM;
		goto exit_kek;
	}

	memcpy(bioscfg_drv.spm_data.endorsement_key, buf, length);

	ret = hp_wmi_perform_query(HPWMI_SECUREPLATFORM_SET_KEK,
				   HPWMI_SECUREPLATFORM,
				   (void *)bioscfg_drv.spm_data.endorsement_key,
				   count, 0);

	if (!ret) {
		bioscfg_drv.spm_data.mechanism = ENDORSEMENT_KEY;
		hp_set_reboot_and_signal_event();
	}

exit_kek:
	kfree(bioscfg_drv.spm_data.endorsement_key);
	bioscfg_drv.spm_data.endorsement_key = NULL;

	return ret ? ret : count;
}

static struct kobj_attribute password_spm_endorsement_key = __ATTR_WO(kek);

static ssize_t role_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	return sysfs_emit(buf, "%s\n", BIOS_SPM);
}

static struct kobj_attribute password_spm_role = __ATTR_RO(role);

static ssize_t auth_token_store(struct kobject *kobj,
				struct kobj_attribute *attr,
				const char *buf, size_t count)
{
	int ret = 0;
	int length;

	length = count;
	if (buf[length - 1] == '\n')
		length--;

	/* allocate space and copy current auth token */
	bioscfg_drv.spm_data.auth_token = kmalloc(length, GFP_KERNEL);
	if (!bioscfg_drv.spm_data.auth_token) {
		ret = -ENOMEM;
		goto exit_token;
	}

	memcpy(bioscfg_drv.spm_data.auth_token, buf, length);
	return count;

exit_token:
	kfree(bioscfg_drv.spm_data.auth_token);
	bioscfg_drv.spm_data.auth_token = NULL;

	return ret;
}

static struct kobj_attribute password_spm_auth_token = __ATTR_WO(auth_token);

static struct attribute *secure_platform_attrs[] = {
	&password_spm_is_key_enabled.attr,
	&password_spm_signing_key.attr,
	&password_spm_endorsement_key.attr,
	&password_spm_key_mechanism.attr,
	&password_spm_status.attr,
	&password_spm_role.attr,
	&password_spm_auth_token.attr,
	NULL,
};

static const struct attribute_group secure_platform_attr_group = {
	.attrs = secure_platform_attrs,
};

void hp_exit_secure_platform_attributes(void)
{
	/* remove secure platform sysfs entry and free key data*/

	kfree(bioscfg_drv.spm_data.endorsement_key);
	bioscfg_drv.spm_data.endorsement_key = NULL;

	kfree(bioscfg_drv.spm_data.signing_key);
	bioscfg_drv.spm_data.signing_key = NULL;

	kfree(bioscfg_drv.spm_data.auth_token);
	bioscfg_drv.spm_data.auth_token = NULL;

	if (bioscfg_drv.spm_data.attr_name_kobj)
		sysfs_remove_group(bioscfg_drv.spm_data.attr_name_kobj,
				   &secure_platform_attr_group);
}

int hp_populate_secure_platform_data(struct kobject *attr_name_kobj)
{
	/* Populate data for Secure Platform Management */
	bioscfg_drv.spm_data.attr_name_kobj = attr_name_kobj;

	strscpy(bioscfg_drv.spm_data.attribute_name, SPM_STR,
		sizeof(bioscfg_drv.spm_data.attribute_name));

	bioscfg_drv.spm_data.is_enabled = 0;
	bioscfg_drv.spm_data.mechanism = 0;
	bioscfg_drv.pending_reboot = false;
	update_spm_state();

	bioscfg_drv.spm_data.endorsement_key = NULL;
	bioscfg_drv.spm_data.signing_key = NULL;
	bioscfg_drv.spm_data.auth_token = NULL;

	return sysfs_create_group(attr_name_kobj, &secure_platform_attr_group);
}
