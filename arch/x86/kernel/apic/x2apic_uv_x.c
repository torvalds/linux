/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * SGI UV APIC functions (note: not an Intel compatible APIC)
 *
 * Copyright (C) 2007-2014 Silicon Graphics, Inc. All rights reserved.
 */
#include <linux/cpumask.h>
#include <linux/hardirq.h>
#include <linux/proc_fs.h>
#include <linux/threads.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/pci.h>
#include <linux/kdebug.h>
#include <linux/delay.h>
#include <linux/crash_dump.h>
#include <linux/reboot.h>

#include <asm/uv/uv_mmrs.h>
#include <asm/uv/uv_hub.h>
#include <asm/current.h>
#include <asm/pgtable.h>
#include <asm/uv/bios.h>
#include <asm/uv/uv.h>
#include <asm/apic.h>
#include <asm/ipi.h>
#include <asm/smp.h>
#include <asm/x86_init.h>
#include <asm/nmi.h>

DEFINE_PER_CPU(int, x2apic_extra_bits);

#define PR_DEVEL(fmt, args...)	pr_devel("%s: " fmt, __func__, args)

static enum uv_system_type uv_system_type;
static u64 gru_start_paddr, gru_end_paddr;
static u64 gru_dist_base, gru_first_node_paddr = -1LL, gru_last_node_paddr;
static u64 gru_dist_lmask, gru_dist_umask;
static union uvh_apicid uvh_apicid;
int uv_min_hub_revision_id;
EXPORT_SYMBOL_GPL(uv_min_hub_revision_id);
unsigned int uv_apicid_hibits;
EXPORT_SYMBOL_GPL(uv_apicid_hibits);

static struct apic apic_x2apic_uv_x;

static unsigned long __init uv_early_read_mmr(unsigned long addr)
{
	unsigned long val, *mmr;

	mmr = early_ioremap(UV_LOCAL_MMR_BASE | addr, sizeof(*mmr));
	val = *mmr;
	early_iounmap(mmr, sizeof(*mmr));
	return val;
}

static inline bool is_GRU_range(u64 start, u64 end)
{
	if (gru_dist_base) {
		u64 su = start & gru_dist_umask; /* upper (incl pnode) bits */
		u64 sl = start & gru_dist_lmask; /* base offset bits */
		u64 eu = end & gru_dist_umask;
		u64 el = end & gru_dist_lmask;

		/* Must reside completely within a single GRU range */
		return (sl == gru_dist_base && el == gru_dist_base &&
			su >= gru_first_node_paddr &&
			su <= gru_last_node_paddr &&
			eu == su);
	} else {
		return start >= gru_start_paddr && end <= gru_end_paddr;
	}
}

static bool uv_is_untracked_pat_range(u64 start, u64 end)
{
	return is_ISA_range(start, end) || is_GRU_range(start, end);
}

static int __init early_get_pnodeid(void)
{
	union uvh_node_id_u node_id;
	union uvh_rh_gam_config_mmr_u  m_n_config;
	int pnode;

	/* Currently, all blades have same revision number */
	node_id.v = uv_early_read_mmr(UVH_NODE_ID);
	m_n_config.v = uv_early_read_mmr(UVH_RH_GAM_CONFIG_MMR);
	uv_min_hub_revision_id = node_id.s.revision;

	switch (node_id.s.part_number) {
	case UV2_HUB_PART_NUMBER:
	case UV2_HUB_PART_NUMBER_X:
		uv_min_hub_revision_id += UV2_HUB_REVISION_BASE - 1;
		break;
	case UV3_HUB_PART_NUMBER:
	case UV3_HUB_PART_NUMBER_X:
		uv_min_hub_revision_id += UV3_HUB_REVISION_BASE;
		break;
	}

	uv_hub_info->hub_revision = uv_min_hub_revision_id;
	pnode = (node_id.s.node_id >> 1) & ((1 << m_n_config.s.n_skt) - 1);
	return pnode;
}

static void __init early_get_apic_pnode_shift(void)
{
	uvh_apicid.v = uv_early_read_mmr(UVH_APICID);
	if (!uvh_apicid.v)
		/*
		 * Old bios, use default value
		 */
		uvh_apicid.s.pnode_shift = UV_APIC_PNODE_SHIFT;
}

/*
 * Add an extra bit as dictated by bios to the destination apicid of
 * interrupts potentially passing through the UV HUB.  This prevents
 * a deadlock between interrupts and IO port operations.
 */
static void __init uv_set_apicid_hibit(void)
{
	union uv1h_lb_target_physical_apic_id_mask_u apicid_mask;

	if (is_uv1_hub()) {
		apicid_mask.v =
			uv_early_read_mmr(UV1H_LB_TARGET_PHYSICAL_APIC_ID_MASK);
		uv_apicid_hibits =
			apicid_mask.s1.bit_enables & UV_APICID_HIBIT_MASK;
	}
}

static int __init uv_acpi_madt_oem_check(char *oem_id, char *oem_table_id)
{
	int pnodeid;
	int uv_apic;

	if (strncmp(oem_id, "SGI", 3) != 0)
		return 0;

	/*
	 * Determine UV arch type.
	 *   SGI: UV100/1000
	 *   SGI2: UV2000/3000
	 *   SGI3: UV300 (truncated to 4 chars because of different varieties)
	 */
	uv_hub_info->hub_revision =
		!strncmp(oem_id, "SGI3", 4) ? UV3_HUB_REVISION_BASE :
		!strcmp(oem_id, "SGI2") ? UV2_HUB_REVISION_BASE :
		!strcmp(oem_id, "SGI") ? UV1_HUB_REVISION_BASE : 0;

	if (uv_hub_info->hub_revision == 0)
		goto badbios;

	pnodeid = early_get_pnodeid();
	early_get_apic_pnode_shift();
	x86_platform.is_untracked_pat_range =  uv_is_untracked_pat_range;
	x86_platform.nmi_init = uv_nmi_init;

	if (!strcmp(oem_table_id, "UVX")) {		/* most common */
		uv_system_type = UV_X2APIC;
		uv_apic = 0;

	} else if (!strcmp(oem_table_id, "UVH")) {	/* only UV1 systems */
		uv_system_type = UV_NON_UNIQUE_APIC;
		__this_cpu_write(x2apic_extra_bits,
			pnodeid << uvh_apicid.s.pnode_shift);
		uv_set_apicid_hibit();
		uv_apic = 1;

	} else	if (!strcmp(oem_table_id, "UVL")) {	/* only used for */
		uv_system_type = UV_LEGACY_APIC;	/* very small systems */
		uv_apic = 0;

	} else {
		goto badbios;
	}

	pr_info("UV: OEM IDs %s/%s, System/HUB Types %d/%d, uv_apic %d\n",
		oem_id, oem_table_id, uv_system_type,
		uv_min_hub_revision_id, uv_apic);

	return uv_apic;

badbios:
	pr_err("UV: OEM_ID:%s OEM_TABLE_ID:%s\n", oem_id, oem_table_id);
	pr_err("Current BIOS not supported, update kernel and/or BIOS\n");
	BUG();
}

enum uv_system_type get_uv_system_type(void)
{
	return uv_system_type;
}

int is_uv_system(void)
{
	return uv_system_type != UV_NONE;
}
EXPORT_SYMBOL_GPL(is_uv_system);

DEFINE_PER_CPU(struct uv_hub_info_s, __uv_hub_info);
EXPORT_PER_CPU_SYMBOL_GPL(__uv_hub_info);

struct uv_blade_info *uv_blade_info;
EXPORT_SYMBOL_GPL(uv_blade_info);

short *uv_node_to_blade;
EXPORT_SYMBOL_GPL(uv_node_to_blade);

short *uv_cpu_to_blade;
EXPORT_SYMBOL_GPL(uv_cpu_to_blade);

short uv_possible_blades;
EXPORT_SYMBOL_GPL(uv_possible_blades);

unsigned long sn_rtc_cycles_per_second;
EXPORT_SYMBOL(sn_rtc_cycles_per_second);

static int uv_wakeup_secondary(int phys_apicid, unsigned long start_rip)
{
	unsigned long val;
	int pnode;

	pnode = uv_apicid_to_pnode(phys_apicid);
	phys_apicid |= uv_apicid_hibits;
	val = (1UL << UVH_IPI_INT_SEND_SHFT) |
	    (phys_apicid << UVH_IPI_INT_APIC_ID_SHFT) |
	    ((start_rip << UVH_IPI_INT_VECTOR_SHFT) >> 12) |
	    APIC_DM_INIT;
	uv_write_global_mmr64(pnode, UVH_IPI_INT, val);

	val = (1UL << UVH_IPI_INT_SEND_SHFT) |
	    (phys_apicid << UVH_IPI_INT_APIC_ID_SHFT) |
	    ((start_rip << UVH_IPI_INT_VECTOR_SHFT) >> 12) |
	    APIC_DM_STARTUP;
	uv_write_global_mmr64(pnode, UVH_IPI_INT, val);

	atomic_set(&init_deasserted, 1);
	return 0;
}

static void uv_send_IPI_one(int cpu, int vector)
{
	unsigned long apicid;
	int pnode;

	apicid = per_cpu(x86_cpu_to_apicid, cpu);
	pnode = uv_apicid_to_pnode(apicid);
	uv_hub_send_ipi(pnode, apicid, vector);
}

static void uv_send_IPI_mask(const struct cpumask *mask, int vector)
{
	unsigned int cpu;

	for_each_cpu(cpu, mask)
		uv_send_IPI_one(cpu, vector);
}

static void uv_send_IPI_mask_allbutself(const struct cpumask *mask, int vector)
{
	unsigned int this_cpu = smp_processor_id();
	unsigned int cpu;

	for_each_cpu(cpu, mask) {
		if (cpu != this_cpu)
			uv_send_IPI_one(cpu, vector);
	}
}

static void uv_send_IPI_allbutself(int vector)
{
	unsigned int this_cpu = smp_processor_id();
	unsigned int cpu;

	for_each_online_cpu(cpu) {
		if (cpu != this_cpu)
			uv_send_IPI_one(cpu, vector);
	}
}

static void uv_send_IPI_all(int vector)
{
	uv_send_IPI_mask(cpu_online_mask, vector);
}

static int uv_apic_id_valid(int apicid)
{
	return 1;
}

static int uv_apic_id_registered(void)
{
	return 1;
}

static void uv_init_apic_ldr(void)
{
}

static int
uv_cpu_mask_to_apicid_and(const struct cpumask *cpumask,
			  const struct cpumask *andmask,
			  unsigned int *apicid)
{
	int unsigned cpu;

	/*
	 * We're using fixed IRQ delivery, can only return one phys APIC ID.
	 * May as well be the first.
	 */
	for_each_cpu_and(cpu, cpumask, andmask) {
		if (cpumask_test_cpu(cpu, cpu_online_mask))
			break;
	}

	if (likely(cpu < nr_cpu_ids)) {
		*apicid = per_cpu(x86_cpu_to_apicid, cpu) | uv_apicid_hibits;
		return 0;
	}

	return -EINVAL;
}

static unsigned int x2apic_get_apic_id(unsigned long x)
{
	unsigned int id;

	WARN_ON(preemptible() && num_online_cpus() > 1);
	id = x | __this_cpu_read(x2apic_extra_bits);

	return id;
}

static unsigned long set_apic_id(unsigned int id)
{
	unsigned long x;

	/* maskout x2apic_extra_bits ? */
	x = id;
	return x;
}

static unsigned int uv_read_apic_id(void)
{

	return x2apic_get_apic_id(apic_read(APIC_ID));
}

static int uv_phys_pkg_id(int initial_apicid, int index_msb)
{
	return uv_read_apic_id() >> index_msb;
}

static void uv_send_IPI_self(int vector)
{
	apic_write(APIC_SELF_IPI, vector);
}

static int uv_probe(void)
{
	return apic == &apic_x2apic_uv_x;
}

static struct apic __refdata apic_x2apic_uv_x = {

	.name				= "UV large system",
	.probe				= uv_probe,
	.acpi_madt_oem_check		= uv_acpi_madt_oem_check,
	.apic_id_valid			= uv_apic_id_valid,
	.apic_id_registered		= uv_apic_id_registered,

	.irq_delivery_mode		= dest_Fixed,
	.irq_dest_mode			= 0, /* physical */

	.target_cpus			= online_target_cpus,
	.disable_esr			= 0,
	.dest_logical			= APIC_DEST_LOGICAL,
	.check_apicid_used		= NULL,

	.vector_allocation_domain	= default_vector_allocation_domain,
	.init_apic_ldr			= uv_init_apic_ldr,

	.ioapic_phys_id_map		= NULL,
	.setup_apic_routing		= NULL,
	.cpu_present_to_apicid		= default_cpu_present_to_apicid,
	.apicid_to_cpu_present		= NULL,
	.check_phys_apicid_present	= default_check_phys_apicid_present,
	.phys_pkg_id			= uv_phys_pkg_id,

	.get_apic_id			= x2apic_get_apic_id,
	.set_apic_id			= set_apic_id,
	.apic_id_mask			= 0xFFFFFFFFu,

	.cpu_mask_to_apicid_and		= uv_cpu_mask_to_apicid_and,

	.send_IPI_mask			= uv_send_IPI_mask,
	.send_IPI_mask_allbutself	= uv_send_IPI_mask_allbutself,
	.send_IPI_allbutself		= uv_send_IPI_allbutself,
	.send_IPI_all			= uv_send_IPI_all,
	.send_IPI_self			= uv_send_IPI_self,

	.wakeup_secondary_cpu		= uv_wakeup_secondary,
	.wait_for_init_deassert		= false,
	.inquire_remote_apic		= NULL,

	.read				= native_apic_msr_read,
	.write				= native_apic_msr_write,
	.eoi_write			= native_apic_msr_eoi_write,
	.icr_read			= native_x2apic_icr_read,
	.icr_write			= native_x2apic_icr_write,
	.wait_icr_idle			= native_x2apic_wait_icr_idle,
	.safe_wait_icr_idle		= native_safe_x2apic_wait_icr_idle,
};

static void set_x2apic_extra_bits(int pnode)
{
	__this_cpu_write(x2apic_extra_bits, pnode << uvh_apicid.s.pnode_shift);
}

/*
 * Called on boot cpu.
 */
static __init int boot_pnode_to_blade(int pnode)
{
	int blade;

	for (blade = 0; blade < uv_num_possible_blades(); blade++)
		if (pnode == uv_blade_info[blade].pnode)
			return blade;
	BUG();
}

struct redir_addr {
	unsigned long redirect;
	unsigned long alias;
};

#define DEST_SHIFT UVH_RH_GAM_ALIAS210_REDIRECT_CONFIG_0_MMR_DEST_BASE_SHFT

static __initdata struct redir_addr redir_addrs[] = {
	{UVH_RH_GAM_ALIAS210_REDIRECT_CONFIG_0_MMR, UVH_RH_GAM_ALIAS210_OVERLAY_CONFIG_0_MMR},
	{UVH_RH_GAM_ALIAS210_REDIRECT_CONFIG_1_MMR, UVH_RH_GAM_ALIAS210_OVERLAY_CONFIG_1_MMR},
	{UVH_RH_GAM_ALIAS210_REDIRECT_CONFIG_2_MMR, UVH_RH_GAM_ALIAS210_OVERLAY_CONFIG_2_MMR},
};

static unsigned char get_n_lshift(int m_val)
{
	union uv3h_gr0_gam_gr_config_u m_gr_config;

	if (is_uv1_hub())
		return m_val;

	if (is_uv2_hub())
		return m_val == 40 ? 40 : 39;

	m_gr_config.v = uv_read_local_mmr(UV3H_GR0_GAM_GR_CONFIG);
	return m_gr_config.s3.m_skt;
}

static __init void get_lowmem_redirect(unsigned long *base, unsigned long *size)
{
	union uvh_rh_gam_alias210_overlay_config_2_mmr_u alias;
	union uvh_rh_gam_alias210_redirect_config_2_mmr_u redirect;
	int i;

	for (i = 0; i < ARRAY_SIZE(redir_addrs); i++) {
		alias.v = uv_read_local_mmr(redir_addrs[i].alias);
		if (alias.s.enable && alias.s.base == 0) {
			*size = (1UL << alias.s.m_alias);
			redirect.v = uv_read_local_mmr(redir_addrs[i].redirect);
			*base = (unsigned long)redirect.s.dest_base << DEST_SHIFT;
			return;
		}
	}
	*base = *size = 0;
}

enum map_type {map_wb, map_uc};

static __init void map_high(char *id, unsigned long base, int pshift,
			int bshift, int max_pnode, enum map_type map_type)
{
	unsigned long bytes, paddr;

	paddr = base << pshift;
	bytes = (1UL << bshift) * (max_pnode + 1);
	if (!paddr) {
		pr_info("UV: Map %s_HI base address NULL\n", id);
		return;
	}
	pr_debug("UV: Map %s_HI 0x%lx - 0x%lx\n", id, paddr, paddr + bytes);
	if (map_type == map_uc)
		init_extra_mapping_uc(paddr, bytes);
	else
		init_extra_mapping_wb(paddr, bytes);
}

static __init void map_gru_distributed(unsigned long c)
{
	union uvh_rh_gam_gru_overlay_config_mmr_u gru;
	u64 paddr;
	unsigned long bytes;
	int nid;

	gru.v = c;
	/* only base bits 42:28 relevant in dist mode */
	gru_dist_base = gru.v & 0x000007fff0000000UL;
	if (!gru_dist_base) {
		pr_info("UV: Map GRU_DIST base address NULL\n");
		return;
	}
	bytes = 1UL << UVH_RH_GAM_GRU_OVERLAY_CONFIG_MMR_BASE_SHFT;
	gru_dist_lmask = ((1UL << uv_hub_info->m_val) - 1) & ~(bytes - 1);
	gru_dist_umask = ~((1UL << uv_hub_info->m_val) - 1);
	gru_dist_base &= gru_dist_lmask; /* Clear bits above M */
	for_each_online_node(nid) {
		paddr = ((u64)uv_node_to_pnode(nid) << uv_hub_info->m_val) |
				gru_dist_base;
		init_extra_mapping_wb(paddr, bytes);
		gru_first_node_paddr = min(paddr, gru_first_node_paddr);
		gru_last_node_paddr = max(paddr, gru_last_node_paddr);
	}
	/* Save upper (63:M) bits of address only for is_GRU_range */
	gru_first_node_paddr &= gru_dist_umask;
	gru_last_node_paddr &= gru_dist_umask;
	pr_debug("UV: Map GRU_DIST base 0x%016llx  0x%016llx - 0x%016llx\n",
		gru_dist_base, gru_first_node_paddr, gru_last_node_paddr);
}

static __init void map_gru_high(int max_pnode)
{
	union uvh_rh_gam_gru_overlay_config_mmr_u gru;
	int shift = UVH_RH_GAM_GRU_OVERLAY_CONFIG_MMR_BASE_SHFT;

	gru.v = uv_read_local_mmr(UVH_RH_GAM_GRU_OVERLAY_CONFIG_MMR);
	if (!gru.s.enable) {
		pr_info("UV: GRU disabled\n");
		return;
	}

	if (is_uv3_hub() && gru.s3.mode) {
		map_gru_distributed(gru.v);
		return;
	}
	map_high("GRU", gru.s.base, shift, shift, max_pnode, map_wb);
	gru_start_paddr = ((u64)gru.s.base << shift);
	gru_end_paddr = gru_start_paddr + (1UL << shift) * (max_pnode + 1);
}

static __init void map_mmr_high(int max_pnode)
{
	union uvh_rh_gam_mmr_overlay_config_mmr_u mmr;
	int shift = UVH_RH_GAM_MMR_OVERLAY_CONFIG_MMR_BASE_SHFT;

	mmr.v = uv_read_local_mmr(UVH_RH_GAM_MMR_OVERLAY_CONFIG_MMR);
	if (mmr.s.enable)
		map_high("MMR", mmr.s.base, shift, shift, max_pnode, map_uc);
	else
		pr_info("UV: MMR disabled\n");
}

/*
 * This commonality works because both 0 & 1 versions of the MMIOH OVERLAY
 * and REDIRECT MMR regs are exactly the same on UV3.
 */
struct mmioh_config {
	unsigned long overlay;
	unsigned long redirect;
	char *id;
};

static __initdata struct mmioh_config mmiohs[] = {
	{
		UV3H_RH_GAM_MMIOH_OVERLAY_CONFIG0_MMR,
		UV3H_RH_GAM_MMIOH_REDIRECT_CONFIG0_MMR,
		"MMIOH0"
	},
	{
		UV3H_RH_GAM_MMIOH_OVERLAY_CONFIG1_MMR,
		UV3H_RH_GAM_MMIOH_REDIRECT_CONFIG1_MMR,
		"MMIOH1"
	},
};

static __init void map_mmioh_high_uv3(int index, int min_pnode, int max_pnode)
{
	union uv3h_rh_gam_mmioh_overlay_config0_mmr_u overlay;
	unsigned long mmr;
	unsigned long base;
	int i, n, shift, m_io, max_io;
	int nasid, lnasid, fi, li;
	char *id;

	id = mmiohs[index].id;
	overlay.v = uv_read_local_mmr(mmiohs[index].overlay);
	pr_info("UV: %s overlay 0x%lx base:0x%x m_io:%d\n",
		id, overlay.v, overlay.s3.base, overlay.s3.m_io);
	if (!overlay.s3.enable) {
		pr_info("UV: %s disabled\n", id);
		return;
	}

	shift = UV3H_RH_GAM_MMIOH_OVERLAY_CONFIG0_MMR_BASE_SHFT;
	base = (unsigned long)overlay.s3.base;
	m_io = overlay.s3.m_io;
	mmr = mmiohs[index].redirect;
	n = UV3H_RH_GAM_MMIOH_REDIRECT_CONFIG0_MMR_DEPTH;
	min_pnode *= 2;				/* convert to NASID */
	max_pnode *= 2;
	max_io = lnasid = fi = li = -1;

	for (i = 0; i < n; i++) {
		union uv3h_rh_gam_mmioh_redirect_config0_mmr_u redirect;

		redirect.v = uv_read_local_mmr(mmr + i * 8);
		nasid = redirect.s3.nasid;
		if (nasid < min_pnode || max_pnode < nasid)
			nasid = -1;		/* invalid NASID */

		if (nasid == lnasid) {
			li = i;
			if (i != n-1)		/* last entry check */
				continue;
		}

		/* check if we have a cached (or last) redirect to print */
		if (lnasid != -1 || (i == n-1 && nasid != -1))  {
			unsigned long addr1, addr2;
			int f, l;

			if (lnasid == -1) {
				f = l = i;
				lnasid = nasid;
			} else {
				f = fi;
				l = li;
			}
			addr1 = (base << shift) +
				f * (unsigned long)(1 << m_io);
			addr2 = (base << shift) +
				(l + 1) * (unsigned long)(1 << m_io);
			pr_info("UV: %s[%03d..%03d] NASID 0x%04x ADDR 0x%016lx - 0x%016lx\n",
				id, fi, li, lnasid, addr1, addr2);
			if (max_io < l)
				max_io = l;
		}
		fi = li = i;
		lnasid = nasid;
	}

	pr_info("UV: %s base:0x%lx shift:%d M_IO:%d MAX_IO:%d\n",
		id, base, shift, m_io, max_io);

	if (max_io >= 0)
		map_high(id, base, shift, m_io, max_io, map_uc);
}

static __init void map_mmioh_high(int min_pnode, int max_pnode)
{
	union uvh_rh_gam_mmioh_overlay_config_mmr_u mmioh;
	unsigned long mmr, base;
	int shift, enable, m_io, n_io;

	if (is_uv3_hub()) {
		/* Map both MMIOH Regions */
		map_mmioh_high_uv3(0, min_pnode, max_pnode);
		map_mmioh_high_uv3(1, min_pnode, max_pnode);
		return;
	}

	if (is_uv1_hub()) {
		mmr = UV1H_RH_GAM_MMIOH_OVERLAY_CONFIG_MMR;
		shift = UV1H_RH_GAM_MMIOH_OVERLAY_CONFIG_MMR_BASE_SHFT;
		mmioh.v = uv_read_local_mmr(mmr);
		enable = !!mmioh.s1.enable;
		base = mmioh.s1.base;
		m_io = mmioh.s1.m_io;
		n_io = mmioh.s1.n_io;
	} else if (is_uv2_hub()) {
		mmr = UV2H_RH_GAM_MMIOH_OVERLAY_CONFIG_MMR;
		shift = UV2H_RH_GAM_MMIOH_OVERLAY_CONFIG_MMR_BASE_SHFT;
		mmioh.v = uv_read_local_mmr(mmr);
		enable = !!mmioh.s2.enable;
		base = mmioh.s2.base;
		m_io = mmioh.s2.m_io;
		n_io = mmioh.s2.n_io;
	} else
		return;

	if (enable) {
		max_pnode &= (1 << n_io) - 1;
		pr_info(
		    "UV: base:0x%lx shift:%d N_IO:%d M_IO:%d max_pnode:0x%x\n",
			base, shift, m_io, n_io, max_pnode);
		map_high("MMIOH", base, shift, m_io, max_pnode, map_uc);
	} else {
		pr_info("UV: MMIOH disabled\n");
	}
}

static __init void map_low_mmrs(void)
{
	init_extra_mapping_uc(UV_GLOBAL_MMR32_BASE, UV_GLOBAL_MMR32_SIZE);
	init_extra_mapping_uc(UV_LOCAL_MMR_BASE, UV_LOCAL_MMR_SIZE);
}

static __init void uv_rtc_init(void)
{
	long status;
	u64 ticks_per_sec;

	status = uv_bios_freq_base(BIOS_FREQ_BASE_REALTIME_CLOCK,
					&ticks_per_sec);
	if (status != BIOS_STATUS_SUCCESS || ticks_per_sec < 100000) {
		printk(KERN_WARNING
			"unable to determine platform RTC clock frequency, "
			"guessing.\n");
		/* BIOS gives wrong value for clock freq. so guess */
		sn_rtc_cycles_per_second = 1000000000000UL / 30000UL;
	} else
		sn_rtc_cycles_per_second = ticks_per_sec;
}

/*
 * percpu heartbeat timer
 */
static void uv_heartbeat(unsigned long ignored)
{
	struct timer_list *timer = &uv_hub_info->scir.timer;
	unsigned char bits = uv_hub_info->scir.state;

	/* flip heartbeat bit */
	bits ^= SCIR_CPU_HEARTBEAT;

	/* is this cpu idle? */
	if (idle_cpu(raw_smp_processor_id()))
		bits &= ~SCIR_CPU_ACTIVITY;
	else
		bits |= SCIR_CPU_ACTIVITY;

	/* update system controller interface reg */
	uv_set_scir_bits(bits);

	/* enable next timer period */
	mod_timer_pinned(timer, jiffies + SCIR_CPU_HB_INTERVAL);
}

static void uv_heartbeat_enable(int cpu)
{
	while (!uv_cpu_hub_info(cpu)->scir.enabled) {
		struct timer_list *timer = &uv_cpu_hub_info(cpu)->scir.timer;

		uv_set_cpu_scir_bits(cpu, SCIR_CPU_HEARTBEAT|SCIR_CPU_ACTIVITY);
		setup_timer(timer, uv_heartbeat, cpu);
		timer->expires = jiffies + SCIR_CPU_HB_INTERVAL;
		add_timer_on(timer, cpu);
		uv_cpu_hub_info(cpu)->scir.enabled = 1;

		/* also ensure that boot cpu is enabled */
		cpu = 0;
	}
}

#ifdef CONFIG_HOTPLUG_CPU
static void uv_heartbeat_disable(int cpu)
{
	if (uv_cpu_hub_info(cpu)->scir.enabled) {
		uv_cpu_hub_info(cpu)->scir.enabled = 0;
		del_timer(&uv_cpu_hub_info(cpu)->scir.timer);
	}
	uv_set_cpu_scir_bits(cpu, 0xff);
}

/*
 * cpu hotplug notifier
 */
static int uv_scir_cpu_notify(struct notifier_block *self, unsigned long action,
			      void *hcpu)
{
	long cpu = (long)hcpu;

	switch (action) {
	case CPU_ONLINE:
		uv_heartbeat_enable(cpu);
		break;
	case CPU_DOWN_PREPARE:
		uv_heartbeat_disable(cpu);
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

static __init void uv_scir_register_cpu_notifier(void)
{
	hotcpu_notifier(uv_scir_cpu_notify, 0);
}

#else /* !CONFIG_HOTPLUG_CPU */

static __init void uv_scir_register_cpu_notifier(void)
{
}

static __init int uv_init_heartbeat(void)
{
	int cpu;

	if (is_uv_system())
		for_each_online_cpu(cpu)
			uv_heartbeat_enable(cpu);
	return 0;
}

late_initcall(uv_init_heartbeat);

#endif /* !CONFIG_HOTPLUG_CPU */

/* Direct Legacy VGA I/O traffic to designated IOH */
int uv_set_vga_state(struct pci_dev *pdev, bool decode,
		      unsigned int command_bits, u32 flags)
{
	int domain, bus, rc;

	PR_DEVEL("devfn %x decode %d cmd %x flags %d\n",
			pdev->devfn, decode, command_bits, flags);

	if (!(flags & PCI_VGA_STATE_CHANGE_BRIDGE))
		return 0;

	if ((command_bits & PCI_COMMAND_IO) == 0)
		return 0;

	domain = pci_domain_nr(pdev->bus);
	bus = pdev->bus->number;

	rc = uv_bios_set_legacy_vga_target(decode, domain, bus);
	PR_DEVEL("vga decode %d %x:%x, rc: %d\n", decode, domain, bus, rc);

	return rc;
}

/*
 * Called on each cpu to initialize the per_cpu UV data area.
 * FIXME: hotplug not supported yet
 */
void uv_cpu_init(void)
{
	/* CPU 0 initilization will be done via uv_system_init. */
	if (!uv_blade_info)
		return;

	uv_blade_info[uv_numa_blade_id()].nr_online_cpus++;

	if (get_uv_system_type() == UV_NON_UNIQUE_APIC)
		set_x2apic_extra_bits(uv_hub_info->pnode);
}

void __init uv_system_init(void)
{
	union uvh_rh_gam_config_mmr_u  m_n_config;
	union uvh_node_id_u node_id;
	unsigned long gnode_upper, lowmem_redir_base, lowmem_redir_size;
	int bytes, nid, cpu, lcpu, pnode, blade, i, j, m_val, n_val;
	int gnode_extra, min_pnode = 999999, max_pnode = -1;
	unsigned long mmr_base, present, paddr;
	unsigned short pnode_mask;
	unsigned char n_lshift;
	char *hub = (is_uv1_hub() ? "UV100/1000" :
		    (is_uv2_hub() ? "UV2000/3000" :
		    (is_uv3_hub() ? "UV300" : NULL)));

	if (!hub) {
		pr_err("UV: Unknown/unsupported UV hub\n");
		return;
	}
	pr_info("UV: Found %s hub\n", hub);
	map_low_mmrs();

	m_n_config.v = uv_read_local_mmr(UVH_RH_GAM_CONFIG_MMR );
	m_val = m_n_config.s.m_skt;
	n_val = m_n_config.s.n_skt;
	pnode_mask = (1 << n_val) - 1;
	n_lshift = get_n_lshift(m_val);
	mmr_base =
	    uv_read_local_mmr(UVH_RH_GAM_MMR_OVERLAY_CONFIG_MMR) &
	    ~UV_MMR_ENABLE;

	node_id.v = uv_read_local_mmr(UVH_NODE_ID);
	gnode_extra = (node_id.s.node_id & ~((1 << n_val) - 1)) >> 1;
	gnode_upper = ((unsigned long)gnode_extra  << m_val);
	pr_info("UV: N:%d M:%d pnode_mask:0x%x gnode_upper/extra:0x%lx/0x%x n_lshift 0x%x\n",
			n_val, m_val, pnode_mask, gnode_upper, gnode_extra,
			n_lshift);

	pr_info("UV: global MMR base 0x%lx\n", mmr_base);

	for(i = 0; i < UVH_NODE_PRESENT_TABLE_DEPTH; i++)
		uv_possible_blades +=
		  hweight64(uv_read_local_mmr( UVH_NODE_PRESENT_TABLE + i * 8));

	/* uv_num_possible_blades() is really the hub count */
	pr_info("UV: Found %d blades, %d hubs\n",
			is_uv1_hub() ? uv_num_possible_blades() :
			(uv_num_possible_blades() + 1) / 2,
			uv_num_possible_blades());

	bytes = sizeof(struct uv_blade_info) * uv_num_possible_blades();
	uv_blade_info = kzalloc(bytes, GFP_KERNEL);
	BUG_ON(!uv_blade_info);

	for (blade = 0; blade < uv_num_possible_blades(); blade++)
		uv_blade_info[blade].memory_nid = -1;

	get_lowmem_redirect(&lowmem_redir_base, &lowmem_redir_size);

	bytes = sizeof(uv_node_to_blade[0]) * num_possible_nodes();
	uv_node_to_blade = kmalloc(bytes, GFP_KERNEL);
	BUG_ON(!uv_node_to_blade);
	memset(uv_node_to_blade, 255, bytes);

	bytes = sizeof(uv_cpu_to_blade[0]) * num_possible_cpus();
	uv_cpu_to_blade = kmalloc(bytes, GFP_KERNEL);
	BUG_ON(!uv_cpu_to_blade);
	memset(uv_cpu_to_blade, 255, bytes);

	blade = 0;
	for (i = 0; i < UVH_NODE_PRESENT_TABLE_DEPTH; i++) {
		present = uv_read_local_mmr(UVH_NODE_PRESENT_TABLE + i * 8);
		for (j = 0; j < 64; j++) {
			if (!test_bit(j, &present))
				continue;
			pnode = (i * 64 + j) & pnode_mask;
			uv_blade_info[blade].pnode = pnode;
			uv_blade_info[blade].nr_possible_cpus = 0;
			uv_blade_info[blade].nr_online_cpus = 0;
			spin_lock_init(&uv_blade_info[blade].nmi_lock);
			min_pnode = min(pnode, min_pnode);
			max_pnode = max(pnode, max_pnode);
			blade++;
		}
	}

	uv_bios_init();
	uv_bios_get_sn_info(0, &uv_type, &sn_partition_id, &sn_coherency_id,
			    &sn_region_size, &system_serial_number);
	uv_rtc_init();

	for_each_present_cpu(cpu) {
		int apicid = per_cpu(x86_cpu_to_apicid, cpu);

		nid = cpu_to_node(cpu);
		/*
		 * apic_pnode_shift must be set before calling uv_apicid_to_pnode();
		 */
		uv_cpu_hub_info(cpu)->pnode_mask = pnode_mask;
		uv_cpu_hub_info(cpu)->apic_pnode_shift = uvh_apicid.s.pnode_shift;
		uv_cpu_hub_info(cpu)->hub_revision = uv_hub_info->hub_revision;

		uv_cpu_hub_info(cpu)->m_shift = 64 - m_val;
		uv_cpu_hub_info(cpu)->n_lshift = n_lshift;

		pnode = uv_apicid_to_pnode(apicid);
		blade = boot_pnode_to_blade(pnode);
		lcpu = uv_blade_info[blade].nr_possible_cpus;
		uv_blade_info[blade].nr_possible_cpus++;

		/* Any node on the blade, else will contain -1. */
		uv_blade_info[blade].memory_nid = nid;

		uv_cpu_hub_info(cpu)->lowmem_remap_base = lowmem_redir_base;
		uv_cpu_hub_info(cpu)->lowmem_remap_top = lowmem_redir_size;
		uv_cpu_hub_info(cpu)->m_val = m_val;
		uv_cpu_hub_info(cpu)->n_val = n_val;
		uv_cpu_hub_info(cpu)->numa_blade_id = blade;
		uv_cpu_hub_info(cpu)->blade_processor_id = lcpu;
		uv_cpu_hub_info(cpu)->pnode = pnode;
		uv_cpu_hub_info(cpu)->gpa_mask = (1UL << (m_val + n_val)) - 1;
		uv_cpu_hub_info(cpu)->gnode_upper = gnode_upper;
		uv_cpu_hub_info(cpu)->gnode_extra = gnode_extra;
		uv_cpu_hub_info(cpu)->global_mmr_base = mmr_base;
		uv_cpu_hub_info(cpu)->coherency_domain_number = sn_coherency_id;
		uv_cpu_hub_info(cpu)->scir.offset = uv_scir_offset(apicid);
		uv_node_to_blade[nid] = blade;
		uv_cpu_to_blade[cpu] = blade;
	}

	/* Add blade/pnode info for nodes without cpus */
	for_each_online_node(nid) {
		if (uv_node_to_blade[nid] >= 0)
			continue;
		paddr = node_start_pfn(nid) << PAGE_SHIFT;
		pnode = uv_gpa_to_pnode(uv_soc_phys_ram_to_gpa(paddr));
		blade = boot_pnode_to_blade(pnode);
		uv_node_to_blade[nid] = blade;
	}

	map_gru_high(max_pnode);
	map_mmr_high(max_pnode);
	map_mmioh_high(min_pnode, max_pnode);

	uv_nmi_setup();
	uv_cpu_init();
	uv_scir_register_cpu_notifier();
	proc_mkdir("sgi_uv", NULL);

	/* register Legacy VGA I/O redirection handler */
	pci_register_set_vga_state(uv_set_vga_state);

	/*
	 * For a kdump kernel the reset must be BOOT_ACPI, not BOOT_EFI, as
	 * EFI is not enabled in the kdump kernel.
	 */
	if (is_kdump_kernel())
		reboot_type = BOOT_ACPI;
}

apic_driver(apic_x2apic_uv_x);
