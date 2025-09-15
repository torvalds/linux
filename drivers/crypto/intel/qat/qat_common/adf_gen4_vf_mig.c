// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2024 Intel Corporation */
#include <linux/delay.h>
#include <linux/dev_printk.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <asm/errno.h>

#include "adf_accel_devices.h"
#include "adf_bank_state.h"
#include "adf_common_drv.h"
#include "adf_gen4_hw_data.h"
#include "adf_gen4_pfvf.h"
#include "adf_pfvf_utils.h"
#include "adf_mstate_mgr.h"
#include "adf_gen4_vf_mig.h"

#define ADF_GEN4_VF_MSTATE_SIZE		4096
#define ADF_GEN4_PFVF_RSP_TIMEOUT_US	5000

static int adf_gen4_vfmig_save_setup(struct qat_mig_dev *mdev);
static int adf_gen4_vfmig_load_setup(struct qat_mig_dev *mdev, int len);

static int adf_gen4_vfmig_init_device(struct qat_mig_dev *mdev)
{
	u8 *state;

	state = kmalloc(ADF_GEN4_VF_MSTATE_SIZE, GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	mdev->state = state;
	mdev->state_size = ADF_GEN4_VF_MSTATE_SIZE;
	mdev->setup_size = 0;
	mdev->remote_setup_size = 0;

	return 0;
}

static void adf_gen4_vfmig_cleanup_device(struct qat_mig_dev *mdev)
{
	kfree(mdev->state);
	mdev->state = NULL;
}

static void adf_gen4_vfmig_reset_device(struct qat_mig_dev *mdev)
{
	mdev->setup_size = 0;
	mdev->remote_setup_size = 0;
}

static int adf_gen4_vfmig_open_device(struct qat_mig_dev *mdev)
{
	struct adf_accel_dev *accel_dev = mdev->parent_accel_dev;
	struct adf_accel_vf_info *vf_info;
	struct adf_gen4_vfmig *vfmig;

	vf_info = &accel_dev->pf.vf_info[mdev->vf_id];

	vfmig = kzalloc(sizeof(*vfmig), GFP_KERNEL);
	if (!vfmig)
		return -ENOMEM;

	vfmig->mstate_mgr = adf_mstate_mgr_new(mdev->state, mdev->state_size);
	if (!vfmig->mstate_mgr) {
		kfree(vfmig);
		return -ENOMEM;
	}
	vf_info->mig_priv = vfmig;
	mdev->setup_size = 0;
	mdev->remote_setup_size = 0;

	return 0;
}

static void adf_gen4_vfmig_close_device(struct qat_mig_dev *mdev)
{
	struct adf_accel_dev *accel_dev = mdev->parent_accel_dev;
	struct adf_accel_vf_info *vf_info;
	struct adf_gen4_vfmig *vfmig;

	vf_info = &accel_dev->pf.vf_info[mdev->vf_id];
	if (vf_info->mig_priv) {
		vfmig = vf_info->mig_priv;
		adf_mstate_mgr_destroy(vfmig->mstate_mgr);
		kfree(vfmig);
		vf_info->mig_priv = NULL;
	}
}

static int adf_gen4_vfmig_suspend_device(struct qat_mig_dev *mdev)
{
	struct adf_accel_dev *accel_dev = mdev->parent_accel_dev;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	struct adf_accel_vf_info *vf_info;
	struct adf_gen4_vfmig *vf_mig;
	u32 vf_nr = mdev->vf_id;
	int ret, i;

	vf_info = &accel_dev->pf.vf_info[vf_nr];
	vf_mig = vf_info->mig_priv;

	/* Stop all inflight jobs */
	for (i = 0; i < hw_data->num_banks_per_vf; i++) {
		u32 pf_bank_nr = i + vf_nr * hw_data->num_banks_per_vf;

		ret = adf_gen4_bank_drain_start(accel_dev, pf_bank_nr,
						ADF_RPRESET_POLL_TIMEOUT_US);
		if (ret) {
			dev_err(&GET_DEV(accel_dev),
				"Failed to drain bank %d for vf_nr %d\n", i,
				vf_nr);
			return ret;
		}
		vf_mig->bank_stopped[i] = true;

		adf_gen4_bank_quiesce_coal_timer(accel_dev, pf_bank_nr,
						 ADF_COALESCED_POLL_TIMEOUT_US);
	}

	return 0;
}

static int adf_gen4_vfmig_resume_device(struct qat_mig_dev *mdev)
{
	struct adf_accel_dev *accel_dev = mdev->parent_accel_dev;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	struct adf_accel_vf_info *vf_info;
	struct adf_gen4_vfmig *vf_mig;
	u32 vf_nr = mdev->vf_id;
	int i;

	vf_info = &accel_dev->pf.vf_info[vf_nr];
	vf_mig = vf_info->mig_priv;

	for (i = 0; i < hw_data->num_banks_per_vf; i++) {
		u32 pf_bank_nr = i + vf_nr * hw_data->num_banks_per_vf;

		if (vf_mig->bank_stopped[i]) {
			adf_gen4_bank_drain_finish(accel_dev, pf_bank_nr);
			vf_mig->bank_stopped[i] = false;
		}
	}

	return 0;
}

struct adf_vf_bank_info {
	struct adf_accel_dev *accel_dev;
	u32 vf_nr;
	u32 bank_nr;
};

struct mig_user_sla {
	enum adf_base_services srv;
	u64 rp_mask;
	u32 cir;
	u32 pir;
};

static int adf_mstate_sla_check(struct adf_mstate_mgr *sub_mgr, u8 *src_buf,
				u32 src_size, void *opaque)
{
	struct adf_mstate_vreginfo _sinfo = { src_buf, src_size };
	struct adf_mstate_vreginfo *sinfo = &_sinfo, *dinfo = opaque;
	u32 src_sla_cnt = sinfo->size / sizeof(struct mig_user_sla);
	u32 dst_sla_cnt = dinfo->size / sizeof(struct mig_user_sla);
	struct mig_user_sla *src_slas = sinfo->addr;
	struct mig_user_sla *dst_slas = dinfo->addr;
	int i, j;

	for (i = 0; i < src_sla_cnt; i++) {
		for (j = 0; j < dst_sla_cnt; j++) {
			if (src_slas[i].srv != dst_slas[j].srv ||
			    src_slas[i].rp_mask != dst_slas[j].rp_mask)
				continue;

			if (src_slas[i].cir > dst_slas[j].cir ||
			    src_slas[i].pir > dst_slas[j].pir) {
				pr_err("QAT: DST VF rate limiting mismatch.\n");
				return -EINVAL;
			}
			break;
		}

		if (j == dst_sla_cnt) {
			pr_err("QAT: SRC VF rate limiting mismatch - SRC srv %d and rp_mask 0x%llx.\n",
			       src_slas[i].srv, src_slas[i].rp_mask);
			return -EINVAL;
		}
	}

	return 0;
}

static inline int adf_mstate_check_cap_size(u32 src_sz, u32 dst_sz, u32 max_sz)
{
	if (src_sz > max_sz || dst_sz > max_sz)
		return -EINVAL;
	else
		return 0;
}

static int adf_mstate_compatver_check(struct adf_mstate_mgr *sub_mgr,
				      u8 *src_buf, u32 src_sz, void *opaque)
{
	struct adf_mstate_vreginfo *info = opaque;
	u8 compat = 0;
	u8 *pcompat;

	if (src_sz != info->size) {
		pr_debug("QAT: State mismatch (compat version size), current %u, expected %u\n",
			 src_sz, info->size);
		return -EINVAL;
	}

	memcpy(info->addr, src_buf, info->size);
	pcompat = info->addr;
	if (*pcompat == 0) {
		pr_warn("QAT: Unable to determine the version of VF\n");
		return 0;
	}

	compat = adf_vf_compat_checker(*pcompat);
	if (compat == ADF_PF2VF_VF_INCOMPATIBLE) {
		pr_debug("QAT: SRC VF driver (ver=%u) is incompatible with DST PF driver (ver=%u)\n",
			 *pcompat, ADF_PFVF_COMPAT_THIS_VERSION);
		return -EINVAL;
	}

	if (compat == ADF_PF2VF_VF_COMPAT_UNKNOWN)
		pr_debug("QAT: SRC VF driver (ver=%u) is newer than DST PF driver (ver=%u)\n",
			 *pcompat, ADF_PFVF_COMPAT_THIS_VERSION);

	return 0;
}

/*
 * adf_mstate_capmask_compare() - compare QAT device capability mask
 * @sinfo:	Pointer to source capability info
 * @dinfo:	Pointer to target capability info
 *
 * This function compares the capability mask between source VF and target VF
 *
 * Returns: 0 if target capability mask is identical to source capability mask,
 * 1 if target mask can represent all the capabilities represented by source mask,
 * -1 if target mask can't represent all the capabilities represented by source
 * mask.
 */
static int adf_mstate_capmask_compare(struct adf_mstate_vreginfo *sinfo,
				      struct adf_mstate_vreginfo *dinfo)
{
	u64 src = 0, dst = 0;

	if (adf_mstate_check_cap_size(sinfo->size, dinfo->size, sizeof(u64))) {
		pr_debug("QAT: Unexpected capability size %u %u %zu\n",
			 sinfo->size, dinfo->size, sizeof(u64));
		return -1;
	}

	memcpy(&src, sinfo->addr, sinfo->size);
	memcpy(&dst, dinfo->addr, dinfo->size);

	pr_debug("QAT: Check cap compatibility of cap %llu %llu\n", src, dst);

	if (src == dst)
		return 0;

	if ((src | dst) == dst)
		return 1;

	return -1;
}

static int adf_mstate_capmask_superset(struct adf_mstate_mgr *sub_mgr, u8 *buf,
				       u32 size, void *opa)
{
	struct adf_mstate_vreginfo sinfo = { buf, size };

	if (adf_mstate_capmask_compare(&sinfo, opa) >= 0)
		return 0;

	return -EINVAL;
}

static int adf_mstate_capmask_equal(struct adf_mstate_mgr *sub_mgr, u8 *buf,
				    u32 size, void *opa)
{
	struct adf_mstate_vreginfo sinfo = { buf, size };

	if (adf_mstate_capmask_compare(&sinfo, opa) == 0)
		return 0;

	return -EINVAL;
}

static int adf_mstate_set_vreg(struct adf_mstate_mgr *sub_mgr, u8 *buf,
			       u32 size, void *opa)
{
	struct adf_mstate_vreginfo *info = opa;

	if (size != info->size) {
		pr_debug("QAT: Unexpected cap size %u %u\n", size, info->size);
		return -EINVAL;
	}
	memcpy(info->addr, buf, info->size);

	return 0;
}

static u32 adf_gen4_vfmig_get_slas(struct adf_accel_dev *accel_dev, u32 vf_nr,
				   struct mig_user_sla *pmig_slas)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	struct adf_rl *rl_data = accel_dev->rate_limiting;
	struct rl_sla **sla_type_arr = NULL;
	u64 rp_mask, rp_index;
	u32 max_num_sla;
	u32 sla_cnt = 0;
	int i, j;

	if (!accel_dev->rate_limiting)
		return 0;

	rp_index = vf_nr * hw_data->num_banks_per_vf;
	max_num_sla = adf_rl_get_sla_arr_of_type(rl_data, RL_LEAF, &sla_type_arr);

	for (i = 0; i < max_num_sla; i++) {
		if (!sla_type_arr[i])
			continue;

		rp_mask = 0;
		for (j = 0; j < sla_type_arr[i]->ring_pairs_cnt; j++)
			rp_mask |= BIT(sla_type_arr[i]->ring_pairs_ids[j]);

		if (rp_mask & GENMASK_ULL(rp_index + 3, rp_index)) {
			pmig_slas->rp_mask = rp_mask;
			pmig_slas->cir = sla_type_arr[i]->cir;
			pmig_slas->pir = sla_type_arr[i]->pir;
			pmig_slas->srv = sla_type_arr[i]->srv;
			pmig_slas++;
			sla_cnt++;
		}
	}

	return sla_cnt;
}

static int adf_gen4_vfmig_load_etr_regs(struct adf_mstate_mgr *sub_mgr,
					u8 *state, u32 size, void *opa)
{
	struct adf_vf_bank_info *vf_bank_info = opa;
	struct adf_accel_dev *accel_dev = vf_bank_info->accel_dev;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	u32 pf_bank_nr;
	int ret;

	pf_bank_nr = vf_bank_info->bank_nr + vf_bank_info->vf_nr * hw_data->num_banks_per_vf;
	ret = hw_data->bank_state_restore(accel_dev, pf_bank_nr,
					  (struct adf_bank_state *)state);
	if (ret) {
		dev_err(&GET_DEV(accel_dev),
			"Failed to load regs for vf%d bank%d\n",
			vf_bank_info->vf_nr, vf_bank_info->bank_nr);
		return ret;
	}

	return 0;
}

static int adf_gen4_vfmig_load_etr_bank(struct adf_accel_dev *accel_dev,
					u32 vf_nr, u32 bank_nr,
					struct adf_mstate_mgr *mstate_mgr)
{
	struct adf_vf_bank_info vf_bank_info = {accel_dev, vf_nr, bank_nr};
	struct adf_mstate_sect_h *subsec, *l2_subsec;
	struct adf_mstate_mgr sub_sects_mgr;
	char bank_ids[ADF_MSTATE_ID_LEN];

	snprintf(bank_ids, sizeof(bank_ids), ADF_MSTATE_BANK_IDX_IDS "%x", bank_nr);
	subsec = adf_mstate_sect_lookup(mstate_mgr, bank_ids, NULL, NULL);
	if (!subsec) {
		dev_err(&GET_DEV(accel_dev),
			"Failed to lookup sec %s for vf%d bank%d\n",
			ADF_MSTATE_BANK_IDX_IDS, vf_nr, bank_nr);
		return -EINVAL;
	}

	adf_mstate_mgr_init_from_psect(&sub_sects_mgr, subsec);
	l2_subsec = adf_mstate_sect_lookup(&sub_sects_mgr, ADF_MSTATE_ETR_REGS_IDS,
					   adf_gen4_vfmig_load_etr_regs,
					   &vf_bank_info);
	if (!l2_subsec) {
		dev_err(&GET_DEV(accel_dev),
			"Failed to add sec %s for vf%d bank%d\n",
			ADF_MSTATE_ETR_REGS_IDS, vf_nr, bank_nr);
		return -EINVAL;
	}

	return 0;
}

static int adf_gen4_vfmig_load_etr(struct adf_accel_dev *accel_dev, u32 vf_nr)
{
	struct adf_accel_vf_info *vf_info = &accel_dev->pf.vf_info[vf_nr];
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	struct adf_gen4_vfmig *vfmig = vf_info->mig_priv;
	struct adf_mstate_mgr *mstate_mgr = vfmig->mstate_mgr;
	struct adf_mstate_mgr sub_sects_mgr;
	struct adf_mstate_sect_h *subsec;
	int ret, i;

	subsec = adf_mstate_sect_lookup(mstate_mgr, ADF_MSTATE_ETRB_IDS, NULL,
					NULL);
	if (!subsec) {
		dev_err(&GET_DEV(accel_dev), "Failed to load sec %s\n",
			ADF_MSTATE_ETRB_IDS);
		return -EINVAL;
	}

	adf_mstate_mgr_init_from_psect(&sub_sects_mgr, subsec);
	for (i = 0; i < hw_data->num_banks_per_vf; i++) {
		ret = adf_gen4_vfmig_load_etr_bank(accel_dev, vf_nr, i,
						   &sub_sects_mgr);
		if (ret)
			return ret;
	}

	return 0;
}

static int adf_gen4_vfmig_load_misc(struct adf_accel_dev *accel_dev, u32 vf_nr)
{
	struct adf_accel_vf_info *vf_info = &accel_dev->pf.vf_info[vf_nr];
	struct adf_gen4_vfmig *vfmig = vf_info->mig_priv;
	void __iomem *csr = adf_get_pmisc_base(accel_dev);
	struct adf_mstate_mgr *mstate_mgr = vfmig->mstate_mgr;
	struct adf_mstate_sect_h *subsec, *l2_subsec;
	struct adf_mstate_mgr sub_sects_mgr;
	struct {
		char *id;
		u64 ofs;
	} misc_states[] = {
		{ADF_MSTATE_VINTMSK_IDS, ADF_GEN4_VINTMSK_OFFSET(vf_nr)},
		{ADF_MSTATE_VINTMSK_PF2VM_IDS, ADF_GEN4_VINTMSKPF2VM_OFFSET(vf_nr)},
		{ADF_MSTATE_PF2VM_IDS, ADF_GEN4_PF2VM_OFFSET(vf_nr)},
		{ADF_MSTATE_VM2PF_IDS, ADF_GEN4_VM2PF_OFFSET(vf_nr)},
	};
	int i;

	subsec = adf_mstate_sect_lookup(mstate_mgr, ADF_MSTATE_MISCB_IDS, NULL,
					NULL);
	if (!subsec) {
		dev_err(&GET_DEV(accel_dev), "Failed to load sec %s\n",
			ADF_MSTATE_MISCB_IDS);
		return -EINVAL;
	}

	adf_mstate_mgr_init_from_psect(&sub_sects_mgr, subsec);
	for (i = 0; i < ARRAY_SIZE(misc_states); i++) {
		struct adf_mstate_vreginfo info;
		u32 regv;

		info.addr = &regv;
		info.size = sizeof(regv);
		l2_subsec = adf_mstate_sect_lookup(&sub_sects_mgr,
						   misc_states[i].id,
						   adf_mstate_set_vreg,
						   &info);
		if (!l2_subsec) {
			dev_err(&GET_DEV(accel_dev),
				"Failed to load sec %s\n", misc_states[i].id);
			return -EINVAL;
		}
		ADF_CSR_WR(csr, misc_states[i].ofs, regv);
	}

	return 0;
}

static int adf_gen4_vfmig_load_generic(struct adf_accel_dev *accel_dev, u32 vf_nr)
{
	struct adf_accel_vf_info *vf_info = &accel_dev->pf.vf_info[vf_nr];
	struct mig_user_sla dst_slas[RL_RP_CNT_PER_LEAF_MAX] = { };
	struct adf_gen4_vfmig *vfmig = vf_info->mig_priv;
	struct adf_mstate_mgr *mstate_mgr = vfmig->mstate_mgr;
	struct adf_mstate_sect_h *subsec, *l2_subsec;
	struct adf_mstate_mgr sub_sects_mgr;
	u32 dst_sla_cnt;
	struct {
		char *id;
		int (*action)(struct adf_mstate_mgr *sub_mgr, u8 *buf, u32 size, void *opa);
		struct adf_mstate_vreginfo info;
	} gen_states[] = {
		{ADF_MSTATE_IOV_INIT_IDS, adf_mstate_set_vreg,
		{&vf_info->init, sizeof(vf_info->init)}},
		{ADF_MSTATE_COMPAT_VER_IDS, adf_mstate_compatver_check,
		{&vf_info->vf_compat_ver, sizeof(vf_info->vf_compat_ver)}},
		{ADF_MSTATE_SLA_IDS, adf_mstate_sla_check, {dst_slas, 0}},
	};
	int i;

	subsec = adf_mstate_sect_lookup(mstate_mgr, ADF_MSTATE_GEN_IDS, NULL, NULL);
	if (!subsec) {
		dev_err(&GET_DEV(accel_dev), "Failed to load sec %s\n",
			ADF_MSTATE_GEN_IDS);
		return -EINVAL;
	}

	adf_mstate_mgr_init_from_psect(&sub_sects_mgr, subsec);
	for (i = 0; i < ARRAY_SIZE(gen_states); i++) {
		if (gen_states[i].info.addr == dst_slas) {
			dst_sla_cnt = adf_gen4_vfmig_get_slas(accel_dev, vf_nr, dst_slas);
			gen_states[i].info.size = dst_sla_cnt * sizeof(struct mig_user_sla);
		}

		l2_subsec = adf_mstate_sect_lookup(&sub_sects_mgr,
						   gen_states[i].id,
						   gen_states[i].action,
						   &gen_states[i].info);
		if (!l2_subsec) {
			dev_err(&GET_DEV(accel_dev), "Failed to load sec %s\n",
				gen_states[i].id);
			return -EINVAL;
		}
	}

	return 0;
}

static int adf_gen4_vfmig_load_config(struct adf_accel_dev *accel_dev, u32 vf_nr)
{
	struct adf_accel_vf_info *vf_info = &accel_dev->pf.vf_info[vf_nr];
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	struct adf_gen4_vfmig *vfmig = vf_info->mig_priv;
	struct adf_mstate_mgr *mstate_mgr = vfmig->mstate_mgr;
	struct adf_mstate_sect_h *subsec, *l2_subsec;
	struct adf_mstate_mgr sub_sects_mgr;
	struct {
		char *id;
		int (*action)(struct adf_mstate_mgr *sub_mgr, u8 *buf, u32 size, void *opa);
		struct adf_mstate_vreginfo info;
	} setups[] = {
		{ADF_MSTATE_GEN_CAP_IDS, adf_mstate_capmask_superset,
		{&hw_data->accel_capabilities_mask, sizeof(hw_data->accel_capabilities_mask)}},
		{ADF_MSTATE_GEN_SVCMAP_IDS, adf_mstate_capmask_equal,
		{&hw_data->ring_to_svc_map, sizeof(hw_data->ring_to_svc_map)}},
		{ADF_MSTATE_GEN_EXTDC_IDS, adf_mstate_capmask_superset,
		{&hw_data->extended_dc_capabilities, sizeof(hw_data->extended_dc_capabilities)}},
	};
	int i;

	subsec = adf_mstate_sect_lookup(mstate_mgr, ADF_MSTATE_CONFIG_IDS, NULL, NULL);
	if (!subsec) {
		dev_err(&GET_DEV(accel_dev), "Failed to load sec %s\n",
			ADF_MSTATE_CONFIG_IDS);
		return -EINVAL;
	}

	adf_mstate_mgr_init_from_psect(&sub_sects_mgr, subsec);
	for (i = 0; i < ARRAY_SIZE(setups); i++) {
		l2_subsec = adf_mstate_sect_lookup(&sub_sects_mgr, setups[i].id,
						   setups[i].action, &setups[i].info);
		if (!l2_subsec) {
			dev_err(&GET_DEV(accel_dev), "Failed to load sec %s\n",
				setups[i].id);
			return -EINVAL;
		}
	}

	return 0;
}

static int adf_gen4_vfmig_save_etr_regs(struct adf_mstate_mgr *subs, u8 *state,
					u32 size, void *opa)
{
	struct adf_vf_bank_info *vf_bank_info = opa;
	struct adf_accel_dev *accel_dev = vf_bank_info->accel_dev;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	u32 pf_bank_nr;
	int ret;

	pf_bank_nr = vf_bank_info->bank_nr;
	pf_bank_nr += vf_bank_info->vf_nr * hw_data->num_banks_per_vf;

	ret = hw_data->bank_state_save(accel_dev, pf_bank_nr,
				       (struct adf_bank_state *)state);
	if (ret) {
		dev_err(&GET_DEV(accel_dev),
			"Failed to save regs for vf%d bank%d\n",
			vf_bank_info->vf_nr, vf_bank_info->bank_nr);
		return ret;
	}

	return sizeof(struct adf_bank_state);
}

static int adf_gen4_vfmig_save_etr_bank(struct adf_accel_dev *accel_dev,
					u32 vf_nr, u32 bank_nr,
					struct adf_mstate_mgr *mstate_mgr)
{
	struct adf_mstate_sect_h *subsec, *l2_subsec;
	struct adf_vf_bank_info vf_bank_info;
	struct adf_mstate_mgr sub_sects_mgr;
	char bank_ids[ADF_MSTATE_ID_LEN];

	snprintf(bank_ids, sizeof(bank_ids), ADF_MSTATE_BANK_IDX_IDS "%x", bank_nr);

	subsec = adf_mstate_sect_add(mstate_mgr, bank_ids, NULL, NULL);
	if (!subsec) {
		dev_err(&GET_DEV(accel_dev),
			"Failed to add sec %s for vf%d bank%d\n",
			ADF_MSTATE_BANK_IDX_IDS, vf_nr, bank_nr);
		return -EINVAL;
	}

	adf_mstate_mgr_init_from_parent(&sub_sects_mgr, mstate_mgr);
	vf_bank_info.accel_dev = accel_dev;
	vf_bank_info.vf_nr = vf_nr;
	vf_bank_info.bank_nr = bank_nr;
	l2_subsec = adf_mstate_sect_add(&sub_sects_mgr, ADF_MSTATE_ETR_REGS_IDS,
					adf_gen4_vfmig_save_etr_regs,
					&vf_bank_info);
	if (!l2_subsec) {
		dev_err(&GET_DEV(accel_dev),
			"Failed to add sec %s for vf%d bank%d\n",
			ADF_MSTATE_ETR_REGS_IDS, vf_nr, bank_nr);
		return -EINVAL;
	}
	adf_mstate_sect_update(mstate_mgr, &sub_sects_mgr, subsec);

	return 0;
}

static int adf_gen4_vfmig_save_etr(struct adf_accel_dev *accel_dev, u32 vf_nr)
{
	struct adf_accel_vf_info *vf_info = &accel_dev->pf.vf_info[vf_nr];
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	struct adf_gen4_vfmig *vfmig = vf_info->mig_priv;
	struct adf_mstate_mgr *mstate_mgr = vfmig->mstate_mgr;
	struct adf_mstate_mgr sub_sects_mgr;
	struct adf_mstate_sect_h *subsec;
	int ret, i;

	subsec = adf_mstate_sect_add(mstate_mgr, ADF_MSTATE_ETRB_IDS, NULL, NULL);
	if (!subsec) {
		dev_err(&GET_DEV(accel_dev), "Failed to add sec %s\n",
			ADF_MSTATE_ETRB_IDS);
		return -EINVAL;
	}

	adf_mstate_mgr_init_from_parent(&sub_sects_mgr, mstate_mgr);
	for (i = 0; i < hw_data->num_banks_per_vf; i++) {
		ret = adf_gen4_vfmig_save_etr_bank(accel_dev, vf_nr, i,
						   &sub_sects_mgr);
		if (ret)
			return ret;
	}
	adf_mstate_sect_update(mstate_mgr, &sub_sects_mgr, subsec);

	return 0;
}

static int adf_gen4_vfmig_save_misc(struct adf_accel_dev *accel_dev, u32 vf_nr)
{
	struct adf_accel_vf_info *vf_info = &accel_dev->pf.vf_info[vf_nr];
	struct adf_gen4_vfmig *vfmig = vf_info->mig_priv;
	struct adf_mstate_mgr *mstate_mgr = vfmig->mstate_mgr;
	void __iomem *csr = adf_get_pmisc_base(accel_dev);
	struct adf_mstate_sect_h *subsec, *l2_subsec;
	struct adf_mstate_mgr sub_sects_mgr;
	struct {
		char *id;
		u64 offset;
	} misc_states[] = {
		{ADF_MSTATE_VINTSRC_IDS, ADF_GEN4_VINTSOU_OFFSET(vf_nr)},
		{ADF_MSTATE_VINTMSK_IDS, ADF_GEN4_VINTMSK_OFFSET(vf_nr)},
		{ADF_MSTATE_VINTSRC_PF2VM_IDS, ADF_GEN4_VINTSOUPF2VM_OFFSET(vf_nr)},
		{ADF_MSTATE_VINTMSK_PF2VM_IDS, ADF_GEN4_VINTMSKPF2VM_OFFSET(vf_nr)},
		{ADF_MSTATE_PF2VM_IDS, ADF_GEN4_PF2VM_OFFSET(vf_nr)},
		{ADF_MSTATE_VM2PF_IDS, ADF_GEN4_VM2PF_OFFSET(vf_nr)},
	};
	ktime_t time_exp;
	int i;

	subsec = adf_mstate_sect_add(mstate_mgr, ADF_MSTATE_MISCB_IDS, NULL, NULL);
	if (!subsec) {
		dev_err(&GET_DEV(accel_dev), "Failed to add sec %s\n",
			ADF_MSTATE_MISCB_IDS);
		return -EINVAL;
	}

	time_exp = ktime_add_us(ktime_get(), ADF_GEN4_PFVF_RSP_TIMEOUT_US);
	while (!mutex_trylock(&vf_info->pfvf_mig_lock)) {
		if (ktime_after(ktime_get(), time_exp)) {
			dev_err(&GET_DEV(accel_dev), "Failed to get pfvf mig lock\n");
			return -ETIMEDOUT;
		}
		usleep_range(500, 1000);
	}

	adf_mstate_mgr_init_from_parent(&sub_sects_mgr, mstate_mgr);
	for (i = 0; i < ARRAY_SIZE(misc_states); i++) {
		struct adf_mstate_vreginfo info;
		u32 regv;

		info.addr = &regv;
		info.size = sizeof(regv);
		regv = ADF_CSR_RD(csr, misc_states[i].offset);

		l2_subsec = adf_mstate_sect_add_vreg(&sub_sects_mgr,
						     misc_states[i].id,
						     &info);
		if (!l2_subsec) {
			dev_err(&GET_DEV(accel_dev), "Failed to add sec %s\n",
				misc_states[i].id);
			mutex_unlock(&vf_info->pfvf_mig_lock);
			return -EINVAL;
		}
	}

	mutex_unlock(&vf_info->pfvf_mig_lock);
	adf_mstate_sect_update(mstate_mgr, &sub_sects_mgr, subsec);

	return 0;
}

static int adf_gen4_vfmig_save_generic(struct adf_accel_dev *accel_dev, u32 vf_nr)
{
	struct adf_accel_vf_info *vf_info = &accel_dev->pf.vf_info[vf_nr];
	struct adf_gen4_vfmig *vfmig = vf_info->mig_priv;
	struct adf_mstate_mgr *mstate_mgr = vfmig->mstate_mgr;
	struct adf_mstate_mgr sub_sects_mgr;
	struct adf_mstate_sect_h *subsec, *l2_subsec;
	struct mig_user_sla src_slas[RL_RP_CNT_PER_LEAF_MAX] = { };
	u32 src_sla_cnt;
	struct {
		char *id;
		struct adf_mstate_vreginfo info;
	} gen_states[] = {
		{ADF_MSTATE_IOV_INIT_IDS,
		{&vf_info->init, sizeof(vf_info->init)}},
		{ADF_MSTATE_COMPAT_VER_IDS,
		{&vf_info->vf_compat_ver, sizeof(vf_info->vf_compat_ver)}},
		{ADF_MSTATE_SLA_IDS, {src_slas, 0}},
	};
	int i;

	subsec = adf_mstate_sect_add(mstate_mgr, ADF_MSTATE_GEN_IDS, NULL, NULL);
	if (!subsec) {
		dev_err(&GET_DEV(accel_dev), "Failed to add sec %s\n",
			ADF_MSTATE_GEN_IDS);
		return -EINVAL;
	}

	adf_mstate_mgr_init_from_parent(&sub_sects_mgr, mstate_mgr);
	for (i = 0; i < ARRAY_SIZE(gen_states); i++) {
		if (gen_states[i].info.addr == src_slas) {
			src_sla_cnt = adf_gen4_vfmig_get_slas(accel_dev, vf_nr, src_slas);
			gen_states[i].info.size = src_sla_cnt * sizeof(struct mig_user_sla);
		}

		l2_subsec = adf_mstate_sect_add_vreg(&sub_sects_mgr,
						     gen_states[i].id,
						     &gen_states[i].info);
		if (!l2_subsec) {
			dev_err(&GET_DEV(accel_dev), "Failed to add sec %s\n",
				gen_states[i].id);
			return -EINVAL;
		}
	}
	adf_mstate_sect_update(mstate_mgr, &sub_sects_mgr, subsec);

	return 0;
}

static int adf_gen4_vfmig_save_config(struct adf_accel_dev *accel_dev, u32 vf_nr)
{
	struct adf_accel_vf_info *vf_info = &accel_dev->pf.vf_info[vf_nr];
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	struct adf_gen4_vfmig *vfmig = vf_info->mig_priv;
	struct adf_mstate_mgr *mstate_mgr = vfmig->mstate_mgr;
	struct adf_mstate_mgr sub_sects_mgr;
	struct adf_mstate_sect_h *subsec, *l2_subsec;
	struct {
		char *id;
		struct adf_mstate_vreginfo info;
	} setups[] = {
		{ADF_MSTATE_GEN_CAP_IDS,
		{&hw_data->accel_capabilities_mask, sizeof(hw_data->accel_capabilities_mask)}},
		{ADF_MSTATE_GEN_SVCMAP_IDS,
		{&hw_data->ring_to_svc_map, sizeof(hw_data->ring_to_svc_map)}},
		{ADF_MSTATE_GEN_EXTDC_IDS,
		{&hw_data->extended_dc_capabilities, sizeof(hw_data->extended_dc_capabilities)}},
	};
	int i;

	subsec = adf_mstate_sect_add(mstate_mgr, ADF_MSTATE_CONFIG_IDS, NULL, NULL);
	if (!subsec) {
		dev_err(&GET_DEV(accel_dev), "Failed to add sec %s\n",
			ADF_MSTATE_CONFIG_IDS);
		return -EINVAL;
	}

	adf_mstate_mgr_init_from_parent(&sub_sects_mgr, mstate_mgr);
	for (i = 0; i < ARRAY_SIZE(setups); i++) {
		l2_subsec = adf_mstate_sect_add_vreg(&sub_sects_mgr, setups[i].id,
						     &setups[i].info);
		if (!l2_subsec) {
			dev_err(&GET_DEV(accel_dev), "Failed to add sec %s\n",
				setups[i].id);
			return -EINVAL;
		}
	}
	adf_mstate_sect_update(mstate_mgr, &sub_sects_mgr, subsec);

	return 0;
}

static int adf_gen4_vfmig_save_state(struct qat_mig_dev *mdev)
{
	struct adf_accel_dev *accel_dev = mdev->parent_accel_dev;
	struct adf_accel_vf_info *vf_info;
	struct adf_gen4_vfmig *vfmig;
	u32 vf_nr = mdev->vf_id;
	int ret;

	vf_info = &accel_dev->pf.vf_info[vf_nr];
	vfmig = vf_info->mig_priv;

	ret = adf_gen4_vfmig_save_setup(mdev);
	if (ret) {
		dev_err(&GET_DEV(accel_dev),
			"Failed to save setup for vf_nr %d\n", vf_nr);
		return ret;
	}

	adf_mstate_mgr_init(vfmig->mstate_mgr, mdev->state + mdev->setup_size,
			    mdev->state_size - mdev->setup_size);
	if (!adf_mstate_preamble_add(vfmig->mstate_mgr))
		return -EINVAL;

	ret = adf_gen4_vfmig_save_generic(accel_dev, vf_nr);
	if (ret) {
		dev_err(&GET_DEV(accel_dev),
			"Failed to save generic state for vf_nr %d\n", vf_nr);
		return ret;
	}

	ret = adf_gen4_vfmig_save_misc(accel_dev, vf_nr);
	if (ret) {
		dev_err(&GET_DEV(accel_dev),
			"Failed to save misc bar state for vf_nr %d\n", vf_nr);
		return ret;
	}

	ret = adf_gen4_vfmig_save_etr(accel_dev, vf_nr);
	if (ret) {
		dev_err(&GET_DEV(accel_dev),
			"Failed to save etr bar state for vf_nr %d\n", vf_nr);
		return ret;
	}

	adf_mstate_preamble_update(vfmig->mstate_mgr);

	return 0;
}

static int adf_gen4_vfmig_load_state(struct qat_mig_dev *mdev)
{
	struct adf_accel_dev *accel_dev = mdev->parent_accel_dev;
	struct adf_accel_vf_info *vf_info;
	struct adf_gen4_vfmig *vfmig;
	u32 vf_nr = mdev->vf_id;
	int ret;

	vf_info = &accel_dev->pf.vf_info[vf_nr];
	vfmig = vf_info->mig_priv;

	ret = adf_gen4_vfmig_load_setup(mdev, mdev->state_size);
	if (ret) {
		dev_err(&GET_DEV(accel_dev), "Failed to load setup for vf_nr %d\n",
			vf_nr);
		return ret;
	}

	ret = adf_mstate_mgr_init_from_remote(vfmig->mstate_mgr,
					      mdev->state + mdev->remote_setup_size,
					      mdev->state_size - mdev->remote_setup_size,
					      NULL, NULL);
	if (ret) {
		dev_err(&GET_DEV(accel_dev), "Invalid state for vf_nr %d\n",
			vf_nr);
		return ret;
	}

	ret = adf_gen4_vfmig_load_generic(accel_dev, vf_nr);
	if (ret) {
		dev_err(&GET_DEV(accel_dev),
			"Failed to load general state for vf_nr %d\n", vf_nr);
		return ret;
	}

	ret = adf_gen4_vfmig_load_misc(accel_dev, vf_nr);
	if (ret) {
		dev_err(&GET_DEV(accel_dev),
			"Failed to load misc bar state for vf_nr %d\n", vf_nr);
		return ret;
	}

	ret = adf_gen4_vfmig_load_etr(accel_dev, vf_nr);
	if (ret) {
		dev_err(&GET_DEV(accel_dev),
			"Failed to load etr bar state for vf_nr %d\n", vf_nr);
		return ret;
	}

	return 0;
}

static int adf_gen4_vfmig_save_setup(struct qat_mig_dev *mdev)
{
	struct adf_accel_dev *accel_dev = mdev->parent_accel_dev;
	struct adf_accel_vf_info *vf_info;
	struct adf_gen4_vfmig *vfmig;
	u32 vf_nr = mdev->vf_id;
	int ret;

	vf_info = &accel_dev->pf.vf_info[vf_nr];
	vfmig = vf_info->mig_priv;

	if (mdev->setup_size)
		return 0;

	adf_mstate_mgr_init(vfmig->mstate_mgr, mdev->state, mdev->state_size);
	if (!adf_mstate_preamble_add(vfmig->mstate_mgr))
		return -EINVAL;

	ret = adf_gen4_vfmig_save_config(accel_dev, mdev->vf_id);
	if (ret)
		return ret;

	adf_mstate_preamble_update(vfmig->mstate_mgr);
	mdev->setup_size = adf_mstate_state_size(vfmig->mstate_mgr);

	return 0;
}

static int adf_gen4_vfmig_load_setup(struct qat_mig_dev *mdev, int len)
{
	struct adf_accel_dev *accel_dev = mdev->parent_accel_dev;
	struct adf_accel_vf_info *vf_info;
	struct adf_gen4_vfmig *vfmig;
	u32 vf_nr = mdev->vf_id;
	u32 setup_size;
	int ret;

	vf_info = &accel_dev->pf.vf_info[vf_nr];
	vfmig = vf_info->mig_priv;

	if (mdev->remote_setup_size)
		return 0;

	if (len < sizeof(struct adf_mstate_preh))
		return -EAGAIN;

	adf_mstate_mgr_init(vfmig->mstate_mgr, mdev->state, mdev->state_size);
	setup_size = adf_mstate_state_size_from_remote(vfmig->mstate_mgr);
	if (setup_size > mdev->state_size)
		return -EINVAL;

	if (len < setup_size)
		return -EAGAIN;

	ret = adf_mstate_mgr_init_from_remote(vfmig->mstate_mgr, mdev->state,
					      setup_size, NULL, NULL);
	if (ret) {
		dev_err(&GET_DEV(accel_dev), "Invalid setup for vf_nr %d\n",
			vf_nr);
		return ret;
	}

	mdev->remote_setup_size = setup_size;

	ret = adf_gen4_vfmig_load_config(accel_dev, vf_nr);
	if (ret) {
		dev_err(&GET_DEV(accel_dev),
			"Failed to load config for vf_nr %d\n", vf_nr);
		return ret;
	}

	return 0;
}

void adf_gen4_init_vf_mig_ops(struct qat_migdev_ops *vfmig_ops)
{
	vfmig_ops->init = adf_gen4_vfmig_init_device;
	vfmig_ops->cleanup = adf_gen4_vfmig_cleanup_device;
	vfmig_ops->reset = adf_gen4_vfmig_reset_device;
	vfmig_ops->open = adf_gen4_vfmig_open_device;
	vfmig_ops->close = adf_gen4_vfmig_close_device;
	vfmig_ops->suspend = adf_gen4_vfmig_suspend_device;
	vfmig_ops->resume = adf_gen4_vfmig_resume_device;
	vfmig_ops->save_state = adf_gen4_vfmig_save_state;
	vfmig_ops->load_state = adf_gen4_vfmig_load_state;
	vfmig_ops->load_setup = adf_gen4_vfmig_load_setup;
	vfmig_ops->save_setup = adf_gen4_vfmig_save_setup;
}
EXPORT_SYMBOL_GPL(adf_gen4_init_vf_mig_ops);
