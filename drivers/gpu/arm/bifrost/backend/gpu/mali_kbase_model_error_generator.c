// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2014-2015, 2018-2022 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#include <mali_kbase.h>
#include <linux/random.h>
#include "backend/gpu/mali_kbase_model_dummy.h"

static struct kbase_error_atom *error_track_list;

unsigned int rand_seed;

/*following error probability are set quite high in order to stress the driver*/
unsigned int error_probability = 50;	/* to be set between 0 and 100 */
/* probability to have multiple error give that there is an error */
unsigned int multiple_error_probability = 50;

#ifdef CONFIG_MALI_ERROR_INJECT_RANDOM

/* all the error conditions supported by the model */
#define TOTAL_FAULTS 27
/* maximum number of levels in the MMU translation table tree */
#define MAX_MMU_TABLE_LEVEL 4
/* worst case scenario is <1 MMU fault + 1 job fault + 2 GPU faults> */
#define MAX_CONCURRENT_FAULTS 3

/**
 * gpu_generate_error - Generate GPU error
 */
static void gpu_generate_error(void)
{
	unsigned int errors_num = 0;

	/*is there at least one error? */
	if ((prandom_u32() % 100) < error_probability) {
		/* pick up a faulty mmu address space */
		hw_error_status.faulty_mmu_as = prandom_u32() % NUM_MMU_AS;
		/* pick up an mmu table level */
		hw_error_status.mmu_table_level =
			1 + (prandom_u32() % MAX_MMU_TABLE_LEVEL);
		hw_error_status.errors_mask =
			(u32)(1 << (prandom_u32() % TOTAL_FAULTS));

		/*is there also one or more errors? */
		if ((prandom_u32() % 100) < multiple_error_probability) {
			errors_num = 1 + (prandom_u32() %
					  (MAX_CONCURRENT_FAULTS - 1));
			while (errors_num-- > 0) {
				u32 temp_mask;

				temp_mask = (u32)(
					1 << (prandom_u32() % TOTAL_FAULTS));
				/* below we check that no bit of the same error
				 * type is set again in the error mask
				 */
				if ((temp_mask & IS_A_JOB_ERROR) &&
						(hw_error_status.errors_mask &
							IS_A_JOB_ERROR)) {
					errors_num++;
					continue;
				}
				if ((temp_mask & IS_A_MMU_ERROR) &&
						(hw_error_status.errors_mask &
							IS_A_MMU_ERROR)) {
					errors_num++;
					continue;
				}
				if ((temp_mask & IS_A_GPU_ERROR) &&
						(hw_error_status.errors_mask &
							IS_A_GPU_ERROR)) {
					errors_num++;
					continue;
				}
				/* this error mask is already set */
				if ((hw_error_status.errors_mask | temp_mask) ==
						hw_error_status.errors_mask) {
					errors_num++;
					continue;
				}
				hw_error_status.errors_mask |= temp_mask;
			}
		}
	}
}
#endif

int job_atom_inject_error(struct kbase_error_params *params)
{
	struct kbase_error_atom *new_elem;

	KBASE_DEBUG_ASSERT(params);

	new_elem = kzalloc(sizeof(*new_elem), GFP_KERNEL);

	if (!new_elem) {
		model_error_log(KBASE_CORE,
			"\njob_atom_inject_error: kzalloc failed for new_elem\n"
									);
		return -ENOMEM;
	}
	new_elem->params.jc = params->jc;
	new_elem->params.errors_mask = params->errors_mask;
	new_elem->params.mmu_table_level = params->mmu_table_level;
	new_elem->params.faulty_mmu_as = params->faulty_mmu_as;

	/*circular list below */
	if (error_track_list == NULL) {	/*no elements */
		error_track_list = new_elem;
		new_elem->next = error_track_list;
	} else {
		struct kbase_error_atom *walker = error_track_list;

		while (walker->next != error_track_list)
			walker = walker->next;

		new_elem->next = error_track_list;
		walker->next = new_elem;
	}
	return 0;
}

void midgard_set_error(int job_slot)
{
#ifdef CONFIG_MALI_ERROR_INJECT_RANDOM
	gpu_generate_error();
#else
	struct kbase_error_atom *walker, *auxiliar;

	if (error_track_list != NULL) {
		walker = error_track_list->next;
		auxiliar = error_track_list;
		do {
			if (walker->params.jc == hw_error_status.current_jc) {
				/* found a faulty atom matching with the
				 * current one
				 */
				hw_error_status.errors_mask =
						walker->params.errors_mask;
				hw_error_status.mmu_table_level =
						walker->params.mmu_table_level;
				hw_error_status.faulty_mmu_as =
						walker->params.faulty_mmu_as;
				hw_error_status.current_job_slot = job_slot;

				if (walker->next == walker) {
					/* only one element */
					kfree(error_track_list);
					error_track_list = NULL;
				} else {
					auxiliar->next = walker->next;
					if (walker == error_track_list)
						error_track_list = walker->next;

					kfree(walker);
				}
				break;
			}
			auxiliar = walker;
			walker = walker->next;
		} while (auxiliar->next != error_track_list);
	}
#endif				/* CONFIG_MALI_ERROR_INJECT_RANDOM */
}
