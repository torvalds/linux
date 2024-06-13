// SPDX-License-Identifier: GPL-2.0
/*
 *	Low-Level PCI Support for PC -- Routing of Interrupts
 *
 *	(c) 1999--2000 Martin Mares <mj@ucw.cz>
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/dmi.h>
#include <linux/io.h>
#include <linux/smp.h>
#include <linux/spinlock.h>
#include <asm/io_apic.h>
#include <linux/irq.h>
#include <linux/acpi.h>

#include <asm/i8259.h>
#include <asm/pc-conf-reg.h>
#include <asm/pci_x86.h>

#define PIRQ_SIGNATURE	(('$' << 0) + ('P' << 8) + ('I' << 16) + ('R' << 24))
#define PIRQ_VERSION 0x0100

#define IRT_SIGNATURE	(('$' << 0) + ('I' << 8) + ('R' << 16) + ('T' << 24))

static int broken_hp_bios_irq9;
static int acer_tm360_irqrouting;

static struct irq_routing_table *pirq_table;

static int pirq_enable_irq(struct pci_dev *dev);
static void pirq_disable_irq(struct pci_dev *dev);

/*
 * Never use: 0, 1, 2 (timer, keyboard, and cascade)
 * Avoid using: 13, 14 and 15 (FP error and IDE).
 * Penalize: 3, 4, 6, 7, 12 (known ISA uses: serial, floppy, parallel and mouse)
 */
unsigned int pcibios_irq_mask = 0xfff8;

static int pirq_penalty[16] = {
	1000000, 1000000, 1000000, 1000, 1000, 0, 1000, 1000,
	0, 0, 0, 0, 1000, 100000, 100000, 100000
};

struct irq_router {
	char *name;
	u16 vendor, device;
	int (*get)(struct pci_dev *router, struct pci_dev *dev, int pirq);
	int (*set)(struct pci_dev *router, struct pci_dev *dev, int pirq,
		int new);
	int (*lvl)(struct pci_dev *router, struct pci_dev *dev, int pirq,
		int irq);
};

struct irq_router_handler {
	u16 vendor;
	int (*probe)(struct irq_router *r, struct pci_dev *router, u16 device);
};

int (*pcibios_enable_irq)(struct pci_dev *dev) = pirq_enable_irq;
void (*pcibios_disable_irq)(struct pci_dev *dev) = pirq_disable_irq;

/*
 *  Check passed address for the PCI IRQ Routing Table signature
 *  and perform checksum verification.
 */

static inline struct irq_routing_table *pirq_check_routing_table(u8 *addr,
								 u8 *limit)
{
	struct irq_routing_table *rt;
	int i;
	u8 sum;

	rt = (struct irq_routing_table *)addr;
	if (rt->signature != PIRQ_SIGNATURE ||
	    rt->version != PIRQ_VERSION ||
	    rt->size % 16 ||
	    rt->size < sizeof(struct irq_routing_table) ||
	    (limit && rt->size > limit - addr))
		return NULL;
	sum = 0;
	for (i = 0; i < rt->size; i++)
		sum += addr[i];
	if (!sum) {
		DBG(KERN_DEBUG "PCI: Interrupt Routing Table found at 0x%lx\n",
		    __pa(rt));
		return rt;
	}
	return NULL;
}

/*
 * Handle the $IRT PCI IRQ Routing Table format used by AMI for its BCP
 * (BIOS Configuration Program) external tool meant for tweaking BIOS
 * structures without the need to rebuild it from sources.  The $IRT
 * format has been invented by AMI before Microsoft has come up with its
 * $PIR format and a $IRT table is therefore there in some systems that
 * lack a $PIR table.
 *
 * It uses the same PCI BIOS 2.1 format for interrupt routing entries
 * themselves but has a different simpler header prepended instead,
 * occupying 8 bytes, where a `$IRT' signature is followed by one byte
 * specifying the total number of interrupt routing entries allocated in
 * the table, then one byte specifying the actual number of entries used
 * (which the BCP tool can take advantage of when modifying the table),
 * and finally a 16-bit word giving the IRQs devoted exclusively to PCI.
 * Unlike with the $PIR table there is no alignment guarantee.
 *
 * Given the similarity of the two formats the $IRT one is trivial to
 * convert to the $PIR one, which we do here, except that obviously we
 * have no information as to the router device to use, but we can handle
 * it by matching PCI device IDs actually seen on the bus against ones
 * that our individual routers recognise.
 *
 * Reportedly there is another $IRT table format where a 16-bit word
 * follows the header instead that points to interrupt routing entries
 * in a $PIR table provided elsewhere.  In that case this code will not
 * be reached though as the $PIR table will have been chosen instead.
 */
static inline struct irq_routing_table *pirq_convert_irt_table(u8 *addr,
							       u8 *limit)
{
	struct irt_routing_table *ir;
	struct irq_routing_table *rt;
	u16 size;
	u8 sum;
	int i;

	ir = (struct irt_routing_table *)addr;
	if (ir->signature != IRT_SIGNATURE || !ir->used || ir->size < ir->used)
		return NULL;

	size = sizeof(*ir) + ir->used * sizeof(ir->slots[0]);
	if (size > limit - addr)
		return NULL;

	DBG(KERN_DEBUG "PCI: $IRT Interrupt Routing Table found at 0x%lx\n",
	    __pa(ir));

	size = sizeof(*rt) + ir->used * sizeof(rt->slots[0]);
	rt = kzalloc(size, GFP_KERNEL);
	if (!rt)
		return NULL;

	rt->signature = PIRQ_SIGNATURE;
	rt->version = PIRQ_VERSION;
	rt->size = size;
	rt->exclusive_irqs = ir->exclusive_irqs;
	for (i = 0; i < ir->used; i++)
		rt->slots[i] = ir->slots[i];

	addr = (u8 *)rt;
	sum = 0;
	for (i = 0; i < size; i++)
		sum += addr[i];
	rt->checksum = -sum;

	return rt;
}

/*
 *  Search 0xf0000 -- 0xfffff for the PCI IRQ Routing Table.
 */

static struct irq_routing_table * __init pirq_find_routing_table(void)
{
	u8 * const bios_start = (u8 *)__va(0xf0000);
	u8 * const bios_end = (u8 *)__va(0x100000);
	u8 *addr;
	struct irq_routing_table *rt;

	if (pirq_table_addr) {
		rt = pirq_check_routing_table((u8 *)__va(pirq_table_addr),
					      NULL);
		if (rt)
			return rt;
		printk(KERN_WARNING "PCI: PIRQ table NOT found at pirqaddr\n");
	}
	for (addr = bios_start;
	     addr < bios_end - sizeof(struct irq_routing_table);
	     addr += 16) {
		rt = pirq_check_routing_table(addr, bios_end);
		if (rt)
			return rt;
	}
	for (addr = bios_start;
	     addr < bios_end - sizeof(struct irt_routing_table);
	     addr++) {
		rt = pirq_convert_irt_table(addr, bios_end);
		if (rt)
			return rt;
	}
	return NULL;
}

/*
 *  If we have a IRQ routing table, use it to search for peer host
 *  bridges.  It's a gross hack, but since there are no other known
 *  ways how to get a list of buses, we have to go this way.
 */

static void __init pirq_peer_trick(void)
{
	struct irq_routing_table *rt = pirq_table;
	u8 busmap[256];
	int i;
	struct irq_info *e;

	memset(busmap, 0, sizeof(busmap));
	for (i = 0; i < (rt->size - sizeof(struct irq_routing_table)) / sizeof(struct irq_info); i++) {
		e = &rt->slots[i];
#ifdef DEBUG
		{
			int j;
			DBG(KERN_DEBUG "%02x:%02x.%x slot=%02x",
			    e->bus, e->devfn / 8, e->devfn % 8, e->slot);
			for (j = 0; j < 4; j++)
				DBG(" %d:%02x/%04x", j, e->irq[j].link, e->irq[j].bitmap);
			DBG("\n");
		}
#endif
		busmap[e->bus] = 1;
	}
	for (i = 1; i < 256; i++) {
		if (!busmap[i] || pci_find_bus(0, i))
			continue;
		pcibios_scan_root(i);
	}
	pcibios_last_bus = -1;
}

/*
 *  Code for querying and setting of IRQ routes on various interrupt routers.
 *  PIC Edge/Level Control Registers (ELCR) 0x4d0 & 0x4d1.
 */

void elcr_set_level_irq(unsigned int irq)
{
	unsigned char mask = 1 << (irq & 7);
	unsigned int port = PIC_ELCR1 + (irq >> 3);
	unsigned char val;
	static u16 elcr_irq_mask;

	if (irq >= 16 || (1 << irq) & elcr_irq_mask)
		return;

	elcr_irq_mask |= (1 << irq);
	printk(KERN_DEBUG "PCI: setting IRQ %u as level-triggered\n", irq);
	val = inb(port);
	if (!(val & mask)) {
		DBG(KERN_DEBUG " -> edge");
		outb(val | mask, port);
	}
}

/*
 *	PIRQ routing for the M1487 ISA Bus Controller (IBC) ASIC used
 *	with the ALi FinALi 486 chipset.  The IBC is not decoded in the
 *	PCI configuration space, so we identify it by the accompanying
 *	M1489 Cache-Memory PCI Controller (CMP) ASIC.
 *
 *	There are four 4-bit mappings provided, spread across two PCI
 *	INTx Routing Table Mapping Registers, available in the port I/O
 *	space accessible indirectly via the index/data register pair at
 *	0x22/0x23, located at indices 0x42 and 0x43 for the INT1/INT2
 *	and INT3/INT4 lines respectively.  The INT1/INT3 and INT2/INT4
 *	lines are mapped in the low and the high 4-bit nibble of the
 *	corresponding register as follows:
 *
 *	0000 : Disabled
 *	0001 : IRQ9
 *	0010 : IRQ3
 *	0011 : IRQ10
 *	0100 : IRQ4
 *	0101 : IRQ5
 *	0110 : IRQ7
 *	0111 : IRQ6
 *	1000 : Reserved
 *	1001 : IRQ11
 *	1010 : Reserved
 *	1011 : IRQ12
 *	1100 : Reserved
 *	1101 : IRQ14
 *	1110 : Reserved
 *	1111 : IRQ15
 *
 *	In addition to the usual ELCR register pair there is a separate
 *	PCI INTx Sensitivity Register at index 0x44 in the same port I/O
 *	space, whose bits 3:0 select the trigger mode for INT[4:1] lines
 *	respectively.  Any bit set to 1 causes interrupts coming on the
 *	corresponding line to be passed to ISA as edge-triggered and
 *	otherwise they are passed as level-triggered.  Manufacturer's
 *	documentation says this register has to be set consistently with
 *	the relevant ELCR register.
 *
 *	Accesses to the port I/O space concerned here need to be unlocked
 *	by writing the value of 0xc5 to the Lock Register at index 0x03
 *	beforehand.  Any other value written to said register prevents
 *	further accesses from reaching the register file, except for the
 *	Lock Register being written with 0xc5 again.
 *
 *	References:
 *
 *	"M1489/M1487: 486 PCI Chip Set", Version 1.2, Acer Laboratories
 *	Inc., July 1997
 */

#define PC_CONF_FINALI_LOCK		0x03u
#define PC_CONF_FINALI_PCI_INTX_RT1	0x42u
#define PC_CONF_FINALI_PCI_INTX_RT2	0x43u
#define PC_CONF_FINALI_PCI_INTX_SENS	0x44u

#define PC_CONF_FINALI_LOCK_KEY		0xc5u

static u8 read_pc_conf_nybble(u8 base, u8 index)
{
	u8 reg = base + (index >> 1);
	u8 x;

	x = pc_conf_get(reg);
	return index & 1 ? x >> 4 : x & 0xf;
}

static void write_pc_conf_nybble(u8 base, u8 index, u8 val)
{
	u8 reg = base + (index >> 1);
	u8 x;

	x = pc_conf_get(reg);
	x = index & 1 ? (x & 0x0f) | (val << 4) : (x & 0xf0) | val;
	pc_conf_set(reg, x);
}

/*
 * FinALi pirq rules are as follows:
 *
 * - bit 0 selects between INTx Routing Table Mapping Registers,
 *
 * - bit 3 selects the nibble within the INTx Routing Table Mapping Register,
 *
 * - bits 7:4 map to bits 3:0 of the PCI INTx Sensitivity Register.
 */
static int pirq_finali_get(struct pci_dev *router, struct pci_dev *dev,
			   int pirq)
{
	static const u8 irqmap[16] = {
		0, 9, 3, 10, 4, 5, 7, 6, 0, 11, 0, 12, 0, 14, 0, 15
	};
	unsigned long flags;
	u8 index;
	u8 x;

	index = (pirq & 1) << 1 | (pirq & 8) >> 3;
	raw_spin_lock_irqsave(&pc_conf_lock, flags);
	pc_conf_set(PC_CONF_FINALI_LOCK, PC_CONF_FINALI_LOCK_KEY);
	x = irqmap[read_pc_conf_nybble(PC_CONF_FINALI_PCI_INTX_RT1, index)];
	pc_conf_set(PC_CONF_FINALI_LOCK, 0);
	raw_spin_unlock_irqrestore(&pc_conf_lock, flags);
	return x;
}

static int pirq_finali_set(struct pci_dev *router, struct pci_dev *dev,
			   int pirq, int irq)
{
	static const u8 irqmap[16] = {
		0, 0, 0, 2, 4, 5, 7, 6, 0, 1, 3, 9, 11, 0, 13, 15
	};
	u8 val = irqmap[irq];
	unsigned long flags;
	u8 index;

	if (!val)
		return 0;

	index = (pirq & 1) << 1 | (pirq & 8) >> 3;
	raw_spin_lock_irqsave(&pc_conf_lock, flags);
	pc_conf_set(PC_CONF_FINALI_LOCK, PC_CONF_FINALI_LOCK_KEY);
	write_pc_conf_nybble(PC_CONF_FINALI_PCI_INTX_RT1, index, val);
	pc_conf_set(PC_CONF_FINALI_LOCK, 0);
	raw_spin_unlock_irqrestore(&pc_conf_lock, flags);
	return 1;
}

static int pirq_finali_lvl(struct pci_dev *router, struct pci_dev *dev,
			   int pirq, int irq)
{
	u8 mask = ~((pirq & 0xf0u) >> 4);
	unsigned long flags;
	u8 trig;

	elcr_set_level_irq(irq);
	raw_spin_lock_irqsave(&pc_conf_lock, flags);
	pc_conf_set(PC_CONF_FINALI_LOCK, PC_CONF_FINALI_LOCK_KEY);
	trig = pc_conf_get(PC_CONF_FINALI_PCI_INTX_SENS);
	trig &= mask;
	pc_conf_set(PC_CONF_FINALI_PCI_INTX_SENS, trig);
	pc_conf_set(PC_CONF_FINALI_LOCK, 0);
	raw_spin_unlock_irqrestore(&pc_conf_lock, flags);
	return 1;
}

/*
 * Common IRQ routing practice: nibbles in config space,
 * offset by some magic constant.
 */
static unsigned int read_config_nybble(struct pci_dev *router, unsigned offset, unsigned nr)
{
	u8 x;
	unsigned reg = offset + (nr >> 1);

	pci_read_config_byte(router, reg, &x);
	return (nr & 1) ? (x >> 4) : (x & 0xf);
}

static void write_config_nybble(struct pci_dev *router, unsigned offset,
	unsigned nr, unsigned int val)
{
	u8 x;
	unsigned reg = offset + (nr >> 1);

	pci_read_config_byte(router, reg, &x);
	x = (nr & 1) ? ((x & 0x0f) | (val << 4)) : ((x & 0xf0) | val);
	pci_write_config_byte(router, reg, x);
}

/*
 * ALI pirq entries are damn ugly, and completely undocumented.
 * This has been figured out from pirq tables, and it's not a pretty
 * picture.
 */
static int pirq_ali_get(struct pci_dev *router, struct pci_dev *dev, int pirq)
{
	static const unsigned char irqmap[16] = { 0, 9, 3, 10, 4, 5, 7, 6, 1, 11, 0, 12, 0, 14, 0, 15 };

	WARN_ON_ONCE(pirq > 16);
	return irqmap[read_config_nybble(router, 0x48, pirq-1)];
}

static int pirq_ali_set(struct pci_dev *router, struct pci_dev *dev, int pirq, int irq)
{
	static const unsigned char irqmap[16] = { 0, 8, 0, 2, 4, 5, 7, 6, 0, 1, 3, 9, 11, 0, 13, 15 };
	unsigned int val = irqmap[irq];

	WARN_ON_ONCE(pirq > 16);
	if (val) {
		write_config_nybble(router, 0x48, pirq-1, val);
		return 1;
	}
	return 0;
}

/*
 *	PIRQ routing for the 82374EB/82374SB EISA System Component (ESC)
 *	ASIC used with the Intel 82420 and 82430 PCIsets.  The ESC is not
 *	decoded in the PCI configuration space, so we identify it by the
 *	accompanying 82375EB/82375SB PCI-EISA Bridge (PCEB) ASIC.
 *
 *	There are four PIRQ Route Control registers, available in the
 *	port I/O space accessible indirectly via the index/data register
 *	pair at 0x22/0x23, located at indices 0x60/0x61/0x62/0x63 for the
 *	PIRQ0/1/2/3# lines respectively.  The semantics is the same as
 *	with the PIIX router.
 *
 *	Accesses to the port I/O space concerned here need to be unlocked
 *	by writing the value of 0x0f to the ESC ID Register at index 0x02
 *	beforehand.  Any other value written to said register prevents
 *	further accesses from reaching the register file, except for the
 *	ESC ID Register being written with 0x0f again.
 *
 *	References:
 *
 *	"82374EB/82374SB EISA System Component (ESC)", Intel Corporation,
 *	Order Number: 290476-004, March 1996
 *
 *	"82375EB/82375SB PCI-EISA Bridge (PCEB)", Intel Corporation, Order
 *	Number: 290477-004, March 1996
 */

#define PC_CONF_I82374_ESC_ID			0x02u
#define PC_CONF_I82374_PIRQ_ROUTE_CONTROL	0x60u

#define PC_CONF_I82374_ESC_ID_KEY		0x0fu

static int pirq_esc_get(struct pci_dev *router, struct pci_dev *dev, int pirq)
{
	unsigned long flags;
	int reg;
	u8 x;

	reg = pirq;
	if (reg >= 1 && reg <= 4)
		reg += PC_CONF_I82374_PIRQ_ROUTE_CONTROL - 1;

	raw_spin_lock_irqsave(&pc_conf_lock, flags);
	pc_conf_set(PC_CONF_I82374_ESC_ID, PC_CONF_I82374_ESC_ID_KEY);
	x = pc_conf_get(reg);
	pc_conf_set(PC_CONF_I82374_ESC_ID, 0);
	raw_spin_unlock_irqrestore(&pc_conf_lock, flags);
	return (x < 16) ? x : 0;
}

static int pirq_esc_set(struct pci_dev *router, struct pci_dev *dev, int pirq,
		       int irq)
{
	unsigned long flags;
	int reg;

	reg = pirq;
	if (reg >= 1 && reg <= 4)
		reg += PC_CONF_I82374_PIRQ_ROUTE_CONTROL - 1;

	raw_spin_lock_irqsave(&pc_conf_lock, flags);
	pc_conf_set(PC_CONF_I82374_ESC_ID, PC_CONF_I82374_ESC_ID_KEY);
	pc_conf_set(reg, irq);
	pc_conf_set(PC_CONF_I82374_ESC_ID, 0);
	raw_spin_unlock_irqrestore(&pc_conf_lock, flags);
	return 1;
}

/*
 * The Intel PIIX4 pirq rules are fairly simple: "pirq" is
 * just a pointer to the config space.
 */
static int pirq_piix_get(struct pci_dev *router, struct pci_dev *dev, int pirq)
{
	u8 x;

	pci_read_config_byte(router, pirq, &x);
	return (x < 16) ? x : 0;
}

static int pirq_piix_set(struct pci_dev *router, struct pci_dev *dev, int pirq, int irq)
{
	pci_write_config_byte(router, pirq, irq);
	return 1;
}

/*
 *	PIRQ routing for the 82426EX ISA Bridge (IB) ASIC used with the
 *	Intel 82420EX PCIset.
 *
 *	There are only two PIRQ Route Control registers, available in the
 *	combined 82425EX/82426EX PCI configuration space, at 0x66 and 0x67
 *	for the PIRQ0# and PIRQ1# lines respectively.  The semantics is
 *	the same as with the PIIX router.
 *
 *	References:
 *
 *	"82420EX PCIset Data Sheet, 82425EX PCI System Controller (PSC)
 *	and 82426EX ISA Bridge (IB)", Intel Corporation, Order Number:
 *	290488-004, December 1995
 */

#define PCI_I82426EX_PIRQ_ROUTE_CONTROL	0x66u

static int pirq_ib_get(struct pci_dev *router, struct pci_dev *dev, int pirq)
{
	int reg;
	u8 x;

	reg = pirq;
	if (reg >= 1 && reg <= 2)
		reg += PCI_I82426EX_PIRQ_ROUTE_CONTROL - 1;

	pci_read_config_byte(router, reg, &x);
	return (x < 16) ? x : 0;
}

static int pirq_ib_set(struct pci_dev *router, struct pci_dev *dev, int pirq,
		       int irq)
{
	int reg;

	reg = pirq;
	if (reg >= 1 && reg <= 2)
		reg += PCI_I82426EX_PIRQ_ROUTE_CONTROL - 1;

	pci_write_config_byte(router, reg, irq);
	return 1;
}

/*
 * The VIA pirq rules are nibble-based, like ALI,
 * but without the ugly irq number munging.
 * However, PIRQD is in the upper instead of lower 4 bits.
 */
static int pirq_via_get(struct pci_dev *router, struct pci_dev *dev, int pirq)
{
	return read_config_nybble(router, 0x55, pirq == 4 ? 5 : pirq);
}

static int pirq_via_set(struct pci_dev *router, struct pci_dev *dev, int pirq, int irq)
{
	write_config_nybble(router, 0x55, pirq == 4 ? 5 : pirq, irq);
	return 1;
}

/*
 * The VIA pirq rules are nibble-based, like ALI,
 * but without the ugly irq number munging.
 * However, for 82C586, nibble map is different .
 */
static int pirq_via586_get(struct pci_dev *router, struct pci_dev *dev, int pirq)
{
	static const unsigned int pirqmap[5] = { 3, 2, 5, 1, 1 };

	WARN_ON_ONCE(pirq > 5);
	return read_config_nybble(router, 0x55, pirqmap[pirq-1]);
}

static int pirq_via586_set(struct pci_dev *router, struct pci_dev *dev, int pirq, int irq)
{
	static const unsigned int pirqmap[5] = { 3, 2, 5, 1, 1 };

	WARN_ON_ONCE(pirq > 5);
	write_config_nybble(router, 0x55, pirqmap[pirq-1], irq);
	return 1;
}

/*
 * ITE 8330G pirq rules are nibble-based
 * FIXME: pirqmap may be { 1, 0, 3, 2 },
 * 	  2+3 are both mapped to irq 9 on my system
 */
static int pirq_ite_get(struct pci_dev *router, struct pci_dev *dev, int pirq)
{
	static const unsigned char pirqmap[4] = { 1, 0, 2, 3 };

	WARN_ON_ONCE(pirq > 4);
	return read_config_nybble(router, 0x43, pirqmap[pirq-1]);
}

static int pirq_ite_set(struct pci_dev *router, struct pci_dev *dev, int pirq, int irq)
{
	static const unsigned char pirqmap[4] = { 1, 0, 2, 3 };

	WARN_ON_ONCE(pirq > 4);
	write_config_nybble(router, 0x43, pirqmap[pirq-1], irq);
	return 1;
}

/*
 * OPTI: high four bits are nibble pointer..
 * I wonder what the low bits do?
 */
static int pirq_opti_get(struct pci_dev *router, struct pci_dev *dev, int pirq)
{
	return read_config_nybble(router, 0xb8, pirq >> 4);
}

static int pirq_opti_set(struct pci_dev *router, struct pci_dev *dev, int pirq, int irq)
{
	write_config_nybble(router, 0xb8, pirq >> 4, irq);
	return 1;
}

/*
 * Cyrix: nibble offset 0x5C
 * 0x5C bits 7:4 is INTB bits 3:0 is INTA
 * 0x5D bits 7:4 is INTD bits 3:0 is INTC
 */
static int pirq_cyrix_get(struct pci_dev *router, struct pci_dev *dev, int pirq)
{
	return read_config_nybble(router, 0x5C, (pirq-1)^1);
}

static int pirq_cyrix_set(struct pci_dev *router, struct pci_dev *dev, int pirq, int irq)
{
	write_config_nybble(router, 0x5C, (pirq-1)^1, irq);
	return 1;
}


/*
 *	PIRQ routing for the SiS85C497 AT Bus Controller & Megacell (ATM)
 *	ISA bridge used with the SiS 85C496/497 486 Green PC VESA/ISA/PCI
 *	Chipset.
 *
 *	There are four PCI INTx#-to-IRQ Link registers provided in the
 *	SiS85C497 part of the peculiar combined 85C496/497 configuration
 *	space decoded by the SiS85C496 PCI & CPU Memory Controller (PCM)
 *	host bridge, at 0xc0/0xc1/0xc2/0xc3 respectively for the PCI INT
 *	A/B/C/D lines.  Bit 7 enables the respective link if set and bits
 *	3:0 select the 8259A IRQ line as follows:
 *
 *	0000 : Reserved
 *	0001 : Reserved
 *	0010 : Reserved
 *	0011 : IRQ3
 *	0100 : IRQ4
 *	0101 : IRQ5
 *	0110 : IRQ6
 *	0111 : IRQ7
 *	1000 : Reserved
 *	1001 : IRQ9
 *	1010 : IRQ10
 *	1011 : IRQ11
 *	1100 : IRQ12
 *	1101 : Reserved
 *	1110 : IRQ14
 *	1111 : IRQ15
 *
 *	We avoid using a reserved value for disabled links, hence the
 *	choice of IRQ15 for that case.
 *
 *	References:
 *
 *	"486 Green PC VESA/ISA/PCI Chipset, SiS 85C496/497", Rev 3.0,
 *	Silicon Integrated Systems Corp., July 1995
 */

#define PCI_SIS497_INTA_TO_IRQ_LINK	0xc0u

#define PIRQ_SIS497_IRQ_MASK		0x0fu
#define PIRQ_SIS497_IRQ_ENABLE		0x80u

static int pirq_sis497_get(struct pci_dev *router, struct pci_dev *dev,
			   int pirq)
{
	int reg;
	u8 x;

	reg = pirq;
	if (reg >= 1 && reg <= 4)
		reg += PCI_SIS497_INTA_TO_IRQ_LINK - 1;

	pci_read_config_byte(router, reg, &x);
	return (x & PIRQ_SIS497_IRQ_ENABLE) ? (x & PIRQ_SIS497_IRQ_MASK) : 0;
}

static int pirq_sis497_set(struct pci_dev *router, struct pci_dev *dev,
			   int pirq, int irq)
{
	int reg;
	u8 x;

	reg = pirq;
	if (reg >= 1 && reg <= 4)
		reg += PCI_SIS497_INTA_TO_IRQ_LINK - 1;

	pci_read_config_byte(router, reg, &x);
	x &= ~(PIRQ_SIS497_IRQ_MASK | PIRQ_SIS497_IRQ_ENABLE);
	x |= irq ? (PIRQ_SIS497_IRQ_ENABLE | irq) : PIRQ_SIS497_IRQ_MASK;
	pci_write_config_byte(router, reg, x);
	return 1;
}

/*
 *	PIRQ routing for SiS 85C503 router used in several SiS chipsets.
 *	We have to deal with the following issues here:
 *	- vendors have different ideas about the meaning of link values
 *	- some onboard devices (integrated in the chipset) have special
 *	  links and are thus routed differently (i.e. not via PCI INTA-INTD)
 *	- different revision of the router have a different layout for
 *	  the routing registers, particularly for the onchip devices
 *
 *	For all routing registers the common thing is we have one byte
 *	per routeable link which is defined as:
 *		 bit 7      IRQ mapping enabled (0) or disabled (1)
 *		 bits [6:4] reserved (sometimes used for onchip devices)
 *		 bits [3:0] IRQ to map to
 *		     allowed: 3-7, 9-12, 14-15
 *		     reserved: 0, 1, 2, 8, 13
 *
 *	The config-space registers located at 0x41/0x42/0x43/0x44 are
 *	always used to route the normal PCI INT A/B/C/D respectively.
 *	Apparently there are systems implementing PCI routing table using
 *	link values 0x01-0x04 and others using 0x41-0x44 for PCI INTA..D.
 *	We try our best to handle both link mappings.
 *
 *	Currently (2003-05-21) it appears most SiS chipsets follow the
 *	definition of routing registers from the SiS-5595 southbridge.
 *	According to the SiS 5595 datasheets the revision id's of the
 *	router (ISA-bridge) should be 0x01 or 0xb0.
 *
 *	Furthermore we've also seen lspci dumps with revision 0x00 and 0xb1.
 *	Looks like these are used in a number of SiS 5xx/6xx/7xx chipsets.
 *	They seem to work with the current routing code. However there is
 *	some concern because of the two USB-OHCI HCs (original SiS 5595
 *	had only one). YMMV.
 *
 *	Onchip routing for router rev-id 0x01/0xb0 and probably 0x00/0xb1:
 *
 *	0x61:	IDEIRQ:
 *		bits [6:5] must be written 01
 *		bit 4 channel-select primary (0), secondary (1)
 *
 *	0x62:	USBIRQ:
 *		bit 6 OHCI function disabled (0), enabled (1)
 *
 *	0x6a:	ACPI/SCI IRQ: bits 4-6 reserved
 *
 *	0x7e:	Data Acq. Module IRQ - bits 4-6 reserved
 *
 *	We support USBIRQ (in addition to INTA-INTD) and keep the
 *	IDE, ACPI and DAQ routing untouched as set by the BIOS.
 *
 *	Currently the only reported exception is the new SiS 65x chipset
 *	which includes the SiS 69x southbridge. Here we have the 85C503
 *	router revision 0x04 and there are changes in the register layout
 *	mostly related to the different USB HCs with USB 2.0 support.
 *
 *	Onchip routing for router rev-id 0x04 (try-and-error observation)
 *
 *	0x60/0x61/0x62/0x63:	1xEHCI and 3xOHCI (companion) USB-HCs
 *				bit 6-4 are probably unused, not like 5595
 */

#define PIRQ_SIS503_IRQ_MASK	0x0f
#define PIRQ_SIS503_IRQ_DISABLE	0x80
#define PIRQ_SIS503_USB_ENABLE	0x40

static int pirq_sis503_get(struct pci_dev *router, struct pci_dev *dev,
			   int pirq)
{
	u8 x;
	int reg;

	reg = pirq;
	if (reg >= 0x01 && reg <= 0x04)
		reg += 0x40;
	pci_read_config_byte(router, reg, &x);
	return (x & PIRQ_SIS503_IRQ_DISABLE) ? 0 : (x & PIRQ_SIS503_IRQ_MASK);
}

static int pirq_sis503_set(struct pci_dev *router, struct pci_dev *dev,
			   int pirq, int irq)
{
	u8 x;
	int reg;

	reg = pirq;
	if (reg >= 0x01 && reg <= 0x04)
		reg += 0x40;
	pci_read_config_byte(router, reg, &x);
	x &= ~(PIRQ_SIS503_IRQ_MASK | PIRQ_SIS503_IRQ_DISABLE);
	x |= irq ? irq : PIRQ_SIS503_IRQ_DISABLE;
	pci_write_config_byte(router, reg, x);
	return 1;
}


/*
 * VLSI: nibble offset 0x74 - educated guess due to routing table and
 *       config space of VLSI 82C534 PCI-bridge/router (1004:0102)
 *       Tested on HP OmniBook 800 covering PIRQ 1, 2, 4, 8 for onboard
 *       devices, PIRQ 3 for non-pci(!) soundchip and (untested) PIRQ 6
 *       for the busbridge to the docking station.
 */

static int pirq_vlsi_get(struct pci_dev *router, struct pci_dev *dev, int pirq)
{
	WARN_ON_ONCE(pirq >= 9);
	if (pirq > 8) {
		dev_info(&dev->dev, "VLSI router PIRQ escape (%d)\n", pirq);
		return 0;
	}
	return read_config_nybble(router, 0x74, pirq-1);
}

static int pirq_vlsi_set(struct pci_dev *router, struct pci_dev *dev, int pirq, int irq)
{
	WARN_ON_ONCE(pirq >= 9);
	if (pirq > 8) {
		dev_info(&dev->dev, "VLSI router PIRQ escape (%d)\n", pirq);
		return 0;
	}
	write_config_nybble(router, 0x74, pirq-1, irq);
	return 1;
}

/*
 * ServerWorks: PCI interrupts mapped to system IRQ lines through Index
 * and Redirect I/O registers (0x0c00 and 0x0c01).  The Index register
 * format is (PCIIRQ## | 0x10), e.g.: PCIIRQ10=0x1a.  The Redirect
 * register is a straight binary coding of desired PIC IRQ (low nibble).
 *
 * The 'link' value in the PIRQ table is already in the correct format
 * for the Index register.  There are some special index values:
 * 0x00 for ACPI (SCI), 0x01 for USB, 0x02 for IDE0, 0x04 for IDE1,
 * and 0x03 for SMBus.
 */
static int pirq_serverworks_get(struct pci_dev *router, struct pci_dev *dev, int pirq)
{
	outb(pirq, 0xc00);
	return inb(0xc01) & 0xf;
}

static int pirq_serverworks_set(struct pci_dev *router, struct pci_dev *dev,
	int pirq, int irq)
{
	outb(pirq, 0xc00);
	outb(irq, 0xc01);
	return 1;
}

/* Support for AMD756 PCI IRQ Routing
 * Jhon H. Caicedo <jhcaiced@osso.org.co>
 * Jun/21/2001 0.2.0 Release, fixed to use "nybble" functions... (jhcaiced)
 * Jun/19/2001 Alpha Release 0.1.0 (jhcaiced)
 * The AMD756 pirq rules are nibble-based
 * offset 0x56 0-3 PIRQA  4-7  PIRQB
 * offset 0x57 0-3 PIRQC  4-7  PIRQD
 */
static int pirq_amd756_get(struct pci_dev *router, struct pci_dev *dev, int pirq)
{
	u8 irq;
	irq = 0;
	if (pirq <= 4)
		irq = read_config_nybble(router, 0x56, pirq - 1);
	dev_info(&dev->dev,
		 "AMD756: dev [%04x:%04x], router PIRQ %d get IRQ %d\n",
		 dev->vendor, dev->device, pirq, irq);
	return irq;
}

static int pirq_amd756_set(struct pci_dev *router, struct pci_dev *dev, int pirq, int irq)
{
	dev_info(&dev->dev,
		 "AMD756: dev [%04x:%04x], router PIRQ %d set IRQ %d\n",
		 dev->vendor, dev->device, pirq, irq);
	if (pirq <= 4)
		write_config_nybble(router, 0x56, pirq - 1, irq);
	return 1;
}

/*
 * PicoPower PT86C523
 */
static int pirq_pico_get(struct pci_dev *router, struct pci_dev *dev, int pirq)
{
	outb(0x10 + ((pirq - 1) >> 1), 0x24);
	return ((pirq - 1) & 1) ? (inb(0x26) >> 4) : (inb(0x26) & 0xf);
}

static int pirq_pico_set(struct pci_dev *router, struct pci_dev *dev, int pirq,
			int irq)
{
	unsigned int x;
	outb(0x10 + ((pirq - 1) >> 1), 0x24);
	x = inb(0x26);
	x = ((pirq - 1) & 1) ? ((x & 0x0f) | (irq << 4)) : ((x & 0xf0) | (irq));
	outb(x, 0x26);
	return 1;
}

#ifdef CONFIG_PCI_BIOS

static int pirq_bios_set(struct pci_dev *router, struct pci_dev *dev, int pirq, int irq)
{
	struct pci_dev *bridge;
	int pin = pci_get_interrupt_pin(dev, &bridge);
	return pcibios_set_irq_routing(bridge, pin - 1, irq);
}

#endif

static __init int intel_router_probe(struct irq_router *r, struct pci_dev *router, u16 device)
{
	static struct pci_device_id __initdata pirq_440gx[] = {
		{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82443GX_0) },
		{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82443GX_2) },
		{ },
	};

	/* 440GX has a proprietary PIRQ router -- don't use it */
	if (pci_dev_present(pirq_440gx))
		return 0;

	switch (device) {
	case PCI_DEVICE_ID_INTEL_82375:
		r->name = "PCEB/ESC";
		r->get = pirq_esc_get;
		r->set = pirq_esc_set;
		return 1;
	case PCI_DEVICE_ID_INTEL_82371FB_0:
	case PCI_DEVICE_ID_INTEL_82371SB_0:
	case PCI_DEVICE_ID_INTEL_82371AB_0:
	case PCI_DEVICE_ID_INTEL_82371MX:
	case PCI_DEVICE_ID_INTEL_82443MX_0:
	case PCI_DEVICE_ID_INTEL_82801AA_0:
	case PCI_DEVICE_ID_INTEL_82801AB_0:
	case PCI_DEVICE_ID_INTEL_82801BA_0:
	case PCI_DEVICE_ID_INTEL_82801BA_10:
	case PCI_DEVICE_ID_INTEL_82801CA_0:
	case PCI_DEVICE_ID_INTEL_82801CA_12:
	case PCI_DEVICE_ID_INTEL_82801DB_0:
	case PCI_DEVICE_ID_INTEL_82801E_0:
	case PCI_DEVICE_ID_INTEL_82801EB_0:
	case PCI_DEVICE_ID_INTEL_ESB_1:
	case PCI_DEVICE_ID_INTEL_ICH6_0:
	case PCI_DEVICE_ID_INTEL_ICH6_1:
	case PCI_DEVICE_ID_INTEL_ICH7_0:
	case PCI_DEVICE_ID_INTEL_ICH7_1:
	case PCI_DEVICE_ID_INTEL_ICH7_30:
	case PCI_DEVICE_ID_INTEL_ICH7_31:
	case PCI_DEVICE_ID_INTEL_TGP_LPC:
	case PCI_DEVICE_ID_INTEL_ESB2_0:
	case PCI_DEVICE_ID_INTEL_ICH8_0:
	case PCI_DEVICE_ID_INTEL_ICH8_1:
	case PCI_DEVICE_ID_INTEL_ICH8_2:
	case PCI_DEVICE_ID_INTEL_ICH8_3:
	case PCI_DEVICE_ID_INTEL_ICH8_4:
	case PCI_DEVICE_ID_INTEL_ICH9_0:
	case PCI_DEVICE_ID_INTEL_ICH9_1:
	case PCI_DEVICE_ID_INTEL_ICH9_2:
	case PCI_DEVICE_ID_INTEL_ICH9_3:
	case PCI_DEVICE_ID_INTEL_ICH9_4:
	case PCI_DEVICE_ID_INTEL_ICH9_5:
	case PCI_DEVICE_ID_INTEL_EP80579_0:
	case PCI_DEVICE_ID_INTEL_ICH10_0:
	case PCI_DEVICE_ID_INTEL_ICH10_1:
	case PCI_DEVICE_ID_INTEL_ICH10_2:
	case PCI_DEVICE_ID_INTEL_ICH10_3:
	case PCI_DEVICE_ID_INTEL_PATSBURG_LPC_0:
	case PCI_DEVICE_ID_INTEL_PATSBURG_LPC_1:
		r->name = "PIIX/ICH";
		r->get = pirq_piix_get;
		r->set = pirq_piix_set;
		return 1;
	case PCI_DEVICE_ID_INTEL_82425:
		r->name = "PSC/IB";
		r->get = pirq_ib_get;
		r->set = pirq_ib_set;
		return 1;
	}

	if ((device >= PCI_DEVICE_ID_INTEL_5_3400_SERIES_LPC_MIN && 
	     device <= PCI_DEVICE_ID_INTEL_5_3400_SERIES_LPC_MAX) 
	||  (device >= PCI_DEVICE_ID_INTEL_COUGARPOINT_LPC_MIN && 
	     device <= PCI_DEVICE_ID_INTEL_COUGARPOINT_LPC_MAX)
	||  (device >= PCI_DEVICE_ID_INTEL_DH89XXCC_LPC_MIN &&
	     device <= PCI_DEVICE_ID_INTEL_DH89XXCC_LPC_MAX)
	||  (device >= PCI_DEVICE_ID_INTEL_PANTHERPOINT_LPC_MIN &&
	     device <= PCI_DEVICE_ID_INTEL_PANTHERPOINT_LPC_MAX)) {
		r->name = "PIIX/ICH";
		r->get = pirq_piix_get;
		r->set = pirq_piix_set;
		return 1;
	}

	return 0;
}

static __init int via_router_probe(struct irq_router *r,
				struct pci_dev *router, u16 device)
{
	/* FIXME: We should move some of the quirk fixup stuff here */

	/*
	 * workarounds for some buggy BIOSes
	 */
	if (device == PCI_DEVICE_ID_VIA_82C586_0) {
		switch (router->device) {
		case PCI_DEVICE_ID_VIA_82C686:
			/*
			 * Asus k7m bios wrongly reports 82C686A
			 * as 586-compatible
			 */
			device = PCI_DEVICE_ID_VIA_82C686;
			break;
		case PCI_DEVICE_ID_VIA_8235:
			/**
			 * Asus a7v-x bios wrongly reports 8235
			 * as 586-compatible
			 */
			device = PCI_DEVICE_ID_VIA_8235;
			break;
		case PCI_DEVICE_ID_VIA_8237:
			/**
			 * Asus a7v600 bios wrongly reports 8237
			 * as 586-compatible
			 */
			device = PCI_DEVICE_ID_VIA_8237;
			break;
		}
	}

	switch (device) {
	case PCI_DEVICE_ID_VIA_82C586_0:
		r->name = "VIA";
		r->get = pirq_via586_get;
		r->set = pirq_via586_set;
		return 1;
	case PCI_DEVICE_ID_VIA_82C596:
	case PCI_DEVICE_ID_VIA_82C686:
	case PCI_DEVICE_ID_VIA_8231:
	case PCI_DEVICE_ID_VIA_8233A:
	case PCI_DEVICE_ID_VIA_8235:
	case PCI_DEVICE_ID_VIA_8237:
		/* FIXME: add new ones for 8233/5 */
		r->name = "VIA";
		r->get = pirq_via_get;
		r->set = pirq_via_set;
		return 1;
	}
	return 0;
}

static __init int vlsi_router_probe(struct irq_router *r, struct pci_dev *router, u16 device)
{
	switch (device) {
	case PCI_DEVICE_ID_VLSI_82C534:
		r->name = "VLSI 82C534";
		r->get = pirq_vlsi_get;
		r->set = pirq_vlsi_set;
		return 1;
	}
	return 0;
}


static __init int serverworks_router_probe(struct irq_router *r,
		struct pci_dev *router, u16 device)
{
	switch (device) {
	case PCI_DEVICE_ID_SERVERWORKS_OSB4:
	case PCI_DEVICE_ID_SERVERWORKS_CSB5:
		r->name = "ServerWorks";
		r->get = pirq_serverworks_get;
		r->set = pirq_serverworks_set;
		return 1;
	}
	return 0;
}

static __init int sis_router_probe(struct irq_router *r, struct pci_dev *router, u16 device)
{
	switch (device) {
	case PCI_DEVICE_ID_SI_496:
		r->name = "SiS85C497";
		r->get = pirq_sis497_get;
		r->set = pirq_sis497_set;
		return 1;
	case PCI_DEVICE_ID_SI_503:
		r->name = "SiS85C503";
		r->get = pirq_sis503_get;
		r->set = pirq_sis503_set;
		return 1;
	}
	return 0;
}

static __init int cyrix_router_probe(struct irq_router *r, struct pci_dev *router, u16 device)
{
	switch (device) {
	case PCI_DEVICE_ID_CYRIX_5520:
		r->name = "NatSemi";
		r->get = pirq_cyrix_get;
		r->set = pirq_cyrix_set;
		return 1;
	}
	return 0;
}

static __init int opti_router_probe(struct irq_router *r, struct pci_dev *router, u16 device)
{
	switch (device) {
	case PCI_DEVICE_ID_OPTI_82C700:
		r->name = "OPTI";
		r->get = pirq_opti_get;
		r->set = pirq_opti_set;
		return 1;
	}
	return 0;
}

static __init int ite_router_probe(struct irq_router *r, struct pci_dev *router, u16 device)
{
	switch (device) {
	case PCI_DEVICE_ID_ITE_IT8330G_0:
		r->name = "ITE";
		r->get = pirq_ite_get;
		r->set = pirq_ite_set;
		return 1;
	}
	return 0;
}

static __init int ali_router_probe(struct irq_router *r, struct pci_dev *router, u16 device)
{
	switch (device) {
	case PCI_DEVICE_ID_AL_M1489:
		r->name = "FinALi";
		r->get = pirq_finali_get;
		r->set = pirq_finali_set;
		r->lvl = pirq_finali_lvl;
		return 1;
	case PCI_DEVICE_ID_AL_M1533:
	case PCI_DEVICE_ID_AL_M1563:
		r->name = "ALI";
		r->get = pirq_ali_get;
		r->set = pirq_ali_set;
		return 1;
	}
	return 0;
}

static __init int amd_router_probe(struct irq_router *r, struct pci_dev *router, u16 device)
{
	switch (device) {
	case PCI_DEVICE_ID_AMD_VIPER_740B:
		r->name = "AMD756";
		break;
	case PCI_DEVICE_ID_AMD_VIPER_7413:
		r->name = "AMD766";
		break;
	case PCI_DEVICE_ID_AMD_VIPER_7443:
		r->name = "AMD768";
		break;
	default:
		return 0;
	}
	r->get = pirq_amd756_get;
	r->set = pirq_amd756_set;
	return 1;
}

static __init int pico_router_probe(struct irq_router *r, struct pci_dev *router, u16 device)
{
	switch (device) {
	case PCI_DEVICE_ID_PICOPOWER_PT86C523:
		r->name = "PicoPower PT86C523";
		r->get = pirq_pico_get;
		r->set = pirq_pico_set;
		return 1;

	case PCI_DEVICE_ID_PICOPOWER_PT86C523BBP:
		r->name = "PicoPower PT86C523 rev. BB+";
		r->get = pirq_pico_get;
		r->set = pirq_pico_set;
		return 1;
	}
	return 0;
}

static __initdata struct irq_router_handler pirq_routers[] = {
	{ PCI_VENDOR_ID_INTEL, intel_router_probe },
	{ PCI_VENDOR_ID_AL, ali_router_probe },
	{ PCI_VENDOR_ID_ITE, ite_router_probe },
	{ PCI_VENDOR_ID_VIA, via_router_probe },
	{ PCI_VENDOR_ID_OPTI, opti_router_probe },
	{ PCI_VENDOR_ID_SI, sis_router_probe },
	{ PCI_VENDOR_ID_CYRIX, cyrix_router_probe },
	{ PCI_VENDOR_ID_VLSI, vlsi_router_probe },
	{ PCI_VENDOR_ID_SERVERWORKS, serverworks_router_probe },
	{ PCI_VENDOR_ID_AMD, amd_router_probe },
	{ PCI_VENDOR_ID_PICOPOWER, pico_router_probe },
	/* Someone with docs needs to add the ATI Radeon IGP */
	{ 0, NULL }
};
static struct irq_router pirq_router;
static struct pci_dev *pirq_router_dev;


/*
 *	FIXME: should we have an option to say "generic for
 *	chipset" ?
 */

static bool __init pirq_try_router(struct irq_router *r,
				   struct irq_routing_table *rt,
				   struct pci_dev *dev)
{
	struct irq_router_handler *h;

	DBG(KERN_DEBUG "PCI: Trying IRQ router for [%04x:%04x]\n",
	    dev->vendor, dev->device);

	for (h = pirq_routers; h->vendor; h++) {
		/* First look for a router match */
		if (rt->rtr_vendor == h->vendor &&
		    h->probe(r, dev, rt->rtr_device))
			return true;
		/* Fall back to a device match */
		if (dev->vendor == h->vendor &&
		    h->probe(r, dev, dev->device))
			return true;
	}
	return false;
}

static void __init pirq_find_router(struct irq_router *r)
{
	struct irq_routing_table *rt = pirq_table;
	struct pci_dev *dev;

#ifdef CONFIG_PCI_BIOS
	if (!rt->signature) {
		printk(KERN_INFO "PCI: Using BIOS for IRQ routing\n");
		r->set = pirq_bios_set;
		r->name = "BIOS";
		return;
	}
#endif

	/* Default unless a driver reloads it */
	r->name = "default";
	r->get = NULL;
	r->set = NULL;

	DBG(KERN_DEBUG "PCI: Attempting to find IRQ router for [%04x:%04x]\n",
	    rt->rtr_vendor, rt->rtr_device);

	/* Use any vendor:device provided by the routing table or try all.  */
	if (rt->rtr_vendor) {
		dev = pci_get_domain_bus_and_slot(0, rt->rtr_bus,
						  rt->rtr_devfn);
		if (dev && pirq_try_router(r, rt, dev))
			pirq_router_dev = dev;
	} else {
		dev = NULL;
		for_each_pci_dev(dev) {
			if (pirq_try_router(r, rt, dev)) {
				pirq_router_dev = dev;
				break;
			}
		}
	}

	if (pirq_router_dev)
		dev_info(&pirq_router_dev->dev, "%s IRQ router [%04x:%04x]\n",
			 pirq_router.name,
			 pirq_router_dev->vendor, pirq_router_dev->device);
	else
		DBG(KERN_DEBUG "PCI: Interrupt router not found at "
		    "%02x:%02x\n", rt->rtr_bus, rt->rtr_devfn);

	/* The device remains referenced for the kernel lifetime */
}

/*
 * We're supposed to match on the PCI device only and not the function,
 * but some BIOSes build their tables with the PCI function included
 * for motherboard devices, so if a complete match is found, then give
 * it precedence over a slot match.
 */
static struct irq_info *pirq_get_dev_info(struct pci_dev *dev)
{
	struct irq_routing_table *rt = pirq_table;
	int entries = (rt->size - sizeof(struct irq_routing_table)) /
		sizeof(struct irq_info);
	struct irq_info *slotinfo = NULL;
	struct irq_info *info;

	for (info = rt->slots; entries--; info++)
		if (info->bus == dev->bus->number) {
			if (info->devfn == dev->devfn)
				return info;
			if (!slotinfo &&
			    PCI_SLOT(info->devfn) == PCI_SLOT(dev->devfn))
				slotinfo = info;
		}
	return slotinfo;
}

/*
 * Buses behind bridges are typically not listed in the PIRQ routing table.
 * Do the usual dance then and walk the tree of bridges up adjusting the
 * pin number accordingly on the way until the originating root bus device
 * has been reached and then use its routing information.
 */
static struct irq_info *pirq_get_info(struct pci_dev *dev, u8 *pin)
{
	struct pci_dev *temp_dev = dev;
	struct irq_info *info;
	u8 temp_pin = *pin;
	u8 dpin = temp_pin;

	info = pirq_get_dev_info(dev);
	while (!info && temp_dev->bus->parent) {
		struct pci_dev *bridge = temp_dev->bus->self;

		temp_pin = pci_swizzle_interrupt_pin(temp_dev, temp_pin);
		info = pirq_get_dev_info(bridge);
		if (info)
			dev_warn(&dev->dev,
				 "using bridge %s INT %c to get INT %c\n",
				 pci_name(bridge),
				 'A' + temp_pin - 1, 'A' + dpin - 1);

		temp_dev = bridge;
	}
	*pin = temp_pin;
	return info;
}

static int pcibios_lookup_irq(struct pci_dev *dev, int assign)
{
	struct irq_info *info;
	int i, pirq, newirq;
	u8 dpin, pin;
	int irq = 0;
	u32 mask;
	struct irq_router *r = &pirq_router;
	struct pci_dev *dev2 = NULL;
	char *msg = NULL;

	/* Find IRQ pin */
	pci_read_config_byte(dev, PCI_INTERRUPT_PIN, &dpin);
	if (!dpin) {
		dev_dbg(&dev->dev, "no interrupt pin\n");
		return 0;
	}

	if (io_apic_assign_pci_irqs)
		return 0;

	/* Find IRQ routing entry */

	if (!pirq_table)
		return 0;

	pin = dpin;
	info = pirq_get_info(dev, &pin);
	if (!info) {
		dev_dbg(&dev->dev, "PCI INT %c not found in routing table\n",
			'A' + dpin - 1);
		return 0;
	}
	pirq = info->irq[pin - 1].link;
	mask = info->irq[pin - 1].bitmap;
	if (!pirq) {
		dev_dbg(&dev->dev, "PCI INT %c not routed\n", 'A' + dpin - 1);
		return 0;
	}
	dev_dbg(&dev->dev, "PCI INT %c -> PIRQ %02x, mask %04x, excl %04x",
		'A' + dpin - 1, pirq, mask, pirq_table->exclusive_irqs);
	mask &= pcibios_irq_mask;

	/* Work around broken HP Pavilion Notebooks which assign USB to
	   IRQ 9 even though it is actually wired to IRQ 11 */

	if (broken_hp_bios_irq9 && pirq == 0x59 && dev->irq == 9) {
		dev->irq = 11;
		pci_write_config_byte(dev, PCI_INTERRUPT_LINE, 11);
		r->set(pirq_router_dev, dev, pirq, 11);
	}

	/* same for Acer Travelmate 360, but with CB and irq 11 -> 10 */
	if (acer_tm360_irqrouting && dev->irq == 11 &&
		dev->vendor == PCI_VENDOR_ID_O2) {
		pirq = 0x68;
		mask = 0x400;
		dev->irq = r->get(pirq_router_dev, dev, pirq);
		pci_write_config_byte(dev, PCI_INTERRUPT_LINE, dev->irq);
	}

	/*
	 * Find the best IRQ to assign: use the one
	 * reported by the device if possible.
	 */
	newirq = dev->irq;
	if (newirq && !((1 << newirq) & mask)) {
		if (pci_probe & PCI_USE_PIRQ_MASK)
			newirq = 0;
		else
			dev_warn(&dev->dev, "IRQ %d doesn't match PIRQ mask "
				 "%#x; try pci=usepirqmask\n", newirq, mask);
	}
	if (!newirq && assign) {
		for (i = 0; i < 16; i++) {
			if (!(mask & (1 << i)))
				continue;
			if (pirq_penalty[i] < pirq_penalty[newirq] &&
				can_request_irq(i, IRQF_SHARED))
				newirq = i;
		}
	}
	dev_dbg(&dev->dev, "PCI INT %c -> newirq %d", 'A' + dpin - 1, newirq);

	/* Check if it is hardcoded */
	if ((pirq & 0xf0) == 0xf0) {
		irq = pirq & 0xf;
		msg = "hardcoded";
	} else if (r->get && (irq = r->get(pirq_router_dev, dev, pirq)) && \
	((!(pci_probe & PCI_USE_PIRQ_MASK)) || ((1 << irq) & mask))) {
		msg = "found";
		if (r->lvl)
			r->lvl(pirq_router_dev, dev, pirq, irq);
		else
			elcr_set_level_irq(irq);
	} else if (newirq && r->set &&
		(dev->class >> 8) != PCI_CLASS_DISPLAY_VGA) {
		if (r->set(pirq_router_dev, dev, pirq, newirq)) {
			if (r->lvl)
				r->lvl(pirq_router_dev, dev, pirq, newirq);
			else
				elcr_set_level_irq(newirq);
			msg = "assigned";
			irq = newirq;
		}
	}

	if (!irq) {
		if (newirq && mask == (1 << newirq)) {
			msg = "guessed";
			irq = newirq;
		} else {
			dev_dbg(&dev->dev, "can't route interrupt\n");
			return 0;
		}
	}
	dev_info(&dev->dev, "%s PCI INT %c -> IRQ %d\n",
		 msg, 'A' + dpin - 1, irq);

	/* Update IRQ for all devices with the same pirq value */
	for_each_pci_dev(dev2) {
		pci_read_config_byte(dev2, PCI_INTERRUPT_PIN, &dpin);
		if (!dpin)
			continue;

		pin = dpin;
		info = pirq_get_info(dev2, &pin);
		if (!info)
			continue;
		if (info->irq[pin - 1].link == pirq) {
			/*
			 * We refuse to override the dev->irq
			 * information. Give a warning!
			 */
			if (dev2->irq && dev2->irq != irq && \
			(!(pci_probe & PCI_USE_PIRQ_MASK) || \
			((1 << dev2->irq) & mask))) {
#ifndef CONFIG_PCI_MSI
				dev_info(&dev2->dev, "IRQ routing conflict: "
					 "have IRQ %d, want IRQ %d\n",
					 dev2->irq, irq);
#endif
				continue;
			}
			dev2->irq = irq;
			pirq_penalty[irq]++;
			if (dev != dev2)
				dev_info(&dev->dev, "sharing IRQ %d with %s\n",
					 irq, pci_name(dev2));
		}
	}
	return 1;
}

void __init pcibios_fixup_irqs(void)
{
	struct pci_dev *dev = NULL;
	u8 pin;

	DBG(KERN_DEBUG "PCI: IRQ fixup\n");
	for_each_pci_dev(dev) {
		/*
		 * If the BIOS has set an out of range IRQ number, just
		 * ignore it.  Also keep track of which IRQ's are
		 * already in use.
		 */
		if (dev->irq >= 16) {
			dev_dbg(&dev->dev, "ignoring bogus IRQ %d\n", dev->irq);
			dev->irq = 0;
		}
		/*
		 * If the IRQ is already assigned to a PCI device,
		 * ignore its ISA use penalty
		 */
		if (pirq_penalty[dev->irq] >= 100 &&
				pirq_penalty[dev->irq] < 100000)
			pirq_penalty[dev->irq] = 0;
		pirq_penalty[dev->irq]++;
	}

	if (io_apic_assign_pci_irqs)
		return;

	dev = NULL;
	for_each_pci_dev(dev) {
		pci_read_config_byte(dev, PCI_INTERRUPT_PIN, &pin);
		if (!pin)
			continue;

		/*
		 * Still no IRQ? Try to lookup one...
		 */
		if (!dev->irq)
			pcibios_lookup_irq(dev, 0);
	}
}

/*
 * Work around broken HP Pavilion Notebooks which assign USB to
 * IRQ 9 even though it is actually wired to IRQ 11
 */
static int __init fix_broken_hp_bios_irq9(const struct dmi_system_id *d)
{
	if (!broken_hp_bios_irq9) {
		broken_hp_bios_irq9 = 1;
		printk(KERN_INFO "%s detected - fixing broken IRQ routing\n",
			d->ident);
	}
	return 0;
}

/*
 * Work around broken Acer TravelMate 360 Notebooks which assign
 * Cardbus to IRQ 11 even though it is actually wired to IRQ 10
 */
static int __init fix_acer_tm360_irqrouting(const struct dmi_system_id *d)
{
	if (!acer_tm360_irqrouting) {
		acer_tm360_irqrouting = 1;
		printk(KERN_INFO "%s detected - fixing broken IRQ routing\n",
			d->ident);
	}
	return 0;
}

static const struct dmi_system_id pciirq_dmi_table[] __initconst = {
	{
		.callback = fix_broken_hp_bios_irq9,
		.ident = "HP Pavilion N5400 Series Laptop",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Hewlett-Packard"),
			DMI_MATCH(DMI_BIOS_VERSION, "GE.M1.03"),
			DMI_MATCH(DMI_PRODUCT_VERSION,
				"HP Pavilion Notebook Model GE"),
			DMI_MATCH(DMI_BOARD_VERSION, "OmniBook N32N-736"),
		},
	},
	{
		.callback = fix_acer_tm360_irqrouting,
		.ident = "Acer TravelMate 36x Laptop",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "TravelMate 360"),
		},
	},
	{ }
};

void __init pcibios_irq_init(void)
{
	struct irq_routing_table *rtable = NULL;

	DBG(KERN_DEBUG "PCI: IRQ init\n");

	if (raw_pci_ops == NULL)
		return;

	dmi_check_system(pciirq_dmi_table);

	pirq_table = pirq_find_routing_table();

#ifdef CONFIG_PCI_BIOS
	if (!pirq_table && (pci_probe & PCI_BIOS_IRQ_SCAN)) {
		pirq_table = pcibios_get_irq_routing_table();
		rtable = pirq_table;
	}
#endif
	if (pirq_table) {
		pirq_peer_trick();
		pirq_find_router(&pirq_router);
		if (pirq_table->exclusive_irqs) {
			int i;
			for (i = 0; i < 16; i++)
				if (!(pirq_table->exclusive_irqs & (1 << i)))
					pirq_penalty[i] += 100;
		}
		/*
		 * If we're using the I/O APIC, avoid using the PCI IRQ
		 * routing table
		 */
		if (io_apic_assign_pci_irqs) {
			kfree(rtable);
			pirq_table = NULL;
		}
	}

	x86_init.pci.fixup_irqs();

	if (io_apic_assign_pci_irqs && pci_routeirq) {
		struct pci_dev *dev = NULL;
		/*
		 * PCI IRQ routing is set up by pci_enable_device(), but we
		 * also do it here in case there are still broken drivers that
		 * don't use pci_enable_device().
		 */
		printk(KERN_INFO "PCI: Routing PCI interrupts for all devices because \"pci=routeirq\" specified\n");
		for_each_pci_dev(dev)
			pirq_enable_irq(dev);
	}
}

static void pirq_penalize_isa_irq(int irq, int active)
{
	/*
	 *  If any ISAPnP device reports an IRQ in its list of possible
	 *  IRQ's, we try to avoid assigning it to PCI devices.
	 */
	if (irq < 16) {
		if (active)
			pirq_penalty[irq] += 1000;
		else
			pirq_penalty[irq] += 100;
	}
}

void pcibios_penalize_isa_irq(int irq, int active)
{
#ifdef CONFIG_ACPI
	if (!acpi_noirq)
		acpi_penalize_isa_irq(irq, active);
	else
#endif
		pirq_penalize_isa_irq(irq, active);
}

static int pirq_enable_irq(struct pci_dev *dev)
{
	u8 pin = 0;

	pci_read_config_byte(dev, PCI_INTERRUPT_PIN, &pin);
	if (pin && !pcibios_lookup_irq(dev, 1)) {
		char *msg = "";

		if (!io_apic_assign_pci_irqs && dev->irq)
			return 0;

		if (io_apic_assign_pci_irqs) {
#ifdef CONFIG_X86_IO_APIC
			struct pci_dev *temp_dev;
			int irq;

			if (dev->irq_managed && dev->irq > 0)
				return 0;

			irq = IO_APIC_get_PCI_irq_vector(dev->bus->number,
						PCI_SLOT(dev->devfn), pin - 1);
			/*
			 * Busses behind bridges are typically not listed in the MP-table.
			 * In this case we have to look up the IRQ based on the parent bus,
			 * parent slot, and pin number. The SMP code detects such bridged
			 * busses itself so we should get into this branch reliably.
			 */
			temp_dev = dev;
			while (irq < 0 && dev->bus->parent) { /* go back to the bridge */
				struct pci_dev *bridge = dev->bus->self;

				pin = pci_swizzle_interrupt_pin(dev, pin);
				irq = IO_APIC_get_PCI_irq_vector(bridge->bus->number,
						PCI_SLOT(bridge->devfn),
						pin - 1);
				if (irq >= 0)
					dev_warn(&dev->dev, "using bridge %s "
						 "INT %c to get IRQ %d\n",
						 pci_name(bridge), 'A' + pin - 1,
						 irq);
				dev = bridge;
			}
			dev = temp_dev;
			if (irq >= 0) {
				dev->irq_managed = 1;
				dev->irq = irq;
				dev_info(&dev->dev, "PCI->APIC IRQ transform: "
					 "INT %c -> IRQ %d\n", 'A' + pin - 1, irq);
				return 0;
			} else
				msg = "; probably buggy MP table";
#endif
		} else if (pci_probe & PCI_BIOS_IRQ_SCAN)
			msg = "";
		else
			msg = "; please try using pci=biosirq";

		/*
		 * With IDE legacy devices the IRQ lookup failure is not
		 * a problem..
		 */
		if (dev->class >> 8 == PCI_CLASS_STORAGE_IDE &&
				!(dev->class & 0x5))
			return 0;

		dev_warn(&dev->dev, "can't find IRQ for PCI INT %c%s\n",
			 'A' + pin - 1, msg);
	}
	return 0;
}

bool mp_should_keep_irq(struct device *dev)
{
	if (dev->power.is_prepared)
		return true;
#ifdef CONFIG_PM
	if (dev->power.runtime_status == RPM_SUSPENDING)
		return true;
#endif

	return false;
}

static void pirq_disable_irq(struct pci_dev *dev)
{
	if (io_apic_assign_pci_irqs && !mp_should_keep_irq(&dev->dev) &&
	    dev->irq_managed && dev->irq) {
		mp_unmap_irq(dev->irq);
		dev->irq = 0;
		dev->irq_managed = 0;
	}
}
