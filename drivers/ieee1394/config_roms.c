/*
 * IEEE 1394 for Linux
 *
 * ConfigROM  entries
 *
 * Copyright (C) 2004 Ben Collins
 *
 * This code is licensed under the GPL.  See the file COPYING in the root
 * directory of the kernel sources for details.
 */

#include <linux/types.h>

#include "csr1212.h"
#include "ieee1394.h"
#include "ieee1394_types.h"
#include "hosts.h"
#include "ieee1394_core.h"
#include "highlevel.h"
#include "csr.h"
#include "config_roms.h"

struct hpsb_config_rom_entry {
	const char *name;

	/* Base initialization, called at module load */
	int (*init)(void);

	/* Add entry to specified host */
	int (*add)(struct hpsb_host *host);

	/* Remove entry from specified host */
	void (*remove)(struct hpsb_host *host);

	/* Cleanup called at module exit */
	void (*cleanup)(void);

	/* The flag added to host->config_roms */
	unsigned int flag;
};


int hpsb_default_host_entry(struct hpsb_host *host)
{
	struct csr1212_keyval *root;
	struct csr1212_keyval *vend_id = NULL;
	struct csr1212_keyval *text = NULL;
	char csr_name[128];
	int ret;

	sprintf(csr_name, "Linux - %s", host->driver->name);
	root = host->csr.rom->root_kv;

	vend_id = csr1212_new_immediate(CSR1212_KV_ID_VENDOR, host->csr.guid_hi >> 8);
	text = csr1212_new_string_descriptor_leaf(csr_name);

	if (!vend_id || !text) {
		if (vend_id)
			csr1212_release_keyval(vend_id);
		if (text)
			csr1212_release_keyval(text);
		csr1212_destroy_csr(host->csr.rom);
		return -ENOMEM;
	}

	ret = csr1212_associate_keyval(vend_id, text);
	csr1212_release_keyval(text);
	ret |= csr1212_attach_keyval_to_directory(root, vend_id);
	csr1212_release_keyval(vend_id);
	if (ret != CSR1212_SUCCESS) {
		csr1212_destroy_csr(host->csr.rom);
		return -ENOMEM;
	}

	host->update_config_rom = 1;

	return 0;
}


#ifdef CONFIG_IEEE1394_CONFIG_ROM_IP1394
#include "eth1394.h"

static struct csr1212_keyval *ip1394_ud;

static int config_rom_ip1394_init(void)
{
	struct csr1212_keyval *spec_id = NULL;
	struct csr1212_keyval *spec_desc = NULL;
	struct csr1212_keyval *ver = NULL;
	struct csr1212_keyval *ver_desc = NULL;
	int ret = -ENOMEM;

	ip1394_ud = csr1212_new_directory(CSR1212_KV_ID_UNIT);

	spec_id = csr1212_new_immediate(CSR1212_KV_ID_SPECIFIER_ID,
					ETHER1394_GASP_SPECIFIER_ID);
	spec_desc = csr1212_new_string_descriptor_leaf("IANA");
	ver = csr1212_new_immediate(CSR1212_KV_ID_VERSION,
				    ETHER1394_GASP_VERSION);
	ver_desc = csr1212_new_string_descriptor_leaf("IPv4");

	if (!ip1394_ud || !spec_id || !spec_desc || !ver || !ver_desc)
		goto ip1394_fail;

	if (csr1212_associate_keyval(spec_id, spec_desc) == CSR1212_SUCCESS &&
	    csr1212_associate_keyval(ver, ver_desc) == CSR1212_SUCCESS &&
	    csr1212_attach_keyval_to_directory(ip1394_ud, spec_id) == CSR1212_SUCCESS &&
	    csr1212_attach_keyval_to_directory(ip1394_ud, ver) == CSR1212_SUCCESS)
		ret = 0;

ip1394_fail:
	if (ret && ip1394_ud) {
		csr1212_release_keyval(ip1394_ud);
		ip1394_ud = NULL;
	}

	if (spec_id)
		csr1212_release_keyval(spec_id);
	if (spec_desc)
		csr1212_release_keyval(spec_desc);
	if (ver)
		csr1212_release_keyval(ver);
	if (ver_desc)
		csr1212_release_keyval(ver_desc);

	return ret;
}

static void config_rom_ip1394_cleanup(void)
{
	if (ip1394_ud) {
		csr1212_release_keyval(ip1394_ud);
		ip1394_ud = NULL;
	}
}

static int config_rom_ip1394_add(struct hpsb_host *host)
{
	if (!ip1394_ud)
		return -ENODEV;

	if (csr1212_attach_keyval_to_directory(host->csr.rom->root_kv,
					       ip1394_ud) != CSR1212_SUCCESS)
		return -ENOMEM;

	return 0;
}

static void config_rom_ip1394_remove(struct hpsb_host *host)
{
	csr1212_detach_keyval_from_directory(host->csr.rom->root_kv, ip1394_ud);
}

static struct hpsb_config_rom_entry ip1394_entry = {
	.name		= "ip1394",
	.init		= config_rom_ip1394_init,
	.add		= config_rom_ip1394_add,
	.remove		= config_rom_ip1394_remove,
	.cleanup	= config_rom_ip1394_cleanup,
	.flag		= HPSB_CONFIG_ROM_ENTRY_IP1394,
};
#endif /* CONFIG_IEEE1394_CONFIG_ROM_IP1394 */


static struct hpsb_config_rom_entry *const config_rom_entries[] = {
#ifdef CONFIG_IEEE1394_CONFIG_ROM_IP1394
	&ip1394_entry,
#endif
	NULL,
};


int hpsb_init_config_roms(void)
{
	int i, error = 0;

	for (i = 0; config_rom_entries[i]; i++) {
		if (!config_rom_entries[i]->init)
			continue;

		if (config_rom_entries[i]->init()) {
			HPSB_ERR("Failed to initialize config rom entry `%s'",
				 config_rom_entries[i]->name);
			error = -1;
		} else
			HPSB_DEBUG("Initialized config rom entry `%s'",
				   config_rom_entries[i]->name);
	}

	return error;
}

void hpsb_cleanup_config_roms(void)
{
	int i;

	for (i = 0; config_rom_entries[i]; i++) {
		if (config_rom_entries[i]->cleanup)
			config_rom_entries[i]->cleanup();
	}
}

int hpsb_add_extra_config_roms(struct hpsb_host *host)
{
	int i, error = 0;

	for (i = 0; config_rom_entries[i]; i++) {
		if (config_rom_entries[i]->add(host)) {
			HPSB_ERR("fw-host%d: Failed to attach config rom entry `%s'",
				 host->id, config_rom_entries[i]->name);
			error = -1;
		} else {
			host->config_roms |= config_rom_entries[i]->flag;
			host->update_config_rom = 1;
		}
	}

	return error;
}

void hpsb_remove_extra_config_roms(struct hpsb_host *host)
{
	int i;

	for (i = 0; config_rom_entries[i]; i++) {
		if (!(host->config_roms & config_rom_entries[i]->flag))
			continue;

		config_rom_entries[i]->remove(host);

		host->config_roms &= ~config_rom_entries[i]->flag;
		host->update_config_rom = 1;
	}
}
