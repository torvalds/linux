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
#include <linux/delay.h>
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

#define FFA_DRIVER_VERSION	FFA_VERSION_1_2
#define FFA_MIN_VERSION		FFA_VERSION_1_0

#define SENDER_ID_MASK		GENMASK(31, 16)
#define RECEIVER_ID_MASK	GENMASK(15, 0)
#define SENDER_ID(x)		((u16)(FIELD_GET(SENDER_ID_MASK, (x))))
#define RECEIVER_ID(x)		((u16)(FIELD_GET(RECEIVER_ID_MASK, (x))))
#define PACK_TARGET_INFO(s, r)		\
	(FIELD_PREP(SENDER_ID_MASK, (s)) | FIELD_PREP(RECEIVER_ID_MASK, (r)))

#define RXTX_MAP_MIN_BUFSZ_MASK	GENMASK(1, 0)
#define RXTX_MAP_MIN_BUFSZ(x)	((x) & RXTX_MAP_MIN_BUFSZ_MASK)

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
	-EAGAIN,	/* FFA_RET_NOT_READY */
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
	size_t rxtx_bufsz;
	bool mem_ops_native;
	bool msg_direct_req2_supp;
	bool bitmap_created;
	bool notif_enabled;
	unsigned int sched_recv_irq;
	unsigned int notif_pend_irq;
	unsigned int cpuhp_state;
	struct ffa_pcpu_irq __percpu *irq_pcpu;
	struct workqueue_struct *notif_pcpu_wq;
	struct work_struct notif_pcpu_work;
	struct work_struct sched_recv_irq_work;
	struct xarray partition_info;
	DECLARE_HASHTABLE(notifier_hash, ilog2(FFA_MAX_NOTIFICATIONS));
	struct mutex notify_lock; /* lock to protect notifier hashtable  */
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

	if ((s32)ver.a0 == FFA_RET_NOT_SUPPORTED) {
		pr_info("FFA_VERSION returned not supported\n");
		return -EOPNOTSUPP;
	}

	if (FFA_MAJOR_VERSION(ver.a0) > FFA_MAJOR_VERSION(FFA_DRIVER_VERSION)) {
		pr_err("Incompatible v%d.%d! Latest supported v%d.%d\n",
		       FFA_MAJOR_VERSION(ver.a0), FFA_MINOR_VERSION(ver.a0),
		       FFA_MAJOR_VERSION(FFA_DRIVER_VERSION),
		       FFA_MINOR_VERSION(FFA_DRIVER_VERSION));
		return -EINVAL;
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
		for (idx = 0; idx < count; idx++) {
			struct ffa_partition_info_le {
				__le16 id;
				__le16 exec_ctxt;
				__le32 properties;
				uuid_t uuid;
			} *rx_buf = drv_info->rx_buffer + idx * sz;
			struct ffa_partition_info *buf = buffer + idx;

			buf->id = le16_to_cpu(rx_buf->id);
			buf->exec_ctxt = le16_to_cpu(rx_buf->exec_ctxt);
			buf->properties = le32_to_cpu(rx_buf->properties);
			if (buf_sz > 8)
				import_uuid(&buf->uuid, (u8 *)&rx_buf->uuid);
		}

	if (!(flags & PARTITION_INFO_GET_RETURN_COUNT_ONLY))
		ffa_rx_release();

	mutex_unlock(&drv_info->rx_lock);

	return count;
}

#define LAST_INDEX_MASK		GENMASK(15, 0)
#define CURRENT_INDEX_MASK	GENMASK(31, 16)
#define UUID_INFO_TAG_MASK	GENMASK(47, 32)
#define PARTITION_INFO_SZ_MASK	GENMASK(63, 48)
#define PARTITION_COUNT(x)	((u16)(FIELD_GET(LAST_INDEX_MASK, (x))) + 1)
#define CURRENT_INDEX(x)	((u16)(FIELD_GET(CURRENT_INDEX_MASK, (x))))
#define UUID_INFO_TAG(x)	((u16)(FIELD_GET(UUID_INFO_TAG_MASK, (x))))
#define PARTITION_INFO_SZ(x)	((u16)(FIELD_GET(PARTITION_INFO_SZ_MASK, (x))))
#define PART_INFO_ID_MASK	GENMASK(15, 0)
#define PART_INFO_EXEC_CXT_MASK	GENMASK(31, 16)
#define PART_INFO_PROPS_MASK	GENMASK(63, 32)
#define PART_INFO_ID(x)		((u16)(FIELD_GET(PART_INFO_ID_MASK, (x))))
#define PART_INFO_EXEC_CXT(x)	((u16)(FIELD_GET(PART_INFO_EXEC_CXT_MASK, (x))))
#define PART_INFO_PROPERTIES(x)	((u32)(FIELD_GET(PART_INFO_PROPS_MASK, (x))))
static int
__ffa_partition_info_get_regs(u32 uuid0, u32 uuid1, u32 uuid2, u32 uuid3,
			      struct ffa_partition_info *buffer, int num_parts)
{
	u16 buf_sz, start_idx, cur_idx, count = 0, prev_idx = 0, tag = 0;
	struct ffa_partition_info *buf = buffer;
	ffa_value_t partition_info;

	do {
		__le64 *regs;
		int idx;

		start_idx = prev_idx ? prev_idx + 1 : 0;

		invoke_ffa_fn((ffa_value_t){
			      .a0 = FFA_PARTITION_INFO_GET_REGS,
			      .a1 = (u64)uuid1 << 32 | uuid0,
			      .a2 = (u64)uuid3 << 32 | uuid2,
			      .a3 = start_idx | tag << 16,
			      }, &partition_info);

		if (partition_info.a0 == FFA_ERROR)
			return ffa_to_linux_errno((int)partition_info.a2);

		if (!count)
			count = PARTITION_COUNT(partition_info.a2);
		if (!buffer || !num_parts) /* count only */
			return count;

		cur_idx = CURRENT_INDEX(partition_info.a2);
		tag = UUID_INFO_TAG(partition_info.a2);
		buf_sz = PARTITION_INFO_SZ(partition_info.a2);
		if (buf_sz > sizeof(*buffer))
			buf_sz = sizeof(*buffer);

		regs = (void *)&partition_info.a3;
		for (idx = 0; idx < cur_idx - start_idx + 1; idx++, buf++) {
			union {
				uuid_t uuid;
				u64 regs[2];
			} uuid_regs = {
				.regs = {
					le64_to_cpu(*(regs + 1)),
					le64_to_cpu(*(regs + 2)),
					}
			};
			u64 val = *(u64 *)regs;

			buf->id = PART_INFO_ID(val);
			buf->exec_ctxt = PART_INFO_EXEC_CXT(val);
			buf->properties = PART_INFO_PROPERTIES(val);
			uuid_copy(&buf->uuid, &uuid_regs.uuid);
			regs += 3;
		}
		prev_idx = cur_idx;

	} while (cur_idx < (count - 1));

	return count;
}

/* buffer is allocated and caller must free the same if returned count > 0 */
static int
ffa_partition_probe(const uuid_t *uuid, struct ffa_partition_info **buffer)
{
	int count;
	u32 uuid0_4[4];
	bool reg_mode = false;
	struct ffa_partition_info *pbuf;

	if (!ffa_features(FFA_PARTITION_INFO_GET_REGS, 0, NULL, NULL))
		reg_mode = true;

	export_uuid((u8 *)uuid0_4, uuid);
	if (reg_mode)
		count = __ffa_partition_info_get_regs(uuid0_4[0], uuid0_4[1],
						      uuid0_4[2], uuid0_4[3],
						      NULL, 0);
	else
		count = __ffa_partition_info_get(uuid0_4[0], uuid0_4[1],
						 uuid0_4[2], uuid0_4[3],
						 NULL, 0);
	if (count <= 0)
		return count;

	pbuf = kcalloc(count, sizeof(*pbuf), GFP_KERNEL);
	if (!pbuf)
		return -ENOMEM;

	if (reg_mode)
		count = __ffa_partition_info_get_regs(uuid0_4[0], uuid0_4[1],
						      uuid0_4[2], uuid0_4[3],
						      pbuf, count);
	else
		count = __ffa_partition_info_get(uuid0_4[0], uuid0_4[1],
						 uuid0_4[2], uuid0_4[3],
						 pbuf, count);
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

static inline void ffa_msg_send_wait_for_completion(ffa_value_t *ret)
{
	while (ret->a0 == FFA_INTERRUPT || ret->a0 == FFA_YIELD) {
		if (ret->a0 == FFA_YIELD)
			fsleep(1000);

		invoke_ffa_fn((ffa_value_t){
			      .a0 = FFA_RUN, .a1 = ret->a1,
			      }, ret);
	}
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

	ffa_msg_send_wait_for_completion(&ret);

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

static int ffa_msg_send2(struct ffa_device *dev, u16 src_id, void *buf, size_t sz)
{
	u32 src_dst_ids = PACK_TARGET_INFO(src_id, dev->vm_id);
	struct ffa_indirect_msg_hdr *msg;
	ffa_value_t ret;
	int retval = 0;

	if (sz > (drv_info->rxtx_bufsz - sizeof(*msg)))
		return -ERANGE;

	mutex_lock(&drv_info->tx_lock);

	msg = drv_info->tx_buffer;
	msg->flags = 0;
	msg->res0 = 0;
	msg->offset = sizeof(*msg);
	msg->send_recv_id = src_dst_ids;
	msg->size = sz;
	uuid_copy(&msg->uuid, &dev->uuid);
	memcpy((u8 *)msg + msg->offset, buf, sz);

	/* flags = 0, sender VMID = 0 works for both physical/virtual NS */
	invoke_ffa_fn((ffa_value_t){
		      .a0 = FFA_MSG_SEND2, .a1 = 0, .a2 = 0
		      }, &ret);

	if (ret.a0 == FFA_ERROR)
		retval = ffa_to_linux_errno((int)ret.a2);

	mutex_unlock(&drv_info->tx_lock);
	return retval;
}

static int ffa_msg_send_direct_req2(u16 src_id, u16 dst_id, const uuid_t *uuid,
				    struct ffa_send_direct_data2 *data)
{
	u32 src_dst_ids = PACK_TARGET_INFO(src_id, dst_id);
	union {
		uuid_t uuid;
		__le64 regs[2];
	} uuid_regs = { .uuid = *uuid };
	ffa_value_t ret, args = {
		.a0 = FFA_MSG_SEND_DIRECT_REQ2,
		.a1 = src_dst_ids,
		.a2 = le64_to_cpu(uuid_regs.regs[0]),
		.a3 = le64_to_cpu(uuid_regs.regs[1]),
	};
	memcpy((void *)&args + offsetof(ffa_value_t, a4), data, sizeof(*data));

	invoke_ffa_fn(args, &ret);

	ffa_msg_send_wait_for_completion(&ret);

	if (ret.a0 == FFA_ERROR)
		return ffa_to_linux_errno((int)ret.a2);

	if (ret.a0 == FFA_MSG_SEND_DIRECT_RESP2) {
		memcpy(data, (void *)&ret + offsetof(ffa_value_t, a4), sizeof(*data));
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
	size_t rxtx_bufsz = drv_info->rxtx_bufsz;

	if (!args->use_txbuf) {
		buffer = alloc_pages_exact(rxtx_bufsz, GFP_KERNEL);
		if (!buffer)
			return -ENOMEM;
	} else {
		buffer = drv_info->tx_buffer;
		mutex_lock(&drv_info->tx_lock);
	}

	ret = ffa_setup_and_transmit(func_id, buffer, rxtx_bufsz, args);

	if (args->use_txbuf)
		mutex_unlock(&drv_info->tx_lock);
	else
		free_pages_exact(buffer, rxtx_bufsz);

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

enum notify_type {
	SECURE_PARTITION,
	NON_SECURE_VM,
	SPM_FRAMEWORK,
	NS_HYP_FRAMEWORK,
};

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
#define SECURE_PARTITION_BITMAP_ENABLE		BIT(SECURE_PARTITION)
#define NON_SECURE_VM_BITMAP_ENABLE		BIT(NON_SECURE_VM)
#define SPM_FRAMEWORK_BITMAP_ENABLE		BIT(SPM_FRAMEWORK)
#define NS_HYP_FRAMEWORK_BITMAP_ENABLE		BIT(NS_HYP_FRAMEWORK)
#define FFA_BITMAP_SECURE_ENABLE_MASK		\
	(SECURE_PARTITION_BITMAP_ENABLE | SPM_FRAMEWORK_BITMAP_ENABLE)
#define FFA_BITMAP_NS_ENABLE_MASK		\
	(NON_SECURE_VM_BITMAP_ENABLE | NS_HYP_FRAMEWORK_BITMAP_ENABLE)
#define FFA_BITMAP_ALL_ENABLE_MASK		\
	(FFA_BITMAP_SECURE_ENABLE_MASK | FFA_BITMAP_NS_ENABLE_MASK)

#define FFA_SECURE_PARTITION_ID_FLAG		BIT(15)

#define SPM_FRAMEWORK_BITMAP(x)			NOTIFICATION_BITMAP_LOW(x)
#define NS_HYP_FRAMEWORK_BITMAP(x)		NOTIFICATION_BITMAP_HIGH(x)
#define FRAMEWORK_NOTIFY_RX_BUFFER_FULL		BIT(0)

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

	if (flags & SECURE_PARTITION_BITMAP_ENABLE)
		notify->sp_map = PACK_NOTIFICATION_BITMAP(ret.a2, ret.a3);
	if (flags & NON_SECURE_VM_BITMAP_ENABLE)
		notify->vm_map = PACK_NOTIFICATION_BITMAP(ret.a4, ret.a5);
	if (flags & SPM_FRAMEWORK_BITMAP_ENABLE)
		notify->arch_map = SPM_FRAMEWORK_BITMAP(ret.a6);
	if (flags & NS_HYP_FRAMEWORK_BITMAP_ENABLE)
		notify->arch_map = PACK_NOTIFICATION_BITMAP(notify->arch_map,
							    ret.a7);

	return 0;
}

struct ffa_dev_part_info {
	ffa_sched_recv_cb callback;
	void *cb_data;
	rwlock_t rw_lock;
	struct ffa_device *dev;
	struct list_head node;
};

static void __do_sched_recv_cb(u16 part_id, u16 vcpu, bool is_per_vcpu)
{
	struct ffa_dev_part_info *partition = NULL, *tmp;
	ffa_sched_recv_cb callback;
	struct list_head *phead;
	void *cb_data;

	phead = xa_load(&drv_info->partition_info, part_id);
	if (!phead) {
		pr_err("%s: Invalid partition ID 0x%x\n", __func__, part_id);
		return;
	}

	list_for_each_entry_safe(partition, tmp, phead, node) {
		read_lock(&partition->rw_lock);
		callback = partition->callback;
		cb_data = partition->cb_data;
		read_unlock(&partition->rw_lock);

		if (callback)
			callback(vcpu, is_per_vcpu, cb_data);
	}
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
			if ((s32)ret.a2 != FFA_RET_NO_DATA)
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
			for (idx = 1; idx < ids_count[list]; idx++) {
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

static void ffa_drvinfo_flags_init(void)
{
	if (!ffa_features(FFA_FN_NATIVE(MEM_LEND), 0, NULL, NULL) ||
	    !ffa_features(FFA_FN_NATIVE(MEM_SHARE), 0, NULL, NULL))
		drv_info->mem_ops_native = true;

	if (!ffa_features(FFA_MSG_SEND_DIRECT_REQ2, 0, NULL, NULL) ||
	    !ffa_features(FFA_MSG_SEND_DIRECT_RESP2, 0, NULL, NULL))
		drv_info->msg_direct_req2_supp = true;
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

static int ffa_indirect_msg_send(struct ffa_device *dev, void *buf, size_t sz)
{
	return ffa_msg_send2(dev, drv_info->vm_id, buf, sz);
}

static int ffa_sync_send_receive2(struct ffa_device *dev,
				  struct ffa_send_direct_data2 *data)
{
	if (!drv_info->msg_direct_req2_supp)
		return -EOPNOTSUPP;

	return ffa_msg_send_direct_req2(drv_info->vm_id, dev->vm_id,
					&dev->uuid, data);
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

#define ffa_notifications_disabled()	(!drv_info->notif_enabled)

struct notifier_cb_info {
	struct hlist_node hnode;
	struct ffa_device *dev;
	ffa_fwk_notifier_cb fwk_cb;
	ffa_notifier_cb cb;
	void *cb_data;
};

static int
ffa_sched_recv_cb_update(struct ffa_device *dev, ffa_sched_recv_cb callback,
			 void *cb_data, bool is_registration)
{
	struct ffa_dev_part_info *partition = NULL, *tmp;
	struct list_head *phead;
	bool cb_valid;

	if (ffa_notifications_disabled())
		return -EOPNOTSUPP;

	phead = xa_load(&drv_info->partition_info, dev->vm_id);
	if (!phead) {
		pr_err("%s: Invalid partition ID 0x%x\n", __func__, dev->vm_id);
		return -EINVAL;
	}

	list_for_each_entry_safe(partition, tmp, phead, node)
		if (partition->dev == dev)
			break;

	if (!partition) {
		pr_err("%s: No such partition ID 0x%x\n", __func__, dev->vm_id);
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
	return ffa_sched_recv_cb_update(dev, cb, cb_data, true);
}

static int ffa_sched_recv_cb_unregister(struct ffa_device *dev)
{
	return ffa_sched_recv_cb_update(dev, NULL, NULL, false);
}

static int ffa_notification_bind(u16 dst_id, u64 bitmap, u32 flags)
{
	return ffa_notification_bind_common(dst_id, bitmap, flags, true);
}

static int ffa_notification_unbind(u16 dst_id, u64 bitmap)
{
	return ffa_notification_bind_common(dst_id, bitmap, 0, false);
}

static enum notify_type ffa_notify_type_get(u16 vm_id)
{
	if (vm_id & FFA_SECURE_PARTITION_ID_FLAG)
		return SECURE_PARTITION;
	else
		return NON_SECURE_VM;
}

/* notifier_hnode_get* should be called with notify_lock held */
static struct notifier_cb_info *
notifier_hnode_get_by_vmid(u16 notify_id, int vmid)
{
	struct notifier_cb_info *node;

	hash_for_each_possible(drv_info->notifier_hash, node, hnode, notify_id)
		if (node->fwk_cb && vmid == node->dev->vm_id)
			return node;

	return NULL;
}

static struct notifier_cb_info *
notifier_hnode_get_by_vmid_uuid(u16 notify_id, int vmid, const uuid_t *uuid)
{
	struct notifier_cb_info *node;

	if (uuid_is_null(uuid))
		return notifier_hnode_get_by_vmid(notify_id, vmid);

	hash_for_each_possible(drv_info->notifier_hash, node, hnode, notify_id)
		if (node->fwk_cb && vmid == node->dev->vm_id &&
		    uuid_equal(&node->dev->uuid, uuid))
			return node;

	return NULL;
}

static struct notifier_cb_info *
notifier_hnode_get_by_type(u16 notify_id, enum notify_type type)
{
	struct notifier_cb_info *node;

	hash_for_each_possible(drv_info->notifier_hash, node, hnode, notify_id)
		if (node->cb && type == ffa_notify_type_get(node->dev->vm_id))
			return node;

	return NULL;
}

static int
update_notifier_cb(struct ffa_device *dev, int notify_id, void *cb,
		   void *cb_data, bool is_registration, bool is_framework)
{
	struct notifier_cb_info *cb_info = NULL;
	enum notify_type type = ffa_notify_type_get(dev->vm_id);
	bool cb_found;

	if (is_framework)
		cb_info = notifier_hnode_get_by_vmid_uuid(notify_id, dev->vm_id,
							  &dev->uuid);
	else
		cb_info = notifier_hnode_get_by_type(notify_id, type);

	cb_found = !!cb_info;

	if (!(is_registration ^ cb_found))
		return -EINVAL;

	if (is_registration) {
		cb_info = kzalloc(sizeof(*cb_info), GFP_KERNEL);
		if (!cb_info)
			return -ENOMEM;

		cb_info->dev = dev;
		cb_info->cb_data = cb_data;
		if (is_framework)
			cb_info->fwk_cb = cb;
		else
			cb_info->cb = cb;

		hash_add(drv_info->notifier_hash, &cb_info->hnode, notify_id);
	} else {
		hash_del(&cb_info->hnode);
	}

	return 0;
}

static int __ffa_notify_relinquish(struct ffa_device *dev, int notify_id,
				   bool is_framework)
{
	int rc;

	if (ffa_notifications_disabled())
		return -EOPNOTSUPP;

	if (notify_id >= FFA_MAX_NOTIFICATIONS)
		return -EINVAL;

	mutex_lock(&drv_info->notify_lock);

	rc = update_notifier_cb(dev, notify_id, NULL, NULL, false,
				is_framework);
	if (rc) {
		pr_err("Could not unregister notification callback\n");
		mutex_unlock(&drv_info->notify_lock);
		return rc;
	}

	if (!is_framework)
		rc = ffa_notification_unbind(dev->vm_id, BIT(notify_id));

	mutex_unlock(&drv_info->notify_lock);

	return rc;
}

static int ffa_notify_relinquish(struct ffa_device *dev, int notify_id)
{
	return __ffa_notify_relinquish(dev, notify_id, false);
}

static int ffa_fwk_notify_relinquish(struct ffa_device *dev, int notify_id)
{
	return __ffa_notify_relinquish(dev, notify_id, true);
}

static int __ffa_notify_request(struct ffa_device *dev, bool is_per_vcpu,
				void *cb, void *cb_data,
				int notify_id, bool is_framework)
{
	int rc;
	u32 flags = 0;

	if (ffa_notifications_disabled())
		return -EOPNOTSUPP;

	if (notify_id >= FFA_MAX_NOTIFICATIONS)
		return -EINVAL;

	mutex_lock(&drv_info->notify_lock);

	if (!is_framework) {
		if (is_per_vcpu)
			flags = PER_VCPU_NOTIFICATION_FLAG;

		rc = ffa_notification_bind(dev->vm_id, BIT(notify_id), flags);
		if (rc) {
			mutex_unlock(&drv_info->notify_lock);
			return rc;
		}
	}

	rc = update_notifier_cb(dev, notify_id, cb, cb_data, true,
				is_framework);
	if (rc) {
		pr_err("Failed to register callback for %d - %d\n",
		       notify_id, rc);
		if (!is_framework)
			ffa_notification_unbind(dev->vm_id, BIT(notify_id));
	}
	mutex_unlock(&drv_info->notify_lock);

	return rc;
}

static int ffa_notify_request(struct ffa_device *dev, bool is_per_vcpu,
			      ffa_notifier_cb cb, void *cb_data, int notify_id)
{
	return __ffa_notify_request(dev, is_per_vcpu, cb, cb_data, notify_id,
				    false);
}

static int
ffa_fwk_notify_request(struct ffa_device *dev, ffa_fwk_notifier_cb cb,
		       void *cb_data, int notify_id)
{
	return __ffa_notify_request(dev, false, cb, cb_data, notify_id, true);
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
		cb_info = notifier_hnode_get_by_type(notify_id, type);
		mutex_unlock(&drv_info->notify_lock);

		if (cb_info && cb_info->cb)
			cb_info->cb(notify_id, cb_info->cb_data);
	}
}

static void handle_fwk_notif_callbacks(u32 bitmap)
{
	void *buf;
	uuid_t uuid;
	int notify_id = 0, target;
	struct ffa_indirect_msg_hdr *msg;
	struct notifier_cb_info *cb_info = NULL;

	/* Only one framework notification defined and supported for now */
	if (!(bitmap & FRAMEWORK_NOTIFY_RX_BUFFER_FULL))
		return;

	mutex_lock(&drv_info->rx_lock);

	msg = drv_info->rx_buffer;
	buf = kmemdup((void *)msg + msg->offset, msg->size, GFP_KERNEL);
	if (!buf) {
		mutex_unlock(&drv_info->rx_lock);
		return;
	}

	target = SENDER_ID(msg->send_recv_id);
	if (msg->offset >= sizeof(*msg))
		uuid_copy(&uuid, &msg->uuid);
	else
		uuid_copy(&uuid, &uuid_null);

	mutex_unlock(&drv_info->rx_lock);

	ffa_rx_release();

	mutex_lock(&drv_info->notify_lock);
	cb_info = notifier_hnode_get_by_vmid_uuid(notify_id, target, &uuid);
	mutex_unlock(&drv_info->notify_lock);

	if (cb_info && cb_info->fwk_cb)
		cb_info->fwk_cb(notify_id, cb_info->cb_data, buf);
	kfree(buf);
}

static void notif_get_and_handle(void *cb_data)
{
	int rc;
	u32 flags;
	struct ffa_drv_info *info = cb_data;
	struct ffa_notify_bitmaps bitmaps = { 0 };

	if (info->vm_id == 0) /* Non secure physical instance */
		flags = FFA_BITMAP_SECURE_ENABLE_MASK;
	else
		flags = FFA_BITMAP_ALL_ENABLE_MASK;

	rc = ffa_notification_get(flags, &bitmaps);
	if (rc) {
		pr_err("Failed to retrieve notifications with %d!\n", rc);
		return;
	}

	handle_fwk_notif_callbacks(SPM_FRAMEWORK_BITMAP(bitmaps.arch_map));
	handle_fwk_notif_callbacks(NS_HYP_FRAMEWORK_BITMAP(bitmaps.arch_map));
	handle_notif_callbacks(bitmaps.vm_map, NON_SECURE_VM);
	handle_notif_callbacks(bitmaps.sp_map, SECURE_PARTITION);
}

static void
ffa_self_notif_handle(u16 vcpu, bool is_per_vcpu, void *cb_data)
{
	struct ffa_drv_info *info = cb_data;

	if (!is_per_vcpu)
		notif_get_and_handle(info);
	else
		smp_call_function_single(vcpu, notif_get_and_handle, info, 0);
}

static void notif_pcpu_irq_work_fn(struct work_struct *work)
{
	struct ffa_drv_info *info = container_of(work, struct ffa_drv_info,
						 notif_pcpu_work);

	ffa_self_notif_handle(smp_processor_id(), true, info);
}

static const struct ffa_info_ops ffa_drv_info_ops = {
	.api_version_get = ffa_api_version_get,
	.partition_info_get = ffa_partition_info_get,
};

static const struct ffa_msg_ops ffa_drv_msg_ops = {
	.mode_32bit_set = ffa_mode_32bit_set,
	.sync_send_receive = ffa_sync_send_receive,
	.indirect_send = ffa_indirect_msg_send,
	.sync_send_receive2 = ffa_sync_send_receive2,
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
	.fwk_notify_request = ffa_fwk_notify_request,
	.fwk_notify_relinquish = ffa_fwk_notify_relinquish,
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

	count = ffa_partition_probe(uuid, &pbuf);
	if (count <= 0)
		return;

	for (idx = 0, tpbuf = pbuf; idx < count; idx++, tpbuf++)
		if (tpbuf->id == ffa_dev->vm_id)
			uuid_copy(&ffa_dev->uuid, uuid);
	kfree(pbuf);
}

static int
ffa_bus_notifier(struct notifier_block *nb, unsigned long action, void *data)
{
	struct device *dev = data;
	struct ffa_device *fdev = to_ffa_dev(dev);

	if (action == BUS_NOTIFY_BIND_DRIVER) {
		struct ffa_driver *ffa_drv = to_ffa_driver(dev->driver);
		const struct ffa_device_id *id_table = ffa_drv->id_table;

		/*
		 * FF-A v1.1 provides UUID for each partition as part of the
		 * discovery API, the discovered UUID must be populated in the
		 * device's UUID and there is no need to workaround by copying
		 * the same from the driver table.
		 */
		if (uuid_is_null(&fdev->uuid))
			ffa_device_match_uuid(fdev, &id_table->uuid);

		return NOTIFY_OK;
	}

	return NOTIFY_DONE;
}

static struct notifier_block ffa_bus_nb = {
	.notifier_call = ffa_bus_notifier,
};

static int ffa_xa_add_partition_info(struct ffa_device *dev)
{
	struct ffa_dev_part_info *info;
	struct list_head *head, *phead;
	int ret = -ENOMEM;

	phead = xa_load(&drv_info->partition_info, dev->vm_id);
	if (phead) {
		head = phead;
		list_for_each_entry(info, head, node) {
			if (info->dev == dev) {
				pr_err("%s: duplicate dev %p part ID 0x%x\n",
				       __func__, dev, dev->vm_id);
				return -EEXIST;
			}
		}
	}

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return ret;

	rwlock_init(&info->rw_lock);
	info->dev = dev;

	if (!phead) {
		phead = kzalloc(sizeof(*phead), GFP_KERNEL);
		if (!phead)
			goto free_out;

		INIT_LIST_HEAD(phead);

		ret = xa_insert(&drv_info->partition_info, dev->vm_id, phead,
				GFP_KERNEL);
		if (ret) {
			pr_err("%s: failed to save part ID 0x%x Ret:%d\n",
			       __func__, dev->vm_id, ret);
			goto free_out;
		}
	}
	list_add(&info->node, phead);
	return 0;

free_out:
	kfree(phead);
	kfree(info);
	return ret;
}

static int ffa_setup_host_partition(int vm_id)
{
	struct ffa_partition_info buf = { 0 };
	struct ffa_device *ffa_dev;
	int ret;

	buf.id = vm_id;
	ffa_dev = ffa_device_register(&buf, &ffa_drv_ops);
	if (!ffa_dev) {
		pr_err("%s: failed to register host partition ID 0x%x\n",
		       __func__, vm_id);
		return -EINVAL;
	}

	ret = ffa_xa_add_partition_info(ffa_dev);
	if (ret)
		return ret;

	if (ffa_notifications_disabled())
		return 0;

	ret = ffa_sched_recv_cb_update(ffa_dev, ffa_self_notif_handle,
				       drv_info, true);
	if (ret)
		pr_info("Failed to register driver sched callback %d\n", ret);

	return ret;
}

static void ffa_partitions_cleanup(void)
{
	struct list_head *phead;
	unsigned long idx;

	/* Clean up/free all registered devices */
	ffa_devices_unregister();

	xa_for_each(&drv_info->partition_info, idx, phead) {
		struct ffa_dev_part_info *info, *tmp;

		xa_erase(&drv_info->partition_info, idx);
		list_for_each_entry_safe(info, tmp, phead, node) {
			list_del(&info->node);
			kfree(info);
		}
		kfree(phead);
	}

	xa_destroy(&drv_info->partition_info);
}

static int ffa_setup_partitions(void)
{
	int count, idx, ret;
	struct ffa_device *ffa_dev;
	struct ffa_partition_info *pbuf, *tpbuf;

	if (drv_info->version == FFA_VERSION_1_0) {
		ret = bus_register_notifier(&ffa_bus_type, &ffa_bus_nb);
		if (ret)
			pr_err("Failed to register FF-A bus notifiers\n");
	}

	count = ffa_partition_probe(&uuid_null, &pbuf);
	if (count <= 0) {
		pr_info("%s: No partitions found, error %d\n", __func__, count);
		return -EINVAL;
	}

	xa_init(&drv_info->partition_info);
	for (idx = 0, tpbuf = pbuf; idx < count; idx++, tpbuf++) {
		/* Note that if the UUID will be uuid_null, that will require
		 * ffa_bus_notifier() to find the UUID of this partition id
		 * with help of ffa_device_match_uuid(). FF-A v1.1 and above
		 * provides UUID here for each partition as part of the
		 * discovery API and the same is passed.
		 */
		ffa_dev = ffa_device_register(tpbuf, &ffa_drv_ops);
		if (!ffa_dev) {
			pr_err("%s: failed to register partition ID 0x%x\n",
			       __func__, tpbuf->id);
			continue;
		}

		if (drv_info->version > FFA_VERSION_1_0 &&
		    !(tpbuf->properties & FFA_PARTITION_AARCH64_EXEC))
			ffa_mode_32bit_set(ffa_dev);

		if (ffa_xa_add_partition_info(ffa_dev)) {
			ffa_device_unregister(ffa_dev);
			continue;
		}
	}

	kfree(pbuf);

	/*
	 * Check if the host is already added as part of partition info
	 * No multiple UUID possible for the host, so just checking if
	 * there is an entry will suffice
	 */
	if (xa_load(&drv_info->partition_info, drv_info->vm_id))
		return 0;

	/* Allocate for the host */
	ret = ffa_setup_host_partition(drv_info->vm_id);
	if (ret)
		ffa_partitions_cleanup();

	return ret;
}

/* FFA FEATURE IDs */
#define FFA_FEAT_NOTIFICATION_PENDING_INT	(1)
#define FFA_FEAT_SCHEDULE_RECEIVER_INT		(2)
#define FFA_FEAT_MANAGED_EXIT_INT		(3)

static irqreturn_t ffa_sched_recv_irq_handler(int irq, void *irq_data)
{
	struct ffa_pcpu_irq *pcpu = irq_data;
	struct ffa_drv_info *info = pcpu->info;

	queue_work(info->notif_pcpu_wq, &info->sched_recv_irq_work);

	return IRQ_HANDLED;
}

static irqreturn_t notif_pend_irq_handler(int irq, void *irq_data)
{
	struct ffa_pcpu_irq *pcpu = irq_data;
	struct ffa_drv_info *info = pcpu->info;

	queue_work_on(smp_processor_id(), info->notif_pcpu_wq,
		      &info->notif_pcpu_work);

	return IRQ_HANDLED;
}

static void ffa_sched_recv_irq_work_fn(struct work_struct *work)
{
	ffa_notification_info_get();
}

static int ffa_irq_map(u32 id)
{
	char *err_str;
	int ret, irq, intid;

	if (id == FFA_FEAT_NOTIFICATION_PENDING_INT)
		err_str = "Notification Pending Interrupt";
	else if (id == FFA_FEAT_SCHEDULE_RECEIVER_INT)
		err_str = "Schedule Receiver Interrupt";
	else
		err_str = "Unknown ID";

	/* The returned intid is assumed to be SGI donated to NS world */
	ret = ffa_features(id, 0, &intid, NULL);
	if (ret < 0) {
		if (ret != -EOPNOTSUPP)
			pr_err("Failed to retrieve FF-A %s %u\n", err_str, id);
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
		oirq.args[0] = intid;
		irq = irq_create_of_mapping(&oirq);
		of_node_put(gic);
#ifdef CONFIG_ACPI
	} else {
		irq = acpi_register_gsi(NULL, intid, ACPI_EDGE_SENSITIVE,
					ACPI_ACTIVE_HIGH);
#endif
	}

	if (irq <= 0) {
		pr_err("Failed to create IRQ mapping!\n");
		return -ENODATA;
	}

	return irq;
}

static void ffa_irq_unmap(unsigned int irq)
{
	if (!irq)
		return;
	irq_dispose_mapping(irq);
}

static int ffa_cpuhp_pcpu_irq_enable(unsigned int cpu)
{
	if (drv_info->sched_recv_irq)
		enable_percpu_irq(drv_info->sched_recv_irq, IRQ_TYPE_NONE);
	if (drv_info->notif_pend_irq)
		enable_percpu_irq(drv_info->notif_pend_irq, IRQ_TYPE_NONE);
	return 0;
}

static int ffa_cpuhp_pcpu_irq_disable(unsigned int cpu)
{
	if (drv_info->sched_recv_irq)
		disable_percpu_irq(drv_info->sched_recv_irq);
	if (drv_info->notif_pend_irq)
		disable_percpu_irq(drv_info->notif_pend_irq);
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

	if (drv_info->notif_pend_irq)
		free_percpu_irq(drv_info->notif_pend_irq, drv_info->irq_pcpu);

	if (drv_info->irq_pcpu) {
		free_percpu(drv_info->irq_pcpu);
		drv_info->irq_pcpu = NULL;
	}
}

static int ffa_init_pcpu_irq(void)
{
	struct ffa_pcpu_irq __percpu *irq_pcpu;
	int ret, cpu;

	irq_pcpu = alloc_percpu(struct ffa_pcpu_irq);
	if (!irq_pcpu)
		return -ENOMEM;

	for_each_present_cpu(cpu)
		per_cpu_ptr(irq_pcpu, cpu)->info = drv_info;

	drv_info->irq_pcpu = irq_pcpu;

	if (drv_info->sched_recv_irq) {
		ret = request_percpu_irq(drv_info->sched_recv_irq,
					 ffa_sched_recv_irq_handler,
					 "ARM-FFA-SRI", irq_pcpu);
		if (ret) {
			pr_err("Error registering percpu SRI nIRQ %d : %d\n",
			       drv_info->sched_recv_irq, ret);
			drv_info->sched_recv_irq = 0;
			return ret;
		}
	}

	if (drv_info->notif_pend_irq) {
		ret = request_percpu_irq(drv_info->notif_pend_irq,
					 notif_pend_irq_handler,
					 "ARM-FFA-NPI", irq_pcpu);
		if (ret) {
			pr_err("Error registering percpu NPI nIRQ %d : %d\n",
			       drv_info->notif_pend_irq, ret);
			drv_info->notif_pend_irq = 0;
			return ret;
		}
	}

	INIT_WORK(&drv_info->sched_recv_irq_work, ffa_sched_recv_irq_work_fn);
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
	ffa_irq_unmap(drv_info->sched_recv_irq);
	drv_info->sched_recv_irq = 0;
	ffa_irq_unmap(drv_info->notif_pend_irq);
	drv_info->notif_pend_irq = 0;

	if (drv_info->bitmap_created) {
		ffa_notification_bitmap_destroy();
		drv_info->bitmap_created = false;
	}
	drv_info->notif_enabled = false;
}

static void ffa_notifications_setup(void)
{
	int ret;

	ret = ffa_features(FFA_NOTIFICATION_BITMAP_CREATE, 0, NULL, NULL);
	if (!ret) {
		ret = ffa_notification_bitmap_create();
		if (ret) {
			pr_err("Notification bitmap create error %d\n", ret);
			return;
		}

		drv_info->bitmap_created = true;
	}

	ret = ffa_irq_map(FFA_FEAT_SCHEDULE_RECEIVER_INT);
	if (ret > 0)
		drv_info->sched_recv_irq = ret;

	ret = ffa_irq_map(FFA_FEAT_NOTIFICATION_PENDING_INT);
	if (ret > 0)
		drv_info->notif_pend_irq = ret;

	if (!drv_info->sched_recv_irq && !drv_info->notif_pend_irq)
		goto cleanup;

	ret = ffa_init_pcpu_irq();
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
	u32 buf_sz;
	size_t rxtx_bufsz = SZ_4K;

	ret = ffa_transport_init(&invoke_ffa_fn);
	if (ret)
		return ret;

	drv_info = kzalloc(sizeof(*drv_info), GFP_KERNEL);
	if (!drv_info)
		return -ENOMEM;

	ret = ffa_version_check(&drv_info->version);
	if (ret)
		goto free_drv_info;

	if (ffa_id_get(&drv_info->vm_id)) {
		pr_err("failed to obtain VM id for self\n");
		ret = -ENODEV;
		goto free_drv_info;
	}

	ret = ffa_features(FFA_FN_NATIVE(RXTX_MAP), 0, &buf_sz, NULL);
	if (!ret) {
		if (RXTX_MAP_MIN_BUFSZ(buf_sz) == 1)
			rxtx_bufsz = SZ_64K;
		else if (RXTX_MAP_MIN_BUFSZ(buf_sz) == 2)
			rxtx_bufsz = SZ_16K;
		else
			rxtx_bufsz = SZ_4K;
	}

	drv_info->rxtx_bufsz = rxtx_bufsz;
	drv_info->rx_buffer = alloc_pages_exact(rxtx_bufsz, GFP_KERNEL);
	if (!drv_info->rx_buffer) {
		ret = -ENOMEM;
		goto free_pages;
	}

	drv_info->tx_buffer = alloc_pages_exact(rxtx_bufsz, GFP_KERNEL);
	if (!drv_info->tx_buffer) {
		ret = -ENOMEM;
		goto free_pages;
	}

	ret = ffa_rxtx_map(virt_to_phys(drv_info->tx_buffer),
			   virt_to_phys(drv_info->rx_buffer),
			   rxtx_bufsz / FFA_PAGE_SIZE);
	if (ret) {
		pr_err("failed to register FFA RxTx buffers\n");
		goto free_pages;
	}

	mutex_init(&drv_info->rx_lock);
	mutex_init(&drv_info->tx_lock);

	ffa_drvinfo_flags_init();

	ffa_notifications_setup();

	ret = ffa_setup_partitions();
	if (!ret)
		return ret;

	pr_err("failed to setup partitions\n");
	ffa_notifications_cleanup();
free_pages:
	if (drv_info->tx_buffer)
		free_pages_exact(drv_info->tx_buffer, rxtx_bufsz);
	free_pages_exact(drv_info->rx_buffer, rxtx_bufsz);
free_drv_info:
	kfree(drv_info);
	return ret;
}
module_init(ffa_init);

static void __exit ffa_exit(void)
{
	ffa_notifications_cleanup();
	ffa_partitions_cleanup();
	ffa_rxtx_unmap(drv_info->vm_id);
	free_pages_exact(drv_info->tx_buffer, drv_info->rxtx_bufsz);
	free_pages_exact(drv_info->rx_buffer, drv_info->rxtx_bufsz);
	kfree(drv_info);
}
module_exit(ffa_exit);

MODULE_ALIAS("arm-ffa");
MODULE_AUTHOR("Sudeep Holla <sudeep.holla@arm.com>");
MODULE_DESCRIPTION("Arm FF-A interface driver");
MODULE_LICENSE("GPL v2");
