// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2003 Digi International (www.digi.com)
 *	Scott H Kilau <Scott_Kilau at digi dot com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include "dgnc_driver.h"
#include "dgnc_tty.h"
#include "dgnc_cls.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Digi International, http://www.digi.com");
MODULE_DESCRIPTION("Driver for the Digi International Neo and Classic PCI based product line");
MODULE_SUPPORTED_DEVICE("dgnc");

static unsigned int dgnc_num_boards;
struct dgnc_board		*dgnc_board[MAXBOARDS];
static DEFINE_SPINLOCK(dgnc_poll_lock); /* Poll scheduling lock */

static int		dgnc_poll_tick = 20;	/* Poll interval - 20 ms */
static ulong		dgnc_poll_time; /* Time of next poll */
static uint		dgnc_poll_stop; /* Used to tell poller to stop */
static struct timer_list dgnc_poll_timer;

#define DIGI_VID				0x114F
#define PCI_DEVICE_CLASSIC_4_DID		0x0028
#define PCI_DEVICE_CLASSIC_8_DID		0x0029
#define PCI_DEVICE_CLASSIC_4_422_DID		0x00D0
#define PCI_DEVICE_CLASSIC_8_422_DID		0x00D1

#define PCI_DEVICE_CLASSIC_4_PCI_NAME		"ClassicBoard 4 PCI"
#define PCI_DEVICE_CLASSIC_8_PCI_NAME		"ClassicBoard 8 PCI"
#define PCI_DEVICE_CLASSIC_4_422_PCI_NAME	"ClassicBoard 4 422 PCI"
#define PCI_DEVICE_CLASSIC_8_422_PCI_NAME	"ClassicBoard 8 422 PCI"

static const struct pci_device_id dgnc_pci_tbl[] = {
	{PCI_DEVICE(DIGI_VID, PCI_DEVICE_CLASSIC_4_DID),     .driver_data = 0},
	{PCI_DEVICE(DIGI_VID, PCI_DEVICE_CLASSIC_4_422_DID), .driver_data = 1},
	{PCI_DEVICE(DIGI_VID, PCI_DEVICE_CLASSIC_8_DID),     .driver_data = 2},
	{PCI_DEVICE(DIGI_VID, PCI_DEVICE_CLASSIC_8_422_DID), .driver_data = 3},
	{0,}
};
MODULE_DEVICE_TABLE(pci, dgnc_pci_tbl);

struct board_id {
	unsigned char *name;
	uint maxports;
	unsigned int is_pci_express;
};

static const struct board_id dgnc_ids[] = {
	{	PCI_DEVICE_CLASSIC_4_PCI_NAME,		4,	0	},
	{	PCI_DEVICE_CLASSIC_4_422_PCI_NAME,	4,	0	},
	{	PCI_DEVICE_CLASSIC_8_PCI_NAME,		8,	0	},
	{	PCI_DEVICE_CLASSIC_8_422_PCI_NAME,	8,	0	},
	{	NULL,					0,	0	}
};

/* Remap PCI memory. */
static int dgnc_do_remap(struct dgnc_board *brd)
{
	brd->re_map_membase = ioremap(brd->membase, 0x1000);
	if (!brd->re_map_membase)
		return -ENOMEM;

	return 0;
}

/* A board has been found, initialize  it. */
static struct dgnc_board *dgnc_found_board(struct pci_dev *pdev, int id)
{
	struct dgnc_board *brd;
	unsigned int pci_irq;
	int rc = 0;

	brd = kzalloc(sizeof(*brd), GFP_KERNEL);
	if (!brd)
		return ERR_PTR(-ENOMEM);

	/* store the info for the board we've found */
	brd->boardnum = dgnc_num_boards;
	brd->device = dgnc_pci_tbl[id].device;
	brd->pdev = pdev;
	brd->name = dgnc_ids[id].name;
	brd->maxports = dgnc_ids[id].maxports;
	init_waitqueue_head(&brd->state_wait);

	spin_lock_init(&brd->bd_lock);
	spin_lock_init(&brd->bd_intr_lock);

	brd->state		= BOARD_FOUND;

	pci_irq = pdev->irq;
	brd->irq = pci_irq;

	switch (brd->device) {
	case PCI_DEVICE_CLASSIC_4_DID:
	case PCI_DEVICE_CLASSIC_8_DID:
	case PCI_DEVICE_CLASSIC_4_422_DID:
	case PCI_DEVICE_CLASSIC_8_422_DID:
		/*
		 * For PCI ClassicBoards
		 * PCI Local Address (i.e. "resource" number) space
		 * 0	PLX Memory Mapped Config
		 * 1	PLX I/O Mapped Config
		 * 2	I/O Mapped UARTs and Status
		 * 3	Memory Mapped VPD
		 * 4	Memory Mapped UARTs and Status
		 */

		brd->membase = pci_resource_start(pdev, 4);

		if (!brd->membase) {
			dev_err(&brd->pdev->dev,
				"Card has no PCI IO resources, failing.\n");
			rc = -ENODEV;
			goto failed;
		}

		brd->membase_end = pci_resource_end(pdev, 4);

		if (brd->membase & 1)
			brd->membase &= ~3;
		else
			brd->membase &= ~15;

		brd->iobase	= pci_resource_start(pdev, 1);
		brd->iobase_end = pci_resource_end(pdev, 1);
		brd->iobase	= ((unsigned int)(brd->iobase)) & 0xFFFE;

		brd->bd_ops = &dgnc_cls_ops;

		brd->bd_uart_offset = 0x8;
		brd->bd_dividend = 921600;

		rc = dgnc_do_remap(brd);
		if (rc < 0)
			goto failed;

		/*
		 * Enable Local Interrupt 1		  (0x1),
		 * Local Interrupt 1 Polarity Active high (0x2),
		 * Enable PCI interrupt			  (0x40)
		 */
		outb(0x43, brd->iobase + 0x4c);

		break;

	default:
		dev_err(&brd->pdev->dev,
			"Didn't find any compatible Neo/Classic PCI boards.\n");
		rc = -ENXIO;
		goto failed;
	}

	tasklet_init(&brd->helper_tasklet,
		     brd->bd_ops->tasklet,
		     (unsigned long)brd);

	wake_up_interruptible(&brd->state_wait);

	return brd;

failed:
	kfree(brd);

	return ERR_PTR(rc);
}

static int dgnc_request_irq(struct dgnc_board *brd)
{
	if (brd->irq) {
		int rc = request_irq(brd->irq, brd->bd_ops->intr,
				 IRQF_SHARED, "DGNC", brd);
		if (rc) {
			dev_err(&brd->pdev->dev,
				"Failed to hook IRQ %d\n", brd->irq);
			brd->state = BOARD_FAILED;
			return -ENODEV;
		}
	}
	return 0;
}

static void dgnc_free_irq(struct dgnc_board *brd)
{
	if (brd->irq)
		free_irq(brd->irq, brd);
}

 /*
  * As each timer expires, it determines (a) whether the "transmit"
  * waiter needs to be woken up, and (b) whether the poller needs to
  * be rescheduled.
  */
static void dgnc_poll_handler(struct timer_list *unused)
{
	struct dgnc_board *brd;
	unsigned long flags;
	int i;
	unsigned long new_time;

	for (i = 0; i < dgnc_num_boards; i++) {
		brd = dgnc_board[i];

		spin_lock_irqsave(&brd->bd_lock, flags);

		if (brd->state == BOARD_FAILED) {
			spin_unlock_irqrestore(&brd->bd_lock, flags);
			continue;
		}

		tasklet_schedule(&brd->helper_tasklet);

		spin_unlock_irqrestore(&brd->bd_lock, flags);
	}

	/* Schedule ourself back at the nominal wakeup interval. */

	spin_lock_irqsave(&dgnc_poll_lock, flags);
	dgnc_poll_time += dgnc_jiffies_from_ms(dgnc_poll_tick);

	new_time = dgnc_poll_time - jiffies;

	if ((ulong)new_time >= 2 * dgnc_poll_tick)
		dgnc_poll_time = jiffies + dgnc_jiffies_from_ms(dgnc_poll_tick);

	timer_setup(&dgnc_poll_timer, dgnc_poll_handler, 0);
	dgnc_poll_timer.expires = dgnc_poll_time;
	spin_unlock_irqrestore(&dgnc_poll_lock, flags);

	if (!dgnc_poll_stop)
		add_timer(&dgnc_poll_timer);
}

/* returns count (>= 0), or negative on error */
static int dgnc_init_one(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int rc;
	struct dgnc_board *brd;

	rc = pci_enable_device(pdev);
	if (rc)
		return -EIO;

	brd = dgnc_found_board(pdev, ent->driver_data);
	if (IS_ERR(brd))
		return PTR_ERR(brd);

	rc = dgnc_tty_register(brd);
	if (rc < 0) {
		pr_err(DRVSTR ": Can't register tty devices (%d)\n", rc);
		goto failed;
	}

	rc = dgnc_request_irq(brd);
	if (rc < 0) {
		pr_err(DRVSTR ": Can't finalize board init (%d)\n", rc);
		goto unregister_tty;
	}

	rc = dgnc_tty_init(brd);
	if (rc < 0) {
		pr_err(DRVSTR ": Can't init tty devices (%d)\n", rc);
		goto free_irq;
	}

	brd->state = BOARD_READY;

	dgnc_board[dgnc_num_boards++] = brd;

	return 0;

free_irq:
	dgnc_free_irq(brd);
unregister_tty:
	dgnc_tty_unregister(brd);
failed:
	kfree(brd);

	return rc;
}

static struct pci_driver dgnc_driver = {
	.name		= "dgnc",
	.probe		= dgnc_init_one,
	.id_table       = dgnc_pci_tbl,
};

static int dgnc_start(void)
{
	unsigned long flags;

	/* Start the poller */
	spin_lock_irqsave(&dgnc_poll_lock, flags);
	timer_setup(&dgnc_poll_timer, dgnc_poll_handler, 0);
	dgnc_poll_time = jiffies + dgnc_jiffies_from_ms(dgnc_poll_tick);
	dgnc_poll_timer.expires = dgnc_poll_time;
	spin_unlock_irqrestore(&dgnc_poll_lock, flags);

	add_timer(&dgnc_poll_timer);

	return 0;
}

/* Free all the memory associated with a board */
static void dgnc_cleanup_board(struct dgnc_board *brd)
{
	int i = 0;

	if (!brd)
		return;

	switch (brd->device) {
	case PCI_DEVICE_CLASSIC_4_DID:
	case PCI_DEVICE_CLASSIC_8_DID:
	case PCI_DEVICE_CLASSIC_4_422_DID:
	case PCI_DEVICE_CLASSIC_8_422_DID:

		/* Tell card not to interrupt anymore. */
		outb(0, brd->iobase + 0x4c);
		break;

	default:
		break;
	}

	if (brd->irq)
		free_irq(brd->irq, brd);

	tasklet_kill(&brd->helper_tasklet);

	if (brd->re_map_membase) {
		iounmap(brd->re_map_membase);
		brd->re_map_membase = NULL;
	}

	for (i = 0; i < MAXPORTS ; i++) {
		if (brd->channels[i]) {
			kfree(brd->channels[i]->ch_rqueue);
			kfree(brd->channels[i]->ch_equeue);
			kfree(brd->channels[i]->ch_wqueue);
			kfree(brd->channels[i]);
			brd->channels[i] = NULL;
		}
	}

	dgnc_board[brd->boardnum] = NULL;

	kfree(brd);
}

/* Driver load/unload functions */

static void cleanup(void)
{
	int i;
	unsigned long flags;

	spin_lock_irqsave(&dgnc_poll_lock, flags);
	dgnc_poll_stop = 1;
	spin_unlock_irqrestore(&dgnc_poll_lock, flags);

	/* Turn off poller right away. */
	del_timer_sync(&dgnc_poll_timer);

	for (i = 0; i < dgnc_num_boards; ++i) {
		dgnc_cleanup_tty(dgnc_board[i]);
		dgnc_cleanup_board(dgnc_board[i]);
	}
}

static void __exit dgnc_cleanup_module(void)
{
	cleanup();
	pci_unregister_driver(&dgnc_driver);
}

static int __init dgnc_init_module(void)
{
	int rc;

	/* Initialize global stuff */
	rc = dgnc_start();
	if (rc < 0)
		return rc;

	/* Find and configure all the cards */
	rc = pci_register_driver(&dgnc_driver);
	if (rc) {
		pr_warn("WARNING: dgnc driver load failed.  No Digi Neo or Classic boards found.\n");
		cleanup();
		return rc;
	}
	return 0;
}

module_init(dgnc_init_module);
module_exit(dgnc_cleanup_module);
