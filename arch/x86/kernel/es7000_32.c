/*
 * Written by: Garry Forsgren, Unisys Corporation
 *             Natalie Protasevich, Unisys Corporation
 * This file contains the code to configure and interface
 * with Unisys ES7000 series hardware system manager.
 *
 * Copyright (c) 2003 Unisys Corporation.  All Rights Reserved.
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

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/acpi.h>
#include <asm/io.h>
#include <asm/nmi.h>
#include <asm/smp.h>
#include <asm/atomic.h>
#include <asm/apicdef.h>
#include <asm/apic.h>
#include <asm/setup.h>

/*
 * ES7000 chipsets
 */

#define NON_UNISYS		0
#define ES7000_CLASSIC		1
#define ES7000_ZORRO		2


#define	MIP_REG			1
#define	MIP_PSAI_REG		4

#define	MIP_BUSY		1
#define	MIP_SPIN		0xf0000
#define	MIP_VALID		0x0100000000000000ULL
#define	MIP_PORT(VALUE)	((VALUE >> 32) & 0xffff)

#define	MIP_RD_LO(VALUE)	(VALUE & 0xffffffff)

struct mip_reg_info {
	unsigned long long mip_info;
	unsigned long long delivery_info;
	unsigned long long host_reg;
	unsigned long long mip_reg;
};

struct part_info {
	unsigned char type;
	unsigned char length;
	unsigned char part_id;
	unsigned char apic_mode;
	unsigned long snum;
	char ptype[16];
	char sname[64];
	char pname[64];
};

struct psai {
	unsigned long long entry_type;
	unsigned long long addr;
	unsigned long long bep_addr;
};

struct es7000_mem_info {
	unsigned char type;
	unsigned char length;
	unsigned char resv[6];
	unsigned long long  start;
	unsigned long long  size;
};

struct es7000_oem_table {
	unsigned long long hdr;
	struct mip_reg_info mip;
	struct part_info pif;
	struct es7000_mem_info shm;
	struct psai psai;
};

#ifdef CONFIG_ACPI

struct oem_table {
	struct acpi_table_header Header;
	u32 OEMTableAddr;
	u32 OEMTableSize;
};

extern int find_unisys_acpi_oem_table(unsigned long *oem_addr);
extern void unmap_unisys_acpi_oem_table(unsigned long oem_addr);
#endif

struct mip_reg {
	unsigned long long off_0;
	unsigned long long off_8;
	unsigned long long off_10;
	unsigned long long off_18;
	unsigned long long off_20;
	unsigned long long off_28;
	unsigned long long off_30;
	unsigned long long off_38;
};

#define	MIP_SW_APIC		0x1020b
#define	MIP_FUNC(VALUE)		(VALUE & 0xff)

/*
 * ES7000 Globals
 */

static volatile unsigned long	*psai = NULL;
static struct mip_reg		*mip_reg;
static struct mip_reg		*host_reg;
static int 			mip_port;
static unsigned long		mip_addr, host_addr;

int es7000_plat;

/*
 * GSI override for ES7000 platforms.
 */

static unsigned int base;

static int
es7000_rename_gsi(int ioapic, int gsi)
{
	if (es7000_plat == ES7000_ZORRO)
		return gsi;

	if (!base) {
		int i;
		for (i = 0; i < nr_ioapics; i++)
			base += nr_ioapic_registers[i];
	}

	if (!ioapic && (gsi < 16))
		gsi += base;
	return gsi;
}

static int wakeup_secondary_cpu_via_mip(int cpu, unsigned long eip)
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

static int __init es7000_update_genapic(void)
{
	apic->wakeup_cpu = wakeup_secondary_cpu_via_mip;

	/* MPENTIUMIII */
	if (boot_cpu_data.x86 == 6 &&
	    (boot_cpu_data.x86_model >= 7 || boot_cpu_data.x86_model <= 11)) {
		es7000_update_genapic_to_cluster();
		apic->wait_for_init_deassert = NULL;
		apic->wakeup_cpu = wakeup_secondary_cpu_via_mip;
	}

	return 0;
}

void __init
setup_unisys(void)
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
	ioapic_renumber_irq = es7000_rename_gsi;

	x86_quirks->update_genapic = es7000_update_genapic;
}

/*
 * Parse the OEM Table
 */

int __init
parse_unisys_oem (char *oemptr)
{
	int                     i;
	int 			success = 0;
	unsigned char           type, size;
	unsigned long           val;
	char                    *tp = NULL;
	struct psai             *psaip = NULL;
	struct mip_reg_info 	*mi;
	struct mip_reg		*host, *mip;

	tp = oemptr;

	tp += 8;

	for (i=0; i <= 6; i++) {
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
			pr_debug("es7000_mipcfg: host_reg = 0x%lx \n",
				 (unsigned long)host_reg);
			pr_debug("es7000_mipcfg: mip_reg = 0x%lx \n",
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

	if (success < 2) {
		es7000_plat = NON_UNISYS;
	} else
		setup_unisys();
	return es7000_plat;
}

#ifdef CONFIG_ACPI
static unsigned long oem_addrX;
static unsigned long oem_size;
int __init find_unisys_acpi_oem_table(unsigned long *oem_addr)
{
	struct acpi_table_header *header = NULL;
	int i = 0;
	acpi_size tbl_size;

	while (ACPI_SUCCESS(acpi_get_table_with_size("OEM1", i++, &header, &tbl_size))) {
		if (!memcmp((char *) &header->oem_id, "UNISYS", 6)) {
			struct oem_table *t = (struct oem_table *)header;

			oem_addrX = t->OEMTableAddr;
			oem_size = t->OEMTableSize;
			early_acpi_os_unmap_memory(header, tbl_size);

			*oem_addr = (unsigned long)__acpi_map_table(oem_addrX,
								    oem_size);
			return 0;
		}
		early_acpi_os_unmap_memory(header, tbl_size);
	}
	return -1;
}

void __init unmap_unisys_acpi_oem_table(unsigned long oem_addr)
{
	if (!oem_addr)
		return;

	__acpi_unmap_table((char *)oem_addr, oem_size);
}
#endif

static void
es7000_spin(int n)
{
	int i = 0;

	while (i++ < n)
		rep_nop();
}

static int __init
es7000_mip_write(struct mip_reg *mip_reg)
{
	int			status = 0;
	int			spin;

	spin = MIP_SPIN;
	while (((unsigned long long)host_reg->off_38 &
		(unsigned long long)MIP_VALID) != 0) {
			if (--spin <= 0) {
				printk("es7000_mip_write: Timeout waiting for Host Valid Flag");
				return -1;
			}
		es7000_spin(MIP_SPIN);
	}

	memcpy(host_reg, mip_reg, sizeof(struct mip_reg));
	outb(1, mip_port);

	spin = MIP_SPIN;

	while (((unsigned long long)mip_reg->off_38 &
		(unsigned long long)MIP_VALID) == 0) {
		if (--spin <= 0) {
			printk("es7000_mip_write: Timeout waiting for MIP Valid Flag");
			return -1;
		}
		es7000_spin(MIP_SPIN);
	}

	status = ((unsigned long long)mip_reg->off_0 &
		(unsigned long long)0xffff0000000000ULL) >> 48;
	mip_reg->off_38 = ((unsigned long long)mip_reg->off_38 &
		(unsigned long long)~MIP_VALID);
	return status;
}

void __init es7000_enable_apic_mode(void)
{
	struct mip_reg es7000_mip_reg;
	int mip_status;

	if (!es7000_plat)
		return;

	printk("ES7000: Enabling APIC mode.\n");
       	memset(&es7000_mip_reg, 0, sizeof(struct mip_reg));
       	es7000_mip_reg.off_0 = MIP_SW_APIC;
       	es7000_mip_reg.off_38 = MIP_VALID;

       	while ((mip_status = es7000_mip_write(&es7000_mip_reg)) != 0) {
		printk("es7000_enable_apic_mode: command failed, status = %x\n",
			mip_status);
	}
}

/*
 * APIC driver for the Unisys ES7000 chipset.
 */
#define APIC_DEFINITION 1
#include <linux/threads.h>
#include <linux/cpumask.h>
#include <asm/mpspec.h>
#include <asm/fixmap.h>
#include <asm/apicdef.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/acpi.h>
#include <linux/smp.h>
#include <asm/ipi.h>

#define APIC_DFR_VALUE_CLUSTER		(APIC_DFR_CLUSTER)
#define INT_DELIVERY_MODE_CLUSTER	(dest_LowestPrio)
#define INT_DEST_MODE_CLUSTER		(1) /* logical delivery broadcast to all procs */

#define APIC_DFR_VALUE			(APIC_DFR_FLAT)

extern void es7000_enable_apic_mode(void);
extern int apic_version [MAX_APICS];
extern u8 cpu_2_logical_apicid[];
extern unsigned int boot_cpu_physical_apicid;

extern int parse_unisys_oem (char *oemptr);
extern int find_unisys_acpi_oem_table(unsigned long *oem_addr);
extern void unmap_unisys_acpi_oem_table(unsigned long oem_addr);
extern void setup_unisys(void);

#define apicid_cluster(apicid)		(apicid & 0xF0)
#define xapic_phys_to_log_apicid(cpu)	per_cpu(x86_bios_cpu_apicid, cpu)

static void es7000_vector_allocation_domain(int cpu, cpumask_t *retmask)
{
	/* Careful. Some cpus do not strictly honor the set of cpus
	 * specified in the interrupt destination when using lowest
	 * priority interrupt delivery mode.
	 *
	 * In particular there was a hyperthreading cpu observed to
	 * deliver interrupts to the wrong hyperthread when only one
	 * hyperthread was specified in the interrupt desitination.
	 */
	*retmask = (cpumask_t){ { [0] = APIC_ALL_CPUS, } };
}


static void es7000_wait_for_init_deassert(atomic_t *deassert)
{
#ifndef CONFIG_ES7000_CLUSTERED_APIC
	while (!atomic_read(deassert))
		cpu_relax();
#endif
	return;
}

static unsigned int es7000_get_apic_id(unsigned long x)
{
	return (x >> 24) & 0xFF;
}

#ifdef CONFIG_ACPI
static int es7000_check_dsdt(void)
{
	struct acpi_table_header header;

	if (ACPI_SUCCESS(acpi_get_table_header(ACPI_SIG_DSDT, 0, &header)) &&
	    !strncmp(header.oem_id, "UNISYS", 6))
		return 1;
	return 0;
}
#endif

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

static const cpumask_t *target_cpus_cluster(void)
{
	return &CPU_MASK_ALL;
}

static const cpumask_t *es7000_target_cpus(void)
{
	return &cpumask_of_cpu(smp_processor_id());
}

static unsigned long
es7000_check_apicid_used(physid_mask_t bitmap, int apicid)
{
	return 0;
}
static unsigned long es7000_check_apicid_present(int bit)
{
	return physid_isset(bit, phys_cpu_present_map);
}

static unsigned long calculate_ldr(int cpu)
{
	unsigned long id = xapic_phys_to_log_apicid(cpu);

	return (SET_APIC_LOGICAL_ID(id));
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

	apic_write(APIC_DFR, APIC_DFR_VALUE_CLUSTER);
	val = calculate_ldr(cpu);
	apic_write(APIC_LDR, val);
}

static void es7000_init_apic_ldr(void)
{
	unsigned long val;
	int cpu = smp_processor_id();

	apic_write(APIC_DFR, APIC_DFR_VALUE);
	val = calculate_ldr(cpu);
	apic_write(APIC_LDR, val);
}

static void es7000_setup_apic_routing(void)
{
	int apic = per_cpu(x86_bios_cpu_apicid, smp_processor_id());
	printk("Enabling APIC mode:  %s. Using %d I/O APICs, target cpus %lx\n",
		(apic_version[apic] == 0x14) ?
			"Physical Cluster" : "Logical Cluster",
			nr_ioapics, cpus_addr(*es7000_target_cpus())[0]);
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
		return (int) per_cpu(x86_bios_cpu_apicid, mps_cpu);
	else
		return BAD_APICID;
}

static physid_mask_t es7000_apicid_to_cpu_present(int phys_apicid)
{
	static int id = 0;
	physid_mask_t mask;

	mask = physid_mask_of_physid(id);
	++id;

	return mask;
}

/* Mapping from cpu number to logical apicid */
static int es7000_cpu_to_logical_apicid(int cpu)
{
#ifdef CONFIG_SMP
	if (cpu >= nr_cpu_ids)
		return BAD_APICID;
	return (int)cpu_2_logical_apicid[cpu];
#else
	return logical_smp_processor_id();
#endif
}

static physid_mask_t es7000_ioapic_phys_id_map(physid_mask_t phys_map)
{
	/* For clustered we don't have a good way to do this yet - hack */
	return physids_promote(0xff);
}

static int es7000_check_phys_apicid_present(int cpu_physical_apicid)
{
	boot_cpu_physical_apicid = read_apic_id();
	return (1);
}

static unsigned int
es7000_cpu_mask_to_apicid_cluster(const struct cpumask *cpumask)
{
	int cpus_found = 0;
	int num_bits_set;
	int apicid;
	int cpu;

	num_bits_set = cpumask_weight(cpumask);
	/* Return id to all */
	if (num_bits_set == nr_cpu_ids)
		return 0xFF;
	/*
	 * The cpus in the mask must all be on the apic cluster.  If are not
	 * on the same apicid cluster return default value of target_cpus():
	 */
	cpu = cpumask_first(cpumask);
	apicid = es7000_cpu_to_logical_apicid(cpu);

	while (cpus_found < num_bits_set) {
		if (cpumask_test_cpu(cpu, cpumask)) {
			int new_apicid = es7000_cpu_to_logical_apicid(cpu);

			if (apicid_cluster(apicid) !=
					apicid_cluster(new_apicid)) {
				printk ("%s: Not a valid mask!\n", __func__);

				return 0xFF;
			}
			apicid = new_apicid;
			cpus_found++;
		}
		cpu++;
	}
	return apicid;
}

static unsigned int es7000_cpu_mask_to_apicid(const cpumask_t *cpumask)
{
	int cpus_found = 0;
	int num_bits_set;
	int apicid;
	int cpu;

	num_bits_set = cpus_weight(*cpumask);
	/* Return id to all */
	if (num_bits_set == nr_cpu_ids)
		return es7000_cpu_to_logical_apicid(0);
	/*
	 * The cpus in the mask must all be on the apic cluster.  If are not
	 * on the same apicid cluster return default value of target_cpus():
	 */
	cpu = first_cpu(*cpumask);
	apicid = es7000_cpu_to_logical_apicid(cpu);
	while (cpus_found < num_bits_set) {
		if (cpu_isset(cpu, *cpumask)) {
			int new_apicid = es7000_cpu_to_logical_apicid(cpu);

			if (apicid_cluster(apicid) !=
					apicid_cluster(new_apicid)) {
				printk ("%s: Not a valid mask!\n", __func__);

				return es7000_cpu_to_logical_apicid(0);
			}
			apicid = new_apicid;
			cpus_found++;
		}
		cpu++;
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

void __init es7000_update_genapic_to_cluster(void)
{
	apic->target_cpus = target_cpus_cluster;
	apic->irq_delivery_mode = INT_DELIVERY_MODE_CLUSTER;
	apic->irq_dest_mode = INT_DEST_MODE_CLUSTER;

	apic->init_apic_ldr = es7000_init_apic_ldr_cluster;

	apic->cpu_mask_to_apicid = es7000_cpu_mask_to_apicid_cluster;
}

static int probe_es7000(void)
{
	/* probed later in mptable/ACPI hooks */
	return 0;
}

static __init int
es7000_mps_oem_check(struct mpc_table *mpc, char *oem, char *productid)
{
	if (mpc->oemptr) {
		struct mpc_oemtable *oem_table =
			(struct mpc_oemtable *)mpc->oemptr;

		if (!strncmp(oem, "UNISYS", 6))
			return parse_unisys_oem((char *)oem_table);
	}
	return 0;
}

#ifdef CONFIG_ACPI
/* Hook from generic ACPI tables.c */
static int __init es7000_acpi_madt_oem_check(char *oem_id, char *oem_table_id)
{
	unsigned long oem_addr = 0;
	int check_dsdt;
	int ret = 0;

	/* check dsdt at first to avoid clear fix_map for oem_addr */
	check_dsdt = es7000_check_dsdt();

	if (!find_unisys_acpi_oem_table(&oem_addr)) {
		if (check_dsdt)
			ret = parse_unisys_oem((char *)oem_addr);
		else {
			setup_unisys();
			ret = 1;
		}
		/*
		 * we need to unmap it
		 */
		unmap_unisys_acpi_oem_table(oem_addr);
	}
	return ret;
}
#else
static int __init es7000_acpi_madt_oem_check(char *oem_id, char *oem_table_id)
{
	return 0;
}
#endif


struct genapic apic_es7000 = {

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

	.wakeup_cpu			= NULL,

	.trampoline_phys_low		= 0x467,
	.trampoline_phys_high		= 0x469,

	.wait_for_init_deassert		= es7000_wait_for_init_deassert,

	/* Nothing to do for most platforms, since cleared by the INIT cycle: */
	.smp_callin_clear_local_apic	= NULL,
	.store_NMI_vector		= NULL,
	.inquire_remote_apic		= default_inquire_remote_apic,

	.read				= native_apic_mem_read,
	.write				= native_apic_mem_write,
	.icr_read			= native_apic_icr_read,
	.icr_write			= native_apic_icr_write,
	.wait_icr_idle			= native_apic_wait_icr_idle,
	.safe_wait_icr_idle		= native_safe_apic_wait_icr_idle,
};
