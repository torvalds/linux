// SPDX-License-Identifier: GPL-2.0
/*
 * SCLP early driver
 *
 * Copyright IBM Corp. 2013
 */

#define KMSG_COMPONENT "sclp_early"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/errno.h>
#include <linux/memblock.h>
#include <asm/ctlreg.h>
#include <asm/sclp.h>
#include <asm/ipl.h>
#include <asm/setup.h>
#include <asm/facility.h>
#include "sclp_sdias.h"
#include "sclp.h"

static struct sclp_ipl_info sclp_ipl_info;

struct sclp_info sclp;
EXPORT_SYMBOL(sclp);

static void __init sclp_early_facilities_detect(void)
{
	struct sclp_core_entry *cpue;
	struct read_info_sccb *sccb;
	u16 boot_cpu_address, cpu;

	sccb = sclp_early_get_info();
	if (!sccb)
		return;

	sclp.facilities = sccb->facilities;
	sclp.has_sprp = !!(sccb->fac84 & 0x02);
	sclp.has_core_type = !!(sccb->fac84 & 0x01);
	sclp.has_gsls = !!(sccb->fac85 & 0x80);
	sclp.has_64bscao = !!(sccb->fac116 & 0x80);
	sclp.has_cmma = !!(sccb->fac116 & 0x40);
	sclp.has_esca = !!(sccb->fac116 & 0x08);
	sclp.has_pfmfi = !!(sccb->fac117 & 0x40);
	sclp.has_ibs = !!(sccb->fac117 & 0x20);
	sclp.has_gisaf = !!(sccb->fac118 & 0x08);
	sclp.has_hvs = !!(sccb->fac119 & 0x80);
	sclp.has_kss = !!(sccb->fac98 & 0x01);
	sclp.has_aisii = !!(sccb->fac118 & 0x40);
	sclp.has_aeni = !!(sccb->fac118 & 0x20);
	sclp.has_aisi = !!(sccb->fac118 & 0x10);
	sclp.has_zpci_lsi = !!(sccb->fac118 & 0x01);
	if (sccb->fac85 & 0x02)
		S390_lowcore.machine_flags |= MACHINE_FLAG_ESOP;
	if (sccb->fac91 & 0x40)
		S390_lowcore.machine_flags |= MACHINE_FLAG_TLB_GUEST;
	if (sccb->cpuoff > 134) {
		sclp.has_diag318 = !!(sccb->byte_134 & 0x80);
		sclp.has_diag320 = !!(sccb->byte_134 & 0x04);
		sclp.has_iplcc = !!(sccb->byte_134 & 0x02);
	}
	if (sccb->cpuoff > 137) {
		sclp.has_sipl = !!(sccb->cbl & 0x4000);
		sclp.has_sipl_eckd = !!(sccb->cbl & 0x2000);
	}
	sclp.rnmax = sccb->rnmax ? sccb->rnmax : sccb->rnmax2;
	sclp.rzm = sccb->rnsize ? sccb->rnsize : sccb->rnsize2;
	sclp.rzm <<= 20;
	sclp.ibc = sccb->ibc;

	if (sccb->hamaxpow && sccb->hamaxpow < 64)
		sclp.hamax = (1UL << sccb->hamaxpow) - 1;
	else
		sclp.hamax = U64_MAX;

	if (!sccb->hcpua) {
		if (MACHINE_IS_VM)
			sclp.max_cores = 64;
		else
			sclp.max_cores = sccb->ncpurl;
	} else {
		sclp.max_cores = sccb->hcpua + 1;
	}

	boot_cpu_address = stap();
	cpue = (void *)sccb + sccb->cpuoff;
	for (cpu = 0; cpu < sccb->ncpurl; cpue++, cpu++) {
		if (boot_cpu_address != cpue->core_id)
			continue;
		sclp.has_siif = cpue->siif;
		sclp.has_sigpif = cpue->sigpif;
		sclp.has_sief2 = cpue->sief2;
		sclp.has_gpere = cpue->gpere;
		sclp.has_ib = cpue->ib;
		sclp.has_cei = cpue->cei;
		sclp.has_skey = cpue->skey;
		break;
	}

	/* Save IPL information */
	sclp_ipl_info.is_valid = 1;
	if (sccb->fac91 & 0x2)
		sclp_ipl_info.has_dump = 1;
	memcpy(&sclp_ipl_info.loadparm, &sccb->loadparm, LOADPARM_LEN);

	if (sccb->hsa_size)
		sclp.hsa_size = (sccb->hsa_size - 1) * PAGE_SIZE;
	sclp.mtid = (sccb->fac42 & 0x80) ? (sccb->fac42 & 31) : 0;
	sclp.mtid_cp = (sccb->fac42 & 0x80) ? (sccb->fac43 & 31) : 0;
	sclp.mtid_prev = (sccb->fac42 & 0x80) ? (sccb->fac66 & 31) : 0;

	sclp.hmfai = sccb->hmfai;
	sclp.has_dirq = !!(sccb->cpudirq & 0x80);
}

/*
 * This function will be called after sclp_early_facilities_detect(), which gets
 * called from early.c code. The sclp_early_facilities_detect() function retrieves
 * and saves the IPL information.
 */
void __init sclp_early_get_ipl_info(struct sclp_ipl_info *info)
{
	*info = sclp_ipl_info;
}

int __init sclp_early_get_core_info(struct sclp_core_info *info)
{
	struct read_cpu_info_sccb *sccb;
	int length = test_facility(140) ? EXT_SCCB_READ_CPU : PAGE_SIZE;
	int rc = 0;

	if (!SCLP_HAS_CPU_INFO)
		return -EOPNOTSUPP;

	sccb = memblock_alloc_low(length, PAGE_SIZE);
	if (!sccb)
		return -ENOMEM;

	memset(sccb, 0, length);
	sccb->header.length = length;
	sccb->header.control_mask[2] = 0x80;
	if (sclp_early_cmd(SCLP_CMDW_READ_CPU_INFO, sccb)) {
		rc = -EIO;
		goto out;
	}
	if (sccb->header.response_code != 0x0010) {
		rc = -EIO;
		goto out;
	}
	sclp_fill_core_info(info, sccb);
out:
	memblock_free(sccb, length);
	return rc;
}

static void __init sclp_early_console_detect(struct init_sccb *sccb)
{
	if (sccb->header.response_code != 0x20)
		return;

	if (sclp_early_con_check_vt220(sccb))
		sclp.has_vt220 = 1;

	if (sclp_early_con_check_linemode(sccb))
		sclp.has_linemode = 1;
}

void __init __no_sanitize_address sclp_early_adjust_va(void)
{
	sclp_early_sccb = __va((unsigned long)sclp_early_sccb);
}

void __init sclp_early_detect(void)
{
	void *sccb = sclp_early_sccb;

	sclp_early_facilities_detect();

	/*
	 * Turn off SCLP event notifications.  Also save remote masks in the
	 * sccb.  These are sufficient to detect sclp console capabilities.
	 */
	sclp_early_set_event_mask(sccb, 0, 0);
	sclp_early_console_detect(sccb);
}
