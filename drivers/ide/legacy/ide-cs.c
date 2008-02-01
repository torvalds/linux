/*======================================================================

    A driver for PCMCIA IDE/ATA disk cards

    The contents of this file are subject to the Mozilla Public
    License Version 1.1 (the "License"); you may not use this file
    except in compliance with the License. You may obtain a copy of
    the License at http://www.mozilla.org/MPL/

    Software distributed under the License is distributed on an "AS
    IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
    implied. See the License for the specific language governing
    rights and limitations under the License.

    The initial developer of the original code is David A. Hinds
    <dahinds@users.sourceforge.net>.  Portions created by David A. Hinds
    are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.

    Alternatively, the contents of this file may be used under the
    terms of the GNU General Public License version 2 (the "GPL"), in
    which case the provisions of the GPL are applicable instead of the
    above.  If you wish to allow the use of your version of this file
    only under the terms of the GPL and not to allow others to use
    your version of this file under the MPL, indicate your decision
    by deleting the provisions above and replace them with the notice
    and other provisions required by the GPL.  If you do not delete
    the provisions above, a recipient may use your version of this
    file under either the MPL or the GPL.

======================================================================*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/ioport.h>
#include <linux/ide.h>
#include <linux/hdreg.h>
#include <linux/major.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <asm/system.h>

#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/ds.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ciscode.h>

/*====================================================================*/

/* Module parameters */

MODULE_AUTHOR("David Hinds <dahinds@users.sourceforge.net>");
MODULE_DESCRIPTION("PCMCIA ATA/IDE card driver");
MODULE_LICENSE("Dual MPL/GPL");

#define INT_MODULE_PARM(n, v) static int n = v; module_param(n, int, 0)

#ifdef PCMCIA_DEBUG
INT_MODULE_PARM(pc_debug, PCMCIA_DEBUG);
#define DEBUG(n, args...) if (pc_debug>(n)) printk(KERN_DEBUG args)
static char *version =
"ide-cs.c 1.3 2002/10/26 05:45:31 (David Hinds)";
#else
#define DEBUG(n, args...)
#endif

/*====================================================================*/

static const char ide_major[] = {
    IDE0_MAJOR, IDE1_MAJOR, IDE2_MAJOR, IDE3_MAJOR,
    IDE4_MAJOR, IDE5_MAJOR
};

typedef struct ide_info_t {
	struct pcmcia_device	*p_dev;
    int		ndev;
    dev_node_t	node;
    int		hd;
} ide_info_t;

static void ide_release(struct pcmcia_device *);
static int ide_config(struct pcmcia_device *);

static void ide_detach(struct pcmcia_device *p_dev);




/*======================================================================

    ide_attach() creates an "instance" of the driver, allocating
    local data structures for one device.  The device is registered
    with Card Services.

======================================================================*/

static int ide_probe(struct pcmcia_device *link)
{
    ide_info_t *info;

    DEBUG(0, "ide_attach()\n");

    /* Create new ide device */
    info = kzalloc(sizeof(*info), GFP_KERNEL);
    if (!info)
	return -ENOMEM;

    info->p_dev = link;
    link->priv = info;

    link->io.Attributes1 = IO_DATA_PATH_WIDTH_AUTO;
    link->io.Attributes2 = IO_DATA_PATH_WIDTH_8;
    link->io.IOAddrLines = 3;
    link->irq.Attributes = IRQ_TYPE_DYNAMIC_SHARING;
    link->irq.IRQInfo1 = IRQ_LEVEL_ID;
    link->conf.Attributes = CONF_ENABLE_IRQ;
    link->conf.IntType = INT_MEMORY_AND_IO;

    return ide_config(link);
} /* ide_attach */

/*======================================================================

    This deletes a driver "instance".  The device is de-registered
    with Card Services.  If it has been released, all local data
    structures are freed.  Otherwise, the structures will be freed
    when the device is released.

======================================================================*/

static void ide_detach(struct pcmcia_device *link)
{
    DEBUG(0, "ide_detach(0x%p)\n", link);

    ide_release(link);

    kfree(link->priv);
} /* ide_detach */

static int idecs_register(unsigned long io, unsigned long ctl, unsigned long irq, struct pcmcia_device *handle)
{
    hw_regs_t hw;
    memset(&hw, 0, sizeof(hw));
    ide_init_hwif_ports(&hw, io, ctl, NULL);
    hw.irq = irq;
    hw.chipset = ide_pci;
    hw.dev = &handle->dev;
    return ide_register_hw(&hw, &ide_undecoded_slave, NULL);
}

/*======================================================================

    ide_config() is scheduled to run after a CARD_INSERTION event
    is received, to configure the PCMCIA socket, and to make the
    ide device available to the system.

======================================================================*/

#define CS_CHECK(fn, ret) \
do { last_fn = (fn); if ((last_ret = (ret)) != 0) goto cs_failed; } while (0)

static int ide_config(struct pcmcia_device *link)
{
    ide_info_t *info = link->priv;
    tuple_t tuple;
    struct {
	u_short		buf[128];
	cisparse_t	parse;
	config_info_t	conf;
	cistpl_cftable_entry_t dflt;
    } *stk = NULL;
    cistpl_cftable_entry_t *cfg;
    int i, pass, last_ret = 0, last_fn = 0, hd, is_kme = 0;
    unsigned long io_base, ctl_base;

    DEBUG(0, "ide_config(0x%p)\n", link);

    stk = kzalloc(sizeof(*stk), GFP_KERNEL);
    if (!stk) goto err_mem;
    cfg = &stk->parse.cftable_entry;

    tuple.TupleData = (cisdata_t *)&stk->buf;
    tuple.TupleOffset = 0;
    tuple.TupleDataMax = 255;
    tuple.Attributes = 0;

    is_kme = ((link->manf_id == MANFID_KME) &&
	      ((link->card_id == PRODID_KME_KXLC005_A) ||
	       (link->card_id == PRODID_KME_KXLC005_B)));

    /* Not sure if this is right... look up the current Vcc */
    CS_CHECK(GetConfigurationInfo, pcmcia_get_configuration_info(link, &stk->conf));

    pass = io_base = ctl_base = 0;
    tuple.DesiredTuple = CISTPL_CFTABLE_ENTRY;
    tuple.Attributes = 0;
    CS_CHECK(GetFirstTuple, pcmcia_get_first_tuple(link, &tuple));
    while (1) {
    	if (pcmcia_get_tuple_data(link, &tuple) != 0) goto next_entry;
	if (pcmcia_parse_tuple(link, &tuple, &stk->parse) != 0) goto next_entry;

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
	    link->conf.Vpp =
		cfg->vpp1.param[CISTPL_POWER_VNOM] / 10000;
	else if (stk->dflt.vpp1.present & (1 << CISTPL_POWER_VNOM))
	    link->conf.Vpp =
		stk->dflt.vpp1.param[CISTPL_POWER_VNOM] / 10000;

	if ((cfg->io.nwin > 0) || (stk->dflt.io.nwin > 0)) {
	    cistpl_io_t *io = (cfg->io.nwin) ? &cfg->io : &stk->dflt.io;
	    link->conf.ConfigIndex = cfg->index;
	    link->io.BasePort1 = io->win[0].base;
	    link->io.IOAddrLines = io->flags & CISTPL_IO_LINES_MASK;
	    if (!(io->flags & CISTPL_IO_16BIT))
		link->io.Attributes1 = IO_DATA_PATH_WIDTH_8;
	    if (io->nwin == 2) {
		link->io.NumPorts1 = 8;
		link->io.BasePort2 = io->win[1].base;
		link->io.NumPorts2 = (is_kme) ? 2 : 1;
		if (pcmcia_request_io(link, &link->io) != 0)
			goto next_entry;
		io_base = link->io.BasePort1;
		ctl_base = link->io.BasePort2;
	    } else if ((io->nwin == 1) && (io->win[0].len >= 16)) {
		link->io.NumPorts1 = io->win[0].len;
		link->io.NumPorts2 = 0;
		if (pcmcia_request_io(link, &link->io) != 0)
			goto next_entry;
		io_base = link->io.BasePort1;
		ctl_base = link->io.BasePort1 + 0x0e;
	    } else goto next_entry;
	    /* If we've got this far, we're done */
	    break;
	}

    next_entry:
	if (cfg->flags & CISTPL_CFTABLE_DEFAULT)
	    memcpy(&stk->dflt, cfg, sizeof(stk->dflt));
	if (pass) {
	    CS_CHECK(GetNextTuple, pcmcia_get_next_tuple(link, &tuple));
	} else if (pcmcia_get_next_tuple(link, &tuple) != 0) {
	    CS_CHECK(GetFirstTuple, pcmcia_get_first_tuple(link, &tuple));
	    memset(&stk->dflt, 0, sizeof(stk->dflt));
	    pass++;
	}
    }

    CS_CHECK(RequestIRQ, pcmcia_request_irq(link, &link->irq));
    CS_CHECK(RequestConfiguration, pcmcia_request_configuration(link, &link->conf));

    /* disable drive interrupts during IDE probe */
    outb(0x02, ctl_base);

    /* special setup for KXLC005 card */
    if (is_kme)
	outb(0x81, ctl_base+1);

    /* retry registration in case device is still spinning up */
    for (hd = -1, i = 0; i < 10; i++) {
	hd = idecs_register(io_base, ctl_base, link->irq.AssignedIRQ, link);
	if (hd >= 0) break;
	if (link->io.NumPorts1 == 0x20) {
	    outb(0x02, ctl_base + 0x10);
	    hd = idecs_register(io_base + 0x10, ctl_base + 0x10,
				link->irq.AssignedIRQ, link);
	    if (hd >= 0) {
		io_base += 0x10;
		ctl_base += 0x10;
		break;
	    }
	}
	msleep(100);
    }

    if (hd < 0) {
	printk(KERN_NOTICE "ide-cs: ide_register() at 0x%3lx & 0x%3lx"
	       ", irq %u failed\n", io_base, ctl_base,
	       link->irq.AssignedIRQ);
	goto failed;
    }

    info->ndev = 1;
    sprintf(info->node.dev_name, "hd%c", 'a' + (hd * 2));
    info->node.major = ide_major[hd];
    info->node.minor = 0;
    info->hd = hd;
    link->dev_node = &info->node;
    printk(KERN_INFO "ide-cs: %s: Vpp = %d.%d\n",
	   info->node.dev_name, link->conf.Vpp / 10, link->conf.Vpp % 10);

    kfree(stk);
    return 0;

err_mem:
    printk(KERN_NOTICE "ide-cs: ide_config failed memory allocation\n");
    goto failed;

cs_failed:
    cs_error(link, last_fn, last_ret);
failed:
    kfree(stk);
    ide_release(link);
    return -ENODEV;
} /* ide_config */

/*======================================================================

    After a card is removed, ide_release() will unregister the net
    device, and release the PCMCIA configuration.  If the device is
    still open, this will be postponed until it is closed.

======================================================================*/

void ide_release(struct pcmcia_device *link)
{
    ide_info_t *info = link->priv;

    DEBUG(0, "ide_release(0x%p)\n", link);

    if (info->ndev) {
	/* FIXME: if this fails we need to queue the cleanup somehow
	   -- need to investigate the required PCMCIA magic */
	ide_unregister(info->hd);
    }
    info->ndev = 0;

    pcmcia_disable_device(link);
} /* ide_release */


/*======================================================================

    The card status event handler.  Mostly, this schedules other
    stuff to run after an event is received.  A CARD_REMOVAL event
    also sets some flags to discourage the ide drivers from
    talking to the ports.

======================================================================*/

static struct pcmcia_device_id ide_ids[] = {
	PCMCIA_DEVICE_FUNC_ID(4),
	PCMCIA_DEVICE_MANF_CARD(0x0000, 0x0000),	/* Corsair */
	PCMCIA_DEVICE_MANF_CARD(0x0007, 0x0000),	/* Hitachi */
	PCMCIA_DEVICE_MANF_CARD(0x000a, 0x0000),	/* I-O Data CFA */
	PCMCIA_DEVICE_MANF_CARD(0x001c, 0x0001),	/* Mitsubishi CFA */
	PCMCIA_DEVICE_MANF_CARD(0x0032, 0x0704),
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
	PCMCIA_DEVICE_PROD_ID12("HITACHI", "FLASH", 0xf4f43949, 0x9eb86aae),
	PCMCIA_DEVICE_PROD_ID12("HITACHI", "microdrive", 0xf4f43949, 0xa6d76178),
	PCMCIA_DEVICE_PROD_ID12("Hyperstone", "Model1", 0x3d5b9ef5, 0xca6ab420),
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
MODULE_DEVICE_TABLE(pcmcia, ide_ids);

static struct pcmcia_driver ide_cs_driver = {
	.owner		= THIS_MODULE,
	.drv		= {
		.name	= "ide-cs",
	},
	.probe		= ide_probe,
	.remove		= ide_detach,
	.id_table       = ide_ids,
};

static int __init init_ide_cs(void)
{
	return pcmcia_register_driver(&ide_cs_driver);
}

static void __exit exit_ide_cs(void)
{
	pcmcia_unregister_driver(&ide_cs_driver);
}

late_initcall(init_ide_cs);
module_exit(exit_ide_cs);
