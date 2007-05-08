/* pci_fire.c: Sun4u platform PCI-E controller support.
 *
 * Copyright (C) 2007 David S. Miller (davem@davemloft.net)
 */
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/init.h>

#include <asm/oplib.h>
#include <asm/prom.h>

#include "pci_impl.h"

#define fire_read(__reg) \
({	u64 __ret; \
	__asm__ __volatile__("ldxa [%1] %2, %0" \
			     : "=r" (__ret) \
			     : "r" (__reg), "i" (ASI_PHYS_BYPASS_EC_E) \
			     : "memory"); \
	__ret; \
})
#define fire_write(__reg, __val) \
	__asm__ __volatile__("stxa %0, [%1] %2" \
			     : /* no outputs */ \
			     : "r" (__val), "r" (__reg), \
			       "i" (ASI_PHYS_BYPASS_EC_E) \
			     : "memory")

/* Fire config space address format is nearly identical to
 * that of SCHIZO and PSYCHO, except that in order to accomodate
 * PCI-E extended config space the encoding can handle 12 bits
 * of register address:
 *
 *  32     28 27 20 19    15 14      12 11  2  1 0
 * -------------------------------------------------
 * |0 0 0 0 0| bus | device | function | reg | 0 0 |
 * -------------------------------------------------
 */
#define FIRE_CONFIG_BASE(PBM)	((PBM)->config_space)
#define FIRE_CONFIG_ENCODE(BUS, DEVFN, REG)	\
	(((unsigned long)(BUS)   << 20) |	\
	 ((unsigned long)(DEVFN) << 12)  |	\
	 ((unsigned long)(REG)))

static void *fire_pci_config_mkaddr(struct pci_pbm_info *pbm,
				      unsigned char bus,
				      unsigned int devfn,
				      int where)
{
	if (!pbm)
		return NULL;
	return (void *)
		(FIRE_CONFIG_BASE(pbm) |
		 FIRE_CONFIG_ENCODE(bus, devfn, where));
}

/* FIRE PCI configuration space accessors. */

static int fire_read_pci_cfg(struct pci_bus *bus_dev, unsigned int devfn,
			     int where, int size, u32 *value)
{
	struct pci_pbm_info *pbm = bus_dev->sysdata;
	unsigned char bus = bus_dev->number;
	u32 *addr;
	u16 tmp16;
	u8 tmp8;

	if (bus_dev == pbm->pci_bus && devfn == 0x00)
		return pci_host_bridge_read_pci_cfg(bus_dev, devfn, where,
						    size, value);
	switch (size) {
	case 1:
		*value = 0xff;
		break;
	case 2:
		*value = 0xffff;
		break;
	case 4:
		*value = 0xffffffff;
		break;
	}

	addr = fire_pci_config_mkaddr(pbm, bus, devfn, where);
	if (!addr)
		return PCIBIOS_SUCCESSFUL;

	switch (size) {
	case 1:
		pci_config_read8((u8 *)addr, &tmp8);
		*value = tmp8;
		break;

	case 2:
		if (where & 0x01) {
			printk("pci_read_config_word: misaligned reg [%x]\n",
			       where);
			return PCIBIOS_SUCCESSFUL;
		}
		pci_config_read16((u16 *)addr, &tmp16);
		*value = tmp16;
		break;

	case 4:
		if (where & 0x03) {
			printk("pci_read_config_dword: misaligned reg [%x]\n",
			       where);
			return PCIBIOS_SUCCESSFUL;
		}

		pci_config_read32(addr, value);
		break;
	}
	return PCIBIOS_SUCCESSFUL;
}

static int fire_write_pci_cfg(struct pci_bus *bus_dev, unsigned int devfn,
			      int where, int size, u32 value)
{
	struct pci_pbm_info *pbm = bus_dev->sysdata;
	unsigned char bus = bus_dev->number;
	u32 *addr;

	if (bus_dev == pbm->pci_bus && devfn == 0x00)
		return pci_host_bridge_write_pci_cfg(bus_dev, devfn, where,
						     size, value);
	addr = fire_pci_config_mkaddr(pbm, bus, devfn, where);
	if (!addr)
		return PCIBIOS_SUCCESSFUL;

	switch (size) {
	case 1:
		pci_config_write8((u8 *)addr, value);
		break;

	case 2:
		if (where & 0x01) {
			printk("pci_write_config_word: misaligned reg [%x]\n",
			       where);
			return PCIBIOS_SUCCESSFUL;
		}
		pci_config_write16((u16 *)addr, value);
		break;

	case 4:
		if (where & 0x03) {
			printk("pci_write_config_dword: misaligned reg [%x]\n",
			       where);
			return PCIBIOS_SUCCESSFUL;
		}

		pci_config_write32(addr, value);
	}
	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops pci_fire_ops = {
	.read	=	fire_read_pci_cfg,
	.write	=	fire_write_pci_cfg,
};

static void pci_fire_scan_bus(struct pci_pbm_info *pbm)
{
	pbm->pci_bus = pci_scan_one_pbm(pbm);

	/* XXX register error interrupt handlers XXX */
}

#define FIRE_IOMMU_CONTROL	0x40000UL
#define FIRE_IOMMU_TSBBASE	0x40008UL
#define FIRE_IOMMU_FLUSH	0x40100UL
#define FIRE_IOMMU_FLUSHINV	0x40100UL

static void pci_fire_pbm_iommu_init(struct pci_pbm_info *pbm)
{
	struct iommu *iommu = pbm->iommu;
	u32 vdma[2], dma_mask;
	u64 control;
	int tsbsize;

	/* No virtual-dma property on these guys, use largest size.  */
	vdma[0] = 0xc0000000; /* base */
	vdma[1] = 0x40000000; /* size */
	dma_mask = 0xffffffff;
	tsbsize = 128;

	/* Register addresses. */
	iommu->iommu_control  = pbm->pbm_regs + FIRE_IOMMU_CONTROL;
	iommu->iommu_tsbbase  = pbm->pbm_regs + FIRE_IOMMU_TSBBASE;
	iommu->iommu_flush    = pbm->pbm_regs + FIRE_IOMMU_FLUSH;
	iommu->iommu_flushinv = pbm->pbm_regs + FIRE_IOMMU_FLUSHINV;

	/* We use the main control/status register of FIRE as the write
	 * completion register.
	 */
	iommu->write_complete_reg = pbm->controller_regs + 0x410000UL;

	/*
	 * Invalidate TLB Entries.
	 */
	fire_write(iommu->iommu_flushinv, ~(u64)0);

	pci_iommu_table_init(iommu, tsbsize * 8 * 1024, vdma[0], dma_mask);

	fire_write(iommu->iommu_tsbbase, __pa(iommu->page_table) | 0x7UL);

	control = fire_read(iommu->iommu_control);
	control |= (0x00000400 /* TSB cache snoop enable */	|
		    0x00000300 /* Cache mode */			|
		    0x00000002 /* Bypass enable */		|
		    0x00000001 /* Translation enable */);
	fire_write(iommu->iommu_control, control);
}

/* Based at pbm->controller_regs */
#define FIRE_PARITY_CONTROL	0x470010UL
#define  FIRE_PARITY_ENAB	0x8000000000000000UL
#define FIRE_FATAL_RESET_CTL	0x471028UL
#define  FIRE_FATAL_RESET_SPARE	0x0000000004000000UL
#define  FIRE_FATAL_RESET_MB	0x0000000002000000UL
#define  FIRE_FATAL_RESET_CPE	0x0000000000008000UL
#define  FIRE_FATAL_RESET_APE	0x0000000000004000UL
#define  FIRE_FATAL_RESET_PIO	0x0000000000000040UL
#define  FIRE_FATAL_RESET_JW	0x0000000000000004UL
#define  FIRE_FATAL_RESET_JI	0x0000000000000002UL
#define  FIRE_FATAL_RESET_JR	0x0000000000000001UL
#define FIRE_CORE_INTR_ENABLE	0x471800UL

/* Based at pbm->pbm_regs */
#define FIRE_TLU_CTRL		0x80000UL
#define  FIRE_TLU_CTRL_TIM	0x00000000da000000UL
#define  FIRE_TLU_CTRL_QDET	0x0000000000000100UL
#define  FIRE_TLU_CTRL_CFG	0x0000000000000001UL
#define FIRE_TLU_DEV_CTRL	0x90008UL
#define FIRE_TLU_LINK_CTRL	0x90020UL
#define FIRE_TLU_LINK_CTRL_CLK	0x0000000000000040UL
#define FIRE_LPU_RESET		0xe2008UL
#define FIRE_LPU_LLCFG		0xe2200UL
#define  FIRE_LPU_LLCFG_VC0	0x0000000000000100UL
#define FIRE_LPU_FCTRL_UCTRL	0xe2240UL
#define  FIRE_LPU_FCTRL_UCTRL_N	0x0000000000000002UL
#define  FIRE_LPU_FCTRL_UCTRL_P	0x0000000000000001UL
#define FIRE_LPU_TXL_FIFOP	0xe2430UL
#define FIRE_LPU_LTSSM_CFG2	0xe2788UL
#define FIRE_LPU_LTSSM_CFG3	0xe2790UL
#define FIRE_LPU_LTSSM_CFG4	0xe2798UL
#define FIRE_LPU_LTSSM_CFG5	0xe27a0UL
#define FIRE_DMC_IENAB		0x31800UL
#define FIRE_DMC_DBG_SEL_A	0x53000UL
#define FIRE_DMC_DBG_SEL_B	0x53008UL
#define FIRE_PEC_IENAB		0x51800UL

static void pci_fire_hw_init(struct pci_pbm_info *pbm)
{
	u64 val;

	fire_write(pbm->controller_regs + FIRE_PARITY_CONTROL,
		   FIRE_PARITY_ENAB);

	fire_write(pbm->controller_regs + FIRE_FATAL_RESET_CTL,
		   (FIRE_FATAL_RESET_SPARE |
		    FIRE_FATAL_RESET_MB |
		    FIRE_FATAL_RESET_CPE |
		    FIRE_FATAL_RESET_APE |
		    FIRE_FATAL_RESET_PIO |
		    FIRE_FATAL_RESET_JW |
		    FIRE_FATAL_RESET_JI |
		    FIRE_FATAL_RESET_JR));

	fire_write(pbm->controller_regs + FIRE_CORE_INTR_ENABLE, ~(u64)0);

	val = fire_read(pbm->pbm_regs + FIRE_TLU_CTRL);
	val |= (FIRE_TLU_CTRL_TIM |
		FIRE_TLU_CTRL_QDET |
		FIRE_TLU_CTRL_CFG);
	fire_write(pbm->pbm_regs + FIRE_TLU_CTRL, val);
	fire_write(pbm->pbm_regs + FIRE_TLU_DEV_CTRL, 0);
	fire_write(pbm->pbm_regs + FIRE_TLU_LINK_CTRL,
		   FIRE_TLU_LINK_CTRL_CLK);

	fire_write(pbm->pbm_regs + FIRE_LPU_RESET, 0);
	fire_write(pbm->pbm_regs + FIRE_LPU_LLCFG,
		   FIRE_LPU_LLCFG_VC0);
	fire_write(pbm->pbm_regs + FIRE_LPU_FCTRL_UCTRL,
		   (FIRE_LPU_FCTRL_UCTRL_N |
		    FIRE_LPU_FCTRL_UCTRL_P));
	fire_write(pbm->pbm_regs + FIRE_LPU_TXL_FIFOP,
		   ((0xffff << 16) | (0x0000 << 0)));
	fire_write(pbm->pbm_regs + FIRE_LPU_LTSSM_CFG2, 3000000);
	fire_write(pbm->pbm_regs + FIRE_LPU_LTSSM_CFG3, 500000);
	fire_write(pbm->pbm_regs + FIRE_LPU_LTSSM_CFG4,
		   (2 << 16) | (140 << 8));
	fire_write(pbm->pbm_regs + FIRE_LPU_LTSSM_CFG5, 0);

	fire_write(pbm->pbm_regs + FIRE_DMC_IENAB, ~(u64)0);
	fire_write(pbm->pbm_regs + FIRE_DMC_DBG_SEL_A, 0);
	fire_write(pbm->pbm_regs + FIRE_DMC_DBG_SEL_B, 0);

	fire_write(pbm->pbm_regs + FIRE_PEC_IENAB, ~(u64)0);
}

static void pci_fire_pbm_init(struct pci_controller_info *p,
			      struct device_node *dp, u32 portid)
{
	const struct linux_prom64_registers *regs;
	struct pci_pbm_info *pbm;

	if ((portid & 1) == 0)
		pbm = &p->pbm_A;
	else
		pbm = &p->pbm_B;

	pbm->next = pci_pbm_root;
	pci_pbm_root = pbm;

	pbm->scan_bus = pci_fire_scan_bus;
	pbm->pci_ops = &pci_fire_ops;

	pbm->index = pci_num_pbms++;

	pbm->portid = portid;
	pbm->parent = p;
	pbm->prom_node = dp;
	pbm->name = dp->full_name;

	regs = of_get_property(dp, "reg", NULL);
	pbm->pbm_regs = regs[0].phys_addr;
	pbm->controller_regs = regs[1].phys_addr - 0x410000UL;

	printk("%s: SUN4U PCIE Bus Module\n", pbm->name);

	pci_determine_mem_io_space(pbm);

	pci_get_pbm_props(pbm);

	pci_fire_hw_init(pbm);
	pci_fire_pbm_iommu_init(pbm);
}

static inline int portid_compare(u32 x, u32 y)
{
	if (x == (y ^ 1))
		return 1;
	return 0;
}

void fire_pci_init(struct device_node *dp, const char *model_name)
{
	struct pci_controller_info *p;
	u32 portid = of_getintprop_default(dp, "portid", 0xff);
	struct iommu *iommu;
	struct pci_pbm_info *pbm;

	for (pbm = pci_pbm_root; pbm; pbm = pbm->next) {
		if (portid_compare(pbm->portid, portid)) {
			pci_fire_pbm_init(pbm->parent, dp, portid);
			return;
		}
	}

	p = kzalloc(sizeof(struct pci_controller_info), GFP_ATOMIC);
	if (!p)
		goto fatal_memory_error;

	iommu = kzalloc(sizeof(struct iommu), GFP_ATOMIC);
	if (!iommu)
		goto fatal_memory_error;

	p->pbm_A.iommu = iommu;

	iommu = kzalloc(sizeof(struct iommu), GFP_ATOMIC);
	if (!iommu)
		goto fatal_memory_error;

	p->pbm_B.iommu = iommu;

	/* XXX MSI support XXX */

	/* Like PSYCHO and SCHIZO we have a 2GB aligned area
	 * for memory space.
	 */
	pci_memspace_mask = 0x7fffffffUL;

	pci_fire_pbm_init(p, dp, portid);
	return;

fatal_memory_error:
	prom_printf("PCI_FIRE: Fatal memory allocation error.\n");
	prom_halt();
}
