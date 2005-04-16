/*
 * arch/ppc/platforms/pcore_setup.c
 *
 * Setup routines for Force PCORE boards
 *
 * Author: Matt Porter <mporter@mvista.com>
 *
 * 2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/config.h>
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/reboot.h>
#include <linux/pci.h>
#include <linux/kdev_t.h>
#include <linux/types.h>
#include <linux/major.h>
#include <linux/initrd.h>
#include <linux/console.h>
#include <linux/irq.h>
#include <linux/seq_file.h>
#include <linux/root_dev.h>

#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/time.h>
#include <asm/i8259.h>
#include <asm/mpc10x.h>
#include <asm/todc.h>
#include <asm/bootinfo.h>
#include <asm/kgdb.h>

#include "pcore.h"

extern unsigned long loops_per_jiffy;

static int board_type;

static inline int __init
pcore_6750_map_irq(struct pci_dev *dev, unsigned char idsel, unsigned char pin)
{
	static char pci_irq_table[][4] =
	/*
	 *      PCI IDSEL/INTPIN->INTLINE
	 *      A       B       C       D
	 */
	{
		{9,	10,	11,	12},	/* IDSEL 24 - DEC 21554 */
		{10,	0,	0,	0},	/* IDSEL 25 - DEC 21143 */
		{11,	12,	9,	10},	/* IDSEL 26 - PMC I */
		{12,	9,	10,	11},	/* IDSEL 27 - PMC II */
		{0,	0,	0,	0},	/* IDSEL 28 - unused */
		{0,	0,	9,	0},	/* IDSEL 29 - unused */
		{0,	0,	0,	0},	/* IDSEL 30 - Winbond */
		};
	const long min_idsel = 24, max_idsel = 30, irqs_per_slot = 4;
	return PCI_IRQ_TABLE_LOOKUP;
};

static inline int __init
pcore_680_map_irq(struct pci_dev *dev, unsigned char idsel, unsigned char pin)
{
	static char pci_irq_table[][4] =
	/*
	 *      PCI IDSEL/INTPIN->INTLINE
	 *      A       B       C       D
	 */
	{
		{9,	10,	11,	12},	/* IDSEL 24 - Sentinel */
		{10,	0,	0,	0},	/* IDSEL 25 - i82559 #1 */
		{11,	12,	9,	10},	/* IDSEL 26 - PMC I */
		{12,	9,	10,	11},	/* IDSEL 27 - PMC II */
		{9,	0,	0,	0},	/* IDSEL 28 - i82559 #2 */
		{0,	0,	0,	0},	/* IDSEL 29 - unused */
		{0,	0,	0,	0},	/* IDSEL 30 - Winbond */
		};
	const long min_idsel = 24, max_idsel = 30, irqs_per_slot = 4;
	return PCI_IRQ_TABLE_LOOKUP;
};

void __init
pcore_pcibios_fixup(void)
{
	struct pci_dev *dev;

	if ((dev = pci_get_device(PCI_VENDOR_ID_WINBOND,
				PCI_DEVICE_ID_WINBOND_83C553,
				0)))
	{
		/* Reroute interrupts both IDE channels to 15 */
		pci_write_config_byte(dev,
				PCORE_WINBOND_IDE_INT,
				0xff);

		/* Route INTA-D to IRQ9-12, respectively */
		pci_write_config_word(dev,
				PCORE_WINBOND_PCI_INT,
				0x9abc);

		/*
		 * Set up 8259 edge/level triggering
		 */
 		outb(0x00, PCORE_WINBOND_PRI_EDG_LVL);
		outb(0x1e, PCORE_WINBOND_SEC_EDG_LVL);
		pci_dev_put(dev);
	}
}

int __init
pcore_find_bridges(void)
{
	struct pci_controller* hose;
	int host_bridge, board_type;

	hose = pcibios_alloc_controller();
	if (!hose)
		return 0;

	mpc10x_bridge_init(hose,
			MPC10X_MEM_MAP_B,
			MPC10X_MEM_MAP_B,
			MPC10X_MAPB_EUMB_BASE);

	/* Determine board type */
	early_read_config_dword(hose,
			0,
			PCI_DEVFN(0,0),
			PCI_VENDOR_ID,
			&host_bridge);
	if (host_bridge == MPC10X_BRIDGE_106)
		board_type = PCORE_TYPE_6750;
	else /* MPC10X_BRIDGE_107 */
		board_type = PCORE_TYPE_680;

	hose->last_busno = pciauto_bus_scan(hose, hose->first_busno);

	ppc_md.pcibios_fixup = pcore_pcibios_fixup;
	ppc_md.pci_swizzle = common_swizzle;

	if (board_type == PCORE_TYPE_6750)
		ppc_md.pci_map_irq = pcore_6750_map_irq;
	else /* PCORE_TYPE_680 */
		ppc_md.pci_map_irq = pcore_680_map_irq;

	return board_type;
}

/* Dummy variable to satisfy mpc10x_common.o */
void *OpenPIC_Addr;

static int
pcore_show_cpuinfo(struct seq_file *m)
{
	seq_printf(m, "vendor\t\t: Force Computers\n");

	if (board_type == PCORE_TYPE_6750)
		seq_printf(m, "machine\t\t: PowerCore 6750\n");
	else /* PCORE_TYPE_680 */
		seq_printf(m, "machine\t\t: PowerCore 680\n");

	seq_printf(m, "L2\t\t: " );
	if (board_type == PCORE_TYPE_6750)
		switch (readb(PCORE_DCCR_REG) & PCORE_DCCR_L2_MASK)
		{
			case PCORE_DCCR_L2_0KB:
				seq_printf(m, "nocache");
				break;
			case PCORE_DCCR_L2_256KB:
				seq_printf(m, "256KB");
				break;
			case PCORE_DCCR_L2_1MB:
				seq_printf(m, "1MB");
				break;
			case PCORE_DCCR_L2_512KB:
				seq_printf(m, "512KB");
				break;
			default:
				seq_printf(m, "error");
				break;
		}
	else /* PCORE_TYPE_680 */
		switch (readb(PCORE_DCCR_REG) & PCORE_DCCR_L2_MASK)
		{
			case PCORE_DCCR_L2_2MB:
				seq_printf(m, "2MB");
				break;
			case PCORE_DCCR_L2_256KB:
				seq_printf(m, "reserved");
				break;
			case PCORE_DCCR_L2_1MB:
				seq_printf(m, "1MB");
				break;
			case PCORE_DCCR_L2_512KB:
				seq_printf(m, "512KB");
				break;
			default:
				seq_printf(m, "error");
				break;
		}

	seq_printf(m, "\n");

	return 0;
}

static void __init
pcore_setup_arch(void)
{
	/* init to some ~sane value until calibrate_delay() runs */
	loops_per_jiffy = 50000000/HZ;

	/* Lookup PCI host bridges */
	board_type = pcore_find_bridges();

#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start)
		ROOT_DEV = Root_RAM0;
        else
#endif
#ifdef CONFIG_ROOT_NFS
		ROOT_DEV = Root_NFS;
#else
		ROOT_DEV = Root_SDA2;
#endif

 	printk(KERN_INFO "Force PowerCore ");
	if (board_type == PCORE_TYPE_6750)
		printk("6750\n");
	else
		printk("680\n");
	printk(KERN_INFO "Port by MontaVista Software, Inc. (source@mvista.com)\n");
	_set_L2CR(L2CR_L2E | _get_L2CR());

}

static void
pcore_restart(char *cmd)
{
	local_irq_disable();
	/* Hard reset */
	writeb(0x11, 0xfe000332);
	while(1);
}

static void
pcore_halt(void)
{
	local_irq_disable();
	/* Turn off user LEDs */
	writeb(0x00, 0xfe000300);
	while (1);
}

static void
pcore_power_off(void)
{
	pcore_halt();
}


static void __init
pcore_init_IRQ(void)
{
	int i;

	for ( i = 0 ; i < 16 ; i++ )
		irq_desc[i].handler = &i8259_pic;

	i8259_init(0);
}

/*
 * Set BAT 3 to map 0xf0000000 to end of physical memory space.
 */
static __inline__ void
pcore_set_bat(void)
{
	mb();
	mtspr(SPRN_DBAT3U, 0xf0001ffe);
	mtspr(SPRN_DBAT3L, 0xfe80002a);
	mb();

}

static unsigned long __init
pcore_find_end_of_memory(void)
{

	return mpc10x_get_mem_size(MPC10X_MEM_MAP_B);
}

static void __init
pcore_map_io(void)
{
	io_block_mapping(0xfe000000, 0xfe000000, 0x02000000, _PAGE_IO);
}

TODC_ALLOC();

void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
		unsigned long r6, unsigned long r7)
{
	parse_bootinfo(find_bootinfo());

	/* Cover I/O space with a BAT */
	/* yuck, better hope your ram size is a power of 2  -- paulus */
	pcore_set_bat();

	isa_io_base = MPC10X_MAPB_ISA_IO_BASE;
	isa_mem_base = MPC10X_MAPB_ISA_MEM_BASE;
	pci_dram_offset = MPC10X_MAPB_DRAM_OFFSET;

	ppc_md.setup_arch	= pcore_setup_arch;
	ppc_md.show_cpuinfo	= pcore_show_cpuinfo;
	ppc_md.init_IRQ		= pcore_init_IRQ;
	ppc_md.get_irq		= i8259_irq;

	ppc_md.find_end_of_memory = pcore_find_end_of_memory;
	ppc_md.setup_io_mappings = pcore_map_io;

	ppc_md.restart		= pcore_restart;
	ppc_md.power_off	= pcore_power_off;
	ppc_md.halt		= pcore_halt;

	TODC_INIT(TODC_TYPE_MK48T59,
		  PCORE_NVRAM_AS0,
		  PCORE_NVRAM_AS1,
		  PCORE_NVRAM_DATA,
		  8);

	ppc_md.time_init	= todc_time_init;
	ppc_md.get_rtc_time	= todc_get_rtc_time;
	ppc_md.set_rtc_time	= todc_set_rtc_time;
	ppc_md.calibrate_decr	= todc_calibrate_decr;

	ppc_md.nvram_read_val	= todc_m48txx_read_val;
	ppc_md.nvram_write_val	= todc_m48txx_write_val;

#ifdef CONFIG_SERIAL_TEXT_DEBUG
	ppc_md.progress = gen550_progress;
#endif
#ifdef CONFIG_KGDB
	ppc_md.kgdb_map_scc = gen550_kgdb_map_scc;
#endif
}
