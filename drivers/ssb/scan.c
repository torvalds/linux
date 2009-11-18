/*
 * Sonics Silicon Backplane
 * Bus scanning
 *
 * Copyright (C) 2005-2007 Michael Buesch <mb@bu3sch.de>
 * Copyright (C) 2005 Martin Langer <martin-langer@gmx.de>
 * Copyright (C) 2005 Stefano Brivio <st3@riseup.net>
 * Copyright (C) 2005 Danny van Dyk <kugelfang@gentoo.org>
 * Copyright (C) 2005 Andreas Jaggi <andreas.jaggi@waterwave.ch>
 * Copyright (C) 2006 Broadcom Corporation.
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include <linux/ssb/ssb.h>
#include <linux/ssb/ssb_regs.h>
#include <linux/pci.h>
#include <linux/io.h>

#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/ds.h>

#include "ssb_private.h"


const char *ssb_core_name(u16 coreid)
{
	switch (coreid) {
	case SSB_DEV_CHIPCOMMON:
		return "ChipCommon";
	case SSB_DEV_ILINE20:
		return "ILine 20";
	case SSB_DEV_SDRAM:
		return "SDRAM";
	case SSB_DEV_PCI:
		return "PCI";
	case SSB_DEV_MIPS:
		return "MIPS";
	case SSB_DEV_ETHERNET:
		return "Fast Ethernet";
	case SSB_DEV_V90:
		return "V90";
	case SSB_DEV_USB11_HOSTDEV:
		return "USB 1.1 Hostdev";
	case SSB_DEV_ADSL:
		return "ADSL";
	case SSB_DEV_ILINE100:
		return "ILine 100";
	case SSB_DEV_IPSEC:
		return "IPSEC";
	case SSB_DEV_PCMCIA:
		return "PCMCIA";
	case SSB_DEV_INTERNAL_MEM:
		return "Internal Memory";
	case SSB_DEV_MEMC_SDRAM:
		return "MEMC SDRAM";
	case SSB_DEV_EXTIF:
		return "EXTIF";
	case SSB_DEV_80211:
		return "IEEE 802.11";
	case SSB_DEV_MIPS_3302:
		return "MIPS 3302";
	case SSB_DEV_USB11_HOST:
		return "USB 1.1 Host";
	case SSB_DEV_USB11_DEV:
		return "USB 1.1 Device";
	case SSB_DEV_USB20_HOST:
		return "USB 2.0 Host";
	case SSB_DEV_USB20_DEV:
		return "USB 2.0 Device";
	case SSB_DEV_SDIO_HOST:
		return "SDIO Host";
	case SSB_DEV_ROBOSWITCH:
		return "Roboswitch";
	case SSB_DEV_PARA_ATA:
		return "PATA";
	case SSB_DEV_SATA_XORDMA:
		return "SATA XOR-DMA";
	case SSB_DEV_ETHERNET_GBIT:
		return "GBit Ethernet";
	case SSB_DEV_PCIE:
		return "PCI-E";
	case SSB_DEV_MIMO_PHY:
		return "MIMO PHY";
	case SSB_DEV_SRAM_CTRLR:
		return "SRAM Controller";
	case SSB_DEV_MINI_MACPHY:
		return "Mini MACPHY";
	case SSB_DEV_ARM_1176:
		return "ARM 1176";
	case SSB_DEV_ARM_7TDMI:
		return "ARM 7TDMI";
	}
	return "UNKNOWN";
}

static u16 pcidev_to_chipid(struct pci_dev *pci_dev)
{
	u16 chipid_fallback = 0;

	switch (pci_dev->device) {
	case 0x4301:
		chipid_fallback = 0x4301;
		break;
	case 0x4305 ... 0x4307:
		chipid_fallback = 0x4307;
		break;
	case 0x4403:
		chipid_fallback = 0x4402;
		break;
	case 0x4610 ... 0x4615:
		chipid_fallback = 0x4610;
		break;
	case 0x4710 ... 0x4715:
		chipid_fallback = 0x4710;
		break;
	case 0x4320 ... 0x4325:
		chipid_fallback = 0x4309;
		break;
	case PCI_DEVICE_ID_BCM4401:
	case PCI_DEVICE_ID_BCM4401B0:
	case PCI_DEVICE_ID_BCM4401B1:
		chipid_fallback = 0x4401;
		break;
	default:
		ssb_printk(KERN_ERR PFX
			   "PCI-ID not in fallback list\n");
	}

	return chipid_fallback;
}

static u8 chipid_to_nrcores(u16 chipid)
{
	switch (chipid) {
	case 0x5365:
		return 7;
	case 0x4306:
		return 6;
	case 0x4310:
		return 8;
	case 0x4307:
	case 0x4301:
		return 5;
	case 0x4401:
	case 0x4402:
		return 3;
	case 0x4710:
	case 0x4610:
	case 0x4704:
		return 9;
	default:
		ssb_printk(KERN_ERR PFX
			   "CHIPID not in nrcores fallback list\n");
	}

	return 1;
}

static u32 scan_read32(struct ssb_bus *bus, u8 current_coreidx,
		       u16 offset)
{
	u32 lo, hi;

	switch (bus->bustype) {
	case SSB_BUSTYPE_SSB:
		offset += current_coreidx * SSB_CORE_SIZE;
		break;
	case SSB_BUSTYPE_PCI:
		break;
	case SSB_BUSTYPE_PCMCIA:
		if (offset >= 0x800) {
			ssb_pcmcia_switch_segment(bus, 1);
			offset -= 0x800;
		} else
			ssb_pcmcia_switch_segment(bus, 0);
		lo = readw(bus->mmio + offset);
		hi = readw(bus->mmio + offset + 2);
		return lo | (hi << 16);
	case SSB_BUSTYPE_SDIO:
		offset += current_coreidx * SSB_CORE_SIZE;
		return ssb_sdio_scan_read32(bus, offset);
	}
	return readl(bus->mmio + offset);
}

static int scan_switchcore(struct ssb_bus *bus, u8 coreidx)
{
	switch (bus->bustype) {
	case SSB_BUSTYPE_SSB:
		break;
	case SSB_BUSTYPE_PCI:
		return ssb_pci_switch_coreidx(bus, coreidx);
	case SSB_BUSTYPE_PCMCIA:
		return ssb_pcmcia_switch_coreidx(bus, coreidx);
	case SSB_BUSTYPE_SDIO:
		return ssb_sdio_scan_switch_coreidx(bus, coreidx);
	}
	return 0;
}

void ssb_iounmap(struct ssb_bus *bus)
{
	switch (bus->bustype) {
	case SSB_BUSTYPE_SSB:
	case SSB_BUSTYPE_PCMCIA:
		iounmap(bus->mmio);
		break;
	case SSB_BUSTYPE_PCI:
#ifdef CONFIG_SSB_PCIHOST
		pci_iounmap(bus->host_pci, bus->mmio);
#else
		SSB_BUG_ON(1); /* Can't reach this code. */
#endif
		break;
	case SSB_BUSTYPE_SDIO:
		break;
	}
	bus->mmio = NULL;
	bus->mapped_device = NULL;
}

static void __iomem *ssb_ioremap(struct ssb_bus *bus,
				 unsigned long baseaddr)
{
	void __iomem *mmio = NULL;

	switch (bus->bustype) {
	case SSB_BUSTYPE_SSB:
		/* Only map the first core for now. */
		/* fallthrough... */
	case SSB_BUSTYPE_PCMCIA:
		mmio = ioremap(baseaddr, SSB_CORE_SIZE);
		break;
	case SSB_BUSTYPE_PCI:
#ifdef CONFIG_SSB_PCIHOST
		mmio = pci_iomap(bus->host_pci, 0, ~0UL);
#else
		SSB_BUG_ON(1); /* Can't reach this code. */
#endif
		break;
	case SSB_BUSTYPE_SDIO:
		/* Nothing to ioremap in the SDIO case, just fake it */
		mmio = (void __iomem *)baseaddr;
		break;
	}

	return mmio;
}

static int we_support_multiple_80211_cores(struct ssb_bus *bus)
{
	/* More than one 802.11 core is only supported by special chips.
	 * There are chips with two 802.11 cores, but with dangling
	 * pins on the second core. Be careful and reject them here.
	 */

#ifdef CONFIG_SSB_PCIHOST
	if (bus->bustype == SSB_BUSTYPE_PCI) {
		if (bus->host_pci->vendor == PCI_VENDOR_ID_BROADCOM &&
		    bus->host_pci->device == 0x4324)
			return 1;
	}
#endif /* CONFIG_SSB_PCIHOST */
	return 0;
}

int ssb_bus_scan(struct ssb_bus *bus,
		 unsigned long baseaddr)
{
	int err = -ENOMEM;
	void __iomem *mmio;
	u32 idhi, cc, rev, tmp;
	int dev_i, i;
	struct ssb_device *dev;
	int nr_80211_cores = 0;

	mmio = ssb_ioremap(bus, baseaddr);
	if (!mmio)
		goto out;
	bus->mmio = mmio;

	err = scan_switchcore(bus, 0); /* Switch to first core */
	if (err)
		goto err_unmap;

	idhi = scan_read32(bus, 0, SSB_IDHIGH);
	cc = (idhi & SSB_IDHIGH_CC) >> SSB_IDHIGH_CC_SHIFT;
	rev = (idhi & SSB_IDHIGH_RCLO);
	rev |= (idhi & SSB_IDHIGH_RCHI) >> SSB_IDHIGH_RCHI_SHIFT;

	bus->nr_devices = 0;
	if (cc == SSB_DEV_CHIPCOMMON) {
		tmp = scan_read32(bus, 0, SSB_CHIPCO_CHIPID);

		bus->chip_id = (tmp & SSB_CHIPCO_IDMASK);
		bus->chip_rev = (tmp & SSB_CHIPCO_REVMASK) >>
				SSB_CHIPCO_REVSHIFT;
		bus->chip_package = (tmp & SSB_CHIPCO_PACKMASK) >>
				    SSB_CHIPCO_PACKSHIFT;
		if (rev >= 4) {
			bus->nr_devices = (tmp & SSB_CHIPCO_NRCORESMASK) >>
					  SSB_CHIPCO_NRCORESSHIFT;
		}
		tmp = scan_read32(bus, 0, SSB_CHIPCO_CAP);
		bus->chipco.capabilities = tmp;
	} else {
		if (bus->bustype == SSB_BUSTYPE_PCI) {
			bus->chip_id = pcidev_to_chipid(bus->host_pci);
			pci_read_config_word(bus->host_pci, PCI_REVISION_ID,
					     &bus->chip_rev);
			bus->chip_package = 0;
		} else {
			bus->chip_id = 0x4710;
			bus->chip_rev = 0;
			bus->chip_package = 0;
		}
	}
	if (!bus->nr_devices)
		bus->nr_devices = chipid_to_nrcores(bus->chip_id);
	if (bus->nr_devices > ARRAY_SIZE(bus->devices)) {
		ssb_printk(KERN_ERR PFX
			   "More than %d ssb cores found (%d)\n",
			   SSB_MAX_NR_CORES, bus->nr_devices);
		goto err_unmap;
	}
	if (bus->bustype == SSB_BUSTYPE_SSB) {
		/* Now that we know the number of cores,
		 * remap the whole IO space for all cores.
		 */
		err = -ENOMEM;
		iounmap(mmio);
		mmio = ioremap(baseaddr, SSB_CORE_SIZE * bus->nr_devices);
		if (!mmio)
			goto out;
		bus->mmio = mmio;
	}

	/* Fetch basic information about each core/device */
	for (i = 0, dev_i = 0; i < bus->nr_devices; i++) {
		err = scan_switchcore(bus, i);
		if (err)
			goto err_unmap;
		dev = &(bus->devices[dev_i]);

		idhi = scan_read32(bus, i, SSB_IDHIGH);
		dev->id.coreid = (idhi & SSB_IDHIGH_CC) >> SSB_IDHIGH_CC_SHIFT;
		dev->id.revision = (idhi & SSB_IDHIGH_RCLO);
		dev->id.revision |= (idhi & SSB_IDHIGH_RCHI) >> SSB_IDHIGH_RCHI_SHIFT;
		dev->id.vendor = (idhi & SSB_IDHIGH_VC) >> SSB_IDHIGH_VC_SHIFT;
		dev->core_index = i;
		dev->bus = bus;
		dev->ops = bus->ops;

		ssb_dprintk(KERN_INFO PFX
			    "Core %d found: %s "
			    "(cc 0x%03X, rev 0x%02X, vendor 0x%04X)\n",
			    i, ssb_core_name(dev->id.coreid),
			    dev->id.coreid, dev->id.revision, dev->id.vendor);

		switch (dev->id.coreid) {
		case SSB_DEV_80211:
			nr_80211_cores++;
			if (nr_80211_cores > 1) {
				if (!we_support_multiple_80211_cores(bus)) {
					ssb_dprintk(KERN_INFO PFX "Ignoring additional "
						    "802.11 core\n");
					continue;
				}
			}
			break;
		case SSB_DEV_EXTIF:
#ifdef CONFIG_SSB_DRIVER_EXTIF
			if (bus->extif.dev) {
				ssb_printk(KERN_WARNING PFX
					   "WARNING: Multiple EXTIFs found\n");
				break;
			}
			bus->extif.dev = dev;
#endif /* CONFIG_SSB_DRIVER_EXTIF */
			break;
		case SSB_DEV_CHIPCOMMON:
			if (bus->chipco.dev) {
				ssb_printk(KERN_WARNING PFX
					   "WARNING: Multiple ChipCommon found\n");
				break;
			}
			bus->chipco.dev = dev;
			break;
		case SSB_DEV_MIPS:
		case SSB_DEV_MIPS_3302:
#ifdef CONFIG_SSB_DRIVER_MIPS
			if (bus->mipscore.dev) {
				ssb_printk(KERN_WARNING PFX
					   "WARNING: Multiple MIPS cores found\n");
				break;
			}
			bus->mipscore.dev = dev;
#endif /* CONFIG_SSB_DRIVER_MIPS */
			break;
		case SSB_DEV_PCI:
		case SSB_DEV_PCIE:
#ifdef CONFIG_SSB_DRIVER_PCICORE
			if (bus->bustype == SSB_BUSTYPE_PCI) {
				/* Ignore PCI cores on PCI-E cards.
				 * Ignore PCI-E cores on PCI cards. */
				if (dev->id.coreid == SSB_DEV_PCI) {
					if (bus->host_pci->is_pcie)
						continue;
				} else {
					if (!bus->host_pci->is_pcie)
						continue;
				}
			}
			if (bus->pcicore.dev) {
				ssb_printk(KERN_WARNING PFX
					   "WARNING: Multiple PCI(E) cores found\n");
				break;
			}
			bus->pcicore.dev = dev;
#endif /* CONFIG_SSB_DRIVER_PCICORE */
			break;
		default:
			break;
		}

		dev_i++;
	}
	bus->nr_devices = dev_i;

	err = 0;
out:
	return err;
err_unmap:
	ssb_iounmap(bus);
	goto out;
}
