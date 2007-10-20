/*
 *	Intel Multiprocessor Specification 1.1 and 1.4
 *	compliant MP-table parsing routines.
 *
 *	(c) 1995 Alan Cox, Building #3 <alan@redhat.com>
 *	(c) 1998, 1999, 2000 Ingo Molnar <mingo@redhat.com>
 *
 *	Fixes
 *		Erich Boleyn	:	MP v1.4 and additional changes.
 *		Alan Cox	:	Added EBDA scanning
 *		Ingo Molnar	:	various cleanups and rewrites
 *		Maciej W. Rozycki:	Bits for default MP configurations
 *		Paul Diefenbaugh:	Added full ACPI support
 */

#include <linux/mm.h>
#include <linux/init.h>
#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/bootmem.h>
#include <linux/kernel_stat.h>
#include <linux/mc146818rtc.h>
#include <linux/bitops.h>

#include <asm/smp.h>
#include <asm/acpi.h>
#include <asm/mtrr.h>
#include <asm/mpspec.h>
#include <asm/io_apic.h>

#include <mach_apic.h>
#include <mach_apicdef.h>
#include <mach_mpparse.h>
#include <bios_ebda.h>

/* Have we found an MP table */
int smp_found_config;
unsigned int __cpuinitdata maxcpus = NR_CPUS;

/*
 * Various Linux-internal data structures created from the
 * MP-table.
 */
int apic_version [MAX_APICS];
int mp_bus_id_to_type [MAX_MP_BUSSES];
int mp_bus_id_to_node [MAX_MP_BUSSES];
int mp_bus_id_to_local [MAX_MP_BUSSES];
int quad_local_to_mp_bus_id [NR_CPUS/4][4];
int mp_bus_id_to_pci_bus [MAX_MP_BUSSES] = { [0 ... MAX_MP_BUSSES-1] = -1 };
static int mp_current_pci_id;

/* I/O APIC entries */
struct mpc_config_ioapic mp_ioapics[MAX_IO_APICS];

/* # of MP IRQ source entries */
struct mpc_config_intsrc mp_irqs[MAX_IRQ_SOURCES];

/* MP IRQ source entries */
int mp_irq_entries;

int nr_ioapics;

int pic_mode;
unsigned long mp_lapic_addr;

unsigned int def_to_bigsmp = 0;

/* Processor that is doing the boot up */
unsigned int boot_cpu_physical_apicid = -1U;
/* Internal processor count */
unsigned int __cpuinitdata num_processors;

/* Bitmask of physically existing CPUs */
physid_mask_t phys_cpu_present_map;

u8 bios_cpu_apicid[NR_CPUS] = { [0 ... NR_CPUS-1] = BAD_APICID };

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

/*
 * Have to match translation table entries to main table entries by counter
 * hence the mpc_record variable .... can't see a less disgusting way of
 * doing this ....
 */

static int mpc_record; 
static struct mpc_config_translation *translation_table[MAX_MPC_ENTRY] __cpuinitdata;

static void __cpuinit MP_processor_info (struct mpc_config_processor *m)
{
 	int ver, apicid;
	physid_mask_t phys_cpu;
 	
	if (!(m->mpc_cpuflag & CPU_ENABLED))
		return;

	apicid = mpc_apic_id(m, translation_table[mpc_record]);

	if (m->mpc_featureflag&(1<<0))
		Dprintk("    Floating point unit present.\n");
	if (m->mpc_featureflag&(1<<7))
		Dprintk("    Machine Exception supported.\n");
	if (m->mpc_featureflag&(1<<8))
		Dprintk("    64 bit compare & exchange supported.\n");
	if (m->mpc_featureflag&(1<<9))
		Dprintk("    Internal APIC present.\n");
	if (m->mpc_featureflag&(1<<11))
		Dprintk("    SEP present.\n");
	if (m->mpc_featureflag&(1<<12))
		Dprintk("    MTRR  present.\n");
	if (m->mpc_featureflag&(1<<13))
		Dprintk("    PGE  present.\n");
	if (m->mpc_featureflag&(1<<14))
		Dprintk("    MCA  present.\n");
	if (m->mpc_featureflag&(1<<15))
		Dprintk("    CMOV  present.\n");
	if (m->mpc_featureflag&(1<<16))
		Dprintk("    PAT  present.\n");
	if (m->mpc_featureflag&(1<<17))
		Dprintk("    PSE  present.\n");
	if (m->mpc_featureflag&(1<<18))
		Dprintk("    PSN  present.\n");
	if (m->mpc_featureflag&(1<<19))
		Dprintk("    Cache Line Flush Instruction present.\n");
	/* 20 Reserved */
	if (m->mpc_featureflag&(1<<21))
		Dprintk("    Debug Trace and EMON Store present.\n");
	if (m->mpc_featureflag&(1<<22))
		Dprintk("    ACPI Thermal Throttle Registers  present.\n");
	if (m->mpc_featureflag&(1<<23))
		Dprintk("    MMX  present.\n");
	if (m->mpc_featureflag&(1<<24))
		Dprintk("    FXSR  present.\n");
	if (m->mpc_featureflag&(1<<25))
		Dprintk("    XMM  present.\n");
	if (m->mpc_featureflag&(1<<26))
		Dprintk("    Willamette New Instructions  present.\n");
	if (m->mpc_featureflag&(1<<27))
		Dprintk("    Self Snoop  present.\n");
	if (m->mpc_featureflag&(1<<28))
		Dprintk("    HT  present.\n");
	if (m->mpc_featureflag&(1<<29))
		Dprintk("    Thermal Monitor present.\n");
	/* 30, 31 Reserved */


	if (m->mpc_cpuflag & CPU_BOOTPROCESSOR) {
		Dprintk("    Bootup CPU\n");
		boot_cpu_physical_apicid = m->mpc_apicid;
	}

	ver = m->mpc_apicver;

	/*
	 * Validate version
	 */
	if (ver == 0x0) {
		printk(KERN_WARNING "BIOS bug, APIC version is 0 for CPU#%d! "
				"fixing up to 0x10. (tell your hw vendor)\n",
				m->mpc_apicid);
		ver = 0x10;
	}
	apic_version[m->mpc_apicid] = ver;

	phys_cpu = apicid_to_cpu_present(apicid);
	physids_or(phys_cpu_present_map, phys_cpu_present_map, phys_cpu);

	if (num_processors >= NR_CPUS) {
		printk(KERN_WARNING "WARNING: NR_CPUS limit of %i reached."
			"  Processor ignored.\n", NR_CPUS);
		return;
	}

	if (num_processors >= maxcpus) {
		printk(KERN_WARNING "WARNING: maxcpus limit of %i reached."
			" Processor ignored.\n", maxcpus);
		return;
	}

	cpu_set(num_processors, cpu_possible_map);
	num_processors++;

	/*
	 * Would be preferable to switch to bigsmp when CONFIG_HOTPLUG_CPU=y
	 * but we need to work other dependencies like SMP_SUSPEND etc
	 * before this can be done without some confusion.
	 * if (CPU_HOTPLUG_ENABLED || num_processors > 8)
	 *       - Ashok Raj <ashok.raj@intel.com>
	 */
	if (num_processors > 8) {
		switch (boot_cpu_data.x86_vendor) {
		case X86_VENDOR_INTEL:
			if (!APIC_XAPIC(ver)) {
				def_to_bigsmp = 0;
				break;
			}
			/* If P4 and above fall through */
		case X86_VENDOR_AMD:
			def_to_bigsmp = 1;
		}
	}
	bios_cpu_apicid[num_processors - 1] = m->mpc_apicid;
}

static void __init MP_bus_info (struct mpc_config_bus *m)
{
	char str[7];

	memcpy(str, m->mpc_bustype, 6);
	str[6] = 0;

	mpc_oem_bus_info(m, str, translation_table[mpc_record]);

#if MAX_MP_BUSSES < 256
	if (m->mpc_busid >= MAX_MP_BUSSES) {
		printk(KERN_WARNING "MP table busid value (%d) for bustype %s "
			" is too large, max. supported is %d\n",
			m->mpc_busid, str, MAX_MP_BUSSES - 1);
		return;
	}
#endif

	if (strncmp(str, BUSTYPE_ISA, sizeof(BUSTYPE_ISA)-1) == 0) {
		mp_bus_id_to_type[m->mpc_busid] = MP_BUS_ISA;
	} else if (strncmp(str, BUSTYPE_EISA, sizeof(BUSTYPE_EISA)-1) == 0) {
		mp_bus_id_to_type[m->mpc_busid] = MP_BUS_EISA;
	} else if (strncmp(str, BUSTYPE_PCI, sizeof(BUSTYPE_PCI)-1) == 0) {
		mpc_oem_pci_bus(m, translation_table[mpc_record]);
		mp_bus_id_to_type[m->mpc_busid] = MP_BUS_PCI;
		mp_bus_id_to_pci_bus[m->mpc_busid] = mp_current_pci_id;
		mp_current_pci_id++;
	} else if (strncmp(str, BUSTYPE_MCA, sizeof(BUSTYPE_MCA)-1) == 0) {
		mp_bus_id_to_type[m->mpc_busid] = MP_BUS_MCA;
	} else {
		printk(KERN_WARNING "Unknown bustype %s - ignoring\n", str);
	}
}

static void __init MP_ioapic_info (struct mpc_config_ioapic *m)
{
	if (!(m->mpc_flags & MPC_APIC_USABLE))
		return;

	printk(KERN_INFO "I/O APIC #%d Version %d at 0x%lX.\n",
		m->mpc_apicid, m->mpc_apicver, m->mpc_apicaddr);
	if (nr_ioapics >= MAX_IO_APICS) {
		printk(KERN_CRIT "Max # of I/O APICs (%d) exceeded (found %d).\n",
			MAX_IO_APICS, nr_ioapics);
		panic("Recompile kernel with bigger MAX_IO_APICS!.\n");
	}
	if (!m->mpc_apicaddr) {
		printk(KERN_ERR "WARNING: bogus zero I/O APIC address"
			" found in MP table, skipping!\n");
		return;
	}
	mp_ioapics[nr_ioapics] = *m;
	nr_ioapics++;
}

static void __init MP_intsrc_info (struct mpc_config_intsrc *m)
{
	mp_irqs [mp_irq_entries] = *m;
	Dprintk("Int: type %d, pol %d, trig %d, bus %d,"
		" IRQ %02x, APIC ID %x, APIC INT %02x\n",
			m->mpc_irqtype, m->mpc_irqflag & 3,
			(m->mpc_irqflag >> 2) & 3, m->mpc_srcbus,
			m->mpc_srcbusirq, m->mpc_dstapic, m->mpc_dstirq);
	if (++mp_irq_entries == MAX_IRQ_SOURCES)
		panic("Max # of irq sources exceeded!!\n");
}

static void __init MP_lintsrc_info (struct mpc_config_lintsrc *m)
{
	Dprintk("Lint: type %d, pol %d, trig %d, bus %d,"
		" IRQ %02x, APIC ID %x, APIC LINT %02x\n",
			m->mpc_irqtype, m->mpc_irqflag & 3,
			(m->mpc_irqflag >> 2) &3, m->mpc_srcbusid,
			m->mpc_srcbusirq, m->mpc_destapic, m->mpc_destapiclint);
}

#ifdef CONFIG_X86_NUMAQ
static void __init MP_translation_info (struct mpc_config_translation *m)
{
	printk(KERN_INFO "Translation: record %d, type %d, quad %d, global %d, local %d\n", mpc_record, m->trans_type, m->trans_quad, m->trans_global, m->trans_local);

	if (mpc_record >= MAX_MPC_ENTRY) 
		printk(KERN_ERR "MAX_MPC_ENTRY exceeded!\n");
	else
		translation_table[mpc_record] = m; /* stash this for later */
	if (m->trans_quad < MAX_NUMNODES && !node_online(m->trans_quad))
		node_set_online(m->trans_quad);
}

/*
 * Read/parse the MPC oem tables
 */

static void __init smp_read_mpc_oem(struct mp_config_oemtable *oemtable, \
	unsigned short oemsize)
{
	int count = sizeof (*oemtable); /* the header size */
	unsigned char *oemptr = ((unsigned char *)oemtable)+count;
	
	mpc_record = 0;
	printk(KERN_INFO "Found an OEM MPC table at %8p - parsing it ... \n", oemtable);
	if (memcmp(oemtable->oem_signature,MPC_OEM_SIGNATURE,4))
	{
		printk(KERN_WARNING "SMP mpc oemtable: bad signature [%c%c%c%c]!\n",
			oemtable->oem_signature[0],
			oemtable->oem_signature[1],
			oemtable->oem_signature[2],
			oemtable->oem_signature[3]);
		return;
	}
	if (mpf_checksum((unsigned char *)oemtable,oemtable->oem_length))
	{
		printk(KERN_WARNING "SMP oem mptable: checksum error!\n");
		return;
	}
	while (count < oemtable->oem_length) {
		switch (*oemptr) {
			case MP_TRANSLATION:
			{
				struct mpc_config_translation *m=
					(struct mpc_config_translation *)oemptr;
				MP_translation_info(m);
				oemptr += sizeof(*m);
				count += sizeof(*m);
				++mpc_record;
				break;
			}
			default:
			{
				printk(KERN_WARNING "Unrecognised OEM table entry type! - %d\n", (int) *oemptr);
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
		smp_read_mpc_oem((struct mp_config_oemtable *) mpc->mpc_oemptr,
				mpc->mpc_oemsize);
}
#endif	/* CONFIG_X86_NUMAQ */

/*
 * Read/parse the MPC
 */

static int __init smp_read_mpc(struct mp_config_table *mpc)
{
	char str[16];
	char oem[10];
	int count=sizeof(*mpc);
	unsigned char *mpt=((unsigned char *)mpc)+count;

	if (memcmp(mpc->mpc_signature,MPC_SIGNATURE,4)) {
		printk(KERN_ERR "SMP mptable: bad signature [0x%x]!\n",
			*(u32 *)mpc->mpc_signature);
		return 0;
	}
	if (mpf_checksum((unsigned char *)mpc,mpc->mpc_length)) {
		printk(KERN_ERR "SMP mptable: checksum error!\n");
		return 0;
	}
	if (mpc->mpc_spec!=0x01 && mpc->mpc_spec!=0x04) {
		printk(KERN_ERR "SMP mptable: bad table version (%d)!!\n",
			mpc->mpc_spec);
		return 0;
	}
	if (!mpc->mpc_lapic) {
		printk(KERN_ERR "SMP mptable: null local APIC address!\n");
		return 0;
	}
	memcpy(oem,mpc->mpc_oem,8);
	oem[8]=0;
	printk(KERN_INFO "OEM ID: %s ",oem);

	memcpy(str,mpc->mpc_productid,12);
	str[12]=0;
	printk("Product ID: %s ",str);

	mps_oem_check(mpc, oem, str);

	printk("APIC at: 0x%lX\n",mpc->mpc_lapic);

	/* 
	 * Save the local APIC address (it might be non-default) -- but only
	 * if we're not using ACPI.
	 */
	if (!acpi_lapic)
		mp_lapic_addr = mpc->mpc_lapic;

	/*
	 *	Now process the configuration blocks.
	 */
	mpc_record = 0;
	while (count < mpc->mpc_length) {
		switch(*mpt) {
			case MP_PROCESSOR:
			{
				struct mpc_config_processor *m=
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
				struct mpc_config_bus *m=
					(struct mpc_config_bus *)mpt;
				MP_bus_info(m);
				mpt += sizeof(*m);
				count += sizeof(*m);
				break;
			}
			case MP_IOAPIC:
			{
				struct mpc_config_ioapic *m=
					(struct mpc_config_ioapic *)mpt;
				MP_ioapic_info(m);
				mpt+=sizeof(*m);
				count+=sizeof(*m);
				break;
			}
			case MP_INTSRC:
			{
				struct mpc_config_intsrc *m=
					(struct mpc_config_intsrc *)mpt;

				MP_intsrc_info(m);
				mpt+=sizeof(*m);
				count+=sizeof(*m);
				break;
			}
			case MP_LINTSRC:
			{
				struct mpc_config_lintsrc *m=
					(struct mpc_config_lintsrc *)mpt;
				MP_lintsrc_info(m);
				mpt+=sizeof(*m);
				count+=sizeof(*m);
				break;
			}
			default:
			{
				count = mpc->mpc_length;
				break;
			}
		}
		++mpc_record;
	}
	setup_apic_routing();
	if (!num_processors)
		printk(KERN_ERR "SMP mptable: no processors registered!\n");
	return num_processors;
}

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
	intsrc.mpc_irqflag = 0;			/* conforming */
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
		printk(KERN_INFO "ISA/PCI bus type with no IRQ information... falling back to ELCR\n");

		if (ELCR_trigger(0) || ELCR_trigger(1) || ELCR_trigger(2) || ELCR_trigger(13))
			printk(KERN_WARNING "ELCR contains invalid data... not using ELCR\n");
		else {
			printk(KERN_INFO "Using ELCR to identify PCI interrupts\n");
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
		intsrc.mpc_dstirq = i ? i : 2;		/* IRQ0 to INTIN2 */
		MP_intsrc_info(&intsrc);
	}

	intsrc.mpc_irqtype = mp_ExtINT;
	intsrc.mpc_srcbusirq = 0;
	intsrc.mpc_dstirq = 0;				/* 8259A to INTIN0 */
	MP_intsrc_info(&intsrc);
}

static inline void __init construct_default_ISA_mptable(int mpc_default_type)
{
	struct mpc_config_processor processor;
	struct mpc_config_bus bus;
	struct mpc_config_ioapic ioapic;
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
				   (boot_cpu_data.x86_model << 4) |
				   boot_cpu_data.x86_mask;
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
			printk("???\n");
			printk(KERN_ERR "Unknown standard configuration %d\n",
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

	lintsrc.mpc_type = MP_LINTSRC;
	lintsrc.mpc_irqflag = 0;		/* conforming */
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
void __init get_smp_config (void)
{
	struct intel_mp_floating *mpf = mpf_found;

	/*
	 * ACPI supports both logical (e.g. Hyper-Threading) and physical 
	 * processors, where MPS only supports physical.
	 */
	if (acpi_lapic && acpi_ioapic) {
		printk(KERN_INFO "Using ACPI (MADT) for SMP configuration information\n");
		return;
	}
	else if (acpi_lapic)
		printk(KERN_INFO "Using ACPI for processor (LAPIC) configuration information\n");

	printk(KERN_INFO "Intel MultiProcessor Specification v1.%d\n", mpf->mpf_specification);
	if (mpf->mpf_feature2 & (1<<7)) {
		printk(KERN_INFO "    IMCR and PIC compatibility mode.\n");
		pic_mode = 1;
	} else {
		printk(KERN_INFO "    Virtual Wire compatibility mode.\n");
		pic_mode = 0;
	}

	/*
	 * Now see if we need to read further.
	 */
	if (mpf->mpf_feature1 != 0) {

		printk(KERN_INFO "Default MP configuration #%d\n", mpf->mpf_feature1);
		construct_default_ISA_mptable(mpf->mpf_feature1);

	} else if (mpf->mpf_physptr) {

		/*
		 * Read the physical hardware table.  Anything here will
		 * override the defaults.
		 */
		if (!smp_read_mpc(phys_to_virt(mpf->mpf_physptr))) {
			smp_found_config = 0;
			printk(KERN_ERR "BIOS bug, MP table errors detected!...\n");
			printk(KERN_ERR "... disabling SMP support. (tell your hw vendor)\n");
			return;
		}
		/*
		 * If there are no explicit MP IRQ entries, then we are
		 * broken.  We set up most of the low 16 IO-APIC pins to
		 * ISA defaults and hope it will work.
		 */
		if (!mp_irq_entries) {
			struct mpc_config_bus bus;

			printk(KERN_ERR "BIOS bug, no explicit IRQ entries, using default mptable. (tell your hw vendor)\n");

			bus.mpc_type = MP_BUS;
			bus.mpc_busid = 0;
			memcpy(bus.mpc_bustype, "ISA   ", 6);
			MP_bus_info(&bus);

			construct_default_ioirq_mptable(0);
		}

	} else
		BUG();

	printk(KERN_INFO "Processors: %d\n", num_processors);
	/*
	 * Only use the first configuration found.
	 */
}

static int __init smp_scan_config (unsigned long base, unsigned long length)
{
	unsigned long *bp = phys_to_virt(base);
	struct intel_mp_floating *mpf;

	Dprintk("Scan SMP from %p for %ld bytes.\n", bp,length);
	if (sizeof(*mpf) != 16)
		printk("Error: MPF size\n");

	while (length > 0) {
		mpf = (struct intel_mp_floating *)bp;
		if ((*bp == SMP_MAGIC_IDENT) &&
			(mpf->mpf_length == 1) &&
			!mpf_checksum((unsigned char *)bp, 16) &&
			((mpf->mpf_specification == 1)
				|| (mpf->mpf_specification == 4)) ) {

			smp_found_config = 1;
			printk(KERN_INFO "found SMP MP-table at %08lx\n",
						virt_to_phys(mpf));
			reserve_bootmem(virt_to_phys(mpf), PAGE_SIZE);
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
				reserve_bootmem(mpf->mpf_physptr, size);
			}

			mpf_found = mpf;
			return 1;
		}
		bp += 4;
		length -= 16;
	}
	return 0;
}

void __init find_smp_config (void)
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
	if (smp_scan_config(0x0,0x400) ||
		smp_scan_config(639*0x400,0x400) ||
			smp_scan_config(0xF0000,0x10000))
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
		smp_scan_config(address, 0x400);
}

int es7000_plat;

/* --------------------------------------------------------------------------
                            ACPI-based MP Configuration
   -------------------------------------------------------------------------- */

#ifdef CONFIG_ACPI

void __init mp_register_lapic_address(u64 address)
{
	mp_lapic_addr = (unsigned long) address;

	set_fixmap_nocache(FIX_APIC_BASE, mp_lapic_addr);

	if (boot_cpu_physical_apicid == -1U)
		boot_cpu_physical_apicid = GET_APIC_ID(apic_read(APIC_ID));

	Dprintk("Boot CPU = %d\n", boot_cpu_physical_apicid);
}

void __cpuinit mp_register_lapic (u8 id, u8 enabled)
{
	struct mpc_config_processor processor;
	int boot_cpu = 0;
	
	if (MAX_APICS - id <= 0) {
		printk(KERN_WARNING "Processor #%d invalid (max %d)\n",
			id, MAX_APICS);
		return;
	}

	if (id == boot_cpu_physical_apicid)
		boot_cpu = 1;

	processor.mpc_type = MP_PROCESSOR;
	processor.mpc_apicid = id;
	processor.mpc_apicver = GET_APIC_VERSION(apic_read(APIC_LVR));
	processor.mpc_cpuflag = (enabled ? CPU_ENABLED : 0);
	processor.mpc_cpuflag |= (boot_cpu ? CPU_BOOTPROCESSOR : 0);
	processor.mpc_cpufeature = (boot_cpu_data.x86 << 8) | 
		(boot_cpu_data.x86_model << 4) | boot_cpu_data.x86_mask;
	processor.mpc_featureflag = boot_cpu_data.x86_capability[0];
	processor.mpc_reserved[0] = 0;
	processor.mpc_reserved[1] = 0;

	MP_processor_info(&processor);
}

#ifdef	CONFIG_X86_IO_APIC

#define MP_ISA_BUS		0
#define MP_MAX_IOAPIC_PIN	127

static struct mp_ioapic_routing {
	int			apic_id;
	int			gsi_base;
	int			gsi_end;
	u32			pin_programmed[4];
} mp_ioapic_routing[MAX_IO_APICS];

static int mp_find_ioapic (int gsi)
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

void __init mp_register_ioapic(u8 id, u32 address, u32 gsi_base)
{
	int idx = 0;
	int tmpid;

	if (nr_ioapics >= MAX_IO_APICS) {
		printk(KERN_ERR "ERROR: Max # of I/O APICs (%d) exceeded "
			"(found %d)\n", MAX_IO_APICS, nr_ioapics);
		panic("Recompile kernel with bigger MAX_IO_APICS!\n");
	}
	if (!address) {
		printk(KERN_ERR "WARNING: Bogus (zero) I/O APIC address"
			" found in MADT table, skipping!\n");
		return;
	}

	idx = nr_ioapics++;

	mp_ioapics[idx].mpc_type = MP_IOAPIC;
	mp_ioapics[idx].mpc_flags = MPC_APIC_USABLE;
	mp_ioapics[idx].mpc_apicaddr = address;

	set_fixmap_nocache(FIX_IO_APIC_BASE_0 + idx, address);
	if ((boot_cpu_data.x86_vendor == X86_VENDOR_INTEL)
		&& !APIC_XAPIC(apic_version[boot_cpu_physical_apicid]))
		tmpid = io_apic_get_unique_id(idx, id);
	else
		tmpid = id;
	if (tmpid == -1) {
		nr_ioapics--;
		return;
	}
	mp_ioapics[idx].mpc_apicid = tmpid;
	mp_ioapics[idx].mpc_apicver = io_apic_get_version(idx);
	
	/* 
	 * Build basic GSI lookup table to facilitate gsi->io_apic lookups
	 * and to prevent reprogramming of IOAPIC pins (PCI GSIs).
	 */
	mp_ioapic_routing[idx].apic_id = mp_ioapics[idx].mpc_apicid;
	mp_ioapic_routing[idx].gsi_base = gsi_base;
	mp_ioapic_routing[idx].gsi_end = gsi_base + 
		io_apic_get_redir_entries(idx);

	printk("IOAPIC[%d]: apic_id %d, version %d, address 0x%lx, "
		"GSI %d-%d\n", idx, mp_ioapics[idx].mpc_apicid, 
		mp_ioapics[idx].mpc_apicver, mp_ioapics[idx].mpc_apicaddr,
		mp_ioapic_routing[idx].gsi_base,
		mp_ioapic_routing[idx].gsi_end);
}

void __init
mp_override_legacy_irq(u8 bus_irq, u8 polarity, u8 trigger, u32 gsi)
{
	struct mpc_config_intsrc intsrc;
	int			ioapic = -1;
	int			pin = -1;

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
	intsrc.mpc_srcbusirq = bus_irq;				       /* IRQ */
	intsrc.mpc_dstapic = mp_ioapics[ioapic].mpc_apicid;	   /* APIC ID */
	intsrc.mpc_dstirq = pin;				    /* INTIN# */

	Dprintk("Int: type %d, pol %d, trig %d, bus %d, irq %d, %d-%d\n",
		intsrc.mpc_irqtype, intsrc.mpc_irqflag & 3, 
		(intsrc.mpc_irqflag >> 2) & 3, intsrc.mpc_srcbus, 
		intsrc.mpc_srcbusirq, intsrc.mpc_dstapic, intsrc.mpc_dstirq);

	mp_irqs[mp_irq_entries] = intsrc;
	if (++mp_irq_entries == MAX_IRQ_SOURCES)
		panic("Max # of irq sources exceeded!\n");
}

void __init mp_config_acpi_legacy_irqs (void)
{
	struct mpc_config_intsrc intsrc;
	int i = 0;
	int ioapic = -1;

	/* 
	 * Fabricate the legacy ISA bus (bus #31).
	 */
	mp_bus_id_to_type[MP_ISA_BUS] = MP_BUS_ISA;
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
	intsrc.mpc_irqflag = 0;					/* Conforming */
	intsrc.mpc_srcbus = MP_ISA_BUS;
	intsrc.mpc_dstapic = mp_ioapics[ioapic].mpc_apicid;

	/* 
	 * Use the default configuration for the IRQs 0-15.  Unless
	 * overridden by (MADT) interrupt source override entries.
	 */
	for (i = 0; i < 16; i++) {
		int idx;

		for (idx = 0; idx < mp_irq_entries; idx++) {
			struct mpc_config_intsrc *irq = mp_irqs + idx;

			/* Do we already have a mapping for this ISA IRQ? */
			if (irq->mpc_srcbus == MP_ISA_BUS && irq->mpc_srcbusirq == i)
				break;

			/* Do we already have a mapping for this IOAPIC pin */
			if ((irq->mpc_dstapic == intsrc.mpc_dstapic) &&
				(irq->mpc_dstirq == i))
				break;
		}

		if (idx != mp_irq_entries) {
			printk(KERN_DEBUG "ACPI: IRQ%d used by override.\n", i);
			continue;			/* IRQ already used */
		}

		intsrc.mpc_irqtype = mp_INT;
		intsrc.mpc_srcbusirq = i;		   /* Identity mapped */
		intsrc.mpc_dstirq = i;

		Dprintk("Int: type %d, pol %d, trig %d, bus %d, irq %d, "
			"%d-%d\n", intsrc.mpc_irqtype, intsrc.mpc_irqflag & 3, 
			(intsrc.mpc_irqflag >> 2) & 3, intsrc.mpc_srcbus, 
			intsrc.mpc_srcbusirq, intsrc.mpc_dstapic, 
			intsrc.mpc_dstirq);

		mp_irqs[mp_irq_entries] = intsrc;
		if (++mp_irq_entries == MAX_IRQ_SOURCES)
			panic("Max # of irq sources exceeded!\n");
	}
}

#define MAX_GSI_NUM	4096

int mp_register_gsi(u32 gsi, int triggering, int polarity)
{
	int ioapic = -1;
	int ioapic_pin = 0;
	int idx, bit = 0;
	static int pci_irq = 16;
	/*
	 * Mapping between Global System Interrups, which
	 * represent all possible interrupts, and IRQs
	 * assigned to actual devices.
	 */
	static int		gsi_to_irq[MAX_GSI_NUM];

	/* Don't set up the ACPI SCI because it's already set up */
	if (acpi_gbl_FADT.sci_interrupt == gsi)
		return gsi;

	ioapic = mp_find_ioapic(gsi);
	if (ioapic < 0) {
		printk(KERN_WARNING "No IOAPIC for GSI %u\n", gsi);
		return gsi;
	}

	ioapic_pin = gsi - mp_ioapic_routing[ioapic].gsi_base;

	if (ioapic_renumber_irq)
		gsi = ioapic_renumber_irq(ioapic, gsi);

	/* 
	 * Avoid pin reprogramming.  PRTs typically include entries  
	 * with redundant pin->gsi mappings (but unique PCI devices);
	 * we only program the IOAPIC on the first.
	 */
	bit = ioapic_pin % 32;
	idx = (ioapic_pin < 32) ? 0 : (ioapic_pin / 32);
	if (idx > 3) {
		printk(KERN_ERR "Invalid reference to IOAPIC pin "
			"%d-%d\n", mp_ioapic_routing[ioapic].apic_id, 
			ioapic_pin);
		return gsi;
	}
	if ((1<<bit) & mp_ioapic_routing[ioapic].pin_programmed[idx]) {
		Dprintk(KERN_DEBUG "Pin %d-%d already programmed\n",
			mp_ioapic_routing[ioapic].apic_id, ioapic_pin);
		return gsi_to_irq[gsi];
	}

	mp_ioapic_routing[ioapic].pin_programmed[idx] |= (1<<bit);

	if (triggering == ACPI_LEVEL_SENSITIVE) {
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
			if (gsi > 15 || (gsi == 0 && !timer_uses_ioapic_pin_0))
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

	io_apic_set_pci_routing(ioapic, ioapic_pin, gsi,
		    triggering == ACPI_EDGE_SENSITIVE ? 0 : 1,
		    polarity == ACPI_ACTIVE_HIGH ? 0 : 1);
	return gsi;
}

#endif /* CONFIG_X86_IO_APIC */
#endif /* CONFIG_ACPI */
