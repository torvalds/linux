/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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
#ifndef _SMUMGR_H_
#define _SMUMGR_H_
#include <linux/types.h>
#include "pp_instance.h"
#include "amd_powerplay.h"

struct pp_smumgr;
struct pp_instance;

#define smu_lower_32_bits(n) ((uint32_t)(n))
#define smu_upper_32_bits(n) ((uint32_t)(((n)>>16)>>16))

enum AVFS_BTC_STATUS {
	AVFS_BTC_BOOT = 0,
	AVFS_BTC_BOOT_STARTEDSMU,
	AVFS_LOAD_VIRUS,
	AVFS_BTC_VIRUS_LOADED,
	AVFS_BTC_VIRUS_FAIL,
	AVFS_BTC_COMPLETED_PREVIOUSLY,
	AVFS_BTC_ENABLEAVFS,
	AVFS_BTC_STARTED,
	AVFS_BTC_FAILED,
	AVFS_BTC_RESTOREVFT_FAILED,
	AVFS_BTC_SAVEVFT_FAILED,
	AVFS_BTC_DPMTABLESETUP_FAILED,
	AVFS_BTC_COMPLETED_UNSAVED,
	AVFS_BTC_COMPLETED_SAVED,
	AVFS_BTC_COMPLETED_RESTORED,
	AVFS_BTC_DISABLED,
	AVFS_BTC_NOTSUPPORTED,
	AVFS_BTC_SMUMSG_ERROR
};

struct pp_smumgr_func {
	int (*smu_init)(struct pp_smumgr *smumgr);
	int (*smu_fini)(struct pp_smumgr *smumgr);
	int (*start_smu)(struct pp_smumgr *smumgr);
	int (*check_fw_load_finish)(struct pp_smumgr *smumgr,
				    uint32_t firmware);
	int (*request_smu_load_fw)(struct pp_smumgr *smumgr);
	int (*request_smu_load_specific_fw)(struct pp_smumgr *smumgr,
					    uint32_t firmware);
	int (*get_argument)(struct pp_smumgr *smumgr);
	int (*send_msg_to_smc)(struct pp_smumgr *smumgr, uint16_t msg);
	int (*send_msg_to_smc_with_parameter)(struct pp_smumgr *smumgr,
					  uint16_t msg, uint32_t parameter);
	int (*download_pptable_settings)(struct pp_smumgr *smumgr,
					 void **table);
	int (*upload_pptable_settings)(struct pp_smumgr *smumgr);
};

struct pp_smumgr {
	uint32_t chip_family;
	uint32_t chip_id;
	void *device;
	void *backend;
	uint32_t usec_timeout;
	bool reload_fw;
	const struct pp_smumgr_func *smumgr_funcs;
};


extern int smum_init(struct amd_pp_init *pp_init,
		     struct pp_instance *handle);

extern int smum_fini(struct pp_smumgr *smumgr);

extern int smum_get_argument(struct pp_smumgr *smumgr);

extern int smum_download_powerplay_table(struct pp_smumgr *smumgr, void **table);

extern int smum_upload_powerplay_table(struct pp_smumgr *smumgr);

extern int smum_send_msg_to_smc(struct pp_smumgr *smumgr, uint16_t msg);

extern int smum_send_msg_to_smc_with_parameter(struct pp_smumgr *smumgr,
					uint16_t msg, uint32_t parameter);

extern int smum_wait_on_register(struct pp_smumgr *smumgr,
				uint32_t index, uint32_t value, uint32_t mask);

extern int smum_wait_for_register_unequal(struct pp_smumgr *smumgr,
				uint32_t index, uint32_t value, uint32_t mask);

extern int smum_wait_on_indirect_register(struct pp_smumgr *smumgr,
				uint32_t indirect_port, uint32_t index,
				uint32_t value, uint32_t mask);


extern void smum_wait_for_indirect_register_unequal(
				struct pp_smumgr *smumgr,
				uint32_t indirect_port, uint32_t index,
				uint32_t value, uint32_t mask);

extern int smu_allocate_memory(void *device, uint32_t size,
			 enum cgs_gpu_mem_type type,
			 uint32_t byte_align, uint64_t *mc_addr,
			 void **kptr, void *handle);

extern int smu_free_memory(void *device, void *handle);

#define SMUM_FIELD_SHIFT(reg, field) reg##__##field##__SHIFT

#define SMUM_FIELD_MASK(reg, field) reg##__##field##_MASK

#define SMUM_WAIT_INDIRECT_REGISTER_GIVEN_INDEX(smumgr,			\
					port, index, value, mask)	\
	smum_wait_on_indirect_register(smumgr,				\
				mm##port##_INDEX, index, value, mask)

#define SMUM_WAIT_INDIRECT_REGISTER(smumgr, port, reg, value, mask)    \
	    SMUM_WAIT_INDIRECT_REGISTER_GIVEN_INDEX(smumgr, port, ix##reg, value, mask)

#define SMUM_WAIT_INDIRECT_FIELD(smumgr, port, reg, field, fieldval)                          \
	    SMUM_WAIT_INDIRECT_REGISTER(smumgr, port, reg, (fieldval) << SMUM_FIELD_SHIFT(reg, field), \
			            SMUM_FIELD_MASK(reg, field) )

#define SMUM_WAIT_REGISTER_UNEQUAL_GIVEN_INDEX(smumgr,         \
							index, value, mask) \
		smum_wait_for_register_unequal(smumgr,            \
					index, value, mask)

#define SMUM_WAIT_REGISTER_UNEQUAL(smumgr, reg, value, mask)		\
	SMUM_WAIT_REGISTER_UNEQUAL_GIVEN_INDEX(smumgr,			\
				mm##reg, value, mask)

#define SMUM_WAIT_FIELD_UNEQUAL(smumgr, reg, field, fieldval)		\
	SMUM_WAIT_REGISTER_UNEQUAL(smumgr, reg,				\
		(fieldval) << SMUM_FIELD_SHIFT(reg, field),		\
		SMUM_FIELD_MASK(reg, field))

#define SMUM_GET_FIELD(value, reg, field)				\
		(((value) & SMUM_FIELD_MASK(reg, field))		\
		>> SMUM_FIELD_SHIFT(reg, field))

#define SMUM_READ_FIELD(device, reg, field)                           \
		SMUM_GET_FIELD(cgs_read_register(device, mm##reg), reg, field)

#define SMUM_SET_FIELD(value, reg, field, field_val)                  \
		(((value) & ~SMUM_FIELD_MASK(reg, field)) |                    \
		(SMUM_FIELD_MASK(reg, field) & ((field_val) <<                 \
			SMUM_FIELD_SHIFT(reg, field))))

#define SMUM_READ_INDIRECT_FIELD(device, port, reg, field) \
	    SMUM_GET_FIELD(cgs_read_ind_register(device, port, ix##reg), \
			   reg, field)

#define SMUM_WAIT_VFPF_INDIRECT_REGISTER_GIVEN_INDEX(smumgr,		\
				port, index, value, mask)		\
	smum_wait_on_indirect_register(smumgr,				\
		mm##port##_INDEX_0, index, value, mask)

#define SMUM_WAIT_VFPF_INDIRECT_REGISTER_UNEQUAL_GIVEN_INDEX(smumgr,	\
				port, index, value, mask)		\
	smum_wait_for_indirect_register_unequal(smumgr,			\
		mm##port##_INDEX_0, index, value, mask)


#define SMUM_WAIT_VFPF_INDIRECT_REGISTER(smumgr, port, reg, value, mask) \
	SMUM_WAIT_VFPF_INDIRECT_REGISTER_GIVEN_INDEX(smumgr, port, ix##reg, value, mask)

#define SMUM_WAIT_VFPF_INDIRECT_REGISTER_UNEQUAL(smumgr, port, reg, value, mask)     \
		SMUM_WAIT_VFPF_INDIRECT_REGISTER_UNEQUAL_GIVEN_INDEX(smumgr, port, ix##reg, value, mask)


/*Operations on named fields.*/

#define SMUM_READ_VFPF_INDIRECT_FIELD(device, port, reg, field) \
		SMUM_GET_FIELD(cgs_read_ind_register(device, port, ix##reg), \
			reg, field)

#define SMUM_WRITE_FIELD(device, reg, field, fieldval)            \
		cgs_write_register(device, mm##reg, \
		SMUM_SET_FIELD(cgs_read_register(device, mm##reg), reg, field, fieldval))

#define SMUM_WRITE_VFPF_INDIRECT_FIELD(device, port, reg, field, fieldval)    \
		cgs_write_ind_register(device, port, ix##reg, \
			SMUM_SET_FIELD(cgs_read_ind_register(device, port, ix##reg), \
			reg, field, fieldval))


#define SMUM_WRITE_INDIRECT_FIELD(device, port, reg, field, fieldval)    		\
		cgs_write_ind_register(device, port, ix##reg, 				\
			SMUM_SET_FIELD(cgs_read_ind_register(device, port, ix##reg), 	\
				       reg, field, fieldval))


#define SMUM_WAIT_VFPF_INDIRECT_FIELD(smumgr, port, reg, field, fieldval) \
	SMUM_WAIT_VFPF_INDIRECT_REGISTER(smumgr, port, reg,		\
		(fieldval) << SMUM_FIELD_SHIFT(reg, field),		\
		SMUM_FIELD_MASK(reg, field))

#define SMUM_WAIT_VFPF_INDIRECT_FIELD_UNEQUAL(smumgr, port, reg, field, fieldval) \
	SMUM_WAIT_VFPF_INDIRECT_REGISTER_UNEQUAL(smumgr, port, reg,	\
		(fieldval) << SMUM_FIELD_SHIFT(reg, field),		\
		SMUM_FIELD_MASK(reg, field))

#define SMUM_WAIT_INDIRECT_REGISTER_UNEQUAL_GIVEN_INDEX(smumgr, port, index, value, mask)    \
	smum_wait_for_indirect_register_unequal(smumgr,			\
		mm##port##_INDEX, index, value, mask)

#define SMUM_WAIT_INDIRECT_REGISTER_UNEQUAL(smumgr, port, reg, value, mask)    \
	    SMUM_WAIT_INDIRECT_REGISTER_UNEQUAL_GIVEN_INDEX(smumgr, port, ix##reg, value, mask)

#define SMUM_WAIT_INDIRECT_FIELD_UNEQUAL(smumgr, port, reg, field, fieldval)                          \
	    SMUM_WAIT_INDIRECT_REGISTER_UNEQUAL(smumgr, port, reg, (fieldval) << SMUM_FIELD_SHIFT(reg, field), \
			            SMUM_FIELD_MASK(reg, field) )

#endif
