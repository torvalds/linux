// SPDX-License-Identifier: GPL-2.0-only
/*
 * POWER Platform specific code for non-volatile SED key access
 * Copyright (C) 2022 IBM Corporation
 *
 * Define operations for SED Opal to read/write keys
 * from POWER LPAR Platform KeyStore(PLPKS).
 *
 * Self Encrypting Drives(SED) key storage using PLPKS
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/ioctl.h>
#include <linux/sed-opal-key.h>
#include <asm/plpks.h>

static bool plpks_sed_initialized = false;
static bool plpks_sed_available = false;

/*
 * structure that contains all SED data
 */
struct plpks_sed_object_data {
	u_char version;
	u_char pad1[7];
	u_long authority;
	u_long range;
	u_int  key_len;
	u_char key[32];
};

#define PLPKS_SED_OBJECT_DATA_V0        0
#define PLPKS_SED_MANGLED_LABEL         "/default/pri"
#define PLPKS_SED_COMPONENT             "sed-opal"
#define PLPKS_SED_KEY                   "opal-boot-pin"

/*
 * authority is admin1 and range is global
 */
#define PLPKS_SED_AUTHORITY  0x0000000900010001
#define PLPKS_SED_RANGE      0x0000080200000001

static void plpks_init_var(struct plpks_var *var, char *keyname)
{
	if (!plpks_sed_initialized) {
		plpks_sed_initialized = true;
		plpks_sed_available = plpks_is_available();
		if (!plpks_sed_available)
			pr_err("SED: plpks not available\n");
	}

	var->name = keyname;
	var->namelen = strlen(keyname);
	if (strcmp(PLPKS_SED_KEY, keyname) == 0) {
		var->name = PLPKS_SED_MANGLED_LABEL;
		var->namelen = strlen(keyname);
	}
	var->policy = PLPKS_WORLDREADABLE;
	var->os = PLPKS_VAR_COMMON;
	var->data = NULL;
	var->datalen = 0;
	var->component = PLPKS_SED_COMPONENT;
}

/*
 * Read the SED Opal key from PLPKS given the label
 */
int sed_read_key(char *keyname, char *key, u_int *keylen)
{
	struct plpks_var var;
	struct plpks_sed_object_data data;
	int ret;
	u_int len;

	plpks_init_var(&var, keyname);

	if (!plpks_sed_available)
		return -EOPNOTSUPP;

	var.data = (u8 *)&data;
	var.datalen = sizeof(data);

	ret = plpks_read_os_var(&var);
	if (ret != 0)
		return ret;

	len = min_t(u16, be32_to_cpu(data.key_len), var.datalen);
	memcpy(key, data.key, len);
	key[len] = '\0';
	*keylen = len;

	return 0;
}

/*
 * Write the SED Opal key to PLPKS given the label
 */
int sed_write_key(char *keyname, char *key, u_int keylen)
{
	struct plpks_var var;
	struct plpks_sed_object_data data;
	struct plpks_var_name vname;

	plpks_init_var(&var, keyname);

	if (!plpks_sed_available)
		return -EOPNOTSUPP;

	var.datalen = sizeof(struct plpks_sed_object_data);
	var.data = (u8 *)&data;

	/* initialize SED object */
	data.version = PLPKS_SED_OBJECT_DATA_V0;
	data.authority = cpu_to_be64(PLPKS_SED_AUTHORITY);
	data.range = cpu_to_be64(PLPKS_SED_RANGE);
	memset(&data.pad1, '\0', sizeof(data.pad1));
	data.key_len = cpu_to_be32(keylen);
	memcpy(data.key, (char *)key, keylen);

	/*
	 * Key update requires remove first. The return value
	 * is ignored since it's okay if the key doesn't exist.
	 */
	vname.namelen = var.namelen;
	vname.name = var.name;
	plpks_remove_var(var.component, var.os, vname);

	return plpks_write_var(var);
}
