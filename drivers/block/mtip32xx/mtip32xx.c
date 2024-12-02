// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for the Micron P320 SSD
 *   Copyright (C) 2011 Micron Technology, Inc.
 *
 * Portions of this code were derived from works subjected to the
 * following copyright:
 *    Copyright (C) 2009 Integrated Device Technology, Inc.
 */

#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/ata.h>
#include <linux/delay.h>
#include <linux/hdreg.h>
#include <linux/uaccess.h>
#include <linux/random.h>
#include <linux/smp.h>
#include <linux/compat.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/bio.h>
#include <linux/dma-mapping.h>
#include <linux/idr.h>
#include <linux/kthread.h>
#include <../drivers/ata/ahci.h>
#include <linux/export.h>
#include <linux/debugfs.h>
#include <linux/prefetch.h>
#include <linux/numa.h>
#include "mtip32xx.h"

#define HW_CMD_SLOT_SZ		(MTIP_MAX_COMMAND_SLOTS * 32)

/* DMA region containing RX Fis, Identify, RLE10, and SMART buffers */
#define AHCI_RX_FIS_SZ          0x100
#define AHCI_RX_FIS_OFFSET      0x0
#define AHCI_IDFY_SZ            ATA_SECT_SIZE
#define AHCI_IDFY_OFFSET        0x400
#define AHCI_SECTBUF_SZ         ATA_SECT_SIZE
#define AHCI_SECTBUF_OFFSET     0x800
#define AHCI_SMARTBUF_SZ        ATA_SECT_SIZE
#define AHCI_SMARTBUF_OFFSET    0xC00
/* 0x100 + 0x200 + 0x200 + 0x200 is smaller than 4k but we pad it out */
#define BLOCK_DMA_ALLOC_SZ      4096

/* DMA region containing command table (should be 8192 bytes) */
#define AHCI_CMD_SLOT_SZ        sizeof(struct mtip_cmd_hdr)
#define AHCI_CMD_TBL_SZ         (MTIP_MAX_COMMAND_SLOTS * AHCI_CMD_SLOT_SZ)
#define AHCI_CMD_TBL_OFFSET     0x0

/* DMA region per command (contains header and SGL) */
#define AHCI_CMD_TBL_HDR_SZ     0x80
#define AHCI_CMD_TBL_HDR_OFFSET 0x0
#define AHCI_CMD_TBL_SGL_SZ     (MTIP_MAX_SG * sizeof(struct mtip_cmd_sg))
#define AHCI_CMD_TBL_SGL_OFFSET AHCI_CMD_TBL_HDR_SZ
#define CMD_DMA_ALLOC_SZ        (AHCI_CMD_TBL_SGL_SZ + AHCI_CMD_TBL_HDR_SZ)


#define HOST_CAP_NZDMA		(1 << 19)
#define HOST_HSORG		0xFC
#define HSORG_DISABLE_SLOTGRP_INTR (1<<24)
#define HSORG_DISABLE_SLOTGRP_PXIS (1<<16)
#define HSORG_HWREV		0xFF00
#define HSORG_STYLE		0x8
#define HSORG_SLOTGROUPS	0x7

#define PORT_COMMAND_ISSUE	0x38
#define PORT_SDBV		0x7C

#define PORT_OFFSET		0x100
#define PORT_MEM_SIZE		0x80

#define PORT_IRQ_ERR \
	(PORT_IRQ_HBUS_ERR | PORT_IRQ_IF_ERR | PORT_IRQ_CONNECT | \
	 PORT_IRQ_PHYRDY | PORT_IRQ_UNK_FIS | PORT_IRQ_BAD_PMP | \
	 PORT_IRQ_TF_ERR | PORT_IRQ_HBUS_DATA_ERR | PORT_IRQ_IF_NONFATAL | \
	 PORT_IRQ_OVERFLOW)
#define PORT_IRQ_LEGACY \
	(PORT_IRQ_PIOS_FIS | PORT_IRQ_D2H_REG_FIS)
#define PORT_IRQ_HANDLED \
	(PORT_IRQ_SDB_FIS | PORT_IRQ_LEGACY | \
	 PORT_IRQ_TF_ERR | PORT_IRQ_IF_ERR | \
	 PORT_IRQ_CONNECT | PORT_IRQ_PHYRDY)
#define DEF_PORT_IRQ \
	(PORT_IRQ_ERR | PORT_IRQ_LEGACY | PORT_IRQ_SDB_FIS)

/* product numbers */
#define MTIP_PRODUCT_UNKNOWN	0x00
#define MTIP_PRODUCT_ASICFPGA	0x11

/* Device instance number, incremented each time a device is probed. */
static int instance;

/*
 * Global variable used to hold the major block device number
 * allocated in mtip_init().
 */
static int mtip_major;
static struct dentry *dfs_parent;

static u32 cpu_use[NR_CPUS];

static DEFINE_IDA(rssd_index_ida);

static int mtip_block_initialize(struct driver_data *dd);

#ifdef CONFIG_COMPAT
struct mtip_compat_ide_task_request_s {
	__u8		io_ports[8];
	__u8		hob_ports[8];
	ide_reg_valid_t	out_flags;
	ide_reg_valid_t	in_flags;
	int		data_phase;
	int		req_cmd;
	compat_ulong_t	out_size;
	compat_ulong_t	in_size;
};
#endif

/*
 * This function check_for_surprise_removal is called
 * while card is removed from the system and it will
 * read the vendor id from the configuration space
 *
 * @pdev Pointer to the pci_dev structure.
 *
 * return value
 *	 true if device removed, else false
 */
static bool mtip_check_surprise_removal(struct driver_data *dd)
{
	u16 vendor_id = 0;

	if (dd->sr)
		return true;

       /* Read the vendorID from the configuration space */
	pci_read_config_word(dd->pdev, 0x00, &vendor_id);
	if (vendor_id == 0xFFFF) {
		dd->sr = true;
		if (dd->disk)
			blk_mark_disk_dead(dd->disk);
		return true; /* device removed */
	}

	return false; /* device present */
}

static struct mtip_cmd *mtip_cmd_from_tag(struct driver_data *dd,
					  unsigned int tag)
{
	return blk_mq_rq_to_pdu(blk_mq_tag_to_rq(dd->tags.tags[0], tag));
}

/*
 * Reset the HBA (without sleeping)
 *
 * @dd Pointer to the driver data structure.
 *
 * return value
 *	0	The reset was successful.
 *	-1	The HBA Reset bit did not clear.
 */
static int mtip_hba_reset(struct driver_data *dd)
{
	unsigned long timeout;

	/* Set the reset bit */
	writel(HOST_RESET, dd->mmio + HOST_CTL);

	/* Flush */
	readl(dd->mmio + HOST_CTL);

	/*
	 * Spin for up to 10 seconds waiting for reset acknowledgement. Spec
	 * is 1 sec but in LUN failure conditions, up to 10 secs are required
	 */
	timeout = jiffies + msecs_to_jiffies(10000);
	do {
		mdelay(10);
		if (test_bit(MTIP_DDF_REMOVE_PENDING_BIT, &dd->dd_flag))
			return -1;

	} while ((readl(dd->mmio + HOST_CTL) & HOST_RESET)
		 && time_before(jiffies, timeout));

	if (readl(dd->mmio + HOST_CTL) & HOST_RESET)
		return -1;

	return 0;
}

/*
 * Issue a command to the hardware.
 *
 * Set the appropriate bit in the s_active and Command Issue hardware
 * registers, causing hardware command processing to begin.
 *
 * @port Pointer to the port structure.
 * @tag  The tag of the command to be issued.
 *
 * return value
 *      None
 */
static inline void mtip_issue_ncq_command(struct mtip_port *port, int tag)
{
	int group = tag >> 5;

	/* guard SACT and CI registers */
	spin_lock(&port->cmd_issue_lock[group]);
	writel((1 << MTIP_TAG_BIT(tag)),
			port->s_active[MTIP_TAG_INDEX(tag)]);
	writel((1 << MTIP_TAG_BIT(tag)),
			port->cmd_issue[MTIP_TAG_INDEX(tag)]);
	spin_unlock(&port->cmd_issue_lock[group]);
}

/*
 * Enable/disable the reception of FIS
 *
 * @port   Pointer to the port data structure
 * @enable 1 to enable, 0 to disable
 *
 * return value
 *	Previous state: 1 enabled, 0 disabled
 */
static int mtip_enable_fis(struct mtip_port *port, int enable)
{
	u32 tmp;

	/* enable FIS reception */
	tmp = readl(port->mmio + PORT_CMD);
	if (enable)
		writel(tmp | PORT_CMD_FIS_RX, port->mmio + PORT_CMD);
	else
		writel(tmp & ~PORT_CMD_FIS_RX, port->mmio + PORT_CMD);

	/* Flush */
	readl(port->mmio + PORT_CMD);

	return (((tmp & PORT_CMD_FIS_RX) == PORT_CMD_FIS_RX));
}

/*
 * Enable/disable the DMA engine
 *
 * @port   Pointer to the port data structure
 * @enable 1 to enable, 0 to disable
 *
 * return value
 *	Previous state: 1 enabled, 0 disabled.
 */
static int mtip_enable_engine(struct mtip_port *port, int enable)
{
	u32 tmp;

	/* enable FIS reception */
	tmp = readl(port->mmio + PORT_CMD);
	if (enable)
		writel(tmp | PORT_CMD_START, port->mmio + PORT_CMD);
	else
		writel(tmp & ~PORT_CMD_START, port->mmio + PORT_CMD);

	readl(port->mmio + PORT_CMD);
	return (((tmp & PORT_CMD_START) == PORT_CMD_START));
}

/*
 * Enables the port DMA engine and FIS reception.
 *
 * return value
 *	None
 */
static inline void mtip_start_port(struct mtip_port *port)
{
	/* Enable FIS reception */
	mtip_enable_fis(port, 1);

	/* Enable the DMA engine */
	mtip_enable_engine(port, 1);
}

/*
 * Deinitialize a port by disabling port interrupts, the DMA engine,
 * and FIS reception.
 *
 * @port Pointer to the port structure
 *
 * return value
 *	None
 */
static inline void mtip_deinit_port(struct mtip_port *port)
{
	/* Disable interrupts on this port */
	writel(0, port->mmio + PORT_IRQ_MASK);

	/* Disable the DMA engine */
	mtip_enable_engine(port, 0);

	/* Disable FIS reception */
	mtip_enable_fis(port, 0);
}

/*
 * Initialize a port.
 *
 * This function deinitializes the port by calling mtip_deinit_port() and
 * then initializes it by setting the command header and RX FIS addresses,
 * clearing the SError register and any pending port interrupts before
 * re-enabling the default set of port interrupts.
 *
 * @port Pointer to the port structure.
 *
 * return value
 *	None
 */
static void mtip_init_port(struct mtip_port *port)
{
	int i;
	mtip_deinit_port(port);

	/* Program the command list base and FIS base addresses */
	if (readl(port->dd->mmio + HOST_CAP) & HOST_CAP_64) {
		writel((port->command_list_dma >> 16) >> 16,
			 port->mmio + PORT_LST_ADDR_HI);
		writel((port->rxfis_dma >> 16) >> 16,
			 port->mmio + PORT_FIS_ADDR_HI);
		set_bit(MTIP_PF_HOST_CAP_64, &port->flags);
	}

	writel(port->command_list_dma & 0xFFFFFFFF,
			port->mmio + PORT_LST_ADDR);
	writel(port->rxfis_dma & 0xFFFFFFFF, port->mmio + PORT_FIS_ADDR);

	/* Clear SError */
	writel(readl(port->mmio + PORT_SCR_ERR), port->mmio + PORT_SCR_ERR);

	/* reset the completed registers.*/
	for (i = 0; i < port->dd->slot_groups; i++)
		writel(0xFFFFFFFF, port->completed[i]);

	/* Clear any pending interrupts for this port */
	writel(readl(port->mmio + PORT_IRQ_STAT), port->mmio + PORT_IRQ_STAT);

	/* Clear any pending interrupts on the HBA. */
	writel(readl(port->dd->mmio + HOST_IRQ_STAT),
					port->dd->mmio + HOST_IRQ_STAT);

	/* Enable port interrupts */
	writel(DEF_PORT_IRQ, port->mmio + PORT_IRQ_MASK);
}

/*
 * Restart a port
 *
 * @port Pointer to the port data structure.
 *
 * return value
 *	None
 */
static void mtip_restart_port(struct mtip_port *port)
{
	unsigned long timeout;

	/* Disable the DMA engine */
	mtip_enable_engine(port, 0);

	/* Chip quirk: wait up to 500ms for PxCMD.CR == 0 */
	timeout = jiffies + msecs_to_jiffies(500);
	while ((readl(port->mmio + PORT_CMD) & PORT_CMD_LIST_ON)
		 && time_before(jiffies, timeout))
		;

	if (test_bit(MTIP_DDF_REMOVE_PENDING_BIT, &port->dd->dd_flag))
		return;

	/*
	 * Chip quirk: escalate to hba reset if
	 * PxCMD.CR not clear after 500 ms
	 */
	if (readl(port->mmio + PORT_CMD) & PORT_CMD_LIST_ON) {
		dev_warn(&port->dd->pdev->dev,
			"PxCMD.CR not clear, escalating reset\n");

		if (mtip_hba_reset(port->dd))
			dev_err(&port->dd->pdev->dev,
				"HBA reset escalation failed.\n");

		/* 30 ms delay before com reset to quiesce chip */
		mdelay(30);
	}

	dev_warn(&port->dd->pdev->dev, "Issuing COM reset\n");

	/* Set PxSCTL.DET */
	writel(readl(port->mmio + PORT_SCR_CTL) |
			 1, port->mmio + PORT_SCR_CTL);
	readl(port->mmio + PORT_SCR_CTL);

	/* Wait 1 ms to quiesce chip function */
	timeout = jiffies + msecs_to_jiffies(1);
	while (time_before(jiffies, timeout))
		;

	if (test_bit(MTIP_DDF_REMOVE_PENDING_BIT, &port->dd->dd_flag))
		return;

	/* Clear PxSCTL.DET */
	writel(readl(port->mmio + PORT_SCR_CTL) & ~1,
			 port->mmio + PORT_SCR_CTL);
	readl(port->mmio + PORT_SCR_CTL);

	/* Wait 500 ms for bit 0 of PORT_SCR_STS to be set */
	timeout = jiffies + msecs_to_jiffies(500);
	while (((readl(port->mmio + PORT_SCR_STAT) & 0x01) == 0)
			 && time_before(jiffies, timeout))
		;

	if (test_bit(MTIP_DDF_REMOVE_PENDING_BIT, &port->dd->dd_flag))
		return;

	if ((readl(port->mmio + PORT_SCR_STAT) & 0x01) == 0)
		dev_warn(&port->dd->pdev->dev,
			"COM reset failed\n");

	mtip_init_port(port);
	mtip_start_port(port);

}

static int mtip_device_reset(struct driver_data *dd)
{
	int rv = 0;

	if (mtip_check_surprise_removal(dd))
		return 0;

	if (mtip_hba_reset(dd) < 0)
		rv = -EFAULT;

	mdelay(1);
	mtip_init_port(dd->port);
	mtip_start_port(dd->port);

	/* Enable interrupts on the HBA. */
	writel(readl(dd->mmio + HOST_CTL) | HOST_IRQ_EN,
					dd->mmio + HOST_CTL);
	return rv;
}

/*
 * Helper function for tag logging
 */
static void print_tags(struct driver_data *dd,
			char *msg,
			unsigned long *tagbits,
			int cnt)
{
	unsigned char tagmap[128];
	int group, tagmap_len = 0;

	memset(tagmap, 0, sizeof(tagmap));
	for (group = SLOTBITS_IN_LONGS; group > 0; group--)
		tagmap_len += sprintf(tagmap + tagmap_len, "%016lX ",
						tagbits[group-1]);
	dev_warn(&dd->pdev->dev,
			"%d command(s) %s: tagmap [%s]", cnt, msg, tagmap);
}

static int mtip_read_log_page(struct mtip_port *port, u8 page, u16 *buffer,
				dma_addr_t buffer_dma, unsigned int sectors);
static int mtip_get_smart_attr(struct mtip_port *port, unsigned int id,
						struct smart_attr *attrib);

static void mtip_complete_command(struct mtip_cmd *cmd, blk_status_t status)
{
	struct request *req = blk_mq_rq_from_pdu(cmd);

	cmd->status = status;
	if (likely(!blk_should_fake_timeout(req->q)))
		blk_mq_complete_request(req);
}

/*
 * Handle an error.
 *
 * @dd Pointer to the DRIVER_DATA structure.
 *
 * return value
 *	None
 */
static void mtip_handle_tfe(struct driver_data *dd)
{
	int group, tag, bit, reissue, rv;
	struct mtip_port *port;
	struct mtip_cmd  *cmd;
	u32 completed;
	struct host_to_dev_fis *fis;
	unsigned long tagaccum[SLOTBITS_IN_LONGS];
	unsigned int cmd_cnt = 0;
	unsigned char *buf;
	char *fail_reason = NULL;
	int fail_all_ncq_write = 0, fail_all_ncq_cmds = 0;

	dev_warn(&dd->pdev->dev, "Taskfile error\n");

	port = dd->port;

	if (test_bit(MTIP_PF_IC_ACTIVE_BIT, &port->flags)) {
		cmd = mtip_cmd_from_tag(dd, MTIP_TAG_INTERNAL);
		dbg_printk(MTIP_DRV_NAME " TFE for the internal command\n");
		mtip_complete_command(cmd, BLK_STS_IOERR);
		return;
	}

	/* clear the tag accumulator */
	memset(tagaccum, 0, SLOTBITS_IN_LONGS * sizeof(long));

	/* Loop through all the groups */
	for (group = 0; group < dd->slot_groups; group++) {
		completed = readl(port->completed[group]);

		dev_warn(&dd->pdev->dev, "g=%u, comp=%x\n", group, completed);

		/* clear completed status register in the hardware.*/
		writel(completed, port->completed[group]);

		/* Process successfully completed commands */
		for (bit = 0; bit < 32 && completed; bit++) {
			if (!(completed & (1<<bit)))
				continue;
			tag = (group << 5) + bit;

			/* Skip the internal command slot */
			if (tag == MTIP_TAG_INTERNAL)
				continue;

			cmd = mtip_cmd_from_tag(dd, tag);
			mtip_complete_command(cmd, 0);
			set_bit(tag, tagaccum);
			cmd_cnt++;
		}
	}

	print_tags(dd, "completed (TFE)", tagaccum, cmd_cnt);

	/* Restart the port */
	mdelay(20);
	mtip_restart_port(port);

	/* Trying to determine the cause of the error */
	rv = mtip_read_log_page(dd->port, ATA_LOG_SATA_NCQ,
				dd->port->log_buf,
				dd->port->log_buf_dma, 1);
	if (rv) {
		dev_warn(&dd->pdev->dev,
			"Error in READ LOG EXT (10h) command\n");
		/* non-critical error, don't fail the load */
	} else {
		buf = (unsigned char *)dd->port->log_buf;
		if (buf[259] & 0x1) {
			dev_info(&dd->pdev->dev,
				"Write protect bit is set.\n");
			set_bit(MTIP_DDF_WRITE_PROTECT_BIT, &dd->dd_flag);
			fail_all_ncq_write = 1;
			fail_reason = "write protect";
		}
		if (buf[288] == 0xF7) {
			dev_info(&dd->pdev->dev,
				"Exceeded Tmax, drive in thermal shutdown.\n");
			set_bit(MTIP_DDF_OVER_TEMP_BIT, &dd->dd_flag);
			fail_all_ncq_cmds = 1;
			fail_reason = "thermal shutdown";
		}
		if (buf[288] == 0xBF) {
			set_bit(MTIP_DDF_REBUILD_FAILED_BIT, &dd->dd_flag);
			dev_info(&dd->pdev->dev,
				"Drive indicates rebuild has failed. Secure erase required.\n");
			fail_all_ncq_cmds = 1;
			fail_reason = "rebuild failed";
		}
	}

	/* clear the tag accumulator */
	memset(tagaccum, 0, SLOTBITS_IN_LONGS * sizeof(long));

	/* Loop through all the groups */
	for (group = 0; group < dd->slot_groups; group++) {
		for (bit = 0; bit < 32; bit++) {
			reissue = 1;
			tag = (group << 5) + bit;
			cmd = mtip_cmd_from_tag(dd, tag);

			fis = (struct host_to_dev_fis *)cmd->command;

			/* Should re-issue? */
			if (tag == MTIP_TAG_INTERNAL ||
			    fis->command == ATA_CMD_SET_FEATURES)
				reissue = 0;
			else {
				if (fail_all_ncq_cmds ||
					(fail_all_ncq_write &&
					fis->command == ATA_CMD_FPDMA_WRITE)) {
					dev_warn(&dd->pdev->dev,
					"  Fail: %s w/tag %d [%s].\n",
					fis->command == ATA_CMD_FPDMA_WRITE ?
						"write" : "read",
					tag,
					fail_reason != NULL ?
						fail_reason : "unknown");
					mtip_complete_command(cmd, BLK_STS_MEDIUM);
					continue;
				}
			}

			/*
			 * First check if this command has
			 *  exceeded its retries.
			 */
			if (reissue && (cmd->retries-- > 0)) {

				set_bit(tag, tagaccum);

				/* Re-issue the command. */
				mtip_issue_ncq_command(port, tag);

				continue;
			}

			/* Retire a command that will not be reissued */
			dev_warn(&port->dd->pdev->dev,
				"retiring tag %d\n", tag);

			mtip_complete_command(cmd, BLK_STS_IOERR);
		}
	}
	print_tags(dd, "reissued (TFE)", tagaccum, cmd_cnt);
}

/*
 * Handle a set device bits interrupt
 */
static inline void mtip_workq_sdbfx(struct mtip_port *port, int group,
							u32 completed)
{
	struct driver_data *dd = port->dd;
	int tag, bit;
	struct mtip_cmd *command;

	if (!completed) {
		WARN_ON_ONCE(!completed);
		return;
	}
	/* clear completed status register in the hardware.*/
	writel(completed, port->completed[group]);

	/* Process completed commands. */
	for (bit = 0; (bit < 32) && completed; bit++) {
		if (completed & 0x01) {
			tag = (group << 5) | bit;

			/* skip internal command slot. */
			if (unlikely(tag == MTIP_TAG_INTERNAL))
				continue;

			command = mtip_cmd_from_tag(dd, tag);
			mtip_complete_command(command, 0);
		}
		completed >>= 1;
	}

	/* If last, re-enable interrupts */
	if (atomic_dec_return(&dd->irq_workers_active) == 0)
		writel(0xffffffff, dd->mmio + HOST_IRQ_STAT);
}

/*
 * Process legacy pio and d2h interrupts
 */
static inline void mtip_process_legacy(struct driver_data *dd, u32 port_stat)
{
	struct mtip_port *port = dd->port;
	struct mtip_cmd *cmd = mtip_cmd_from_tag(dd, MTIP_TAG_INTERNAL);

	if (test_bit(MTIP_PF_IC_ACTIVE_BIT, &port->flags) && cmd) {
		int group = MTIP_TAG_INDEX(MTIP_TAG_INTERNAL);
		int status = readl(port->cmd_issue[group]);

		if (!(status & (1 << MTIP_TAG_BIT(MTIP_TAG_INTERNAL))))
			mtip_complete_command(cmd, 0);
	}
}

/*
 * Demux and handle errors
 */
static inline void mtip_process_errors(struct driver_data *dd, u32 port_stat)
{
	if (unlikely(port_stat & PORT_IRQ_CONNECT)) {
		dev_warn(&dd->pdev->dev,
			"Clearing PxSERR.DIAG.x\n");
		writel((1 << 26), dd->port->mmio + PORT_SCR_ERR);
	}

	if (unlikely(port_stat & PORT_IRQ_PHYRDY)) {
		dev_warn(&dd->pdev->dev,
			"Clearing PxSERR.DIAG.n\n");
		writel((1 << 16), dd->port->mmio + PORT_SCR_ERR);
	}

	if (unlikely(port_stat & ~PORT_IRQ_HANDLED)) {
		dev_warn(&dd->pdev->dev,
			"Port stat errors %x unhandled\n",
			(port_stat & ~PORT_IRQ_HANDLED));
		if (mtip_check_surprise_removal(dd))
			return;
	}
	if (likely(port_stat & (PORT_IRQ_TF_ERR | PORT_IRQ_IF_ERR))) {
		set_bit(MTIP_PF_EH_ACTIVE_BIT, &dd->port->flags);
		wake_up_interruptible(&dd->port->svc_wait);
	}
}

static inline irqreturn_t mtip_handle_irq(struct driver_data *data)
{
	struct driver_data *dd = (struct driver_data *) data;
	struct mtip_port *port = dd->port;
	u32 hba_stat, port_stat;
	int rv = IRQ_NONE;
	int do_irq_enable = 1, i, workers;
	struct mtip_work *twork;

	hba_stat = readl(dd->mmio + HOST_IRQ_STAT);
	if (hba_stat) {
		rv = IRQ_HANDLED;

		/* Acknowledge the interrupt status on the port.*/
		port_stat = readl(port->mmio + PORT_IRQ_STAT);
		if (unlikely(port_stat == 0xFFFFFFFF)) {
			mtip_check_surprise_removal(dd);
			return IRQ_HANDLED;
		}
		writel(port_stat, port->mmio + PORT_IRQ_STAT);

		/* Demux port status */
		if (likely(port_stat & PORT_IRQ_SDB_FIS)) {
			do_irq_enable = 0;
			WARN_ON_ONCE(atomic_read(&dd->irq_workers_active) != 0);

			/* Start at 1: group zero is always local? */
			for (i = 0, workers = 0; i < MTIP_MAX_SLOT_GROUPS;
									i++) {
				twork = &dd->work[i];
				twork->completed = readl(port->completed[i]);
				if (twork->completed)
					workers++;
			}

			atomic_set(&dd->irq_workers_active, workers);
			if (workers) {
				for (i = 1; i < MTIP_MAX_SLOT_GROUPS; i++) {
					twork = &dd->work[i];
					if (twork->completed)
						queue_work_on(
							twork->cpu_binding,
							dd->isr_workq,
							&twork->work);
				}

				if (likely(dd->work[0].completed))
					mtip_workq_sdbfx(port, 0,
							dd->work[0].completed);

			} else {
				/*
				 * Chip quirk: SDB interrupt but nothing
				 * to complete
				 */
				do_irq_enable = 1;
			}
		}

		if (unlikely(port_stat & PORT_IRQ_ERR)) {
			if (unlikely(mtip_check_surprise_removal(dd))) {
				/* don't proceed further */
				return IRQ_HANDLED;
			}
			if (test_bit(MTIP_DDF_REMOVE_PENDING_BIT,
							&dd->dd_flag))
				return rv;

			mtip_process_errors(dd, port_stat & PORT_IRQ_ERR);
		}

		if (unlikely(port_stat & PORT_IRQ_LEGACY))
			mtip_process_legacy(dd, port_stat & PORT_IRQ_LEGACY);
	}

	/* acknowledge interrupt */
	if (unlikely(do_irq_enable))
		writel(hba_stat, dd->mmio + HOST_IRQ_STAT);

	return rv;
}

/*
 * HBA interrupt subroutine.
 *
 * @irq		IRQ number.
 * @instance	Pointer to the driver data structure.
 *
 * return value
 *	IRQ_HANDLED	A HBA interrupt was pending and handled.
 *	IRQ_NONE	This interrupt was not for the HBA.
 */
static irqreturn_t mtip_irq_handler(int irq, void *instance)
{
	struct driver_data *dd = instance;

	return mtip_handle_irq(dd);
}

static void mtip_issue_non_ncq_command(struct mtip_port *port, int tag)
{
	writel(1 << MTIP_TAG_BIT(tag), port->cmd_issue[MTIP_TAG_INDEX(tag)]);
}

static bool mtip_pause_ncq(struct mtip_port *port,
				struct host_to_dev_fis *fis)
{
	unsigned long task_file_data;

	task_file_data = readl(port->mmio+PORT_TFDATA);
	if ((task_file_data & 1))
		return false;

	if (fis->command == ATA_CMD_SEC_ERASE_PREP) {
		port->ic_pause_timer = jiffies;
		return true;
	} else if ((fis->command == ATA_CMD_DOWNLOAD_MICRO) &&
					(fis->features == 0x03)) {
		set_bit(MTIP_PF_DM_ACTIVE_BIT, &port->flags);
		port->ic_pause_timer = jiffies;
		return true;
	} else if ((fis->command == ATA_CMD_SEC_ERASE_UNIT) ||
		((fis->command == 0xFC) &&
			(fis->features == 0x27 || fis->features == 0x72 ||
			 fis->features == 0x62 || fis->features == 0x26))) {
		clear_bit(MTIP_DDF_SEC_LOCK_BIT, &port->dd->dd_flag);
		clear_bit(MTIP_DDF_REBUILD_FAILED_BIT, &port->dd->dd_flag);
		/* Com reset after secure erase or lowlevel format */
		mtip_restart_port(port);
		clear_bit(MTIP_PF_SE_ACTIVE_BIT, &port->flags);
		return false;
	}

	return false;
}

static bool mtip_commands_active(struct mtip_port *port)
{
	unsigned int active;
	unsigned int n;

	/*
	 * Ignore s_active bit 0 of array element 0.
	 * This bit will always be set
	 */
	active = readl(port->s_active[0]) & 0xFFFFFFFE;
	for (n = 1; n < port->dd->slot_groups; n++)
		active |= readl(port->s_active[n]);

	return active != 0;
}

/*
 * Wait for port to quiesce
 *
 * @port    Pointer to port data structure
 * @timeout Max duration to wait (ms)
 *
 * return value
 *	0	Success
 *	-EBUSY  Commands still active
 */
static int mtip_quiesce_io(struct mtip_port *port, unsigned long timeout)
{
	unsigned long to;
	bool active = true;

	blk_mq_quiesce_queue(port->dd->queue);

	to = jiffies + msecs_to_jiffies(timeout);
	do {
		if (test_bit(MTIP_PF_SVC_THD_ACTIVE_BIT, &port->flags) &&
			test_bit(MTIP_PF_ISSUE_CMDS_BIT, &port->flags)) {
			msleep(20);
			continue; /* svc thd is actively issuing commands */
		}

		msleep(100);

		if (mtip_check_surprise_removal(port->dd))
			goto err_fault;

		active = mtip_commands_active(port);
		if (!active)
			break;
	} while (time_before(jiffies, to));

	blk_mq_unquiesce_queue(port->dd->queue);
	return active ? -EBUSY : 0;
err_fault:
	blk_mq_unquiesce_queue(port->dd->queue);
	return -EFAULT;
}

struct mtip_int_cmd {
	int fis_len;
	dma_addr_t buffer;
	int buf_len;
	u32 opts;
};

/*
 * Execute an internal command and wait for the completion.
 *
 * @port    Pointer to the port data structure.
 * @fis     Pointer to the FIS that describes the command.
 * @fis_len  Length in WORDS of the FIS.
 * @buffer  DMA accessible for command data.
 * @buf_len  Length, in bytes, of the data buffer.
 * @opts    Command header options, excluding the FIS length
 *             and the number of PRD entries.
 * @timeout Time in ms to wait for the command to complete.
 *
 * return value
 *	0	 Command completed successfully.
 *	-EFAULT  The buffer address is not correctly aligned.
 *	-EBUSY   Internal command or other IO in progress.
 *	-EAGAIN  Time out waiting for command to complete.
 */
static int mtip_exec_internal_command(struct mtip_port *port,
					struct host_to_dev_fis *fis,
					int fis_len,
					dma_addr_t buffer,
					int buf_len,
					u32 opts,
					unsigned long timeout)
{
	struct mtip_cmd *int_cmd;
	struct driver_data *dd = port->dd;
	struct request *rq;
	struct mtip_int_cmd icmd = {
		.fis_len = fis_len,
		.buffer = buffer,
		.buf_len = buf_len,
		.opts = opts
	};
	int rv = 0;

	/* Make sure the buffer is 8 byte aligned. This is asic specific. */
	if (buffer & 0x00000007) {
		dev_err(&dd->pdev->dev, "SG buffer is not 8 byte aligned\n");
		return -EFAULT;
	}

	if (mtip_check_surprise_removal(dd))
		return -EFAULT;

	rq = blk_mq_alloc_request(dd->queue, REQ_OP_DRV_IN, BLK_MQ_REQ_RESERVED);
	if (IS_ERR(rq)) {
		dbg_printk(MTIP_DRV_NAME "Unable to allocate tag for PIO cmd\n");
		return -EFAULT;
	}

	set_bit(MTIP_PF_IC_ACTIVE_BIT, &port->flags);

	if (fis->command == ATA_CMD_SEC_ERASE_PREP)
		set_bit(MTIP_PF_SE_ACTIVE_BIT, &port->flags);

	clear_bit(MTIP_PF_DM_ACTIVE_BIT, &port->flags);

	if (fis->command != ATA_CMD_STANDBYNOW1) {
		/* wait for io to complete if non atomic */
		if (mtip_quiesce_io(port, MTIP_QUIESCE_IO_TIMEOUT_MS) < 0) {
			dev_warn(&dd->pdev->dev, "Failed to quiesce IO\n");
			blk_mq_free_request(rq);
			clear_bit(MTIP_PF_IC_ACTIVE_BIT, &port->flags);
			wake_up_interruptible(&port->svc_wait);
			return -EBUSY;
		}
	}

	/* Copy the command to the command table */
	int_cmd = blk_mq_rq_to_pdu(rq);
	int_cmd->icmd = &icmd;
	memcpy(int_cmd->command, fis, fis_len*4);

	rq->timeout = timeout;

	/* insert request and run queue */
	blk_execute_rq(rq, true);

	if (int_cmd->status) {
		dev_err(&dd->pdev->dev, "Internal command [%02X] failed %d\n",
				fis->command, int_cmd->status);
		rv = -EIO;

		if (mtip_check_surprise_removal(dd) ||
			test_bit(MTIP_DDF_REMOVE_PENDING_BIT,
					&dd->dd_flag)) {
			dev_err(&dd->pdev->dev,
				"Internal command [%02X] wait returned due to SR\n",
				fis->command);
			rv = -ENXIO;
			goto exec_ic_exit;
		}
		mtip_device_reset(dd); /* recover from timeout issue */
		rv = -EAGAIN;
		goto exec_ic_exit;
	}

	if (readl(port->cmd_issue[MTIP_TAG_INDEX(MTIP_TAG_INTERNAL)])
			& (1 << MTIP_TAG_BIT(MTIP_TAG_INTERNAL))) {
		rv = -ENXIO;
		if (!test_bit(MTIP_DDF_REMOVE_PENDING_BIT, &dd->dd_flag)) {
			mtip_device_reset(dd);
			rv = -EAGAIN;
		}
	}
exec_ic_exit:
	/* Clear the allocated and active bits for the internal command. */
	blk_mq_free_request(rq);
	clear_bit(MTIP_PF_IC_ACTIVE_BIT, &port->flags);
	if (rv >= 0 && mtip_pause_ncq(port, fis)) {
		/* NCQ paused */
		return rv;
	}
	wake_up_interruptible(&port->svc_wait);

	return rv;
}

/*
 * Byte-swap ATA ID strings.
 *
 * ATA identify data contains strings in byte-swapped 16-bit words.
 * They must be swapped (on all architectures) to be usable as C strings.
 * This function swaps bytes in-place.
 *
 * @buf The buffer location of the string
 * @len The number of bytes to swap
 *
 * return value
 *	None
 */
static inline void ata_swap_string(u16 *buf, unsigned int len)
{
	int i;
	for (i = 0; i < (len/2); i++)
		be16_to_cpus(&buf[i]);
}

static void mtip_set_timeout(struct driver_data *dd,
					struct host_to_dev_fis *fis,
					unsigned int *timeout, u8 erasemode)
{
	switch (fis->command) {
	case ATA_CMD_DOWNLOAD_MICRO:
		*timeout = 120000; /* 2 minutes */
		break;
	case ATA_CMD_SEC_ERASE_UNIT:
	case 0xFC:
		if (erasemode)
			*timeout = ((*(dd->port->identify + 90) * 2) * 60000);
		else
			*timeout = ((*(dd->port->identify + 89) * 2) * 60000);
		break;
	case ATA_CMD_STANDBYNOW1:
		*timeout = 120000;  /* 2 minutes */
		break;
	case 0xF7:
	case 0xFA:
		*timeout = 60000;  /* 60 seconds */
		break;
	case ATA_CMD_SMART:
		*timeout = 15000;  /* 15 seconds */
		break;
	default:
		*timeout = MTIP_IOCTL_CMD_TIMEOUT_MS;
		break;
	}
}

/*
 * Request the device identity information.
 *
 * If a user space buffer is not specified, i.e. is NULL, the
 * identify information is still read from the drive and placed
 * into the identify data buffer (@e port->identify) in the
 * port data structure.
 * When the identify buffer contains valid identify information @e
 * port->identify_valid is non-zero.
 *
 * @port	 Pointer to the port structure.
 * @user_buffer  A user space buffer where the identify data should be
 *                    copied.
 *
 * return value
 *	0	Command completed successfully.
 *	-EFAULT An error occurred while coping data to the user buffer.
 *	-1	Command failed.
 */
static int mtip_get_identify(struct mtip_port *port, void __user *user_buffer)
{
	int rv = 0;
	struct host_to_dev_fis fis;

	if (test_bit(MTIP_DDF_REMOVE_PENDING_BIT, &port->dd->dd_flag))
		return -EFAULT;

	/* Build the FIS. */
	memset(&fis, 0, sizeof(struct host_to_dev_fis));
	fis.type	= 0x27;
	fis.opts	= 1 << 7;
	fis.command	= ATA_CMD_ID_ATA;

	/* Set the identify information as invalid. */
	port->identify_valid = 0;

	/* Clear the identify information. */
	memset(port->identify, 0, sizeof(u16) * ATA_ID_WORDS);

	/* Execute the command. */
	if (mtip_exec_internal_command(port,
				&fis,
				5,
				port->identify_dma,
				sizeof(u16) * ATA_ID_WORDS,
				0,
				MTIP_INT_CMD_TIMEOUT_MS)
				< 0) {
		rv = -1;
		goto out;
	}

	/*
	 * Perform any necessary byte-swapping.  Yes, the kernel does in fact
	 * perform field-sensitive swapping on the string fields.
	 * See the kernel use of ata_id_string() for proof of this.
	 */
#ifdef __LITTLE_ENDIAN
	ata_swap_string(port->identify + 27, 40);  /* model string*/
	ata_swap_string(port->identify + 23, 8);   /* firmware string*/
	ata_swap_string(port->identify + 10, 20);  /* serial# string*/
#else
	{
		int i;
		for (i = 0; i < ATA_ID_WORDS; i++)
			port->identify[i] = le16_to_cpu(port->identify[i]);
	}
#endif

	/* Check security locked state */
	if (port->identify[128] & 0x4)
		set_bit(MTIP_DDF_SEC_LOCK_BIT, &port->dd->dd_flag);
	else
		clear_bit(MTIP_DDF_SEC_LOCK_BIT, &port->dd->dd_flag);

	/* Set the identify buffer as valid. */
	port->identify_valid = 1;

	if (user_buffer) {
		if (copy_to_user(
			user_buffer,
			port->identify,
			ATA_ID_WORDS * sizeof(u16))) {
			rv = -EFAULT;
			goto out;
		}
	}

out:
	return rv;
}

/*
 * Issue a standby immediate command to the device.
 *
 * @port Pointer to the port structure.
 *
 * return value
 *	0	Command was executed successfully.
 *	-1	An error occurred while executing the command.
 */
static int mtip_standby_immediate(struct mtip_port *port)
{
	int rv;
	struct host_to_dev_fis	fis;
	unsigned long __maybe_unused start;
	unsigned int timeout;

	/* Build the FIS. */
	memset(&fis, 0, sizeof(struct host_to_dev_fis));
	fis.type	= 0x27;
	fis.opts	= 1 << 7;
	fis.command	= ATA_CMD_STANDBYNOW1;

	mtip_set_timeout(port->dd, &fis, &timeout, 0);

	start = jiffies;
	rv = mtip_exec_internal_command(port,
					&fis,
					5,
					0,
					0,
					0,
					timeout);
	dbg_printk(MTIP_DRV_NAME "Time taken to complete standby cmd: %d ms\n",
			jiffies_to_msecs(jiffies - start));
	if (rv)
		dev_warn(&port->dd->pdev->dev,
			"STANDBY IMMEDIATE command failed.\n");

	return rv;
}

/*
 * Issue a READ LOG EXT command to the device.
 *
 * @port	pointer to the port structure.
 * @page	page number to fetch
 * @buffer	pointer to buffer
 * @buffer_dma	dma address corresponding to @buffer
 * @sectors	page length to fetch, in sectors
 *
 * return value
 *	@rv	return value from mtip_exec_internal_command()
 */
static int mtip_read_log_page(struct mtip_port *port, u8 page, u16 *buffer,
				dma_addr_t buffer_dma, unsigned int sectors)
{
	struct host_to_dev_fis fis;

	memset(&fis, 0, sizeof(struct host_to_dev_fis));
	fis.type	= 0x27;
	fis.opts	= 1 << 7;
	fis.command	= ATA_CMD_READ_LOG_EXT;
	fis.sect_count	= sectors & 0xFF;
	fis.sect_cnt_ex	= (sectors >> 8) & 0xFF;
	fis.lba_low	= page;
	fis.lba_mid	= 0;
	fis.device	= ATA_DEVICE_OBS;

	memset(buffer, 0, sectors * ATA_SECT_SIZE);

	return mtip_exec_internal_command(port,
					&fis,
					5,
					buffer_dma,
					sectors * ATA_SECT_SIZE,
					0,
					MTIP_INT_CMD_TIMEOUT_MS);
}

/*
 * Issue a SMART READ DATA command to the device.
 *
 * @port	pointer to the port structure.
 * @buffer	pointer to buffer
 * @buffer_dma	dma address corresponding to @buffer
 *
 * return value
 *	@rv	return value from mtip_exec_internal_command()
 */
static int mtip_get_smart_data(struct mtip_port *port, u8 *buffer,
					dma_addr_t buffer_dma)
{
	struct host_to_dev_fis fis;

	memset(&fis, 0, sizeof(struct host_to_dev_fis));
	fis.type	= 0x27;
	fis.opts	= 1 << 7;
	fis.command	= ATA_CMD_SMART;
	fis.features	= 0xD0;
	fis.sect_count	= 1;
	fis.lba_mid	= 0x4F;
	fis.lba_hi	= 0xC2;
	fis.device	= ATA_DEVICE_OBS;

	return mtip_exec_internal_command(port,
					&fis,
					5,
					buffer_dma,
					ATA_SECT_SIZE,
					0,
					15000);
}

/*
 * Get the value of a smart attribute
 *
 * @port	pointer to the port structure
 * @id		attribute number
 * @attrib	pointer to return attrib information corresponding to @id
 *
 * return value
 *	-EINVAL	NULL buffer passed or unsupported attribute @id.
 *	-EPERM	Identify data not valid, SMART not supported or not enabled
 */
static int mtip_get_smart_attr(struct mtip_port *port, unsigned int id,
						struct smart_attr *attrib)
{
	int rv, i;
	struct smart_attr *pattr;

	if (!attrib)
		return -EINVAL;

	if (!port->identify_valid) {
		dev_warn(&port->dd->pdev->dev, "IDENTIFY DATA not valid\n");
		return -EPERM;
	}
	if (!(port->identify[82] & 0x1)) {
		dev_warn(&port->dd->pdev->dev, "SMART not supported\n");
		return -EPERM;
	}
	if (!(port->identify[85] & 0x1)) {
		dev_warn(&port->dd->pdev->dev, "SMART not enabled\n");
		return -EPERM;
	}

	memset(port->smart_buf, 0, ATA_SECT_SIZE);
	rv = mtip_get_smart_data(port, port->smart_buf, port->smart_buf_dma);
	if (rv) {
		dev_warn(&port->dd->pdev->dev, "Failed to ge SMART data\n");
		return rv;
	}

	pattr = (struct smart_attr *)(port->smart_buf + 2);
	for (i = 0; i < 29; i++, pattr++)
		if (pattr->attr_id == id) {
			memcpy(attrib, pattr, sizeof(struct smart_attr));
			break;
		}

	if (i == 29) {
		dev_warn(&port->dd->pdev->dev,
			"Query for invalid SMART attribute ID\n");
		rv = -EINVAL;
	}

	return rv;
}

/*
 * Get the drive capacity.
 *
 * @dd      Pointer to the device data structure.
 * @sectors Pointer to the variable that will receive the sector count.
 *
 * return value
 *	1 Capacity was returned successfully.
 *	0 The identify information is invalid.
 */
static bool mtip_hw_get_capacity(struct driver_data *dd, sector_t *sectors)
{
	struct mtip_port *port = dd->port;
	u64 total, raw0, raw1, raw2, raw3;
	raw0 = port->identify[100];
	raw1 = port->identify[101];
	raw2 = port->identify[102];
	raw3 = port->identify[103];
	total = raw0 | raw1<<16 | raw2<<32 | raw3<<48;
	*sectors = total;
	return (bool) !!port->identify_valid;
}

/*
 * Display the identify command data.
 *
 * @port Pointer to the port data structure.
 *
 * return value
 *	None
 */
static void mtip_dump_identify(struct mtip_port *port)
{
	sector_t sectors;
	unsigned short revid;
	char cbuf[42];

	if (!port->identify_valid)
		return;

	strscpy(cbuf, (char *)(port->identify + 10), 21);
	dev_info(&port->dd->pdev->dev,
		"Serial No.: %s\n", cbuf);

	strscpy(cbuf, (char *)(port->identify + 23), 9);
	dev_info(&port->dd->pdev->dev,
		"Firmware Ver.: %s\n", cbuf);

	strscpy(cbuf, (char *)(port->identify + 27), 41);
	dev_info(&port->dd->pdev->dev, "Model: %s\n", cbuf);

	dev_info(&port->dd->pdev->dev, "Security: %04x %s\n",
		port->identify[128],
		port->identify[128] & 0x4 ? "(LOCKED)" : "");

	if (mtip_hw_get_capacity(port->dd, &sectors))
		dev_info(&port->dd->pdev->dev,
			"Capacity: %llu sectors (%llu MB)\n",
			 (u64)sectors,
			 ((u64)sectors) * ATA_SECT_SIZE >> 20);

	pci_read_config_word(port->dd->pdev, PCI_REVISION_ID, &revid);
	switch (revid & 0xFF) {
	case 0x1:
		strscpy(cbuf, "A0", 3);
		break;
	case 0x3:
		strscpy(cbuf, "A2", 3);
		break;
	default:
		strscpy(cbuf, "?", 2);
		break;
	}
	dev_info(&port->dd->pdev->dev,
		"Card Type: %s\n", cbuf);
}

/*
 * Map the commands scatter list into the command table.
 *
 * @command Pointer to the command.
 * @nents Number of scatter list entries.
 *
 * return value
 *	None
 */
static inline void fill_command_sg(struct driver_data *dd,
				struct mtip_cmd *command,
				int nents)
{
	int n;
	unsigned int dma_len;
	struct mtip_cmd_sg *command_sg;
	struct scatterlist *sg;

	command_sg = command->command + AHCI_CMD_TBL_HDR_SZ;

	for_each_sg(command->sg, sg, nents, n) {
		dma_len = sg_dma_len(sg);
		if (dma_len > 0x400000)
			dev_err(&dd->pdev->dev,
				"DMA segment length truncated\n");
		command_sg->info = cpu_to_le32((dma_len-1) & 0x3FFFFF);
		command_sg->dba	=  cpu_to_le32(sg_dma_address(sg));
		command_sg->dba_upper =
			cpu_to_le32((sg_dma_address(sg) >> 16) >> 16);
		command_sg++;
	}
}

/*
 * @brief Execute a drive command.
 *
 * return value 0 The command completed successfully.
 * return value -1 An error occurred while executing the command.
 */
static int exec_drive_task(struct mtip_port *port, u8 *command)
{
	struct host_to_dev_fis	fis;
	struct host_to_dev_fis *reply = (port->rxfis + RX_FIS_D2H_REG);
	unsigned int to;

	/* Build the FIS. */
	memset(&fis, 0, sizeof(struct host_to_dev_fis));
	fis.type	= 0x27;
	fis.opts	= 1 << 7;
	fis.command	= command[0];
	fis.features	= command[1];
	fis.sect_count	= command[2];
	fis.sector	= command[3];
	fis.cyl_low	= command[4];
	fis.cyl_hi	= command[5];
	fis.device	= command[6] & ~0x10; /* Clear the dev bit*/

	mtip_set_timeout(port->dd, &fis, &to, 0);

	dbg_printk(MTIP_DRV_NAME " %s: User Command: cmd %x, feat %x, nsect %x, sect %x, lcyl %x, hcyl %x, sel %x\n",
		__func__,
		command[0],
		command[1],
		command[2],
		command[3],
		command[4],
		command[5],
		command[6]);

	/* Execute the command. */
	if (mtip_exec_internal_command(port,
				 &fis,
				 5,
				 0,
				 0,
				 0,
				 to) < 0) {
		return -1;
	}

	command[0] = reply->command; /* Status*/
	command[1] = reply->features; /* Error*/
	command[4] = reply->cyl_low;
	command[5] = reply->cyl_hi;

	dbg_printk(MTIP_DRV_NAME " %s: Completion Status: stat %x, err %x , cyl_lo %x cyl_hi %x\n",
		__func__,
		command[0],
		command[1],
		command[4],
		command[5]);

	return 0;
}

/*
 * @brief Execute a drive command.
 *
 * @param port Pointer to the port data structure.
 * @param command Pointer to the user specified command parameters.
 * @param user_buffer Pointer to the user space buffer where read sector
 *                   data should be copied.
 *
 * return value 0 The command completed successfully.
 * return value -EFAULT An error occurred while copying the completion
 *                 data to the user space buffer.
 * return value -1 An error occurred while executing the command.
 */
static int exec_drive_command(struct mtip_port *port, u8 *command,
				void __user *user_buffer)
{
	struct host_to_dev_fis	fis;
	struct host_to_dev_fis *reply;
	u8 *buf = NULL;
	dma_addr_t dma_addr = 0;
	int rv = 0, xfer_sz = command[3];
	unsigned int to;

	if (xfer_sz) {
		if (!user_buffer)
			return -EFAULT;

		buf = dma_alloc_coherent(&port->dd->pdev->dev,
				ATA_SECT_SIZE * xfer_sz,
				&dma_addr,
				GFP_KERNEL);
		if (!buf) {
			dev_err(&port->dd->pdev->dev,
				"Memory allocation failed (%d bytes)\n",
				ATA_SECT_SIZE * xfer_sz);
			return -ENOMEM;
		}
	}

	/* Build the FIS. */
	memset(&fis, 0, sizeof(struct host_to_dev_fis));
	fis.type	= 0x27;
	fis.opts	= 1 << 7;
	fis.command	= command[0];
	fis.features	= command[2];
	fis.sect_count	= command[3];
	if (fis.command == ATA_CMD_SMART) {
		fis.sector	= command[1];
		fis.cyl_low	= 0x4F;
		fis.cyl_hi	= 0xC2;
	}

	mtip_set_timeout(port->dd, &fis, &to, 0);

	if (xfer_sz)
		reply = (port->rxfis + RX_FIS_PIO_SETUP);
	else
		reply = (port->rxfis + RX_FIS_D2H_REG);

	dbg_printk(MTIP_DRV_NAME
		" %s: User Command: cmd %x, sect %x, "
		"feat %x, sectcnt %x\n",
		__func__,
		command[0],
		command[1],
		command[2],
		command[3]);

	/* Execute the command. */
	if (mtip_exec_internal_command(port,
				&fis,
				 5,
				 (xfer_sz ? dma_addr : 0),
				 (xfer_sz ? ATA_SECT_SIZE * xfer_sz : 0),
				 0,
				 to)
				 < 0) {
		rv = -EFAULT;
		goto exit_drive_command;
	}

	/* Collect the completion status. */
	command[0] = reply->command; /* Status*/
	command[1] = reply->features; /* Error*/
	command[2] = reply->sect_count;

	dbg_printk(MTIP_DRV_NAME
		" %s: Completion Status: stat %x, "
		"err %x, nsect %x\n",
		__func__,
		command[0],
		command[1],
		command[2]);

	if (xfer_sz) {
		if (copy_to_user(user_buffer,
				 buf,
				 ATA_SECT_SIZE * command[3])) {
			rv = -EFAULT;
			goto exit_drive_command;
		}
	}
exit_drive_command:
	if (buf)
		dma_free_coherent(&port->dd->pdev->dev,
				ATA_SECT_SIZE * xfer_sz, buf, dma_addr);
	return rv;
}

/*
 *  Indicates whether a command has a single sector payload.
 *
 *  @command passed to the device to perform the certain event.
 *  @features passed to the device to perform the certain event.
 *
 *  return value
 *	1	command is one that always has a single sector payload,
 *		regardless of the value in the Sector Count field.
 *      0       otherwise
 *
 */
static unsigned int implicit_sector(unsigned char command,
				    unsigned char features)
{
	unsigned int rv = 0;

	/* list of commands that have an implicit sector count of 1 */
	switch (command) {
	case ATA_CMD_SEC_SET_PASS:
	case ATA_CMD_SEC_UNLOCK:
	case ATA_CMD_SEC_ERASE_PREP:
	case ATA_CMD_SEC_ERASE_UNIT:
	case ATA_CMD_SEC_FREEZE_LOCK:
	case ATA_CMD_SEC_DISABLE_PASS:
	case ATA_CMD_PMP_READ:
	case ATA_CMD_PMP_WRITE:
		rv = 1;
		break;
	case ATA_CMD_SET_MAX:
		if (features == ATA_SET_MAX_UNLOCK)
			rv = 1;
		break;
	case ATA_CMD_SMART:
		if ((features == ATA_SMART_READ_VALUES) ||
				(features == ATA_SMART_READ_THRESHOLDS))
			rv = 1;
		break;
	case ATA_CMD_CONF_OVERLAY:
		if ((features == ATA_DCO_IDENTIFY) ||
				(features == ATA_DCO_SET))
			rv = 1;
		break;
	}
	return rv;
}

/*
 * Executes a taskfile
 * See ide_taskfile_ioctl() for derivation
 */
static int exec_drive_taskfile(struct driver_data *dd,
			       void __user *buf,
			       ide_task_request_t *req_task,
			       int outtotal)
{
	struct host_to_dev_fis	fis;
	struct host_to_dev_fis *reply;
	u8 *outbuf = NULL;
	u8 *inbuf = NULL;
	dma_addr_t outbuf_dma = 0;
	dma_addr_t inbuf_dma = 0;
	dma_addr_t dma_buffer = 0;
	int err = 0;
	unsigned int taskin = 0;
	unsigned int taskout = 0;
	u8 nsect = 0;
	unsigned int timeout;
	unsigned int force_single_sector;
	unsigned int transfer_size;
	unsigned long task_file_data;
	int intotal = outtotal + req_task->out_size;
	int erasemode = 0;

	taskout = req_task->out_size;
	taskin = req_task->in_size;
	/* 130560 = 512 * 0xFF*/
	if (taskin > 130560 || taskout > 130560)
		return -EINVAL;

	if (taskout) {
		outbuf = memdup_user(buf + outtotal, taskout);
		if (IS_ERR(outbuf))
			return PTR_ERR(outbuf);

		outbuf_dma = dma_map_single(&dd->pdev->dev, outbuf,
					    taskout, DMA_TO_DEVICE);
		if (dma_mapping_error(&dd->pdev->dev, outbuf_dma)) {
			err = -ENOMEM;
			goto abort;
		}
		dma_buffer = outbuf_dma;
	}

	if (taskin) {
		inbuf = memdup_user(buf + intotal, taskin);
		if (IS_ERR(inbuf)) {
			err = PTR_ERR(inbuf);
			inbuf = NULL;
			goto abort;
		}
		inbuf_dma = dma_map_single(&dd->pdev->dev, inbuf,
					   taskin, DMA_FROM_DEVICE);
		if (dma_mapping_error(&dd->pdev->dev, inbuf_dma)) {
			err = -ENOMEM;
			goto abort;
		}
		dma_buffer = inbuf_dma;
	}

	/* only supports PIO and non-data commands from this ioctl. */
	switch (req_task->data_phase) {
	case TASKFILE_OUT:
		nsect = taskout / ATA_SECT_SIZE;
		reply = (dd->port->rxfis + RX_FIS_PIO_SETUP);
		break;
	case TASKFILE_IN:
		reply = (dd->port->rxfis + RX_FIS_PIO_SETUP);
		break;
	case TASKFILE_NO_DATA:
		reply = (dd->port->rxfis + RX_FIS_D2H_REG);
		break;
	default:
		err = -EINVAL;
		goto abort;
	}

	/* Build the FIS. */
	memset(&fis, 0, sizeof(struct host_to_dev_fis));

	fis.type	= 0x27;
	fis.opts	= 1 << 7;
	fis.command	= req_task->io_ports[7];
	fis.features	= req_task->io_ports[1];
	fis.sect_count	= req_task->io_ports[2];
	fis.lba_low	= req_task->io_ports[3];
	fis.lba_mid	= req_task->io_ports[4];
	fis.lba_hi	= req_task->io_ports[5];
	 /* Clear the dev bit*/
	fis.device	= req_task->io_ports[6] & ~0x10;

	if ((req_task->in_flags.all == 0) && (req_task->out_flags.all & 1)) {
		req_task->in_flags.all	=
			IDE_TASKFILE_STD_IN_FLAGS |
			(IDE_HOB_STD_IN_FLAGS << 8);
		fis.lba_low_ex		= req_task->hob_ports[3];
		fis.lba_mid_ex		= req_task->hob_ports[4];
		fis.lba_hi_ex		= req_task->hob_ports[5];
		fis.features_ex		= req_task->hob_ports[1];
		fis.sect_cnt_ex		= req_task->hob_ports[2];

	} else {
		req_task->in_flags.all = IDE_TASKFILE_STD_IN_FLAGS;
	}

	force_single_sector = implicit_sector(fis.command, fis.features);

	if ((taskin || taskout) && (!fis.sect_count)) {
		if (nsect)
			fis.sect_count = nsect;
		else {
			if (!force_single_sector) {
				dev_warn(&dd->pdev->dev,
					"data movement but "
					"sect_count is 0\n");
				err = -EINVAL;
				goto abort;
			}
		}
	}

	dbg_printk(MTIP_DRV_NAME
		" %s: cmd %x, feat %x, nsect %x,"
		" sect/lbal %x, lcyl/lbam %x, hcyl/lbah %x,"
		" head/dev %x\n",
		__func__,
		fis.command,
		fis.features,
		fis.sect_count,
		fis.lba_low,
		fis.lba_mid,
		fis.lba_hi,
		fis.device);

	/* check for erase mode support during secure erase.*/
	if ((fis.command == ATA_CMD_SEC_ERASE_UNIT) && outbuf &&
					(outbuf[0] & MTIP_SEC_ERASE_MODE)) {
		erasemode = 1;
	}

	mtip_set_timeout(dd, &fis, &timeout, erasemode);

	/* Determine the correct transfer size.*/
	if (force_single_sector)
		transfer_size = ATA_SECT_SIZE;
	else
		transfer_size = ATA_SECT_SIZE * fis.sect_count;

	/* Execute the command.*/
	if (mtip_exec_internal_command(dd->port,
				 &fis,
				 5,
				 dma_buffer,
				 transfer_size,
				 0,
				 timeout) < 0) {
		err = -EIO;
		goto abort;
	}

	task_file_data = readl(dd->port->mmio+PORT_TFDATA);

	if ((req_task->data_phase == TASKFILE_IN) && !(task_file_data & 1)) {
		reply = dd->port->rxfis + RX_FIS_PIO_SETUP;
		req_task->io_ports[7] = reply->control;
	} else {
		reply = dd->port->rxfis + RX_FIS_D2H_REG;
		req_task->io_ports[7] = reply->command;
	}

	/* reclaim the DMA buffers.*/
	if (inbuf_dma)
		dma_unmap_single(&dd->pdev->dev, inbuf_dma, taskin,
				 DMA_FROM_DEVICE);
	if (outbuf_dma)
		dma_unmap_single(&dd->pdev->dev, outbuf_dma, taskout,
				 DMA_TO_DEVICE);
	inbuf_dma  = 0;
	outbuf_dma = 0;

	/* return the ATA registers to the caller.*/
	req_task->io_ports[1] = reply->features;
	req_task->io_ports[2] = reply->sect_count;
	req_task->io_ports[3] = reply->lba_low;
	req_task->io_ports[4] = reply->lba_mid;
	req_task->io_ports[5] = reply->lba_hi;
	req_task->io_ports[6] = reply->device;

	if (req_task->out_flags.all & 1)  {

		req_task->hob_ports[3] = reply->lba_low_ex;
		req_task->hob_ports[4] = reply->lba_mid_ex;
		req_task->hob_ports[5] = reply->lba_hi_ex;
		req_task->hob_ports[1] = reply->features_ex;
		req_task->hob_ports[2] = reply->sect_cnt_ex;
	}
	dbg_printk(MTIP_DRV_NAME
		" %s: Completion: stat %x,"
		"err %x, sect_cnt %x, lbalo %x,"
		"lbamid %x, lbahi %x, dev %x\n",
		__func__,
		req_task->io_ports[7],
		req_task->io_ports[1],
		req_task->io_ports[2],
		req_task->io_ports[3],
		req_task->io_ports[4],
		req_task->io_ports[5],
		req_task->io_ports[6]);

	if (taskout) {
		if (copy_to_user(buf + outtotal, outbuf, taskout)) {
			err = -EFAULT;
			goto abort;
		}
	}
	if (taskin) {
		if (copy_to_user(buf + intotal, inbuf, taskin)) {
			err = -EFAULT;
			goto abort;
		}
	}
abort:
	if (inbuf_dma)
		dma_unmap_single(&dd->pdev->dev, inbuf_dma, taskin,
				 DMA_FROM_DEVICE);
	if (outbuf_dma)
		dma_unmap_single(&dd->pdev->dev, outbuf_dma, taskout,
				 DMA_TO_DEVICE);
	kfree(outbuf);
	kfree(inbuf);

	return err;
}

/*
 * Handle IOCTL calls from the Block Layer.
 *
 * This function is called by the Block Layer when it receives an IOCTL
 * command that it does not understand. If the IOCTL command is not supported
 * this function returns -ENOTTY.
 *
 * @dd  Pointer to the driver data structure.
 * @cmd IOCTL command passed from the Block Layer.
 * @arg IOCTL argument passed from the Block Layer.
 *
 * return value
 *	0	The IOCTL completed successfully.
 *	-ENOTTY The specified command is not supported.
 *	-EFAULT An error occurred copying data to a user space buffer.
 *	-EIO	An error occurred while executing the command.
 */
static int mtip_hw_ioctl(struct driver_data *dd, unsigned int cmd,
			 unsigned long arg)
{
	switch (cmd) {
	case HDIO_GET_IDENTITY:
	{
		if (copy_to_user((void __user *)arg, dd->port->identify,
						sizeof(u16) * ATA_ID_WORDS))
			return -EFAULT;
		break;
	}
	case HDIO_DRIVE_CMD:
	{
		u8 drive_command[4];

		/* Copy the user command info to our buffer. */
		if (copy_from_user(drive_command,
					 (void __user *) arg,
					 sizeof(drive_command)))
			return -EFAULT;

		/* Execute the drive command. */
		if (exec_drive_command(dd->port,
					 drive_command,
					 (void __user *) (arg+4)))
			return -EIO;

		/* Copy the status back to the users buffer. */
		if (copy_to_user((void __user *) arg,
					 drive_command,
					 sizeof(drive_command)))
			return -EFAULT;

		break;
	}
	case HDIO_DRIVE_TASK:
	{
		u8 drive_command[7];

		/* Copy the user command info to our buffer. */
		if (copy_from_user(drive_command,
					 (void __user *) arg,
					 sizeof(drive_command)))
			return -EFAULT;

		/* Execute the drive command. */
		if (exec_drive_task(dd->port, drive_command))
			return -EIO;

		/* Copy the status back to the users buffer. */
		if (copy_to_user((void __user *) arg,
					 drive_command,
					 sizeof(drive_command)))
			return -EFAULT;

		break;
	}
	case HDIO_DRIVE_TASKFILE: {
		ide_task_request_t req_task;
		int ret, outtotal;

		if (copy_from_user(&req_task, (void __user *) arg,
					sizeof(req_task)))
			return -EFAULT;

		outtotal = sizeof(req_task);

		ret = exec_drive_taskfile(dd, (void __user *) arg,
						&req_task, outtotal);

		if (copy_to_user((void __user *) arg, &req_task,
							sizeof(req_task)))
			return -EFAULT;

		return ret;
	}

	default:
		return -EINVAL;
	}
	return 0;
}

/*
 * Submit an IO to the hw
 *
 * This function is called by the block layer to issue an io
 * to the device. Upon completion, the callback function will
 * be called with the data parameter passed as the callback data.
 *
 * @dd       Pointer to the driver data structure.
 * @start    First sector to read.
 * @nsect    Number of sectors to read.
 * @tag      The tag of this read command.
 * @callback Pointer to the function that should be called
 *	     when the read completes.
 * @data     Callback data passed to the callback function
 *	     when the read completes.
 * @dir      Direction (read or write)
 *
 * return value
 *	None
 */
static void mtip_hw_submit_io(struct driver_data *dd, struct request *rq,
			      struct mtip_cmd *command,
			      struct blk_mq_hw_ctx *hctx)
{
	struct mtip_cmd_hdr *hdr =
		dd->port->command_list + sizeof(struct mtip_cmd_hdr) * rq->tag;
	struct host_to_dev_fis	*fis;
	struct mtip_port *port = dd->port;
	int dma_dir = rq_data_dir(rq) == READ ? DMA_FROM_DEVICE : DMA_TO_DEVICE;
	u64 start = blk_rq_pos(rq);
	unsigned int nsect = blk_rq_sectors(rq);
	unsigned int nents;

	/* Map the scatter list for DMA access */
	nents = blk_rq_map_sg(hctx->queue, rq, command->sg);
	nents = dma_map_sg(&dd->pdev->dev, command->sg, nents, dma_dir);

	prefetch(&port->flags);

	command->scatter_ents = nents;

	/*
	 * The number of retries for this command before it is
	 * reported as a failure to the upper layers.
	 */
	command->retries = MTIP_MAX_RETRIES;

	/* Fill out fis */
	fis = command->command;
	fis->type        = 0x27;
	fis->opts        = 1 << 7;
	if (dma_dir == DMA_FROM_DEVICE)
		fis->command = ATA_CMD_FPDMA_READ;
	else
		fis->command = ATA_CMD_FPDMA_WRITE;
	fis->lba_low     = start & 0xFF;
	fis->lba_mid     = (start >> 8) & 0xFF;
	fis->lba_hi      = (start >> 16) & 0xFF;
	fis->lba_low_ex  = (start >> 24) & 0xFF;
	fis->lba_mid_ex  = (start >> 32) & 0xFF;
	fis->lba_hi_ex   = (start >> 40) & 0xFF;
	fis->device	 = 1 << 6;
	fis->features    = nsect & 0xFF;
	fis->features_ex = (nsect >> 8) & 0xFF;
	fis->sect_count  = ((rq->tag << 3) | (rq->tag >> 5));
	fis->sect_cnt_ex = 0;
	fis->control     = 0;
	fis->res2        = 0;
	fis->res3        = 0;
	fill_command_sg(dd, command, nents);

	if (unlikely(command->unaligned))
		fis->device |= 1 << 7;

	/* Populate the command header */
	hdr->ctba = cpu_to_le32(command->command_dma & 0xFFFFFFFF);
	if (test_bit(MTIP_PF_HOST_CAP_64, &dd->port->flags))
		hdr->ctbau = cpu_to_le32((command->command_dma >> 16) >> 16);
	hdr->opts = cpu_to_le32((nents << 16) | 5 | AHCI_CMD_PREFETCH);
	hdr->byte_count = 0;

	command->direction = dma_dir;

	/*
	 * To prevent this command from being issued
	 * if an internal command is in progress or error handling is active.
	 */
	if (unlikely(port->flags & MTIP_PF_PAUSE_IO)) {
		set_bit(rq->tag, port->cmds_to_issue);
		set_bit(MTIP_PF_ISSUE_CMDS_BIT, &port->flags);
		return;
	}

	/* Issue the command to the hardware */
	mtip_issue_ncq_command(port, rq->tag);
}

/*
 * Sysfs status dump.
 *
 * @dev  Pointer to the device structure, passed by the kernrel.
 * @attr Pointer to the device_attribute structure passed by the kernel.
 * @buf  Pointer to the char buffer that will receive the stats info.
 *
 * return value
 *	The size, in bytes, of the data copied into buf.
 */
static ssize_t mtip_hw_show_status(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct driver_data *dd = dev_to_disk(dev)->private_data;
	int size = 0;

	if (test_bit(MTIP_DDF_OVER_TEMP_BIT, &dd->dd_flag))
		size += sprintf(buf, "%s", "thermal_shutdown\n");
	else if (test_bit(MTIP_DDF_WRITE_PROTECT_BIT, &dd->dd_flag))
		size += sprintf(buf, "%s", "write_protect\n");
	else
		size += sprintf(buf, "%s", "online\n");

	return size;
}

static DEVICE_ATTR(status, 0444, mtip_hw_show_status, NULL);

static struct attribute *mtip_disk_attrs[] = {
	&dev_attr_status.attr,
	NULL,
};

static const struct attribute_group mtip_disk_attr_group = {
	.attrs = mtip_disk_attrs,
};

static const struct attribute_group *mtip_disk_attr_groups[] = {
	&mtip_disk_attr_group,
	NULL,
};

static ssize_t mtip_hw_read_registers(struct file *f, char __user *ubuf,
				  size_t len, loff_t *offset)
{
	struct driver_data *dd =  (struct driver_data *)f->private_data;
	char *buf;
	u32 group_allocated;
	int size = *offset;
	int n, rv = 0;

	if (!len || size)
		return 0;

	buf = kzalloc(MTIP_DFS_MAX_BUF_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	size += sprintf(&buf[size], "H/ S ACTive      : [ 0x");

	for (n = dd->slot_groups-1; n >= 0; n--)
		size += sprintf(&buf[size], "%08X ",
					 readl(dd->port->s_active[n]));

	size += sprintf(&buf[size], "]\n");
	size += sprintf(&buf[size], "H/ Command Issue : [ 0x");

	for (n = dd->slot_groups-1; n >= 0; n--)
		size += sprintf(&buf[size], "%08X ",
					readl(dd->port->cmd_issue[n]));

	size += sprintf(&buf[size], "]\n");
	size += sprintf(&buf[size], "H/ Completed     : [ 0x");

	for (n = dd->slot_groups-1; n >= 0; n--)
		size += sprintf(&buf[size], "%08X ",
				readl(dd->port->completed[n]));

	size += sprintf(&buf[size], "]\n");
	size += sprintf(&buf[size], "H/ PORT IRQ STAT : [ 0x%08X ]\n",
				readl(dd->port->mmio + PORT_IRQ_STAT));
	size += sprintf(&buf[size], "H/ HOST IRQ STAT : [ 0x%08X ]\n",
				readl(dd->mmio + HOST_IRQ_STAT));
	size += sprintf(&buf[size], "\n");

	size += sprintf(&buf[size], "L/ Commands in Q : [ 0x");

	for (n = dd->slot_groups-1; n >= 0; n--) {
		if (sizeof(long) > sizeof(u32))
			group_allocated =
				dd->port->cmds_to_issue[n/2] >> (32*(n&1));
		else
			group_allocated = dd->port->cmds_to_issue[n];
		size += sprintf(&buf[size], "%08X ", group_allocated);
	}
	size += sprintf(&buf[size], "]\n");

	*offset = size <= len ? size : len;
	size = copy_to_user(ubuf, buf, *offset);
	if (size)
		rv = -EFAULT;

	kfree(buf);
	return rv ? rv : *offset;
}

static ssize_t mtip_hw_read_flags(struct file *f, char __user *ubuf,
				  size_t len, loff_t *offset)
{
	struct driver_data *dd =  (struct driver_data *)f->private_data;
	char *buf;
	int size = *offset;
	int rv = 0;

	if (!len || size)
		return 0;

	buf = kzalloc(MTIP_DFS_MAX_BUF_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	size += sprintf(&buf[size], "Flag-port : [ %08lX ]\n",
							dd->port->flags);
	size += sprintf(&buf[size], "Flag-dd   : [ %08lX ]\n",
							dd->dd_flag);

	*offset = size <= len ? size : len;
	size = copy_to_user(ubuf, buf, *offset);
	if (size)
		rv = -EFAULT;

	kfree(buf);
	return rv ? rv : *offset;
}

static const struct file_operations mtip_regs_fops = {
	.owner  = THIS_MODULE,
	.open   = simple_open,
	.read   = mtip_hw_read_registers,
	.llseek = no_llseek,
};

static const struct file_operations mtip_flags_fops = {
	.owner  = THIS_MODULE,
	.open   = simple_open,
	.read   = mtip_hw_read_flags,
	.llseek = no_llseek,
};

static int mtip_hw_debugfs_init(struct driver_data *dd)
{
	if (!dfs_parent)
		return -1;

	dd->dfs_node = debugfs_create_dir(dd->disk->disk_name, dfs_parent);
	if (IS_ERR_OR_NULL(dd->dfs_node)) {
		dev_warn(&dd->pdev->dev,
			"Error creating node %s under debugfs\n",
						dd->disk->disk_name);
		dd->dfs_node = NULL;
		return -1;
	}

	debugfs_create_file("flags", 0444, dd->dfs_node, dd, &mtip_flags_fops);
	debugfs_create_file("registers", 0444, dd->dfs_node, dd,
			    &mtip_regs_fops);

	return 0;
}

static void mtip_hw_debugfs_exit(struct driver_data *dd)
{
	debugfs_remove_recursive(dd->dfs_node);
}

/*
 * Perform any init/resume time hardware setup
 *
 * @dd Pointer to the driver data structure.
 *
 * return value
 *	None
 */
static inline void hba_setup(struct driver_data *dd)
{
	u32 hwdata;
	hwdata = readl(dd->mmio + HOST_HSORG);

	/* interrupt bug workaround: use only 1 IS bit.*/
	writel(hwdata |
		HSORG_DISABLE_SLOTGRP_INTR |
		HSORG_DISABLE_SLOTGRP_PXIS,
		dd->mmio + HOST_HSORG);
}

static int mtip_device_unaligned_constrained(struct driver_data *dd)
{
	return (dd->pdev->device == P420M_DEVICE_ID ? 1 : 0);
}

/*
 * Detect the details of the product, and store anything needed
 * into the driver data structure.  This includes product type and
 * version and number of slot groups.
 *
 * @dd Pointer to the driver data structure.
 *
 * return value
 *	None
 */
static void mtip_detect_product(struct driver_data *dd)
{
	u32 hwdata;
	unsigned int rev, slotgroups;

	/*
	 * HBA base + 0xFC [15:0] - vendor-specific hardware interface
	 * info register:
	 * [15:8] hardware/software interface rev#
	 * [   3] asic-style interface
	 * [ 2:0] number of slot groups, minus 1 (only valid for asic-style).
	 */
	hwdata = readl(dd->mmio + HOST_HSORG);

	dd->product_type = MTIP_PRODUCT_UNKNOWN;
	dd->slot_groups = 1;

	if (hwdata & 0x8) {
		dd->product_type = MTIP_PRODUCT_ASICFPGA;
		rev = (hwdata & HSORG_HWREV) >> 8;
		slotgroups = (hwdata & HSORG_SLOTGROUPS) + 1;
		dev_info(&dd->pdev->dev,
			"ASIC-FPGA design, HS rev 0x%x, "
			"%i slot groups [%i slots]\n",
			 rev,
			 slotgroups,
			 slotgroups * 32);

		if (slotgroups > MTIP_MAX_SLOT_GROUPS) {
			dev_warn(&dd->pdev->dev,
				"Warning: driver only supports "
				"%i slot groups.\n", MTIP_MAX_SLOT_GROUPS);
			slotgroups = MTIP_MAX_SLOT_GROUPS;
		}
		dd->slot_groups = slotgroups;
		return;
	}

	dev_warn(&dd->pdev->dev, "Unrecognized product id\n");
}

/*
 * Blocking wait for FTL rebuild to complete
 *
 * @dd Pointer to the DRIVER_DATA structure.
 *
 * return value
 *	0	FTL rebuild completed successfully
 *	-EFAULT FTL rebuild error/timeout/interruption
 */
static int mtip_ftl_rebuild_poll(struct driver_data *dd)
{
	unsigned long timeout, cnt = 0, start;

	dev_warn(&dd->pdev->dev,
		"FTL rebuild in progress. Polling for completion.\n");

	start = jiffies;
	timeout = jiffies + msecs_to_jiffies(MTIP_FTL_REBUILD_TIMEOUT_MS);

	do {
		if (unlikely(test_bit(MTIP_DDF_REMOVE_PENDING_BIT,
				&dd->dd_flag)))
			return -EFAULT;
		if (mtip_check_surprise_removal(dd))
			return -EFAULT;

		if (mtip_get_identify(dd->port, NULL) < 0)
			return -EFAULT;

		if (*(dd->port->identify + MTIP_FTL_REBUILD_OFFSET) ==
			MTIP_FTL_REBUILD_MAGIC) {
			ssleep(1);
			/* Print message every 3 minutes */
			if (cnt++ >= 180) {
				dev_warn(&dd->pdev->dev,
				"FTL rebuild in progress (%d secs).\n",
				jiffies_to_msecs(jiffies - start) / 1000);
				cnt = 0;
			}
		} else {
			dev_warn(&dd->pdev->dev,
				"FTL rebuild complete (%d secs).\n",
			jiffies_to_msecs(jiffies - start) / 1000);
			mtip_block_initialize(dd);
			return 0;
		}
	} while (time_before(jiffies, timeout));

	/* Check for timeout */
	dev_err(&dd->pdev->dev,
		"Timed out waiting for FTL rebuild to complete (%d secs).\n",
		jiffies_to_msecs(jiffies - start) / 1000);
	return -EFAULT;
}

static void mtip_softirq_done_fn(struct request *rq)
{
	struct mtip_cmd *cmd = blk_mq_rq_to_pdu(rq);
	struct driver_data *dd = rq->q->queuedata;

	/* Unmap the DMA scatter list entries */
	dma_unmap_sg(&dd->pdev->dev, cmd->sg, cmd->scatter_ents,
							cmd->direction);

	if (unlikely(cmd->unaligned))
		atomic_inc(&dd->port->cmd_slot_unal);

	blk_mq_end_request(rq, cmd->status);
}

static bool mtip_abort_cmd(struct request *req, void *data)
{
	struct mtip_cmd *cmd = blk_mq_rq_to_pdu(req);
	struct driver_data *dd = data;

	dbg_printk(MTIP_DRV_NAME " Aborting request, tag = %d\n", req->tag);

	clear_bit(req->tag, dd->port->cmds_to_issue);
	cmd->status = BLK_STS_IOERR;
	mtip_softirq_done_fn(req);
	return true;
}

static bool mtip_queue_cmd(struct request *req, void *data)
{
	struct driver_data *dd = data;

	set_bit(req->tag, dd->port->cmds_to_issue);
	blk_abort_request(req);
	return true;
}

/*
 * service thread to issue queued commands
 *
 * @data Pointer to the driver data structure.
 *
 * return value
 *	0
 */

static int mtip_service_thread(void *data)
{
	struct driver_data *dd = (struct driver_data *)data;
	unsigned long slot, slot_start, slot_wrap, to;
	unsigned int num_cmd_slots = dd->slot_groups * 32;
	struct mtip_port *port = dd->port;

	while (1) {
		if (kthread_should_stop() ||
			test_bit(MTIP_PF_SVC_THD_STOP_BIT, &port->flags))
			goto st_out;
		clear_bit(MTIP_PF_SVC_THD_ACTIVE_BIT, &port->flags);

		/*
		 * the condition is to check neither an internal command is
		 * is in progress nor error handling is active
		 */
		wait_event_interruptible(port->svc_wait, (port->flags) &&
			(port->flags & MTIP_PF_SVC_THD_WORK));

		if (kthread_should_stop() ||
			test_bit(MTIP_PF_SVC_THD_STOP_BIT, &port->flags))
			goto st_out;

		if (unlikely(test_bit(MTIP_DDF_REMOVE_PENDING_BIT,
				&dd->dd_flag)))
			goto st_out;

		set_bit(MTIP_PF_SVC_THD_ACTIVE_BIT, &port->flags);

restart_eh:
		/* Demux bits: start with error handling */
		if (test_bit(MTIP_PF_EH_ACTIVE_BIT, &port->flags)) {
			mtip_handle_tfe(dd);
			clear_bit(MTIP_PF_EH_ACTIVE_BIT, &port->flags);
		}

		if (test_bit(MTIP_PF_EH_ACTIVE_BIT, &port->flags))
			goto restart_eh;

		if (test_bit(MTIP_PF_TO_ACTIVE_BIT, &port->flags)) {
			to = jiffies + msecs_to_jiffies(5000);

			do {
				mdelay(100);
			} while (atomic_read(&dd->irq_workers_active) != 0 &&
				time_before(jiffies, to));

			if (atomic_read(&dd->irq_workers_active) != 0)
				dev_warn(&dd->pdev->dev,
					"Completion workers still active!");

			blk_mq_quiesce_queue(dd->queue);

			blk_mq_tagset_busy_iter(&dd->tags, mtip_queue_cmd, dd);

			set_bit(MTIP_PF_ISSUE_CMDS_BIT, &dd->port->flags);

			if (mtip_device_reset(dd))
				blk_mq_tagset_busy_iter(&dd->tags,
							mtip_abort_cmd, dd);

			clear_bit(MTIP_PF_TO_ACTIVE_BIT, &dd->port->flags);

			blk_mq_unquiesce_queue(dd->queue);
		}

		if (test_bit(MTIP_PF_ISSUE_CMDS_BIT, &port->flags)) {
			slot = 1;
			/* used to restrict the loop to one iteration */
			slot_start = num_cmd_slots;
			slot_wrap = 0;
			while (1) {
				slot = find_next_bit(port->cmds_to_issue,
						num_cmd_slots, slot);
				if (slot_wrap == 1) {
					if ((slot_start >= slot) ||
						(slot >= num_cmd_slots))
						break;
				}
				if (unlikely(slot_start == num_cmd_slots))
					slot_start = slot;

				if (unlikely(slot == num_cmd_slots)) {
					slot = 1;
					slot_wrap = 1;
					continue;
				}

				/* Issue the command to the hardware */
				mtip_issue_ncq_command(port, slot);

				clear_bit(slot, port->cmds_to_issue);
			}

			clear_bit(MTIP_PF_ISSUE_CMDS_BIT, &port->flags);
		}

		if (test_bit(MTIP_PF_REBUILD_BIT, &port->flags)) {
			if (mtip_ftl_rebuild_poll(dd) == 0)
				clear_bit(MTIP_PF_REBUILD_BIT, &port->flags);
		}
	}

st_out:
	return 0;
}

/*
 * DMA region teardown
 *
 * @dd Pointer to driver_data structure
 *
 * return value
 *      None
 */
static void mtip_dma_free(struct driver_data *dd)
{
	struct mtip_port *port = dd->port;

	if (port->block1)
		dma_free_coherent(&dd->pdev->dev, BLOCK_DMA_ALLOC_SZ,
					port->block1, port->block1_dma);

	if (port->command_list) {
		dma_free_coherent(&dd->pdev->dev, AHCI_CMD_TBL_SZ,
				port->command_list, port->command_list_dma);
	}
}

/*
 * DMA region setup
 *
 * @dd Pointer to driver_data structure
 *
 * return value
 *      -ENOMEM Not enough free DMA region space to initialize driver
 */
static int mtip_dma_alloc(struct driver_data *dd)
{
	struct mtip_port *port = dd->port;

	/* Allocate dma memory for RX Fis, Identify, and Sector Buffer */
	port->block1 =
		dma_alloc_coherent(&dd->pdev->dev, BLOCK_DMA_ALLOC_SZ,
					&port->block1_dma, GFP_KERNEL);
	if (!port->block1)
		return -ENOMEM;

	/* Allocate dma memory for command list */
	port->command_list =
		dma_alloc_coherent(&dd->pdev->dev, AHCI_CMD_TBL_SZ,
					&port->command_list_dma, GFP_KERNEL);
	if (!port->command_list) {
		dma_free_coherent(&dd->pdev->dev, BLOCK_DMA_ALLOC_SZ,
					port->block1, port->block1_dma);
		port->block1 = NULL;
		port->block1_dma = 0;
		return -ENOMEM;
	}

	/* Setup all pointers into first DMA region */
	port->rxfis         = port->block1 + AHCI_RX_FIS_OFFSET;
	port->rxfis_dma     = port->block1_dma + AHCI_RX_FIS_OFFSET;
	port->identify      = port->block1 + AHCI_IDFY_OFFSET;
	port->identify_dma  = port->block1_dma + AHCI_IDFY_OFFSET;
	port->log_buf       = port->block1 + AHCI_SECTBUF_OFFSET;
	port->log_buf_dma   = port->block1_dma + AHCI_SECTBUF_OFFSET;
	port->smart_buf     = port->block1 + AHCI_SMARTBUF_OFFSET;
	port->smart_buf_dma = port->block1_dma + AHCI_SMARTBUF_OFFSET;

	return 0;
}

static int mtip_hw_get_identify(struct driver_data *dd)
{
	struct smart_attr attr242;
	unsigned char *buf;
	int rv;

	if (mtip_get_identify(dd->port, NULL) < 0)
		return -EFAULT;

	if (*(dd->port->identify + MTIP_FTL_REBUILD_OFFSET) ==
		MTIP_FTL_REBUILD_MAGIC) {
		set_bit(MTIP_PF_REBUILD_BIT, &dd->port->flags);
		return MTIP_FTL_REBUILD_MAGIC;
	}
	mtip_dump_identify(dd->port);

	/* check write protect, over temp and rebuild statuses */
	rv = mtip_read_log_page(dd->port, ATA_LOG_SATA_NCQ,
				dd->port->log_buf,
				dd->port->log_buf_dma, 1);
	if (rv) {
		dev_warn(&dd->pdev->dev,
			"Error in READ LOG EXT (10h) command\n");
		/* non-critical error, don't fail the load */
	} else {
		buf = (unsigned char *)dd->port->log_buf;
		if (buf[259] & 0x1) {
			dev_info(&dd->pdev->dev,
				"Write protect bit is set.\n");
			set_bit(MTIP_DDF_WRITE_PROTECT_BIT, &dd->dd_flag);
		}
		if (buf[288] == 0xF7) {
			dev_info(&dd->pdev->dev,
				"Exceeded Tmax, drive in thermal shutdown.\n");
			set_bit(MTIP_DDF_OVER_TEMP_BIT, &dd->dd_flag);
		}
		if (buf[288] == 0xBF) {
			dev_info(&dd->pdev->dev,
				"Drive indicates rebuild has failed.\n");
			set_bit(MTIP_DDF_REBUILD_FAILED_BIT, &dd->dd_flag);
		}
	}

	/* get write protect progess */
	memset(&attr242, 0, sizeof(struct smart_attr));
	if (mtip_get_smart_attr(dd->port, 242, &attr242))
		dev_warn(&dd->pdev->dev,
				"Unable to check write protect progress\n");
	else
		dev_info(&dd->pdev->dev,
				"Write protect progress: %u%% (%u blocks)\n",
				attr242.cur, le32_to_cpu(attr242.data));

	return rv;
}

/*
 * Called once for each card.
 *
 * @dd Pointer to the driver data structure.
 *
 * return value
 *	0 on success, else an error code.
 */
static int mtip_hw_init(struct driver_data *dd)
{
	int i;
	int rv;
	unsigned long timeout, timetaken;

	dd->mmio = pcim_iomap_table(dd->pdev)[MTIP_ABAR];

	mtip_detect_product(dd);
	if (dd->product_type == MTIP_PRODUCT_UNKNOWN) {
		rv = -EIO;
		goto out1;
	}

	hba_setup(dd);

	dd->port = kzalloc_node(sizeof(struct mtip_port), GFP_KERNEL,
				dd->numa_node);
	if (!dd->port)
		return -ENOMEM;

	/* Continue workqueue setup */
	for (i = 0; i < MTIP_MAX_SLOT_GROUPS; i++)
		dd->work[i].port = dd->port;

	/* Enable unaligned IO constraints for some devices */
	if (mtip_device_unaligned_constrained(dd))
		dd->unal_qdepth = MTIP_MAX_UNALIGNED_SLOTS;
	else
		dd->unal_qdepth = 0;

	atomic_set(&dd->port->cmd_slot_unal, dd->unal_qdepth);

	/* Spinlock to prevent concurrent issue */
	for (i = 0; i < MTIP_MAX_SLOT_GROUPS; i++)
		spin_lock_init(&dd->port->cmd_issue_lock[i]);

	/* Set the port mmio base address. */
	dd->port->mmio	= dd->mmio + PORT_OFFSET;
	dd->port->dd	= dd;

	/* DMA allocations */
	rv = mtip_dma_alloc(dd);
	if (rv < 0)
		goto out1;

	/* Setup the pointers to the extended s_active and CI registers. */
	for (i = 0; i < dd->slot_groups; i++) {
		dd->port->s_active[i] =
			dd->port->mmio + i*0x80 + PORT_SCR_ACT;
		dd->port->cmd_issue[i] =
			dd->port->mmio + i*0x80 + PORT_COMMAND_ISSUE;
		dd->port->completed[i] =
			dd->port->mmio + i*0x80 + PORT_SDBV;
	}

	timetaken = jiffies;
	timeout = jiffies + msecs_to_jiffies(30000);
	while (((readl(dd->port->mmio + PORT_SCR_STAT) & 0x0F) != 0x03) &&
		 time_before(jiffies, timeout)) {
		mdelay(100);
	}
	if (unlikely(mtip_check_surprise_removal(dd))) {
		timetaken = jiffies - timetaken;
		dev_warn(&dd->pdev->dev,
			"Surprise removal detected at %u ms\n",
			jiffies_to_msecs(timetaken));
		rv = -ENODEV;
		goto out2 ;
	}
	if (unlikely(test_bit(MTIP_DDF_REMOVE_PENDING_BIT, &dd->dd_flag))) {
		timetaken = jiffies - timetaken;
		dev_warn(&dd->pdev->dev,
			"Removal detected at %u ms\n",
			jiffies_to_msecs(timetaken));
		rv = -EFAULT;
		goto out2;
	}

	/* Conditionally reset the HBA. */
	if (!(readl(dd->mmio + HOST_CAP) & HOST_CAP_NZDMA)) {
		if (mtip_hba_reset(dd) < 0) {
			dev_err(&dd->pdev->dev,
				"Card did not reset within timeout\n");
			rv = -EIO;
			goto out2;
		}
	} else {
		/* Clear any pending interrupts on the HBA */
		writel(readl(dd->mmio + HOST_IRQ_STAT),
			dd->mmio + HOST_IRQ_STAT);
	}

	mtip_init_port(dd->port);
	mtip_start_port(dd->port);

	/* Setup the ISR and enable interrupts. */
	rv = request_irq(dd->pdev->irq, mtip_irq_handler, IRQF_SHARED,
			 dev_driver_string(&dd->pdev->dev), dd);
	if (rv) {
		dev_err(&dd->pdev->dev,
			"Unable to allocate IRQ %d\n", dd->pdev->irq);
		goto out2;
	}
	irq_set_affinity_hint(dd->pdev->irq, get_cpu_mask(dd->isr_binding));

	/* Enable interrupts on the HBA. */
	writel(readl(dd->mmio + HOST_CTL) | HOST_IRQ_EN,
					dd->mmio + HOST_CTL);

	init_waitqueue_head(&dd->port->svc_wait);

	if (test_bit(MTIP_DDF_REMOVE_PENDING_BIT, &dd->dd_flag)) {
		rv = -EFAULT;
		goto out3;
	}

	return rv;

out3:
	/* Disable interrupts on the HBA. */
	writel(readl(dd->mmio + HOST_CTL) & ~HOST_IRQ_EN,
			dd->mmio + HOST_CTL);

	/* Release the IRQ. */
	irq_set_affinity_hint(dd->pdev->irq, NULL);
	free_irq(dd->pdev->irq, dd);

out2:
	mtip_deinit_port(dd->port);
	mtip_dma_free(dd);

out1:
	/* Free the memory allocated for the for structure. */
	kfree(dd->port);

	return rv;
}

static int mtip_standby_drive(struct driver_data *dd)
{
	int rv = 0;

	if (dd->sr || !dd->port)
		return -ENODEV;
	/*
	 * Send standby immediate (E0h) to the drive so that it
	 * saves its state.
	 */
	if (!test_bit(MTIP_PF_REBUILD_BIT, &dd->port->flags) &&
	    !test_bit(MTIP_DDF_REBUILD_FAILED_BIT, &dd->dd_flag) &&
	    !test_bit(MTIP_DDF_SEC_LOCK_BIT, &dd->dd_flag)) {
		rv = mtip_standby_immediate(dd->port);
		if (rv)
			dev_warn(&dd->pdev->dev,
				"STANDBY IMMEDIATE failed\n");
	}
	return rv;
}

/*
 * Called to deinitialize an interface.
 *
 * @dd Pointer to the driver data structure.
 *
 * return value
 *	0
 */
static int mtip_hw_exit(struct driver_data *dd)
{
	if (!dd->sr) {
		/* de-initialize the port. */
		mtip_deinit_port(dd->port);

		/* Disable interrupts on the HBA. */
		writel(readl(dd->mmio + HOST_CTL) & ~HOST_IRQ_EN,
				dd->mmio + HOST_CTL);
	}

	/* Release the IRQ. */
	irq_set_affinity_hint(dd->pdev->irq, NULL);
	free_irq(dd->pdev->irq, dd);
	msleep(1000);

	/* Free dma regions */
	mtip_dma_free(dd);

	/* Free the memory allocated for the for structure. */
	kfree(dd->port);
	dd->port = NULL;

	return 0;
}

/*
 * Issue a Standby Immediate command to the device.
 *
 * This function is called by the Block Layer just before the
 * system powers off during a shutdown.
 *
 * @dd Pointer to the driver data structure.
 *
 * return value
 *	0
 */
static int mtip_hw_shutdown(struct driver_data *dd)
{
	/*
	 * Send standby immediate (E0h) to the drive so that it
	 * saves its state.
	 */
	mtip_standby_drive(dd);

	return 0;
}

/*
 * Suspend function
 *
 * This function is called by the Block Layer just before the
 * system hibernates.
 *
 * @dd Pointer to the driver data structure.
 *
 * return value
 *	0	Suspend was successful
 *	-EFAULT Suspend was not successful
 */
static int mtip_hw_suspend(struct driver_data *dd)
{
	/*
	 * Send standby immediate (E0h) to the drive
	 * so that it saves its state.
	 */
	if (mtip_standby_drive(dd) != 0) {
		dev_err(&dd->pdev->dev,
			"Failed standby-immediate command\n");
		return -EFAULT;
	}

	/* Disable interrupts on the HBA.*/
	writel(readl(dd->mmio + HOST_CTL) & ~HOST_IRQ_EN,
			dd->mmio + HOST_CTL);
	mtip_deinit_port(dd->port);

	return 0;
}

/*
 * Resume function
 *
 * This function is called by the Block Layer as the
 * system resumes.
 *
 * @dd Pointer to the driver data structure.
 *
 * return value
 *	0	Resume was successful
 *      -EFAULT Resume was not successful
 */
static int mtip_hw_resume(struct driver_data *dd)
{
	/* Perform any needed hardware setup steps */
	hba_setup(dd);

	/* Reset the HBA */
	if (mtip_hba_reset(dd) != 0) {
		dev_err(&dd->pdev->dev,
			"Unable to reset the HBA\n");
		return -EFAULT;
	}

	/*
	 * Enable the port, DMA engine, and FIS reception specific
	 * h/w in controller.
	 */
	mtip_init_port(dd->port);
	mtip_start_port(dd->port);

	/* Enable interrupts on the HBA.*/
	writel(readl(dd->mmio + HOST_CTL) | HOST_IRQ_EN,
			dd->mmio + HOST_CTL);

	return 0;
}

/*
 * Helper function for reusing disk name
 * upon hot insertion.
 */
static int rssd_disk_name_format(char *prefix,
				 int index,
				 char *buf,
				 int buflen)
{
	const int base = 'z' - 'a' + 1;
	char *begin = buf + strlen(prefix);
	char *end = buf + buflen;
	char *p;
	int unit;

	p = end - 1;
	*p = '\0';
	unit = base;
	do {
		if (p == begin)
			return -EINVAL;
		*--p = 'a' + (index % unit);
		index = (index / unit) - 1;
	} while (index >= 0);

	memmove(begin, p, end - p);
	memcpy(buf, prefix, strlen(prefix));

	return 0;
}

/*
 * Block layer IOCTL handler.
 *
 * @dev Pointer to the block_device structure.
 * @mode ignored
 * @cmd IOCTL command passed from the user application.
 * @arg Argument passed from the user application.
 *
 * return value
 *	0        IOCTL completed successfully.
 *	-ENOTTY  IOCTL not supported or invalid driver data
 *                 structure pointer.
 */
static int mtip_block_ioctl(struct block_device *dev,
			    fmode_t mode,
			    unsigned cmd,
			    unsigned long arg)
{
	struct driver_data *dd = dev->bd_disk->private_data;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	if (!dd)
		return -ENOTTY;

	if (unlikely(test_bit(MTIP_DDF_REMOVE_PENDING_BIT, &dd->dd_flag)))
		return -ENOTTY;

	switch (cmd) {
	case BLKFLSBUF:
		return -ENOTTY;
	default:
		return mtip_hw_ioctl(dd, cmd, arg);
	}
}

#ifdef CONFIG_COMPAT
/*
 * Block layer compat IOCTL handler.
 *
 * @dev Pointer to the block_device structure.
 * @mode ignored
 * @cmd IOCTL command passed from the user application.
 * @arg Argument passed from the user application.
 *
 * return value
 *	0        IOCTL completed successfully.
 *	-ENOTTY  IOCTL not supported or invalid driver data
 *                 structure pointer.
 */
static int mtip_block_compat_ioctl(struct block_device *dev,
			    fmode_t mode,
			    unsigned cmd,
			    unsigned long arg)
{
	struct driver_data *dd = dev->bd_disk->private_data;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	if (!dd)
		return -ENOTTY;

	if (unlikely(test_bit(MTIP_DDF_REMOVE_PENDING_BIT, &dd->dd_flag)))
		return -ENOTTY;

	switch (cmd) {
	case BLKFLSBUF:
		return -ENOTTY;
	case HDIO_DRIVE_TASKFILE: {
		struct mtip_compat_ide_task_request_s __user *compat_req_task;
		ide_task_request_t req_task;
		int compat_tasksize, outtotal, ret;

		compat_tasksize =
			sizeof(struct mtip_compat_ide_task_request_s);

		compat_req_task =
			(struct mtip_compat_ide_task_request_s __user *) arg;

		if (copy_from_user(&req_task, (void __user *) arg,
			compat_tasksize - (2 * sizeof(compat_long_t))))
			return -EFAULT;

		if (get_user(req_task.out_size, &compat_req_task->out_size))
			return -EFAULT;

		if (get_user(req_task.in_size, &compat_req_task->in_size))
			return -EFAULT;

		outtotal = sizeof(struct mtip_compat_ide_task_request_s);

		ret = exec_drive_taskfile(dd, (void __user *) arg,
						&req_task, outtotal);

		if (copy_to_user((void __user *) arg, &req_task,
				compat_tasksize -
				(2 * sizeof(compat_long_t))))
			return -EFAULT;

		if (put_user(req_task.out_size, &compat_req_task->out_size))
			return -EFAULT;

		if (put_user(req_task.in_size, &compat_req_task->in_size))
			return -EFAULT;

		return ret;
	}
	default:
		return mtip_hw_ioctl(dd, cmd, arg);
	}
}
#endif

/*
 * Obtain the geometry of the device.
 *
 * You may think that this function is obsolete, but some applications,
 * fdisk for example still used CHS values. This function describes the
 * device as having 224 heads and 56 sectors per cylinder. These values are
 * chosen so that each cylinder is aligned on a 4KB boundary. Since a
 * partition is described in terms of a start and end cylinder this means
 * that each partition is also 4KB aligned. Non-aligned partitions adversely
 * affects performance.
 *
 * @dev Pointer to the block_device strucutre.
 * @geo Pointer to a hd_geometry structure.
 *
 * return value
 *	0       Operation completed successfully.
 *	-ENOTTY An error occurred while reading the drive capacity.
 */
static int mtip_block_getgeo(struct block_device *dev,
				struct hd_geometry *geo)
{
	struct driver_data *dd = dev->bd_disk->private_data;
	sector_t capacity;

	if (!dd)
		return -ENOTTY;

	if (!(mtip_hw_get_capacity(dd, &capacity))) {
		dev_warn(&dd->pdev->dev,
			"Could not get drive capacity.\n");
		return -ENOTTY;
	}

	geo->heads = 224;
	geo->sectors = 56;
	sector_div(capacity, (geo->heads * geo->sectors));
	geo->cylinders = capacity;
	return 0;
}

static void mtip_block_free_disk(struct gendisk *disk)
{
	struct driver_data *dd = disk->private_data;

	ida_free(&rssd_index_ida, dd->index);
	kfree(dd);
}

/*
 * Block device operation function.
 *
 * This structure contains pointers to the functions required by the block
 * layer.
 */
static const struct block_device_operations mtip_block_ops = {
	.ioctl		= mtip_block_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= mtip_block_compat_ioctl,
#endif
	.getgeo		= mtip_block_getgeo,
	.free_disk	= mtip_block_free_disk,
	.owner		= THIS_MODULE
};

static inline bool is_se_active(struct driver_data *dd)
{
	if (unlikely(test_bit(MTIP_PF_SE_ACTIVE_BIT, &dd->port->flags))) {
		if (dd->port->ic_pause_timer) {
			unsigned long to = dd->port->ic_pause_timer +
							msecs_to_jiffies(1000);
			if (time_after(jiffies, to)) {
				clear_bit(MTIP_PF_SE_ACTIVE_BIT,
							&dd->port->flags);
				clear_bit(MTIP_DDF_SEC_LOCK_BIT, &dd->dd_flag);
				dd->port->ic_pause_timer = 0;
				wake_up_interruptible(&dd->port->svc_wait);
				return false;
			}
		}
		return true;
	}
	return false;
}

static inline bool is_stopped(struct driver_data *dd, struct request *rq)
{
	if (likely(!(dd->dd_flag & MTIP_DDF_STOP_IO)))
		return false;

	if (test_bit(MTIP_DDF_REMOVE_PENDING_BIT, &dd->dd_flag))
		return true;
	if (test_bit(MTIP_DDF_OVER_TEMP_BIT, &dd->dd_flag))
		return true;
	if (test_bit(MTIP_DDF_WRITE_PROTECT_BIT, &dd->dd_flag) &&
	    rq_data_dir(rq))
		return true;
	if (test_bit(MTIP_DDF_SEC_LOCK_BIT, &dd->dd_flag))
		return true;
	if (test_bit(MTIP_DDF_REBUILD_FAILED_BIT, &dd->dd_flag))
		return true;

	return false;
}

static bool mtip_check_unal_depth(struct blk_mq_hw_ctx *hctx,
				  struct request *rq)
{
	struct driver_data *dd = hctx->queue->queuedata;
	struct mtip_cmd *cmd = blk_mq_rq_to_pdu(rq);

	if (rq_data_dir(rq) == READ || !dd->unal_qdepth)
		return false;

	/*
	 * If unaligned depth must be limited on this controller, mark it
	 * as unaligned if the IO isn't on a 4k boundary (start of length).
	 */
	if (blk_rq_sectors(rq) <= 64) {
		if ((blk_rq_pos(rq) & 7) || (blk_rq_sectors(rq) & 7))
			cmd->unaligned = 1;
	}

	if (cmd->unaligned && atomic_dec_if_positive(&dd->port->cmd_slot_unal) >= 0)
		return true;

	return false;
}

static blk_status_t mtip_issue_reserved_cmd(struct blk_mq_hw_ctx *hctx,
		struct request *rq)
{
	struct driver_data *dd = hctx->queue->queuedata;
	struct mtip_cmd *cmd = blk_mq_rq_to_pdu(rq);
	struct mtip_int_cmd *icmd = cmd->icmd;
	struct mtip_cmd_hdr *hdr =
		dd->port->command_list + sizeof(struct mtip_cmd_hdr) * rq->tag;
	struct mtip_cmd_sg *command_sg;

	if (mtip_commands_active(dd->port))
		return BLK_STS_DEV_RESOURCE;

	hdr->ctba = cpu_to_le32(cmd->command_dma & 0xFFFFFFFF);
	if (test_bit(MTIP_PF_HOST_CAP_64, &dd->port->flags))
		hdr->ctbau = cpu_to_le32((cmd->command_dma >> 16) >> 16);
	/* Populate the SG list */
	hdr->opts = cpu_to_le32(icmd->opts | icmd->fis_len);
	if (icmd->buf_len) {
		command_sg = cmd->command + AHCI_CMD_TBL_HDR_SZ;

		command_sg->info = cpu_to_le32((icmd->buf_len-1) & 0x3FFFFF);
		command_sg->dba	= cpu_to_le32(icmd->buffer & 0xFFFFFFFF);
		command_sg->dba_upper =
			cpu_to_le32((icmd->buffer >> 16) >> 16);

		hdr->opts |= cpu_to_le32((1 << 16));
	}

	/* Populate the command header */
	hdr->byte_count = 0;

	blk_mq_start_request(rq);
	mtip_issue_non_ncq_command(dd->port, rq->tag);
	return 0;
}

static blk_status_t mtip_queue_rq(struct blk_mq_hw_ctx *hctx,
			 const struct blk_mq_queue_data *bd)
{
	struct driver_data *dd = hctx->queue->queuedata;
	struct request *rq = bd->rq;
	struct mtip_cmd *cmd = blk_mq_rq_to_pdu(rq);

	if (blk_rq_is_passthrough(rq))
		return mtip_issue_reserved_cmd(hctx, rq);

	if (unlikely(mtip_check_unal_depth(hctx, rq)))
		return BLK_STS_DEV_RESOURCE;

	if (is_se_active(dd) || is_stopped(dd, rq))
		return BLK_STS_IOERR;

	blk_mq_start_request(rq);

	mtip_hw_submit_io(dd, rq, cmd, hctx);
	return BLK_STS_OK;
}

static void mtip_free_cmd(struct blk_mq_tag_set *set, struct request *rq,
			  unsigned int hctx_idx)
{
	struct driver_data *dd = set->driver_data;
	struct mtip_cmd *cmd = blk_mq_rq_to_pdu(rq);

	if (!cmd->command)
		return;

	dma_free_coherent(&dd->pdev->dev, CMD_DMA_ALLOC_SZ, cmd->command,
			  cmd->command_dma);
}

static int mtip_init_cmd(struct blk_mq_tag_set *set, struct request *rq,
			 unsigned int hctx_idx, unsigned int numa_node)
{
	struct driver_data *dd = set->driver_data;
	struct mtip_cmd *cmd = blk_mq_rq_to_pdu(rq);

	cmd->command = dma_alloc_coherent(&dd->pdev->dev, CMD_DMA_ALLOC_SZ,
			&cmd->command_dma, GFP_KERNEL);
	if (!cmd->command)
		return -ENOMEM;

	sg_init_table(cmd->sg, MTIP_MAX_SG);
	return 0;
}

static enum blk_eh_timer_return mtip_cmd_timeout(struct request *req)
{
	struct driver_data *dd = req->q->queuedata;

	if (blk_mq_is_reserved_rq(req)) {
		struct mtip_cmd *cmd = blk_mq_rq_to_pdu(req);

		cmd->status = BLK_STS_TIMEOUT;
		blk_mq_complete_request(req);
		return BLK_EH_DONE;
	}

	if (test_bit(req->tag, dd->port->cmds_to_issue))
		goto exit_handler;

	if (test_and_set_bit(MTIP_PF_TO_ACTIVE_BIT, &dd->port->flags))
		goto exit_handler;

	wake_up_interruptible(&dd->port->svc_wait);
exit_handler:
	return BLK_EH_RESET_TIMER;
}

static const struct blk_mq_ops mtip_mq_ops = {
	.queue_rq	= mtip_queue_rq,
	.init_request	= mtip_init_cmd,
	.exit_request	= mtip_free_cmd,
	.complete	= mtip_softirq_done_fn,
	.timeout        = mtip_cmd_timeout,
};

/*
 * Block layer initialization function.
 *
 * This function is called once by the PCI layer for each P320
 * device that is connected to the system.
 *
 * @dd Pointer to the driver data structure.
 *
 * return value
 *	0 on success else an error code.
 */
static int mtip_block_initialize(struct driver_data *dd)
{
	int rv = 0, wait_for_rebuild = 0;
	sector_t capacity;
	unsigned int index = 0;

	if (dd->disk)
		goto skip_create_disk; /* hw init done, before rebuild */

	if (mtip_hw_init(dd)) {
		rv = -EINVAL;
		goto protocol_init_error;
	}

	memset(&dd->tags, 0, sizeof(dd->tags));
	dd->tags.ops = &mtip_mq_ops;
	dd->tags.nr_hw_queues = 1;
	dd->tags.queue_depth = MTIP_MAX_COMMAND_SLOTS;
	dd->tags.reserved_tags = 1;
	dd->tags.cmd_size = sizeof(struct mtip_cmd);
	dd->tags.numa_node = dd->numa_node;
	dd->tags.flags = BLK_MQ_F_SHOULD_MERGE;
	dd->tags.driver_data = dd;
	dd->tags.timeout = MTIP_NCQ_CMD_TIMEOUT_MS;

	rv = blk_mq_alloc_tag_set(&dd->tags);
	if (rv) {
		dev_err(&dd->pdev->dev,
			"Unable to allocate request queue\n");
		goto block_queue_alloc_tag_error;
	}

	dd->disk = blk_mq_alloc_disk(&dd->tags, dd);
	if (IS_ERR(dd->disk)) {
		dev_err(&dd->pdev->dev,
			"Unable to allocate request queue\n");
		rv = -ENOMEM;
		goto block_queue_alloc_init_error;
	}
	dd->queue		= dd->disk->queue;

	rv = ida_alloc(&rssd_index_ida, GFP_KERNEL);
	if (rv < 0)
		goto ida_get_error;
	index = rv;

	rv = rssd_disk_name_format("rssd",
				index,
				dd->disk->disk_name,
				DISK_NAME_LEN);
	if (rv)
		goto disk_index_error;

	dd->disk->major		= dd->major;
	dd->disk->first_minor	= index * MTIP_MAX_MINORS;
	dd->disk->minors 	= MTIP_MAX_MINORS;
	dd->disk->fops		= &mtip_block_ops;
	dd->disk->private_data	= dd;
	dd->index		= index;

	mtip_hw_debugfs_init(dd);

skip_create_disk:
	/* Initialize the protocol layer. */
	wait_for_rebuild = mtip_hw_get_identify(dd);
	if (wait_for_rebuild < 0) {
		dev_err(&dd->pdev->dev,
			"Protocol layer initialization failed\n");
		rv = -EINVAL;
		goto init_hw_cmds_error;
	}

	/*
	 * if rebuild pending, start the service thread, and delay the block
	 * queue creation and device_add_disk()
	 */
	if (wait_for_rebuild == MTIP_FTL_REBUILD_MAGIC)
		goto start_service_thread;

	/* Set device limits. */
	blk_queue_flag_set(QUEUE_FLAG_NONROT, dd->queue);
	blk_queue_flag_clear(QUEUE_FLAG_ADD_RANDOM, dd->queue);
	blk_queue_max_segments(dd->queue, MTIP_MAX_SG);
	blk_queue_physical_block_size(dd->queue, 4096);
	blk_queue_max_hw_sectors(dd->queue, 0xffff);
	blk_queue_max_segment_size(dd->queue, 0x400000);
	dma_set_max_seg_size(&dd->pdev->dev, 0x400000);
	blk_queue_io_min(dd->queue, 4096);

	/* Set the capacity of the device in 512 byte sectors. */
	if (!(mtip_hw_get_capacity(dd, &capacity))) {
		dev_warn(&dd->pdev->dev,
			"Could not read drive capacity\n");
		rv = -EIO;
		goto read_capacity_error;
	}
	set_capacity(dd->disk, capacity);

	/* Enable the block device and add it to /dev */
	rv = device_add_disk(&dd->pdev->dev, dd->disk, mtip_disk_attr_groups);
	if (rv)
		goto read_capacity_error;

	if (dd->mtip_svc_handler) {
		set_bit(MTIP_DDF_INIT_DONE_BIT, &dd->dd_flag);
		return rv; /* service thread created for handling rebuild */
	}

start_service_thread:
	dd->mtip_svc_handler = kthread_create_on_node(mtip_service_thread,
						dd, dd->numa_node,
						"mtip_svc_thd_%02d", index);

	if (IS_ERR(dd->mtip_svc_handler)) {
		dev_err(&dd->pdev->dev, "service thread failed to start\n");
		dd->mtip_svc_handler = NULL;
		rv = -EFAULT;
		goto kthread_run_error;
	}
	wake_up_process(dd->mtip_svc_handler);
	if (wait_for_rebuild == MTIP_FTL_REBUILD_MAGIC)
		rv = wait_for_rebuild;

	return rv;

kthread_run_error:
	/* Delete our gendisk. This also removes the device from /dev */
	del_gendisk(dd->disk);
read_capacity_error:
init_hw_cmds_error:
	mtip_hw_debugfs_exit(dd);
disk_index_error:
	ida_free(&rssd_index_ida, index);
ida_get_error:
	put_disk(dd->disk);
block_queue_alloc_init_error:
	blk_mq_free_tag_set(&dd->tags);
block_queue_alloc_tag_error:
	mtip_hw_exit(dd); /* De-initialize the protocol layer. */
protocol_init_error:
	return rv;
}

/*
 * Function called by the PCI layer when just before the
 * machine shuts down.
 *
 * If a protocol layer shutdown function is present it will be called
 * by this function.
 *
 * @dd Pointer to the driver data structure.
 *
 * return value
 *	0
 */
static int mtip_block_shutdown(struct driver_data *dd)
{
	mtip_hw_shutdown(dd);

	dev_info(&dd->pdev->dev,
		"Shutting down %s ...\n", dd->disk->disk_name);

	if (test_bit(MTIP_DDF_INIT_DONE_BIT, &dd->dd_flag))
		del_gendisk(dd->disk);

	blk_mq_free_tag_set(&dd->tags);
	put_disk(dd->disk);
	return 0;
}

static int mtip_block_suspend(struct driver_data *dd)
{
	dev_info(&dd->pdev->dev,
		"Suspending %s ...\n", dd->disk->disk_name);
	mtip_hw_suspend(dd);
	return 0;
}

static int mtip_block_resume(struct driver_data *dd)
{
	dev_info(&dd->pdev->dev, "Resuming %s ...\n",
		dd->disk->disk_name);
	mtip_hw_resume(dd);
	return 0;
}

static void drop_cpu(int cpu)
{
	cpu_use[cpu]--;
}

static int get_least_used_cpu_on_node(int node)
{
	int cpu, least_used_cpu, least_cnt;
	const struct cpumask *node_mask;

	node_mask = cpumask_of_node(node);
	least_used_cpu = cpumask_first(node_mask);
	least_cnt = cpu_use[least_used_cpu];
	cpu = least_used_cpu;

	for_each_cpu(cpu, node_mask) {
		if (cpu_use[cpu] < least_cnt) {
			least_used_cpu = cpu;
			least_cnt = cpu_use[cpu];
		}
	}
	cpu_use[least_used_cpu]++;
	return least_used_cpu;
}

/* Helper for selecting a node in round robin mode */
static inline int mtip_get_next_rr_node(void)
{
	static int next_node = NUMA_NO_NODE;

	if (next_node == NUMA_NO_NODE) {
		next_node = first_online_node;
		return next_node;
	}

	next_node = next_online_node(next_node);
	if (next_node == MAX_NUMNODES)
		next_node = first_online_node;
	return next_node;
}

static DEFINE_HANDLER(0);
static DEFINE_HANDLER(1);
static DEFINE_HANDLER(2);
static DEFINE_HANDLER(3);
static DEFINE_HANDLER(4);
static DEFINE_HANDLER(5);
static DEFINE_HANDLER(6);
static DEFINE_HANDLER(7);

static void mtip_disable_link_opts(struct driver_data *dd, struct pci_dev *pdev)
{
	unsigned short pcie_dev_ctrl;

	if (pci_is_pcie(pdev)) {
		pcie_capability_read_word(pdev, PCI_EXP_DEVCTL, &pcie_dev_ctrl);
		if (pcie_dev_ctrl & PCI_EXP_DEVCTL_NOSNOOP_EN ||
		    pcie_dev_ctrl & PCI_EXP_DEVCTL_RELAX_EN) {
			dev_info(&dd->pdev->dev,
				"Disabling ERO/No-Snoop on bridge device %04x:%04x\n",
					pdev->vendor, pdev->device);
			pcie_dev_ctrl &= ~(PCI_EXP_DEVCTL_NOSNOOP_EN |
						PCI_EXP_DEVCTL_RELAX_EN);
			pcie_capability_write_word(pdev, PCI_EXP_DEVCTL,
				pcie_dev_ctrl);
		}
	}
}

static void mtip_fix_ero_nosnoop(struct driver_data *dd, struct pci_dev *pdev)
{
	/*
	 * This workaround is specific to AMD/ATI chipset with a PCI upstream
	 * device with device id 0x5aXX
	 */
	if (pdev->bus && pdev->bus->self) {
		if (pdev->bus->self->vendor == PCI_VENDOR_ID_ATI &&
		    ((pdev->bus->self->device & 0xff00) == 0x5a00)) {
			mtip_disable_link_opts(dd, pdev->bus->self);
		} else {
			/* Check further up the topology */
			struct pci_dev *parent_dev = pdev->bus->self;
			if (parent_dev->bus &&
				parent_dev->bus->parent &&
				parent_dev->bus->parent->self &&
				parent_dev->bus->parent->self->vendor ==
					 PCI_VENDOR_ID_ATI &&
				(parent_dev->bus->parent->self->device &
					0xff00) == 0x5a00) {
				mtip_disable_link_opts(dd,
					parent_dev->bus->parent->self);
			}
		}
	}
}

/*
 * Called for each supported PCI device detected.
 *
 * This function allocates the private data structure, enables the
 * PCI device and then calls the block layer initialization function.
 *
 * return value
 *	0 on success else an error code.
 */
static int mtip_pci_probe(struct pci_dev *pdev,
			const struct pci_device_id *ent)
{
	int rv = 0;
	struct driver_data *dd = NULL;
	char cpu_list[256];
	const struct cpumask *node_mask;
	int cpu, i = 0, j = 0;
	int my_node = NUMA_NO_NODE;

	/* Allocate memory for this devices private data. */
	my_node = pcibus_to_node(pdev->bus);
	if (my_node != NUMA_NO_NODE) {
		if (!node_online(my_node))
			my_node = mtip_get_next_rr_node();
	} else {
		dev_info(&pdev->dev, "Kernel not reporting proximity, choosing a node\n");
		my_node = mtip_get_next_rr_node();
	}
	dev_info(&pdev->dev, "NUMA node %d (closest: %d,%d, probe on %d:%d)\n",
		my_node, pcibus_to_node(pdev->bus), dev_to_node(&pdev->dev),
		cpu_to_node(raw_smp_processor_id()), raw_smp_processor_id());

	dd = kzalloc_node(sizeof(struct driver_data), GFP_KERNEL, my_node);
	if (!dd)
		return -ENOMEM;

	/* Attach the private data to this PCI device.  */
	pci_set_drvdata(pdev, dd);

	rv = pcim_enable_device(pdev);
	if (rv < 0) {
		dev_err(&pdev->dev, "Unable to enable device\n");
		goto iomap_err;
	}

	/* Map BAR5 to memory. */
	rv = pcim_iomap_regions(pdev, 1 << MTIP_ABAR, MTIP_DRV_NAME);
	if (rv < 0) {
		dev_err(&pdev->dev, "Unable to map regions\n");
		goto iomap_err;
	}

	rv = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (rv) {
		dev_warn(&pdev->dev, "64-bit DMA enable failed\n");
		goto setmask_err;
	}

	/* Copy the info we may need later into the private data structure. */
	dd->major	= mtip_major;
	dd->instance	= instance;
	dd->pdev	= pdev;
	dd->numa_node	= my_node;

	memset(dd->workq_name, 0, 32);
	snprintf(dd->workq_name, 31, "mtipq%d", dd->instance);

	dd->isr_workq = create_workqueue(dd->workq_name);
	if (!dd->isr_workq) {
		dev_warn(&pdev->dev, "Can't create wq %d\n", dd->instance);
		rv = -ENOMEM;
		goto setmask_err;
	}

	memset(cpu_list, 0, sizeof(cpu_list));

	node_mask = cpumask_of_node(dd->numa_node);
	if (!cpumask_empty(node_mask)) {
		for_each_cpu(cpu, node_mask)
		{
			snprintf(&cpu_list[j], 256 - j, "%d ", cpu);
			j = strlen(cpu_list);
		}

		dev_info(&pdev->dev, "Node %d on package %d has %d cpu(s): %s\n",
			dd->numa_node,
			topology_physical_package_id(cpumask_first(node_mask)),
			nr_cpus_node(dd->numa_node),
			cpu_list);
	} else
		dev_dbg(&pdev->dev, "mtip32xx: node_mask empty\n");

	dd->isr_binding = get_least_used_cpu_on_node(dd->numa_node);
	dev_info(&pdev->dev, "Initial IRQ binding node:cpu %d:%d\n",
		cpu_to_node(dd->isr_binding), dd->isr_binding);

	/* first worker context always runs in ISR */
	dd->work[0].cpu_binding = dd->isr_binding;
	dd->work[1].cpu_binding = get_least_used_cpu_on_node(dd->numa_node);
	dd->work[2].cpu_binding = get_least_used_cpu_on_node(dd->numa_node);
	dd->work[3].cpu_binding = dd->work[0].cpu_binding;
	dd->work[4].cpu_binding = dd->work[1].cpu_binding;
	dd->work[5].cpu_binding = dd->work[2].cpu_binding;
	dd->work[6].cpu_binding = dd->work[2].cpu_binding;
	dd->work[7].cpu_binding = dd->work[1].cpu_binding;

	/* Log the bindings */
	for_each_present_cpu(cpu) {
		memset(cpu_list, 0, sizeof(cpu_list));
		for (i = 0, j = 0; i < MTIP_MAX_SLOT_GROUPS; i++) {
			if (dd->work[i].cpu_binding == cpu) {
				snprintf(&cpu_list[j], 256 - j, "%d ", i);
				j = strlen(cpu_list);
			}
		}
		if (j)
			dev_info(&pdev->dev, "CPU %d: WQs %s\n", cpu, cpu_list);
	}

	INIT_WORK(&dd->work[0].work, mtip_workq_sdbf0);
	INIT_WORK(&dd->work[1].work, mtip_workq_sdbf1);
	INIT_WORK(&dd->work[2].work, mtip_workq_sdbf2);
	INIT_WORK(&dd->work[3].work, mtip_workq_sdbf3);
	INIT_WORK(&dd->work[4].work, mtip_workq_sdbf4);
	INIT_WORK(&dd->work[5].work, mtip_workq_sdbf5);
	INIT_WORK(&dd->work[6].work, mtip_workq_sdbf6);
	INIT_WORK(&dd->work[7].work, mtip_workq_sdbf7);

	pci_set_master(pdev);
	rv = pci_enable_msi(pdev);
	if (rv) {
		dev_warn(&pdev->dev,
			"Unable to enable MSI interrupt.\n");
		goto msi_initialize_err;
	}

	mtip_fix_ero_nosnoop(dd, pdev);

	/* Initialize the block layer. */
	rv = mtip_block_initialize(dd);
	if (rv < 0) {
		dev_err(&pdev->dev,
			"Unable to initialize block layer\n");
		goto block_initialize_err;
	}

	/*
	 * Increment the instance count so that each device has a unique
	 * instance number.
	 */
	instance++;
	if (rv != MTIP_FTL_REBUILD_MAGIC)
		set_bit(MTIP_DDF_INIT_DONE_BIT, &dd->dd_flag);
	else
		rv = 0; /* device in rebuild state, return 0 from probe */

	goto done;

block_initialize_err:
	pci_disable_msi(pdev);

msi_initialize_err:
	if (dd->isr_workq) {
		destroy_workqueue(dd->isr_workq);
		drop_cpu(dd->work[0].cpu_binding);
		drop_cpu(dd->work[1].cpu_binding);
		drop_cpu(dd->work[2].cpu_binding);
	}
setmask_err:
	pcim_iounmap_regions(pdev, 1 << MTIP_ABAR);

iomap_err:
	kfree(dd);
	pci_set_drvdata(pdev, NULL);
	return rv;
done:
	return rv;
}

/*
 * Called for each probed device when the device is removed or the
 * driver is unloaded.
 *
 * return value
 *	None
 */
static void mtip_pci_remove(struct pci_dev *pdev)
{
	struct driver_data *dd = pci_get_drvdata(pdev);
	unsigned long to;

	mtip_check_surprise_removal(dd);
	synchronize_irq(dd->pdev->irq);

	/* Spin until workers are done */
	to = jiffies + msecs_to_jiffies(4000);
	do {
		msleep(20);
	} while (atomic_read(&dd->irq_workers_active) != 0 &&
		time_before(jiffies, to));

	if (atomic_read(&dd->irq_workers_active) != 0) {
		dev_warn(&dd->pdev->dev,
			"Completion workers still active!\n");
	}

	set_bit(MTIP_DDF_REMOVE_PENDING_BIT, &dd->dd_flag);

	if (test_bit(MTIP_DDF_INIT_DONE_BIT, &dd->dd_flag))
		del_gendisk(dd->disk);

	mtip_hw_debugfs_exit(dd);

	if (dd->mtip_svc_handler) {
		set_bit(MTIP_PF_SVC_THD_STOP_BIT, &dd->port->flags);
		wake_up_interruptible(&dd->port->svc_wait);
		kthread_stop(dd->mtip_svc_handler);
	}

	if (!dd->sr) {
		/*
		 * Explicitly wait here for IOs to quiesce,
		 * as mtip_standby_drive usually won't wait for IOs.
		 */
		if (!mtip_quiesce_io(dd->port, MTIP_QUIESCE_IO_TIMEOUT_MS))
			mtip_standby_drive(dd);
	}
	else
		dev_info(&dd->pdev->dev, "device %s surprise removal\n",
						dd->disk->disk_name);

	blk_mq_free_tag_set(&dd->tags);

	/* De-initialize the protocol layer. */
	mtip_hw_exit(dd);

	if (dd->isr_workq) {
		destroy_workqueue(dd->isr_workq);
		drop_cpu(dd->work[0].cpu_binding);
		drop_cpu(dd->work[1].cpu_binding);
		drop_cpu(dd->work[2].cpu_binding);
	}

	pci_disable_msi(pdev);

	pcim_iounmap_regions(pdev, 1 << MTIP_ABAR);
	pci_set_drvdata(pdev, NULL);

	put_disk(dd->disk);
}

/*
 * Called for each probed device when the device is suspended.
 *
 * return value
 *	0  Success
 *	<0 Error
 */
static int __maybe_unused mtip_pci_suspend(struct device *dev)
{
	int rv = 0;
	struct driver_data *dd = dev_get_drvdata(dev);

	set_bit(MTIP_DDF_RESUME_BIT, &dd->dd_flag);

	/* Disable ports & interrupts then send standby immediate */
	rv = mtip_block_suspend(dd);
	if (rv < 0)
		dev_err(dev, "Failed to suspend controller\n");

	return rv;
}

/*
 * Called for each probed device when the device is resumed.
 *
 * return value
 *      0  Success
 *      <0 Error
 */
static int __maybe_unused mtip_pci_resume(struct device *dev)
{
	int rv = 0;
	struct driver_data *dd = dev_get_drvdata(dev);

	/*
	 * Calls hbaReset, initPort, & startPort function
	 * then enables interrupts
	 */
	rv = mtip_block_resume(dd);
	if (rv < 0)
		dev_err(dev, "Unable to resume\n");

	clear_bit(MTIP_DDF_RESUME_BIT, &dd->dd_flag);

	return rv;
}

/*
 * Shutdown routine
 *
 * return value
 *      None
 */
static void mtip_pci_shutdown(struct pci_dev *pdev)
{
	struct driver_data *dd = pci_get_drvdata(pdev);
	if (dd)
		mtip_block_shutdown(dd);
}

/* Table of device ids supported by this driver. */
static const struct pci_device_id mtip_pci_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_MICRON, P320H_DEVICE_ID) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MICRON, P320M_DEVICE_ID) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MICRON, P320S_DEVICE_ID) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MICRON, P325M_DEVICE_ID) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MICRON, P420H_DEVICE_ID) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MICRON, P420M_DEVICE_ID) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MICRON, P425M_DEVICE_ID) },
	{ 0 }
};

static SIMPLE_DEV_PM_OPS(mtip_pci_pm_ops, mtip_pci_suspend, mtip_pci_resume);

/* Structure that describes the PCI driver functions. */
static struct pci_driver mtip_pci_driver = {
	.name			= MTIP_DRV_NAME,
	.id_table		= mtip_pci_tbl,
	.probe			= mtip_pci_probe,
	.remove			= mtip_pci_remove,
	.driver.pm		= &mtip_pci_pm_ops,
	.shutdown		= mtip_pci_shutdown,
};

MODULE_DEVICE_TABLE(pci, mtip_pci_tbl);

/*
 * Module initialization function.
 *
 * Called once when the module is loaded. This function allocates a major
 * block device number to the Cyclone devices and registers the PCI layer
 * of the driver.
 *
 * Return value
 *      0 on success else error code.
 */
static int __init mtip_init(void)
{
	int error;

	pr_info(MTIP_DRV_NAME " Version " MTIP_DRV_VERSION "\n");

	/* Allocate a major block device number to use with this driver. */
	error = register_blkdev(0, MTIP_DRV_NAME);
	if (error <= 0) {
		pr_err("Unable to register block device (%d)\n",
		error);
		return -EBUSY;
	}
	mtip_major = error;

	dfs_parent = debugfs_create_dir("rssd", NULL);
	if (IS_ERR_OR_NULL(dfs_parent)) {
		pr_warn("Error creating debugfs parent\n");
		dfs_parent = NULL;
	}

	/* Register our PCI operations. */
	error = pci_register_driver(&mtip_pci_driver);
	if (error) {
		debugfs_remove(dfs_parent);
		unregister_blkdev(mtip_major, MTIP_DRV_NAME);
	}

	return error;
}

/*
 * Module de-initialization function.
 *
 * Called once when the module is unloaded. This function deallocates
 * the major block device number allocated by mtip_init() and
 * unregisters the PCI layer of the driver.
 *
 * Return value
 *      none
 */
static void __exit mtip_exit(void)
{
	/* Release the allocated major block device number. */
	unregister_blkdev(mtip_major, MTIP_DRV_NAME);

	/* Unregister the PCI driver. */
	pci_unregister_driver(&mtip_pci_driver);

	debugfs_remove_recursive(dfs_parent);
}

MODULE_AUTHOR("Micron Technology, Inc");
MODULE_DESCRIPTION("Micron RealSSD PCIe Block Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(MTIP_DRV_VERSION);

module_init(mtip_init);
module_exit(mtip_exit);
