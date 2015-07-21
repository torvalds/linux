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
#include "linux/delay.h"
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include "cgs_common.h"
#include "power_state.h"
#include "hwmgr.h"



int hwmgr_init(struct amd_pp_init *pp_init, struct pp_instance *handle)
{
	struct pp_hwmgr *hwmgr;

	if ((handle == NULL) || (pp_init == NULL))
		return -EINVAL;

	hwmgr = kzalloc(sizeof(struct pp_hwmgr), GFP_KERNEL);
	if (hwmgr == NULL)
		return -ENOMEM;

	handle->hwmgr = hwmgr;
	hwmgr->smumgr = handle->smu_mgr;
	hwmgr->device = pp_init->device;
	hwmgr->chip_family = pp_init->chip_family;
	hwmgr->chip_id = pp_init->chip_id;
	hwmgr->hw_revision = pp_init->rev_id;
	hwmgr->usec_timeout = AMD_MAX_USEC_TIMEOUT;
	hwmgr->power_source = PP_PowerSource_AC;

	switch (hwmgr->chip_family) {
	default:
		return -EINVAL;
	}

	phm_init_dynamic_caps(hwmgr);

	return 0;
}

int hwmgr_fini(struct pp_hwmgr *hwmgr)
{
	if (hwmgr == NULL || hwmgr->ps == NULL)
		return -EINVAL;

	kfree(hwmgr->ps);
	kfree(hwmgr);
	return 0;
}

int hw_init_power_state_table(struct pp_hwmgr *hwmgr)
{
	int result;
	unsigned int i;
	unsigned int table_entries;
	struct pp_power_state *state;
	int size;

	if (hwmgr->hwmgr_func->get_num_of_pp_table_entries == NULL)
		return -EINVAL;

	if (hwmgr->hwmgr_func->get_power_state_size == NULL)
		return -EINVAL;

	hwmgr->num_ps = table_entries = hwmgr->hwmgr_func->get_num_of_pp_table_entries(hwmgr);

	hwmgr->ps_size = size = hwmgr->hwmgr_func->get_power_state_size(hwmgr) +
					  sizeof(struct pp_power_state);

	hwmgr->ps = kzalloc(size * table_entries, GFP_KERNEL);

	state = hwmgr->ps;

	for (i = 0; i < table_entries; i++) {
		result = hwmgr->hwmgr_func->get_pp_table_entry(hwmgr, i, state);
		if (state->classification.flags & PP_StateClassificationFlag_Boot) {
			hwmgr->boot_ps = state;
			hwmgr->current_ps = hwmgr->request_ps = state;
		}

		state->id = i + 1; /* assigned unique num for every power state id */

		if (state->classification.flags & PP_StateClassificationFlag_Uvd)
			hwmgr->uvd_ps = state;
		state = (struct pp_power_state *)((uint64_t)state + size);
	}

	return 0;
}


/**
 * Returns once the part of the register indicated by the mask has
 * reached the given value.
 */
int phm_wait_on_register(struct pp_hwmgr *hwmgr, uint32_t index,
			 uint32_t value, uint32_t mask)
{
	uint32_t i;
	uint32_t cur_value;

	if (hwmgr == NULL || hwmgr->device == NULL) {
		printk(KERN_ERR "[ powerplay ] Invalid Hardware Manager!");
		return -EINVAL;
	}

	for (i = 0; i < hwmgr->usec_timeout; i++) {
		cur_value = cgs_read_register(hwmgr->device, index);
		if ((cur_value & mask) == (value & mask))
			break;
		udelay(1);
	}

	/* timeout means wrong logic*/
	if (i == hwmgr->usec_timeout)
		return -1;
	return 0;
}

int phm_wait_for_register_unequal(struct pp_hwmgr *hwmgr,
				uint32_t index, uint32_t value, uint32_t mask)
{
	uint32_t i;
	uint32_t cur_value;

	if (hwmgr == NULL || hwmgr->device == NULL) {
		printk(KERN_ERR "[ powerplay ] Invalid Hardware Manager!");
		return -EINVAL;
	}

	for (i = 0; i < hwmgr->usec_timeout; i++) {
		cur_value = cgs_read_register(hwmgr->device, index);
		if ((cur_value & mask) != (value & mask))
			break;
		udelay(1);
	}

	/* timeout means wrong logic*/
	if (i == hwmgr->usec_timeout)
		return -1;
	return 0;
}


/**
 * Returns once the part of the register indicated by the mask has
 * reached the given value.The indirect space is described by giving
 * the memory-mapped index of the indirect index register.
 */
void phm_wait_on_indirect_register(struct pp_hwmgr *hwmgr,
				uint32_t indirect_port,
				uint32_t index,
				uint32_t value,
				uint32_t mask)
{
	if (hwmgr == NULL || hwmgr->device == NULL) {
		printk(KERN_ERR "[ powerplay ] Invalid Hardware Manager!");
		return;
	}

	cgs_write_register(hwmgr->device, indirect_port, index);
	phm_wait_on_register(hwmgr, indirect_port + 1, mask, value);
}

void phm_wait_for_indirect_register_unequal(struct pp_hwmgr *hwmgr,
					uint32_t indirect_port,
					uint32_t index,
					uint32_t value,
					uint32_t mask)
{
	if (hwmgr == NULL || hwmgr->device == NULL) {
		printk(KERN_ERR "[ powerplay ] Invalid Hardware Manager!");
		return;
	}

	cgs_write_register(hwmgr->device, indirect_port, index);
	phm_wait_for_register_unequal(hwmgr, indirect_port + 1,
				      value, mask);
}
