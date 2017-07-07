/*
 * Copyright 2003-2011 NetLogic Microsystems, Inc. (NetLogic). All rights
 * reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the NetLogic
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY NETLOGIC ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL NETLOGIC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/init.h>

#include <asm/time.h>
#include <asm/cpu-features.h>

#include <asm/netlogic/interrupt.h>
#include <asm/netlogic/common.h>
#include <asm/netlogic/haldefs.h>

#if defined(CONFIG_CPU_XLP)
#include <asm/netlogic/xlp-hal/iomap.h>
#include <asm/netlogic/xlp-hal/xlp.h>
#include <asm/netlogic/xlp-hal/sys.h>
#include <asm/netlogic/xlp-hal/pic.h>
#elif defined(CONFIG_CPU_XLR)
#include <asm/netlogic/xlr/iomap.h>
#include <asm/netlogic/xlr/pic.h>
#include <asm/netlogic/xlr/xlr.h>
#else
#error "Unknown CPU"
#endif

unsigned int get_c0_compare_int(void)
{
	return IRQ_TIMER;
}

static u64 nlm_get_pic_timer(struct clocksource *cs)
{
	uint64_t picbase = nlm_get_node(0)->picbase;

	return ~nlm_pic_read_timer(picbase, PIC_CLOCK_TIMER);
}

static u64 nlm_get_pic_timer32(struct clocksource *cs)
{
	uint64_t picbase = nlm_get_node(0)->picbase;

	return ~nlm_pic_read_timer32(picbase, PIC_CLOCK_TIMER);
}

static struct clocksource csrc_pic = {
	.name		= "PIC",
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

static void nlm_init_pic_timer(void)
{
	uint64_t picbase = nlm_get_node(0)->picbase;
	u32 picfreq;

	nlm_pic_set_timer(picbase, PIC_CLOCK_TIMER, ~0ULL, 0, 0);
	if (current_cpu_data.cputype == CPU_XLR) {
		csrc_pic.mask	= CLOCKSOURCE_MASK(32);
		csrc_pic.read	= nlm_get_pic_timer32;
	} else {
		csrc_pic.mask	= CLOCKSOURCE_MASK(64);
		csrc_pic.read	= nlm_get_pic_timer;
	}
	csrc_pic.rating = 1000;
	picfreq = pic_timer_freq();
	clocksource_register_hz(&csrc_pic, picfreq);
	pr_info("PIC clock source added, frequency %d\n", picfreq);
}

void __init plat_time_init(void)
{
	nlm_init_pic_timer();
	mips_hpt_frequency = nlm_get_cpu_frequency();
	if (current_cpu_type() == CPU_XLR)
		preset_lpj = mips_hpt_frequency / (3 * HZ);
	else
		preset_lpj = mips_hpt_frequency / (2 * HZ);
	pr_info("MIPS counter frequency [%ld]\n",
			(unsigned long)mips_hpt_frequency);
}
