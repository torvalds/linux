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

#ifndef __AMDGPU_ACA_H__
#define __AMDGPU_ACA_H__

#include <linux/list.h>

struct ras_err_data;
struct ras_query_context;

#define ACA_MAX_REGS_COUNT	(16)

#define ACA_REG_FIELD(x, h, l)			(((x) & GENMASK_ULL(h, l)) >> l)
#define ACA_REG__STATUS__VAL(x)			ACA_REG_FIELD(x, 63, 63)
#define ACA_REG__STATUS__OVERFLOW(x)		ACA_REG_FIELD(x, 62, 62)
#define ACA_REG__STATUS__UC(x)			ACA_REG_FIELD(x, 61, 61)
#define ACA_REG__STATUS__EN(x)			ACA_REG_FIELD(x, 60, 60)
#define ACA_REG__STATUS__MISCV(x)		ACA_REG_FIELD(x, 59, 59)
#define ACA_REG__STATUS__ADDRV(x)		ACA_REG_FIELD(x, 58, 58)
#define ACA_REG__STATUS__PCC(x)			ACA_REG_FIELD(x, 57, 57)
#define ACA_REG__STATUS__ERRCOREIDVAL(x)	ACA_REG_FIELD(x, 56, 56)
#define ACA_REG__STATUS__TCC(x)			ACA_REG_FIELD(x, 55, 55)
#define ACA_REG__STATUS__SYNDV(x)		ACA_REG_FIELD(x, 53, 53)
#define ACA_REG__STATUS__CECC(x)		ACA_REG_FIELD(x, 46, 46)
#define ACA_REG__STATUS__UECC(x)		ACA_REG_FIELD(x, 45, 45)
#define ACA_REG__STATUS__DEFERRED(x)		ACA_REG_FIELD(x, 44, 44)
#define ACA_REG__STATUS__POISON(x)		ACA_REG_FIELD(x, 43, 43)
#define ACA_REG__STATUS__SCRUB(x)		ACA_REG_FIELD(x, 40, 40)
#define ACA_REG__STATUS__ERRCOREID(x)		ACA_REG_FIELD(x, 37, 32)
#define ACA_REG__STATUS__ADDRLSB(x)		ACA_REG_FIELD(x, 29, 24)
#define ACA_REG__STATUS__ERRORCODEEXT(x)	ACA_REG_FIELD(x, 21, 16)
#define ACA_REG__STATUS__ERRORCODE(x)		ACA_REG_FIELD(x, 15, 0)

#define ACA_REG__IPID__MCATYPE(x)		ACA_REG_FIELD(x, 63, 48)
#define ACA_REG__IPID__INSTANCEIDHI(x)		ACA_REG_FIELD(x, 47, 44)
#define ACA_REG__IPID__HARDWAREID(x)		ACA_REG_FIELD(x, 43, 32)
#define ACA_REG__IPID__INSTANCEIDLO(x)		ACA_REG_FIELD(x, 31, 0)

#define ACA_REG__MISC0__VALID(x)		ACA_REG_FIELD(x, 63, 63)
#define ACA_REG__MISC0__OVRFLW(x)		ACA_REG_FIELD(x, 48, 48)
#define ACA_REG__MISC0__ERRCNT(x)		ACA_REG_FIELD(x, 43, 32)

#define ACA_REG__SYND__ERRORINFORMATION(x)	ACA_REG_FIELD(x, 17, 0)

/* NOTE: The following codes refers to the smu header file */
#define ACA_EXTERROR_CODE_CE			0x3a
#define ACA_EXTERROR_CODE_FAULT			0x3b

#define ACA_ERROR_UE_MASK		BIT_MASK(ACA_ERROR_TYPE_UE)
#define ACA_ERROR_CE_MASK		BIT_MASK(ACA_ERROR_TYPE_CE)
#define ACA_ERROR_DEFERRED_MASK		BIT_MASK(ACA_ERROR_TYPE_DEFERRED)

#define mmSMNAID_AID0_MCA_SMU		0x03b30400	/* SMN AID AID0 */
#define mmSMNAID_XCD0_MCA_SMU		0x36430400	/* SMN AID XCD0 */
#define mmSMNAID_XCD1_MCA_SMU		0x38430400	/* SMN AID XCD1 */
#define mmSMNXCD_XCD0_MCA_SMU		0x40430400	/* SMN XCD XCD0 */

enum aca_reg_idx {
	ACA_REG_IDX_CTL			= 0,
	ACA_REG_IDX_STATUS		= 1,
	ACA_REG_IDX_ADDR		= 2,
	ACA_REG_IDX_MISC0		= 3,
	ACA_REG_IDX_CONFG		= 4,
	ACA_REG_IDX_IPID		= 5,
	ACA_REG_IDX_SYND		= 6,
	ACA_REG_IDX_DESTAT		= 8,
	ACA_REG_IDX_DEADDR		= 9,
	ACA_REG_IDX_CTL_MASK		= 10,
	ACA_REG_IDX_COUNT		= 16,
};

enum aca_hwip_type {
	ACA_HWIP_TYPE_UNKNOW = -1,
	ACA_HWIP_TYPE_PSP = 0,
	ACA_HWIP_TYPE_UMC,
	ACA_HWIP_TYPE_SMU,
	ACA_HWIP_TYPE_PCS_XGMI,
	ACA_HWIP_TYPE_COUNT,
};

enum aca_error_type {
	ACA_ERROR_TYPE_INVALID = -1,
	ACA_ERROR_TYPE_UE = 0,
	ACA_ERROR_TYPE_CE,
	ACA_ERROR_TYPE_DEFERRED,
	ACA_ERROR_TYPE_COUNT
};

enum aca_smu_type {
	ACA_SMU_TYPE_UE = 0,
	ACA_SMU_TYPE_CE,
	ACA_SMU_TYPE_COUNT,
};

struct aca_bank {
	enum aca_smu_type type;
	u64 regs[ACA_MAX_REGS_COUNT];
};

struct aca_bank_node {
	struct aca_bank bank;
	struct list_head node;
};

struct aca_bank_info {
	int die_id;
	int socket_id;
	int hwid;
	int mcatype;
};

struct aca_bank_error {
	struct list_head node;
	struct aca_bank_info info;
	u64 count;
};

struct aca_error {
	struct list_head list;
	struct mutex lock;
	enum aca_error_type type;
	int nr_errors;
};

struct aca_handle_manager {
	struct list_head list;
	int nr_handles;
};

struct aca_error_cache {
	struct aca_error errors[ACA_ERROR_TYPE_COUNT];
};

struct aca_handle {
	struct list_head node;
	enum aca_hwip_type hwip;
	struct amdgpu_device *adev;
	struct aca_handle_manager *mgr;
	struct aca_error_cache error_cache;
	const struct aca_bank_ops *bank_ops;
	struct device_attribute aca_attr;
	char attr_name[64];
	const char *name;
	u32 mask;
	void *data;
};

struct aca_bank_ops {
	int (*aca_bank_parser)(struct aca_handle *handle, struct aca_bank *bank, enum aca_smu_type type, void *data);
	bool (*aca_bank_is_valid)(struct aca_handle *handle, struct aca_bank *bank, enum aca_smu_type type,
				  void *data);
};

struct aca_smu_funcs {
	int max_ue_bank_count;
	int max_ce_bank_count;
	int (*set_debug_mode)(struct amdgpu_device *adev, bool enable);
	int (*get_valid_aca_count)(struct amdgpu_device *adev, enum aca_smu_type type, u32 *count);
	int (*get_valid_aca_bank)(struct amdgpu_device *adev, enum aca_smu_type type, int idx, struct aca_bank *bank);
	int (*parse_error_code)(struct amdgpu_device *adev, struct aca_bank *bank);
};

struct amdgpu_aca {
	struct aca_handle_manager mgr;
	const struct aca_smu_funcs *smu_funcs;
	atomic_t ue_update_flag;
	bool is_enabled;
};

struct aca_info {
	enum aca_hwip_type hwip;
	const struct aca_bank_ops *bank_ops;
	u32 mask;
};

int amdgpu_aca_init(struct amdgpu_device *adev);
void amdgpu_aca_fini(struct amdgpu_device *adev);
int amdgpu_aca_reset(struct amdgpu_device *adev);
void amdgpu_aca_set_smu_funcs(struct amdgpu_device *adev, const struct aca_smu_funcs *smu_funcs);
bool amdgpu_aca_is_enabled(struct amdgpu_device *adev);

int aca_bank_info_decode(struct aca_bank *bank, struct aca_bank_info *info);
int aca_bank_check_error_codes(struct amdgpu_device *adev, struct aca_bank *bank, int *err_codes, int size);

int amdgpu_aca_add_handle(struct amdgpu_device *adev, struct aca_handle *handle,
			  const char *name, const struct aca_info *aca_info, void *data);
void amdgpu_aca_remove_handle(struct aca_handle *handle);
int amdgpu_aca_get_error_data(struct amdgpu_device *adev, struct aca_handle *handle,
			      enum aca_error_type type, struct ras_err_data *err_data,
			      struct ras_query_context *qctx);
int amdgpu_aca_smu_set_debug_mode(struct amdgpu_device *adev, bool en);
void amdgpu_aca_smu_debugfs_init(struct amdgpu_device *adev, struct dentry *root);
int aca_error_cache_log_bank_error(struct aca_handle *handle, struct aca_bank_info *info,
				   enum aca_error_type type, u64 count);
#endif
