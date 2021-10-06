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
};

static struct ffa_drv_info *drv_info;

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

	if (ver.a0 < FFA_MIN_VERSION || ver.a0 > FFA_DRIVER_VERSION) {
		pr_err("Incompatible version %d.%d found\n",
		       MAJOR_VERSION(ver.a0), MINOR_VERSION(ver.a0));
		return -EINVAL;
	}

	*version = ver.a0;
	pr_info("Version %d.%d found\n", MAJOR_VERSION(ver.a0),
		MINOR_VERSION(ver.a0));
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

/* buffer must be sizeof(struct ffa_partition_info) * num_partitions */
static int
__ffa_partition_info_get(u32 uuid0, u32 uuid1, u32 uuid2, u32 uuid3,
			 struct ffa_partition_info *buffer, int num_partitions)
{
	int count;
	ffa_value_t partition_info;

	mutex_lock(&drv_info->rx_lock);
	invoke_ffa_fn((ffa_value_t){
		      .a0 = FFA_PARTITION_INFO_GET,
		      .a1 = uuid0, .a2 = uuid1, .a3 = uuid2, .a4 = uuid3,
		      }, &partition_info);

	if (partition_info.a0 == FFA_ERROR) {
		mutex_unlock(&drv_info->rx_lock);
		return ffa_to_linux_errno((int)partition_info.a2);
	}

	count = partition_info.a2;

	if (buffer && count <= num_partitions)
		memcpy(buffer, drv_info->rx_buffer, sizeof(*buffer) * count);

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

	if (ret.a0 != FFA_SUCCESS)
		return -EOPNOTSUPP;

	if (handle)
		*handle = PACK_HANDLE(ret.a2, ret.a3);

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

	if (ret.a0 != FFA_MEM_FRAG_RX)
		return -EOPNOTSUPP;

	return ret.a3;
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

	count = ffa_partition_probe(&uuid_null, &pbuf);
	if (count <= 0)
		return -ENOENT;

	memcpy(buffer, pbuf, sizeof(*pbuf) * count);
	kfree(pbuf);
	return 0;
}

static void ffa_mode_32bit_set(struct ffa_device *dev)
{
	dev->mode_32bit = true;
}

static int ffa_sync_send_receive(struct ffa_device *dev,
				 struct ffa_send_direct_data *data)
{
	return ffa_msg_send_direct_req(drv_info->vm_id, dev->vm_id,
				       dev->mode_32bit, data);
}

static int
ffa_memory_share(struct ffa_device *dev, struct ffa_mem_ops_args *args)
{
	if (dev->mode_32bit)
		return ffa_memory_ops(FFA_MEM_SHARE, args);

	return ffa_memory_ops(FFA_FN_NATIVE(MEM_SHARE), args);
}

static const struct ffa_dev_ops ffa_ops = {
	.api_version_get = ffa_api_version_get,
	.partition_info_get = ffa_partition_info_get,
	.mode_32bit_set = ffa_mode_32bit_set,
	.sync_send_receive = ffa_sync_send_receive,
	.memory_reclaim = ffa_memory_reclaim,
	.memory_share = ffa_memory_share,
};

const struct ffa_dev_ops *ffa_dev_ops_get(struct ffa_device *dev)
{
	if (ffa_device_is_valid(dev))
		return &ffa_ops;

	return NULL;
}
EXPORT_SYMBOL_GPL(ffa_dev_ops_get);

void ffa_device_match_uuid(struct ffa_device *ffa_dev, const uuid_t *uuid)
{
	int count, idx;
	struct ffa_partition_info *pbuf, *tpbuf;

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
	struct ffa_device *ffa_dev;
	struct ffa_partition_info *pbuf, *tpbuf;

	count = ffa_partition_probe(&uuid_null, &pbuf);
	if (count <= 0) {
		pr_info("%s: No partitions found, error %d\n", __func__, count);
		return;
	}

	for (idx = 0, tpbuf = pbuf; idx < count; idx++, tpbuf++) {
		/* Note that the &uuid_null parameter will require
		 * ffa_device_match() to find the UUID of this partition id
		 * with help of ffa_device_match_uuid(). Once the FF-A spec
		 * is updated to provide correct UUID here for each partition
		 * as part of the discovery API, we need to pass the
		 * discovered UUID here instead.
		 */
		ffa_dev = ffa_device_register(&uuid_null, tpbuf->id);
		if (!ffa_dev) {
			pr_err("%s: failed to register partition ID 0x%x\n",
			       __func__, tpbuf->id);
			continue;
		}

		ffa_dev_set_drvdata(ffa_dev, drv_info);
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
