/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef __HAB_H
#define __HAB_H

#include "hab_os.h"	/* OS-specific part in the core header file */

enum hab_payload_type {
	HAB_PAYLOAD_TYPE_MSG = 0x0,
	HAB_PAYLOAD_TYPE_INIT,
	HAB_PAYLOAD_TYPE_INIT_ACK,
	HAB_PAYLOAD_TYPE_INIT_DONE,
	HAB_PAYLOAD_TYPE_EXPORT,
	HAB_PAYLOAD_TYPE_EXPORT_ACK,
	HAB_PAYLOAD_TYPE_PROFILE,
	HAB_PAYLOAD_TYPE_CLOSE,
	HAB_PAYLOAD_TYPE_INIT_CANCEL,
	HAB_PAYLOAD_TYPE_SCHE_MSG,
	HAB_PAYLOAD_TYPE_SCHE_MSG_ACK,
	HAB_PAYLOAD_TYPE_SCHE_RESULT_REQ,
	HAB_PAYLOAD_TYPE_SCHE_RESULT_RSP,
	HAB_PAYLOAD_TYPE_IMPORT,
	HAB_PAYLOAD_TYPE_IMPORT_ACK,
	HAB_PAYLOAD_TYPE_IMPORT_ACK_FAIL,
	HAB_PAYLOAD_TYPE_UNIMPORT,
	HAB_PAYLOAD_TYPE_MAX,
};
#define LOOPBACK_DOM 0xFF

/*
 * Tuning required. If there are multiple clients, the aging of previous
 * "request" might be discarded
 */
#define Q_AGE_THRESHOLD  1000000

/* match the name to dtsi if for real HYP framework */
#define DEVICE_AUD1_NAME "hab_aud1"
#define DEVICE_AUD2_NAME "hab_aud2"
#define DEVICE_AUD3_NAME "hab_aud3"
#define DEVICE_AUD4_NAME "hab_aud4"
#define DEVICE_CAM1_NAME "hab_cam1"
#define DEVICE_CAM2_NAME "hab_cam2"
#define DEVICE_DISP1_NAME "hab_disp1"
#define DEVICE_DISP2_NAME "hab_disp2"
#define DEVICE_DISP3_NAME "hab_disp3"
#define DEVICE_DISP4_NAME "hab_disp4"
#define DEVICE_DISP5_NAME "hab_disp5"
#define DEVICE_GFX_NAME "hab_ogles"
#define DEVICE_VID_NAME "hab_vid"
#define DEVICE_VID2_NAME "hab_vid2"
#define DEVICE_VID3_NAME "hab_vid3"
#define DEVICE_MISC_NAME "hab_misc"
#define DEVICE_QCPE1_NAME "hab_qcpe_vm1"
#define DEVICE_CLK1_NAME "hab_clock_vm1"
#define DEVICE_CLK2_NAME "hab_clock_vm2"
#define DEVICE_FDE1_NAME "hab_fde1"
#define DEVICE_BUFFERQ1_NAME "hab_bufferq1"
#define DEVICE_DATA1_NAME "hab_data_network1"
#define DEVICE_DATA2_NAME "hab_data_network2"
#define DEVICE_HSI2S1_NAME "hab_hsi2s1"
#define DEVICE_XVM1_NAME "hab_xvm1"
#define DEVICE_XVM2_NAME "hab_xvm2"
#define DEVICE_XVM3_NAME "hab_xvm3"
#define DEVICE_VNW1_NAME "hab_vnw1"
#define DEVICE_EXT1_NAME "hab_ext1"
#define DEVICE_GPCE1_NAME "hab_gpce1"

#define HABCFG_MMID_NUM        26
#define HAB_MMID_ALL_AREA      0

/* make sure concascaded name is less than this value */
#define MAX_VMID_NAME_SIZE 30

/*
 * The maximum value of payload_count in struct export_desc
 * Max u32_t size_bytes from hab_ioctl.h(0xFFFFFFFF) / page size(0x1000)
 */
#define MAX_EXP_PAYLOAD_COUNT 0xFFFFF

#define HABCFG_FILE_SIZE_MAX   256
#define HABCFG_MMID_AREA_MAX   (MM_ID_MAX/100)

#define HABCFG_VMID_MAX        16
#define HABCFG_VMID_INVALID    (-1)
#define HABCFG_VMID_DONT_CARE  (-2)

#define HABCFG_ID_LINE_LIMIT   ","
#define HABCFG_ID_VMID         "VMID="
#define HABCFG_ID_BE           "BE="
#define HABCFG_ID_FE           "FE="
#define HABCFG_ID_MMID         "MMID="
#define HABCFG_ID_RANGE        "-"
#define HABCFG_ID_DONTCARE     "X"

#define HABCFG_FOUND_VMID      1
#define HABCFG_FOUND_FE_MMIDS  2
#define HABCFG_FOUND_BE_MMIDS  3
#define HABCFG_FOUND_NOTHING   (-1)

#define HABCFG_BE_FALSE        0
#define HABCFG_BE_TRUE         1

#define HABCFG_GET_VMID(_local_cfg_, _vmid_) \
	((settings)->vmid_mmid_list[_vmid_].vmid)
#define HABCFG_GET_MMID(_local_cfg_, _vmid_, _mmid_) \
	((settings)->vmid_mmid_list[_vmid_].mmid[_mmid_])
#define HABCFG_GET_BE(_local_cfg_, _vmid_, _mmid_) \
	((settings)->vmid_mmid_list[_vmid_].is_listener[_mmid_])
#define HABCFG_GET_KERNEL(_local_cfg_, _vmid_, _mmid_) \
	((settings)->vmid_mmid_list[_vmid_].kernel_only[_mmid_])

struct hab_header {
	uint32_t id_type;
	uint32_t payload_size;
	uint32_t session_id;
	uint32_t signature;
	uint32_t sequence;
} __packed;

/* "Size" of the HAB_HEADER_ID and HAB_VCID_ID must match */
#define HAB_HEADER_TYPE_SHIFT 16
#define HAB_HEADER_EXT_TYPE_SHIFT 0
#define HAB_HEADER_ID_SHIFT 20
/*
 * On HQX platforms, the maximum payload size is
 * PIPE_SHMEM_SIZE - sizeof(hab_header)
 * 500KB is big enough for now and leave a margin for other usage
 */
#define HAB_HEADER_SIZE_MAX  0x0007D000
#define HAB_HEADER_TYPE_MASK 0x000F0000
/* TYPE_LEN is the number of 1 bit in TYPE_MASK */
#define HAB_HEADER_TYPE_LEN 4
#define HAB_HEADER_EXT_TYPE_MASK 0x0000000F
#define HAB_HEADER_ID_MASK   0xFFF00000
#define HAB_HEADER_INITIALIZER {0}

#define HAB_MMID_GET_MAJOR(mmid) (mmid & 0xFFFF)
#define HAB_MMID_GET_MINOR(mmid) ((mmid>>16) & 0xFF)

#define HAB_VCID_ID_SHIFT 0
#define HAB_VCID_DOMID_SHIFT 12
#define HAB_VCID_MMID_SHIFT 20
#define HAB_VCID_ID_MASK 0x00000FFF
#define HAB_VCID_DOMID_MASK 0x000FF000
#define HAB_VCID_MMID_MASK 0xFFF00000
#define HAB_VCID_GET_ID(vcid) \
	(((vcid) & HAB_VCID_ID_MASK) >> HAB_VCID_ID_SHIFT)


#define HAB_HEADER_SET_SESSION_ID(header, sid) \
	((header).session_id = (sid))

#define HAB_HEADER_SET_SIZE(header, size) \
	((header).payload_size = (size))

#define HAB_HEADER_SET_TYPE(header, type) \
	((header).id_type = ((header).id_type & \
			(~(HAB_HEADER_TYPE_MASK | HAB_HEADER_EXT_TYPE_MASK))) | \
			(((type) << HAB_HEADER_TYPE_SHIFT) & \
			HAB_HEADER_TYPE_MASK) | \
			((((type) >> HAB_HEADER_TYPE_LEN) << HAB_HEADER_EXT_TYPE_SHIFT) & \
			HAB_HEADER_EXT_TYPE_MASK))

#define HAB_HEADER_SET_ID(header, id) \
	((header).id_type = ((header).id_type & \
			(~HAB_HEADER_ID_MASK)) | \
			((HAB_VCID_GET_ID(id) << HAB_HEADER_ID_SHIFT) & \
			HAB_HEADER_ID_MASK))

#define HAB_HEADER_GET_SIZE(header) \
	((header).payload_size)

#define HAB_HEADER_GET_TYPE(header) \
	((((header).id_type & \
		HAB_HEADER_TYPE_MASK) >> HAB_HEADER_TYPE_SHIFT) | \
		(((header).id_type & HAB_HEADER_EXT_TYPE_MASK) << HAB_HEADER_TYPE_LEN))

#define HAB_HEADER_GET_ID(header) \
	((((header).id_type & HAB_HEADER_ID_MASK) >> \
	(HAB_HEADER_ID_SHIFT - HAB_VCID_ID_SHIFT)) & HAB_VCID_ID_MASK)

#define HAB_HEADER_GET_SESSION_ID(header) ((header).session_id)

#define HAB_HS_TIMEOUT (10*1000*1000)
#define HAB_HEAD_SIGNATURE 0xBEE1BEE1

/* only used when vchan is not existed */
#define HAB_VCID_UNIMPORT 0x1
#define HAB_SESSIONID_UNIMPORT 0x1

/* 1 - enhanced memory sharing protocol with sync import and async unimport */
#define HAB_VER_PROT 1

struct physical_channel {
	struct list_head node;
	char name[MAX_VMID_NAME_SIZE];
	int is_be;
	struct kref refcount;
	struct hab_device *habdev;
	struct idr vchan_idr;
	spinlock_t vid_lock;

	struct idr expid_idr;
	spinlock_t expid_lock;

	void *hyp_data;
	int dom_id; /* BE role: remote vmid; FE role: don't care */
	int vmid_local; /* from DT or hab_config */
	int vmid_remote;
	char vmname_local[12]; /* from DT */
	char vmname_remote[12];
	int closed;

	spinlock_t rxbuf_lock;

	/* debug only */
	uint32_t sequence_tx;
	uint32_t sequence_rx;
	uint32_t status;

	/* vchans on this pchan */
	struct list_head vchannels;
	int vcnt;
	rwlock_t vchans_lock;
	int kernel_only;
	uint32_t mem_proto;
};
/* this payload has to be used together with type */
struct hab_open_send_data {
	int vchan_id;
	int sub_id;
	int open_id;
	int ver_fe;
	int ver_be;
	int ver_proto;
};

struct hab_open_request {
	int type;
	struct physical_channel *pchan;
	struct hab_open_send_data xdata;
};

struct hab_open_node {
	struct hab_open_request request;
	struct list_head node;
	int64_t age; /* sec */
};

struct hab_export_ack {
	uint32_t export_id;
	int32_t vcid_local;
	int32_t vcid_remote;
};

struct hab_export_ack_recvd {
	struct hab_export_ack ack;
	struct list_head node;
	int age;
};

struct hab_import_ack {
	uint32_t export_id;
	int32_t vcid_local;
	int32_t vcid_remote;
	uint32_t imp_whse_added; /* indicating exp node added into imp whse */
};

struct hab_import_ack_recvd {
	struct hab_import_ack ack;
	struct list_head node;
	int age;
};

struct hab_import_data {
	uint32_t exp_id;
	uint32_t page_cnt;
	uint32_t reserved;
} __packed;

struct hab_message {
	struct list_head node;
	size_t sizebytes;
	bool scatter;
	uint32_t sequence_rx;
	uint32_t data[];
};

/* for all the pchans of same kind */
struct hab_device {
	char name[MAX_VMID_NAME_SIZE];
	uint32_t id;
	struct list_head pchannels;
	int pchan_cnt;
	rwlock_t pchan_lock;
	struct list_head openq_list; /* received */
	spinlock_t openlock;
	wait_queue_head_t openq;
	int openq_cnt;
};

struct uhab_context {
	struct list_head node; /* managed by the driver */
	struct kref refcount;
	struct work_struct destroy_work;

	struct list_head vchannels;
	int vcnt;

	struct list_head exp_whse;
	rwlock_t exp_lock;
	uint32_t export_total;

	wait_queue_head_t exp_wq;
	struct list_head exp_rxq;
	spinlock_t expq_lock;

	HAB_RB_ROOT imp_whse;
	spinlock_t imp_lock;
	uint32_t import_total;

	wait_queue_head_t imp_wq;
	struct list_head imp_rxq;
	spinlock_t impq_lock;

	void *import_ctx;

	struct list_head pending_open; /* sent to remote */
	int pending_cnt;

	rwlock_t ctx_lock;
	int closing;
	int kernel;
	int owner;
	/*
	 * only used for user-space hab client
	 * if created through /dev/hab-* node, mmid_grp_index = MMID / 100
	 * if created through /dev/hab node, mmid_grp_index = 0
	 */
	int mmid_grp_index;

	int lb_be; /* loopback only */
};

/*
 * array to describe the VM and its MMID configuration as
 * what is connected to so this is describing a pchan's remote side
 */
struct vmid_mmid_desc {
	int vmid; /* remote vmid  */
	int mmid[HABCFG_MMID_AREA_MAX+1]; /* selected or not */
	int is_listener[HABCFG_MMID_AREA_MAX+1]; /* yes or no */
	int kernel_only[HABCFG_MMID_AREA_MAX+1]; /* yes or no */
};

struct local_vmid {
	int32_t self; /* only this field is for local */
	struct vmid_mmid_desc vmid_mmid_list[HABCFG_VMID_MAX];
};

struct hab_driver {
	/* hab driver has many char devices, so we need an array of struct device pointers. */
	struct device **dev;
	struct cdev *cdev;
	dev_t major;
	struct class *class;

	int ndevices;
	struct hab_device *devp;
	struct uhab_context *kctx;

	struct list_head uctx_list;
	int ctx_cnt;
	spinlock_t drvlock;

	struct list_head imp_list;
	int imp_cnt;
	spinlock_t imp_lock;

	struct list_head reclaim_list;
	spinlock_t reclaim_lock;
	struct work_struct reclaim_work;

	struct local_vmid settings; /* parser results */

	int b_server_dom;
	int b_loopback_be; /* only allow 2 apps simultaneously 1 fe 1 be */
	int b_loopback;

	void *hyp_priv; /* hypervisor plug-in storage */

	void *hab_vmm_handle;

	int hab_init_success;
};

struct virtual_channel {
	struct list_head node; /* for ctx */
	struct list_head pnode; /* for pchan */
	/*
	 * refcount is used to track the references from hab core to the virtual
	 * channel such as references from physical channels,
	 * i.e. references from the "other" side
	 */
	struct kref refcount;
	struct physical_channel *pchan;
	struct uhab_context *ctx;
	struct list_head rx_list;
	wait_queue_head_t rx_queue;
	spinlock_t rx_lock;
	int id;
	int otherend_id;
	int otherend_closed;
	uint32_t session_id;

	/*
	 * set when local close() is called explicitly. vchan could be
	 * used in hab-recv-msg() path (2) then close() is called (1).
	 * this is same case as close is not called and no msg path
	 */
	int closed;
	int forked; /* if fork is detected and assume only once */
	/* stats */
	atomic64_t tx_cnt; /* total succeeded tx */
	atomic64_t rx_cnt; /* total succeeded rx */
	int rx_inflight; /* rx in progress/blocking */
};

/*
 * Struct shared between local and remote, contents
 * are composed by exporter, the importer only writes
 * to pdata and local (exporter) domID
 */
struct export_desc {
	uint32_t  export_id;
	int       readonly;
	uint64_t  import_index;

	struct virtual_channel *vchan; /* vchan could be freed earlier */
	struct uhab_context *ctx;
	struct physical_channel *pchan;

	int32_t             vcid_local;
	int32_t             vcid_remote;
	int                 domid_local;
	int                 domid_remote;
	int                 flags;

	struct list_head    node;
	void *kva;
	int                 payload_count; /* number of the pages */
	unsigned char       payload[1];
} __packed;

/*
 * hab_mem_import       hab_mem_unimport
 * --------------       ----------------
 *      lock                 lock
 *      query                query
 *      unlock               unlock
 *
 *      use                  free
 *
 *      ret                  ret
 *
 * There are three scenarios to handle.
 * First is:
 * 1.thread1 enters import and finds out the exp desc, then unlock,
 * 2.thread2 is scheduled to run on the same CPU,
 * 3.it enters unimport, finds out the same exp desc, frees it and returns,
 * 4.cpu is back to run thread1,
 * 5.UAF occurs once thread1 uses this exp desc.
 * We could use EXP_DESC_IMPORTED at the end of import and add query check
 * in unimport to sync this access.
 * A more complicated case is:
 * 1.thread1 has completed the import,
 * 2.thread2 enters import and gets the exp desc,
 * 3.at this time point, thread3 which calls unimport could find out this
 * exp desc due to its current state is EXP_DESC_IMPORTED,
 * 4.if thread3 frees it, thread2 uses it afterward, will also occur UAF.
 * Add query check with EXP_DESC_IMPORTED in import could avoid this,
 * but it can not deal with the 3rd scenario:
 * 1.thread1 and thread2 call import and both find out this exp desc,
 * 2.thread1 runs quickly and returns from import,
 * 3.then thread3 calls unimport and frees the exp desc,
 * 4.UAF occurs once thread2 uses this exp desc afterward.
 * In import, querying exp desc is a critical section, should prevent
 * thread2 entering if thread1 is in. so EXP_DESC_IMPORTING is here.
 */
enum exp_desc_state {
	EXP_DESC_INIT,
	EXP_DESC_IMPORTING,	/* hab_mem_import is in progress */
	EXP_DESC_IMPORTED,	/* hab_mem_import is called and returns success */
};

enum export_state {
	HAB_EXP_EXPORTING,
	HAB_EXP_SUCCESS,
};

struct export_desc_super {
	struct kref refcount;
	void *platform_data;
	unsigned long offset;
	unsigned int payload_size; /* size of the compressed pfn structure */

	enum exp_desc_state import_state;
	enum export_state exp_state;
	uint32_t remote_imported;

	HAB_RB_ENTRY node;

	/*
	 * exp must be the last member
	 * because it is a variable length struct with pfns as payload
	 */
	struct export_desc exp;
};

int hab_vchan_open(struct uhab_context *ctx,
		unsigned int mmid, int32_t *vcid,
		int32_t timeout, uint32_t flags);
int hab_vchan_close(struct uhab_context *ctx,
		int32_t vcid);
long hab_vchan_send(struct uhab_context *ctx,
		int vcid,
		size_t sizebytes,
		void *data,
		unsigned int flags);
int hab_vchan_recv(struct uhab_context *ctx,
		struct hab_message **msg,
		int vcid,
		int *rsize,
		unsigned int timeout,
		unsigned int flags);
void hab_vchan_stop(struct virtual_channel *vchan);
void hab_vchans_stop(struct physical_channel *pchan);
void hab_vchan_stop_notify(struct virtual_channel *vchan);
void hab_vchans_empty_wait(int vmid);

int hab_mem_export(struct uhab_context *ctx,
		struct hab_export *param, int kernel);
int hab_mem_import(struct uhab_context *ctx,
		struct hab_import *param, int kernel);
int hab_mem_unexport(struct uhab_context *ctx,
		struct hab_unexport *param, int kernel);
void habmem_export_get(struct export_desc_super *exp_super);
int habmem_export_put(struct export_desc_super *exp_super);

int hab_mem_unimport(struct uhab_context *ctx,
		struct hab_unimport *param, int kernel);

void habmem_remove_export(struct export_desc *exp);

/* memory hypervisor framework plugin I/F */
struct export_desc_super *habmem_add_export(
		struct virtual_channel *vchan,
		int sizebytes,
		uint32_t flags);

int habmem_hyp_grant_user(struct virtual_channel *vchan,
		unsigned long address,
		int page_count,
		int flags,
		int remotedom,
		int *compressed,
		int *compressed_size,
		int *export_id);

int habmem_hyp_grant(struct virtual_channel *vchan,
		unsigned long address,
		int page_count,
		int flags,
		int remotedom,
		int *compressed,
		int *compressed_size,
		int *export_id);

int habmem_hyp_revoke(void *expdata, uint32_t count);
int habmem_exp_release(struct export_desc_super *exp_super);

void *habmem_imp_hyp_open(void);
void habmem_imp_hyp_close(void *priv, int kernel);

int habmem_imp_hyp_map(void *imp_ctx, struct hab_import *param,
		struct export_desc *exp, int kernel);

int habmm_imp_hyp_unmap(void *imp_ctx, struct export_desc *exp, int kernel);

int habmem_imp_hyp_mmap(struct file *flip, struct vm_area_struct *vma);

int habmm_imp_hyp_map_check(void *imp_ctx, struct export_desc *exp);

void hab_msg_free(struct hab_message *message);
int hab_msg_dequeue(struct virtual_channel *vchan,
		struct hab_message **msg, int *rsize, unsigned int timeout,
		unsigned int flags);

int hab_msg_recv(struct physical_channel *pchan,
		struct hab_header *header);

void hab_open_request_init(struct hab_open_request *request,
		int type,
		struct physical_channel *pchan,
		int vchan_id,
		int sub_id,
		int open_id);
int hab_open_request_send(struct hab_open_request *request);
int hab_open_request_add(struct physical_channel *pchan,
		size_t sizebytes, int request_type);
void hab_open_request_free(struct hab_open_request *request);
int hab_open_listen(struct uhab_context *ctx,
		struct hab_device *dev,
		struct hab_open_request *listen,
		struct hab_open_request **recv_request,
		int ms_timeout,
		uint32_t flags);

struct virtual_channel *hab_vchan_alloc(struct uhab_context *ctx,
		struct physical_channel *pchan, int openid);
struct virtual_channel *hab_vchan_get(struct physical_channel *pchan,
						  struct hab_header *header);
void hab_vchan_put(struct virtual_channel *vchan);

struct virtual_channel *hab_get_vchan_fromvcid(int32_t vcid,
		struct uhab_context *ctx, int ignore_remote);
struct physical_channel *hab_pchan_alloc(struct hab_device *habdev,
		int otherend_id);
struct physical_channel *hab_pchan_find_domid(struct hab_device *dev,
		int dom_id);
int hab_vchan_find_domid(struct virtual_channel *vchan);

void hab_pchan_get(struct physical_channel *pchan);
void hab_pchan_put(struct physical_channel *pchan);

struct uhab_context *hab_ctx_alloc(int kernel);

void hab_ctx_free(struct kref *ref);

void hab_ctx_free_fn(struct uhab_context *ctx);
void hab_ctx_free_os(struct kref *ref);

static inline void hab_ctx_get(struct uhab_context *ctx)
{
	if (ctx)
		kref_get(&ctx->refcount);
}

static inline void hab_ctx_put(struct uhab_context *ctx)
{
	if (ctx)
		kref_put(&ctx->refcount, hab_ctx_free);
}

void hab_send_close_msg(struct virtual_channel *vchan);
void hab_send_unimport_msg(struct virtual_channel *vchan, uint32_t exp_id);

int hab_hypervisor_register(void);
int hab_hypervisor_register_post(void);
int hab_hypervisor_register_os(void);
int hab_hypervisor_unregister_os(void);
void hab_hypervisor_unregister(void);
void hab_hypervisor_unregister_common(void);
int habhyp_commdev_alloc(void **commdev, int is_be, char *name,
		int vmid_remote, struct hab_device *mmid_device);
int habhyp_commdev_dealloc(void *commdev);
void habhyp_commdev_dealloc_os(void *commdev);
int habhyp_commdev_create_dispatcher(struct physical_channel *pchan);

int physical_channel_read(struct physical_channel *pchan,
		void *payload,
		size_t read_size);

int physical_channel_send(struct physical_channel *pchan,
		struct hab_header *header,
		void *payload,
		unsigned int flags);

void physical_channel_rx_dispatch(unsigned long physical_channel);
void physical_channel_rx_dispatch_common(unsigned long physical_channel);

int loopback_pchan_create(struct hab_device *dev, char *pchan_name);

int hab_parse(struct local_vmid *settings);

int do_hab_parse(void);

int fill_default_gvm_settings(struct local_vmid *settings,
		int vmid_local, int mmid_start, int mmid_end);

bool hab_is_loopback(void);

int hab_vchan_query(struct uhab_context *ctx, int32_t vcid, uint64_t *ids,
		char *names, size_t name_size, uint32_t flags);

struct hab_device *find_hab_device(unsigned int mm_id);

unsigned int get_refcnt(struct kref ref);

int hab_open_pending_enter(struct uhab_context *ctx,
		struct physical_channel *pchan,
		struct hab_open_node *pending);

int hab_open_pending_exit(struct uhab_context *ctx,
		struct physical_channel *pchan,
		struct hab_open_node *pending);

int hab_open_cancel_notify(struct hab_open_request *request);

int hab_open_receive_cancel(struct physical_channel *pchan,
		size_t sizebytes);

int hab_stat_init(struct hab_driver *drv);
int hab_stat_deinit(struct hab_driver *drv);
int hab_stat_show_vchan(struct hab_driver *drv, char *buf, int sz);
int hab_stat_show_ctx(struct hab_driver *drv, char *buf, int sz);
int hab_stat_show_expimp(struct hab_driver *drv, int pid, char *buf, int sz);
int hab_stat_show_reclaim(struct hab_driver *drv, char *buf, int sz);
int hab_stat_init_sub(struct hab_driver *drv);
int hab_stat_deinit_sub(struct hab_driver *drv);

static inline void hab_spin_lock(spinlock_t *lock, int irqs_disabled)
{
	if (irqs_disabled)
		spin_lock(lock);
	else
		spin_lock_bh(lock);
}

static inline void hab_spin_unlock(spinlock_t *lock, int irqs_disabled)
{
	if (irqs_disabled)
		spin_unlock(lock);
	else
		spin_unlock_bh(lock);
}

static inline void hab_write_lock(rwlock_t *lock, int no_touch_bh)
{
	if (no_touch_bh)
		write_lock(lock);
	else
		write_lock_bh(lock);
}

static inline void hab_write_unlock(rwlock_t *lock, int no_touch_bh)
{
	if (no_touch_bh)
		write_unlock(lock);
	else
		write_unlock_bh(lock);
}

/* Global singleton HAB instance */
extern struct hab_driver hab_driver;

int dump_hab_get_file_name(char *file_time, int ft_size);
int dump_hab_open(void);
void dump_hab_close(void);
int dump_hab_buf(void *buf, int size);
void hab_pipe_read_dump(struct physical_channel *pchan);
void dump_hab(int mmid);
void dump_hab_wq(struct physical_channel *pchan);
int hab_stat_log(struct physical_channel **pchans, int pchan_cnt, char *dest,
			int dest_size);
int hab_stat_buffer_print(char *dest,
		int dest_size, const char *fmt, ...);
int hab_create_cdev_node(int mmid_grp_index);

struct export_desc_super *hab_rb_exp_insert(struct rb_root *root, struct export_desc_super *exp_s);
struct export_desc_super *hab_rb_exp_find(struct rb_root *root, struct export_desc_super *key);

#endif /* __HAB_H */
