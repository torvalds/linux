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
#define DRV_VERSION "0.2.11"

/*
 *	Private data structure to glue stuff together
 */

struct ata_pcmcia_info {
	struct pcmcia_device *pdev;
	int		ndev;
	dev_node_t	node;
};

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
	.port_disable	= ata_port_disable,
	.tf_load	= ata_tf_load,
	.tf_read	= ata_tf_read,
	.check_status 	= ata_check_status,
	.exec_command	= ata_exec_command,
	.dev_select 	= ata_std_dev_select,

	.freeze		= ata_bmdma_freeze,
	.thaw		= ata_bmdma_thaw,
	.error_handler	= ata_bmdma_error_handler,
	.post_internal_cmd = ata_bmdma_post_internal_cmd,

	.qc_prep 	= ata_qc_prep,
	.qc_issue	= ata_qc_issue_prot,

	.data_xfer	= ata_pio_data_xfer_noirq,

	.irq_handler	= ata_interrupt,
	.irq_clear	= ata_bmdma_irq_clear,

	.port_start	= ata_port_start,
	.port_stop	= ata_port_stop,
	.host_stop	= ata_host_stop
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
	struct ata_probe_ent ae;
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
	tuple.DesiredTuple = CISTPL_CONFIG;

	CS_CHECK(GetFirstTuple, pcmcia_get_first_tuple(pdev, &tuple));
	CS_CHECK(GetTupleData, pcmcia_get_tuple_data(pdev, &tuple));
	CS_CHECK(ParseTuple, pcmcia_parse_tuple(pdev, &tuple, &stk->parse));
	pdev->conf.ConfigBase = stk->parse.config.base;
	pdev->conf.Present = stk->parse.config.rmask[0];

	/* See if we have a manufacturer identifier. Use it to set is_kme for
	   vendor quirks */
	tuple.DesiredTuple = CISTPL_MANFID;
	if (!pcmcia_get_first_tuple(pdev, &tuple) && !pcmcia_get_tuple_data(pdev, &tuple) && !pcmcia_parse_tuple(pdev, &tuple, &stk->parse))
			is_kme = ((stk->parse.manfid.manf == MANFID_KME) && ((stk->parse.manfid.card == PRODID_KME_KXLC005_A) || (stk->parse.manfid.card == PRODID_KME_KXLC005_B)));

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
			} else goto next_entry;
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

	/* Success. Disable the IRQ nIEN line, do quirks */
	outb(0x02, ctl_base);
	if (is_kme)
		outb(0x81, ctl_base + 0x01);

	/* FIXME: Could be more ports at base + 0x10 but we only deal with
	   one right now */
	if (pdev->io.NumPorts1 >= 0x20)
		printk(KERN_WARNING DRV_NAME ": second channel not yet supported.\n");

	/*
 	 *	Having done the PCMCIA plumbing the ATA side is relatively
 	 *	sane.
	 */

	memset(&ae, 0, sizeof(struct ata_probe_ent));
	INIT_LIST_HEAD(&ae.node);
	ae.dev = &pdev->dev;
	ae.port_ops = &pcmcia_port_ops;
	ae.sht = &pcmcia_sht;
	ae.n_ports = 1;
	ae.pio_mask = 1;		/* ISA so PIO 0 cycles */
	ae.irq = pdev->irq.AssignedIRQ;
	ae.irq_flags = SA_SHIRQ;
	ae.port_flags = ATA_FLAG_SLAVE_POSS | ATA_FLAG_SRST;
	ae.port[0].cmd_addr = io_base;
	ae.port[0].altstatus_addr = ctl_base;
	ae.port[0].ctl_addr = ctl_base;
	ata_std_ports(&ae.port[0]);

	if (ata_device_add(&ae) == 0)
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
			ata_host_remove(host);
			dev_set_drvdata(dev, NULL);
		}
		info->ndev = 0;
		pdev->priv = NULL;
	}
	pcmcia_disable_device(pdev);
	kfree(info);
}

static struct pcmcia_device_id pcmcia_devices[] = {
	PCMCIA_DEVICE_FUNC_ID(4),
	PCMCIA_DEVICE_MANF_CARD(0x0007, 0x0000),	/* Hitachi */
	PCMCIA_DEVICE_MANF_CARD(0x0032, 0x0704),
	PCMCIA_DEVICE_MANF_CARD(0x0045, 0x0401),
	PCMCIA_DEVICE_MANF_CARD(0x0098, 0x0000),	/* Toshiba */
	PCMCIA_DEVICE_MANF_CARD(0x00a4, 0x002d),
	PCMCIA_DEVICE_MANF_CARD(0x00ce, 0x0000),	/* Samsung */
 	PCMCIA_DEVICE_MANF_CARD(0x0319, 0x0000),	/* Hitachi */
	PCMCIA_DEVICE_MANF_CARD(0x2080, 0x0001),
	PCMCIA_DEVICE_MANF_CARD(0x4e01, 0x0200),	/* Lexar */
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
	PCMCIA_DEVICE_PROD_ID12("HITACHI", "FLASH", 0xf4f43949, 0x9eb86aae),
	PCMCIA_DEVICE_PROD_ID12("HITACHI", "microdrive", 0xf4f43949, 0xa6d76178),
	PCMCIA_DEVICE_PROD_ID12("IBM", "microdrive", 0xb569a6e5, 0xa6d76178),
	PCMCIA_DEVICE_PROD_ID12("IBM", "IBM17JSSFP20", 0xb569a6e5, 0xf2508753),
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
