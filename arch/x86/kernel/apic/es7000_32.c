/*
 * Written by: Garry Forsgren, Unisys Corporation
 *             Natalie Protasevich, Unisys Corporation
 *
 * This file contains the code to configure and interface
 * with Unisys ES7000 series hardware system manager.
 *
 * Copyright (c) 2003 Unisys Corporation.
 * Copyright (C) 2009, Red Hat, Inc., Ingo Molnar
 *
 *   All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Unisys Corporation, Township Line & Union Meeting
 * Roads-A, Unisys Way, Blue Bell, Pennsylvania, 19424, or:
 *
 * http://www.unisys.com
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/notifier.h>
#include <linux/spinlock.h>
#include <linux/cpumask.h>
#include <linux/threads.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/acpi.h>
#include <linux/init.h>
#include <linux/gfp.h>
#include <linux/nmi.h>
#include <linux/smp.h>
#include <linux/io.h>

#include <asm/apicdef.h>
#include <asm/atomic.h>
#include <asm/fixmap.h>
#include <asm/mpspec.h>
#include <asm/setup.h>
#include <asm/apic.h>
#include <asm/ipi.h>

/*
 * ES7000 chipsets
 */

#define NON_UNISYS			0
#define ES7000_CLASSIC			1
#define ES7000_ZORRO			2

#define	MIP_REG				1
#define	MIP_PSAI_REG			4

#define	MIP_BUSY			1
#define	MIP_SPIN			0xf0000
#define	MIP_VALID			0x0100000000000000ULL
#define	MIP_SW_APIC			0x1020b

#define	MIP_PORT(val)			((val >> 32) & 0xffff)

#define	MIP_RD_LO(val)			(val & 0xffffffff)

struct mip_reg {
	unsigned long long		off_0x00;
	unsigned long long		off_0x08;
	unsigned long long		off_0x10;
	unsigned long long		off_0x18;
	unsigned long long		off_0x20;
	unsigned long long		off_0x28;
	unsigned long long		off_0x30;
	unsigned long long		off_0x38;
};

struct mip_reg_info {
	unsigned long long		mip_info;
	unsigned long long		delivery_info;
	unsigned long long		host_reg;
	unsigned long long		mip_reg;
};

struct psai {
	unsigned long long		entry_type;
	unsigned long long		addr;
	unsigned long long		bep_addr;
};

#ifdef CONFIG_ACPI

struct es7000_oem_table {
	struct acpi_table_header	Header;
	u32				OEMTableAddr;
	u32				OEMTableSize;
};

static unsigned long			oem_addrX;
static unsigned long			oem_size;

#endif

/*
 * ES7000 Globals
 */

static volatile unsigned long		*psai;
static struct mip_reg			*mip_reg;
static struct mip_reg			*host_reg;
static int 				mip_port;
static unsigned long			mip_addr;
static unsigned long			host_addr;

int					es7000_plat;

/*
 * GSI override for ES7000 platforms.
 */


static int __cpuinit wakeup_secondary_cpu_via_mip(int cpu, unsigned long eip)
{
	unsigned long vect = 0, psaival = 0;

	if (psai == NULL)
		return -1;

	vect = ((unsigned long)__pa(eip)/0x1000) << 16;
	psaival = (0x1000000 | vect | cpu);

	while (*psai & 0x1000000)
		;

	*psai = psaival;

	return 0;
}

static int es7000_apic_is_cluster(void)
{
	/* MPENTIUMIII */
	if (boot_cpu_data.x86 == 6 &&
	    (boot_cpu_data.x86_model >= 7 && boot_cpu_data.x86_model <= 11))
		return 1;

	return 0;
}

static void setup_unisys(void)
{
	/*
	 * Determine the generation of the ES7000 currently running.
	 *
	 * es7000_plat = 1 if the machine is a 5xx ES7000 box
	 * es7000_plat = 2 if the machine is a x86_64 ES7000 box
	 *
	 */
	if (!(boot_cpu_data.x86 <= 15 && boot_cpu_data.x86_model <= 2))
		es7000_plat = ES7000_ZORRO;
	else
		es7000_plat = ES7000_CLASSIC;
}

/*
 * Parse the OEM Table:
 */
static int parse_unisys_oem(char *oemptr)
{
	int			i;
	int 			success = 0;
	unsigned char		type, size;
	unsigned long		val;
	char			*tp = NULL;
	struct psai		*psaip = NULL;
	struct mip_reg_info 	*mi;
	struct mip_reg		*host, *mip;

	tp = oemptr;

	tp += 8;

	for (i = 0; i <= 6; i++) {
		type = *tp++;
		size = *tp++;
		tp -= 2;
		switch (type) {
		case MIP_REG:
			mi = (struct mip_reg_info *)tp;
			val = MIP_RD_LO(mi->host_reg);
			host_addr = val;
			host = (struct mip_reg *)val;
			host_reg = __va(host);
			val = MIP_RD_LO(mi->mip_reg);
			mip_port = MIP_PORT(mi->mip_info);
			mip_addr = val;
			mip = (struct mip_reg *)val;
			mip_reg = __va(mip);
			pr_debug("host_reg = 0x%lx\n",
				 (unsigned long)host_reg);
			pr_debug("mip_reg = 0x%lx\n",
				 (unsigned long)mip_reg);
			success++;
			break;
		case MIP_PSAI_REG:
			psaip = (struct psai *)tp;
			if (tp != NULL) {
				if (psaip->addr)
					psai = __va(psaip->addr);
				else
					psai = NULL;
				success++;
			}
			break;
		default:
			break;
		}
		tp += size;
	}

	if (success < 2)
		es7000_plat = NON_UNISYS;
	else
		setup_unisys();

	return es7000_plat;
}

#ifdef CONFIG_ACPI
static int __init find_unisys_acpi_oem_table(unsigned long *oem_addr)
{
	struct acpi_table_header *header = NULL;
	struct es7000_oem_table *table;
	acpi_size tbl_size;
	acpi_status ret;
	int i = 0;

	for (;;) {
		ret = acpi_get_table_with_size("OEM1", i++, &header, &tbl_size);
		if (!ACPI_SUCCESS(ret))
			return -1;

		if (!memcmp((char *) &header->oem_id, "UNISYS", 6))
			break;

		early_acpi_os_unmap_memory(header, tbl_size);
	}

	table = (void *)header;

	oem_addrX	= table->OEMTableAddr;
	oem_size	= table->OEMTableSize;

	early_acpi_os_unmap_memory(header, tbl_size);

	*oem_addr	= (unsigned long)__acpi_map_table(oem_addrX, oem_size);

	return 0;
}

static void __init unmap_unisys_acpi_oem_table(unsigned long oem_addr)
{
	if (!oem_addr)
		return;

	__acpi_unmap_table((char *)oem_addr, oem_size);
}

static int es7000_check_dsdt(void)
{
	struct acpi_table_header header;

	if (ACPI_SUCCESS(acpi_get_table_header(ACPI_SIG_DSDT, 0, &header)) &&
	    !strncmp(header.oem_id, "UNISYS", 6))
		return 1;
	return 0;
}

static int es7000_acpi_ret;

/* Hook from generic ACPI tables.c */
static int __init es7000_acpi_madt_oem_check(char *oem_id, char *oem_table_id)
{
	unsigned long oem_addr = 0;
	int check_dsdt;
	int ret = 0;

	/* check dsdt at first to avoid clear fix_map for oem_addr */
	check_dsdt = es7000_check_dsdt();

	if (!find_unisys_acpi_oem_table(&oem_addr)) {
		if (check_dsdt) {
			ret = parse_unisys_oem((char *)oem_addr);
		} else {
			setup_unisys();
			ret = 1;
		}
		/*
		 * we need to unmap it
		 */
		unmap_unisys_acpi_oem_table(oem_addr);
	}

	es7000_acpi_ret = ret;

	return ret && !es7000_apic_is_cluster();
}

static int es7000_acpi_madt_oem_check_cluster(char *oem_id, char *oem_table_id)
{
	int ret = es7000_acpi_ret;

	return ret && es7000_apic_is_cluster();
}

#else /* !CONFIG_ACPI: */
static int es7000_acpi_madt_oem_check(char *oem_id, char *oem_table_id)
{
	return 0;
}

static int es7000_acpi_madt_oem_check_cluster(char *oem_id, char *oem_table_id)
{
	return 0;
}
#endif /* !CONFIG_ACPI */

static void es7000_spin(int n)
{
	int i = 0;

	while (i++ < n)
		rep_nop();
}

static int es7000_mip_write(struct mip_reg *mip_reg)
{
	int status = 0;
	int spin;

	spin = MIP_SPIN;
	while ((host_reg->off_0x38 & MIP_VALID) != 0) {
		if (--spin <= 0) {
			WARN(1,	"Timeout waiting for Host Valid Flag\n");
			return -1;
		}
		es7000_spin(MIP_SPIN);
	}

	memcpy(host_reg, mip_reg, sizeof(struct mip_reg));
	outb(1, mip_port);

	spin = MIP_SPIN;

	while ((mip_reg->off_0x38 & MIP_VALID) == 0) {
		if (--spin <= 0) {
			WARN(1,	"Timeout waiting for MIP Valid Flag\n");
			return -1;
		}
		es7000_spin(MIP_SPIN);
	}

	status = (mip_reg->off_0x00 & 0xffff0000000000ULL) >> 48;
	mip_reg->off_0x38 &= ~MIP_VALID;

	return status;
}

static void es7000_enable_apic_mode(void)
{
	struct mip_reg es7000_mip_reg;
	int mip_status;

	if (!es7000_plat)
		return;

	pr_info("Enabling APIC mode.\n");
	memset(&es7000_mip_reg, 0, sizeof(struct mip_reg));
	es7000_mip_reg.off_0x00 = MIP_SW_APIC;
	es7000_mip_reg.off_0x38 = MIP_VALID;

	while ((mip_status = es7000_mip_write(&es7000_mip_reg)) != 0)
		WARN(1, "Command failed, status = %x\n", mip_status);
}

static void es7000_vector_allocation_domain(int cpu, struct cpumask *retmask)
{
	/* Careful. Some cpus do not strictly honor the set of cpus
	 * specified in the interrupt destination when using lowest
	 * priority interrupt delivery mode.
	 *
	 * In particular there was a hyperthreading cpu observed to
	 * deliver interrupts to the wrong hyperthread when only one
	 * hyperthread was specified in the interrupt desitination.
	 */
	cpumask_clear(retmask);
	cpumask_bits(retmask)[0] = APIC_ALL_CPUS;
}


static void es7000_wait_for_init_deassert(atomic_t *deassert)
{
	while (!atomic_read(deassert))
		cpu_relax();
}

static unsigned int es7000_get_apic_id(unsigned long x)
{
	return (x >> 24) & 0xFF;
}

static void es7000_send_IPI_mask(const struct cpumask *mask, int vector)
{
	default_send_IPI_mask_sequence_phys(mask, vector);
}

static void es7000_send_IPI_allbutself(int vector)
{
	default_send_IPI_mask_allbutself_phys(cpu_online_mask, vector);
}

static void es7000_send_IPI_all(int vector)
{
	es7000_send_IPI_mask(cpu_online_mask, vector);
}

static int es7000_apic_id_registered(void)
{
	return 1;
}

static const struct cpumask *target_cpus_cluster(void)
{
	return cpu_all_mask;
}

static const struct cpumask *es7000_target_cpus(void)
{
	return cpumask_of(smp_processor_id());
}

static unsigned long es7000_check_apicid_used(physid_mask_t *map, int apicid)
{
	return 0;
}

static unsigned long es7000_check_apicid_present(int bit)
{
	return physid_isset(bit, phys_cpu_present_map);
}

static unsigned long calculate_ldr(int cpu)
{
	unsigned long id = per_cpu(x86_bios_cpu_apicid, cpu);

	return SET_APIC_LOGICAL_ID(id);
}

/*
 * Set up the logical destination ID.
 *
 * Intel recommends to set DFR, LdR and TPR before enabling
 * an APIC.  See e.g. "AP-388 82489DX User's Manual" (Intel
 * document number 292116).  So here it goes...
 */
static void es7000_init_apic_ldr_cluster(void)
{
	unsigned long val;
	int cpu = smp_processor_id();

	apic_write(APIC_DFR, APIC_DFR_CLUSTER);
	val = calculate_ldr(cpu);
	apic_write(APIC_LDR, val);
}

static void es7000_init_apic_ldr(void)
{
	unsigned long val;
	int cpu = smp_processor_id();

	apic_write(APIC_DFR, APIC_DFR_FLAT);
	val = calculate_ldr(cpu);
	apic_write(APIC_LDR, val);
}

static void es7000_setup_apic_routing(void)
{
	int apic = per_cpu(x86_bios_cpu_apicid, smp_processor_id());

	pr_info("Enabling APIC mode:  %s. Using %d I/O APICs, target cpus %lx\n",
		(apic_version[apic] == 0x14) ?
			"Physical Cluster" : "Logical Cluster",
		nr_ioapics, cpumask_bits(es7000_target_cpus())[0]);
}

static int es7000_apicid_to_node(int logical_apicid)
{
	return 0;
}


static int es7000_cpu_present_to_apicid(int mps_cpu)
{
	if (!mps_cpu)
		return boot_cpu_physical_apicid;
	else if (mps_cpu < nr_cpu_ids)
		return per_cpu(x86_bios_cpu_apicid, mps_cpu);
	else
		return BAD_APICID;
}

static int cpu_id;

static void es7000_apicid_to_cpu_present(int phys_apicid, physid_mask_t *retmap)
{
	physid_set_mask_of_physid(cpu_id, retmap);
	++cpu_id;
}

/* Mapping from cpu number to logical apicid */
static int es7000_cpu_to_logical_apicid(int cpu)
{
#ifdef CONFIG_SMP
	if (cpu >= nr_cpu_ids)
		return BAD_APICID;
	return cpu_2_logical_apicid[cpu];
#else
	return logical_smp_processor_id();
#endif
}

static void es7000_ioapic_phys_id_map(physid_mask_t *phys_map, physid_mask_t *retmap)
{
	/* For clustered we don't have a good way to do this yet - hack */
	physids_promote(0xFFL, retmap);
}

static int es7000_check_phys_apicid_present(int cpu_physical_apicid)
{
	boot_cpu_physical_apicid = read_apic_id();
	return 1;
}

static unsigned int es7000_cpu_mask_to_apicid(const struct cpumask *cpumask)
{
	unsigned int round = 0;
	int cpu, uninitialized_var(apicid);

	/*
	 * The cpus in the mask must all be on the apic cluster.
	 */
	for_each_cpu(cpu, cpumask) {
		int new_apicid = es7000_cpu_to_logical_apicid(cpu);

		if (round && APIC_CLUSTER(apicid) != APIC_CLUSTER(new_apicid)) {
			WARN(1, "Not a valid mask!");

			return BAD_APICID;
		}
		apicid = new_apicid;
		round++;
	}
	return apicid;
}

static unsigned int
es7000_cpu_mask_to_apicid_and(const struct cpumask *inmask,
			      const struct cpumask *andmask)
{
	int apicid = es7000_cpu_to_logical_apicid(0);
	cpumask_var_t cpumask;

	if (!alloc_cpumask_var(&cpumask, GFP_ATOMIC))
		return apicid;

	cpumask_and(cpumask, inmask, andmask);
	cpumask_and(cpumask, cpumask, cpu_online_mask);
	apicid = es7000_cpu_mask_to_apicid(cpumask);

	free_cpumask_var(cpumask);

	return apicid;
}

static int es7000_phys_pkg_id(int cpuid_apic, int index_msb)
{
	return cpuid_apic >> index_msb;
}

static int probe_es7000(void)
{
	/* probed later in mptable/ACPI hooks */
	return 0;
}

static int es7000_mps_ret;
static int es7000_mps_oem_check(struct mpc_table *mpc, char *oem,
		char *productid)
{
	int ret = 0;

	if (mpc->oemptr) {
		struct mpc_oemtable *oem_table =
			(struct mpc_oemtable *)mpc->oemptr;

		if (!strncmp(oem, "UNISYS", 6))
			ret = parse_unisys_oem((char *)oem_table);
	}

	es7000_mps_ret = ret;

	return ret && !es7000_apic_is_cluster();
}

static int es7000_mps_oem_check_cluster(struct mpc_table *mpc, char *oem,
		char *productid)
{
	int ret = es7000_mps_ret;

	return ret && es7000_apic_is_cluster();
}

/* We've been warned by a false positive warning.Use __refdata to keep calm. */
struct apic __refdata apic_es7000_cluster = {

	.name				= "es7000",
	.probe				= probe_es7000,
	.acpi_madt_oem_check		= es7000_acpi_madt_oem_check_cluster,
	.apic_id_registered		= es7000_apic_id_registered,

	.irq_delivery_mode		= dest_LowestPrio,
	/* logical delivery broadcast to all procs: */
	.irq_dest_mode			= 1,

	.target_cpus			= target_cpus_cluster,
	.disable_esr			= 1,
	.dest_logical			= 0,
	.check_apicid_used		= es7000_check_apicid_used,
	.check_apicid_present		= es7000_check_apicid_present,

	.vector_allocation_domain	= es7000_vector_allocation_domain,
	.init_apic_ldr			= es7000_init_apic_ldr_cluster,

	.ioapic_phys_id_map		= es7000_ioapic_phys_id_map,
	.setup_apic_routing		= es7000_setup_apic_routing,
	.multi_timer_check		= NULL,
	.apicid_to_node			= es7000_apicid_to_node,
	.cpu_to_logical_apicid		= es7000_cpu_to_logical_apicid,
	.cpu_present_to_apicid		= es7000_cpu_present_to_apicid,
	.apicid_to_cpu_present		= es7000_apicid_to_cpu_present,
	.setup_portio_remap		= NULL,
	.check_phys_apicid_present	= es7000_check_phys_apicid_present,
	.enable_apic_mode		= es7000_enable_apic_mode,
	.phys_pkg_id			= es7000_phys_pkg_id,
	.mps_oem_check			= es7000_mps_oem_check_cluster,

	.get_apic_id			= es7000_get_apic_id,
	.set_apic_id			= NULL,
	.apic_id_mask			= 0xFF << 24,

	.cpu_mask_to_apicid		= es7000_cpu_mask_to_apicid,
	.cpu_mask_to_apicid_and		= es7000_cpu_mask_to_apicid_and,

	.send_IPI_mask			= es7000_send_IPI_mask,
	.send_IPI_mask_allbutself	= NULL,
	.send_IPI_allbutself		= es7000_send_IPI_allbutself,
	.send_IPI_all			= es7000_send_IPI_all,
	.send_IPI_self			= default_send_IPI_self,

	.wakeup_secondary_cpu		= wakeup_secondary_cpu_via_mip,

	.trampoline_phys_low		= 0x467,
	.trampoline_phys_high		= 0x469,

	.wait_for_init_deassert		= NULL,

	/* Nothing to do for most platforms, since cleared by the INIT cycle: */
	.smp_callin_clear_local_apic	= NULL,
	.inquire_remote_apic		= default_inquire_remote_apic,

	.read				= native_apic_mem_read,
	.write				= native_apic_mem_write,
	.icr_read			= native_apic_icr_read,
	.icr_write			= native_apic_icr_write,
	.wait_icr_idle			= native_apic_wait_icr_idle,
	.safe_wait_icr_idle		= native_safe_apic_wait_icr_idle,
};

struct apic __refdata apic_es7000 = {

	.name				= "es7000",
	.probe				= probe_es7000,
	.acpi_madt_oem_check		= es7000_acpi_madt_oem_check,
	.apic_id_registered		= es7000_apic_id_registered,

	.irq_delivery_mode		= dest_Fixed,
	/* phys delivery to target CPUs: */
	.irq_dest_mode			= 0,

	.target_cpus			= es7000_target_cpus,
	.disable_esr			= 1,
	.dest_logical			= 0,
	.check_apicid_used		= es7000_check_apicid_used,
	.check_apicid_present		= es7000_check_apicid_present,

	.vector_allocation_domain	= es7000_vector_allocation_domain,
	.init_apic_ldr			= es7000_init_apic_ldr,

	.ioapic_phys_id_map		= es7000_ioapic_phys_id_map,
	.setup_apic_routing		= es7000_setup_apic_routing,
	.multi_timer_check		= NULL,
	.apicid_to_node			= es7000_apicid_to_node,
	.cpu_to_logical_apicid		= es7000_cpu_to_logical_apicid,
	.cpu_present_to_apicid		= es7000_cpu_present_to_apicid,
	.apicid_to_cpu_present		= es7000_apicid_to_cpu_present,
	.setup_portio_remap		= NULL,
	.check_phys_apicid_present	= es7000_check_phys_apicid_present,
	.enable_apic_mode		= es7000_enable_apic_mode,
	.phys_pkg_id			= es7000_phys_pkg_id,
	.mps_oem_check			= es7000_mps_oem_check,

	.get_apic_id			= es7000_get_apic_id,
	.set_apic_id			= NULL,
	.apic_id_mask			= 0xFF << 24,

	.cpu_mask_to_apicid		= es7000_cpu_mask_to_apicid,
	.cpu_mask_to_apicid_and		= es7000_cpu_mask_to_apicid_and,

	.send_IPI_mask			= es7000_send_IPI_mask,
	.send_IPI_mask_allbutself	= NULL,
	.send_IPI_allbutself		= es7000_send_IPI_allbutself,
	.send_IPI_all			= es7000_send_IPI_all,
	.send_IPI_self			= default_send_IPI_self,

	.trampoline_phys_low		= 0x467,
	.trampoline_phys_high		= 0x469,

	.wait_for_init_deassert		= es7000_wait_for_init_deassert,

	/* Nothing to do for most platforms, since cleared by the INIT cycle: */
	.smp_callin_clear_local_apic	= NULL,
	.inquire_remote_apic		= default_inquire_remote_apic,

	.read				= native_apic_mem_read,
	.write				= native_apic_mem_write,
	.icr_read			= native_apic_icr_read,
	.icr_write			= native_apic_icr_write,
	.wait_icr_idle			= native_apic_wait_icr_idle,
	.safe_wait_icr_idle		= native_safe_apic_wait_icr_idle,
};
