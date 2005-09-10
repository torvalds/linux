/*
 * arch/ppc/syslib/mpc10x_common.c
 *
 * Common routines for the Motorola SPS MPC106, MPC107 and MPC8240 Host bridge,
 * Mem ctlr, EPIC, etc.
 *
 * Author: Mark A. Greer
 *         mgreer@mvista.com
 *
 * 2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

/*
 * *** WARNING - A BAT MUST be set to access the PCI config addr/data regs ***
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/serial_8250.h>
#include <linux/fsl_devices.h>
#include <linux/device.h>

#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/machdep.h>
#include <asm/pci-bridge.h>
#include <asm/open_pic.h>
#include <asm/mpc10x.h>
#include <asm/ppc_sys.h>

#ifdef CONFIG_MPC10X_OPENPIC
#ifdef CONFIG_EPIC_SERIAL_MODE
#define EPIC_IRQ_BASE (epic_serial_mode ? 16 : 5)
#else
#define EPIC_IRQ_BASE 5
#endif
#define MPC10X_I2C_IRQ (EPIC_IRQ_BASE + NUM_8259_INTERRUPTS)
#define MPC10X_DMA0_IRQ (EPIC_IRQ_BASE + 1 + NUM_8259_INTERRUPTS)
#define MPC10X_DMA1_IRQ (EPIC_IRQ_BASE + 2 + NUM_8259_INTERRUPTS)
#define MPC10X_UART0_IRQ (EPIC_IRQ_BASE + 4 + NUM_8259_INTERRUPTS)
#define MPC10X_UART1_IRQ (EPIC_IRQ_BASE + 5 + NUM_8259_INTERRUPTS)
#else
#define MPC10X_I2C_IRQ -1
#define MPC10X_DMA0_IRQ -1
#define MPC10X_DMA1_IRQ -1
#define MPC10X_UART0_IRQ -1
#define MPC10X_UART1_IRQ -1
#endif

static struct fsl_i2c_platform_data mpc10x_i2c_pdata = {
	.device_flags		= 0,
};

static struct plat_serial8250_port serial_plat_uart0[] = {
	[0] = {
		.mapbase	= 0x4500,
		.iotype		= UPIO_MEM,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
	},
	{ },
};
static struct plat_serial8250_port serial_plat_uart1[] = {
	[0] = {
		.mapbase	= 0x4600,
		.iotype		= UPIO_MEM,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
	},
	{ },
};

struct platform_device ppc_sys_platform_devices[] = {
	[MPC10X_IIC1] = {
		.name 	= "fsl-i2c",
		.id	= 1,
		.dev.platform_data = &mpc10x_i2c_pdata,
		.num_resources = 2,
		.resource = (struct resource[]) {
			{
				.start 	= MPC10X_EUMB_I2C_OFFSET,
				.end	= MPC10X_EUMB_I2C_OFFSET +
		                            MPC10X_EUMB_I2C_SIZE - 1,
				.flags	= IORESOURCE_MEM,
			},
			{
				.flags 	= IORESOURCE_IRQ
			},
		},
	},
	[MPC10X_DMA0] = {
		.name	= "fsl-dma",
		.id	= 0,
		.num_resources = 2,
		.resource = (struct resource[]) {
			{
				.start 	= MPC10X_EUMB_DMA_OFFSET + 0x10,
				.end	= MPC10X_EUMB_DMA_OFFSET + 0x1f,
				.flags	= IORESOURCE_MEM,
			},
			{
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC10X_DMA1] = {
		.name	= "fsl-dma",
		.id	= 1,
		.num_resources = 2,
		.resource = (struct resource[]) {
			{
				.start	= MPC10X_EUMB_DMA_OFFSET + 0x20,
				.end	= MPC10X_EUMB_DMA_OFFSET + 0x2f,
				.flags	= IORESOURCE_MEM,
			},
			{
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC10X_DMA1] = {
		.name	= "fsl-dma",
		.id	= 1,
		.num_resources = 2,
		.resource = (struct resource[]) {
			{
				.start	= MPC10X_EUMB_DMA_OFFSET + 0x20,
				.end	= MPC10X_EUMB_DMA_OFFSET + 0x2f,
				.flags	= IORESOURCE_MEM,
			},
			{
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC10X_UART0] = {
		.name = "serial8250",
		.id	= PLAT8250_DEV_PLATFORM,
		.dev.platform_data = serial_plat_uart0,
	},
	[MPC10X_UART1] = {
		.name = "serial8250",
		.id	= PLAT8250_DEV_PLATFORM1,
		.dev.platform_data = serial_plat_uart1,
	},

};

/* We use the PCI ID to match on */
struct ppc_sys_spec *cur_ppc_sys_spec;
struct ppc_sys_spec ppc_sys_specs[] = {
	{
		.ppc_sys_name 	= "8245",
		.mask		= 0xFFFFFFFF,
		.value		= MPC10X_BRIDGE_8245,
		.num_devices	= 5,
		.device_list	= (enum ppc_sys_devices[])
		{
			MPC10X_IIC1, MPC10X_DMA0, MPC10X_DMA1, MPC10X_UART0, MPC10X_UART1,
		},
	},
	{
		.ppc_sys_name 	= "8240",
		.mask		= 0xFFFFFFFF,
		.value		= MPC10X_BRIDGE_8240,
		.num_devices	= 3,
		.device_list	= (enum ppc_sys_devices[])
		{
			MPC10X_IIC1, MPC10X_DMA0, MPC10X_DMA1,
		},
	},
	{
		.ppc_sys_name	= "107",
		.mask		= 0xFFFFFFFF,
		.value		= MPC10X_BRIDGE_107,
		.num_devices	= 3,
		.device_list	= (enum ppc_sys_devices[])
		{
			MPC10X_IIC1, MPC10X_DMA0, MPC10X_DMA1,
		},
	},
	{       /* default match */
		.ppc_sys_name   = "",
		.mask           = 0x00000000,
		.value          = 0x00000000,
	},
};

/*
 * mach_mpc10x_fixup: This function enables DUART mode if it detects
 * if it detects two UARTS in the platform device entries.
 */
static int __init mach_mpc10x_fixup(struct platform_device *pdev)
{
	if (strncmp (pdev->name, "serial8250", 10) == 0 && pdev->id == 1)
		writeb(readb(serial_plat_uart1[0].membase + 0x11) | 0x1,
				serial_plat_uart1[0].membase + 0x11);
	return 0;
}

static int __init mach_mpc10x_init(void)
{
	ppc_sys_device_fixup = mach_mpc10x_fixup;
	return 0;
}
postcore_initcall(mach_mpc10x_init);

/* Set resources to match bridge memory map */
void __init
mpc10x_bridge_set_resources(int map, struct pci_controller *hose)
{

	switch (map) {
		case MPC10X_MEM_MAP_A:
			pci_init_resource(&hose->io_resource,
					0x00000000,
					0x3f7fffff,
					IORESOURCE_IO,
					"PCI host bridge");

			pci_init_resource (&hose->mem_resources[0],
					0xc0000000,
					0xfeffffff,
					IORESOURCE_MEM,
					"PCI host bridge");
			break;
		case MPC10X_MEM_MAP_B:
			pci_init_resource(&hose->io_resource,
					0x00000000,
					0x00bfffff,
					IORESOURCE_IO,
					"PCI host bridge");

			pci_init_resource (&hose->mem_resources[0],
					0x80000000,
					0xfcffffff,
					IORESOURCE_MEM,
					"PCI host bridge");
			break;
		default:
			printk("mpc10x_bridge_set_resources: "
					"Invalid map specified\n");
			if (ppc_md.progress)
				ppc_md.progress("mpc10x:exit1", 0x100);
	}
}

/*
 * Do some initialization and put the EUMB registers at the specified address
 * (also map the EPIC registers into virtual space--OpenPIC_Addr will be set).
 *
 * The EPIC is not on the 106, only the 8240 and 107.
 */
int __init
mpc10x_bridge_init(struct pci_controller *hose,
		   uint current_map,
		   uint new_map,
		   uint phys_eumb_base)
{
	int	host_bridge, picr1, picr1_bit, i;
	ulong	pci_config_addr, pci_config_data;
	u_char	pir, byte;

	if (ppc_md.progress) ppc_md.progress("mpc10x:enter", 0x100);

	/* Set up for current map so we can get at config regs */
	switch (current_map) {
		case MPC10X_MEM_MAP_A:
			setup_indirect_pci(hose,
					   MPC10X_MAPA_CNFG_ADDR,
					   MPC10X_MAPA_CNFG_DATA);
			break;
		case MPC10X_MEM_MAP_B:
			setup_indirect_pci(hose,
					   MPC10X_MAPB_CNFG_ADDR,
					   MPC10X_MAPB_CNFG_DATA);
			break;
		default:
			printk("mpc10x_bridge_init: %s\n",
				"Invalid current map specified");
			if (ppc_md.progress)
				ppc_md.progress("mpc10x:exit1", 0x100);
			return -1;
	}

	/* Make sure it's a supported bridge */
	early_read_config_dword(hose,
			        0,
			        PCI_DEVFN(0,0),
			        PCI_VENDOR_ID,
			        &host_bridge);

	switch (host_bridge) {
		case MPC10X_BRIDGE_106:
		case MPC10X_BRIDGE_8240:
		case MPC10X_BRIDGE_107:
		case MPC10X_BRIDGE_8245:
			break;
		default:
			if (ppc_md.progress)
				ppc_md.progress("mpc10x:exit2", 0x100);
			return -1;
	}

	switch (new_map) {
		case MPC10X_MEM_MAP_A:
			MPC10X_SETUP_HOSE(hose, A);
			pci_config_addr = MPC10X_MAPA_CNFG_ADDR;
			pci_config_data = MPC10X_MAPA_CNFG_DATA;
			picr1_bit = MPC10X_CFG_PICR1_ADDR_MAP_A;
			break;
		case MPC10X_MEM_MAP_B:
			MPC10X_SETUP_HOSE(hose, B);
			pci_config_addr = MPC10X_MAPB_CNFG_ADDR;
			pci_config_data = MPC10X_MAPB_CNFG_DATA;
			picr1_bit = MPC10X_CFG_PICR1_ADDR_MAP_B;
			break;
		default:
			printk("mpc10x_bridge_init: %s\n",
				"Invalid new map specified");
			if (ppc_md.progress)
				ppc_md.progress("mpc10x:exit3", 0x100);
			return -1;
	}

	/* Make bridge use the 'new_map', if not already usng it */
	if (current_map != new_map) {
		early_read_config_dword(hose,
					0,
					PCI_DEVFN(0,0),
					MPC10X_CFG_PICR1_REG,
					&picr1);

		picr1 = (picr1 & ~MPC10X_CFG_PICR1_ADDR_MAP_MASK) |
			 picr1_bit;

		early_write_config_dword(hose,
					 0,
					 PCI_DEVFN(0,0),
					 MPC10X_CFG_PICR1_REG,
					 picr1);

		asm volatile("sync");

		/* Undo old mappings & map in new cfg data/addr regs */
		iounmap((void *)hose->cfg_addr);
		iounmap((void *)hose->cfg_data);

		setup_indirect_pci(hose,
				   pci_config_addr,
				   pci_config_data);
	}

	/* Setup resources to match map */
	mpc10x_bridge_set_resources(new_map, hose);

	/*
	 * Want processor accesses of 0xFDxxxxxx to be mapped
	 * to PCI memory space at 0x00000000.  Do not want
	 * host bridge to respond to PCI memory accesses of
	 * 0xFDxxxxxx.  Do not want host bridge to respond
	 * to PCI memory addresses 0xFD000000-0xFDFFFFFF;
	 * want processor accesses from 0x000A0000-0x000BFFFF
	 * to be forwarded to system memory.
	 *
	 * Only valid if not in agent mode and using MAP B.
	 */
	if (new_map == MPC10X_MEM_MAP_B) {
		early_read_config_byte(hose,
				       0,
				       PCI_DEVFN(0,0),
				       MPC10X_CFG_MAPB_OPTIONS_REG,
				       &byte);

		byte &= ~(MPC10X_CFG_MAPB_OPTIONS_PFAE  |
			  MPC10X_CFG_MAPB_OPTIONS_PCICH |
			  MPC10X_CFG_MAPB_OPTIONS_PROCCH);

		if (host_bridge != MPC10X_BRIDGE_106) {
			byte |= MPC10X_CFG_MAPB_OPTIONS_CFAE;
		}

		early_write_config_byte(hose,
					0,
					PCI_DEVFN(0,0),
					MPC10X_CFG_MAPB_OPTIONS_REG,
					byte);
	}

	if (host_bridge != MPC10X_BRIDGE_106) {
		early_read_config_byte(hose,
				       0,
				       PCI_DEVFN(0,0),
				       MPC10X_CFG_PIR_REG,
				       &pir);

		if (pir != MPC10X_CFG_PIR_HOST_BRIDGE) {
			printk("Host bridge in Agent mode\n");
			/* Read or Set LMBAR & PCSRBAR? */
		}

		/* Set base addr of the 8240/107 EUMB.  */
		early_write_config_dword(hose,
					 0,
					 PCI_DEVFN(0,0),
					 MPC10X_CFG_EUMBBAR,
					 phys_eumb_base);
#ifdef CONFIG_MPC10X_OPENPIC
		/* Map EPIC register part of EUMB into vitual memory  - PCORE
		   uses an i8259 instead of EPIC. */
		OpenPIC_Addr =
			ioremap(phys_eumb_base + MPC10X_EUMB_EPIC_OFFSET,
				MPC10X_EUMB_EPIC_SIZE);
#endif
	}

#ifdef CONFIG_MPC10X_STORE_GATHERING
	mpc10x_enable_store_gathering(hose);
#else
	mpc10x_disable_store_gathering(hose);
#endif

	/* setup platform devices for MPC10x bridges */
	identify_ppc_sys_by_id (host_bridge);

	for (i = 0; i < cur_ppc_sys_spec->num_devices; i++) {
		unsigned int dev_id = cur_ppc_sys_spec->device_list[i];
		ppc_sys_fixup_mem_resource(&ppc_sys_platform_devices[dev_id],
			phys_eumb_base);
	}

	/* IRQ's are determined at runtime */
	ppc_sys_platform_devices[MPC10X_IIC1].resource[1].start = MPC10X_I2C_IRQ;
	ppc_sys_platform_devices[MPC10X_IIC1].resource[1].end = MPC10X_I2C_IRQ;
	ppc_sys_platform_devices[MPC10X_DMA0].resource[1].start = MPC10X_DMA0_IRQ;
	ppc_sys_platform_devices[MPC10X_DMA0].resource[1].end = MPC10X_DMA0_IRQ;
	ppc_sys_platform_devices[MPC10X_DMA1].resource[1].start = MPC10X_DMA1_IRQ;
	ppc_sys_platform_devices[MPC10X_DMA1].resource[1].end = MPC10X_DMA1_IRQ;

	serial_plat_uart0[0].mapbase += phys_eumb_base;
	serial_plat_uart0[0].irq = MPC10X_UART0_IRQ;
	serial_plat_uart0[0].membase = ioremap(serial_plat_uart0[0].mapbase, 0x100);

	serial_plat_uart1[0].mapbase += phys_eumb_base;
	serial_plat_uart1[0].irq = MPC10X_UART1_IRQ;
	serial_plat_uart1[0].membase = ioremap(serial_plat_uart1[0].mapbase, 0x100);

	/*
	 * 8240 erratum 26, 8241/8245 erratum 29, 107 erratum 23: speculative
	 * PCI reads may return stale data so turn off.
	 */
	if ((host_bridge == MPC10X_BRIDGE_8240)
		|| (host_bridge == MPC10X_BRIDGE_8245)
		|| (host_bridge == MPC10X_BRIDGE_107)) {

		early_read_config_dword(hose, 0, PCI_DEVFN(0,0),
			MPC10X_CFG_PICR1_REG, &picr1);

		picr1 &= ~MPC10X_CFG_PICR1_SPEC_PCI_RD;

		early_write_config_dword(hose, 0, PCI_DEVFN(0,0),
			MPC10X_CFG_PICR1_REG, picr1);
	}

	/*
	 * 8241/8245 erratum 28: PCI reads from local memory may return
	 * stale data.  Workaround by setting PICR2[0] to disable copyback
	 * optimization.  Oddly, the latest available user manual for the
	 * 8245 (Rev 2., dated 10/2003) says PICR2[0] is reserverd.
	 */
	if (host_bridge == MPC10X_BRIDGE_8245) {
		u32	picr2;

		early_read_config_dword(hose, 0, PCI_DEVFN(0,0),
			MPC10X_CFG_PICR2_REG, &picr2);

		picr2 |= MPC10X_CFG_PICR2_COPYBACK_OPT;

		early_write_config_dword(hose, 0, PCI_DEVFN(0,0),
			 MPC10X_CFG_PICR2_REG, picr2);
	}

	if (ppc_md.progress) ppc_md.progress("mpc10x:exit", 0x100);
	return 0;
}

/*
 * Need to make our own PCI config space access macros because
 * mpc10x_get_mem_size() is called before the data structures are set up for
 * the 'early_xxx' and 'indirect_xxx' routines to work.
 * Assumes bus 0.
 */
#define MPC10X_CFG_read(val, addr, type, op)	*val = op((type)(addr))
#define MPC10X_CFG_write(val, addr, type, op)	op((type *)(addr), (val))

#define MPC10X_PCI_OP(rw, size, type, op, mask)			 	\
static void								\
mpc10x_##rw##_config_##size(uint *cfg_addr, uint *cfg_data, int devfn, int offset, type val) \
{									\
	out_be32(cfg_addr, 						\
		 ((offset & 0xfc) << 24) | (devfn << 16)		\
		 | (0 << 8) | 0x80);					\
	MPC10X_CFG_##rw(val, cfg_data + (offset & mask), type, op);	\
	return;    					 		\
}

MPC10X_PCI_OP(read,  byte,  u8 *, in_8, 3)
MPC10X_PCI_OP(read,  dword, u32 *, in_le32, 0)
#if 0	/* Not used */
MPC10X_PCI_OP(write, byte,  u8, out_8, 3)
MPC10X_PCI_OP(read,  word,  u16 *, in_le16, 2)
MPC10X_PCI_OP(write, word,  u16, out_le16, 2)
MPC10X_PCI_OP(write, dword, u32, out_le32, 0)
#endif

/*
 * Read the memory controller registers to determine the amount of memory in
 * the system.  This assumes that the firmware has correctly set up the memory
 * controller registers.
 */
unsigned long __init
mpc10x_get_mem_size(uint mem_map)
{
	uint			*config_addr, *config_data, val;
	ulong			start, end, total, offset;
	int			i;
	u_char			bank_enables;

	switch (mem_map) {
		case MPC10X_MEM_MAP_A:
			config_addr = (uint *)MPC10X_MAPA_CNFG_ADDR;
			config_data = (uint *)MPC10X_MAPA_CNFG_DATA;
			break;
		case MPC10X_MEM_MAP_B:
			config_addr = (uint *)MPC10X_MAPB_CNFG_ADDR;
			config_data = (uint *)MPC10X_MAPB_CNFG_DATA;
			break;
		default:
			return 0;
	}

	mpc10x_read_config_byte(config_addr,
				config_data,
				PCI_DEVFN(0,0),
			        MPC10X_MCTLR_MEM_BANK_ENABLES,
			        &bank_enables);

	total = 0;

	for (i=0; i<8; i++) {
		if (bank_enables & (1 << i)) {
			offset = MPC10X_MCTLR_MEM_START_1 + ((i > 3) ? 4 : 0);
			mpc10x_read_config_dword(config_addr,
						 config_data,
						 PCI_DEVFN(0,0),
						 offset,
						 &val);
			start = (val >> ((i & 3) << 3)) & 0xff;

			offset = MPC10X_MCTLR_EXT_MEM_START_1 + ((i>3) ? 4 : 0);
			mpc10x_read_config_dword(config_addr,
						 config_data,
						 PCI_DEVFN(0,0),
						 offset,
						 &val);
			val = (val >> ((i & 3) << 3)) & 0x03;
			start = (val << 28) | (start << 20);

			offset = MPC10X_MCTLR_MEM_END_1 + ((i > 3) ? 4 : 0);
			mpc10x_read_config_dword(config_addr,
						 config_data,
						 PCI_DEVFN(0,0),
						 offset,
						 &val);
			end = (val >> ((i & 3) << 3)) & 0xff;

			offset = MPC10X_MCTLR_EXT_MEM_END_1 + ((i > 3) ? 4 : 0);
			mpc10x_read_config_dword(config_addr,
						 config_data,
						 PCI_DEVFN(0,0),
						 offset,
						 &val);
			val = (val >> ((i & 3) << 3)) & 0x03;
			end = (val << 28) | (end << 20) | 0xfffff;

			total += (end - start + 1);
		}
	}

	return total;
}

int __init
mpc10x_enable_store_gathering(struct pci_controller *hose)
{
	uint picr1;

	early_read_config_dword(hose,
				0,
				PCI_DEVFN(0,0),
			        MPC10X_CFG_PICR1_REG,
			        &picr1);

	picr1 |= MPC10X_CFG_PICR1_ST_GATH_EN;

	early_write_config_dword(hose,
				0,
				PCI_DEVFN(0,0),
				MPC10X_CFG_PICR1_REG,
				picr1);

	return 0;
}

int __init
mpc10x_disable_store_gathering(struct pci_controller *hose)
{
	uint picr1;

	early_read_config_dword(hose,
				0,
				PCI_DEVFN(0,0),
			        MPC10X_CFG_PICR1_REG,
			        &picr1);

	picr1 &= ~MPC10X_CFG_PICR1_ST_GATH_EN;

	early_write_config_dword(hose,
				0,
				PCI_DEVFN(0,0),
				MPC10X_CFG_PICR1_REG,
				picr1);

	return 0;
}

#ifdef CONFIG_MPC10X_OPENPIC
void __init mpc10x_set_openpic(void)
{
	/* Map external IRQs */
	openpic_set_sources(0, EPIC_IRQ_BASE, OpenPIC_Addr + 0x10200);
	/* Skip reserved space and map i2c and DMA Ch[01] */
	openpic_set_sources(EPIC_IRQ_BASE, 3, OpenPIC_Addr + 0x11020);
	/* Skip reserved space and map Message Unit Interrupt (I2O) */
	openpic_set_sources(EPIC_IRQ_BASE + 3, 1, OpenPIC_Addr + 0x110C0);
	/* Skip reserved space and map Serial Interupts */
	openpic_set_sources(EPIC_IRQ_BASE + 4, 2, OpenPIC_Addr + 0x11120);

	openpic_init(NUM_8259_INTERRUPTS);
}
#endif
