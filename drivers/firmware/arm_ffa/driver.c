// SPDX-License-Identifier: GPL-2.0-only
/*
 * Arm Firmware Framework for ARMv8-A(FFA) interface driver
 *
 * The Arm FFA specification[1] describes a software architecture to
 * leverages the virtualization extension to isolate software images
 * provided by an ecosystem of vendors from each other and describes
 * interfaces that standardize communication between the various software
 * images including communication between images in the Secure world and
 * Normal world. Any Hypervisor could use the FFA interfaces to enable
 * communication between VMs it manages.
 *
 * The Hypervisor a.k.a Partition managers in FFA terminology can assign
 * system resources(Memory regions, Devices, CPU cycles) to the partitions
 * and manage isolation amongst them.
 *
 * [1] https://developer.arm.com/docs/den0077/latest
 *
 * Copyright (C) 2021 ARM Ltd.
 */

#define DRIVER_NAME "ARM FF-A"
#define pr_fmt(fmt) DRIVER_NAME ": " fmt

#include <linux/arm_ffa.h>
#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/uuid.h>

#include "common.h"

#define FFA_DRIVER_VERSION	FFA_VERSION_1_0

#define FFA_SMC(calling_convention, func_num)				\
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL, (calling_convention),	\
			   ARM_SMCCC_OWNER_STANDARD, (func_num))

#define FFA_SMC_32(func_num)	FFA_SMC(ARM_SMCCC_SMC_32, (func_num))
#define FFA_SMC_64(func_num)	FFA_SMC(ARM_SMCCC_SMC_64, (func_num))

#define FFA_ERROR			FFA_SMC_32(0x60)
#define FFA_SUCCESS			FFA_SMC_32(0x61)
#define FFA_INTERRUPT			FFA_SMC_32(0x62)
#define FFA_VERSION			FFA_SMC_32(0x63)
#define FFA_FEATURES			FFA_SMC_32(0x64)
#define FFA_RX_RELEASE			FFA_SMC_32(0x65)
#define FFA_RXTX_MAP			FFA_SMC_32(0x66)
#define FFA_FN64_RXTX_MAP		FFA_SMC_64(0x66)
#define FFA_RXTX_UNMAP			FFA_SMC_32(0x67)
#define FFA_PARTITION_INFO_GET		FFA_SMC_32(0x68)
#define FFA_ID_GET			FFA_SMC_32(0x69)
#define FFA_MSG_POLL			FFA_SMC_32(0x6A)
#define FFA_MSG_WAIT			FFA_SMC_32(0x6B)
#define FFA_YIELD			FFA_SMC_32(0x6C)
#define FFA_RUN				FFA_SMC_32(0x6D)
#define FFA_MSG_SEND			FFA_SMC_32(0x6E)
#define FFA_MSG_SEND_DIRECT_REQ		FFA_SMC_32(0x6F)
#define FFA_FN64_MSG_SEND_DIRECT_REQ	FFA_SMC_64(0x6F)
#define FFA_MSG_SEND_DIRECT_RESP	FFA_SMC_32(0x70)
#define FFA_FN64_MSG_SEND_DIRECT_RESP	FFA_SMC_64(0x70)
#define FFA_MEM_DONATE			FFA_SMC_32(0x71)
#define FFA_FN64_MEM_DONATE		FFA_SMC_64(0x71)
#define FFA_MEM_LEND			FFA_SMC_32(0x72)
#define FFA_FN64_MEM_LEND		FFA_SMC_64(0x72)
#define FFA_MEM_SHARE			FFA_SMC_32(0x73)
#define FFA_FN64_MEM_SHARE		FFA_SMC_64(0x73)
#define FFA_MEM_RETRIEVE_REQ		FFA_SMC_32(0x74)
#define FFA_FN64_MEM_RETRIEVE_REQ	FFA_SMC_64(0x74)
#define FFA_MEM_RETRIEVE_RESP		FFA_SMC_32(0x75)
#define FFA_MEM_RELINQUISH		FFA_SMC_32(0x76)
#define FFA_MEM_RECLAIM			FFA_SMC_32(0x77)
#define FFA_MEM_OP_PAUSE		FFA_SMC_32(0x78)
#define FFA_MEM_OP_RESUME		FFA_SMC_32(0x79)
#define FFA_MEM_FRAG_RX			FFA_SMC_32(0x7A)
#define FFA_MEM_FRAG_TX			FFA_SMC_32(0x7B)
#define FFA_NORMAL_WORLD_RESUME		FFA_SMC_32(0x7C)

/*
 * For some calls it is necessary to use SMC64 to pass or return 64-bit values.
 * For such calls FFA_FN_NATIVE(name) will choose the appropriate
 * (native-width) function ID.
 */
#ifdef CONFIG_64BIT
#define FFA_FN_NATIVE(name)	FFA_FN64_##name
#else
#define FFA_FN_NATIVE(name)	FFA_##name
#endif

/* FFA error codes. */
#define FFA_RET_SUCCESS            (0)
#define FFA_RET_NOT_SUPPORTED      (-1)
#define FFA_RET_INVALID_PARAMETERS (-2)
#define FFA_RET_NO_MEMORY          (-3)
#define FFA_RET_BUSY               (-4)
#define FFA_RET_INTERRUPTED        (-5)
#define FFA_RET_DENIED             (-6)
#define FFA_RET_RETRY              (-7)
#define FFA_RET_ABORTED            (-8)

#define MAJOR_VERSION_MASK	GENMASK(30, 16)
#define MINOR_VERSION_MASK	GENMASK(15, 0)
#define MAJOR_VERSION(x)	((u16)(FIELD_GET(MAJOR_VERSION_MASK, (x))))
#define MINOR_VERSION(x)	((u16)(FIELD_GET(MINOR_VERSION_MASK, (x))))
#define PACK_VERSION_INFO(major, minor)			\
	(FIELD_PREP(MAJOR_VERSION_MASK, (major)) |	\
	 FIELD_PREP(MINOR_VERSION_MASK, (minor)))
#define FFA_VERSION_1_0		PACK_VERSION_INFO(1, 0)
#define FFA_MIN_VERSION		FFA_VERSION_1_0

#define SENDER_ID_MASK		GENMASK(31, 16)
#define RECEIVER_ID_MASK	GENMASK(15, 0)
#define SENDER_ID(x)		((u16)(FIELD_GET(SENDER_ID_MASK, (x))))
#define RECEIVER_ID(x)		((u16)(FIELD_GET(RECEIVER_ID_MASK, (x))))
#define PACK_TARGET_INFO(s, r)		\
	(FIELD_PREP(SENDER_ID_MASK, (s)) | FIELD_PREP(RECEIVER_ID_MASK, (r)))

/*
 * FF-A specification mentions explicitly about '4K pages'. This should
 * not be confused with the kernel PAGE_SIZE, which is the translation
 * granule kernel is configured and may be one among 4K, 16K and 64K.
 */
#define FFA_PAGE_SIZE		SZ_4K
/*
 * Keeping RX TX buffer size as 4K for now
 * 64K may be preferred to keep it min a page in 64K PAGE_SIZE config
 */
#define RXTX_BUFFER_SIZE	SZ_4K

static ffa_fn *invoke_ffa_fn;

static const int ffa_linux_errmap[] = {
	/* better than switch case as long as return value is continuous */
	0,		/* FFA_RET_SUCCESS */
	-EOPNOTSUPP,	/* FFA_RET_NOT_SUPPORTED */
	-EINVAL,	/* FFA_RET_INVALID_PARAMETERS */
	-ENOMEM,	/* FFA_RET_NO_MEMORY */
	-EBUSY,		/* FFA_RET_BUSY */
	-EINTR,		/* FFA_RET_INTERRUPTED */
	-EACCES,	/* FFA_RET_DENIED */
	-EAGAIN,	/* FFA_RET_RETRY */
	-ECANCELED,	/* FFA_RET_ABORTED */
};

static inline int ffa_to_linux_errno(int errno)
{
	int err_idx = -errno;

	if (err_idx >= 0 && err_idx < ARRAY_SIZE(ffa_linux_errmap))
		return ffa_linux_errmap[err_idx];
	return -EINVAL;
}

struct ffa_drv_info {
	u32 version;
	u16 vm_id;
	struct mutex rx_lock; /* lock to protect Rx buffer */
	struct mutex tx_lock; /* lock to protect Tx buffer */
	void *rx_buffer;
	void *tx_buffer;
	bool mem_ops_native;
};

static struct ffa_drv_info *drv_info;

/*
 * The driver must be able to support all the versions from the earliest
 * supported FFA_MIN_VERSION to the latest supported FFA_DRIVER_VERSION.
 * The specification states that if firmware supports a FFA implementation
 * that is incompatible with and at a greater version number than specified
 * by the caller(FFA_DRIVER_VERSION passed as parameter to FFA_VERSION),
 * it must return the NOT_SUPPORTED error code.
 */
static u32 ffa_compatible_version_find(u32 version)
{
	u16 major = MAJOR_VERSION(version), minor = MINOR_VERSION(version);
	u16 drv_major = MAJOR_VERSION(FFA_DRIVER_VERSION);
	u16 drv_minor = MINOR_VERSION(FFA_DRIVER_VERSION);

	if ((major < drv_major) || (major == drv_major && minor <= drv_minor))
		return version;

	pr_info("Firmware version higher than driver version, downgrading\n");
	return FFA_DRIVER_VERSION;
}

static int ffa_version_check(u32 *version)
{
	ffa_value_t ver;

	invoke_ffa_fn((ffa_value_t){
		      .a0 = FFA_VERSION, .a1 = FFA_DRIVER_VERSION,
		      }, &ver);

	if (ver.a0 == FFA_RET_NOT_SUPPORTED) {
		pr_info("FFA_VERSION returned not supported\n");
		return -EOPNOTSUPP;
	}

	if (ver.a0 < FFA_MIN_VERSION) {
		pr_err("Incompatible v%d.%d! Earliest supported v%d.%d\n",
		       MAJOR_VERSION(ver.a0), MINOR_VERSION(ver.a0),
		       MAJOR_VERSION(FFA_MIN_VERSION),
		       MINOR_VERSION(FFA_MIN_VERSION));
		return -EINVAL;
	}

	pr_info("Driver version %d.%d\n", MAJOR_VERSION(FFA_DRIVER_VERSION),
		MINOR_VERSION(FFA_DRIVER_VERSION));
	pr_info("Firmware version %d.%d found\n", MAJOR_VERSION(ver.a0),
		MINOR_VERSION(ver.a0));
	*version = ffa_compatible_version_find(ver.a0);

	return 0;
}

static int ffa_rx_release(void)
{
	ffa_value_t ret;

	invoke_ffa_fn((ffa_value_t){
		      .a0 = FFA_RX_RELEASE,
		      }, &ret);

	if (ret.a0 == FFA_ERROR)
		return ffa_to_linux_errno((int)ret.a2);

	/* check for ret.a0 == FFA_RX_RELEASE ? */

	return 0;
}

static int ffa_rxtx_map(phys_addr_t tx_buf, phys_addr_t rx_buf, u32 pg_cnt)
{
	ffa_value_t ret;

	invoke_ffa_fn((ffa_value_t){
		      .a0 = FFA_FN_NATIVE(RXTX_MAP),
		      .a1 = tx_buf, .a2 = rx_buf, .a3 = pg_cnt,
		      }, &ret);

	if (ret.a0 == FFA_ERROR)
		return ffa_to_linux_errno((int)ret.a2);

	return 0;
}

static int ffa_rxtx_unmap(u16 vm_id)
{
	ffa_value_t ret;

	invoke_ffa_fn((ffa_value_t){
		      .a0 = FFA_RXTX_UNMAP, .a1 = PACK_TARGET_INFO(vm_id, 0),
		      }, &ret);

	if (ret.a0 == FFA_ERROR)
		return ffa_to_linux_errno((int)ret.a2);

	return 0;
}

#define PARTITION_INFO_GET_RETURN_COUNT_ONLY	BIT(0)

/* buffer must be sizeof(struct ffa_partition_info) * num_partitions */
static int
__ffa_partition_info_get(u32 uuid0, u32 uuid1, u32 uuid2, u32 uuid3,
			 struct ffa_partition_info *buffer, int num_partitions)
{
	int idx, count, flags = 0, sz, buf_sz;
	ffa_value_t partition_info;

	if (!buffer || !num_partitions) /* Just get the count for now */
		flags = PARTITION_INFO_GET_RETURN_COUNT_ONLY;

	mutex_lock(&drv_info->rx_lock);
	invoke_ffa_fn((ffa_value_t){
		      .a0 = FFA_PARTITION_INFO_GET,
		      .a1 = uuid0, .a2 = uuid1, .a3 = uuid2, .a4 = uuid3,
		      .a5 = flags,
		      }, &partition_info);

	if (partition_info.a0 == FFA_ERROR) {
		mutex_unlock(&drv_info->rx_lock);
		return ffa_to_linux_errno((int)partition_info.a2);
	}

	count = partition_info.a2;

	if (drv_info->version > FFA_VERSION_1_0) {
		buf_sz = sz = partition_info.a3;
		if (sz > sizeof(*buffer))
			buf_sz = sizeof(*buffer);
	} else {
		/* FFA_VERSION_1_0 lacks size in the response */
		buf_sz = sz = 8;
	}

	if (buffer && count <= num_partitions)
		for (idx = 0; idx < count; idx++)
			memcpy(buffer + idx, drv_info->rx_buffer + idx * sz,
			       buf_sz);

	ffa_rx_release();

	mutex_unlock(&drv_info->rx_lock);

	return count;
}

/* buffer is allocated and caller must free the same if returned count > 0 */
static int
ffa_partition_probe(const uuid_t *uuid, struct ffa_partition_info **buffer)
{
	int count;
	u32 uuid0_4[4];
	struct ffa_partition_info *pbuf;

	export_uuid((u8 *)uuid0_4, uuid);
	count = __ffa_partition_info_get(uuid0_4[0], uuid0_4[1], uuid0_4[2],
					 uuid0_4[3], NULL, 0);
	if (count <= 0)
		return count;

	pbuf = kcalloc(count, sizeof(*pbuf), GFP_KERNEL);
	if (!pbuf)
		return -ENOMEM;

	count = __ffa_partition_info_get(uuid0_4[0], uuid0_4[1], uuid0_4[2],
					 uuid0_4[3], pbuf, count);
	if (count <= 0)
		kfree(pbuf);
	else
		*buffer = pbuf;

	return count;
}

#define VM_ID_MASK	GENMASK(15, 0)
static int ffa_id_get(u16 *vm_id)
{
	ffa_value_t id;

	invoke_ffa_fn((ffa_value_t){
		      .a0 = FFA_ID_GET,
		      }, &id);

	if (id.a0 == FFA_ERROR)
		return ffa_to_linux_errno((int)id.a2);

	*vm_id = FIELD_GET(VM_ID_MASK, (id.a2));

	return 0;
}

static int ffa_msg_send_direct_req(u16 src_id, u16 dst_id, bool mode_32bit,
				   struct ffa_send_direct_data *data)
{
	u32 req_id, resp_id, src_dst_ids = PACK_TARGET_INFO(src_id, dst_id);
	ffa_value_t ret;

	if (mode_32bit) {
		req_id = FFA_MSG_SEND_DIRECT_REQ;
		resp_id = FFA_MSG_SEND_DIRECT_RESP;
	} else {
		req_id = FFA_FN_NATIVE(MSG_SEND_DIRECT_REQ);
		resp_id = FFA_FN_NATIVE(MSG_SEND_DIRECT_RESP);
	}

	invoke_ffa_fn((ffa_value_t){
		      .a0 = req_id, .a1 = src_dst_ids, .a2 = 0,
		      .a3 = data->data0, .a4 = data->data1, .a5 = data->data2,
		      .a6 = data->data3, .a7 = data->data4,
		      }, &ret);

	while (ret.a0 == FFA_INTERRUPT)
		invoke_ffa_fn((ffa_value_t){
			      .a0 = FFA_RUN, .a1 = ret.a1,
			      }, &ret);

	if (ret.a0 == FFA_ERROR)
		return ffa_to_linux_errno((int)ret.a2);

	if (ret.a0 == resp_id) {
		data->data0 = ret.a3;
		data->data1 = ret.a4;
		data->data2 = ret.a5;
		data->data3 = ret.a6;
		data->data4 = ret.a7;
		return 0;
	}

	return -EINVAL;
}

static int ffa_mem_first_frag(u32 func_id, phys_addr_t buf, u32 buf_sz,
			      u32 frag_len, u32 len, u64 *handle)
{
	ffa_value_t ret;

	invoke_ffa_fn((ffa_value_t){
		      .a0 = func_id, .a1 = len, .a2 = frag_len,
		      .a3 = buf, .a4 = buf_sz,
		      }, &ret);

	while (ret.a0 == FFA_MEM_OP_PAUSE)
		invoke_ffa_fn((ffa_value_t){
			      .a0 = FFA_MEM_OP_RESUME,
			      .a1 = ret.a1, .a2 = ret.a2,
			      }, &ret);

	if (ret.a0 == FFA_ERROR)
		return ffa_to_linux_errno((int)ret.a2);

	if (ret.a0 == FFA_SUCCESS) {
		if (handle)
			*handle = PACK_HANDLE(ret.a2, ret.a3);
	} else if (ret.a0 == FFA_MEM_FRAG_RX) {
		if (handle)
			*handle = PACK_HANDLE(ret.a1, ret.a2);
	} else {
		return -EOPNOTSUPP;
	}

	return frag_len;
}

static int ffa_mem_next_frag(u64 handle, u32 frag_len)
{
	ffa_value_t ret;

	invoke_ffa_fn((ffa_value_t){
		      .a0 = FFA_MEM_FRAG_TX,
		      .a1 = HANDLE_LOW(handle), .a2 = HANDLE_HIGH(handle),
		      .a3 = frag_len,
		      }, &ret);

	while (ret.a0 == FFA_MEM_OP_PAUSE)
		invoke_ffa_fn((ffa_value_t){
			      .a0 = FFA_MEM_OP_RESUME,
			      .a1 = ret.a1, .a2 = ret.a2,
			      }, &ret);

	if (ret.a0 == FFA_ERROR)
		return ffa_to_linux_errno((int)ret.a2);

	if (ret.a0 == FFA_MEM_FRAG_RX)
		return ret.a3;
	else if (ret.a0 == FFA_SUCCESS)
		return 0;

	return -EOPNOTSUPP;
}

static int
ffa_transmit_fragment(u32 func_id, phys_addr_t buf, u32 buf_sz, u32 frag_len,
		      u32 len, u64 *handle, bool first)
{
	if (!first)
		return ffa_mem_next_frag(*handle, frag_len);

	return ffa_mem_first_frag(func_id, buf, buf_sz, frag_len, len, handle);
}

static u32 ffa_get_num_pages_sg(struct scatterlist *sg)
{
	u32 num_pages = 0;

	do {
		num_pages += sg->length / FFA_PAGE_SIZE;
	} while ((sg = sg_next(sg)));

	return num_pages;
}

static int
ffa_setup_and_transmit(u32 func_id, void *buffer, u32 max_fragsize,
		       struct ffa_mem_ops_args *args)
{
	int rc = 0;
	bool first = true;
	phys_addr_t addr = 0;
	struct ffa_composite_mem_region *composite;
	struct ffa_mem_region_addr_range *constituents;
	struct ffa_mem_region_attributes *ep_mem_access;
	struct ffa_mem_region *mem_region = buffer;
	u32 idx, frag_len, length, buf_sz = 0, num_entries = sg_nents(args->sg);

	mem_region->tag = args->tag;
	mem_region->flags = args->flags;
	mem_region->sender_id = drv_info->vm_id;
	mem_region->attributes = FFA_MEM_NORMAL | FFA_MEM_WRITE_BACK |
				 FFA_MEM_INNER_SHAREABLE;
	ep_mem_access = &mem_region->ep_mem_access[0];

	for (idx = 0; idx < args->nattrs; idx++, ep_mem_access++) {
		ep_mem_access->receiver = args->attrs[idx].receiver;
		ep_mem_access->attrs = args->attrs[idx].attrs;
		ep_mem_access->composite_off = COMPOSITE_OFFSET(args->nattrs);
	}
	mem_region->ep_count = args->nattrs;

	composite = buffer + COMPOSITE_OFFSET(args->nattrs);
	composite->total_pg_cnt = ffa_get_num_pages_sg(args->sg);
	composite->addr_range_cnt = num_entries;

	length = COMPOSITE_CONSTITUENTS_OFFSET(args->nattrs, num_entries);
	frag_len = COMPOSITE_CONSTITUENTS_OFFSET(args->nattrs, 0);
	if (frag_len > max_fragsize)
		return -ENXIO;

	if (!args->use_txbuf) {
		addr = virt_to_phys(buffer);
		buf_sz = max_fragsize / FFA_PAGE_SIZE;
	}

	constituents = buffer + frag_len;
	idx = 0;
	do {
		if (frag_len == max_fragsize) {
			rc = ffa_transmit_fragment(func_id, addr, buf_sz,
						   frag_len, length,
						   &args->g_handle, first);
			if (rc < 0)
				return -ENXIO;

			first = false;
			idx = 0;
			frag_len = 0;
			constituents = buffer;
		}

		if ((void *)constituents - buffer > max_fragsize) {
			pr_err("Memory Region Fragment > Tx Buffer size\n");
			return -EFAULT;
		}

		constituents->address = sg_phys(args->sg);
		constituents->pg_cnt = args->sg->length / FFA_PAGE_SIZE;
		constituents++;
		frag_len += sizeof(struct ffa_mem_region_addr_range);
	} while ((args->sg = sg_next(args->sg)));

	return ffa_transmit_fragment(func_id, addr, buf_sz, frag_len,
				     length, &args->g_handle, first);
}

static int ffa_memory_ops(u32 func_id, struct ffa_mem_ops_args *args)
{
	int ret;
	void *buffer;

	if (!args->use_txbuf) {
		buffer = alloc_pages_exact(RXTX_BUFFER_SIZE, GFP_KERNEL);
		if (!buffer)
			return -ENOMEM;
	} else {
		buffer = drv_info->tx_buffer;
		mutex_lock(&drv_info->tx_lock);
	}

	ret = ffa_setup_and_transmit(func_id, buffer, RXTX_BUFFER_SIZE, args);

	if (args->use_txbuf)
		mutex_unlock(&drv_info->tx_lock);
	else
		free_pages_exact(buffer, RXTX_BUFFER_SIZE);

	return ret < 0 ? ret : 0;
}

static int ffa_memory_reclaim(u64 g_handle, u32 flags)
{
	ffa_value_t ret;

	invoke_ffa_fn((ffa_value_t){
		      .a0 = FFA_MEM_RECLAIM,
		      .a1 = HANDLE_LOW(g_handle), .a2 = HANDLE_HIGH(g_handle),
		      .a3 = flags,
		      }, &ret);

	if (ret.a0 == FFA_ERROR)
		return ffa_to_linux_errno((int)ret.a2);

	return 0;
}

static int ffa_features(u32 func_feat_id, u32 input_props,
			u32 *if_props_1, u32 *if_props_2)
{
	ffa_value_t id;

	if (!ARM_SMCCC_IS_FAST_CALL(func_feat_id) && input_props) {
		pr_err("%s: Invalid Parameters: %x, %x", __func__,
		       func_feat_id, input_props);
		return ffa_to_linux_errno(FFA_RET_INVALID_PARAMETERS);
	}

	invoke_ffa_fn((ffa_value_t){
		.a0 = FFA_FEATURES, .a1 = func_feat_id, .a2 = input_props,
		}, &id);

	if (id.a0 == FFA_ERROR)
		return ffa_to_linux_errno((int)id.a2);

	if (if_props_1)
		*if_props_1 = id.a2;
	if (if_props_2)
		*if_props_2 = id.a3;

	return 0;
}

static void ffa_set_up_mem_ops_native_flag(void)
{
	if (!ffa_features(FFA_FN_NATIVE(MEM_LEND), 0, NULL, NULL) ||
	    !ffa_features(FFA_FN_NATIVE(MEM_SHARE), 0, NULL, NULL))
		drv_info->mem_ops_native = true;
}

static u32 ffa_api_version_get(void)
{
	return drv_info->version;
}

static int ffa_partition_info_get(const char *uuid_str,
				  struct ffa_partition_info *buffer)
{
	int count;
	uuid_t uuid;
	struct ffa_partition_info *pbuf;

	if (uuid_parse(uuid_str, &uuid)) {
		pr_err("invalid uuid (%s)\n", uuid_str);
		return -ENODEV;
	}

	count = ffa_partition_probe(&uuid, &pbuf);
	if (count <= 0)
		return -ENOENT;

	memcpy(buffer, pbuf, sizeof(*pbuf) * count);
	kfree(pbuf);
	return 0;
}

static void _ffa_mode_32bit_set(struct ffa_device *dev)
{
	dev->mode_32bit = true;
}

static void ffa_mode_32bit_set(struct ffa_device *dev)
{
	if (drv_info->version > FFA_VERSION_1_0)
		return;

	_ffa_mode_32bit_set(dev);
}

static int ffa_sync_send_receive(struct ffa_device *dev,
				 struct ffa_send_direct_data *data)
{
	return ffa_msg_send_direct_req(drv_info->vm_id, dev->vm_id,
				       dev->mode_32bit, data);
}

static int ffa_memory_share(struct ffa_mem_ops_args *args)
{
	if (drv_info->mem_ops_native)
		return ffa_memory_ops(FFA_FN_NATIVE(MEM_SHARE), args);

	return ffa_memory_ops(FFA_MEM_SHARE, args);
}

static int ffa_memory_lend(struct ffa_mem_ops_args *args)
{
	/* Note that upon a successful MEM_LEND request the caller
	 * must ensure that the memory region specified is not accessed
	 * until a successful MEM_RECALIM call has been made.
	 * On systems with a hypervisor present this will been enforced,
	 * however on systems without a hypervisor the responsibility
	 * falls to the calling kernel driver to prevent access.
	 */
	if (drv_info->mem_ops_native)
		return ffa_memory_ops(FFA_FN_NATIVE(MEM_LEND), args);

	return ffa_memory_ops(FFA_MEM_LEND, args);
}

static const struct ffa_info_ops ffa_drv_info_ops = {
	.api_version_get = ffa_api_version_get,
	.partition_info_get = ffa_partition_info_get,
};

static const struct ffa_msg_ops ffa_drv_msg_ops = {
	.mode_32bit_set = ffa_mode_32bit_set,
	.sync_send_receive = ffa_sync_send_receive,
};

static const struct ffa_mem_ops ffa_drv_mem_ops = {
	.memory_reclaim = ffa_memory_reclaim,
	.memory_share = ffa_memory_share,
	.memory_lend = ffa_memory_lend,
};

static const struct ffa_ops ffa_drv_ops = {
	.info_ops = &ffa_drv_info_ops,
	.msg_ops = &ffa_drv_msg_ops,
	.mem_ops = &ffa_drv_mem_ops,
};

void ffa_device_match_uuid(struct ffa_device *ffa_dev, const uuid_t *uuid)
{
	int count, idx;
	struct ffa_partition_info *pbuf, *tpbuf;

	/*
	 * FF-A v1.1 provides UUID for each partition as part of the discovery
	 * API, the discovered UUID must be populated in the device's UUID and
	 * there is no need to copy the same from the driver table.
	 */
	if (drv_info->version > FFA_VERSION_1_0)
		return;

	count = ffa_partition_probe(uuid, &pbuf);
	if (count <= 0)
		return;

	for (idx = 0, tpbuf = pbuf; idx < count; idx++, tpbuf++)
		if (tpbuf->id == ffa_dev->vm_id)
			uuid_copy(&ffa_dev->uuid, uuid);
	kfree(pbuf);
}

static void ffa_setup_partitions(void)
{
	int count, idx;
	uuid_t uuid;
	struct ffa_device *ffa_dev;
	struct ffa_partition_info *pbuf, *tpbuf;

	count = ffa_partition_probe(&uuid_null, &pbuf);
	if (count <= 0) {
		pr_info("%s: No partitions found, error %d\n", __func__, count);
		return;
	}

	for (idx = 0, tpbuf = pbuf; idx < count; idx++, tpbuf++) {
		import_uuid(&uuid, (u8 *)tpbuf->uuid);

		/* Note that if the UUID will be uuid_null, that will require
		 * ffa_device_match() to find the UUID of this partition id
		 * with help of ffa_device_match_uuid(). FF-A v1.1 and above
		 * provides UUID here for each partition as part of the
		 * discovery API and the same is passed.
		 */
		ffa_dev = ffa_device_register(&uuid, tpbuf->id, &ffa_drv_ops);
		if (!ffa_dev) {
			pr_err("%s: failed to register partition ID 0x%x\n",
			       __func__, tpbuf->id);
			continue;
		}

		if (drv_info->version > FFA_VERSION_1_0 &&
		    !(tpbuf->properties & FFA_PARTITION_AARCH64_EXEC))
			_ffa_mode_32bit_set(ffa_dev);
	}
	kfree(pbuf);
}

static int __init ffa_init(void)
{
	int ret;

	ret = ffa_transport_init(&invoke_ffa_fn);
	if (ret)
		return ret;

	ret = arm_ffa_bus_init();
	if (ret)
		return ret;

	drv_info = kzalloc(sizeof(*drv_info), GFP_KERNEL);
	if (!drv_info) {
		ret = -ENOMEM;
		goto ffa_bus_exit;
	}

	ret = ffa_version_check(&drv_info->version);
	if (ret)
		goto free_drv_info;

	if (ffa_id_get(&drv_info->vm_id)) {
		pr_err("failed to obtain VM id for self\n");
		ret = -ENODEV;
		goto free_drv_info;
	}

	drv_info->rx_buffer = alloc_pages_exact(RXTX_BUFFER_SIZE, GFP_KERNEL);
	if (!drv_info->rx_buffer) {
		ret = -ENOMEM;
		goto free_pages;
	}

	drv_info->tx_buffer = alloc_pages_exact(RXTX_BUFFER_SIZE, GFP_KERNEL);
	if (!drv_info->tx_buffer) {
		ret = -ENOMEM;
		goto free_pages;
	}

	ret = ffa_rxtx_map(virt_to_phys(drv_info->tx_buffer),
			   virt_to_phys(drv_info->rx_buffer),
			   RXTX_BUFFER_SIZE / FFA_PAGE_SIZE);
	if (ret) {
		pr_err("failed to register FFA RxTx buffers\n");
		goto free_pages;
	}

	mutex_init(&drv_info->rx_lock);
	mutex_init(&drv_info->tx_lock);

	ffa_setup_partitions();

	ffa_set_up_mem_ops_native_flag();

	return 0;
free_pages:
	if (drv_info->tx_buffer)
		free_pages_exact(drv_info->tx_buffer, RXTX_BUFFER_SIZE);
	free_pages_exact(drv_info->rx_buffer, RXTX_BUFFER_SIZE);
free_drv_info:
	kfree(drv_info);
ffa_bus_exit:
	arm_ffa_bus_exit();
	return ret;
}
subsys_initcall(ffa_init);

static void __exit ffa_exit(void)
{
	ffa_rxtx_unmap(drv_info->vm_id);
	free_pages_exact(drv_info->tx_buffer, RXTX_BUFFER_SIZE);
	free_pages_exact(drv_info->rx_buffer, RXTX_BUFFER_SIZE);
	kfree(drv_info);
	arm_ffa_bus_exit();
}
module_exit(ffa_exit);

MODULE_ALIAS("arm-ffa");
MODULE_AUTHOR("Sudeep Holla <sudeep.holla@arm.com>");
MODULE_DESCRIPTION("Arm FF-A interface driver");
MODULE_LICENSE("GPL v2");
