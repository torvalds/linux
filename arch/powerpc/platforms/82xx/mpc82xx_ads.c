/*
 * MPC82xx_ads setup and early boot code plus other random bits.
 *
 * Author: Vitaly Bordug <vbordug@ru.mvista.com>
 * m82xx_restart fix by Wade Farnsworth <wfarnsworth@mvista.com>
 *
 * Copyright (c) 2006 MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/reboot.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/kdev_t.h>
#include <linux/major.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/seq_file.h>
#include <linux/root_dev.h>
#include <linux/initrd.h>
#include <linux/module.h>
#include <linux/fsl_devices.h>
#include <linux/fs_uart_pd.h>

#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/atomic.h>
#include <asm/time.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/bootinfo.h>
#include <asm/pci-bridge.h>
#include <asm/mpc8260.h>
#include <asm/irq.h>
#include <mm/mmu_decl.h>
#include <asm/prom.h>
#include <asm/cpm2.h>
#include <asm/udbg.h>
#include <asm/i8259.h>
#include <linux/fs_enet_pd.h>

#include <sysdev/fsl_soc.h>
#include <../sysdev/cpm2_pic.h>

#include "pq2ads_pd.h"

#ifdef CONFIG_PCI
static uint pci_clk_frq;
static struct {
	unsigned long *pci_int_stat_reg;
	unsigned long *pci_int_mask_reg;
} pci_regs;

static unsigned long pci_int_base;
static struct irq_host *pci_pic_host;
static struct device_node *pci_pic_node;
#endif

static void __init mpc82xx_ads_pic_init(void)
{
	struct device_node *np = of_find_compatible_node(NULL, "cpm-pic", "CPM2");
	struct resource r;
	cpm2_map_t *cpm_reg;

	if (np == NULL) {
		printk(KERN_ERR "PIC init: can not find cpm-pic node\n");
		return;
	}
	if (of_address_to_resource(np, 0, &r)) {
		printk(KERN_ERR "PIC init: invalid resource\n");
		of_node_put(np);
		return;
	}
	cpm2_pic_init(np);
	of_node_put(np);

	/* Initialize the default interrupt mapping priorities,
	 * in case the boot rom changed something on us.
	 */
	cpm_reg = (cpm2_map_t *) ioremap(get_immrbase(), sizeof(cpm2_map_t));
	cpm_reg->im_intctl.ic_siprr = 0x05309770;
	iounmap(cpm_reg);
#ifdef CONFIG_PCI
	/* Initialize stuff for the 82xx CPLD IC and install demux  */
	m82xx_pci_init_irq();
#endif
}

static void init_fcc1_ioports(struct fs_platform_info *fpi)
{
	struct io_port *io;
	u32 tempval;
	cpm2_map_t *immap = ioremap(get_immrbase(), sizeof(cpm2_map_t));
	struct device_node *np;
	struct resource r;
	u32 *bcsr;

	np = of_find_node_by_type(NULL, "memory");
	if (!np) {
		printk(KERN_INFO "No memory node in device tree\n");
		return;
	}
	if (of_address_to_resource(np, 1, &r)) {
		printk(KERN_INFO "No memory reg property [1] in devicetree\n");
		return;
	}
	of_node_put(np);
	bcsr = ioremap(r.start + 4, sizeof(u32));
	io = &immap->im_ioport;

	/* Enable the PHY */
	clrbits32(bcsr, BCSR1_FETHIEN);
	setbits32(bcsr, BCSR1_FETH_RST);

	/* FCC1 pins are on port A/C. */
	/* Configure port A and C pins for FCC1 Ethernet. */

	tempval = in_be32(&io->iop_pdira);
	tempval &= ~PA1_DIRA0;
	tempval |= PA1_DIRA1;
	out_be32(&io->iop_pdira, tempval);

	tempval = in_be32(&io->iop_psora);
	tempval &= ~PA1_PSORA0;
	tempval |= PA1_PSORA1;
	out_be32(&io->iop_psora, tempval);

	setbits32(&io->iop_ppara, PA1_DIRA0 | PA1_DIRA1);

	/* Alter clocks */
	tempval = PC_CLK(fpi->clk_tx - 8) | PC_CLK(fpi->clk_rx - 8);

	clrbits32(&io->iop_psorc, tempval);
	clrbits32(&io->iop_pdirc, tempval);
	setbits32(&io->iop_pparc, tempval);

	cpm2_clk_setup(CPM_CLK_FCC1, fpi->clk_rx, CPM_CLK_RX);
	cpm2_clk_setup(CPM_CLK_FCC1, fpi->clk_tx, CPM_CLK_TX);

	iounmap(bcsr);
	iounmap(immap);
}

static void init_fcc2_ioports(struct fs_platform_info *fpi)
{
	cpm2_map_t *immap = ioremap(get_immrbase(), sizeof(cpm2_map_t));
	struct device_node *np;
	struct resource r;
	u32 *bcsr;

	struct io_port *io;
	u32 tempval;

	np = of_find_node_by_type(NULL, "memory");
	if (!np) {
		printk(KERN_INFO "No memory node in device tree\n");
		return;
	}
	if (of_address_to_resource(np, 1, &r)) {
		printk(KERN_INFO "No memory reg property [1] in devicetree\n");
		return;
	}
	of_node_put(np);
	io = &immap->im_ioport;
	bcsr = ioremap(r.start + 12, sizeof(u32));

	/* Enable the PHY */
	clrbits32(bcsr, BCSR3_FETHIEN2);
	setbits32(bcsr, BCSR3_FETH2_RST);

	/* FCC2 are port B/C. */
	/* Configure port A and C pins for FCC2 Ethernet. */

	tempval = in_be32(&io->iop_pdirb);
	tempval &= ~PB2_DIRB0;
	tempval |= PB2_DIRB1;
	out_be32(&io->iop_pdirb, tempval);

	tempval = in_be32(&io->iop_psorb);
	tempval &= ~PB2_PSORB0;
	tempval |= PB2_PSORB1;
	out_be32(&io->iop_psorb, tempval);

	setbits32(&io->iop_pparb, PB2_DIRB0 | PB2_DIRB1);

	tempval = PC_CLK(fpi->clk_tx - 8) | PC_CLK(fpi->clk_rx - 8);

	/* Alter clocks */
	clrbits32(&io->iop_psorc, tempval);
	clrbits32(&io->iop_pdirc, tempval);
	setbits32(&io->iop_pparc, tempval);

	cpm2_clk_setup(CPM_CLK_FCC2, fpi->clk_rx, CPM_CLK_RX);
	cpm2_clk_setup(CPM_CLK_FCC2, fpi->clk_tx, CPM_CLK_TX);

	iounmap(bcsr);
	iounmap(immap);
}

void init_fcc_ioports(struct fs_platform_info *fpi)
{
	int fcc_no = fs_get_fcc_index(fpi->fs_no);

	switch (fcc_no) {
	case 0:
		init_fcc1_ioports(fpi);
		break;
	case 1:
		init_fcc2_ioports(fpi);
		break;
	default:
		printk(KERN_ERR "init_fcc_ioports: invalid FCC number\n");
		return;
	}
}

static void init_scc1_uart_ioports(struct fs_uart_platform_info *data)
{
	cpm2_map_t *immap = ioremap(get_immrbase(), sizeof(cpm2_map_t));

	/* SCC1 is only on port D */
	setbits32(&immap->im_ioport.iop_ppard, 0x00000003);
	clrbits32(&immap->im_ioport.iop_psord, 0x00000001);
	setbits32(&immap->im_ioport.iop_psord, 0x00000002);
	clrbits32(&immap->im_ioport.iop_pdird, 0x00000001);
	setbits32(&immap->im_ioport.iop_pdird, 0x00000002);

	clrbits32(&immap->im_cpmux.cmx_scr, (0x00000007 << (4 - data->clk_tx)));
	clrbits32(&immap->im_cpmux.cmx_scr, (0x00000038 << (4 - data->clk_rx)));
	setbits32(&immap->im_cpmux.cmx_scr,
		  ((data->clk_tx - 1) << (4 - data->clk_tx)));
	setbits32(&immap->im_cpmux.cmx_scr,
		  ((data->clk_rx - 1) << (4 - data->clk_rx)));

	iounmap(immap);
}

static void init_scc4_uart_ioports(struct fs_uart_platform_info *data)
{
	cpm2_map_t *immap = ioremap(get_immrbase(), sizeof(cpm2_map_t));

	setbits32(&immap->im_ioport.iop_ppard, 0x00000600);
	clrbits32(&immap->im_ioport.iop_psord, 0x00000600);
	clrbits32(&immap->im_ioport.iop_pdird, 0x00000200);
	setbits32(&immap->im_ioport.iop_pdird, 0x00000400);

	clrbits32(&immap->im_cpmux.cmx_scr, (0x00000007 << (4 - data->clk_tx)));
	clrbits32(&immap->im_cpmux.cmx_scr, (0x00000038 << (4 - data->clk_rx)));
	setbits32(&immap->im_cpmux.cmx_scr,
		  ((data->clk_tx - 1) << (4 - data->clk_tx)));
	setbits32(&immap->im_cpmux.cmx_scr,
		  ((data->clk_rx - 1) << (4 - data->clk_rx)));

	iounmap(immap);
}

void init_scc_ioports(struct fs_uart_platform_info *data)
{
	int scc_no = fs_get_scc_index(data->fs_no);

	switch (scc_no) {
	case 0:
		init_scc1_uart_ioports(data);
		data->brg = data->clk_rx;
		break;
	case 3:
		init_scc4_uart_ioports(data);
		data->brg = data->clk_rx;
		break;
	default:
		printk(KERN_ERR "init_scc_ioports: invalid SCC number\n");
		return;
	}
}

void __init m82xx_board_setup(void)
{
	cpm2_map_t *immap = ioremap(get_immrbase(), sizeof(cpm2_map_t));
	struct device_node *np;
	struct resource r;
	u32 *bcsr;

	np = of_find_node_by_type(NULL, "memory");
	if (!np) {
		printk(KERN_INFO "No memory node in device tree\n");
		return;
	}
	if (of_address_to_resource(np, 1, &r)) {
		printk(KERN_INFO "No memory reg property [1] in devicetree\n");
		return;
	}
	of_node_put(np);
	bcsr = ioremap(r.start + 4, sizeof(u32));
	/* Enable the 2nd UART port */
	clrbits32(bcsr, BCSR1_RS232_EN2);

#ifdef CONFIG_SERIAL_CPM_SCC1
	clrbits32((u32 *) & immap->im_scc[0].scc_sccm,
		  UART_SCCM_TX | UART_SCCM_RX);
	clrbits32((u32 *) & immap->im_scc[0].scc_gsmrl,
		  SCC_GSMRL_ENR | SCC_GSMRL_ENT);
#endif

#ifdef CONFIG_SERIAL_CPM_SCC2
	clrbits32((u32 *) & immap->im_scc[1].scc_sccm,
		  UART_SCCM_TX | UART_SCCM_RX);
	clrbits32((u32 *) & immap->im_scc[1].scc_gsmrl,
		  SCC_GSMRL_ENR | SCC_GSMRL_ENT);
#endif

#ifdef CONFIG_SERIAL_CPM_SCC3
	clrbits32((u32 *) & immap->im_scc[2].scc_sccm,
		  UART_SCCM_TX | UART_SCCM_RX);
	clrbits32((u32 *) & immap->im_scc[2].scc_gsmrl,
		  SCC_GSMRL_ENR | SCC_GSMRL_ENT);
#endif

#ifdef CONFIG_SERIAL_CPM_SCC4
	clrbits32((u32 *) & immap->im_scc[3].scc_sccm,
		  UART_SCCM_TX | UART_SCCM_RX);
	clrbits32((u32 *) & immap->im_scc[3].scc_gsmrl,
		  SCC_GSMRL_ENR | SCC_GSMRL_ENT);
#endif

	iounmap(bcsr);
	iounmap(immap);
}

#ifdef CONFIG_PCI
static void m82xx_pci_mask_irq(unsigned int irq)
{
	int bit = irq - pci_int_base;

	*pci_regs.pci_int_mask_reg |= (1 << (31 - bit));
	return;
}

static void m82xx_pci_unmask_irq(unsigned int irq)
{
	int bit = irq - pci_int_base;

	*pci_regs.pci_int_mask_reg &= ~(1 << (31 - bit));
	return;
}

static void m82xx_pci_mask_and_ack(unsigned int irq)
{
	int bit = irq - pci_int_base;

	*pci_regs.pci_int_mask_reg |= (1 << (31 - bit));
	return;
}

static void m82xx_pci_end_irq(unsigned int irq)
{
	int bit = irq - pci_int_base;

	*pci_regs.pci_int_mask_reg &= ~(1 << (31 - bit));
	return;
}

struct hw_interrupt_type m82xx_pci_ic = {
	.typename = "MPC82xx ADS PCI",
	.name = "MPC82xx ADS PCI",
	.enable = m82xx_pci_unmask_irq,
	.disable = m82xx_pci_mask_irq,
	.ack = m82xx_pci_mask_and_ack,
	.end = m82xx_pci_end_irq,
	.mask = m82xx_pci_mask_irq,
	.mask_ack = m82xx_pci_mask_and_ack,
	.unmask = m82xx_pci_unmask_irq,
	.eoi = m82xx_pci_end_irq,
};

static void
m82xx_pci_irq_demux(unsigned int irq, struct irq_desc *desc)
{
	unsigned long stat, mask, pend;
	int bit;

	for (;;) {
		stat = *pci_regs.pci_int_stat_reg;
		mask = *pci_regs.pci_int_mask_reg;
		pend = stat & ~mask & 0xf0000000;
		if (!pend)
			break;
		for (bit = 0; pend != 0; ++bit, pend <<= 1) {
			if (pend & 0x80000000)
				__do_IRQ(pci_int_base + bit);
		}
	}
}

static int pci_pic_host_match(struct irq_host *h, struct device_node *node)
{
	return node == pci_pic_node;
}

static int pci_pic_host_map(struct irq_host *h, unsigned int virq,
			    irq_hw_number_t hw)
{
	get_irq_desc(virq)->status |= IRQ_LEVEL;
	set_irq_chip(virq, &m82xx_pci_ic);
	return 0;
}

static void pci_host_unmap(struct irq_host *h, unsigned int virq)
{
	/* remove chip and handler */
	set_irq_chip(virq, NULL);
}

static struct irq_host_ops pci_pic_host_ops = {
	.match = pci_pic_host_match,
	.map = pci_pic_host_map,
	.unmap = pci_host_unmap,
};

void m82xx_pci_init_irq(void)
{
	int irq;
	cpm2_map_t *immap;
	struct device_node *np;
	struct resource r;
	const u32 *regs;
	unsigned int size;
	const u32 *irq_map;
	int i;
	unsigned int irq_max, irq_min;

	if ((np = of_find_node_by_type(NULL, "soc")) == NULL) {
		printk(KERN_INFO "No SOC node in device tree\n");
		return;
	}
	memset(&r, 0, sizeof(r));
	if (of_address_to_resource(np, 0, &r)) {
		printk(KERN_INFO "No SOC reg property in device tree\n");
		return;
	}
	immap = ioremap(r.start, sizeof(*immap));
	of_node_put(np);

	/* install the demultiplexer for the PCI cascade interrupt */
	np = of_find_node_by_type(NULL, "pci");
	if (!np) {
		printk(KERN_INFO "No pci node on device tree\n");
		iounmap(immap);
		return;
	}
	irq_map = get_property(np, "interrupt-map", &size);
	if ((!irq_map) || (size <= 7)) {
		printk(KERN_INFO "No interrupt-map property of pci node\n");
		iounmap(immap);
		return;
	}
	size /= sizeof(irq_map[0]);
	for (i = 0, irq_max = 0, irq_min = 512; i < size; i += 7, irq_map += 7) {
		if (irq_map[5] < irq_min)
			irq_min = irq_map[5];
		if (irq_map[5] > irq_max)
			irq_max = irq_map[5];
	}
	pci_int_base = irq_min;
	irq = irq_of_parse_and_map(np, 0);
	set_irq_chained_handler(irq, m82xx_pci_irq_demux);
	of_node_put(np);
	np = of_find_node_by_type(NULL, "pci-pic");
	if (!np) {
		printk(KERN_INFO "No pci pic node on device tree\n");
		iounmap(immap);
		return;
	}
	pci_pic_node = of_node_get(np);
	/* PCI interrupt controller registers: status and mask */
	regs = get_property(np, "reg", &size);
	if ((!regs) || (size <= 2)) {
		printk(KERN_INFO "No reg property in pci pic node\n");
		iounmap(immap);
		return;
	}
	pci_regs.pci_int_stat_reg =
	    ioremap(regs[0], sizeof(*pci_regs.pci_int_stat_reg));
	pci_regs.pci_int_mask_reg =
	    ioremap(regs[1], sizeof(*pci_regs.pci_int_mask_reg));
	of_node_put(np);
	/* configure chip select for PCI interrupt controller */
	immap->im_memctl.memc_br3 = regs[0] | 0x00001801;
	immap->im_memctl.memc_or3 = 0xffff8010;
	/* make PCI IRQ level sensitive */
	immap->im_intctl.ic_siexr &= ~(1 << (14 - (irq - SIU_INT_IRQ1)));

	/* mask all PCI interrupts */
	*pci_regs.pci_int_mask_reg |= 0xfff00000;
	iounmap(immap);
	pci_pic_host =
	    irq_alloc_host(IRQ_HOST_MAP_LINEAR, irq_max - irq_min + 1,
			   &pci_pic_host_ops, irq_max + 1);
	return;
}

static int m82xx_pci_exclude_device(u_char bus, u_char devfn)
{
	if (bus == 0 && PCI_SLOT(devfn) == 0)
		return PCIBIOS_DEVICE_NOT_FOUND;
	else
		return PCIBIOS_SUCCESSFUL;
}

void __init add_bridge(struct device_node *np)
{
	int len;
	struct pci_controller *hose;
	struct resource r;
	const int *bus_range;
	const void *ptr;

	memset(&r, 0, sizeof(r));
	if (of_address_to_resource(np, 0, &r)) {
		printk(KERN_INFO "No PCI reg property in device tree\n");
		return;
	}
	if (!(ptr = get_property(np, "clock-frequency", NULL))) {
		printk(KERN_INFO "No clock-frequency property in PCI node");
		return;
	}
	pci_clk_frq = *(uint *) ptr;
	of_node_put(np);
	bus_range = get_property(np, "bus-range", &len);
	if (bus_range == NULL || len < 2 * sizeof(int)) {
		printk(KERN_WARNING "Can't get bus-range for %s, assume"
		       " bus 0\n", np->full_name);
	}

	pci_assign_all_buses = 1;

	hose = pcibios_alloc_controller();

	if (!hose)
		return;

	hose->arch_data = np;
	hose->set_cfg_type = 1;

	hose->first_busno = bus_range ? bus_range[0] : 0;
	hose->last_busno = bus_range ? bus_range[1] : 0xff;
	hose->bus_offset = 0;

	hose->set_cfg_type = 1;

	setup_indirect_pci(hose,
			   r.start + offsetof(pci_cpm2_t, pci_cfg_addr),
			   r.start + offsetof(pci_cpm2_t, pci_cfg_data));

	pci_process_bridge_OF_ranges(hose, np, 1);
}
#endif

/*
 * Setup the architecture
 */
static void __init mpc82xx_ads_setup_arch(void)
{
#ifdef CONFIG_PCI
	struct device_node *np;
#endif

	if (ppc_md.progress)
		ppc_md.progress("mpc82xx_ads_setup_arch()", 0);
	cpm2_reset();

	/* Map I/O region to a 256MB BAT */

	m82xx_board_setup();

#ifdef CONFIG_PCI
	ppc_md.pci_exclude_device = m82xx_pci_exclude_device;
	for (np = NULL; (np = of_find_node_by_type(np, "pci")) != NULL;)
		add_bridge(np);

	of_node_put(np);
#endif

#ifdef  CONFIG_ROOT_NFS
	ROOT_DEV = Root_NFS;
#else
	ROOT_DEV = Root_HDA1;
#endif

	if (ppc_md.progress)
		ppc_md.progress("mpc82xx_ads_setup_arch(), finish", 0);
}

/*
 * Called very early, device-tree isn't unflattened
 */
static int __init mpc82xx_ads_probe(void)
{
	/* We always match for now, eventually we should look at
	 * the flat dev tree to ensure this is the board we are
	 * supposed to run on
	 */
	return 1;
}

#define RMR_CSRE 0x00000001
static void m82xx_restart(char *cmd)
{
	__volatile__ unsigned char dummy;

	local_irq_disable();
	((cpm2_map_t *) cpm2_immr)->im_clkrst.car_rmr |= RMR_CSRE;

	/* Clear the ME,EE,IR & DR bits in MSR to cause checkstop */
	mtmsr(mfmsr() & ~(MSR_ME | MSR_EE | MSR_IR | MSR_DR));
	dummy = ((cpm2_map_t *) cpm2_immr)->im_clkrst.res[0];
	printk("Restart failed\n");
	while (1) ;
}

static void m82xx_halt(void)
{
	local_irq_disable();
	while (1) ;
}

define_machine(mpc82xx_ads)
{
	.name = "MPC82xx ADS",
	.probe = mpc82xx_ads_probe,
	.setup_arch =    mpc82xx_ads_setup_arch,
	.init_IRQ =    mpc82xx_ads_pic_init,
	.show_cpuinfo =    mpc82xx_ads_show_cpuinfo,
	.get_irq =    cpm2_get_irq,
	.calibrate_decr =    m82xx_calibrate_decr,
	.restart = m82xx_restart,.halt = m82xx_halt,
};
