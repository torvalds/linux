// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2022 Intel Corporation. */

#include <linux/firmware.h>
#include <asm/cpu.h>
#include <asm/microcode_intel.h>

#include "ifs.h"

static int ifs_sanity_check(struct device *dev,
			    const struct microcode_header_intel *mc_header)
{
	unsigned long total_size, data_size;
	u32 sum, *mc;

	total_size = get_totalsize(mc_header);
	data_size = get_datasize(mc_header);

	if ((data_size + MC_HEADER_SIZE > total_size) || (total_size % sizeof(u32))) {
		dev_err(dev, "bad ifs data file size.\n");
		return -EINVAL;
	}

	if (mc_header->ldrver != 1 || mc_header->hdrver != 1) {
		dev_err(dev, "invalid/unknown ifs update format.\n");
		return -EINVAL;
	}

	mc = (u32 *)mc_header;
	sum = 0;
	for (int i = 0; i < total_size / sizeof(u32); i++)
		sum += mc[i];

	if (sum) {
		dev_err(dev, "bad ifs data checksum, aborting.\n");
		return -EINVAL;
	}

	return 0;
}

static bool find_ifs_matching_signature(struct device *dev, struct ucode_cpu_info *uci,
					const struct microcode_header_intel *shdr)
{
	unsigned int mc_size;

	mc_size = get_totalsize(shdr);

	if (!mc_size || ifs_sanity_check(dev, shdr) < 0) {
		dev_err(dev, "ifs sanity check failure\n");
		return false;
	}

	if (!intel_cpu_signatures_match(uci->cpu_sig.sig, uci->cpu_sig.pf, shdr->sig, shdr->pf)) {
		dev_err(dev, "ifs signature, pf not matching\n");
		return false;
	}

	return true;
}

static bool ifs_image_sanity_check(struct device *dev, const struct microcode_header_intel *data)
{
	struct ucode_cpu_info uci;

	intel_cpu_collect_info(&uci);

	return find_ifs_matching_signature(dev, &uci, data);
}

/*
 * Load ifs image. Before loading ifs module, the ifs image must be located
 * in /lib/firmware/intel/ifs and named as {family/model/stepping}.{testname}.
 */
void ifs_load_firmware(struct device *dev)
{
	const struct firmware *fw;
	char scan_path[32];
	int ret;

	snprintf(scan_path, sizeof(scan_path), "intel/ifs/%02x-%02x-%02x.scan",
		 boot_cpu_data.x86, boot_cpu_data.x86_model, boot_cpu_data.x86_stepping);

	ret = request_firmware_direct(&fw, scan_path, dev);
	if (ret) {
		dev_err(dev, "ifs file %s load failed\n", scan_path);
		return;
	}

	if (!ifs_image_sanity_check(dev, (struct microcode_header_intel *)fw->data))
		dev_err(dev, "ifs header sanity check failed\n");

	release_firmware(fw);
}
