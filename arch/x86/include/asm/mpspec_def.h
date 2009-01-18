#ifndef _ASM_X86_MPSPEC_DEF_H
#define _ASM_X86_MPSPEC_DEF_H

/*
 * Structure definitions for SMP machines following the
 * Intel Multiprocessing Specification 1.1 and 1.4.
 */

/*
 * This tag identifies where the SMP configuration
 * information is.
 */

#define SMP_MAGIC_IDENT	(('_'<<24) | ('P'<<16) | ('M'<<8) | '_')

#ifdef CONFIG_X86_32
# define MAX_MPC_ENTRY 1024
# define MAX_APICS      256
#else
# if NR_CPUS <= 255
#  define MAX_APICS     255
# else
#  define MAX_APICS   32768
# endif
#endif

/* Intel MP Floating Pointer Structure */
struct mpf_intel {
	char signature[4];		/* "_MP_"			*/
	unsigned int physptr;		/* Configuration table address	*/
	unsigned char length;		/* Our length (paragraphs)	*/
	unsigned char specification;	/* Specification version	*/
	unsigned char checksum;		/* Checksum (makes sum 0)	*/
	unsigned char feature1;		/* Standard or configuration ?	*/
	unsigned char feature2;		/* Bit7 set for IMCR|PIC	*/
	unsigned char feature3;		/* Unused (0)			*/
	unsigned char feature4;		/* Unused (0)			*/
	unsigned char feature5;		/* Unused (0)			*/
};

#define MPC_SIGNATURE "PCMP"

struct mpc_table {
	char signature[4];
	unsigned short length;		/* Size of table */
	char spec;			/* 0x01 */
	char checksum;
	char oem[8];
	char productid[12];
	unsigned int oemptr;		/* 0 if not present */
	unsigned short oemsize;		/* 0 if not present */
	unsigned short oemcount;
	unsigned int lapic;		/* APIC address */
	unsigned int reserved;
};

/* Followed by entries */

#define	MP_PROCESSOR		0
#define	MP_BUS			1
#define	MP_IOAPIC		2
#define	MP_INTSRC		3
#define	MP_LINTSRC		4
/* Used by IBM NUMA-Q to describe node locality */
#define	MP_TRANSLATION		192

#define CPU_ENABLED		1	/* Processor is available */
#define CPU_BOOTPROCESSOR	2	/* Processor is the BP */

#define CPU_STEPPING_MASK	0x000F
#define CPU_MODEL_MASK		0x00F0
#define CPU_FAMILY_MASK		0x0F00

struct mpc_cpu {
	unsigned char type;
	unsigned char apicid;		/* Local APIC number */
	unsigned char apicver;		/* Its versions */
	unsigned char cpuflag;
	unsigned int cpufeature;
	unsigned int featureflag;	/* CPUID feature value */
	unsigned int reserved[2];
};

struct mpc_bus {
	unsigned char type;
	unsigned char busid;
	unsigned char bustype[6];
};

/* List of Bus Type string values, Intel MP Spec. */
#define BUSTYPE_EISA	"EISA"
#define BUSTYPE_ISA	"ISA"
#define BUSTYPE_INTERN	"INTERN"	/* Internal BUS */
#define BUSTYPE_MCA	"MCA"
#define BUSTYPE_VL	"VL"		/* Local bus */
#define BUSTYPE_PCI	"PCI"
#define BUSTYPE_PCMCIA	"PCMCIA"
#define BUSTYPE_CBUS	"CBUS"
#define BUSTYPE_CBUSII	"CBUSII"
#define BUSTYPE_FUTURE	"FUTURE"
#define BUSTYPE_MBI	"MBI"
#define BUSTYPE_MBII	"MBII"
#define BUSTYPE_MPI	"MPI"
#define BUSTYPE_MPSA	"MPSA"
#define BUSTYPE_NUBUS	"NUBUS"
#define BUSTYPE_TC	"TC"
#define BUSTYPE_VME	"VME"
#define BUSTYPE_XPRESS	"XPRESS"

#define MPC_APIC_USABLE		0x01

struct mpc_ioapic {
	unsigned char type;
	unsigned char apicid;
	unsigned char apicver;
	unsigned char flags;
	unsigned int apicaddr;
};

struct mpc_intsrc {
	unsigned char type;
	unsigned char irqtype;
	unsigned short irqflag;
	unsigned char srcbus;
	unsigned char srcbusirq;
	unsigned char dstapic;
	unsigned char dstirq;
};

enum mp_irq_source_types {
	mp_INT = 0,
	mp_NMI = 1,
	mp_SMI = 2,
	mp_ExtINT = 3
};

#define MP_IRQDIR_DEFAULT	0
#define MP_IRQDIR_HIGH		1
#define MP_IRQDIR_LOW		3

#define MP_APIC_ALL	0xFF

struct mpc_lintsrc {
	unsigned char type;
	unsigned char irqtype;
	unsigned short irqflag;
	unsigned char srcbusid;
	unsigned char srcbusirq;
	unsigned char destapic;
	unsigned char destapiclint;
};

#define MPC_OEM_SIGNATURE "_OEM"

struct mpc_oemtable {
	char signature[4];
	unsigned short length;		/* Size of table */
	char  rev;			/* 0x01 */
	char  checksum;
	char  mpc[8];
};

/*
 *	Default configurations
 *
 *	1	2 CPU ISA 82489DX
 *	2	2 CPU EISA 82489DX neither IRQ 0 timer nor IRQ 13 DMA chaining
 *	3	2 CPU EISA 82489DX
 *	4	2 CPU MCA 82489DX
 *	5	2 CPU ISA+PCI
 *	6	2 CPU EISA+PCI
 *	7	2 CPU MCA+PCI
 */

enum mp_bustype {
	MP_BUS_ISA = 1,
	MP_BUS_EISA,
	MP_BUS_PCI,
	MP_BUS_MCA,
};
#endif /* _ASM_X86_MPSPEC_DEF_H */
