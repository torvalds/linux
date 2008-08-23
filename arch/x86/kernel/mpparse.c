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
#include <asm/e820.h>
#include <asm/trampoline.h>
#include <asm/setup.h>

#include <mach_apic.h>
#ifdef CONFIG_X86_32
#include <mach_apicdef.h>
#include <mach_mpparse.h>
#endif

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

static void __cpuinit MP_processor_info(struct mpc_config_processor *m)
{
	int apicid;
	char *bootup_cpu = "";

	if (!(m->mpc_cpuflag & CPU_ENABLED)) {
		disabled_cpus++;
		return;
	}

	if (x86_quirks->mpc_apic_id)
		apicid = x86_quirks->mpc_apic_id(m);
	else
		apicid = m->mpc_apicid;

	if (m->mpc_cpuflag & CPU_BOOTPROCESSOR) {
		bootup_cpu = " (Bootup-CPU)";
		boot_cpu_physical_apicid = m->mpc_apicid;
	}

	printk(KERN_INFO "Processor #%d%s\n", m->mpc_apicid, bootup_cpu);
	generic_processor_info(apicid, m->mpc_apicver);
}

#ifdef CONFIG_X86_IO_APIC
static void __init MP_bus_info(struct mpc_config_bus *m)
{
	char str[7];
	memcpy(str, m->mpc_bustype, 6);
	str[6] = 0;

	if (x86_quirks->mpc_oem_bus_info)
		x86_quirks->mpc_oem_bus_info(m, str);
	else
		apic_printk(APIC_VERBOSE, "Bus #%d is %s\n", m->mpc_busid, str);

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
		if (x86_quirks->mpc_oem_pci_bus)
			x86_quirks->mpc_oem_pci_bus(m);

		clear_bit(m->mpc_busid, mp_bus_not_pci);
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
#endif

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

	mp_ioapics[nr_ioapics].mp_apicaddr = m->mpc_apicaddr;
	mp_ioapics[nr_ioapics].mp_apicid = m->mpc_apicid;
	mp_ioapics[nr_ioapics].mp_type = m->mpc_type;
	mp_ioapics[nr_ioapics].mp_apicver = m->mpc_apicver;
	mp_ioapics[nr_ioapics].mp_flags = m->mpc_flags;
	nr_ioapics++;
}

static void print_MP_intsrc_info(struct mpc_config_intsrc *m)
{
	apic_printk(APIC_VERBOSE, "Int: type %d, pol %d, trig %d, bus %02x,"
		" IRQ %02x, APIC ID %x, APIC INT %02x\n",
		m->mpc_irqtype, m->mpc_irqflag & 3,
		(m->mpc_irqflag >> 2) & 3, m->mpc_srcbus,
		m->mpc_srcbusirq, m->mpc_dstapic, m->mpc_dstirq);
}

static void __init print_mp_irq_info(struct mp_config_intsrc *mp_irq)
{
	apic_printk(APIC_VERBOSE, "Int: type %d, pol %d, trig %d, bus %02x,"
		" IRQ %02x, APIC ID %x, APIC INT %02x\n",
		mp_irq->mp_irqtype, mp_irq->mp_irqflag & 3,
		(mp_irq->mp_irqflag >> 2) & 3, mp_irq->mp_srcbus,
		mp_irq->mp_srcbusirq, mp_irq->mp_dstapic, mp_irq->mp_dstirq);
}

static void __init assign_to_mp_irq(struct mpc_config_intsrc *m,
				    struct mp_config_intsrc *mp_irq)
{
	mp_irq->mp_dstapic = m->mpc_dstapic;
	mp_irq->mp_type = m->mpc_type;
	mp_irq->mp_irqtype = m->mpc_irqtype;
	mp_irq->mp_irqflag = m->mpc_irqflag;
	mp_irq->mp_srcbus = m->mpc_srcbus;
	mp_irq->mp_srcbusirq = m->mpc_srcbusirq;
	mp_irq->mp_dstirq = m->mpc_dstirq;
}

static void __init assign_to_mpc_intsrc(struct mp_config_intsrc *mp_irq,
					struct mpc_config_intsrc *m)
{
	m->mpc_dstapic = mp_irq->mp_dstapic;
	m->mpc_type = mp_irq->mp_type;
	m->mpc_irqtype = mp_irq->mp_irqtype;
	m->mpc_irqflag = mp_irq->mp_irqflag;
	m->mpc_srcbus = mp_irq->mp_srcbus;
	m->mpc_srcbusirq = mp_irq->mp_srcbusirq;
	m->mpc_dstirq = mp_irq->mp_dstirq;
}

static int __init mp_irq_mpc_intsrc_cmp(struct mp_config_intsrc *mp_irq,
					struct mpc_config_intsrc *m)
{
	if (mp_irq->mp_dstapic != m->mpc_dstapic)
		return 1;
	if (mp_irq->mp_type != m->mpc_type)
		return 2;
	if (mp_irq->mp_irqtype != m->mpc_irqtype)
		return 3;
	if (mp_irq->mp_irqflag != m->mpc_irqflag)
		return 4;
	if (mp_irq->mp_srcbus != m->mpc_srcbus)
		return 5;
	if (mp_irq->mp_srcbusirq != m->mpc_srcbusirq)
		return 6;
	if (mp_irq->mp_dstirq != m->mpc_dstirq)
		return 7;

	return 0;
}

static void __init MP_intsrc_info(struct mpc_config_intsrc *m)
{
	int i;

	print_MP_intsrc_info(m);

	for (i = 0; i < mp_irq_entries; i++) {
		if (!mp_irq_mpc_intsrc_cmp(&mp_irqs[i], m))
			return;
	}

	assign_to_mp_irq(m, &mp_irqs[mp_irq_entries]);
	if (++mp_irq_entries == MAX_IRQ_SOURCES)
		panic("Max # of irq sources exceeded!!\n");
}

#endif

static void __init MP_lintsrc_info(struct mpc_config_lintsrc *m)
{
	apic_printk(APIC_VERBOSE, "Lint: type %d, pol %d, trig %d, bus %02x,"
		" IRQ %02x, APIC ID %x, APIC LINT %02x\n",
		m->mpc_irqtype, m->mpc_irqflag & 3,
		(m->mpc_irqflag >> 2) & 3, m->mpc_srcbusid,
		m->mpc_srcbusirq, m->mpc_destapic, m->mpc_destapiclint);
}

/*
 * Read/parse the MPC
 */

static int __init smp_check_mpc(struct mp_config_table *mpc, char *oem,
				char *str)
{

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
	printk(KERN_INFO "MPTABLE: OEM ID: %s\n", oem);

	memcpy(str, mpc->mpc_productid, 12);
	str[12] = 0;

	printk(KERN_INFO "MPTABLE: Product ID: %s\n", str);

	printk(KERN_INFO "MPTABLE: APIC at: 0x%X\n", mpc->mpc_lapic);

	return 1;
}

static int __init smp_read_mpc(struct mp_config_table *mpc, unsigned early)
{
	char str[16];
	char oem[10];

	int count = sizeof(*mpc);
	unsigned char *mpt = ((unsigned char *)mpc) + count;

	if (!smp_check_mpc(mpc, oem, str))
		return 0;

#ifdef CONFIG_X86_32
	/*
	 * need to make sure summit and es7000's mps_oem_check is safe to be
	 * called early via genericarch 's mps_oem_check
	 */
	if (early) {
#ifdef CONFIG_X86_NUMAQ
		numaq_mps_oem_check(mpc, oem, str);
#endif
	} else
		mps_oem_check(mpc, oem, str);
#endif
	/* save the local APIC address, it might be non-default */
	if (!acpi_lapic)
		mp_lapic_addr = mpc->mpc_lapic;

	if (early)
		return 1;

	if (mpc->mpc_oemptr && x86_quirks->smp_read_mpc_oem) {
		struct mp_config_oemtable *oem_table = (struct mp_config_oemtable *)(unsigned long)mpc->mpc_oemptr;
		x86_quirks->smp_read_mpc_oem(oem_table, mpc->mpc_oemsize);
	}

	/*
	 *      Now process the configuration blocks.
	 */
	if (x86_quirks->mpc_record)
		*x86_quirks->mpc_record = 0;

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
#ifdef CONFIG_X86_IO_APIC
				MP_bus_info(m);
#endif
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
		if (x86_quirks->mpc_record)
			(*x86_quirks->mpc_record)++;
	}

#ifdef CONFIG_X86_GENERICARCH
       generic_bigsmp_probe();
#endif

#ifdef CONFIG_X86_32
	setup_apic_routing();
#endif
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
	intsrc.mpc_dstapic = mp_ioapics[0].mp_apicid;

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


static void construct_ioapic_table(int mpc_default_type)
{
	struct mpc_config_ioapic ioapic;
	struct mpc_config_bus bus;

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
}
#else
static inline void construct_ioapic_table(int mpc_default_type) { }
#endif

static inline void __init construct_default_ISA_mptable(int mpc_default_type)
{
	struct mpc_config_processor processor;
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

	construct_ioapic_table(mpc_default_type);

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
static void __init __get_smp_config(unsigned int early)
{
	struct intel_mp_floating *mpf = mpf_found;

	if (x86_quirks->mach_get_smp_config) {
		if (x86_quirks->mach_get_smp_config(early))
			return;
	}
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
#if defined(CONFIG_X86_LOCAL_APIC) && defined(CONFIG_X86_32)
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
#ifdef CONFIG_X86_LOCAL_APIC
			smp_found_config = 0;
#endif
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

	apic_printk(APIC_VERBOSE, "Scan SMP from %p for %ld bytes.\n",
			bp, length);
	BUILD_BUG_ON(sizeof(*mpf) != 16);

	while (length > 0) {
		mpf = (struct intel_mp_floating *)bp;
		if ((*bp == SMP_MAGIC_IDENT) &&
		    (mpf->mpf_length == 1) &&
		    !mpf_checksum((unsigned char *)bp, 16) &&
		    ((mpf->mpf_specification == 1)
		     || (mpf->mpf_specification == 4))) {
#ifdef CONFIG_X86_LOCAL_APIC
			smp_found_config = 1;
#endif
			mpf_found = mpf;

			printk(KERN_INFO "found SMP MP-table at [%p] %08lx\n",
			       mpf, virt_to_phys(mpf));

			if (!reserve)
				return 1;
			reserve_bootmem_generic(virt_to_phys(mpf), PAGE_SIZE,
					BOOTMEM_DEFAULT);
			if (mpf->mpf_physptr) {
				unsigned long size = PAGE_SIZE;
#ifdef CONFIG_X86_32
				/*
				 * We cannot access to MPC table to compute
				 * table size yet, as only few megabytes from
				 * the bottom is mapped now.
				 * PC-9800's MPC table places on the very last
				 * of physical memory; so that simply reserving
				 * PAGE_SIZE from mpg->mpf_physptr yields BUG()
				 * in reserve_bootmem.
				 */
				unsigned long end = max_low_pfn * PAGE_SIZE;
				if (mpf->mpf_physptr + size > end)
					size = end - mpf->mpf_physptr;
#endif
				reserve_bootmem_generic(mpf->mpf_physptr, size,
						BOOTMEM_DEFAULT);
			}

			return 1;
		}
		bp += 4;
		length -= 16;
	}
	return 0;
}

static void __init __find_smp_config(unsigned int reserve)
{
	unsigned int address;

	if (x86_quirks->mach_find_smp_config) {
		if (x86_quirks->mach_find_smp_config(reserve))
			return;
	}
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

#ifdef CONFIG_X86_IO_APIC
static u8 __initdata irq_used[MAX_IRQ_SOURCES];

static int  __init get_MP_intsrc_index(struct mpc_config_intsrc *m)
{
	int i;

	if (m->mpc_irqtype != mp_INT)
		return 0;

	if (m->mpc_irqflag != 0x0f)
		return 0;

	/* not legacy */

	for (i = 0; i < mp_irq_entries; i++) {
		if (mp_irqs[i].mp_irqtype != mp_INT)
			continue;

		if (mp_irqs[i].mp_irqflag != 0x0f)
			continue;

		if (mp_irqs[i].mp_srcbus != m->mpc_srcbus)
			continue;
		if (mp_irqs[i].mp_srcbusirq != m->mpc_srcbusirq)
			continue;
		if (irq_used[i]) {
			/* already claimed */
			return -2;
		}
		irq_used[i] = 1;
		return i;
	}

	/* not found */
	return -1;
}

#define SPARE_SLOT_NUM 20

static struct mpc_config_intsrc __initdata *m_spare[SPARE_SLOT_NUM];
#endif

static int  __init replace_intsrc_all(struct mp_config_table *mpc,
					unsigned long mpc_new_phys,
					unsigned long mpc_new_length)
{
#ifdef CONFIG_X86_IO_APIC
	int i;
	int nr_m_spare = 0;
#endif

	int count = sizeof(*mpc);
	unsigned char *mpt = ((unsigned char *)mpc) + count;

	printk(KERN_INFO "mpc_length %x\n", mpc->mpc_length);
	while (count < mpc->mpc_length) {
		switch (*mpt) {
		case MP_PROCESSOR:
			{
				struct mpc_config_processor *m =
				    (struct mpc_config_processor *)mpt;
				mpt += sizeof(*m);
				count += sizeof(*m);
				break;
			}
		case MP_BUS:
			{
				struct mpc_config_bus *m =
				    (struct mpc_config_bus *)mpt;
				mpt += sizeof(*m);
				count += sizeof(*m);
				break;
			}
		case MP_IOAPIC:
			{
				mpt += sizeof(struct mpc_config_ioapic);
				count += sizeof(struct mpc_config_ioapic);
				break;
			}
		case MP_INTSRC:
			{
#ifdef CONFIG_X86_IO_APIC
				struct mpc_config_intsrc *m =
				    (struct mpc_config_intsrc *)mpt;

				printk(KERN_INFO "OLD ");
				print_MP_intsrc_info(m);
				i = get_MP_intsrc_index(m);
				if (i > 0) {
					assign_to_mpc_intsrc(&mp_irqs[i], m);
					printk(KERN_INFO "NEW ");
					print_mp_irq_info(&mp_irqs[i]);
				} else if (!i) {
					/* legacy, do nothing */
				} else if (nr_m_spare < SPARE_SLOT_NUM) {
					/*
					 * not found (-1), or duplicated (-2)
					 * are invalid entries,
					 * we need to use the slot  later
					 */
					m_spare[nr_m_spare] = m;
					nr_m_spare++;
				}
#endif
				mpt += sizeof(struct mpc_config_intsrc);
				count += sizeof(struct mpc_config_intsrc);
				break;
			}
		case MP_LINTSRC:
			{
				struct mpc_config_lintsrc *m =
				    (struct mpc_config_lintsrc *)mpt;
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
			goto out;
		}
	}

#ifdef CONFIG_X86_IO_APIC
	for (i = 0; i < mp_irq_entries; i++) {
		if (irq_used[i])
			continue;

		if (mp_irqs[i].mp_irqtype != mp_INT)
			continue;

		if (mp_irqs[i].mp_irqflag != 0x0f)
			continue;

		if (nr_m_spare > 0) {
			printk(KERN_INFO "*NEW* found ");
			nr_m_spare--;
			assign_to_mpc_intsrc(&mp_irqs[i], m_spare[nr_m_spare]);
			m_spare[nr_m_spare] = NULL;
		} else {
			struct mpc_config_intsrc *m =
			    (struct mpc_config_intsrc *)mpt;
			count += sizeof(struct mpc_config_intsrc);
			if (!mpc_new_phys) {
				printk(KERN_INFO "No spare slots, try to append...take your risk, new mpc_length %x\n", count);
			} else {
				if (count <= mpc_new_length)
					printk(KERN_INFO "No spare slots, try to append..., new mpc_length %x\n", count);
				else {
					printk(KERN_ERR "mpc_new_length %lx is too small\n", mpc_new_length);
					goto out;
				}
			}
			assign_to_mpc_intsrc(&mp_irqs[i], m);
			mpc->mpc_length = count;
			mpt += sizeof(struct mpc_config_intsrc);
		}
		print_mp_irq_info(&mp_irqs[i]);
	}
#endif
out:
	/* update checksum */
	mpc->mpc_checksum = 0;
	mpc->mpc_checksum -= mpf_checksum((unsigned char *)mpc,
					   mpc->mpc_length);

	return 0;
}

static int __initdata enable_update_mptable;

static int __init update_mptable_setup(char *str)
{
	enable_update_mptable = 1;
	return 0;
}
early_param("update_mptable", update_mptable_setup);

static unsigned long __initdata mpc_new_phys;
static unsigned long mpc_new_length __initdata = 4096;

/* alloc_mptable or alloc_mptable=4k */
static int __initdata alloc_mptable;
static int __init parse_alloc_mptable_opt(char *p)
{
	enable_update_mptable = 1;
	alloc_mptable = 1;
	if (!p)
		return 0;
	mpc_new_length = memparse(p, &p);
	return 0;
}
early_param("alloc_mptable", parse_alloc_mptable_opt);

void __init early_reserve_e820_mpc_new(void)
{
	if (enable_update_mptable && alloc_mptable) {
		u64 startt = 0;
#ifdef CONFIG_X86_TRAMPOLINE
		startt = TRAMPOLINE_BASE;
#endif
		mpc_new_phys = early_reserve_e820(startt, mpc_new_length, 4);
	}
}

static int __init update_mp_table(void)
{
	char str[16];
	char oem[10];
	struct intel_mp_floating *mpf;
	struct mp_config_table *mpc;
	struct mp_config_table *mpc_new;

	if (!enable_update_mptable)
		return 0;

	mpf = mpf_found;
	if (!mpf)
		return 0;

	/*
	 * Now see if we need to go further.
	 */
	if (mpf->mpf_feature1 != 0)
		return 0;

	if (!mpf->mpf_physptr)
		return 0;

	mpc = phys_to_virt(mpf->mpf_physptr);

	if (!smp_check_mpc(mpc, oem, str))
		return 0;

	printk(KERN_INFO "mpf: %lx\n", virt_to_phys(mpf));
	printk(KERN_INFO "mpf_physptr: %x\n", mpf->mpf_physptr);

	if (mpc_new_phys && mpc->mpc_length > mpc_new_length) {
		mpc_new_phys = 0;
		printk(KERN_INFO "mpc_new_length is %ld, please use alloc_mptable=8k\n",
			 mpc_new_length);
	}

	if (!mpc_new_phys) {
		unsigned char old, new;
		/* check if we can change the postion */
		mpc->mpc_checksum = 0;
		old = mpf_checksum((unsigned char *)mpc, mpc->mpc_length);
		mpc->mpc_checksum = 0xff;
		new = mpf_checksum((unsigned char *)mpc, mpc->mpc_length);
		if (old == new) {
			printk(KERN_INFO "mpc is readonly, please try alloc_mptable instead\n");
			return 0;
		}
		printk(KERN_INFO "use in-positon replacing\n");
	} else {
		mpf->mpf_physptr = mpc_new_phys;
		mpc_new = phys_to_virt(mpc_new_phys);
		memcpy(mpc_new, mpc, mpc->mpc_length);
		mpc = mpc_new;
		/* check if we can modify that */
		if (mpc_new_phys - mpf->mpf_physptr) {
			struct intel_mp_floating *mpf_new;
			/* steal 16 bytes from [0, 1k) */
			printk(KERN_INFO "mpf new: %x\n", 0x400 - 16);
			mpf_new = phys_to_virt(0x400 - 16);
			memcpy(mpf_new, mpf, 16);
			mpf = mpf_new;
			mpf->mpf_physptr = mpc_new_phys;
		}
		mpf->mpf_checksum = 0;
		mpf->mpf_checksum -= mpf_checksum((unsigned char *)mpf, 16);
		printk(KERN_INFO "mpf_physptr new: %x\n", mpf->mpf_physptr);
	}

	/*
	 * only replace the one with mp_INT and
	 *	 MP_IRQ_TRIGGER_LEVEL|MP_IRQ_POLARITY_LOW,
	 * already in mp_irqs , stored by ... and mp_config_acpi_gsi,
	 * may need pci=routeirq for all coverage
	 */
	replace_intsrc_all(mpc, mpc_new_phys, mpc_new_length);

	return 0;
}

late_initcall(update_mp_table);
