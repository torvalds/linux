/*
 *  boot.c - Architecture-Specific Low-Level ACPI Boot Support
 *
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *  Copyright (C) 2001 Jun Nakajima <jun.nakajima@intel.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/init.h>
#include <linux/config.h>
#include <linux/acpi.h>
#include <linux/efi.h>
#include <linux/module.h>
#include <linux/dmi.h>
#include <linux/irq.h>

#include <asm/pgtable.h>
#include <asm/io_apic.h>
#include <asm/apic.h>
#include <asm/io.h>
#include <asm/mpspec.h>

#ifdef	CONFIG_X86_64

extern void __init clustered_apic_check(void);

extern int gsi_irq_sharing(int gsi);
#include <asm/proto.h>

#else				/* X86 */

#ifdef	CONFIG_X86_LOCAL_APIC
#include <mach_apic.h>
#include <mach_mpparse.h>
#endif				/* CONFIG_X86_LOCAL_APIC */

static inline int gsi_irq_sharing(int gsi) { return gsi; }

#endif				/* X86 */

#define BAD_MADT_ENTRY(entry, end) (					    \
		(!entry) || (unsigned long)entry + sizeof(*entry) > end ||  \
		((acpi_table_entry_header *)entry)->length != sizeof(*entry))

#define PREFIX			"ACPI: "

int acpi_noirq __initdata;	/* skip ACPI IRQ initialization */
int acpi_pci_disabled __initdata;	/* skip ACPI PCI scan and IRQ initialization */
int acpi_ht __initdata = 1;	/* enable HT */

int acpi_lapic;
int acpi_ioapic;
int acpi_strict;
EXPORT_SYMBOL(acpi_strict);

acpi_interrupt_flags acpi_sci_flags __initdata;
int acpi_sci_override_gsi __initdata;
int acpi_skip_timer_override __initdata;

#ifdef CONFIG_X86_LOCAL_APIC
static u64 acpi_lapic_addr __initdata = APIC_DEFAULT_PHYS_BASE;
#endif

#ifndef __HAVE_ARCH_CMPXCHG
#warning ACPI uses CMPXCHG, i486 and later hardware
#endif

#define MAX_MADT_ENTRIES	256
u8 x86_acpiid_to_apicid[MAX_MADT_ENTRIES] =
    {[0 ... MAX_MADT_ENTRIES - 1] = 0xff };
EXPORT_SYMBOL(x86_acpiid_to_apicid);

/* --------------------------------------------------------------------------
                              Boot-time Configuration
   -------------------------------------------------------------------------- */

/*
 * The default interrupt routing model is PIC (8259).  This gets
 * overriden if IOAPICs are enumerated (below).
 */
enum acpi_irq_model_id acpi_irq_model = ACPI_IRQ_MODEL_PIC;

#ifdef	CONFIG_X86_64

/* rely on all ACPI tables being in the direct mapping */
char *__acpi_map_table(unsigned long phys_addr, unsigned long size)
{
	if (!phys_addr || !size)
		return NULL;

	if (phys_addr+size <= (end_pfn_map << PAGE_SHIFT) + PAGE_SIZE)
		return __va(phys_addr);

	return NULL;
}

#else

/*
 * Temporarily use the virtual area starting from FIX_IO_APIC_BASE_END,
 * to map the target physical address. The problem is that set_fixmap()
 * provides a single page, and it is possible that the page is not
 * sufficient.
 * By using this area, we can map up to MAX_IO_APICS pages temporarily,
 * i.e. until the next __va_range() call.
 *
 * Important Safety Note:  The fixed I/O APIC page numbers are *subtracted*
 * from the fixed base.  That's why we start at FIX_IO_APIC_BASE_END and
 * count idx down while incrementing the phys address.
 */
char *__acpi_map_table(unsigned long phys, unsigned long size)
{
	unsigned long base, offset, mapped_size;
	int idx;

	if (phys + size < 8 * 1024 * 1024)
		return __va(phys);

	offset = phys & (PAGE_SIZE - 1);
	mapped_size = PAGE_SIZE - offset;
	set_fixmap(FIX_ACPI_END, phys);
	base = fix_to_virt(FIX_ACPI_END);

	/*
	 * Most cases can be covered by the below.
	 */
	idx = FIX_ACPI_END;
	while (mapped_size < size) {
		if (--idx < FIX_ACPI_BEGIN)
			return NULL;	/* cannot handle this */
		phys += PAGE_SIZE;
		set_fixmap(idx, phys);
		mapped_size += PAGE_SIZE;
	}

	return ((unsigned char *)base + offset);
}
#endif

#ifdef CONFIG_PCI_MMCONFIG
/* The physical address of the MMCONFIG aperture.  Set from ACPI tables. */
struct acpi_table_mcfg_config *pci_mmcfg_config;
int pci_mmcfg_config_num;

int __init acpi_parse_mcfg(unsigned long phys_addr, unsigned long size)
{
	struct acpi_table_mcfg *mcfg;
	unsigned long i;
	int config_size;

	if (!phys_addr || !size)
		return -EINVAL;

	mcfg = (struct acpi_table_mcfg *)__acpi_map_table(phys_addr, size);
	if (!mcfg) {
		printk(KERN_WARNING PREFIX "Unable to map MCFG\n");
		return -ENODEV;
	}

	/* how many config structures do we have */
	pci_mmcfg_config_num = 0;
	i = size - sizeof(struct acpi_table_mcfg);
	while (i >= sizeof(struct acpi_table_mcfg_config)) {
		++pci_mmcfg_config_num;
		i -= sizeof(struct acpi_table_mcfg_config);
	};
	if (pci_mmcfg_config_num == 0) {
		printk(KERN_ERR PREFIX "MMCONFIG has no entries\n");
		return -ENODEV;
	}

	config_size = pci_mmcfg_config_num * sizeof(*pci_mmcfg_config);
	pci_mmcfg_config = kmalloc(config_size, GFP_KERNEL);
	if (!pci_mmcfg_config) {
		printk(KERN_WARNING PREFIX
		       "No memory for MCFG config tables\n");
		return -ENOMEM;
	}

	memcpy(pci_mmcfg_config, &mcfg->config, config_size);
	for (i = 0; i < pci_mmcfg_config_num; ++i) {
		if (mcfg->config[i].base_reserved) {
			printk(KERN_ERR PREFIX
			       "MMCONFIG not in low 4GB of memory\n");
			return -ENODEV;
		}
	}

	return 0;
}
#endif				/* CONFIG_PCI_MMCONFIG */

#ifdef CONFIG_X86_LOCAL_APIC
static int __init acpi_parse_madt(unsigned long phys_addr, unsigned long size)
{
	struct acpi_table_madt *madt = NULL;

	if (!phys_addr || !size)
		return -EINVAL;

	madt = (struct acpi_table_madt *)__acpi_map_table(phys_addr, size);
	if (!madt) {
		printk(KERN_WARNING PREFIX "Unable to map MADT\n");
		return -ENODEV;
	}

	if (madt->lapic_address) {
		acpi_lapic_addr = (u64) madt->lapic_address;

		printk(KERN_DEBUG PREFIX "Local APIC address 0x%08x\n",
		       madt->lapic_address);
	}

	acpi_madt_oem_check(madt->header.oem_id, madt->header.oem_table_id);

	return 0;
}

static int __init
acpi_parse_lapic(acpi_table_entry_header * header, const unsigned long end)
{
	struct acpi_table_lapic *processor = NULL;

	processor = (struct acpi_table_lapic *)header;

	if (BAD_MADT_ENTRY(processor, end))
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	/* Record local apic id only when enabled */
	if (processor->flags.enabled)
		x86_acpiid_to_apicid[processor->acpi_id] = processor->id;

	/*
	 * We need to register disabled CPU as well to permit
	 * counting disabled CPUs. This allows us to size
	 * cpus_possible_map more accurately, to permit
	 * to not preallocating memory for all NR_CPUS
	 * when we use CPU hotplug.
	 */
	mp_register_lapic(processor->id,	/* APIC ID */
			  processor->flags.enabled);	/* Enabled? */

	return 0;
}

static int __init
acpi_parse_lapic_addr_ovr(acpi_table_entry_header * header,
			  const unsigned long end)
{
	struct acpi_table_lapic_addr_ovr *lapic_addr_ovr = NULL;

	lapic_addr_ovr = (struct acpi_table_lapic_addr_ovr *)header;

	if (BAD_MADT_ENTRY(lapic_addr_ovr, end))
		return -EINVAL;

	acpi_lapic_addr = lapic_addr_ovr->address;

	return 0;
}

static int __init
acpi_parse_lapic_nmi(acpi_table_entry_header * header, const unsigned long end)
{
	struct acpi_table_lapic_nmi *lapic_nmi = NULL;

	lapic_nmi = (struct acpi_table_lapic_nmi *)header;

	if (BAD_MADT_ENTRY(lapic_nmi, end))
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	if (lapic_nmi->lint != 1)
		printk(KERN_WARNING PREFIX "NMI not connected to LINT 1!\n");

	return 0;
}

#endif				/*CONFIG_X86_LOCAL_APIC */

#ifdef CONFIG_X86_IO_APIC

static int __init
acpi_parse_ioapic(acpi_table_entry_header * header, const unsigned long end)
{
	struct acpi_table_ioapic *ioapic = NULL;

	ioapic = (struct acpi_table_ioapic *)header;

	if (BAD_MADT_ENTRY(ioapic, end))
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	mp_register_ioapic(ioapic->id,
			   ioapic->address, ioapic->global_irq_base);

	return 0;
}

/*
 * Parse Interrupt Source Override for the ACPI SCI
 */
static void acpi_sci_ioapic_setup(u32 gsi, u16 polarity, u16 trigger)
{
	if (trigger == 0)	/* compatible SCI trigger is level */
		trigger = 3;

	if (polarity == 0)	/* compatible SCI polarity is low */
		polarity = 3;

	/* Command-line over-ride via acpi_sci= */
	if (acpi_sci_flags.trigger)
		trigger = acpi_sci_flags.trigger;

	if (acpi_sci_flags.polarity)
		polarity = acpi_sci_flags.polarity;

	/*
	 * mp_config_acpi_legacy_irqs() already setup IRQs < 16
	 * If GSI is < 16, this will update its flags,
	 * else it will create a new mp_irqs[] entry.
	 */
	mp_override_legacy_irq(gsi, polarity, trigger, gsi);

	/*
	 * stash over-ride to indicate we've been here
	 * and for later update of acpi_fadt
	 */
	acpi_sci_override_gsi = gsi;
	return;
}

static int __init
acpi_parse_int_src_ovr(acpi_table_entry_header * header,
		       const unsigned long end)
{
	struct acpi_table_int_src_ovr *intsrc = NULL;

	intsrc = (struct acpi_table_int_src_ovr *)header;

	if (BAD_MADT_ENTRY(intsrc, end))
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	if (intsrc->bus_irq == acpi_fadt.sci_int) {
		acpi_sci_ioapic_setup(intsrc->global_irq,
				      intsrc->flags.polarity,
				      intsrc->flags.trigger);
		return 0;
	}

	if (acpi_skip_timer_override &&
	    intsrc->bus_irq == 0 && intsrc->global_irq == 2) {
		printk(PREFIX "BIOS IRQ0 pin2 override ignored.\n");
		return 0;
	}

	mp_override_legacy_irq(intsrc->bus_irq,
			       intsrc->flags.polarity,
			       intsrc->flags.trigger, intsrc->global_irq);

	return 0;
}

static int __init
acpi_parse_nmi_src(acpi_table_entry_header * header, const unsigned long end)
{
	struct acpi_table_nmi_src *nmi_src = NULL;

	nmi_src = (struct acpi_table_nmi_src *)header;

	if (BAD_MADT_ENTRY(nmi_src, end))
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	/* TBD: Support nimsrc entries? */

	return 0;
}

#endif				/* CONFIG_X86_IO_APIC */

/*
 * acpi_pic_sci_set_trigger()
 * 
 * use ELCR to set PIC-mode trigger type for SCI
 *
 * If a PIC-mode SCI is not recognized or gives spurious IRQ7's
 * it may require Edge Trigger -- use "acpi_sci=edge"
 *
 * Port 0x4d0-4d1 are ECLR1 and ECLR2, the Edge/Level Control Registers
 * for the 8259 PIC.  bit[n] = 1 means irq[n] is Level, otherwise Edge.
 * ECLR1 is IRQ's 0-7 (IRQ 0, 1, 2 must be 0)
 * ECLR2 is IRQ's 8-15 (IRQ 8, 13 must be 0)
 */

void __init acpi_pic_sci_set_trigger(unsigned int irq, u16 trigger)
{
	unsigned int mask = 1 << irq;
	unsigned int old, new;

	/* Real old ELCR mask */
	old = inb(0x4d0) | (inb(0x4d1) << 8);

	/*
	 * If we use ACPI to set PCI irq's, then we should clear ELCR
	 * since we will set it correctly as we enable the PCI irq
	 * routing.
	 */
	new = acpi_noirq ? old : 0;

	/*
	 * Update SCI information in the ELCR, it isn't in the PCI
	 * routing tables..
	 */
	switch (trigger) {
	case 1:		/* Edge - clear */
		new &= ~mask;
		break;
	case 3:		/* Level - set */
		new |= mask;
		break;
	}

	if (old == new)
		return;

	printk(PREFIX "setting ELCR to %04x (from %04x)\n", new, old);
	outb(new, 0x4d0);
	outb(new >> 8, 0x4d1);
}

int acpi_gsi_to_irq(u32 gsi, unsigned int *irq)
{
#ifdef CONFIG_X86_IO_APIC
	if (use_pci_vector() && !platform_legacy_irq(gsi))
		*irq = IO_APIC_VECTOR(gsi);
	else
#endif
		*irq = gsi_irq_sharing(gsi);
	return 0;
}

/*
 * success: return IRQ number (>=0)
 * failure: return < 0
 */
int acpi_register_gsi(u32 gsi, int triggering, int polarity)
{
	unsigned int irq;
	unsigned int plat_gsi = gsi;

#ifdef CONFIG_PCI
	/*
	 * Make sure all (legacy) PCI IRQs are set as level-triggered.
	 */
	if (acpi_irq_model == ACPI_IRQ_MODEL_PIC) {
		extern void eisa_set_level_irq(unsigned int irq);

		if (triggering == ACPI_LEVEL_SENSITIVE)
			eisa_set_level_irq(gsi);
	}
#endif

#ifdef CONFIG_X86_IO_APIC
	if (acpi_irq_model == ACPI_IRQ_MODEL_IOAPIC) {
		plat_gsi = mp_register_gsi(gsi, triggering, polarity);
	}
#endif
	acpi_gsi_to_irq(plat_gsi, &irq);
	return irq;
}

EXPORT_SYMBOL(acpi_register_gsi);

/*
 *  ACPI based hotplug support for CPU
 */
#ifdef CONFIG_ACPI_HOTPLUG_CPU
int acpi_map_lsapic(acpi_handle handle, int *pcpu)
{
	/* TBD */
	return -EINVAL;
}

EXPORT_SYMBOL(acpi_map_lsapic);

int acpi_unmap_lsapic(int cpu)
{
	/* TBD */
	return -EINVAL;
}

EXPORT_SYMBOL(acpi_unmap_lsapic);
#endif				/* CONFIG_ACPI_HOTPLUG_CPU */

int acpi_register_ioapic(acpi_handle handle, u64 phys_addr, u32 gsi_base)
{
	/* TBD */
	return -EINVAL;
}

EXPORT_SYMBOL(acpi_register_ioapic);

int acpi_unregister_ioapic(acpi_handle handle, u32 gsi_base)
{
	/* TBD */
	return -EINVAL;
}

EXPORT_SYMBOL(acpi_unregister_ioapic);

static unsigned long __init
acpi_scan_rsdp(unsigned long start, unsigned long length)
{
	unsigned long offset = 0;
	unsigned long sig_len = sizeof("RSD PTR ") - 1;

	/*
	 * Scan all 16-byte boundaries of the physical memory region for the
	 * RSDP signature.
	 */
	for (offset = 0; offset < length; offset += 16) {
		if (strncmp((char *)(phys_to_virt(start) + offset), "RSD PTR ", sig_len))
			continue;
		return (start + offset);
	}

	return 0;
}

static int __init acpi_parse_sbf(unsigned long phys_addr, unsigned long size)
{
	struct acpi_table_sbf *sb;

	if (!phys_addr || !size)
		return -EINVAL;

	sb = (struct acpi_table_sbf *)__acpi_map_table(phys_addr, size);
	if (!sb) {
		printk(KERN_WARNING PREFIX "Unable to map SBF\n");
		return -ENODEV;
	}

	sbf_port = sb->sbf_cmos;	/* Save CMOS port */

	return 0;
}

#ifdef CONFIG_HPET_TIMER

static int __init acpi_parse_hpet(unsigned long phys, unsigned long size)
{
	struct acpi_table_hpet *hpet_tbl;

	if (!phys || !size)
		return -EINVAL;

	hpet_tbl = (struct acpi_table_hpet *)__acpi_map_table(phys, size);
	if (!hpet_tbl) {
		printk(KERN_WARNING PREFIX "Unable to map HPET\n");
		return -ENODEV;
	}

	if (hpet_tbl->addr.space_id != ACPI_SPACE_MEM) {
		printk(KERN_WARNING PREFIX "HPET timers must be located in "
		       "memory.\n");
		return -1;
	}
#ifdef	CONFIG_X86_64
	vxtime.hpet_address = hpet_tbl->addr.addrl |
	    ((long)hpet_tbl->addr.addrh << 32);

	printk(KERN_INFO PREFIX "HPET id: %#x base: %#lx\n",
	       hpet_tbl->id, vxtime.hpet_address);
#else				/* X86 */
	{
		extern unsigned long hpet_address;

		hpet_address = hpet_tbl->addr.addrl;
		printk(KERN_INFO PREFIX "HPET id: %#x base: %#lx\n",
		       hpet_tbl->id, hpet_address);
	}
#endif				/* X86 */

	return 0;
}
#else
#define	acpi_parse_hpet	NULL
#endif

#ifdef CONFIG_X86_PM_TIMER
extern u32 pmtmr_ioport;
#endif

static int __init acpi_parse_fadt(unsigned long phys, unsigned long size)
{
	struct fadt_descriptor_rev2 *fadt = NULL;

	fadt = (struct fadt_descriptor_rev2 *)__acpi_map_table(phys, size);
	if (!fadt) {
		printk(KERN_WARNING PREFIX "Unable to map FADT\n");
		return 0;
	}
	/* initialize sci_int early for INT_SRC_OVR MADT parsing */
	acpi_fadt.sci_int = fadt->sci_int;

	/* initialize rev and apic_phys_dest_mode for x86_64 genapic */
	acpi_fadt.revision = fadt->revision;
	acpi_fadt.force_apic_physical_destination_mode =
	    fadt->force_apic_physical_destination_mode;

#ifdef CONFIG_X86_PM_TIMER
	/* detect the location of the ACPI PM Timer */
	if (fadt->revision >= FADT2_REVISION_ID) {
		/* FADT rev. 2 */
		if (fadt->xpm_tmr_blk.address_space_id !=
		    ACPI_ADR_SPACE_SYSTEM_IO)
			return 0;

		pmtmr_ioport = fadt->xpm_tmr_blk.address;
		/*
		 * "X" fields are optional extensions to the original V1.0
		 * fields, so we must selectively expand V1.0 fields if the
		 * corresponding X field is zero.
	 	 */
		if (!pmtmr_ioport)
			pmtmr_ioport = fadt->V1_pm_tmr_blk;
	} else {
		/* FADT rev. 1 */
		pmtmr_ioport = fadt->V1_pm_tmr_blk;
	}
	if (pmtmr_ioport)
		printk(KERN_INFO PREFIX "PM-Timer IO Port: %#x\n",
		       pmtmr_ioport);
#endif
	return 0;
}

unsigned long __init acpi_find_rsdp(void)
{
	unsigned long rsdp_phys = 0;

	if (efi_enabled) {
		if (efi.acpi20)
			return __pa(efi.acpi20);
		else if (efi.acpi)
			return __pa(efi.acpi);
	}
	/*
	 * Scan memory looking for the RSDP signature. First search EBDA (low
	 * memory) paragraphs and then search upper memory (E0000-FFFFF).
	 */
	rsdp_phys = acpi_scan_rsdp(0, 0x400);
	if (!rsdp_phys)
		rsdp_phys = acpi_scan_rsdp(0xE0000, 0x20000);

	return rsdp_phys;
}

#ifdef	CONFIG_X86_LOCAL_APIC
/*
 * Parse LAPIC entries in MADT
 * returns 0 on success, < 0 on error
 */
static int __init acpi_parse_madt_lapic_entries(void)
{
	int count;

	/* 
	 * Note that the LAPIC address is obtained from the MADT (32-bit value)
	 * and (optionally) overriden by a LAPIC_ADDR_OVR entry (64-bit value).
	 */

	count =
	    acpi_table_parse_madt(ACPI_MADT_LAPIC_ADDR_OVR,
				  acpi_parse_lapic_addr_ovr, 0);
	if (count < 0) {
		printk(KERN_ERR PREFIX
		       "Error parsing LAPIC address override entry\n");
		return count;
	}

	mp_register_lapic_address(acpi_lapic_addr);

	count = acpi_table_parse_madt(ACPI_MADT_LAPIC, acpi_parse_lapic,
				      MAX_APICS);
	if (!count) {
		printk(KERN_ERR PREFIX "No LAPIC entries present\n");
		/* TBD: Cleanup to allow fallback to MPS */
		return -ENODEV;
	} else if (count < 0) {
		printk(KERN_ERR PREFIX "Error parsing LAPIC entry\n");
		/* TBD: Cleanup to allow fallback to MPS */
		return count;
	}

	count =
	    acpi_table_parse_madt(ACPI_MADT_LAPIC_NMI, acpi_parse_lapic_nmi, 0);
	if (count < 0) {
		printk(KERN_ERR PREFIX "Error parsing LAPIC NMI entry\n");
		/* TBD: Cleanup to allow fallback to MPS */
		return count;
	}
	return 0;
}
#endif				/* CONFIG_X86_LOCAL_APIC */

#ifdef	CONFIG_X86_IO_APIC
/*
 * Parse IOAPIC related entries in MADT
 * returns 0 on success, < 0 on error
 */
static int __init acpi_parse_madt_ioapic_entries(void)
{
	int count;

	/*
	 * ACPI interpreter is required to complete interrupt setup,
	 * so if it is off, don't enumerate the io-apics with ACPI.
	 * If MPS is present, it will handle them,
	 * otherwise the system will stay in PIC mode
	 */
	if (acpi_disabled || acpi_noirq) {
		return -ENODEV;
	}

	/*
	 * if "noapic" boot option, don't look for IO-APICs
	 */
	if (skip_ioapic_setup) {
		printk(KERN_INFO PREFIX "Skipping IOAPIC probe "
		       "due to 'noapic' option.\n");
		return -ENODEV;
	}

	count =
	    acpi_table_parse_madt(ACPI_MADT_IOAPIC, acpi_parse_ioapic,
				  MAX_IO_APICS);
	if (!count) {
		printk(KERN_ERR PREFIX "No IOAPIC entries present\n");
		return -ENODEV;
	} else if (count < 0) {
		printk(KERN_ERR PREFIX "Error parsing IOAPIC entry\n");
		return count;
	}

	count =
	    acpi_table_parse_madt(ACPI_MADT_INT_SRC_OVR, acpi_parse_int_src_ovr,
				  NR_IRQ_VECTORS);
	if (count < 0) {
		printk(KERN_ERR PREFIX
		       "Error parsing interrupt source overrides entry\n");
		/* TBD: Cleanup to allow fallback to MPS */
		return count;
	}

	/*
	 * If BIOS did not supply an INT_SRC_OVR for the SCI
	 * pretend we got one so we can set the SCI flags.
	 */
	if (!acpi_sci_override_gsi)
		acpi_sci_ioapic_setup(acpi_fadt.sci_int, 0, 0);

	/* Fill in identity legacy mapings where no override */
	mp_config_acpi_legacy_irqs();

	count =
	    acpi_table_parse_madt(ACPI_MADT_NMI_SRC, acpi_parse_nmi_src,
				  NR_IRQ_VECTORS);
	if (count < 0) {
		printk(KERN_ERR PREFIX "Error parsing NMI SRC entry\n");
		/* TBD: Cleanup to allow fallback to MPS */
		return count;
	}

	return 0;
}
#else
static inline int acpi_parse_madt_ioapic_entries(void)
{
	return -1;
}
#endif	/* !CONFIG_X86_IO_APIC */

static void __init acpi_process_madt(void)
{
#ifdef CONFIG_X86_LOCAL_APIC
	int count, error;

	count = acpi_table_parse(ACPI_APIC, acpi_parse_madt);
	if (count >= 1) {

		/*
		 * Parse MADT LAPIC entries
		 */
		error = acpi_parse_madt_lapic_entries();
		if (!error) {
			acpi_lapic = 1;

#ifdef CONFIG_X86_GENERICARCH
			generic_bigsmp_probe();
#endif
			/*
			 * Parse MADT IO-APIC entries
			 */
			error = acpi_parse_madt_ioapic_entries();
			if (!error) {
				acpi_irq_model = ACPI_IRQ_MODEL_IOAPIC;
				acpi_irq_balance_set(NULL);
				acpi_ioapic = 1;

				smp_found_config = 1;
				clustered_apic_check();
			}
		}
		if (error == -EINVAL) {
			/*
			 * Dell Precision Workstation 410, 610 come here.
			 */
			printk(KERN_ERR PREFIX
			       "Invalid BIOS MADT, disabling ACPI\n");
			disable_acpi();
		}
	}
#endif
	return;
}

extern int acpi_force;

#ifdef __i386__

static int __init disable_acpi_irq(struct dmi_system_id *d)
{
	if (!acpi_force) {
		printk(KERN_NOTICE "%s detected: force use of acpi=noirq\n",
		       d->ident);
		acpi_noirq_set();
	}
	return 0;
}

static int __init disable_acpi_pci(struct dmi_system_id *d)
{
	if (!acpi_force) {
		printk(KERN_NOTICE "%s detected: force use of pci=noacpi\n",
		       d->ident);
		acpi_disable_pci();
	}
	return 0;
}

static int __init dmi_disable_acpi(struct dmi_system_id *d)
{
	if (!acpi_force) {
		printk(KERN_NOTICE "%s detected: acpi off\n", d->ident);
		disable_acpi();
	} else {
		printk(KERN_NOTICE
		       "Warning: DMI blacklist says broken, but acpi forced\n");
	}
	return 0;
}

/*
 * Limit ACPI to CPU enumeration for HT
 */
static int __init force_acpi_ht(struct dmi_system_id *d)
{
	if (!acpi_force) {
		printk(KERN_NOTICE "%s detected: force use of acpi=ht\n",
		       d->ident);
		disable_acpi();
		acpi_ht = 1;
	} else {
		printk(KERN_NOTICE
		       "Warning: acpi=force overrules DMI blacklist: acpi=ht\n");
	}
	return 0;
}

/*
 * If your system is blacklisted here, but you find that acpi=force
 * works for you, please contact acpi-devel@sourceforge.net
 */
static struct dmi_system_id __initdata acpi_dmi_table[] = {
	/*
	 * Boxes that need ACPI disabled
	 */
	{
	 .callback = dmi_disable_acpi,
	 .ident = "IBM Thinkpad",
	 .matches = {
		     DMI_MATCH(DMI_BOARD_VENDOR, "IBM"),
		     DMI_MATCH(DMI_BOARD_NAME, "2629H1G"),
		     },
	 },

	/*
	 * Boxes that need acpi=ht
	 */
	{
	 .callback = force_acpi_ht,
	 .ident = "FSC Primergy T850",
	 .matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU SIEMENS"),
		     DMI_MATCH(DMI_PRODUCT_NAME, "PRIMERGY T850"),
		     },
	 },
	{
	 .callback = force_acpi_ht,
	 .ident = "DELL GX240",
	 .matches = {
		     DMI_MATCH(DMI_BOARD_VENDOR, "Dell Computer Corporation"),
		     DMI_MATCH(DMI_BOARD_NAME, "OptiPlex GX240"),
		     },
	 },
	{
	 .callback = force_acpi_ht,
	 .ident = "HP VISUALIZE NT Workstation",
	 .matches = {
		     DMI_MATCH(DMI_BOARD_VENDOR, "Hewlett-Packard"),
		     DMI_MATCH(DMI_PRODUCT_NAME, "HP VISUALIZE NT Workstation"),
		     },
	 },
	{
	 .callback = force_acpi_ht,
	 .ident = "Compaq Workstation W8000",
	 .matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "Compaq"),
		     DMI_MATCH(DMI_PRODUCT_NAME, "Workstation W8000"),
		     },
	 },
	{
	 .callback = force_acpi_ht,
	 .ident = "ASUS P4B266",
	 .matches = {
		     DMI_MATCH(DMI_BOARD_VENDOR, "ASUSTeK Computer INC."),
		     DMI_MATCH(DMI_BOARD_NAME, "P4B266"),
		     },
	 },
	{
	 .callback = force_acpi_ht,
	 .ident = "ASUS P2B-DS",
	 .matches = {
		     DMI_MATCH(DMI_BOARD_VENDOR, "ASUSTeK Computer INC."),
		     DMI_MATCH(DMI_BOARD_NAME, "P2B-DS"),
		     },
	 },
	{
	 .callback = force_acpi_ht,
	 .ident = "ASUS CUR-DLS",
	 .matches = {
		     DMI_MATCH(DMI_BOARD_VENDOR, "ASUSTeK Computer INC."),
		     DMI_MATCH(DMI_BOARD_NAME, "CUR-DLS"),
		     },
	 },
	{
	 .callback = force_acpi_ht,
	 .ident = "ABIT i440BX-W83977",
	 .matches = {
		     DMI_MATCH(DMI_BOARD_VENDOR, "ABIT <http://www.abit.com>"),
		     DMI_MATCH(DMI_BOARD_NAME, "i440BX-W83977 (BP6)"),
		     },
	 },
	{
	 .callback = force_acpi_ht,
	 .ident = "IBM Bladecenter",
	 .matches = {
		     DMI_MATCH(DMI_BOARD_VENDOR, "IBM"),
		     DMI_MATCH(DMI_BOARD_NAME, "IBM eServer BladeCenter HS20"),
		     },
	 },
	{
	 .callback = force_acpi_ht,
	 .ident = "IBM eServer xSeries 360",
	 .matches = {
		     DMI_MATCH(DMI_BOARD_VENDOR, "IBM"),
		     DMI_MATCH(DMI_BOARD_NAME, "eServer xSeries 360"),
		     },
	 },
	{
	 .callback = force_acpi_ht,
	 .ident = "IBM eserver xSeries 330",
	 .matches = {
		     DMI_MATCH(DMI_BOARD_VENDOR, "IBM"),
		     DMI_MATCH(DMI_BOARD_NAME, "eserver xSeries 330"),
		     },
	 },
	{
	 .callback = force_acpi_ht,
	 .ident = "IBM eserver xSeries 440",
	 .matches = {
		     DMI_MATCH(DMI_BOARD_VENDOR, "IBM"),
		     DMI_MATCH(DMI_PRODUCT_NAME, "eserver xSeries 440"),
		     },
	 },

	/*
	 * Boxes that need ACPI PCI IRQ routing disabled
	 */
	{
	 .callback = disable_acpi_irq,
	 .ident = "ASUS A7V",
	 .matches = {
		     DMI_MATCH(DMI_BOARD_VENDOR, "ASUSTeK Computer INC"),
		     DMI_MATCH(DMI_BOARD_NAME, "<A7V>"),
		     /* newer BIOS, Revision 1011, does work */
		     DMI_MATCH(DMI_BIOS_VERSION,
			       "ASUS A7V ACPI BIOS Revision 1007"),
		     },
	 },

	/*
	 * Boxes that need ACPI PCI IRQ routing and PCI scan disabled
	 */
	{			/* _BBN 0 bug */
	 .callback = disable_acpi_pci,
	 .ident = "ASUS PR-DLS",
	 .matches = {
		     DMI_MATCH(DMI_BOARD_VENDOR, "ASUSTeK Computer INC."),
		     DMI_MATCH(DMI_BOARD_NAME, "PR-DLS"),
		     DMI_MATCH(DMI_BIOS_VERSION,
			       "ASUS PR-DLS ACPI BIOS Revision 1010"),
		     DMI_MATCH(DMI_BIOS_DATE, "03/21/2003")
		     },
	 },
	{
	 .callback = disable_acpi_pci,
	 .ident = "Acer TravelMate 36x Laptop",
	 .matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
		     DMI_MATCH(DMI_PRODUCT_NAME, "TravelMate 360"),
		     },
	 },
	{}
};

#endif				/* __i386__ */

/*
 * acpi_boot_table_init() and acpi_boot_init()
 *  called from setup_arch(), always.
 *	1. checksums all tables
 *	2. enumerates lapics
 *	3. enumerates io-apics
 *
 * acpi_table_init() is separate to allow reading SRAT without
 * other side effects.
 *
 * side effects of acpi_boot_init:
 *	acpi_lapic = 1 if LAPIC found
 *	acpi_ioapic = 1 if IOAPIC found
 *	if (acpi_lapic && acpi_ioapic) smp_found_config = 1;
 *	if acpi_blacklisted() acpi_disabled = 1;
 *	acpi_irq_model=...
 *	...
 *
 * return value: (currently ignored)
 *	0: success
 *	!0: failure
 */

int __init acpi_boot_table_init(void)
{
	int error;

#ifdef __i386__
	dmi_check_system(acpi_dmi_table);
#endif

	/*
	 * If acpi_disabled, bail out
	 * One exception: acpi=ht continues far enough to enumerate LAPICs
	 */
	if (acpi_disabled && !acpi_ht)
		return 1;

	/* 
	 * Initialize the ACPI boot-time table parser.
	 */
	error = acpi_table_init();
	if (error) {
		disable_acpi();
		return error;
	}
#ifdef __i386__
	check_acpi_pci();
#endif

	acpi_table_parse(ACPI_BOOT, acpi_parse_sbf);

	/*
	 * blacklist may disable ACPI entirely
	 */
	error = acpi_blacklisted();
	if (error) {
		if (acpi_force) {
			printk(KERN_WARNING PREFIX "acpi=force override\n");
		} else {
			printk(KERN_WARNING PREFIX "Disabling ACPI support\n");
			disable_acpi();
			return error;
		}
	}

	return 0;
}

int __init acpi_boot_init(void)
{
	/*
	 * If acpi_disabled, bail out
	 * One exception: acpi=ht continues far enough to enumerate LAPICs
	 */
	if (acpi_disabled && !acpi_ht)
		return 1;

	acpi_table_parse(ACPI_BOOT, acpi_parse_sbf);

	/*
	 * set sci_int and PM timer address
	 */
	acpi_table_parse(ACPI_FADT, acpi_parse_fadt);

	/*
	 * Process the Multiple APIC Description Table (MADT), if present
	 */
	acpi_process_madt();

	acpi_table_parse(ACPI_HPET, acpi_parse_hpet);

	return 0;
}
