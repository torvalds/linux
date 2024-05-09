/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2023 Intel Corporation */

#ifndef ADF_RL_H_
#define ADF_RL_H_

#include <linux/mutex.h>
#include <linux/types.h>

struct adf_accel_dev;

#define RL_ROOT_MAX		4
#define RL_CLUSTER_MAX		16
#define RL_LEAF_MAX		64
#define RL_NODES_CNT_MAX	(RL_ROOT_MAX + RL_CLUSTER_MAX + RL_LEAF_MAX)
#define RL_RP_CNT_PER_LEAF_MAX	4U
#define RL_RP_CNT_MAX		64
#define RL_SLA_EMPTY_ID		-1
#define RL_PARENT_DEFAULT_ID	-1

enum rl_node_type {
	RL_ROOT,
	RL_CLUSTER,
	RL_LEAF,
};

enum adf_base_services {
	ADF_SVC_ASYM = 0,
	ADF_SVC_SYM,
	ADF_SVC_DC,
	ADF_SVC_NONE,
};

/**
 * struct adf_rl_sla_input_data - ratelimiting user input data structure
 * @rp_mask: 64 bit bitmask of ring pair IDs which will be assigned to SLA.
 *	     Eg. 0x5 -> RP0 and RP2 assigned; 0xA005 -> RP0,2,13,15 assigned.
 * @sla_id: ID of current SLA for operations update, rm, get. For the add
 *	    operation, this field will be updated with the ID of the newly
 *	    added SLA
 * @parent_id: ID of the SLA to which the current one should be assigned.
 *	       Set to -1 to refer to the default parent.
 * @cir: Committed information rate. Rate guaranteed to be achieved. Input value
 *	 is expressed in permille scale, i.e. 1000 refers to the maximum
 *	 device throughput for a selected service.
 * @pir: Peak information rate. Maximum rate available that the SLA can achieve.
 *	 Input value is expressed in permille scale, i.e. 1000 refers to
 *	 the maximum device throughput for a selected service.
 * @type: SLA type: root, cluster, node
 * @srv: Service associated to the SLA: asym, sym dc.
 *
 * This structure is used to perform operations on an SLA.
 * Depending on the operation, some of the parameters are ignored.
 * The following list reports which parameters should be set for each operation.
 *	- add: all except sla_id
 *	- update: cir, pir, sla_id
 *	- rm: sla_id
 *	- rm_all: -
 *	- get: sla_id
 *	- get_capability_rem: srv, sla_id
 */
struct adf_rl_sla_input_data {
	u64 rp_mask;
	int sla_id;
	int parent_id;
	unsigned int cir;
	unsigned int pir;
	enum rl_node_type type;
	enum adf_base_services srv;
};

struct rl_slice_cnt {
	u8 dcpr_cnt;
	u8 pke_cnt;
	u8 cph_cnt;
};

struct adf_rl_interface_data {
	struct adf_rl_sla_input_data input;
	enum adf_base_services cap_rem_srv;
	struct rw_semaphore lock;
};

struct adf_rl_hw_data {
	u32 scale_ref;
	u32 scan_interval;
	u32 r2l_offset;
	u32 l2c_offset;
	u32 c2s_offset;
	u32 pciin_tb_offset;
	u32 pciout_tb_offset;
	u32 pcie_scale_mul;
	u32 pcie_scale_div;
	u32 dcpr_correction;
	u32 max_tp[RL_ROOT_MAX];
	struct rl_slice_cnt slices;
};

/**
 * struct adf_rl - ratelimiting data structure
 * @accel_dev: pointer to acceleration device data
 * @device_data: pointer to rate limiting data specific to a device type (or revision)
 * @sla: array of pointers to SLA objects
 * @root: array of pointers to root type SLAs, element number reflects node_id
 * @cluster: array of pointers to cluster type SLAs, element number reflects node_id
 * @leaf: array of pointers to leaf type SLAs, element number reflects node_id
 * @rp_in_use: array of ring pair IDs already used in one of SLAs
 * @rl_lock: mutex object which is protecting data in this structure
 * @input: structure which is used for holding the data received from user
 */
struct adf_rl {
	struct adf_accel_dev *accel_dev;
	struct adf_rl_hw_data *device_data;
	/* mapping sla_id to SLA objects */
	struct rl_sla *sla[RL_NODES_CNT_MAX];
	struct rl_sla *root[RL_ROOT_MAX];
	struct rl_sla *cluster[RL_CLUSTER_MAX];
	struct rl_sla *leaf[RL_LEAF_MAX];
	bool rp_in_use[RL_RP_CNT_MAX];
	/* Mutex protecting writing to SLAs lists */
	struct mutex rl_lock;
	struct adf_rl_interface_data user_input;
};

/**
 * struct rl_sla - SLA object data structure
 * @parent: pointer to the parent SLA (root/cluster)
 * @type: SLA type
 * @srv: service associated with this SLA
 * @sla_id: ID of the SLA, used as element number in SLA array and as identifier
 *	    shared with the user
 * @node_id: ID of node, each of SLA type have a separate ID list
 * @cir: committed information rate
 * @pir: peak information rate (PIR >= CIR)
 * @rem_cir: if this SLA is a parent then this field represents a remaining
 *	     value to be used by child SLAs.
 * @ring_pairs_ids: array with numeric ring pairs IDs assigned to this SLA
 * @ring_pairs_cnt: number of assigned ring pairs listed in the array above
 */
struct rl_sla {
	struct rl_sla *parent;
	enum rl_node_type type;
	enum adf_base_services srv;
	u32 sla_id;
	u32 node_id;
	u32 cir;
	u32 pir;
	u32 rem_cir;
	u16 ring_pairs_ids[RL_RP_CNT_PER_LEAF_MAX];
	u16 ring_pairs_cnt;
};

int adf_rl_add_sla(struct adf_accel_dev *accel_dev,
		   struct adf_rl_sla_input_data *sla_in);
int adf_rl_update_sla(struct adf_accel_dev *accel_dev,
		      struct adf_rl_sla_input_data *sla_in);
int adf_rl_get_sla(struct adf_accel_dev *accel_dev,
		   struct adf_rl_sla_input_data *sla_in);
int adf_rl_get_capability_remaining(struct adf_accel_dev *accel_dev,
				    enum adf_base_services srv, int sla_id);
int adf_rl_remove_sla(struct adf_accel_dev *accel_dev, u32 sla_id);
void adf_rl_remove_sla_all(struct adf_accel_dev *accel_dev, bool incl_default);

int adf_rl_init(struct adf_accel_dev *accel_dev);
int adf_rl_start(struct adf_accel_dev *accel_dev);
void adf_rl_stop(struct adf_accel_dev *accel_dev);
void adf_rl_exit(struct adf_accel_dev *accel_dev);

u32 adf_rl_calculate_pci_bw(struct adf_accel_dev *accel_dev, u32 sla_val,
			    enum adf_base_services svc_type, bool is_bw_out);
u32 adf_rl_calculate_ae_cycles(struct adf_accel_dev *accel_dev, u32 sla_val,
			       enum adf_base_services svc_type);
u32 adf_rl_calculate_slice_tokens(struct adf_accel_dev *accel_dev, u32 sla_val,
				  enum adf_base_services svc_type);

#endif /* ADF_RL_H_ */
