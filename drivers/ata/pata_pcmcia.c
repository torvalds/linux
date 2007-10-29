/*
 *   pata_pcmcia.c - PCMCIA PATA controller driver.
 *   Copyright 2005-2006 Red Hat Inc <alan@redhat.com>, all rights reserved.
 *   PCMCIA ident update Copyright 2006 Marcin Juszkiewicz
 *						<openembedded@hrw.one.pl>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *   Heavily based upon ide-cs.c
 *   The initial developer of the original code is David A. Hinds
 *   <dahinds@users.sourceforge.net>.  Portions created by David A. Hinds
 *   are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <scsi/scsi_host.h>
#include <linux/ata.h>
#include <linux/libata.h>

#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/ds.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ciscode.h>


#define DRV_NAME "pata_pcmcia"
#define DRV_VERSION "0.3.2"

/*
 *	Private data structure to glue stuff together
 */

struct ata_pcmcia_info {
	struct pcmcia_device *pdev;
	int		ndev;
	dev_node_t	node;
};

/**
 *	pcmcia_set_mode	-	PCMCIA specific mode setup
 *	@link: link
 *	@r_failed_dev: Return pointer for failed device
 *
 *	Perform the tuning and setup of the devices and timings, which
 *	for PCMCIA is the same as any other controller. We wrap it however
 *	as we need to spot hardware with incorrect or missing master/slave
 *	decode, which alas is embarrassingly common in the PC world
 */

static int pcmcia_set_mode(struct ata_link *link, struct ata_device **r_failed_dev)
{
	struct ata_device *master = &link->device[0];
	struct ata_device *slave = &link->device[1];

	if (!ata_dev_enabled(master) || !ata_dev_enabled(slave))
		return ata_do_set_mode(link, r_failed_dev);

	if (memcmp(master->id + ATA_ID_FW_REV,  slave->id + ATA_ID_FW_REV,
			   ATA_ID_FW_REV_LEN + ATA_ID_PROD_LEN) == 0) {
		/* Suspicious match, but could be two cards from
		   the same vendor - check serial */
		if (memcmp(master->id + ATA_ID_SERNO, slave->id + ATA_ID_SERNO,
			   ATA_ID_SERNO_LEN) == 0 && master->id[ATA_ID_SERNO] >> 8) {
			ata_dev_printk(slave, KERN_WARNING, "is a ghost device, ignoring.\n");
			ata_dev_disable(slave);
		}
	}
	return ata_do_set_mode(link, r_failed_dev);
}

static struct scsi_host_template pcmcia_sht = {
	.module			= THIS_MODULE,
	.name			= DRV_NAME,
	.ioctl			= ata_scsi_ioctl,
	.queuecommand		= ata_scsi_queuecmd,
	.can_queue		= ATA_DEF_QUEUE,
	.this_id		= ATA_SHT_THIS_ID,
	.sg_tablesize		= LIBATA_MAX_PRD,
	.cmd_per_lun		= ATA_SHT_CMD_PER_LUN,
	.emulated		= ATA_SHT_EMULATED,
	.use_clustering		= ATA_SHT_USE_CLUSTERING,
	.proc_name		= DRV_NAME,
	.dma_boundary		= ATA_DMA_BOUNDARY,
	.slave_configure	= ata_scsi_slave_config,
	.slave_destroy		= ata_scsi_slave_destroy,
	.bios_param		= ata_std_bios_param,
};

static struct ata_port_operations pcmcia_port_ops = {
	.set_mode	= pcmcia_set_mode,
	.tf_load	= ata_tf_load,
	.tf_read	= ata_tf_read,
	.check_status 	= ata_check_status,
	.exec_command	= ata_exec_command,
	.dev_select 	= ata_std_dev_select,

	.freeze		= ata_bmdma_freeze,
	.thaw		= ata_bmdma_thaw,
	.error_handler	= ata_bmdma_error_handler,
	.post_internal_cmd = ata_bmdma_post_internal_cmd,
	.cable_detect	= ata_cable_40wire,

	.qc_prep 	= ata_qc_prep,
	.qc_issue	= ata_qc_issue_prot,

	.data_xfer	= ata_data_xfer_noirq,

	.irq_clear	= ata_bmdma_irq_clear,
	.irq_on		= ata_irq_on,

	.port_start	= ata_sff_port_start,
};

#define CS_CHECK(fn, ret) \
do { last_fn = (fn); if ((last_ret = (ret)) != 0) goto cs_failed; } while (0)

/**
 *	pcmcia_init_one		-	attach a PCMCIA interface
 *	@pdev: pcmcia device
 *
 *	Register a PCMCIA IDE interface. Such interfaces are PIO 0 and
 *	shared IRQ.
 */

static int pcmcia_init_one(struct pcmcia_device *pdev)
{
	struct ata_host *host;
	struct ata_port *ap;
	struct ata_pcmcia_info *info;
	tuple_t tuple;
	struct {
		unsigned short buf[128];
		cisparse_t parse;
		config_info_t conf;
		cistpl_cftable_entry_t dflt;
	} *stk = NULL;
	cistpl_cftable_entry_t *cfg;
	int pass, last_ret = 0, last_fn = 0, is_kme = 0, ret = -ENOMEM;
	unsigned long io_base, ctl_base;
	void __iomem *io_addr, *ctl_addr;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (info == NULL)
		return -ENOMEM;

	/* Glue stuff together. FIXME: We may be able to get rid of info with care */
	info->pdev = pdev;
	pdev->priv = info;

	/* Set up attributes in order to probe card and get resources */
	pdev->io.Attributes1 = IO_DATA_PATH_WIDTH_AUTO;
	pdev->io.Attributes2 = IO_DATA_PATH_WIDTH_8;
	pdev->io.IOAddrLines = 3;
	pdev->irq.Attributes = IRQ_TYPE_DYNAMIC_SHARING;
	pdev->irq.IRQInfo1 = IRQ_LEVEL_ID;
	pdev->conf.Attributes = CONF_ENABLE_IRQ;
	pdev->conf.IntType = INT_MEMORY_AND_IO;

	/* Allocate resoure probing structures */

	stk = kzalloc(sizeof(*stk), GFP_KERNEL);
	if (!stk)
		goto out1;

	cfg = &stk->parse.cftable_entry;

	/* Tuples we are walking */
	tuple.TupleData = (cisdata_t *)&stk->buf;
	tuple.TupleOffset = 0;
	tuple.TupleDataMax = 255;
	tuple.Attributes = 0;

	/* See if we have a manufacturer identifier. Use it to set is_kme for
	   vendor quirks */
	is_kme = ((pdev->manf_id == MANFID_KME) &&
		  ((pdev->card_id == PRODID_KME_KXLC005_A) ||
		   (pdev->card_id == PRODID_KME_KXLC005_B)));

	/* Not sure if this is right... look up the current Vcc */
	CS_CHECK(GetConfigurationInfo, pcmcia_get_configuration_info(pdev, &stk->conf));
/*	link->conf.Vcc = stk->conf.Vcc; */

	pass = io_base = ctl_base = 0;
	tuple.DesiredTuple = CISTPL_CFTABLE_ENTRY;
	tuple.Attributes = 0;
	CS_CHECK(GetFirstTuple, pcmcia_get_first_tuple(pdev, &tuple));

	/* Now munch the resources looking for a suitable set */
	while (1) {
		if (pcmcia_get_tuple_data(pdev, &tuple) != 0)
			goto next_entry;
		if (pcmcia_parse_tuple(pdev, &tuple, &stk->parse) != 0)
			goto next_entry;
		/* Check for matching Vcc, unless we're desperate */
		if (!pass) {
			if (cfg->vcc.present & (1 << CISTPL_POWER_VNOM)) {
				if (stk->conf.Vcc != cfg->vcc.param[CISTPL_POWER_VNOM] / 10000)
					goto next_entry;
			} else if (stk->dflt.vcc.present & (1 << CISTPL_POWER_VNOM)) {
				if (stk->conf.Vcc != stk->dflt.vcc.param[CISTPL_POWER_VNOM] / 10000)
					goto next_entry;
			}
		}

		if (cfg->vpp1.present & (1 << CISTPL_POWER_VNOM))
			pdev->conf.Vpp = cfg->vpp1.param[CISTPL_POWER_VNOM] / 10000;
		else if (stk->dflt.vpp1.present & (1 << CISTPL_POWER_VNOM))
			pdev->conf.Vpp = stk->dflt.vpp1.param[CISTPL_POWER_VNOM] / 10000;

		if ((cfg->io.nwin > 0) || (stk->dflt.io.nwin > 0)) {
			cistpl_io_t *io = (cfg->io.nwin) ? &cfg->io : &stk->dflt.io;
			pdev->conf.ConfigIndex = cfg->index;
			pdev->io.BasePort1 = io->win[0].base;
			pdev->io.IOAddrLines = io->flags & CISTPL_IO_LINES_MASK;
			if (!(io->flags & CISTPL_IO_16BIT))
				pdev->io.Attributes1 = IO_DATA_PATH_WIDTH_8;
			if (io->nwin == 2) {
				pdev->io.NumPorts1 = 8;
				pdev->io.BasePort2 = io->win[1].base;
				pdev->io.NumPorts2 = (is_kme) ? 2 : 1;
				if (pcmcia_request_io(pdev, &pdev->io) != 0)
					goto next_entry;
				io_base = pdev->io.BasePort1;
				ctl_base = pdev->io.BasePort2;
			} else if ((io->nwin == 1) && (io->win[0].len >= 16)) {
				pdev->io.NumPorts1 = io->win[0].len;
				pdev->io.NumPorts2 = 0;
				if (pcmcia_request_io(pdev, &pdev->io) != 0)
					goto next_entry;
				io_base = pdev->io.BasePort1;
				ctl_base = pdev->io.BasePort1 + 0x0e;
			} else
				goto next_entry;
			/* If we've got this far, we're done */
			break;
		}
next_entry:
		if (cfg->flags & CISTPL_CFTABLE_DEFAULT)
			memcpy(&stk->dflt, cfg, sizeof(stk->dflt));
		if (pass) {
			CS_CHECK(GetNextTuple, pcmcia_get_next_tuple(pdev, &tuple));
		} else if (pcmcia_get_next_tuple(pdev, &tuple) != 0) {
			CS_CHECK(GetFirstTuple, pcmcia_get_first_tuple(pdev, &tuple));
			memset(&stk->dflt, 0, sizeof(stk->dflt));
			pass++;
		}
	}

	CS_CHECK(RequestIRQ, pcmcia_request_irq(pdev, &pdev->irq));
	CS_CHECK(RequestConfiguration, pcmcia_request_configuration(pdev, &pdev->conf));

	/* iomap */
	ret = -ENOMEM;
	io_addr = devm_ioport_map(&pdev->dev, io_base, 8);
	ctl_addr = devm_ioport_map(&pdev->dev, ctl_base, 1);
	if (!io_addr || !ctl_addr)
		goto failed;

	/* Success. Disable the IRQ nIEN line, do quirks */
	iowrite8(0x02, ctl_addr);
	if (is_kme)
		iowrite8(0x81, ctl_addr + 0x01);

	/* FIXME: Could be more ports at base + 0x10 but we only deal with
	   one right now */
	if (pdev->io.NumPorts1 >= 0x20)
		printk(KERN_WARNING DRV_NAME ": second channel not yet supported.\n");

	/*
	 *	Having done the PCMCIA plumbing the ATA side is relatively
	 *	sane.
	 */
	ret = -ENOMEM;
	host = ata_host_alloc(&pdev->dev, 1);
	if (!host)
		goto failed;
	ap = host->ports[0];

	ap->ops = &pcmcia_port_ops;
	ap->pio_mask = 1;		/* ISA so PIO 0 cycles */
	ap->flags |= ATA_FLAG_SLAVE_POSS;
	ap->ioaddr.cmd_addr = io_addr;
	ap->ioaddr.altstatus_addr = ctl_addr;
	ap->ioaddr.ctl_addr = ctl_addr;
	ata_std_ports(&ap->ioaddr);

	ata_port_desc(ap, "cmd 0x%lx ctl 0x%lx", io_base, ctl_base);

	/* activate */
	ret = ata_host_activate(host, pdev->irq.AssignedIRQ, ata_interrupt,
				IRQF_SHARED, &pcmcia_sht);
	if (ret)
		goto failed;

	info->ndev = 1;
	kfree(stk);
	return 0;

cs_failed:
	cs_error(pdev, last_fn, last_ret);
failed:
	kfree(stk);
	info->ndev = 0;
	pcmcia_disable_device(pdev);
out1:
	kfree(info);
	return ret;
}

/**
 *	pcmcia_remove_one	-	unplug an pcmcia interface
 *	@pdev: pcmcia device
 *
 *	A PCMCIA ATA device has been unplugged. Perform the needed
 *	cleanup. Also called on module unload for any active devices.
 */

static void pcmcia_remove_one(struct pcmcia_device *pdev)
{
	struct ata_pcmcia_info *info = pdev->priv;
	struct device *dev = &pdev->dev;

	if (info != NULL) {
		/* If we have attached the device to the ATA layer, detach it */
		if (info->ndev) {
			struct ata_host *host = dev_get_drvdata(dev);
			ata_host_detach(host);
		}
		info->ndev = 0;
		pdev->priv = NULL;
	}
	pcmcia_disable_device(pdev);
	kfree(info);
}

static struct pcmcia_device_id pcmcia_devices[] = {
	PCMCIA_DEVICE_FUNC_ID(4),
	PCMCIA_DEVICE_MANF_CARD(0x0000, 0x0000),	/* Corsair */
	PCMCIA_DEVICE_MANF_CARD(0x0007, 0x0000),	/* Hitachi */
	PCMCIA_DEVICE_MANF_CARD(0x000a, 0x0000),	/* I-O Data CFA */
	PCMCIA_DEVICE_MANF_CARD(0x001c, 0x0001),	/* Mitsubishi CFA */
	PCMCIA_DEVICE_MANF_CARD(0x0032, 0x0704),
	PCMCIA_DEVICE_MANF_CARD(0x0032, 0x2904),
	PCMCIA_DEVICE_MANF_CARD(0x0045, 0x0401),	/* SanDisk CFA */
	PCMCIA_DEVICE_MANF_CARD(0x0098, 0x0000),	/* Toshiba */
	PCMCIA_DEVICE_MANF_CARD(0x00a4, 0x002d),
	PCMCIA_DEVICE_MANF_CARD(0x00ce, 0x0000),	/* Samsung */
	PCMCIA_DEVICE_MANF_CARD(0x0319, 0x0000),	/* Hitachi */
	PCMCIA_DEVICE_MANF_CARD(0x2080, 0x0001),
	PCMCIA_DEVICE_MANF_CARD(0x4e01, 0x0100),	/* Viking CFA */
	PCMCIA_DEVICE_MANF_CARD(0x4e01, 0x0200),	/* Lexar, Viking CFA */
	PCMCIA_DEVICE_PROD_ID123("Caravelle", "PSC-IDE ", "PSC000", 0x8c36137c, 0xd0693ab8, 0x2768a9f0),
	PCMCIA_DEVICE_PROD_ID123("CDROM", "IDE", "MCD-601p", 0x1b9179ca, 0xede88951, 0x0d902f74),
	PCMCIA_DEVICE_PROD_ID123("PCMCIA", "IDE CARD", "F1", 0x281f1c5d, 0x1907960c, 0xf7fde8b9),
	PCMCIA_DEVICE_PROD_ID12("ARGOSY", "CD-ROM", 0x78f308dc, 0x66536591),
	PCMCIA_DEVICE_PROD_ID12("ARGOSY", "PnPIDE", 0x78f308dc, 0x0c694728),
	PCMCIA_DEVICE_PROD_ID12("CNF CD-M", "CD-ROM", 0x7d93b852, 0x66536591),
	PCMCIA_DEVICE_PROD_ID12("Creative Technology Ltd.", "PCMCIA CD-ROM Interface Card", 0xff8c8a45, 0xfe8020c4),
	PCMCIA_DEVICE_PROD_ID12("Digital Equipment Corporation.", "Digital Mobile Media CD-ROM", 0x17692a66, 0xef1dcbde),
	PCMCIA_DEVICE_PROD_ID12("EXP", "CD+GAME", 0x6f58c983, 0x63c13aaf),
	PCMCIA_DEVICE_PROD_ID12("EXP   ", "CD-ROM", 0x0a5c52fd, 0x66536591),
	PCMCIA_DEVICE_PROD_ID12("EXP   ", "PnPIDE", 0x0a5c52fd, 0x0c694728),
	PCMCIA_DEVICE_PROD_ID12("FREECOM", "PCCARD-IDE", 0x5714cbf7, 0x48e0ab8e),
	PCMCIA_DEVICE_PROD_ID12("Hyperstone", "Model1", 0x3d5b9ef5, 0xca6ab420),
	PCMCIA_DEVICE_PROD_ID12("HITACHI", "FLASH", 0xf4f43949, 0x9eb86aae),
	PCMCIA_DEVICE_PROD_ID12("HITACHI", "microdrive", 0xf4f43949, 0xa6d76178),
	PCMCIA_DEVICE_PROD_ID12("IBM", "microdrive", 0xb569a6e5, 0xa6d76178),
	PCMCIA_DEVICE_PROD_ID12("IBM", "IBM17JSSFP20", 0xb569a6e5, 0xf2508753),
	PCMCIA_DEVICE_PROD_ID12("KINGSTON", "CF8GB", 0x2e6d1829, 0xacbe682e),
	PCMCIA_DEVICE_PROD_ID12("IO DATA", "CBIDE2      ", 0x547e66dc, 0x8671043b),
	PCMCIA_DEVICE_PROD_ID12("IO DATA", "PCIDE", 0x547e66dc, 0x5c5ab149),
	PCMCIA_DEVICE_PROD_ID12("IO DATA", "PCIDEII", 0x547e66dc, 0xb3662674),
	PCMCIA_DEVICE_PROD_ID12("LOOKMEET", "CBIDE2      ", 0xe37be2b5, 0x8671043b),
	PCMCIA_DEVICE_PROD_ID12("M-Systems", "CF500", 0x7ed2ad87, 0x7a13045c),
	PCMCIA_DEVICE_PROD_ID2("NinjaATA-", 0xebe0bd79),
	PCMCIA_DEVICE_PROD_ID12("PCMCIA", "CD-ROM", 0x281f1c5d, 0x66536591),
	PCMCIA_DEVICE_PROD_ID12("PCMCIA", "PnPIDE", 0x281f1c5d, 0x0c694728),
	PCMCIA_DEVICE_PROD_ID12("SHUTTLE TECHNOLOGY LTD.", "PCCARD-IDE/ATAPI Adapter", 0x4a3f0ba0, 0x322560e1),
	PCMCIA_DEVICE_PROD_ID12("SEAGATE", "ST1", 0x87c1b330, 0xe1f30883),
	PCMCIA_DEVICE_PROD_ID12("SAMSUNG", "04/05/06", 0x43d74cb4, 0x6a22777d),
	PCMCIA_DEVICE_PROD_ID12("SMI VENDOR", "SMI PRODUCT", 0x30896c92, 0x703cc5f6),
	PCMCIA_DEVICE_PROD_ID12("TOSHIBA", "MK2001MPL", 0xb4585a1a, 0x3489e003),
	PCMCIA_DEVICE_PROD_ID1("TRANSCEND    512M   ", 0xd0909443),
	PCMCIA_DEVICE_PROD_ID12("TRANSCEND", "TS1GCF80", 0x709b1bf1, 0x2a54d4b1),
	PCMCIA_DEVICE_PROD_ID12("TRANSCEND", "TS2GCF120", 0x709b1bf1, 0x969aa4f2),
	PCMCIA_DEVICE_PROD_ID12("TRANSCEND", "TS4GCF120", 0x709b1bf1, 0xf54a91c8),
	PCMCIA_DEVICE_PROD_ID12("WIT", "IDE16", 0x244e5994, 0x3e232852),
	PCMCIA_DEVICE_PROD_ID12("WEIDA", "TWTTI", 0xcc7cf69c, 0x212bb918),
	PCMCIA_DEVICE_PROD_ID1("STI Flash", 0xe4a13209),
	PCMCIA_DEVICE_PROD_ID12("STI", "Flash 5.0", 0xbf2df18d, 0x8cb57a0e),
	PCMCIA_MFC_DEVICE_PROD_ID12(1, "SanDisk", "ConnectPlus", 0x7a954bd9, 0x74be00c6),
	PCMCIA_DEVICE_NULL,
};

MODULE_DEVICE_TABLE(pcmcia, pcmcia_devices);

static struct pcmcia_driver pcmcia_driver = {
	.owner		= THIS_MODULE,
	.drv = {
		.name		= DRV_NAME,
	},
	.id_table	= pcmcia_devices,
	.probe		= pcmcia_init_one,
	.remove		= pcmcia_remove_one,
};

static int __init pcmcia_init(void)
{
	return pcmcia_register_driver(&pcmcia_driver);
}

static void __exit pcmcia_exit(void)
{
	pcmcia_unregister_driver(&pcmcia_driver);
}

MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("low-level driver for PCMCIA ATA");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

module_init(pcmcia_init);
module_exit(pcmcia_exit);
