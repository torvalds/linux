// SPDX-License-Identifier: GPL-2.0
/*
 * store hypervisor information instruction emulation functions.
 *
 * Copyright IBM Corp. 2016
 * Author(s): Janosch Frank <frankja@linux.vnet.ibm.com>
 */
#include <linux/errno.h>
#include <linux/pagemap.h>
#include <linux/vmalloc.h>
#include <linux/syscalls.h>
#include <linux/mutex.h>
#include <asm/asm-offsets.h>
#include <asm/sclp.h>
#include <asm/diag.h>
#include <asm/sysinfo.h>
#include <asm/ebcdic.h>
#include <asm/facility.h>
#include <asm/sthyi.h>
#include "entry.h"

#define DED_WEIGHT 0xffff
/*
 * CP and IFL as EBCDIC strings, SP/0x40 determines the end of string
 * as they are justified with spaces.
 */
#define CP  0xc3d7404040404040UL
#define IFL 0xc9c6d34040404040UL

enum hdr_flags {
	HDR_NOT_LPAR   = 0x10,
	HDR_STACK_INCM = 0x20,
	HDR_STSI_UNAV  = 0x40,
	HDR_PERF_UNAV  = 0x80,
};

enum mac_validity {
	MAC_NAME_VLD = 0x20,
	MAC_ID_VLD   = 0x40,
	MAC_CNT_VLD  = 0x80,
};

enum par_flag {
	PAR_MT_EN = 0x80,
};

enum par_validity {
	PAR_GRP_VLD  = 0x08,
	PAR_ID_VLD   = 0x10,
	PAR_ABS_VLD  = 0x20,
	PAR_WGHT_VLD = 0x40,
	PAR_PCNT_VLD  = 0x80,
};

struct hdr_sctn {
	u8 infhflg1;
	u8 infhflg2; /* reserved */
	u8 infhval1; /* reserved */
	u8 infhval2; /* reserved */
	u8 reserved[3];
	u8 infhygct;
	u16 infhtotl;
	u16 infhdln;
	u16 infmoff;
	u16 infmlen;
	u16 infpoff;
	u16 infplen;
	u16 infhoff1;
	u16 infhlen1;
	u16 infgoff1;
	u16 infglen1;
	u16 infhoff2;
	u16 infhlen2;
	u16 infgoff2;
	u16 infglen2;
	u16 infhoff3;
	u16 infhlen3;
	u16 infgoff3;
	u16 infglen3;
	u8 reserved2[4];
} __packed;

struct mac_sctn {
	u8 infmflg1; /* reserved */
	u8 infmflg2; /* reserved */
	u8 infmval1;
	u8 infmval2; /* reserved */
	u16 infmscps;
	u16 infmdcps;
	u16 infmsifl;
	u16 infmdifl;
	char infmname[8];
	char infmtype[4];
	char infmmanu[16];
	char infmseq[16];
	char infmpman[4];
	u8 reserved[4];
} __packed;

struct par_sctn {
	u8 infpflg1;
	u8 infpflg2; /* reserved */
	u8 infpval1;
	u8 infpval2; /* reserved */
	u16 infppnum;
	u16 infpscps;
	u16 infpdcps;
	u16 infpsifl;
	u16 infpdifl;
	u16 reserved;
	char infppnam[8];
	u32 infpwbcp;
	u32 infpabcp;
	u32 infpwbif;
	u32 infpabif;
	char infplgnm[8];
	u32 infplgcp;
	u32 infplgif;
} __packed;

struct sthyi_sctns {
	struct hdr_sctn hdr;
	struct mac_sctn mac;
	struct par_sctn par;
} __packed;

struct cpu_inf {
	u64 lpar_cap;
	u64 lpar_grp_cap;
	u64 lpar_weight;
	u64 all_weight;
	int cpu_num_ded;
	int cpu_num_shd;
};

struct lpar_cpu_inf {
	struct cpu_inf cp;
	struct cpu_inf ifl;
};

/*
 * STHYI requires extensive locking in the higher hypervisors
 * and is very computational/memory expensive. Therefore we
 * cache the retrieved data whose valid period is 1s.
 */
#define CACHE_VALID_JIFFIES	HZ

struct sthyi_info {
	void *info;
	unsigned long end;
};

static DEFINE_MUTEX(sthyi_mutex);
static struct sthyi_info sthyi_cache;

static inline u64 cpu_id(u8 ctidx, void *diag224_buf)
{
	return *((u64 *)(diag224_buf + (ctidx + 1) * DIAG204_CPU_NAME_LEN));
}

/*
 * Scales the cpu capping from the lpar range to the one expected in
 * sthyi data.
 *
 * diag204 reports a cap in hundredths of processor units.
 * z/VM's range for one core is 0 - 0x10000.
 */
static u32 scale_cap(u32 in)
{
	return (0x10000 * in) / 100;
}

static void fill_hdr(struct sthyi_sctns *sctns)
{
	sctns->hdr.infhdln = sizeof(sctns->hdr);
	sctns->hdr.infmoff = sizeof(sctns->hdr);
	sctns->hdr.infmlen = sizeof(sctns->mac);
	sctns->hdr.infplen = sizeof(sctns->par);
	sctns->hdr.infpoff = sctns->hdr.infhdln + sctns->hdr.infmlen;
	sctns->hdr.infhtotl = sctns->hdr.infpoff + sctns->hdr.infplen;
}

static void fill_stsi_mac(struct sthyi_sctns *sctns,
			  struct sysinfo_1_1_1 *sysinfo)
{
	if (stsi(sysinfo, 1, 1, 1))
		return;

	sclp_ocf_cpc_name_copy(sctns->mac.infmname);

	memcpy(sctns->mac.infmtype, sysinfo->type, sizeof(sctns->mac.infmtype));
	memcpy(sctns->mac.infmmanu, sysinfo->manufacturer, sizeof(sctns->mac.infmmanu));
	memcpy(sctns->mac.infmpman, sysinfo->plant, sizeof(sctns->mac.infmpman));
	memcpy(sctns->mac.infmseq, sysinfo->sequence, sizeof(sctns->mac.infmseq));

	sctns->mac.infmval1 |= MAC_ID_VLD | MAC_NAME_VLD;
}

static void fill_stsi_par(struct sthyi_sctns *sctns,
			  struct sysinfo_2_2_2 *sysinfo)
{
	if (stsi(sysinfo, 2, 2, 2))
		return;

	sctns->par.infppnum = sysinfo->lpar_number;
	memcpy(sctns->par.infppnam, sysinfo->name, sizeof(sctns->par.infppnam));

	sctns->par.infpval1 |= PAR_ID_VLD;
}

static void fill_stsi(struct sthyi_sctns *sctns)
{
	void *sysinfo;

	/* Errors are handled through the validity bits in the response. */
	sysinfo = (void *)__get_free_page(GFP_KERNEL);
	if (!sysinfo)
		return;

	fill_stsi_mac(sctns, sysinfo);
	fill_stsi_par(sctns, sysinfo);

	free_pages((unsigned long)sysinfo, 0);
}

static void fill_diag_mac(struct sthyi_sctns *sctns,
			  struct diag204_x_phys_block *block,
			  void *diag224_buf)
{
	int i;

	for (i = 0; i < block->hdr.cpus; i++) {
		switch (cpu_id(block->cpus[i].ctidx, diag224_buf)) {
		case CP:
			if (block->cpus[i].weight == DED_WEIGHT)
				sctns->mac.infmdcps++;
			else
				sctns->mac.infmscps++;
			break;
		case IFL:
			if (block->cpus[i].weight == DED_WEIGHT)
				sctns->mac.infmdifl++;
			else
				sctns->mac.infmsifl++;
			break;
		}
	}
	sctns->mac.infmval1 |= MAC_CNT_VLD;
}

/* Returns a pointer to the the next partition block. */
static struct diag204_x_part_block *lpar_cpu_inf(struct lpar_cpu_inf *part_inf,
						 bool this_lpar,
						 void *diag224_buf,
						 struct diag204_x_part_block *block)
{
	int i, capped = 0, weight_cp = 0, weight_ifl = 0;
	struct cpu_inf *cpu_inf;

	for (i = 0; i < block->hdr.rcpus; i++) {
		if (!(block->cpus[i].cflag & DIAG204_CPU_ONLINE))
			continue;

		switch (cpu_id(block->cpus[i].ctidx, diag224_buf)) {
		case CP:
			cpu_inf = &part_inf->cp;
			if (block->cpus[i].cur_weight < DED_WEIGHT)
				weight_cp |= block->cpus[i].cur_weight;
			break;
		case IFL:
			cpu_inf = &part_inf->ifl;
			if (block->cpus[i].cur_weight < DED_WEIGHT)
				weight_ifl |= block->cpus[i].cur_weight;
			break;
		default:
			continue;
		}

		if (!this_lpar)
			continue;

		capped |= block->cpus[i].cflag & DIAG204_CPU_CAPPED;
		cpu_inf->lpar_cap |= block->cpus[i].cpu_type_cap;
		cpu_inf->lpar_grp_cap |= block->cpus[i].group_cpu_type_cap;

		if (block->cpus[i].weight == DED_WEIGHT)
			cpu_inf->cpu_num_ded += 1;
		else
			cpu_inf->cpu_num_shd += 1;
	}

	if (this_lpar && capped) {
		part_inf->cp.lpar_weight = weight_cp;
		part_inf->ifl.lpar_weight = weight_ifl;
	}
	part_inf->cp.all_weight += weight_cp;
	part_inf->ifl.all_weight += weight_ifl;
	return (struct diag204_x_part_block *)&block->cpus[i];
}

static void fill_diag(struct sthyi_sctns *sctns)
{
	int i, r, pages;
	bool this_lpar;
	void *diag204_buf;
	void *diag224_buf = NULL;
	struct diag204_x_info_blk_hdr *ti_hdr;
	struct diag204_x_part_block *part_block;
	struct diag204_x_phys_block *phys_block;
	struct lpar_cpu_inf lpar_inf = {};

	/* Errors are handled through the validity bits in the response. */
	pages = diag204((unsigned long)DIAG204_SUBC_RSI |
			(unsigned long)DIAG204_INFO_EXT, 0, NULL);
	if (pages <= 0)
		return;

	diag204_buf = vmalloc(PAGE_SIZE * pages);
	if (!diag204_buf)
		return;

	r = diag204((unsigned long)DIAG204_SUBC_STIB7 |
		    (unsigned long)DIAG204_INFO_EXT, pages, diag204_buf);
	if (r < 0)
		goto out;

	diag224_buf = (void *)__get_free_page(GFP_KERNEL | GFP_DMA);
	if (!diag224_buf || diag224(diag224_buf))
		goto out;

	ti_hdr = diag204_buf;
	part_block = diag204_buf + sizeof(*ti_hdr);

	for (i = 0; i < ti_hdr->npar; i++) {
		/*
		 * For the calling lpar we also need to get the cpu
		 * caps and weights. The time information block header
		 * specifies the offset to the partition block of the
		 * caller lpar, so we know when we process its data.
		 */
		this_lpar = (void *)part_block - diag204_buf == ti_hdr->this_part;
		part_block = lpar_cpu_inf(&lpar_inf, this_lpar, diag224_buf,
					  part_block);
	}

	phys_block = (struct diag204_x_phys_block *)part_block;
	part_block = diag204_buf + ti_hdr->this_part;
	if (part_block->hdr.mtid)
		sctns->par.infpflg1 = PAR_MT_EN;

	sctns->par.infpval1 |= PAR_GRP_VLD;
	sctns->par.infplgcp = scale_cap(lpar_inf.cp.lpar_grp_cap);
	sctns->par.infplgif = scale_cap(lpar_inf.ifl.lpar_grp_cap);
	memcpy(sctns->par.infplgnm, part_block->hdr.hardware_group_name,
	       sizeof(sctns->par.infplgnm));

	sctns->par.infpscps = lpar_inf.cp.cpu_num_shd;
	sctns->par.infpdcps = lpar_inf.cp.cpu_num_ded;
	sctns->par.infpsifl = lpar_inf.ifl.cpu_num_shd;
	sctns->par.infpdifl = lpar_inf.ifl.cpu_num_ded;
	sctns->par.infpval1 |= PAR_PCNT_VLD;

	sctns->par.infpabcp = scale_cap(lpar_inf.cp.lpar_cap);
	sctns->par.infpabif = scale_cap(lpar_inf.ifl.lpar_cap);
	sctns->par.infpval1 |= PAR_ABS_VLD;

	/*
	 * Everything below needs global performance data to be
	 * meaningful.
	 */
	if (!(ti_hdr->flags & DIAG204_LPAR_PHYS_FLG)) {
		sctns->hdr.infhflg1 |= HDR_PERF_UNAV;
		goto out;
	}

	fill_diag_mac(sctns, phys_block, diag224_buf);

	if (lpar_inf.cp.lpar_weight) {
		sctns->par.infpwbcp = sctns->mac.infmscps * 0x10000 *
			lpar_inf.cp.lpar_weight / lpar_inf.cp.all_weight;
	}

	if (lpar_inf.ifl.lpar_weight) {
		sctns->par.infpwbif = sctns->mac.infmsifl * 0x10000 *
			lpar_inf.ifl.lpar_weight / lpar_inf.ifl.all_weight;
	}
	sctns->par.infpval1 |= PAR_WGHT_VLD;

out:
	free_page((unsigned long)diag224_buf);
	vfree(diag204_buf);
}

static int sthyi(u64 vaddr, u64 *rc)
{
	register u64 code asm("0") = 0;
	register u64 addr asm("2") = vaddr;
	register u64 rcode asm("3");
	int cc;

	asm volatile(
		".insn   rre,0xB2560000,%[code],%[addr]\n"
		"ipm     %[cc]\n"
		"srl     %[cc],28\n"
		: [cc] "=d" (cc), "=d" (rcode)
		: [code] "d" (code), [addr] "a" (addr)
		: "memory", "cc");
	*rc = rcode;
	return cc;
}

static int fill_dst(void *dst, u64 *rc)
{
	struct sthyi_sctns *sctns = (struct sthyi_sctns *)dst;

	/*
	 * If the facility is on, we don't want to emulate the instruction.
	 * We ask the hypervisor to provide the data.
	 */
	if (test_facility(74))
		return sthyi((u64)dst, rc);

	fill_hdr(sctns);
	fill_stsi(sctns);
	fill_diag(sctns);
	*rc = 0;
	return 0;
}

static int sthyi_init_cache(void)
{
	if (sthyi_cache.info)
		return 0;
	sthyi_cache.info = (void *)get_zeroed_page(GFP_KERNEL);
	if (!sthyi_cache.info)
		return -ENOMEM;
	sthyi_cache.end = jiffies - 1; /* expired */
	return 0;
}

static int sthyi_update_cache(u64 *rc)
{
	int r;

	memset(sthyi_cache.info, 0, PAGE_SIZE);
	r = fill_dst(sthyi_cache.info, rc);
	if (r)
		return r;
	sthyi_cache.end = jiffies + CACHE_VALID_JIFFIES;
	return r;
}

/*
 * sthyi_fill - Fill page with data returned by the STHYI instruction
 *
 * @dst: Pointer to zeroed page
 * @rc:  Pointer for storing the return code of the instruction
 *
 * Fills the destination with system information returned by the STHYI
 * instruction. The data is generated by emulation or execution of STHYI,
 * if available. The return value is the condition code that would be
 * returned, the rc parameter is the return code which is passed in
 * register R2 + 1.
 */
int sthyi_fill(void *dst, u64 *rc)
{
	int r;

	mutex_lock(&sthyi_mutex);
	r = sthyi_init_cache();
	if (r)
		goto out;

	if (time_is_before_jiffies(sthyi_cache.end)) {
		/* cache expired */
		r = sthyi_update_cache(rc);
		if (r)
			goto out;
	}
	*rc = 0;
	memcpy(dst, sthyi_cache.info, PAGE_SIZE);
out:
	mutex_unlock(&sthyi_mutex);
	return r;
}
EXPORT_SYMBOL_GPL(sthyi_fill);

SYSCALL_DEFINE4(s390_sthyi, unsigned long, function_code, void __user *, buffer,
		u64 __user *, return_code, unsigned long, flags)
{
	u64 sthyi_rc;
	void *info;
	int r;

	if (flags)
		return -EINVAL;
	if (function_code != STHYI_FC_CP_IFL_CAP)
		return -EOPNOTSUPP;
	info = (void *)get_zeroed_page(GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	r = sthyi_fill(info, &sthyi_rc);
	if (r < 0)
		goto out;
	if (return_code && put_user(sthyi_rc, return_code)) {
		r = -EFAULT;
		goto out;
	}
	if (copy_to_user(buffer, info, PAGE_SIZE))
		r = -EFAULT;
out:
	free_page((unsigned long)info);
	return r;
}
