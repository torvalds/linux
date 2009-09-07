/*
 * IBM Summit-Specific Code
 *
 * Written By: Matthew Dobson, IBM Corporation
 *
 * Copyright (c) 2003 IBM Corp.
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Send feedback to <colpatch@us.ibm.com>
 *
 */

#include <linux/mm.h>
#include <linux/init.h>
#include <asm/io.h>
#include <asm/bios_ebda.h>

/*
 * APIC driver for the IBM "Summit" chipset.
 */
#include <linux/threads.h>
#include <linux/cpumask.h>
#include <asm/mpspec.h>
#include <asm/apic.h>
#include <asm/smp.h>
#include <asm/fixmap.h>
#include <asm/apicdef.h>
#include <asm/ipi.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/gfp.h>
#include <linux/smp.h>

static unsigned summit_get_apic_id(unsigned long x)
{
	return (x >> 24) & 0xFF;
}

static inline void summit_send_IPI_mask(const struct cpumask *mask, int vector)
{
	default_send_IPI_mask_sequence_logical(mask, vector);
}

static void summit_send_IPI_allbutself(int vector)
{
	default_send_IPI_mask_allbutself_logical(cpu_online_mask, vector);
}

static void summit_send_IPI_all(int vector)
{
	summit_send_IPI_mask(cpu_online_mask, vector);
}

#include <asm/tsc.h>

extern int use_cyclone;

#ifdef CONFIG_X86_SUMMIT_NUMA
static void setup_summit(void);
#else
static inline void setup_summit(void) {}
#endif

static int summit_mps_oem_check(struct mpc_table *mpc, char *oem,
		char *productid)
{
	if (!strncmp(oem, "IBM ENSW", 8) &&
			(!strncmp(productid, "VIGIL SMP", 9)
			 || !strncmp(productid, "EXA", 3)
			 || !strncmp(productid, "RUTHLESS SMP", 12))){
		mark_tsc_unstable("Summit based system");
		use_cyclone = 1; /*enable cyclone-timer*/
		setup_summit();
		return 1;
	}
	return 0;
}

/* Hook from generic ACPI tables.c */
static int summit_acpi_madt_oem_check(char *oem_id, char *oem_table_id)
{
	if (!strncmp(oem_id, "IBM", 3) &&
	    (!strncmp(oem_table_id, "SERVIGIL", 8)
	     || !strncmp(oem_table_id, "EXA", 3))){
		mark_tsc_unstable("Summit based system");
		use_cyclone = 1; /*enable cyclone-timer*/
		setup_summit();
		return 1;
	}
	return 0;
}

struct rio_table_hdr {
	unsigned char version;      /* Version number of this data structure           */
	                            /* Version 3 adds chassis_num & WP_index           */
	unsigned char num_scal_dev; /* # of Scalability devices (Twisters for Vigil)   */
	unsigned char num_rio_dev;  /* # of RIO I/O devices (Cyclones and Winnipegs)   */
} __attribute__((packed));

struct scal_detail {
	unsigned char node_id;      /* Scalability Node ID                             */
	unsigned long CBAR;         /* Address of 1MB register space                   */
	unsigned char port0node;    /* Node ID port connected to: 0xFF=None            */
	unsigned char port0port;    /* Port num port connected to: 0,1,2, or 0xFF=None */
	unsigned char port1node;    /* Node ID port connected to: 0xFF = None          */
	unsigned char port1port;    /* Port num port connected to: 0,1,2, or 0xFF=None */
	unsigned char port2node;    /* Node ID port connected to: 0xFF = None          */
	unsigned char port2port;    /* Port num port connected to: 0,1,2, or 0xFF=None */
	unsigned char chassis_num;  /* 1 based Chassis number (1 = boot node)          */
} __attribute__((packed));

struct rio_detail {
	unsigned char node_id;      /* RIO Node ID                                     */
	unsigned long BBAR;         /* Address of 1MB register space                   */
	unsigned char type;         /* Type of device                                  */
	unsigned char owner_id;     /* For WPEG: Node ID of Cyclone that owns this WPEG*/
	                            /* For CYC:  Node ID of Twister that owns this CYC */
	unsigned char port0node;    /* Node ID port connected to: 0xFF=None            */
	unsigned char port0port;    /* Port num port connected to: 0,1,2, or 0xFF=None */
	unsigned char port1node;    /* Node ID port connected to: 0xFF=None            */
	unsigned char port1port;    /* Port num port connected to: 0,1,2, or 0xFF=None */
	unsigned char first_slot;   /* For WPEG: Lowest slot number below this WPEG    */
	                            /* For CYC:  0                                     */
	unsigned char status;       /* For WPEG: Bit 0 = 1 : the XAPIC is used         */
	                            /*                 = 0 : the XAPIC is not used, ie:*/
	                            /*                     ints fwded to another XAPIC */
	                            /*           Bits1:7 Reserved                      */
	                            /* For CYC:  Bits0:7 Reserved                      */
	unsigned char WP_index;     /* For WPEG: WPEG instance index - lower ones have */
	                            /*           lower slot numbers/PCI bus numbers    */
	                            /* For CYC:  No meaning                            */
	unsigned char chassis_num;  /* 1 based Chassis number                          */
	                            /* For LookOut WPEGs this field indicates the      */
	                            /* Expansion Chassis #, enumerated from Boot       */
	                            /* Node WPEG external port, then Boot Node CYC     */
	                            /* external port, then Next Vigil chassis WPEG     */
	                            /* external port, etc.                             */
	                            /* Shared Lookouts have only 1 chassis number (the */
	                            /* first one assigned)                             */
} __attribute__((packed));


typedef enum {
	CompatTwister = 0,  /* Compatibility Twister               */
	AltTwister    = 1,  /* Alternate Twister of internal 8-way */
	CompatCyclone = 2,  /* Compatibility Cyclone               */
	AltCyclone    = 3,  /* Alternate Cyclone of internal 8-way */
	CompatWPEG    = 4,  /* Compatibility WPEG                  */
	AltWPEG       = 5,  /* Second Planar WPEG                  */
	LookOutAWPEG  = 6,  /* LookOut WPEG                        */
	LookOutBWPEG  = 7,  /* LookOut WPEG                        */
} node_type;

static inline int is_WPEG(struct rio_detail *rio){
	return (rio->type == CompatWPEG || rio->type == AltWPEG ||
		rio->type == LookOutAWPEG || rio->type == LookOutBWPEG);
}

#define SUMMIT_APIC_DFR_VALUE	(APIC_DFR_CLUSTER)

static const struct cpumask *summit_target_cpus(void)
{
	/* CPU_MASK_ALL (0xff) has undefined behaviour with
	 * dest_LowestPrio mode logical clustered apic interrupt routing
	 * Just start on cpu 0.  IRQ balancing will spread load
	 */
	return cpumask_of(0);
}

static unsigned long summit_check_apicid_used(physid_mask_t bitmap, int apicid)
{
	return 0;
}

/* we don't use the phys_cpu_present_map to indicate apicid presence */
static unsigned long summit_check_apicid_present(int bit)
{
	return 1;
}

static void summit_init_apic_ldr(void)
{
	unsigned long val, id;
	int count = 0;
	u8 my_id = (u8)hard_smp_processor_id();
	u8 my_cluster = APIC_CLUSTER(my_id);
#ifdef CONFIG_SMP
	u8 lid;
	int i;

	/* Create logical APIC IDs by counting CPUs already in cluster. */
	for (count = 0, i = nr_cpu_ids; --i >= 0; ) {
		lid = cpu_2_logical_apicid[i];
		if (lid != BAD_APICID && APIC_CLUSTER(lid) == my_cluster)
			++count;
	}
#endif
	/* We only have a 4 wide bitmap in cluster mode.  If a deranged
	 * BIOS puts 5 CPUs in one APIC cluster, we're hosed. */
	BUG_ON(count >= XAPIC_DEST_CPUS_SHIFT);
	id = my_cluster | (1UL << count);
	apic_write(APIC_DFR, SUMMIT_APIC_DFR_VALUE);
	val = apic_read(APIC_LDR) & ~APIC_LDR_MASK;
	val |= SET_APIC_LOGICAL_ID(id);
	apic_write(APIC_LDR, val);
}

static int summit_apic_id_registered(void)
{
	return 1;
}

static void summit_setup_apic_routing(void)
{
	printk("Enabling APIC mode:  Summit.  Using %d I/O APICs\n",
						nr_ioapics);
}

static int summit_apicid_to_node(int logical_apicid)
{
#ifdef CONFIG_SMP
	return apicid_2_node[hard_smp_processor_id()];
#else
	return 0;
#endif
}

/* Mapping from cpu number to logical apicid */
static inline int summit_cpu_to_logical_apicid(int cpu)
{
#ifdef CONFIG_SMP
	if (cpu >= nr_cpu_ids)
		return BAD_APICID;
	return cpu_2_logical_apicid[cpu];
#else
	return logical_smp_processor_id();
#endif
}

static int summit_cpu_present_to_apicid(int mps_cpu)
{
	if (mps_cpu < nr_cpu_ids)
		return (int)per_cpu(x86_bios_cpu_apicid, mps_cpu);
	else
		return BAD_APICID;
}

static physid_mask_t summit_ioapic_phys_id_map(physid_mask_t phys_id_map)
{
	/* For clustered we don't have a good way to do this yet - hack */
	return physids_promote(0x0F);
}

static physid_mask_t summit_apicid_to_cpu_present(int apicid)
{
	return physid_mask_of_physid(0);
}

static int summit_check_phys_apicid_present(int boot_cpu_physical_apicid)
{
	return 1;
}

static unsigned int summit_cpu_mask_to_apicid(const struct cpumask *cpumask)
{
	unsigned int round = 0;
	int cpu, apicid = 0;

	/*
	 * The cpus in the mask must all be on the apic cluster.
	 */
	for_each_cpu(cpu, cpumask) {
		int new_apicid = summit_cpu_to_logical_apicid(cpu);

		if (round && APIC_CLUSTER(apicid) != APIC_CLUSTER(new_apicid)) {
			printk("%s: Not a valid mask!\n", __func__);
			return BAD_APICID;
		}
		apicid |= new_apicid;
		round++;
	}
	return apicid;
}

static unsigned int summit_cpu_mask_to_apicid_and(const struct cpumask *inmask,
			      const struct cpumask *andmask)
{
	int apicid = summit_cpu_to_logical_apicid(0);
	cpumask_var_t cpumask;

	if (!alloc_cpumask_var(&cpumask, GFP_ATOMIC))
		return apicid;

	cpumask_and(cpumask, inmask, andmask);
	cpumask_and(cpumask, cpumask, cpu_online_mask);
	apicid = summit_cpu_mask_to_apicid(cpumask);

	free_cpumask_var(cpumask);

	return apicid;
}

/*
 * cpuid returns the value latched in the HW at reset, not the APIC ID
 * register's value.  For any box whose BIOS changes APIC IDs, like
 * clustered APIC systems, we must use hard_smp_processor_id.
 *
 * See Intel's IA-32 SW Dev's Manual Vol2 under CPUID.
 */
static int summit_phys_pkg_id(int cpuid_apic, int index_msb)
{
	return hard_smp_processor_id() >> index_msb;
}

static int probe_summit(void)
{
	/* probed later in mptable/ACPI hooks */
	return 0;
}

static void summit_vector_allocation_domain(int cpu, struct cpumask *retmask)
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

#ifdef CONFIG_X86_SUMMIT_NUMA
static struct rio_table_hdr *rio_table_hdr;
static struct scal_detail   *scal_devs[MAX_NUMNODES];
static struct rio_detail    *rio_devs[MAX_NUMNODES*4];

#ifndef CONFIG_X86_NUMAQ
static int mp_bus_id_to_node[MAX_MP_BUSSES];
#endif

static int setup_pci_node_map_for_wpeg(int wpeg_num, int last_bus)
{
	int twister = 0, node = 0;
	int i, bus, num_buses;

	for (i = 0; i < rio_table_hdr->num_rio_dev; i++) {
		if (rio_devs[i]->node_id == rio_devs[wpeg_num]->owner_id) {
			twister = rio_devs[i]->owner_id;
			break;
		}
	}
	if (i == rio_table_hdr->num_rio_dev) {
		printk(KERN_ERR "%s: Couldn't find owner Cyclone for Winnipeg!\n", __func__);
		return last_bus;
	}

	for (i = 0; i < rio_table_hdr->num_scal_dev; i++) {
		if (scal_devs[i]->node_id == twister) {
			node = scal_devs[i]->node_id;
			break;
		}
	}
	if (i == rio_table_hdr->num_scal_dev) {
		printk(KERN_ERR "%s: Couldn't find owner Twister for Cyclone!\n", __func__);
		return last_bus;
	}

	switch (rio_devs[wpeg_num]->type) {
	case CompatWPEG:
		/*
		 * The Compatibility Winnipeg controls the 2 legacy buses,
		 * the 66MHz PCI bus [2 slots] and the 2 "extra" buses in case
		 * a PCI-PCI bridge card is used in either slot: total 5 buses.
		 */
		num_buses = 5;
		break;
	case AltWPEG:
		/*
		 * The Alternate Winnipeg controls the 2 133MHz buses [1 slot
		 * each], their 2 "extra" buses, the 100MHz bus [2 slots] and
		 * the "extra" buses for each of those slots: total 7 buses.
		 */
		num_buses = 7;
		break;
	case LookOutAWPEG:
	case LookOutBWPEG:
		/*
		 * A Lookout Winnipeg controls 3 100MHz buses [2 slots each]
		 * & the "extra" buses for each of those slots: total 9 buses.
		 */
		num_buses = 9;
		break;
	default:
		printk(KERN_INFO "%s: Unsupported Winnipeg type!\n", __func__);
		return last_bus;
	}

	for (bus = last_bus; bus < last_bus + num_buses; bus++)
		mp_bus_id_to_node[bus] = node;
	return bus;
}

static int build_detail_arrays(void)
{
	unsigned long ptr;
	int i, scal_detail_size, rio_detail_size;

	if (rio_table_hdr->num_scal_dev > MAX_NUMNODES) {
		printk(KERN_WARNING "%s: MAX_NUMNODES too low!  Defined as %d, but system has %d nodes.\n", __func__, MAX_NUMNODES, rio_table_hdr->num_scal_dev);
		return 0;
	}

	switch (rio_table_hdr->version) {
	default:
		printk(KERN_WARNING "%s: Invalid Rio Grande Table Version: %d\n", __func__, rio_table_hdr->version);
		return 0;
	case 2:
		scal_detail_size = 11;
		rio_detail_size = 13;
		break;
	case 3:
		scal_detail_size = 12;
		rio_detail_size = 15;
		break;
	}

	ptr = (unsigned long)rio_table_hdr + 3;
	for (i = 0; i < rio_table_hdr->num_scal_dev; i++, ptr += scal_detail_size)
		scal_devs[i] = (struct scal_detail *)ptr;

	for (i = 0; i < rio_table_hdr->num_rio_dev; i++, ptr += rio_detail_size)
		rio_devs[i] = (struct rio_detail *)ptr;

	return 1;
}

void setup_summit(void)
{
	unsigned long		ptr;
	unsigned short		offset;
	int			i, next_wpeg, next_bus = 0;

	/* The pointer to the EBDA is stored in the word @ phys 0x40E(40:0E) */
	ptr = get_bios_ebda();
	ptr = (unsigned long)phys_to_virt(ptr);

	rio_table_hdr = NULL;
	offset = 0x180;
	while (offset) {
		/* The block id is stored in the 2nd word */
		if (*((unsigned short *)(ptr + offset + 2)) == 0x4752) {
			/* set the pointer past the offset & block id */
			rio_table_hdr = (struct rio_table_hdr *)(ptr + offset + 4);
			break;
		}
		/* The next offset is stored in the 1st word.  0 means no more */
		offset = *((unsigned short *)(ptr + offset));
	}
	if (!rio_table_hdr) {
		printk(KERN_ERR "%s: Unable to locate Rio Grande Table in EBDA - bailing!\n", __func__);
		return;
	}

	if (!build_detail_arrays())
		return;

	/* The first Winnipeg we're looking for has an index of 0 */
	next_wpeg = 0;
	do {
		for (i = 0; i < rio_table_hdr->num_rio_dev; i++) {
			if (is_WPEG(rio_devs[i]) && rio_devs[i]->WP_index == next_wpeg) {
				/* It's the Winnipeg we're looking for! */
				next_bus = setup_pci_node_map_for_wpeg(i, next_bus);
				next_wpeg++;
				break;
			}
		}
		/*
		 * If we go through all Rio devices and don't find one with
		 * the next index, it means we've found all the Winnipegs,
		 * and thus all the PCI buses.
		 */
		if (i == rio_table_hdr->num_rio_dev)
			next_wpeg = 0;
	} while (next_wpeg != 0);
}
#endif

struct apic apic_summit = {

	.name				= "summit",
	.probe				= probe_summit,
	.acpi_madt_oem_check		= summit_acpi_madt_oem_check,
	.apic_id_registered		= summit_apic_id_registered,

	.irq_delivery_mode		= dest_LowestPrio,
	/* logical delivery broadcast to all CPUs: */
	.irq_dest_mode			= 1,

	.target_cpus			= summit_target_cpus,
	.disable_esr			= 1,
	.dest_logical			= APIC_DEST_LOGICAL,
	.check_apicid_used		= summit_check_apicid_used,
	.check_apicid_present		= summit_check_apicid_present,

	.vector_allocation_domain	= summit_vector_allocation_domain,
	.init_apic_ldr			= summit_init_apic_ldr,

	.ioapic_phys_id_map		= summit_ioapic_phys_id_map,
	.setup_apic_routing		= summit_setup_apic_routing,
	.multi_timer_check		= NULL,
	.apicid_to_node			= summit_apicid_to_node,
	.cpu_to_logical_apicid		= summit_cpu_to_logical_apicid,
	.cpu_present_to_apicid		= summit_cpu_present_to_apicid,
	.apicid_to_cpu_present		= summit_apicid_to_cpu_present,
	.setup_portio_remap		= NULL,
	.check_phys_apicid_present	= summit_check_phys_apicid_present,
	.enable_apic_mode		= NULL,
	.phys_pkg_id			= summit_phys_pkg_id,
	.mps_oem_check			= summit_mps_oem_check,

	.get_apic_id			= summit_get_apic_id,
	.set_apic_id			= NULL,
	.apic_id_mask			= 0xFF << 24,

	.cpu_mask_to_apicid		= summit_cpu_mask_to_apicid,
	.cpu_mask_to_apicid_and		= summit_cpu_mask_to_apicid_and,

	.send_IPI_mask			= summit_send_IPI_mask,
	.send_IPI_mask_allbutself	= NULL,
	.send_IPI_allbutself		= summit_send_IPI_allbutself,
	.send_IPI_all			= summit_send_IPI_all,
	.send_IPI_self			= default_send_IPI_self,

	.trampoline_phys_low		= DEFAULT_TRAMPOLINE_PHYS_LOW,
	.trampoline_phys_high		= DEFAULT_TRAMPOLINE_PHYS_HIGH,

	.wait_for_init_deassert		= default_wait_for_init_deassert,

	.smp_callin_clear_local_apic	= NULL,
	.inquire_remote_apic		= default_inquire_remote_apic,

	.read				= native_apic_mem_read,
	.write				= native_apic_mem_write,
	.icr_read			= native_apic_icr_read,
	.icr_write			= native_apic_icr_write,
	.wait_icr_idle			= native_apic_wait_icr_idle,
	.safe_wait_icr_idle		= native_safe_apic_wait_icr_idle,
};
