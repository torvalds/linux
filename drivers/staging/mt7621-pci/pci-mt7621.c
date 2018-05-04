/**************************************************************************
 *
 *  BRIEF MODULE DESCRIPTION
 *     PCI init for Ralink RT2880 solution
 *
 *  Copyright 2007 Ralink Inc. (bruce_chang@ralinktech.com.tw)
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 **************************************************************************
 * May 2007 Bruce Chang
 * Initial Release
 *
 * May 2009 Bruce Chang
 * support RT2880/RT3883 PCIe
 *
 * May 2011 Bruce Chang
 * support RT6855/MT7620 PCIe
 *
 **************************************************************************
 */

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <asm/pci.h>
#include <asm/io.h>
#include <asm/mips-cm.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_pci.h>
#include <linux/platform_device.h>

#include <ralink_regs.h>

extern void pcie_phy_init(void);
extern void chk_phy_pll(void);

/*
 * These functions and structures provide the BIOS scan and mapping of the PCI
 * devices.
 */

#define RALINK_PCIE0_CLK_EN		(1<<24)
#define RALINK_PCIE1_CLK_EN		(1<<25)
#define RALINK_PCIE2_CLK_EN		(1<<26)

#define RALINK_PCI_CONFIG_ADDR		0x20
#define RALINK_PCI_CONFIG_DATA_VIRTUAL_REG	0x24
#define RALINK_PCI_MEMBASE		*(volatile u32 *)(RALINK_PCI_BASE + 0x0028)
#define RALINK_PCI_IOBASE		*(volatile u32 *)(RALINK_PCI_BASE + 0x002C)
#define RALINK_PCIE0_RST		(1<<24)
#define RALINK_PCIE1_RST		(1<<25)
#define RALINK_PCIE2_RST		(1<<26)
#define RALINK_SYSCTL_BASE		0xBE000000

#define RALINK_PCI_PCICFG_ADDR		*(volatile u32 *)(RALINK_PCI_BASE + 0x0000)
#define RALINK_PCI_PCIMSK_ADDR		*(volatile u32 *)(RALINK_PCI_BASE + 0x000C)
#define RALINK_PCI_BASE	0xBE140000

#define RALINK_PCIEPHY_P0P1_CTL_OFFSET	(RALINK_PCI_BASE + 0x9000)
#define RT6855_PCIE0_OFFSET		0x2000
#define RT6855_PCIE1_OFFSET		0x3000
#define RT6855_PCIE2_OFFSET		0x4000

#define RALINK_PCI0_BAR0SETUP_ADDR	*(volatile u32 *)(RALINK_PCI_BASE + RT6855_PCIE0_OFFSET + 0x0010)
#define RALINK_PCI0_IMBASEBAR0_ADDR	*(volatile u32 *)(RALINK_PCI_BASE + RT6855_PCIE0_OFFSET + 0x0018)
#define RALINK_PCI0_ID			*(volatile u32 *)(RALINK_PCI_BASE + RT6855_PCIE0_OFFSET + 0x0030)
#define RALINK_PCI0_CLASS		*(volatile u32 *)(RALINK_PCI_BASE + RT6855_PCIE0_OFFSET + 0x0034)
#define RALINK_PCI0_SUBID		*(volatile u32 *)(RALINK_PCI_BASE + RT6855_PCIE0_OFFSET + 0x0038)
#define RALINK_PCI0_STATUS		*(volatile u32 *)(RALINK_PCI_BASE + RT6855_PCIE0_OFFSET + 0x0050)
#define RALINK_PCI0_DERR		*(volatile u32 *)(RALINK_PCI_BASE + RT6855_PCIE0_OFFSET + 0x0060)
#define RALINK_PCI0_ECRC		*(volatile u32 *)(RALINK_PCI_BASE + RT6855_PCIE0_OFFSET + 0x0064)

#define RALINK_PCI1_BAR0SETUP_ADDR	*(volatile u32 *)(RALINK_PCI_BASE + RT6855_PCIE1_OFFSET + 0x0010)
#define RALINK_PCI1_IMBASEBAR0_ADDR	*(volatile u32 *)(RALINK_PCI_BASE + RT6855_PCIE1_OFFSET + 0x0018)
#define RALINK_PCI1_ID			*(volatile u32 *)(RALINK_PCI_BASE + RT6855_PCIE1_OFFSET + 0x0030)
#define RALINK_PCI1_CLASS		*(volatile u32 *)(RALINK_PCI_BASE + RT6855_PCIE1_OFFSET + 0x0034)
#define RALINK_PCI1_SUBID		*(volatile u32 *)(RALINK_PCI_BASE + RT6855_PCIE1_OFFSET + 0x0038)
#define RALINK_PCI1_STATUS		*(volatile u32 *)(RALINK_PCI_BASE + RT6855_PCIE1_OFFSET + 0x0050)
#define RALINK_PCI1_DERR		*(volatile u32 *)(RALINK_PCI_BASE + RT6855_PCIE1_OFFSET + 0x0060)
#define RALINK_PCI1_ECRC		*(volatile u32 *)(RALINK_PCI_BASE + RT6855_PCIE1_OFFSET + 0x0064)

#define RALINK_PCI2_BAR0SETUP_ADDR	*(volatile u32 *)(RALINK_PCI_BASE + RT6855_PCIE2_OFFSET + 0x0010)
#define RALINK_PCI2_IMBASEBAR0_ADDR	*(volatile u32 *)(RALINK_PCI_BASE + RT6855_PCIE2_OFFSET + 0x0018)
#define RALINK_PCI2_ID			*(volatile u32 *)(RALINK_PCI_BASE + RT6855_PCIE2_OFFSET + 0x0030)
#define RALINK_PCI2_CLASS		*(volatile u32 *)(RALINK_PCI_BASE + RT6855_PCIE2_OFFSET + 0x0034)
#define RALINK_PCI2_SUBID		*(volatile u32 *)(RALINK_PCI_BASE + RT6855_PCIE2_OFFSET + 0x0038)
#define RALINK_PCI2_STATUS		*(volatile u32 *)(RALINK_PCI_BASE + RT6855_PCIE2_OFFSET + 0x0050)
#define RALINK_PCI2_DERR		*(volatile u32 *)(RALINK_PCI_BASE + RT6855_PCIE2_OFFSET + 0x0060)
#define RALINK_PCI2_ECRC		*(volatile u32 *)(RALINK_PCI_BASE + RT6855_PCIE2_OFFSET + 0x0064)

#define RALINK_PCIEPHY_P0P1_CTL_OFFSET	(RALINK_PCI_BASE + 0x9000)
#define RALINK_PCIEPHY_P2_CTL_OFFSET	(RALINK_PCI_BASE + 0xA000)

#define MV_WRITE(ofs, data)	\
	*(volatile u32 *)(RALINK_PCI_BASE+(ofs)) = cpu_to_le32(data)
#define MV_READ(ofs, data)	\
	*(data) = le32_to_cpu(*(volatile u32 *)(RALINK_PCI_BASE+(ofs)))
#define MV_READ_DATA(ofs)	\
	le32_to_cpu(*(volatile u32 *)(RALINK_PCI_BASE+(ofs)))

#define MV_WRITE_16(ofs, data)	\
	*(volatile u16 *)(RALINK_PCI_BASE+(ofs)) = cpu_to_le16(data)
#define MV_READ_16(ofs, data)	\
	*(data) = le16_to_cpu(*(volatile u16 *)(RALINK_PCI_BASE+(ofs)))

#define MV_WRITE_8(ofs, data)	\
	*(volatile u8 *)(RALINK_PCI_BASE+(ofs)) = data
#define MV_READ_8(ofs, data)	\
	*(data) = *(volatile u8 *)(RALINK_PCI_BASE+(ofs))

#define RALINK_PCI_MM_MAP_BASE		0x60000000
#define RALINK_PCI_IO_MAP_BASE		0x1e160000

#define RALINK_SYSTEM_CONTROL_BASE	0xbe000000

#define ASSERT_SYSRST_PCIE(val)		\
	do {								\
		if (*(unsigned int *)(0xbe00000c) == 0x00030101)	\
			RALINK_RSTCTRL |= val;				\
		else							\
			RALINK_RSTCTRL &= ~val;				\
	} while(0)
#define DEASSERT_SYSRST_PCIE(val)	\
	do {								\
		if (*(unsigned int *)(0xbe00000c) == 0x00030101)	\
			RALINK_RSTCTRL &= ~val;				\
		else							\
			RALINK_RSTCTRL |= val;				\
	} while(0)
#define RALINK_SYSCFG1			*(unsigned int *)(RALINK_SYSTEM_CONTROL_BASE + 0x14)
#define RALINK_CLKCFG1			*(unsigned int *)(RALINK_SYSTEM_CONTROL_BASE + 0x30)
#define RALINK_RSTCTRL			*(unsigned int *)(RALINK_SYSTEM_CONTROL_BASE + 0x34)
#define RALINK_GPIOMODE			*(unsigned int *)(RALINK_SYSTEM_CONTROL_BASE + 0x60)
#define RALINK_PCIE_CLK_GEN		*(unsigned int *)(RALINK_SYSTEM_CONTROL_BASE + 0x7c)
#define RALINK_PCIE_CLK_GEN1		*(unsigned int *)(RALINK_SYSTEM_CONTROL_BASE + 0x80)
#define PPLL_CFG1			*(unsigned int *)(RALINK_SYSTEM_CONTROL_BASE + 0x9c)
#define PPLL_DRV			*(unsigned int *)(RALINK_SYSTEM_CONTROL_BASE + 0xa0)
//RALINK_SYSCFG1 bit
#define RALINK_PCI_HOST_MODE_EN		(1<<7)
#define RALINK_PCIE_RC_MODE_EN		(1<<8)
//RALINK_RSTCTRL bit
#define RALINK_PCIE_RST			(1<<23)
#define RALINK_PCI_RST			(1<<24)
//RALINK_CLKCFG1 bit
#define RALINK_PCI_CLK_EN		(1<<19)
#define RALINK_PCIE_CLK_EN		(1<<21)
//RALINK_GPIOMODE bit
#define PCI_SLOTx2			(1<<11)
#define PCI_SLOTx1			(2<<11)
//MTK PCIE PLL bit
#define PDRV_SW_SET			(1<<31)
#define LC_CKDRVPD_			(1<<19)

#define MEMORY_BASE 0x0
static int pcie_link_status = 0;

#define PCI_ACCESS_READ_1  0
#define PCI_ACCESS_READ_2  1
#define PCI_ACCESS_READ_4  2
#define PCI_ACCESS_WRITE_1 3
#define PCI_ACCESS_WRITE_2 4
#define PCI_ACCESS_WRITE_4 5

static int config_access(unsigned char access_type, struct pci_bus *bus,
			unsigned int devfn, unsigned int where, u32 * data)
{
	unsigned int slot = PCI_SLOT(devfn);
	u8 func = PCI_FUNC(devfn);
	uint32_t address_reg, data_reg;
	unsigned int address;

	address_reg = RALINK_PCI_CONFIG_ADDR;
	data_reg = RALINK_PCI_CONFIG_DATA_VIRTUAL_REG;

	address = (((where&0xF00)>>8)<<24) |(bus->number << 16) | (slot << 11) | (func << 8) | (where & 0xfc) | 0x80000000;
	MV_WRITE(address_reg, address);

	switch(access_type) {
	case PCI_ACCESS_WRITE_1:
		MV_WRITE_8(data_reg+(where&0x3), *data);
		break;
	case PCI_ACCESS_WRITE_2:
		MV_WRITE_16(data_reg+(where&0x3), *data);
		break;
	case PCI_ACCESS_WRITE_4:
		MV_WRITE(data_reg, *data);
		break;
	case PCI_ACCESS_READ_1:
		MV_READ_8( data_reg+(where&0x3), data);
		break;
	case PCI_ACCESS_READ_2:
		MV_READ_16(data_reg+(where&0x3), data);
		break;
	case PCI_ACCESS_READ_4:
		MV_READ(data_reg, data);
		break;
	default:
		printk("no specify access type\n");
		break;
	}
	return 0;
}

static int
read_config_byte(struct pci_bus *bus, unsigned int devfn, int where, u8 * val)
{
	return config_access(PCI_ACCESS_READ_1, bus, devfn, (unsigned int)where, (u32 *)val);
}

static int
read_config_word(struct pci_bus *bus, unsigned int devfn, int where, u16 * val)
{
	return config_access(PCI_ACCESS_READ_2, bus, devfn, (unsigned int)where, (u32 *)val);
}

static int
read_config_dword(struct pci_bus *bus, unsigned int devfn, int where, u32 * val)
{
	return config_access(PCI_ACCESS_READ_4, bus, devfn, (unsigned int)where, (u32 *)val);
}

static int
write_config_byte(struct pci_bus *bus, unsigned int devfn, int where, u8 val)
{
	if (config_access(PCI_ACCESS_WRITE_1, bus, devfn, (unsigned int)where, (u32 *)&val))
		return -1;

	return PCIBIOS_SUCCESSFUL;
}

static int
write_config_word(struct pci_bus *bus, unsigned int devfn, int where, u16 val)
{
	if (config_access(PCI_ACCESS_WRITE_2, bus, devfn, where, (u32 *)&val))
		return -1;

	return PCIBIOS_SUCCESSFUL;
}

static int
write_config_dword(struct pci_bus *bus, unsigned int devfn, int where, u32 val)
{
	if (config_access(PCI_ACCESS_WRITE_4, bus, devfn, where, &val))
		return -1;

	return PCIBIOS_SUCCESSFUL;
}

static int
pci_config_read(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 * val)
{
	switch (size) {
	case 1:
		return read_config_byte(bus, devfn, where, (u8 *) val);
	case 2:
		return read_config_word(bus, devfn, where, (u16 *) val);
	default:
		return read_config_dword(bus, devfn, where, val);
	}
}

static int
pci_config_write(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 val)
{
	switch (size) {
	case 1:
		return write_config_byte(bus, devfn, where, (u8) val);
	case 2:
		return write_config_word(bus, devfn, where, (u16) val);
	default:
		return write_config_dword(bus, devfn, where, val);
	}
}

struct pci_ops mt7621_pci_ops= {
	.read		= pci_config_read,
	.write		= pci_config_write,
};

static struct resource mt7621_res_pci_mem1;
static struct resource mt7621_res_pci_io1;
static struct pci_controller mt7621_controller = {
	.pci_ops	= &mt7621_pci_ops,
	.mem_resource	= &mt7621_res_pci_mem1,
	.io_resource	= &mt7621_res_pci_io1,
};

static void
read_config(unsigned long bus, unsigned long dev, unsigned long func, unsigned long reg, unsigned long *val)
{
	unsigned int address_reg, data_reg, address;

	address_reg = RALINK_PCI_CONFIG_ADDR;
	data_reg = RALINK_PCI_CONFIG_DATA_VIRTUAL_REG;
	address = (((reg & 0xF00)>>8)<<24) | (bus << 16) | (dev << 11) | (func << 8) | (reg & 0xfc) | 0x80000000 ;
	MV_WRITE(address_reg, address);
	MV_READ(data_reg, val);
	return;
}

static void
write_config(unsigned long bus, unsigned long dev, unsigned long func, unsigned long reg, unsigned long val)
{
	unsigned int address_reg, data_reg, address;

	address_reg = RALINK_PCI_CONFIG_ADDR;
	data_reg = RALINK_PCI_CONFIG_DATA_VIRTUAL_REG;
	address = (((reg & 0xF00)>>8)<<24) | (bus << 16) | (dev << 11) | (func << 8) | (reg & 0xfc) | 0x80000000 ;
	MV_WRITE(address_reg, address);
	MV_WRITE(data_reg, val);
	return;
}

int
pcibios_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	u16 cmd;
	u32 val;
	int irq;

	if (dev->bus->number == 0) {
		write_config(0, slot, 0, PCI_BASE_ADDRESS_0, MEMORY_BASE);
		read_config(0, slot, 0, PCI_BASE_ADDRESS_0, (unsigned long *)&val);
		printk("BAR0 at slot %d = %x\n", slot, val);
	}

	pci_write_config_byte(dev, PCI_CACHE_LINE_SIZE, 0x14);  //configure cache line size 0x14
	pci_write_config_byte(dev, PCI_LATENCY_TIMER, 0xFF);  //configure latency timer 0x10
	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	cmd = cmd | PCI_COMMAND_MASTER | PCI_COMMAND_IO | PCI_COMMAND_MEMORY;
	pci_write_config_word(dev, PCI_COMMAND, cmd);

	irq = of_irq_parse_and_map_pci(dev, slot, pin);

	pci_write_config_byte(dev, PCI_INTERRUPT_LINE, irq);
	return irq;
}

void
set_pcie_phy(u32 *addr, int start_b, int bits, int val)
{
//	printk("0x%p:", addr);
//	printk(" %x", *addr);
	*(unsigned int *)(addr) &= ~(((1<<bits) - 1)<<start_b);
	*(unsigned int *)(addr) |= val << start_b;
//	printk(" -> %x\n", *addr);
}

void
bypass_pipe_rst(void)
{
	/* PCIe Port 0 */
	set_pcie_phy((u32 *)(RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x02c), 12, 1, 0x01);	// rg_pe1_pipe_rst_b
	set_pcie_phy((u32 *)(RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x02c),  4, 1, 0x01);	// rg_pe1_pipe_cmd_frc[4]
	/* PCIe Port 1 */
	set_pcie_phy((u32 *)(RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x12c), 12, 1, 0x01);	// rg_pe1_pipe_rst_b
	set_pcie_phy((u32 *)(RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x12c),  4, 1, 0x01);	// rg_pe1_pipe_cmd_frc[4]
	/* PCIe Port 2 */
	set_pcie_phy((u32 *)(RALINK_PCIEPHY_P2_CTL_OFFSET + 0x02c), 12, 1, 0x01);	// rg_pe1_pipe_rst_b
	set_pcie_phy((u32 *)(RALINK_PCIEPHY_P2_CTL_OFFSET + 0x02c),  4, 1, 0x01);	// rg_pe1_pipe_cmd_frc[4]
}

void
set_phy_for_ssc(void)
{
	unsigned long reg = (*(volatile u32 *)(RALINK_SYSCTL_BASE + 0x10));

	reg = (reg >> 6) & 0x7;
	/* Set PCIe Port0 & Port1 PHY to disable SSC */
	/* Debug Xtal Type */
	set_pcie_phy((u32 *)(RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x400),  8, 1, 0x01);	// rg_pe1_frc_h_xtal_type
	set_pcie_phy((u32 *)(RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x400),  9, 2, 0x00);	// rg_pe1_h_xtal_type
	set_pcie_phy((u32 *)(RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x000),  4, 1, 0x01);	// rg_pe1_frc_phy_en	//Force Port 0 enable control
	set_pcie_phy((u32 *)(RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x100),  4, 1, 0x01);	// rg_pe1_frc_phy_en	//Force Port 1 enable control
	set_pcie_phy((u32 *)(RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x000),  5, 1, 0x00);	// rg_pe1_phy_en	//Port 0 disable
	set_pcie_phy((u32 *)(RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x100),  5, 1, 0x00);	// rg_pe1_phy_en	//Port 1 disable
	if(reg <= 5 && reg >= 3) {	// 40MHz Xtal
		set_pcie_phy((u32 *)(RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x490),  6, 2, 0x01);	// RG_PE1_H_PLL_PREDIV	//Pre-divider ratio (for host mode)
		printk("***** Xtal 40MHz *****\n");
	} else {			// 25MHz | 20MHz Xtal
		set_pcie_phy((u32 *)(RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x490),  6, 2, 0x00);	// RG_PE1_H_PLL_PREDIV	//Pre-divider ratio (for host mode)
		if (reg >= 6) {
			printk("***** Xtal 25MHz *****\n");
			set_pcie_phy((u32 *)(RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x4bc),  4, 2, 0x01);	// RG_PE1_H_PLL_FBKSEL	//Feedback clock select
			set_pcie_phy((u32 *)(RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x49c),  0,31, 0x18000000);	// RG_PE1_H_LCDDS_PCW_NCPO	//DDS NCPO PCW (for host mode)
			set_pcie_phy((u32 *)(RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x4a4),  0,16, 0x18d);	// RG_PE1_H_LCDDS_SSC_PRD	//DDS SSC dither period control
			set_pcie_phy((u32 *)(RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x4a8),  0,12, 0x4a);	// RG_PE1_H_LCDDS_SSC_DELTA	//DDS SSC dither amplitude control
			set_pcie_phy((u32 *)(RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x4a8), 16,12, 0x4a);	// RG_PE1_H_LCDDS_SSC_DELTA1	//DDS SSC dither amplitude control for initial
		} else {
			printk("***** Xtal 20MHz *****\n");
		}
	}
	set_pcie_phy((u32 *)(RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x4a0),  5, 1, 0x01);	// RG_PE1_LCDDS_CLK_PH_INV	//DDS clock inversion
	set_pcie_phy((u32 *)(RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x490), 22, 2, 0x02);	// RG_PE1_H_PLL_BC
	set_pcie_phy((u32 *)(RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x490), 18, 4, 0x06);	// RG_PE1_H_PLL_BP
	set_pcie_phy((u32 *)(RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x490), 12, 4, 0x02);	// RG_PE1_H_PLL_IR
	set_pcie_phy((u32 *)(RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x490),  8, 4, 0x01);	// RG_PE1_H_PLL_IC
	set_pcie_phy((u32 *)(RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x4ac), 16, 3, 0x00);	// RG_PE1_H_PLL_BR
	set_pcie_phy((u32 *)(RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x490),  1, 3, 0x02);	// RG_PE1_PLL_DIVEN
	if(reg <= 5 && reg >= 3) {	// 40MHz Xtal
		set_pcie_phy((u32 *)(RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x414),  6, 2, 0x01);	// rg_pe1_mstckdiv		//value of da_pe1_mstckdiv when force mode enable
		set_pcie_phy((u32 *)(RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x414),  5, 1, 0x01);	// rg_pe1_frc_mstckdiv	//force mode enable of da_pe1_mstckdiv
	}
	/* Enable PHY and disable force mode */
	set_pcie_phy((u32 *)(RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x000),  5, 1, 0x01);	// rg_pe1_phy_en	//Port 0 enable
	set_pcie_phy((u32 *)(RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x100),  5, 1, 0x01);	// rg_pe1_phy_en	//Port 1 enable
	set_pcie_phy((u32 *)(RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x000),  4, 1, 0x00);	// rg_pe1_frc_phy_en	//Force Port 0 disable control
	set_pcie_phy((u32 *)(RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x100),  4, 1, 0x00);	// rg_pe1_frc_phy_en	//Force Port 1 disable control

	/* Set PCIe Port2 PHY to disable SSC */
	/* Debug Xtal Type */
	set_pcie_phy((u32 *)(RALINK_PCIEPHY_P2_CTL_OFFSET + 0x400),  8, 1, 0x01);	// rg_pe1_frc_h_xtal_type
	set_pcie_phy((u32 *)(RALINK_PCIEPHY_P2_CTL_OFFSET + 0x400),  9, 2, 0x00);	// rg_pe1_h_xtal_type
	set_pcie_phy((u32 *)(RALINK_PCIEPHY_P2_CTL_OFFSET + 0x000),  4, 1, 0x01);	// rg_pe1_frc_phy_en	//Force Port 0 enable control
	set_pcie_phy((u32 *)(RALINK_PCIEPHY_P2_CTL_OFFSET + 0x000),  5, 1, 0x00);	// rg_pe1_phy_en	//Port 0 disable
	if(reg <= 5 && reg >= 3) {	// 40MHz Xtal
		set_pcie_phy((u32 *)(RALINK_PCIEPHY_P2_CTL_OFFSET + 0x490),  6, 2, 0x01);	// RG_PE1_H_PLL_PREDIV	//Pre-divider ratio (for host mode)
	} else {			// 25MHz | 20MHz Xtal
		set_pcie_phy((u32 *)(RALINK_PCIEPHY_P2_CTL_OFFSET + 0x490),  6, 2, 0x00);	// RG_PE1_H_PLL_PREDIV	//Pre-divider ratio (for host mode)
		if (reg >= 6) {		// 25MHz Xtal
			set_pcie_phy((u32 *)(RALINK_PCIEPHY_P2_CTL_OFFSET + 0x4bc),  4, 2, 0x01);	// RG_PE1_H_PLL_FBKSEL	//Feedback clock select
			set_pcie_phy((u32 *)(RALINK_PCIEPHY_P2_CTL_OFFSET + 0x49c),  0,31, 0x18000000);	// RG_PE1_H_LCDDS_PCW_NCPO	//DDS NCPO PCW (for host mode)
			set_pcie_phy((u32 *)(RALINK_PCIEPHY_P2_CTL_OFFSET + 0x4a4),  0,16, 0x18d);	// RG_PE1_H_LCDDS_SSC_PRD	//DDS SSC dither period control
			set_pcie_phy((u32 *)(RALINK_PCIEPHY_P2_CTL_OFFSET + 0x4a8),  0,12, 0x4a);	// RG_PE1_H_LCDDS_SSC_DELTA	//DDS SSC dither amplitude control
			set_pcie_phy((u32 *)(RALINK_PCIEPHY_P2_CTL_OFFSET + 0x4a8), 16,12, 0x4a);	// RG_PE1_H_LCDDS_SSC_DELTA1	//DDS SSC dither amplitude control for initial
		}
	}
	set_pcie_phy((u32 *)(RALINK_PCIEPHY_P2_CTL_OFFSET + 0x4a0),  5, 1, 0x01);	// RG_PE1_LCDDS_CLK_PH_INV	//DDS clock inversion
	set_pcie_phy((u32 *)(RALINK_PCIEPHY_P2_CTL_OFFSET + 0x490), 22, 2, 0x02);	// RG_PE1_H_PLL_BC
	set_pcie_phy((u32 *)(RALINK_PCIEPHY_P2_CTL_OFFSET + 0x490), 18, 4, 0x06);	// RG_PE1_H_PLL_BP
	set_pcie_phy((u32 *)(RALINK_PCIEPHY_P2_CTL_OFFSET + 0x490), 12, 4, 0x02);	// RG_PE1_H_PLL_IR
	set_pcie_phy((u32 *)(RALINK_PCIEPHY_P2_CTL_OFFSET + 0x490),  8, 4, 0x01);	// RG_PE1_H_PLL_IC
	set_pcie_phy((u32 *)(RALINK_PCIEPHY_P2_CTL_OFFSET + 0x4ac), 16, 3, 0x00);	// RG_PE1_H_PLL_BR
	set_pcie_phy((u32 *)(RALINK_PCIEPHY_P2_CTL_OFFSET + 0x490),  1, 3, 0x02);	// RG_PE1_PLL_DIVEN
	if(reg <= 5 && reg >= 3) {	// 40MHz Xtal
		set_pcie_phy((u32 *)(RALINK_PCIEPHY_P2_CTL_OFFSET + 0x414),  6, 2, 0x01);	// rg_pe1_mstckdiv		//value of da_pe1_mstckdiv when force mode enable
		set_pcie_phy((u32 *)(RALINK_PCIEPHY_P2_CTL_OFFSET + 0x414),  5, 1, 0x01);	// rg_pe1_frc_mstckdiv	//force mode enable of da_pe1_mstckdiv
	}
	/* Enable PHY and disable force mode */
	set_pcie_phy((u32 *)(RALINK_PCIEPHY_P2_CTL_OFFSET + 0x000),  5, 1, 0x01);	// rg_pe1_phy_en	//Port 0 enable
	set_pcie_phy((u32 *)(RALINK_PCIEPHY_P2_CTL_OFFSET + 0x000),  4, 1, 0x00);	// rg_pe1_frc_phy_en	//Force Port 0 disable control
}

void setup_cm_memory_region(struct resource *mem_resource)
{
	resource_size_t mask;
	if (mips_cps_numiocu(0)) {
		/* FIXME: hardware doesn't accept mask values with 1s after
		 * 0s (e.g. 0xffef), so it would be great to warn if that's
		 * about to happen */
		mask = ~(mem_resource->end - mem_resource->start);

		write_gcr_reg1_base(mem_resource->start);
		write_gcr_reg1_mask(mask | CM_GCR_REGn_MASK_CMTGT_IOCU0);
		printk("PCI coherence region base: 0x%08llx, mask/settings: 0x%08llx\n",
			(unsigned long long)read_gcr_reg1_base(),
			(unsigned long long)read_gcr_reg1_mask());
	}
}

static int mt7621_pci_probe(struct platform_device *pdev)
{
	unsigned long val = 0;

	iomem_resource.start = 0;
	iomem_resource.end= ~0;
	ioport_resource.start= 0;
	ioport_resource.end = ~0;

	val = RALINK_PCIE0_RST;
	val |= RALINK_PCIE1_RST;
	val |= RALINK_PCIE2_RST;

	ASSERT_SYSRST_PCIE(RALINK_PCIE0_RST | RALINK_PCIE1_RST | RALINK_PCIE2_RST);
	printk("pull PCIe RST: RALINK_RSTCTRL = %x\n", RALINK_RSTCTRL);

	*(unsigned int *)(0xbe000060) &= ~(0x3<<10 | 0x3<<3);
	*(unsigned int *)(0xbe000060) |= 0x1<<10 | 0x1<<3;
	mdelay(100);
	*(unsigned int *)(0xbe000600) |= 0x1<<19 | 0x1<<8 | 0x1<<7; // use GPIO19/GPIO8/GPIO7 (PERST_N/UART_RXD3/UART_TXD3)
	mdelay(100);
	*(unsigned int *)(0xbe000620) &= ~(0x1<<19 | 0x1<<8 | 0x1<<7);		// clear DATA

	mdelay(100);

	val = RALINK_PCIE0_RST;
	val |= RALINK_PCIE1_RST;
	val |= RALINK_PCIE2_RST;

	DEASSERT_SYSRST_PCIE(val);
	printk("release PCIe RST: RALINK_RSTCTRL = %x\n", RALINK_RSTCTRL);

	if ((*(unsigned int *)(0xbe00000c)&0xFFFF) == 0x0101) // MT7621 E2
		bypass_pipe_rst();
	set_phy_for_ssc();
	printk("release PCIe RST: RALINK_RSTCTRL = %x\n", RALINK_RSTCTRL);

	read_config(0, 0, 0, 0x70c, &val);
	printk("Port 0 N_FTS = %x\n", (unsigned int)val);

	read_config(0, 1, 0, 0x70c, &val);
	printk("Port 1 N_FTS = %x\n", (unsigned int)val);

	read_config(0, 2, 0, 0x70c, &val);
	printk("Port 2 N_FTS = %x\n", (unsigned int)val);

	RALINK_RSTCTRL = (RALINK_RSTCTRL | RALINK_PCIE_RST);
	RALINK_SYSCFG1 &= ~(0x30);
	RALINK_SYSCFG1 |= (2<<4);
	RALINK_PCIE_CLK_GEN &= 0x7fffffff;
	RALINK_PCIE_CLK_GEN1 &= 0x80ffffff;
	RALINK_PCIE_CLK_GEN1 |= 0xa << 24;
	RALINK_PCIE_CLK_GEN |= 0x80000000;
	mdelay(50);
	RALINK_RSTCTRL = (RALINK_RSTCTRL & ~RALINK_PCIE_RST);

	/* Use GPIO control instead of PERST_N */
	*(unsigned int *)(0xbe000620) |= 0x1<<19 | 0x1<<8 | 0x1<<7;		// set DATA
	mdelay(1000);

	if(( RALINK_PCI0_STATUS & 0x1) == 0)
	{
		printk("PCIE0 no card, disable it(RST&CLK)\n");
		ASSERT_SYSRST_PCIE(RALINK_PCIE0_RST);
		RALINK_CLKCFG1 = (RALINK_CLKCFG1 & ~RALINK_PCIE0_CLK_EN);
		pcie_link_status &= ~(1<<0);
	} else {
		pcie_link_status |= 1<<0;
		RALINK_PCI_PCIMSK_ADDR |= (1<<20); // enable pcie1 interrupt
	}

	if(( RALINK_PCI1_STATUS & 0x1) == 0)
	{
		printk("PCIE1 no card, disable it(RST&CLK)\n");
		ASSERT_SYSRST_PCIE(RALINK_PCIE1_RST);
		RALINK_CLKCFG1 = (RALINK_CLKCFG1 & ~RALINK_PCIE1_CLK_EN);
		pcie_link_status &= ~(1<<1);
	} else {
		pcie_link_status |= 1<<1;
		RALINK_PCI_PCIMSK_ADDR |= (1<<21); // enable pcie1 interrupt
	}

	if (( RALINK_PCI2_STATUS & 0x1) == 0) {
		printk("PCIE2 no card, disable it(RST&CLK)\n");
		ASSERT_SYSRST_PCIE(RALINK_PCIE2_RST);
		RALINK_CLKCFG1 = (RALINK_CLKCFG1 & ~RALINK_PCIE2_CLK_EN);
		pcie_link_status &= ~(1<<2);
	} else {
		pcie_link_status |= 1<<2;
		RALINK_PCI_PCIMSK_ADDR |= (1<<22); // enable pcie2 interrupt
	}

	if (pcie_link_status == 0)
		return 0;

/*
pcie(2/1/0) link status	pcie2_num	pcie1_num	pcie0_num
3'b000			x		x		x
3'b001			x		x		0
3'b010			x		0		x
3'b011			x		1		0
3'b100			0		x		x
3'b101			1		x		0
3'b110			1		0		x
3'b111			2		1		0
*/
	switch(pcie_link_status) {
	case 2:
		RALINK_PCI_PCICFG_ADDR &= ~0x00ff0000;
		RALINK_PCI_PCICFG_ADDR |= 0x1 << 16;	//port0
		RALINK_PCI_PCICFG_ADDR |= 0x0 << 20;	//port1
		break;
	case 4:
		RALINK_PCI_PCICFG_ADDR &= ~0x0fff0000;
		RALINK_PCI_PCICFG_ADDR |= 0x1 << 16;	//port0
		RALINK_PCI_PCICFG_ADDR |= 0x2 << 20;	//port1
		RALINK_PCI_PCICFG_ADDR |= 0x0 << 24;	//port2
		break;
	case 5:
		RALINK_PCI_PCICFG_ADDR &= ~0x0fff0000;
		RALINK_PCI_PCICFG_ADDR |= 0x0 << 16;	//port0
		RALINK_PCI_PCICFG_ADDR |= 0x2 << 20;	//port1
		RALINK_PCI_PCICFG_ADDR |= 0x1 << 24;	//port2
		break;
	case 6:
		RALINK_PCI_PCICFG_ADDR &= ~0x0fff0000;
		RALINK_PCI_PCICFG_ADDR |= 0x2 << 16;	//port0
		RALINK_PCI_PCICFG_ADDR |= 0x0 << 20;	//port1
		RALINK_PCI_PCICFG_ADDR |= 0x1 << 24;	//port2
		break;
	}
	printk(" -> %x\n", RALINK_PCI_PCICFG_ADDR);
	//printk(" RALINK_PCI_ARBCTL = %x\n", RALINK_PCI_ARBCTL);

/*
	ioport_resource.start = mt7621_res_pci_io1.start;
	ioport_resource.end = mt7621_res_pci_io1.end;
*/

	RALINK_PCI_MEMBASE = 0xffffffff; //RALINK_PCI_MM_MAP_BASE;
	RALINK_PCI_IOBASE = RALINK_PCI_IO_MAP_BASE;

	//PCIe0
	if((pcie_link_status & 0x1) != 0) {
		RALINK_PCI0_BAR0SETUP_ADDR = 0x7FFF0001;	//open 7FFF:2G; ENABLE
		RALINK_PCI0_IMBASEBAR0_ADDR = MEMORY_BASE;
		RALINK_PCI0_CLASS = 0x06040001;
		printk("PCIE0 enabled\n");
	}

	//PCIe1
	if ((pcie_link_status & 0x2) != 0) {
		RALINK_PCI1_BAR0SETUP_ADDR = 0x7FFF0001;	//open 7FFF:2G; ENABLE
		RALINK_PCI1_IMBASEBAR0_ADDR = MEMORY_BASE;
		RALINK_PCI1_CLASS = 0x06040001;
		printk("PCIE1 enabled\n");
	}

	//PCIe2
	if ((pcie_link_status & 0x4) != 0) {
		RALINK_PCI2_BAR0SETUP_ADDR = 0x7FFF0001;	//open 7FFF:2G; ENABLE
		RALINK_PCI2_IMBASEBAR0_ADDR = MEMORY_BASE;
		RALINK_PCI2_CLASS = 0x06040001;
		printk("PCIE2 enabled\n");
	}

	switch(pcie_link_status) {
	case 7:
		read_config(0, 2, 0, 0x4, &val);
		write_config(0, 2, 0, 0x4, val|0x4);
		// write_config(0, 1, 0, 0x4, val|0x7);
		read_config(0, 2, 0, 0x70c, &val);
		val &= ~(0xff)<<8;
		val |= 0x50<<8;
		write_config(0, 2, 0, 0x70c, val);
	case 3:
	case 5:
	case 6:
		read_config(0, 1, 0, 0x4, &val);
		write_config(0, 1, 0, 0x4, val|0x4);
		// write_config(0, 1, 0, 0x4, val|0x7);
		read_config(0, 1, 0, 0x70c, &val);
		val &= ~(0xff)<<8;
		val |= 0x50<<8;
		write_config(0, 1, 0, 0x70c, val);
	default:
		read_config(0, 0, 0, 0x4, &val);
		write_config(0, 0, 0, 0x4, val|0x4); //bus master enable
		// write_config(0, 0, 0, 0x4, val|0x7); //bus master enable
		read_config(0, 0, 0, 0x70c, &val);
		val &= ~(0xff)<<8;
		val |= 0x50<<8;
		write_config(0, 0, 0, 0x70c, val);
	}

	pci_load_of_ranges(&mt7621_controller, pdev->dev.of_node);
	setup_cm_memory_region(mt7621_controller.mem_resource);
	register_pci_controller(&mt7621_controller);
	return 0;

}

int pcibios_plat_dev_init(struct pci_dev *dev)
{
	return 0;
}

static const struct of_device_id mt7621_pci_ids[] = {
	{ .compatible = "mediatek,mt7621-pci" },
	{},
};
MODULE_DEVICE_TABLE(of, mt7621_pci_ids);

static struct platform_driver mt7621_pci_driver = {
	.probe = mt7621_pci_probe,
	.driver = {
		.name = "mt7621-pci",
		.of_match_table = of_match_ptr(mt7621_pci_ids),
	},
};

static int __init mt7621_pci_init(void)
{
	return platform_driver_register(&mt7621_pci_driver);
}

arch_initcall(mt7621_pci_init);
