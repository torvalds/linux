/*
 * PReP pci functions.
 * Originally by Gary Thomas
 * rewritten and updated by Cort Dougan (cort@cs.nmt.edu)
 *
 * The motherboard routes/maps will disappear shortly. -- Cort
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/sections.h>
#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/ptrace.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include <asm/residual.h>
#include <asm/irq.h>
#include <asm/machdep.h>
#include <asm/open_pic.h>

extern void (*setup_ibm_pci)(char *irq_lo, char *irq_hi);

/* Which PCI interrupt line does a given device [slot] use? */
/* Note: This really should be two dimensional based in slot/pin used */
static unsigned char *Motherboard_map;
unsigned char *Motherboard_map_name;

/* How is the 82378 PIRQ mapping setup? */
static unsigned char *Motherboard_routes;

static void (*Motherboard_non0)(struct pci_dev *);

static void Powerplus_Map_Non0(struct pci_dev *);

/* Used for Motorola to store system config register */
static unsigned long	*ProcInfo;

/* Tables for known hardware */

/* Motorola PowerStackII - Utah */
static char Utah_pci_IRQ_map[23] __prepdata =
{
        0,   /* Slot 0  - unused */
        0,   /* Slot 1  - unused */
        5,   /* Slot 2  - SCSI - NCR825A  */
        0,   /* Slot 3  - unused */
        3,   /* Slot 4  - Ethernet - DEC2114x */
        0,   /* Slot 5  - unused */
        2,   /* Slot 6  - PCI Card slot #1 */
        3,   /* Slot 7  - PCI Card slot #2 */
        5,   /* Slot 8  - PCI Card slot #3 */
        5,   /* Slot 9  - PCI Bridge */
             /* added here in case we ever support PCI bridges */
             /* Secondary PCI bus cards are at slot-9,6 & slot-9,7 */
        0,   /* Slot 10 - unused */
        0,   /* Slot 11 - unused */
        5,   /* Slot 12 - SCSI - NCR825A */
        0,   /* Slot 13 - unused */
        3,   /* Slot 14 - enet */
        0,   /* Slot 15 - unused */
        2,   /* Slot 16 - unused */
        3,   /* Slot 17 - unused */
        5,   /* Slot 18 - unused */
        0,   /* Slot 19 - unused */
        0,   /* Slot 20 - unused */
        0,   /* Slot 21 - unused */
        0,   /* Slot 22 - unused */
};

static char Utah_pci_IRQ_routes[] __prepdata =
{
        0,   /* Line 0 - Unused */
        9,   /* Line 1 */
	10,  /* Line 2 */
        11,  /* Line 3 */
        14,  /* Line 4 */
        15,  /* Line 5 */
};

/* Motorola PowerStackII - Omaha */
/* no integrated SCSI or ethernet */
static char Omaha_pci_IRQ_map[23] __prepdata =
{
        0,   /* Slot 0  - unused */
        0,   /* Slot 1  - unused */
        3,   /* Slot 2  - Winbond EIDE */
        0,   /* Slot 3  - unused */
        0,   /* Slot 4  - unused */
        0,   /* Slot 5  - unused */
        1,   /* Slot 6  - PCI slot 1 */
        2,   /* Slot 7  - PCI slot 2  */
        3,   /* Slot 8  - PCI slot 3 */
        4,   /* Slot 9  - PCI slot 4 */ /* needs indirect access */
        0,   /* Slot 10 - unused */
        0,   /* Slot 11 - unused */
        0,   /* Slot 12 - unused */
        0,   /* Slot 13 - unused */
        0,   /* Slot 14 - unused */
        0,   /* Slot 15 - unused */
        1,   /* Slot 16  - PCI slot 1 */
        2,   /* Slot 17  - PCI slot 2  */
        3,   /* Slot 18  - PCI slot 3 */
        4,   /* Slot 19  - PCI slot 4 */ /* needs indirect access */
        0,
        0,
        0,
};

static char Omaha_pci_IRQ_routes[] __prepdata =
{
        0,   /* Line 0 - Unused */
        9,   /* Line 1 */
        11,  /* Line 2 */
        14,  /* Line 3 */
        15   /* Line 4 */
};

/* Motorola PowerStack */
static char Blackhawk_pci_IRQ_map[19] __prepdata =
{
  	0,	/* Slot 0  - unused */
  	0,	/* Slot 1  - unused */
  	0,	/* Slot 2  - unused */
  	0,	/* Slot 3  - unused */
  	0,	/* Slot 4  - unused */
  	0,	/* Slot 5  - unused */
  	0,	/* Slot 6  - unused */
  	0,	/* Slot 7  - unused */
  	0,	/* Slot 8  - unused */
  	0,	/* Slot 9  - unused */
  	0,	/* Slot 10 - unused */
  	0,	/* Slot 11 - unused */
  	3,	/* Slot 12 - SCSI */
  	0,	/* Slot 13 - unused */
  	1,	/* Slot 14 - Ethernet */
  	0,	/* Slot 15 - unused */
 	1,	/* Slot P7 */
 	2,	/* Slot P6 */
 	3,	/* Slot P5 */
};

static char Blackhawk_pci_IRQ_routes[] __prepdata =
{
   	0,	/* Line 0 - Unused */
   	9,	/* Line 1 */
   	11,	/* Line 2 */
   	15,	/* Line 3 */
   	15	/* Line 4 */
};

/* Motorola Mesquite */
static char Mesquite_pci_IRQ_map[23] __prepdata =
{
	0,	/* Slot 0  - unused */
	0,	/* Slot 1  - unused */
	0,	/* Slot 2  - unused */
	0,	/* Slot 3  - unused */
	0,	/* Slot 4  - unused */
	0,	/* Slot 5  - unused */
	0,	/* Slot 6  - unused */
	0,	/* Slot 7  - unused */
	0,	/* Slot 8  - unused */
	0,	/* Slot 9  - unused */
	0,	/* Slot 10 - unused */
	0,	/* Slot 11 - unused */
	0,	/* Slot 12 - unused */
	0,	/* Slot 13 - unused */
	2,	/* Slot 14 - Ethernet */
	0,	/* Slot 15 - unused */
	3,	/* Slot 16 - PMC */
	0,	/* Slot 17 - unused */
	0,	/* Slot 18 - unused */
	0,	/* Slot 19 - unused */
	0,	/* Slot 20 - unused */
	0,	/* Slot 21 - unused */
	0,	/* Slot 22 - unused */
};

/* Motorola Sitka */
static char Sitka_pci_IRQ_map[21] __prepdata =
{
	0,      /* Slot 0  - unused */
	0,      /* Slot 1  - unused */
	0,      /* Slot 2  - unused */
	0,      /* Slot 3  - unused */
	0,      /* Slot 4  - unused */
	0,      /* Slot 5  - unused */
	0,      /* Slot 6  - unused */
	0,      /* Slot 7  - unused */
	0,      /* Slot 8  - unused */
	0,      /* Slot 9  - unused */
	0,      /* Slot 10 - unused */
	0,      /* Slot 11 - unused */
	0,      /* Slot 12 - unused */
	0,      /* Slot 13 - unused */
	2,      /* Slot 14 - Ethernet */
	0,      /* Slot 15 - unused */
	9,      /* Slot 16 - PMC 1  */
	12,     /* Slot 17 - PMC 2  */
	0,      /* Slot 18 - unused */
	0,      /* Slot 19 - unused */
	4,      /* Slot 20 - NT P2P bridge */
};

/* Motorola MTX */
static char MTX_pci_IRQ_map[23] __prepdata =
{
	0,	/* Slot 0  - unused */
	0,	/* Slot 1  - unused */
	0,	/* Slot 2  - unused */
	0,	/* Slot 3  - unused */
	0,	/* Slot 4  - unused */
	0,	/* Slot 5  - unused */
	0,	/* Slot 6  - unused */
	0,	/* Slot 7  - unused */
	0,	/* Slot 8  - unused */
	0,	/* Slot 9  - unused */
	0,	/* Slot 10 - unused */
	0,	/* Slot 11 - unused */
	3,	/* Slot 12 - SCSI */
	0,	/* Slot 13 - unused */
	2,	/* Slot 14 - Ethernet */
	0,	/* Slot 15 - unused */
	9,      /* Slot 16 - PCI/PMC slot 1 */
	10,     /* Slot 17 - PCI/PMC slot 2 */
	11,     /* Slot 18 - PCI slot 3 */
	0,	/* Slot 19 - unused */
	0,	/* Slot 20 - unused */
	0,	/* Slot 21 - unused */
	0,	/* Slot 22 - unused */
};

/* Motorola MTX Plus */
/* Secondary bus interrupt routing is not supported yet */
static char MTXplus_pci_IRQ_map[23] __prepdata =
{
        0,      /* Slot 0  - unused */
        0,      /* Slot 1  - unused */
        0,      /* Slot 2  - unused */
        0,      /* Slot 3  - unused */
        0,      /* Slot 4  - unused */
        0,      /* Slot 5  - unused */
        0,      /* Slot 6  - unused */
        0,      /* Slot 7  - unused */
        0,      /* Slot 8  - unused */
        0,      /* Slot 9  - unused */
        0,      /* Slot 10 - unused */
        0,      /* Slot 11 - unused */
        3,      /* Slot 12 - SCSI */
        0,      /* Slot 13 - unused */
        2,      /* Slot 14 - Ethernet 1 */
        0,      /* Slot 15 - unused */
        9,      /* Slot 16 - PCI slot 1P */
        10,     /* Slot 17 - PCI slot 2P */
        11,     /* Slot 18 - PCI slot 3P */
        10,     /* Slot 19 - Ethernet 2 */
        0,      /* Slot 20 - P2P Bridge */
        0,      /* Slot 21 - unused */
        0,      /* Slot 22 - unused */
};

static char Raven_pci_IRQ_routes[] __prepdata =
{
   	0,	/* This is a dummy structure */
};

/* Motorola MVME16xx */
static char Genesis_pci_IRQ_map[16] __prepdata =
{
  	0,	/* Slot 0  - unused */
  	0,	/* Slot 1  - unused */
  	0,	/* Slot 2  - unused */
  	0,	/* Slot 3  - unused */
  	0,	/* Slot 4  - unused */
  	0,	/* Slot 5  - unused */
  	0,	/* Slot 6  - unused */
  	0,	/* Slot 7  - unused */
  	0,	/* Slot 8  - unused */
  	0,	/* Slot 9  - unused */
  	0,	/* Slot 10 - unused */
  	0,	/* Slot 11 - unused */
  	3,	/* Slot 12 - SCSI */
  	0,	/* Slot 13 - unused */
  	1,	/* Slot 14 - Ethernet */
  	0,	/* Slot 15 - unused */
};

static char Genesis_pci_IRQ_routes[] __prepdata =
{
   	0,	/* Line 0 - Unused */
   	10,	/* Line 1 */
   	11,	/* Line 2 */
   	14,	/* Line 3 */
   	15	/* Line 4 */
};

static char Genesis2_pci_IRQ_map[23] __prepdata =
{
	0,	/* Slot 0  - unused */
	0,	/* Slot 1  - unused */
	0,	/* Slot 2  - unused */
	0,	/* Slot 3  - unused */
	0,	/* Slot 4  - unused */
	0,	/* Slot 5  - unused */
	0,	/* Slot 6  - unused */
	0,	/* Slot 7  - unused */
	0,	/* Slot 8  - unused */
	0,	/* Slot 9  - unused */
	0,	/* Slot 10 - unused */
	0,	/* Slot 11 - IDE */
	3,	/* Slot 12 - SCSI */
	5,	/* Slot 13 - Universe PCI - VME Bridge */
	2,	/* Slot 14 - Ethernet */
	0,	/* Slot 15 - unused */
	9,	/* Slot 16 - PMC 1 */
	12,	/* Slot 17 - pci */
	11,	/* Slot 18 - pci */
	10,	/* Slot 19 - pci */
	0,	/* Slot 20 - pci */
	0,	/* Slot 21 - unused */
	0,	/* Slot 22 - unused */
};

/* Motorola Series-E */
static char Comet_pci_IRQ_map[23] __prepdata =
{
  	0,	/* Slot 0  - unused */
  	0,	/* Slot 1  - unused */
  	0,	/* Slot 2  - unused */
  	0,	/* Slot 3  - unused */
  	0,	/* Slot 4  - unused */
  	0,	/* Slot 5  - unused */
  	0,	/* Slot 6  - unused */
  	0,	/* Slot 7  - unused */
  	0,	/* Slot 8  - unused */
  	0,	/* Slot 9  - unused */
  	0,	/* Slot 10 - unused */
  	0,	/* Slot 11 - unused */
  	3,	/* Slot 12 - SCSI */
  	0,	/* Slot 13 - unused */
  	1,	/* Slot 14 - Ethernet */
  	0,	/* Slot 15 - unused */
	1,	/* Slot 16 - PCI slot 1 */
	2,	/* Slot 17 - PCI slot 2 */
	3,	/* Slot 18 - PCI slot 3 */
	4,	/* Slot 19 - PCI bridge */
	0,
	0,
	0,
};

static char Comet_pci_IRQ_routes[] __prepdata =
{
   	0,	/* Line 0 - Unused */
   	10,	/* Line 1 */
   	11,	/* Line 2 */
   	14,	/* Line 3 */
   	15	/* Line 4 */
};

/* Motorola Series-EX */
static char Comet2_pci_IRQ_map[23] __prepdata =
{
	0,	/* Slot 0  - unused */
	0,	/* Slot 1  - unused */
	3,	/* Slot 2  - SCSI - NCR825A */
	0,	/* Slot 3  - unused */
	1,	/* Slot 4  - Ethernet - DEC2104X */
	0,	/* Slot 5  - unused */
	1,	/* Slot 6  - PCI slot 1 */
	2,	/* Slot 7  - PCI slot 2 */
	3,	/* Slot 8  - PCI slot 3 */
	4,	/* Slot 9  - PCI bridge  */
	0,	/* Slot 10 - unused */
	0,	/* Slot 11 - unused */
	3,	/* Slot 12 - SCSI - NCR825A */
	0,	/* Slot 13 - unused */
	1,	/* Slot 14 - Ethernet - DEC2104X */
	0,	/* Slot 15 - unused */
	1,	/* Slot 16 - PCI slot 1 */
	2,	/* Slot 17 - PCI slot 2 */
	3,	/* Slot 18 - PCI slot 3 */
	4,	/* Slot 19 - PCI bridge */
	0,
	0,
	0,
};

static char Comet2_pci_IRQ_routes[] __prepdata =
{
	0,	/* Line 0 - Unused */
	10,	/* Line 1 */
	11,	/* Line 2 */
	14,	/* Line 3 */
	15,	/* Line 4 */
};

/*
 * ibm 830 (and 850?).
 * This is actually based on the Carolina motherboard
 * -- Cort
 */
static char ibm8xx_pci_IRQ_map[23] __prepdata = {
        0, /* Slot 0  - unused */
        0, /* Slot 1  - unused */
        0, /* Slot 2  - unused */
        0, /* Slot 3  - unused */
        0, /* Slot 4  - unused */
        0, /* Slot 5  - unused */
        0, /* Slot 6  - unused */
        0, /* Slot 7  - unused */
        0, /* Slot 8  - unused */
        0, /* Slot 9  - unused */
        0, /* Slot 10 - unused */
        0, /* Slot 11 - FireCoral */
        4, /* Slot 12 - Ethernet  PCIINTD# */
        2, /* Slot 13 - PCI Slot #2 */
        2, /* Slot 14 - S3 Video PCIINTD# */
        0, /* Slot 15 - onboard SCSI (INDI) [1] */
        3, /* Slot 16 - NCR58C810 RS6000 Only PCIINTC# */
        0, /* Slot 17 - unused */
        2, /* Slot 18 - PCI Slot 2 PCIINTx# (See below) */
        0, /* Slot 19 - unused */
        0, /* Slot 20 - unused */
        0, /* Slot 21 - unused */
        2, /* Slot 22 - PCI slot 1 PCIINTx# (See below) */
};

static char ibm8xx_pci_IRQ_routes[] __prepdata = {
        0,      /* Line 0 - unused */
        15,     /* Line 1 */
        15,     /* Line 2 */
        15,     /* Line 3 */
        15,     /* Line 4 */
};

/*
 * a 6015 ibm board
 * -- Cort
 */
static char ibm6015_pci_IRQ_map[23] __prepdata = {
        0, /* Slot 0  - unused */
        0, /* Slot 1  - unused */
        0, /* Slot 2  - unused */
        0, /* Slot 3  - unused */
        0, /* Slot 4  - unused */
        0, /* Slot 5  - unused */
        0, /* Slot 6  - unused */
        0, /* Slot 7  - unused */
        0, /* Slot 8  - unused */
        0, /* Slot 9  - unused */
        0, /* Slot 10 - unused */
        0, /* Slot 11 -  */
        1, /* Slot 12 - SCSI */
        2, /* Slot 13 -  */
        2, /* Slot 14 -  */
        1, /* Slot 15 -  */
        1, /* Slot 16 -  */
        0, /* Slot 17 -  */
        2, /* Slot 18 -  */
        0, /* Slot 19 -  */
        0, /* Slot 20 -  */
        0, /* Slot 21 -  */
        2, /* Slot 22 -  */
};

static char ibm6015_pci_IRQ_routes[] __prepdata = {
        0,      /* Line 0 - unused */
        13,     /* Line 1 */
        15,     /* Line 2 */
        15,     /* Line 3 */
        15,     /* Line 4 */
};


/* IBM Nobis and Thinkpad 850 */
static char Nobis_pci_IRQ_map[23] __prepdata ={
        0, /* Slot 0  - unused */
        0, /* Slot 1  - unused */
        0, /* Slot 2  - unused */
        0, /* Slot 3  - unused */
        0, /* Slot 4  - unused */
        0, /* Slot 5  - unused */
        0, /* Slot 6  - unused */
        0, /* Slot 7  - unused */
        0, /* Slot 8  - unused */
        0, /* Slot 9  - unused */
        0, /* Slot 10 - unused */
        0, /* Slot 11 - unused */
        3, /* Slot 12 - SCSI */
        0, /* Slot 13 - unused */
        0, /* Slot 14 - unused */
        0, /* Slot 15 - unused */
};

static char Nobis_pci_IRQ_routes[] __prepdata = {
        0, /* Line 0 - Unused */
        13, /* Line 1 */
        13, /* Line 2 */
        13, /* Line 3 */
        13      /* Line 4 */
};

/*
 * IBM RS/6000 43p/140  -- paulus
 * XXX we should get all this from the residual data
 */
static char ibm43p_pci_IRQ_map[23] __prepdata = {
        0, /* Slot 0  - unused */
        0, /* Slot 1  - unused */
        0, /* Slot 2  - unused */
        0, /* Slot 3  - unused */
        0, /* Slot 4  - unused */
        0, /* Slot 5  - unused */
        0, /* Slot 6  - unused */
        0, /* Slot 7  - unused */
        0, /* Slot 8  - unused */
        0, /* Slot 9  - unused */
        0, /* Slot 10 - unused */
        0, /* Slot 11 - FireCoral ISA bridge */
        6, /* Slot 12 - Ethernet  */
        0, /* Slot 13 - openpic */
        0, /* Slot 14 - unused */
        0, /* Slot 15 - unused */
        7, /* Slot 16 - NCR58C825a onboard scsi */
        0, /* Slot 17 - unused */
        2, /* Slot 18 - PCI Slot 2 PCIINTx# (See below) */
        0, /* Slot 19 - unused */
        0, /* Slot 20 - unused */
        0, /* Slot 21 - unused */
        1, /* Slot 22 - PCI slot 1 PCIINTx# (See below) */
};

static char ibm43p_pci_IRQ_routes[] __prepdata = {
        0,      /* Line 0 - unused */
        15,     /* Line 1 */
        15,     /* Line 2 */
        15,     /* Line 3 */
        15,     /* Line 4 */
};

/* Motorola PowerPlus architecture PCI IRQ tables */
/* Interrupt line values for INTA-D on primary/secondary MPIC inputs */

struct powerplus_irq_list
{
	unsigned char primary[4];       /* INT A-D */
	unsigned char secondary[4];     /* INT A-D */
};

/*
 * For standard PowerPlus boards, bus 0 PCI INTs A-D are routed to
 * OpenPIC inputs 9-12.  PCI INTs A-D from the on board P2P bridge
 * are routed to OpenPIC inputs 5-8.  These values are offset by
 * 16 in the table to reflect the Linux kernel interrupt value.
 */
struct powerplus_irq_list Powerplus_pci_IRQ_list __prepdata =
{
	{25, 26, 27, 28},
	{21, 22, 23, 24}
};

/*
 * For the MCP750 (system slot board), cPCI INTs A-D are routed to
 * OpenPIC inputs 8-11 and the PMC INTs A-D are routed to OpenPIC
 * input 3.  On a hot swap MCP750, the companion card PCI INTs A-D
 * are routed to OpenPIC inputs 12-15. These values are offset by
 * 16 in the table to reflect the Linux kernel interrupt value.
 */
struct powerplus_irq_list Mesquite_pci_IRQ_list __prepdata =
{
	{24, 25, 26, 27},
	{28, 29, 30, 31}
};

/*
 * This table represents the standard PCI swizzle defined in the
 * PCI bus specification.
 */
static unsigned char prep_pci_intpins[4][4] __prepdata =
{
	{ 1, 2, 3, 4},  /* Buses 0, 4, 8, ... */
	{ 2, 3, 4, 1},  /* Buses 1, 5, 9, ... */
	{ 3, 4, 1, 2},  /* Buses 2, 6, 10 ... */
	{ 4, 1, 2, 3},  /* Buses 3, 7, 11 ... */
};

/* We have to turn on LEVEL mode for changed IRQ's */
/* All PCI IRQ's need to be level mode, so this should be something
 * other than hard-coded as well... IRQ's are individually mappable
 * to either edge or level.
 */

/*
 * 8259 edge/level control definitions
 */
#define ISA8259_M_ELCR 0x4d0
#define ISA8259_S_ELCR 0x4d1

#define ELCRS_INT15_LVL         0x80
#define ELCRS_INT14_LVL         0x40
#define ELCRS_INT12_LVL         0x10
#define ELCRS_INT11_LVL         0x08
#define ELCRS_INT10_LVL         0x04
#define ELCRS_INT9_LVL          0x02
#define ELCRS_INT8_LVL          0x01
#define ELCRM_INT7_LVL          0x80
#define ELCRM_INT5_LVL          0x20

#if 0
/*
 * PCI config space access.
 */
#define CFGADDR(dev)	((1<<(dev>>3)) | ((dev&7)<<8))
#define DEVNO(dev)	(dev>>3)

#define MIN_DEVNR	11
#define MAX_DEVNR	22

static int __prep
prep_read_config(struct pci_bus *bus, unsigned int devfn, int offset,
		 int len, u32 *val)
{
	struct pci_controller *hose = bus->sysdata;
	volatile void __iomem *cfg_data;

	if (bus->number != 0 || DEVNO(devfn) < MIN_DEVNR
	    || DEVNO(devfn) > MAX_DEVNR)
		return PCIBIOS_DEVICE_NOT_FOUND;

	/*
	 * Note: the caller has already checked that offset is
	 * suitably aligned and that len is 1, 2 or 4.
	 */
	cfg_data = hose->cfg_data + CFGADDR(devfn) + offset;
	switch (len) {
	case 1:
		*val = in_8(cfg_data);
		break;
	case 2:
		*val = in_le16(cfg_data);
		break;
	default:
		*val = in_le32(cfg_data);
		break;
	}
	return PCIBIOS_SUCCESSFUL;
}

static int __prep
prep_write_config(struct pci_bus *bus, unsigned int devfn, int offset,
		  int len, u32 val)
{
	struct pci_controller *hose = bus->sysdata;
	volatile void __iomem *cfg_data;

	if (bus->number != 0 || DEVNO(devfn) < MIN_DEVNR
	    || DEVNO(devfn) > MAX_DEVNR)
		return PCIBIOS_DEVICE_NOT_FOUND;

	/*
	 * Note: the caller has already checked that offset is
	 * suitably aligned and that len is 1, 2 or 4.
	 */
	cfg_data = hose->cfg_data + CFGADDR(devfn) + offset;
	switch (len) {
	case 1:
		out_8(cfg_data, val);
		break;
	case 2:
		out_le16(cfg_data, val);
		break;
	default:
		out_le32(cfg_data, val);
		break;
	}
	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops prep_pci_ops =
{
	prep_read_config,
	prep_write_config
};
#endif

#define MOTOROLA_CPUTYPE_REG	0x800
#define MOTOROLA_BASETYPE_REG	0x803
#define MPIC_RAVEN_ID		0x48010000
#define	MPIC_HAWK_ID		0x48030000
#define	MOT_PROC2_BIT		0x800

static u_char prep_openpic_initsenses[] __initdata = {
    (IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE), /* MVME2600_INT_SIO */
    (IRQ_SENSE_EDGE  | IRQ_POLARITY_NEGATIVE), /* MVME2600_INT_FALCN_ECC_ERR */
    (IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE), /* MVME2600_INT_PCI_ETHERNET */
    (IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE), /* MVME2600_INT_PCI_SCSI */
    (IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE), /* MVME2600_INT_PCI_GRAPHICS */
    (IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE), /* MVME2600_INT_PCI_VME0 */
    (IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE), /* MVME2600_INT_PCI_VME1 */
    (IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE), /* MVME2600_INT_PCI_VME2 */
    (IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE), /* MVME2600_INT_PCI_VME3 */
    (IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE), /* MVME2600_INT_PCI_INTA */
    (IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE), /* MVME2600_INT_PCI_INTB */
    (IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE), /* MVME2600_INT_PCI_INTC */
    (IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE), /* MVME2600_INT_PCI_INTD */
    (IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE), /* MVME2600_INT_LM_SIG0 */
    (IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE), /* MVME2600_INT_LM_SIG1 */
};

#define MOT_RAVEN_PRESENT	0x1
#define MOT_HAWK_PRESENT	0x2

int mot_entry = -1;
int prep_keybd_present = 1;
int MotMPIC;
int mot_multi;

int __init
raven_init(void)
{
	unsigned int	devid;
	unsigned int	pci_membase;
	unsigned char	base_mod;

	/* Check to see if the Raven chip exists. */
	if ( _prep_type != _PREP_Motorola) {
		OpenPIC_Addr = NULL;
		return 0;
	}

	/* Check to see if this board is a type that might have a Raven. */
	if ((inb(MOTOROLA_CPUTYPE_REG) & 0xF0) != 0xE0) {
		OpenPIC_Addr = NULL;
		return 0;
	}

	/* Check the first PCI device to see if it is a Raven. */
	early_read_config_dword(NULL, 0, 0, PCI_VENDOR_ID, &devid);

	switch (devid & 0xffff0000) {
	case MPIC_RAVEN_ID:
		MotMPIC = MOT_RAVEN_PRESENT;
		break;
	case MPIC_HAWK_ID:
		MotMPIC = MOT_HAWK_PRESENT;
		break;
	default:
		OpenPIC_Addr = NULL;
		return 0;
	}


	/* Read the memory base register. */
	early_read_config_dword(NULL, 0, 0, PCI_BASE_ADDRESS_1, &pci_membase);

	if (pci_membase == 0) {
		OpenPIC_Addr = NULL;
		return 0;
	}

	/* Map the Raven MPIC registers to virtual memory. */
	OpenPIC_Addr = ioremap(pci_membase+0xC0000000, 0x22000);

	OpenPIC_InitSenses = prep_openpic_initsenses;
	OpenPIC_NumInitSenses = sizeof(prep_openpic_initsenses);

	ppc_md.get_irq = openpic_get_irq;

	/* If raven is present on Motorola store the system config register
	 * for later use.
	 */
	ProcInfo = (unsigned long *)ioremap(0xfef80400, 4);

	/* Indicate to system if this is a multiprocessor board */
	if (!(*ProcInfo & MOT_PROC2_BIT)) {
		mot_multi = 1;
	}

	/* This is a hack.  If this is a 2300 or 2400 mot board then there is
	 * no keyboard controller and we have to indicate that.
	 */
	base_mod = inb(MOTOROLA_BASETYPE_REG);
	if ((MotMPIC == MOT_HAWK_PRESENT) || (base_mod == 0xF9) ||
	    (base_mod == 0xFA) || (base_mod == 0xE1))
		prep_keybd_present = 0;

	return 1;
}

struct mot_info {
	int		cpu_type;	/* 0x100 mask assumes for Raven and Hawk boards that the level/edge are set */
					/* 0x200 if this board has a Hawk chip. */
	int		base_type;
	int		max_cpu;	/* ored with 0x80 if this board should be checked for multi CPU */
	const char	*name;
	unsigned char	*map;
	unsigned char	*routes;
	void            (*map_non0_bus)(struct pci_dev *);      /* For boards with more than bus 0 devices. */
	struct powerplus_irq_list *pci_irq_list; /* List of PCI MPIC inputs */
	unsigned char   secondary_bridge_devfn; /* devfn of secondary bus transparent bridge */
} mot_info[] __prepdata = {
	{0x300, 0x00, 0x00, "MVME 2400",			Genesis2_pci_IRQ_map,	Raven_pci_IRQ_routes, Powerplus_Map_Non0, &Powerplus_pci_IRQ_list, 0xFF},
	{0x010, 0x00, 0x00, "Genesis",				Genesis_pci_IRQ_map,	Genesis_pci_IRQ_routes, Powerplus_Map_Non0, &Powerplus_pci_IRQ_list, 0x00},
	{0x020, 0x00, 0x00, "Powerstack (Series E)",		Comet_pci_IRQ_map,	Comet_pci_IRQ_routes, NULL, NULL, 0x00},
	{0x040, 0x00, 0x00, "Blackhawk (Powerstack)",		Blackhawk_pci_IRQ_map,	Blackhawk_pci_IRQ_routes, NULL, NULL, 0x00},
	{0x050, 0x00, 0x00, "Omaha (PowerStack II Pro3000)",	Omaha_pci_IRQ_map,	Omaha_pci_IRQ_routes, NULL, NULL, 0x00},
	{0x060, 0x00, 0x00, "Utah (Powerstack II Pro4000)",	Utah_pci_IRQ_map,	Utah_pci_IRQ_routes, NULL, NULL, 0x00},
	{0x0A0, 0x00, 0x00, "Powerstack (Series EX)",		Comet2_pci_IRQ_map,	Comet2_pci_IRQ_routes, NULL, NULL, 0x00},
	{0x1E0, 0xE0, 0x00, "Mesquite cPCI (MCP750)",		Mesquite_pci_IRQ_map,	Raven_pci_IRQ_routes, Powerplus_Map_Non0, &Mesquite_pci_IRQ_list, 0xFF},
	{0x1E0, 0xE1, 0x00, "Sitka cPCI (MCPN750)",		Sitka_pci_IRQ_map,	Raven_pci_IRQ_routes, Powerplus_Map_Non0, &Powerplus_pci_IRQ_list, 0xFF},
	{0x1E0, 0xE2, 0x00, "Mesquite cPCI (MCP750) w/ HAC",	Mesquite_pci_IRQ_map,	Raven_pci_IRQ_routes, Powerplus_Map_Non0, &Mesquite_pci_IRQ_list, 0xC0},
	{0x1E0, 0xF6, 0x80, "MTX Plus",				MTXplus_pci_IRQ_map,	Raven_pci_IRQ_routes, Powerplus_Map_Non0, &Powerplus_pci_IRQ_list, 0xA0},
	{0x1E0, 0xF6, 0x81, "Dual MTX Plus",			MTXplus_pci_IRQ_map,	Raven_pci_IRQ_routes, Powerplus_Map_Non0, &Powerplus_pci_IRQ_list, 0xA0},
	{0x1E0, 0xF7, 0x80, "MTX wo/ Parallel Port",		MTX_pci_IRQ_map,	Raven_pci_IRQ_routes, Powerplus_Map_Non0, &Powerplus_pci_IRQ_list, 0x00},
	{0x1E0, 0xF7, 0x81, "Dual MTX wo/ Parallel Port",	MTX_pci_IRQ_map,	Raven_pci_IRQ_routes, Powerplus_Map_Non0, &Powerplus_pci_IRQ_list, 0x00},
	{0x1E0, 0xF8, 0x80, "MTX w/ Parallel Port",		MTX_pci_IRQ_map,	Raven_pci_IRQ_routes, Powerplus_Map_Non0, &Powerplus_pci_IRQ_list, 0x00},
	{0x1E0, 0xF8, 0x81, "Dual MTX w/ Parallel Port",	MTX_pci_IRQ_map,	Raven_pci_IRQ_routes, Powerplus_Map_Non0, &Powerplus_pci_IRQ_list, 0x00},
	{0x1E0, 0xF9, 0x00, "MVME 2300",			Genesis2_pci_IRQ_map,	Raven_pci_IRQ_routes, Powerplus_Map_Non0, &Powerplus_pci_IRQ_list, 0xFF},
	{0x1E0, 0xFA, 0x00, "MVME 2300SC/2600",			Genesis2_pci_IRQ_map,	Raven_pci_IRQ_routes, Powerplus_Map_Non0, &Powerplus_pci_IRQ_list, 0xFF},
	{0x1E0, 0xFB, 0x00, "MVME 2600 with MVME712M",		Genesis2_pci_IRQ_map,	Raven_pci_IRQ_routes, Powerplus_Map_Non0, &Powerplus_pci_IRQ_list, 0xFF},
	{0x1E0, 0xFC, 0x00, "MVME 2600/2700 with MVME761",	Genesis2_pci_IRQ_map,	Raven_pci_IRQ_routes, Powerplus_Map_Non0, &Powerplus_pci_IRQ_list, 0xFF},
	{0x1E0, 0xFD, 0x80, "MVME 3600 with MVME712M",		Genesis2_pci_IRQ_map,	Raven_pci_IRQ_routes, Powerplus_Map_Non0, &Powerplus_pci_IRQ_list, 0x00},
	{0x1E0, 0xFD, 0x81, "MVME 4600 with MVME712M",		Genesis2_pci_IRQ_map,	Raven_pci_IRQ_routes, Powerplus_Map_Non0, &Powerplus_pci_IRQ_list, 0xFF},
	{0x1E0, 0xFE, 0x80, "MVME 3600 with MVME761",		Genesis2_pci_IRQ_map,	Raven_pci_IRQ_routes, Powerplus_Map_Non0, &Powerplus_pci_IRQ_list, 0xFF},
	{0x1E0, 0xFE, 0x81, "MVME 4600 with MVME761",		Genesis2_pci_IRQ_map,	Raven_pci_IRQ_routes, Powerplus_Map_Non0, &Powerplus_pci_IRQ_list, 0xFF},
	{0x1E0, 0xFF, 0x00, "MVME 1600-001 or 1600-011",	Genesis2_pci_IRQ_map,	Raven_pci_IRQ_routes, Powerplus_Map_Non0, &Powerplus_pci_IRQ_list, 0xFF},
	{0x000, 0x00, 0x00, "",					NULL,			NULL, NULL, NULL, 0x00}
};

void __init
ibm_prep_init(void)
{
	if (have_residual_data) {
		u32 addr, real_addr, len, offset;
		PPC_DEVICE *mpic;
		PnP_TAG_PACKET *pkt;

		/* Use the PReP residual data to determine if an OpenPIC is
		 * present.  If so, get the large vendor packet which will
		 * tell us the base address and length in memory.
		 * If we are successful, ioremap the memory area and set
		 * OpenPIC_Addr (this indicates that the OpenPIC was found).
		 */
		mpic = residual_find_device(-1, NULL, SystemPeripheral,
				    ProgrammableInterruptController, MPIC, 0);
		if (!mpic)
			return;

		pkt = PnP_find_large_vendor_packet(res->DevicePnPHeap +
				mpic->AllocatedOffset, 9, 0);

		if (!pkt)
			return;

#define p pkt->L4_Pack.L4_Data.L4_PPCPack
	 	if (p.PPCData[1] == 32) {
			switch (p.PPCData[0]) {
				case 1:  offset = PREP_ISA_IO_BASE;  break;
				case 2:  offset = PREP_ISA_MEM_BASE; break;
				default: return; /* Not I/O or memory?? */
			}
		}
		else
			return; /* Not a 32-bit address */

		real_addr = ld_le32((unsigned int *) (p.PPCData + 4));
		if (real_addr == 0xffffffff)
			return;

		/* Adjust address to be as seen by CPU */
		addr = real_addr + offset;

		len = ld_le32((unsigned int *) (p.PPCData + 12));
		if (!len)
			return;
#undef p
		OpenPIC_Addr = ioremap(addr, len);
		ppc_md.get_irq = openpic_get_irq;

		OpenPIC_InitSenses = prep_openpic_initsenses;
		OpenPIC_NumInitSenses = sizeof(prep_openpic_initsenses);

		printk(KERN_INFO "MPIC at 0x%08x (0x%08x), length 0x%08x "
		       "mapped to 0x%p\n", addr, real_addr, len, OpenPIC_Addr);
	}
}

static void __init
ibm43p_pci_map_non0(struct pci_dev *dev)
{
	unsigned char intpin;
	static unsigned char bridge_intrs[4] = { 3, 4, 5, 8 };

	if (dev == NULL)
		return;
	pci_read_config_byte(dev, PCI_INTERRUPT_PIN, &intpin);
	if (intpin < 1 || intpin > 4)
		return;
	intpin = (PCI_SLOT(dev->devfn) + intpin - 1) & 3;
	dev->irq = openpic_to_irq(bridge_intrs[intpin]);
	pci_write_config_byte(dev, PCI_INTERRUPT_LINE, dev->irq);
}

void __init
prep_residual_setup_pci(char *irq_edge_mask_lo, char *irq_edge_mask_hi)
{
	if (have_residual_data) {
		Motherboard_map_name = res->VitalProductData.PrintableModel;
		Motherboard_map = NULL;
		Motherboard_routes = NULL;
		residual_irq_mask(irq_edge_mask_lo, irq_edge_mask_hi);
	}
}

void __init
prep_sandalfoot_setup_pci(char *irq_edge_mask_lo, char *irq_edge_mask_hi)
{
	Motherboard_map_name = "IBM 6015/7020 (Sandalfoot/Sandalbow)";
	Motherboard_map = ibm6015_pci_IRQ_map;
	Motherboard_routes = ibm6015_pci_IRQ_routes;
	*irq_edge_mask_lo = 0x00; /* irq's 0-7 all edge-triggered */
	*irq_edge_mask_hi = 0xA0; /* irq's 13, 15 level-triggered */
}

void __init
prep_thinkpad_setup_pci(char *irq_edge_mask_lo, char *irq_edge_mask_hi)
{
	Motherboard_map_name = "IBM Thinkpad 850/860";
	Motherboard_map = Nobis_pci_IRQ_map;
	Motherboard_routes = Nobis_pci_IRQ_routes;
	*irq_edge_mask_lo = 0x00; /* irq's 0-7 all edge-triggered */
	*irq_edge_mask_hi = 0xA0; /* irq's 13, 15 level-triggered */
}

void __init
prep_carolina_setup_pci(char *irq_edge_mask_lo, char *irq_edge_mask_hi)
{
	Motherboard_map_name = "IBM 7248, PowerSeries 830/850 (Carolina)";
	Motherboard_map = ibm8xx_pci_IRQ_map;
	Motherboard_routes = ibm8xx_pci_IRQ_routes;
	*irq_edge_mask_lo = 0x00; /* irq's 0-7 all edge-triggered */
	*irq_edge_mask_hi = 0xA4; /* irq's 10, 13, 15 level-triggered */
}

void __init
prep_tiger1_setup_pci(char *irq_edge_mask_lo, char *irq_edge_mask_hi)
{
	Motherboard_map_name = "IBM 43P-140 (Tiger1)";
	Motherboard_map = ibm43p_pci_IRQ_map;
	Motherboard_routes = ibm43p_pci_IRQ_routes;
	Motherboard_non0 = ibm43p_pci_map_non0;
	*irq_edge_mask_lo = 0x00; /* irq's 0-7 all edge-triggered */
	*irq_edge_mask_hi = 0xA0; /* irq's 13, 15 level-triggered */
}

void __init
prep_route_pci_interrupts(void)
{
	unsigned char *ibc_pirq = (unsigned char *)0x80800860;
	unsigned char *ibc_pcicon = (unsigned char *)0x80800840;
	int i;

	if ( _prep_type == _PREP_Motorola)
	{
		unsigned short irq_mode;
		unsigned char  cpu_type;
		unsigned char  base_mod;
		int	       entry;

		cpu_type = inb(MOTOROLA_CPUTYPE_REG) & 0xF0;
		base_mod = inb(MOTOROLA_BASETYPE_REG);

		for (entry = 0; mot_info[entry].cpu_type != 0; entry++) {
			if (mot_info[entry].cpu_type & 0x200) {		 	/* Check for Hawk chip */
				if (!(MotMPIC & MOT_HAWK_PRESENT))
					continue;
			} else {						/* Check non hawk boards */
				if ((mot_info[entry].cpu_type & 0xff) != cpu_type)
					continue;

				if (mot_info[entry].base_type == 0) {
					mot_entry = entry;
					break;
				}

				if (mot_info[entry].base_type != base_mod)
					continue;
			}

			if (!(mot_info[entry].max_cpu & 0x80)) {
				mot_entry = entry;
				break;
			}

			/* processor 1 not present and max processor zero indicated */
			if ((*ProcInfo & MOT_PROC2_BIT) && !(mot_info[entry].max_cpu & 0x7f)) {
				mot_entry = entry;
				break;
			}

			/* processor 1 present and max processor zero indicated */
			if (!(*ProcInfo & MOT_PROC2_BIT) && (mot_info[entry].max_cpu & 0x7f)) {
				mot_entry = entry;
				break;
			}
		}

		if (mot_entry == -1) 	/* No particular cpu type found - assume Blackhawk */
			mot_entry = 3;

		Motherboard_map_name = (unsigned char *)mot_info[mot_entry].name;
		Motherboard_map = mot_info[mot_entry].map;
		Motherboard_routes = mot_info[mot_entry].routes;
		Motherboard_non0 = mot_info[mot_entry].map_non0_bus;

		if (!(mot_info[entry].cpu_type & 0x100)) {
			/* AJF adjust level/edge control according to routes */
			irq_mode = 0;
			for (i = 1;  i <= 4;  i++)
				irq_mode |= ( 1 << Motherboard_routes[i] );
			outb( irq_mode & 0xff, 0x4d0 );
			outb( (irq_mode >> 8) & 0xff, 0x4d1 );
		}
	} else if ( _prep_type == _PREP_IBM ) {
		unsigned char irq_edge_mask_lo, irq_edge_mask_hi;
		unsigned short irq_edge_mask;
		int i;

		setup_ibm_pci(&irq_edge_mask_lo, &irq_edge_mask_hi);

		outb(inb(0x04d0)|irq_edge_mask_lo, 0x4d0); /* primary 8259 */
		outb(inb(0x04d1)|irq_edge_mask_hi, 0x4d1); /* cascaded 8259 */

		irq_edge_mask = (irq_edge_mask_hi << 8) | irq_edge_mask_lo;
		for (i = 0; i < 16; ++i, irq_edge_mask >>= 1)
			if (irq_edge_mask & 1)
				irq_desc[i].status |= IRQ_LEVEL;
	} else {
		printk("No known machine pci routing!\n");
		return;
	}

	/* Set up mapping from slots */
	if (Motherboard_routes) {
		for (i = 1;  i <= 4;  i++)
			ibc_pirq[i-1] = Motherboard_routes[i];

		/* Enable PCI interrupts */
		*ibc_pcicon |= 0x20;
	}
}

void __init
prep_pib_init(void)
{
	unsigned char   reg;
	unsigned short  short_reg;

	struct pci_dev *dev = NULL;

	if (( _prep_type == _PREP_Motorola) && (OpenPIC_Addr)) {
		/*
		 * Perform specific configuration for the Via Tech or
		 * or Winbond PCI-ISA-Bridge part.
		 */
		if ((dev = pci_get_device(PCI_VENDOR_ID_VIA,
					PCI_DEVICE_ID_VIA_82C586_1, dev))) {
			/*
			 * PPCBUG does not set the enable bits
			 * for the IDE device. Force them on here.
			 */
			pci_read_config_byte(dev, 0x40, &reg);

			reg |= 0x03; /* IDE: Chip Enable Bits */
			pci_write_config_byte(dev, 0x40, reg);
		}
		if ((dev = pci_get_device(PCI_VENDOR_ID_VIA,
						PCI_DEVICE_ID_VIA_82C586_2,
						dev)) && (dev->devfn = 0x5a)) {
			/* Force correct USB interrupt */
			dev->irq = 11;
			pci_write_config_byte(dev,
					PCI_INTERRUPT_LINE,
					dev->irq);
		}
		if ((dev = pci_get_device(PCI_VENDOR_ID_WINBOND,
					PCI_DEVICE_ID_WINBOND_83C553, dev))) {
			 /* Clear PCI Interrupt Routing Control Register. */
			short_reg = 0x0000;
			pci_write_config_word(dev, 0x44, short_reg);
			if (OpenPIC_Addr){
				/* Route IDE interrupts to IRQ 14 */
				reg = 0xEE;
				pci_write_config_byte(dev, 0x43, reg);
			}
		}
		pci_dev_put(dev);
	}

	if ((dev = pci_get_device(PCI_VENDOR_ID_WINBOND,
				   PCI_DEVICE_ID_WINBOND_82C105, dev))){
		if (OpenPIC_Addr){
			/*
			 * Disable LEGIRQ mode so PCI INTS are routed
			 * directly to the 8259 and enable both channels
			 */
			pci_write_config_dword(dev, 0x40, 0x10ff0033);

			/* Force correct IDE interrupt */
			dev->irq = 14;
			pci_write_config_byte(dev,
					PCI_INTERRUPT_LINE,
					dev->irq);
		} else {
			/* Enable LEGIRQ for PCI INT -> 8259 IRQ routing */
			pci_write_config_dword(dev, 0x40, 0x10ff08a1);
		}
	}
	pci_dev_put(dev);
}

static void __init
Powerplus_Map_Non0(struct pci_dev *dev)
{
	struct pci_bus  *pbus;          /* Parent bus structure pointer */
	struct pci_dev  *tdev = dev;    /* Temporary device structure */
	unsigned int    devnum;         /* Accumulated device number */
	unsigned char   intline;        /* Linux interrupt value */
	unsigned char   intpin;         /* PCI interrupt pin */

	/* Check for valid PCI dev pointer */
	if (dev == NULL) return;

	/* Initialize bridge IDSEL variable */
	devnum = PCI_SLOT(tdev->devfn);

	/* Read the interrupt pin of the device and adjust for indexing */
	pci_read_config_byte(dev, PCI_INTERRUPT_PIN, &intpin);

	/* If device doesn't request an interrupt, return */
	if ( (intpin < 1) || (intpin > 4) )
		return;

	intpin--;

	/*
	 * Walk up to bus 0, adjusting the interrupt pin for the standard
	 * PCI bus swizzle.
	 */
	do {
		intpin = (prep_pci_intpins[devnum % 4][intpin]) - 1;
		pbus = tdev->bus;        /* up one level */
		tdev = pbus->self;
		devnum = PCI_SLOT(tdev->devfn);
	} while(tdev->bus->number);

	/* Use the primary interrupt inputs by default */
	intline = mot_info[mot_entry].pci_irq_list->primary[intpin];

	/*
	 * If the board has secondary interrupt inputs, walk the bus and
	 * note the devfn of the bridge from bus 0.  If it is the same as
	 * the devfn of the bus bridge with secondary inputs, use those.
	 * Otherwise, assume it's a PMC site and get the interrupt line
	 * value from the interrupt routing table.
	 */
	if (mot_info[mot_entry].secondary_bridge_devfn) {
		pbus = dev->bus;

		while (pbus->primary != 0)
			pbus = pbus->parent;

		if ((pbus->self)->devfn != 0xA0) {
			if ((pbus->self)->devfn == mot_info[mot_entry].secondary_bridge_devfn)
				intline = mot_info[mot_entry].pci_irq_list->secondary[intpin];
			else {
				if ((char *)(mot_info[mot_entry].map) == (char *)Mesquite_pci_IRQ_map)
					intline = mot_info[mot_entry].map[((pbus->self)->devfn)/8] + 16;
				else {
					int i;
					for (i=0;i<3;i++)
						intpin = (prep_pci_intpins[devnum % 4][intpin]) - 1;
					intline = mot_info[mot_entry].pci_irq_list->primary[intpin];
				}
			}
		}
	}

	/* Write calculated interrupt value to header and device list */
	dev->irq = intline;
	pci_write_config_byte(dev, PCI_INTERRUPT_LINE, (u8)dev->irq);
}

void __init
prep_pcibios_fixup(void)
{
        struct pci_dev *dev = NULL;
	int irq;
	int have_openpic = (OpenPIC_Addr != NULL);

	prep_route_pci_interrupts();

	printk("Setting PCI interrupts for a \"%s\"\n", Motherboard_map_name);

	/* Iterate through all the PCI devices, setting the IRQ */
	for_each_pci_dev(dev) {
		/*
		 * If we have residual data, then this is easy: query the
		 * residual data for the IRQ line allocated to the device.
		 * This works the same whether we have an OpenPic or not.
		 */
		if (have_residual_data) {
			irq = residual_pcidev_irq(dev);
			dev->irq = have_openpic ? openpic_to_irq(irq) : irq;
		}
		/*
		 * If we don't have residual data, then we need to use
		 * tables to determine the IRQ.  The table organisation
		 * is different depending on whether there is an OpenPIC
		 * or not.  The tables are only used for bus 0, so check
		 * this first.
		 */
		else if (dev->bus->number == 0) {
			irq = Motherboard_map[PCI_SLOT(dev->devfn)];
			dev->irq = have_openpic ? openpic_to_irq(irq)
						: Motherboard_routes[irq];
		}
		/*
		 * Finally, if we don't have residual data and the bus is
		 * non-zero, use the callback (if provided)
		 */
		else {
			if (Motherboard_non0 != NULL)
				Motherboard_non0(dev);

			continue;
		}

		pci_write_config_byte(dev, PCI_INTERRUPT_LINE, dev->irq);
	}

	/* Setup the Winbond or Via PIB */
	prep_pib_init();
}

static void __init
prep_pcibios_after_init(void)
{
#if 0
	struct pci_dev *dev;

	/* If there is a WD 90C, reset the IO BAR to 0x0 (it started that
	 * way, but the PCI layer relocated it because it thought 0x0 was
	 * invalid for a BAR).
	 * If you don't do this, the card's VGA base will be <IO BAR>+0xc0000
	 * instead of 0xc0000. vgacon.c (for example) is completely unaware of
	 * this little quirk.
	 */
	dev = pci_get_device(PCI_VENDOR_ID_WD, PCI_DEVICE_ID_WD_90C, NULL);
	if (dev) {
		dev->resource[1].end -= dev->resource[1].start;
		dev->resource[1].start = 0;
		/* tell the hardware */
		pci_write_config_dword(dev, PCI_BASE_ADDRESS_1, 0x0);
		pci_dev_put(dev);
	}
#endif
}

static void __init
prep_init_resource(struct resource *res, unsigned long start,
		   unsigned long end, int flags)
{
	res->flags = flags;
	res->start = start;
	res->end = end;
	res->name = "PCI host bridge";
	res->parent = NULL;
	res->sibling = NULL;
	res->child = NULL;
}

void __init
prep_find_bridges(void)
{
	struct pci_controller* hose;

	hose = pcibios_alloc_controller();
	if (!hose)
		return;

	hose->first_busno = 0;
	hose->last_busno = 0xff;
	hose->pci_mem_offset = PREP_ISA_MEM_BASE;
	hose->io_base_phys = PREP_ISA_IO_BASE;
	hose->io_base_virt = ioremap(PREP_ISA_IO_BASE, 0x800000);
	prep_init_resource(&hose->io_resource, 0, 0x007fffff, IORESOURCE_IO);
	prep_init_resource(&hose->mem_resources[0], 0xc0000000, 0xfeffffff,
			   IORESOURCE_MEM);
	setup_indirect_pci(hose, PREP_ISA_IO_BASE + 0xcf8,
			   PREP_ISA_IO_BASE + 0xcfc);

	printk("PReP architecture\n");

	if (have_residual_data) {
		PPC_DEVICE *hostbridge;

		hostbridge = residual_find_device(PROCESSORDEVICE, NULL,
			BridgeController, PCIBridge, -1, 0);
		if (hostbridge &&
			((hostbridge->DeviceId.Interface == PCIBridgeIndirect) ||
			 (hostbridge->DeviceId.Interface == PCIBridgeRS6K))) {
			PnP_TAG_PACKET * pkt;
			pkt = PnP_find_large_vendor_packet(
				res->DevicePnPHeap+hostbridge->AllocatedOffset,
				3, 0);
			if(pkt) {
#define p pkt->L4_Pack.L4_Data.L4_PPCPack
				setup_indirect_pci(hose,
					ld_le32((unsigned *) (p.PPCData)),
					ld_le32((unsigned *) (p.PPCData+8)));
#undef p
			} else
				setup_indirect_pci(hose, 0x80000cf8, 0x80000cfc);
		}
	}

	ppc_md.pcibios_fixup = prep_pcibios_fixup;
	ppc_md.pcibios_after_init = prep_pcibios_after_init;
}
