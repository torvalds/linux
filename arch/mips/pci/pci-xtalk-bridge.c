// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2003 Christoph Hellwig (hch@lst.de)
 * Copyright (C) 1999, 2000, 04 Ralf Baechle (ralf@linux-mips.org)
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/pci.h>
#include <linux/smp.h>
#include <linux/dma-direct.h>
#include <linux/platform_device.h>
#include <linux/platform_data/xtalk-bridge.h>
#include <linux/nvmem-consumer.h>
#include <linux/crc16.h>
#include <linux/irqdomain.h>

#include <asm/pci/bridge.h>
#include <asm/paccess.h>
#include <asm/sn/irq_alloc.h>
#include <asm/sn/ioc3.h>

#define CRC16_INIT	0
#define CRC16_VALID	0xb001

/*
 * Common phys<->dma mapping for platforms using pci xtalk bridge
 */
dma_addr_t phys_to_dma(struct device *dev, phys_addr_t paddr)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct bridge_controller *bc = BRIDGE_CONTROLLER(pdev->bus);

	return bc->baddr + paddr;
}

phys_addr_t dma_to_phys(struct device *dev, dma_addr_t dma_addr)
{
	return dma_addr & ~(0xffUL << 56);
}

/*
 * Most of the IOC3 PCI config register aren't present
 * we emulate what is needed for a normal PCI enumeration
 */
static int ioc3_cfg_rd(void *addr, int where, int size, u32 *value, u32 sid)
{
	u32 cf, shift, mask;

	switch (where & ~3) {
	case 0x00 ... 0x10:
	case 0x40 ... 0x44:
		if (get_dbe(cf, (u32 *)addr))
			return PCIBIOS_DEVICE_NOT_FOUND;
		break;
	case 0x2c:
		cf = sid;
		break;
	case 0x3c:
		/* emulate sane interrupt pin value */
		cf = 0x00000100;
		break;
	default:
		cf = 0;
		break;
	}
	shift = (where & 3) << 3;
	mask = 0xffffffffU >> ((4 - size) << 3);
	*value = (cf >> shift) & mask;

	return PCIBIOS_SUCCESSFUL;
}

static int ioc3_cfg_wr(void *addr, int where, int size, u32 value)
{
	u32 cf, shift, mask, smask;

	if ((where >= 0x14 && where < 0x40) || (where >= 0x48))
		return PCIBIOS_SUCCESSFUL;

	if (get_dbe(cf, (u32 *)addr))
		return PCIBIOS_DEVICE_NOT_FOUND;

	shift = ((where & 3) << 3);
	mask = (0xffffffffU >> ((4 - size) << 3));
	smask = mask << shift;

	cf = (cf & ~smask) | ((value & mask) << shift);
	if (put_dbe(cf, (u32 *)addr))
		return PCIBIOS_DEVICE_NOT_FOUND;

	return PCIBIOS_SUCCESSFUL;
}

static void bridge_disable_swapping(struct pci_dev *dev)
{
	struct bridge_controller *bc = BRIDGE_CONTROLLER(dev->bus);
	int slot = PCI_SLOT(dev->devfn);

	/* Turn off byte swapping */
	bridge_clr(bc, b_device[slot].reg, BRIDGE_DEV_SWAP_DIR);
	bridge_read(bc, b_widget.w_tflush);	/* Flush */
}

DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_SGI, PCI_DEVICE_ID_SGI_IOC3,
	bridge_disable_swapping);


/*
 * The Bridge ASIC supports both type 0 and type 1 access.  Type 1 is
 * not really documented, so right now I can't write code which uses it.
 * Therefore we use type 0 accesses for now even though they won't work
 * correctly for PCI-to-PCI bridges.
 *
 * The function is complicated by the ultimate brokenness of the IOC3 chip
 * which is used in SGI systems.  The IOC3 can only handle 32-bit PCI
 * accesses and does only decode parts of its address space.
 */
static int pci_conf0_read_config(struct pci_bus *bus, unsigned int devfn,
				 int where, int size, u32 *value)
{
	struct bridge_controller *bc = BRIDGE_CONTROLLER(bus);
	struct bridge_regs *bridge = bc->base;
	int slot = PCI_SLOT(devfn);
	int fn = PCI_FUNC(devfn);
	void *addr;
	u32 cf;
	int res;

	addr = &bridge->b_type0_cfg_dev[slot].f[fn].c[PCI_VENDOR_ID];
	if (get_dbe(cf, (u32 *)addr))
		return PCIBIOS_DEVICE_NOT_FOUND;

	/*
	 * IOC3 is broken beyond belief ...  Don't even give the
	 * generic PCI code a chance to look at it for real ...
	 */
	if (cf == (PCI_VENDOR_ID_SGI | (PCI_DEVICE_ID_SGI_IOC3 << 16))) {
		addr = &bridge->b_type0_cfg_dev[slot].f[fn].l[where >> 2];
		return ioc3_cfg_rd(addr, where, size, value,
				   bc->ioc3_sid[slot]);
	}

	addr = &bridge->b_type0_cfg_dev[slot].f[fn].c[where ^ (4 - size)];

	if (size == 1)
		res = get_dbe(*value, (u8 *)addr);
	else if (size == 2)
		res = get_dbe(*value, (u16 *)addr);
	else
		res = get_dbe(*value, (u32 *)addr);

	return res ? PCIBIOS_DEVICE_NOT_FOUND : PCIBIOS_SUCCESSFUL;
}

static int pci_conf1_read_config(struct pci_bus *bus, unsigned int devfn,
				 int where, int size, u32 *value)
{
	struct bridge_controller *bc = BRIDGE_CONTROLLER(bus);
	struct bridge_regs *bridge = bc->base;
	int busno = bus->number;
	int slot = PCI_SLOT(devfn);
	int fn = PCI_FUNC(devfn);
	void *addr;
	u32 cf;
	int res;

	bridge_write(bc, b_pci_cfg, (busno << 16) | (slot << 11));
	addr = &bridge->b_type1_cfg.c[(fn << 8) | PCI_VENDOR_ID];
	if (get_dbe(cf, (u32 *)addr))
		return PCIBIOS_DEVICE_NOT_FOUND;

	/*
	 * IOC3 is broken beyond belief ...  Don't even give the
	 * generic PCI code a chance to look at it for real ...
	 */
	if (cf == (PCI_VENDOR_ID_SGI | (PCI_DEVICE_ID_SGI_IOC3 << 16))) {
		addr = &bridge->b_type1_cfg.c[(fn << 8) | (where & ~3)];
		return ioc3_cfg_rd(addr, where, size, value,
				   bc->ioc3_sid[slot]);
	}

	addr = &bridge->b_type1_cfg.c[(fn << 8) | (where ^ (4 - size))];

	if (size == 1)
		res = get_dbe(*value, (u8 *)addr);
	else if (size == 2)
		res = get_dbe(*value, (u16 *)addr);
	else
		res = get_dbe(*value, (u32 *)addr);

	return res ? PCIBIOS_DEVICE_NOT_FOUND : PCIBIOS_SUCCESSFUL;
}

static int pci_read_config(struct pci_bus *bus, unsigned int devfn,
			   int where, int size, u32 *value)
{
	if (!pci_is_root_bus(bus))
		return pci_conf1_read_config(bus, devfn, where, size, value);

	return pci_conf0_read_config(bus, devfn, where, size, value);
}

static int pci_conf0_write_config(struct pci_bus *bus, unsigned int devfn,
				  int where, int size, u32 value)
{
	struct bridge_controller *bc = BRIDGE_CONTROLLER(bus);
	struct bridge_regs *bridge = bc->base;
	int slot = PCI_SLOT(devfn);
	int fn = PCI_FUNC(devfn);
	void *addr;
	u32 cf;
	int res;

	addr = &bridge->b_type0_cfg_dev[slot].f[fn].c[PCI_VENDOR_ID];
	if (get_dbe(cf, (u32 *)addr))
		return PCIBIOS_DEVICE_NOT_FOUND;

	/*
	 * IOC3 is broken beyond belief ...  Don't even give the
	 * generic PCI code a chance to look at it for real ...
	 */
	if (cf == (PCI_VENDOR_ID_SGI | (PCI_DEVICE_ID_SGI_IOC3 << 16))) {
		addr = &bridge->b_type0_cfg_dev[slot].f[fn].l[where >> 2];
		return ioc3_cfg_wr(addr, where, size, value);
	}

	addr = &bridge->b_type0_cfg_dev[slot].f[fn].c[where ^ (4 - size)];

	if (size == 1)
		res = put_dbe(value, (u8 *)addr);
	else if (size == 2)
		res = put_dbe(value, (u16 *)addr);
	else
		res = put_dbe(value, (u32 *)addr);

	if (res)
		return PCIBIOS_DEVICE_NOT_FOUND;

	return PCIBIOS_SUCCESSFUL;
}

static int pci_conf1_write_config(struct pci_bus *bus, unsigned int devfn,
				  int where, int size, u32 value)
{
	struct bridge_controller *bc = BRIDGE_CONTROLLER(bus);
	struct bridge_regs *bridge = bc->base;
	int slot = PCI_SLOT(devfn);
	int fn = PCI_FUNC(devfn);
	int busno = bus->number;
	void *addr;
	u32 cf;
	int res;

	bridge_write(bc, b_pci_cfg, (busno << 16) | (slot << 11));
	addr = &bridge->b_type1_cfg.c[(fn << 8) | PCI_VENDOR_ID];
	if (get_dbe(cf, (u32 *)addr))
		return PCIBIOS_DEVICE_NOT_FOUND;

	/*
	 * IOC3 is broken beyond belief ...  Don't even give the
	 * generic PCI code a chance to look at it for real ...
	 */
	if (cf == (PCI_VENDOR_ID_SGI | (PCI_DEVICE_ID_SGI_IOC3 << 16))) {
		addr = &bridge->b_type0_cfg_dev[slot].f[fn].l[where >> 2];
		return ioc3_cfg_wr(addr, where, size, value);
	}

	addr = &bridge->b_type1_cfg.c[(fn << 8) | (where ^ (4 - size))];

	if (size == 1)
		res = put_dbe(value, (u8 *)addr);
	else if (size == 2)
		res = put_dbe(value, (u16 *)addr);
	else
		res = put_dbe(value, (u32 *)addr);

	if (res)
		return PCIBIOS_DEVICE_NOT_FOUND;

	return PCIBIOS_SUCCESSFUL;
}

static int pci_write_config(struct pci_bus *bus, unsigned int devfn,
	int where, int size, u32 value)
{
	if (!pci_is_root_bus(bus))
		return pci_conf1_write_config(bus, devfn, where, size, value);

	return pci_conf0_write_config(bus, devfn, where, size, value);
}

static struct pci_ops bridge_pci_ops = {
	.read	 = pci_read_config,
	.write	 = pci_write_config,
};

struct bridge_irq_chip_data {
	struct bridge_controller *bc;
	nasid_t nasid;
};

static int bridge_set_affinity(struct irq_data *d, const struct cpumask *mask,
			       bool force)
{
#ifdef CONFIG_NUMA
	struct bridge_irq_chip_data *data = d->chip_data;
	int bit = d->parent_data->hwirq;
	int pin = d->hwirq;
	int ret, cpu;

	ret = irq_chip_set_affinity_parent(d, mask, force);
	if (ret >= 0) {
		cpu = cpumask_first_and(mask, cpu_online_mask);
		data->nasid = cpu_to_node(cpu);
		bridge_write(data->bc, b_int_addr[pin].addr,
			     (((data->bc->intr_addr >> 30) & 0x30000) |
			      bit | (data->nasid << 8)));
		bridge_read(data->bc, b_wid_tflush);
	}
	return ret;
#else
	return irq_chip_set_affinity_parent(d, mask, force);
#endif
}

struct irq_chip bridge_irq_chip = {
	.name             = "BRIDGE",
	.irq_mask         = irq_chip_mask_parent,
	.irq_unmask       = irq_chip_unmask_parent,
	.irq_set_affinity = bridge_set_affinity
};

static int bridge_domain_alloc(struct irq_domain *domain, unsigned int virq,
			       unsigned int nr_irqs, void *arg)
{
	struct bridge_irq_chip_data *data;
	struct irq_alloc_info *info = arg;
	int ret;

	if (nr_irqs > 1 || !info)
		return -EINVAL;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	ret = irq_domain_alloc_irqs_parent(domain, virq, nr_irqs, arg);
	if (ret >= 0) {
		data->bc = info->ctrl;
		data->nasid = info->nasid;
		irq_domain_set_info(domain, virq, info->pin, &bridge_irq_chip,
				    data, handle_level_irq, NULL, NULL);
	} else {
		kfree(data);
	}

	return ret;
}

static void bridge_domain_free(struct irq_domain *domain, unsigned int virq,
			       unsigned int nr_irqs)
{
	struct irq_data *irqd = irq_domain_get_irq_data(domain, virq);

	if (nr_irqs)
		return;

	kfree(irqd->chip_data);
	irq_domain_free_irqs_top(domain, virq, nr_irqs);
}

static int bridge_domain_activate(struct irq_domain *domain,
				  struct irq_data *irqd, bool reserve)
{
	struct bridge_irq_chip_data *data = irqd->chip_data;
	struct bridge_controller *bc = data->bc;
	int bit = irqd->parent_data->hwirq;
	int pin = irqd->hwirq;
	u32 device;

	bridge_write(bc, b_int_addr[pin].addr,
		     (((bc->intr_addr >> 30) & 0x30000) |
		      bit | (data->nasid << 8)));
	bridge_set(bc, b_int_enable, (1 << pin));
	bridge_set(bc, b_int_enable, 0x7ffffe00); /* more stuff in int_enable */

	/*
	 * Enable sending of an interrupt clear packet to the hub on a high to
	 * low transition of the interrupt pin.
	 *
	 * IRIX sets additional bits in the address which are documented as
	 * reserved in the bridge docs.
	 */
	bridge_set(bc, b_int_mode, (1UL << pin));

	/*
	 * We assume the bridge to have a 1:1 mapping between devices
	 * (slots) and intr pins.
	 */
	device = bridge_read(bc, b_int_device);
	device &= ~(7 << (pin*3));
	device |= (pin << (pin*3));
	bridge_write(bc, b_int_device, device);

	bridge_read(bc, b_wid_tflush);
	return 0;
}

static void bridge_domain_deactivate(struct irq_domain *domain,
				     struct irq_data *irqd)
{
	struct bridge_irq_chip_data *data = irqd->chip_data;

	bridge_clr(data->bc, b_int_enable, (1 << irqd->hwirq));
	bridge_read(data->bc, b_wid_tflush);
}

static const struct irq_domain_ops bridge_domain_ops = {
	.alloc      = bridge_domain_alloc,
	.free       = bridge_domain_free,
	.activate   = bridge_domain_activate,
	.deactivate = bridge_domain_deactivate
};

/*
 * All observed requests have pin == 1. We could have a global here, that
 * gets incremented and returned every time - unfortunately, pci_map_irq
 * may be called on the same device over and over, and need to return the
 * same value. On O2000, pin can be 0 or 1, and PCI slots can be [0..7].
 *
 * A given PCI device, in general, should be able to intr any of the cpus
 * on any one of the hubs connected to its xbow.
 */
static int bridge_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	struct bridge_controller *bc = BRIDGE_CONTROLLER(dev->bus);
	struct irq_alloc_info info;
	int irq;

	switch (pin) {
	case PCI_INTERRUPT_UNKNOWN:
	case PCI_INTERRUPT_INTA:
	case PCI_INTERRUPT_INTC:
		pin = 0;
		break;
	case PCI_INTERRUPT_INTB:
	case PCI_INTERRUPT_INTD:
		pin = 1;
	}

	irq = bc->pci_int[slot][pin];
	if (irq == -1) {
		info.ctrl = bc;
		info.nasid = bc->nasid;
		info.pin = bc->int_mapping[slot][pin];

		irq = irq_domain_alloc_irqs(bc->domain, 1, bc->nasid, &info);
		if (irq < 0)
			return irq;

		bc->pci_int[slot][pin] = irq;
	}
	return irq;
}

#define IOC3_SID(sid)	(PCI_VENDOR_ID_SGI | ((sid) << 16))

static void bridge_setup_ip27_baseio6g(struct bridge_controller *bc)
{
	bc->ioc3_sid[2] = IOC3_SID(IOC3_SUBSYS_IP27_BASEIO6G);
	bc->ioc3_sid[6] = IOC3_SID(IOC3_SUBSYS_IP27_MIO);
	bc->int_mapping[2][1] = 4;
	bc->int_mapping[6][1] = 6;
}

static void bridge_setup_ip27_baseio(struct bridge_controller *bc)
{
	bc->ioc3_sid[2] = IOC3_SID(IOC3_SUBSYS_IP27_BASEIO);
	bc->int_mapping[2][1] = 4;
}

static void bridge_setup_ip29_baseio(struct bridge_controller *bc)
{
	bc->ioc3_sid[2] = IOC3_SID(IOC3_SUBSYS_IP29_SYSBOARD);
	bc->int_mapping[2][1] = 3;
}

static void bridge_setup_ip30_sysboard(struct bridge_controller *bc)
{
	bc->ioc3_sid[2] = IOC3_SID(IOC3_SUBSYS_IP30_SYSBOARD);
	bc->int_mapping[2][1] = 4;
}

static void bridge_setup_menet(struct bridge_controller *bc)
{
	bc->ioc3_sid[0] = IOC3_SID(IOC3_SUBSYS_MENET);
	bc->ioc3_sid[1] = IOC3_SID(IOC3_SUBSYS_MENET);
	bc->ioc3_sid[2] = IOC3_SID(IOC3_SUBSYS_MENET);
	bc->ioc3_sid[3] = IOC3_SID(IOC3_SUBSYS_MENET4);
}

static void bridge_setup_io7(struct bridge_controller *bc)
{
	bc->ioc3_sid[4] = IOC3_SID(IOC3_SUBSYS_IO7);
}

static void bridge_setup_io8(struct bridge_controller *bc)
{
	bc->ioc3_sid[4] = IOC3_SID(IOC3_SUBSYS_IO8);
}

static void bridge_setup_io9(struct bridge_controller *bc)
{
	bc->ioc3_sid[1] = IOC3_SID(IOC3_SUBSYS_IO9);
}

static void bridge_setup_ip34_fuel_sysboard(struct bridge_controller *bc)
{
	bc->ioc3_sid[4] = IOC3_SID(IOC3_SUBSYS_IP34_SYSBOARD);
}

#define BRIDGE_BOARD_SETUP(_partno, _setup)	\
	{ .match = _partno, .setup = _setup }

static const struct {
	char *match;
	void (*setup)(struct bridge_controller *bc);
} bridge_ioc3_devid[] = {
	BRIDGE_BOARD_SETUP("030-0734-", bridge_setup_ip27_baseio6g),
	BRIDGE_BOARD_SETUP("030-0880-", bridge_setup_ip27_baseio6g),
	BRIDGE_BOARD_SETUP("030-1023-", bridge_setup_ip27_baseio),
	BRIDGE_BOARD_SETUP("030-1124-", bridge_setup_ip27_baseio),
	BRIDGE_BOARD_SETUP("030-1025-", bridge_setup_ip29_baseio),
	BRIDGE_BOARD_SETUP("030-1244-", bridge_setup_ip29_baseio),
	BRIDGE_BOARD_SETUP("030-1389-", bridge_setup_ip29_baseio),
	BRIDGE_BOARD_SETUP("030-0887-", bridge_setup_ip30_sysboard),
	BRIDGE_BOARD_SETUP("030-1467-", bridge_setup_ip30_sysboard),
	BRIDGE_BOARD_SETUP("030-0873-", bridge_setup_menet),
	BRIDGE_BOARD_SETUP("030-1557-", bridge_setup_io7),
	BRIDGE_BOARD_SETUP("030-1673-", bridge_setup_io8),
	BRIDGE_BOARD_SETUP("030-1771-", bridge_setup_io9),
	BRIDGE_BOARD_SETUP("030-1707-", bridge_setup_ip34_fuel_sysboard),
};

static void bridge_setup_board(struct bridge_controller *bc, char *partnum)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(bridge_ioc3_devid); i++)
		if (!strncmp(partnum, bridge_ioc3_devid[i].match,
			     strlen(bridge_ioc3_devid[i].match))) {
			bridge_ioc3_devid[i].setup(bc);
		}
}

static int bridge_nvmem_match(struct device *dev, const void *data)
{
	const char *name = dev_name(dev);
	const char *prefix = data;

	if (strlen(name) < strlen(prefix))
		return 0;

	return memcmp(prefix, dev_name(dev), strlen(prefix)) == 0;
}

static int bridge_get_partnum(u64 baddr, char *partnum)
{
	struct nvmem_device *nvmem;
	char prefix[24];
	u8 prom[64];
	int i, j;
	int ret;

	snprintf(prefix, sizeof(prefix), "bridge-%012llx-0b-", baddr);

	nvmem = nvmem_device_find(prefix, bridge_nvmem_match);
	if (IS_ERR(nvmem))
		return PTR_ERR(nvmem);

	ret = nvmem_device_read(nvmem, 0, 64, prom);
	nvmem_device_put(nvmem);

	if (ret != 64)
		return ret;

	if (crc16(CRC16_INIT, prom, 32) != CRC16_VALID ||
	    crc16(CRC16_INIT, prom + 32, 32) != CRC16_VALID)
		return -EINVAL;

	/* Assemble part number */
	j = 0;
	for (i = 0; i < 19; i++)
		if (prom[i + 11] != ' ')
			partnum[j++] = prom[i + 11];

	for (i = 0; i < 6; i++)
		if (prom[i + 32] != ' ')
			partnum[j++] = prom[i + 32];

	partnum[j] = 0;

	return 0;
}

static int bridge_probe(struct platform_device *pdev)
{
	struct xtalk_bridge_platform_data *bd = dev_get_platdata(&pdev->dev);
	struct device *dev = &pdev->dev;
	struct bridge_controller *bc;
	struct pci_host_bridge *host;
	struct irq_domain *domain, *parent;
	struct fwnode_handle *fn;
	char partnum[26];
	int slot;
	int err;

	/* get part number from one wire prom */
	if (bridge_get_partnum(virt_to_phys((void *)bd->bridge_addr), partnum))
		return -EPROBE_DEFER; /* not available yet */

	parent = irq_get_default_host();
	if (!parent)
		return -ENODEV;
	fn = irq_domain_alloc_named_fwnode("BRIDGE");
	if (!fn)
		return -ENOMEM;
	domain = irq_domain_create_hierarchy(parent, 0, 8, fn,
					     &bridge_domain_ops, NULL);
	if (!domain) {
		irq_domain_free_fwnode(fn);
		return -ENOMEM;
	}

	pci_set_flags(PCI_PROBE_ONLY);

	host = devm_pci_alloc_host_bridge(dev, sizeof(*bc));
	if (!host) {
		err = -ENOMEM;
		goto err_remove_domain;
	}

	bc = pci_host_bridge_priv(host);

	bc->busn.name		= "Bridge PCI busn";
	bc->busn.start		= 0;
	bc->busn.end		= 0xff;
	bc->busn.flags		= IORESOURCE_BUS;

	bc->domain		= domain;

	pci_add_resource_offset(&host->windows, &bd->mem, bd->mem_offset);
	pci_add_resource_offset(&host->windows, &bd->io, bd->io_offset);
	pci_add_resource(&host->windows, &bc->busn);

	err = devm_request_pci_bus_resources(dev, &host->windows);
	if (err < 0)
		goto err_free_resource;

	bc->nasid = bd->nasid;

	bc->baddr = (u64)bd->masterwid << 60 | PCI64_ATTR_BAR;
	bc->base = (struct bridge_regs *)bd->bridge_addr;
	bc->intr_addr = bd->intr_addr;

	/*
	 * Clear all pending interrupts.
	 */
	bridge_write(bc, b_int_rst_stat, BRIDGE_IRR_ALL_CLR);

	/*
	 * Until otherwise set up, assume all interrupts are from slot 0
	 */
	bridge_write(bc, b_int_device, 0x0);

	/*
	 * disable swapping for big windows
	 */
	bridge_clr(bc, b_wid_control,
		   BRIDGE_CTRL_IO_SWAP | BRIDGE_CTRL_MEM_SWAP);
#ifdef CONFIG_PAGE_SIZE_4KB
	bridge_clr(bc, b_wid_control, BRIDGE_CTRL_PAGE_SIZE);
#else /* 16kB or larger */
	bridge_set(bc, b_wid_control, BRIDGE_CTRL_PAGE_SIZE);
#endif

	/*
	 * Hmm...  IRIX sets additional bits in the address which
	 * are documented as reserved in the bridge docs.
	 */
	bridge_write(bc, b_wid_int_upper,
		     ((bc->intr_addr >> 32) & 0xffff) | (bd->masterwid << 16));
	bridge_write(bc, b_wid_int_lower, bc->intr_addr & 0xffffffff);
	bridge_write(bc, b_dir_map, (bd->masterwid << 20));	/* DMA */
	bridge_write(bc, b_int_enable, 0);

	for (slot = 0; slot < 8; slot++) {
		bridge_set(bc, b_device[slot].reg, BRIDGE_DEV_SWAP_DIR);
		bc->pci_int[slot][0] = -1;
		bc->pci_int[slot][1] = -1;
		/* default interrupt pin mapping */
		bc->int_mapping[slot][0] = slot;
		bc->int_mapping[slot][1] = slot ^ 4;
	}
	bridge_read(bc, b_wid_tflush);	  /* wait until Bridge PIO complete */

	bridge_setup_board(bc, partnum);

	host->dev.parent = dev;
	host->sysdata = bc;
	host->busnr = 0;
	host->ops = &bridge_pci_ops;
	host->map_irq = bridge_map_irq;
	host->swizzle_irq = pci_common_swizzle;

	err = pci_scan_root_bus_bridge(host);
	if (err < 0)
		goto err_free_resource;

	pci_bus_claim_resources(host->bus);
	pci_bus_add_devices(host->bus);

	platform_set_drvdata(pdev, host->bus);

	return 0;

err_free_resource:
	pci_free_resource_list(&host->windows);
err_remove_domain:
	irq_domain_remove(domain);
	irq_domain_free_fwnode(fn);
	return err;
}

static void bridge_remove(struct platform_device *pdev)
{
	struct pci_bus *bus = platform_get_drvdata(pdev);
	struct bridge_controller *bc = BRIDGE_CONTROLLER(bus);
	struct fwnode_handle *fn = bc->domain->fwnode;

	irq_domain_remove(bc->domain);
	irq_domain_free_fwnode(fn);
	pci_lock_rescan_remove();
	pci_stop_root_bus(bus);
	pci_remove_root_bus(bus);
	pci_unlock_rescan_remove();
}

static struct platform_driver bridge_driver = {
	.probe = bridge_probe,
	.remove_new = bridge_remove,
	.driver = {
		.name = "xtalk-bridge",
	}
};

builtin_platform_driver(bridge_driver);
