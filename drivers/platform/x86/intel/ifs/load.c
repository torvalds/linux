// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2022 Intel Corporation. */

#include <linux/firmware.h>
#include <linux/sizes.h>
#include <asm/cpu.h>
#include <asm/microcode.h>

#include "ifs.h"

#define IFS_CHUNK_ALIGNMENT	256
union meta_data {
	struct {
		u32 meta_type;		// metadata type
		u32 meta_size;		// size of this entire struct including hdrs.
		u32 test_type;		// IFS test type
		u32 fusa_info;		// Fusa info
		u32 total_images;	// Total number of images
		u32 current_image;	// Current Image #
		u32 total_chunks;	// Total number of chunks in this image
		u32 starting_chunk;	// Starting chunk number in this image
		u32 size_per_chunk;	// size of each chunk
		u32 chunks_per_stride;	// number of chunks in a stride
	};
	u8 padding[IFS_CHUNK_ALIGNMENT];
};

#define IFS_HEADER_SIZE	(sizeof(struct microcode_header_intel))
#define META_TYPE_IFS	1
#define INVALIDATE_STRIDE	0x1UL
#define IFS_GEN_STRIDE_AWARE	2
#define AUTH_INTERRUPTED_ERROR	5
#define IFS_AUTH_RETRY_CT	10

static  struct microcode_header_intel *ifs_header_ptr;	/* pointer to the ifs image header */
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
	[2] = "Chunk authentication error. The hash of chunk did not match expected value",
	[3] = "Reserved",
	[4] = "Chunk outside the current stride",
	[5] = "Authentication flow interrupted",
};

#define MC_HEADER_META_TYPE_END		(0)

struct metadata_header {
	unsigned int		type;
	unsigned int		blk_size;
};

static struct metadata_header *find_meta_data(void *ucode, unsigned int meta_type)
{
	struct microcode_header_intel *hdr = &((struct microcode_intel *)ucode)->hdr;
	struct metadata_header *meta_header;
	unsigned long data_size, total_meta;
	unsigned long meta_size = 0;

	data_size = intel_microcode_get_datasize(hdr);
	total_meta = hdr->metasize;
	if (!total_meta)
		return NULL;

	meta_header = (ucode + MC_HEADER_SIZE + data_size) - total_meta;

	while (meta_header->type != MC_HEADER_META_TYPE_END &&
	       meta_header->blk_size &&
	       meta_size < total_meta) {
		meta_size += meta_header->blk_size;
		if (meta_header->type == meta_type)
			return meta_header;

		meta_header = (void *)meta_header + meta_header->blk_size;
	}
	return NULL;
}

static void hashcopy_err_message(struct device *dev, u32 err_code)
{
	if (err_code >= ARRAY_SIZE(scan_hash_status))
		dev_err(dev, "invalid error code 0x%x for hash copy\n", err_code);
	else
		dev_err(dev, "Hash copy error : %s\n", scan_hash_status[err_code]);
}

static void auth_err_message(struct device *dev, u32 err_code)
{
	if (err_code >= ARRAY_SIZE(scan_authentication_status))
		dev_err(dev, "invalid error code 0x%x for authentication\n", err_code);
	else
		dev_err(dev, "Chunk authentication error : %s\n",
			scan_authentication_status[err_code]);
}

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
		hashcopy_err_message(dev, err_code);
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
			auth_err_message(dev, err_code);
			goto done;
		}
	}
done:
	complete(&ifs_done);
}

static int get_num_chunks(int gen, union ifs_scan_hashes_status_gen2 status)
{
	return gen >= IFS_GEN_STRIDE_AWARE ? status.chunks_in_stride : status.num_chunks;
}

static bool need_copy_scan_hashes(struct ifs_data *ifsd)
{
	return !ifsd->loaded ||
		ifsd->generation < IFS_GEN_STRIDE_AWARE ||
		ifsd->loaded_version != ifs_header_ptr->rev;
}

static int copy_hashes_authenticate_chunks_gen2(struct device *dev)
{
	union ifs_scan_hashes_status_gen2 hashes_status;
	union ifs_chunks_auth_status_gen2 chunk_status;
	u32 err_code, valid_chunks, total_chunks;
	int i, num_chunks, chunk_size;
	union meta_data *ifs_meta;
	int starting_chunk_nr;
	struct ifs_data *ifsd;
	u64 linear_addr, base;
	u64 chunk_table[2];
	int retry_count;

	ifsd = ifs_get_data(dev);

	if (need_copy_scan_hashes(ifsd)) {
		wrmsrl(MSR_COPY_SCAN_HASHES, ifs_hash_ptr);
		rdmsrl(MSR_SCAN_HASHES_STATUS, hashes_status.data);

		/* enumerate the scan image information */
		chunk_size = hashes_status.chunk_size * SZ_1K;
		err_code = hashes_status.error_code;

		num_chunks = get_num_chunks(ifsd->generation, hashes_status);

		if (!hashes_status.valid) {
			hashcopy_err_message(dev, err_code);
			return -EIO;
		}
		ifsd->loaded_version = ifs_header_ptr->rev;
		ifsd->chunk_size = chunk_size;
	} else {
		num_chunks = ifsd->valid_chunks;
		chunk_size = ifsd->chunk_size;
	}

	if (ifsd->generation >= IFS_GEN_STRIDE_AWARE) {
		wrmsrl(MSR_SAF_CTRL, INVALIDATE_STRIDE);
		rdmsrl(MSR_CHUNKS_AUTHENTICATION_STATUS, chunk_status.data);
		if (chunk_status.valid_chunks != 0) {
			dev_err(dev, "Couldn't invalidate installed stride - %d\n",
				chunk_status.valid_chunks);
			return -EIO;
		}
	}

	base = ifs_test_image_ptr;
	ifs_meta = (union meta_data *)find_meta_data(ifs_header_ptr, META_TYPE_IFS);
	starting_chunk_nr = ifs_meta->starting_chunk;

	/* scan data authentication and copy chunks to secured memory */
	for (i = 0; i < num_chunks; i++) {
		retry_count = IFS_AUTH_RETRY_CT;
		linear_addr = base + i * chunk_size;

		chunk_table[0] = starting_chunk_nr + i;
		chunk_table[1] = linear_addr;
		do {
			wrmsrl(MSR_AUTHENTICATE_AND_COPY_CHUNK, (u64)chunk_table);
			rdmsrl(MSR_CHUNKS_AUTHENTICATION_STATUS, chunk_status.data);
			err_code = chunk_status.error_code;
		} while (err_code == AUTH_INTERRUPTED_ERROR && --retry_count);

		if (err_code) {
			ifsd->loading_error = true;
			auth_err_message(dev, err_code);
			return -EIO;
		}
	}

	valid_chunks = chunk_status.valid_chunks;
	total_chunks = chunk_status.total_chunks;

	if (valid_chunks != total_chunks) {
		ifsd->loading_error = true;
		dev_err(dev, "Couldn't authenticate all the chunks. Authenticated %d total %d.\n",
			valid_chunks, total_chunks);
		return -EIO;
	}
	ifsd->valid_chunks = valid_chunks;

	return 0;
}

static int validate_ifs_metadata(struct device *dev)
{
	struct ifs_data *ifsd = ifs_get_data(dev);
	union meta_data *ifs_meta;
	char test_file[64];
	int ret = -EINVAL;

	snprintf(test_file, sizeof(test_file), "%02x-%02x-%02x-%02x.scan",
		 boot_cpu_data.x86, boot_cpu_data.x86_model,
		 boot_cpu_data.x86_stepping, ifsd->cur_batch);

	ifs_meta = (union meta_data *)find_meta_data(ifs_header_ptr, META_TYPE_IFS);
	if (!ifs_meta) {
		dev_err(dev, "IFS Metadata missing in file %s\n", test_file);
		return ret;
	}

	ifs_test_image_ptr = (u64)ifs_meta + sizeof(union meta_data);

	/* Scan chunk start must be 256 byte aligned */
	if (!IS_ALIGNED(ifs_test_image_ptr, IFS_CHUNK_ALIGNMENT)) {
		dev_err(dev, "Scan pattern is not aligned on %d bytes aligned in %s\n",
			IFS_CHUNK_ALIGNMENT, test_file);
		return ret;
	}

	if (ifs_meta->current_image != ifsd->cur_batch) {
		dev_warn(dev, "Mismatch between filename %s and batch metadata 0x%02x\n",
			 test_file, ifs_meta->current_image);
		return ret;
	}

	if (ifs_meta->chunks_per_stride &&
	    (ifs_meta->starting_chunk % ifs_meta->chunks_per_stride != 0)) {
		dev_warn(dev, "Starting chunk num %u not a multiple of chunks_per_stride %u\n",
			 ifs_meta->starting_chunk, ifs_meta->chunks_per_stride);
		return ret;
	}

	return 0;
}

/*
 * IFS requires scan chunks authenticated per each socket in the platform.
 * Once the test chunk is authenticated, it is automatically copied to secured memory
 * and proceed the authentication for the next chunk.
 */
static int scan_chunks_sanity_check(struct device *dev)
{
	struct ifs_data *ifsd = ifs_get_data(dev);
	struct ifs_work local_work;
	int curr_pkg, cpu, ret;

	memset(ifs_pkg_auth, 0, (topology_max_packages() * sizeof(bool)));
	ret = validate_ifs_metadata(dev);
	if (ret)
		return ret;

	ifsd->loading_error = false;

	if (ifsd->generation > 0)
		return copy_hashes_authenticate_chunks_gen2(dev);

	/* copy the scan hash and authenticate per package */
	cpus_read_lock();
	for_each_online_cpu(cpu) {
		curr_pkg = topology_physical_package_id(cpu);
		if (ifs_pkg_auth[curr_pkg])
			continue;
		reinit_completion(&ifs_done);
		local_work.dev = dev;
		INIT_WORK_ONSTACK(&local_work.w, copy_hashes_authenticate_chunks);
		schedule_work_on(cpu, &local_work.w);
		wait_for_completion(&ifs_done);
		if (ifsd->loading_error) {
			ret = -EIO;
			goto out;
		}
		ifs_pkg_auth[curr_pkg] = 1;
	}
	ret = 0;
	ifsd->loaded_version = ifs_header_ptr->rev;
out:
	cpus_read_unlock();

	return ret;
}

static int image_sanity_check(struct device *dev, const struct microcode_header_intel *data)
{
	struct cpu_signature sig;

	/* Provide a specific error message when loading an older/unsupported image */
	if (data->hdrver != MC_HEADER_TYPE_IFS) {
		dev_err(dev, "Header version %d not supported\n", data->hdrver);
		return -EINVAL;
	}

	if (intel_microcode_sanity_check((void *)data, true, MC_HEADER_TYPE_IFS)) {
		dev_err(dev, "sanity check failed\n");
		return -EINVAL;
	}

	intel_collect_cpu_info(&sig);

	if (!intel_find_matching_signature((void *)data, &sig)) {
		dev_err(dev, "cpu signature, processor flags not matching\n");
		return -EINVAL;
	}

	return 0;
}

/*
 * Load ifs image. Before loading ifs module, the ifs image must be located
 * in /lib/firmware/intel/ifs_x/ and named as family-model-stepping-02x.{testname}.
 */
int ifs_load_firmware(struct device *dev)
{
	const struct ifs_test_caps *test = ifs_get_test_caps(dev);
	struct ifs_data *ifsd = ifs_get_data(dev);
	unsigned int expected_size;
	const struct firmware *fw;
	char scan_path[64];
	int ret;

	snprintf(scan_path, sizeof(scan_path), "intel/ifs_%d/%02x-%02x-%02x-%02x.scan",
		 test->test_num, boot_cpu_data.x86, boot_cpu_data.x86_model,
		 boot_cpu_data.x86_stepping, ifsd->cur_batch);

	ret = request_firmware_direct(&fw, scan_path, dev);
	if (ret) {
		dev_err(dev, "ifs file %s load failed\n", scan_path);
		goto done;
	}

	expected_size = ((struct microcode_header_intel *)fw->data)->totalsize;
	if (fw->size != expected_size) {
		dev_err(dev, "File size mismatch (expected %u, actual %zu). Corrupted IFS image.\n",
			expected_size, fw->size);
		ret = -EINVAL;
		goto release;
	}

	ret = image_sanity_check(dev, (struct microcode_header_intel *)fw->data);
	if (ret)
		goto release;

	ifs_header_ptr = (struct microcode_header_intel *)fw->data;
	ifs_hash_ptr = (u64)(ifs_header_ptr + 1);

	ret = scan_chunks_sanity_check(dev);
	if (ret)
		dev_err(dev, "Load failure for batch: %02x\n", ifsd->cur_batch);

release:
	release_firmware(fw);
done:
	ifsd->loaded = (ret == 0);

	return ret;
}
