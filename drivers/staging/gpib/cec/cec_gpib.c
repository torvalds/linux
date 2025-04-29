// SPDX-License-Identifier: GPL-2.0

/***************************************************************************
 *   copyright            : (C) 2002 by Frank Mori Hess
 ***************************************************************************/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#define dev_fmt pr_fmt
#define DRV_NAME KBUILD_MODNAME

#include "cec.h"
#include <linux/pci.h>
#include <linux/io.h>
#include <linux/bitops.h>
#include <asm/dma.h>
#include <linux/module.h>
#include <linux/slab.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("GPIB driver for CEC PCI and PCMCIA boards");

/*
 * GPIB interrupt service routines
 */

static irqreturn_t cec_interrupt(int irq, void *arg)
{
	struct gpib_board *board = arg;
	struct cec_priv *priv = board->private_data;
	unsigned long flags;
	irqreturn_t retval;

	spin_lock_irqsave(&board->spinlock, flags);
	retval = nec7210_interrupt(board, &priv->nec7210_priv);
	spin_unlock_irqrestore(&board->spinlock, flags);
	return retval;
}

#define CEC_VENDOR_ID 0x12fc
#define CEC_DEV_ID    0x5cec
#define CEC_SUBID 0x9050

static int cec_pci_attach(struct gpib_board *board, const gpib_board_config_t *config);

static void cec_pci_detach(struct gpib_board *board);

// wrappers for interface functions
static int cec_read(struct gpib_board *board, uint8_t *buffer, size_t length, int *end,
		    size_t *bytes_read)
{
	struct cec_priv *priv = board->private_data;

	return nec7210_read(board, &priv->nec7210_priv, buffer, length, end, bytes_read);
}

static int cec_write(struct gpib_board *board, uint8_t *buffer, size_t length, int send_eoi,
		     size_t *bytes_written)
{
	struct cec_priv *priv = board->private_data;

	return nec7210_write(board, &priv->nec7210_priv, buffer, length, send_eoi, bytes_written);
}

static int cec_command(struct gpib_board *board, uint8_t *buffer,
		       size_t length, size_t *bytes_written)
{
	struct cec_priv *priv = board->private_data;

	return nec7210_command(board, &priv->nec7210_priv, buffer, length, bytes_written);
}

static int cec_take_control(struct gpib_board *board, int synchronous)
{
	struct cec_priv *priv = board->private_data;

	return nec7210_take_control(board, &priv->nec7210_priv, synchronous);
}

static int cec_go_to_standby(struct gpib_board *board)
{
	struct cec_priv *priv = board->private_data;

	return nec7210_go_to_standby(board, &priv->nec7210_priv);
}

static void cec_request_system_control(struct gpib_board *board, int request_control)
{
	struct cec_priv *priv = board->private_data;

	nec7210_request_system_control(board, &priv->nec7210_priv, request_control);
}

static void cec_interface_clear(struct gpib_board *board, int assert)
{
	struct cec_priv *priv = board->private_data;

	nec7210_interface_clear(board, &priv->nec7210_priv, assert);
}

static void cec_remote_enable(struct gpib_board *board, int enable)
{
	struct cec_priv *priv = board->private_data;

	nec7210_remote_enable(board, &priv->nec7210_priv, enable);
}

static int cec_enable_eos(struct gpib_board *board, uint8_t eos_byte, int compare_8_bits)
{
	struct cec_priv *priv = board->private_data;

	return nec7210_enable_eos(board, &priv->nec7210_priv, eos_byte, compare_8_bits);
}

static void cec_disable_eos(struct gpib_board *board)
{
	struct cec_priv *priv = board->private_data;

	nec7210_disable_eos(board, &priv->nec7210_priv);
}

static unsigned int cec_update_status(struct gpib_board *board, unsigned int clear_mask)
{
	struct cec_priv *priv = board->private_data;

	return nec7210_update_status(board, &priv->nec7210_priv, clear_mask);
}

static int cec_primary_address(struct gpib_board *board, unsigned int address)
{
	struct cec_priv *priv = board->private_data;

	return nec7210_primary_address(board, &priv->nec7210_priv, address);
}

static int cec_secondary_address(struct gpib_board *board, unsigned int address, int enable)
{
	struct cec_priv *priv = board->private_data;

	return nec7210_secondary_address(board, &priv->nec7210_priv, address, enable);
}

static int cec_parallel_poll(struct gpib_board *board, uint8_t *result)
{
	struct cec_priv *priv = board->private_data;

	return nec7210_parallel_poll(board, &priv->nec7210_priv, result);
}

static void cec_parallel_poll_configure(struct gpib_board *board, uint8_t config)
{
	struct cec_priv *priv = board->private_data;

	nec7210_parallel_poll_configure(board, &priv->nec7210_priv, config);
}

static void cec_parallel_poll_response(struct gpib_board *board, int ist)
{
	struct cec_priv *priv = board->private_data;

	nec7210_parallel_poll_response(board, &priv->nec7210_priv, ist);
}

static void cec_serial_poll_response(struct gpib_board *board, uint8_t status)
{
	struct cec_priv *priv = board->private_data;

	nec7210_serial_poll_response(board, &priv->nec7210_priv, status);
}

static uint8_t cec_serial_poll_status(struct gpib_board *board)
{
	struct cec_priv *priv = board->private_data;

	return nec7210_serial_poll_status(board, &priv->nec7210_priv);
}

static int cec_t1_delay(struct gpib_board *board, unsigned int nano_sec)
{
	struct cec_priv *priv = board->private_data;

	return nec7210_t1_delay(board, &priv->nec7210_priv, nano_sec);
}

static void cec_return_to_local(struct gpib_board *board)
{
	struct cec_priv *priv = board->private_data;

	nec7210_return_to_local(board, &priv->nec7210_priv);
}

static gpib_interface_t cec_pci_interface = {
	.name = "cec_pci",
	.attach = cec_pci_attach,
	.detach = cec_pci_detach,
	.read = cec_read,
	.write = cec_write,
	.command = cec_command,
	.take_control = cec_take_control,
	.go_to_standby = cec_go_to_standby,
	.request_system_control = cec_request_system_control,
	.interface_clear = cec_interface_clear,
	.remote_enable = cec_remote_enable,
	.enable_eos = cec_enable_eos,
	.disable_eos = cec_disable_eos,
	.parallel_poll = cec_parallel_poll,
	.parallel_poll_configure = cec_parallel_poll_configure,
	.parallel_poll_response = cec_parallel_poll_response,
	.local_parallel_poll_mode = NULL, // XXX
	.line_status = NULL,	//XXX
	.update_status = cec_update_status,
	.primary_address = cec_primary_address,
	.secondary_address = cec_secondary_address,
	.serial_poll_response = cec_serial_poll_response,
	.serial_poll_status = cec_serial_poll_status,
	.t1_delay = cec_t1_delay,
	.return_to_local = cec_return_to_local,
};

static int cec_allocate_private(struct gpib_board *board)
{
	struct cec_priv *priv;

	board->private_data = kmalloc(sizeof(struct cec_priv), GFP_KERNEL);
	if (!board->private_data)
		return -1;
	priv = board->private_data;
	memset(priv, 0, sizeof(struct cec_priv));
	init_nec7210_private(&priv->nec7210_priv);
	return 0;
}

static void cec_free_private(struct gpib_board *board)
{
	kfree(board->private_data);
	board->private_data = NULL;
}

static int cec_generic_attach(struct gpib_board *board)
{
	struct cec_priv *cec_priv;
	struct nec7210_priv *nec_priv;

	board->status = 0;

	if (cec_allocate_private(board))
		return -ENOMEM;
	cec_priv = board->private_data;
	nec_priv = &cec_priv->nec7210_priv;
	nec_priv->read_byte = nec7210_ioport_read_byte;
	nec_priv->write_byte = nec7210_ioport_write_byte;
	nec_priv->offset = cec_reg_offset;
	nec_priv->type = NEC7210;	// guess
	return 0;
}

static void cec_init(struct cec_priv *cec_priv, const struct gpib_board *board)
{
	struct nec7210_priv *nec_priv = &cec_priv->nec7210_priv;

	nec7210_board_reset(nec_priv, board);

	/* set internal counter register for 8 MHz input clock */
	write_byte(nec_priv, ICR | 8, AUXMR);

	nec7210_board_online(nec_priv, board);
}

static int cec_pci_attach(struct gpib_board *board, const gpib_board_config_t *config)
{
	struct cec_priv *cec_priv;
	struct nec7210_priv *nec_priv;
	int isr_flags = 0;
	int retval;

	retval = cec_generic_attach(board);
	if (retval)
		return retval;

	cec_priv = board->private_data;
	nec_priv = &cec_priv->nec7210_priv;

	// find board
	cec_priv->pci_device = NULL;
	while ((cec_priv->pci_device =
		gpib_pci_get_device(config, CEC_VENDOR_ID,
				    CEC_DEV_ID, cec_priv->pci_device)))	{
		// check for board with plx9050 controller
		if (cec_priv->pci_device->subsystem_device == CEC_SUBID)
			break;
	}
	if (!cec_priv->pci_device) {
		dev_err(board->gpib_dev, "no cec PCI board found\n");
		return -ENODEV;
	}

	if (pci_enable_device(cec_priv->pci_device)) {
		dev_err(board->gpib_dev, "error enabling pci device\n");
		return -EIO;
	}

	if (pci_request_regions(cec_priv->pci_device, "cec-gpib"))
		return -EBUSY;

	cec_priv->plx_iobase = pci_resource_start(cec_priv->pci_device, 1);
		nec_priv->iobase = pci_resource_start(cec_priv->pci_device, 3);

	isr_flags |= IRQF_SHARED;
	if (request_irq(cec_priv->pci_device->irq, cec_interrupt, isr_flags, DRV_NAME, board)) {
		dev_err(board->gpib_dev, "failed to obtain IRQ %d\n", cec_priv->pci_device->irq);
		return -EBUSY;
	}
	cec_priv->irq = cec_priv->pci_device->irq;
	if (gpib_request_pseudo_irq(board, cec_interrupt)) {
		dev_err(board->gpib_dev, "failed to allocate pseudo irq\n");
		return -1;
	}
	cec_init(cec_priv, board);

	// enable interrupts on plx chip
	outl(PLX9050_LINTR1_EN_BIT | PLX9050_LINTR1_POLARITY_BIT | PLX9050_PCI_INTR_EN_BIT,
	     cec_priv->plx_iobase + PLX9050_INTCSR_REG);

	return 0;
}

static void cec_pci_detach(struct gpib_board *board)
{
	struct cec_priv *cec_priv = board->private_data;
	struct nec7210_priv *nec_priv;

	if (cec_priv) {
		nec_priv = &cec_priv->nec7210_priv;
		gpib_free_pseudo_irq(board);
		if (cec_priv->irq) {
			// disable plx9050 interrupts
			outl(0, cec_priv->plx_iobase + PLX9050_INTCSR_REG);
			free_irq(cec_priv->irq, board);
		}
		if (nec_priv->iobase) {
			nec7210_board_reset(nec_priv, board);
			pci_release_regions(cec_priv->pci_device);
		}
		if (cec_priv->pci_device)
			pci_dev_put(cec_priv->pci_device);
	}
	cec_free_private(board);
}

static int cec_pci_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	return 0;
}

static const struct pci_device_id cec_pci_table[] = {
	{CEC_VENDOR_ID, CEC_DEV_ID, PCI_ANY_ID, CEC_SUBID, 0, 0, 0 },
	{0}
};
MODULE_DEVICE_TABLE(pci, cec_pci_table);

static struct pci_driver cec_pci_driver = {
	.name = DRV_NAME,
	.id_table = cec_pci_table,
	.probe = &cec_pci_probe
};

static int __init cec_init_module(void)
{
	int result;

	result = pci_register_driver(&cec_pci_driver);
	if (result) {
		pr_err("pci_register_driver failed: error = %d\n", result);
		return result;
	}

	result = gpib_register_driver(&cec_pci_interface, THIS_MODULE);
	if (result) {
		pr_err("gpib_register_driver failed: error = %d\n", result);
		return result;
	}

	return 0;
}

static void cec_exit_module(void)
{
	gpib_unregister_driver(&cec_pci_interface);

	pci_unregister_driver(&cec_pci_driver);
}

module_init(cec_init_module);
module_exit(cec_exit_module);
