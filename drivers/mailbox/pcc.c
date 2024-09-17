// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	Copyright (C) 2014 Linaro Ltd.
 *	Author:	Ashwin Chaugule <ashwin.chaugule@linaro.org>
 *
 *  PCC (Platform Communication Channel) is defined in the ACPI 5.0+
 *  specification. It is a mailbox like mechanism to allow clients
 *  such as CPPC (Collaborative Processor Performance Control), RAS
 *  (Reliability, Availability and Serviceability) and MPST (Memory
 *  Node Power State Table) to talk to the platform (e.g. BMC) through
 *  shared memory regions as defined in the PCC table entries. The PCC
 *  specification supports a Doorbell mechanism for the PCC clients
 *  to notify the platform about new data. This Doorbell information
 *  is also specified in each PCC table entry.
 *
 *  Typical high level flow of operation is:
 *
 *  PCC Reads:
 *  * Client tries to acquire a channel lock.
 *  * After it is acquired it writes READ cmd in communication region cmd
 *		address.
 *  * Client issues mbox_send_message() which rings the PCC doorbell
 *		for its PCC channel.
 *  * If command completes, then client has control over channel and
 *		it can proceed with its reads.
 *  * Client releases lock.
 *
 *  PCC Writes:
 *  * Client tries to acquire channel lock.
 *  * Client writes to its communication region after it acquires a
 *		channel lock.
 *  * Client writes WRITE cmd in communication region cmd address.
 *  * Client issues mbox_send_message() which rings the PCC doorbell
 *		for its PCC channel.
 *  * If command completes, then writes have succeeded and it can release
 *		the channel lock.
 *
 *  There is a Nominal latency defined for each channel which indicates
 *  how long to wait until a command completes. If command is not complete
 *  the client needs to retry or assume failure.
 *
 *	For more details about PCC, please see the ACPI specification from
 *  http://www.uefi.org/ACPIv5.1 Section 14.
 *
 *  This file implements PCC as a Mailbox controller and allows for PCC
 *  clients to be implemented as its Mailbox Client Channels.
 */

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/log2.h>
#include <linux/platform_device.h>
#include <linux/mailbox_controller.h>
#include <linux/mailbox_client.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <acpi/pcc.h>

#include "mailbox.h"

#define MBOX_IRQ_NAME		"pcc-mbox"

/**
 * struct pcc_chan_reg - PCC register bundle
 *
 * @vaddr: cached virtual address for this register
 * @gas: pointer to the generic address structure for this register
 * @preserve_mask: bitmask to preserve when writing to this register
 * @set_mask: bitmask to set when writing to this register
 * @status_mask: bitmask to determine and/or update the status for this register
 */
struct pcc_chan_reg {
	void __iomem *vaddr;
	struct acpi_generic_address *gas;
	u64 preserve_mask;
	u64 set_mask;
	u64 status_mask;
};

/**
 * struct pcc_chan_info - PCC channel specific information
 *
 * @chan: PCC channel information with Shared Memory Region info
 * @db: PCC register bundle for the doorbell register
 * @plat_irq_ack: PCC register bundle for the platform interrupt acknowledge
 *	register
 * @cmd_complete: PCC register bundle for the command complete check register
 * @cmd_update: PCC register bundle for the command complete update register
 * @error: PCC register bundle for the error status register
 * @plat_irq: platform interrupt
 */
struct pcc_chan_info {
	struct pcc_mbox_chan chan;
	struct pcc_chan_reg db;
	struct pcc_chan_reg plat_irq_ack;
	struct pcc_chan_reg cmd_complete;
	struct pcc_chan_reg cmd_update;
	struct pcc_chan_reg error;
	int plat_irq;
};

#define to_pcc_chan_info(c) container_of(c, struct pcc_chan_info, chan)
static struct pcc_chan_info *chan_info;
static int pcc_chan_count;

/*
 * PCC can be used with perf critical drivers such as CPPC
 * So it makes sense to locally cache the virtual address and
 * use it to read/write to PCC registers such as doorbell register
 *
 * The below read_register and write_registers are used to read and
 * write from perf critical registers such as PCC doorbell register
 */
static void read_register(void __iomem *vaddr, u64 *val, unsigned int bit_width)
{
	switch (bit_width) {
	case 8:
		*val = readb(vaddr);
		break;
	case 16:
		*val = readw(vaddr);
		break;
	case 32:
		*val = readl(vaddr);
		break;
	case 64:
		*val = readq(vaddr);
		break;
	}
}

static void write_register(void __iomem *vaddr, u64 val, unsigned int bit_width)
{
	switch (bit_width) {
	case 8:
		writeb(val, vaddr);
		break;
	case 16:
		writew(val, vaddr);
		break;
	case 32:
		writel(val, vaddr);
		break;
	case 64:
		writeq(val, vaddr);
		break;
	}
}

static int pcc_chan_reg_read(struct pcc_chan_reg *reg, u64 *val)
{
	int ret = 0;

	if (!reg->gas) {
		*val = 0;
		return 0;
	}

	if (reg->vaddr)
		read_register(reg->vaddr, val, reg->gas->bit_width);
	else
		ret = acpi_read(val, reg->gas);

	return ret;
}

static int pcc_chan_reg_write(struct pcc_chan_reg *reg, u64 val)
{
	int ret = 0;

	if (!reg->gas)
		return 0;

	if (reg->vaddr)
		write_register(reg->vaddr, val, reg->gas->bit_width);
	else
		ret = acpi_write(val, reg->gas);

	return ret;
}

static int pcc_chan_reg_read_modify_write(struct pcc_chan_reg *reg)
{
	int ret = 0;
	u64 val;

	ret = pcc_chan_reg_read(reg, &val);
	if (ret)
		return ret;

	val &= reg->preserve_mask;
	val |= reg->set_mask;

	return pcc_chan_reg_write(reg, val);
}

/**
 * pcc_map_interrupt - Map a PCC subspace GSI to a linux IRQ number
 * @interrupt: GSI number.
 * @flags: interrupt flags
 *
 * Returns: a valid linux IRQ number on success
 *		0 or -EINVAL on failure
 */
static int pcc_map_interrupt(u32 interrupt, u32 flags)
{
	int trigger, polarity;

	if (!interrupt)
		return 0;

	trigger = (flags & ACPI_PCCT_INTERRUPT_MODE) ? ACPI_EDGE_SENSITIVE
			: ACPI_LEVEL_SENSITIVE;

	polarity = (flags & ACPI_PCCT_INTERRUPT_POLARITY) ? ACPI_ACTIVE_LOW
			: ACPI_ACTIVE_HIGH;

	return acpi_register_gsi(NULL, interrupt, trigger, polarity);
}

/**
 * pcc_mbox_irq - PCC mailbox interrupt handler
 * @irq:	interrupt number
 * @p: data/cookie passed from the caller to identify the channel
 *
 * Returns: IRQ_HANDLED if interrupt is handled or IRQ_NONE if not
 */
static irqreturn_t pcc_mbox_irq(int irq, void *p)
{
	struct pcc_chan_info *pchan;
	struct mbox_chan *chan = p;
	u64 val;
	int ret;

	pchan = chan->con_priv;

	ret = pcc_chan_reg_read(&pchan->cmd_complete, &val);
	if (ret)
		return IRQ_NONE;

	if (val) { /* Ensure GAS exists and value is non-zero */
		val &= pchan->cmd_complete.status_mask;
		if (!val)
			return IRQ_NONE;
	}

	ret = pcc_chan_reg_read(&pchan->error, &val);
	if (ret)
		return IRQ_NONE;
	val &= pchan->error.status_mask;
	if (val) {
		val &= ~pchan->error.status_mask;
		pcc_chan_reg_write(&pchan->error, val);
		return IRQ_NONE;
	}

	if (pcc_chan_reg_read_modify_write(&pchan->plat_irq_ack))
		return IRQ_NONE;

	mbox_chan_received_data(chan, NULL);

	return IRQ_HANDLED;
}

/**
 * pcc_mbox_request_channel - PCC clients call this function to
 *		request a pointer to their PCC subspace, from which they
 *		can get the details of communicating with the remote.
 * @cl: Pointer to Mailbox client, so we know where to bind the
 *		Channel.
 * @subspace_id: The PCC Subspace index as parsed in the PCC client
 *		ACPI package. This is used to lookup the array of PCC
 *		subspaces as parsed by the PCC Mailbox controller.
 *
 * Return: Pointer to the PCC Mailbox Channel if successful or ERR_PTR.
 */
struct pcc_mbox_chan *
pcc_mbox_request_channel(struct mbox_client *cl, int subspace_id)
{
	struct pcc_chan_info *pchan;
	struct mbox_chan *chan;
	struct device *dev;
	unsigned long flags;

	if (subspace_id < 0 || subspace_id >= pcc_chan_count)
		return ERR_PTR(-ENOENT);

	pchan = chan_info + subspace_id;
	chan = pchan->chan.mchan;
	if (IS_ERR(chan) || chan->cl) {
		pr_err("Channel not found for idx: %d\n", subspace_id);
		return ERR_PTR(-EBUSY);
	}
	dev = chan->mbox->dev;

	spin_lock_irqsave(&chan->lock, flags);
	chan->msg_free = 0;
	chan->msg_count = 0;
	chan->active_req = NULL;
	chan->cl = cl;
	init_completion(&chan->tx_complete);

	if (chan->txdone_method == TXDONE_BY_POLL && cl->knows_txdone)
		chan->txdone_method = TXDONE_BY_ACK;

	spin_unlock_irqrestore(&chan->lock, flags);

	if (pchan->plat_irq > 0) {
		int rc;

		rc = devm_request_irq(dev, pchan->plat_irq, pcc_mbox_irq, 0,
				      MBOX_IRQ_NAME, chan);
		if (unlikely(rc)) {
			dev_err(dev, "failed to register PCC interrupt %d\n",
				pchan->plat_irq);
			pcc_mbox_free_channel(&pchan->chan);
			return ERR_PTR(rc);
		}
	}

	return &pchan->chan;
}
EXPORT_SYMBOL_GPL(pcc_mbox_request_channel);

/**
 * pcc_mbox_free_channel - Clients call this to free their Channel.
 *
 * @pchan: Pointer to the PCC mailbox channel as returned by
 *	   pcc_mbox_request_channel()
 */
void pcc_mbox_free_channel(struct pcc_mbox_chan *pchan)
{
	struct pcc_chan_info *pchan_info = to_pcc_chan_info(pchan);
	struct mbox_chan *chan = pchan->mchan;
	unsigned long flags;

	if (!chan || !chan->cl)
		return;

	if (pchan_info->plat_irq > 0)
		devm_free_irq(chan->mbox->dev, pchan_info->plat_irq, chan);

	spin_lock_irqsave(&chan->lock, flags);
	chan->cl = NULL;
	chan->active_req = NULL;
	if (chan->txdone_method == TXDONE_BY_ACK)
		chan->txdone_method = TXDONE_BY_POLL;

	spin_unlock_irqrestore(&chan->lock, flags);
}
EXPORT_SYMBOL_GPL(pcc_mbox_free_channel);

/**
 * pcc_send_data - Called from Mailbox Controller code. Used
 *		here only to ring the channel doorbell. The PCC client
 *		specific read/write is done in the client driver in
 *		order to maintain atomicity over PCC channel once
 *		OS has control over it. See above for flow of operations.
 * @chan: Pointer to Mailbox channel over which to send data.
 * @data: Client specific data written over channel. Used here
 *		only for debug after PCC transaction completes.
 *
 * Return: Err if something failed else 0 for success.
 */
static int pcc_send_data(struct mbox_chan *chan, void *data)
{
	int ret;
	struct pcc_chan_info *pchan = chan->con_priv;

	ret = pcc_chan_reg_read_modify_write(&pchan->cmd_update);
	if (ret)
		return ret;

	return pcc_chan_reg_read_modify_write(&pchan->db);
}

static const struct mbox_chan_ops pcc_chan_ops = {
	.send_data = pcc_send_data,
};

/**
 * parse_pcc_subspace - Count PCC subspaces defined
 * @header: Pointer to the ACPI subtable header under the PCCT.
 * @end: End of subtable entry.
 *
 * Return: If we find a PCC subspace entry of a valid type, return 0.
 *	Otherwise, return -EINVAL.
 *
 * This gets called for each entry in the PCC table.
 */
static int parse_pcc_subspace(union acpi_subtable_headers *header,
		const unsigned long end)
{
	struct acpi_pcct_subspace *ss = (struct acpi_pcct_subspace *) header;

	if (ss->header.type < ACPI_PCCT_TYPE_RESERVED)
		return 0;

	return -EINVAL;
}

static int
pcc_chan_reg_init(struct pcc_chan_reg *reg, struct acpi_generic_address *gas,
		  u64 preserve_mask, u64 set_mask, u64 status_mask, char *name)
{
	if (gas->space_id == ACPI_ADR_SPACE_SYSTEM_MEMORY) {
		if (!(gas->bit_width >= 8 && gas->bit_width <= 64 &&
		      is_power_of_2(gas->bit_width))) {
			pr_err("Error: Cannot access register of %u bit width",
			       gas->bit_width);
			return -EFAULT;
		}

		reg->vaddr = acpi_os_ioremap(gas->address, gas->bit_width / 8);
		if (!reg->vaddr) {
			pr_err("Failed to ioremap PCC %s register\n", name);
			return -ENOMEM;
		}
	}
	reg->gas = gas;
	reg->preserve_mask = preserve_mask;
	reg->set_mask = set_mask;
	reg->status_mask = status_mask;
	return 0;
}

/**
 * pcc_parse_subspace_irq - Parse the PCC IRQ and PCC ACK register
 *
 * @pchan: Pointer to the PCC channel info structure.
 * @pcct_entry: Pointer to the ACPI subtable header.
 *
 * Return: 0 for Success, else errno.
 *
 * There should be one entry per PCC channel. This gets called for each
 * entry in the PCC table. This uses PCCY Type1 structure for all applicable
 * types(Type 1-4) to fetch irq
 */
static int pcc_parse_subspace_irq(struct pcc_chan_info *pchan,
				  struct acpi_subtable_header *pcct_entry)
{
	int ret = 0;
	struct acpi_pcct_hw_reduced *pcct_ss;

	if (pcct_entry->type < ACPI_PCCT_TYPE_HW_REDUCED_SUBSPACE ||
	    pcct_entry->type > ACPI_PCCT_TYPE_EXT_PCC_SLAVE_SUBSPACE)
		return 0;

	pcct_ss = (struct acpi_pcct_hw_reduced *)pcct_entry;
	pchan->plat_irq = pcc_map_interrupt(pcct_ss->platform_interrupt,
					    (u32)pcct_ss->flags);
	if (pchan->plat_irq <= 0) {
		pr_err("PCC GSI %d not registered\n",
		       pcct_ss->platform_interrupt);
		return -EINVAL;
	}

	if (pcct_ss->header.type == ACPI_PCCT_TYPE_HW_REDUCED_SUBSPACE_TYPE2) {
		struct acpi_pcct_hw_reduced_type2 *pcct2_ss = (void *)pcct_ss;

		ret = pcc_chan_reg_init(&pchan->plat_irq_ack,
					&pcct2_ss->platform_ack_register,
					pcct2_ss->ack_preserve_mask,
					pcct2_ss->ack_write_mask, 0,
					"PLAT IRQ ACK");

	} else if (pcct_ss->header.type == ACPI_PCCT_TYPE_EXT_PCC_MASTER_SUBSPACE ||
		   pcct_ss->header.type == ACPI_PCCT_TYPE_EXT_PCC_SLAVE_SUBSPACE) {
		struct acpi_pcct_ext_pcc_master *pcct_ext = (void *)pcct_ss;

		ret = pcc_chan_reg_init(&pchan->plat_irq_ack,
					&pcct_ext->platform_ack_register,
					pcct_ext->ack_preserve_mask,
					pcct_ext->ack_set_mask, 0,
					"PLAT IRQ ACK");
	}

	return ret;
}

/**
 * pcc_parse_subspace_db_reg - Parse the PCC doorbell register
 *
 * @pchan: Pointer to the PCC channel info structure.
 * @pcct_entry: Pointer to the ACPI subtable header.
 *
 * Return: 0 for Success, else errno.
 */
static int pcc_parse_subspace_db_reg(struct pcc_chan_info *pchan,
				     struct acpi_subtable_header *pcct_entry)
{
	int ret = 0;

	if (pcct_entry->type <= ACPI_PCCT_TYPE_HW_REDUCED_SUBSPACE_TYPE2) {
		struct acpi_pcct_subspace *pcct_ss;

		pcct_ss = (struct acpi_pcct_subspace *)pcct_entry;

		ret = pcc_chan_reg_init(&pchan->db,
					&pcct_ss->doorbell_register,
					pcct_ss->preserve_mask,
					pcct_ss->write_mask, 0,	"Doorbell");

	} else {
		struct acpi_pcct_ext_pcc_master *pcct_ext;

		pcct_ext = (struct acpi_pcct_ext_pcc_master *)pcct_entry;

		ret = pcc_chan_reg_init(&pchan->db,
					&pcct_ext->doorbell_register,
					pcct_ext->preserve_mask,
					pcct_ext->write_mask, 0, "Doorbell");
		if (ret)
			return ret;

		ret = pcc_chan_reg_init(&pchan->cmd_complete,
					&pcct_ext->cmd_complete_register,
					0, 0, pcct_ext->cmd_complete_mask,
					"Command Complete Check");
		if (ret)
			return ret;

		ret = pcc_chan_reg_init(&pchan->cmd_update,
					&pcct_ext->cmd_update_register,
					pcct_ext->cmd_update_preserve_mask,
					pcct_ext->cmd_update_set_mask, 0,
					"Command Complete Update");
		if (ret)
			return ret;

		ret = pcc_chan_reg_init(&pchan->error,
					&pcct_ext->error_status_register,
					0, 0, pcct_ext->error_status_mask,
					"Error Status");
	}
	return ret;
}

/**
 * pcc_parse_subspace_shmem - Parse the PCC Shared Memory Region information
 *
 * @pchan: Pointer to the PCC channel info structure.
 * @pcct_entry: Pointer to the ACPI subtable header.
 *
 */
static void pcc_parse_subspace_shmem(struct pcc_chan_info *pchan,
				     struct acpi_subtable_header *pcct_entry)
{
	if (pcct_entry->type <= ACPI_PCCT_TYPE_HW_REDUCED_SUBSPACE_TYPE2) {
		struct acpi_pcct_subspace *pcct_ss =
			(struct acpi_pcct_subspace *)pcct_entry;

		pchan->chan.shmem_base_addr = pcct_ss->base_address;
		pchan->chan.shmem_size = pcct_ss->length;
		pchan->chan.latency = pcct_ss->latency;
		pchan->chan.max_access_rate = pcct_ss->max_access_rate;
		pchan->chan.min_turnaround_time = pcct_ss->min_turnaround_time;
	} else {
		struct acpi_pcct_ext_pcc_master *pcct_ext =
			(struct acpi_pcct_ext_pcc_master *)pcct_entry;

		pchan->chan.shmem_base_addr = pcct_ext->base_address;
		pchan->chan.shmem_size = pcct_ext->length;
		pchan->chan.latency = pcct_ext->latency;
		pchan->chan.max_access_rate = pcct_ext->max_access_rate;
		pchan->chan.min_turnaround_time = pcct_ext->min_turnaround_time;
	}
}

/**
 * acpi_pcc_probe - Parse the ACPI tree for the PCCT.
 *
 * Return: 0 for Success, else errno.
 */
static int __init acpi_pcc_probe(void)
{
	int count, i, rc = 0;
	acpi_status status;
	struct acpi_table_header *pcct_tbl;
	struct acpi_subtable_proc proc[ACPI_PCCT_TYPE_RESERVED];

	status = acpi_get_table(ACPI_SIG_PCCT, 0, &pcct_tbl);
	if (ACPI_FAILURE(status) || !pcct_tbl)
		return -ENODEV;

	/* Set up the subtable handlers */
	for (i = ACPI_PCCT_TYPE_GENERIC_SUBSPACE;
	     i < ACPI_PCCT_TYPE_RESERVED; i++) {
		proc[i].id = i;
		proc[i].count = 0;
		proc[i].handler = parse_pcc_subspace;
	}

	count = acpi_table_parse_entries_array(ACPI_SIG_PCCT,
			sizeof(struct acpi_table_pcct), proc,
			ACPI_PCCT_TYPE_RESERVED, MAX_PCC_SUBSPACES);
	if (count <= 0 || count > MAX_PCC_SUBSPACES) {
		if (count < 0)
			pr_warn("Error parsing PCC subspaces from PCCT\n");
		else
			pr_warn("Invalid PCCT: %d PCC subspaces\n", count);

		rc = -EINVAL;
	} else {
		pcc_chan_count = count;
	}

	acpi_put_table(pcct_tbl);

	return rc;
}

/**
 * pcc_mbox_probe - Called when we find a match for the
 *	PCCT platform device. This is purely used to represent
 *	the PCCT as a virtual device for registering with the
 *	generic Mailbox framework.
 *
 * @pdev: Pointer to platform device returned when a match
 *	is found.
 *
 *	Return: 0 for Success, else errno.
 */
static int pcc_mbox_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mbox_controller *pcc_mbox_ctrl;
	struct mbox_chan *pcc_mbox_channels;
	struct acpi_table_header *pcct_tbl;
	struct acpi_subtable_header *pcct_entry;
	struct acpi_table_pcct *acpi_pcct_tbl;
	acpi_status status = AE_OK;
	int i, rc, count = pcc_chan_count;

	/* Search for PCCT */
	status = acpi_get_table(ACPI_SIG_PCCT, 0, &pcct_tbl);

	if (ACPI_FAILURE(status) || !pcct_tbl)
		return -ENODEV;

	pcc_mbox_channels = devm_kcalloc(dev, count, sizeof(*pcc_mbox_channels),
					 GFP_KERNEL);
	if (!pcc_mbox_channels) {
		rc = -ENOMEM;
		goto err;
	}

	chan_info = devm_kcalloc(dev, count, sizeof(*chan_info), GFP_KERNEL);
	if (!chan_info) {
		rc = -ENOMEM;
		goto err;
	}

	pcc_mbox_ctrl = devm_kzalloc(dev, sizeof(*pcc_mbox_ctrl), GFP_KERNEL);
	if (!pcc_mbox_ctrl) {
		rc = -ENOMEM;
		goto err;
	}

	/* Point to the first PCC subspace entry */
	pcct_entry = (struct acpi_subtable_header *) (
		(unsigned long) pcct_tbl + sizeof(struct acpi_table_pcct));

	acpi_pcct_tbl = (struct acpi_table_pcct *) pcct_tbl;
	if (acpi_pcct_tbl->flags & ACPI_PCCT_DOORBELL)
		pcc_mbox_ctrl->txdone_irq = true;

	for (i = 0; i < count; i++) {
		struct pcc_chan_info *pchan = chan_info + i;

		pcc_mbox_channels[i].con_priv = pchan;
		pchan->chan.mchan = &pcc_mbox_channels[i];

		if (pcct_entry->type == ACPI_PCCT_TYPE_EXT_PCC_SLAVE_SUBSPACE &&
		    !pcc_mbox_ctrl->txdone_irq) {
			pr_err("Platform Interrupt flag must be set to 1");
			rc = -EINVAL;
			goto err;
		}

		if (pcc_mbox_ctrl->txdone_irq) {
			rc = pcc_parse_subspace_irq(pchan, pcct_entry);
			if (rc < 0)
				goto err;
		}
		rc = pcc_parse_subspace_db_reg(pchan, pcct_entry);
		if (rc < 0)
			goto err;

		pcc_parse_subspace_shmem(pchan, pcct_entry);

		pcct_entry = (struct acpi_subtable_header *)
			((unsigned long) pcct_entry + pcct_entry->length);
	}

	pcc_mbox_ctrl->num_chans = count;

	pr_info("Detected %d PCC Subspaces\n", pcc_mbox_ctrl->num_chans);

	pcc_mbox_ctrl->chans = pcc_mbox_channels;
	pcc_mbox_ctrl->ops = &pcc_chan_ops;
	pcc_mbox_ctrl->dev = dev;

	pr_info("Registering PCC driver as Mailbox controller\n");
	rc = mbox_controller_register(pcc_mbox_ctrl);
	if (rc)
		pr_err("Err registering PCC as Mailbox controller: %d\n", rc);
	else
		return 0;
err:
	acpi_put_table(pcct_tbl);
	return rc;
}

static struct platform_driver pcc_mbox_driver = {
	.probe = pcc_mbox_probe,
	.driver = {
		.name = "PCCT",
	},
};

static int __init pcc_init(void)
{
	int ret;
	struct platform_device *pcc_pdev;

	if (acpi_disabled)
		return -ENODEV;

	/* Check if PCC support is available. */
	ret = acpi_pcc_probe();

	if (ret) {
		pr_debug("ACPI PCC probe failed.\n");
		return -ENODEV;
	}

	pcc_pdev = platform_create_bundle(&pcc_mbox_driver,
			pcc_mbox_probe, NULL, 0, NULL, 0);

	if (IS_ERR(pcc_pdev)) {
		pr_debug("Err creating PCC platform bundle\n");
		pcc_chan_count = 0;
		return PTR_ERR(pcc_pdev);
	}

	return 0;
}

/*
 * Make PCC init postcore so that users of this mailbox
 * such as the ACPI Processor driver have it available
 * at their init.
 */
postcore_initcall(pcc_init);
