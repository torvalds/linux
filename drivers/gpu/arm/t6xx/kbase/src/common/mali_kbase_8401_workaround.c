/*
 *
 * (C) COPYRIGHT 2011-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */



/**
 * @file mali_kbase_8401_workaround.c
 * Functions related to working around BASE_HW_ISSUE_8401
 */

#include <kbase/src/common/mali_kbase.h>
#include <kbase/src/common/mali_kbase_defs.h>
#include <kbase/src/common/mali_kbase_jm.h>
#include <kbase/src/common/mali_kbase_8401_workaround.h>

#define WORKAROUND_PAGE_OFFSET (2)
#define URT_POINTER_INDEX      (20)
#define RMU_POINTER_INDEX      (23)
#define RSD_POINTER_INDEX      (24)
#define TSD_POINTER_INDEX      (31)

static const u32 compute_job_32bit_header[] =
{
	/* Job Descriptor Header */

	/* Job Status */
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	/* Flags and Indices */
	/* job_type = compute shader job */
	0x00000008, 0x00000000,
	/* Pointer to next job */
	0x00000000,
	/* Reserved */
	0x00000000,
	/* Job Dimension Data */
	0x0000000f, 0x21040842,
	/* Task Split */
	0x08000000,
	/* Reserved */
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,

	/* Draw Call Descriptor - 32 bit (Must be aligned to a 64-byte boundry) */

	/* Flags */
	0x00000004,
	/* Primary Attribute Offset */
	0x00000000,
	/* Primitive Index Base Value */
	0x00000000,

	/* Pointer To Vertex Position Array (64-byte alignment) */
	0x00000000,
	/* Pointer To Uniform Remapping Table (8-byte alignment) */
	0,
	/* Pointer To Image Descriptor Pointer Table */
	0x00000000,
	/* Pointer To Sampler Array */
	0x00000000,
	/* Pointer To Register-Mapped Uniform Data Area (16-byte alignment) */
	0,
	/* Pointer To Renderer State Descriptor (64-byte alignment) */
	0,
	/* Pointer To Primary Attribute Buffer Array */
	0x00000000,
	/* Pointer To Primary Attribute Array */
	0x00000000,
	/* Pointer To Secondary Attribute Buffer Array */
	0x00000000,
	/* Pointer To Secondary Attribute Array */
	0x00000000,
	/* Pointer To Viewport Descriptor */
	0x00000000,
	/* Pointer To Occlusion Query Result */
	0x00000000,
	/* Pointer To Thread Storage (64 byte alignment) */
	0,
};


static const u32 compute_job_32bit_urt[] =
{
	/* Uniform Remapping Table Entry */
	0, 0,
};


static const u32 compute_job_32bit_rmu[] =
{
	/* Register Mapped Uniform Data Area (16 byte aligned), an array of 128-bit
	 * register values.
	 *
	 * NOTE: this is also used as the URT pointer, so the first 16-byte entry
	 * must be all zeros.
	 *
	 * For BASE_HW_ISSUE_8987, we place 16 RMUs here, because this should only
	 * be run concurrently with other GLES jobs (i.e. FS jobs from slot 0).
	 */
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,

	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,

	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,

	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000
};

static const u32 compute_job_32bit_rsd[] =
{
	/* Renderer State Descriptor */

	/* Shader program inital PC (low) */
	0x00000001,
	/* Shader program initial PC (high) */
	0x00000000,
	/* Image descriptor array sizes */
	0x00000000,
	/* Attribute array sizes */
	0x00000000,
	/* Uniform array size and Shader Flags */
	/* Flags set: R, D, SE, Reg Uniforms==16, FPM==OpenCL */
	0x42003800,
	/* Depth bias */
	0x00000000,
	/* Depth slope bias */
	0x00000000,
	/* Depth bias clamp */
	0x00000000,
	/* Multisample Write Mask and Flags */
	0x00000000,
	/* Stencil Write Masks and Alpha parameters */
	0x00000000,
	/* Stencil tests - forward facing */
	0x00000000,
	/* Stencel tests - back facing */
	0x00000000,
	/* Alpha Test Reference Value */
	0x00000000,
	/* Thread Balancing Information */
	0x00000000,
	/* Blend Parameters or Pointer (low) */
	0x00000000,
	/* Blend Parameters or Pointer (high) */
	0x00000000,
};

static const u32 compute_job_32bit_tsd[] =
{
	/* Thread Storage Descriptor */

	/* Thread Local Storage Sizes */
	0x00000000,
	/* Workgroup Local Memory Area Flags */
	0x0000001f,
	/* Pointer to Local Storage Area */
	0x00021000, 0x00000001,
	/* Pointer to Workgroup Local Storage Area */
	0x00000000, 0x00000000,
	/* Pointer to Shader Exception Handler */
	0x00000000, 0x00000000
};

static kbase_jd_atom dummy_job_atom[KBASE_8401_WORKAROUND_COMPUTEJOB_COUNT];

/**
 * Initialize the compute job sturcture.
 */

static void kbasep_8401_workaround_update_job_pointers(u32 *dummy_compute_job, int page_nr)
{
	u32 base_address = (page_nr+WORKAROUND_PAGE_OFFSET)*PAGE_SIZE;
	u8 *dummy_job = (u8*) dummy_compute_job;
	u8 *dummy_job_urt;
	u8 *dummy_job_rmu;
	u8 *dummy_job_rsd;
	u8 *dummy_job_tsd;

	OSK_ASSERT(dummy_compute_job);

	/* determin where each job section goes taking alignment restrictions into consideration */
	dummy_job_urt = (u8*) ((((uintptr_t)dummy_job + sizeof(compute_job_32bit_header))+7) & ~7);
	dummy_job_rmu = (u8*) ((((uintptr_t)dummy_job_urt + sizeof(compute_job_32bit_urt))+15) & ~15);
	dummy_job_rsd = (u8*) ((((uintptr_t)dummy_job_rmu + sizeof(compute_job_32bit_rmu))+63) & ~63);
	dummy_job_tsd = (u8*) ((((uintptr_t)dummy_job_rsd + sizeof(compute_job_32bit_rsd))+63) & ~63);

	/* Make sure the job fits within a single page */
	OSK_ASSERT(PAGE_SIZE > ((dummy_job_tsd+sizeof(compute_job_32bit_tsd)) - dummy_job));

	/* Copy the job sections to the allocated memory */
	memcpy(dummy_job, compute_job_32bit_header, sizeof(compute_job_32bit_header));
	memcpy(dummy_job_urt, compute_job_32bit_urt, sizeof(compute_job_32bit_urt));
	memcpy(dummy_job_rmu, compute_job_32bit_rmu, sizeof(compute_job_32bit_rmu));
	memcpy(dummy_job_rsd, compute_job_32bit_rsd, sizeof(compute_job_32bit_rsd));
	memcpy(dummy_job_tsd, compute_job_32bit_tsd, sizeof(compute_job_32bit_tsd));

	/* Update header pointers */
	*(dummy_compute_job + URT_POINTER_INDEX) = (dummy_job_urt - dummy_job) + base_address;
	*(dummy_compute_job + RMU_POINTER_INDEX) = (dummy_job_rmu - dummy_job) + base_address;
	*(dummy_compute_job + RSD_POINTER_INDEX) = (dummy_job_rsd - dummy_job) + base_address;
	*(dummy_compute_job + TSD_POINTER_INDEX) = (dummy_job_tsd - dummy_job) + base_address;
	/* Update URT pointer */
	*((u32*)dummy_job_urt+0) = (((dummy_job_rmu - dummy_job) + base_address) << 8) & 0xffffff00;
	*((u32*)dummy_job_urt+1) = (((dummy_job_rmu - dummy_job) + base_address) >> 24) & 0xff;
}

/**
 * Initialize the memory for 8401 workaround.
 */

mali_error kbasep_8401_workaround_init(kbase_device *kbdev)
{
	kbasep_js_device_data *js_devdata;
	kbase_context *workaround_kctx;
	int i;
	u16 as_present_mask;

	OSK_ASSERT(kbdev);
	OSK_ASSERT(kbdev->workaround_kctx == NULL);

	js_devdata = &kbdev->js_data;

	/* For this workaround we reserve one address space to allow us to
	 * submit a special job independent of other contexts */
	--(kbdev->nr_hw_address_spaces);

	if ( kbdev->nr_user_address_spaces == (kbdev->nr_hw_address_spaces + 1) )
	{
		/* Only update nr_user_address_spaces if it was unchanged - to ensure
		 * HW workarounds that have modified this will still work */
		--(kbdev->nr_user_address_spaces);
	}
	OSK_ASSERT( kbdev->nr_user_address_spaces <= kbdev->nr_hw_address_spaces );

	/* Recalculate the free address spaces bit-pattern */
	as_present_mask = (1U << kbdev->nr_hw_address_spaces) - 1;
	js_devdata->as_free &= as_present_mask;

	workaround_kctx = kbase_create_context(kbdev);
	if(!workaround_kctx)
	{
		return MALI_ERROR_FUNCTION_FAILED;
	}

	/* Allocate the pages required to contain the job */
	if (MALI_ERROR_NONE != kbase_mem_allocator_alloc(&workaround_kctx->osalloc, KBASE_8401_WORKAROUND_COMPUTEJOB_COUNT, kbdev->workaround_compute_job_pa, KBASE_REG_MUST_ZERO))
	{
		goto no_pages;
	}

	/* Get virtual address of mapped memory and write a compute job for each page */
	for(i = 0; i < KBASE_8401_WORKAROUND_COMPUTEJOB_COUNT; i++)
	{
		kbdev->workaround_compute_job_va[i] = osk_kmap(kbdev->workaround_compute_job_pa[i]);
		if(NULL == kbdev->workaround_compute_job_va[i])
		{
			goto page_free;
		}

		/* Generate the compute job data */
		kbasep_8401_workaround_update_job_pointers((u32*)kbdev->workaround_compute_job_va[i], i);
	}

	/* Insert pages to the gpu mmu. */
	kbase_mmu_insert_pages(workaround_kctx,
	                       /* vpfn = page number */
	                       (u64)WORKAROUND_PAGE_OFFSET,
	                       /* physical address */
	                       kbdev->workaround_compute_job_pa,
	                       /* number of pages */
			       KBASE_8401_WORKAROUND_COMPUTEJOB_COUNT,
	                       /* flags */
	                       KBASE_REG_GPU_RD|KBASE_REG_CPU_RD|KBASE_REG_CPU_WR|KBASE_REG_GPU_WR);

	kbdev->workaround_kctx = workaround_kctx;
	return MALI_ERROR_NONE;
page_free:
	while(i--)
	{
		osk_kunmap(kbdev->workaround_compute_job_pa[i], kbdev->workaround_compute_job_va[i]);
	}
	kbase_mem_allocator_free(&workaround_kctx->osalloc, KBASE_8401_WORKAROUND_COMPUTEJOB_COUNT, kbdev->workaround_compute_job_pa);
no_pages:
	kbase_destroy_context(workaround_kctx);

	return MALI_ERROR_FUNCTION_FAILED;
}

/**
 * Free up the memory used by 8401 workaround.
 **/

void kbasep_8401_workaround_term(kbase_device *kbdev)
{
	kbasep_js_device_data *js_devdata;
	int i;
	u16 restored_as;

	OSK_ASSERT(kbdev);
	OSK_ASSERT(kbdev->workaround_kctx);

	js_devdata = &kbdev->js_data;

	for(i = 0; i < KBASE_8401_WORKAROUND_COMPUTEJOB_COUNT; i++)
	{
		osk_kunmap(kbdev->workaround_compute_job_pa[i], kbdev->workaround_compute_job_va[i]);
	}

	kbase_mem_allocator_free(&kbdev->workaround_kctx->osalloc, KBASE_8401_WORKAROUND_COMPUTEJOB_COUNT, kbdev->workaround_compute_job_pa);

	kbase_destroy_context(kbdev->workaround_kctx);
	kbdev->workaround_kctx = NULL;

	/* Free up the workaround address space */
	kbdev->nr_hw_address_spaces++;

	if ( kbdev->nr_user_address_spaces == (kbdev->nr_hw_address_spaces - 1) )
	{
		/* Only update nr_user_address_spaces if it was unchanged - to ensure
		 * HW workarounds that have modified this will still work */
		++(kbdev->nr_user_address_spaces);
	}
	OSK_ASSERT( kbdev->nr_user_address_spaces <= kbdev->nr_hw_address_spaces );

	/* Recalculate the free address spaces bit-pattern */
	restored_as = (1U << kbdev->nr_hw_address_spaces);
	js_devdata->as_free |= restored_as;
}

/**
 * Submit the 8401 workaround job.
 *
 * Important for BASE_HW_ISSUE_8987: This job always uses 16 RMUs
 * - Therefore, on slot[1] it will always use the same number of RMUs as another
 * GLES job.
 * - On slot[2], no other job (GLES or otherwise) will be running on the
 * cores, by virtue of it being slot[2]. Therefore, any value of RMUs is
 * acceptable.
 */
void kbasep_8401_submit_dummy_job(kbase_device *kbdev, int js)
{
	u32 cfg;
	mali_addr64 jc;
	u32 pgd_high;	

	/* While this workaround is active we reserve the last address space just for submitting the dummy jobs */
	int as = kbdev->nr_hw_address_spaces;

	/* Don't issue compute jobs on job slot 0 */
	OSK_ASSERT(js != 0);
	OSK_ASSERT(js < KBASE_8401_WORKAROUND_COMPUTEJOB_COUNT);

	/* Job chain GPU address */
	jc = (js+WORKAROUND_PAGE_OFFSET)*PAGE_SIZE; /* GPU phys address (see kbase_mmu_insert_pages call in kbasep_8401_workaround_init*/

	/* Clear the job status words which may contain values from a previous job completion */
	memset(kbdev->workaround_compute_job_va[js], 0,  4*sizeof(u32));

	/* Get the affinity of the previous job */
	dummy_job_atom[js].affinity = ((u64)kbase_reg_read(kbdev, JOB_SLOT_REG(js, JSn_AFFINITY_LO), NULL)) |
	                              (((u64)kbase_reg_read(kbdev, JOB_SLOT_REG(js, JSn_AFFINITY_HI), NULL)) << 32);

	/* Don't submit a compute job if the affinity was previously zero (i.e. no jobs have run yet on this slot) */
	if(!dummy_job_atom[js].affinity)
	{
		return;
	}

	/* Ensure that our page tables are programmed into the MMU */
	kbase_reg_write(kbdev, MMU_AS_REG(as, ASn_TRANSTAB_LO),
	                       (kbdev->workaround_kctx->pgd & ASn_TRANSTAB_ADDR_SPACE_MASK) | ASn_TRANSTAB_READ_INNER
	                       | ASn_TRANSTAB_ADRMODE_TABLE, NULL);

	/* Need to use a conditional expression to avoid "right shift count >= width of type"
	 * error when using an if statement - although the size_of condition is evaluated at compile
	 * time the unused branch is not removed until after it is type-checked and the error
	 * produced.
	 */
	pgd_high = sizeof(kbdev->workaround_kctx->pgd) > 4 ? (kbdev->workaround_kctx->pgd >> 32) : 0;
	kbase_reg_write(kbdev, MMU_AS_REG(as, ASn_TRANSTAB_HI), pgd_high, NULL);

	kbase_reg_write(kbdev, MMU_AS_REG(as, ASn_MEMATTR_LO), ASn_MEMATTR_IMPL_DEF_CACHE_POLICY, NULL);
	kbase_reg_write(kbdev, MMU_AS_REG(as, ASn_MEMATTR_HI), ASn_MEMATTR_IMPL_DEF_CACHE_POLICY, NULL);
	kbase_reg_write(kbdev, MMU_AS_REG(as, ASn_COMMAND), ASn_COMMAND_UPDATE, NULL);

	kbase_reg_write(kbdev, JOB_SLOT_REG(js, JSn_HEAD_NEXT_LO), jc & 0xFFFFFFFF, NULL);
	kbase_reg_write(kbdev, JOB_SLOT_REG(js, JSn_HEAD_NEXT_HI), jc >> 32, NULL);

	kbase_reg_write(kbdev, JOB_SLOT_REG(js, JSn_AFFINITY_NEXT_LO), dummy_job_atom[js].affinity & 0xFFFFFFFF, NULL);
	kbase_reg_write(kbdev, JOB_SLOT_REG(js, JSn_AFFINITY_NEXT_HI), dummy_job_atom[js].affinity >> 32, NULL);

	/* start MMU, medium priority, cache clean/flush on end, clean/flush on start */
	cfg = as | JSn_CONFIG_END_FLUSH_CLEAN_INVALIDATE | JSn_CONFIG_START_MMU
	         | JSn_CONFIG_START_FLUSH_CLEAN_INVALIDATE | JSn_CONFIG_THREAD_PRI(8);
	kbase_reg_write(kbdev, JOB_SLOT_REG(js, JSn_CONFIG_NEXT), cfg, NULL);

	KBASE_TRACE_ADD_SLOT( kbdev, JM_SUBMIT, NULL, 0, jc, js );

	kbase_reg_write(kbdev, JOB_SLOT_REG(js, JSn_COMMAND_NEXT), JSn_COMMAND_START, NULL);
	/* Report that the job has been submitted */
	kbasep_jm_enqueue_submit_slot(&kbdev->jm_slots[js], &dummy_job_atom[js]);
}

/**
 * Check if the katom given is a dummy compute job.
 */
mali_bool kbasep_8401_is_workaround_job(kbase_jd_atom *katom)
{
	int i;

	/* Note: we don't check the first dummy_job_atom as slot 0 is never used for the workaround */
	for(i = 1; i < KBASE_8401_WORKAROUND_COMPUTEJOB_COUNT; i++)
	{
		if(katom == &dummy_job_atom[i])
		{
			/* This is a dummy job */
			return MALI_TRUE;
		}
	}

	/* This is a real job */
	return MALI_FALSE;
}

