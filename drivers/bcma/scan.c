/*
 * Broadcom specific AMBA
 * Bus scanning
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include "scan.h"
#include "bcma_private.h"

#include <linux/bcma/bcma.h>
#include <linux/bcma/bcma_regs.h>
#include <linux/pci.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>

struct bcma_device_id_name {
	u16 id;
	const char *name;
};

static const struct bcma_device_id_name bcma_arm_device_names[] = {
	{ BCMA_CORE_4706_MAC_GBIT_COMMON, "BCM4706 GBit MAC Common" },
	{ BCMA_CORE_ARM_1176, "ARM 1176" },
	{ BCMA_CORE_ARM_7TDMI, "ARM 7TDMI" },
	{ BCMA_CORE_ARM_CM3, "ARM CM3" },
};

static const struct bcma_device_id_name bcma_bcm_device_names[] = {
	{ BCMA_CORE_OOB_ROUTER, "OOB Router" },
	{ BCMA_CORE_4706_CHIPCOMMON, "BCM4706 ChipCommon" },
	{ BCMA_CORE_4706_SOC_RAM, "BCM4706 SOC RAM" },
	{ BCMA_CORE_4706_MAC_GBIT, "BCM4706 GBit MAC" },
	{ BCMA_CORE_NS_PCIEG2, "PCIe Gen 2" },
	{ BCMA_CORE_NS_DMA, "DMA" },
	{ BCMA_CORE_NS_SDIO3, "SDIO3" },
	{ BCMA_CORE_NS_USB20, "USB 2.0" },
	{ BCMA_CORE_NS_USB30, "USB 3.0" },
	{ BCMA_CORE_NS_A9JTAG, "ARM Cortex A9 JTAG" },
	{ BCMA_CORE_NS_DDR23, "Denali DDR2/DDR3 memory controller" },
	{ BCMA_CORE_NS_ROM, "ROM" },
	{ BCMA_CORE_NS_NAND, "NAND flash controller" },
	{ BCMA_CORE_NS_QSPI, "SPI flash controller" },
	{ BCMA_CORE_NS_CHIPCOMMON_B, "Chipcommon B" },
	{ BCMA_CORE_ARMCA9, "ARM Cortex A9 core (ihost)" },
	{ BCMA_CORE_AMEMC, "AMEMC (DDR)" },
	{ BCMA_CORE_ALTA, "ALTA (I2S)" },
	{ BCMA_CORE_INVALID, "Invalid" },
	{ BCMA_CORE_CHIPCOMMON, "ChipCommon" },
	{ BCMA_CORE_ILINE20, "ILine 20" },
	{ BCMA_CORE_SRAM, "SRAM" },
	{ BCMA_CORE_SDRAM, "SDRAM" },
	{ BCMA_CORE_PCI, "PCI" },
	{ BCMA_CORE_ETHERNET, "Fast Ethernet" },
	{ BCMA_CORE_V90, "V90" },
	{ BCMA_CORE_USB11_HOSTDEV, "USB 1.1 Hostdev" },
	{ BCMA_CORE_ADSL, "ADSL" },
	{ BCMA_CORE_ILINE100, "ILine 100" },
	{ BCMA_CORE_IPSEC, "IPSEC" },
	{ BCMA_CORE_UTOPIA, "UTOPIA" },
	{ BCMA_CORE_PCMCIA, "PCMCIA" },
	{ BCMA_CORE_INTERNAL_MEM, "Internal Memory" },
	{ BCMA_CORE_MEMC_SDRAM, "MEMC SDRAM" },
	{ BCMA_CORE_OFDM, "OFDM" },
	{ BCMA_CORE_EXTIF, "EXTIF" },
	{ BCMA_CORE_80211, "IEEE 802.11" },
	{ BCMA_CORE_PHY_A, "PHY A" },
	{ BCMA_CORE_PHY_B, "PHY B" },
	{ BCMA_CORE_PHY_G, "PHY G" },
	{ BCMA_CORE_USB11_HOST, "USB 1.1 Host" },
	{ BCMA_CORE_USB11_DEV, "USB 1.1 Device" },
	{ BCMA_CORE_USB20_HOST, "USB 2.0 Host" },
	{ BCMA_CORE_USB20_DEV, "USB 2.0 Device" },
	{ BCMA_CORE_SDIO_HOST, "SDIO Host" },
	{ BCMA_CORE_ROBOSWITCH, "Roboswitch" },
	{ BCMA_CORE_PARA_ATA, "PATA" },
	{ BCMA_CORE_SATA_XORDMA, "SATA XOR-DMA" },
	{ BCMA_CORE_ETHERNET_GBIT, "GBit Ethernet" },
	{ BCMA_CORE_PCIE, "PCIe" },
	{ BCMA_CORE_PHY_N, "PHY N" },
	{ BCMA_CORE_SRAM_CTL, "SRAM Controller" },
	{ BCMA_CORE_MINI_MACPHY, "Mini MACPHY" },
	{ BCMA_CORE_PHY_LP, "PHY LP" },
	{ BCMA_CORE_PMU, "PMU" },
	{ BCMA_CORE_PHY_SSN, "PHY SSN" },
	{ BCMA_CORE_SDIO_DEV, "SDIO Device" },
	{ BCMA_CORE_PHY_HT, "PHY HT" },
	{ BCMA_CORE_MAC_GBIT, "GBit MAC" },
	{ BCMA_CORE_DDR12_MEM_CTL, "DDR1/DDR2 Memory Controller" },
	{ BCMA_CORE_PCIE_RC, "PCIe Root Complex" },
	{ BCMA_CORE_OCP_OCP_BRIDGE, "OCP to OCP Bridge" },
	{ BCMA_CORE_SHARED_COMMON, "Common Shared" },
	{ BCMA_CORE_OCP_AHB_BRIDGE, "OCP to AHB Bridge" },
	{ BCMA_CORE_SPI_HOST, "SPI Host" },
	{ BCMA_CORE_I2S, "I2S" },
	{ BCMA_CORE_SDR_DDR1_MEM_CTL, "SDR/DDR1 Memory Controller" },
	{ BCMA_CORE_SHIM, "SHIM" },
	{ BCMA_CORE_PCIE2, "PCIe Gen2" },
	{ BCMA_CORE_ARM_CR4, "ARM CR4" },
	{ BCMA_CORE_DEFAULT, "Default" },
};

static const struct bcma_device_id_name bcma_mips_device_names[] = {
	{ BCMA_CORE_MIPS, "MIPS" },
	{ BCMA_CORE_MIPS_3302, "MIPS 3302" },
	{ BCMA_CORE_MIPS_74K, "MIPS 74K" },
};

static const char *bcma_device_name(const struct bcma_device_id *id)
{
	const struct bcma_device_id_name *names;
	int size, i;

	/* search manufacturer specific names */
	switch (id->manuf) {
	case BCMA_MANUF_ARM:
		names = bcma_arm_device_names;
		size = ARRAY_SIZE(bcma_arm_device_names);
		break;
	case BCMA_MANUF_BCM:
		names = bcma_bcm_device_names;
		size = ARRAY_SIZE(bcma_bcm_device_names);
		break;
	case BCMA_MANUF_MIPS:
		names = bcma_mips_device_names;
		size = ARRAY_SIZE(bcma_mips_device_names);
		break;
	default:
		return "UNKNOWN";
	}

	for (i = 0; i < size; i++) {
		if (names[i].id == id->id)
			return names[i].name;
	}

	return "UNKNOWN";
}

static u32 bcma_scan_read32(struct bcma_bus *bus, u8 current_coreidx,
		       u16 offset)
{
	return readl(bus->mmio + offset);
}

static void bcma_scan_switch_core(struct bcma_bus *bus, u32 addr)
{
	if (bus->hosttype == BCMA_HOSTTYPE_PCI)
		pci_write_config_dword(bus->host_pci, BCMA_PCI_BAR0_WIN,
				       addr);
}

static u32 bcma_erom_get_ent(struct bcma_bus *bus, u32 __iomem **eromptr)
{
	u32 ent = readl(*eromptr);
	(*eromptr)++;
	return ent;
}

static void bcma_erom_push_ent(u32 __iomem **eromptr)
{
	(*eromptr)--;
}

static s32 bcma_erom_get_ci(struct bcma_bus *bus, u32 __iomem **eromptr)
{
	u32 ent = bcma_erom_get_ent(bus, eromptr);
	if (!(ent & SCAN_ER_VALID))
		return -ENOENT;
	if ((ent & SCAN_ER_TAG) != SCAN_ER_TAG_CI)
		return -ENOENT;
	return ent;
}

static bool bcma_erom_is_end(struct bcma_bus *bus, u32 __iomem **eromptr)
{
	u32 ent = bcma_erom_get_ent(bus, eromptr);
	bcma_erom_push_ent(eromptr);
	return (ent == (SCAN_ER_TAG_END | SCAN_ER_VALID));
}

static bool bcma_erom_is_bridge(struct bcma_bus *bus, u32 __iomem **eromptr)
{
	u32 ent = bcma_erom_get_ent(bus, eromptr);
	bcma_erom_push_ent(eromptr);
	return (((ent & SCAN_ER_VALID)) &&
		((ent & SCAN_ER_TAGX) == SCAN_ER_TAG_ADDR) &&
		((ent & SCAN_ADDR_TYPE) == SCAN_ADDR_TYPE_BRIDGE));
}

static void bcma_erom_skip_component(struct bcma_bus *bus, u32 __iomem **eromptr)
{
	u32 ent;
	while (1) {
		ent = bcma_erom_get_ent(bus, eromptr);
		if ((ent & SCAN_ER_VALID) &&
		    ((ent & SCAN_ER_TAG) == SCAN_ER_TAG_CI))
			break;
		if (ent == (SCAN_ER_TAG_END | SCAN_ER_VALID))
			break;
	}
	bcma_erom_push_ent(eromptr);
}

static s32 bcma_erom_get_mst_port(struct bcma_bus *bus, u32 __iomem **eromptr)
{
	u32 ent = bcma_erom_get_ent(bus, eromptr);
	if (!(ent & SCAN_ER_VALID))
		return -ENOENT;
	if ((ent & SCAN_ER_TAG) != SCAN_ER_TAG_MP)
		return -ENOENT;
	return ent;
}

static u32 bcma_erom_get_addr_desc(struct bcma_bus *bus, u32 __iomem **eromptr,
				  u32 type, u8 port)
{
	u32 addrl, addrh, sizel, sizeh = 0;
	u32 size;

	u32 ent = bcma_erom_get_ent(bus, eromptr);
	if ((!(ent & SCAN_ER_VALID)) ||
	    ((ent & SCAN_ER_TAGX) != SCAN_ER_TAG_ADDR) ||
	    ((ent & SCAN_ADDR_TYPE) != type) ||
	    (((ent & SCAN_ADDR_PORT) >> SCAN_ADDR_PORT_SHIFT) != port)) {
		bcma_erom_push_ent(eromptr);
		return (u32)-EINVAL;
	}

	addrl = ent & SCAN_ADDR_ADDR;
	if (ent & SCAN_ADDR_AG32)
		addrh = bcma_erom_get_ent(bus, eromptr);
	else
		addrh = 0;

	if ((ent & SCAN_ADDR_SZ) == SCAN_ADDR_SZ_SZD) {
		size = bcma_erom_get_ent(bus, eromptr);
		sizel = size & SCAN_SIZE_SZ;
		if (size & SCAN_SIZE_SG32)
			sizeh = bcma_erom_get_ent(bus, eromptr);
	} else
		sizel = SCAN_ADDR_SZ_BASE <<
				((ent & SCAN_ADDR_SZ) >> SCAN_ADDR_SZ_SHIFT);

	return addrl;
}

static struct bcma_device *bcma_find_core_by_index(struct bcma_bus *bus,
						   u16 index)
{
	struct bcma_device *core;

	list_for_each_entry(core, &bus->cores, list) {
		if (core->core_index == index)
			return core;
	}
	return NULL;
}

static struct bcma_device *bcma_find_core_reverse(struct bcma_bus *bus, u16 coreid)
{
	struct bcma_device *core;

	list_for_each_entry_reverse(core, &bus->cores, list) {
		if (core->id.id == coreid)
			return core;
	}
	return NULL;
}

#define IS_ERR_VALUE_U32(x) ((x) >= (u32)-MAX_ERRNO)

static int bcma_get_next_core(struct bcma_bus *bus, u32 __iomem **eromptr,
			      struct bcma_device_id *match, int core_num,
			      struct bcma_device *core)
{
	u32 tmp;
	u8 i, j, k;
	s32 cia, cib;
	u8 ports[2], wrappers[2];

	/* get CIs */
	cia = bcma_erom_get_ci(bus, eromptr);
	if (cia < 0) {
		bcma_erom_push_ent(eromptr);
		if (bcma_erom_is_end(bus, eromptr))
			return -ESPIPE;
		return -EILSEQ;
	}
	cib = bcma_erom_get_ci(bus, eromptr);
	if (cib < 0)
		return -EILSEQ;

	/* parse CIs */
	core->id.class = (cia & SCAN_CIA_CLASS) >> SCAN_CIA_CLASS_SHIFT;
	core->id.id = (cia & SCAN_CIA_ID) >> SCAN_CIA_ID_SHIFT;
	core->id.manuf = (cia & SCAN_CIA_MANUF) >> SCAN_CIA_MANUF_SHIFT;
	ports[0] = (cib & SCAN_CIB_NMP) >> SCAN_CIB_NMP_SHIFT;
	ports[1] = (cib & SCAN_CIB_NSP) >> SCAN_CIB_NSP_SHIFT;
	wrappers[0] = (cib & SCAN_CIB_NMW) >> SCAN_CIB_NMW_SHIFT;
	wrappers[1] = (cib & SCAN_CIB_NSW) >> SCAN_CIB_NSW_SHIFT;
	core->id.rev = (cib & SCAN_CIB_REV) >> SCAN_CIB_REV_SHIFT;

	if (((core->id.manuf == BCMA_MANUF_ARM) &&
	     (core->id.id == 0xFFF)) ||
	    (ports[1] == 0)) {
		bcma_erom_skip_component(bus, eromptr);
		return -ENXIO;
	}

	/* check if component is a core at all */
	if (wrappers[0] + wrappers[1] == 0) {
		/* Some specific cores don't need wrappers */
		switch (core->id.id) {
		case BCMA_CORE_4706_MAC_GBIT_COMMON:
		case BCMA_CORE_NS_CHIPCOMMON_B:
		/* Not used yet: case BCMA_CORE_OOB_ROUTER: */
			break;
		default:
			bcma_erom_skip_component(bus, eromptr);
			return -ENXIO;
		}
	}

	if (bcma_erom_is_bridge(bus, eromptr)) {
		bcma_erom_skip_component(bus, eromptr);
		return -ENXIO;
	}

	if (bcma_find_core_by_index(bus, core_num)) {
		bcma_erom_skip_component(bus, eromptr);
		return -ENODEV;
	}

	if (match && ((match->manuf != BCMA_ANY_MANUF &&
	      match->manuf != core->id.manuf) ||
	     (match->id != BCMA_ANY_ID && match->id != core->id.id) ||
	     (match->rev != BCMA_ANY_REV && match->rev != core->id.rev) ||
	     (match->class != BCMA_ANY_CLASS && match->class != core->id.class)
	    )) {
		bcma_erom_skip_component(bus, eromptr);
		return -ENODEV;
	}

	/* get & parse master ports */
	for (i = 0; i < ports[0]; i++) {
		s32 mst_port_d = bcma_erom_get_mst_port(bus, eromptr);
		if (mst_port_d < 0)
			return -EILSEQ;
	}

	/* First Slave Address Descriptor should be port 0:
	 * the main register space for the core
	 */
	tmp = bcma_erom_get_addr_desc(bus, eromptr, SCAN_ADDR_TYPE_SLAVE, 0);
	if (tmp == 0 || IS_ERR_VALUE_U32(tmp)) {
		/* Try again to see if it is a bridge */
		tmp = bcma_erom_get_addr_desc(bus, eromptr,
					      SCAN_ADDR_TYPE_BRIDGE, 0);
		if (tmp == 0 || IS_ERR_VALUE_U32(tmp)) {
			return -EILSEQ;
		} else {
			bcma_info(bus, "Bridge found\n");
			return -ENXIO;
		}
	}
	core->addr = tmp;

	/* get & parse slave ports */
	k = 0;
	for (i = 0; i < ports[1]; i++) {
		for (j = 0; ; j++) {
			tmp = bcma_erom_get_addr_desc(bus, eromptr,
				SCAN_ADDR_TYPE_SLAVE, i);
			if (IS_ERR_VALUE_U32(tmp)) {
				/* no more entries for port _i_ */
				/* pr_debug("erom: slave port %d "
				 * "has %d descriptors\n", i, j); */
				break;
			} else if (k < ARRAY_SIZE(core->addr_s)) {
				core->addr_s[k] = tmp;
				k++;
			}
		}
	}

	/* get & parse master wrappers */
	for (i = 0; i < wrappers[0]; i++) {
		for (j = 0; ; j++) {
			tmp = bcma_erom_get_addr_desc(bus, eromptr,
				SCAN_ADDR_TYPE_MWRAP, i);
			if (IS_ERR_VALUE_U32(tmp)) {
				/* no more entries for port _i_ */
				/* pr_debug("erom: master wrapper %d "
				 * "has %d descriptors\n", i, j); */
				break;
			} else {
				if (i == 0 && j == 0)
					core->wrap = tmp;
			}
		}
	}

	/* get & parse slave wrappers */
	for (i = 0; i < wrappers[1]; i++) {
		u8 hack = (ports[1] == 1) ? 0 : 1;
		for (j = 0; ; j++) {
			tmp = bcma_erom_get_addr_desc(bus, eromptr,
				SCAN_ADDR_TYPE_SWRAP, i + hack);
			if (IS_ERR_VALUE_U32(tmp)) {
				/* no more entries for port _i_ */
				/* pr_debug("erom: master wrapper %d "
				 * has %d descriptors\n", i, j); */
				break;
			} else {
				if (wrappers[0] == 0 && !i && !j)
					core->wrap = tmp;
			}
		}
	}
	if (bus->hosttype == BCMA_HOSTTYPE_SOC) {
		core->io_addr = ioremap_nocache(core->addr, BCMA_CORE_SIZE);
		if (!core->io_addr)
			return -ENOMEM;
		if (core->wrap) {
			core->io_wrap = ioremap_nocache(core->wrap,
							BCMA_CORE_SIZE);
			if (!core->io_wrap) {
				iounmap(core->io_addr);
				return -ENOMEM;
			}
		}
	}
	return 0;
}

void bcma_init_bus(struct bcma_bus *bus)
{
	s32 tmp;
	struct bcma_chipinfo *chipinfo = &(bus->chipinfo);
	char chip_id[8];

	INIT_LIST_HEAD(&bus->cores);
	bus->nr_cores = 0;

	bcma_scan_switch_core(bus, BCMA_ADDR_BASE);

	tmp = bcma_scan_read32(bus, 0, BCMA_CC_ID);
	chipinfo->id = (tmp & BCMA_CC_ID_ID) >> BCMA_CC_ID_ID_SHIFT;
	chipinfo->rev = (tmp & BCMA_CC_ID_REV) >> BCMA_CC_ID_REV_SHIFT;
	chipinfo->pkg = (tmp & BCMA_CC_ID_PKG) >> BCMA_CC_ID_PKG_SHIFT;

	snprintf(chip_id, ARRAY_SIZE(chip_id),
		 (chipinfo->id > 0x9999) ? "%d" : "0x%04X", chipinfo->id);
	bcma_info(bus, "Found chip with id %s, rev 0x%02X and package 0x%02X\n",
		  chip_id, chipinfo->rev, chipinfo->pkg);
}

int bcma_bus_scan(struct bcma_bus *bus)
{
	u32 erombase;
	u32 __iomem *eromptr, *eromend;

	int err, core_num = 0;

	erombase = bcma_scan_read32(bus, 0, BCMA_CC_EROM);
	if (bus->hosttype == BCMA_HOSTTYPE_SOC) {
		eromptr = ioremap_nocache(erombase, BCMA_CORE_SIZE);
		if (!eromptr)
			return -ENOMEM;
	} else {
		eromptr = bus->mmio;
	}

	eromend = eromptr + BCMA_CORE_SIZE / sizeof(u32);

	bcma_scan_switch_core(bus, erombase);

	while (eromptr < eromend) {
		struct bcma_device *other_core;
		struct bcma_device *core = kzalloc(sizeof(*core), GFP_KERNEL);
		if (!core) {
			err = -ENOMEM;
			goto out;
		}
		INIT_LIST_HEAD(&core->list);
		core->bus = bus;

		err = bcma_get_next_core(bus, &eromptr, NULL, core_num, core);
		if (err < 0) {
			kfree(core);
			if (err == -ENODEV) {
				core_num++;
				continue;
			} else if (err == -ENXIO) {
				continue;
			} else if (err == -ESPIPE) {
				break;
			}
			goto out;
		}

		core->core_index = core_num++;
		bus->nr_cores++;
		other_core = bcma_find_core_reverse(bus, core->id.id);
		core->core_unit = (other_core == NULL) ? 0 : other_core->core_unit + 1;
		bcma_prepare_core(bus, core);

		bcma_info(bus, "Core %d found: %s (manuf 0x%03X, id 0x%03X, rev 0x%02X, class 0x%X)\n",
			  core->core_index, bcma_device_name(&core->id),
			  core->id.manuf, core->id.id, core->id.rev,
			  core->id.class);

		list_add_tail(&core->list, &bus->cores);
	}

	err = 0;
out:
	if (bus->hosttype == BCMA_HOSTTYPE_SOC)
		iounmap(eromptr);

	return err;
}

int __init bcma_bus_scan_early(struct bcma_bus *bus,
			       struct bcma_device_id *match,
			       struct bcma_device *core)
{
	u32 erombase;
	u32 __iomem *eromptr, *eromend;

	int err = -ENODEV;
	int core_num = 0;

	erombase = bcma_scan_read32(bus, 0, BCMA_CC_EROM);
	if (bus->hosttype == BCMA_HOSTTYPE_SOC) {
		eromptr = ioremap_nocache(erombase, BCMA_CORE_SIZE);
		if (!eromptr)
			return -ENOMEM;
	} else {
		eromptr = bus->mmio;
	}

	eromend = eromptr + BCMA_CORE_SIZE / sizeof(u32);

	bcma_scan_switch_core(bus, erombase);

	while (eromptr < eromend) {
		memset(core, 0, sizeof(*core));
		INIT_LIST_HEAD(&core->list);
		core->bus = bus;

		err = bcma_get_next_core(bus, &eromptr, match, core_num, core);
		if (err == -ENODEV) {
			core_num++;
			continue;
		} else if (err == -ENXIO)
			continue;
		else if (err == -ESPIPE)
			break;
		else if (err < 0)
			goto out;

		core->core_index = core_num++;
		bus->nr_cores++;
		bcma_info(bus, "Core %d found: %s (manuf 0x%03X, id 0x%03X, rev 0x%02X, class 0x%X)\n",
			  core->core_index, bcma_device_name(&core->id),
			  core->id.manuf, core->id.id, core->id.rev,
			  core->id.class);

		list_add_tail(&core->list, &bus->cores);
		err = 0;
		break;
	}

out:
	if (bus->hosttype == BCMA_HOSTTYPE_SOC)
		iounmap(eromptr);

	return err;
}
