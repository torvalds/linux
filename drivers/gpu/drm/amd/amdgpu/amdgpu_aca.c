/*
 * Copyright 2023 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <linux/list.h>
#include "amdgpu.h"
#include "amdgpu_aca.h"
#include "amdgpu_ras.h"

#define ACA_BANK_HWID(type, hwid, mcatype) [ACA_HWIP_TYPE_##type] = {hwid, mcatype}

typedef int bank_handler_t(struct aca_handle *handle, struct aca_bank *bank, enum aca_smu_type type, void *data);

struct aca_banks {
	int nr_banks;
	struct list_head list;
};

struct aca_hwip {
	int hwid;
	int mcatype;
};

static struct aca_hwip aca_hwid_mcatypes[ACA_HWIP_TYPE_COUNT] = {
	ACA_BANK_HWID(SMU,	0x01,	0x01),
	ACA_BANK_HWID(PCS_XGMI, 0x50,	0x00),
	ACA_BANK_HWID(UMC,	0x96,	0x00),
};

static void aca_banks_init(struct aca_banks *banks)
{
	if (!banks)
		return;

	memset(banks, 0, sizeof(*banks));
	INIT_LIST_HEAD(&banks->list);
}

static int aca_banks_add_bank(struct aca_banks *banks, struct aca_bank *bank)
{
	struct aca_bank_node *node;

	if (!bank)
		return -EINVAL;

	node = kvzalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		return -ENOMEM;

	memcpy(&node->bank, bank, sizeof(*bank));

	INIT_LIST_HEAD(&node->node);
	list_add_tail(&node->node, &banks->list);

	banks->nr_banks++;

	return 0;
}

static void aca_banks_release(struct aca_banks *banks)
{
	struct aca_bank_node *node, *tmp;

	if (list_empty(&banks->list))
		return;

	list_for_each_entry_safe(node, tmp, &banks->list, node) {
		list_del(&node->node);
		kvfree(node);
	}
}

static int aca_smu_get_valid_aca_count(struct amdgpu_device *adev, enum aca_smu_type type, u32 *count)
{
	struct amdgpu_aca *aca = &adev->aca;
	const struct aca_smu_funcs *smu_funcs = aca->smu_funcs;

	if (!count)
		return -EINVAL;

	if (!smu_funcs || !smu_funcs->get_valid_aca_count)
		return -EOPNOTSUPP;

	return smu_funcs->get_valid_aca_count(adev, type, count);
}

static struct aca_regs_dump {
	const char *name;
	int reg_idx;
} aca_regs[] = {
	{"CONTROL",		ACA_REG_IDX_CTL},
	{"STATUS",		ACA_REG_IDX_STATUS},
	{"ADDR",		ACA_REG_IDX_ADDR},
	{"MISC",		ACA_REG_IDX_MISC0},
	{"CONFIG",		ACA_REG_IDX_CONFG},
	{"IPID",		ACA_REG_IDX_IPID},
	{"SYND",		ACA_REG_IDX_SYND},
	{"DESTAT",		ACA_REG_IDX_DESTAT},
	{"DEADDR",		ACA_REG_IDX_DEADDR},
	{"CONTROL_MASK",	ACA_REG_IDX_CTL_MASK},
};

static void aca_smu_bank_dump(struct amdgpu_device *adev, int idx, int total, struct aca_bank *bank,
			      struct ras_query_context *qctx)
{
	u64 event_id = qctx ? qctx->evid.event_id : RAS_EVENT_INVALID_ID;
	int i;

	RAS_EVENT_LOG(adev, event_id, HW_ERR "Accelerator Check Architecture events logged\n");
	/* plus 1 for output format, e.g: ACA[08/08]: xxxx */
	for (i = 0; i < ARRAY_SIZE(aca_regs); i++)
		RAS_EVENT_LOG(adev, event_id, HW_ERR "ACA[%02d/%02d].%s=0x%016llx\n",
			      idx + 1, total, aca_regs[i].name, bank->regs[aca_regs[i].reg_idx]);
}

static int aca_smu_get_valid_aca_banks(struct amdgpu_device *adev, enum aca_smu_type type,
				       int start, int count,
				       struct aca_banks *banks, struct ras_query_context *qctx)
{
	struct amdgpu_aca *aca = &adev->aca;
	const struct aca_smu_funcs *smu_funcs = aca->smu_funcs;
	struct aca_bank bank;
	int i, max_count, ret;

	if (!count)
		return 0;

	if (!smu_funcs || !smu_funcs->get_valid_aca_bank)
		return -EOPNOTSUPP;

	switch (type) {
	case ACA_SMU_TYPE_UE:
		max_count = smu_funcs->max_ue_bank_count;
		break;
	case ACA_SMU_TYPE_CE:
		max_count = smu_funcs->max_ce_bank_count;
		break;
	default:
		return -EINVAL;
	}

	if (start + count > max_count)
		return -EINVAL;

	count = min_t(int, count, max_count);
	for (i = 0; i < count; i++) {
		memset(&bank, 0, sizeof(bank));
		ret = smu_funcs->get_valid_aca_bank(adev, type, start + i, &bank);
		if (ret)
			return ret;

		bank.type = type;

		aca_smu_bank_dump(adev, i, count, &bank, qctx);

		ret = aca_banks_add_bank(banks, &bank);
		if (ret)
			return ret;
	}

	return 0;
}

static bool aca_bank_hwip_is_matched(struct aca_bank *bank, enum aca_hwip_type type)
{

	struct aca_hwip *hwip;
	int hwid, mcatype;
	u64 ipid;

	if (!bank || type == ACA_HWIP_TYPE_UNKNOW)
		return false;

	hwip = &aca_hwid_mcatypes[type];
	if (!hwip->hwid)
		return false;

	ipid = bank->regs[ACA_REG_IDX_IPID];
	hwid = ACA_REG__IPID__HARDWAREID(ipid);
	mcatype = ACA_REG__IPID__MCATYPE(ipid);

	return hwip->hwid == hwid && hwip->mcatype == mcatype;
}

static bool aca_bank_is_valid(struct aca_handle *handle, struct aca_bank *bank, enum aca_smu_type type)
{
	const struct aca_bank_ops *bank_ops = handle->bank_ops;

	if (!aca_bank_hwip_is_matched(bank, handle->hwip))
		return false;

	if (!bank_ops->aca_bank_is_valid)
		return true;

	return bank_ops->aca_bank_is_valid(handle, bank, type, handle->data);
}

static struct aca_bank_error *new_bank_error(struct aca_error *aerr, struct aca_bank_info *info)
{
	struct aca_bank_error *bank_error;

	bank_error = kvzalloc(sizeof(*bank_error), GFP_KERNEL);
	if (!bank_error)
		return NULL;

	INIT_LIST_HEAD(&bank_error->node);
	memcpy(&bank_error->info, info, sizeof(*info));

	mutex_lock(&aerr->lock);
	list_add_tail(&bank_error->node, &aerr->list);
	mutex_unlock(&aerr->lock);

	return bank_error;
}

static struct aca_bank_error *find_bank_error(struct aca_error *aerr, struct aca_bank_info *info)
{
	struct aca_bank_error *bank_error = NULL;
	struct aca_bank_info *tmp_info;
	bool found = false;

	mutex_lock(&aerr->lock);
	list_for_each_entry(bank_error, &aerr->list, node) {
		tmp_info = &bank_error->info;
		if (tmp_info->socket_id == info->socket_id &&
		    tmp_info->die_id == info->die_id) {
			found = true;
			goto out_unlock;
		}
	}

out_unlock:
	mutex_unlock(&aerr->lock);

	return found ? bank_error : NULL;
}

static void aca_bank_error_remove(struct aca_error *aerr, struct aca_bank_error *bank_error)
{
	if (!aerr || !bank_error)
		return;

	list_del(&bank_error->node);
	aerr->nr_errors--;

	kvfree(bank_error);
}

static struct aca_bank_error *get_bank_error(struct aca_error *aerr, struct aca_bank_info *info)
{
	struct aca_bank_error *bank_error;

	if (!aerr || !info)
		return NULL;

	bank_error = find_bank_error(aerr, info);
	if (bank_error)
		return bank_error;

	return new_bank_error(aerr, info);
}

int aca_error_cache_log_bank_error(struct aca_handle *handle, struct aca_bank_info *info,
				   enum aca_error_type type, u64 count)
{
	struct aca_error_cache *error_cache = &handle->error_cache;
	struct aca_bank_error *bank_error;
	struct aca_error *aerr;

	if (!handle || !info || type >= ACA_ERROR_TYPE_COUNT)
		return -EINVAL;

	if (!count)
		return 0;

	aerr = &error_cache->errors[type];
	bank_error = get_bank_error(aerr, info);
	if (!bank_error)
		return -ENOMEM;

	bank_error->count += count;

	return 0;
}

static int aca_bank_parser(struct aca_handle *handle, struct aca_bank *bank, enum aca_smu_type type)
{
	const struct aca_bank_ops *bank_ops = handle->bank_ops;

	if (!bank)
		return -EINVAL;

	if (!bank_ops->aca_bank_parser)
		return -EOPNOTSUPP;

	return bank_ops->aca_bank_parser(handle, bank, type,
					 handle->data);
}

static int handler_aca_log_bank_error(struct aca_handle *handle, struct aca_bank *bank,
				      enum aca_smu_type type, void *data)
{
	int ret;

	ret = aca_bank_parser(handle, bank, type);
	if (ret)
		return ret;

	return 0;
}

static int aca_dispatch_bank(struct aca_handle_manager *mgr, struct aca_bank *bank,
			     enum aca_smu_type type, bank_handler_t handler, void *data)
{
	struct aca_handle *handle;
	int ret;

	if (list_empty(&mgr->list))
		return 0;

	list_for_each_entry(handle, &mgr->list, node) {
		if (!aca_bank_is_valid(handle, bank, type))
			continue;

		ret = handler(handle, bank, type, data);
		if (ret)
			return ret;
	}

	return 0;
}

static int aca_dispatch_banks(struct aca_handle_manager *mgr, struct aca_banks *banks,
			      enum aca_smu_type type, bank_handler_t handler, void *data)
{
	struct aca_bank_node *node;
	struct aca_bank *bank;
	int ret;

	if (!mgr || !banks)
		return -EINVAL;

	/* pre check to avoid unnecessary operations */
	if (list_empty(&mgr->list) || list_empty(&banks->list))
		return 0;

	list_for_each_entry(node, &banks->list, node) {
		bank = &node->bank;

		ret = aca_dispatch_bank(mgr, bank, type, handler, data);
		if (ret)
			return ret;
	}

	return 0;
}

static bool aca_bank_should_update(struct amdgpu_device *adev, enum aca_smu_type type)
{
	struct amdgpu_aca *aca = &adev->aca;
	bool ret = true;

	/*
	 * Because the UE Valid MCA count will only be cleared after reset,
	 * in order to avoid repeated counting of the error count,
	 * the aca bank is only updated once during the gpu recovery stage.
	 */
	if (type == ACA_SMU_TYPE_UE) {
		if (amdgpu_ras_intr_triggered())
			ret = atomic_cmpxchg(&aca->ue_update_flag, 0, 1) == 0;
		else
			atomic_set(&aca->ue_update_flag, 0);
	}

	return ret;
}

static int aca_banks_update(struct amdgpu_device *adev, enum aca_smu_type type,
			    bank_handler_t handler, struct ras_query_context *qctx, void *data)
{
	struct amdgpu_aca *aca = &adev->aca;
	struct aca_banks banks;
	u32 count = 0;
	int ret;

	if (list_empty(&aca->mgr.list))
		return 0;

	if (!aca_bank_should_update(adev, type))
		return 0;

	ret = aca_smu_get_valid_aca_count(adev, type, &count);
	if (ret)
		return ret;

	if (!count)
		return 0;

	aca_banks_init(&banks);

	ret = aca_smu_get_valid_aca_banks(adev, type, 0, count, &banks, qctx);
	if (ret)
		goto err_release_banks;

	if (list_empty(&banks.list)) {
		ret = 0;
		goto err_release_banks;
	}

	ret = aca_dispatch_banks(&aca->mgr, &banks, type,
				 handler, data);
	if (ret)
		goto err_release_banks;

err_release_banks:
	aca_banks_release(&banks);

	return ret;
}

static int aca_log_aca_error_data(struct aca_bank_error *bank_error, enum aca_error_type type, struct ras_err_data *err_data)
{
	struct aca_bank_info *info;
	struct amdgpu_smuio_mcm_config_info mcm_info;
	u64 count;

	if (type >= ACA_ERROR_TYPE_COUNT)
		return -EINVAL;

	count = bank_error->count;
	if (!count)
		return 0;

	info = &bank_error->info;
	mcm_info.die_id = info->die_id;
	mcm_info.socket_id = info->socket_id;

	switch (type) {
	case ACA_ERROR_TYPE_UE:
		amdgpu_ras_error_statistic_ue_count(err_data, &mcm_info, count);
		break;
	case ACA_ERROR_TYPE_CE:
		amdgpu_ras_error_statistic_ce_count(err_data, &mcm_info, count);
		break;
	case ACA_ERROR_TYPE_DEFERRED:
		amdgpu_ras_error_statistic_de_count(err_data, &mcm_info, count);
		break;
	default:
		break;
	}

	return 0;
}

static int aca_log_aca_error(struct aca_handle *handle, enum aca_error_type type, struct ras_err_data *err_data)
{
	struct aca_error_cache *error_cache = &handle->error_cache;
	struct aca_error *aerr = &error_cache->errors[type];
	struct aca_bank_error *bank_error, *tmp;

	mutex_lock(&aerr->lock);

	if (list_empty(&aerr->list))
		goto out_unlock;

	list_for_each_entry_safe(bank_error, tmp, &aerr->list, node) {
		aca_log_aca_error_data(bank_error, type, err_data);
		aca_bank_error_remove(aerr, bank_error);
	}

out_unlock:
	mutex_unlock(&aerr->lock);

	return 0;
}

static int __aca_get_error_data(struct amdgpu_device *adev, struct aca_handle *handle, enum aca_error_type type,
				struct ras_err_data *err_data, struct ras_query_context *qctx)
{
	enum aca_smu_type smu_type;
	int ret;

	switch (type) {
	case ACA_ERROR_TYPE_UE:
		smu_type = ACA_SMU_TYPE_UE;
		break;
	case ACA_ERROR_TYPE_CE:
	case ACA_ERROR_TYPE_DEFERRED:
		smu_type = ACA_SMU_TYPE_CE;
		break;
	default:
		return -EINVAL;
	}

	/* update aca bank to aca source error_cache first */
	ret = aca_banks_update(adev, smu_type, handler_aca_log_bank_error, qctx, NULL);
	if (ret)
		return ret;

	return aca_log_aca_error(handle, type, err_data);
}

static bool aca_handle_is_valid(struct aca_handle *handle)
{
	if (!handle->mask || !list_empty(&handle->node))
		return false;

	return true;
}

int amdgpu_aca_get_error_data(struct amdgpu_device *adev, struct aca_handle *handle,
			      enum aca_error_type type, struct ras_err_data *err_data,
			      struct ras_query_context *qctx)
{
	if (!handle || !err_data)
		return -EINVAL;

	if (aca_handle_is_valid(handle))
		return -EOPNOTSUPP;

	if ((type < 0) || (!(BIT(type) & handle->mask)))
		return  0;

	return __aca_get_error_data(adev, handle, type, err_data, qctx);
}

static void aca_error_init(struct aca_error *aerr, enum aca_error_type type)
{
	mutex_init(&aerr->lock);
	INIT_LIST_HEAD(&aerr->list);
	aerr->type = type;
	aerr->nr_errors = 0;
}

static void aca_init_error_cache(struct aca_handle *handle)
{
	struct aca_error_cache *error_cache = &handle->error_cache;
	int type;

	for (type = ACA_ERROR_TYPE_UE; type < ACA_ERROR_TYPE_COUNT; type++)
		aca_error_init(&error_cache->errors[type], type);
}

static void aca_error_fini(struct aca_error *aerr)
{
	struct aca_bank_error *bank_error, *tmp;

	mutex_lock(&aerr->lock);
	if (list_empty(&aerr->list))
		goto out_unlock;

	list_for_each_entry_safe(bank_error, tmp, &aerr->list, node)
		aca_bank_error_remove(aerr, bank_error);

out_unlock:
	mutex_destroy(&aerr->lock);
}

static void aca_fini_error_cache(struct aca_handle *handle)
{
	struct aca_error_cache *error_cache = &handle->error_cache;
	int type;

	for (type = ACA_ERROR_TYPE_UE; type < ACA_ERROR_TYPE_COUNT; type++)
		aca_error_fini(&error_cache->errors[type]);
}

static int add_aca_handle(struct amdgpu_device *adev, struct aca_handle_manager *mgr, struct aca_handle *handle,
			  const char *name, const struct aca_info *ras_info, void *data)
{
	memset(handle, 0, sizeof(*handle));

	handle->adev = adev;
	handle->mgr = mgr;
	handle->name = name;
	handle->hwip = ras_info->hwip;
	handle->mask = ras_info->mask;
	handle->bank_ops = ras_info->bank_ops;
	handle->data = data;
	aca_init_error_cache(handle);

	INIT_LIST_HEAD(&handle->node);
	list_add_tail(&handle->node, &mgr->list);
	mgr->nr_handles++;

	return 0;
}

static ssize_t aca_sysfs_read(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct aca_handle *handle = container_of(attr, struct aca_handle, aca_attr);

	/* NOTE: the aca cache will be auto cleared once read,
	 * So the driver should unify the query entry point, forward request to ras query interface directly */
	return amdgpu_ras_aca_sysfs_read(dev, attr, handle, buf, handle->data);
}

static int add_aca_sysfs(struct amdgpu_device *adev, struct aca_handle *handle)
{
	struct device_attribute *aca_attr = &handle->aca_attr;

	snprintf(handle->attr_name, sizeof(handle->attr_name) - 1, "aca_%s", handle->name);
	aca_attr->show = aca_sysfs_read;
	aca_attr->attr.name = handle->attr_name;
	aca_attr->attr.mode = S_IRUGO;
	sysfs_attr_init(&aca_attr->attr);

	return sysfs_add_file_to_group(&adev->dev->kobj,
				       &aca_attr->attr,
				       "ras");
}

int amdgpu_aca_add_handle(struct amdgpu_device *adev, struct aca_handle *handle,
			  const char *name, const struct aca_info *ras_info, void *data)
{
	struct amdgpu_aca *aca = &adev->aca;
	int ret;

	if (!amdgpu_aca_is_enabled(adev))
		return 0;

	ret = add_aca_handle(adev, &aca->mgr, handle, name, ras_info, data);
	if (ret)
		return ret;

	return add_aca_sysfs(adev, handle);
}

static void remove_aca_handle(struct aca_handle *handle)
{
	struct aca_handle_manager *mgr = handle->mgr;

	aca_fini_error_cache(handle);
	list_del(&handle->node);
	mgr->nr_handles--;
}

static void remove_aca_sysfs(struct aca_handle *handle)
{
	struct amdgpu_device *adev = handle->adev;
	struct device_attribute *aca_attr = &handle->aca_attr;

	if (adev->dev->kobj.sd)
		sysfs_remove_file_from_group(&adev->dev->kobj,
					     &aca_attr->attr,
					     "ras");
}

void amdgpu_aca_remove_handle(struct aca_handle *handle)
{
	if (!handle || list_empty(&handle->node))
		return;

	remove_aca_sysfs(handle);
	remove_aca_handle(handle);
}

static int aca_manager_init(struct aca_handle_manager *mgr)
{
	INIT_LIST_HEAD(&mgr->list);
	mgr->nr_handles = 0;

	return 0;
}

static void aca_manager_fini(struct aca_handle_manager *mgr)
{
	struct aca_handle *handle, *tmp;

	if (list_empty(&mgr->list))
		return;

	list_for_each_entry_safe(handle, tmp, &mgr->list, node)
		amdgpu_aca_remove_handle(handle);
}

bool amdgpu_aca_is_enabled(struct amdgpu_device *adev)
{
	return (adev->aca.is_enabled ||
		adev->debug_enable_ras_aca);
}

int amdgpu_aca_init(struct amdgpu_device *adev)
{
	struct amdgpu_aca *aca = &adev->aca;
	int ret;

	atomic_set(&aca->ue_update_flag, 0);

	ret = aca_manager_init(&aca->mgr);
	if (ret)
		return ret;

	return 0;
}

void amdgpu_aca_fini(struct amdgpu_device *adev)
{
	struct amdgpu_aca *aca = &adev->aca;

	aca_manager_fini(&aca->mgr);

	atomic_set(&aca->ue_update_flag, 0);
}

int amdgpu_aca_reset(struct amdgpu_device *adev)
{
	struct amdgpu_aca *aca = &adev->aca;

	atomic_set(&aca->ue_update_flag, 0);

	return 0;
}

void amdgpu_aca_set_smu_funcs(struct amdgpu_device *adev, const struct aca_smu_funcs *smu_funcs)
{
	struct amdgpu_aca *aca = &adev->aca;

	WARN_ON(aca->smu_funcs);
	aca->smu_funcs = smu_funcs;
}

int aca_bank_info_decode(struct aca_bank *bank, struct aca_bank_info *info)
{
	u64 ipid;
	u32 instidhi, instidlo;

	if (!bank || !info)
		return -EINVAL;

	ipid = bank->regs[ACA_REG_IDX_IPID];
	info->hwid = ACA_REG__IPID__HARDWAREID(ipid);
	info->mcatype = ACA_REG__IPID__MCATYPE(ipid);
	/*
	 * Unfied DieID Format: SAASS. A:AID, S:Socket.
	 * Unfied DieID[4:4] = InstanceId[0:0]
	 * Unfied DieID[0:3] = InstanceIdHi[0:3]
	 */
	instidhi = ACA_REG__IPID__INSTANCEIDHI(ipid);
	instidlo = ACA_REG__IPID__INSTANCEIDLO(ipid);
	info->die_id = ((instidhi >> 2) & 0x03);
	info->socket_id = ((instidlo & 0x1) << 2) | (instidhi & 0x03);

	return 0;
}

static int aca_bank_get_error_code(struct amdgpu_device *adev, struct aca_bank *bank)
{
	struct amdgpu_aca *aca = &adev->aca;
	const struct aca_smu_funcs *smu_funcs = aca->smu_funcs;

	if (!smu_funcs || !smu_funcs->parse_error_code)
		return -EOPNOTSUPP;

	return smu_funcs->parse_error_code(adev, bank);
}

int aca_bank_check_error_codes(struct amdgpu_device *adev, struct aca_bank *bank, int *err_codes, int size)
{
	int i, error_code;

	if (!bank || !err_codes)
		return -EINVAL;

	error_code = aca_bank_get_error_code(adev, bank);
	if (error_code < 0)
		return error_code;

	for (i = 0; i < size; i++) {
		if (err_codes[i] == error_code)
			return 0;
	}

	return -EINVAL;
}

int amdgpu_aca_smu_set_debug_mode(struct amdgpu_device *adev, bool en)
{
	struct amdgpu_aca *aca = &adev->aca;
	const struct aca_smu_funcs *smu_funcs = aca->smu_funcs;

	if (!smu_funcs || !smu_funcs->set_debug_mode)
		return -EOPNOTSUPP;

	return smu_funcs->set_debug_mode(adev, en);
}

#if defined(CONFIG_DEBUG_FS)
static int amdgpu_aca_smu_debug_mode_set(void *data, u64 val)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)data;
	int ret;

	ret = amdgpu_ras_set_aca_debug_mode(adev, val ? true : false);
	if (ret)
		return ret;

	dev_info(adev->dev, "amdgpu set smu aca debug mode %s success\n", val ? "on" : "off");

	return 0;
}

static void aca_dump_entry(struct seq_file *m, struct aca_bank *bank, enum aca_smu_type type, int idx)
{
	struct aca_bank_info info;
	int i, ret;

	ret = aca_bank_info_decode(bank, &info);
	if (ret)
		return;

	seq_printf(m, "aca entry[%d].type: %s\n", idx, type ==  ACA_SMU_TYPE_UE ? "UE" : "CE");
	seq_printf(m, "aca entry[%d].info: socketid:%d aid:%d hwid:0x%03x mcatype:0x%04x\n",
		   idx, info.socket_id, info.die_id, info.hwid, info.mcatype);

	for (i = 0; i < ARRAY_SIZE(aca_regs); i++)
		seq_printf(m, "aca entry[%d].regs[%d]: 0x%016llx\n", idx, aca_regs[i].reg_idx, bank->regs[aca_regs[i].reg_idx]);
}

struct aca_dump_context {
	struct seq_file *m;
	int idx;
};

static int handler_aca_bank_dump(struct aca_handle *handle, struct aca_bank *bank,
				 enum aca_smu_type type, void *data)
{
	struct aca_dump_context *ctx = (struct aca_dump_context *)data;

	aca_dump_entry(ctx->m, bank, type, ctx->idx++);

	return handler_aca_log_bank_error(handle, bank, type, NULL);
}

static int aca_dump_show(struct seq_file *m, enum aca_smu_type type)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)m->private;
	struct aca_dump_context context = {
		.m = m,
		.idx = 0,
	};

	return aca_banks_update(adev, type, handler_aca_bank_dump, NULL, (void *)&context);
}

static int aca_dump_ce_show(struct seq_file *m, void *unused)
{
	return aca_dump_show(m, ACA_SMU_TYPE_CE);
}

static int aca_dump_ce_open(struct inode *inode, struct file *file)
{
	return single_open(file, aca_dump_ce_show, inode->i_private);
}

static const struct file_operations aca_ce_dump_debug_fops = {
	.owner = THIS_MODULE,
	.open = aca_dump_ce_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int aca_dump_ue_show(struct seq_file *m, void *unused)
{
	return aca_dump_show(m, ACA_SMU_TYPE_UE);
}

static int aca_dump_ue_open(struct inode *inode, struct file *file)
{
	return single_open(file, aca_dump_ue_show, inode->i_private);
}

static const struct file_operations aca_ue_dump_debug_fops = {
	.owner = THIS_MODULE,
	.open = aca_dump_ue_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

DEFINE_DEBUGFS_ATTRIBUTE(aca_debug_mode_fops, NULL, amdgpu_aca_smu_debug_mode_set, "%llu\n");
#endif

void amdgpu_aca_smu_debugfs_init(struct amdgpu_device *adev, struct dentry *root)
{
#if defined(CONFIG_DEBUG_FS)
	if (!root)
		return;

	debugfs_create_file("aca_debug_mode", 0200, root, adev, &aca_debug_mode_fops);
	debugfs_create_file("aca_ue_dump", 0400, root, adev, &aca_ue_dump_debug_fops);
	debugfs_create_file("aca_ce_dump", 0400, root, adev, &aca_ce_dump_debug_fops);
#endif
}
