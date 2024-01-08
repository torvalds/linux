// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2023 Intel Corporation */

#define dev_fmt(fmt) "RateLimiting: " fmt

#include <asm/errno.h>
#include <asm/div64.h>

#include <linux/dev_printk.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/units.h>

#include "adf_accel_devices.h"
#include "adf_common_drv.h"
#include "adf_rl_admin.h"
#include "adf_rl.h"
#include "adf_sysfs_rl.h"

#define RL_TOKEN_GRANULARITY_PCIEIN_BUCKET	0U
#define RL_TOKEN_GRANULARITY_PCIEOUT_BUCKET	0U
#define RL_TOKEN_PCIE_SIZE			64
#define RL_TOKEN_ASYM_SIZE			1024
#define RL_CSR_SIZE				4U
#define RL_CAPABILITY_MASK			GENMASK(6, 4)
#define RL_CAPABILITY_VALUE			0x70
#define RL_VALIDATE_NON_ZERO(input)		((input) == 0)
#define ROOT_MASK				GENMASK(1, 0)
#define CLUSTER_MASK				GENMASK(3, 0)
#define LEAF_MASK				GENMASK(5, 0)

static int validate_user_input(struct adf_accel_dev *accel_dev,
			       struct adf_rl_sla_input_data *sla_in,
			       bool is_update)
{
	const unsigned long rp_mask = sla_in->rp_mask;
	size_t rp_mask_size;
	int i, cnt;

	if (sla_in->pir < sla_in->cir) {
		dev_notice(&GET_DEV(accel_dev),
			   "PIR must be >= CIR, setting PIR to CIR\n");
		sla_in->pir = sla_in->cir;
	}

	if (!is_update) {
		cnt = 0;
		rp_mask_size = sizeof(sla_in->rp_mask) * BITS_PER_BYTE;
		for_each_set_bit(i, &rp_mask, rp_mask_size) {
			if (++cnt > RL_RP_CNT_PER_LEAF_MAX) {
				dev_notice(&GET_DEV(accel_dev),
					   "Too many ring pairs selected for this SLA\n");
				return -EINVAL;
			}
		}

		if (sla_in->srv >= ADF_SVC_NONE) {
			dev_notice(&GET_DEV(accel_dev),
				   "Wrong service type\n");
			return -EINVAL;
		}

		if (sla_in->type > RL_LEAF) {
			dev_notice(&GET_DEV(accel_dev),
				   "Wrong node type\n");
			return -EINVAL;
		}

		if (sla_in->parent_id < RL_PARENT_DEFAULT_ID ||
		    sla_in->parent_id >= RL_NODES_CNT_MAX) {
			dev_notice(&GET_DEV(accel_dev),
				   "Wrong parent ID\n");
			return -EINVAL;
		}
	}

	return 0;
}

static int validate_sla_id(struct adf_accel_dev *accel_dev, int sla_id)
{
	struct rl_sla *sla;

	if (sla_id <= RL_SLA_EMPTY_ID || sla_id >= RL_NODES_CNT_MAX) {
		dev_notice(&GET_DEV(accel_dev), "Provided ID is out of bounds\n");
		return -EINVAL;
	}

	sla = accel_dev->rate_limiting->sla[sla_id];

	if (!sla) {
		dev_notice(&GET_DEV(accel_dev), "SLA with provided ID does not exist\n");
		return -EINVAL;
	}

	if (sla->type != RL_LEAF) {
		dev_notice(&GET_DEV(accel_dev), "This ID is reserved for internal use\n");
		return -EINVAL;
	}

	return 0;
}

/**
 * find_parent() - Find the parent for a new SLA
 * @rl_data: pointer to ratelimiting data
 * @sla_in: pointer to user input data for a new SLA
 *
 * Function returns a pointer to the parent SLA. If the parent ID is provided
 * as input in the user data, then such ID is validated and the parent SLA
 * is returned.
 * Otherwise, it returns the default parent SLA (root or cluster) for
 * the new object.
 *
 * Return:
 * * Pointer to the parent SLA object
 * * NULL - when parent cannot be found
 */
static struct rl_sla *find_parent(struct adf_rl *rl_data,
				  struct adf_rl_sla_input_data *sla_in)
{
	int input_parent_id = sla_in->parent_id;
	struct rl_sla *root = NULL;
	struct rl_sla *parent_sla;
	int i;

	if (sla_in->type == RL_ROOT)
		return NULL;

	if (input_parent_id > RL_PARENT_DEFAULT_ID) {
		parent_sla = rl_data->sla[input_parent_id];
		/*
		 * SLA can be a parent if it has the same service as the child
		 * and its type is higher in the hierarchy,
		 * for example the parent type of a LEAF must be a CLUSTER.
		 */
		if (parent_sla && parent_sla->srv == sla_in->srv &&
		    parent_sla->type == sla_in->type - 1)
			return parent_sla;

		return NULL;
	}

	/* If input_parent_id is not valid, get root for this service type. */
	for (i = 0; i < RL_ROOT_MAX; i++) {
		if (rl_data->root[i] && rl_data->root[i]->srv == sla_in->srv) {
			root = rl_data->root[i];
			break;
		}
	}

	if (!root)
		return NULL;

	/*
	 * If the type of this SLA is cluster, then return the root.
	 * Otherwise, find the default (i.e. first) cluster for this service.
	 */
	if (sla_in->type == RL_CLUSTER)
		return root;

	for (i = 0; i < RL_CLUSTER_MAX; i++) {
		if (rl_data->cluster[i] && rl_data->cluster[i]->parent == root)
			return rl_data->cluster[i];
	}

	return NULL;
}

static enum adf_cfg_service_type srv_to_cfg_svc_type(enum adf_base_services rl_srv)
{
	switch (rl_srv) {
	case ADF_SVC_ASYM:
		return ASYM;
	case ADF_SVC_SYM:
		return SYM;
	case ADF_SVC_DC:
		return COMP;
	default:
		return UNUSED;
	}
}

/**
 * get_sla_arr_of_type() - Returns a pointer to SLA type specific array
 * @rl_data: pointer to ratelimiting data
 * @type: SLA type
 * @sla_arr: pointer to variable where requested pointer will be stored
 *
 * Return: Max number of elements allowed for the returned array
 */
static u32 get_sla_arr_of_type(struct adf_rl *rl_data, enum rl_node_type type,
			       struct rl_sla ***sla_arr)
{
	switch (type) {
	case RL_LEAF:
		*sla_arr = rl_data->leaf;
		return RL_LEAF_MAX;
	case RL_CLUSTER:
		*sla_arr = rl_data->cluster;
		return RL_CLUSTER_MAX;
	case RL_ROOT:
		*sla_arr = rl_data->root;
		return RL_ROOT_MAX;
	default:
		*sla_arr = NULL;
		return 0;
	}
}

static bool is_service_enabled(struct adf_accel_dev *accel_dev,
			       enum adf_base_services rl_srv)
{
	enum adf_cfg_service_type arb_srv = srv_to_cfg_svc_type(rl_srv);
	struct adf_hw_device_data *hw_data = GET_HW_DATA(accel_dev);
	u8 rps_per_bundle = hw_data->num_banks_per_vf;
	int i;

	for (i = 0; i < rps_per_bundle; i++) {
		if (GET_SRV_TYPE(accel_dev, i) == arb_srv)
			return true;
	}

	return false;
}

/**
 * prepare_rp_ids() - Creates an array of ring pair IDs from bitmask
 * @accel_dev: pointer to acceleration device structure
 * @sla: SLA object data where result will be written
 * @rp_mask: bitmask of ring pair IDs
 *
 * Function tries to convert provided bitmap to an array of IDs. It checks if
 * RPs aren't in use, are assigned to SLA  service or if a number of provided
 * IDs is not too big. If successful, writes the result into the field
 * sla->ring_pairs_cnt.
 *
 * Return:
 * * 0		- ok
 * * -EINVAL	- ring pairs array cannot be created from provided mask
 */
static int prepare_rp_ids(struct adf_accel_dev *accel_dev, struct rl_sla *sla,
			  const unsigned long rp_mask)
{
	enum adf_cfg_service_type arb_srv = srv_to_cfg_svc_type(sla->srv);
	u16 rps_per_bundle = GET_HW_DATA(accel_dev)->num_banks_per_vf;
	bool *rp_in_use = accel_dev->rate_limiting->rp_in_use;
	size_t rp_cnt_max = ARRAY_SIZE(sla->ring_pairs_ids);
	u16 rp_id_max = GET_HW_DATA(accel_dev)->num_banks;
	u16 cnt = 0;
	u16 rp_id;

	for_each_set_bit(rp_id, &rp_mask, rp_id_max) {
		if (cnt >= rp_cnt_max) {
			dev_notice(&GET_DEV(accel_dev),
				   "Assigned more ring pairs than supported");
			return -EINVAL;
		}

		if (rp_in_use[rp_id]) {
			dev_notice(&GET_DEV(accel_dev),
				   "RP %u already assigned to other SLA", rp_id);
			return -EINVAL;
		}

		if (GET_SRV_TYPE(accel_dev, rp_id % rps_per_bundle) != arb_srv) {
			dev_notice(&GET_DEV(accel_dev),
				   "RP %u does not support SLA service", rp_id);
			return -EINVAL;
		}

		sla->ring_pairs_ids[cnt++] = rp_id;
	}

	sla->ring_pairs_cnt = cnt;

	return 0;
}

static void mark_rps_usage(struct rl_sla *sla, bool *rp_in_use, bool used)
{
	u16 rp_id;
	int i;

	for (i = 0; i < sla->ring_pairs_cnt; i++) {
		rp_id = sla->ring_pairs_ids[i];
		rp_in_use[rp_id] = used;
	}
}

static void assign_rps_to_leaf(struct adf_accel_dev *accel_dev,
			       struct rl_sla *sla, bool clear)
{
	struct adf_hw_device_data *hw_data = GET_HW_DATA(accel_dev);
	void __iomem *pmisc_addr = adf_get_pmisc_base(accel_dev);
	u32 base_offset = hw_data->rl_data.r2l_offset;
	u32 node_id = clear ? 0U : (sla->node_id & LEAF_MASK);
	u32 offset;
	int i;

	for (i = 0; i < sla->ring_pairs_cnt; i++) {
		offset = base_offset + (RL_CSR_SIZE * sla->ring_pairs_ids[i]);
		ADF_CSR_WR(pmisc_addr, offset, node_id);
	}
}

static void assign_leaf_to_cluster(struct adf_accel_dev *accel_dev,
				   struct rl_sla *sla, bool clear)
{
	struct adf_hw_device_data *hw_data = GET_HW_DATA(accel_dev);
	void __iomem *pmisc_addr = adf_get_pmisc_base(accel_dev);
	u32 base_offset = hw_data->rl_data.l2c_offset;
	u32 node_id = sla->node_id & LEAF_MASK;
	u32 parent_id = clear ? 0U : (sla->parent->node_id & CLUSTER_MASK);
	u32 offset;

	offset = base_offset + (RL_CSR_SIZE * node_id);
	ADF_CSR_WR(pmisc_addr, offset, parent_id);
}

static void assign_cluster_to_root(struct adf_accel_dev *accel_dev,
				   struct rl_sla *sla, bool clear)
{
	struct adf_hw_device_data *hw_data = GET_HW_DATA(accel_dev);
	void __iomem *pmisc_addr = adf_get_pmisc_base(accel_dev);
	u32 base_offset = hw_data->rl_data.c2s_offset;
	u32 node_id = sla->node_id & CLUSTER_MASK;
	u32 parent_id = clear ? 0U : (sla->parent->node_id & ROOT_MASK);
	u32 offset;

	offset = base_offset + (RL_CSR_SIZE * node_id);
	ADF_CSR_WR(pmisc_addr, offset, parent_id);
}

static void assign_node_to_parent(struct adf_accel_dev *accel_dev,
				  struct rl_sla *sla, bool clear_assignment)
{
	switch (sla->type) {
	case RL_LEAF:
		assign_rps_to_leaf(accel_dev, sla, clear_assignment);
		assign_leaf_to_cluster(accel_dev, sla, clear_assignment);
		break;
	case RL_CLUSTER:
		assign_cluster_to_root(accel_dev, sla, clear_assignment);
		break;
	default:
		break;
	}
}

/**
 * can_parent_afford_sla() - Verifies if parent allows to create an SLA
 * @sla_in: pointer to user input data for a new SLA
 * @sla_parent: pointer to parent SLA object
 * @sla_cir: current child CIR value (only for update)
 * @is_update: request is a update
 *
 * Algorithm verifies if parent has enough remaining budget to take assignment
 * of a child with provided parameters. In update case current CIR value must be
 * returned to budget first.
 * PIR value cannot exceed the PIR assigned to parent.
 *
 * Return:
 * * true	- SLA can be created
 * * false	- SLA cannot be created
 */
static bool can_parent_afford_sla(struct adf_rl_sla_input_data *sla_in,
				  struct rl_sla *sla_parent, u32 sla_cir,
				  bool is_update)
{
	u32 rem_cir = sla_parent->rem_cir;

	if (is_update)
		rem_cir += sla_cir;

	if (sla_in->cir > rem_cir || sla_in->pir > sla_parent->pir)
		return false;

	return true;
}

/**
 * can_node_afford_update() - Verifies if SLA can be updated with input data
 * @sla_in: pointer to user input data for a new SLA
 * @sla: pointer to SLA object selected for update
 *
 * Algorithm verifies if a new CIR value is big enough to satisfy currently
 * assigned child SLAs and if PIR can be updated
 *
 * Return:
 * * true	- SLA can be updated
 * * false	- SLA cannot be updated
 */
static bool can_node_afford_update(struct adf_rl_sla_input_data *sla_in,
				   struct rl_sla *sla)
{
	u32 cir_in_use = sla->cir - sla->rem_cir;

	/* new CIR cannot be smaller then currently consumed value */
	if (cir_in_use > sla_in->cir)
		return false;

	/* PIR of root/cluster cannot be reduced in node with assigned children */
	if (sla_in->pir < sla->pir && sla->type != RL_LEAF && cir_in_use > 0)
		return false;

	return true;
}

static bool is_enough_budget(struct adf_rl *rl_data, struct rl_sla *sla,
			     struct adf_rl_sla_input_data *sla_in,
			     bool is_update)
{
	u32 max_val = rl_data->device_data->scale_ref;
	struct rl_sla *parent = sla->parent;
	bool ret = true;

	if (sla_in->cir > max_val || sla_in->pir > max_val)
		ret = false;

	switch (sla->type) {
	case RL_LEAF:
		ret &= can_parent_afford_sla(sla_in, parent, sla->cir,
						  is_update);
		break;
	case RL_CLUSTER:
		ret &= can_parent_afford_sla(sla_in, parent, sla->cir,
						  is_update);

		if (is_update)
			ret &= can_node_afford_update(sla_in, sla);

		break;
	case RL_ROOT:
		if (is_update)
			ret &= can_node_afford_update(sla_in, sla);

		break;
	default:
		ret = false;
		break;
	}

	return ret;
}

static void update_budget(struct rl_sla *sla, u32 old_cir, bool is_update)
{
	switch (sla->type) {
	case RL_LEAF:
		if (is_update)
			sla->parent->rem_cir += old_cir;

		sla->parent->rem_cir -= sla->cir;
		sla->rem_cir = 0;
		break;
	case RL_CLUSTER:
		if (is_update) {
			sla->parent->rem_cir += old_cir;
			sla->rem_cir = sla->cir - (old_cir - sla->rem_cir);
		} else {
			sla->rem_cir = sla->cir;
		}

		sla->parent->rem_cir -= sla->cir;
		break;
	case RL_ROOT:
		if (is_update)
			sla->rem_cir = sla->cir - (old_cir - sla->rem_cir);
		else
			sla->rem_cir = sla->cir;
		break;
	default:
		break;
	}
}

/**
 * get_next_free_sla_id() - finds next free ID in the SLA array
 * @rl_data: Pointer to ratelimiting data structure
 *
 * Return:
 * * 0 : RL_NODES_CNT_MAX	- correct ID
 * * -ENOSPC			- all SLA slots are in use
 */
static int get_next_free_sla_id(struct adf_rl *rl_data)
{
	int i = 0;

	while (i < RL_NODES_CNT_MAX && rl_data->sla[i++])
		;

	if (i == RL_NODES_CNT_MAX)
		return -ENOSPC;

	return i - 1;
}

/**
 * get_next_free_node_id() - finds next free ID in the array of that node type
 * @rl_data: Pointer to ratelimiting data structure
 * @sla: Pointer to SLA object for which the ID is searched
 *
 * Return:
 * * 0 : RL_[NODE_TYPE]_MAX	- correct ID
 * * -ENOSPC			- all slots of that type are in use
 */
static int get_next_free_node_id(struct adf_rl *rl_data, struct rl_sla *sla)
{
	struct adf_hw_device_data *hw_device = GET_HW_DATA(rl_data->accel_dev);
	int max_id, i, step, rp_per_leaf;
	struct rl_sla **sla_list;

	rp_per_leaf = hw_device->num_banks / hw_device->num_banks_per_vf;

	/*
	 * Static nodes mapping:
	 * root0 - cluster[0,4,8,12] - leaf[0-15]
	 * root1 - cluster[1,5,9,13] - leaf[16-31]
	 * root2 - cluster[2,6,10,14] - leaf[32-47]
	 */
	switch (sla->type) {
	case RL_LEAF:
		i = sla->srv * rp_per_leaf;
		step = 1;
		max_id = i + rp_per_leaf;
		sla_list = rl_data->leaf;
		break;
	case RL_CLUSTER:
		i = sla->srv;
		step = 4;
		max_id = RL_CLUSTER_MAX;
		sla_list = rl_data->cluster;
		break;
	case RL_ROOT:
		return sla->srv;
	default:
		return -EINVAL;
	}

	while (i < max_id && sla_list[i])
		i += step;

	if (i >= max_id)
		return -ENOSPC;

	return i;
}

u32 adf_rl_calculate_slice_tokens(struct adf_accel_dev *accel_dev, u32 sla_val,
				  enum adf_base_services svc_type)
{
	struct adf_rl_hw_data *device_data = &accel_dev->hw_device->rl_data;
	struct adf_hw_device_data *hw_data = GET_HW_DATA(accel_dev);
	u64 avail_slice_cycles, allocated_tokens;

	if (!sla_val)
		return 0;

	avail_slice_cycles = hw_data->clock_frequency;

	switch (svc_type) {
	case ADF_SVC_ASYM:
		avail_slice_cycles *= device_data->slices.pke_cnt;
		break;
	case ADF_SVC_SYM:
		avail_slice_cycles *= device_data->slices.cph_cnt;
		break;
	case ADF_SVC_DC:
		avail_slice_cycles *= device_data->slices.dcpr_cnt;
		break;
	default:
		break;
	}

	do_div(avail_slice_cycles, device_data->scan_interval);
	allocated_tokens = avail_slice_cycles * sla_val;
	do_div(allocated_tokens, device_data->scale_ref);

	return allocated_tokens;
}

u32 adf_rl_calculate_ae_cycles(struct adf_accel_dev *accel_dev, u32 sla_val,
			       enum adf_base_services svc_type)
{
	struct adf_rl_hw_data *device_data = &accel_dev->hw_device->rl_data;
	struct adf_hw_device_data *hw_data = GET_HW_DATA(accel_dev);
	u64 allocated_ae_cycles, avail_ae_cycles;

	if (!sla_val)
		return 0;

	avail_ae_cycles = hw_data->clock_frequency;
	avail_ae_cycles *= hw_data->get_num_aes(hw_data) - 1;
	do_div(avail_ae_cycles, device_data->scan_interval);

	sla_val *= device_data->max_tp[svc_type];
	sla_val /= device_data->scale_ref;

	allocated_ae_cycles = (sla_val * avail_ae_cycles);
	do_div(allocated_ae_cycles, device_data->max_tp[svc_type]);

	return allocated_ae_cycles;
}

u32 adf_rl_calculate_pci_bw(struct adf_accel_dev *accel_dev, u32 sla_val,
			    enum adf_base_services svc_type, bool is_bw_out)
{
	struct adf_rl_hw_data *device_data = &accel_dev->hw_device->rl_data;
	u64 sla_to_bytes, allocated_bw, sla_scaled;

	if (!sla_val)
		return 0;

	sla_to_bytes = sla_val;
	sla_to_bytes *= device_data->max_tp[svc_type];
	do_div(sla_to_bytes, device_data->scale_ref);

	sla_to_bytes *= (svc_type == ADF_SVC_ASYM) ? RL_TOKEN_ASYM_SIZE :
						     BYTES_PER_MBIT;
	if (svc_type == ADF_SVC_DC && is_bw_out)
		sla_to_bytes *= device_data->slices.dcpr_cnt -
				device_data->dcpr_correction;

	sla_scaled = sla_to_bytes * device_data->pcie_scale_mul;
	do_div(sla_scaled, device_data->pcie_scale_div);
	allocated_bw = sla_scaled;
	do_div(allocated_bw, RL_TOKEN_PCIE_SIZE);
	do_div(allocated_bw, device_data->scan_interval);

	return allocated_bw;
}

/**
 * add_new_sla_entry() - creates a new SLA object and fills it with user data
 * @accel_dev: pointer to acceleration device structure
 * @sla_in: pointer to user input data for a new SLA
 * @sla_out: Pointer to variable that will contain the address of a new
 *	     SLA object if the operation succeeds
 *
 * Return:
 * * 0		- ok
 * * -ENOMEM	- memory allocation failed
 * * -EINVAL	- invalid user input
 * * -ENOSPC	- all available SLAs are in use
 */
static int add_new_sla_entry(struct adf_accel_dev *accel_dev,
			     struct adf_rl_sla_input_data *sla_in,
			     struct rl_sla **sla_out)
{
	struct adf_rl *rl_data = accel_dev->rate_limiting;
	struct rl_sla *sla;
	int ret = 0;

	sla = kzalloc(sizeof(*sla), GFP_KERNEL);
	if (!sla) {
		ret = -ENOMEM;
		goto ret_err;
	}
	*sla_out = sla;

	if (!is_service_enabled(accel_dev, sla_in->srv)) {
		dev_notice(&GET_DEV(accel_dev),
			   "Provided service is not enabled\n");
		ret = -EINVAL;
		goto ret_err;
	}

	sla->srv = sla_in->srv;
	sla->type = sla_in->type;
	ret = get_next_free_node_id(rl_data, sla);
	if (ret < 0) {
		dev_notice(&GET_DEV(accel_dev),
			   "Exceeded number of available nodes for that service\n");
		goto ret_err;
	}
	sla->node_id = ret;

	ret = get_next_free_sla_id(rl_data);
	if (ret < 0) {
		dev_notice(&GET_DEV(accel_dev),
			   "Allocated maximum SLAs number\n");
		goto ret_err;
	}
	sla->sla_id = ret;

	sla->parent = find_parent(rl_data, sla_in);
	if (!sla->parent && sla->type != RL_ROOT) {
		if (sla_in->parent_id != RL_PARENT_DEFAULT_ID)
			dev_notice(&GET_DEV(accel_dev),
				   "Provided parent ID does not exist or cannot be parent for this SLA.");
		else
			dev_notice(&GET_DEV(accel_dev),
				   "Unable to find parent node for this service. Is service enabled?");
		ret = -EINVAL;
		goto ret_err;
	}

	if (sla->type == RL_LEAF) {
		ret = prepare_rp_ids(accel_dev, sla, sla_in->rp_mask);
		if (!sla->ring_pairs_cnt || ret) {
			dev_notice(&GET_DEV(accel_dev),
				   "Unable to find ring pairs to assign to the leaf");
			if (!ret)
				ret = -EINVAL;

			goto ret_err;
		}
	}

	return 0;

ret_err:
	kfree(sla);
	*sla_out = NULL;

	return ret;
}

static int initialize_default_nodes(struct adf_accel_dev *accel_dev)
{
	struct adf_rl *rl_data = accel_dev->rate_limiting;
	struct adf_rl_hw_data *device_data = rl_data->device_data;
	struct adf_rl_sla_input_data sla_in = { };
	int ret = 0;
	int i;

	/* Init root for each enabled service */
	sla_in.type = RL_ROOT;
	sla_in.parent_id = RL_PARENT_DEFAULT_ID;

	for (i = 0; i < ADF_SVC_NONE; i++) {
		if (!is_service_enabled(accel_dev, i))
			continue;

		sla_in.cir = device_data->scale_ref;
		sla_in.pir = sla_in.cir;
		sla_in.srv = i;

		ret = adf_rl_add_sla(accel_dev, &sla_in);
		if (ret)
			return ret;
	}

	/* Init default cluster for each root */
	sla_in.type = RL_CLUSTER;
	for (i = 0; i < ADF_SVC_NONE; i++) {
		if (!rl_data->root[i])
			continue;

		sla_in.cir = rl_data->root[i]->cir;
		sla_in.pir = sla_in.cir;
		sla_in.srv = rl_data->root[i]->srv;

		ret = adf_rl_add_sla(accel_dev, &sla_in);
		if (ret)
			return ret;
	}

	return 0;
}

static void clear_sla(struct adf_rl *rl_data, struct rl_sla *sla)
{
	bool *rp_in_use = rl_data->rp_in_use;
	struct rl_sla **sla_type_arr = NULL;
	int i, sla_id, node_id;
	u32 old_cir;

	sla_id = sla->sla_id;
	node_id = sla->node_id;
	old_cir = sla->cir;
	sla->cir = 0;
	sla->pir = 0;

	for (i = 0; i < sla->ring_pairs_cnt; i++)
		rp_in_use[sla->ring_pairs_ids[i]] = false;

	update_budget(sla, old_cir, true);
	get_sla_arr_of_type(rl_data, sla->type, &sla_type_arr);
	assign_node_to_parent(rl_data->accel_dev, sla, true);
	adf_rl_send_admin_delete_msg(rl_data->accel_dev, node_id, sla->type);
	mark_rps_usage(sla, rl_data->rp_in_use, false);

	kfree(sla);
	rl_data->sla[sla_id] = NULL;
	sla_type_arr[node_id] = NULL;
}

/**
 * add_update_sla() - handles the creation and the update of an SLA
 * @accel_dev: pointer to acceleration device structure
 * @sla_in: pointer to user input data for a new/updated SLA
 * @is_update: flag to indicate if this is an update or an add operation
 *
 * Return:
 * * 0		- ok
 * * -ENOMEM	- memory allocation failed
 * * -EINVAL	- user input data cannot be used to create SLA
 * * -ENOSPC	- all available SLAs are in use
 */
static int add_update_sla(struct adf_accel_dev *accel_dev,
			  struct adf_rl_sla_input_data *sla_in, bool is_update)
{
	struct adf_rl *rl_data = accel_dev->rate_limiting;
	struct rl_sla **sla_type_arr = NULL;
	struct rl_sla *sla = NULL;
	u32 old_cir = 0;
	int ret;

	if (!sla_in) {
		dev_warn(&GET_DEV(accel_dev),
			 "SLA input data pointer is missing\n");
		ret = -EFAULT;
		goto ret_err;
	}

	/* Input validation */
	ret = validate_user_input(accel_dev, sla_in, is_update);
	if (ret)
		goto ret_err;

	mutex_lock(&rl_data->rl_lock);

	if (is_update) {
		ret = validate_sla_id(accel_dev, sla_in->sla_id);
		if (ret)
			goto ret_err;

		sla = rl_data->sla[sla_in->sla_id];
		old_cir = sla->cir;
	} else {
		ret = add_new_sla_entry(accel_dev, sla_in, &sla);
		if (ret)
			goto ret_err;
	}

	if (!is_enough_budget(rl_data, sla, sla_in, is_update)) {
		dev_notice(&GET_DEV(accel_dev),
			   "Input value exceeds the remaining budget%s\n",
			   is_update ? " or more budget is already in use" : "");
		ret = -EINVAL;
		goto ret_err;
	}
	sla->cir = sla_in->cir;
	sla->pir = sla_in->pir;

	/* Apply SLA */
	assign_node_to_parent(accel_dev, sla, false);
	ret = adf_rl_send_admin_add_update_msg(accel_dev, sla, is_update);
	if (ret) {
		dev_notice(&GET_DEV(accel_dev),
			   "Failed to apply an SLA\n");
		goto ret_err;
	}
	update_budget(sla, old_cir, is_update);

	if (!is_update) {
		mark_rps_usage(sla, rl_data->rp_in_use, true);
		get_sla_arr_of_type(rl_data, sla->type, &sla_type_arr);
		sla_type_arr[sla->node_id] = sla;
		rl_data->sla[sla->sla_id] = sla;
	}

	sla_in->sla_id = sla->sla_id;
	goto ret_ok;

ret_err:
	if (!is_update) {
		sla_in->sla_id = -1;
		kfree(sla);
	}
ret_ok:
	mutex_unlock(&rl_data->rl_lock);
	return ret;
}

/**
 * adf_rl_add_sla() - handles the creation of an SLA
 * @accel_dev: pointer to acceleration device structure
 * @sla_in: pointer to user input data required to add an SLA
 *
 * Return:
 * * 0		- ok
 * * -ENOMEM	- memory allocation failed
 * * -EINVAL	- invalid user input
 * * -ENOSPC	- all available SLAs are in use
 */
int adf_rl_add_sla(struct adf_accel_dev *accel_dev,
		   struct adf_rl_sla_input_data *sla_in)
{
	return add_update_sla(accel_dev, sla_in, false);
}

/**
 * adf_rl_update_sla() - handles the update of an SLA
 * @accel_dev: pointer to acceleration device structure
 * @sla_in: pointer to user input data required to update an SLA
 *
 * Return:
 * * 0		- ok
 * * -EINVAL	- user input data cannot be used to update SLA
 */
int adf_rl_update_sla(struct adf_accel_dev *accel_dev,
		      struct adf_rl_sla_input_data *sla_in)
{
	return add_update_sla(accel_dev, sla_in, true);
}

/**
 * adf_rl_get_sla() - returns an existing SLA data
 * @accel_dev: pointer to acceleration device structure
 * @sla_in: pointer to user data where SLA info will be stored
 *
 * The sla_id for which data are requested should be set in sla_id structure
 *
 * Return:
 * * 0		- ok
 * * -EINVAL	- provided sla_id does not exist
 */
int adf_rl_get_sla(struct adf_accel_dev *accel_dev,
		   struct adf_rl_sla_input_data *sla_in)
{
	struct rl_sla *sla;
	int ret, i;

	ret = validate_sla_id(accel_dev, sla_in->sla_id);
	if (ret)
		return ret;

	sla = accel_dev->rate_limiting->sla[sla_in->sla_id];
	sla_in->type = sla->type;
	sla_in->srv = sla->srv;
	sla_in->cir = sla->cir;
	sla_in->pir = sla->pir;
	sla_in->rp_mask = 0U;
	if (sla->parent)
		sla_in->parent_id = sla->parent->sla_id;
	else
		sla_in->parent_id = RL_PARENT_DEFAULT_ID;

	for (i = 0; i < sla->ring_pairs_cnt; i++)
		sla_in->rp_mask |= BIT(sla->ring_pairs_ids[i]);

	return 0;
}

/**
 * adf_rl_get_capability_remaining() - returns the remaining SLA value (CIR) for
 *				       selected service or provided sla_id
 * @accel_dev: pointer to acceleration device structure
 * @srv: service ID for which capability is requested
 * @sla_id: ID of the cluster or root to which we want assign a new SLA
 *
 * Check if the provided SLA id is valid. If it is and the service matches
 * the requested service and the type is cluster or root, return the remaining
 * capability.
 * If the provided ID does not match the service or type, return the remaining
 * capacity of the default cluster for that service.
 *
 * Return:
 * * Positive value	- correct remaining value
 * * -EINVAL		- algorithm cannot find a remaining value for provided data
 */
int adf_rl_get_capability_remaining(struct adf_accel_dev *accel_dev,
				    enum adf_base_services srv, int sla_id)
{
	struct adf_rl *rl_data = accel_dev->rate_limiting;
	struct rl_sla *sla = NULL;
	int i;

	if (srv >= ADF_SVC_NONE)
		return -EINVAL;

	if (sla_id > RL_SLA_EMPTY_ID && !validate_sla_id(accel_dev, sla_id)) {
		sla = rl_data->sla[sla_id];

		if (sla->srv == srv && sla->type <= RL_CLUSTER)
			goto ret_ok;
	}

	for (i = 0; i < RL_CLUSTER_MAX; i++) {
		if (!rl_data->cluster[i])
			continue;

		if (rl_data->cluster[i]->srv == srv) {
			sla = rl_data->cluster[i];
			goto ret_ok;
		}
	}

	return -EINVAL;
ret_ok:
	return sla->rem_cir;
}

/**
 * adf_rl_remove_sla() - removes provided sla_id
 * @accel_dev: pointer to acceleration device structure
 * @sla_id: ID of the cluster or root to which we want assign an new SLA
 *
 * Return:
 * * 0		- ok
 * * -EINVAL	- wrong sla_id or it still have assigned children
 */
int adf_rl_remove_sla(struct adf_accel_dev *accel_dev, u32 sla_id)
{
	struct adf_rl *rl_data = accel_dev->rate_limiting;
	struct rl_sla *sla;
	int ret = 0;

	mutex_lock(&rl_data->rl_lock);
	ret = validate_sla_id(accel_dev, sla_id);
	if (ret)
		goto err_ret;

	sla = rl_data->sla[sla_id];

	if (sla->type < RL_LEAF && sla->rem_cir != sla->cir) {
		dev_notice(&GET_DEV(accel_dev),
			   "To remove parent SLA all its children must be removed first");
		ret = -EINVAL;
		goto err_ret;
	}

	clear_sla(rl_data, sla);

err_ret:
	mutex_unlock(&rl_data->rl_lock);
	return ret;
}

/**
 * adf_rl_remove_sla_all() - removes all SLAs from device
 * @accel_dev: pointer to acceleration device structure
 * @incl_default: set to true if default SLAs also should be removed
 */
void adf_rl_remove_sla_all(struct adf_accel_dev *accel_dev, bool incl_default)
{
	struct adf_rl *rl_data = accel_dev->rate_limiting;
	int end_type = incl_default ? RL_ROOT : RL_LEAF;
	struct rl_sla **sla_type_arr = NULL;
	u32 max_id;
	int i, j;

	mutex_lock(&rl_data->rl_lock);

	/* Unregister and remove all SLAs */
	for (j = RL_LEAF; j >= end_type; j--) {
		max_id = get_sla_arr_of_type(rl_data, j, &sla_type_arr);

		for (i = 0; i < max_id; i++) {
			if (!sla_type_arr[i])
				continue;

			clear_sla(rl_data, sla_type_arr[i]);
		}
	}

	mutex_unlock(&rl_data->rl_lock);
}

int adf_rl_init(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_data = GET_HW_DATA(accel_dev);
	struct adf_rl_hw_data *rl_hw_data = &hw_data->rl_data;
	struct adf_rl *rl;
	int ret = 0;

	/* Validate device parameters */
	if (RL_VALIDATE_NON_ZERO(rl_hw_data->max_tp[ADF_SVC_ASYM]) ||
	    RL_VALIDATE_NON_ZERO(rl_hw_data->max_tp[ADF_SVC_SYM]) ||
	    RL_VALIDATE_NON_ZERO(rl_hw_data->max_tp[ADF_SVC_DC]) ||
	    RL_VALIDATE_NON_ZERO(rl_hw_data->scan_interval) ||
	    RL_VALIDATE_NON_ZERO(rl_hw_data->pcie_scale_div) ||
	    RL_VALIDATE_NON_ZERO(rl_hw_data->pcie_scale_mul) ||
	    RL_VALIDATE_NON_ZERO(rl_hw_data->scale_ref)) {
		ret = -EOPNOTSUPP;
		goto err_ret;
	}

	rl = kzalloc(sizeof(*rl), GFP_KERNEL);
	if (!rl) {
		ret = -ENOMEM;
		goto err_ret;
	}

	mutex_init(&rl->rl_lock);
	rl->device_data = &accel_dev->hw_device->rl_data;
	rl->accel_dev = accel_dev;
	accel_dev->rate_limiting = rl;

err_ret:
	return ret;
}

int adf_rl_start(struct adf_accel_dev *accel_dev)
{
	struct adf_rl_hw_data *rl_hw_data = &GET_HW_DATA(accel_dev)->rl_data;
	void __iomem *pmisc_addr = adf_get_pmisc_base(accel_dev);
	u16 fw_caps =  GET_HW_DATA(accel_dev)->fw_capabilities;
	int ret;

	if (!accel_dev->rate_limiting) {
		ret = -EOPNOTSUPP;
		goto ret_err;
	}

	if ((fw_caps & RL_CAPABILITY_MASK) != RL_CAPABILITY_VALUE) {
		dev_info(&GET_DEV(accel_dev), "not supported\n");
		ret = -EOPNOTSUPP;
		goto ret_free;
	}

	ADF_CSR_WR(pmisc_addr, rl_hw_data->pciin_tb_offset,
		   RL_TOKEN_GRANULARITY_PCIEIN_BUCKET);
	ADF_CSR_WR(pmisc_addr, rl_hw_data->pciout_tb_offset,
		   RL_TOKEN_GRANULARITY_PCIEOUT_BUCKET);

	ret = adf_rl_send_admin_init_msg(accel_dev, &rl_hw_data->slices);
	if (ret) {
		dev_err(&GET_DEV(accel_dev), "initialization failed\n");
		goto ret_free;
	}

	ret = initialize_default_nodes(accel_dev);
	if (ret) {
		dev_err(&GET_DEV(accel_dev),
			"failed to initialize default SLAs\n");
		goto ret_sla_rm;
	}

	ret = adf_sysfs_rl_add(accel_dev);
	if (ret) {
		dev_err(&GET_DEV(accel_dev), "failed to add sysfs interface\n");
		goto ret_sysfs_rm;
	}

	return 0;

ret_sysfs_rm:
	adf_sysfs_rl_rm(accel_dev);
ret_sla_rm:
	adf_rl_remove_sla_all(accel_dev, true);
ret_free:
	kfree(accel_dev->rate_limiting);
	accel_dev->rate_limiting = NULL;
ret_err:
	return ret;
}

void adf_rl_stop(struct adf_accel_dev *accel_dev)
{
	if (!accel_dev->rate_limiting)
		return;

	adf_sysfs_rl_rm(accel_dev);
	adf_rl_remove_sla_all(accel_dev, true);
}

void adf_rl_exit(struct adf_accel_dev *accel_dev)
{
	if (!accel_dev->rate_limiting)
		return;

	kfree(accel_dev->rate_limiting);
	accel_dev->rate_limiting = NULL;
}
