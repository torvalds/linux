/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2008 Maxime Bizon <mbizon@freebox.fr>
 */

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <asm/bootinfo.h>

#include <bcm63xx_reset.h>

#include "pci-bcm63xx.h"

/*
 * Allow PCI to be disabled at runtime depending on board nvram
 * configuration
 */
int bcm63xx_pci_enabled;

static struct resource bcm_pci_mem_resource = {
	.name	= "bcm63xx PCI memory space",
	.start	= BCM_PCI_MEM_BASE_PA,
	.end	= BCM_PCI_MEM_END_PA,
	.flags	= IORESOURCE_MEM
};

static struct resource bcm_pci_io_resource = {
	.name	= "bcm63xx PCI IO space",
	.start	= BCM_PCI_IO_BASE_PA,
#ifdef CONFIG_CARDBUS
	.end	= BCM_PCI_IO_HALF_PA,
#else
	.end	= BCM_PCI_IO_END_PA,
#endif
	.flags	= IORESOURCE_IO
};

struct pci_controller bcm63xx_controller = {
	.pci_ops	= &bcm63xx_pci_ops,
	.io_resource	= &bcm_pci_io_resource,
	.mem_resource	= &bcm_pci_mem_resource,
};

/*
 * We handle cardbus  via a fake Cardbus bridge,  memory and io spaces
 * have to be  clearly separated from PCI one  since we have different
 * memory decoder.
 */
#ifdef CONFIG_CARDBUS
static struct resource bcm_cb_mem_resource = {
	.name	= "bcm63xx Cardbus memory space",
	.start	= BCM_CB_MEM_BASE_PA,
	.end	= BCM_CB_MEM_END_PA,
	.flags	= IORESOURCE_MEM
};

static struct resource bcm_cb_io_resource = {
	.name	= "bcm63xx Cardbus IO space",
	.start	= BCM_PCI_IO_HALF_PA + 1,
	.end	= BCM_PCI_IO_END_PA,
	.flags	= IORESOURCE_IO
};

struct pci_controller bcm63xx_cb_controller = {
	.pci_ops	= &bcm63xx_cb_ops,
	.io_resource	= &bcm_cb_io_resource,
	.mem_resource	= &bcm_cb_mem_resource,
};
#endif

static struct resource bcm_pcie_mem_resource = {
	.name	= "bcm63xx PCIe memory space",
	.start	= BCM_PCIE_MEM_BASE_PA,
	.end	= BCM_PCIE_MEM_END_PA,
	.flags	= IORESOURCE_MEM,
};

static struct resource bcm_pcie_io_resource = {
	.name	= "bcm63xx PCIe IO space",
	.start	= 0,
	.end	= 0,
	.flags	= 0,
};

struct pci_controller bcm63xx_pcie_controller = {
	.pci_ops	= &bcm63xx_pcie_ops,
	.io_resource	= &bcm_pcie_io_resource,
	.mem_resource	= &bcm_pcie_mem_resource,
};

static u32 bcm63xx_int_cfg_readl(u32 reg)
{
	u32 tmp;

	tmp = reg & MPI_PCICFGCTL_CFGADDR_MASK;
	tmp |= MPI_PCICFGCTL_WRITEEN_MASK;
	bcm_mpi_writel(tmp, MPI_PCICFGCTL_REG);
	iob();
	return bcm_mpi_readl(MPI_PCICFGDATA_REG);
}

static void bcm63xx_int_cfg_writel(u32 val, u32 reg)
{
	u32 tmp;

	tmp = reg & MPI_PCICFGCTL_CFGADDR_MASK;
	tmp |=	MPI_PCICFGCTL_WRITEEN_MASK;
	bcm_mpi_writel(tmp, MPI_PCICFGCTL_REG);
	bcm_mpi_writel(val, MPI_PCICFGDATA_REG);
}

void __iomem *pci_iospace_start;

static void __init bcm63xx_reset_pcie(void)
{
	u32 val;
	u32 reg;

	/* enable SERDES */
	if (BCMCPU_IS_6328())
		reg = MISC_SERDES_CTRL_6328_REG;
	else
		reg = MISC_SERDES_CTRL_6362_REG;

	val = bcm_misc_readl(reg);
	val |= SERDES_PCIE_EN | SERDES_PCIE_EXD_EN;
	bcm_misc_writel(val, reg);

	/* reset the PCIe core */
	bcm63xx_core_set_reset(BCM63XX_RESET_PCIE, 1);
	bcm63xx_core_set_reset(BCM63XX_RESET_PCIE_EXT, 1);
	mdelay(10);

	bcm63xx_core_set_reset(BCM63XX_RESET_PCIE, 0);
	mdelay(10);

	bcm63xx_core_set_reset(BCM63XX_RESET_PCIE_EXT, 0);
	mdelay(200);
}

static struct clk *pcie_clk;

static int __init bcm63xx_register_pcie(void)
{
	u32 val;

	/* enable clock */
	pcie_clk = clk_get(NULL, "pcie");
	if (IS_ERR_OR_NULL(pcie_clk))
		return -ENODEV;

	clk_prepare_enable(pcie_clk);

	bcm63xx_reset_pcie();

	/* configure the PCIe bridge */
	val = bcm_pcie_readl(PCIE_BRIDGE_OPT1_REG);
	val |= OPT1_RD_BE_OPT_EN;
	val |= OPT1_RD_REPLY_BE_FIX_EN;
	val |= OPT1_PCIE_BRIDGE_HOLE_DET_EN;
	val |= OPT1_L1_INT_STATUS_MASK_POL;
	bcm_pcie_writel(val, PCIE_BRIDGE_OPT1_REG);

	/* setup the interrupts */
	val = bcm_pcie_readl(PCIE_BRIDGE_RC_INT_MASK_REG);
	val |= PCIE_RC_INT_A | PCIE_RC_INT_B | PCIE_RC_INT_C | PCIE_RC_INT_D;
	bcm_pcie_writel(val, PCIE_BRIDGE_RC_INT_MASK_REG);

	val = bcm_pcie_readl(PCIE_BRIDGE_OPT2_REG);
	/* enable credit checking and error checking */
	val |= OPT2_TX_CREDIT_CHK_EN;
	val |= OPT2_UBUS_UR_DECODE_DIS;

	/* set device bus/func for the pcie device */
	val |= (PCIE_BUS_DEVICE << OPT2_CFG_TYPE1_BUS_NO_SHIFT);
	val |= OPT2_CFG_TYPE1_BD_SEL;
	bcm_pcie_writel(val, PCIE_BRIDGE_OPT2_REG);

	/* setup class code as bridge */
	val = bcm_pcie_readl(PCIE_IDVAL3_REG);
	val &= ~IDVAL3_CLASS_CODE_MASK;
	val |= (PCI_CLASS_BRIDGE_PCI << IDVAL3_SUBCLASS_SHIFT);
	bcm_pcie_writel(val, PCIE_IDVAL3_REG);

	/* disable bar1 size */
	val = bcm_pcie_readl(PCIE_CONFIG2_REG);
	val &= ~CONFIG2_BAR1_SIZE_MASK;
	bcm_pcie_writel(val, PCIE_CONFIG2_REG);

	/* set bar0 to little endian */
	val = (BCM_PCIE_MEM_BASE_PA >> 20) << BASEMASK_BASE_SHIFT;
	val |= (BCM_PCIE_MEM_BASE_PA >> 20) << BASEMASK_MASK_SHIFT;
	val |= BASEMASK_REMAP_EN;
	bcm_pcie_writel(val, PCIE_BRIDGE_BAR0_BASEMASK_REG);

	val = (BCM_PCIE_MEM_BASE_PA >> 20) << REBASE_ADDR_BASE_SHIFT;
	bcm_pcie_writel(val, PCIE_BRIDGE_BAR0_REBASE_ADDR_REG);

	register_pci_controller(&bcm63xx_pcie_controller);

	return 0;
}

static int __init bcm63xx_register_pci(void)
{
	unsigned int mem_size;
	u32 val;
	/*
	 * configuration  access are  done through  IO space,  remap 4
	 * first bytes to access it from CPU.
	 *
	 * this means that  no io access from CPU  should happen while
	 * we do a configuration cycle,	 but there's no way we can add
	 * a spinlock for each io access, so this is currently kind of
	 * broken on SMP.
	 */
	pci_iospace_start = ioremap(BCM_PCI_IO_BASE_PA, 4);
	if (!pci_iospace_start)
		return -ENOMEM;

	/* setup local bus to PCI access (PCI memory) */
	val = BCM_PCI_MEM_BASE_PA & MPI_L2P_BASE_MASK;
	bcm_mpi_writel(val, MPI_L2PMEMBASE1_REG);
	bcm_mpi_writel(~(BCM_PCI_MEM_SIZE - 1), MPI_L2PMEMRANGE1_REG);
	bcm_mpi_writel(val | MPI_L2PREMAP_ENABLED_MASK, MPI_L2PMEMREMAP1_REG);

	/* set Cardbus IDSEL (type 0 cfg access on primary bus for
	 * this IDSEL will be done on Cardbus instead) */
	val = bcm_pcmcia_readl(PCMCIA_C1_REG);
	val &= ~PCMCIA_C1_CBIDSEL_MASK;
	val |= (CARDBUS_PCI_IDSEL << PCMCIA_C1_CBIDSEL_SHIFT);
	bcm_pcmcia_writel(val, PCMCIA_C1_REG);

#ifdef CONFIG_CARDBUS
	/* setup local bus to PCI access (Cardbus memory) */
	val = BCM_CB_MEM_BASE_PA & MPI_L2P_BASE_MASK;
	bcm_mpi_writel(val, MPI_L2PMEMBASE2_REG);
	bcm_mpi_writel(~(BCM_CB_MEM_SIZE - 1), MPI_L2PMEMRANGE2_REG);
	val |= MPI_L2PREMAP_ENABLED_MASK | MPI_L2PREMAP_IS_CARDBUS_MASK;
	bcm_mpi_writel(val, MPI_L2PMEMREMAP2_REG);
#else
	/* disable second access windows */
	bcm_mpi_writel(0, MPI_L2PMEMREMAP2_REG);
#endif

	/* setup local bus  to PCI access (IO memory),	we have only 1
	 * IO window  for both PCI  and cardbus, but it	 cannot handle
	 * both	 at the	 same time,  assume standard  PCI for  now, if
	 * cardbus card has  IO zone, PCI fixup will  change window to
	 * cardbus */
	val = BCM_PCI_IO_BASE_PA & MPI_L2P_BASE_MASK;
	bcm_mpi_writel(val, MPI_L2PIOBASE_REG);
	bcm_mpi_writel(~(BCM_PCI_IO_SIZE - 1), MPI_L2PIORANGE_REG);
	bcm_mpi_writel(val | MPI_L2PREMAP_ENABLED_MASK, MPI_L2PIOREMAP_REG);

	/* enable PCI related GPIO pins */
	bcm_mpi_writel(MPI_LOCBUSCTL_EN_PCI_GPIO_MASK, MPI_LOCBUSCTL_REG);

	/* setup PCI to local bus access, used by PCI device to target
	 * local RAM while bus mastering */
	bcm63xx_int_cfg_writel(0, PCI_BASE_ADDRESS_3);
	if (BCMCPU_IS_3368() || BCMCPU_IS_6358() || BCMCPU_IS_6368())
		val = MPI_SP0_REMAP_ENABLE_MASK;
	else
		val = 0;
	bcm_mpi_writel(val, MPI_SP0_REMAP_REG);

	bcm63xx_int_cfg_writel(0x0, PCI_BASE_ADDRESS_4);
	bcm_mpi_writel(0, MPI_SP1_REMAP_REG);

	mem_size = bcm63xx_get_memory_size();

	/* 6348 before rev b0 exposes only 16 MB of RAM memory through
	 * PCI, throw a warning if we have more memory */
	if (BCMCPU_IS_6348() && (bcm63xx_get_cpu_rev() & 0xf0) == 0xa0) {
		if (mem_size > (16 * 1024 * 1024))
			printk(KERN_WARNING "bcm63xx: this CPU "
			       "revision cannot handle more than 16MB "
			       "of RAM for PCI bus mastering\n");
	} else {
		/* setup sp0 range to local RAM size */
		bcm_mpi_writel(~(mem_size - 1), MPI_SP0_RANGE_REG);
		bcm_mpi_writel(0, MPI_SP1_RANGE_REG);
	}

	/* change  host bridge	retry  counter to  infinite number  of
	 * retry,  needed for  some broadcom  wifi cards  with Silicon
	 * Backplane bus where access to srom seems very slow  */
	val = bcm63xx_int_cfg_readl(BCMPCI_REG_TIMERS);
	val &= ~REG_TIMER_RETRY_MASK;
	bcm63xx_int_cfg_writel(val, BCMPCI_REG_TIMERS);

	/* enable memory decoder and bus mastering */
	val = bcm63xx_int_cfg_readl(PCI_COMMAND);
	val |= (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
	bcm63xx_int_cfg_writel(val, PCI_COMMAND);

	/* enable read prefetching & disable byte swapping for bus
	 * mastering transfers */
	val = bcm_mpi_readl(MPI_PCIMODESEL_REG);
	val &= ~MPI_PCIMODESEL_BAR1_NOSWAP_MASK;
	val &= ~MPI_PCIMODESEL_BAR2_NOSWAP_MASK;
	val &= ~MPI_PCIMODESEL_PREFETCH_MASK;
	val |= (8 << MPI_PCIMODESEL_PREFETCH_SHIFT);
	bcm_mpi_writel(val, MPI_PCIMODESEL_REG);

	/* enable pci interrupt */
	val = bcm_mpi_readl(MPI_LOCINT_REG);
	val |= MPI_LOCINT_MASK(MPI_LOCINT_EXT_PCI_INT);
	bcm_mpi_writel(val, MPI_LOCINT_REG);

	register_pci_controller(&bcm63xx_controller);

#ifdef CONFIG_CARDBUS
	register_pci_controller(&bcm63xx_cb_controller);
#endif

	/* mark memory space used for IO mapping as reserved */
	request_mem_region(BCM_PCI_IO_BASE_PA, BCM_PCI_IO_SIZE,
			   "bcm63xx PCI IO space");
	return 0;
}


static int __init bcm63xx_pci_init(void)
{
	if (!bcm63xx_pci_enabled)
		return -ENODEV;

	switch (bcm63xx_get_cpu_id()) {
	case BCM6328_CPU_ID:
	case BCM6362_CPU_ID:
		return bcm63xx_register_pcie();
	case BCM3368_CPU_ID:
	case BCM6348_CPU_ID:
	case BCM6358_CPU_ID:
	case BCM6368_CPU_ID:
		return bcm63xx_register_pci();
	default:
		return -ENODEV;
	}
}

arch_initcall(bcm63xx_pci_init);
