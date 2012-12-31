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



#include <kbase/src/common/mali_kbase.h>

/**
 * @file mali_kbase_softjobs.c
 *
 * This file implements the logic behind software only jobs that are
 * executed within the driver rather than being handed over to the GPU.
 */

static base_jd_event_code kbase_dump_cpu_gpu_time(kbase_context *kctx, mali_addr64 jc)
{
	kbase_va_region *reg;
	osk_phy_addr addr;
	u64 pfn;
	u32 offset;
	char *page;
	osk_timeval tv;
	base_dump_cpu_gpu_counters data;
	u64 system_time;
	u64 cycle_counter;

	u32 hi1, hi2;

	OSK_MEMSET(&data, 0, sizeof(data));

	/* Read hi, lo, hi to ensure that overflow from lo to hi is handled correctly */
	do {
		hi1 = kbase_reg_read(kctx->kbdev, GPU_CONTROL_REG(CYCLE_COUNT_HI), NULL);
		cycle_counter = kbase_reg_read(kctx->kbdev, GPU_CONTROL_REG(CYCLE_COUNT_LO), NULL);
		hi2 = kbase_reg_read(kctx->kbdev, GPU_CONTROL_REG(CYCLE_COUNT_HI), NULL);
		cycle_counter |= (((u64)hi1) << 32);
	} while (hi1 != hi2);

	/* Read hi, lo, hi to ensure that overflow from lo to hi is handled correctly */
	do {
		hi1 = kbase_reg_read(kctx->kbdev, GPU_CONTROL_REG(TIMESTAMP_HI), NULL);
		system_time = kbase_reg_read(kctx->kbdev, GPU_CONTROL_REG(TIMESTAMP_LO), NULL);
		hi2 = kbase_reg_read(kctx->kbdev, GPU_CONTROL_REG(TIMESTAMP_HI), NULL);
		system_time |= (((u64)hi1) << 32);
	} while (hi1 != hi2);

	/* Record the CPU's idea of current time */
	osk_gettimeofday(&tv);

	data.sec = tv.tv_sec;
	data.usec = tv.tv_usec;
	data.system_time = system_time;
	data.cycle_counter = cycle_counter;

	pfn = jc >> 12;
	offset = jc & 0xFFF;

	if (offset > 0x1000-sizeof(data))
	{
		/* Wouldn't fit in the page */
		return BASE_JD_EVENT_JOB_CANCELLED;
	}

	reg = kbase_region_lookup(kctx, jc);
	if (!reg)
	{
		return BASE_JD_EVENT_JOB_CANCELLED;
	}

	if (! (reg->flags & KBASE_REG_GPU_RW) )
	{
		/* Region is not writable by GPU so we won't write to it either */
		return BASE_JD_EVENT_JOB_CANCELLED;
	}

	if (!reg->phy_pages)
	{
		return BASE_JD_EVENT_JOB_CANCELLED;
	}

	addr = reg->phy_pages[pfn - reg->start_pfn];
	if (!addr)
	{
		return BASE_JD_EVENT_JOB_CANCELLED;
	}

	page = osk_kmap(addr);
	if (!page)
	{
		return BASE_JD_EVENT_JOB_CANCELLED;
	}
	memcpy(page+offset, &data, sizeof(data));
	osk_kunmap(addr, page);

	return BASE_JD_EVENT_DONE;
}

void kbase_process_soft_job( kbase_context *kctx, kbase_jd_atom *katom )
{
	switch(katom->core_req) {
		case BASE_JD_REQ_SOFT_DUMP_CPU_GPU_TIME:
			katom->event.event_code = kbase_dump_cpu_gpu_time( kctx, katom->jc);
			break;
	}
}
