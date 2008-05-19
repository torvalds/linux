/*
 *	Intel Multiprocessor Specification 1.1 and 1.4
 *	compliant MP-table parsing routines.
 *
 *	(c) 1995 Alan Cox, Building #3 <alan@redhat.com>
 *	(c) 1998, 1999, 2000 Ingo Molnar <mingo@redhat.com>
 *      (c) 2008 Alexey Starikovskiy <astarikovskiy@suse.de>
 */

#include <linux/mm.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/bootmem.h>
#include <linux/kernel_stat.h>
#include <linux/mc146818rtc.h>
#include <linux/bitops.h>
#include <linux/acpi.h>
#include <linux/module.h>

#include <asm/smp.h>
#include <asm/mtrr.h>
#include <asm/mpspec.h>
#include <asm/pgalloc.h>
#include <asm/io_apic.h>
#include <asm/proto.h>
#include <asm/acpi.h>
#include <asm/bios_ebda.h>

#include <mach_apic.h>
#ifdef CONFIG_X86_32
#include <mach_apicdef.h>
#include <mach_mpparse.h>
#endif

/* Have we found an MP table */
int smp_found_config;

/*
 * Various Linux-internal data structures created from the
 * MP-table.
 */
#if defined (CONFIG_MCA) || defined (CONFIG_EISA)
int mp_bus_id_to_type[MAX_MP_BUSSES];
#endif

DECLARE_BITMAP(mp_bus_not_pci, MAX_MP_BUSSES);
int mp_bus_id_to_pci_bus[MAX_MP_BUSSES] = {[0 ... MAX_MP_BUSSES - 1] = -1 };

static int mp_current_pci_id;

int pic_mode;

/*
 * Intel MP BIOS table parsing routines:
 */

/*
 * Checksum an MP configuration block.
 */

static int __init mpf_checksum(unsigned char *mp, int len)
{
	int sum = 0;

	while (len--)
		sum += *mp++;

	return sum & 0xFF;
}

#ifdef CONFIG_X86_NUMAQ
/*
 * Have to match translation table entries to main table entries by counter
 * hence the mpc_record variable .... can't see a less disgusting way of
 * doing this ....
 */

static int mpc_record;
static struct mpc_config_translation *translation_table[MAX_MPC_ENTRY]
    __cpuinitdata;
#endif

static void __cpuinit MP_processor_info(struct mpc_config_processor *m)
{
	int apicid;
	char *bootup_cpu = "";

	if (!(m->mpc_cpuflag & CPU_ENABLED)) {
		disabled_cpus++;
		return;
	}
#ifdef CONFIG_X86_NUMAQ
	apicid = mpc_apic_id(m, translation_table[mpc_record]);
#else
	apicid = m->mpc_apicid;
#endif
	if (m->mpc_cpuflag & CPU_BOOTPROCESSOR) {
		bootup_cpu = " (Bootup-CPU)";
		boot_cpu_physical_apicid = m->mpc_apicid;
	}

	printk(KERN_INFO "Processor #%d%s\n", m->mpc_apicid, bootup_cpu);
	generic_processor_info(apicid, m->mpc_apicver);
}

static void __init MP_bus_info(struct mpc_config_bus *m)
{
	char str[7];

	memcpy(str, m->mpc_bustype, 6);
	str[6] = 0;

#ifdef CONFIG_X86_NUMAQ
	mpc_oem_bus_info(m, str, translation_table[mpc_record]);
#else
	Dprintk("Bus #%d is %s\n", m->mpc_busid, str);
#endif

#if MAX_MP_BUSSES < 256
	if (m->mpc_busid >= MAX_MP_BUSSES) {
		printk(KERN_WARNING "MP table busid value (%d) for bustype %s "
		       " is too large, max. supported is %d\n",
		       m->mpc_busid, str, MAX_MP_BUSSES - 1);
		return;
	}
#endif

	if (strncmp(str, BUSTYPE_ISA, sizeof(BUSTYPE_ISA) - 1) == 0) {
		 set_bit(m->mpc_busid, mp_bus_not_pci);
#if defined(CONFIG_EISA) || defined (CONFIG_MCA)
		mp_bus_id_to_type[m->mpc_busid] = MP_BUS_ISA;
#endif
	} else if (strncmp(str, BUSTYPE_PCI, sizeof(BUSTYPE_PCI) - 1) == 0) {
#ifdef CONFIG_X86_NUMAQ
		mpc_oem_pci_bus(m, translation_table[mpc_record]);
#endif
		clear_bit(m->mpc_busid, mp_bus_not_pci);
		mp_bus_id_to_pci_bus[m->mpc_busid] = mp_current_pci_id;
		mp_current_pci_id++;
#if defined(CONFIG_EISA) || defined (CONFIG_MCA)
		mp_bus_id_to_type[m->mpc_busid] = MP_BUS_PCI;
	} else if (strncmp(str, BUSTYPE_EISA, sizeof(BUSTYPE_EISA) - 1) == 0) {
		mp_bus_id_to_type[m->mpc_busid] = MP_BUS_EISA;
	} else if (strncmp(str, BUSTYPE_MCA, sizeof(BUSTYPE_MCA) - 1) == 0) {
		mp_bus_id_to_type[m->mpc_busid] = MP_BUS_MCA;
#endif
	} else
		printk(KERN_WARNING "Unknown bustype %s - ignoring\n", str);
}

#ifdef CONFIG_X86_IO_APIC

static int bad_ioapic(unsigned long address)
{
	if (nr_ioapics >= MAX_IO_APICS) {
		printk(KERN_ERR "ERROR: Max # of I/O APICs (%d) exceeded "
		       "(found %d)\n", MAX_IO_APICS, nr_ioapics);
		panic("Recompile kernel with bigger MAX_IO_APICS!\n");
	}
	if (!address) {
		printk(KERN_ERR "WARNING: Bogus (zero) I/O APIC address"
		       " found in table, skipping!\n");
		return 1;
	}
	return 0;
}

static void __init MP_ioapic_info(struct mpc_config_ioapic *m)
{
	if (!(m->mpc_flags & MPC_APIC_USABLE))
		return;

	printk(KERN_INFO "I/O APIC #%d Version %d at 0x%X.\n",
	       m->mpc_apicid, m->mpc_apicver, m->mpc_apicaddr);

	if (bad_ioapic(m->mpc_apicaddr))
		return;

	mp_ioapics[nr_ioapics] = *m;
	nr_ioapics++;
}

static void __init MP_intsrc_info(struct mpc_config_intsrc *m)
{
	mp_irqs[mp_irq_entries] = *m;
	Dprintk("Int: type %d, pol %d, trig %d, bus %d,"
		" IRQ %02x, APIC ID %x, APIC INT %02x\n",
		m->mpc_irqtype, m->mpc_irqflag & 3,
		(m->mpc_irqflag >> 2) & 3, m->mpc_srcbus,
		m->mpc_srcbusirq, m->mpc_dstapic, m->mpc_dstirq);
	if (++mp_irq_entries == MAX_IRQ_SOURCES)
		panic("Max # of irq sources exceeded!!\n");
}

#endif

static void __init MP_lintsrc_info(struct mpc_config_lintsrc *m)
{
	Dprintk("Lint: type %d, pol %d, trig %d, bus %d,"
		" IRQ %02x, APIC ID %x, APIC LINT %02x\n",
		m->mpc_irqtype, m->mpc_irqflag & 3,
		(m->mpc_irqflag >> 2) & 3, m->mpc_srcbusid,
		m->mpc_srcbusirq, m->mpc_destapic, m->mpc_destapiclint);
}

#ifdef CONFIG_X86_NUMAQ
static void __init MP_translation_info(struct mpc_config_translation *m)
{
	printk(KERN_INFO
	       "Translation: record %d, type %d, quad %d, global %d, local %d\n",
	       mpc_record, m->trans_type, m->trans_quad, m->trans_global,
	       m->trans_local);

	if (mpc_record >= MAX_MPC_ENTRY)
		printk(KERN_ERR "MAX_MPC_ENTRY exceeded!\n");
	else
		translation_table[mpc_record] = m;	/* stash this for later */
	if (m->trans_quad < MAX_NUMNODES && !node_online(m->trans_quad))
		node_set_online(m->trans_quad);
}

/*
 * Read/parse the MPC oem tables
 */

static void __init smp_read_mpc_oem(struct mp_config_oemtable *oemtable,
				    unsigned short oemsize)
{
	int count = sizeof(*oemtable);	/* the header size */
	unsigned char *oemptr = ((unsigned char *)oemtable) + count;

	mpc_record = 0;
	printk(KERN_INFO "Found an OEM MPC table at %8p - parsing it ... \n",
	       oemtable);
	if (memcmp(oemtable->oem_signature, MPC_OEM_SIGNATURE, 4)) {
		printk(KERN_WARNING
		       "SMP mpc oemtable: bad signature [%c%c%c%c]!\n",
		       oemtable->oem_signature[0], oemtable->oem_signature[1],
		       oemtable->oem_signature[2], oemtable->oem_signature[3]);
		return;
	}
	if (mpf_checksum((unsigned char *)oemtable, oemtable->oem_length)) {
		printk(KERN_WARNING "SMP oem mptable: checksum error!\n");
		return;
	}
	while (count < oemtable->oem_length) {
		switch (*oemptr) {
		case MP_TRANSLATION:
			{
				struct mpc_config_translation *m =
				    (struct mpc_config_translation *)oemptr;
				MP_translation_info(m);
				oemptr += sizeof(*m);
				count += sizeof(*m);
				++mpc_record;
				break;
			}
		default:
			{
				printk(KERN_WARNING
				       "Unrecognised OEM table entry type! - %d\n",
				       (int)*oemptr);
				return;
			}
		}
	}
}

static inline void mps_oem_check(struct mp_config_table *mpc, char *oem,
				 char *productid)
{
	if (strncmp(oem, "IBM NUMA", 8))
		printk("Warning!  May not be a NUMA-Q system!\n");
	if (mpc->mpc_oemptr)
		smp_read_mpc_oem((struct mp_config_oemtable *)mpc->mpc_oemptr,
				 mpc->mpc_oemsize);
}
#endif /* CONFIG_X86_NUMAQ */

/*
 * Read/parse the MPC
 */

static int __init smp_read_mpc(struct mp_config_table *mpc, unsigned early)
{
	char str[16];
	char oem[10];
	int count = sizeof(*mpc);
	unsigned char *mpt = ((unsigned char *)mpc) + count;

	if (memcmp(mpc->mpc_signature, MPC_SIGNATURE, 4)) {
		printk(KERN_ERR "MPTABLE: bad signature [%c%c%c%c]!\n",
		       mpc->mpc_signature[0], mpc->mpc_signature[1],
		       mpc->mpc_signature[2], mpc->mpc_signature[3]);
		return 0;
	}
	if (mpf_checksum((unsigned char *)mpc, mpc->mpc_length)) {
		printk(KERN_ERR "MPTABLE: checksum error!\n");
		return 0;
	}
	if (mpc->mpc_spec != 0x01 && mpc->mpc_spec != 0x04) {
		printk(KERN_ERR "MPTABLE: bad table version (%d)!!\n",
		       mpc->mpc_spec);
		return 0;
	}
	if (!mpc->mpc_lapic) {
		printk(KERN_ERR "MPTABLE: null local APIC address!\n");
		return 0;
	}
	memcpy(oem, mpc->mpc_oem, 8);
	oem[8] = 0;
	printk(KERN_INFO "MPTABLE: OEM ID: %s ", oem);

	memcpy(str, mpc->mpc_productid, 12);
	str[12] = 0;
	printk("Product ID: %s ", str);

#ifdef CONFIG_X86_32
	mps_oem_check(mpc, oem, str);
#endif
	printk(KERN_INFO "MPTABLE: Product ID: %s ", str);

	printk(KERN_INFO "MPTABLE: APIC at: 0x%X\n", mpc->mpc_lapic);

	/* save the local APIC address, it might be non-default */
	if (!acpi_lapic)
		mp_lapic_addr = mpc->mpc_lapic;

	if (early)
		return 1;

	/*
	 *      Now process the configuration blocks.
	 */
#ifdef CONFIG_X86_NUMAQ
	mpc_record = 0;
#endif
	while (count < mpc->mpc_length) {
		switch (*mpt) {
		case MP_PROCESSOR:
			{
				struct mpc_config_processor *m =
				    (struct mpc_config_processor *)mpt;
				/* ACPI may have already provided this data */
				if (!acpi_lapic)
					MP_processor_info(m);
				mpt += sizeof(*m);
				count += sizeof(*m);
				break;
			}
		case MP_BUS:
			{
				struct mpc_config_bus *m =
				    (struct mpc_config_bus *)mpt;
				MP_bus_info(m);
				mpt += sizeof(*m);
				count += sizeof(*m);
				break;
			}
		case MP_IOAPIC:
			{
#ifdef CONFIG_X86_IO_APIC
				struct mpc_config_ioapic *m =
				    (struct mpc_config_ioapic *)mpt;
				MP_ioapic_info(m);
#endif
				mpt += sizeof(struct mpc_config_ioapic);
				count += sizeof(struct mpc_config_ioapic);
				break;
			}
		case MP_INTSRC:
			{
#ifdef CONFIG_X86_IO_APIC
				struct mpc_config_intsrc *m =
				    (struct mpc_config_intsrc *)mpt;

				MP_intsrc_info(m);
#endif
				mpt += sizeof(struct mpc_config_intsrc);
				count += sizeof(struct mpc_config_intsrc);
				break;
			}
		case MP_LINTSRC:
			{
				struct mpc_config_lintsrc *m =
				    (struct mpc_config_lintsrc *)mpt;
				MP_lintsrc_info(m);
				mpt += sizeof(*m);
				count += sizeof(*m);
				break;
			}
		default:
			/* wrong mptable */
			printk(KERN_ERR "Your mptable is wrong, contact your HW vendor!\n");
			printk(KERN_ERR "type %x\n", *mpt);
			print_hex_dump(KERN_ERR, "  ", DUMP_PREFIX_ADDRESS, 16,
					1, mpc, mpc->mpc_length, 1);
			count = mpc->mpc_length;
			break;
		}
#ifdef CONFIG_X86_NUMAQ
		++mpc_record;
#endif
	}
	setup_apic_routing();
	if (!num_processors)
		printk(KERN_ERR "MPTABLE: no processors registered!\n");
	return num_processors;
}

#ifdef CONFIG_X86_IO_APIC

static int __init ELCR_trigger(unsigned int irq)
{
	unsigned int port;

	port = 0x4d0 + (irq >> 3);
	return (inb(port) >> (irq & 7)) & 1;
}

static void __init construct_default_ioirq_mptable(int mpc_default_type)
{
	struct mpc_config_intsrc intsrc;
	int i;
	int ELCR_fallback = 0;

	intsrc.mpc_type = MP_INTSRC;
	intsrc.mpc_irqflag = 0;	/* conforming */
	intsrc.mpc_srcbus = 0;
	intsrc.mpc_dstapic = mp_ioapics[0].mpc_apicid;

	intsrc.mpc_irqtype = mp_INT;

	/*
	 *  If true, we have an ISA/PCI system with no IRQ entries
	 *  in the MP table. To prevent the PCI interrupts from being set up
	 *  incorrectly, we try to use the ELCR. The sanity check to see if
	 *  there is good ELCR data is very simple - IRQ0, 1, 2 and 13 can
	 *  never be level sensitive, so we simply see if the ELCR agrees.
	 *  If it does, we assume it's valid.
	 */
	if (mpc_default_type == 5) {
		printk(KERN_INFO "ISA/PCI bus type with no IRQ information... "
		       "falling back to ELCR\n");

		if (ELCR_trigger(0) || ELCR_trigger(1) || ELCR_trigger(2) ||
		    ELCR_trigger(13))
			printk(KERN_ERR "ELCR contains invalid data... "
			       "not using ELCR\n");
		else {
			printk(KERN_INFO
			       "Using ELCR to identify PCI interrupts\n");
			ELCR_fallback = 1;
		}
	}

	for (i = 0; i < 16; i++) {
		switch (mpc_default_type) {
		case 2:
			if (i == 0 || i == 13)
				continue;	/* IRQ0 & IRQ13 not connected */
			/* fall through */
		default:
			if (i == 2)
				continue;	/* IRQ2 is never connected */
		}

		if (ELCR_fallback) {
			/*
			 *  If the ELCR indicates a level-sensitive interrupt, we
			 *  copy that information over to the MP table in the
			 *  irqflag field (level sensitive, active high polarity).
			 */
			if (ELCR_trigger(i))
				intsrc.mpc_irqflag = 13;
			else
				intsrc.mpc_irqflag = 0;
		}

		intsrc.mpc_srcbusirq = i;
		intsrc.mpc_dstirq = i ? i : 2;	/* IRQ0 to INTIN2 */
		MP_intsrc_info(&intsrc);
	}

	intsrc.mpc_irqtype = mp_ExtINT;
	intsrc.mpc_srcbusirq = 0;
	intsrc.mpc_dstirq = 0;	/* 8259A to INTIN0 */
	MP_intsrc_info(&intsrc);
}

#endif

static inline void __init construct_default_ISA_mptable(int mpc_default_type)
{
	struct mpc_config_processor processor;
	struct mpc_config_bus bus;
#ifdef CONFIG_X86_IO_APIC
	struct mpc_config_ioapic ioapic;
#endif
	struct mpc_config_lintsrc lintsrc;
	int linttypes[2] = { mp_ExtINT, mp_NMI };
	int i;

	/*
	 * local APIC has default address
	 */
	mp_lapic_addr = APIC_DEFAULT_PHYS_BASE;

	/*
	 * 2 CPUs, numbered 0 & 1.
	 */
	processor.mpc_type = MP_PROCESSOR;
	/* Either an integrated APIC or a discrete 82489DX. */
	processor.mpc_apicver = mpc_default_type > 4 ? 0x10 : 0x01;
	processor.mpc_cpuflag = CPU_ENABLED;
	processor.mpc_cpufeature = (boot_cpu_data.x86 << 8) |
	    (boot_cpu_data.x86_model << 4) | boot_cpu_data.x86_mask;
	processor.mpc_featureflag = boot_cpu_data.x86_capability[0];
	processor.mpc_reserved[0] = 0;
	processor.mpc_reserved[1] = 0;
	for (i = 0; i < 2; i++) {
		processor.mpc_apicid = i;
		MP_processor_info(&processor);
	}

	bus.mpc_type = MP_BUS;
	bus.mpc_busid = 0;
	switch (mpc_default_type) {
	default:
		printk(KERN_ERR "???\nUnknown standard configuration %d\n",
		       mpc_default_type);
		/* fall through */
	case 1:
	case 5:
		memcpy(bus.mpc_bustype, "ISA   ", 6);
		break;
	case 2:
	case 6:
	case 3:
		memcpy(bus.mpc_bustype, "EISA  ", 6);
		break;
	case 4:
	case 7:
		memcpy(bus.mpc_bustype, "MCA   ", 6);
	}
	MP_bus_info(&bus);
	if (mpc_default_type > 4) {
		bus.mpc_busid = 1;
		memcpy(bus.mpc_bustype, "PCI   ", 6);
		MP_bus_info(&bus);
	}

#ifdef CONFIG_X86_IO_APIC
	ioapic.mpc_type = MP_IOAPIC;
	ioapic.mpc_apicid = 2;
	ioapic.mpc_apicver = mpc_default_type > 4 ? 0x10 : 0x01;
	ioapic.mpc_flags = MPC_APIC_USABLE;
	ioapic.mpc_apicaddr = 0xFEC00000;
	MP_ioapic_info(&ioapic);

	/*
	 * We set up most of the low 16 IO-APIC pins according to MPS rules.
	 */
	construct_default_ioirq_mptable(mpc_default_type);
#endif
	lintsrc.mpc_type = MP_LINTSRC;
	lintsrc.mpc_irqflag = 0;	/* conforming */
	lintsrc.mpc_srcbusid = 0;
	lintsrc.mpc_srcbusirq = 0;
	lintsrc.mpc_destapic = MP_APIC_ALL;
	for (i = 0; i < 2; i++) {
		lintsrc.mpc_irqtype = linttypes[i];
		lintsrc.mpc_destapiclint = i;
		MP_lintsrc_info(&lintsrc);
	}
}

static struct intel_mp_floating *mpf_found;

/*
 * Scan the memory blocks for an SMP configuration block.
 */
static void __init __get_smp_config(unsigned early)
{
	struct intel_mp_floating *mpf = mpf_found;

	if (acpi_lapic && early)
		return;
	/*
	 * ACPI supports both logical (e.g. Hyper-Threading) and physical
	 * processors, where MPS only supports physical.
	 */
	if (acpi_lapic && acpi_ioapic) {
		printk(KERN_INFO "Using ACPI (MADT) for SMP configuration "
		       "information\n");
		return;
	} else if (acpi_lapic)
		printk(KERN_INFO "Using ACPI for processor (LAPIC) "
		       "configuration information\n");

	printk(KERN_INFO "Intel MultiProcessor Specification v1.%d\n",
	       mpf->mpf_specification);
#ifdef CONFIG_X86_32
	if (mpf->mpf_feature2 & (1 << 7)) {
		printk(KERN_INFO "    IMCR and PIC compatibility mode.\n");
		pic_mode = 1;
	} else {
		printk(KERN_INFO "    Virtual Wire compatibility mode.\n");
		pic_mode = 0;
	}
#endif
	/*
	 * Now see if we need to read further.
	 */
	if (mpf->mpf_feature1 != 0) {
		if (early) {
			/*
			 * local APIC has default address
			 */
			mp_lapic_addr = APIC_DEFAULT_PHYS_BASE;
			return;
		}

		printk(KERN_INFO "Default MP configuration #%d\n",
		       mpf->mpf_feature1);
		construct_default_ISA_mptable(mpf->mpf_feature1);

	} else if (mpf->mpf_physptr) {

		/*
		 * Read the physical hardware table.  Anything here will
		 * override the defaults.
		 */
		if (!smp_read_mpc(phys_to_virt(mpf->mpf_physptr), early)) {
			smp_found_config = 0;
			printk(KERN_ERR
			       "BIOS bug, MP table errors detected!...\n");
			printk(KERN_ERR "... disabling SMP support. "
			       "(tell your hw vendor)\n");
			return;
		}

		if (early)
			return;
#ifdef CONFIG_X86_IO_APIC
		/*
		 * If there are no explicit MP IRQ entries, then we are
		 * broken.  We set up most of the low 16 IO-APIC pins to
		 * ISA defaults and hope it will work.
		 */
		if (!mp_irq_entries) {
			struct mpc_config_bus bus;

			printk(KERN_ERR "BIOS bug, no explicit IRQ entries, "
			       "using default mptable. "
			       "(tell your hw vendor)\n");

			bus.mpc_type = MP_BUS;
			bus.mpc_busid = 0;
			memcpy(bus.mpc_bustype, "ISA   ", 6);
			MP_bus_info(&bus);

			construct_default_ioirq_mptable(0);
		}
#endif
	} else
		BUG();

	if (!early)
		printk(KERN_INFO "Processors: %d\n", num_processors);
	/*
	 * Only use the first configuration found.
	 */
}

void __init early_get_smp_config(void)
{
	__get_smp_config(1);
}

void __init get_smp_config(void)
{
	__get_smp_config(0);
}

static int __init smp_scan_config(unsigned long base, unsigned long length,
				  unsigned reserve)
{
	unsigned int *bp = phys_to_virt(base);
	struct intel_mp_floating *mpf;

	Dprintk("Scan SMP from %p for %ld bytes.\n", bp, length);
	BUILD_BUG_ON(sizeof(*mpf) != 16);

	while (length > 0) {
		mpf = (struct intel_mp_floating *)bp;
		if ((*bp == SMP_MAGIC_IDENT) &&
		    (mpf->mpf_length == 1) &&
		    !mpf_checksum((unsigned char *)bp, 16) &&
		    ((mpf->mpf_specification == 1)
		     || (mpf->mpf_specification == 4))) {

			smp_found_config = 1;
			mpf_found = mpf;
#ifdef CONFIG_X86_32
			printk(KERN_INFO "found SMP MP-table at [%p] %08lx\n",
			       mpf, virt_to_phys(mpf));
			reserve_bootmem(virt_to_phys(mpf), PAGE_SIZE,
					BOOTMEM_DEFAULT);
			if (mpf->mpf_physptr) {
				/*
				 * We cannot access to MPC table to compute
				 * table size yet, as only few megabytes from
				 * the bottom is mapped now.
				 * PC-9800's MPC table places on the very last
				 * of physical memory; so that simply reserving
				 * PAGE_SIZE from mpg->mpf_physptr yields BUG()
				 * in reserve_bootmem.
				 */
				unsigned long size = PAGE_SIZE;
				unsigned long end = max_low_pfn * PAGE_SIZE;
				if (mpf->mpf_physptr + size > end)
					size = end - mpf->mpf_physptr;
				reserve_bootmem(mpf->mpf_physptr, size,
						BOOTMEM_DEFAULT);
			}

#else
			if (!reserve)
				return 1;

			reserve_bootmem_generic(virt_to_phys(mpf), PAGE_SIZE);
			if (mpf->mpf_physptr)
				reserve_bootmem_generic(mpf->mpf_physptr,
							PAGE_SIZE);
#endif
		return 1;
		}
		bp += 4;
		length -= 16;
	}
	return 0;
}

static void __init __find_smp_config(unsigned reserve)
{
	unsigned int address;

	/*
	 * FIXME: Linux assumes you have 640K of base ram..
	 * this continues the error...
	 *
	 * 1) Scan the bottom 1K for a signature
	 * 2) Scan the top 1K of base RAM
	 * 3) Scan the 64K of bios
	 */
	if (smp_scan_config(0x0, 0x400, reserve) ||
	    smp_scan_config(639 * 0x400, 0x400, reserve) ||
	    smp_scan_config(0xF0000, 0x10000, reserve))
		return;
	/*
	 * If it is an SMP machine we should know now, unless the
	 * configuration is in an EISA/MCA bus machine with an
	 * extended bios data area.
	 *
	 * there is a real-mode segmented pointer pointing to the
	 * 4K EBDA area at 0x40E, calculate and scan it here.
	 *
	 * NOTE! There are Linux loaders that will corrupt the EBDA
	 * area, and as such this kind of SMP config may be less
	 * trustworthy, simply because the SMP table may have been
	 * stomped on during early boot. These loaders are buggy and
	 * should be fixed.
	 *
	 * MP1.4 SPEC states to only scan first 1K of 4K EBDA.
	 */

	address = get_bios_ebda();
	if (address)
		smp_scan_config(address, 0x400, reserve);
}

void __init early_find_smp_config(void)
{
	__find_smp_config(0);
}

void __init find_smp_config(void)
{
	__find_smp_config(1);
}

/* --------------------------------------------------------------------------
                            ACPI-based MP Configuration
   -------------------------------------------------------------------------- */

/*
 * Keep this outside and initialized to 0, for !CONFIG_ACPI builds:
 */
int es7000_plat;

#ifdef CONFIG_ACPI

#ifdef	CONFIG_X86_IO_APIC

#define MP_ISA_BUS		0

extern struct mp_ioapic_routing mp_ioapic_routing[MAX_IO_APICS];

static int mp_find_ioapic(int gsi)
{
	int i = 0;

	/* Find the IOAPIC that manages this GSI. */
	for (i = 0; i < nr_ioapics; i++) {
		if ((gsi >= mp_ioapic_routing[i].gsi_base)
		    && (gsi <= mp_ioapic_routing[i].gsi_end))
			return i;
	}

	printk(KERN_ERR "ERROR: Unable to locate IOAPIC for GSI %d\n", gsi);
	return -1;
}

static u8 __init uniq_ioapic_id(u8 id)
{
#ifdef CONFIG_X86_32
	if ((boot_cpu_data.x86_vendor == X86_VENDOR_INTEL) &&
	    !APIC_XAPIC(apic_version[boot_cpu_physical_apicid]))
		return io_apic_get_unique_id(nr_ioapics, id);
	else
		return id;
#else
	int i;
	DECLARE_BITMAP(used, 256);
	bitmap_zero(used, 256);
	for (i = 0; i < nr_ioapics; i++) {
		struct mpc_config_ioapic *ia = &mp_ioapics[i];
		__set_bit(ia->mpc_apicid, used);
	}
	if (!test_bit(id, used))
		return id;
	return find_first_zero_bit(used, 256);
#endif
}

void __init mp_register_ioapic(int id, u32 address, u32 gsi_base)
{
	int idx = 0;

	if (bad_ioapic(address))
		return;

	idx = nr_ioapics;

	mp_ioapics[idx].mpc_type = MP_IOAPIC;
	mp_ioapics[idx].mpc_flags = MPC_APIC_USABLE;
	mp_ioapics[idx].mpc_apicaddr = address;

	set_fixmap_nocache(FIX_IO_APIC_BASE_0 + idx, address);
	mp_ioapics[idx].mpc_apicid = uniq_ioapic_id(id);
#ifdef CONFIG_X86_32
	mp_ioapics[idx].mpc_apicver = io_apic_get_version(idx);
#else
	mp_ioapics[idx].mpc_apicver = 0;
#endif
	/*
	 * Build basic GSI lookup table to facilitate gsi->io_apic lookups
	 * and to prevent reprogramming of IOAPIC pins (PCI GSIs).
	 */
	mp_ioapic_routing[idx].apic_id = mp_ioapics[idx].mpc_apicid;
	mp_ioapic_routing[idx].gsi_base = gsi_base;
	mp_ioapic_routing[idx].gsi_end = gsi_base +
	    io_apic_get_redir_entries(idx);

	printk(KERN_INFO "IOAPIC[%d]: apic_id %d, version %d, address 0x%x, "
	       "GSI %d-%d\n", idx, mp_ioapics[idx].mpc_apicid,
	       mp_ioapics[idx].mpc_apicver, mp_ioapics[idx].mpc_apicaddr,
	       mp_ioapic_routing[idx].gsi_base, mp_ioapic_routing[idx].gsi_end);

	nr_ioapics++;
}

void __init mp_override_legacy_irq(u8 bus_irq, u8 polarity, u8 trigger, u32 gsi)
{
	struct mpc_config_intsrc intsrc;
	int ioapic = -1;
	int pin = -1;

	/*
	 * Convert 'gsi' to 'ioapic.pin'.
	 */
	ioapic = mp_find_ioapic(gsi);
	if (ioapic < 0)
		return;
	pin = gsi - mp_ioapic_routing[ioapic].gsi_base;

	/*
	 * TBD: This check is for faulty timer entries, where the override
	 *      erroneously sets the trigger to level, resulting in a HUGE
	 *      increase of timer interrupts!
	 */
	if ((bus_irq == 0) && (trigger == 3))
		trigger = 1;

	intsrc.mpc_type = MP_INTSRC;
	intsrc.mpc_irqtype = mp_INT;
	intsrc.mpc_irqflag = (trigger << 2) | polarity;
	intsrc.mpc_srcbus = MP_ISA_BUS;
	intsrc.mpc_srcbusirq = bus_irq;	/* IRQ */
	intsrc.mpc_dstapic = mp_ioapics[ioapic].mpc_apicid;	/* APIC ID */
	intsrc.mpc_dstirq = pin;	/* INTIN# */

	MP_intsrc_info(&intsrc);
}

void __init mp_config_acpi_legacy_irqs(void)
{
	struct mpc_config_intsrc intsrc;
	int i = 0;
	int ioapic = -1;

#if defined (CONFIG_MCA) || defined (CONFIG_EISA)
	/*
	 * Fabricate the legacy ISA bus (bus #31).
	 */
	mp_bus_id_to_type[MP_ISA_BUS] = MP_BUS_ISA;
#endif
	set_bit(MP_ISA_BUS, mp_bus_not_pci);
	Dprintk("Bus #%d is ISA\n", MP_ISA_BUS);

	/*
	 * Older generations of ES7000 have no legacy identity mappings
	 */
	if (es7000_plat == 1)
		return;

	/*
	 * Locate the IOAPIC that manages the ISA IRQs (0-15).
	 */
	ioapic = mp_find_ioapic(0);
	if (ioapic < 0)
		return;

	intsrc.mpc_type = MP_INTSRC;
	intsrc.mpc_irqflag = 0;	/* Conforming */
	intsrc.mpc_srcbus = MP_ISA_BUS;
#ifdef CONFIG_X86_IO_APIC
	intsrc.mpc_dstapic = mp_ioapics[ioapic].mpc_apicid;
#endif
	/*
	 * Use the default configuration for the IRQs 0-15.  Unless
	 * overridden by (MADT) interrupt source override entries.
	 */
	for (i = 0; i < 16; i++) {
		int idx;

		for (idx = 0; idx < mp_irq_entries; idx++) {
			struct mpc_config_intsrc *irq = mp_irqs + idx;

			/* Do we already have a mapping for this ISA IRQ? */
			if (irq->mpc_srcbus == MP_ISA_BUS
			    && irq->mpc_srcbusirq == i)
				break;

			/* Do we already have a mapping for this IOAPIC pin */
			if ((irq->mpc_dstapic == intsrc.mpc_dstapic) &&
			    (irq->mpc_dstirq == i))
				break;
		}

		if (idx != mp_irq_entries) {
			printk(KERN_DEBUG "ACPI: IRQ%d used by override.\n", i);
			continue;	/* IRQ already used */
		}

		intsrc.mpc_irqtype = mp_INT;
		intsrc.mpc_srcbusirq = i;	/* Identity mapped */
		intsrc.mpc_dstirq = i;

		MP_intsrc_info(&intsrc);
	}
}

int mp_register_gsi(u32 gsi, int triggering, int polarity)
{
	int ioapic;
	int ioapic_pin;
#ifdef CONFIG_X86_32
#define MAX_GSI_NUM	4096
#define IRQ_COMPRESSION_START	64

	static int pci_irq = IRQ_COMPRESSION_START;
	/*
	 * Mapping between Global System Interrupts, which
	 * represent all possible interrupts, and IRQs
	 * assigned to actual devices.
	 */
	static int gsi_to_irq[MAX_GSI_NUM];
#else

	if (acpi_irq_model != ACPI_IRQ_MODEL_IOAPIC)
		return gsi;
#endif

	/* Don't set up the ACPI SCI because it's already set up */
	if (acpi_gbl_FADT.sci_interrupt == gsi)
		return gsi;

	ioapic = mp_find_ioapic(gsi);
	if (ioapic < 0) {
		printk(KERN_WARNING "No IOAPIC for GSI %u\n", gsi);
		return gsi;
	}

	ioapic_pin = gsi - mp_ioapic_routing[ioapic].gsi_base;

#ifdef CONFIG_X86_32
	if (ioapic_renumber_irq)
		gsi = ioapic_renumber_irq(ioapic, gsi);
#endif

	/*
	 * Avoid pin reprogramming.  PRTs typically include entries
	 * with redundant pin->gsi mappings (but unique PCI devices);
	 * we only program the IOAPIC on the first.
	 */
	if (ioapic_pin > MP_MAX_IOAPIC_PIN) {
		printk(KERN_ERR "Invalid reference to IOAPIC pin "
		       "%d-%d\n", mp_ioapic_routing[ioapic].apic_id,
		       ioapic_pin);
		return gsi;
	}
	if (test_bit(ioapic_pin, mp_ioapic_routing[ioapic].pin_programmed)) {
		Dprintk(KERN_DEBUG "Pin %d-%d already programmed\n",
			mp_ioapic_routing[ioapic].apic_id, ioapic_pin);
#ifdef CONFIG_X86_32
		return (gsi < IRQ_COMPRESSION_START ? gsi : gsi_to_irq[gsi]);
#else
		return gsi;
#endif
	}

	set_bit(ioapic_pin, mp_ioapic_routing[ioapic].pin_programmed);
#ifdef CONFIG_X86_32
	/*
	 * For GSI >= 64, use IRQ compression
	 */
	if ((gsi >= IRQ_COMPRESSION_START)
	    && (triggering == ACPI_LEVEL_SENSITIVE)) {
		/*
		 * For PCI devices assign IRQs in order, avoiding gaps
		 * due to unused I/O APIC pins.
		 */
		int irq = gsi;
		if (gsi < MAX_GSI_NUM) {
			/*
			 * Retain the VIA chipset work-around (gsi > 15), but
			 * avoid a problem where the 8254 timer (IRQ0) is setup
			 * via an override (so it's not on pin 0 of the ioapic),
			 * and at the same time, the pin 0 interrupt is a PCI
			 * type.  The gsi > 15 test could cause these two pins
			 * to be shared as IRQ0, and they are not shareable.
			 * So test for this condition, and if necessary, avoid
			 * the pin collision.
			 */
			gsi = pci_irq++;
			/*
			 * Don't assign IRQ used by ACPI SCI
			 */
			if (gsi == acpi_gbl_FADT.sci_interrupt)
				gsi = pci_irq++;
			gsi_to_irq[irq] = gsi;
		} else {
			printk(KERN_ERR "GSI %u is too high\n", gsi);
			return gsi;
		}
	}
#endif
	io_apic_set_pci_routing(ioapic, ioapic_pin, gsi,
				triggering == ACPI_EDGE_SENSITIVE ? 0 : 1,
				polarity == ACPI_ACTIVE_HIGH ? 0 : 1);
	return gsi;
}

#endif /* CONFIG_X86_IO_APIC */
#endif /* CONFIG_ACPI */
