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

#include <linux/acpi.h>
#include <linux/arm_ffa.h>
#include <linux/bitfield.h>
#include <linux/cpuhotplug.h>
#include <linux/device.h>
#include <linux/hashtable.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/of_irq.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/uuid.h>
#include <linux/xarray.h>

#include "common.h"

#define FFA_DRIVER_VERSION	FFA_VERSION_1_1
#define FFA_MIN_VERSION		FFA_VERSION_1_0

#define SENDER_ID_MASK		GENMASK(31, 16)
#define RECEIVER_ID_MASK	GENMASK(15, 0)
#define SENDER_ID(x)		((u16)(FIELD_GET(SENDER_ID_MASK, (x))))
#define RECEIVER_ID(x)		((u16)(FIELD_GET(RECEIVER_ID_MASK, (x))))
#define PACK_TARGET_INFO(s, r)		\
	(FIELD_PREP(SENDER_ID_MASK, (s)) | FIELD_PREP(RECEIVER_ID_MASK, (r)))

/*
 * Keeping RX TX buffer size as 4K for now
 * 64K may be preferred to keep it min a page in 64K PAGE_SIZE config
 */
#define RXTX_BUFFER_SIZE	SZ_4K

#define FFA_MAX_NOTIFICATIONS		64

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
	-ENODATA,	/* FFA_RET_NO_DATA */
};

static inline int ffa_to_linux_errno(int errno)
{
	int err_idx = -errno;

	if (err_idx >= 0 && err_idx < ARRAY_SIZE(ffa_linux_errmap))
		return ffa_linux_errmap[err_idx];
	return -EINVAL;
}

struct ffa_pcpu_irq {
	struct ffa_drv_info *info;
};

struct ffa_drv_info {
	u32 version;
	u16 vm_id;
	struct mutex rx_lock; /* lock to protect Rx buffer */
	struct mutex tx_lock; /* lock to protect Tx buffer */
	void *rx_buffer;
	void *tx_buffer;
	bool mem_ops_native;
	bool bitmap_created;
	bool notif_enabled;
	unsigned int sched_recv_irq;
	unsigned int cpuhp_state;
	struct ffa_pcpu_irq __percpu *irq_pcpu;
	struct workqueue_struct *notif_pcpu_wq;
	struct work_struct notif_pcpu_work;
	struct work_struct irq_work;
	struct xarray partition_info;
	DECLARE_HASHTABLE(notifier_hash, ilog2(FFA_MAX_NOTIFICATIONS));
	struct mutex notify_lock; /* lock to protect notifier hashtable  */
};

static struct ffa_drv_info *drv_info;
static void ffa_partitions_cleanup(void);

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
	u16 major = FFA_MAJOR_VERSION(version), minor = FFA_MINOR_VERSION(version);
	u16 drv_major = FFA_MAJOR_VERSION(FFA_DRIVER_VERSION);
	u16 drv_minor = FFA_MINOR_VERSION(FFA_DRIVER_VERSION);

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
		       FFA_MAJOR_VERSION(ver.a0), FFA_MINOR_VERSION(ver.a0),
		       FFA_MAJOR_VERSION(FFA_MIN_VERSION),
		       FFA_MINOR_VERSION(FFA_MIN_VERSION));
		return -EINVAL;
	}

	pr_info("Driver version %d.%d\n", FFA_MAJOR_VERSION(FFA_DRIVER_VERSION),
		FFA_MINOR_VERSION(FFA_DRIVER_VERSION));
	pr_info("Firmware version %d.%d found\n", FFA_MAJOR_VERSION(ver.a0),
		FFA_MINOR_VERSION(ver.a0));
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

	if (drv_info->version > FFA_VERSION_1_0 &&
	    (!buffer || !num_partitions)) /* Just get the count for now */
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

static u16 ffa_memory_attributes_get(u32 func_id)
{
	/*
	 * For the memory lend or donate operation, if the receiver is a PE or
	 * a proxy endpoint, the owner/sender must not specify the attributes
	 */
	if (func_id == FFA_FN_NATIVE(MEM_LEND) ||
	    func_id == FFA_MEM_LEND)
		return 0;

	return FFA_MEM_NORMAL | FFA_MEM_WRITE_BACK | FFA_MEM_INNER_SHAREABLE;
}

static int
ffa_setup_and_transmit(u32 func_id, void *buffer, u32 max_fragsize,
		       struct ffa_mem_ops_args *args)
{
	int rc = 0;
	bool first = true;
	u32 composite_offset;
	phys_addr_t addr = 0;
	struct ffa_mem_region *mem_region = buffer;
	struct ffa_composite_mem_region *composite;
	struct ffa_mem_region_addr_range *constituents;
	struct ffa_mem_region_attributes *ep_mem_access;
	u32 idx, frag_len, length, buf_sz = 0, num_entries = sg_nents(args->sg);

	mem_region->tag = args->tag;
	mem_region->flags = args->flags;
	mem_region->sender_id = drv_info->vm_id;
	mem_region->attributes = ffa_memory_attributes_get(func_id);
	ep_mem_access = buffer +
			ffa_mem_desc_offset(buffer, 0, drv_info->version);
	composite_offset = ffa_mem_desc_offset(buffer, args->nattrs,
					       drv_info->version);

	for (idx = 0; idx < args->nattrs; idx++, ep_mem_access++) {
		ep_mem_access->receiver = args->attrs[idx].receiver;
		ep_mem_access->attrs = args->attrs[idx].attrs;
		ep_mem_access->composite_off = composite_offset;
		ep_mem_access->flag = 0;
		ep_mem_access->reserved = 0;
	}
	mem_region->handle = 0;
	mem_region->ep_count = args->nattrs;
	if (drv_info->version <= FFA_VERSION_1_0) {
		mem_region->ep_mem_size = 0;
	} else {
		mem_region->ep_mem_size = sizeof(*ep_mem_access);
		mem_region->ep_mem_offset = sizeof(*mem_region);
		memset(mem_region->reserved, 0, 12);
	}

	composite = buffer + composite_offset;
	composite->total_pg_cnt = ffa_get_num_pages_sg(args->sg);
	composite->addr_range_cnt = num_entries;
	composite->reserved = 0;

	length = composite_offset + CONSTITUENTS_OFFSET(num_entries);
	frag_len = composite_offset + CONSTITUENTS_OFFSET(0);
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
		constituents->reserved = 0;
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

static int ffa_notification_bitmap_create(void)
{
	ffa_value_t ret;
	u16 vcpu_count = nr_cpu_ids;

	invoke_ffa_fn((ffa_value_t){
		      .a0 = FFA_NOTIFICATION_BITMAP_CREATE,
		      .a1 = drv_info->vm_id, .a2 = vcpu_count,
		      }, &ret);

	if (ret.a0 == FFA_ERROR)
		return ffa_to_linux_errno((int)ret.a2);

	return 0;
}

static int ffa_notification_bitmap_destroy(void)
{
	ffa_value_t ret;

	invoke_ffa_fn((ffa_value_t){
		      .a0 = FFA_NOTIFICATION_BITMAP_DESTROY,
		      .a1 = drv_info->vm_id,
		      }, &ret);

	if (ret.a0 == FFA_ERROR)
		return ffa_to_linux_errno((int)ret.a2);

	return 0;
}

#define NOTIFICATION_LOW_MASK		GENMASK(31, 0)
#define NOTIFICATION_HIGH_MASK		GENMASK(63, 32)
#define NOTIFICATION_BITMAP_HIGH(x)	\
		((u32)(FIELD_GET(NOTIFICATION_HIGH_MASK, (x))))
#define NOTIFICATION_BITMAP_LOW(x)	\
		((u32)(FIELD_GET(NOTIFICATION_LOW_MASK, (x))))
#define PACK_NOTIFICATION_BITMAP(low, high)	\
	(FIELD_PREP(NOTIFICATION_LOW_MASK, (low)) | \
	 FIELD_PREP(NOTIFICATION_HIGH_MASK, (high)))

#define RECEIVER_VCPU_MASK		GENMASK(31, 16)
#define PACK_NOTIFICATION_GET_RECEIVER_INFO(vcpu_r, r) \
	(FIELD_PREP(RECEIVER_VCPU_MASK, (vcpu_r)) | \
	 FIELD_PREP(RECEIVER_ID_MASK, (r)))

#define NOTIFICATION_INFO_GET_MORE_PEND_MASK	BIT(0)
#define NOTIFICATION_INFO_GET_ID_COUNT		GENMASK(11, 7)
#define ID_LIST_MASK_64				GENMASK(51, 12)
#define ID_LIST_MASK_32				GENMASK(31, 12)
#define MAX_IDS_64				20
#define MAX_IDS_32				10

#define PER_VCPU_NOTIFICATION_FLAG		BIT(0)
#define SECURE_PARTITION_BITMAP			BIT(0)
#define NON_SECURE_VM_BITMAP			BIT(1)
#define SPM_FRAMEWORK_BITMAP			BIT(2)
#define NS_HYP_FRAMEWORK_BITMAP			BIT(3)

static int ffa_notification_bind_common(u16 dst_id, u64 bitmap,
					u32 flags, bool is_bind)
{
	ffa_value_t ret;
	u32 func, src_dst_ids = PACK_TARGET_INFO(dst_id, drv_info->vm_id);

	func = is_bind ? FFA_NOTIFICATION_BIND : FFA_NOTIFICATION_UNBIND;

	invoke_ffa_fn((ffa_value_t){
		  .a0 = func, .a1 = src_dst_ids, .a2 = flags,
		  .a3 = NOTIFICATION_BITMAP_LOW(bitmap),
		  .a4 = NOTIFICATION_BITMAP_HIGH(bitmap),
		  }, &ret);

	if (ret.a0 == FFA_ERROR)
		return ffa_to_linux_errno((int)ret.a2);
	else if (ret.a0 != FFA_SUCCESS)
		return -EINVAL;

	return 0;
}

static
int ffa_notification_set(u16 src_id, u16 dst_id, u32 flags, u64 bitmap)
{
	ffa_value_t ret;
	u32 src_dst_ids = PACK_TARGET_INFO(dst_id, src_id);

	invoke_ffa_fn((ffa_value_t) {
		  .a0 = FFA_NOTIFICATION_SET, .a1 = src_dst_ids, .a2 = flags,
		  .a3 = NOTIFICATION_BITMAP_LOW(bitmap),
		  .a4 = NOTIFICATION_BITMAP_HIGH(bitmap),
		  }, &ret);

	if (ret.a0 == FFA_ERROR)
		return ffa_to_linux_errno((int)ret.a2);
	else if (ret.a0 != FFA_SUCCESS)
		return -EINVAL;

	return 0;
}

struct ffa_notify_bitmaps {
	u64 sp_map;
	u64 vm_map;
	u64 arch_map;
};

static int ffa_notification_get(u32 flags, struct ffa_notify_bitmaps *notify)
{
	ffa_value_t ret;
	u16 src_id = drv_info->vm_id;
	u16 cpu_id = smp_processor_id();
	u32 rec_vcpu_ids = PACK_NOTIFICATION_GET_RECEIVER_INFO(cpu_id, src_id);

	invoke_ffa_fn((ffa_value_t){
		  .a0 = FFA_NOTIFICATION_GET, .a1 = rec_vcpu_ids, .a2 = flags,
		  }, &ret);

	if (ret.a0 == FFA_ERROR)
		return ffa_to_linux_errno((int)ret.a2);
	else if (ret.a0 != FFA_SUCCESS)
		return -EINVAL; /* Something else went wrong. */

	notify->sp_map = PACK_NOTIFICATION_BITMAP(ret.a2, ret.a3);
	notify->vm_map = PACK_NOTIFICATION_BITMAP(ret.a4, ret.a5);
	notify->arch_map = PACK_NOTIFICATION_BITMAP(ret.a6, ret.a7);

	return 0;
}

struct ffa_dev_part_info {
	ffa_sched_recv_cb callback;
	void *cb_data;
	rwlock_t rw_lock;
};

static void __do_sched_recv_cb(u16 part_id, u16 vcpu, bool is_per_vcpu)
{
	struct ffa_dev_part_info *partition;
	ffa_sched_recv_cb callback;
	void *cb_data;

	partition = xa_load(&drv_info->partition_info, part_id);
	if (!partition) {
		pr_err("%s: Invalid partition ID 0x%x\n", __func__, part_id);
		return;
	}

	read_lock(&partition->rw_lock);
	callback = partition->callback;
	cb_data = partition->cb_data;
	read_unlock(&partition->rw_lock);

	if (callback)
		callback(vcpu, is_per_vcpu, cb_data);
}

static void ffa_notification_info_get(void)
{
	int idx, list, max_ids, lists_cnt, ids_processed, ids_count[MAX_IDS_64];
	bool is_64b_resp;
	ffa_value_t ret;
	u64 id_list;

	do {
		invoke_ffa_fn((ffa_value_t){
			  .a0 = FFA_FN_NATIVE(NOTIFICATION_INFO_GET),
			  }, &ret);

		if (ret.a0 != FFA_FN_NATIVE(SUCCESS) && ret.a0 != FFA_SUCCESS) {
			if (ret.a2 != FFA_RET_NO_DATA)
				pr_err("Notification Info fetch failed: 0x%lx (0x%lx)",
				       ret.a0, ret.a2);
			return;
		}

		is_64b_resp = (ret.a0 == FFA_FN64_SUCCESS);

		ids_processed = 0;
		lists_cnt = FIELD_GET(NOTIFICATION_INFO_GET_ID_COUNT, ret.a2);
		if (is_64b_resp) {
			max_ids = MAX_IDS_64;
			id_list = FIELD_GET(ID_LIST_MASK_64, ret.a2);
		} else {
			max_ids = MAX_IDS_32;
			id_list = FIELD_GET(ID_LIST_MASK_32, ret.a2);
		}

		for (idx = 0; idx < lists_cnt; idx++, id_list >>= 2)
			ids_count[idx] = (id_list & 0x3) + 1;

		/* Process IDs */
		for (list = 0; list < lists_cnt; list++) {
			u16 vcpu_id, part_id, *packed_id_list = (u16 *)&ret.a3;

			if (ids_processed >= max_ids - 1)
				break;

			part_id = packed_id_list[ids_processed++];

			if (ids_count[list] == 1) { /* Global Notification */
				__do_sched_recv_cb(part_id, 0, false);
				continue;
			}

			/* Per vCPU Notification */
			for (idx = 0; idx < ids_count[list]; idx++) {
				if (ids_processed >= max_ids - 1)
					break;

				vcpu_id = packed_id_list[ids_processed++];

				__do_sched_recv_cb(part_id, vcpu_id, true);
			}
		}
	} while (ret.a2 & NOTIFICATION_INFO_GET_MORE_PEND_MASK);
}

static int ffa_run(struct ffa_device *dev, u16 vcpu)
{
	ffa_value_t ret;
	u32 target = dev->vm_id << 16 | vcpu;

	invoke_ffa_fn((ffa_value_t){ .a0 = FFA_RUN, .a1 = target, }, &ret);

	while (ret.a0 == FFA_INTERRUPT)
		invoke_ffa_fn((ffa_value_t){ .a0 = FFA_RUN, .a1 = ret.a1, },
			      &ret);

	if (ret.a0 == FFA_ERROR)
		return ffa_to_linux_errno((int)ret.a2);

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

#define FFA_SECURE_PARTITION_ID_FLAG	BIT(15)

#define ffa_notifications_disabled()	(!drv_info->notif_enabled)

enum notify_type {
	NON_SECURE_VM,
	SECURE_PARTITION,
	FRAMEWORK,
};

struct notifier_cb_info {
	struct hlist_node hnode;
	ffa_notifier_cb cb;
	void *cb_data;
	enum notify_type type;
};

static int ffa_sched_recv_cb_update(u16 part_id, ffa_sched_recv_cb callback,
				    void *cb_data, bool is_registration)
{
	struct ffa_dev_part_info *partition;
	bool cb_valid;

	if (ffa_notifications_disabled())
		return -EOPNOTSUPP;

	partition = xa_load(&drv_info->partition_info, part_id);
	if (!partition) {
		pr_err("%s: Invalid partition ID 0x%x\n", __func__, part_id);
		return -EINVAL;
	}

	write_lock(&partition->rw_lock);

	cb_valid = !!partition->callback;
	if (!(is_registration ^ cb_valid)) {
		write_unlock(&partition->rw_lock);
		return -EINVAL;
	}

	partition->callback = callback;
	partition->cb_data = cb_data;

	write_unlock(&partition->rw_lock);
	return 0;
}

static int ffa_sched_recv_cb_register(struct ffa_device *dev,
				      ffa_sched_recv_cb cb, void *cb_data)
{
	return ffa_sched_recv_cb_update(dev->vm_id, cb, cb_data, true);
}

static int ffa_sched_recv_cb_unregister(struct ffa_device *dev)
{
	return ffa_sched_recv_cb_update(dev->vm_id, NULL, NULL, false);
}

static int ffa_notification_bind(u16 dst_id, u64 bitmap, u32 flags)
{
	return ffa_notification_bind_common(dst_id, bitmap, flags, true);
}

static int ffa_notification_unbind(u16 dst_id, u64 bitmap)
{
	return ffa_notification_bind_common(dst_id, bitmap, 0, false);
}

/* Should be called while the notify_lock is taken */
static struct notifier_cb_info *
notifier_hash_node_get(u16 notify_id, enum notify_type type)
{
	struct notifier_cb_info *node;

	hash_for_each_possible(drv_info->notifier_hash, node, hnode, notify_id)
		if (type == node->type)
			return node;

	return NULL;
}

static int
update_notifier_cb(int notify_id, enum notify_type type, ffa_notifier_cb cb,
		   void *cb_data, bool is_registration)
{
	struct notifier_cb_info *cb_info = NULL;
	bool cb_found;

	cb_info = notifier_hash_node_get(notify_id, type);
	cb_found = !!cb_info;

	if (!(is_registration ^ cb_found))
		return -EINVAL;

	if (is_registration) {
		cb_info = kzalloc(sizeof(*cb_info), GFP_KERNEL);
		if (!cb_info)
			return -ENOMEM;

		cb_info->type = type;
		cb_info->cb = cb;
		cb_info->cb_data = cb_data;

		hash_add(drv_info->notifier_hash, &cb_info->hnode, notify_id);
	} else {
		hash_del(&cb_info->hnode);
	}

	return 0;
}

static enum notify_type ffa_notify_type_get(u16 vm_id)
{
	if (vm_id & FFA_SECURE_PARTITION_ID_FLAG)
		return SECURE_PARTITION;
	else
		return NON_SECURE_VM;
}

static int ffa_notify_relinquish(struct ffa_device *dev, int notify_id)
{
	int rc;
	enum notify_type type = ffa_notify_type_get(dev->vm_id);

	if (ffa_notifications_disabled())
		return -EOPNOTSUPP;

	if (notify_id >= FFA_MAX_NOTIFICATIONS)
		return -EINVAL;

	mutex_lock(&drv_info->notify_lock);

	rc = update_notifier_cb(notify_id, type, NULL, NULL, false);
	if (rc) {
		pr_err("Could not unregister notification callback\n");
		mutex_unlock(&drv_info->notify_lock);
		return rc;
	}

	rc = ffa_notification_unbind(dev->vm_id, BIT(notify_id));

	mutex_unlock(&drv_info->notify_lock);

	return rc;
}

static int ffa_notify_request(struct ffa_device *dev, bool is_per_vcpu,
			      ffa_notifier_cb cb, void *cb_data, int notify_id)
{
	int rc;
	u32 flags = 0;
	enum notify_type type = ffa_notify_type_get(dev->vm_id);

	if (ffa_notifications_disabled())
		return -EOPNOTSUPP;

	if (notify_id >= FFA_MAX_NOTIFICATIONS)
		return -EINVAL;

	mutex_lock(&drv_info->notify_lock);

	if (is_per_vcpu)
		flags = PER_VCPU_NOTIFICATION_FLAG;

	rc = ffa_notification_bind(dev->vm_id, BIT(notify_id), flags);
	if (rc) {
		mutex_unlock(&drv_info->notify_lock);
		return rc;
	}

	rc = update_notifier_cb(notify_id, type, cb, cb_data, true);
	if (rc) {
		pr_err("Failed to register callback for %d - %d\n",
		       notify_id, rc);
		ffa_notification_unbind(dev->vm_id, BIT(notify_id));
	}
	mutex_unlock(&drv_info->notify_lock);

	return rc;
}

static int ffa_notify_send(struct ffa_device *dev, int notify_id,
			   bool is_per_vcpu, u16 vcpu)
{
	u32 flags = 0;

	if (ffa_notifications_disabled())
		return -EOPNOTSUPP;

	if (is_per_vcpu)
		flags |= (PER_VCPU_NOTIFICATION_FLAG | vcpu << 16);

	return ffa_notification_set(dev->vm_id, drv_info->vm_id, flags,
				    BIT(notify_id));
}

static void handle_notif_callbacks(u64 bitmap, enum notify_type type)
{
	int notify_id;
	struct notifier_cb_info *cb_info = NULL;

	for (notify_id = 0; notify_id <= FFA_MAX_NOTIFICATIONS && bitmap;
	     notify_id++, bitmap >>= 1) {
		if (!(bitmap & 1))
			continue;

		mutex_lock(&drv_info->notify_lock);
		cb_info = notifier_hash_node_get(notify_id, type);
		mutex_unlock(&drv_info->notify_lock);

		if (cb_info && cb_info->cb)
			cb_info->cb(notify_id, cb_info->cb_data);
	}
}

static void notif_pcpu_irq_work_fn(struct work_struct *work)
{
	int rc;
	struct ffa_notify_bitmaps bitmaps;

	rc = ffa_notification_get(SECURE_PARTITION_BITMAP |
				  SPM_FRAMEWORK_BITMAP, &bitmaps);
	if (rc) {
		pr_err("Failed to retrieve notifications with %d!\n", rc);
		return;
	}

	handle_notif_callbacks(bitmaps.vm_map, NON_SECURE_VM);
	handle_notif_callbacks(bitmaps.sp_map, SECURE_PARTITION);
	handle_notif_callbacks(bitmaps.arch_map, FRAMEWORK);
}

static void
ffa_self_notif_handle(u16 vcpu, bool is_per_vcpu, void *cb_data)
{
	struct ffa_drv_info *info = cb_data;

	if (!is_per_vcpu)
		notif_pcpu_irq_work_fn(&info->notif_pcpu_work);
	else
		queue_work_on(vcpu, info->notif_pcpu_wq,
			      &info->notif_pcpu_work);
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

static const struct ffa_cpu_ops ffa_drv_cpu_ops = {
	.run = ffa_run,
};

static const struct ffa_notifier_ops ffa_drv_notifier_ops = {
	.sched_recv_cb_register = ffa_sched_recv_cb_register,
	.sched_recv_cb_unregister = ffa_sched_recv_cb_unregister,
	.notify_request = ffa_notify_request,
	.notify_relinquish = ffa_notify_relinquish,
	.notify_send = ffa_notify_send,
};

static const struct ffa_ops ffa_drv_ops = {
	.info_ops = &ffa_drv_info_ops,
	.msg_ops = &ffa_drv_msg_ops,
	.mem_ops = &ffa_drv_mem_ops,
	.cpu_ops = &ffa_drv_cpu_ops,
	.notifier_ops = &ffa_drv_notifier_ops,
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

static int ffa_setup_partitions(void)
{
	int count, idx, ret;
	uuid_t uuid;
	struct ffa_device *ffa_dev;
	struct ffa_dev_part_info *info;
	struct ffa_partition_info *pbuf, *tpbuf;

	count = ffa_partition_probe(&uuid_null, &pbuf);
	if (count <= 0) {
		pr_info("%s: No partitions found, error %d\n", __func__, count);
		return -EINVAL;
	}

	xa_init(&drv_info->partition_info);
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
			ffa_mode_32bit_set(ffa_dev);

		info = kzalloc(sizeof(*info), GFP_KERNEL);
		if (!info) {
			ffa_device_unregister(ffa_dev);
			continue;
		}
		rwlock_init(&info->rw_lock);
		ret = xa_insert(&drv_info->partition_info, tpbuf->id,
				info, GFP_KERNEL);
		if (ret) {
			pr_err("%s: failed to save partition ID 0x%x - ret:%d\n",
			       __func__, tpbuf->id, ret);
			ffa_device_unregister(ffa_dev);
			kfree(info);
		}
	}

	kfree(pbuf);

	/* Allocate for the host */
	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		pr_err("%s: failed to alloc Host partition ID 0x%x. Abort.\n",
		       __func__, drv_info->vm_id);
		/* Already registered devices are freed on bus_exit */
		ffa_partitions_cleanup();
		return -ENOMEM;
	}

	rwlock_init(&info->rw_lock);
	ret = xa_insert(&drv_info->partition_info, drv_info->vm_id,
			info, GFP_KERNEL);
	if (ret) {
		pr_err("%s: failed to save Host partition ID 0x%x - ret:%d. Abort.\n",
		       __func__, drv_info->vm_id, ret);
		kfree(info);
		/* Already registered devices are freed on bus_exit */
		ffa_partitions_cleanup();
	}

	return ret;
}

static void ffa_partitions_cleanup(void)
{
	struct ffa_dev_part_info *info;
	unsigned long idx;

	xa_for_each(&drv_info->partition_info, idx, info) {
		xa_erase(&drv_info->partition_info, idx);
		kfree(info);
	}

	xa_destroy(&drv_info->partition_info);
}

/* FFA FEATURE IDs */
#define FFA_FEAT_NOTIFICATION_PENDING_INT	(1)
#define FFA_FEAT_SCHEDULE_RECEIVER_INT		(2)
#define FFA_FEAT_MANAGED_EXIT_INT		(3)

static irqreturn_t irq_handler(int irq, void *irq_data)
{
	struct ffa_pcpu_irq *pcpu = irq_data;
	struct ffa_drv_info *info = pcpu->info;

	queue_work(info->notif_pcpu_wq, &info->irq_work);

	return IRQ_HANDLED;
}

static void ffa_sched_recv_irq_work_fn(struct work_struct *work)
{
	ffa_notification_info_get();
}

static int ffa_sched_recv_irq_map(void)
{
	int ret, irq, sr_intid;

	/* The returned sr_intid is assumed to be SGI donated to NS world */
	ret = ffa_features(FFA_FEAT_SCHEDULE_RECEIVER_INT, 0, &sr_intid, NULL);
	if (ret < 0) {
		if (ret != -EOPNOTSUPP)
			pr_err("Failed to retrieve scheduler Rx interrupt\n");
		return ret;
	}

	if (acpi_disabled) {
		struct of_phandle_args oirq = {};
		struct device_node *gic;

		/* Only GICv3 supported currently with the device tree */
		gic = of_find_compatible_node(NULL, NULL, "arm,gic-v3");
		if (!gic)
			return -ENXIO;

		oirq.np = gic;
		oirq.args_count = 1;
		oirq.args[0] = sr_intid;
		irq = irq_create_of_mapping(&oirq);
		of_node_put(gic);
#ifdef CONFIG_ACPI
	} else {
		irq = acpi_register_gsi(NULL, sr_intid, ACPI_EDGE_SENSITIVE,
					ACPI_ACTIVE_HIGH);
#endif
	}

	if (irq <= 0) {
		pr_err("Failed to create IRQ mapping!\n");
		return -ENODATA;
	}

	return irq;
}

static void ffa_sched_recv_irq_unmap(void)
{
	if (drv_info->sched_recv_irq) {
		irq_dispose_mapping(drv_info->sched_recv_irq);
		drv_info->sched_recv_irq = 0;
	}
}

static int ffa_cpuhp_pcpu_irq_enable(unsigned int cpu)
{
	enable_percpu_irq(drv_info->sched_recv_irq, IRQ_TYPE_NONE);
	return 0;
}

static int ffa_cpuhp_pcpu_irq_disable(unsigned int cpu)
{
	disable_percpu_irq(drv_info->sched_recv_irq);
	return 0;
}

static void ffa_uninit_pcpu_irq(void)
{
	if (drv_info->cpuhp_state) {
		cpuhp_remove_state(drv_info->cpuhp_state);
		drv_info->cpuhp_state = 0;
	}

	if (drv_info->notif_pcpu_wq) {
		destroy_workqueue(drv_info->notif_pcpu_wq);
		drv_info->notif_pcpu_wq = NULL;
	}

	if (drv_info->sched_recv_irq)
		free_percpu_irq(drv_info->sched_recv_irq, drv_info->irq_pcpu);

	if (drv_info->irq_pcpu) {
		free_percpu(drv_info->irq_pcpu);
		drv_info->irq_pcpu = NULL;
	}
}

static int ffa_init_pcpu_irq(unsigned int irq)
{
	struct ffa_pcpu_irq __percpu *irq_pcpu;
	int ret, cpu;

	irq_pcpu = alloc_percpu(struct ffa_pcpu_irq);
	if (!irq_pcpu)
		return -ENOMEM;

	for_each_present_cpu(cpu)
		per_cpu_ptr(irq_pcpu, cpu)->info = drv_info;

	drv_info->irq_pcpu = irq_pcpu;

	ret = request_percpu_irq(irq, irq_handler, "ARM-FFA", irq_pcpu);
	if (ret) {
		pr_err("Error registering notification IRQ %d: %d\n", irq, ret);
		return ret;
	}

	INIT_WORK(&drv_info->irq_work, ffa_sched_recv_irq_work_fn);
	INIT_WORK(&drv_info->notif_pcpu_work, notif_pcpu_irq_work_fn);
	drv_info->notif_pcpu_wq = create_workqueue("ffa_pcpu_irq_notification");
	if (!drv_info->notif_pcpu_wq)
		return -EINVAL;

	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "ffa/pcpu-irq:starting",
				ffa_cpuhp_pcpu_irq_enable,
				ffa_cpuhp_pcpu_irq_disable);

	if (ret < 0)
		return ret;

	drv_info->cpuhp_state = ret;
	return 0;
}

static void ffa_notifications_cleanup(void)
{
	ffa_uninit_pcpu_irq();
	ffa_sched_recv_irq_unmap();

	if (drv_info->bitmap_created) {
		ffa_notification_bitmap_destroy();
		drv_info->bitmap_created = false;
	}
	drv_info->notif_enabled = false;
}

static void ffa_notifications_setup(void)
{
	int ret, irq;

	ret = ffa_features(FFA_NOTIFICATION_BITMAP_CREATE, 0, NULL, NULL);
	if (ret) {
		pr_info("Notifications not supported, continuing with it ..\n");
		return;
	}

	ret = ffa_notification_bitmap_create();
	if (ret) {
		pr_info("Notification bitmap create error %d\n", ret);
		return;
	}
	drv_info->bitmap_created = true;

	irq = ffa_sched_recv_irq_map();
	if (irq <= 0) {
		ret = irq;
		goto cleanup;
	}

	drv_info->sched_recv_irq = irq;

	ret = ffa_init_pcpu_irq(irq);
	if (ret)
		goto cleanup;

	hash_init(drv_info->notifier_hash);
	mutex_init(&drv_info->notify_lock);

	drv_info->notif_enabled = true;
	return;
cleanup:
	pr_info("Notification setup failed %d, not enabled\n", ret);
	ffa_notifications_cleanup();
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

	ffa_set_up_mem_ops_native_flag();

	ffa_notifications_setup();

	ret = ffa_setup_partitions();
	if (ret) {
		pr_err("failed to setup partitions\n");
		goto cleanup_notifs;
	}

	ret = ffa_sched_recv_cb_update(drv_info->vm_id, ffa_self_notif_handle,
				       drv_info, true);
	if (ret)
		pr_info("Failed to register driver sched callback %d\n", ret);

	return 0;

cleanup_notifs:
	ffa_notifications_cleanup();
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
	ffa_notifications_cleanup();
	ffa_partitions_cleanup();
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
