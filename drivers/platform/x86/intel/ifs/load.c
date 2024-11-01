// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2022 Intel Corporation. */

#include <linux/firmware.h>
#include <asm/cpu.h>
#include <linux/slab.h>
#include <asm/microcode_intel.h>

#include "ifs.h"

struct ifs_header {
	u32 header_ver;
	u32 blob_revision;
	u32 date;
	u32 processor_sig;
	u32 check_sum;
	u32 loader_rev;
	u32 processor_flags;
	u32 metadata_size;
	u32 total_size;
	u32 fusa_info;
	u64 reserved;
};

#define IFS_HEADER_SIZE	(sizeof(struct ifs_header))
static struct ifs_header *ifs_header_ptr;	/* pointer to the ifs image header */
static u64 ifs_hash_ptr;			/* Address of ifs metadata (hash) */
static u64 ifs_test_image_ptr;			/* 256B aligned address of test pattern */
static DECLARE_COMPLETION(ifs_done);

static const char * const scan_hash_status[] = {
	[0] = "No error reported",
	[1] = "Attempt to copy scan hashes when copy already in progress",
	[2] = "Secure Memory not set up correctly",
	[3] = "FuSaInfo.ProgramID does not match or ff-mm-ss does not match",
	[4] = "Reserved",
	[5] = "Integrity check failed",
	[6] = "Scan reload or test is in progress"
};

static const char * const scan_authentication_status[] = {
	[0] = "No error reported",
	[1] = "Attempt to authenticate a chunk which is already marked as authentic",
	[2] = "Chunk authentication error. The hash of chunk did not match expected value"
};

/*
 * To copy scan hashes and authenticate test chunks, the initiating cpu must point
 * to the EDX:EAX to the test image in linear address.
 * Run wrmsr(MSR_COPY_SCAN_HASHES) for scan hash copy and run wrmsr(MSR_AUTHENTICATE_AND_COPY_CHUNK)
 * for scan hash copy and test chunk authentication.
 */
static void copy_hashes_authenticate_chunks(struct work_struct *work)
{
	struct ifs_work *local_work = container_of(work, struct ifs_work, w);
	union ifs_scan_hashes_status hashes_status;
	union ifs_chunks_auth_status chunk_status;
	struct device *dev = local_work->dev;
	int i, num_chunks, chunk_size;
	struct ifs_data *ifsd;
	u64 linear_addr, base;
	u32 err_code;

	ifsd = ifs_get_data(dev);
	/* run scan hash copy */
	wrmsrl(MSR_COPY_SCAN_HASHES, ifs_hash_ptr);
	rdmsrl(MSR_SCAN_HASHES_STATUS, hashes_status.data);

	/* enumerate the scan image information */
	num_chunks = hashes_status.num_chunks;
	chunk_size = hashes_status.chunk_size * 1024;
	err_code = hashes_status.error_code;

	if (!hashes_status.valid) {
		ifsd->loading_error = true;
		if (err_code >= ARRAY_SIZE(scan_hash_status)) {
			dev_err(dev, "invalid error code 0x%x for hash copy\n", err_code);
			goto done;
		}
		dev_err(dev, "Hash copy error : %s", scan_hash_status[err_code]);
		goto done;
	}

	/* base linear address to the scan data */
	base = ifs_test_image_ptr;

	/* scan data authentication and copy chunks to secured memory */
	for (i = 0; i < num_chunks; i++) {
		linear_addr = base + i * chunk_size;
		linear_addr |= i;

		wrmsrl(MSR_AUTHENTICATE_AND_COPY_CHUNK, linear_addr);
		rdmsrl(MSR_CHUNKS_AUTHENTICATION_STATUS, chunk_status.data);

		ifsd->valid_chunks = chunk_status.valid_chunks;
		err_code = chunk_status.error_code;

		if (err_code) {
			ifsd->loading_error = true;
			if (err_code >= ARRAY_SIZE(scan_authentication_status)) {
				dev_err(dev,
					"invalid error code 0x%x for authentication\n", err_code);
				goto done;
			}
			dev_err(dev, "Chunk authentication error %s\n",
				scan_authentication_status[err_code]);
			goto done;
		}
	}
done:
	complete(&ifs_done);
}

/*
 * IFS requires scan chunks authenticated per each socket in the platform.
 * Once the test chunk is authenticated, it is automatically copied to secured memory
 * and proceed the authentication for the next chunk.
 */
static int scan_chunks_sanity_check(struct device *dev)
{
	int metadata_size, curr_pkg, cpu, ret = -ENOMEM;
	struct ifs_data *ifsd = ifs_get_data(dev);
	bool *package_authenticated;
	struct ifs_work local_work;
	char *test_ptr;

	package_authenticated = kcalloc(topology_max_packages(), sizeof(bool), GFP_KERNEL);
	if (!package_authenticated)
		return ret;

	metadata_size = ifs_header_ptr->metadata_size;

	/* Spec says that if the Meta Data Size = 0 then it should be treated as 2000 */
	if (metadata_size == 0)
		metadata_size = 2000;

	/* Scan chunk start must be 256 byte aligned */
	if ((metadata_size + IFS_HEADER_SIZE) % 256) {
		dev_err(dev, "Scan pattern offset within the binary is not 256 byte aligned\n");
		return -EINVAL;
	}

	test_ptr = (char *)ifs_header_ptr + IFS_HEADER_SIZE + metadata_size;
	ifsd->loading_error = false;

	ifs_test_image_ptr = (u64)test_ptr;
	ifsd->loaded_version = ifs_header_ptr->blob_revision;

	/* copy the scan hash and authenticate per package */
	cpus_read_lock();
	for_each_online_cpu(cpu) {
		curr_pkg = topology_physical_package_id(cpu);
		if (package_authenticated[curr_pkg])
			continue;
		reinit_completion(&ifs_done);
		local_work.dev = dev;
		INIT_WORK_ONSTACK(&local_work.w, copy_hashes_authenticate_chunks);
		schedule_work_on(cpu, &local_work.w);
		wait_for_completion(&ifs_done);
		if (ifsd->loading_error)
			goto out;
		package_authenticated[curr_pkg] = 1;
	}
	ret = 0;
out:
	cpus_read_unlock();
	kfree(package_authenticated);

	return ret;
}

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
	struct ifs_data *ifsd = ifs_get_data(dev);
	const struct firmware *fw;
	char scan_path[32];
	int ret;

	snprintf(scan_path, sizeof(scan_path), "intel/ifs/%02x-%02x-%02x.scan",
		 boot_cpu_data.x86, boot_cpu_data.x86_model, boot_cpu_data.x86_stepping);

	ret = request_firmware_direct(&fw, scan_path, dev);
	if (ret) {
		dev_err(dev, "ifs file %s load failed\n", scan_path);
		goto done;
	}

	if (!ifs_image_sanity_check(dev, (struct microcode_header_intel *)fw->data)) {
		dev_err(dev, "ifs header sanity check failed\n");
		goto release;
	}

	ifs_header_ptr = (struct ifs_header *)fw->data;
	ifs_hash_ptr = (u64)(ifs_header_ptr + 1);

	ret = scan_chunks_sanity_check(dev);
release:
	release_firmware(fw);
done:
	ifsd->loaded = (ret == 0);
}
