/* adi_64.c: support for ADI (Application Data Integrity) feature on
 * sparc m7 and newer processors. This feature is also known as
 * SSM (Silicon Secured Memory).
 *
 * Copyright (C) 2016 Oracle and/or its affiliates. All rights reserved.
 * Author: Khalid Aziz (khalid.aziz@oracle.com)
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 */
#include <linux/init.h>
#include <asm/mdesc.h>
#include <asm/adi_64.h>

struct adi_config adi_state;

/* mdesc_adi_init() : Parse machine description provided by the
 *	hypervisor to detect ADI capabilities
 *
 * Hypervisor reports ADI capabilities of platform in "hwcap-list" property
 * for "cpu" node. If the platform supports ADI, "hwcap-list" property
 * contains the keyword "adp". If the platform supports ADI, "platform"
 * node will contain "adp-blksz", "adp-nbits" and "ue-on-adp" properties
 * to describe the ADI capabilities.
 */
void __init mdesc_adi_init(void)
{
	struct mdesc_handle *hp = mdesc_grab();
	const char *prop;
	u64 pn, *val;
	int len;

	if (!hp)
		goto adi_not_found;

	pn = mdesc_node_by_name(hp, MDESC_NODE_NULL, "cpu");
	if (pn == MDESC_NODE_NULL)
		goto adi_not_found;

	prop = mdesc_get_property(hp, pn, "hwcap-list", &len);
	if (!prop)
		goto adi_not_found;

	/*
	 * Look for "adp" keyword in hwcap-list which would indicate
	 * ADI support
	 */
	adi_state.enabled = false;
	while (len) {
		int plen;

		if (!strcmp(prop, "adp")) {
			adi_state.enabled = true;
			break;
		}

		plen = strlen(prop) + 1;
		prop += plen;
		len -= plen;
	}

	if (!adi_state.enabled)
		goto adi_not_found;

	/* Find the ADI properties in "platform" node. If all ADI
	 * properties are not found, ADI support is incomplete and
	 * do not enable ADI in the kernel.
	 */
	pn = mdesc_node_by_name(hp, MDESC_NODE_NULL, "platform");
	if (pn == MDESC_NODE_NULL)
		goto adi_not_found;

	val = (u64 *) mdesc_get_property(hp, pn, "adp-blksz", &len);
	if (!val)
		goto adi_not_found;
	adi_state.caps.blksz = *val;

	val = (u64 *) mdesc_get_property(hp, pn, "adp-nbits", &len);
	if (!val)
		goto adi_not_found;
	adi_state.caps.nbits = *val;

	val = (u64 *) mdesc_get_property(hp, pn, "ue-on-adp", &len);
	if (!val)
		goto adi_not_found;
	adi_state.caps.ue_on_adi = *val;

	mdesc_release(hp);
	return;

adi_not_found:
	adi_state.enabled = false;
	adi_state.caps.blksz = 0;
	adi_state.caps.nbits = 0;
	if (hp)
		mdesc_release(hp);
}
