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

#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/machdep.h>
#include <asm/pci-bridge.h>
#include <asm/open_pic.h>
#include <asm/mpc10x.h>
#include <asm/ocp.h>

/* The OCP structure is fixed by code below, before OCP initialises.
   paddr depends on where the board places the EUMB.
    - fixed in mpc10x_bridge_init().
   irq depends on two things:
    > does the board use the EPIC at all? (PCORE does not).
    > is the EPIC in serial or parallel mode?
    - fixed in mpc10x_set_openpic().
*/

#ifdef CONFIG_MPC10X_OPENPIC
#ifdef CONFIG_EPIC_SERIAL_MODE
#define EPIC_IRQ_BASE (epic_serial_mode ? 16 : 5)
#else
#define EPIC_IRQ_BASE 5
#endif
#define MPC10X_I2C_IRQ (EPIC_IRQ_BASE + NUM_8259_INTERRUPTS)
#define MPC10X_DMA0_IRQ (EPIC_IRQ_BASE + 1 + NUM_8259_INTERRUPTS)
#define MPC10X_DMA1_IRQ (EPIC_IRQ_BASE + 2 + NUM_8259_INTERRUPTS)
#else
#define MPC10X_I2C_IRQ OCP_IRQ_NA
#define MPC10X_DMA0_IRQ OCP_IRQ_NA
#define MPC10X_DMA1_IRQ OCP_IRQ_NA
#endif


struct ocp_def core_ocp[] = {
	{ .vendor	= OCP_VENDOR_INVALID
	}
};

static struct ocp_fs_i2c_data mpc10x_i2c_data = {
	.flags		= 0
};
static struct ocp_def mpc10x_i2c_ocp = {
	.vendor		= OCP_VENDOR_MOTOROLA,
	.function	= OCP_FUNC_IIC,
	.index		= 0,
	.additions	= &mpc10x_i2c_data
};

static struct ocp_def mpc10x_dma_ocp[2] = {
{	.vendor		= OCP_VENDOR_MOTOROLA,
	.function	= OCP_FUNC_DMA,
	.index		= 0 },
{	.vendor		= OCP_VENDOR_MOTOROLA,
	.function	= OCP_FUNC_DMA,
	.index		= 1 }
};

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
	int	host_bridge, picr1, picr1_bit;
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
		mpc10x_i2c_ocp.paddr = phys_eumb_base + MPC10X_EUMB_I2C_OFFSET;
		mpc10x_i2c_ocp.irq = MPC10X_I2C_IRQ;
		ocp_add_one_device(&mpc10x_i2c_ocp);
		mpc10x_dma_ocp[0].paddr = phys_eumb_base +
					MPC10X_EUMB_DMA_OFFSET + 0x100;
		mpc10x_dma_ocp[0].irq = MPC10X_DMA0_IRQ;
		ocp_add_one_device(&mpc10x_dma_ocp[0]);
		mpc10x_dma_ocp[1].paddr = phys_eumb_base +
					MPC10X_EUMB_DMA_OFFSET + 0x200;
		mpc10x_dma_ocp[1].irq = MPC10X_DMA1_IRQ;
		ocp_add_one_device(&mpc10x_dma_ocp[1]);
	}

#ifdef CONFIG_MPC10X_STORE_GATHERING
	mpc10x_enable_store_gathering(hose);
#else
	mpc10x_disable_store_gathering(hose);
#endif

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
		ulong	picr2;

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

	openpic_init(NUM_8259_INTERRUPTS);
}
#endif
