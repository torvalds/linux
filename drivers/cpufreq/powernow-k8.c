// SPDX-License-Identifier: GPL-2.0-only
/*
 *   (c) 2003-2012 Advanced Micro Devices, Inc.
 *
 *  Maintainer:
 *  Andreas Herrmann <herrmann.der.user@googlemail.com>
 *
 *  Based on the powernow-k7.c module written by Dave Jones.
 *  (C) 2003 Dave Jones on behalf of SuSE Labs
 *  (C) 2004 Dominik Brodowski <linux@brodo.de>
 *  (C) 2004 Pavel Machek <pavel@ucw.cz>
 *  Based upon datasheets & sample CPUs kindly provided by AMD.
 *
 *  Valuable input gratefully received from Dave Jones, Pavel Machek,
 *  Dominik Brodowski, Jacob Shin, and others.
 *  Originally developed by Paul Devriendt.
 *
 *  Processor information obtained from Chapter 9 (Power and Thermal
 *  Management) of the "BIOS and Kernel Developer's Guide (BKDG) for
 *  the AMD Athlon 64 and AMD Opteron Processors" and section "2.x
 *  Power Management" in BKDGs for newer AMD CPU families.
 *
 *  Tables for specific CPUs can be inferred from AMD's processor
 *  power and thermal data sheets, (e.g. 30417.pdf, 30430.pdf, 43375.pdf)
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/cpumask.h>
#include <linux/io.h>
#include <linux/delay.h>

#include <asm/msr.h>
#include <asm/cpu_device_id.h>

#include <linux/acpi.h>
#include <linux/mutex.h>
#include <acpi/processor.h>

#define VERSION "version 2.20.00"
#include "powernow-k8.h"

/* serialize freq changes  */
static DEFINE_MUTEX(fidvid_mutex);

static DEFINE_PER_CPU(struct powernow_k8_data *, powernow_data);

static struct cpufreq_driver cpufreq_amd64_driver;

/* Return a frequency in MHz, given an input fid */
static u32 find_freq_from_fid(u32 fid)
{
	return 800 + (fid * 100);
}

/* Return a frequency in KHz, given an input fid */
static u32 find_khz_freq_from_fid(u32 fid)
{
	return 1000 * find_freq_from_fid(fid);
}

/* Return the vco fid for an input fid
 *
 * Each "low" fid has corresponding "high" fid, and you can get to "low" fids
 * only from corresponding high fids. This returns "high" fid corresponding to
 * "low" one.
 */
static u32 convert_fid_to_vco_fid(u32 fid)
{
	if (fid < HI_FID_TABLE_BOTTOM)
		return 8 + (2 * fid);
	else
		return fid;
}

/*
 * Return 1 if the pending bit is set. Unless we just instructed the processor
 * to transition to a new state, seeing this bit set is really bad news.
 */
static int pending_bit_stuck(void)
{
	u32 lo, hi __always_unused;

	rdmsr(MSR_FIDVID_STATUS, lo, hi);
	return lo & MSR_S_LO_CHANGE_PENDING ? 1 : 0;
}

/*
 * Update the global current fid / vid values from the status msr.
 * Returns 1 on error.
 */
static int query_current_values_with_pending_wait(struct powernow_k8_data *data)
{
	u32 lo, hi;
	u32 i = 0;

	do {
		if (i++ > 10000) {
			pr_debug("detected change pending stuck\n");
			return 1;
		}
		rdmsr(MSR_FIDVID_STATUS, lo, hi);
	} while (lo & MSR_S_LO_CHANGE_PENDING);

	data->currvid = hi & MSR_S_HI_CURRENT_VID;
	data->currfid = lo & MSR_S_LO_CURRENT_FID;

	return 0;
}

/* the isochronous relief time */
static void count_off_irt(struct powernow_k8_data *data)
{
	udelay((1 << data->irt) * 10);
}

/* the voltage stabilization time */
static void count_off_vst(struct powernow_k8_data *data)
{
	udelay(data->vstable * VST_UNITS_20US);
}

/* need to init the control msr to a safe value (for each cpu) */
static void fidvid_msr_init(void)
{
	u32 lo, hi;
	u8 fid, vid;

	rdmsr(MSR_FIDVID_STATUS, lo, hi);
	vid = hi & MSR_S_HI_CURRENT_VID;
	fid = lo & MSR_S_LO_CURRENT_FID;
	lo = fid | (vid << MSR_C_LO_VID_SHIFT);
	hi = MSR_C_HI_STP_GNT_BENIGN;
	pr_debug("cpu%d, init lo 0x%x, hi 0x%x\n", smp_processor_id(), lo, hi);
	wrmsr(MSR_FIDVID_CTL, lo, hi);
}

/* write the new fid value along with the other control fields to the msr */
static int write_new_fid(struct powernow_k8_data *data, u32 fid)
{
	u32 lo;
	u32 savevid = data->currvid;
	u32 i = 0;

	if ((fid & INVALID_FID_MASK) || (data->currvid & INVALID_VID_MASK)) {
		pr_err("internal error - overflow on fid write\n");
		return 1;
	}

	lo = fid;
	lo |= (data->currvid << MSR_C_LO_VID_SHIFT);
	lo |= MSR_C_LO_INIT_FID_VID;

	pr_debug("writing fid 0x%x, lo 0x%x, hi 0x%x\n",
		fid, lo, data->plllock * PLL_LOCK_CONVERSION);

	do {
		wrmsr(MSR_FIDVID_CTL, lo, data->plllock * PLL_LOCK_CONVERSION);
		if (i++ > 100) {
			pr_err("Hardware error - pending bit very stuck - no further pstate changes possible\n");
			return 1;
		}
	} while (query_current_values_with_pending_wait(data));

	count_off_irt(data);

	if (savevid != data->currvid) {
		pr_err("vid change on fid trans, old 0x%x, new 0x%x\n",
		       savevid, data->currvid);
		return 1;
	}

	if (fid != data->currfid) {
		pr_err("fid trans failed, fid 0x%x, curr 0x%x\n", fid,
			data->currfid);
		return 1;
	}

	return 0;
}

/* Write a new vid to the hardware */
static int write_new_vid(struct powernow_k8_data *data, u32 vid)
{
	u32 lo;
	u32 savefid = data->currfid;
	int i = 0;

	if ((data->currfid & INVALID_FID_MASK) || (vid & INVALID_VID_MASK)) {
		pr_err("internal error - overflow on vid write\n");
		return 1;
	}

	lo = data->currfid;
	lo |= (vid << MSR_C_LO_VID_SHIFT);
	lo |= MSR_C_LO_INIT_FID_VID;

	pr_debug("writing vid 0x%x, lo 0x%x, hi 0x%x\n",
		vid, lo, STOP_GRANT_5NS);

	do {
		wrmsr(MSR_FIDVID_CTL, lo, STOP_GRANT_5NS);
		if (i++ > 100) {
			pr_err("internal error - pending bit very stuck - no further pstate changes possible\n");
			return 1;
		}
	} while (query_current_values_with_pending_wait(data));

	if (savefid != data->currfid) {
		pr_err("fid changed on vid trans, old 0x%x new 0x%x\n",
			savefid, data->currfid);
		return 1;
	}

	if (vid != data->currvid) {
		pr_err("vid trans failed, vid 0x%x, curr 0x%x\n",
				vid, data->currvid);
		return 1;
	}

	return 0;
}

/*
 * Reduce the vid by the max of step or reqvid.
 * Decreasing vid codes represent increasing voltages:
 * vid of 0 is 1.550V, vid of 0x1e is 0.800V, vid of VID_OFF is off.
 */
static int decrease_vid_code_by_step(struct powernow_k8_data *data,
		u32 reqvid, u32 step)
{
	if ((data->currvid - reqvid) > step)
		reqvid = data->currvid - step;

	if (write_new_vid(data, reqvid))
		return 1;

	count_off_vst(data);

	return 0;
}

/* Change Opteron/Athlon64 fid and vid, by the 3 phases. */
static int transition_fid_vid(struct powernow_k8_data *data,
		u32 reqfid, u32 reqvid)
{
	if (core_voltage_pre_transition(data, reqvid, reqfid))
		return 1;

	if (core_frequency_transition(data, reqfid))
		return 1;

	if (core_voltage_post_transition(data, reqvid))
		return 1;

	if (query_current_values_with_pending_wait(data))
		return 1;

	if ((reqfid != data->currfid) || (reqvid != data->currvid)) {
		pr_err("failed (cpu%d): req 0x%x 0x%x, curr 0x%x 0x%x\n",
				smp_processor_id(),
				reqfid, reqvid, data->currfid, data->currvid);
		return 1;
	}

	pr_debug("transitioned (cpu%d): new fid 0x%x, vid 0x%x\n",
		smp_processor_id(), data->currfid, data->currvid);

	return 0;
}

/* Phase 1 - core voltage transition ... setup voltage */
static int core_voltage_pre_transition(struct powernow_k8_data *data,
		u32 reqvid, u32 reqfid)
{
	u32 rvosteps = data->rvo;
	u32 savefid = data->currfid;
	u32 maxvid, lo __always_unused, rvomult = 1;

	pr_debug("ph1 (cpu%d): start, currfid 0x%x, currvid 0x%x, reqvid 0x%x, rvo 0x%x\n",
		smp_processor_id(),
		data->currfid, data->currvid, reqvid, data->rvo);

	if ((savefid < LO_FID_TABLE_TOP) && (reqfid < LO_FID_TABLE_TOP))
		rvomult = 2;
	rvosteps *= rvomult;
	rdmsr(MSR_FIDVID_STATUS, lo, maxvid);
	maxvid = 0x1f & (maxvid >> 16);
	pr_debug("ph1 maxvid=0x%x\n", maxvid);
	if (reqvid < maxvid) /* lower numbers are higher voltages */
		reqvid = maxvid;

	while (data->currvid > reqvid) {
		pr_debug("ph1: curr 0x%x, req vid 0x%x\n",
			data->currvid, reqvid);
		if (decrease_vid_code_by_step(data, reqvid, data->vidmvs))
			return 1;
	}

	while ((rvosteps > 0) &&
			((rvomult * data->rvo + data->currvid) > reqvid)) {
		if (data->currvid == maxvid) {
			rvosteps = 0;
		} else {
			pr_debug("ph1: changing vid for rvo, req 0x%x\n",
				data->currvid - 1);
			if (decrease_vid_code_by_step(data, data->currvid-1, 1))
				return 1;
			rvosteps--;
		}
	}

	if (query_current_values_with_pending_wait(data))
		return 1;

	if (savefid != data->currfid) {
		pr_err("ph1 err, currfid changed 0x%x\n", data->currfid);
		return 1;
	}

	pr_debug("ph1 complete, currfid 0x%x, currvid 0x%x\n",
		data->currfid, data->currvid);

	return 0;
}

/* Phase 2 - core frequency transition */
static int core_frequency_transition(struct powernow_k8_data *data, u32 reqfid)
{
	u32 vcoreqfid, vcocurrfid, vcofiddiff;
	u32 fid_interval, savevid = data->currvid;

	if (data->currfid == reqfid) {
		pr_err("ph2 null fid transition 0x%x\n", data->currfid);
		return 0;
	}

	pr_debug("ph2 (cpu%d): starting, currfid 0x%x, currvid 0x%x, reqfid 0x%x\n",
		smp_processor_id(),
		data->currfid, data->currvid, reqfid);

	vcoreqfid = convert_fid_to_vco_fid(reqfid);
	vcocurrfid = convert_fid_to_vco_fid(data->currfid);
	vcofiddiff = vcocurrfid > vcoreqfid ? vcocurrfid - vcoreqfid
	    : vcoreqfid - vcocurrfid;

	if ((reqfid <= LO_FID_TABLE_TOP) && (data->currfid <= LO_FID_TABLE_TOP))
		vcofiddiff = 0;

	while (vcofiddiff > 2) {
		(data->currfid & 1) ? (fid_interval = 1) : (fid_interval = 2);

		if (reqfid > data->currfid) {
			if (data->currfid > LO_FID_TABLE_TOP) {
				if (write_new_fid(data,
						data->currfid + fid_interval))
					return 1;
			} else {
				if (write_new_fid
				    (data,
				     2 + convert_fid_to_vco_fid(data->currfid)))
					return 1;
			}
		} else {
			if (write_new_fid(data, data->currfid - fid_interval))
				return 1;
		}

		vcocurrfid = convert_fid_to_vco_fid(data->currfid);
		vcofiddiff = vcocurrfid > vcoreqfid ? vcocurrfid - vcoreqfid
		    : vcoreqfid - vcocurrfid;
	}

	if (write_new_fid(data, reqfid))
		return 1;

	if (query_current_values_with_pending_wait(data))
		return 1;

	if (data->currfid != reqfid) {
		pr_err("ph2: mismatch, failed fid transition, curr 0x%x, req 0x%x\n",
			data->currfid, reqfid);
		return 1;
	}

	if (savevid != data->currvid) {
		pr_err("ph2: vid changed, save 0x%x, curr 0x%x\n",
			savevid, data->currvid);
		return 1;
	}

	pr_debug("ph2 complete, currfid 0x%x, currvid 0x%x\n",
		data->currfid, data->currvid);

	return 0;
}

/* Phase 3 - core voltage transition flow ... jump to the final vid. */
static int core_voltage_post_transition(struct powernow_k8_data *data,
		u32 reqvid)
{
	u32 savefid = data->currfid;
	u32 savereqvid = reqvid;

	pr_debug("ph3 (cpu%d): starting, currfid 0x%x, currvid 0x%x\n",
		smp_processor_id(),
		data->currfid, data->currvid);

	if (reqvid != data->currvid) {
		if (write_new_vid(data, reqvid))
			return 1;

		if (savefid != data->currfid) {
			pr_err("ph3: bad fid change, save 0x%x, curr 0x%x\n",
				savefid, data->currfid);
			return 1;
		}

		if (data->currvid != reqvid) {
			pr_err("ph3: failed vid transition\n, req 0x%x, curr 0x%x",
				reqvid, data->currvid);
			return 1;
		}
	}

	if (query_current_values_with_pending_wait(data))
		return 1;

	if (savereqvid != data->currvid) {
		pr_debug("ph3 failed, currvid 0x%x\n", data->currvid);
		return 1;
	}

	if (savefid != data->currfid) {
		pr_debug("ph3 failed, currfid changed 0x%x\n",
			data->currfid);
		return 1;
	}

	pr_debug("ph3 complete, currfid 0x%x, currvid 0x%x\n",
		data->currfid, data->currvid);

	return 0;
}

static const struct x86_cpu_id powernow_k8_ids[] = {
	/* IO based frequency switching */
	X86_MATCH_VENDOR_FAM(AMD, 0xf, NULL),
	{}
};
MODULE_DEVICE_TABLE(x86cpu, powernow_k8_ids);

static void check_supported_cpu(void *_rc)
{
	u32 eax, ebx, ecx, edx;
	int *rc = _rc;

	*rc = -ENODEV;

	eax = cpuid_eax(CPUID_PROCESSOR_SIGNATURE);

	if ((eax & CPUID_XFAM) == CPUID_XFAM_K8) {
		if (((eax & CPUID_USE_XFAM_XMOD) != CPUID_USE_XFAM_XMOD) ||
		    ((eax & CPUID_XMOD) > CPUID_XMOD_REV_MASK)) {
			pr_info("Processor cpuid %x not supported\n", eax);
			return;
		}

		eax = cpuid_eax(CPUID_GET_MAX_CAPABILITIES);
		if (eax < CPUID_FREQ_VOLT_CAPABILITIES) {
			pr_info("No frequency change capabilities detected\n");
			return;
		}

		cpuid(CPUID_FREQ_VOLT_CAPABILITIES, &eax, &ebx, &ecx, &edx);
		if ((edx & P_STATE_TRANSITION_CAPABLE)
			!= P_STATE_TRANSITION_CAPABLE) {
			pr_info_once("Power state transitions not supported\n");
			return;
		}
		*rc = 0;
	}
}

static int check_pst_table(struct powernow_k8_data *data, struct pst_s *pst,
		u8 maxvid)
{
	unsigned int j;
	u8 lastfid = 0xff;

	for (j = 0; j < data->numps; j++) {
		if (pst[j].vid > LEAST_VID) {
			pr_err(FW_BUG "vid %d invalid : 0x%x\n", j,
				pst[j].vid);
			return -EINVAL;
		}
		if (pst[j].vid < data->rvo) {
			/* vid + rvo >= 0 */
			pr_err(FW_BUG "0 vid exceeded with pstate %d\n", j);
			return -ENODEV;
		}
		if (pst[j].vid < maxvid + data->rvo) {
			/* vid + rvo >= maxvid */
			pr_err(FW_BUG "maxvid exceeded with pstate %d\n", j);
			return -ENODEV;
		}
		if (pst[j].fid > MAX_FID) {
			pr_err(FW_BUG "maxfid exceeded with pstate %d\n", j);
			return -ENODEV;
		}
		if (j && (pst[j].fid < HI_FID_TABLE_BOTTOM)) {
			/* Only first fid is allowed to be in "low" range */
			pr_err(FW_BUG "two low fids - %d : 0x%x\n", j,
				pst[j].fid);
			return -EINVAL;
		}
		if (pst[j].fid < lastfid)
			lastfid = pst[j].fid;
	}
	if (lastfid & 1) {
		pr_err(FW_BUG "lastfid invalid\n");
		return -EINVAL;
	}
	if (lastfid > LO_FID_TABLE_TOP)
		pr_info(FW_BUG "first fid not from lo freq table\n");

	return 0;
}

static void invalidate_entry(struct cpufreq_frequency_table *powernow_table,
		unsigned int entry)
{
	powernow_table[entry].frequency = CPUFREQ_ENTRY_INVALID;
}

static void print_basics(struct powernow_k8_data *data)
{
	int j;
	for (j = 0; j < data->numps; j++) {
		if (data->powernow_table[j].frequency !=
				CPUFREQ_ENTRY_INVALID) {
			pr_info("fid 0x%x (%d MHz), vid 0x%x\n",
				data->powernow_table[j].driver_data & 0xff,
				data->powernow_table[j].frequency/1000,
				data->powernow_table[j].driver_data >> 8);
		}
	}
	if (data->batps)
		pr_info("Only %d pstates on battery\n", data->batps);
}

static int fill_powernow_table(struct powernow_k8_data *data,
		struct pst_s *pst, u8 maxvid)
{
	struct cpufreq_frequency_table *powernow_table;
	unsigned int j;

	if (data->batps) {
		/* use ACPI support to get full speed on mains power */
		pr_warn("Only %d pstates usable (use ACPI driver for full range\n",
			data->batps);
		data->numps = data->batps;
	}

	for (j = 1; j < data->numps; j++) {
		if (pst[j-1].fid >= pst[j].fid) {
			pr_err("PST out of sequence\n");
			return -EINVAL;
		}
	}

	if (data->numps < 2) {
		pr_err("no p states to transition\n");
		return -ENODEV;
	}

	if (check_pst_table(data, pst, maxvid))
		return -EINVAL;

	powernow_table = kzalloc((sizeof(*powernow_table)
		* (data->numps + 1)), GFP_KERNEL);
	if (!powernow_table)
		return -ENOMEM;

	for (j = 0; j < data->numps; j++) {
		int freq;
		powernow_table[j].driver_data = pst[j].fid; /* lower 8 bits */
		powernow_table[j].driver_data |= (pst[j].vid << 8); /* upper 8 bits */
		freq = find_khz_freq_from_fid(pst[j].fid);
		powernow_table[j].frequency = freq;
	}
	powernow_table[data->numps].frequency = CPUFREQ_TABLE_END;
	powernow_table[data->numps].driver_data = 0;

	if (query_current_values_with_pending_wait(data)) {
		kfree(powernow_table);
		return -EIO;
	}

	pr_debug("cfid 0x%x, cvid 0x%x\n", data->currfid, data->currvid);
	data->powernow_table = powernow_table;
	if (cpumask_first(topology_core_cpumask(data->cpu)) == data->cpu)
		print_basics(data);

	for (j = 0; j < data->numps; j++)
		if ((pst[j].fid == data->currfid) &&
		    (pst[j].vid == data->currvid))
			return 0;

	pr_debug("currfid/vid do not match PST, ignoring\n");
	return 0;
}

/* Find and validate the PSB/PST table in BIOS. */
static int find_psb_table(struct powernow_k8_data *data)
{
	struct psb_s *psb;
	unsigned int i;
	u32 mvs;
	u8 maxvid;
	u32 cpst = 0;
	u32 thiscpuid;

	for (i = 0xc0000; i < 0xffff0; i += 0x10) {
		/* Scan BIOS looking for the signature. */
		/* It can not be at ffff0 - it is too big. */

		psb = phys_to_virt(i);
		if (memcmp(psb, PSB_ID_STRING, PSB_ID_STRING_LEN) != 0)
			continue;

		pr_debug("found PSB header at 0x%p\n", psb);

		pr_debug("table vers: 0x%x\n", psb->tableversion);
		if (psb->tableversion != PSB_VERSION_1_4) {
			pr_err(FW_BUG "PSB table is not v1.4\n");
			return -ENODEV;
		}

		pr_debug("flags: 0x%x\n", psb->flags1);
		if (psb->flags1) {
			pr_err(FW_BUG "unknown flags\n");
			return -ENODEV;
		}

		data->vstable = psb->vstable;
		pr_debug("voltage stabilization time: %d(*20us)\n",
				data->vstable);

		pr_debug("flags2: 0x%x\n", psb->flags2);
		data->rvo = psb->flags2 & 3;
		data->irt = ((psb->flags2) >> 2) & 3;
		mvs = ((psb->flags2) >> 4) & 3;
		data->vidmvs = 1 << mvs;
		data->batps = ((psb->flags2) >> 6) & 3;

		pr_debug("ramp voltage offset: %d\n", data->rvo);
		pr_debug("isochronous relief time: %d\n", data->irt);
		pr_debug("maximum voltage step: %d - 0x%x\n", mvs, data->vidmvs);

		pr_debug("numpst: 0x%x\n", psb->num_tables);
		cpst = psb->num_tables;
		if ((psb->cpuid == 0x00000fc0) ||
		    (psb->cpuid == 0x00000fe0)) {
			thiscpuid = cpuid_eax(CPUID_PROCESSOR_SIGNATURE);
			if ((thiscpuid == 0x00000fc0) ||
			    (thiscpuid == 0x00000fe0))
				cpst = 1;
		}
		if (cpst != 1) {
			pr_err(FW_BUG "numpst must be 1\n");
			return -ENODEV;
		}

		data->plllock = psb->plllocktime;
		pr_debug("plllocktime: 0x%x (units 1us)\n", psb->plllocktime);
		pr_debug("maxfid: 0x%x\n", psb->maxfid);
		pr_debug("maxvid: 0x%x\n", psb->maxvid);
		maxvid = psb->maxvid;

		data->numps = psb->numps;
		pr_debug("numpstates: 0x%x\n", data->numps);
		return fill_powernow_table(data,
				(struct pst_s *)(psb+1), maxvid);
	}
	/*
	 * If you see this message, complain to BIOS manufacturer. If
	 * he tells you "we do not support Linux" or some similar
	 * nonsense, remember that Windows 2000 uses the same legacy
	 * mechanism that the old Linux PSB driver uses. Tell them it
	 * is broken with Windows 2000.
	 *
	 * The reference to the AMD documentation is chapter 9 in the
	 * BIOS and Kernel Developer's Guide, which is available on
	 * www.amd.com
	 */
	pr_err(FW_BUG "No PSB or ACPI _PSS objects\n");
	pr_err("Make sure that your BIOS is up to date and Cool'N'Quiet support is enabled in BIOS setup\n");
	return -ENODEV;
}

static void powernow_k8_acpi_pst_values(struct powernow_k8_data *data,
		unsigned int index)
{
	u64 control;

	if (!data->acpi_data.state_count)
		return;

	control = data->acpi_data.states[index].control;
	data->irt = (control >> IRT_SHIFT) & IRT_MASK;
	data->rvo = (control >> RVO_SHIFT) & RVO_MASK;
	data->exttype = (control >> EXT_TYPE_SHIFT) & EXT_TYPE_MASK;
	data->plllock = (control >> PLL_L_SHIFT) & PLL_L_MASK;
	data->vidmvs = 1 << ((control >> MVS_SHIFT) & MVS_MASK);
	data->vstable = (control >> VST_SHIFT) & VST_MASK;
}

static int powernow_k8_cpu_init_acpi(struct powernow_k8_data *data)
{
	struct cpufreq_frequency_table *powernow_table;
	int ret_val = -ENODEV;
	u64 control, status;

	if (acpi_processor_register_performance(&data->acpi_data, data->cpu)) {
		pr_debug("register performance failed: bad ACPI data\n");
		return -EIO;
	}

	/* verify the data contained in the ACPI structures */
	if (data->acpi_data.state_count <= 1) {
		pr_debug("No ACPI P-States\n");
		goto err_out;
	}

	control = data->acpi_data.control_register.space_id;
	status = data->acpi_data.status_register.space_id;

	if ((control != ACPI_ADR_SPACE_FIXED_HARDWARE) ||
	    (status != ACPI_ADR_SPACE_FIXED_HARDWARE)) {
		pr_debug("Invalid control/status registers (%llx - %llx)\n",
			control, status);
		goto err_out;
	}

	/* fill in data->powernow_table */
	powernow_table = kzalloc((sizeof(*powernow_table)
		* (data->acpi_data.state_count + 1)), GFP_KERNEL);
	if (!powernow_table)
		goto err_out;

	/* fill in data */
	data->numps = data->acpi_data.state_count;
	powernow_k8_acpi_pst_values(data, 0);

	ret_val = fill_powernow_table_fidvid(data, powernow_table);
	if (ret_val)
		goto err_out_mem;

	powernow_table[data->acpi_data.state_count].frequency =
		CPUFREQ_TABLE_END;
	data->powernow_table = powernow_table;

	if (cpumask_first(topology_core_cpumask(data->cpu)) == data->cpu)
		print_basics(data);

	/* notify BIOS that we exist */
	acpi_processor_notify_smm(THIS_MODULE);

	if (!zalloc_cpumask_var(&data->acpi_data.shared_cpu_map, GFP_KERNEL)) {
		pr_err("unable to alloc powernow_k8_data cpumask\n");
		ret_val = -ENOMEM;
		goto err_out_mem;
	}

	return 0;

err_out_mem:
	kfree(powernow_table);

err_out:
	acpi_processor_unregister_performance(data->cpu);

	/* data->acpi_data.state_count informs us at ->exit()
	 * whether ACPI was used */
	data->acpi_data.state_count = 0;

	return ret_val;
}

static int fill_powernow_table_fidvid(struct powernow_k8_data *data,
		struct cpufreq_frequency_table *powernow_table)
{
	int i;

	for (i = 0; i < data->acpi_data.state_count; i++) {
		u32 fid;
		u32 vid;
		u32 freq, index;
		u64 status, control;

		if (data->exttype) {
			status =  data->acpi_data.states[i].status;
			fid = status & EXT_FID_MASK;
			vid = (status >> VID_SHIFT) & EXT_VID_MASK;
		} else {
			control =  data->acpi_data.states[i].control;
			fid = control & FID_MASK;
			vid = (control >> VID_SHIFT) & VID_MASK;
		}

		pr_debug("   %d : fid 0x%x, vid 0x%x\n", i, fid, vid);

		index = fid | (vid<<8);
		powernow_table[i].driver_data = index;

		freq = find_khz_freq_from_fid(fid);
		powernow_table[i].frequency = freq;

		/* verify frequency is OK */
		if ((freq > (MAX_FREQ * 1000)) || (freq < (MIN_FREQ * 1000))) {
			pr_debug("invalid freq %u kHz, ignoring\n", freq);
			invalidate_entry(powernow_table, i);
			continue;
		}

		/* verify voltage is OK -
		 * BIOSs are using "off" to indicate invalid */
		if (vid == VID_OFF) {
			pr_debug("invalid vid %u, ignoring\n", vid);
			invalidate_entry(powernow_table, i);
			continue;
		}

		if (freq != (data->acpi_data.states[i].core_frequency * 1000)) {
			pr_info("invalid freq entries %u kHz vs. %u kHz\n",
				freq, (unsigned int)
				(data->acpi_data.states[i].core_frequency
				 * 1000));
			invalidate_entry(powernow_table, i);
			continue;
		}
	}
	return 0;
}

static void powernow_k8_cpu_exit_acpi(struct powernow_k8_data *data)
{
	if (data->acpi_data.state_count)
		acpi_processor_unregister_performance(data->cpu);
	free_cpumask_var(data->acpi_data.shared_cpu_map);
}

static int get_transition_latency(struct powernow_k8_data *data)
{
	int max_latency = 0;
	int i;
	for (i = 0; i < data->acpi_data.state_count; i++) {
		int cur_latency = data->acpi_data.states[i].transition_latency
			+ data->acpi_data.states[i].bus_master_latency;
		if (cur_latency > max_latency)
			max_latency = cur_latency;
	}
	if (max_latency == 0) {
		pr_err(FW_WARN "Invalid zero transition latency\n");
		max_latency = 1;
	}
	/* value in usecs, needs to be in nanoseconds */
	return 1000 * max_latency;
}

/* Take a frequency, and issue the fid/vid transition command */
static int transition_frequency_fidvid(struct powernow_k8_data *data,
		unsigned int index,
		struct cpufreq_policy *policy)
{
	u32 fid = 0;
	u32 vid = 0;
	int res;
	struct cpufreq_freqs freqs;

	pr_debug("cpu %d transition to index %u\n", smp_processor_id(), index);

	/* fid/vid correctness check for k8 */
	/* fid are the lower 8 bits of the index we stored into
	 * the cpufreq frequency table in find_psb_table, vid
	 * are the upper 8 bits.
	 */
	fid = data->powernow_table[index].driver_data & 0xFF;
	vid = (data->powernow_table[index].driver_data & 0xFF00) >> 8;

	pr_debug("table matched fid 0x%x, giving vid 0x%x\n", fid, vid);

	if (query_current_values_with_pending_wait(data))
		return 1;

	if ((data->currvid == vid) && (data->currfid == fid)) {
		pr_debug("target matches current values (fid 0x%x, vid 0x%x)\n",
			fid, vid);
		return 0;
	}

	pr_debug("cpu %d, changing to fid 0x%x, vid 0x%x\n",
		smp_processor_id(), fid, vid);
	freqs.old = find_khz_freq_from_fid(data->currfid);
	freqs.new = find_khz_freq_from_fid(fid);

	cpufreq_freq_transition_begin(policy, &freqs);
	res = transition_fid_vid(data, fid, vid);
	cpufreq_freq_transition_end(policy, &freqs, res);

	return res;
}

struct powernowk8_target_arg {
	struct cpufreq_policy		*pol;
	unsigned			newstate;
};

static long powernowk8_target_fn(void *arg)
{
	struct powernowk8_target_arg *pta = arg;
	struct cpufreq_policy *pol = pta->pol;
	unsigned newstate = pta->newstate;
	struct powernow_k8_data *data = per_cpu(powernow_data, pol->cpu);
	u32 checkfid;
	u32 checkvid;
	int ret;

	if (!data)
		return -EINVAL;

	checkfid = data->currfid;
	checkvid = data->currvid;

	if (pending_bit_stuck()) {
		pr_err("failing targ, change pending bit set\n");
		return -EIO;
	}

	pr_debug("targ: cpu %d, %d kHz, min %d, max %d\n",
		pol->cpu, data->powernow_table[newstate].frequency, pol->min,
		pol->max);

	if (query_current_values_with_pending_wait(data))
		return -EIO;

	pr_debug("targ: curr fid 0x%x, vid 0x%x\n",
		data->currfid, data->currvid);

	if ((checkvid != data->currvid) ||
	    (checkfid != data->currfid)) {
		pr_info("error - out of sync, fix 0x%x 0x%x, vid 0x%x 0x%x\n",
		       checkfid, data->currfid,
		       checkvid, data->currvid);
	}

	mutex_lock(&fidvid_mutex);

	powernow_k8_acpi_pst_values(data, newstate);

	ret = transition_frequency_fidvid(data, newstate, pol);

	if (ret) {
		pr_err("transition frequency failed\n");
		mutex_unlock(&fidvid_mutex);
		return 1;
	}
	mutex_unlock(&fidvid_mutex);

	pol->cur = find_khz_freq_from_fid(data->currfid);

	return 0;
}

/* Driver entry point to switch to the target frequency */
static int powernowk8_target(struct cpufreq_policy *pol, unsigned index)
{
	struct powernowk8_target_arg pta = { .pol = pol, .newstate = index };

	return work_on_cpu(pol->cpu, powernowk8_target_fn, &pta);
}

struct init_on_cpu {
	struct powernow_k8_data *data;
	int rc;
};

static void powernowk8_cpu_init_on_cpu(void *_init_on_cpu)
{
	struct init_on_cpu *init_on_cpu = _init_on_cpu;

	if (pending_bit_stuck()) {
		pr_err("failing init, change pending bit set\n");
		init_on_cpu->rc = -ENODEV;
		return;
	}

	if (query_current_values_with_pending_wait(init_on_cpu->data)) {
		init_on_cpu->rc = -ENODEV;
		return;
	}

	fidvid_msr_init();

	init_on_cpu->rc = 0;
}

#define MISSING_PSS_MSG \
	FW_BUG "No compatible ACPI _PSS objects found.\n" \
	FW_BUG "First, make sure Cool'N'Quiet is enabled in the BIOS.\n" \
	FW_BUG "If that doesn't help, try upgrading your BIOS.\n"

/* per CPU init entry point to the driver */
static int powernowk8_cpu_init(struct cpufreq_policy *pol)
{
	struct powernow_k8_data *data;
	struct init_on_cpu init_on_cpu;
	int rc, cpu;

	smp_call_function_single(pol->cpu, check_supported_cpu, &rc, 1);
	if (rc)
		return -ENODEV;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->cpu = pol->cpu;

	if (powernow_k8_cpu_init_acpi(data)) {
		/*
		 * Use the PSB BIOS structure. This is only available on
		 * an UP version, and is deprecated by AMD.
		 */
		if (num_online_cpus() != 1) {
			pr_err_once(MISSING_PSS_MSG);
			goto err_out;
		}
		if (pol->cpu != 0) {
			pr_err(FW_BUG "No ACPI _PSS objects for CPU other than CPU0. Complain to your BIOS vendor.\n");
			goto err_out;
		}
		rc = find_psb_table(data);
		if (rc)
			goto err_out;

		/* Take a crude guess here.
		 * That guess was in microseconds, so multiply with 1000 */
		pol->cpuinfo.transition_latency = (
			 ((data->rvo + 8) * data->vstable * VST_UNITS_20US) +
			 ((1 << data->irt) * 30)) * 1000;
	} else /* ACPI _PSS objects available */
		pol->cpuinfo.transition_latency = get_transition_latency(data);

	/* only run on specific CPU from here on */
	init_on_cpu.data = data;
	smp_call_function_single(data->cpu, powernowk8_cpu_init_on_cpu,
				 &init_on_cpu, 1);
	rc = init_on_cpu.rc;
	if (rc != 0)
		goto err_out_exit_acpi;

	cpumask_copy(pol->cpus, topology_core_cpumask(pol->cpu));
	data->available_cores = pol->cpus;
	pol->freq_table = data->powernow_table;

	pr_debug("cpu_init done, current fid 0x%x, vid 0x%x\n",
		data->currfid, data->currvid);

	/* Point all the CPUs in this policy to the same data */
	for_each_cpu(cpu, pol->cpus)
		per_cpu(powernow_data, cpu) = data;

	return 0;

err_out_exit_acpi:
	powernow_k8_cpu_exit_acpi(data);

err_out:
	kfree(data);
	return -ENODEV;
}

static void powernowk8_cpu_exit(struct cpufreq_policy *pol)
{
	struct powernow_k8_data *data = per_cpu(powernow_data, pol->cpu);
	int cpu;

	if (!data)
		return;

	powernow_k8_cpu_exit_acpi(data);

	kfree(data->powernow_table);
	kfree(data);
	/* pol->cpus will be empty here, use related_cpus instead. */
	for_each_cpu(cpu, pol->related_cpus)
		per_cpu(powernow_data, cpu) = NULL;
}

static void query_values_on_cpu(void *_err)
{
	int *err = _err;
	struct powernow_k8_data *data = __this_cpu_read(powernow_data);

	*err = query_current_values_with_pending_wait(data);
}

static unsigned int powernowk8_get(unsigned int cpu)
{
	struct powernow_k8_data *data = per_cpu(powernow_data, cpu);
	unsigned int khz = 0;
	int err;

	if (!data)
		return 0;

	smp_call_function_single(cpu, query_values_on_cpu, &err, true);
	if (err)
		goto out;

	khz = find_khz_freq_from_fid(data->currfid);


out:
	return khz;
}

static struct cpufreq_driver cpufreq_amd64_driver = {
	.flags		= CPUFREQ_ASYNC_NOTIFICATION,
	.verify		= cpufreq_generic_frequency_table_verify,
	.target_index	= powernowk8_target,
	.bios_limit	= acpi_processor_get_bios_limit,
	.init		= powernowk8_cpu_init,
	.exit		= powernowk8_cpu_exit,
	.get		= powernowk8_get,
	.name		= "powernow-k8",
};

static void __request_acpi_cpufreq(void)
{
	const char drv[] = "acpi-cpufreq";
	const char *cur_drv;

	cur_drv = cpufreq_get_current_driver();
	if (!cur_drv)
		goto request;

	if (strncmp(cur_drv, drv, min_t(size_t, strlen(cur_drv), strlen(drv))))
		pr_warn("WTF driver: %s\n", cur_drv);

	return;

 request:
	pr_warn("This CPU is not supported anymore, using acpi-cpufreq instead.\n");
	request_module(drv);
}

/* driver entry point for init */
static int powernowk8_init(void)
{
	unsigned int i, supported_cpus = 0;
	int ret;

	if (!x86_match_cpu(powernow_k8_ids))
		return -ENODEV;

	if (boot_cpu_has(X86_FEATURE_HW_PSTATE)) {
		__request_acpi_cpufreq();
		return -ENODEV;
	}

	cpus_read_lock();
	for_each_online_cpu(i) {
		smp_call_function_single(i, check_supported_cpu, &ret, 1);
		if (!ret)
			supported_cpus++;
	}

	if (supported_cpus != num_online_cpus()) {
		cpus_read_unlock();
		return -ENODEV;
	}
	cpus_read_unlock();

	ret = cpufreq_register_driver(&cpufreq_amd64_driver);
	if (ret)
		return ret;

	pr_info("Found %d %s (%d cpu cores) (" VERSION ")\n",
		num_online_nodes(), boot_cpu_data.x86_model_id, supported_cpus);

	return ret;
}

/* driver entry point for term */
static void __exit powernowk8_exit(void)
{
	pr_debug("exit\n");

	cpufreq_unregister_driver(&cpufreq_amd64_driver);
}

MODULE_AUTHOR("Paul Devriendt <paul.devriendt@amd.com>");
MODULE_AUTHOR("Mark Langsdorf <mark.langsdorf@amd.com>");
MODULE_DESCRIPTION("AMD Athlon 64 and Opteron processor frequency driver.");
MODULE_LICENSE("GPL");

late_initcall(powernowk8_init);
module_exit(powernowk8_exit);
