// SPDX-License-Identifier: GPL-2.0-only
#include <linux/export.h>
#include <linux/bitops.h>
#include <linux/elf.h>
#include <linux/mm.h>

#include <linux/io.h>
#include <linux/sched.h>
#include <linux/sched/clock.h>
#include <linux/random.h>
#include <linux/topology.h>
#include <asm/processor.h>
#include <asm/apic.h>
#include <asm/cacheinfo.h>
#include <asm/cpu.h>
#include <asm/spec-ctrl.h>
#include <asm/smp.h>
#include <asm/numa.h>
#include <asm/pci-direct.h>
#include <asm/delay.h>
#include <asm/debugreg.h>
#include <asm/resctrl.h>

#ifdef CONFIG_X86_64
# include <asm/mmconfig.h>
#endif

#include "cpu.h"

/*
 * nodes_per_socket: Stores the number of nodes per socket.
 * Refer to Fam15h Models 00-0fh BKDG - CPUID Fn8000_001E_ECX
 * Node Identifiers[10:8]
 */
static u32 nodes_per_socket = 1;

static inline int rdmsrl_amd_safe(unsigned msr, unsigned long long *p)
{
	u32 gprs[8] = { 0 };
	int err;

	WARN_ONCE((boot_cpu_data.x86 != 0xf),
		  "%s should only be used on K8!\n", __func__);

	gprs[1] = msr;
	gprs[7] = 0x9c5a203a;

	err = rdmsr_safe_regs(gprs);

	*p = gprs[0] | ((u64)gprs[2] << 32);

	return err;
}

static inline int wrmsrl_amd_safe(unsigned msr, unsigned long long val)
{
	u32 gprs[8] = { 0 };

	WARN_ONCE((boot_cpu_data.x86 != 0xf),
		  "%s should only be used on K8!\n", __func__);

	gprs[0] = (u32)val;
	gprs[1] = msr;
	gprs[2] = val >> 32;
	gprs[7] = 0x9c5a203a;

	return wrmsr_safe_regs(gprs);
}

/*
 *	B step AMD K6 before B 9730xxxx have hardware bugs that can cause
 *	misexecution of code under Linux. Owners of such processors should
 *	contact AMD for precise details and a CPU swap.
 *
 *	See	http://www.multimania.com/poulot/k6bug.html
 *	and	section 2.6.2 of "AMD-K6 Processor Revision Guide - Model 6"
 *		(Publication # 21266  Issue Date: August 1998)
 *
 *	The following test is erm.. interesting. AMD neglected to up
 *	the chip setting when fixing the bug but they also tweaked some
 *	performance at the same time..
 */

#ifdef CONFIG_X86_32
extern __visible void vide(void);
__asm__(".text\n"
	".globl vide\n"
	".type vide, @function\n"
	".align 4\n"
	"vide: ret\n");
#endif

static void init_amd_k5(struct cpuinfo_x86 *c)
{
#ifdef CONFIG_X86_32
/*
 * General Systems BIOSen alias the cpu frequency registers
 * of the Elan at 0x000df000. Unfortunately, one of the Linux
 * drivers subsequently pokes it, and changes the CPU speed.
 * Workaround : Remove the unneeded alias.
 */
#define CBAR		(0xfffc) /* Configuration Base Address  (32-bit) */
#define CBAR_ENB	(0x80000000)
#define CBAR_KEY	(0X000000CB)
	if (c->x86_model == 9 || c->x86_model == 10) {
		if (inl(CBAR) & CBAR_ENB)
			outl(0 | CBAR_KEY, CBAR);
	}
#endif
}

static void init_amd_k6(struct cpuinfo_x86 *c)
{
#ifdef CONFIG_X86_32
	u32 l, h;
	int mbytes = get_num_physpages() >> (20-PAGE_SHIFT);

	if (c->x86_model < 6) {
		/* Based on AMD doc 20734R - June 2000 */
		if (c->x86_model == 0) {
			clear_cpu_cap(c, X86_FEATURE_APIC);
			set_cpu_cap(c, X86_FEATURE_PGE);
		}
		return;
	}

	if (c->x86_model == 6 && c->x86_stepping == 1) {
		const int K6_BUG_LOOP = 1000000;
		int n;
		void (*f_vide)(void);
		u64 d, d2;

		pr_info("AMD K6 stepping B detected - ");

		/*
		 * It looks like AMD fixed the 2.6.2 bug and improved indirect
		 * calls at the same time.
		 */

		n = K6_BUG_LOOP;
		f_vide = vide;
		OPTIMIZER_HIDE_VAR(f_vide);
		d = rdtsc();
		while (n--)
			f_vide();
		d2 = rdtsc();
		d = d2-d;

		if (d > 20*K6_BUG_LOOP)
			pr_cont("system stability may be impaired when more than 32 MB are used.\n");
		else
			pr_cont("probably OK (after B9730xxxx).\n");
	}

	/* K6 with old style WHCR */
	if (c->x86_model < 8 ||
	   (c->x86_model == 8 && c->x86_stepping < 8)) {
		/* We can only write allocate on the low 508Mb */
		if (mbytes > 508)
			mbytes = 508;

		rdmsr(MSR_K6_WHCR, l, h);
		if ((l&0x0000FFFF) == 0) {
			unsigned long flags;
			l = (1<<0)|((mbytes/4)<<1);
			local_irq_save(flags);
			wbinvd();
			wrmsr(MSR_K6_WHCR, l, h);
			local_irq_restore(flags);
			pr_info("Enabling old style K6 write allocation for %d Mb\n",
				mbytes);
		}
		return;
	}

	if ((c->x86_model == 8 && c->x86_stepping > 7) ||
	     c->x86_model == 9 || c->x86_model == 13) {
		/* The more serious chips .. */

		if (mbytes > 4092)
			mbytes = 4092;

		rdmsr(MSR_K6_WHCR, l, h);
		if ((l&0xFFFF0000) == 0) {
			unsigned long flags;
			l = ((mbytes>>2)<<22)|(1<<16);
			local_irq_save(flags);
			wbinvd();
			wrmsr(MSR_K6_WHCR, l, h);
			local_irq_restore(flags);
			pr_info("Enabling new style K6 write allocation for %d Mb\n",
				mbytes);
		}

		return;
	}

	if (c->x86_model == 10) {
		/* AMD Geode LX is model 10 */
		/* placeholder for any needed mods */
		return;
	}
#endif
}

static void init_amd_k7(struct cpuinfo_x86 *c)
{
#ifdef CONFIG_X86_32
	u32 l, h;

	/*
	 * Bit 15 of Athlon specific MSR 15, needs to be 0
	 * to enable SSE on Palomino/Morgan/Barton CPU's.
	 * If the BIOS didn't enable it already, enable it here.
	 */
	if (c->x86_model >= 6 && c->x86_model <= 10) {
		if (!cpu_has(c, X86_FEATURE_XMM)) {
			pr_info("Enabling disabled K7/SSE Support.\n");
			msr_clear_bit(MSR_K7_HWCR, 15);
			set_cpu_cap(c, X86_FEATURE_XMM);
		}
	}

	/*
	 * It's been determined by AMD that Athlons since model 8 stepping 1
	 * are more robust with CLK_CTL set to 200xxxxx instead of 600xxxxx
	 * As per AMD technical note 27212 0.2
	 */
	if ((c->x86_model == 8 && c->x86_stepping >= 1) || (c->x86_model > 8)) {
		rdmsr(MSR_K7_CLK_CTL, l, h);
		if ((l & 0xfff00000) != 0x20000000) {
			pr_info("CPU: CLK_CTL MSR was %x. Reprogramming to %x\n",
				l, ((l & 0x000fffff)|0x20000000));
			wrmsr(MSR_K7_CLK_CTL, (l & 0x000fffff)|0x20000000, h);
		}
	}

	/* calling is from identify_secondary_cpu() ? */
	if (!c->cpu_index)
		return;

	/*
	 * Certain Athlons might work (for various values of 'work') in SMP
	 * but they are not certified as MP capable.
	 */
	/* Athlon 660/661 is valid. */
	if ((c->x86_model == 6) && ((c->x86_stepping == 0) ||
	    (c->x86_stepping == 1)))
		return;

	/* Duron 670 is valid */
	if ((c->x86_model == 7) && (c->x86_stepping == 0))
		return;

	/*
	 * Athlon 662, Duron 671, and Athlon >model 7 have capability
	 * bit. It's worth noting that the A5 stepping (662) of some
	 * Athlon XP's have the MP bit set.
	 * See http://www.heise.de/newsticker/data/jow-18.10.01-000 for
	 * more.
	 */
	if (((c->x86_model == 6) && (c->x86_stepping >= 2)) ||
	    ((c->x86_model == 7) && (c->x86_stepping >= 1)) ||
	     (c->x86_model > 7))
		if (cpu_has(c, X86_FEATURE_MP))
			return;

	/* If we get here, not a certified SMP capable AMD system. */

	/*
	 * Don't taint if we are running SMP kernel on a single non-MP
	 * approved Athlon
	 */
	WARN_ONCE(1, "WARNING: This combination of AMD"
		" processors is not suitable for SMP.\n");
	add_taint(TAINT_CPU_OUT_OF_SPEC, LOCKDEP_NOW_UNRELIABLE);
#endif
}

#ifdef CONFIG_NUMA
/*
 * To workaround broken NUMA config.  Read the comment in
 * srat_detect_node().
 */
static int nearby_node(int apicid)
{
	int i, node;

	for (i = apicid - 1; i >= 0; i--) {
		node = __apicid_to_node[i];
		if (node != NUMA_NO_NODE && node_online(node))
			return node;
	}
	for (i = apicid + 1; i < MAX_LOCAL_APIC; i++) {
		node = __apicid_to_node[i];
		if (node != NUMA_NO_NODE && node_online(node))
			return node;
	}
	return first_node(node_online_map); /* Shouldn't happen */
}
#endif

/*
 * Fix up topo::core_id for pre-F17h systems to be in the
 * [0 .. cores_per_node - 1] range. Not really needed but
 * kept so as not to break existing setups.
 */
static void legacy_fixup_core_id(struct cpuinfo_x86 *c)
{
	u32 cus_per_node;

	if (c->x86 >= 0x17)
		return;

	cus_per_node = c->x86_max_cores / nodes_per_socket;
	c->topo.core_id %= cus_per_node;
}

/*
 * Fixup core topology information for
 * (1) AMD multi-node processors
 *     Assumption: Number of cores in each internal node is the same.
 * (2) AMD processors supporting compute units
 */
static void amd_get_topology(struct cpuinfo_x86 *c)
{
	/* get information required for multi-node processors */
	if (boot_cpu_has(X86_FEATURE_TOPOEXT)) {
		int err;
		u32 eax, ebx, ecx, edx;

		cpuid(0x8000001e, &eax, &ebx, &ecx, &edx);

		c->topo.die_id  = ecx & 0xff;

		if (c->x86 == 0x15)
			c->topo.cu_id = ebx & 0xff;

		if (c->x86 >= 0x17) {
			c->topo.core_id = ebx & 0xff;

			if (smp_num_siblings > 1)
				c->x86_max_cores /= smp_num_siblings;
		}

		/*
		 * In case leaf B is available, use it to derive
		 * topology information.
		 */
		err = detect_extended_topology(c);
		if (!err)
			c->x86_coreid_bits = get_count_order(c->x86_max_cores);

		cacheinfo_amd_init_llc_id(c);

	} else if (cpu_has(c, X86_FEATURE_NODEID_MSR)) {
		u64 value;

		rdmsrl(MSR_FAM10H_NODE_ID, value);
		c->topo.die_id = value & 7;
		c->topo.llc_id = c->topo.die_id;
	} else
		return;

	if (nodes_per_socket > 1) {
		set_cpu_cap(c, X86_FEATURE_AMD_DCM);
		legacy_fixup_core_id(c);
	}
}

/*
 * On a AMD dual core setup the lower bits of the APIC id distinguish the cores.
 * Assumes number of cores is a power of two.
 */
static void amd_detect_cmp(struct cpuinfo_x86 *c)
{
	unsigned bits;

	bits = c->x86_coreid_bits;
	/* Low order bits define the core id (index of core in socket) */
	c->topo.core_id = c->topo.initial_apicid & ((1 << bits)-1);
	/* Convert the initial APIC ID into the socket ID */
	c->topo.pkg_id = c->topo.initial_apicid >> bits;
	/* use socket ID also for last level cache */
	c->topo.llc_id = c->topo.die_id = c->topo.pkg_id;
}

u32 amd_get_nodes_per_socket(void)
{
	return nodes_per_socket;
}
EXPORT_SYMBOL_GPL(amd_get_nodes_per_socket);

static void srat_detect_node(struct cpuinfo_x86 *c)
{
#ifdef CONFIG_NUMA
	int cpu = smp_processor_id();
	int node;
	unsigned apicid = c->topo.apicid;

	node = numa_cpu_node(cpu);
	if (node == NUMA_NO_NODE)
		node = per_cpu_llc_id(cpu);

	/*
	 * On multi-fabric platform (e.g. Numascale NumaChip) a
	 * platform-specific handler needs to be called to fixup some
	 * IDs of the CPU.
	 */
	if (x86_cpuinit.fixup_cpu_id)
		x86_cpuinit.fixup_cpu_id(c, node);

	if (!node_online(node)) {
		/*
		 * Two possibilities here:
		 *
		 * - The CPU is missing memory and no node was created.  In
		 *   that case try picking one from a nearby CPU.
		 *
		 * - The APIC IDs differ from the HyperTransport node IDs
		 *   which the K8 northbridge parsing fills in.  Assume
		 *   they are all increased by a constant offset, but in
		 *   the same order as the HT nodeids.  If that doesn't
		 *   result in a usable node fall back to the path for the
		 *   previous case.
		 *
		 * This workaround operates directly on the mapping between
		 * APIC ID and NUMA node, assuming certain relationship
		 * between APIC ID, HT node ID and NUMA topology.  As going
		 * through CPU mapping may alter the outcome, directly
		 * access __apicid_to_node[].
		 */
		int ht_nodeid = c->topo.initial_apicid;

		if (__apicid_to_node[ht_nodeid] != NUMA_NO_NODE)
			node = __apicid_to_node[ht_nodeid];
		/* Pick a nearby node */
		if (!node_online(node))
			node = nearby_node(apicid);
	}
	numa_set_node(cpu, node);
#endif
}

static void early_init_amd_mc(struct cpuinfo_x86 *c)
{
#ifdef CONFIG_SMP
	unsigned bits, ecx;

	/* Multi core CPU? */
	if (c->extended_cpuid_level < 0x80000008)
		return;

	ecx = cpuid_ecx(0x80000008);

	c->x86_max_cores = (ecx & 0xff) + 1;

	/* CPU telling us the core id bits shift? */
	bits = (ecx >> 12) & 0xF;

	/* Otherwise recompute */
	if (bits == 0) {
		while ((1 << bits) < c->x86_max_cores)
			bits++;
	}

	c->x86_coreid_bits = bits;
#endif
}

static void bsp_init_amd(struct cpuinfo_x86 *c)
{
	if (cpu_has(c, X86_FEATURE_CONSTANT_TSC)) {

		if (c->x86 > 0x10 ||
		    (c->x86 == 0x10 && c->x86_model >= 0x2)) {
			u64 val;

			rdmsrl(MSR_K7_HWCR, val);
			if (!(val & BIT(24)))
				pr_warn(FW_BUG "TSC doesn't count with P0 frequency!\n");
		}
	}

	if (c->x86 == 0x15) {
		unsigned long upperbit;
		u32 cpuid, assoc;

		cpuid	 = cpuid_edx(0x80000005);
		assoc	 = cpuid >> 16 & 0xff;
		upperbit = ((cpuid >> 24) << 10) / assoc;

		va_align.mask	  = (upperbit - 1) & PAGE_MASK;
		va_align.flags    = ALIGN_VA_32 | ALIGN_VA_64;

		/* A random value per boot for bit slice [12:upper_bit) */
		va_align.bits = get_random_u32() & va_align.mask;
	}

	if (cpu_has(c, X86_FEATURE_MWAITX))
		use_mwaitx_delay();

	if (boot_cpu_has(X86_FEATURE_TOPOEXT)) {
		u32 ecx;

		ecx = cpuid_ecx(0x8000001e);
		__max_die_per_package = nodes_per_socket = ((ecx >> 8) & 7) + 1;
	} else if (boot_cpu_has(X86_FEATURE_NODEID_MSR)) {
		u64 value;

		rdmsrl(MSR_FAM10H_NODE_ID, value);
		__max_die_per_package = nodes_per_socket = ((value >> 3) & 7) + 1;
	}

	if (!boot_cpu_has(X86_FEATURE_AMD_SSBD) &&
	    !boot_cpu_has(X86_FEATURE_VIRT_SSBD) &&
	    c->x86 >= 0x15 && c->x86 <= 0x17) {
		unsigned int bit;

		switch (c->x86) {
		case 0x15: bit = 54; break;
		case 0x16: bit = 33; break;
		case 0x17: bit = 10; break;
		default: return;
		}
		/*
		 * Try to cache the base value so further operations can
		 * avoid RMW. If that faults, do not enable SSBD.
		 */
		if (!rdmsrl_safe(MSR_AMD64_LS_CFG, &x86_amd_ls_cfg_base)) {
			setup_force_cpu_cap(X86_FEATURE_LS_CFG_SSBD);
			setup_force_cpu_cap(X86_FEATURE_SSBD);
			x86_amd_ls_cfg_ssbd_mask = 1ULL << bit;
		}
	}

	resctrl_cpu_detect(c);

	/* Figure out Zen generations: */
	switch (c->x86) {
	case 0x17:
		switch (c->x86_model) {
		case 0x00 ... 0x2f:
		case 0x50 ... 0x5f:
			setup_force_cpu_cap(X86_FEATURE_ZEN1);
			break;
		case 0x30 ... 0x4f:
		case 0x60 ... 0x7f:
		case 0x90 ... 0x91:
		case 0xa0 ... 0xaf:
			setup_force_cpu_cap(X86_FEATURE_ZEN2);
			break;
		default:
			goto warn;
		}
		break;

	case 0x19:
		switch (c->x86_model) {
		case 0x00 ... 0x0f:
		case 0x20 ... 0x5f:
			setup_force_cpu_cap(X86_FEATURE_ZEN3);
			break;
		case 0x10 ... 0x1f:
		case 0x60 ... 0xaf:
			setup_force_cpu_cap(X86_FEATURE_ZEN4);
			break;
		default:
			goto warn;
		}
		break;

	case 0x1a:
		switch (c->x86_model) {
		case 0x00 ... 0x0f:
		case 0x20 ... 0x2f:
		case 0x40 ... 0x4f:
		case 0x70 ... 0x7f:
			setup_force_cpu_cap(X86_FEATURE_ZEN5);
			break;
		default:
			goto warn;
		}
		break;

	default:
		break;
	}

	return;

warn:
	WARN_ONCE(1, "Family 0x%x, model: 0x%x??\n", c->x86, c->x86_model);
}

static void early_detect_mem_encrypt(struct cpuinfo_x86 *c)
{
	u64 msr;

	/*
	 * BIOS support is required for SME and SEV.
	 *   For SME: If BIOS has enabled SME then adjust x86_phys_bits by
	 *	      the SME physical address space reduction value.
	 *	      If BIOS has not enabled SME then don't advertise the
	 *	      SME feature (set in scattered.c).
	 *	      If the kernel has not enabled SME via any means then
	 *	      don't advertise the SME feature.
	 *   For SEV: If BIOS has not enabled SEV then don't advertise the
	 *            SEV and SEV_ES feature (set in scattered.c).
	 *
	 *   In all cases, since support for SME and SEV requires long mode,
	 *   don't advertise the feature under CONFIG_X86_32.
	 */
	if (cpu_has(c, X86_FEATURE_SME) || cpu_has(c, X86_FEATURE_SEV)) {
		/* Check if memory encryption is enabled */
		rdmsrl(MSR_AMD64_SYSCFG, msr);
		if (!(msr & MSR_AMD64_SYSCFG_MEM_ENCRYPT))
			goto clear_all;

		/*
		 * Always adjust physical address bits. Even though this
		 * will be a value above 32-bits this is still done for
		 * CONFIG_X86_32 so that accurate values are reported.
		 */
		c->x86_phys_bits -= (cpuid_ebx(0x8000001f) >> 6) & 0x3f;

		if (IS_ENABLED(CONFIG_X86_32))
			goto clear_all;

		if (!sme_me_mask)
			setup_clear_cpu_cap(X86_FEATURE_SME);

		rdmsrl(MSR_K7_HWCR, msr);
		if (!(msr & MSR_K7_HWCR_SMMLOCK))
			goto clear_sev;

		return;

clear_all:
		setup_clear_cpu_cap(X86_FEATURE_SME);
clear_sev:
		setup_clear_cpu_cap(X86_FEATURE_SEV);
		setup_clear_cpu_cap(X86_FEATURE_SEV_ES);
	}
}

static void early_init_amd(struct cpuinfo_x86 *c)
{
	u64 value;
	u32 dummy;

	early_init_amd_mc(c);

	if (c->x86 >= 0xf)
		set_cpu_cap(c, X86_FEATURE_K8);

	rdmsr_safe(MSR_AMD64_PATCH_LEVEL, &c->microcode, &dummy);

	/*
	 * c->x86_power is 8000_0007 edx. Bit 8 is TSC runs at constant rate
	 * with P/T states and does not stop in deep C-states
	 */
	if (c->x86_power & (1 << 8)) {
		set_cpu_cap(c, X86_FEATURE_CONSTANT_TSC);
		set_cpu_cap(c, X86_FEATURE_NONSTOP_TSC);
	}

	/* Bit 12 of 8000_0007 edx is accumulated power mechanism. */
	if (c->x86_power & BIT(12))
		set_cpu_cap(c, X86_FEATURE_ACC_POWER);

	/* Bit 14 indicates the Runtime Average Power Limit interface. */
	if (c->x86_power & BIT(14))
		set_cpu_cap(c, X86_FEATURE_RAPL);

#ifdef CONFIG_X86_64
	set_cpu_cap(c, X86_FEATURE_SYSCALL32);
#else
	/*  Set MTRR capability flag if appropriate */
	if (c->x86 == 5)
		if (c->x86_model == 13 || c->x86_model == 9 ||
		    (c->x86_model == 8 && c->x86_stepping >= 8))
			set_cpu_cap(c, X86_FEATURE_K6_MTRR);
#endif
#if defined(CONFIG_X86_LOCAL_APIC) && defined(CONFIG_PCI)
	/*
	 * ApicID can always be treated as an 8-bit value for AMD APIC versions
	 * >= 0x10, but even old K8s came out of reset with version 0x10. So, we
	 * can safely set X86_FEATURE_EXTD_APICID unconditionally for families
	 * after 16h.
	 */
	if (boot_cpu_has(X86_FEATURE_APIC)) {
		if (c->x86 > 0x16)
			set_cpu_cap(c, X86_FEATURE_EXTD_APICID);
		else if (c->x86 >= 0xf) {
			/* check CPU config space for extended APIC ID */
			unsigned int val;

			val = read_pci_config(0, 24, 0, 0x68);
			if ((val >> 17 & 0x3) == 0x3)
				set_cpu_cap(c, X86_FEATURE_EXTD_APICID);
		}
	}
#endif

	/*
	 * This is only needed to tell the kernel whether to use VMCALL
	 * and VMMCALL.  VMMCALL is never executed except under virt, so
	 * we can set it unconditionally.
	 */
	set_cpu_cap(c, X86_FEATURE_VMMCALL);

	/* F16h erratum 793, CVE-2013-6885 */
	if (c->x86 == 0x16 && c->x86_model <= 0xf)
		msr_set_bit(MSR_AMD64_LS_CFG, 15);

	early_detect_mem_encrypt(c);

	/* Re-enable TopologyExtensions if switched off by BIOS */
	if (c->x86 == 0x15 &&
	    (c->x86_model >= 0x10 && c->x86_model <= 0x6f) &&
	    !cpu_has(c, X86_FEATURE_TOPOEXT)) {

		if (msr_set_bit(0xc0011005, 54) > 0) {
			rdmsrl(0xc0011005, value);
			if (value & BIT_64(54)) {
				set_cpu_cap(c, X86_FEATURE_TOPOEXT);
				pr_info_once(FW_INFO "CPU: Re-enabling disabled Topology Extensions Support.\n");
			}
		}
	}

	if (cpu_has(c, X86_FEATURE_TOPOEXT))
		smp_num_siblings = ((cpuid_ebx(0x8000001e) >> 8) & 0xff) + 1;

	if (!cpu_has(c, X86_FEATURE_HYPERVISOR) && !cpu_has(c, X86_FEATURE_IBPB_BRTYPE)) {
		if (c->x86 == 0x17 && boot_cpu_has(X86_FEATURE_AMD_IBPB))
			setup_force_cpu_cap(X86_FEATURE_IBPB_BRTYPE);
		else if (c->x86 >= 0x19 && !wrmsrl_safe(MSR_IA32_PRED_CMD, PRED_CMD_SBPB)) {
			setup_force_cpu_cap(X86_FEATURE_IBPB_BRTYPE);
			setup_force_cpu_cap(X86_FEATURE_SBPB);
		}
	}
}

static void init_amd_k8(struct cpuinfo_x86 *c)
{
	u32 level;
	u64 value;

	/* On C+ stepping K8 rep microcode works well for copy/memset */
	level = cpuid_eax(1);
	if ((level >= 0x0f48 && level < 0x0f50) || level >= 0x0f58)
		set_cpu_cap(c, X86_FEATURE_REP_GOOD);

	/*
	 * Some BIOSes incorrectly force this feature, but only K8 revision D
	 * (model = 0x14) and later actually support it.
	 * (AMD Erratum #110, docId: 25759).
	 */
	if (c->x86_model < 0x14 && cpu_has(c, X86_FEATURE_LAHF_LM)) {
		clear_cpu_cap(c, X86_FEATURE_LAHF_LM);
		if (!rdmsrl_amd_safe(0xc001100d, &value)) {
			value &= ~BIT_64(32);
			wrmsrl_amd_safe(0xc001100d, value);
		}
	}

	if (!c->x86_model_id[0])
		strcpy(c->x86_model_id, "Hammer");

#ifdef CONFIG_SMP
	/*
	 * Disable TLB flush filter by setting HWCR.FFDIS on K8
	 * bit 6 of msr C001_0015
	 *
	 * Errata 63 for SH-B3 steppings
	 * Errata 122 for all steppings (F+ have it disabled by default)
	 */
	msr_set_bit(MSR_K7_HWCR, 6);
#endif
	set_cpu_bug(c, X86_BUG_SWAPGS_FENCE);

	/*
	 * Check models and steppings affected by erratum 400. This is
	 * used to select the proper idle routine and to enable the
	 * check whether the machine is affected in arch_post_acpi_subsys_init()
	 * which sets the X86_BUG_AMD_APIC_C1E bug depending on the MSR check.
	 */
	if (c->x86_model > 0x41 ||
	    (c->x86_model == 0x41 && c->x86_stepping >= 0x2))
		setup_force_cpu_bug(X86_BUG_AMD_E400);
}

static void init_amd_gh(struct cpuinfo_x86 *c)
{
#ifdef CONFIG_MMCONF_FAM10H
	/* do this for boot cpu */
	if (c == &boot_cpu_data)
		check_enable_amd_mmconf_dmi();

	fam10h_check_enable_mmcfg();
#endif

	/*
	 * Disable GART TLB Walk Errors on Fam10h. We do this here because this
	 * is always needed when GART is enabled, even in a kernel which has no
	 * MCE support built in. BIOS should disable GartTlbWlk Errors already.
	 * If it doesn't, we do it here as suggested by the BKDG.
	 *
	 * Fixes: https://bugzilla.kernel.org/show_bug.cgi?id=33012
	 */
	msr_set_bit(MSR_AMD64_MCx_MASK(4), 10);

	/*
	 * On family 10h BIOS may not have properly enabled WC+ support, causing
	 * it to be converted to CD memtype. This may result in performance
	 * degradation for certain nested-paging guests. Prevent this conversion
	 * by clearing bit 24 in MSR_AMD64_BU_CFG2.
	 *
	 * NOTE: we want to use the _safe accessors so as not to #GP kvm
	 * guests on older kvm hosts.
	 */
	msr_clear_bit(MSR_AMD64_BU_CFG2, 24);

	set_cpu_bug(c, X86_BUG_AMD_TLB_MMATCH);

	/*
	 * Check models and steppings affected by erratum 400. This is
	 * used to select the proper idle routine and to enable the
	 * check whether the machine is affected in arch_post_acpi_subsys_init()
	 * which sets the X86_BUG_AMD_APIC_C1E bug depending on the MSR check.
	 */
	if (c->x86_model > 0x2 ||
	    (c->x86_model == 0x2 && c->x86_stepping >= 0x1))
		setup_force_cpu_bug(X86_BUG_AMD_E400);
}

static void init_amd_ln(struct cpuinfo_x86 *c)
{
	/*
	 * Apply erratum 665 fix unconditionally so machines without a BIOS
	 * fix work.
	 */
	msr_set_bit(MSR_AMD64_DE_CFG, 31);
}

static bool rdrand_force;

static int __init rdrand_cmdline(char *str)
{
	if (!str)
		return -EINVAL;

	if (!strcmp(str, "force"))
		rdrand_force = true;
	else
		return -EINVAL;

	return 0;
}
early_param("rdrand", rdrand_cmdline);

static void clear_rdrand_cpuid_bit(struct cpuinfo_x86 *c)
{
	/*
	 * Saving of the MSR used to hide the RDRAND support during
	 * suspend/resume is done by arch/x86/power/cpu.c, which is
	 * dependent on CONFIG_PM_SLEEP.
	 */
	if (!IS_ENABLED(CONFIG_PM_SLEEP))
		return;

	/*
	 * The self-test can clear X86_FEATURE_RDRAND, so check for
	 * RDRAND support using the CPUID function directly.
	 */
	if (!(cpuid_ecx(1) & BIT(30)) || rdrand_force)
		return;

	msr_clear_bit(MSR_AMD64_CPUID_FN_1, 62);

	/*
	 * Verify that the CPUID change has occurred in case the kernel is
	 * running virtualized and the hypervisor doesn't support the MSR.
	 */
	if (cpuid_ecx(1) & BIT(30)) {
		pr_info_once("BIOS may not properly restore RDRAND after suspend, but hypervisor does not support hiding RDRAND via CPUID.\n");
		return;
	}

	clear_cpu_cap(c, X86_FEATURE_RDRAND);
	pr_info_once("BIOS may not properly restore RDRAND after suspend, hiding RDRAND via CPUID. Use rdrand=force to reenable.\n");
}

static void init_amd_jg(struct cpuinfo_x86 *c)
{
	/*
	 * Some BIOS implementations do not restore proper RDRAND support
	 * across suspend and resume. Check on whether to hide the RDRAND
	 * instruction support via CPUID.
	 */
	clear_rdrand_cpuid_bit(c);
}

static void init_amd_bd(struct cpuinfo_x86 *c)
{
	u64 value;

	/*
	 * The way access filter has a performance penalty on some workloads.
	 * Disable it on the affected CPUs.
	 */
	if ((c->x86_model >= 0x02) && (c->x86_model < 0x20)) {
		if (!rdmsrl_safe(MSR_F15H_IC_CFG, &value) && !(value & 0x1E)) {
			value |= 0x1E;
			wrmsrl_safe(MSR_F15H_IC_CFG, value);
		}
	}

	/*
	 * Some BIOS implementations do not restore proper RDRAND support
	 * across suspend and resume. Check on whether to hide the RDRAND
	 * instruction support via CPUID.
	 */
	clear_rdrand_cpuid_bit(c);
}

static void fix_erratum_1386(struct cpuinfo_x86 *c)
{
	/*
	 * Work around Erratum 1386.  The XSAVES instruction malfunctions in
	 * certain circumstances on Zen1/2 uarch, and not all parts have had
	 * updated microcode at the time of writing (March 2023).
	 *
	 * Affected parts all have no supervisor XSAVE states, meaning that
	 * the XSAVEC instruction (which works fine) is equivalent.
	 */
	clear_cpu_cap(c, X86_FEATURE_XSAVES);
}

void init_spectral_chicken(struct cpuinfo_x86 *c)
{
#ifdef CONFIG_CPU_UNRET_ENTRY
	u64 value;

	/*
	 * On Zen2 we offer this chicken (bit) on the altar of Speculation.
	 *
	 * This suppresses speculation from the middle of a basic block, i.e. it
	 * suppresses non-branch predictions.
	 */
	if (!cpu_has(c, X86_FEATURE_HYPERVISOR)) {
		if (!rdmsrl_safe(MSR_ZEN2_SPECTRAL_CHICKEN, &value)) {
			value |= MSR_ZEN2_SPECTRAL_CHICKEN_BIT;
			wrmsrl_safe(MSR_ZEN2_SPECTRAL_CHICKEN, value);
		}
	}
#endif
}

static void init_amd_zen_common(void)
{
	setup_force_cpu_cap(X86_FEATURE_ZEN);
#ifdef CONFIG_NUMA
	node_reclaim_distance = 32;
#endif
}

static void init_amd_zen1(struct cpuinfo_x86 *c)
{
	init_amd_zen_common();
	fix_erratum_1386(c);

	/* Fix up CPUID bits, but only if not virtualised. */
	if (!cpu_has(c, X86_FEATURE_HYPERVISOR)) {

		/* Erratum 1076: CPB feature bit not being set in CPUID. */
		if (!cpu_has(c, X86_FEATURE_CPB))
			set_cpu_cap(c, X86_FEATURE_CPB);
	}

	pr_notice_once("AMD Zen1 DIV0 bug detected. Disable SMT for full protection.\n");
	setup_force_cpu_bug(X86_BUG_DIV0);
}

static bool cpu_has_zenbleed_microcode(void)
{
	u32 good_rev = 0;

	switch (boot_cpu_data.x86_model) {
	case 0x30 ... 0x3f: good_rev = 0x0830107a; break;
	case 0x60 ... 0x67: good_rev = 0x0860010b; break;
	case 0x68 ... 0x6f: good_rev = 0x08608105; break;
	case 0x70 ... 0x7f: good_rev = 0x08701032; break;
	case 0xa0 ... 0xaf: good_rev = 0x08a00008; break;

	default:
		return false;
	}

	if (boot_cpu_data.microcode < good_rev)
		return false;

	return true;
}

static void zen2_zenbleed_check(struct cpuinfo_x86 *c)
{
	if (cpu_has(c, X86_FEATURE_HYPERVISOR))
		return;

	if (!cpu_has(c, X86_FEATURE_AVX))
		return;

	if (!cpu_has_zenbleed_microcode()) {
		pr_notice_once("Zenbleed: please update your microcode for the most optimal fix\n");
		msr_set_bit(MSR_AMD64_DE_CFG, MSR_AMD64_DE_CFG_ZEN2_FP_BACKUP_FIX_BIT);
	} else {
		msr_clear_bit(MSR_AMD64_DE_CFG, MSR_AMD64_DE_CFG_ZEN2_FP_BACKUP_FIX_BIT);
	}
}

static void init_amd_zen2(struct cpuinfo_x86 *c)
{
	init_amd_zen_common();
	init_spectral_chicken(c);
	fix_erratum_1386(c);
	zen2_zenbleed_check(c);
}

static void init_amd_zen3(struct cpuinfo_x86 *c)
{
	init_amd_zen_common();

	if (!cpu_has(c, X86_FEATURE_HYPERVISOR)) {
		/*
		 * Zen3 (Fam19 model < 0x10) parts are not susceptible to
		 * Branch Type Confusion, but predate the allocation of the
		 * BTC_NO bit.
		 */
		if (!cpu_has(c, X86_FEATURE_BTC_NO))
			set_cpu_cap(c, X86_FEATURE_BTC_NO);
	}
}

static void init_amd_zen4(struct cpuinfo_x86 *c)
{
	init_amd_zen_common();

	if (!cpu_has(c, X86_FEATURE_HYPERVISOR))
		msr_set_bit(MSR_ZEN4_BP_CFG, MSR_ZEN4_BP_CFG_SHARED_BTB_FIX_BIT);
}

static void init_amd_zen5(struct cpuinfo_x86 *c)
{
	init_amd_zen_common();
}

static void init_amd(struct cpuinfo_x86 *c)
{
	u64 vm_cr;

	early_init_amd(c);

	/*
	 * Bit 31 in normal CPUID used for nonstandard 3DNow ID;
	 * 3DNow is IDd by bit 31 in extended CPUID (1*32+31) anyway
	 */
	clear_cpu_cap(c, 0*32+31);

	if (c->x86 >= 0x10)
		set_cpu_cap(c, X86_FEATURE_REP_GOOD);

	/* AMD FSRM also implies FSRS */
	if (cpu_has(c, X86_FEATURE_FSRM))
		set_cpu_cap(c, X86_FEATURE_FSRS);

	/* get apicid instead of initial apic id from cpuid */
	c->topo.apicid = read_apic_id();

	/* K6s reports MCEs but don't actually have all the MSRs */
	if (c->x86 < 6)
		clear_cpu_cap(c, X86_FEATURE_MCE);

	switch (c->x86) {
	case 4:    init_amd_k5(c); break;
	case 5:    init_amd_k6(c); break;
	case 6:	   init_amd_k7(c); break;
	case 0xf:  init_amd_k8(c); break;
	case 0x10: init_amd_gh(c); break;
	case 0x12: init_amd_ln(c); break;
	case 0x15: init_amd_bd(c); break;
	case 0x16: init_amd_jg(c); break;
	}

	if (boot_cpu_has(X86_FEATURE_ZEN1))
		init_amd_zen1(c);
	else if (boot_cpu_has(X86_FEATURE_ZEN2))
		init_amd_zen2(c);
	else if (boot_cpu_has(X86_FEATURE_ZEN3))
		init_amd_zen3(c);
	else if (boot_cpu_has(X86_FEATURE_ZEN4))
		init_amd_zen4(c);
	else if (boot_cpu_has(X86_FEATURE_ZEN5))
		init_amd_zen5(c);

	/*
	 * Enable workaround for FXSAVE leak on CPUs
	 * without a XSaveErPtr feature
	 */
	if ((c->x86 >= 6) && (!cpu_has(c, X86_FEATURE_XSAVEERPTR)))
		set_cpu_bug(c, X86_BUG_FXSAVE_LEAK);

	cpu_detect_cache_sizes(c);

	amd_detect_cmp(c);
	amd_get_topology(c);
	srat_detect_node(c);

	init_amd_cacheinfo(c);

	if (cpu_has(c, X86_FEATURE_SVM)) {
		rdmsrl(MSR_VM_CR, vm_cr);
		if (vm_cr & SVM_VM_CR_SVM_DIS_MASK) {
			pr_notice_once("SVM disabled (by BIOS) in MSR_VM_CR\n");
			clear_cpu_cap(c, X86_FEATURE_SVM);
		}
	}

	if (!cpu_has(c, X86_FEATURE_LFENCE_RDTSC) && cpu_has(c, X86_FEATURE_XMM2)) {
		/*
		 * Use LFENCE for execution serialization.  On families which
		 * don't have that MSR, LFENCE is already serializing.
		 * msr_set_bit() uses the safe accessors, too, even if the MSR
		 * is not present.
		 */
		msr_set_bit(MSR_AMD64_DE_CFG,
			    MSR_AMD64_DE_CFG_LFENCE_SERIALIZE_BIT);

		/* A serializing LFENCE stops RDTSC speculation */
		set_cpu_cap(c, X86_FEATURE_LFENCE_RDTSC);
	}

	/*
	 * Family 0x12 and above processors have APIC timer
	 * running in deep C states.
	 */
	if (c->x86 > 0x11)
		set_cpu_cap(c, X86_FEATURE_ARAT);

	/* 3DNow or LM implies PREFETCHW */
	if (!cpu_has(c, X86_FEATURE_3DNOWPREFETCH))
		if (cpu_has(c, X86_FEATURE_3DNOW) || cpu_has(c, X86_FEATURE_LM))
			set_cpu_cap(c, X86_FEATURE_3DNOWPREFETCH);

	/* AMD CPUs don't reset SS attributes on SYSRET, Xen does. */
	if (!cpu_feature_enabled(X86_FEATURE_XENPV))
		set_cpu_bug(c, X86_BUG_SYSRET_SS_ATTRS);

	/*
	 * Turn on the Instructions Retired free counter on machines not
	 * susceptible to erratum #1054 "Instructions Retired Performance
	 * Counter May Be Inaccurate".
	 */
	if (cpu_has(c, X86_FEATURE_IRPERF) &&
	    (boot_cpu_has(X86_FEATURE_ZEN1) && c->x86_model > 0x2f))
		msr_set_bit(MSR_K7_HWCR, MSR_K7_HWCR_IRPERF_EN_BIT);

	check_null_seg_clears_base(c);

	/*
	 * Make sure EFER[AIBRSE - Automatic IBRS Enable] is set. The APs are brought up
	 * using the trampoline code and as part of it, MSR_EFER gets prepared there in
	 * order to be replicated onto them. Regardless, set it here again, if not set,
	 * to protect against any future refactoring/code reorganization which might
	 * miss setting this important bit.
	 */
	if (spectre_v2_in_eibrs_mode(spectre_v2_enabled) &&
	    cpu_has(c, X86_FEATURE_AUTOIBRS))
		WARN_ON_ONCE(msr_set_bit(MSR_EFER, _EFER_AUTOIBRS));

	/* AMD CPUs don't need fencing after x2APIC/TSC_DEADLINE MSR writes. */
	clear_cpu_cap(c, X86_FEATURE_APIC_MSRS_FENCE);
}

#ifdef CONFIG_X86_32
static unsigned int amd_size_cache(struct cpuinfo_x86 *c, unsigned int size)
{
	/* AMD errata T13 (order #21922) */
	if (c->x86 == 6) {
		/* Duron Rev A0 */
		if (c->x86_model == 3 && c->x86_stepping == 0)
			size = 64;
		/* Tbird rev A1/A2 */
		if (c->x86_model == 4 &&
			(c->x86_stepping == 0 || c->x86_stepping == 1))
			size = 256;
	}
	return size;
}
#endif

static void cpu_detect_tlb_amd(struct cpuinfo_x86 *c)
{
	u32 ebx, eax, ecx, edx;
	u16 mask = 0xfff;

	if (c->x86 < 0xf)
		return;

	if (c->extended_cpuid_level < 0x80000006)
		return;

	cpuid(0x80000006, &eax, &ebx, &ecx, &edx);

	tlb_lld_4k[ENTRIES] = (ebx >> 16) & mask;
	tlb_lli_4k[ENTRIES] = ebx & mask;

	/*
	 * K8 doesn't have 2M/4M entries in the L2 TLB so read out the L1 TLB
	 * characteristics from the CPUID function 0x80000005 instead.
	 */
	if (c->x86 == 0xf) {
		cpuid(0x80000005, &eax, &ebx, &ecx, &edx);
		mask = 0xff;
	}

	/* Handle DTLB 2M and 4M sizes, fall back to L1 if L2 is disabled */
	if (!((eax >> 16) & mask))
		tlb_lld_2m[ENTRIES] = (cpuid_eax(0x80000005) >> 16) & 0xff;
	else
		tlb_lld_2m[ENTRIES] = (eax >> 16) & mask;

	/* a 4M entry uses two 2M entries */
	tlb_lld_4m[ENTRIES] = tlb_lld_2m[ENTRIES] >> 1;

	/* Handle ITLB 2M and 4M sizes, fall back to L1 if L2 is disabled */
	if (!(eax & mask)) {
		/* Erratum 658 */
		if (c->x86 == 0x15 && c->x86_model <= 0x1f) {
			tlb_lli_2m[ENTRIES] = 1024;
		} else {
			cpuid(0x80000005, &eax, &ebx, &ecx, &edx);
			tlb_lli_2m[ENTRIES] = eax & 0xff;
		}
	} else
		tlb_lli_2m[ENTRIES] = eax & mask;

	tlb_lli_4m[ENTRIES] = tlb_lli_2m[ENTRIES] >> 1;
}

static const struct cpu_dev amd_cpu_dev = {
	.c_vendor	= "AMD",
	.c_ident	= { "AuthenticAMD" },
#ifdef CONFIG_X86_32
	.legacy_models = {
		{ .family = 4, .model_names =
		  {
			  [3] = "486 DX/2",
			  [7] = "486 DX/2-WB",
			  [8] = "486 DX/4",
			  [9] = "486 DX/4-WB",
			  [14] = "Am5x86-WT",
			  [15] = "Am5x86-WB"
		  }
		},
	},
	.legacy_cache_size = amd_size_cache,
#endif
	.c_early_init   = early_init_amd,
	.c_detect_tlb	= cpu_detect_tlb_amd,
	.c_bsp_init	= bsp_init_amd,
	.c_init		= init_amd,
	.c_x86_vendor	= X86_VENDOR_AMD,
};

cpu_dev_register(amd_cpu_dev);

static DEFINE_PER_CPU_READ_MOSTLY(unsigned long[4], amd_dr_addr_mask);

static unsigned int amd_msr_dr_addr_masks[] = {
	MSR_F16H_DR0_ADDR_MASK,
	MSR_F16H_DR1_ADDR_MASK,
	MSR_F16H_DR1_ADDR_MASK + 1,
	MSR_F16H_DR1_ADDR_MASK + 2
};

void amd_set_dr_addr_mask(unsigned long mask, unsigned int dr)
{
	int cpu = smp_processor_id();

	if (!cpu_feature_enabled(X86_FEATURE_BPEXT))
		return;

	if (WARN_ON_ONCE(dr >= ARRAY_SIZE(amd_msr_dr_addr_masks)))
		return;

	if (per_cpu(amd_dr_addr_mask, cpu)[dr] == mask)
		return;

	wrmsr(amd_msr_dr_addr_masks[dr], mask, 0);
	per_cpu(amd_dr_addr_mask, cpu)[dr] = mask;
}

unsigned long amd_get_dr_addr_mask(unsigned int dr)
{
	if (!cpu_feature_enabled(X86_FEATURE_BPEXT))
		return 0;

	if (WARN_ON_ONCE(dr >= ARRAY_SIZE(amd_msr_dr_addr_masks)))
		return 0;

	return per_cpu(amd_dr_addr_mask[dr], smp_processor_id());
}
EXPORT_SYMBOL_GPL(amd_get_dr_addr_mask);

u32 amd_get_highest_perf(void)
{
	struct cpuinfo_x86 *c = &boot_cpu_data;

	if (c->x86 == 0x17 && ((c->x86_model >= 0x30 && c->x86_model < 0x40) ||
			       (c->x86_model >= 0x70 && c->x86_model < 0x80)))
		return 166;

	if (c->x86 == 0x19 && ((c->x86_model >= 0x20 && c->x86_model < 0x30) ||
			       (c->x86_model >= 0x40 && c->x86_model < 0x70)))
		return 166;

	return 255;
}
EXPORT_SYMBOL_GPL(amd_get_highest_perf);

static void zenbleed_check_cpu(void *unused)
{
	struct cpuinfo_x86 *c = &cpu_data(smp_processor_id());

	zen2_zenbleed_check(c);
}

void amd_check_microcode(void)
{
	if (boot_cpu_data.x86_vendor != X86_VENDOR_AMD)
		return;

	on_each_cpu(zenbleed_check_cpu, NULL, 1);
}

/*
 * Issue a DIV 0/1 insn to clear any division data from previous DIV
 * operations.
 */
void noinstr amd_clear_divider(void)
{
	asm volatile(ALTERNATIVE("", "div %2\n\t", X86_BUG_DIV0)
		     :: "a" (0), "d" (0), "r" (1));
}
EXPORT_SYMBOL_GPL(amd_clear_divider);
