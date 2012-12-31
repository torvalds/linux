/*
 *
 * (C) COPYRIGHT 2010 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * 
 */



/**
 * @file mali_kbase_js_affinity.c
 * Base kernel affinity manager APIs
 */

#include <kbase/src/common/mali_kbase.h>
#include "mali_kbase_js_affinity.h"

#if MALI_DEBUG && 0 /* disabled to avoid compilation warnings */

STATIC void debug_get_binary_string(const u64 n, char *buff, const int size)
{
	unsigned int i;
	for (i = 0; i < size; i++)
	{
		buff[i] = ((n >> i) & 1) ? '*' : '-';
	}
	buff[size] = '\0';
}

#define N_CORES 8
STATIC void debug_print_affinity_info(const kbase_device *kbdev, const kbase_jd_atom *katom, int js, u64 affinity)
{
	char buff[N_CORES +1];
	char buff2[N_CORES +1];
	base_jd_core_req core_req = katom->atom->core_req;
	u8 nr_nss_ctxs_running =  kbdev->js_data.runpool_irq.nr_nss_ctxs_running;
	u64 shader_present_bitmap = kbdev->shader_present_bitmap;

	debug_get_binary_string(shader_present_bitmap, buff, N_CORES);
	debug_get_binary_string(affinity, buff2, N_CORES);

	OSK_PRINT_INFO(OSK_BASE_JM, "Job: NSS COH FS  CS   T  CF   V  JS | NSS_ctx | GPU:12345678 | AFF:12345678");
	OSK_PRINT_INFO(OSK_BASE_JM, "      %s   %s   %s   %s   %s   %s   %s   %u |    %u    |     %s |     %s",
				   core_req & BASE_JD_REQ_NSS            ? "*" : "-",
				   core_req & BASE_JD_REQ_COHERENT_GROUP ? "*" : "-",
				   core_req & BASE_JD_REQ_FS             ? "*" : "-",
				   core_req & BASE_JD_REQ_CS             ? "*" : "-",
				   core_req & BASE_JD_REQ_T              ? "*" : "-",
				   core_req & BASE_JD_REQ_CF             ? "*" : "-",
				   core_req & BASE_JD_REQ_V              ? "*" : "-",
				   js, nr_nss_ctxs_running, buff, buff2);
}

#endif /* MALI_DEBUG */


/*
 * As long as it has been decided to have a deeper modification of
 * what job scheduler, power manager and affinity manager will
 * implement, this function is just an intermediate step that
 * assumes:
 * - all working cores will be powered on when this is called.
 * - largest current configuration is a T658 (2x4 cores).
 * - It has been decided not to have hardcoded values so the cores
 *   for SS and NSS jobs will be evently distributed.
 * - Odd combinations of core requirements have been filtered out
 *   and do not get to this function (e.g. CS+T+NSS is not
 *   supported here).
 * - This function is frequently called and can be optimized,
 *   (see notes in loops), but as the functionallity will soon
 *   be modified, optimization has not been addressed.
*/
void kbase_js_choose_affinity(u64 *affinity, kbase_device *kbdev, kbase_jd_atom *katom, int js)
{
	base_jd_core_req core_req = katom->core_req;
	kbasep_js_device_data *device_data = &kbdev->js_data;
	u64 shader_present_bitmap = kbdev->shader_present_bitmap;
	CSTD_UNUSED(js);

	OSK_ASSERT(0 != shader_present_bitmap);

	if (1 == kbdev->gpu_props.num_cores)
	{
		/* trivial case only one core, nothing to do */
		*affinity = shader_present_bitmap;
	}
	else if (0 == device_data->runpool_irq.nr_nss_ctxs_running)
	{
		/* SS state - all jobs are soft stopable */
		if (core_req & (BASE_JD_REQ_COHERENT_GROUP))
		{
			*affinity = kbdev->gpu_props.props.coherency_info.group[0].core_mask;
		}
		else
		{
			*affinity = shader_present_bitmap;
		}
	}
	else
	{
		/* NSS state - divide cores in two non-overlapping groups
		   for SS and NSS jobs */
		u64 ss_bitmap, nss_bitmap;
		int n_nss_cores = kbdev->gpu_props.num_cores >> 1;
		OSK_ASSERT(0 != n_nss_cores);

		/* compute the nss reserved cores bitmap */
		nss_bitmap = ~0;
		/* note: this can take a while, optimization desirable */
		while (n_nss_cores != osk_count_set_bits(nss_bitmap & shader_present_bitmap))
		{
			nss_bitmap = nss_bitmap << 1;
		}
		nss_bitmap &= shader_present_bitmap;
		
		/* now decide 4 different situations depending on the job being
		   SS or NSS and requiring coherent group or not */
		if (core_req & BASE_JD_REQ_NSS)
		{
			unsigned int num_core_groups = kbdev->gpu_props.num_core_groups;
			OSK_ASSERT(0 != num_core_groups);

			if ((core_req & BASE_JD_REQ_COHERENT_GROUP) && (1 != num_core_groups))
			{
				/* NSS job requiring coherency and coherency matters
				   because we got more than one core group */
				u64 group1_mask = kbdev->gpu_props.props.coherency_info.group[1].core_mask;
				*affinity = nss_bitmap & group1_mask;
			}
			else
			{
				/* NSS job not requiring coherency or coherency is
				   assured as we only have one core_group */
				*affinity = nss_bitmap;
			}
		}
		else
		{
			ss_bitmap = shader_present_bitmap ^ nss_bitmap;
		
			if (core_req & BASE_JD_REQ_COHERENT_GROUP)
			{
				/* SS job in NSS state and req coherent group */
				u64 group0_mask = kbdev->gpu_props.props.coherency_info.group[0].core_mask;
				u64 ss_coh_bitmap = ss_bitmap & group0_mask;
				*affinity = ss_coh_bitmap;
			}
			else
			{
				/* SS job in NSS state and does not req coherent group */
				*affinity = ss_bitmap;
			}
		}
	}

	OSK_ASSERT(*affinity != 0);
}
